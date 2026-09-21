#include "graphics/base_texture.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/base_stream.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>
#include "d3d8.h"
#include "graph.h"

namespace as1
{

    namespace
    {
        constexpr HRESULT kInvalidCall = static_cast<HRESULT>(0x8876086Cu);

        bool isDxtFormat(D3DFORMAT format) noexcept
        {
            const DWORD f = static_cast<DWORD>(format);
            return f == 0x31545844u || f == 0x33545844u || f == 0x35545844u;
        }

        unsigned formatBits(D3DFORMAT format) noexcept
        {
            switch (static_cast<DWORD>(format))
            {
            case 20u: return 24u;
            case 21u:
            case 22u: return 32u;
            case 23u:
            case 24u:
            case 25u:
            case 26u: return 16u;
            case 41u: return 8u;
            case 0x31545844u: return 4u;
            case 0x33545844u:
            case 0x35545844u: return 8u;
            default: return 0u;
            }
        }

        std::uint32_t paletteColor(const PALETTEENTRY* palette, unsigned index) noexcept
        {
            if (!palette)
                return 0xFF000000u;
            const PALETTEENTRY& p = palette[index & 0xFFu];
            return 0xFF000000u | (static_cast<DWORD>(p.peRed) << 16u) |
                   (static_cast<DWORD>(p.peGreen) << 8u) | static_cast<DWORD>(p.peBlue);
        }

        std::uint32_t decodePixel(const BYTE* source, D3DFORMAT format,
                                  const PALETTEENTRY* palette) noexcept
        {
            const DWORD f = static_cast<DWORD>(format);
            if (f == 41u)
                return paletteColor(palette, *source);
            if (f == 20u)
                return 0xFF000000u | (static_cast<DWORD>(source[2]) << 16u) |
                       (static_cast<DWORD>(source[1]) << 8u) | source[0];
            if (f == 21u || f == 22u)
            {
                DWORD v = 0;
                std::memcpy(&v, source, sizeof(v));
                return f == 22u ? (v | 0xFF000000u) : v;
            }

            WORD v = 0;
            std::memcpy(&v, source, sizeof(v));
            if (f == 23u)
            {
                const DWORD r = ((v >> 11u) & 31u) * 255u / 31u;
                const DWORD g = ((v >> 5u) & 63u) * 255u / 63u;
                const DWORD b = (v & 31u) * 255u / 31u;
                return 0xFF000000u | (r << 16u) | (g << 8u) | b;
            }
            if (f == 24u || f == 25u)
            {
                const DWORD a = f == 25u && (v & 0x8000u) == 0u ? 0u : 255u;
                const DWORD r = ((v >> 10u) & 31u) * 255u / 31u;
                const DWORD g = ((v >> 5u) & 31u) * 255u / 31u;
                const DWORD b = (v & 31u) * 255u / 31u;
                return (a << 24u) | (r << 16u) | (g << 8u) | b;
            }
            if (f == 26u)
            {
                const DWORD a = ((v >> 12u) & 15u) * 17u;
                const DWORD r = ((v >> 8u) & 15u) * 17u;
                const DWORD g = ((v >> 4u) & 15u) * 17u;
                const DWORD b = (v & 15u) * 17u;
                return (a << 24u) | (r << 16u) | (g << 8u) | b;
            }
            return 0u;
        }

        void encodePixel(BYTE* destination, D3DFORMAT format, std::uint32_t argb) noexcept
        {
            const DWORD f = static_cast<DWORD>(format);
            const DWORD a = (argb >> 24u) & 0xFFu;
            const DWORD r = (argb >> 16u) & 0xFFu;
            const DWORD g = (argb >> 8u) & 0xFFu;
            const DWORD b = argb & 0xFFu;
            if (f == 20u)
            {
                destination[0] = static_cast<BYTE>(b);
                destination[1] = static_cast<BYTE>(g);
                destination[2] = static_cast<BYTE>(r);
                return;
            }
            if (f == 21u || f == 22u)
            {
                DWORD v = f == 22u ? (argb | 0xFF000000u) : argb;
                std::memcpy(destination, &v, sizeof(v));
                return;
            }
            WORD v = 0;
            if (f == 23u)
                v = static_cast<WORD>(((r >> 3u) << 11u) | ((g >> 2u) << 5u) | (b >> 3u));
            else if (f == 24u)
                v = static_cast<WORD>(0x8000u | ((r >> 3u) << 10u) | ((g >> 3u) << 5u) | (b >> 3u));
            else if (f == 25u)
                v = static_cast<WORD>(((a >= 128u ? 1u : 0u) << 15u) | ((r >> 3u) << 10u) |
                                      ((g >> 3u) << 5u) | (b >> 3u));
            else if (f == 26u)
                v = static_cast<WORD>(((a >> 4u) << 12u) | ((r >> 4u) << 8u) |
                                      ((g >> 4u) << 4u) | (b >> 4u));
            std::memcpy(destination, &v, sizeof(v));
        }

