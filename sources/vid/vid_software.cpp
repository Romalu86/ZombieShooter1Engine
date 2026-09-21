#include "vid/vid_software.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "compress.h"
#include "graphics/base_texture.h"
#include "graphics/color.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>
#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace as1
{


    VID_SOFTWARE::VID_SOFTWARE()
        : VID(),
          m_frameOffsets(nullptr),
          m_frameStorageBytes(0),
          m_frameStorage(nullptr),
          m_gammaPaletteScratch(nullptr)
    {
    }

    namespace
    {
        int truncateFloatToInt32Software(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        int wrapSubSoftware(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int softwareDrawXToInt32(float x, float cameraX, int halfWidth) noexcept
        {
            int value = wrapSubSoftware(truncateFloatToInt32Software(x), truncateFloatToInt32Software(cameraX));
            return wrapSubSoftware(value, halfWidth);
        }

        int softwareDrawYToInt32(float y, float z, float cameraY, int halfHeight) noexcept
        {
            float projectedY = y - z;
            int value = wrapSubSoftware(truncateFloatToInt32Software(projectedY), truncateFloatToInt32Software(cameraY));
            return wrapSubSoftware(value, halfHeight);
        }
    }


    int drawOpaquePaletteSpanWithDepth(BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept
    {

        const std::int32_t result = static_cast<std::int32_t>(g_packedSoftwareDepth);
        if (count == 0)
            return result;
        int remaining = count;
        do
        {
            if (static_cast<std::int16_t>(static_cast<WORD>(g_packedSoftwareDepth)) >
                static_cast<std::int16_t>(*depth))
            {
                *depth = static_cast<WORD>(g_packedSoftwareDepth);
                *color = g_softwarePaletteLookup[*paletteIndexes];
            }
            ++color;
            ++depth;
            ++paletteIndexes;
            --remaining;
        } while (remaining > 0);
        return result;
    }


    int drawAlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept
    {

        if (count <= 0)
            return count;
        for (int i = 0; i < count; ++i)
        {
            if (static_cast<WORD>(g_packedSoftwareDepth) >= depth[i])
            {
                Color* const destination = reinterpret_cast<Color*>(&color[i]);
                const Color* const palette = reinterpret_cast<const Color*>(g_softwarePaletteLookup);
                Color source = palette[paletteIndexes[i]];
                destination->AlphaAdd(source, source.Alpha());
            }
        }
        return count;
    }

    void __stdcall applyGammaToPaletteEntries(DWORD* entries, const Gamma* rawGamma)
    {

        if (!entries || (rawGamma->first == 0u && rawGamma->second == 0u))
            return;
        for (unsigned int index = 0; index < 256u; ++index)
            entries[index] = GammaRawBlend(*rawGamma, entries[index]);
    }

    VID_SOFTWARE::VID_SOFTWARE(const VID_SOFTWARE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_SOFTWARE&>(other).nextMirror = this;
        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_frameOffsets = other.m_frameOffsets;
        m_frameStorage = other.m_frameStorage;
        m_frameStorageBytes = other.m_frameStorageBytes;

        m_gammaPaletteScratch = nullptr;


        noGridZ = 0;
        gridZ = nullptr;
        gridCadrShift = nullptr;
    }

    VID_SOFTWARE::~VID_SOFTWARE()
    {

        if (isMirrorChainOwner())
        {
            if (m_frameStorage)
                ::operator delete(m_frameStorage);
            m_frameStorage = nullptr;

            if (m_frameOffsets)
                ::operator delete(m_frameOffsets);
            m_frameOffsets = nullptr;

            g_vidMemoryInUse -= static_cast<int>(m_frameStorageBytes);
            m_frameStorageBytes = 0;


            if (m_gammaPaletteScratch)
                ::operator delete(m_gammaPaletteScratch);
            m_gammaPaletteScratch = nullptr;
        }
    }

    VID_SOFTWARE* VID_SOFTWARE::CreateMirror()
    {

        return new (std::nothrow) VID_SOFTWARE(*this);
    }

    namespace
    {
        constexpr size_t SOFTWARE_GAMMA_ENTRY_COUNT = 256u;
    }


    void VID_SOFTWARE::Load(RESOURCE* resource)
    {

        std::unique_ptr<script::QS1_CODER> codec;
        if ((formatFlags() & VID_TYPE_COMPRESS) != 0u)
        {
            codec.reset(new (std::nothrow) script::QS1_CODER(1));
        }

        std::array<DWORD, 256> paletteWords{};
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
        {
            int paletteReadError = 0;
            if (resource->GoNext(RESOURCE::ResTypes::PALETTE))
            {
                logVidResourceError(5, "PAL ", 0);
            }
            else if ((formatFlags() & VID_TYPE_NEWVERSION) != 0u)
            {
                paletteReadError = resource->read(paletteWords.data(), 1024u);
            }
            else
            {
                std::array<BYTE, 768> rgb{};
                paletteReadError = resource->read(rgb.data(), static_cast<unsigned>(rgb.size()));
                for (std::size_t index = 0; index < 256u; ++index)
                {
                    const DWORD r = rgb[index * 3u + 0u];
                    const DWORD g = rgb[index * 3u + 1u];
                    const DWORD b = rgb[index * 3u + 2u];
                    paletteWords[index] = 0xFF000000u | (r << 16u) | (g << 8u) | b;
                }
            }

            if (gammaRaw.first != 0u || gammaRaw.second != 0u)
            {
                for (DWORD& color : paletteWords)
                    color = GammaRawBlend(gammaRaw, color);
            }

            if (paletteReadError != 0)
                logVidResourceError(5, "can't' read palette", 0);
        }

        if (resource->GoNext(RESOURCE::ResTypes::DATA))
            logVidResourceError(5, "DATA", 0);

        m_frameStorageBytes = static_cast<DWORD>(resource->CurrentResourceSize());
        const DWORD paletteBlockBytes = (formatFlags() & VID_TYPE_PALETTE) != 0u
            ? static_cast<DWORD>(PaletteSize())
            : 0u;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
            m_frameStorageBytes += 2u * paletteBlockBytes;

        m_frameStorage = static_cast<BYTE*>(::operator new(m_frameStorageBytes, std::nothrow));
        if (!m_frameStorage)
        {
            logVidResourceError(2, "cadr", static_cast<int>(m_frameStorageBytes));
            return;
        }

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(noCadr));
        const std::uint32_t frameOffsetBytes = static_cast<std::uint32_t>(frameCount) << 2u;
        m_frameOffsets = static_cast<DWORD*>(
            ::operator new(static_cast<std::size_t>(frameOffsetBytes), std::nothrow));
        if (!m_frameOffsets)
        {
            logVidResourceError(2, "cadrShift", frameCount);
            return;
        }

        DWORD dataOffset = 0u;
        if ((formatFlags() & VID_TYPE_PALETTE) != 0u)
        {
            if (paletteBlockBytes == 1024u)
            {
                std::memcpy(m_frameStorage, paletteWords.data(), 1024u);
            }
            else
            {
                WORD* packed = reinterpret_cast<WORD*>(m_frameStorage);
                for (std::size_t index = 0; index < 256u; ++index)
                {
                    const DWORD color = paletteWords[index];
                    packed[index] = static_cast<WORD>(
                        ((color >> 3u) & 0x001Fu) |
                        (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
                        (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
                }
            }

            std::memcpy(m_frameStorage + paletteBlockBytes, m_frameStorage, paletteBlockBytes);
            dataOffset = 2u * paletteBlockBytes;
        }

        for (int frame = 0; frame < frameCount; ++frame)
        {
            DWORD decodedSize = 0u;
            if (resource->read(&decodedSize, 4u) != 0)
                logVidResourceError(5, "can't' read size", 0);
            const int missing = resource->ReadPacked(m_frameStorage + dataOffset,
                                                     decodedSize,
                                                     codec.get());
            if (missing != 0)
                logVidResourceError(5, "Can't decode software", static_cast<int>(decodedSize) - missing);

            if (decodedSize == 2u)
            {
                const int alias = static_cast<int>(
                    *reinterpret_cast<const std::int16_t*>(m_frameStorage + dataOffset));
                m_frameOffsets[frame] = m_frameOffsets[alias];
            }
            else
            {
                const WORD typeFlags = formatFlags();
                const GRAPH* const graph = Graph;
                const BASE_TEXTURE* const hi = graph->hiBuffer();
                const bool convertSoftware16 =
                    (typeFlags & (VID_TYPE_PALETTE | VID_TYPE_ALPHA)) == 0u &&
                    (hi->format() == 24u || gammaRaw.first != 0u || gammaRaw.second != 0u);
                if (convertSoftware16)
                {
                    BYTE* row = m_frameStorage + dataOffset;
                    const int contourCount = static_cast<int>(*reinterpret_cast<const std::int16_t*>(row));
                    row += 2 + 6 * contourCount;
                    int line = static_cast<int>(*reinterpret_cast<const std::int16_t*>(row));
                    const int lineEnd = line + static_cast<int>(*reinterpret_cast<const std::int16_t*>(row + 2));
                    row += 4;
                    while (line < lineEnd)
                    {
                        while (*reinterpret_cast<const WORD*>(row) != 0u)
                        {
                            ++row;
                            int run = static_cast<int>(*row++);
                            while (run > 0)
                            {
                                const WORD source565 = *reinterpret_cast<const WORD*>(row);


                                std::uint32_t color = static_cast<std::uint32_t>(source565);
                                std::uint32_t greenBits = color & 0x07F8u;
                                std::uint32_t blueBits = color & 0x001Fu;
                                color &= 0xFFFFFF00u;
                                color |= 0xFFFF0000u;
                                color <<= 3u;
                                color |= greenBits;
                                color <<= 2u;
                                color |= blueBits;
                                color <<= 3u;

                                if (gammaRaw.first != 0u || gammaRaw.second != 0u)
                                    color = GammaRawBlend(gammaRaw, color);

                                WORD packed = 0u;
                                if (hi->format() == 24u)
                                {
                                    packed = static_cast<WORD>(
                                        ((color >> 9u) & 0x7C00u) |
                                        ((color >> 6u) & 0x03E0u) |
                                        ((color >> 3u) & 0x001Fu));
                                }
                                else
                                {
                                    packed = static_cast<WORD>(
                                        ((color >> 8u) & 0xF800u) |
                                        ((color >> 5u) & 0x07E0u) |
                                        ((color >> 3u) & 0x001Fu));
                                }

                                *reinterpret_cast<WORD*>(row) = packed;
                                row += 2;
                                --run;
                            }
                        }
                        row += 2;
                        ++line;
                    }
                }

                m_frameOffsets[frame] = dataOffset;
                dataOffset += decodedSize;
            }
            resource->GoNextSub(RESOURCE::ResTypes::DATA);
        }

        g_vidMemoryInUse += static_cast<int>(m_frameStorageBytes);
        SetLayer();


    }

    void VID_SOFTWARE::SetLayer()
    {


        if ((runtimeAuxFlags() & 0x20u) != 0u)
        {
            layer = (formatFlags() & VID_TYPE_ALPHA) != 0u ? 2 : 1;
        }
        else if ((property & 0x40000000u) != 0u)
        {
            layer = 3;
        }
        else if ((property & P_ALWAYSTOP) != 0u)
        {
            if (directionCount() == 0xFF)
                layer = 18;
            else if (spriteType == 0x10u)
                layer = 16;
            else
                layer = 13;
        }
        else if ((formatFlags() & VID_TYPE_ALPHA) != 0u)
        {
            layer = 7;
        }
        else
        {
            layer = (property & 0x28u) != 0u ? 5 : 6;
        }


        const WORD typeFlags = formatFlags();
        if ((properties() & P_BUILDVIDZTOGRIDZ) != 0u &&
            (typeFlags & VID_TYPE_PALETTE) != 0u &&
            (typeFlags & VID_TYPE_TEXTURE) != 0u)
        {


            VECTOR* const scratch = static_cast<VECTOR*>(
                ::operator new(0x03000000u, std::nothrow));

            const int frameCount = static_cast<int>(static_cast<std::int16_t>(noCadr));
            const std::uint32_t frameShiftBytes = static_cast<std::uint32_t>(frameCount) << 2u;
            gridCadrShift = static_cast<int*>(
                ::operator new(static_cast<std::size_t>(frameShiftBytes), std::nothrow));

            std::array<short, 65536> cells{};
            const int width = static_cast<int>(static_cast<std::int16_t>(vidWidth()));
            const int height = static_cast<int>(static_cast<std::int16_t>(vidHeight()));
            const int halfWidth = width / 2;
            const int halfHeight = height / 2;

            for (int frame = 0; frame < frameCount; ++frame)
            {
                std::fill(cells.begin(), cells.end(), static_cast<short>(-32000));
                gridCadrShift[frame] = noGridZ;

                BYTE* rle = m_frameStorage + m_frameOffsets[frame];
                const int headerCount = static_cast<int>(*reinterpret_cast<const std::int16_t*>(rle));
                rle += 2 + 6 * headerCount;

                const WORD frameTypeFlags = formatFlags();
                if ((frameTypeFlags & VID_TYPE_PALETTE) != 0u)
                {
                    if ((frameTypeFlags & VID_TYPE_TEXTURE) != 0u &&
                        (frameTypeFlags & VID_TYPE_ZBUFFER) != 0u)
                    {
                        int y = static_cast<int>(*reinterpret_cast<const std::int16_t*>(rle));
                        const int yEnd = y + static_cast<int>(*reinterpret_cast<const std::int16_t*>(rle + 2));
                        rle += 4;

                        while (y < yEnd)
                        {
                            int x = 0;
                            while (*reinterpret_cast<const WORD*>(rle) != 0u)
                            {
                                x += static_cast<int>(*rle++);
                                const int count = static_cast<int>(*rle++);
                                BYTE* zData = rle;
                                const int runStartX = x;

                                for (int remaining = count; remaining > 0; --remaining)
                                {
                                    const int z =
                                        (static_cast<int>(*reinterpret_cast<const WORD*>(zData)) >> 3) - 128;
                                    const int projectedY = y + z;
                                    if (projectedY >= 0 && projectedY < 2048 &&
                                        x >= 0 && x < 2048)
                                    {
                                        const int cell = x / 8 + ((projectedY / 8) << 8);
                                        if (z > static_cast<int>(cells[static_cast<std::size_t>(cell)]))
                                            cells[static_cast<std::size_t>(cell)] = static_cast<short>(z);
                                    }
                                    zData += 2;
                                    ++x;
                                }

                                rle += static_cast<std::size_t>(count) * 3u;
                                x = runStartX + count;
                            }
                            ++y;
                            rle += 2;
                        }
                    }
                    else if ((frameTypeFlags & VID_TYPE_TEXTURE) != 0u)
                    {
                        int y = static_cast<int>(*reinterpret_cast<const std::int16_t*>(rle));
                        const int yEnd = y + static_cast<int>(*reinterpret_cast<const std::int16_t*>(rle + 2));
                        rle += 4;

                        while (y < yEnd)
                        {
                            int x = 0;
                            while (*reinterpret_cast<const WORD*>(rle) != 0u)
                            {
                                x += static_cast<int>(*rle++);
                                const int count = static_cast<int>(*rle++);
                                const int runStartX = x;

                                for (int remaining = count; remaining > 0; --remaining, ++x)
                                {
                                    if (y >= 0 && y < 2048 && x >= 0 && x < 2048)
                                        cells[static_cast<std::size_t>((y / 8) * 256 + x / 8)] = 0;
                                }

                                rle += static_cast<std::size_t>(count);
                                x = runStartX + count;
                            }
                            ++y;
                            rle += 2;
                        }
                    }
                }

                for (int row = height / 8 - 1; row >= 0; --row)
                {
                    const short* const line = cells.data() + 256 * row;
                    for (int col = 0; col < width / 8; ++col)
                    {
                        const short z = line[col];
                        if (z == static_cast<short>(-32000))
                            continue;

                        VECTOR& out = scratch[noGridZ];
                        out.x = static_cast<float>(col) * 8.0f - static_cast<float>(halfWidth);
                        out.y = static_cast<float>(row) * 8.0f - static_cast<float>(halfHeight);
                        out.z = static_cast<float>(z);
                        ++noGridZ;
                    }
                }
            }

            const std::uint32_t gridBytes = static_cast<std::uint32_t>(noGridZ) * 12u;
            gridZ = static_cast<VECTOR*>(
                ::operator new(static_cast<std::size_t>(gridBytes), std::nothrow));

            for (int i = 0; i < noGridZ; ++i)
            {
                gridZ[i].x = scratch[i].x;
                gridZ[i].y = scratch[i].y;
                gridZ[i].z = scratch[i].z;
            }

            ::operator delete(scratch);
        }

    }


    void VID_SOFTWARE::ApplyGammaToPaletteRaw(void* palette, const Gamma& rawGamma)
    {
        applyGammaToPaletteEntries(static_cast<DWORD*>(palette), &rawGamma);
    }


    void VID_SOFTWARE::SetGamma(const Gamma& rawGamma, unsigned n_gamma)
    {


        const std::uint32_t blockBytes = static_cast<std::uint32_t>(PaletteSize());
        if (!m_frameStorage || (formatFlags() & VID_TYPE_PALETTE) == 0u)
            return;

        if (n_gamma == 4u)
        {
            if ((formatFlags() & VID_TYPE_3D) != 0u)
            {
                SetGamma(altGammaRaw[0], 0u);
                SetGamma(altGammaRaw[1], 1u);
                SetGamma(altGammaRaw[2], 2u);
                SetGamma(altGammaRaw[3], 3u);
                return;
            }

            std::memcpy(m_frameStorage, m_frameStorage + blockBytes, blockBytes);
            if (spriteClass != 8u && (property & P_GAMMA) == 0u)
                ApplyGammaToPaletteRaw(m_frameStorage, rawGamma);
            return;
        }

        if (n_gamma >= 4u)
        {
            logVidResourceError(4, "n_gamma in VID_SOFTWARE::SetGamma", static_cast<int>(n_gamma));
            return;
        }

        altGammaRaw[n_gamma] = rawGamma;

        if ((formatFlags() & VID_TYPE_3D) == 0u)
        {
            BYTE* const oldBacking = m_frameStorage;
            const std::uint32_t addedBytes = blockBytes * 3u;
            m_frameStorageBytes += addedBytes;
            g_vidMemoryInUse += static_cast<int>(addedBytes);

            m_frameStorage = static_cast<BYTE*>(
                ::operator new(static_cast<std::size_t>(m_frameStorageBytes), std::nothrow));
            if (!m_frameStorage)
            {
                logVidResourceError(2, "SetGamma", static_cast<int>(m_frameStorageBytes));
                return;
            }

            const std::uint32_t fourPaletteBytes = blockBytes * 4u;
            const std::uint32_t payloadBytes = m_frameStorageBytes - fourPaletteBytes;
            std::memcpy(m_frameStorage + fourPaletteBytes,
                        oldBacking + blockBytes,
                        payloadBytes);
            std::memcpy(m_frameStorage,
                        m_frameStorage + fourPaletteBytes,
                        blockBytes);
            std::memcpy(m_frameStorage + blockBytes,
                        m_frameStorage + fourPaletteBytes,
                        blockBytes);
            std::memcpy(m_frameStorage + 2u * blockBytes,
                        m_frameStorage,
                        2u * blockBytes);

            ::operator delete(oldBacking);

            if (m_frameOffsets && static_cast<std::int16_t>(noCadr) > 0)
            {
                const int frameCount = static_cast<int>(static_cast<std::int16_t>(noCadr));
                for (int frame = 0; frame < frameCount; ++frame)
                    m_frameOffsets[frame] += addedBytes;
            }

            type = static_cast<WORD>(type | VID_TYPE_3D);
            for (VID* mirror = nextMirrorVid(); mirror != this; mirror = mirror->nextMirrorVid())
            {
                auto* const softwareMirror = static_cast<VID_SOFTWARE*>(mirror);
                softwareMirror->type = static_cast<WORD>(softwareMirror->type | VID_TYPE_3D);
                softwareMirror->m_frameStorage = m_frameStorage;
            }
        }

        BYTE* const destination = m_frameStorage + blockBytes * n_gamma;
        BYTE* const source = m_frameStorage + 4u * blockBytes;
        std::memcpy(destination, source, blockBytes);

        Gamma applyGamma = rawGamma;
        if (spriteClass != 8u && (property & P_GAMMA) == 0u)
        {
            Gamma composed{};
            composed.setSaturatingAdd(Graph->rawGammaPair(), rawGamma);
            applyGamma = composed;
        }

        ApplyGammaToPaletteRaw(destination, applyGamma);
    }


    void VID_SOFTWARE::SetReColorForArmy(int value)
    {


        if ((formatFlags() & VID_TYPE_PALETTE) == 0u)
        {
            logVidResourceError(10, "SetReColorForArmy for non paletted vid", 0);
            return;
        }


        if (!isMirrorChainOwner())
        {
            VID* const next = nextMirrorVid();
            VID* previous = next;
            while (previous->nextMirrorVid() != this)
                previous = previous->nextMirrorVid();
            previous->nextMirror = next;
            nextMirror = this;

            BYTE* const sharedFrameStorage = m_frameStorage;
            BYTE* const privateFrameStorage = static_cast<BYTE*>(
                ::operator new(static_cast<std::size_t>(m_frameStorageBytes), std::nothrow));
            m_frameStorage = privateFrameStorage;
            if (!privateFrameStorage)
            {
                logVidResourceError(2, "SetGamma", static_cast<int>(m_frameStorageBytes));
                return;
            }

            g_vidMemoryInUse += static_cast<int>(m_frameStorageBytes);
            std::memcpy(privateFrameStorage, sharedFrameStorage,
                        static_cast<std::size_t>(m_frameStorageBytes));

            DWORD* const sharedFrameOffsets = m_frameOffsets;
            const int frameCount = static_cast<int>(static_cast<std::int16_t>(noCadr));
            const std::uint32_t frameOffsetBytes = static_cast<std::uint32_t>(frameCount) << 2u;
            m_frameOffsets = static_cast<DWORD*>(
                ::operator new(static_cast<std::size_t>(frameOffsetBytes), std::nothrow));
            if (!m_frameOffsets)
            {
                logVidResourceError(2, "cadrShift", frameCount);
                return;
            }
            std::memcpy(m_frameOffsets, sharedFrameOffsets, frameOffsetBytes);
        }

        const std::uint32_t paletteBytes = static_cast<std::uint32_t>(PaletteSize());
        const std::uint32_t basePaletteBlock =
            (formatFlags() & VID_TYPE_3D) != 0u ? 4u : 1u;
        BYTE* const basePalette = m_frameStorage + paletteBytes * basePaletteBlock;


        if (!m_gammaPaletteScratch)
        {
            m_gammaPaletteScratch = static_cast<BYTE*>(
                ::operator new(static_cast<std::size_t>(paletteBytes), std::nothrow));
            std::memcpy(m_gammaPaletteScratch, basePalette, paletteBytes);
        }

        DWORD* const destination = reinterpret_cast<DWORD*>(basePalette);
        const DWORD* const source = reinterpret_cast<const DWORD*>(m_gammaPaletteScratch);
        const std::uint32_t packed = static_cast<std::uint32_t>(value);
        const int requestedBlue = static_cast<int>(packed & 0xFFu);
        const int requestedGreen = static_cast<int>((packed >> 8u) & 0xFFu);
        const int requestedRed = static_cast<int>((packed >> 16u) & 0xFFu);

        const auto clampByte = [](int channel) noexcept -> int
        {
            if (channel < 0)
                return 0;
            if (channel > 255)
                return 255;
            return channel;
        };

        for (std::size_t index = 0; index < SOFTWARE_GAMMA_ENTRY_COUNT; ++index)
        {
            const std::uint32_t original = source[index];
            const int originalRed = static_cast<int>((original >> 16u) & 0xFFu);
            const int originalGreen = static_cast<int>((original >> 8u) & 0xFFu);
            const int originalBlue = static_cast<int>(original & 0xFFu);


            if (originalRed <= 20)
                continue;
            const float selectorScale =
                static_cast<float>(originalRed) * 0.006756756920367479f;
            const int maximumGreen = truncateFloatToInt32Software(selectorScale * 80.0f);
            if (originalGreen > maximumGreen)
                continue;
            const int maximumRedBlueDelta =
                truncateFloatToInt32Software(selectorScale * 30.0f);
            if (std::abs(originalRed - originalBlue) > maximumRedBlueDelta)
                continue;


            const float intensity = static_cast<float>(originalRed) * 0.0078125f;
            const int scaledBlue = clampByte(
                truncateFloatToInt32Software(static_cast<float>(requestedBlue) * intensity));
            const int scaledGreen = clampByte(
                truncateFloatToInt32Software(static_cast<float>(requestedGreen) * intensity));
            const int scaledRed = clampByte(
                truncateFloatToInt32Software(static_cast<float>(requestedRed) * intensity));

            const int outBlue = std::min(255, scaledBlue + originalGreen);
            const int outGreen = std::min(255, scaledGreen + originalGreen);
            const int outRed = std::min(255, scaledRed + originalGreen);
            destination[index] = 0xFF000000u |
                (static_cast<DWORD>(outRed) << 16u) |
                (static_cast<DWORD>(outGreen) << 8u) |
                static_cast<DWORD>(outBlue);
        }


        if ((formatFlags() & VID_TYPE_3D) != 0u)
        {
            for (unsigned slot = 0; slot < 4u; ++slot)
                SetGamma(altGammaRaw[slot], slot);
        }
        else
        {
            SetGamma(gammaRaw, 4u);
        }
    }

    void VID_SOFTWARE::Draw(const SPRITE* sprite)
    {

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        GRAPH* const graph = Graph;
                const int clipLeft = g_softwareClipLeft;
        const int clipTop = g_softwareClipTop;
        const int clipRight = g_softwareClipRight;
        const int clipBottom = g_softwareClipBottom;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();
        const int drawLeft = softwareDrawXToInt32(sprite->X(), cameraX, sizeX / 2);
        int drawTop = softwareDrawYToInt32(sprite->Y(), sprite->Z(), cameraY, sizeY / 2);

        if (drawLeft + sizeX < clipLeft || drawLeft >= clipRight ||
            drawTop + sizeY < clipTop || drawTop >= clipBottom)
            return;

        int baseDepth = static_cast<int>(sprite->Z() * 8.0f);
        if ((property & P_ALWAYSTOP) != 0u && baseDepth < 0x3FFF)
            baseDepth += 0x3FFF;
        else if ((property & P_WAVE) != 0u)
        {
            const int waveDepth = static_cast<int>(
                SPRITE::rawDirectionSin(static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu)) *
                moveUpZ() * 8.0f);
            baseDepth += waveDepth;
            drawTop += waveDepth / -8;
        }

        const int frame = sprite->currentFrame();
        BYTE* const frameBase = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(frameBase));
        BYTE* row = frameBase + 2 + 6 * contourCount;
        const int frameTop = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row));
        const int rowCount = static_cast<std::int16_t>(*reinterpret_cast<const WORD*>(row + 2));
        row += 4;
        drawTop += frameTop;
        const int drawBottom = drawTop + rowCount;
        if (drawTop >= clipBottom || drawBottom < clipTop)
            return;

        WORD* const depthBase = graph->softwareDepthBuffer();
        const int depthPitch = graph->softwareDepthPitch();
        if (!graph->backBufferPixels())
            graph->lockBackBuffer();
        DWORD* const colorBase = static_cast<DWORD*>(graph->backBufferPixels());
        const int colorPitch = graph->backBufferPitchPixels();
        const WORD typeFlags = formatFlags();
        const bool texturePayload = (typeFlags & VID_TYPE_TEXTURE) != 0u;
        if (!texturePayload)
            return;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool directPayload = !palettePayload;

        const BYTE* drawPaletteBlock = nullptr;
        std::array<DWORD, 256> drawPaletteScratch{};
        {

            const size_t blockBytes = static_cast<size_t>(PaletteSize());
            Gamma spriteOverride{};
            const bool hasSpriteOverride = sprite->spriteGammaOverride(spriteOverride);
            size_t blockIndex = 0u;
            if (!hasSpriteOverride)
            {
                blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                    ? static_cast<size_t>((sprite->runtimeFlags() >> 12u) & 3u)
                    : 0u;
                drawPaletteBlock = frameStorage() + blockIndex * blockBytes;
            }
            else
            {

                blockIndex = (typeFlags & VID_TYPE_3D) != 0u ? 4u : 0u;
                std::memcpy(drawPaletteScratch.data(), frameStorage() + blockIndex * blockBytes, blockBytes);
                Gamma drawPaletteApply{};
                drawPaletteApply = sprite->GetGamma();
                if ((property & P_GAMMA) == 0u)
                    drawPaletteApply.setSaturatingAdd(drawPaletteApply, Graph->rawGammaPair());
                ApplyGammaToPaletteRaw(drawPaletteScratch.data(), drawPaletteApply);
                drawPaletteBlock = reinterpret_cast<const BYTE*>(drawPaletteScratch.data());
            }
            g_softwarePaletteLookup = reinterpret_cast<const DWORD*>(drawPaletteBlock);
        }


        const int constantDepthInt = std::min(baseDepth + 0x400, 0x7FFF);
        const WORD baseDepthWord = static_cast<WORD>(baseDepth);


        const bool slopedConstantDepthPayload =
            !zPalettePayload && this->sizeZ() > this->sizeY();
        const int constantDepthStep = slopedConstantDepthPayload ? -8 : 0;
        int rowDepthInt = constantDepthInt;
        if (slopedConstantDepthPayload)
            rowDepthInt += rowCount * 8;
        WORD rowDepth = static_cast<WORD>(rowDepthInt);

        if (alphaPalettePayload)
        {
            g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
        }
        else if (zPalettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(baseDepthWord) | (static_cast<DWORD>(baseDepthWord) << 16u);
            g_softwareDepthWordPrimary = baseDepthWord;
            g_softwareDepthWordSecondary = baseDepthWord;
        }
        else if (palettePayload)
        {
            g_packedSoftwareDepth = static_cast<DWORD>(rowDepth) | (static_cast<DWORD>(rowDepth) << 16u);
            g_softwareDepthWordPrimary = rowDepth;
            g_softwareDepthWordSecondary = rowDepth;
        }

        auto directWordToDword = [](WORD value) -> DWORD
        {

            return (static_cast<DWORD>(value & 0x001Fu) << 3u) |
                   ((static_cast<DWORD>(value) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                   ((static_cast<DWORD>(value) << (16u - g_color16RedShift)) & 0x00FF0000u);
        };

        for (int sourceRow = 0; sourceRow < rowCount; ++sourceRow)
        {
            const int dy = drawTop + sourceRow;
            if (dy >= clipBottom)
                break;
            const bool visibleRow = dy >= clipTop;
            int sourceX = 0;
            for (;;)
            {
                const int skip = row[0];
                const int run = row[1];
                row += 2;
                if (skip == 0 && run == 0)
                    break;
                sourceX += skip;

                const WORD* zWords = nullptr;
                if (zPalettePayload)
                {
                    zWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<size_t>(run) * 2u;
                }

                if (visibleRow)
                {
                    DWORD* const colorRow = colorBase + static_cast<size_t>(dy) * static_cast<size_t>(colorPitch);
                    WORD* const depthRow = depthBase + static_cast<size_t>(dy) * static_cast<size_t>(depthPitch);
                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;

                        const WORD oldDepth = depthRow[dx];
                        WORD z = rowDepth;
                        if (zPalettePayload)
                            z = static_cast<WORD>(baseDepthWord + zWords[i]);

                        if (alphaPalettePayload)
                        {

                            if (z < oldDepth)
                                continue;
                        }
                        else if (zPalettePayload)
                        {
                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (palettePayload)
                        {


                            if (static_cast<std::int16_t>(z) <=
                                static_cast<std::int16_t>(oldDepth))
                                continue;
                        }
                        else if (directPayload)
                        {


                            if (rowDepthInt < static_cast<int>(oldDepth))
                                continue;
                        }

                        DWORD source = 0u;
                        if (palettePayload)
                        {
                            source = reinterpret_cast<const DWORD*>(drawPaletteBlock)[paletteIndexes[i]];
                        }
                        else
                        {
                            source = directWordToDword(colorWords[i]);
                        }

                        if (alphaPalettePayload)
                        {
                            const DWORD alpha = source >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destWeight = 256u - sourceWeight;
                            const DWORD dest = colorRow[dx];
                            const DWORD b = (sourceWeight * (source & 0xFFu) + destWeight * (dest & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((source >> 8u) & 0xFFu) + destWeight * ((dest >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((source >> 16u) & 0xFFu) + destWeight * ((dest >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = 0xFF000000u | (r << 16u) | (g << 8u) | b;
                            continue;
                        }

                        depthRow[dx] = z;
                        colorRow[dx] = source;
                    }
                }
                sourceX += run;
            }


            if (constantDepthStep != 0)
            {
                rowDepthInt += constantDepthStep;
                rowDepth = static_cast<WORD>(rowDepthInt);
                if (palettePayload)
                    g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }

    }

    int VID_SOFTWARE::DrawShadow(const SPRITE* sprite) const
    {

        if (!frameStorage())
            return 0;
        if (!frameOffsets())
            return 0;

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return 0;

        GRAPH* const graph = Graph;
        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());
        const float cameraX = appDraw.cameraShiftX();
        const float cameraY = appDraw.cameraShiftY();

        const float drawLeft = sprite->X() - cameraX - static_cast<float>(sizeX / 2);
        const float drawTop = sprite->Y() - sprite->Z() - cameraY - static_cast<float>(sizeY / 2);

        const int leftI = static_cast<int>(drawLeft);
        if (leftI + sizeX + 200 < g_softwareClipLeft || leftI >= g_softwareClipRight)
            return leftI;

        const int topI = static_cast<int>(drawTop);
        if (topI + sizeY + 100 < g_softwareClipTop || topI >= g_softwareClipBottom)
            return topI;


        int earlyReturnValue = topI;
        float shadowZ = sprite->Z();
        const float recordBaseY = drawTop + sprite->Z();
        if ((property & P_WAVE) != 0u)
        {
            const int waveIndex = static_cast<int>((core::CurrentTimeMilliseconds() >> 3u) & 0xFFu);
            earlyReturnValue = waveIndex;
            shadowZ += SPRITE::rawDirectionSin(waveIndex) * moveUpZ();
        }

        const int frame = sprite->currentFrame();
        BYTE* frameData = frameStorage() + frameOffsets()[frame];
        const int contourCount = static_cast<int>(*reinterpret_cast<const std::int16_t*>(frameData));
        if (contourCount == 0)
            return earlyReturnValue;

        struct ShadowVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD diffuse;
            DWORD specular;
        };


        ShadowVertex vertices[514];
        const BYTE* record = frameData + 2;
        const DWORD firstPassColor = 0x00A4A4A4u;

        int generatedVertexCount = 0;
        if (contourCount * 2 > 0)
        {
            generatedVertexCount = contourCount * 2;
        }
        for (int i = 0; i < generatedVertexCount / 2; ++i, record += 6)
        {
            const float recordX = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 0));
            const float recordY = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 2));
            const float recordZ = static_cast<float>(*reinterpret_cast<const std::int16_t*>(record + 4));

            ShadowVertex& first = vertices[i * 2 + 0];
            ShadowVertex& second = vertices[i * 2 + 1];

            first.x = recordX + drawLeft;
            const float projectedY = recordY + recordBaseY;
            const float projectedZ = recordZ + shadowZ;
            first.y = projectedY - projectedZ;
            first.z = projectedZ * 0.00012207031f + 0.015625f;
            first.rhw = 1.0f;
            first.diffuse = firstPassColor;

            second.x = first.x + projectedZ * 0.34999999f;
            second.y = projectedY - projectedZ * 0.69999999f;
            second.z = 0.015625f;
            second.rhw = 1.0f;
            second.diffuse = firstPassColor;
        }

        const int vertexCount = contourCount * 2 + 2;
        std::memcpy(&vertices[generatedVertexCount + 0], &vertices[0], sizeof(ShadowVertex));
        std::memcpy(&vertices[generatedVertexCount + 1], &vertices[1], sizeof(ShadowVertex));

        graph->setRenderStateCached(29u, 0u);
        if (IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(Graph ? Graph->deviceHandle() : nullptr))
            device->SetTexture(0, nullptr);
        graph->setRenderStateCached(22u, 3u);
        graph->SetAlphaBlend(1u, 3u);

        for (int i = 0; i < vertexCount; ++i)
            vertices[i].diffuse = firstPassColor;
        graph->drawPrimitiveUp(5u, 0xC4u, vertices, 24u, vertexCount);

        graph->setRenderStateCached(22u, 2u);
        graph->SetAlphaBlend(9u, 2u);
        for (int i = 0; i < vertexCount; ++i)
            vertices[i].diffuse = 0x008F8F8Fu;
        graph->drawPrimitiveUp(5u, 0xC4u, vertices, 24u, vertexCount);

        return graph->setRenderStateCached(22u, 3u);
    }

    int VID_SOFTWARE::HaveShadow() const
    {

        if (!m_frameStorage)
            return 0;
        const DWORD firstShift = *m_frameOffsets;
        WORD value = 0;
        std::memcpy(&value, m_frameStorage + firstShift, sizeof(value));
        return static_cast<int>(value);
    }

    int VID_SOFTWARE::PaletteSize() const
    {

        return 1024;
    }

}
