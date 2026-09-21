#include "vid/vid_hardware.h"
#include "vid/vid_software16.h"

#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "map.h"
#include "sprite.h"
#include "compress.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <limits>
#include <xmmintrin.h>
#include <new>

namespace as1
{
    namespace
    {


        __forceinline
        DWORD floatBits(float value)
        {
            DWORD bits = 0;

            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        constexpr DWORD kD3dRenderStateAlphaTestEnable = 15u;
        constexpr DWORD kD3dRenderStateZFunc = 23u;
        constexpr DWORD kD3dRenderStateAlphaRef = 24u;
        constexpr DWORD kD3dRenderStateAlphaFunc = 25u;
        constexpr DWORD kD3dRenderStateAlphaBlendEnable = 27u;
        constexpr DWORD kD3dRenderStateSpecularEnable = 29u;
        constexpr DWORD kD3dCmpGreaterEqual = 7u;
        constexpr DWORD kD3dCmpAlways = 8u;
        constexpr DWORD kD3dBlendOne = 2u;
        constexpr DWORD kD3dBlendSrcAlpha = 5u;
        constexpr DWORD kD3dBlendInvSrcAlpha = 6u;
        constexpr DWORD kD3dBlendDestColor = 9u;

        __forceinline
        float interpolateHardwareEffectCurve(const VID_HARDWARE* owner,
                                             float position,
                                             int baseOffset) noexcept
        {
            const int segment = static_cast<int>(position);
            if (segment >= 7)
                return owner->weaponFloatAt(baseOffset + 7 * 4);

            const float first = owner->weaponFloatAt(baseOffset + segment * 4);
            const float second = owner->weaponFloatAt(baseOffset + (segment + 1) * 4);
            return (second - first) * (position - static_cast<float>(segment)) + first;
        }

        __forceinline
        std::int32_t wrapAdd32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        __forceinline
        std::int32_t wrapSub32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        __forceinline
        std::int32_t wrapMul32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
        }

        __forceinline
        std::int32_t abs32(std::int32_t value) noexcept
        {
            const std::uint32_t sign = static_cast<std::uint32_t>(value) >> 31u;
            const std::uint32_t mask = 0u - sign;
            return static_cast<std::int32_t>((static_cast<std::uint32_t>(value) ^ mask) - mask);
        }

        __forceinline
        int hardwareConvertFloatToInt32(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        __forceinline
        int hardwareSubtractAndConvertToInt32(float lhs, float rhs) noexcept
        {
            const float value = lhs - rhs;
            return hardwareConvertFloatToInt32(value);
        }

        __forceinline
        int hardwareSubtractTwoAndConvertToInt32(float lhs, float rhs1, float rhs2) noexcept
        {
            float value = lhs - rhs1;
            value -= rhs2;
            return hardwareConvertFloatToInt32(value);
        }

    }

    VID_HARDWARE::VID_HARDWARE() = default;


    VID_HARDWARE::VID_HARDWARE(const VID_HARDWARE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_HARDWARE&>(other).nextMirror = this;

        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));

