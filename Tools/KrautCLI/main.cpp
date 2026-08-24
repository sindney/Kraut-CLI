// KrautCLI - headless command line interface for Kraut tree generation.
//
// Usage:
//   KrautCLI info <file.tree> [--json]
//   KrautCLI generate <file.tree> [--seed N] [--lod none|0|1|2|3|4] --out <file.obj> [--json]
//   KrautCLI export <file.tree> [--seed N] --format obj|kraut --out <path> [--json]
//   KrautCLI roundtrip <in.tree> <out.tree> [--json]
//
// Exit codes: 0 = success, 1 = usage error, 2 = load failure, 3 = generation failure, 4 = export failure.

#include "KrautExport.h"
#include "ObjExport.h"
#include "Pipeline.h"
#include "TreeFile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
  using namespace KrautCLI;

  const char* const LOD_NAMES[LOD_COUNT] = {"full", "lod0", "lod1", "lod2", "lod3", "lod4"};

  struct Args
  {
    const char* m_szCommand = nullptr;
    const char* m_szInput = nullptr;
    const char* m_szInput2 = nullptr; // roundtrip output path
    const char* m_szOut = nullptr;
    const char* m_szFormat = nullptr;
    aeUInt32 m_uiSeed = 0;
    bool m_bSeedGiven = false;
    aeUInt32 m_uiLod = 0; // 0 = full detail
    bool m_bJson = false;
  };

  void PrintUsage()
  {
    std::printf(
      "KrautCLI - headless Kraut tree generation\n"
      "\n"
      "Usage:\n"
      "  KrautCLI info <file.tree> [--json]\n"
      "  KrautCLI generate <file.tree> [--seed N] [--lod none|0|1|2|3|4] --out <file.obj> [--json]\n"
      "  KrautCLI export <file.tree> [--seed N] --format obj|kraut --out <path> [--json]\n"
      "  KrautCLI roundtrip <in.tree> <out.tree> [--json]\n");
  }

  bool ParseLod(const char* sz, aeUInt32& out_uiLod)
  {
    if (std::strcmp(sz, "none") == 0 || std::strcmp(sz, "full") == 0)
    {
      out_uiLod = 0;
      return true;
    }
    if (sz[0] >= '0' && sz[0] <= '4' && sz[1] == '\0')
    {
      out_uiLod = 1 + (sz[0] - '0');
      return true;
    }
    return false;
  }

  bool ParseArgs(int argc, char** argv, Args& args)
  {
    if (argc < 2)
      return false;

    args.m_szCommand = argv[1];

    // first positional argument = input file
    int iPositional = 0;
    for (int i = 2; i < argc; ++i)
    {
      const char* sz = argv[i];

      if (std::strcmp(sz, "--json") == 0)
        args.m_bJson = true;
      else if (std::strcmp(sz, "--seed") == 0 && i + 1 < argc)
      {
        args.m_uiSeed = (aeUInt32)std::strtoul(argv[++i], nullptr, 10);
        args.m_bSeedGiven = true;
      }
      else if (std::strcmp(sz, "--lod") == 0 && i + 1 < argc)
      {
        if (!ParseLod(argv[++i], args.m_uiLod))
          return false;
      }
      else if (std::strcmp(sz, "--out") == 0 && i + 1 < argc)
        args.m_szOut = argv[++i];
      else if (std::strcmp(sz, "--format") == 0 && i + 1 < argc)
        args.m_szFormat = argv[++i];
      else if (sz[0] != '-' && iPositional == 0)
      {
        args.m_szInput = sz;
        ++iPositional;
      }
      else if (sz[0] != '-' && iPositional == 1)
      {
        args.m_szInput2 = sz;
        ++iPositional;
      }
      else
        return false;
    }

    return (args.m_szInput != nullptr);
  }

  void JsonEscapeAndPrint(const char* sz)
  {
    for (const char* p = sz; *p; ++p)
    {
      if (*p == '\\' || *p == '"')
        std::printf("\\%c", *p);
      else
        std::printf("%c", *p);
    }
  }

  void PrintInfoHuman(const TreeFile& tf, const char* szPath)
  {
    std::printf("File: %s\n", szPath);
    std::printf("Random seed: %u\n", tf.m_StructureDesc.m_uiRandomSeed);
    std::printf("Leaf card mode: %s\n", tf.m_StructureDesc.m_bLeafCardMode ? "yes" : "no");
    std::printf("Procedural trunks: %s\n", tf.m_StructureDesc.m_bGrowProceduralTrunks ? "yes" : "no");
    std::printf("Export scale: %g\n", tf.m_fExportScale);
    std::printf("Forces: %u\n", (unsigned int)tf.m_Forces.size());
    std::printf("AO grid cells: %u\n", (unsigned int)(tf.m_AoGrid.m_CellData.size() / 6));

    std::printf("Branch types:\n");
    for (aeUInt32 bt = 0; bt < Kraut::BranchType::ENUM_COUNT; ++bt)
    {
      const Kraut::SpawnNodeDesc& sn = tf.m_StructureDesc.m_BranchTypes[bt];
      if (!sn.m_bUsed)
        continue;

      std::printf("  %-15s branches %u..%u, length %u..%u cm, geometry [", GetBranchTypeName(bt), sn.m_uiMinBranches, sn.m_uiMaxBranches, sn.m_uiMinBranchLengthInCM, sn.m_uiMaxBranchLengthInCM);
      for (aeUInt32 gt = 0; gt < Kraut::BranchGeometryType::ENUM_COUNT; ++gt)
      {
        if (sn.m_bEnable[gt])
          std::printf("%s ", GetGeometryTypeName(gt));
      }
      std::printf("]\n");
    }

    std::printf("LODs:\n");
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      const Kraut::LodDesc& ld = tf.m_Lods[lod];
      std::printf("  %-5s mode %d, distance %u m, tip detail %g, vertex ring detail %g\n", LOD_NAMES[lod], (int)ld.m_Mode, ld.m_uiLodDistance, ld.m_fTipDetail, ld.m_fVertexRingDetail);
    }
  }

  void PrintInfoJson(const TreeFile& tf, const char* szPath)
  {
    std::printf("{\n  \"file\": \"");
    JsonEscapeAndPrint(szPath);
    std::printf("\",\n  \"randomSeed\": %u,\n", tf.m_StructureDesc.m_uiRandomSeed);
    std::printf("  \"leafCardMode\": %s,\n", tf.m_StructureDesc.m_bLeafCardMode ? "true" : "false");
    std::printf("  \"growProceduralTrunks\": %s,\n", tf.m_StructureDesc.m_bGrowProceduralTrunks ? "true" : "false");
    std::printf("  \"exportScale\": %g,\n", tf.m_fExportScale);
    std::printf("  \"forceCount\": %u,\n", (unsigned int)tf.m_Forces.size());
    std::printf("  \"aoGridCells\": %u,\n", (unsigned int)(tf.m_AoGrid.m_CellData.size() / 6));

    std::printf("  \"branchTypes\": [");
    bool bFirst = true;
    for (aeUInt32 bt = 0; bt < Kraut::BranchType::ENUM_COUNT; ++bt)
    {
      const Kraut::SpawnNodeDesc& sn = tf.m_StructureDesc.m_BranchTypes[bt];
      if (!sn.m_bUsed)
        continue;

      if (!bFirst)
        std::printf(",");
      bFirst = false;

      std::printf("\n    {\"name\": \"%s\", \"minBranches\": %u, \"maxBranches\": %u, \"minLengthCM\": %u, \"maxLengthCM\": %u, \"geometry\": [",
        GetBranchTypeName(bt), sn.m_uiMinBranches, sn.m_uiMaxBranches, sn.m_uiMinBranchLengthInCM, sn.m_uiMaxBranchLengthInCM);

      bool bFirstGeo = true;
      for (aeUInt32 gt = 0; gt < Kraut::BranchGeometryType::ENUM_COUNT; ++gt)
      {
        if (sn.m_bEnable[gt])
        {
          if (!bFirstGeo)
            std::printf(", ");
          bFirstGeo = false;
          std::printf("\"%s\"", GetGeometryTypeName(gt));
        }
      }
      std::printf("]}");
    }
    std::printf("\n  ],\n  \"lods\": [");
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      const Kraut::LodDesc& ld = tf.m_Lods[lod];
      std::printf("%s\n    {\"name\": \"%s\", \"mode\": %d, \"distance\": %u, \"tipDetail\": %g, \"vertexRingDetail\": %g}",
        lod == 0 ? "" : ",", LOD_NAMES[lod], (int)ld.m_Mode, ld.m_uiLodDistance, ld.m_fTipDetail, ld.m_fVertexRingDetail);
    }
    std::printf("\n  ]\n}\n");
  }

  int Fail(int iCode, const char* szWhat, const aeString& sError, bool bJson)
  {
    if (bJson)
    {
      std::printf("{\n  \"error\": \"");
      JsonEscapeAndPrint(sError.c_str());
      std::printf("\"\n}\n");
    }
    else
    {
      std::fprintf(stderr, "Error (%s): %s\n", szWhat, sError.c_str());
    }
    return iCode;
  }

  // Changes the extension of a path (buffer must have room).
  void ChangeExtension(char* szPath, size_t uiBufferSize, const char* szNewExt)
  {
    char* pDot = std::strrchr(szPath, '.');
    char* pSlash = std::strrchr(szPath, '/');
    char* pBackslash = std::strrchr(szPath, '\\');
    if (pDot && (pSlash == nullptr || pDot > pSlash) && (pBackslash == nullptr || pDot > pBackslash))
      *pDot = '\0';
    std::strncat(szPath, ".", uiBufferSize - std::strlen(szPath) - 1);
    std::strncat(szPath, szNewExt, uiBufferSize - std::strlen(szPath) - 1);
  }

  // Removes the extension and appends a suffix + new extension: "a/b.obj" + "_LOD0" -> "a/b_LOD0.obj"
  void MakeLodPath(const char* szBase, const char* szSuffix, char* out_Path, size_t uiBufferSize)
  {
    std::strncpy(out_Path, szBase, uiBufferSize - 1);
    out_Path[uiBufferSize - 1] = '\0';

    char* pDot = std::strrchr(out_Path, '.');
    if (pDot)
      *pDot = '\0';

    std::strncat(out_Path, szSuffix, uiBufferSize - std::strlen(out_Path) - 1);
    std::strncat(out_Path, ".", uiBufferSize - std::strlen(out_Path) - 1);

    const char* pExt = std::strrchr(szBase, '.');
    std::strncat(out_Path, pExt ? pExt + 1 : "obj", uiBufferSize - std::strlen(out_Path) - 1);
  }
} // namespace

