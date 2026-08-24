#pragma once

// Wavefront OBJ export for generated tree meshes.
// Ported from TreePlugin/Mesh/Export/ExportOBJ.cpp, simplified:
// positions are scaled by the file's export scale, texture coordinates use the
// perspective divide (u/z, v/z), one group + usemtl per branch/geometry-type.

#include "Pipeline.h"

namespace KrautCLI
{
  using namespace AE_NS_FOUNDATION;
  // Stable names for branch types / geometry types (same naming as the editor's OBJ export).
  const char* GetBranchTypeName(aeUInt32 uiType);
  const char* GetGeometryTypeName(aeUInt32 uiType);

  // Writes the mesh as OBJ. Returns false and sets out_sError on failure.
  bool ExportObj(const TreeFile& treeFile, const GeneratedTree& tree, const char* szPath, aeString& out_sError);
} // namespace KrautCLI
