// KrautPreview - SDL2 + Dear ImGui + OpenGL 3.3 preview for Kraut tree generation.
//
// Usage:
//   KrautPreview <file.tree> [--seed N] [--lod none|0|1|2|3|4] [--data DIR]
//   KrautPreview <file.tree> [--seed N] [--lod ...] --screenshot out.png [--width W] [--height H]
//
// Interactive mode: orbit camera (drag), zoom (wheel), ImGui panel (seed, LOD, wireframe).
// Screenshot mode: renders offscreen, writes PNG, exits 0 (agent verification path).
//
// Rendering mirrors the official editor (Code/Engine/TreePlugin/Rendering/TreeRendering.cpp):
//  - branches/fronds: diffuse texture with projective UVs (texCoord.xy / texCoord.z)
//  - billboard leaves: quad expanded in the vertex shader around the anchor point
//    (corner in tangent.xy, size in texCoord.z), alpha-tested

#include "Pipeline.h"
#include "TextureLoader.h"
#include "TreeFile.h"

#include <GL/glew.h>
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
  using namespace KrautCLI;

  const char* const LOD_NAMES[LOD_COUNT] = {"full", "lod0", "lod1", "lod2", "lod3", "lod4"};

  struct Options
  {
    const char* m_szInput = nullptr;
    const char* m_szScreenshot = nullptr;
    const char* m_szAtlas = nullptr;
    aeUInt32 m_uiSeed = 0;
    bool m_bSeedGiven = false;
    int m_iLod = 0;
    int m_iWidth = 1280;
    int m_iHeight = 720;
    int m_iAtlasCols = 8;
    int m_iAtlasRes = 256;
    std::vector<std::string> m_DataRoots;
  };

  void PrintUsage()
  {
    std::printf(
      "KrautPreview - SDL2+ImGui preview for Kraut trees\n"
      "Usage:\n"
      "  KrautPreview <file.tree> [--seed N] [--lod none|0|1|2|3|4] [--data DIR]\n"
      "  KrautPreview <file.tree> [--seed N] [--lod ...] --screenshot out.png [--width W] [--height H]\n"
      "  KrautPreview <file.tree> [--seed N] [--lod ...] --atlas out.png [--atlas-cols N] [--atlas-res R]\n"
      "\n"
      "  --data DIR  extra data root for texture lookup (repeatable);\n"
      "              the repo's Data/Content and Data/TreePlugin are probed automatically\n"
      "  --atlas     bake a cylindrical billboard atlas: N orthographic views in one row,\n"
      "              cell k from azimuth ((k + 0.5) / cols - 0.5) * 2*pi around +Z\n");
  }

  bool ParseArgs(int argc, char** argv, Options& opt)
  {
    if (argc < 2)
      return false;

    for (int i = 1; i < argc; ++i)
    {
      const char* sz = argv[i];
      if (std::strcmp(sz, "--seed") == 0 && i + 1 < argc)
      {
        opt.m_uiSeed = (aeUInt32)std::strtoul(argv[++i], nullptr, 10);
        opt.m_bSeedGiven = true;
      }
      else if (std::strcmp(sz, "--lod") == 0 && i + 1 < argc)
      {
        const char* v = argv[++i];
        if (std::strcmp(v, "none") == 0 || std::strcmp(v, "full") == 0)
          opt.m_iLod = 0;
        else if (v[0] >= '0' && v[0] <= '4' && v[1] == '\0')
          opt.m_iLod = 1 + (v[0] - '0');
        else
          return false;
      }
      else if (std::strcmp(sz, "--screenshot") == 0 && i + 1 < argc)
        opt.m_szScreenshot = argv[++i];
      else if (std::strcmp(sz, "--atlas") == 0 && i + 1 < argc)
        opt.m_szAtlas = argv[++i];
      else if (std::strcmp(sz, "--atlas-cols") == 0 && i + 1 < argc)
        opt.m_iAtlasCols = std::max(1, std::atoi(argv[++i]));
      else if (std::strcmp(sz, "--atlas-res") == 0 && i + 1 < argc)
        opt.m_iAtlasRes = std::max(16, std::atoi(argv[++i]));
      else if (std::strcmp(sz, "--width") == 0 && i + 1 < argc)
        opt.m_iWidth = std::atoi(argv[++i]);
      else if (std::strcmp(sz, "--height") == 0 && i + 1 < argc)
        opt.m_iHeight = std::atoi(argv[++i]);
      else if (std::strcmp(sz, "--data") == 0 && i + 1 < argc)
        opt.m_DataRoots.push_back(argv[++i]);
      else if (sz[0] != '-' && opt.m_szInput == nullptr)
        opt.m_szInput = sz;
      else
        return false;
    }
    return opt.m_szInput != nullptr;
  }

  // ---------- minimal GL renderer ----------

  const char* g_szVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aNormal;
    layout(location = 2) in vec3 aUVW;
    layout(location = 3) in vec2 aCorner;
    uniform mat4 uMVP;
    uniform bool uBillboard;
    uniform vec3 uCamRight;
    uniform vec3 uCamUp;
    out vec3 vNormal;
    out vec3 vUVW;
    void main()
    {
      vec3 pos = aPos;
      vNormal = aNormal;
      vUVW = aUVW;
      if (uBillboard)
      {
        // billboard leaf: 4 vertices share the anchor, corner in aCorner (0..1), size in aUVW.z
        vec2 span = aCorner * 2.0 - 1.0;
        pos += (span.x * uCamRight + span.y * uCamUp) * aUVW.z;
        vNormal = vec3(0.0); // lighting falls back to a camera-facing normal in the FS
      }
      gl_Position = uMVP * vec4(pos, 1.0);
    }
  )";

  const char* g_szFragmentShader = R"(
    #version 330 core
    in vec3 vNormal;
    in vec3 vUVW;
    uniform sampler2D uTex;
    uniform bool uHasTex;
    uniform bool uAlphaTest;
    uniform bool uBillboard;
    uniform vec3 uLightDir;
    uniform vec3 uBaseColor;
    uniform vec3 uCamForward;
    out vec4 FragColor;
    void main()
    {
      // branches/fronds use projective UVs (w = ring correction), billboards plain UVs
      vec2 uv = uBillboard ? vUVW.xy : (vUVW.xy / max(vUVW.z, 1e-6));
      vec4 tex = uHasTex ? texture(uTex, uv) : vec4(1.0);
      if (uAlphaTest && tex.a < 0.1)
        discard;
      vec3 n = uBillboard ? -uCamForward : normalize(vNormal);
      float diff = abs(dot(n, normalize(uLightDir))); // two-sided: foliage has flaky normals
      vec3 col = tex.rgb * uBaseColor * (0.25 + 0.75 * diff);
      FragColor = vec4(col, 1.0);
    }
  )";

  GLuint CompileShader(GLenum type, const char* szSrc)
  {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &szSrc, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      char log[1024];
      glGetShaderInfoLog(s, sizeof(log), nullptr, log);
      std::fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
  }

  GLuint CreateProgram()
  {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_szVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_szFragmentShader);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
  }

  struct GpuMesh
  {
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    GLuint m_EBO = 0;
    GLsizei m_NumIndices = 0;
  };

  // Interleaved vertex: pos(3) normal(3) uvw(3) corner(2) = 11 floats
  const int VERTEX_FLOATS = 11;

  struct CpuMesh
  {
    std::vector<float> m_Vertices;
    std::vector<aeUInt32> m_Indices;
  };

  // One draw call: all sub-meshes sharing a geometry type + texture
  struct Batch
  {
    CpuMesh m_Cpu;
    GpuMesh m_Gpu;
    std::string m_sTexturePath; // resolved file path, empty = untextured fallback
    bool m_bBillboard = false;
    bool m_bAlphaTest = false;
    float m_fBaseColor[3] = {1, 1, 1};
  };

  bool IsBillboardLeafMesh(const Kraut::Mesh& mesh)
  {
    // billboard leaves have no normal (the editor uses the same test in CreateTreeRenderData)
    return !mesh.m_Vertices.empty() && mesh.m_Vertices[0].m_vNormal.IsZeroVector(0.001f);
  }

  void AppendMesh(CpuMesh& out, const Kraut::Mesh& mesh)
  {
    const aeUInt32 uiBase = (aeUInt32)(out.m_Vertices.size() / VERTEX_FLOATS);

    for (aeUInt32 v = 0; v < mesh.m_Vertices.size(); ++v)
    {
      const Kraut::Vertex& vtx = mesh.m_Vertices[v];
      out.m_Vertices.push_back(vtx.m_vPosition.x);
      out.m_Vertices.push_back(vtx.m_vPosition.y);
      out.m_Vertices.push_back(vtx.m_vPosition.z);
      out.m_Vertices.push_back(vtx.m_vNormal.x);
      out.m_Vertices.push_back(vtx.m_vNormal.y);
      out.m_Vertices.push_back(vtx.m_vNormal.z);
      out.m_Vertices.push_back(vtx.m_vTexCoord.x);
      out.m_Vertices.push_back(vtx.m_vTexCoord.y);
      out.m_Vertices.push_back(vtx.m_vTexCoord.z);
      out.m_Vertices.push_back(vtx.m_vTangent.x); // billboard corner x (0..1), unused otherwise
      out.m_Vertices.push_back(vtx.m_vTangent.y); // billboard corner y (0..1), unused otherwise
    }

    for (aeUInt32 t = 0; t < mesh.m_Triangles.size(); ++t)
    {
      out.m_Indices.push_back(uiBase + mesh.m_Triangles[t].m_uiVertexIDs[0]);
      out.m_Indices.push_back(uiBase + mesh.m_Triangles[t].m_uiVertexIDs[1]);
      out.m_Indices.push_back(uiBase + mesh.m_Triangles[t].m_uiVertexIDs[2]);
    }
  }

  void UploadBatch(Batch& batch)
  {
    GpuMesh& gpu = batch.m_Gpu;
    glGenVertexArrays(1, &gpu.m_VAO);
    glGenBuffers(1, &gpu.m_VBO);
    glGenBuffers(1, &gpu.m_EBO);

    glBindVertexArray(gpu.m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.m_VBO);
    glBufferData(GL_ARRAY_BUFFER, batch.m_Cpu.m_Vertices.size() * sizeof(float), batch.m_Cpu.m_Vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, batch.m_Cpu.m_Indices.size() * sizeof(aeUInt32), batch.m_Cpu.m_Indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = VERTEX_FLOATS * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)(9 * sizeof(float)));

    glBindVertexArray(0);
    gpu.m_NumIndices = (GLsizei)batch.m_Cpu.m_Indices.size();
  }

  void DeleteBatchGpu(Batch& batch)
  {
    if (batch.m_Gpu.m_VAO)
    {
      glDeleteVertexArrays(1, &batch.m_Gpu.m_VAO);
      glDeleteBuffers(1, &batch.m_Gpu.m_VBO);
      glDeleteBuffers(1, &batch.m_Gpu.m_EBO);
      batch.m_Gpu = GpuMesh();
    }
  }

  GLuint UploadTexture(const KrautPreview::Image& img)
  {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.m_iWidth, img.m_iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.m_Rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
  }

  // column-major 4x4, minimal mat math
  struct Mat4
  {
    float m[16];

    static Mat4 Identity()
    {
      Mat4 r = {};
      r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
      return r;
    }

    static Mat4 Perspective(float fFovYRad, float fAspect, float fNear, float fFar)
    {
      Mat4 r = {};
      const float f = 1.0f / std::tan(fFovYRad * 0.5f);
      r.m[0] = f / fAspect;
      r.m[5] = f;
      r.m[10] = (fFar + fNear) / (fNear - fFar);
      r.m[11] = -1.0f;
      r.m[14] = (2.0f * fFar * fNear) / (fNear - fFar);
      return r;
    }

    static Mat4 LookAt(float ex, float ey, float ez, float cx, float cy, float cz)
    {
      float fx = cx - ex, fy = cy - ey, fz = cz - ez;
      const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
      fx /= fl; fy /= fl; fz /= fl;
      // up = (0,1,0); s = f x up; u = s x f
      float sx = -fz, sy = 0.0f, sz = fx;
      const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
      sx /= sl; sy /= sl; sz /= sl;
      const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;

      Mat4 r = Identity();
      r.m[0] = sx; r.m[4] = sy; r.m[8] = sz;
      r.m[1] = ux; r.m[5] = uy; r.m[9] = uz;
      r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
      r.m[12] = -(sx * ex + sy * ey + sz * ez);
      r.m[13] = -(ux * ex + uy * ey + uz * ez);
      r.m[14] = (fx * ex + fy * ey + fz * ez);
      return r;
    }

    static Mat4 Ortho(float l, float r, float b, float t, float n, float f)
    {
      Mat4 m = Identity();
      m.m[0] = 2.0f / (r - l);
      m.m[5] = 2.0f / (t - b);
      m.m[10] = -2.0f / (f - n);
      m.m[12] = -(r + l) / (r - l);
      m.m[13] = -(t + b) / (t - b);
      m.m[14] = -(f + n) / (f - n);
      return m;
    }

    static Mat4 Mul(const Mat4& a, const Mat4& b)
    {
      Mat4 r = {};
      for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
          for (int k = 0; k < 4; ++k)
            r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
      return r;
    }
  };

  struct Camera
  {
    float m_fYaw = 0.6f;
    float m_fPitch = 0.35f;
    float m_fDistance = 15.0f;
    float m_vCenter[3] = {0, 4, 0};

    void Eye(float out_Eye[3]) const
    {
      const float cp = std::cos(m_fPitch), sp = std::sin(m_fPitch);
      const float cy = std::cos(m_fYaw), sy = std::sin(m_fYaw);
      out_Eye[0] = m_vCenter[0] + m_fDistance * cp * sy;
      out_Eye[1] = m_vCenter[1] + m_fDistance * sp;
      out_Eye[2] = m_vCenter[2] + m_fDistance * cp * cy;
    }

    // world-space camera basis: forward / right / up (matches LookAt below)
    void Basis(float out_Fwd[3], float out_Right[3], float out_Up[3]) const
    {
      float eye[3];
      Eye(eye);
      float f[3] = {m_vCenter[0] - eye[0], m_vCenter[1] - eye[1], m_vCenter[2] - eye[2]};
      const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
      f[0] /= fl; f[1] /= fl; f[2] /= fl;
      float s[3] = {-f[2], 0.0f, f[0]}; // f x (0,1,0)
      const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
      s[0] /= sl; s[1] /= sl; s[2] /= sl;
      const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
      out_Fwd[0] = f[0]; out_Fwd[1] = f[1]; out_Fwd[2] = f[2];
      out_Right[0] = s[0]; out_Right[1] = s[1]; out_Right[2] = s[2];
      out_Up[0] = u[0]; out_Up[1] = u[1]; out_Up[2] = u[2];
    }

    Mat4 View() const
    {
      float eye[3];
      Eye(eye);
      return Mat4::LookAt(eye[0], eye[1], eye[2], m_vCenter[0], m_vCenter[1], m_vCenter[2]);
    }
  };

  struct AppState
  {
    TreeFile m_TreeFile;
    GeneratedTree m_Tree;
    std::vector<Batch> m_Batches;
    std::map<std::string, GLuint> m_Textures; // resolved path -> GL texture (0 if load failed)
    std::vector<std::string> m_TextureRoots;
    bool m_bGpuDirty = true;
    int m_iLod = 0;
    int m_iSeed = 0;
    bool m_bWireframe = false;
    Camera m_Camera;
  };

  void Regenerate(AppState& app)
  {
    BuildInfluencesFromForces(app.m_TreeFile);

    aeString sError;
    if (!GenerateTree(app.m_TreeFile, (aeUInt32)app.m_iSeed, (aeUInt32)app.m_iLod, app.m_Tree, sError))
    {
      std::fprintf(stderr, "Generation failed: %s\n", sError.c_str());
      return;
    }

    // group sub-meshes into batches by (geometry type, texture)
    std::map<std::string, size_t> batchMap;
    app.m_Batches.clear();

    for (aeUInt32 b = 0; b < app.m_Tree.m_Mesh.m_BranchMeshes.size(); ++b)
    {
      const aeUInt32 type = app.m_Tree.m_Structure.m_BranchStructures[b].m_Type;
      const Kraut::SpawnNodeDesc& spawnDesc = app.m_TreeFile.m_StructureDesc.m_BranchTypes[type];

      if (!spawnDesc.m_bVisible)
        continue;

      for (aeUInt32 mt = 0; mt < Kraut::BranchGeometryType::ENUM_COUNT; ++mt)
      {
        const Kraut::Mesh& mesh = app.m_Tree.m_Mesh.m_BranchMeshes[b].m_Mesh[mt];
        if (mesh.m_Triangles.empty())
          continue;

        char key[1024];
        std::snprintf(key, sizeof(key), "%u|%s", mt, spawnDesc.m_sTexture[mt].c_str());

        size_t idx;
        auto it = batchMap.find(key);
        if (it == batchMap.end())
        {
          idx = app.m_Batches.size();
          batchMap[key] = idx;

          Batch batch;
          const bool bBillboard = (mt == Kraut::BranchGeometryType::Leaf) && IsBillboardLeafMesh(mesh);
          batch.m_bBillboard = bBillboard;
          batch.m_bAlphaTest = (mt != Kraut::BranchGeometryType::Branch);
          batch.m_sTexturePath = KrautPreview::ResolveTexturePath(spawnDesc.m_sTexture[mt].c_str(), app.m_TextureRoots);

          if (batch.m_sTexturePath.empty() && !spawnDesc.m_sTexture[mt].empty())
          {
            std::fprintf(stderr, "Warning: texture not found: %s\n", spawnDesc.m_sTexture[mt].c_str());
            const float fallback[Kraut::BranchGeometryType::ENUM_COUNT][3] = {
              {0.50f, 0.40f, 0.32f}, // branch: bark-ish
              {0.45f, 0.60f, 0.35f}, // frond: green
              {0.50f, 0.65f, 0.40f}, // leaf: green
            };
            batch.m_fBaseColor[0] = fallback[mt][0];
            batch.m_fBaseColor[1] = fallback[mt][1];
            batch.m_fBaseColor[2] = fallback[mt][2];
          }

          app.m_Batches.push_back(std::move(batch));
        }
        else
        {
          idx = it->second;
        }

        AppendMesh(app.m_Batches[idx].m_Cpu, mesh);
      }
    }

    app.m_bGpuDirty = true;

    // frame the tree
    const auto& bb = app.m_Tree.m_BBox;
    app.m_Camera.m_vCenter[0] = (bb.m_vMin.x + bb.m_vMax.x) * 0.5f;
    app.m_Camera.m_vCenter[1] = (bb.m_vMin.y + bb.m_vMax.y) * 0.5f;
    app.m_Camera.m_vCenter[2] = (bb.m_vMin.z + bb.m_vMax.z) * 0.5f;
    const float dx = bb.m_vMax.x - bb.m_vMin.x, dy = bb.m_vMax.y - bb.m_vMin.y, dz = bb.m_vMax.z - bb.m_vMin.z;
    const float fMaxDim = std::fmax(dx, std::fmax(dy, dz));
    app.m_Camera.m_fDistance = fMaxDim * 1.2f + 1.0f;
  }

  GLuint GetBatchTexture(AppState& app, const Batch& batch)
  {
    if (batch.m_sTexturePath.empty())
      return 0;

    auto it = app.m_Textures.find(batch.m_sTexturePath);
    if (it != app.m_Textures.end())
      return it->second;

    GLuint tex = 0;
    KrautPreview::Image img;
    if (KrautPreview::LoadImageFile(batch.m_sTexturePath, img))
    {
      tex = UploadTexture(img);
    }
    else
    {
      std::fprintf(stderr, "Warning: failed to load texture: %s\n", batch.m_sTexturePath.c_str());
    }

    app.m_Textures[batch.m_sTexturePath] = tex;
    return tex;
  }

  void EnsureGpuData(AppState& app)
  {
    if (!app.m_bGpuDirty)
      return;

    for (Batch& batch : app.m_Batches)
    {
      DeleteBatchGpu(batch);
      UploadBatch(batch);
    }
    app.m_bGpuDirty = false;
  }

  void DrawScene(AppState& app, GLuint program, int iWidth, int iHeight,
    const Mat4& proj, const Mat4& view, const float clearColor[4],
    const float fwd[3], const float right[3], const float up[3])
  {
    glViewport(0, 0, iWidth, iHeight);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // fronds/leaves are double-sided

    if (app.m_bWireframe)
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(program);

    const Mat4 model = Mat4::Identity();
    const Mat4 mvp = Mat4::Mul(proj, Mat4::Mul(view, model));

    glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniform3f(glGetUniformLocation(program, "uLightDir"), 0.5f, 0.8f, 0.3f);
    glUniform3fv(glGetUniformLocation(program, "uCamForward"), 1, fwd);
    glUniform3fv(glGetUniformLocation(program, "uCamRight"), 1, right);
    glUniform3fv(glGetUniformLocation(program, "uCamUp"), 1, up);
    glUniform1i(glGetUniformLocation(program, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);

    // opaque first, alpha-tested last
    for (int pass = 0; pass < 2; ++pass)
    {
      const bool bWantAlphaTest = (pass == 1);

      for (Batch& batch : app.m_Batches)
      {
        if (batch.m_bAlphaTest != bWantAlphaTest)
          continue;

        const GLuint tex = GetBatchTexture(app, batch);

        glUniform1i(glGetUniformLocation(program, "uBillboard"), batch.m_bBillboard ? 1 : 0);
        glUniform1i(glGetUniformLocation(program, "uAlphaTest"), batch.m_bAlphaTest ? 1 : 0);
        glUniform1i(glGetUniformLocation(program, "uHasTex"), tex != 0 ? 1 : 0);
        glUniform3fv(glGetUniformLocation(program, "uBaseColor"), 1, batch.m_fBaseColor);

        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(batch.m_Gpu.m_VAO);
        glDrawElements(GL_TRIANGLES, batch.m_Gpu.m_NumIndices, GL_UNSIGNED_INT, nullptr);
      }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  int RunScreenshot(AppState& app, const Options& opt, SDL_Window* pWindow, GLuint program)
  {
    const int W = opt.m_iWidth, H = opt.m_iHeight;

    GLuint fbo = 0, colorTex = 0, depthRb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      std::fprintf(stderr, "Framebuffer incomplete.\n");
      return 4;
    }

    const Mat4 proj = Mat4::Perspective(50.0f * 3.14159265f / 180.0f, (float)W / (float)H, 0.05f, 500.0f);
    const Mat4 view = app.m_Camera.View();
    const float clearColor[4] = {0.65f, 0.72f, 0.80f, 1.0f};
    float fwd[3], right[3], up[3];
    app.m_Camera.Basis(fwd, right, up);
    DrawScene(app, program, W, H, proj, view, clearColor, fwd, right, up);
    glFinish();

    std::vector<unsigned char> pixels((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTex);
    glDeleteRenderbuffers(1, &depthRb);

    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(opt.m_szScreenshot, W, H, 4, pixels.data(), W * 4))
    {
      std::fprintf(stderr, "Failed to write PNG: %s\n", opt.m_szScreenshot);
      return 4;
    }

    std::printf("{\n  \"screenshot\": \"%s\",\n  \"width\": %d,\n  \"height\": %d,\n  \"triangles\": %u\n}\n",
      opt.m_szScreenshot, W, H, app.m_Tree.GetNumTriangles());
    return 0;
  }

  std::string DirName(const std::string& sPath)
  {
    const size_t slash = sPath.find_last_of("/\\");
    if (slash == std::string::npos)
      return ".";
    return sPath.substr(0, slash);
  }

  // Billboard atlas bake: `cols` orthographic views in one row, cell k from
  // azimuth ((k + 0.5) / cols - 0.5) * 2*pi around the tree's +Z axis
  // (contract with the fury BILLBOARD shader, see add-glb-export design D5).
  // The ortho box exactly covers the glb's pre-sized billboard quad:
  // width = XZ bbox diagonal (silhouette fits from every azimuth),
  // height = bbox height. Background is transparent.
  int RunAtlas(AppState& app, const Options& opt, SDL_Window* pWindow, GLuint program)
  {
    const int cols = opt.m_iAtlasCols;
    const int res = opt.m_iAtlasRes;

    const auto& bb = app.m_Tree.m_BBox;
    const float cx = (bb.m_vMin.x + bb.m_vMax.x) * 0.5f;
    const float cz = (bb.m_vMin.z + bb.m_vMax.z) * 0.5f;
    const float sx = bb.m_vMax.x - bb.m_vMin.x;
    const float sy = bb.m_vMax.y - bb.m_vMin.y;
    const float sz = bb.m_vMax.z - bb.m_vMin.z;
    const float quadW = std::sqrt(sx * sx + sz * sz);
    const float quadH = sy;
    const float diag = std::sqrt(sx * sx + sy * sy + sz * sz);

    GLuint fbo = 0, colorTex = 0, depthRb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    glGenRenderbuffers(1, &depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, res, res);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      std::fprintf(stderr, "Framebuffer incomplete.\n");
      return 4;
    }

    std::vector<unsigned char> atlas((size_t)cols * res * res * 4, 0);
    std::vector<unsigned char> pixels((size_t)res * res * 4);
    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    for (int k = 0; k < cols; ++k)
    {
      const float az = ((k + 0.5f) / (float)cols - 0.5f) * 6.2831853f;
      const float dist = diag * 2.0f + 1.0f;
      const float eye[3] = {cx + std::sin(az) * dist, quadH * 0.5f, cz + std::cos(az) * dist};
      const Mat4 view = Mat4::LookAt(eye[0], eye[1], eye[2], cx, quadH * 0.5f, cz);
      const Mat4 proj = Mat4::Ortho(-quadW * 0.5f, quadW * 0.5f, -quadH * 0.5f, quadH * 0.5f,
        dist - diag, dist + diag);

      // camera basis for the billboard-leaf vertex shader
      float f[3] = {cx - eye[0], 0.0f, cz - eye[2]};
      const float fl = std::sqrt(f[0] * f[0] + f[2] * f[2]);
      f[0] /= fl; f[2] /= fl;
      float r[3] = {-f[2], 0.0f, f[0]};
      const float u[3] = {0.0f, 1.0f, 0.0f};

      DrawScene(app, program, res, res, proj, view, clearColor, f, r, u);
      glFinish();
      glReadPixels(0, 0, res, res, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

      for (int y = 0; y < res; ++y)
        std::memcpy(&atlas[((size_t)y * (size_t)cols * res + (size_t)k * res) * 4],
          &pixels[(size_t)y * res * 4], (size_t)res * 4);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colorTex);
    glDeleteRenderbuffers(1, &depthRb);

    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(opt.m_szAtlas, cols * res, res, 4, atlas.data(), cols * res * 4))
    {
      std::fprintf(stderr, "Failed to write PNG: %s\n", opt.m_szAtlas);
      return 4;
    }

    std::printf("{\n  \"atlas\": \"%s\",\n  \"cols\": %d,\n  \"rows\": 1,\n  \"cellResolution\": %d,\n  \"width\": %d,\n  \"height\": %d\n}\n",
      opt.m_szAtlas, cols, res, cols * res, res);
    return 0;
  }

  void BuildTextureRoots(AppState& app, const Options& opt, const char* szExeDir)
  {
    // explicit roots first (highest priority)
    for (const std::string& s : opt.m_DataRoots)
      app.m_TextureRoots.push_back(s);

    // directory of the input .tree file, then cwd
    app.m_TextureRoots.push_back(DirName(opt.m_szInput));
    app.m_TextureRoots.push_back(".");

    // repo data dirs, relative to the exe (Output/Bin/<Config> -> repo root)
    if (szExeDir && szExeDir[0])
    {
      const std::string sRepo = std::string(szExeDir) + "/../../..";
      app.m_TextureRoots.push_back(sRepo + "/Data/Content");
      app.m_TextureRoots.push_back(sRepo + "/Data/TreePlugin");
      app.m_TextureRoots.push_back(sRepo);
    }

    // cwd-relative repo layout (running from the repo root)
    app.m_TextureRoots.push_back("Data/Content");
    app.m_TextureRoots.push_back("Data/TreePlugin");
  }
} // namespace

