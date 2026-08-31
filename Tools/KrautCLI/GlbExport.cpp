#include "GlbExport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace KrautCLI
{
  namespace
  {
    // ---- binary buffer assembly ----------------------------------------

    struct BufferBuilder
    {
      std::vector<aeUInt8> m_Data;

      // appends raw data 4-byte aligned, returns the byte offset
      aeUInt32 Append(const void* pData, size_t uiBytes)
      {
        while (m_Data.size() % 4 != 0)
          m_Data.push_back(0);
        const aeUInt32 uiOffset = (aeUInt32)m_Data.size();
        const aeUInt8* p = reinterpret_cast<const aeUInt8*>(pData);
        m_Data.insert(m_Data.end(), p, p + uiBytes);
        return uiOffset;
      }

      template <typename T>
      aeUInt32 AppendVec(const std::vector<T>& v)
      {
        return Append(v.data(), v.size() * sizeof(T));
      }
    };

    // ---- JSON emission ---------------------------------------------------

    void JsonEscape(std::ostringstream& s, const std::string& v)
    {
      for (char c : v)
      {
        switch (c)
        {
        case '"': s << "\\\""; break;
        case '\\': s << "\\\\"; break;
        default: s << c; break;
        }
      }
    }

    // ---- materials --------------------------------------------------------

    struct GlbMaterial
    {
      std::string m_sDiffuseTexture; // descriptor-relative reference
      aeUInt8 m_uiVariationColor[4] = {255, 255, 255, 255};
      bool m_bAlphaTest = false;     // leaf/frond geometry
      aeUInt32 m_uiGeomType = 0;

      bool operator<(const GlbMaterial& rhs) const
      {
        if (m_bAlphaTest != rhs.m_bAlphaTest)
          return m_bAlphaTest < rhs.m_bAlphaTest;
        return m_sDiffuseTexture < rhs.m_sDiffuseTexture;
      }
    };

    // ---- wind weights -----------------------------------------------------

    float SwayBaseForBranchType(aeUInt32 uiType)
    {
      static const float s_Table[12] = {
        0.02f, 0.03f, 0.04f, // Trunk1..3
        0.10f, 0.14f, 0.18f, // MainBranches1..3
        0.25f, 0.32f, 0.40f, // SubBranches1..3
        0.55f, 0.65f, 0.75f  // Twigs1..3
      };
      return uiType < 12 ? s_Table[uiType] : 0.5f;
    }

    float PhaseForBranch(aeUInt32 uiBranch)
    {
      aeUInt32 h = uiBranch * 2654435761u;
      h ^= h >> 13;
      h *= 2246822519u;
      h ^= h >> 16;
      return (h & 0xFFFF) / 65535.0f;
    }

    // COLOR_0: R sway, G flutter, B phase, A color variation.
    void ComputeWindWeights(const Kraut::TreeStructure& structure, aeUInt32 uiBranch,
      aeUInt32 uiGeomType, const Kraut::Vertex& vtx, float out_Weights[4])
    {
      const Kraut::BranchStructure& branch = structure.m_BranchStructures[uiBranch];
      const aeUInt32 uiNodeCount = (aeUInt32)branch.m_Nodes.size();

      float t = 1.0f;
      if (uiNodeCount > 1 && vtx.m_uiBranchNodeIdx != 0xFFFFFFFF)
        t = std::min(vtx.m_uiBranchNodeIdx, uiNodeCount - 1) / (float)(uiNodeCount - 1);

      const float fBase = SwayBaseForBranchType(branch.m_Type);
      out_Weights[0] = std::min(1.0f, fBase * (0.35f + 0.65f * t));
      out_Weights[1] = (uiGeomType == Kraut::BranchGeometryType::Leaf) ? 1.0f
        : (uiGeomType == Kraut::BranchGeometryType::Frond) ? 0.5f : 0.0f;
      out_Weights[2] = PhaseForBranch(uiBranch);
      out_Weights[3] = vtx.m_uiColorVariation / 255.0f;
    }

    // ---- texture resolution -----------------------------------------------

    bool FileExists(const std::string& sPath)
    {
      FILE* pFile = std::fopen(sPath.c_str(), "rb");
      if (pFile == nullptr)
        return false;
      std::fclose(pFile);
      return true;
    }

    std::string BaseName(const std::string& sPath)
    {
      const size_t slash = sPath.find_last_of("/\\");
      return slash == std::string::npos ? sPath : sPath.substr(slash + 1);
    }

    std::string DirName(const std::string& sPath)
    {
      const size_t slash = sPath.find_last_of("/\\");
      return slash == std::string::npos ? std::string() : sPath.substr(0, slash);
    }

    std::string SwapExtension(const std::string& sPath, const char* szExt)
    {
      std::string s = sPath;
      const size_t dot = s.find_last_of('.');
      const size_t slash = s.find_last_of("/\\");
      if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        s = s.substr(0, dot);
      return s + "." + szExt;
    }

    bool EndsWith(const std::string& s, const char* szSuffix)
    {
      const size_t n = std::strlen(szSuffix);
      return s.size() >= n && s.compare(s.size() - n, n, szSuffix) == 0;
    }

    // Resolves a descriptor texture reference ("Textures/.../X.dds") to an
    // existing file. DDS references resolve to .tga/.png siblings (fury's
    // STB loader cannot read DDS). Returns the resolved path; out_Uri gets
    // the basename the glb should reference.
    std::string ResolveTexture(const std::string& sRef, const std::string& sDescriptorDir,
      std::string& out_Uri, std::vector<std::string>& out_Warnings)
    {
      // candidate roots: descriptor dir, its parent (Data/Content for repo
      // descriptors), cwd, cwd/Data/Content
      std::vector<std::string> roots;
      if (!sDescriptorDir.empty())
      {
        roots.push_back(sDescriptorDir);
        roots.push_back(DirName(sDescriptorDir));
      }
      roots.push_back(".");
      roots.push_back("Data/Content");

      std::vector<std::string> candidates;
      if (EndsWith(sRef, ".dds"))
      {
        const std::string sTga = SwapExtension(sRef, "tga");
        const std::string sPng = SwapExtension(sRef, "png");
        for (const std::string& r : roots)
        {
          candidates.push_back(r + "/" + sTga);
          candidates.push_back(r + "/" + DirName(sRef) + "/tga/" + BaseName(sTga));
          candidates.push_back(r + "/" + sPng);
          candidates.push_back(r + "/" + DirName(sRef) + "/png/" + BaseName(sPng));
        }
      }
      for (const std::string& r : roots)
        candidates.push_back(r + "/" + sRef);

      for (const std::string& c : candidates)
      {
        if (FileExists(c))
        {
          out_Uri = BaseName(c);
          return c;
        }
      }

      out_Warnings.push_back("texture not found on disk: " + sRef);
      out_Uri = BaseName(sRef);
      return std::string();
    }

    // ---- per-material combined mesh for one LOD ---------------------------

    struct CombinedPrimitive
    {
      aeUInt32 m_uiMaterialIndex = 0;
      std::vector<float> m_Positions;  // 3 per vertex (scaled)
      std::vector<float> m_Normals;    // 3
      std::vector<float> m_Tangents;   // 4 (w = bitangent handedness)
      std::vector<float> m_UVs;        // 2 (glTF v convention)
      std::vector<float> m_Colors;     // 4 (wind weights)
      std::vector<aeUInt32> m_Indices;
      float m_vMin[3] = {1e30f, 1e30f, 1e30f};
      float m_vMax[3] = {-1e30f, -1e30f, -1e30f};
    };

    void AppendMesh(CombinedPrimitive& out_Prim, const Kraut::Mesh& mesh,
      const Kraut::TreeStructure& structure, aeUInt32 uiBranch, aeUInt32 uiGeomType, float fScale)
    {
      const aeUInt32 uiVertexOffset = (aeUInt32)(out_Prim.m_Positions.size() / 3);

      for (aeUInt32 v = 0; v < mesh.m_Vertices.size(); ++v)
      {
        const Kraut::Vertex& vtx = mesh.m_Vertices[v];

        const float px = fScale * vtx.m_vPosition.x;
        const float py = fScale * vtx.m_vPosition.y;
        const float pz = fScale * vtx.m_vPosition.z;
        out_Prim.m_Positions.push_back(px);
        out_Prim.m_Positions.push_back(py);
        out_Prim.m_Positions.push_back(pz);
        for (int c = 0; c < 3; ++c)
        {
          const float p = c == 0 ? px : c == 1 ? py : pz;
          out_Prim.m_vMin[c] = std::min(out_Prim.m_vMin[c], p);
          out_Prim.m_vMax[c] = std::max(out_Prim.m_vMax[c], p);
        }

        out_Prim.m_Normals.push_back(vtx.m_vNormal.x);
        out_Prim.m_Normals.push_back(vtx.m_vNormal.y);
        out_Prim.m_Normals.push_back(vtx.m_vNormal.z);

        // glTF tangent w = bitangent handedness
        const aeVec3 cross = vtx.m_vNormal.Cross(vtx.m_vTangent);
        const float w = cross.Dot(vtx.m_vBiTangent) < 0.0f ? -1.0f : 1.0f;
        out_Prim.m_Tangents.push_back(vtx.m_vTangent.x);
        out_Prim.m_Tangents.push_back(vtx.m_vTangent.y);
        out_Prim.m_Tangents.push_back(vtx.m_vTangent.z);
        out_Prim.m_Tangents.push_back(w);

        // OBJ (Kraut) v is up, glTF v is down
        const float u = (vtx.m_vTexCoord.z != 0.0f) ? (vtx.m_vTexCoord.x / vtx.m_vTexCoord.z) : 0.0f;
        const float t = (vtx.m_vTexCoord.z != 0.0f) ? (vtx.m_vTexCoord.y / vtx.m_vTexCoord.z) : 0.0f;
        out_Prim.m_UVs.push_back(u);
        out_Prim.m_UVs.push_back(1.0f - t);

        float weights[4];
        ComputeWindWeights(structure, uiBranch, uiGeomType, vtx, weights);
        for (float f : weights)
          out_Prim.m_Colors.push_back(f);
      }

      for (aeUInt32 t = 0; t < mesh.m_Triangles.size(); ++t)
      {
        const Kraut::Triangle& tri = mesh.m_Triangles[t];
        out_Prim.m_Indices.push_back(uiVertexOffset + tri.m_uiVertexIDs[0]);
        out_Prim.m_Indices.push_back(uiVertexOffset + tri.m_uiVertexIDs[1]);
        out_Prim.m_Indices.push_back(uiVertexOffset + tri.m_uiVertexIDs[2]);
      }
    }
  } // namespace

  bool ExportGlb(TreeFile& treeFile, const GlbExportOptions& opts,
    const char* szPath, GlbExportResult& out_Result, aeString& out_sError)
  {
    // ---- determine + generate LODs (same rules as the .kraut export) -----
    bool bExportLod[LOD_COUNT];
    aeUInt32 uiNumLods = 0;
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      bExportLod[lod] = (treeFile.m_Lods[lod].m_Mode == Kraut::LodMode::Full);
      if (bExportLod[lod])
        ++uiNumLods;
    }
    if (uiNumLods == 0)
    {
      out_sError = "No full-mesh LOD available to export.";
      return false;
    }

    GeneratedTree lods[LOD_COUNT];
    Kraut::BoundingBox globalBBox;
    bool bBBoxSet = false;
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      if (!bExportLod[lod])
        continue;
      if (!GenerateTree(treeFile, opts.m_uiSeed, lod, lods[lod], out_sError))
        return false;
      if (!bBBoxSet)
      {
        globalBBox = lods[lod].m_BBox;
        bBBoxSet = true;
      }
    }

    const float fScale = treeFile.m_fExportScale;

    // ---- gather the material set (stable order across LODs) ---------------
    std::map<GlbMaterial, std::set<aeUInt32>> materialMap; // material -> branch types using it
    for (aeUInt32 bt = 0; bt < Kraut::BranchType::ENUM_COUNT; ++bt)
    {
      const Kraut::SpawnNodeDesc& sn = treeFile.m_StructureDesc.m_BranchTypes[bt];
      if (!sn.m_bUsed)
        continue;

      for (aeUInt32 mt = 0; mt < Kraut::BranchGeometryType::ENUM_COUNT; ++mt)
      {
        if (!sn.m_bEnable[mt])
          continue;

        GlbMaterial mat;
        mat.m_sDiffuseTexture = sn.m_sTexture[mt].c_str();
        mat.m_bAlphaTest = (mt != Kraut::BranchGeometryType::Branch);
        mat.m_uiGeomType = mt;
        for (aeUInt32 c = 0; c < 4; ++c)
          mat.m_uiVariationColor[c] = sn.m_uiVariationColor[mt][c];

        materialMap[mat].insert(bt);
      }
    }

    // assign material indices
    std::vector<GlbMaterial> materials;
    std::map<GlbMaterial, std::set<aeUInt32>> materialUses;
    for (const auto& pair : materialMap)
    {
      materials.push_back(pair.first);
      materialUses[pair.first] = pair.second;
    }
    auto findMaterial = [&](const GlbMaterial& mat) -> aeUInt32
    {
      for (aeUInt32 i = 0; i < materials.size(); ++i)
        if (!(materials[i] < mat) && !(mat < materials[i]))
          return i;
      return 0;
    };

    // ---- build primitives per LOD -----------------------------------------
    // lodPrims[tier] = primitives for fury tier `tier` (kraut slot order)
    std::vector<std::vector<CombinedPrimitive>> lodPrims;
    std::vector<aeUInt32> lodSlots; // kraut slot per tier
    for (aeUInt32 lod = 0; lod < LOD_COUNT; ++lod)
    {
      if (!bExportLod[lod])
        continue;
      lodSlots.push_back(lod);

      std::vector<CombinedPrimitive> prims;
      for (aeUInt32 mi = 0; mi < materials.size(); ++mi)
      {
        const GlbMaterial& mat = materials[mi];
        const std::set<aeUInt32>& types = materialUses[mat];

        CombinedPrimitive prim;
        prim.m_uiMaterialIndex = mi;
        aeUInt32 uiBefore = (aeUInt32)prim.m_Indices.size();
        for (aeUInt32 b = 0; b < lods[lod].m_Structure.m_BranchStructures.size(); ++b)
        {
          if (types.find(lods[lod].m_Structure.m_BranchStructures[b].m_Type) == types.end())
            continue;

          const Kraut::Mesh& mesh = lods[lod].m_Mesh.m_BranchMeshes[b].m_Mesh[mat.m_uiGeomType];
          if (mesh.m_Triangles.empty())
            continue;
          AppendMesh(prim, mesh, lods[lod].m_Structure, b, mat.m_uiGeomType, fScale);
        }
        if (prim.m_Indices.size() > uiBefore)
          prims.push_back(std::move(prim));
      }
      lodPrims.push_back(std::move(prims));
    }

    out_Result.m_uiLodCount = (aeUInt32)lodPrims.size();
    for (const auto& prims : lodPrims)
      for (const auto& p : prims)
        out_Result.m_uiTriangles += (aeUInt32)p.m_Indices.size() / 3;

    // ---- billboard quad (pre-sized to the tree bbox) -----------------------
    // Width uses the XZ diagonal: the tree silhouette fits inside the quad
    // from every azimuth (matches the atlas renderer's ortho box).
    const float fSizeX = (globalBBox.m_vMax.x - globalBBox.m_vMin.x) * fScale;
    const float fSizeZ = (globalBBox.m_vMax.z - globalBBox.m_vMin.z) * fScale;
    const float fQuadW = std::sqrt(fSizeX * fSizeX + fSizeZ * fSizeZ);
    const float fQuadH = (globalBBox.m_vMax.y - globalBBox.m_vMin.y) * fScale;

    CombinedPrimitive bbPrim;
    {
      const float hw = fQuadW * 0.5f;
      const float pos[4][3] = {
        {-hw, 0.0f, 0.0f}, {hw, 0.0f, 0.0f}, {hw, fQuadH, 0.0f}, {-hw, fQuadH, 0.0f}};
      const float uv[4][2] = {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
      for (int v = 0; v < 4; ++v)
      {
        for (int c = 0; c < 3; ++c)
        {
          bbPrim.m_Positions.push_back(pos[v][c]);
          bbPrim.m_Normals.push_back(c == 2 ? 1.0f : 0.0f);
          bbPrim.m_vMin[c] = std::min(bbPrim.m_vMin[c], pos[v][c]);
          bbPrim.m_vMax[c] = std::max(bbPrim.m_vMax[c], pos[v][c]);
        }
        bbPrim.m_Tangents.insert(bbPrim.m_Tangents.end(), {1.0f, 0.0f, 0.0f, 1.0f});
        bbPrim.m_UVs.push_back(uv[v][0]);
        bbPrim.m_UVs.push_back(uv[v][1]);
        bbPrim.m_Colors.insert(bbPrim.m_Colors.end(), {0.0f, 0.0f, 0.0f, 1.0f});
      }
      bbPrim.m_Indices = {0, 1, 2, 0, 2, 3};
    }

    // ---- resolve textures --------------------------------------------------
    // material index -> gltf texture index (-1 = untextured)
    std::vector<std::string> imageUris;
    std::vector<int> materialTexture;
    std::map<std::string, aeUInt32> uriToImage;
    for (const GlbMaterial& mat : materials)
    {
      if (mat.m_sDiffuseTexture.empty())
      {
        materialTexture.push_back(-1);
        continue;
      }

      std::string sUri, sResolved;
      sResolved = ResolveTexture(mat.m_sDiffuseTexture, opts.m_DescriptorDir, sUri, out_Result.m_Warnings);

      auto it = uriToImage.find(sUri);
      if (it == uriToImage.end())
      {
        it = uriToImage.emplace(sUri, (aeUInt32)imageUris.size()).first;
        imageUris.push_back(sUri);
        out_Result.m_Textures.push_back({sUri, sResolved});
      }
      materialTexture.push_back((int)it->second);
    }

    // billboard material references the (separately rendered) atlas
    const std::string sAtlasUri = opts.m_TreeName + "_BillboardAtlas.png";

    // ---- LOD thresholds from kraut distances --------------------------------
    std::vector<float> thresholds;
    {
      const aeVec3 vSize = (globalBBox.m_vMax - globalBBox.m_vMin) * fScale;
      const float fRadius = 0.5f * std::sqrt(vSize.x * vSize.x + vSize.y * vSize.y + vSize.z * vSize.z);
      const float fTanHalf = 0.41421356f; // tan(0.7854 * 0.5), reference fov

      bool bAnyDistance = false;
      for (aeUInt32 slot : lodSlots)
        if (treeFile.m_Lods[slot].m_uiLodDistance > 0)
          bAnyDistance = true;

      if (bAnyDistance)
      {
        float fLast = 1.0f;
        // thresholds for fury tiers 1..M-1 (skip slot 0 = full detail).
        // Coverage math matches the engine: radius and distance in the
        // same (scaled) units, halved reference fov.
        for (aeUInt32 i = 1; i < lodSlots.size(); ++i)
        {
          const float fDistM = (float)treeFile.m_Lods[lodSlots[i]].m_uiLodDistance;
          float t = fDistM > 0.0f
            ? std::min(1.0f, fRadius / fDistM / fTanHalf)
            : fLast * 0.5f;
          t = std::max(0.001f, std::min(t, fLast));
          thresholds.push_back(t);
          fLast = t;
        }
        // billboard tier threshold: 1.5x the deepest slot's distance
        const float fBbDistM = treeFile.m_Lods[lodSlots.back()].m_uiLodDistance > 0
          ? treeFile.m_Lods[lodSlots.back()].m_uiLodDistance * 1.5f
          : 100.0f;
        float t = std::min(1.0f, fRadius / fBbDistM / fTanHalf);
        t = std::max(0.0005f, std::min(t, fLast));
        thresholds.push_back(t);
      }
    }

    // ---- assemble glb -------------------------------------------------------
    BufferBuilder bin;
    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"KrautCLI\",\"extras\":{\"kraut\":{";
    json << "\"seed\":" << opts.m_uiSeed << ",\"descriptor\":\"";
    JsonEscape(json, opts.m_DescriptorFile);
    json << "\",\"reference_fov\":0.7854,";
    if (!thresholds.empty())
    {
      json << "\"lod_thresholds\":[";
      for (size_t i = 0; i < thresholds.size(); ++i)
        json << (i ? "," : "") << thresholds[i];
      json << "],";
    }
    json << "\"billboard\":{\"atlas_cols\":" << opts.m_uiAtlasCols
         << ",\"atlas_rows\":1,\"mode\":\"cylindrical\",\"texture\":\"";
    JsonEscape(json, sAtlasUri);
    json << "\",\"quad_width\":" << fQuadW << ",\"quad_height\":" << fQuadH << "},";
    json << "\"wind\":{\"encoding\":\"r=sway,g=flutter,b=phase,a=variation\"}";
    json << "}}},";

    // buffers/views/accessors are appended as we go
    std::ostringstream jBufferViews, jAccessors, jMeshes, jNodes, jMaterials, jTextures, jImages;

    // helper writing one attribute array -> bufferView + accessor
    struct AccessorWriter
    {
      BufferBuilder* m_Bin;
      std::ostringstream* m_Views;
      std::ostringstream* m_Acc;
      aeUInt32 m_ViewCount = 0;
      aeUInt32 m_AccCount = 0;

      aeUInt32 Write(const void* pData, size_t uiBytes, aeUInt32 uiCount, aeUInt32 uiComponentType,
        const char* szType, const float* pMin = nullptr, const float* pMax = nullptr)
      {
        const aeUInt32 uiOffset = m_Bin->Append(pData, uiBytes);
        if (m_ViewCount)
          (*m_Views) << ",";
        (*m_Views) << "{\"buffer\":0,\"byteOffset\":" << uiOffset << ",\"byteLength\":" << uiBytes << "}";
        if (m_AccCount)
          (*m_Acc) << ",";
        (*m_Acc) << "{\"bufferView\":" << m_ViewCount << ",\"componentType\":" << uiComponentType
                 << ",\"count\":" << uiCount << ",\"type\":\"" << szType << "\"";
        if (pMin && pMax)
        {
          (*m_Acc) << ",\"min\":[" << pMin[0] << "," << pMin[1] << "," << pMin[2]
                   << "],\"max\":[" << pMax[0] << "," << pMax[1] << "," << pMax[2] << "]";
        }
        (*m_Acc) << "}";
        ++m_ViewCount;
        return m_AccCount++;
      }
    };

    AccessorWriter writer{&bin, &jBufferViews, &jAccessors};

    auto addPrimitive = [&](const CombinedPrimitive& prim, std::ostringstream& jMesh) -> bool
    {
      const aeUInt32 uiVertCount = (aeUInt32)(prim.m_Positions.size() / 3);
      const aeUInt32 aPos = writer.Write(prim.m_Positions.data(), prim.m_Positions.size() * 4,
        uiVertCount, 5126, "VEC3", prim.m_vMin, prim.m_vMax);
      const aeUInt32 aNrm = writer.Write(prim.m_Normals.data(), prim.m_Normals.size() * 4,
        uiVertCount, 5126, "VEC3");
      const aeUInt32 aTan = writer.Write(prim.m_Tangents.data(), prim.m_Tangents.size() * 4,
        uiVertCount, 5126, "VEC4");
      const aeUInt32 aUV = writer.Write(prim.m_UVs.data(), prim.m_UVs.size() * 4,
        uiVertCount, 5126, "VEC2");
      const aeUInt32 aCol = writer.Write(prim.m_Colors.data(), prim.m_Colors.size() * 4,
        uiVertCount, 5126, "VEC4");
      const aeUInt32 aIdx = writer.Write(prim.m_Indices.data(), prim.m_Indices.size() * 4,
        (aeUInt32)prim.m_Indices.size(), 5125, "SCALAR");

      jMesh << "{\"attributes\":{\"POSITION\":" << aPos << ",\"NORMAL\":" << aNrm
            << ",\"TANGENT\":" << aTan << ",\"TEXCOORD_0\":" << aUV << ",\"COLOR_0\":" << aCol
            << "},\"indices\":" << aIdx << ",\"material\":" << prim.m_uiMaterialIndex << "}";
      return true;
    };

    // meshes
    std::vector<std::string> meshNames;
    for (size_t tier = 0; tier < lodPrims.size(); ++tier)
    {
      std::ostringstream name;
      name << opts.m_TreeName << "_LOD" << tier;
      meshNames.push_back(name.str());
      jMeshes << (tier ? "," : "") << "{\"name\":\"" << name.str() << "\",\"primitives\":[";
      bool bFirst = true;
      for (const CombinedPrimitive& prim : lodPrims[tier])
      {
        jMeshes << (bFirst ? "" : ",");
        addPrimitive(prim, jMeshes);
        bFirst = false;
      }
      jMeshes << "]}";
    }
    // billboard mesh
    {
      const std::string sBbName = opts.m_TreeName + "_Billboard";
      meshNames.push_back(sBbName);
      jMeshes << ",{\"name\":\"" << sBbName << "\",\"primitives\":[";
      const aeUInt32 uiBbMaterial = (aeUInt32)materials.size(); // appended below
      CombinedPrimitive bb = bbPrim;
      bb.m_uiMaterialIndex = uiBbMaterial;
      addPrimitive(bb, jMeshes);
      jMeshes << "]}";
    }

    // nodes (flat, one per mesh)
    for (size_t i = 0; i < meshNames.size(); ++i)
      jNodes << (i ? "," : "") << "{\"name\":\"" << meshNames[i] << "\",\"mesh\":" << i << "}";

    // materials
    const char* const geomNames[3] = {"bark", "frond", "leaf"};
    for (size_t i = 0; i < materials.size(); ++i)
    {
      const GlbMaterial& mat = materials[i];
      jMaterials << (i ? "," : "") << "{\"name\":\"" << geomNames[mat.m_uiGeomType] << "_" << i
                 << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":0.9";
      if (materialTexture[i] >= 0)
        jMaterials << ",\"baseColorTexture\":{\"index\":" << materialTexture[i] << "}";
      jMaterials << "}";
      if (mat.m_bAlphaTest)
        jMaterials << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.4,\"doubleSided\":true";
      jMaterials << "}";
    }
    // billboard material (atlas texture, alpha cut, two-sided)
    jMaterials << (materials.empty() ? "" : ",") << "{\"name\":\"billboard\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":0.9";
    jMaterials << ",\"baseColorTexture\":{\"index\":" << imageUris.size() << "}";
    jMaterials << "},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.4,\"doubleSided\":true}";

    // textures + images (atlas image appended last)
    for (size_t i = 0; i < imageUris.size(); ++i)
    {
      jTextures << (i ? "," : "") << "{\"source\":" << i << "}";
      jImages << (i ? "," : "") << "{\"uri\":\"";
      JsonEscape(jImages, imageUris[i]);
      jImages << "\"}";
    }
    jTextures << (imageUris.empty() ? "" : ",") << "{\"source\":" << imageUris.size() << "}";
    jImages << (imageUris.empty() ? "" : ",") << "{\"uri\":\"";
    JsonEscape(jImages, sAtlasUri);
    jImages << "\"}";

    json << "\"scene\":0,\"scenes\":[{\"nodes\":[";
    for (size_t i = 0; i < meshNames.size(); ++i)
      json << (i ? "," : "") << i;
    json << "]}],\"nodes\":[" << jNodes.str()
         << "],\"meshes\":[" << jMeshes.str()
         << "],\"materials\":[" << jMaterials.str()
         << "],\"textures\":[" << jTextures.str()
         << "],\"images\":[" << jImages.str()
         << "],\"bufferViews\":[" << jBufferViews.str()
         << "],\"accessors\":[" << jAccessors.str()
         << "],\"buffers\":[{\"byteLength\":" << bin.m_Data.size() << "}]}";

    // ---- write the glb -------------------------------------------------------
    const std::string sJson = json.str();
    const aeUInt32 uiJsonPad = (4 - ((aeUInt32)sJson.size() % 4)) % 4;
    const aeUInt32 uiBinPad = (4 - ((aeUInt32)bin.m_Data.size() % 4)) % 4;
    const aeUInt32 uiTotal = 12 + 8 + (aeUInt32)sJson.size() + uiJsonPad + 8 + (aeUInt32)bin.m_Data.size() + uiBinPad;

    FILE* pFile = std::fopen(szPath, "wb");
    if (!pFile)
    {
      out_sError = "Could not open glb file for writing.";
      return false;
    }

    const aeUInt32 uiVersion = 2;
    std::fwrite("glTF", 1, 4, pFile);
    std::fwrite(&uiVersion, 4, 1, pFile);
    std::fwrite(&uiTotal, 4, 1, pFile);

    aeUInt32 uiJsonLen = (aeUInt32)sJson.size() + uiJsonPad;
    std::fwrite(&uiJsonLen, 4, 1, pFile);
    std::fwrite("JSON", 1, 4, pFile);
    std::fwrite(sJson.data(), 1, sJson.size(), pFile);
    for (aeUInt32 i = 0; i < uiJsonPad; ++i)
      std::fputc(' ', pFile);

    aeUInt32 uiBinLen = (aeUInt32)bin.m_Data.size() + uiBinPad;
    std::fwrite(&uiBinLen, 4, 1, pFile);
    std::fwrite("BIN\0", 1, 4, pFile);
    if (!bin.m_Data.empty())
      std::fwrite(bin.m_Data.data(), 1, bin.m_Data.size(), pFile);
    for (aeUInt32 i = 0; i < uiBinPad; ++i)
      std::fputc(0, pFile);

    std::fclose(pFile);
    return true;
  }
} // namespace KrautCLI
