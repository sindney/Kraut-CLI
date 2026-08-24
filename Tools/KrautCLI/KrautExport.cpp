#include "KrautExport.h"

#include "FileStreams.h"
#include <map>
#include <set>

namespace KrautCLI
{
  namespace
  {
    struct KrautMaterial
    {
      aeString m_sDiffuseTexture;
      aeString m_sNormalMapTexture;
      aeUInt8 m_uiVariationColor[4] = {255, 255, 255, 255};

      // Same ordering as aeKrautMaterial in the editor (TreePlugin/Tree/Tree.h).
      bool operator<(const KrautMaterial& rhs) const
      {
        const aeUInt32 uiColor1 = m_uiVariationColor[0] | (m_uiVariationColor[1] << 8) | (m_uiVariationColor[2] << 16) | (m_uiVariationColor[3] << 24);
        const aeUInt32 uiColor2 = rhs.m_uiVariationColor[0] | (rhs.m_uiVariationColor[1] << 8) | (rhs.m_uiVariationColor[2] << 16) | (rhs.m_uiVariationColor[3] << 24);

        if (uiColor1 != uiColor2)
          return uiColor1 < uiColor2;
        if (m_sDiffuseTexture != rhs.m_sDiffuseTexture)
          return m_sDiffuseTexture < rhs.m_sDiffuseTexture;
        return m_sNormalMapTexture < rhs.m_sNormalMapTexture;
      }
    };

    using MaterialMap = std::map<KrautMaterial, std::set<aeUInt32>>; // material -> branch types using it

    void GatherMaterials(const TreeFile& treeFile, MaterialMap (&out_Materials)[Kraut::BranchGeometryType::ENUM_COUNT])
    {
      for (aeUInt32 bt = 0; bt < Kraut::BranchType::ENUM_COUNT; ++bt)
      {
        const Kraut::SpawnNodeDesc& sn = treeFile.m_StructureDesc.m_BranchTypes[bt];
        if (!sn.m_bUsed)
          continue;

        for (aeUInt32 mt = 0; mt < Kraut::BranchGeometryType::ENUM_COUNT; ++mt)
        {
          if (!sn.m_bEnable[mt])
            continue;

          KrautMaterial mat;
          mat.m_sDiffuseTexture = sn.m_sTexture[mt];
          mat.m_sNormalMapTexture = "";
          for (aeUInt32 c = 0; c < 4; ++c)
            mat.m_uiVariationColor[c] = sn.m_uiVariationColor[mt][c];

          out_Materials[mt][mat].insert(bt);
        }
      }
    }

    void WriteMaterial(aeStreamOut& s, const KrautMaterial& mat)
    {
      s << mat.m_sDiffuseTexture;
      s << mat.m_sNormalMapTexture;
      for (aeUInt32 c = 0; c < 4; ++c)
        s << mat.m_uiVariationColor[c];
    }

    void CombineSubMeshes(const std::vector<const Kraut::Mesh*>& subMeshes, Kraut::Mesh& out_Combined)
    {
      aeUInt32 uiVertexOffset = 0;

      for (const Kraut::Mesh* pMesh : subMeshes)
      {
        for (aeUInt32 v = 0; v < pMesh->m_Vertices.size(); ++v)
        {
          out_Combined.m_Vertices.push_back(pMesh->m_Vertices[v]);
          out_Combined.m_Vertices.back().m_iSharedVertex += (aeInt32)uiVertexOffset;
        }

        for (aeUInt32 t = 0; t < pMesh->m_Triangles.size(); ++t)
        {
          out_Combined.m_Triangles.push_back(pMesh->m_Triangles[t]);
          out_Combined.m_Triangles.back().m_uiVertexIDs[0] += uiVertexOffset;
          out_Combined.m_Triangles.back().m_uiVertexIDs[1] += uiVertexOffset;
          out_Combined.m_Triangles.back().m_uiVertexIDs[2] += uiVertexOffset;
        }

        uiVertexOffset += pMesh->m_Vertices.size();
      }
    }