int main(int argc, char** argv)
{
  Options opt;
  if (!ParseArgs(argc, argv, opt))
  {
    PrintUsage();
    return 1;
  }

  AppState app;
  app.m_iLod = opt.m_iLod;

  aeString sError;
  if (!app.m_TreeFile.Load(opt.m_szInput, sError))
  {
    std::fprintf(stderr, "Error (load): %s\n", sError.c_str());
    return 2;
  }

  app.m_iSeed = opt.m_bSeedGiven ? (int)opt.m_uiSeed : (int)app.m_TreeFile.m_StructureDesc.m_uiRandomSeed;

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 4;
  }

  BuildTextureRoots(app, opt, SDL_GetBasePath());

  Regenerate(app);
  if (app.m_Batches.empty())
    return 3;

  const bool bScreenshot = (opt.m_szScreenshot != nullptr);
  const bool bAtlas = (opt.m_szAtlas != nullptr);
  const bool bOffscreen = bScreenshot || bAtlas;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  const aeUInt32 uiWindowFlags = SDL_WINDOW_OPENGL | (bOffscreen ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED));

  SDL_Window* pWindow = SDL_CreateWindow("KrautPreview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, opt.m_iWidth, opt.m_iHeight, uiWindowFlags);
  if (!pWindow)
  {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 4;
  }

  SDL_GLContext glContext = SDL_GL_CreateContext(pWindow);
  if (!glContext)
  {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    return 4;
  }
  SDL_GL_MakeCurrent(pWindow, glContext);
  SDL_GL_SetSwapInterval(bOffscreen ? 0 : 1);

  glewExperimental = GL_TRUE; // required for core profile contexts
  glewInit(); // may return an error on core profiles after successful init; harmless
  glGetError(); // clear the benign error glewInit can leave behind

  const GLuint program = CreateProgram();

  if (bScreenshot)
  {
    EnsureGpuData(app);
    const int iResult = RunScreenshot(app, opt, pWindow, program);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(pWindow);
    SDL_Quit();
    return iResult;
  }

  if (bAtlas)
  {
    EnsureGpuData(app);
    const int iResult = RunAtlas(app, opt, pWindow, program);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(pWindow);
    SDL_Quit();
    return iResult;
  }

  // interactive mode
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(pWindow, glContext);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  bool bRunning = true;
  while (bRunning)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        bRunning = false;

      // orbit camera (only when imgui doesn't want the mouse)
      ImGuiIO& io = ImGui::GetIO();
      if (!io.WantCaptureMouse)
      {
        if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK))
        {
          app.m_Camera.m_fYaw += event.motion.xrel * 0.01f;
          app.m_Camera.m_fPitch = aeMath::Clamp(app.m_Camera.m_fPitch + event.motion.yrel * 0.01f, -1.5f, 1.5f);
        }
        if (event.type == SDL_MOUSEWHEEL)
        {
          app.m_Camera.m_fDistance = aeMath::Max(0.5f, app.m_Camera.m_fDistance - event.wheel.y * app.m_Camera.m_fDistance * 0.1f);
        }
      }
    }

    EnsureGpuData(app);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Kraut Preview");
    ImGui::Text("Triangles: %u", app.m_Tree.GetNumTriangles());
    ImGui::InputInt("Seed", &app.m_iSeed);

    // LOD combo: only full-mesh LODs are selectable
    const char* szLodPreview = LOD_NAMES[app.m_iLod];
    if (ImGui::BeginCombo("LOD", szLodPreview))
    {
      for (int i = 0; i < (int)LOD_COUNT; ++i)
      {
        const bool bMeshMode = (app.m_TreeFile.m_Lods[i].m_Mode == Kraut::LodMode::Full);
        if (!bMeshMode)
          continue;
        if (ImGui::Selectable(LOD_NAMES[i], app.m_iLod == i))
          app.m_iLod = i;
      }
      ImGui::EndCombo();
    }

    ImGui::Checkbox("Wireframe", &app.m_bWireframe);

    if (ImGui::Button("Regenerate"))
      Regenerate(app);

    ImGui::End();

    int iWidth, iHeight;
    SDL_GetWindowSize(pWindow, &iWidth, &iHeight);

    ImGui::Render();
    {
      const Mat4 proj = Mat4::Perspective(50.0f * 3.14159265f / 180.0f, (float)iWidth / (float)iHeight, 0.05f, 500.0f);
      const Mat4 view = app.m_Camera.View();
      const float clearColor[4] = {0.65f, 0.72f, 0.80f, 1.0f};
      float fwd[3], right[3], up[3];
      app.m_Camera.Basis(fwd, right, up);
      DrawScene(app, program, iWidth, iHeight, proj, view, clearColor, fwd, right, up);
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(pWindow);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(pWindow);
  SDL_Quit();
  return 0;
}