        HRESULT copyMemoryToSurface(IDirect3DSurface8* destinationSurface,
                                    const RECTI* destinationRect,
                                    const void* sourceMemory,
                                    D3DFORMAT sourceFormat,
                                    UINT sourcePitch,
                                    const PALETTEENTRY* sourcePalette,
                                    const RECTI* sourceRect) noexcept
        {
            if (!destinationSurface || !sourceMemory)
                return kInvalidCall;

            D3DSURFACE_DESC8 desc{};
            HRESULT hr = destinationSurface->GetDesc(&desc);
            if (FAILED(hr))
                return hr;

            RECT src{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
            if (sourceRect)
                src = *reinterpret_cast<const RECT*>(sourceRect);
            RECT dst{0, 0, src.right - src.left, src.bottom - src.top};
            if (destinationRect)
                dst = *reinterpret_cast<const RECT*>(destinationRect);

            const LONG width = src.right - src.left;
            const LONG height = src.bottom - src.top;
            if (width <= 0 || height <= 0 || dst.right - dst.left != width || dst.bottom - dst.top != height)
                return kInvalidCall;

            D3DLOCKED_RECT locked{};
            hr = destinationSurface->LockRect(&locked, &dst, 0u);
            if (FAILED(hr))
                return hr;

            const BYTE* sourceBase = static_cast<const BYTE*>(sourceMemory);
            BYTE* destinationBase = static_cast<BYTE*>(locked.pBits);
            const unsigned srcBits = formatBits(sourceFormat);
            const unsigned dstBits = formatBits(desc.Format);

            if (sourceFormat == desc.Format)
            {
                if (isDxtFormat(sourceFormat))
                {
                    const UINT rows = static_cast<UINT>((height + 3) / 4);
                    for (UINT y = 0; y < rows; ++y)
                        std::memcpy(destinationBase + y * locked.Pitch,
                                    sourceBase + y * sourcePitch,
                                    std::min<UINT>(sourcePitch, static_cast<UINT>(locked.Pitch)));
                }
                else
                {
                    const UINT rowBytes = static_cast<UINT>(width) * srcBits / 8u;
                    const UINT sourceOffset = static_cast<UINT>(src.top) * sourcePitch +
                        static_cast<UINT>(src.left) * srcBits / 8u;
                    sourceBase += sourceOffset;
                    for (LONG y = 0; y < height; ++y)
                        std::memcpy(destinationBase + y * locked.Pitch,
                                    sourceBase + static_cast<UINT>(y) * sourcePitch,
                                    rowBytes);
                }
            }
            else if (!isDxtFormat(sourceFormat) && !isDxtFormat(desc.Format) &&
                     srcBits != 0u && dstBits != 0u && sourceFormat != static_cast<D3DFORMAT>(41u))
            {
                const UINT srcBytes = srcBits / 8u;
                const UINT dstBytes = dstBits / 8u;
                const BYTE* first = sourceBase + static_cast<UINT>(src.top) * sourcePitch +
                    static_cast<UINT>(src.left) * srcBytes;
                for (LONG y = 0; y < height; ++y)
                {
                    const BYTE* sr = first + static_cast<UINT>(y) * sourcePitch;
                    BYTE* dr = destinationBase + y * locked.Pitch;
                    for (LONG x = 0; x < width; ++x)
                        encodePixel(dr + x * dstBytes, desc.Format,
                                    decodePixel(sr + x * srcBytes, sourceFormat, sourcePalette));
                }
            }
            else if (sourceFormat == static_cast<D3DFORMAT>(41u) && !isDxtFormat(desc.Format) && dstBits != 0u)
            {
                const UINT dstBytes = dstBits / 8u;
                const BYTE* first = sourceBase + static_cast<UINT>(src.top) * sourcePitch +
                    static_cast<UINT>(src.left);
                for (LONG y = 0; y < height; ++y)
                {
                    const BYTE* sr = first + static_cast<UINT>(y) * sourcePitch;
                    BYTE* dr = destinationBase + y * locked.Pitch;
                    for (LONG x = 0; x < width; ++x)
                        encodePixel(dr + x * dstBytes, desc.Format, paletteColor(sourcePalette, sr[x]));
                }
            }
            else
            {
                hr = kInvalidCall;
            }

            const HRESULT unlockHr = destinationSurface->UnlockRect();
            return FAILED(hr) ? hr : unlockHr;
        }
    }

