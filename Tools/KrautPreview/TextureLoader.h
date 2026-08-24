#pragma once

// Texture loading for KrautPreview:
//  - .tga/.png/.jpg/.bmp via stb_image
//  - .dds via a minimal built-in decoder (DXT1, DXT5, uncompressed 24/32-bit)
// Also resolves texture references stored in .tree files (e.g. "Textures/Leaves/Foo.dds")
// against a list of data roots.

#include <string>
#include <vector>

namespace KrautPreview
{
  struct Image
  {
    int m_iWidth = 0;
    int m_iHeight = 0;
    std::vector<unsigned char> m_Rgba; // 4 bytes per pixel, top row first
  };

  // Loads an image file into RGBA8. Returns false on unsupported format / IO error.
  bool LoadImageFile(const std::string& sPath, Image& out_Image);

  // Finds the actual file for a texture reference from a .tree file by probing
  // each root directory. Also tries .dds <-> .tga extension swaps and the repo's
  // "tga/" subdirectory convention. Returns an empty string if not found.
  std::string ResolveTexturePath(const std::string& sRef, const std::vector<std::string>& roots);
} // namespace KrautPreview
