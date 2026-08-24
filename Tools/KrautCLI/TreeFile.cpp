#include "TreeFile.h"

#include "FileStreams.h"
#include <KrautFoundation/Math/Math.h>
#include <KrautGenerator/Serialization/SerializeTree.h>

namespace KrautCLI
{
  // Ported from TreePlugin/Tree/TreeDescriptor.cpp (ResetLodDesc) - default LOD values per slot.
  static void ResetLodDesc(Kraut::LodDesc& desc, aeUInt32 uiLod)
  {
    desc.m_Mode = Kraut::LodMode::Full;
    desc.m_fTipDetail = 0.04f;
    desc.m_fCurvatureThreshold = 5.0f;
    desc.m_fThicknessThreshold = 0.2f;
    desc.m_fVertexRingDetail = 0.2f;
    desc.m_iMaxFrondDetail = 32;
    desc.m_iFrondDetailReduction = 0;
    desc.m_uiLodDistance = 0;
    desc.m_BranchSpikeTipMode = Kraut::BranchSpikeTipMode::FullDetail;

    switch (uiLod)
    {
      case 0: // None (full detail)
        desc.m_fCurvatureThreshold = 0.0f;
        desc.m_fThicknessThreshold = 1.0f / 100.0f;
        desc.m_fTipDetail = 0.03f;
        desc.m_fVertexRingDetail = 0.04f;
        break;
      case 1: // Lod0
        desc.m_fCurvatureThreshold = 2.0f;
        desc.m_fThicknessThreshold = 5.0f / 100.0f;
        desc.m_fTipDetail = 0.04f;
        desc.m_fVertexRingDetail = 0.2f;
        desc.m_uiLodDistance = 10;
        break;
      case 2: // Lod1
        desc.m_fCurvatureThreshold = 5.0f;
        desc.m_fThicknessThreshold = 10.0f / 100.0f;
        desc.m_fTipDetail = 0.10f;
        desc.m_fVertexRingDetail = 0.4f;
        desc.m_iMaxFrondDetail = 6;
        desc.m_iFrondDetailReduction = 1;
        desc.m_uiLodDistance = 20;
        break;
      case 3: // Lod2
        desc.m_fCurvatureThreshold = 10.0f;
        desc.m_fThicknessThreshold = 15.0f / 100.0f;
        desc.m_fTipDetail = 0.20f;
        desc.m_fVertexRingDetail = 0.6f;
        desc.m_iMaxFrondDetail = 4;
        desc.m_iFrondDetailReduction = 2;
        desc.m_uiLodDistance = 30;
        desc.m_BranchSpikeTipMode = Kraut::BranchSpikeTipMode::SingleTriangle;
        break;
      case 4: // Lod3
        desc.m_Mode = Kraut::LodMode::TwoQuads;
        desc.m_fCurvatureThreshold = 15.0f;
        desc.m_fThicknessThreshold = 20.0f / 100.0f;
        desc.m_fTipDetail = 0.30f;
        desc.m_fVertexRingDetail = 0.8f;
        desc.m_iMaxFrondDetail = 2;
        desc.m_iFrondDetailReduction = 3;
        desc.m_uiLodDistance = 40;
        desc.m_BranchSpikeTipMode = Kraut::BranchSpikeTipMode::Hole;
        break;
      case 5: // Lod4
        desc.m_Mode = Kraut::LodMode::Disabled;
        desc.m_fCurvatureThreshold = 20.0f;
        desc.m_fThicknessThreshold = 50.0f / 200.0f;
        desc.m_fTipDetail = 0.40f;
        desc.m_fVertexRingDetail = 1.0f;
        desc.m_iMaxFrondDetail = 0;
        desc.m_iFrondDetailReduction = 4;
        desc.m_uiLodDistance = 1000;
        desc.m_BranchSpikeTipMode = Kraut::BranchSpikeTipMode::Hole;
        break;
    }
  }