    void WriteSubMesh(aeStreamOut& s, const Kraut::Mesh& mesh, float fScale)
    {
      const aeUInt32 uiVertices = mesh.m_Vertices.size();
      const aeUInt32 uiTriangles = mesh.m_Triangles.size();

      s << uiVertices;
      s << uiTriangles;

      for (aeUInt32 v = 0; v < uiVertices; ++v)
      {
        const Kraut::Vertex& mv = mesh.m_Vertices[v];

        const aeVec3 vPos = fScale * mv.m_vPosition;
        s << vPos;
        s << mv.m_vTexCoord;
        s << mv.m_vNormal;
        s << mv.m_vTangent;
        s << mv.m_uiColorVariation;

        // version 2: per-vertex AO (7 samples) - stubbed to fully lit, see header comment
        for (aeUInt32 i = 0; i < 7; ++i)
        {
          const float fAO = 1.0f;
          s << fAO;
        }
      }

      for (aeUInt32 t = 0; t < uiTriangles; ++t)
      {
        const Kraut::Triangle& mt = mesh.m_Triangles[t];
        s << mt.m_uiVertexIDs[0];
        s << mt.m_uiVertexIDs[1];
        s << mt.m_uiVertexIDs[2];
      }
    }
  } // namespace

  bool ExportKraut(TreeFile& treeFile, aeUInt32 uiSeed, const char* szPath, aeUInt32& out_uiSkippedLods, aeString& out_sError)
  {
    out_uiSkippedLods = 0;

    // determine which LOD slots to export (mesh-mode, non-disabled)
    bool bExportLod[LOD_COUNT];
    aeUInt8 uiNumLods = 0;
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      bExportLod[lod] = (treeFile.m_Lods[lod].m_Mode == Kraut::LodMode::Full);
      if (bExportLod[lod])
        ++uiNumLods;
      else if (treeFile.m_Lods[lod].m_Mode != Kraut::LodMode::Disabled)
        out_uiSkippedLods |= (1u << lod);
    }

    if (uiNumLods == 0)
    {
      out_sError = "No full-mesh LOD available to export.";
      return false;
    }

    // generate all required LODs up front (they share the same structure/seed)
    // plain array: GeneratedTree is not movable (Kraut::TreeMesh deletes copy/move)
    GeneratedTree lods[LOD_COUNT];
    Kraut::BoundingBox globalBBox;
    bool bBBoxSet = false;

    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      if (!bExportLod[lod])
        continue;

      if (!GenerateTree(treeFile, uiSeed, lod, lods[lod], out_sError))
        return false;

