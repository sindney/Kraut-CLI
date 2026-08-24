#include "TextureLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace KrautPreview
{
  namespace
  {
    std::string ToLower(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    }

    std::string GetExtension(const std::string& sPath)
    {
      const size_t dot = sPath.find_last_of('.');
      if (dot == std::string::npos)
        return "";
      return ToLower(sPath.substr(dot + 1));
    }

    std::string ReplaceExtension(const std::string& sPath, const char* szNewExt)
    {
      const size_t dot = sPath.find_last_of('.');
      if (dot == std::string::npos)
        return sPath + "." + szNewExt;
      return sPath.substr(0, dot + 1) + szNewExt;
    }

    std::string JoinPath(const std::string& sDir, const std::string& sRel)
    {
      if (sDir.empty())
        return sRel;
      const char cLast = sDir.back();
      if (cLast == '/' || cLast == '\\')
        return sDir + sRel;
      return sDir + "/" + sRel;
    }

    bool FileExists(const std::string& sPath)
    {
      FILE* pFile = std::fopen(sPath.c_str(), "rb");
      if (!pFile)
        return false;
      std::fclose(pFile);
      return true;
    }

    // ---------- minimal DDS decoder (DXT1, DXT5, uncompressed 24/32-bit) ----------

    uint32_t ReadU32(const unsigned char* p)
    {
      return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    uint32_t PopCount32(uint32_t v)
    {
      v = v - ((v >> 1) & 0x55555555u);
      v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
      return (((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
    }

    void DecodeColorBlock(const unsigned char* pBlock, bool bDxt1, unsigned char out_Rgba[4][4][4])
    {
      const uint32_t c0 = (uint32_t)pBlock[0] | ((uint32_t)pBlock[1] << 8);
      const uint32_t c1 = (uint32_t)pBlock[2] | ((uint32_t)pBlock[3] << 8);

      unsigned char colors[4][4];
      for (int i = 0; i < 2; ++i)
      {
        const uint32_t c = (i == 0) ? c0 : c1;
        colors[i][0] = (unsigned char)((((c >> 11) & 0x1F) * 255 + 15) / 31);
        colors[i][1] = (unsigned char)((((c >> 5) & 0x3F) * 255 + 31) / 63);
        colors[i][2] = (unsigned char)(((c & 0x1F) * 255 + 15) / 31);
        colors[i][3] = 255;
      }

      if (!bDxt1 || c0 > c1)
      {
        for (int k = 0; k < 3; ++k)
        {
          colors[2][k] = (unsigned char)((2 * colors[0][k] + colors[1][k]) / 3);
          colors[3][k] = (unsigned char)((colors[0][k] + 2 * colors[1][k]) / 3);
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
      }
      else
      {
        for (int k = 0; k < 3; ++k)
          colors[2][k] = (unsigned char)((colors[0][k] + colors[1][k]) / 2);
        colors[2][3] = 255;
        colors[3][0] = colors[3][1] = colors[3][2] = 0;
        colors[3][3] = 0; // DXT1 1-bit alpha
      }

      const uint32_t indices = ReadU32(pBlock + 4);
      for (int py = 0; py < 4; ++py)
      {
        for (int px = 0; px < 4; ++px)
        {
          const uint32_t idx = (indices >> (2 * (4 * py + px))) & 0x3;
          out_Rgba[py][px][0] = colors[idx][0];
          out_Rgba[py][px][1] = colors[idx][1];
          out_Rgba[py][px][2] = colors[idx][2];
          out_Rgba[py][px][3] = colors[idx][3];
        }
      }
    }

    void DecodeAlphaBlockDxt5(const unsigned char* pBlock, unsigned char out_Rgba[4][4][4])
    {
      const unsigned char a0 = pBlock[0];
      const unsigned char a1 = pBlock[1];

      unsigned char alphas[8];
      alphas[0] = a0;
      alphas[1] = a1;
      if (a0 > a1)
      {
        for (int i = 1; i < 7; ++i)
          alphas[1 + i] = (unsigned char)(((7 - i) * a0 + i * a1) / 7);
      }
      else
      {
        for (int i = 1; i < 5; ++i)
          alphas[1 + i] = (unsigned char)(((5 - i) * a0 + i * a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
      }

      // 16 x 3-bit indices packed into 6 bytes
      uint64_t bits = 0;
      for (int i = 0; i < 6; ++i)
        bits |= ((uint64_t)pBlock[2 + i]) << (8 * i);

      for (int py = 0; py < 4; ++py)
      {
        for (int px = 0; px < 4; ++px)
        {
          const uint32_t idx = (uint32_t)((bits >> (3 * (4 * py + px))) & 0x7);
          out_Rgba[py][px][3] = alphas[idx];
        }
      }
    }

    bool LoadDds(const std::string& sPath, Image& out_Image)
    {
      FILE* pFile = std::fopen(sPath.c_str(), "rb");
      if (!pFile)
        return false;

      unsigned char header[128];
      if (std::fread(header, 1, 128, pFile) != 128 || ReadU32(header) != 0x20534444) // "DDS "
      {
        std::fclose(pFile);
        return false;
      }

      const int iHeight = (int)ReadU32(header + 12);
      const int iWidth = (int)ReadU32(header + 16);
      const uint32_t uiFourCC = ReadU32(header + 84);
      const uint32_t uiRgbBitCount = ReadU32(header + 88);
      const uint32_t uiRMask = ReadU32(header + 92);
      const uint32_t uiGMask = ReadU32(header + 96);
      const uint32_t uiBMask = ReadU32(header + 100);
      const uint32_t uiAMask = ReadU32(header + 104);

      const uint32_t FOURCC_DXT1 = 0x31545844;
      const uint32_t FOURCC_DXT5 = 0x35545844;

      out_Image.m_iWidth = iWidth;
      out_Image.m_iHeight = iHeight;
      out_Image.m_Rgba.resize((size_t)iWidth * iHeight * 4);

      if (uiFourCC == FOURCC_DXT1 || uiFourCC == FOURCC_DXT5)
      {
        const bool bDxt1 = (uiFourCC == FOURCC_DXT1);
        const size_t blockSize = bDxt1 ? 8 : 16;
        unsigned char block[16];
        unsigned char pixels[4][4][4];

        for (int by = 0; by < (iHeight + 3) / 4; ++by)
        {
          for (int bx = 0; bx < (iWidth + 3) / 4; ++bx)
          {
            if (std::fread(block, 1, blockSize, pFile) != blockSize)
            {
              std::fclose(pFile);
              return false;
            }

            if (bDxt1)
            {
              DecodeColorBlock(block, true, pixels);
            }
            else
            {
              DecodeColorBlock(block + 8, false, pixels);
              DecodeAlphaBlockDxt5(block, pixels);
            }

            for (int py = 0; py < 4; ++py)
            {
              const int y = by * 4 + py;
              if (y >= iHeight)
                break;
              for (int px = 0; px < 4; ++px)
              {
                const int x = bx * 4 + px;
                if (x >= iWidth)
                  break;
                unsigned char* dst = &out_Image.m_Rgba[((size_t)y * iWidth + x) * 4];
                dst[0] = pixels[py][px][0];
                dst[1] = pixels[py][px][1];
                dst[2] = pixels[py][px][2];
                dst[3] = pixels[py][px][3];
              }
            }
          }
        }
      }
      else if (uiRgbBitCount == 32 || uiRgbBitCount == 24)
      {
        // uncompressed; honor the channel masks (typically BGR(A))
        const uint32_t masks[4] = {uiRMask, uiGMask, uiBMask, uiAMask};
        uint32_t shifts[4] = {0, 0, 0, 0};
        for (int c = 0; c < 4; ++c)
          while (masks[c] != 0 && ((masks[c] >> shifts[c]) & 1) == 0)
            ++shifts[c];

        const size_t pixelCount = (size_t)iWidth * iHeight;
        for (size_t i = 0; i < pixelCount; ++i)
        {
          unsigned char raw[4] = {0, 0, 0, 0};
          if (std::fread(raw, 1, uiRgbBitCount / 8, pFile) != uiRgbBitCount / 8)
          {
            std::fclose(pFile);
            return false;
          }
          const uint32_t px = ReadU32(raw);
          unsigned char* dst = &out_Image.m_Rgba[i * 4];
          for (int c = 0; c < 3; ++c)
          {
            const uint32_t v = masks[c] ? ((px & masks[c]) >> shifts[c]) : 0;
            const uint32_t bits = PopCount32(masks[c]);
            dst[c] = (unsigned char)(bits >= 8 ? (v >> (bits - 8)) : (v * 255 / ((1u << bits) - 1)));
          }
          dst[3] = uiAMask ? (unsigned char)(((px & uiAMask) >> shifts[3]) * 255 / (uiAMask >> shifts[3])) : 255;
        }
      }
      else
      {
        std::fclose(pFile);
        return false;
      }

      std::fclose(pFile);
      return true;
    }
  } // namespace

  bool LoadImageFile(const std::string& sPath, Image& out_Image)
  {
    const std::string sExt = GetExtension(sPath);

    if (sExt == "dds")
      return LoadDds(sPath, out_Image);

    int w = 0, h = 0, channels = 0;
    stbi_uc* pData = stbi_load(sPath.c_str(), &w, &h, &channels, 4);
    if (!pData)
      return false;

    out_Image.m_iWidth = w;
    out_Image.m_iHeight = h;
    out_Image.m_Rgba.assign(pData, pData + (size_t)w * h * 4);
    stbi_image_free(pData);
    return true;
  }

  std::string ResolveTexturePath(const std::string& sRef, const std::vector<std::string>& roots)
  {
    if (sRef.empty())
      return "";

    // absolute path or relative to cwd
    if (FileExists(sRef))
      return sRef;

    // build candidate names: as-is, extension-swapped, "tga/" subdirectory variant
    std::vector<std::string> candidates;
    candidates.push_back(sRef);

    const std::string sExt = GetExtension(sRef);
    if (sExt == "dds")
    {
      candidates.push_back(ReplaceExtension(sRef, "tga"));
      candidates.push_back(ReplaceExtension(sRef, "png"));

      // repo convention: Textures/Foo/Bar.dds -> Textures/Foo/tga/Bar.tga
      const size_t slash = sRef.find_last_of("/\\");
      if (slash != std::string::npos)
        candidates.push_back(sRef.substr(0, slash + 1) + "tga/" + ReplaceExtension(sRef.substr(slash + 1), "tga"));
    }
    else if (sExt == "tga" || sExt == "png")
    {
      candidates.push_back(ReplaceExtension(sRef, "dds"));
    }

    for (const std::string& sRoot : roots)
    {
      for (const std::string& sCandidate : candidates)
      {
        const std::string sFull = JoinPath(sRoot, sCandidate);
        if (FileExists(sFull))
          return sFull;
      }
    }

    return "";
  }
} // namespace KrautPreview