    static HRESULT __stdcall loadSurfaceFromMemory(
        IDirect3DSurface8* destinationSurface,
        const PALETTEENTRY*,
        const RECTI* destinationRect,
        const void* sourceMemory,
        D3DFORMAT sourceFormat,
        UINT sourcePitch,
        const PALETTEENTRY* sourcePalette,
        const RECTI* sourceRect,
        DWORD,
        D3DCOLOR)
    {
        return copyMemoryToSurface(destinationSurface, destinationRect, sourceMemory,
                                   sourceFormat, sourcePitch, sourcePalette, sourceRect);
    }


    static HRESULT __stdcall loadSurfaceFromSurface(
        IDirect3DSurface8* destinationSurface,
        const PALETTEENTRY* destinationPalette,
        const RECTI* destinationRect,
        IDirect3DSurface8* sourceSurface,
        const PALETTEENTRY* sourcePalette,
        const RECTI* sourceRect,
        DWORD filterFlags,
        D3DCOLOR colorKey)
    {
        if (!destinationSurface || !sourceSurface)
            return kInvalidCall;


        const bool palettesMatch = destinationPalette == sourcePalette;
        const bool noColorKey = colorKey == 0u;
        const bool filterAllowsFastCopy = (filterFlags & 0xFFFFu) != 5u;
        bool equalExtent = true;
        if (destinationRect && sourceRect)
        {
            equalExtent =
                destinationRect->right - destinationRect->left == sourceRect->right - sourceRect->left &&
                destinationRect->bottom - destinationRect->top == sourceRect->bottom - sourceRect->top;
        }

        if (palettesMatch && noColorKey && filterAllowsFastCopy && equalExtent)
        {
            RECT sourceNative{};
            const RECT* sourceNativePtr = nullptr;
            if (sourceRect)
            {
                sourceNative = *reinterpret_cast<const RECT*>(sourceRect);
                sourceNativePtr = &sourceNative;
            }

            POINT destinationPoint{0, 0};
            const POINT* destinationPointPtr = nullptr;
            if (destinationRect)
            {
                destinationPoint.x = destinationRect->left;
                destinationPoint.y = destinationRect->top;
                destinationPointPtr = &destinationPoint;
            }

            IDirect3DDevice8* device = nullptr;
            HRESULT result = sourceSurface->GetDevice(&device);
            if (FAILED(result) || !device)
                return result;
            result = device->CopyRects(sourceSurface, sourceNativePtr, sourceNativePtr ? 1u : 0u,
                                       destinationSurface, destinationPointPtr);
            device->Release();
            return result;
        }


        return kInvalidCall;
    }

    namespace
    {
        struct RECTANGLE_VERTEX_D3D
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float rhw = 1.0f;
            DWORD diffuse = 0xFFFFFFFFu;
            DWORD specular = 0u;
            float u = 0.0f;
            float v = 0.0f;
        };

        struct RECTANGLE_VERTEXES_D3D
        {
            RECTANGLE_VERTEX_D3D v[4];
        };

        BaseTextureCaps g_baseTextureCaps;
        int g_maxTextureWidth = 8192;
        int g_maxTextureHeight = 8192;
        DWORD g_squareTextureMask = 0;
        DWORD g_dynamicTextureFlag = 0;
        DWORD g_powerOfTwoTextureFlag = 2;
        DWORD g_conditionalNonPowerOfTwoFlag = 0;
        DWORD g_paletteTextureSupport = 0;
        DWORD g_alphaPaletteTextureSupport = 0;
        DWORD g_compressedTextureFormatMask = 0;
        DWORD g_textureMemoryBytes = 0;
        DWORD g_paletteSlotCounter = 0;


