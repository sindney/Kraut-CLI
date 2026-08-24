#include "PCH.h"

#include "qtMainWindow.moc.h"
#include <KrautEditorBasics/Plugin.h>
#include <KrautEditorBasics/RenderAPI.h>
#include <KrautGraphics/Communication/GlobalEvent.h>
#include <KrautGraphics/Configuration/CVar.h>
#include <KrautGraphics/Configuration/Startup.h>
#include <KrautGraphics/Configuration/VariableRegistry.h>
#include <KrautGraphics/FileSystem/FileIn.h>
#include <KrautGraphics/Framebuffer/Main.h>
#include <KrautGraphics/Glew/glew.h>
#include <KrautGraphics/Logging/ConsoleWriter.h>
#include <KrautGraphics/Logging/VisualStudioWriter.h>
#include <KrautGraphics/Plugin/PluginManager.h>
#include <KrautGraphics/RenderAPI/Main.h>
#include <KrautGraphics/ShaderManager/Main.h>
#include <KrautGraphics/Time/Time.h>
#include <TreePlugin/Basics/Globals.h>
#include <TreePlugin/Basics/Plugin.h>
#include <TreePlugin/Tree/Tree.h>
#include <QApplication>
#include <QImage>
#include <shellapi.h>
#include <windows.h>

using namespace AE_NS_EDITORBASICS;

aeCVarString CVarMainPlugin("app_MainPlugin", "ezTreePlugin", aeCVarFlags::Restart | aeCVarFlags::Save, "Which Renderer-DLL to use.");

// Returns the directory in which the exe is located
static aeString GetBinaryDirectory(void)
{
  char szTemp[512] = "";
  GetModuleFileName(nullptr, szTemp, 512);

  return (aePathFunctions::GetFileDirectory(szTemp));
}

void StartupMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  // store the HINSTANCE globally
  aeVariableRegistry::StoreRaw("system/windows/hinstance", &hInstance, sizeof(HINSTANCE));

  // send the event that we are now starting up
  AE_BROADCAST_EVENT(aeStartup_main_begin);

  aeString temp = aePathFunctions::GetParentDirectory(GetBinaryDirectory());

  // setup the file system
  //aeFileSystem::AddDataDirectory (GetBinaryDirectory ().c_str (), "BIN", false);
  aeFileSystem::AddDataDirectory((temp + "../../Data/aeEditor").c_str(), "", false);
  aeFileSystem::AddDataDirectory((temp + "../../Data/aeEditor").c_str(), "APP", true);
  aeFileSystem::AddDataDirectory((temp + "../../Data/Content").c_str(), "", true);

  // setup the logging system
  aeLog::RegisterEventReceiver(aeLog_ConsoleWriter::LogMessageHandler, nullptr);
  aeLog::RegisterEventReceiver(aeLog_VisualStudioWriter::LogMessageHandler, nullptr);

  // write some info
  aeLog::Log("Directory: \"%s\"", GetBinaryDirectory().c_str());

  // Load the CVars
  if (!aeCVar::LoadCVars("<APP>CVars.cfg"))
    aeLog::Warning("Could not load CVars.");

  aePluginManager::LoadPlugin(CVarMainPlugin.GetValue(), "");

  // Load the CVars again, after we have initialized the plugins
  if (!aeCVar::LoadCVars("<APP>CVars.cfg"))
    aeLog::Warning("Could not load CVars.");

  AE_BROADCAST_EVENT(aeStartup_main_end);

  // Startup all core modules
  aeStartup::StartupCore();
}

void ShutdownMain()
{
  AE_BROADCAST_EVENT(aeShutdown_main_begin);

  // Save all CVars
  if (!aeCVar::SaveCVars("<APP>CVars.cfg"))
    aeLog::Warning("Could not save CVars.");

  // Shutdown all core modules
  aeStartup::ShutdownCore();

  AE_BROADCAST_EVENT(aeShutdown_main_end);
}

void LoadStylesheet(QApplication& app)
{
  aeFileIn File;
  if (File.Open("App.stylesheet"))
  {
    aeString sSS;
    aeString sLine;

    while (!File.IsEndOfStream())
    {
      sLine.clear();
      File.ReadLine(sLine);
      sSS += sLine;
    }

    app.setStyle("plastique");

    aeEditorPlugin::s_Stylesheet = sSS;
    app.setStyleSheet(sSS.c_str());
  }
}