        m_textureLayout = other.m_textureLayout;
        m_texturePageCount = other.m_texturePageCount;
        m_texturePages = other.m_texturePages;
    }

    VID_HARDWARE* VID_HARDWARE::CreateMirror()
    {

        return new (std::nothrow) VID_HARDWARE(*this);
    }


    VID_HARDWARE::VID_HARDWARE(int nvid, int width, int height)
    {

        nVid = nvid;
        sizeXYZ = VECTOR(256.0f, 256.0f, 1.0f);
        setVidWidth(static_cast<short>(width));
        setVidHeight(static_cast<short>(height));
        noDir = 1;
        layer = 0;
        name = STRING("Self Created Hardware Prerendered Ground ");
        type = 549;


        const std::int32_t tilesXCapacity = width / 256 + 1;
        const std::int32_t tilesYCapacity = height / 256 + 1;
        const std::int32_t rawRecordCapacity =
            wrapAdd32(wrapMul32(tilesXCapacity, tilesYCapacity), 1);
        const std::uint32_t rawRecordBytes =
            static_cast<std::uint32_t>(wrapMul32(rawRecordCapacity, 36));

        m_textureLayout = static_cast<TEX_SIZE*>(
            ::operator new(static_cast<std::size_t>(rawRecordBytes), std::nothrow));
        if (!m_textureLayout)
        {
            logVidResourceError(2, "texcoor", rawRecordCapacity);
            std::exit(1);
        }

        m_texturePageCount = 0;
        std::int32_t recordIndex = 0;
        std::int32_t destinationY = 0;
        std::int32_t remainingHeight = height;
        while (destinationY < height)
        {
            std::int32_t destinationX = 0;
            std::int32_t remainingWidth = width;
            while (destinationX < width)
            {
                TEX_SIZE* const tile = reinterpret_cast<TEX_SIZE*>(
                    reinterpret_cast<BYTE*>(m_textureLayout) +
                    static_cast<std::uint32_t>(wrapMul32(recordIndex, 36)));

                if (recordIndex != 0)
                    (tile - 1)->next = recordIndex;

                tile->textureIndex = static_cast<std::int16_t>(m_texturePageCount);
                tile->sourceX = 0;
                tile->sourceY = 0;
                tile->destinationX = destinationX;
                tile->destinationY = destinationY;
                tile->width = remainingWidth > 256 ? 256 : remainingWidth;
                tile->height = remainingHeight > 256 ? 256 : remainingHeight;
                tile->next = 0;

                destinationX = wrapAdd32(destinationX, 256);
                remainingWidth = wrapAdd32(remainingWidth, -256);
                m_texturePageCount = static_cast<WORD>(m_texturePageCount + 2u);
                recordIndex = wrapAdd32(recordIndex, 1);
            }

            destinationY = wrapAdd32(destinationY, 256);
            remainingHeight = wrapAdd32(remainingHeight, -256);
        }


        const std::int32_t signedTextureCount = static_cast<std::int16_t>(m_texturePageCount);
        const std::uint32_t textureTableBytes =
            static_cast<std::uint32_t>(wrapMul32(signedTextureCount, 4));
        m_texturePages = static_cast<BASE_TEXTURE**>(
            ::operator new(static_cast<std::size_t>(textureTableBytes), std::nothrow));
        if (!m_texturePages)
        {
            logVidResourceError(2, "textures", signedTextureCount);
            return;
        }


        if (signedTextureCount > 0)
            std::fill_n(m_texturePages, signedTextureCount, nullptr);
    }


    VID_HARDWARE::~VID_HARDWARE()
    {


        if (!isMirrorChainOwner())
            return;

        if (m_textureLayout)
            ::operator delete(m_textureLayout);
        m_textureLayout = nullptr;

        if (m_texturePages)
        {

            --m_texturePageCount;
            if (static_cast<std::int16_t>(m_texturePageCount) >= 0)
            {
                do
                {
                    BASE_TEXTURE* const texture =
                        m_texturePages[static_cast<std::int16_t>(m_texturePageCount)];
                    delete texture;
                    --m_texturePageCount;
                }
                while (static_cast<std::int16_t>(m_texturePageCount) >= 0);
            }
            ::operator delete(m_texturePages);
        }
        m_texturePages = nullptr;
        m_texturePageCount = 0;
    }


    void VID_HARDWARE::Load(RESOURCE* resource)
    {

        if (resource->GoNext(RESOURCE::ResTypes::SURFACE) != 0)
            logVidResourceError(5, "SURF", 0);

        resource->read(&m_texturePageCount, 2u);
        if (m_texturePageCount == 0)
            return;

        script::QS1_CODER* colorCodec = nullptr;
        script::QS1_CODER* zCodec = nullptr;
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_COMPRESS) != 0u)
        {
            colorCodec = new (std::nothrow) script::QS1_CODER((typeFlags & VID_TYPE_SHADOW) != 0u ? 1 : 2);
            zCodec = new (std::nothrow) script::QS1_CODER(2);
        }

        const int signedTextureCount = static_cast<std::int16_t>(m_texturePageCount);
        const std::uint32_t textureTableBytes = static_cast<std::uint32_t>(
            wrapMul32(signedTextureCount, 4));
        m_texturePages = static_cast<BASE_TEXTURE**>(
            ::operator new(static_cast<std::size_t>(textureTableBytes), std::nothrow));
        if (!m_texturePages)
        {
            logVidResourceError(2, "textures", signedTextureCount);
            return;
        }
        if (signedTextureCount > 0)
            std::fill_n(m_texturePages, signedTextureCount, nullptr);

        void* const unpackOwner = ::operator new(0x40008u, std::nothrow);
        if (!unpackOwner)
        {
            logVidResourceError(2, "(unpack)", 0);
            return;
        }
        BYTE* const unpack = static_cast<BYTE*>(unpackOwner);

        int textureIndex = 0;
        while (textureIndex < signedTextureCount)
        {


            WORD widthWord = 0;
            WORD heightWord = 0;
            resource->read(&widthWord, 2u);
            resource->read(&heightWord, 2u);
            const int width = static_cast<int>(static_cast<std::int16_t>(widthWord));
            const int height = static_cast<int>(static_cast<std::int16_t>(heightWord));

            DWORD requestedFormat = 25u;
            if ((typeFlags & VID_TYPE_SHADOW) != 0u)
                requestedFormat = ((typeFlags & (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) == (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) ? 0x33545844u : 0x31545844u;
            else if ((typeFlags & VID_TYPE_PALETTE) != 0u)
                requestedFormat = 41u;
            else if ((typeFlags & VID_TYPE_NORMALS) != 0u)
                requestedFormat = 21u;
            else if ((typeFlags & (VID_TYPE_TEXTURE | VID_TYPE_ALPHA)) == (VID_TYPE_TEXTURE | VID_TYPE_ALPHA))
                requestedFormat = 26u;

            BASE_TEXTURE* colorTexture = new (std::nothrow) BASE_TEXTURE(width, height, requestedFormat, 0u);
            m_texturePages[textureIndex] = colorTexture;
            if (!colorTexture->nativeHandle() && !colorTexture->isLoaded())
            {
                logVidResourceError(3, "texture", 0);
                return;
            }

            std::array<BYTE, 768> paletteBytes;
            if ((typeFlags & VID_TYPE_PALETTE) != 0u)
            {
                logVidResourceError(10, "palette %i", colorTexture->format() == 41u ? 1 : 0);
                resource->read(paletteBytes.data(), static_cast<unsigned>(paletteBytes.size()));
                if (colorTexture->format() == 41u)
                {
                    std::array<DWORD, 256> paletteDwords{};
                    for (std::size_t i = 0; i < paletteDwords.size(); ++i)
                    {
                        const DWORD c0 = paletteBytes[i * 3u + 0u];
                        const DWORD c1 = paletteBytes[i * 3u + 1u];
                        const DWORD c2 = paletteBytes[i * 3u + 2u];
                        paletteDwords[i] = 0xFF000000u | (c0 << 16u) | (c1 << 8u) | c2;
                    }
                    colorTexture->createPaletteSlot(paletteDwords.data());
                }
            }

            DWORD decodedSize;
            resource->read(&decodedSize, 4u);
            const int missing = resource->ReadPacked(unpack, decodedSize, colorCodec);

            if (missing != 0)
                logVidResourceError(5, "Can't decode", textureIndex);

            int pitchBytes = 0;
            const std::uint32_t rawExpectedColorBytes = static_cast<std::uint32_t>(
                wrapMul32(wrapMul32(width, height), 2));
            if ((typeFlags & VID_TYPE_PALETTE) == 0u &&
                static_cast<std::int32_t>(decodedSize) < static_cast<std::int32_t>(rawExpectedColorBytes))
            {
                logVidResourceError(10, "Load DXT", 0);
                WORD* const locked = colorTexture->lock16(&pitchBytes, nullptr);
                if (!locked)
                {
                    logVidResourceError(0, "DXT texture surface", 0);
                }
                else
                {
                    std::memcpy(locked, unpack, static_cast<std::size_t>(decodedSize));
                    colorTexture->unlock();
                }
            }
            else
            {
                WORD* const locked = colorTexture->lock16(&pitchBytes, nullptr);
                if (!locked)
                {

                    logVidResourceError(0, "texture surface", 0);
                    return;
                }
                else
                {
                    BYTE* destinationRow = reinterpret_cast<BYTE*>(locked);
                    for (int row = 0; row < height; ++row)
                    {
                        const DWORD actualFormat = colorTexture->format();
                        if ((typeFlags & VID_TYPE_PALETTE) != 0u)
                        {
                            if (actualFormat == 41u)
                            {
                                std::memcpy(destinationRow,
                                            unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                                            static_cast<std::size_t>(width));
                            }
                            else
                            {
                                WORD* destination = reinterpret_cast<WORD*>(destinationRow);
                                const BYTE* indices = unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
                                for (int x = 0; x < width; ++x)
                                {
                                    const std::size_t paletteOffset = static_cast<std::size_t>(indices[x]) * 3u;
                                    const DWORD c0 = paletteBytes[paletteOffset + 0u];
                                    const DWORD c1 = paletteBytes[paletteOffset + 1u];
                                    const DWORD c2 = paletteBytes[paletteOffset + 2u];
                                    destination[x] = static_cast<WORD>(
                                        ((c2 >> 3u) & 0x001Fu) |
                                        ((c0 & 0xF8u) << g_color16RedShift) |
                                        (g_color16GreenMask & (c1 << g_color16GreenShift)));
                                }
                            }
                        }
                        else if (actualFormat == 23u || actualFormat == 26u)
                        {
                            std::memcpy(destinationRow,
                                        unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 2u,
                                        static_cast<std::size_t>(width) * 2u);
                        }
                        else if (actualFormat == 21u)
                        {
                            std::memcpy(destinationRow,
                                        unpack + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4u,
                                        static_cast<std::size_t>(width) * 4u);
                        }
                        else
                        {
                            WORD* destination = reinterpret_cast<WORD*>(destinationRow);
                            const WORD* source = reinterpret_cast<const WORD*>(unpack) +
                                static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
                            for (int x = 0; x < width; ++x)
                            {
                                const WORD value = source[x];


                                destination[x] = value
                                    ? static_cast<WORD>(0x8000u | (value & 0x001Fu) | ((value >> 1u) & 0x7FE0u))
                                    : static_cast<WORD>(0u);
                            }
                        }
                        destinationRow += pitchBytes;
                    }
                    colorTexture->unlock();
                }
            }

            int lastTextureIndex = textureIndex;
            if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
            {
                const int zIndex = textureIndex + 1;
                BASE_TEXTURE* const zTexture = new (std::nothrow) BASE_TEXTURE(width, height, 80u, 2u);
                m_texturePages[zIndex] = zTexture;

                DWORD zDecodedSize;
                resource->read(&zDecodedSize, 4u);
                if (zTexture->isLoaded())
                {
                    int zPitchBytes = 0;
                    WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
                    if (zBits)
                    {
                        const DWORD expectedZBytes = static_cast<DWORD>(wrapMul32(zPitchBytes, height));
                        if (zDecodedSize == expectedZBytes)
                        {
                            const int zMissing = resource->ReadPacked(zBits, zDecodedSize, zCodec);
                            if (zMissing != 0)
                                logVidResourceError(5, "Can't decode z", static_cast<std::int32_t>(
                                    static_cast<std::uint32_t>(zDecodedSize) - static_cast<std::uint32_t>(zMissing)));
                        }
                        else
                        {
                            logVidResourceError(5, "ZBuffer: invalid size", static_cast<int>(zDecodedSize));
                        }
                        zTexture->unlock();
                    }
                    else
                    {
                        logVidResourceError(0, "texture z surface", 0);
                        resource->shift(static_cast<int>(zDecodedSize));
                    }
                }
                else
                {
                    logVidResourceError(3, "texture z surface", 0);
                    resource->shift(static_cast<int>(zDecodedSize));
                }
                lastTextureIndex = zIndex;
            }

            textureIndex = wrapAdd32(lastTextureIndex, 1);
        }

        if (resource->GoNext(RESOURCE::ResTypes::DATA) != 0)
            logVidResourceError(5, "DATA", 0);

        if ((typeFlags & VID_TYPE_NEWVERSION) != 0u)
        {
            const int serializedLayoutBytes = resource->SubSize();
            void* loaded = nullptr;
            resource->SubLoad(&loaded, nullptr);
            m_textureLayout = static_cast<TEX_SIZE*>(loaded);
            if (!m_textureLayout)
                logVidResourceError(5, "tex_coor", 0);

        }
        else
        {
            const int recordCount = resource->SubSize() / 20;
            const std::uint32_t texSizeBytes = static_cast<std::uint32_t>(
                wrapMul32(recordCount, static_cast<int>(sizeof(TEX_SIZE))));
            m_textureLayout = static_cast<TEX_SIZE*>(
                ::operator new(static_cast<std::size_t>(texSizeBytes), std::nothrow));
            for (int record = 0; record < recordCount; ++record)
            {
                TEX_SIZE& out = m_textureLayout[record];
                resource->read(&out.marker, 4u);
                WORD value = 0;
                resource->read(&value, 2u); out.textureIndex = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.sourceX = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.sourceY = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.width = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.height = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.destinationX = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.destinationY = static_cast<int>(static_cast<std::int16_t>(value));
                resource->read(&value, 2u); out.next = static_cast<int>(static_cast<std::int16_t>(value));
            }
        }

        resource->GoNext(RESOURCE::ResTypes::SHADOW);
        delete colorCodec;
        delete zCodec;
        ::operator delete(unpackOwner);
    }


    void VID_HARDWARE::SetLayer()
    {


        if ((property & 0x40000000u) != 0u)
        {
            layer = 4;
            return;
        }
        if (spriteType == 0x40u)
        {
            layer = 10;
            return;
        }
        if (m_texturePageCount == 0)
        {
            layer = 19;
            return;
        }

        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
        {
            layer = (typeFlags & VID_TYPE_ALPHA) != 0u ? 9 : 0;
            return;
        }

        if ((property & P_ALWAYSTOP) != 0u)
        {
            if (directionCount() == 0xFF)
            {
                layer = 18;
                return;
            }
            if (spriteType == 0x10u)
            {
                layer = 17;
                return;
            }
            layer = 14;
            return;
        }

        if (spriteType == 0x10u)
        {
            layer = 15;
            return;
        }

        if ((typeFlags & VID_TYPE_ALPHA) != 0u)
        {
            layer = (property & P_WAVE) != 0u ? 9 : 12;
            return;
        }
        layer = 8;
    }


    void VID_HARDWARE::AddVidToVid(SPRITE* sprite)
    {

        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_TEXTURE) == 0u ||
            (typeFlags & VID_TYPE_ZBUFFER) == 0u ||
            directionCount() != 1)
        {
            return;
        }

        VID* const sourceVid = sprite->Vid();
        if ((sourceVid->properties() & P_INVISIBLEFORENEMY) != 0u)
        {
            return;
        }

        const int savedLeft = g_softwareClipLeft;
        const int savedRight = g_softwareClipRight;
        const int savedTop = g_softwareClipTop;
        const int savedBottom = g_softwareClipBottom;
        const auto restoreCompositeClip = [&]() noexcept
        {
            g_softwareClipLeft = savedLeft;
            g_softwareClipRight = savedRight;
            g_softwareClipTop = savedTop;
            g_softwareClipBottom = savedBottom;
        };

        const int spriteX = hardwareConvertFloatToInt32(sprite->X());
        const int spriteYProjected = hardwareSubtractAndConvertToInt32(sprite->Y(), sprite->Z());
        const int sourceHalfWidth = static_cast<std::int16_t>(sourceVid->vidWidth()) / 2;
        const int sourceHalfHeight = static_cast<std::int16_t>(sourceVid->vidHeight()) / 2;

        VID_HARDWARE::TEX_SIZE* tile = textureLayout();
        while (tile)
        {
            const int tileHalfWidth = tile->width / 2;
            const int tileHalfHeight = tile->height / 2;

            int deltaX = wrapSub32(tile->destinationX, spriteX);
            deltaX = wrapAdd32(deltaX, tile->sourceX);
            deltaX = wrapAdd32(deltaX, tileHalfWidth);
            const int absX = abs32(deltaX);
            const int thresholdX = wrapAdd32(tileHalfWidth, sourceHalfWidth);

            if (absX < thresholdX)
            {
                int deltaY = wrapSub32(tile->destinationY, spriteYProjected);
                deltaY = wrapAdd32(deltaY, tile->sourceY);
                deltaY = wrapAdd32(deltaY, tileHalfHeight);
                const int absY = abs32(deltaY);
                const int thresholdY = wrapAdd32(tileHalfHeight, sourceHalfHeight);

                if (absY < thresholdY)
                {
                    g_softwareClipLeft = tile->sourceX;
                    g_softwareClipRight = wrapAdd32(tile->sourceX, tile->width);
                    g_softwareClipTop = tile->sourceY;
                    g_softwareClipBottom = wrapAdd32(tile->sourceY, tile->height);

                    const int textureIndex = tile->textureIndex;
                    BASE_TEXTURE** const table = texturePages();


                    if (!table[textureIndex])
                    {
                        BASE_TEXTURE* const colorTexture = new (std::nothrow) BASE_TEXTURE(
                            tile->width, tile->height, 23u, 0u);
                        table[textureIndex] = colorTexture;
                        if (!colorTexture)
                        {
                            restoreCompositeClip();
                            return;
                        }

                        int colorPitchBytes = 0;
                        WORD* const colorBits = colorTexture->lock16(&colorPitchBytes, nullptr);
                        if (colorBits)
                        {
                            const std::uint32_t colorBytes = static_cast<std::uint32_t>(
                                wrapMul32(colorTexture->height(), colorPitchBytes));
                            std::memset(colorBits, 0, static_cast<std::size_t>(colorBytes));
                            colorTexture->unlock();
                        }

                        BASE_TEXTURE* const zTexture = new (std::nothrow) BASE_TEXTURE(
                            tile->width, tile->height, 80u, 2u);
                        table[textureIndex + 1] = zTexture;
                        if (!zTexture)
                        {
                            restoreCompositeClip();
                            return;
                        }

                        int zPitchBytes = 0;
                        WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
                        if (zBits)
                        {
                            const std::int32_t zPitchWords = zPitchBytes / 2;
                            const std::uint32_t zWordCount = static_cast<std::uint32_t>(
                                wrapMul32(zTexture->height(), zPitchWords));
                            for (std::uint32_t i = 0; i < zWordCount; ++i)
                                zBits[i] = static_cast<WORD>(0x0400u);
                            zTexture->unlock();
                        }
                    }

                    sourceVid->DrawToVid(sprite, tile, table[textureIndex], table[textureIndex + 1]);
                }
            }

            const int nextIndex = tile->next;
            if (nextIndex == 0)
                break;
            tile = reinterpret_cast<VID_HARDWARE::TEX_SIZE*>(
                reinterpret_cast<BYTE*>(textureLayout()) +
                static_cast<std::uint32_t>(wrapMul32(nextIndex, 36)));
        }

        restoreCompositeClip();
    }

    void VID_HARDWARE::Draw(const SPRITE* sprite)
    {

        if (m_texturePageCount == 0)
            return;

        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        GRAPH* const graph = Graph;
        int frameIndex = sprite->currentFrame();

        const TEX_SIZE* first = &m_textureLayout[static_cast<std::size_t>(frameIndex)];
        if (first->height == 0)
            return;

        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        const WORD typeFlags = formatFlags();
        const float currentX = sprite->X();
        const float currentY = sprite->Y();
        const float currentZ = sprite->Z();
        const float currentProjectedY = currentY - currentZ;

        float drawScaleX = scaleXYZ.x;
        float drawScaleY = scaleXYZ.y;
        int effectOffsetX = 0;
        int effectOffsetY = 0;
        int effectOffsetZ = 0;
        const DWORD auxFlags = runtimeAuxFlags();
        if ((auxFlags & 0x02u) != 0u)
        {
            const float position = sprite->exDataEffectCurvePosition();
            drawScaleX *= interpolateHardwareEffectCurve(this, position, 0x100);
            drawScaleY *= interpolateHardwareEffectCurve(this, position, 0x120);
        }
        if ((auxFlags & 0x04u) != 0u)
        {
            const float position = sprite->exDataEffectCurvePosition();
            effectOffsetX = hardwareConvertFloatToInt32(interpolateHardwareEffectCurve(this, position, 0x160));
            effectOffsetY = hardwareConvertFloatToInt32(interpolateHardwareEffectCurve(this, position, 0x180));
            effectOffsetZ = hardwareConvertFloatToInt32(interpolateHardwareEffectCurve(this, position, 0x1A0));
        }


        int hardwareDirectAngle = 0;
        if ((property & P_HARDWAREDIRECT) != 0u)
        {
            if ((property & P_VERTDIR) != 0u)
            {
                const float historyX = sprite->blurHistoryX();
                const float historyY = sprite->blurHistoryY();
                const float historyZ = sprite->blurHistoryZ();
                if (currentX != historyX || currentY != historyY)
                {
                    const float dx = currentX - historyX;
                    const float dy =
                        (currentY - currentZ - historyY + historyZ) /
                        0.7070602178573608f;
                    hardwareDirectAngle = DirectionFromFloatXY(dx, dy).Int();
                }
                else if (sprite->Speed() != 0.0f && sprite->ZSpeed() != 0.0f)
                {
                    const int facing = sprite->Direction().Int();
                    const float vx = sprite->Speed() * SPRITE::rawDirectionSin(facing);
                    const float vy =
                        -(sprite->ZSpeed() +
                          sprite->Speed() * SPRITE::rawDirectionCos(facing)) /
                        0.7070602178573608f;
                    hardwareDirectAngle = DirectionFromFloatXY(vx, vy).Int();
                }
                else
                {
                    const int realDirection = RealDirection(sprite->Direction());
                    hardwareDirectAngle = static_cast<unsigned char>(
                        sprite->Direction().Int() -
                        realDirection * 256 / directionCount());
                }
            }
            else
            {
                const int realDirection = RealDirection(sprite->Direction());
                hardwareDirectAngle = static_cast<unsigned char>(
                    sprite->Direction().Int() -
                    realDirection * 256 / directionCount());
            }
        }

        int sampleCount = 1;
        if ((property & P_BLUR) != 0u)
        {
            const float historyX = sprite->blurHistoryX();
            const float historyProjectedY = sprite->blurHistoryY() - sprite->blurHistoryZ();
            const float deltaX = historyX - currentX;
            const float deltaY = historyProjectedY - currentProjectedY;


            if ((property & P_HARDWAREDIRECT) != 0u &&
                (property & P_VERTDIR) != 0u)
            {
                const float absX = std::fabs(deltaX);
                const float absY = std::fabs(deltaY);
                const float weighted = (absX >= absY)
                    ? (absX * 0.5f + absY)
                    : (absY * 0.5f + absX);
                const int quotient = hardwareConvertFloatToInt32(
                    (weighted * -1.5f) / static_cast<float>(first->height));
                sampleCount = wrapSub32(1, quotient);
            }
            else
            {
                const int sampleWidth = first->width;
                if (sampleWidth != 0)
                {
                    const int dx = abs32(hardwareConvertFloatToInt32(deltaX)) / sampleWidth;
                    const int dy = abs32(hardwareConvertFloatToInt32(deltaY)) / first->height;
                    sampleCount = wrapAdd32(wrapMul32(2, std::max(dx, dy)), 1);
                }
            }
        }


        graph->setRenderStateCached(kD3dRenderStateAlphaTestEnable, 1u);
        graph->setRenderStateCached(kD3dRenderStateAlphaRef, 0u);
        graph->setRenderStateCached(kD3dRenderStateAlphaFunc, 5u);

        for (int sample = 0; sample < sampleCount; ++sample)
        {
            int screenX = hardwareSubtractAndConvertToInt32(currentX, cameraX) + effectOffsetX;
            int screenY = hardwareSubtractTwoAndConvertToInt32(currentY, currentZ, cameraY) + effectOffsetY;
            int spriteZInt = hardwareConvertFloatToInt32(currentZ) + effectOffsetZ;


            if ((property & P_ZEROZ) != 0u)
                spriteZInt = 3;
            if ((property & P_WAVE) != 0u)
            {
                const int waveShift = hardwareConvertFloatToInt32(
                    SPRITE::rawDirectionSin(
                        static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                    moveUpZ());
                spriteZInt = wrapAdd32(spriteZInt, waveShift);
                screenY = wrapSub32(screenY, waveShift);
            }

            if ((property & P_BLUR) != 0u)
            {
                const float sampleFloat = static_cast<float>(sample);
                const float sampleCountFloat = static_cast<float>(sampleCount);
                const float historyX = sprite->blurHistoryX();
                const float historyY = sprite->blurHistoryY();
                const float historyZ = sprite->blurHistoryZ();

                float interpolatedXDelta = historyX - currentX;
                interpolatedXDelta *= sampleFloat;
                float interpolatedX = currentX - cameraX;
                interpolatedXDelta /= sampleCountFloat;
                interpolatedX += interpolatedXDelta;
                screenX = hardwareConvertFloatToInt32(interpolatedX);

                float historyProjectedY = historyY - historyZ;
                historyProjectedY -= currentY;
                historyProjectedY += currentZ;
                float interpolatedY = currentY - currentZ;
                interpolatedY -= cameraY;
                historyProjectedY *= sampleFloat;
                historyProjectedY /= sampleCountFloat;
                interpolatedY += historyProjectedY;
                screenY = hardwareConvertFloatToInt32(interpolatedY);

                float interpolatedZDelta = historyZ - currentZ;
                interpolatedZDelta *= sampleFloat;
                interpolatedZDelta /= sampleCountFloat;
                interpolatedZDelta += currentZ;
                spriteZInt = hardwareConvertFloatToInt32(interpolatedZDelta);
            }


            const DWORD drawSpriteType = spriteType;
            if ((property & P_ALWAYSTOP) == 0u &&
                (typeFlags & VID_TYPE_ZBUFFER) == 0u &&
                drawSpriteType != U_MENU &&
                !(drawSpriteType == U_TERRAIN && (typeFlags & VID_TYPE_ALPHA) == 0u))
            {


                if (static_cast<float>(screenX) < graph->viewportLeft() ||
                    static_cast<float>(screenX) >= graph->viewportRight() ||
                    static_cast<float>(screenY) < graph->viewportTop() ||
                    static_cast<float>(screenY) >= graph->viewportBottom())
                {
                    continue;
                }

                const WORD* depth = graph->softwareDepthBuffer();
                const int pitch = graph->softwareDepthPitch();
                if (depth[screenX + screenY * pitch] > spriteZInt * 8 + 0x400)
                    continue;
            }

            float baseDepth = static_cast<float>(spriteZInt) * 0.0001220703125f + 0.015625f;
            if ((property & P_WAVE) != 0u && moveUpZ() != 0.0f)
            {
                baseDepth += SPRITE::rawDirectionSin(
                    static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                    moveUpZ() * 0.0001220703125f * 0.5f;
            }

            int recordIndex = frameIndex;
            for (;;)
            {
                const TEX_SIZE& record = m_textureLayout[static_cast<std::size_t>(recordIndex)];
                if (record.height == 0)
                    break;


                BASE_TEXTURE* const texture =
                    m_texturePages[static_cast<std::size_t>(record.textureIndex)];
                if (!texture)
                {
                    if (record.next == 0)
                        break;
                    recordIndex = record.next;
                    continue;
                }

                const int left = screenX + static_cast<int>(
                    static_cast<float>(record.destinationX - static_cast<int>(vidWidth()) / 2) * drawScaleX);
                const int top = screenY + static_cast<int>(
                    static_cast<float>(record.destinationY - static_cast<int>(vidHeight()) / 2) * drawScaleY);

                int drawWidth = record.width;
                int drawHeight = record.height;


                const int cullWidth = hardwareConvertFloatToInt32(
                    static_cast<float>(record.width) * drawScaleX);
                const int cullHeight = hardwareConvertFloatToInt32(
                    static_cast<float>(record.height) * drawScaleY);
                if (left + cullWidth >= g_softwareClipLeft && left < g_softwareClipRight &&
                    top + cullHeight >= g_softwareClipTop && top < g_softwareClipBottom)
                {
                    if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        if (left + drawWidth > g_softwareClipRight)
                            drawWidth = g_softwareClipRight - left;
                        if (top + drawHeight > g_softwareClipBottom)
                            drawHeight = g_softwareClipBottom - top;
                    }

                    RECTI destination{
                        left,
                        top,
                        left + static_cast<int>(static_cast<float>(drawWidth) * drawScaleX),
                        top + static_cast<int>(static_cast<float>(drawHeight) * drawScaleY)
                    };
                    RECTI source{
                        record.sourceX,
                        record.sourceY,
                        record.sourceX + drawWidth,
                        record.sourceY + drawHeight
                    };


                    if ((typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        BASE_TEXTURE* zTexture = m_texturePages[static_cast<std::size_t>(record.textureIndex + 1)];
                        const int copyResult = graph->drawTextureRectClipped(destination, source, *zTexture);
                        if (copyResult != 0)
                            logVidResourceError(1, "zbuffer", copyResult);
                    }

                    float zTop = baseDepth;
                    float zBottom = baseDepth;
                    DWORD zFunc = kD3dCmpGreaterEqual;
                    if ((property & P_ALWAYSTOP) != 0u || (typeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        zTop = 0.9999998807907104f;
                        zBottom = 0.9999998807907104f;
                        zFunc = kD3dCmpAlways;
                    }
                    else if (this->sizeZ() > this->sizeY())
                    {
                        const float depthDelta = static_cast<float>(drawHeight) * 0.0001220703125f;
                        zTop = baseDepth + depthDelta;
                        zBottom = baseDepth - depthDelta;
                    }
                    graph->setRenderStateCached(kD3dRenderStateZFunc, zFunc);

                    if ((typeFlags & VID_TYPE_ALPHA) != 0u)
                    {
                        if ((typeFlags & VID_TYPE_TEXTURE) != 0u)
                            graph->SetAlphaBlend(kD3dBlendSrcAlpha, kD3dBlendInvSrcAlpha);
                        else
                            graph->SetAlphaBlend(kD3dBlendDestColor, kD3dBlendOne);
                    }
                    else
                    {
                        graph->setRenderStateCached(kD3dRenderStateAlphaBlendEnable, 0u);
                    }

                    Gamma selectedGamma{};
                    selectedGamma = sprite->GetGamma();
                    Gamma drawGamma{};
                    drawGamma.setSaturatingAdd(gammaRaw, selectedGamma);
                    if ((property & P_GAMMA) == 0u)
                    {
                        if (Graph)
                            drawGamma.setSaturatingAdd(drawGamma, Graph->rawGammaPair());
                    }
                    const DWORD colors[2] = { drawGamma.first, drawGamma.second };

                    if ((property & P_HARDWAREDIRECT) != 0u)
                    {
                        struct HardwareVertex
                        {
                            float x, y, z, rhw;
                            DWORD diffuse;
                            DWORD specular;
                            float tu, tv;
                        };

                        const float localX[4] = {
                            0.0f, static_cast<float>(record.width),
                            0.0f, static_cast<float>(record.width)
                        };
                        const float localY[4] = {
                            0.0f, 0.0f,
                            static_cast<float>(record.height),
                            static_cast<float>(record.height)
                        };
                        const float textureWidth = static_cast<float>(texture->width());
                        const float textureHeight = static_cast<float>(texture->height());
                        const float sinValue = SPRITE::rawDirectionSin(hardwareDirectAngle);
                        const float cosValue = SPRITE::rawDirectionCos(hardwareDirectAngle);
                        const DWORD diffuse = ~drawGamma.first;
                        const DWORD specular = drawGamma.second;
                        HardwareVertex vertices[4]{};

                        for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
                        {
                            float localDrawX =
                                (static_cast<float>(record.destinationX - static_cast<int>(vidWidth()) / 2) +
                                 localX[vertexIndex]) * drawScaleX;
                            float localDrawY =
                                (static_cast<float>(record.destinationY - static_cast<int>(vidHeight()) / 2) +
                                 localY[vertexIndex]) * drawScaleY;
                            const float rotatedX = localDrawX * cosValue - localDrawY * sinValue;
                            const float rotatedY = localDrawX * sinValue + localDrawY * cosValue;

                            vertices[vertexIndex].x = rotatedX + static_cast<float>(screenX);
                            vertices[vertexIndex].y = rotatedY + static_cast<float>(screenY);
                            vertices[vertexIndex].z = zTop;
                            vertices[vertexIndex].rhw = 1.0f;
                            vertices[vertexIndex].diffuse = diffuse;
                            vertices[vertexIndex].specular = specular;
                            vertices[vertexIndex].tu =
                                (static_cast<float>(record.sourceX) + localX[vertexIndex] + 0.5f) / textureWidth;
                            vertices[vertexIndex].tv =
                                (static_cast<float>(record.sourceY) + localY[vertexIndex] + 0.5f) / textureHeight;
                        }

                        IDirect3DDevice8* const device =
                            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
                        if (device)
                        {
                            (void)device->SetTexture(
                                0u,
                                reinterpret_cast<IDirect3DBaseTexture8*>(texture->nativeHandle()));
                        }
                        graph->setRenderStateCached(
                            kD3dRenderStateSpecularEnable, specular != 0u ? 1u : 0u);
                        (void)graph->drawPrimitiveUp(5u, 0x1C4u, vertices, 0x20u, 4);
                    }
                    else
                    {
                        texture->DrawDepthRectangle(
                            floatBits(zTop),
                            floatBits(zBottom),
                            destination,
                            source,
                            colors);
                    }
                }

                if (record.next == 0)
                    break;
                recordIndex = record.next;
            }
        }

    }

}
