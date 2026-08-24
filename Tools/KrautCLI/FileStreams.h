#pragma once

// Minimal aeStreamIn/aeStreamOut implementations over C stdio.
// The editor uses aeFileIn/aeFileOut from KrautGraphics (with a data-directory
// filesystem and read cache); the byte-level wire format is identical, so these
// simple replacements keep full .tree / .kraut compatibility without pulling in
// the rendering library.

#include <KrautFoundation/Streams/Streams.h>
#include <cstdio>

namespace AE_NS_FOUNDATION
{
  class cliFileIn : public aeStreamIn
  {
  public:
    cliFileIn() = default;
    ~cliFileIn() { Close(); }

    bool Open(const char* szFile)
    {
      Close();
      m_pFile = std::fopen(szFile, "rb");
      m_bIsOpen = (m_pFile != nullptr);
      return m_bIsOpen;
    }

    void Close()
    {
      if (m_pFile)
      {
        std::fclose(m_pFile);
        m_pFile = nullptr;
      }
      m_bIsOpen = false;
    }

  private:
    virtual aeUInt32 ReadFromStream(void* pData, aeUInt32 uiSize) override
    {
      if (!m_pFile)
        return 0;
      return static_cast<aeUInt32>(std::fread(pData, 1, uiSize, m_pFile));
    }

    std::FILE* m_pFile = nullptr;
  };

  class cliFileOut : public aeStreamOut
  {
  public:
    cliFileOut() = default;
    ~cliFileOut() { Close(); }

    bool Open(const char* szFile)
    {
      Close();
      m_pFile = std::fopen(szFile, "wb");
      m_bIsOpen = (m_pFile != nullptr);
      return m_bIsOpen;
    }

    void Close()
    {
      if (m_pFile)
      {
        std::fclose(m_pFile);
        m_pFile = nullptr;
      }
      m_bIsOpen = false;
    }

  private:
    virtual void WriteToStream(const void* pData, aeUInt32 uiSize) override
    {
      if (m_pFile)
        std::fwrite(pData, 1, uiSize, m_pFile);
    }

    std::FILE* m_pFile = nullptr;
  };
} // namespace AE_NS_FOUNDATION