// ---------------------------------------------------------------------------
// Headless screenshot mode
//
// aeEditor.exe --screenshot <in.tree> <out.png> [--seed N] [--width W] [--height H]
//
// Renders a tree with the full official renderer (textures, materials, AO, LODs)
// into a hidden GL window and writes a PNG, then exits. Used by agents to visually
// verify generated trees without opening the GUI.
// ---------------------------------------------------------------------------

static HWND CreateHiddenGLWindow(HINSTANCE hInstance)
{
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = hInstance;
  wc.lpszClassName = "KrautScreenshotWnd";
  RegisterClassA(&wc);

  return CreateWindowA(wc.lpszClassName, "KrautScreenshot", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, hInstance, nullptr);
}

// The engine keeps its glew symbols private to KrautGraphics.dll, so resolve the few
// FBO functions we need directly (GL 1.1 functions come from opengl32.lib as usual).
#define KRAUT_GL_PROC(name) name = (decltype(name))wglGetProcAddress(#name)
static void (APIENTRY* pfn_glGenFramebuffers)(GLsizei, GLuint*);
static void (APIENTRY* pfn_glBindFramebuffer)(GLenum, GLuint);
static void (APIENTRY* pfn_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static void (APIENTRY* pfn_glGenRenderbuffers)(GLsizei, GLuint*);
static void (APIENTRY* pfn_glBindRenderbuffer)(GLenum, GLuint);
static void (APIENTRY* pfn_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
static void (APIENTRY* pfn_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
static GLenum (APIENTRY* pfn_glCheckFramebufferStatus)(GLenum);
static void (APIENTRY* pfn_glDeleteFramebuffers)(GLsizei, const GLuint*);
static void (APIENTRY* pfn_glDeleteRenderbuffers)(GLsizei, const GLuint*);

static bool ResolveGLProcs()
{
  bool bOk = true;
  const struct
  {
    const char* szName;
    void** ppFn;
  } procs[] = {
    {"glGenFramebuffers", (void**)&pfn_glGenFramebuffers},
    {"glBindFramebuffer", (void**)&pfn_glBindFramebuffer},
    {"glFramebufferTexture2D", (void**)&pfn_glFramebufferTexture2D},
    {"glGenRenderbuffers", (void**)&pfn_glGenRenderbuffers},
    {"glBindRenderbuffer", (void**)&pfn_glBindRenderbuffer},
    {"glRenderbufferStorage", (void**)&pfn_glRenderbufferStorage},
    {"glFramebufferRenderbuffer", (void**)&pfn_glFramebufferRenderbuffer},
    {"glCheckFramebufferStatus", (void**)&pfn_glCheckFramebufferStatus},
    {"glDeleteFramebuffers", (void**)&pfn_glDeleteFramebuffers},
    {"glDeleteRenderbuffers", (void**)&pfn_glDeleteRenderbuffers},
  };

  for (const auto& p : procs)
  {
    *p.ppFn = (void*)wglGetProcAddress(p.szName);
    if (*p.ppFn == nullptr)
    {
      fprintf(stderr, "[screenshot] wglGetProcAddress failed: %s\n", p.szName);
      bOk = false;
    }
  }
  return bOk;
}

static aeString WideToUtf8(const wchar_t* szWide)
{
  char szTemp[2048] = "";
  WideCharToMultiByte(CP_UTF8, 0, szWide, -1, szTemp, sizeof(szTemp), nullptr, nullptr);
  return aeString(szTemp);
}

static int RunScreenshotMode(HINSTANCE hInstance, LPSTR lpCmdLine)
{
  // GUI-subsystem app: reattach to the launching console so agents can read output
  if (AttachConsole(ATTACH_PARENT_PROCESS))
  {
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
  }

  // parse arguments
  aeString sInput, sOutput;
  int iWidth = 1280, iHeight = 720;
  aeInt64 iSeed = -1;

  {
    int iArgC = 0;
    LPWSTR* pArgV = CommandLineToArgvW(GetCommandLineW(), &iArgC);
    for (int i = 1; i < iArgC; ++i)
    {
      const aeString sArg = WideToUtf8(pArgV[i]);
      if (sArg == "--screenshot" && i + 2 < iArgC)
      {
        sInput = WideToUtf8(pArgV[++i]);
        sOutput = WideToUtf8(pArgV[++i]);
      }
      else if (sArg == "--width" && i + 1 < iArgC)
        iWidth = atoi(WideToUtf8(pArgV[++i]).c_str());
      else if (sArg == "--height" && i + 1 < iArgC)
        iHeight = atoi(WideToUtf8(pArgV[++i]).c_str());
      else if (sArg == "--seed" && i + 1 < iArgC)
        iSeed = _atoi64(WideToUtf8(pArgV[++i]).c_str());
    }
    LocalFree(pArgV);
  }

  if (sInput.c_str()[0] == '\0' || sOutput.c_str()[0] == '\0')
  {
    fprintf(stderr, "Usage: aeEditor.exe --screenshot <in.tree> <out.png> [--seed N] [--width W] [--height H]\n");
    return 1;
  }

  int argc = 0;
  QApplication app(argc, nullptr);

  StartupMain(hInstance, nullptr, lpCmdLine, SW_HIDE);

  // create the hidden window that hosts the GL context (same registry key the qt3DWidget uses)
  HWND hWnd = CreateHiddenGLWindow(hInstance);
  if (hWnd == nullptr)
  {
    fprintf(stderr, "Failed to create hidden window.\n");
    return 3;
  }
  aeVariableRegistry::StoreRaw("system/windows/hwnd", &hWnd, sizeof(HWND));

  // load the render plugin (done by qt3DWidget in GUI mode)
  aePluginManager::LoadPlugin("ezKrautEditorRenderAPI_GL", "");

  fprintf(stderr, "[screenshot] creating GL context %dx%d...\n", iWidth, iHeight);

  aeEditorRenderAPI* pRenderAPI = aeEditorRenderAPI::GetInstance();
  if (pRenderAPI == nullptr)
  {
    fprintf(stderr, "RenderAPI plugin not available (ezKrautEditorRenderAPI_GL.dll failed to load).\n");
    return 3;
  }
  pRenderAPI->CreateContext(iWidth, iHeight);

  if (!ResolveGLProcs())
    return 3;

  // tell the engine that graphics modules may now initialize
  aeStartup::StartupEngine();
  // note: aeEditor_BeforeFirstFrame is deliberately NOT broadcast here - its only handler
  // (TreePlugin) would dereference qtTreeEditWidget::s_pWidget, which is null in headless mode.
  // The tree is loaded explicitly below instead.

  // load the tree (bypassing aeTreePlugin::LoadTree, which touches GUI widgets)
  {
    fprintf(stderr, "[screenshot] loading %s...\n", sInput.c_str());
    aeFileIn f;
    if (!f.Open(sInput.c_str()))
    {
      fprintf(stderr, "Could not open tree file: %s\n", sInput.c_str());
      return 2;
    }
    g_Tree.m_sTreeFile = sInput.c_str();
    g_Tree.Load(f);
  }

  if (iSeed >= 0)
    g_Tree.m_Descriptor.m_StructureDesc.m_uiRandomSeed = (aeUInt32)iSeed;

  fprintf(stderr, "[screenshot] generating...\n");
  g_Tree.GenerateTree(false);

  // frame the tree with the camera
  {
    const aeVec3 vCenter = (g_Tree.m_BBox.m_vMin + g_Tree.m_BBox.m_vMax) * 0.5f;
    const float fRadius = aeMath::Max(0.5f, (g_Tree.m_BBox.m_vMax - g_Tree.m_BBox.m_vMin).GetLength() * 0.5f);

    g_Globals.s_vCameraPosition = vCenter + aeVec3(fRadius * 0.9f, fRadius * 0.2f, fRadius * 0.9f);
    g_Globals.s_vCameraLookDir = (vCenter - g_Globals.s_vCameraPosition).GetNormalized();
    g_Globals.s_vCameraPivot = vCenter;
  }

  std::vector<unsigned char> pixels((size_t)iWidth * iHeight * 4);

  // Minimal manual render: deliberately NOT going through g_Plugin.Render() (the aeFrame_End
  // pipeline), which is heavily GUI-coupled (undo system, stats, gizmos, ground plane, physics
  // objects). This replicates only the essential state from aeTreePlugin::Render and renders the
  // tree itself with bForExportPreview=true (the editor's own export-preview path: tree only,
  // no collision mesh / ground plane / forces).
  {
    // render into an FBO sized W x H (the hidden 64x64 host window's framebuffer is too small
    // and invisible windows may not even have a real back buffer on some drivers)
    GLuint fbo = 0, colorTex = 0, depthRb = 0;
    pfn_glGenFramebuffers(1, &fbo);
    pfn_glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pfn_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    pfn_glGenRenderbuffers(1, &depthRb);
    pfn_glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    pfn_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, iWidth, iHeight);
    pfn_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);

    if (pfn_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      fprintf(stderr, "[screenshot] framebuffer incomplete.\n");
      return 3;
    }

    AE_NS_GRAPHICS::aeRenderAPI::Clear(true, true, 153 / 255.0f, 171 / 255.0f, 193 / 255.0f, 1.0f);
    AE_NS_GRAPHICS::aeRenderAPI::setViewport(0, 0, iWidth, iHeight);

    g_Globals.m_Camera.setPerspectiveCamera(80.0f, 0.01f, 1000.0f);
    g_Globals.m_Camera.setLookAt(g_Globals.s_vCameraPosition, g_Globals.s_vCameraPivot);
    g_Globals.m_Camera.setViewport(0, 0, iWidth, iHeight);
    g_Globals.m_Camera.ApplyCamera();

    // lighting state (same defaults as the editor's ambient color CVars)
    AE_NS_GRAPHICS::aeShaderManager::setUniformFloat("unif_AmbientLow", 3, 72 / 255.0f, 65 / 255.0f, 58 / 255.0f);
    AE_NS_GRAPHICS::aeShaderManager::setUniformFloat("unif_AmbientHigh", 3, 49 / 255.0f, 75 / 255.0f, 79 / 255.0f);
    AE_NS_GRAPHICS::aeShaderManager::setUniformFloat("unif_LightPos", 3, g_Globals.s_vPointLightPos.x, g_Globals.s_vPointLightPos.y, g_Globals.s_vPointLightPos.z);
    g_Globals.s_vSunLightDir.Normalize();
    AE_NS_GRAPHICS::aeShaderManager::setUniformFloat("unif_SunDir", 3, g_Globals.s_vSunLightDir.x, g_Globals.s_vSunLightDir.y, g_Globals.s_vSunLightDir.z);

    g_Tree.Render(g_Globals.m_Camera.getPosition(), false, /*bForExportPreview=*/true);

    glFinish();

    // read back the framebuffer
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, iWidth, iHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    pfn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pfn_glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTex);
    pfn_glDeleteRenderbuffers(1, &depthRb);
  }

  QImage img(pixels.data(), iWidth, iHeight, QImage::Format_RGBA8888);
  if (!img.mirrored().save(QString::fromUtf8(sOutput.c_str()), "PNG"))
  {
    fprintf(stderr, "Failed to write PNG: %s\n", sOutput.c_str());
    return 3;
  }

  printf("{\"screenshot\": \"%s\", \"width\": %d, \"height\": %d, \"triangles\": %u}\n", sOutput.c_str(), iWidth, iHeight, g_Tree.GetNumAllTriangles(aeLod::Lod0));

  pRenderAPI->DestroyContext();
  aeStartup::ShutdownEngine();
  ShutdownMain();

  return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  // Headless screenshot mode: aeEditor.exe --screenshot <in.tree> <out.png> [--seed N] [--width W] [--height H]
  if (strstr(lpCmdLine, "--screenshot") != nullptr)
    return RunScreenshotMode(hInstance, lpCmdLine);

  int argc = 0;
  QApplication app(argc, nullptr);

  StartupMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);

  LoadStylesheet(app);

  qtMainWindow MainWindow;
  MainWindow.show();

  int iResult = app.exec();

  ShutdownMain();

  return iResult;
}