  // Ported from TreePlugin/Tree/TreeDescriptor.cpp (PushLodSettingsDown/Up, ClampLodValues).
  static void PushLodSettingsDown(aeUInt32 uiStartLod, Kraut::LodDesc* pLodData)
  {
    for (aeUInt32 i = uiStartLod + 1; i < LOD_COUNT; ++i)
    {
      pLodData[i].m_Mode = (Kraut::LodMode::Enum)aeMath::Max((aeInt32)pLodData[i].m_Mode, (aeInt32)pLodData[i - 1].m_Mode);
      pLodData[i].m_Mode = aeMath::Clamp(pLodData[i].m_Mode, Kraut::LodMode::Full, Kraut::LodMode::Disabled);
      pLodData[i].m_uiLodDistance = aeMath::Max(pLodData[i].m_uiLodDistance, pLodData[i - 1].m_uiLodDistance);
      pLodData[i].m_fCurvatureThreshold = aeMath::Max(pLodData[i].m_fCurvatureThreshold, pLodData[i - 1].m_fCurvatureThreshold);
      pLodData[i].m_fThicknessThreshold = aeMath::Max(pLodData[i].m_fThicknessThreshold, pLodData[i - 1].m_fThicknessThreshold);
      pLodData[i].m_fTipDetail = aeMath::Max(pLodData[i].m_fTipDetail, pLodData[i - 1].m_fTipDetail);
      pLodData[i].m_fVertexRingDetail = aeMath::Max(pLodData[i].m_fVertexRingDetail, pLodData[i - 1].m_fVertexRingDetail);

      for (aeUInt32 mt = 0; mt < Kraut::BranchGeometryType::ENUM_COUNT; ++mt)
        pLodData[i].m_AllowTypes[mt] = (pLodData[i].m_AllowTypes[mt] & pLodData[i - 1].m_AllowTypes[mt]);

      pLodData[i].m_iMaxFrondDetail = aeMath::Min(pLodData[i].m_iMaxFrondDetail, pLodData[i - 1].m_iMaxFrondDetail);
      pLodData[i].m_iFrondDetailReduction = aeMath::Max(pLodData[i].m_iFrondDetailReduction, pLodData[i - 1].m_iFrondDetailReduction);
    }
  }

  static void PushLodSettingsUp(aeUInt32 uiStartLod, Kraut::LodDesc* pLodData)
  {
    for (aeInt32 i = (aeInt32)uiStartLod - 1; i >= 0; --i)
    {
      pLodData[i].m_Mode = (Kraut::LodMode::Enum)aeMath::Min((aeInt32)pLodData[i].m_Mode, (aeInt32)pLodData[i + 1].m_Mode);
      pLodData[i].m_Mode = aeMath::Clamp(pLodData[i].m_Mode, Kraut::LodMode::Full, Kraut::LodMode::Disabled);
      pLodData[i].m_uiLodDistance = aeMath::Min(pLodData[i].m_uiLodDistance, pLodData[i + 1].m_uiLodDistance);
      pLodData[i].m_fCurvatureThreshold = aeMath::Min(pLodData[i].m_fCurvatureThreshold, pLodData[i + 1].m_fCurvatureThreshold);
      pLodData[i].m_fThicknessThreshold = aeMath::Min(pLodData[i].m_fThicknessThreshold, pLodData[i + 1].m_fThicknessThreshold);
      pLodData[i].m_fTipDetail = aeMath::Min(pLodData[i].m_fTipDetail, pLodData[i + 1].m_fTipDetail);
      pLodData[i].m_fVertexRingDetail = aeMath::Min(pLodData[i].m_fVertexRingDetail, pLodData[i + 1].m_fVertexRingDetail);

      for (aeUInt32 mt = 0; mt < Kraut::BranchGeometryType::ENUM_COUNT; ++mt)
        pLodData[i].m_AllowTypes[mt] = (pLodData[i].m_AllowTypes[mt] | pLodData[i + 1].m_AllowTypes[mt]);

      pLodData[i].m_iMaxFrondDetail = aeMath::Max(pLodData[i].m_iMaxFrondDetail, pLodData[i + 1].m_iMaxFrondDetail);
      pLodData[i].m_iFrondDetailReduction = aeMath::Min(pLodData[i].m_iFrondDetailReduction, pLodData[i + 1].m_iFrondDetailReduction);
    }
  }

  void TreeFile::ClampLodValues()
  {
    // The editor guards this with an undo check; equivalent to clamping from aeLod::None.
    PushLodSettingsDown(0, m_Lods);
    PushLodSettingsUp(0, m_Lods);
  }

