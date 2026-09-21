#pragma once
#include "core/types.h"
#include "graphics/rect.h"
#include <cstddef>
#include <cstdint>
#include "d3d8.h"

namespace as1
{
    class BaseStream;
    struct BaseTextureCaps
    {
        int maxWidth = 8192;
        int maxHeight = 8192;
        bool requirePowerOfTwo = true;
        bool requireSquare = false;
        bool conditionalNonPowerOfTwo = false;
        bool dynamicTextures = false;
        bool paletteTextures = false;
        bool alphaPaletteTextures = false;
        DWORD compressedFormatMask = 0;
    };



    class BASE_TEXTURE
    {
    public:

        BASE_TEXTURE(int width, int height, DWORD format, DWORD flags);
        BASE_TEXTURE(int width, int height, DWORD format, DWORD flags,
                     const DWORD* paletteEntries, BaseStream* stream);
        virtual ~BASE_TEXTURE();

        BASE_TEXTURE(const BASE_TEXTURE&) = delete;
        BASE_TEXTURE& operator=(const BASE_TEXTURE&) = delete;


        static void ConfigureCaps(const BaseTextureCaps& caps);
        static DWORD CompressedFormatMask() noexcept;
        static DWORD TextureMemoryBytes() noexcept;

        bool isSoftwareBacked() const { return (m_flags & 0x2u) != 0; }
        bool isLoaded() const { return isSoftwareBacked() ? m_softwarePixels != nullptr : m_nativeHandle != nullptr; }
        int width() const { return m_width; }
        int height() const { return m_height; }
        int pitchWords() const { return m_width; }
        DWORD format() const { return m_format; }
        DWORD flags() const { return m_flags; }

        std::uint16_t* lock16(int* pitchBytes, const RECTI* rect = nullptr);
        const std::uint16_t* lock16(int* pitchBytes, const RECTI* rect = nullptr) const;
        void unlock();

        void* nativeHandle() const { return m_nativeHandle; }
        int paletteSlot() const { return m_paletteSlot; }
        std::intptr_t uploadPaletteEntries(const void* paletteEntries);
        void createPaletteSlot(const DWORD* paletteEntries);
        void SetTexture(int stage) noexcept;

        static constexpr std::size_t ObjectSize = 0x20u;

        int PrepareSurfaceCopy(IDirect3DSurface8* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin);
        void DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, const DWORD* colors);
        void DrawDepthRectangle(DWORD zTop, DWORD zBottom, const RECTI& destination, const RECTI& source, const DWORD* colors);

    private:
        void* m_softwarePixels;
        IDirect3DTexture8* m_nativeHandle;
        DWORD m_format;
        int m_width;
        int m_height;
        int m_paletteSlot;
        DWORD m_flags;
    };
}
