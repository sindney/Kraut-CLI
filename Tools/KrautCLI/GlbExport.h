#pragma once

// glTF-binary (glb) export for the fury3d engine (companion change:
// fury3d/openspec/changes/add-kraut-vegetation). One glb per tree:
// per-LOD material-grouped meshes named <tree>_LOD<n>, a billboard quad
// <tree>_Billboard, COLOR_0 wind weights, PBR materials with external
// texture URIs, and asset.extras.kraut (lod thresholds, billboard atlas
// grid, generation provenance). See openspec/changes/add-glb-export.

#include "Pipeline.h"

#include <string>
#include <vector>

namespace KrautCLI
{
  struct GlbTextureRef
  {
    std::string m_Uri;          // basename written into the glb
    std::string m_ResolvedPath; // source file the caller should copy (empty when unresolved)
  };

  struct GlbExportResult
  {
    std::vector<GlbTextureRef> m_Textures; // unique textures to copy next to the glb
    std::vector<std::string> m_Warnings;
    aeUInt32 m_uiLodCount = 0;   // mesh LOD tiers (excl. billboard)
    aeUInt32 m_uiTriangles = 0;  // total across all tiers
  };

  struct GlbExportOptions
  {
    aeUInt32 m_uiSeed = 0;
    aeUInt32 m_uiAtlasCols = 8;
    std::string m_TreeName;       // descriptor stem, used for node names
    std::string m_DescriptorFile; // descriptor basename (provenance)
    std::string m_DescriptorDir;  // descriptor directory (texture resolution)
  };

  // Exports all full-mesh LODs + billboard to one glb at szPath.
  bool ExportGlb(TreeFile& treeFile, const GlbExportOptions& opts,
    const char* szPath, GlbExportResult& out_Result, aeString& out_sError);
} // namespace KrautCLI
