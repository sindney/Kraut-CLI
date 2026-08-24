#include "Pipeline.h"

#include <KrautGenerator/Description/Influence.h>
#include <KrautGenerator/Description/Physics.h>
#include <KrautGenerator/Lod/TreeStructureLodGenerator.h>
#include <KrautGenerator/Mesh/TreeMeshGenerator.h>
#include <KrautGenerator/TreeStructure/TreeStructureGenerator.h>

namespace KrautCLI
{
  void BuildInfluencesFromForces(TreeFile& treeFile)
  {
    // Ported from aeTree::ConfigureInfluences (TreePlugin/Tree/Tree.cpp).
    treeFile.m_StructureDesc.m_Influences.clear();

    for (const ForceData& force : treeFile.m_Forces)
    {
      if (!force.m_bActive)
        continue;

      if (force.m_iType == 0) // aeForceType::Position
      {
        treeFile.m_StructureDesc.m_Influences.push_back();
        treeFile.m_StructureDesc.m_Influences.back() = std::make_unique<Kraut::Influence_Position>();

        auto* pInfluence = static_cast<Kraut::Influence_Position*>(treeFile.m_StructureDesc.m_Influences.back().get());
        pInfluence->m_vPosition = force.m_Transform.GetTranslationVector();
        pInfluence->m_AffectedBranchTypes = force.m_uiInfluencedBranchTypes;
        pInfluence->m_fMinRadius = force.m_fMinRadius;
        pInfluence->m_fMaxRadius = force.m_fMaxRadius;
        pInfluence->m_fStrength = force.m_fStrength;
        pInfluence->m_Falloff = (Kraut::Influence_Position::Falloff::Enum)force.m_iFalloff;
      }
      else if (force.m_iType == 1) // aeForceType::Direction
      {
        treeFile.m_StructureDesc.m_Influences.push_back();
        treeFile.m_StructureDesc.m_Influences.back() = std::make_unique<Kraut::Influence_Direction>();

        auto* pInfluence = static_cast<Kraut::Influence_Direction*>(treeFile.m_StructureDesc.m_Influences.back().get());
        pInfluence->m_vPosition = force.m_Transform.GetTranslationVector();
        pInfluence->m_vDirection = force.m_Transform.TransformDirection(aeVec3(0, 1, 0));
        pInfluence->m_AffectedBranchTypes = force.m_uiInfluencedBranchTypes;
        pInfluence->m_fMinRadius = force.m_fMinRadius;
        pInfluence->m_fMaxRadius = force.m_fMaxRadius;
        pInfluence->m_fStrength = force.m_fStrength;
        pInfluence->m_Falloff = (Kraut::Influence_Position::Falloff::Enum)force.m_iFalloff;
      }
      // aeForceType::Mesh (2) requires editor octree sampling - skipped.
    }
  }

  Kraut::BoundingBox ComputeTreeBoundingBox(const TreeFile& treeFile, const Kraut::TreeStructure& structure)
  {
    // Ported from aeTree::ComputeBoundingBox (TreePlugin/Tree/Tree.cpp).
    Kraut::BoundingBox bbox = structure.ComputeBoundingBox();
    bbox.AddBoundary(aeVec3(treeFile.m_StructureDesc.GetBoundingBoxSizeIncrease()));
    bbox.AddBoundary(aeVec3(treeFile.m_fBBoxAdjustment));
    bbox.m_vMin.y = 0.0f;
    return bbox;
  }

  bool GenerateTree(TreeFile& treeFile, aeUInt32 uiSeed, aeUInt32 uiLodSlot, GeneratedTree& out_Tree, aeString& out_sError)
  {
    if (uiLodSlot >= LOD_COUNT)
    {
      out_sError = "Invalid LOD slot (valid: 0 = full detail, 1..5 = Lod0..Lod4).";
      return false;
    }

    const Kraut::LodDesc& lodDesc = treeFile.m_Lods[uiLodSlot];

    if (uiLodSlot != 0 && !Kraut::LodMode::IsMeshMode(lodDesc.m_Mode))
    {
      out_sError = "The requested LOD uses an impostor mode (FourQuads/TwoQuads/Billboard/Disabled), which cannot be generated headlessly. Use a full-mesh LOD.";
      return false;
    }

    // 1. structure
    Kraut::TreeStructureGenerator structureGen;
    structureGen.m_pTreeStructureDesc = &treeFile.m_StructureDesc;
    structureGen.m_pTreeStructure = &out_Tree.m_Structure;
    structureGen.m_pPhysics = nullptr; // generator falls back to Physics_EmptyImpl
    structureGen.GenerateTreeStructure(uiSeed);

    // 2. LOD structure
    Kraut::TreeStructureLodGenerator lodGen;
    lodGen.m_pTreeStructureLod = &out_Tree.m_StructureLod;
    lodGen.m_pTreeStructure = &out_Tree.m_Structure;
    lodGen.m_pTreeStructureDesc = &treeFile.m_StructureDesc;
    if (uiLodSlot != 0)
      lodGen.m_pLodDesc = &lodDesc; // nullptr = full detail, as in the editor
    lodGen.GenerateTreeStructureLod();

    // 3. mesh
    Kraut::TreeMeshGenerator meshGen;
    meshGen.m_pTreeStructureDesc = &treeFile.m_StructureDesc;
    meshGen.m_pTreeStructure = &out_Tree.m_Structure;
    meshGen.m_pTreeStructureLod = &out_Tree.m_StructureLod;
    meshGen.m_pLodDesc = &lodDesc;
    meshGen.m_pTreeMesh = &out_Tree.m_Mesh;
    meshGen.GenerateTreeMesh();

    out_Tree.m_BBox = ComputeTreeBoundingBox(treeFile, out_Tree.m_Structure);

    return true;
  }
} // namespace KrautCLI
