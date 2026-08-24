#pragma once

// Full-fidelity reader/writer for the editor's .tree file format (version 18).
//
// The on-disk format is aeTreeDescriptor::Save/Load (Code/Engine/TreePlugin/Tree/TreeDescriptor.cpp):
// it embeds the KrautGenerator serializer block (TreeStructureDesc + LODs 0..4) followed by
// legacy sections (all 6 LodDescs, all SpawnNodeDescs) and editor extras (AO grid, forces, ...).
// This implementation round-trips every section, so CLI-saved files open in the official editor.

#include <KrautFoundation/Math/Matrix.h>
#include <KrautFoundation/Math/Vec3.h>
#include <KrautFoundation/Strings/String.h>
#include <KrautGenerator/Description/LodDesc.h>
#include <KrautGenerator/Description/TreeStructureDesc.h>
#include <vector>

namespace KrautCLI
{
  using namespace AE_NS_FOUNDATION;

  // LOD slot count: index 0 is the editor's aeLod::None (full detail), 1..5 are Lod0..Lod4.
  constexpr aeUInt32 LOD_COUNT = 6;

  struct AoGridData
  {
    aeUInt8 m_uiVersion = 0;
    aeVec3 m_vBBoxMin = aeVec3::ZeroVector();
    aeVec3 m_vBBoxMax = aeVec3::ZeroVector();
    aeUInt32 m_uiCells[3] = {0, 0, 0};
    float m_fCellSize = 0.0f;
    std::vector<float> m_CellData; // 6 floats per cell
  };

  struct ForceData
  {
    aeString m_sName;
    bool m_bActive = true;
    aeInt8 m_iType = 0; // 0 = Position, 1 = Direction, 2 = Mesh
    aeUInt32 m_uiInfluencedBranchTypes = 0;
    float m_fStrength = 0.0f;
    float m_fMinRadius = 0.0f;
    float m_fMaxRadius = 0.0f;
    aeMatrix m_Transform;
    aeInt8 m_iFalloff = 0; // 0 = None, 1 = Linear, 2 = Quadratic, 3 = Hard
    aeString m_sMesh;
  };

  struct TreeFile
  {
    Kraut::TreeStructureDesc m_StructureDesc;
    Kraut::LodDesc m_Lods[LOD_COUNT]; // [0] = full detail ("None"), [1..5] = Lod0..Lod4

    // editor extras (round-tripped verbatim)
    bool m_bSnapshotFromAbove = false;
    float m_fBBoxAdjustment = 0.0f;
    aeInt8 m_iImpostorResolution = 2;
    aeUInt8 m_uiLeafCardTexelDilation = 4;
    aeUInt8 m_uiLeafCardResolution = 1; // aeLeafCardResolution::Tex512
    aeUInt8 m_uiLeafCardMipmaps = 0;    // aeLeafCardMipmapResolution::None
    bool m_bUseAO = true;
    float m_fAOSampleSize = 0.3f;
    float m_fAOContrast = 1.0f;
    float m_fExportScale = 1.0f;
    float m_fLodCrossFadeTransition = 0.1f;

    AoGridData m_AoGrid;
    std::vector<ForceData> m_Forces;

    // Loads a .tree file. Only format version 18 (current) is supported.
    // Returns false and sets out_sError on failure.
    bool Load(const char* szPath, aeString& out_sError);

    // Saves a .tree file in exactly the format the editor writes (version 18).
    // Note: mirrors the editor's Save, including LodDesc clamping between the
    // serializer block and the legacy section.
    bool Save(const char* szPath, aeString& out_sError);

  private:
    void ClampLodValues();
  };
} // namespace KrautCLI