        unsigned textureBitsPerPixel(DWORD format) noexcept
        {
            switch (format)
            {
            case 20u: return 24u;
            case 21u:
            case 22u: return 32u;
            case 23u:
            case 24u:
            case 25u:
            case 26u: return 16u;
            case 41u: return 8u;
            case 0x31545844u: return 4u;
            case 0x33545844u:
            case 0x35545844u: return 8u;
            default: return 0u;
            }
        }

        bool textureCompressedPitch(DWORD format) noexcept
        {
            return format == 0x31545844u ||
                   format == 0x33545844u ||
                   format == 0x35545844u;
        }

        void convertPalette(const DWORD* source, BYTE* destination) noexcept
        {
            for (std::size_t i = 0; i < 256u; ++i)
            {
                const DWORD color = source[i];
                destination[i * 4u + 0u] = static_cast<BYTE>((color >> 16u) & 0xFFu);
                destination[i * 4u + 1u] = static_cast<BYTE>((color >> 8u) & 0xFFu);
                destination[i * 4u + 2u] = static_cast<BYTE>(color & 0xFFu);
                destination[i * 4u + 3u] = static_cast<BYTE>((color >> 24u) & 0xFFu);
            }
        }

        int nextPower2(int value)
        {
            int out = 1;
            while (out < value && out < 8192)
                out <<= 1;
            return out;
        }

        IDirect3DDevice8* currentTextureDevice()
        {
            return static_cast<IDirect3DDevice8*>(Graph ? Graph->deviceHandle() : nullptr);
        }


        constexpr DWORD kSurfaceCopyInvalidCall = 0x8876086Cu;
    }


