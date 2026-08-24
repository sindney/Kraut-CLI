#pragma once

// Headless tree generation pipeline:
//   TreeFile -> TreeStructureGenerator -> TreeStructureLodGenerator -> TreeMeshGenerator
//
// Mirrors aeTree::GenerateTree / EnsureSkeletonIsGenerated / EnsureMeshIsGenerated from
// the editor (Code/Engine/TreePlugin/Tree/Tree.cpp), with two documented differences:
//  - physics simulation (Bullet) is replaced by Kraut::Physics_EmptyImpl
//  - impostor LOD modes (FourQuads/TwoQuads/Billboard) cannot be meshed headlessly

#include "TreeFile.h"
#include <KrautGenerator/Infrastructure/BoundingBox.h>
#include <KrautGenerator/Lod/TreeStructureLod.h>
#include <KrautGenerator/Mesh/TreeMesh.h>
#include <KrautGenerator/TreeStructure/TreeStructure.h>

namespace KrautCLI
{
  using namespace AE_NS_FOUNDATION;
  struct GeneratedTree
  {
    Kraut::TreeStructure m_Structure;
    Kraut::TreeStructureLod m_StructureLod;
    Kraut::TreeMesh m_Mesh;
    Kraut::BoundingBox m_BBox;

    aeUInt32 GetNumTriangles() const { return m_Mesh.GetNumTriangles(); }
  };

  // Rebuilds TreeStructureDesc::m_Influences from the file's force list,
  // mirroring aeTree::ConfigureInfluences. Mesh-type forces are skipped
  // (they require the editor's octree sampling).
  void BuildInfluencesFromForces(TreeFile& treeFile);

  // Runs the full pipeline for one LOD slot (0 = full detail, 1..5 = Lod0..Lod4).
  // uiSeed: if bUseFileSeed is false, overrides the descriptor's random seed.
  // Returns false (with error message) if the LOD uses an impostor mode.
  bool GenerateTree(TreeFile& treeFile, aeUInt32 uiSeed, aeUInt32 uiLodSlot, GeneratedTree& out_Tree, aeString& out_sError);

  // Computes the tree bounding box the same way the editor does
  // (structure bbox + size increase + user adjustment, min.y clamped to 0).
  Kraut::BoundingBox ComputeTreeBoundingBox(const TreeFile& treeFile, const Kraut::TreeStructure& structure);
} // namespace KrautCLI
