#include "DescriptorPatch.h"

#include "ObjExport.h" // GetBranchTypeName

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace KrautCLI
{
  namespace
  {
    using Kraut::SpawnNodeDesc;

    enum class FieldKind
    {
      Bool,
      UInt8,
      UInt16,
      Int8,
      Float,
      EnumTargetDir,
      EnumTargetDir2Usage,
      EnumBranchTypeMode,
      EnumLeafOrientation,
      EnumFrondContourMode,
    };

    struct FieldInfo
    {
      const char* m_szName;
      FieldKind m_Kind;
      size_t m_uiOffset;
    };

#define FIELD(name, kind) {#name, kind, offsetof(SpawnNodeDesc, name)}

    const FieldInfo s_Fields[] = {
      FIELD(m_bUsed, FieldKind::Bool),
      FIELD(m_uiMinBranches, FieldKind::UInt8),
      FIELD(m_uiMaxBranches, FieldKind::UInt8),
      FIELD(m_fNodeHeight, FieldKind::Float),
      FIELD(m_fNodeSpacingBefore, FieldKind::Float),
      FIELD(m_fNodeSpacingAfter, FieldKind::Float),
      FIELD(m_BranchTypeMode, FieldKind::EnumBranchTypeMode),
      FIELD(m_fBranchlessPartABS, FieldKind::Float),
      FIELD(m_fBranchlessPartEndABS, FieldKind::Float),
      FIELD(m_uiMinBranchLengthInCM, FieldKind::UInt16),
      FIELD(m_uiMaxBranchLengthInCM, FieldKind::UInt16),
      FIELD(m_uiMinBranchThicknessInCM, FieldKind::UInt16),
      FIELD(m_uiMaxBranchThicknessInCM, FieldKind::UInt16),
      FIELD(m_fMaxRotationalDeviation, FieldKind::Float),
      FIELD(m_fBranchAngle, FieldKind::Float),
      FIELD(m_fMaxBranchAngleDeviation, FieldKind::Float),
      FIELD(m_bTargetDirRelative, FieldKind::Bool),
      FIELD(m_TargetDirection, FieldKind::EnumTargetDir),
      FIELD(m_TargetDirection2, FieldKind::EnumTargetDir),
      FIELD(m_TargetDir2Uage, FieldKind::EnumTargetDir2Usage),
      FIELD(m_fTargetDir2Usage, FieldKind::Float),
      FIELD(m_fMaxTargetDirDeviation, FieldKind::Float),
      FIELD(m_fGrowMaxTargetDirDeviation, FieldKind::Float),
      FIELD(m_fGrowMaxDirChangePerSegment, FieldKind::Float),
      FIELD(m_fRoundnessFactor, FieldKind::Float),
      FIELD(m_bActAsObstacle, FieldKind::Bool),
      FIELD(m_bDoPhysicalSimulation, FieldKind::Bool),
      FIELD(m_fPhysicsLookAhead, FieldKind::Float),
      FIELD(m_fPhysicsEvasionAngle, FieldKind::Float),
      FIELD(m_uiLowerBound, FieldKind::UInt8),
      FIELD(m_uiUpperBound, FieldKind::UInt8),
      FIELD(m_iSegmentLengthCM, FieldKind::Int8),
      FIELD(m_bRestrictGrowthToFrondPlane, FieldKind::Bool),
      FIELD(m_uiNumFronds, FieldKind::UInt8),
      FIELD(m_uiFrondDetail, FieldKind::UInt8),
      FIELD(m_fFrondWidth, FieldKind::Float),
      FIELD(m_fFrondHeight, FieldKind::Float),
      FIELD(m_uiMaxFrondOrientationDeviation, FieldKind::UInt8),
      FIELD(m_FrondUpOrientation, FieldKind::EnumLeafOrientation),
      FIELD(m_FrondContourMode, FieldKind::EnumFrondContourMode),
      FIELD(m_fTextureRepeat, FieldKind::Float),
      FIELD(m_bAlignFrondsOnSurface, FieldKind::Bool),
      FIELD(m_bBillboardLeaves, FieldKind::Bool),
      FIELD(m_fLeafSize, FieldKind::Float),
      FIELD(m_fLeafInterval, FieldKind::Float),
      FIELD(m_uiFlares, FieldKind::UInt8),
      FIELD(m_fFlareWidth, FieldKind::Float),
      FIELD(m_fFlareRotation, FieldKind::Float),
      FIELD(m_bRotateTexCoords, FieldKind::Bool),
    };

#undef FIELD

    const char* const s_TargetDirNames[] = {
      "Straight", "Upwards", "Degree22", "Degree45", "Degree67",
      "Degree90", "Degree112", "Degree135", "Degree157", "Downwards"};
    const char* const s_TargetDir2UsageNames[] = {"Off", "Relative", "Absolute"};
    const char* const s_BranchTypeModeNames[] = {"Default", "Umbrella"};
    const char* const s_LeafOrientationNames[] = {"Upwards", "AlongBranch", "OrthogonalToBranch"};
    const char* const s_FrondContourModeNames[] = {"Full", "Symetric", "InverseSymetric"};

    bool EqualsNoCase(const char* a, const char* b)
    {
      while (*a && *b)
      {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
          return false;
        ++a;
        ++b;
      }
      return *a == *b;
    }

    // Normalized compare: ignores case and underscores.
    bool EqualsTypeName(const char* a, const char* b)
    {
      while (*a && *b)
      {
        if (*a == '_')
        {
          ++a;
          continue;
        }
        if (*b == '_')
        {
          ++b;
          continue;
        }
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
          return false;
        ++a;
        ++b;
      }
      while (*a == '_')
        ++a;
      while (*b == '_')
        ++b;
      return *a == *b;
    }

    bool FindBranchType(const char* szName, aeUInt32& out_uiType)
    {
      for (aeUInt32 i = 0; i < Kraut::BranchType::ENUM_COUNT; ++i)
      {
        if (EqualsTypeName(szName, GetBranchTypeName(i)))
        {
          out_uiType = i;
          return true;
        }
      }
      return false;
    }

    bool ParseEnumValue(const char* szValue, const char* const* pNames, aeUInt32 uiCount, int& out_iValue)
    {
      for (aeUInt32 i = 0; i < uiCount; ++i)
      {
        if (EqualsNoCase(szValue, pNames[i]))
        {
          out_iValue = (int)i;
          return true;
        }
      }
      char* pEnd = nullptr;
      long l = std::strtol(szValue, &pEnd, 10);
      if (pEnd && *pEnd == '\0' && l >= 0 && l < (long)uiCount)
      {
        out_iValue = (int)l;
        return true;
      }
      return false;
    }

    bool ParseBool(const char* szValue, bool& out_b)
    {
      if (EqualsNoCase(szValue, "true") || std::strcmp(szValue, "1") == 0)
      {
        out_b = true;
        return true;
      }
      if (EqualsNoCase(szValue, "false") || std::strcmp(szValue, "0") == 0)
      {
        out_b = false;
        return true;
      }
      return false;
    }

    template <typename T>
    bool ParseInt(const char* szValue, T& out_Value, long lMin, long lMax)
    {
      char* pEnd = nullptr;
      long l = std::strtol(szValue, &pEnd, 10);
      if (!pEnd || *pEnd != '\0' || l < lMin || l > lMax)
        return false;
      out_Value = (T)l;
      return true;
    }

    bool ParseFloat(const char* szValue, float& out_f)
    {
      char* pEnd = nullptr;
      float f = std::strtof(szValue, &pEnd);
      if (!pEnd || *pEnd != '\0')
        return false;
      out_f = f;
      return true;
    }

    bool SetField(SpawnNodeDesc& sn, const FieldInfo& fi, const char* szValue, aeString& out_sError)
    {
      char* pBase = reinterpret_cast<char*>(&sn);
      switch (fi.m_Kind)
      {
        case FieldKind::Bool:
          return ParseBool(szValue, *reinterpret_cast<bool*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected true/false", false);
        case FieldKind::UInt8:
          return ParseInt(szValue, *reinterpret_cast<aeUInt8*>(pBase + fi.m_uiOffset), 0, 255) ||
                 (out_sError = "expected integer 0..255", false);
        case FieldKind::UInt16:
          return ParseInt(szValue, *reinterpret_cast<aeUInt16*>(pBase + fi.m_uiOffset), 0, 65535) ||
                 (out_sError = "expected integer 0..65535", false);
        case FieldKind::Int8:
          return ParseInt(szValue, *reinterpret_cast<aeInt8*>(pBase + fi.m_uiOffset), -128, 127) ||
                 (out_sError = "expected integer -128..127", false);
        case FieldKind::Float:
          return ParseFloat(szValue, *reinterpret_cast<float*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected float", false);
        case FieldKind::EnumTargetDir:
          return ParseEnumValue(szValue, s_TargetDirNames, 10, *reinterpret_cast<int*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected Straight/Upwards/Degree22..Degree157/Downwards", false);
        case FieldKind::EnumTargetDir2Usage:
          return ParseEnumValue(szValue, s_TargetDir2UsageNames, 3, *reinterpret_cast<int*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected Off/Relative/Absolute", false);
        case FieldKind::EnumBranchTypeMode:
          return ParseEnumValue(szValue, s_BranchTypeModeNames, 2, *reinterpret_cast<int*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected Default/Umbrella", false);
        case FieldKind::EnumLeafOrientation:
          return ParseEnumValue(szValue, s_LeafOrientationNames, 3, *reinterpret_cast<int*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected Upwards/AlongBranch/OrthogonalToBranch", false);
        case FieldKind::EnumFrondContourMode:
          return ParseEnumValue(szValue, s_FrondContourModeNames, 3, *reinterpret_cast<int*>(pBase + fi.m_uiOffset)) ||
                 (out_sError = "expected Full/Symetric/InverseSymetric", false);
      }
      out_sError = "unhandled field kind";
      return false;
    }

    // m_bEnable[BranchGeometryType] exposed as EnableBranch / EnableFrond / EnableLeaf.
    // m_bAllowSubType[3] exposed as AllowSubType0..2.
    // m_sTexture[BranchGeometryType] exposed as TextureBranch / TextureFrond / TextureLeaf.
    bool SetSpecialField(SpawnNodeDesc& sn, const char* szField, const char* szValue, bool& out_bHandled, aeString& out_sError)
    {
      out_bHandled = true;
      bool bVal = false;
      if (EqualsNoCase(szField, "EnableBranch") || EqualsNoCase(szField, "EnableFrond") || EqualsNoCase(szField, "EnableLeaf"))
      {
        if (!ParseBool(szValue, bVal))
        {
          out_sError = "expected true/false";
          return false;
        }
        aeUInt32 uiGeo = EqualsNoCase(szField, "EnableBranch") ? Kraut::BranchGeometryType::Branch
                         : EqualsNoCase(szField, "EnableFrond") ? Kraut::BranchGeometryType::Frond
                                                                : Kraut::BranchGeometryType::Leaf;
        sn.m_bEnable[uiGeo] = bVal;
        return true;
      }
      if (EqualsNoCase(szField, "TextureBranch") || EqualsNoCase(szField, "TextureFrond") || EqualsNoCase(szField, "TextureLeaf"))
      {
        aeUInt32 uiGeo = EqualsNoCase(szField, "TextureBranch") ? Kraut::BranchGeometryType::Branch
                         : EqualsNoCase(szField, "TextureFrond") ? Kraut::BranchGeometryType::Frond
                                                                 : Kraut::BranchGeometryType::Leaf;
        sn.m_sTexture[uiGeo] = szValue;
        return true;
      }
      if (std::strncmp(szField, "AllowSubType", 12) == 0 && szField[12] >= '0' && szField[12] <= '2' && szField[13] == '\0')
      {
        if (!ParseBool(szValue, bVal))
        {
          out_sError = "expected true/false";
          return false;
        }
        sn.m_bAllowSubType[szField[12] - '0'] = bVal;
        return true;
      }
      out_bHandled = false;
      return true;
    }

    void PrintValue(const SpawnNodeDesc& sn, const FieldInfo& fi)
    {
      const char* pBase = reinterpret_cast<const char*>(&sn);
      switch (fi.m_Kind)
      {
        case FieldKind::Bool:
          std::printf("%s", *reinterpret_cast<const bool*>(pBase + fi.m_uiOffset) ? "true" : "false");
          break;
        case FieldKind::UInt8:
          std::printf("%u", (unsigned int)*reinterpret_cast<const aeUInt8*>(pBase + fi.m_uiOffset));
          break;
        case FieldKind::UInt16:
          std::printf("%u", (unsigned int)*reinterpret_cast<const aeUInt16*>(pBase + fi.m_uiOffset));
          break;
        case FieldKind::Int8:
          std::printf("%d", (int)*reinterpret_cast<const aeInt8*>(pBase + fi.m_uiOffset));
          break;
        case FieldKind::Float:
          std::printf("%g", (double)*reinterpret_cast<const float*>(pBase + fi.m_uiOffset));
          break;
        case FieldKind::EnumTargetDir:
          std::printf("%s", s_TargetDirNames[*reinterpret_cast<const int*>(pBase + fi.m_uiOffset) % 10]);
          break;
        case FieldKind::EnumTargetDir2Usage:
          std::printf("%s", s_TargetDir2UsageNames[*reinterpret_cast<const int*>(pBase + fi.m_uiOffset) % 3]);
          break;
        case FieldKind::EnumBranchTypeMode:
          std::printf("%s", s_BranchTypeModeNames[*reinterpret_cast<const int*>(pBase + fi.m_uiOffset) % 2]);
          break;
        case FieldKind::EnumLeafOrientation:
          std::printf("%s", s_LeafOrientationNames[*reinterpret_cast<const int*>(pBase + fi.m_uiOffset) % 3]);
          break;
        case FieldKind::EnumFrondContourMode:
          std::printf("%s", s_FrondContourModeNames[*reinterpret_cast<const int*>(pBase + fi.m_uiOffset) % 3]);
          break;
      }
    }
  } // namespace

  bool ApplyDescriptorPatch(TreeFile& treeFile, const char* szPatch, aeString& out_sError)
  {
    // split "Type.Field=Value"
    char szBuf[256];
    std::strncpy(szBuf, szPatch, sizeof(szBuf) - 1);
    szBuf[sizeof(szBuf) - 1] = '\0';

    char* pEq = std::strchr(szBuf, '=');
    if (!pEq)
    {
      out_sError = "patch must be of the form Type.Field=Value";
      return false;
    }
    *pEq = '\0';
    const char* szValue = pEq + 1;

    char* pDot = std::strchr(szBuf, '.');
    if (!pDot)
    {
      out_sError = "patch must be of the form Type.Field=Value";
      return false;
    }
    *pDot = '\0';
    const char* szType = szBuf;
    const char* szField = pDot + 1;

    aeUInt32 uiType = 0;
    if (!FindBranchType(szType, uiType))
    {
      out_sError = "unknown branch type (valid: Trunk_1..3, Main_Branches_1..3, Sub_Branches_1..3, Twigs_1..3)";
      return false;
    }

    SpawnNodeDesc& sn = treeFile.m_StructureDesc.m_BranchTypes[uiType];

    bool bHandled = false;
    if (!SetSpecialField(sn, szField, szValue, bHandled, out_sError))
      return false;
    if (bHandled)
      return true;

    for (const FieldInfo& fi : s_Fields)
    {
      // allow the "m_" prefix to be omitted
      const char* szName = fi.m_szName;
      if (szName[0] == 'm' && szName[1] == '_')
        szName += 2;

      if (EqualsNoCase(szField, szName) || EqualsNoCase(szField, fi.m_szName))
        return SetField(sn, fi, szValue, out_sError);
    }

    out_sError = "unknown field (use 'KrautCLI dump <file.tree>' to list patchable fields)";
    return false;
  }

  bool CopyBranchTypeDesc(TreeFile& treeFile, const char* szFromType, const char* szToType, aeString& out_sError)
  {
    aeUInt32 uiFrom = 0, uiTo = 0;
    if (!FindBranchType(szFromType, uiFrom) || !FindBranchType(szToType, uiTo))
    {
      out_sError = "unknown branch type (valid: Trunk_1..3, Main_Branches_1..3, Sub_Branches_1..3, Twigs_1..3)";
      return false;
    }
    if (uiFrom == uiTo)
    {
      out_sError = "source and target branch type are identical";
      return false;
    }

    SpawnNodeDesc& from = treeFile.m_StructureDesc.m_BranchTypes[uiFrom];
    SpawnNodeDesc& to = treeFile.m_StructureDesc.m_BranchTypes[uiTo];
    to = from;
    to.m_Type = (Kraut::BranchType::Enum)uiTo;
    return true;
  }

  void DumpDescriptorFields(const TreeFile& treeFile)
  {
    std::printf("RandomSeed = %u\n", treeFile.m_StructureDesc.m_uiRandomSeed);

    for (aeUInt32 bt = 0; bt < Kraut::BranchType::ENUM_COUNT; ++bt)
    {
      const SpawnNodeDesc& sn = treeFile.m_StructureDesc.m_BranchTypes[bt];
      if (!sn.m_bUsed)
        continue;

      const char* szType = GetBranchTypeName(bt);
      std::printf("[%s]\n", szType);

      for (const FieldInfo& fi : s_Fields)
      {
        const char* szName = fi.m_szName;
        if (szName[0] == 'm' && szName[1] == '_')
          szName += 2;
        std::printf("%s.%s = ", szType, szName);
        PrintValue(sn, fi);
        std::printf("\n");
      }

      std::printf("%s.EnableBranch = %s\n", szType, sn.m_bEnable[Kraut::BranchGeometryType::Branch] ? "true" : "false");
      std::printf("%s.EnableFrond = %s\n", szType, sn.m_bEnable[Kraut::BranchGeometryType::Frond] ? "true" : "false");
      std::printf("%s.EnableLeaf = %s\n", szType, sn.m_bEnable[Kraut::BranchGeometryType::Leaf] ? "true" : "false");
      std::printf("%s.TextureBranch = %s\n", szType, sn.m_sTexture[Kraut::BranchGeometryType::Branch].c_str());
      std::printf("%s.TextureFrond = %s\n", szType, sn.m_sTexture[Kraut::BranchGeometryType::Frond].c_str());
      std::printf("%s.TextureLeaf = %s\n", szType, sn.m_sTexture[Kraut::BranchGeometryType::Leaf].c_str());
      for (aeUInt32 i = 0; i < 3; ++i)
        std::printf("%s.AllowSubType%u = %s\n", szType, i, sn.m_bAllowSubType[i] ? "true" : "false");
    }
  }
} // namespace KrautCLI