    BASE_TEXTURE::BASE_TEXTURE(int width, int height, DWORD format, DWORD flags)
    {

        m_nativeHandle = nullptr;
        m_softwarePixels = nullptr;

        DWORD adjustedFlags = flags;
        if (g_dynamicTextureFlag == 0)
            adjustedFlags &= 0xFFFFFFFEu;

        int normalizedWidth = width;
        int normalizedHeight = height;
        if (g_squareTextureMask != 0)
        {
            if (normalizedWidth > normalizedHeight)
                normalizedHeight = normalizedWidth;
            else if (normalizedWidth < normalizedHeight)
                normalizedWidth = normalizedHeight;
        }

        if (g_powerOfTwoTextureFlag != 0)
        {
            std::int32_t value = 1;
            while (normalizedWidth > value)
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 1u);
            normalizedWidth = value;

            value = 1;
            while (normalizedHeight > value)
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 1u);
            normalizedHeight = value;
        }

        if (normalizedWidth > g_maxTextureWidth || normalizedWidth <= 0)
        {
            LOG::ResourceError("TEXTURE", 4, "initial sizeX", normalizedWidth);
            return;
        }
        if (normalizedHeight > g_maxTextureHeight || normalizedHeight <= 0)
        {
            LOG::ResourceError("TEXTURE", 4, "initial sizeY", normalizedHeight);
            return;
        }

        if (format == 0x15u || format == 0x1Au || format == 0x19u ||
            format == 0x33545844u || format == 0x35545844u)
        {
            adjustedFlags |= 0x8u;
        }

        DWORD finalFormat = format;


        if (finalFormat == 0x29u && (adjustedFlags & 0x8u) != 0u &&
            g_alphaPaletteTextureSupport == 0u)
        {
            finalFormat = 0x15u;
        }

        if ((adjustedFlags & 0x10u) != 0u)
        {
            if (finalFormat == 0x29u && (adjustedFlags & 0x8u) == 0u)
                finalFormat = 0x19u;
            else if (finalFormat == 0x17u)
                finalFormat = 0x19u;
            else if (finalFormat == 0x14u)
                finalFormat = 0x15u;
        }


        if (finalFormat == 0x29u &&
            g_paletteTextureSupport == 0u)
        {
            finalFormat = 0x17u;
        }

        HRESULT result = D3D_OK;
        if ((adjustedFlags & 0x2u) != 0 && finalFormat == 0x50u)
        {
            const DWORD allocationBytes = static_cast<DWORD>(normalizedWidth) *
                static_cast<DWORD>(normalizedHeight) * 2u;
            m_softwarePixels = ::operator new(static_cast<std::size_t>(allocationBytes));
        }
        else
        {
            DWORD usage = (adjustedFlags & 0x4u) != 0 ? 1u : 0u;
            const bool dynamicTexture = (adjustedFlags & 0x1u) != 0;
            if (dynamicTexture)
                usage |= 0x200u;

            IDirect3DDevice8* device = currentTextureDevice();
            for (;;)
            {
                result = device->CreateTexture(
                    static_cast<UINT>(normalizedWidth),
                    static_cast<UINT>(normalizedHeight),
                    1u,
                    usage,
                    static_cast<D3DFORMAT>(finalFormat),
                    static_cast<D3DPOOL>(dynamicTexture ? 0u : 1u),
                    &m_nativeHandle);
                if (result == D3D_OK)
                    break;

                DWORD fallback = finalFormat;
                switch (finalFormat)
                {
                case 41u:          fallback = (adjustedFlags & 0x8u) != 0u ? 21u : 23u; break;
                case 0x31545844u: fallback = 23u; break;
                case 0x33545844u: fallback = 26u; break;
                case 23u:         fallback = 24u; break;
                case 24u:         fallback = 25u; break;
                case 20u:         fallback = 22u; break;
                case 22u:         fallback = 21u; break;
                default: break;
                }
                if (fallback == finalFormat)
                    break;
                finalFormat = fallback;
            }
        }

        m_width = normalizedWidth;
        m_height = normalizedHeight;
        m_format = finalFormat;
        m_flags = adjustedFlags;

        if (result != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 3, "", static_cast<int>(result));
            return;
        }

        if (m_nativeHandle)
        {
            const DWORD bytesPerPixel = finalFormat == 0x29u ? 1u : 2u;
            g_textureMemoryBytes +=
                static_cast<DWORD>(normalizedWidth) *
                static_cast<DWORD>(normalizedHeight) * bytesPerPixel;
        }
    }


    BASE_TEXTURE::BASE_TEXTURE(int width, int height, DWORD format, DWORD flags,
                               const DWORD* paletteEntries, BaseStream* stream)
        : BASE_TEXTURE(width, height, format, flags)
    {
        std::array<BYTE, 256u * 4u> convertedPalette{};
        const BYTE* convertedPalettePtr = nullptr;
        if (paletteEntries)
        {
            convertPalette(paletteEntries, convertedPalette.data());
            convertedPalettePtr = convertedPalette.data();
        }

        if (m_format == 0x29u)
            (void)uploadPaletteEntries(convertedPalettePtr);

        const unsigned bitsPerPixel = textureBitsPerPixel(format);
        const std::size_t payloadBytes =
            (static_cast<std::size_t>(static_cast<std::uint32_t>(width)) *
             static_cast<std::size_t>(static_cast<std::uint32_t>(height)) *
             static_cast<std::size_t>(bitsPerPixel)) / 8u;

        BYTE* const payload = static_cast<BYTE*>(::operator new(payloadBytes));
        stream->read(payload, static_cast<unsigned>(payloadBytes));

        IDirect3DSurface8* destinationSurface = nullptr;
        (void)m_nativeHandle->GetSurfaceLevel(0u, &destinationSurface);

        RECTI sourceRect{0, 0, width, height};
        unsigned sourcePitch = static_cast<unsigned>(width) * bitsPerPixel;
        sourcePitch >>= textureCompressedPitch(format) ? 1u : 3u;

        const PALETTEENTRY* sourcePalette =
            m_format == 0x29u
                ? reinterpret_cast<const PALETTEENTRY*>(convertedPalettePtr)
                : nullptr;
        const D3DCOLOR colorKey = (m_flags & 8u) != 0u ? 0u : 0xFF000000u;
        (void)loadSurfaceFromMemory(
            destinationSurface,
            nullptr,
            nullptr,
            payload,
            static_cast<D3DFORMAT>(format),
            sourcePitch,
            sourcePalette,
            &sourceRect,
            1u,
            colorKey);
        destinationSurface->Release();

        ::operator delete(payload);
    }


    BASE_TEXTURE::~BASE_TEXTURE()
    {

        if (m_nativeHandle)
        {


            (void)currentTextureDevice()->SetTexture(0u, nullptr);

            const ULONG refs = m_nativeHandle->Release();
            if (refs != 0u)
            {
                LOG::ResourceError("TEXTURE", 10, "Release count !=0", static_cast<int>(refs));
            }
            else
            {
                const DWORD bytesPerPixel = m_format == 0x29u ? 1u : 2u;
                const DWORD bytes = static_cast<DWORD>(m_width) *
                    static_cast<DWORD>(m_height) * bytesPerPixel;


                g_textureMemoryBytes -= bytes;
            }
            m_nativeHandle = nullptr;
        }
        if (m_softwarePixels)
        {
            ::operator delete(m_softwarePixels);
            m_softwarePixels = nullptr;
        }
    }


    void BASE_TEXTURE::ConfigureCaps(const BaseTextureCaps& caps)
    {
        g_baseTextureCaps = caps;

        g_maxTextureWidth = g_baseTextureCaps.maxWidth;
        g_maxTextureHeight = g_baseTextureCaps.maxHeight;
        g_squareTextureMask = g_baseTextureCaps.requireSquare ? 0x20u : 0u;
        g_dynamicTextureFlag = g_baseTextureCaps.dynamicTextures ? 1u : 0u;
        g_powerOfTwoTextureFlag = g_baseTextureCaps.requirePowerOfTwo ? 2u : 0u;
        g_conditionalNonPowerOfTwoFlag =
            g_baseTextureCaps.conditionalNonPowerOfTwo ? 0x100u : 0u;
        g_paletteTextureSupport = g_baseTextureCaps.paletteTextures ? 1u : 0u;
        g_alphaPaletteTextureSupport =
            g_baseTextureCaps.alphaPaletteTextures ? 0x80u : 0u;
        g_compressedTextureFormatMask = g_baseTextureCaps.compressedFormatMask;

        std::string text;
        if (g_squareTextureMask)
            text += "SQUARE ";
        text += g_powerOfTwoTextureFlag ? "POWER2 " : "NOTPOWER2 ";
        if (g_conditionalNonPowerOfTwoFlag)
            text += "COND_NON_POW2 ";
        if (g_dynamicTextureFlag)
            text += "DYNAMIC ";
        if (g_paletteTextureSupport)
            text += "PALETTE ";
        if (g_alphaPaletteTextureSupport)
            text += "ALPHA_PALETTE ";
        if (g_compressedTextureFormatMask & 1u)
            text += "DXT1 ";
        if (g_compressedTextureFormatMask & 4u)
            text += "DXT3 ";
        if (g_compressedTextureFormatMask & 0x10u)
            text += "DXT5 ";
        char maxSizeText[64] = {};
        std::snprintf(maxSizeText, sizeof(maxSizeText), "MAXSIZE=%i,%i",
                      g_maxTextureWidth,
                      g_maxTextureHeight);
        text += maxSizeText;
        LOG::Write("TextureCaps=%s", text.c_str());

        g_powerOfTwoTextureFlag = 1u;
    }


    DWORD BASE_TEXTURE::CompressedFormatMask() noexcept
    {
        return g_compressedTextureFormatMask;
    }

    DWORD BASE_TEXTURE::TextureMemoryBytes() noexcept
    {
        return g_textureMemoryBytes;
    }




    std::intptr_t BASE_TEXTURE::uploadPaletteEntries(const void* paletteEntries)
    {

        if (!m_nativeHandle)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palette for non initialized texture", 0);
        if (m_format != 0x29u)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palette for non palette texture", 0);

        const HRESULT result = currentTextureDevice()->SetPaletteEntries(
            static_cast<UINT>(g_paletteSlotCounter),
            static_cast<const PALETTEENTRY*>(paletteEntries));
        if (result < 0)
            return logFileLoggerResourceError(g_fileLogger, "TEXTURE", 8, "palettes", static_cast<int>(result));

        m_paletteSlot = static_cast<int>(g_paletteSlotCounter);
        ++g_paletteSlotCounter;
        return static_cast<std::intptr_t>(g_paletteSlotCounter);
    }


    void BASE_TEXTURE::createPaletteSlot(const DWORD* paletteEntries)
    {
        std::array<BYTE, 256u * 4u> convertedPalette{};
        convertPalette(paletteEntries, convertedPalette.data());
        (void)uploadPaletteEntries(convertedPalette.data());
    }


    void BASE_TEXTURE::SetTexture(int stage) noexcept
    {
        (void)currentTextureDevice()->SetTexture(static_cast<DWORD>(stage), m_nativeHandle);
    }



    std::uint16_t* BASE_TEXTURE::lock16(int* pitchBytes, const RECTI* rect)
    {

        if ((m_flags & 0x2u) != 0)
        {

            *pitchBytes = static_cast<int>(static_cast<std::uint32_t>(m_width) << 1u);
            if (rect)
            {
                const std::uint32_t row = static_cast<std::uint32_t>(rect->top) *
                                          static_cast<std::uint32_t>(m_width);
                const int offsetPixels = static_cast<int>(row + static_cast<std::uint32_t>(rect->left));
                return reinterpret_cast<std::uint16_t*>(m_softwarePixels) + offsetPixels;
            }
            return reinterpret_cast<std::uint16_t*>(m_softwarePixels);
        }

        D3DLOCKED_RECT lockedRect;
        const HRESULT result = m_nativeHandle->LockRect(
            0u,
            &lockedRect,
            reinterpret_cast<const RECT*>(rect),
            0u);
        if (result != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 0, "", static_cast<int>(result));
            return nullptr;
        }
        *pitchBytes = lockedRect.Pitch;
        return static_cast<std::uint16_t*>(lockedRect.pBits);
    }

    const std::uint16_t* BASE_TEXTURE::lock16(int* pitchBytes, const RECTI* rect) const
    {
        return const_cast<BASE_TEXTURE*>(this)->lock16(pitchBytes, rect);
    }

    void BASE_TEXTURE::unlock()
    {

        if (m_nativeHandle)
            m_nativeHandle->UnlockRect(0u);
    }


    int BASE_TEXTURE::PrepareSurfaceCopy(IDirect3DSurface8* sourceSurface, const RECTI& sourceRect, const RECTI* destinationOrigin)
    {

        IDirect3DSurface8* destinationSurface;
        const HRESULT surfaceResult = m_nativeHandle->GetSurfaceLevel(0u, &destinationSurface);
        if (surfaceResult != D3D_OK)
        {
            LOG::ResourceError("TEXTURE", 9, "surface for copy", static_cast<int>(surfaceResult));
            return static_cast<int>(surfaceResult);
        }

        RECTI destinationRect;
        destinationRect.left = destinationOrigin->left;
        destinationRect.top = destinationOrigin->top;
        destinationRect.right = destinationOrigin->left + (sourceRect.right - sourceRect.left);
        destinationRect.bottom = destinationOrigin->top + (sourceRect.bottom - sourceRect.top);

        const HRESULT copyResult = loadSurfaceFromSurface(
            destinationSurface,
            nullptr,
            &destinationRect,
            sourceSurface,
            nullptr,
            &sourceRect,
            0xFFFFFFFFu,
            0u);
        if (copyResult != D3D_OK)
            LOG::ResourceError("TEXTURE", 1, "from surface", static_cast<int>(copyResult));

        destinationSurface->Release();
        return static_cast<int>(copyResult);
    }


    void BASE_TEXTURE::DrawFixedDepthRectangle(const RECTI& destination, const RECTI& source, const DWORD* colors)
    {

        RECTANGLE_VERTEXES_D3D vertexes;
        float fixedDepth = 0.0f;
        const DWORD fixedDepthRaw = 0x3F7FFFFEu;
        std::memcpy(&fixedDepth, &fixedDepthRaw, sizeof(fixedDepth));
        const float x0 = static_cast<float>(destination.left);
        const float y0 = static_cast<float>(destination.top);
        const float x1 = static_cast<float>(destination.right);
        const float y1 = static_cast<float>(destination.bottom);
        const float u0 = (static_cast<float>(source.left) + 0.5f) / static_cast<float>(m_width);
        const float v0 = (static_cast<float>(source.top) + 0.5f) / static_cast<float>(m_height);
        const float u1 = (static_cast<float>(source.right) + 0.5f) / static_cast<float>(m_width);
        const float v1 = (static_cast<float>(source.bottom) + 0.5f) / static_cast<float>(m_height);
        const DWORD diffuse = ~colors[0];
        const DWORD specular = colors[1];
        vertexes.v[0] = RECTANGLE_VERTEX_D3D{x0, y0, fixedDepth, 1.0f, diffuse, specular, u0, v0};
        vertexes.v[1] = RECTANGLE_VERTEX_D3D{x1, y0, fixedDepth, 1.0f, diffuse, specular, u1, v0};
        vertexes.v[2] = RECTANGLE_VERTEX_D3D{x1, y1, fixedDepth, 1.0f, diffuse, specular, u1, v1};
        vertexes.v[3] = RECTANGLE_VERTEX_D3D{x0, y1, fixedDepth, 1.0f, diffuse, specular, u0, v1};

        IDirect3DDevice8* const device = currentTextureDevice();
        HRESULT result = device->SetTexture(0u, m_nativeHandle);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 8, "", static_cast<int>(result));

        if (m_format == 0x29u)
        {
            result = device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            if (result != D3D_OK)
                LOG::ResourceError("TEXTURE", 8, "palette", static_cast<int>(result));
        }

        Graph->setRenderStateCached(0x1Du, colors[1] != 0u ? 1u : 0u);

        const DWORD filter =
            destination.width() == source.width() && destination.height() == source.height()
                ? 1u
                : 2u;
        D3D8SetSamplerState(device, 0u, D3DSAMP_MAGFILTER, filter);
        D3D8SetSamplerState(device, 0u, D3DSAMP_MINFILTER, filter);
        device->SetVertexShader(0x1C4u);
        result = device->DrawPrimitiveUP(
            D3DPT_TRIANGLEFAN,
            2u,
            vertexes.v,
            0x20u);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 10, "DrawPrimitiveUP", static_cast<int>(result));
    }


    void BASE_TEXTURE::DrawDepthRectangle(
        DWORD zTop,
        DWORD zBottom,
        const RECTI& destination,
        const RECTI& source,
        const DWORD* colors)
    {

        RECTANGLE_VERTEXES_D3D vertexes;
        float zTopFloat = 0.0f;
        float zBottomFloat = 0.0f;
        std::memcpy(&zTopFloat, &zTop, sizeof(zTopFloat));
        std::memcpy(&zBottomFloat, &zBottom, sizeof(zBottomFloat));
        const float x0 = static_cast<float>(destination.left);
        const float y0 = static_cast<float>(destination.top);
        const float x1 = static_cast<float>(destination.right);
        const float y1 = static_cast<float>(destination.bottom);
        const float u0 = (static_cast<float>(source.left) + 0.5f) / static_cast<float>(m_width);
        const float v0 = (static_cast<float>(source.top) + 0.5f) / static_cast<float>(m_height);
        const float u1 = (static_cast<float>(source.right) + 0.5f) / static_cast<float>(m_width);
        const float v1 = (static_cast<float>(source.bottom) + 0.5f) / static_cast<float>(m_height);
        const DWORD diffuse = ~colors[0];
        const DWORD specular = colors[1];
        vertexes.v[0] = RECTANGLE_VERTEX_D3D{x0, y0, zTopFloat,    1.0f, diffuse, specular, u0, v0};
        vertexes.v[1] = RECTANGLE_VERTEX_D3D{x1, y0, zTopFloat,    1.0f, diffuse, specular, u1, v0};
        vertexes.v[2] = RECTANGLE_VERTEX_D3D{x1, y1, zBottomFloat, 1.0f, diffuse, specular, u1, v1};
        vertexes.v[3] = RECTANGLE_VERTEX_D3D{x0, y1, zBottomFloat, 1.0f, diffuse, specular, u0, v1};

        IDirect3DDevice8* const device = currentTextureDevice();
        HRESULT result = device->SetTexture(0u, m_nativeHandle);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 8, "", static_cast<int>(result));

        if (m_format == 0x29u)
        {
            result = device->SetCurrentTexturePalette(static_cast<UINT>(m_paletteSlot));
            if (result != D3D_OK)
                LOG::ResourceError("TEXTURE", 8, "palette", static_cast<int>(result));
        }

        Graph->setRenderStateCached(0x1Du, colors[1] != 0u ? 1u : 0u);
        device->SetVertexShader(0x1C4u);

        const DWORD filter =
            destination.width() == source.width() && destination.height() == source.height()
                ? 1u
                : 2u;
        D3D8SetSamplerState(device, 0u, D3DSAMP_MAGFILTER, filter);
        D3D8SetSamplerState(device, 0u, D3DSAMP_MINFILTER, filter);
        result = device->DrawPrimitiveUP(
            D3DPT_TRIANGLEFAN,
            2u,
            vertexes.v,
            0x20u);
        if (result != D3D_OK)
            LOG::ResourceError("TEXTURE", 10, "DrawPrimitiveUP", static_cast<int>(result));
    }

}