  bool TreeFile::Load(const char* szPath, aeString& out_sError)
  {
    cliFileIn s;
    if (!s.Open(szPath))
    {
      out_sError = "Could not open file for reading.";
      return false;
    }

    aeUInt32 uiVersion = 1;
    s >> uiVersion;

    if (uiVersion < 14)
    {
      out_sError = "Unsupported .tree version (files older than version 14 are not supported).";
      return false;
    }

    if (uiVersion >= 18)
    {
      // version 18 block: TreeStructureDesc + LODs 0..4 (slots 1..5 here)
      Kraut::Deserializer ts;
      ts.m_pTreeStructure = &m_StructureDesc;
      for (aeUInt32 i = 0; i < LOD_COUNT - 1; ++i)
        ts.m_LODs[i] = &m_Lods[i + 1]; // skip LOD 'None'

      if (!ts.Deserialize(s))
      {
        out_sError = "Invalid .tree file (Kraut deserializer rejected the descriptor block).";
        return false;
      }
    }

    s >> m_StructureDesc.m_uiRandomSeed;
    s >> m_StructureDesc.m_bLeafCardMode;

    // legacy section: all 6 LODs (overwrites the deserializer block values, as in the editor)
    for (aeUInt32 i = 0; i < LOD_COUNT; ++i)
    {
      ResetLodDesc(m_Lods[i], i);
      m_Lods[i].Deserialize(s);
    }
    ClampLodValues();

    // legacy section: branch types (SpawnNodeDesc::Deserialize handles all spawn versions internally)
    aeUInt8 uiCount = 0;
    s >> uiCount;
    for (aeUInt32 i = 0; i < uiCount; ++i)
    {
      if (i < Kraut::BranchType::ENUM_COUNT)
        m_StructureDesc.m_BranchTypes[i].Deserialize(s);
      else
      {
        Kraut::SpawnNodeDesc dummy;
        dummy.Deserialize(s);
      }
    }

    s >> m_StructureDesc.m_bGrowProceduralTrunks;

    // version 9
    {
      aeUInt32 uiDummy = 0;
      s >> m_bSnapshotFromAbove;
      s >> uiDummy;
    }

    s >> m_fBBoxAdjustment;                 // version 10
    s >> m_iImpostorResolution;             // version 11
    s >> m_uiLeafCardTexelDilation;         // version 12
    s >> m_uiLeafCardResolution;
    s >> m_uiLeafCardMipmaps;
    s >> m_bUseAO;                          // version 13
    s >> m_fAOSampleSize;
    s >> m_fAOContrast;

    // version 14: AO grid
    {
      s >> m_AoGrid.m_uiVersion;
      s >> m_AoGrid.m_vBBoxMin;
      s >> m_AoGrid.m_vBBoxMax;
      s >> m_AoGrid.m_uiCells[0];
      s >> m_AoGrid.m_uiCells[1];
      s >> m_AoGrid.m_uiCells[2];
      s >> m_AoGrid.m_fCellSize;

      aeUInt32 uiCells = 0;
      s >> uiCells;
      m_AoGrid.m_CellData.resize((size_t)uiCells * 6);
      for (aeUInt32 c = 0; c < uiCells * 6; ++c)
        s >> m_AoGrid.m_CellData[c];
    }

    if (uiVersion >= 15)
      s >> m_fExportScale; // version 15

    // version 16: forces
    if (uiVersion >= 16)
    {
      aeUInt32 uiForces = 0;
      s >> uiForces;
      m_Forces.resize(uiForces);

      for (aeUInt32 i = 0; i < uiForces; ++i)
      {
        ForceData& f = m_Forces[i];

        aeUInt8 uiForceVersion = 0;
        s >> uiForceVersion;
        s >> f.m_sName;
        s >> f.m_bActive;
        s >> f.m_iType;
        s >> f.m_uiInfluencedBranchTypes;
        s >> f.m_fStrength;
        s >> f.m_fMinRadius;
        s >> f.m_fMaxRadius;
        s >> f.m_Transform;

        if (uiForceVersion >= 2)
          s >> f.m_iFalloff;
        if (uiForceVersion >= 3)
          s >> f.m_sMesh;
      }
    }

    if (uiVersion >= 17)
      s >> m_fLodCrossFadeTransition; // version 17

    // note: Beta 3 files may carry trailing data (editor thumbnail etc.) that no reader consumes; ignored here.

    return true;
  }