      if (!bBBoxSet)
      {
        globalBBox = lods[lod].m_BBox;
        bBBoxSet = true;
      }
    }

    const float fScale = treeFile.m_fExportScale;

    cliFileOut s;
    if (!s.Open(szPath))
    {
      out_sError = "Could not open .kraut file for writing.";
      return false;
    }

    // header
    const char* szSignature = "{KRAUT}";
    s.Write(szSignature, sizeof(char) * 7);

    const aeUInt8 uiVersion = 2;
    s << uiVersion;

    const aeVec3 vMin = fScale * globalBBox.m_vMin;
    const aeVec3 vMax = fScale * globalBBox.m_vMax;
    s << vMin;
    s << vMax;

    s << uiNumLods;

    // materials
    MaterialMap materials[Kraut::BranchGeometryType::ENUM_COUNT];
    GatherMaterials(treeFile, materials);

    const aeUInt8 uiMaterialTypes = Kraut::BranchGeometryType::ENUM_COUNT;
    s << uiMaterialTypes;
    for (aeUInt8 mt = 0; mt < uiMaterialTypes; ++mt)
    {
      const aeUInt8 uiMaterials = (aeUInt8)materials[mt].size();
      s << uiMaterials;
      for (const auto& pair : materials[mt])
        WriteMaterial(s, pair.first);
    }

    const aeUInt8 uiMeshTypes = Kraut::BranchGeometryType::ENUM_COUNT;
    s << uiMeshTypes;

    // LODs
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      if (!bExportLod[lod])
        continue;

      const float fLodDistance = (float)treeFile.m_Lods[lod].m_uiLodDistance;
      s << fLodDistance;

      const aeUInt8 uiLodMode = Kraut::LodMode::Full; // 0 == full mesh
      s << uiLodMode;

      s << uiMaterialTypes;

      for (aeUInt8 mt = 0; mt < uiMeshTypes; ++mt)
      {
        s << mt;

        struct CombinedMesh
        {
          aeUInt8 m_uiMaterial;
          Kraut::Mesh m_Mesh;
        };
        std::vector<CombinedMesh> meshes;

        aeUInt32 uiMaterialIdx = 0;
        for (const auto& pair : materials[mt])
        {
          std::vector<const Kraut::Mesh*> subMeshes;

          const std::set<aeUInt32>& types = pair.second;
          for (aeUInt32 b = 0; b < lods[lod].m_Structure.m_BranchStructures.size(); ++b)
          {
            if (types.find(lods[lod].m_Structure.m_BranchStructures[b].m_Type) != types.end())
            {
              const Kraut::Mesh& mesh = lods[lod].m_Mesh.m_BranchMeshes[b].m_Mesh[mt];
              if (!mesh.m_Triangles.empty())
                subMeshes.push_back(&mesh);
            }
          }

          if (!subMeshes.empty())
          {
            CombinedMesh combined;
            combined.m_uiMaterial = (aeUInt8)uiMaterialIdx;
            CombineSubMeshes(subMeshes, combined.m_Mesh);
            meshes.push_back(std::move(combined));
          }

          ++uiMaterialIdx;
        }

        const aeUInt8 uiMeshes = (aeUInt8)meshes.size();
        s << uiMeshes;

        for (const CombinedMesh& m : meshes)
        {
          s << m.m_uiMaterial;
          WriteSubMesh(s, m.m_Mesh, fScale);
        }
      }
    }

    return true;
  }

  bool ValidateKrautFile(const char* szPath, aeString& out_sError)
  {
    // v2 read side (the writer above is the reference implementation;
    // the legacy KrautViewer reader only supports v1 and is outdated).
    cliFileIn s;
    if (!s.Open(szPath))
    {
      out_sError = "Could not open .kraut file for validation.";
      return false;
    }

    char szSignature[8] = "";
    s.Read(szSignature, sizeof(char) * 7);
    if (aeString(szSignature) != "{KRAUT}")
    {
      out_sError = "Invalid .kraut signature.";
      return false;
    }

    aeUInt8 uiVersion = 0;
    s >> uiVersion;
    if (uiVersion != 2)
    {
      out_sError = "Unsupported .kraut version.";
      return false;
    }

    aeVec3 vMin, vMax;
    s >> vMin;
    s >> vMax;

    aeUInt8 uiNumLods = 0;
    s >> uiNumLods;
    if (uiNumLods == 0 || uiNumLods > LOD_COUNT)
    {
      out_sError = "Invalid LOD count in .kraut file.";
      return false;
    }

    // materials
    aeUInt8 uiMaterialTypes = 0;
    s >> uiMaterialTypes;
    for (aeUInt32 mt = 0; mt < uiMaterialTypes; ++mt)
    {
      aeUInt8 uiMaterials = 0;
      s >> uiMaterials;
      for (aeUInt32 m = 0; m < uiMaterials; ++m)
      {
        aeString sDiffuse, sNormal;
        s >> sDiffuse;
        s >> sNormal;
        aeUInt8 dummy[4];
        s >> dummy[0];
        s >> dummy[1];
        s >> dummy[2];
        s >> dummy[3];
      }
    }

    aeUInt8 uiMeshTypes = 0;
    s >> uiMeshTypes;

    // LODs
    for (aeUInt32 lod = 0; lod < uiNumLods; ++lod)
    {
      float fLodDistance = 0.0f;
      s >> fLodDistance;

      aeUInt8 uiLodMode = 0;
      s >> uiLodMode;

      aeUInt8 uiLodMaterialTypes = 0;
      s >> uiLodMaterialTypes;

      for (aeUInt32 mt = 0; mt < uiLodMaterialTypes; ++mt)
      {
        aeUInt8 uiThisType = 0;
        s >> uiThisType;

        aeUInt8 uiMeshes = 0;
        s >> uiMeshes;

        for (aeUInt32 m = 0; m < uiMeshes; ++m)
        {
          aeUInt8 uiMaterialID = 0;
          s >> uiMaterialID;

          aeUInt32 uiVertices = 0, uiTriangles = 0;
          s >> uiVertices;
          s >> uiTriangles;

          // per vertex: pos(3f) texcoord(3f) normal(3f) tangent(3f) color(1b) ao(7f)
          const aeUInt32 uiVertexBytes = uiVertices * (12 + 12 + 12 + 12 + 1 + 28);
          aeUInt8 dummy[512];
          aeUInt32 uiRemaining = uiVertexBytes + uiTriangles * 12;
          while (uiRemaining > 0)
          {
            const aeUInt32 uiChunk = (uiRemaining > sizeof(dummy)) ? sizeof(dummy) : uiRemaining;
            if (s.Read(dummy, uiChunk) != uiChunk)
            {
              out_sError = "Unexpected end of .kraut file (truncated mesh data).";
              return false;
            }
            uiRemaining -= uiChunk;
          }
        }
      }
    }

    if (!s.IsEndOfStream())
    {
      // trailing bytes would indicate a structural mismatch
      aeUInt8 dummy;
      if (s.Read(&dummy, 1) != 0)
      {
        out_sError = "Trailing bytes after .kraut content (structural mismatch).";
        return false;
      }
    }

    return true;
  }
} // namespace KrautCLI
