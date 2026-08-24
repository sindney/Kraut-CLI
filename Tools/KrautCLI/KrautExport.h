#pragma once

// Native .kraut (baked mesh) export, ported from TreePlugin/Mesh/Export/TreeExport.cpp.
//
// Documented differences to the editor export:
//  - per-vertex ambient occlusion is written as 1.0 (AO baking needs the editor's renderer)
//  - impostor LOD modes (FourQuads/TwoQuads/Billboard) are skipped (need the editor's impostor renderer)
//  - material diffuse texture names are the descriptor's material names directly
//    (no material library resolution), normal maps are written as empty strings
//
// Note: the legacy viewer reader (Code/KrautViewer/KrautImport.cpp) only parses format v1;
// the editor writes v2 (adds per-vertex AO). ValidateKrautFile below implements the v2 read side
// and is used for self-validation.

#include "Pipeline.h"

namespace KrautCLI
{
  using namespace AE_NS_FOUNDATION;
  // Exports all full-mesh LODs (slots 0..5 with LodMode::Full) to a .kraut file.
  // out_uiSkippedLods is a bitmask of LOD slots that were skipped (impostor/disabled modes).
  bool ExportKraut(TreeFile& treeFile, aeUInt32 uiSeed, const char* szPath, aeUInt32& out_uiSkippedLods, aeString& out_sError);

  // Structural self-validation of a written .kraut file (v2 reader).
  bool ValidateKrautFile(const char* szPath, aeString& out_sError);
} // namespace KrautCLI