  // Writes one SpawnNodeDesc in the **version 42** layout (the Beta 3 binary's format).
  // Version 43 (current engine) removed the three normal-map texture strings; a v42 reader
  // would desync on v43 data, so the CLI writes v42 with empty normal-map strings.
  // (Field order follows SpawnNodeDesc::Serialize, re-adding the v43-removed strings.)
  static void WriteSpawnNodeDescV42(aeStreamOut& s, const Kraut::SpawnNodeDesc& d)
  {
    const aeUInt32 uiVersion = 42;
    s << uiVersion;

    s << d.m_bUsed;

    const aeInt8 e = d.m_Type;
    s << e;

    s << d.m_uiMinBranches;
    s << d.m_uiMaxBranches;

    s << d.m_fNodeHeight;
    s << d.m_fNodeSpacingBefore;
    s << d.m_fNodeSpacingAfter;

    s << d.m_fBranchlessPartABS;

    s << d.m_fMaxRotationalDeviation;
    s << d.m_fBranchAngle;
    s << d.m_fMaxBranchAngleDeviation;

    aeUInt8 td = d.m_TargetDirection;
    s << td;
    s << d.m_fMaxTargetDirDeviation;

    s << d.m_fGrowMaxTargetDirDeviation;
    s << d.m_fGrowMaxDirChangePerSegment;

    s << d.m_fRoundnessFactor;

    const float fWindInfluence = 0.0f;
    s << fWindInfluence;

    s << d.m_fPhysicsLookAhead;
    s << d.m_fPhysicsEvasionAngle;

    // Version 5
    s << d.m_bAllowSubType[0];
    s << d.m_bAllowSubType[1];
    s << d.m_bAllowSubType[2];

    // Version 6
    const float fWindBendiness = 0.0f;
    s << fWindBendiness;

    // Version 7
    s << d.m_bEnable[Kraut::BranchGeometryType::Frond];

    // Version 8
    s << d.m_sTexture[Kraut::BranchGeometryType::Branch];

    // Version 9 (removed in 43): branch normal-map texture
    s << aeString("");

    // Version 10
    s << d.m_fBranchlessPartEndABS;

    // Version 11
    const bool bAlignSubsAtTip = true;
    s << bAlignSubsAtTip;

    // Version 12
    td = d.m_TargetDirection2;
    s << td;
    td = d.m_TargetDir2Uage;
    s << td;
    s << d.m_fTargetDir2Usage;

    // Version 13
    s << d.m_uiLowerBound;
    s << d.m_uiUpperBound;

    // Version 14
    s << d.m_uiMinBranchLengthInCM;
    s << d.m_uiMaxBranchLengthInCM;

    // Version 16
    s << d.m_bDoPhysicalSimulation;

    // Version 17
    s << (aeUInt8)d.m_FrondUpOrientation;
    s << d.m_uiMaxFrondOrientationDeviation;

    // Version 18
    s << d.m_iSegmentLengthCM;

    // Version 19
    s << d.m_bActAsObstacle;

    // Version 20
    s << d.m_bEnable[Kraut::BranchGeometryType::Branch];
    d.m_BranchContour.Serialize(s);

    // Version 21
    d.m_fBranchLengthScale.Serialize(s);

    // Version 23
    s << d.m_sTexture[Kraut::BranchGeometryType::Frond];
    // (removed in 43): frond normal-map texture
    s << aeString("");

    // Version 24
    s << d.m_uiNumFronds;
    d.m_FrondContour.Serialize(s);
    d.m_FrondWidth.Serialize(s);
    d.m_FrondHeight.Serialize(s);

    // Version 25
    s << d.m_uiFrondDetail;

    // Version 26
    s << (aeInt8)d.m_FrondContourMode;

    // Version 27
    s << d.m_bRestrictGrowthToFrondPlane;

    // Version 28
    d.m_MaxBranchLengthParentScale.Serialize(s);

    // Version 31
    s << d.m_bTargetDirRelative;

    // Version 32
    s << d.m_bEnable[Kraut::BranchGeometryType::Leaf];
    s << d.m_fLeafSize;
    s << d.m_sTexture[Kraut::BranchGeometryType::Leaf];
    // (removed in 43): leaf normal-map texture
    s << aeString("");

    // Version 33
    s << d.m_fLeafInterval;

    // Version 34
    d.m_LeafScale.Serialize(s);

    // Version 35
    s << d.m_uiFlares;
    s << d.m_fFlareWidth;
    d.m_FlareWidthCurve.Serialize(s);
    s << d.m_fFlareRotation;

    // Version 36
    s << d.m_bRotateTexCoords;

    // Version 37
    s << d.m_uiMinBranchThicknessInCM;
    s << d.m_uiMaxBranchThicknessInCM;

    // Version 38
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Frond][0];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Frond][1];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Frond][2];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Frond][3];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Leaf][0];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Leaf][1];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Leaf][2];
    s << d.m_uiVariationColor[Kraut::BranchGeometryType::Leaf][3];

    // Version 39
    s << d.m_fTextureRepeat;
    s << d.m_bAlignFrondsOnSurface;

    // Version 40
    s << d.m_bBillboardLeaves;

    // Version 41
    s << (aeInt8)d.m_BranchTypeMode;

    // Version 42
    s << d.m_fFrondWidth;
    s << d.m_fFrondHeight;
  }

  bool TreeFile::Save(const char* szPath, aeString& out_sError)
  {
    cliFileOut s;
    if (!s.Open(szPath))
    {
      out_sError = "Could not open file for writing.";
      return false;
    }

    // Always write **file version 16** (the last format the released Kraut Beta 3 binary reads).
    // v17 (adds m_fLodCrossFadeTransition) and v18 (adds the Kraut::Serializer block) are readable
    // by newer editors via version gates, but not by Beta 3. v16 is readable by everything.
    const aeUInt32 uiVersion = 16;
    s << uiVersion;

    s << m_StructureDesc.m_uiRandomSeed;
    s << m_StructureDesc.m_bLeafCardMode;

    ClampLodValues();

    for (aeUInt32 i = 0; i < LOD_COUNT; ++i)
      m_Lods[i].Serialize(s);

    const aeUInt8 uiCount = Kraut::BranchType::ENUM_COUNT;
    s << uiCount;
    for (aeUInt32 i = 0; i < Kraut::BranchType::ENUM_COUNT; ++i)
      WriteSpawnNodeDescV42(s, m_StructureDesc.m_BranchTypes[i]);

    s << m_StructureDesc.m_bGrowProceduralTrunks;

    // version 9
    {
      s << m_bSnapshotFromAbove;
      const aeUInt32 uiDummy = 0;
      s << uiDummy;
    }

    s << m_fBBoxAdjustment;                 // version 10
    s << m_iImpostorResolution;             // version 11
    s << m_uiLeafCardTexelDilation;         // version 12
    s << m_uiLeafCardResolution;
    s << m_uiLeafCardMipmaps;
    s << m_bUseAO;                          // version 13
    s << m_fAOSampleSize;
    s << m_fAOContrast;

    // version 14: AO grid
    {
      s << m_AoGrid.m_uiVersion;
      s << m_AoGrid.m_vBBoxMin;
      s << m_AoGrid.m_vBBoxMax;
      s << m_AoGrid.m_uiCells[0];
      s << m_AoGrid.m_uiCells[1];
      s << m_AoGrid.m_uiCells[2];
      s << m_AoGrid.m_fCellSize;

      const aeUInt32 uiCells = (aeUInt32)(m_AoGrid.m_CellData.size() / 6);
      s << uiCells;
      for (aeUInt32 c = 0; c < m_AoGrid.m_CellData.size(); ++c)
        s << m_AoGrid.m_CellData[c];
    }

    s << m_fExportScale; // version 15

    // version 16: forces (always written as force-version 3)
    {
      const aeUInt32 uiForces = (aeUInt32)m_Forces.size();
      s << uiForces;

      for (aeUInt32 i = 0; i < uiForces; ++i)
      {
        const ForceData& f = m_Forces[i];

        const aeUInt8 uiForceVersion = 3;
        s << uiForceVersion;
        s << f.m_sName;
        s << f.m_bActive;
        s << f.m_iType;
        s << f.m_uiInfluencedBranchTypes;
        s << f.m_fStrength;
        s << f.m_fMinRadius;
        s << f.m_fMaxRadius;
        s << f.m_Transform;
        s << f.m_iFalloff;
        s << f.m_sMesh;
      }
    }

    return true;
  }
} // namespace KrautCLI