int main(int argc, char** argv)
{
  Args args;
  if (!ParseArgs(argc, argv, args))
  {
    PrintUsage();
    return 1;
  }

  aeString sError;
  TreeFile tf;

  if (!tf.Load(args.m_szInput, sError))
    return Fail(2, "load", sError, args.m_bJson);

  const aeUInt32 uiSeed = args.m_bSeedGiven ? args.m_uiSeed : tf.m_StructureDesc.m_uiRandomSeed;

  if (std::strcmp(args.m_szCommand, "info") == 0)
  {
    if (args.m_bJson)
      PrintInfoJson(tf, args.m_szInput);
    else
      PrintInfoHuman(tf, args.m_szInput);
    return 0;
  }

  if (std::strcmp(args.m_szCommand, "roundtrip") == 0)
  {
    if (args.m_szInput2 == nullptr)
    {
      PrintUsage();
      return 1;
    }

    if (!tf.Save(args.m_szInput2, sError))
      return Fail(4, "save", sError, args.m_bJson);

    if (args.m_bJson)
      std::printf("{\n  \"saved\": true\n}\n");
    else
      std::printf("Saved: %s\n", args.m_szInput2);
    return 0;
  }

  if (std::strcmp(args.m_szCommand, "generate") == 0)
  {
    if (args.m_szOut == nullptr)
    {
      PrintUsage();
      return 1;
    }

    BuildInfluencesFromForces(tf);

    GeneratedTree tree;
    if (!GenerateTree(tf, uiSeed, args.m_uiLod, tree, sError))
      return Fail(3, "generate", sError, args.m_bJson);

    if (!ExportObj(tf, tree, args.m_szOut, sError))
      return Fail(4, "export", sError, args.m_bJson);

    if (args.m_bJson)
    {
      std::printf("{\n  \"out\": \"");
      JsonEscapeAndPrint(args.m_szOut);
      std::printf("\",\n  \"seed\": %u,\n  \"lod\": \"%s\",\n  \"branches\": %u,\n  \"triangles\": %u,\n  \"bboxMin\": [%g, %g, %g],\n  \"bboxMax\": [%g, %g, %g]\n}\n",
        uiSeed, LOD_NAMES[args.m_uiLod], (unsigned int)tree.m_Structure.m_BranchStructures.size(), tree.GetNumTriangles(),
        tree.m_BBox.m_vMin.x, tree.m_BBox.m_vMin.y, tree.m_BBox.m_vMin.z,
        tree.m_BBox.m_vMax.x, tree.m_BBox.m_vMax.y, tree.m_BBox.m_vMax.z);
    }
    else
    {
      std::printf("Generated %u branches, %u triangles (seed %u, lod %s)\n",
        (unsigned int)tree.m_Structure.m_BranchStructures.size(), tree.GetNumTriangles(), uiSeed, LOD_NAMES[args.m_uiLod]);
      std::printf("Wrote: %s\n", args.m_szOut);
    }
    return 0;
  }

  if (std::strcmp(args.m_szCommand, "export") == 0)
  {
    if (args.m_szOut == nullptr || args.m_szFormat == nullptr)
    {
      PrintUsage();
      return 1;
    }

    BuildInfluencesFromForces(tf);

    if (std::strcmp(args.m_szFormat, "obj") == 0)
    {
      // export every full-mesh LOD as its own OBJ (mirrors the editor's naming)
      for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
      {
        if (tf.m_Lods[lod].m_Mode != Kraut::LodMode::Full)
          continue;

        GeneratedTree tree;
        if (!GenerateTree(tf, uiSeed, lod, tree, sError))
          return Fail(3, "generate", sError, args.m_bJson);

        char szPath[1024];
        if (lod == 0)
        {
          std::strncpy(szPath, args.m_szOut, sizeof(szPath) - 1);
          szPath[sizeof(szPath) - 1] = '\0';
        }
        else
        {
          char szSuffix[16];
          std::snprintf(szSuffix, sizeof(szSuffix), "_LOD%u", lod - 1);
          MakeLodPath(args.m_szOut, szSuffix, szPath, sizeof(szPath));
        }

        if (!ExportObj(tf, tree, szPath, sError))
          return Fail(4, "export", sError, args.m_bJson);

        if (!args.m_bJson)
          std::printf("Wrote: %s (%u triangles)\n", szPath, tree.GetNumTriangles());
      }

      if (args.m_bJson)
        std::printf("{\n  \"format\": \"obj\",\n  \"seed\": %u\n}\n");
      return 0;
    }

    if (std::strcmp(args.m_szFormat, "kraut") == 0)
    {
      aeUInt32 uiSkippedLods = 0;
      if (!ExportKraut(tf, uiSeed, args.m_szOut, uiSkippedLods, sError))
        return Fail(4, "export", sError, args.m_bJson);

      if (!ValidateKrautFile(args.m_szOut, sError))
        return Fail(4, "validate", sError, args.m_bJson);

      if (args.m_bJson)
      {
        std::printf("{\n  \"out\": \"");
        JsonEscapeAndPrint(args.m_szOut);
        std::printf("\",\n  \"format\": \"kraut\",\n  \"seed\": %u,\n  \"skippedLods\": %u,\n  \"validated\": true\n}\n", uiSeed, uiSkippedLods);
      }
      else
      {
        std::printf("Wrote: %s (validated, skipped LOD mask 0x%X)\n", args.m_szOut, uiSkippedLods);
      }
      return 0;
    }

    std::fprintf(stderr, "Unknown format: %s (valid: obj, kraut)\n", args.m_szFormat);
    return 1;
  }

  PrintUsage();
  return 1;
}
