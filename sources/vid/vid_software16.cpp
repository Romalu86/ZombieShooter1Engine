#include "vid/vid_software16.h"
#include "sprite.h"
#include "graph.h"
#include "map.h"
#include "graphics/base_texture.h"
#include "graphics/color.h"
#include "core/application.h"
#include "core/log.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <xmmintrin.h>
#include <array>
#include <cstring>
#include <new>

namespace as1
{
    namespace
    {
        int truncateFloatToInt32Software16(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        int wrapSubSoftware16(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int softwareDrawXToInt3216(float x, float cameraX, int halfWidth) noexcept
        {
            int value = wrapSubSoftware16(truncateFloatToInt32Software16(x), truncateFloatToInt32Software16(cameraX));
            return wrapSubSoftware16(value, halfWidth);
        }

        int softwareDrawYToInt3216(float y, float z, float cameraY, int halfHeight) noexcept
        {
            float projectedY = y - z;
            int value = wrapSubSoftware16(truncateFloatToInt32Software16(projectedY), truncateFloatToInt32Software16(cameraY));
            return wrapSubSoftware16(value, halfHeight);
        }
    }
    DWORD* expandSoftware16ColorToBgra(DWORD* destination, const WORD* source) noexcept
    {

        const DWORD raw = static_cast<DWORD>(*source);
        const DWORD red = (raw << ((16u - g_color16RedShift) & 31u)) & 0x00FF0000u;
        const DWORD green = (raw << ((8u - g_color16GreenShift) & 31u)) & 0x0000FF00u;
        const DWORD blue = (raw & 0x001Fu) << 3u;
        *destination = red | green | blue;
        return destination;
    }


    int drawSoftware16AlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* destinationDepth, WORD* destinationColor, int count) noexcept
    {

        if (count <= 0)
            return count;

        const std::uintptr_t depthAddress = reinterpret_cast<std::uintptr_t>(destinationDepth);
        const std::uintptr_t colorAddress = reinterpret_cast<std::uintptr_t>(destinationColor);
        const std::int32_t result = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(depthAddress - colorAddress));

        for (int i = 0; i < count; ++i)
        {
            if (static_cast<WORD>(g_packedSoftwareDepth) >= destinationDepth[i])
            {
                const Color* const palette = reinterpret_cast<const Color*>(g_softwarePaletteLookup);
                Color source = palette[paletteIndexes[i]];
                DWORD unpacked = 0u;
                (void)expandSoftware16ColorToBgra(&unpacked, &destinationColor[i]);
                Color destination;
                destination.color = unpacked;
                destination.AlphaAdd(source, source.Alpha());
                destinationColor[i] = static_cast<WORD>(
                    ((destination.color >> 3u) & 0x001Fu) |
                    (g_color16RedMask & (destination.color >> (16u - g_color16RedShift))) |
                    (g_color16GreenMask & (destination.color >> (8u - g_color16GreenShift))));
            }
        }
        return result;
    }

    VID_SOFTWARE16::VID_SOFTWARE16(const VID_SOFTWARE16& other)
        : VID_SOFTWARE(other)
    {

    }

    VID_SOFTWARE16* VID_SOFTWARE16::CreateMirror()
    {

        return new (std::nothrow) VID_SOFTWARE16(*this);
    }


    __forceinline
    DWORD VID_SOFTWARE16::UnpackRgb565ToBgra(WORD value) const
    {

        DWORD out = 0;
        (void)expandSoftware16ColorToBgra(&out, &value);
        return out;
    }

    __forceinline
    WORD VID_SOFTWARE16::PackRgb565FromBgra(DWORD value) const
    {

        return static_cast<WORD>(
            ((value >> 3u) & 0x001Fu) |
            (g_color16RedMask & (value >> (16u - g_color16RedShift))) |
            (g_color16GreenMask & (value >> (8u - g_color16GreenShift))));
    }


    void VID_SOFTWARE16::SetReColorForArmy(int value)
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

            BYTE* const sharedFrameStorage = frameStorage();
            const DWORD storageBytes = frameStorageBytes();
            BYTE* const privateFrameStorage = static_cast<BYTE*>(
                ::operator new(static_cast<std::size_t>(storageBytes), std::nothrow));
            setFrameStorage(privateFrameStorage);
            if (!privateFrameStorage)
            {
                logVidResourceError(2, "SetGamma", static_cast<int>(storageBytes));
                return;
            }
            g_vidMemoryInUse += static_cast<int>(storageBytes);
            std::memcpy(privateFrameStorage, sharedFrameStorage,
                        static_cast<std::size_t>(storageBytes));

            DWORD* const sharedFrameOffsets = frameOffsets();
            const int frameCount = static_cast<int>(static_cast<std::int16_t>(totalFrames()));
            const std::uint32_t frameOffsetBytes = static_cast<std::uint32_t>(frameCount) << 2u;
            DWORD* const privateFrameOffsets = static_cast<DWORD*>(
                ::operator new(static_cast<std::size_t>(frameOffsetBytes), std::nothrow));
            setFrameOffsets(privateFrameOffsets);
            if (!privateFrameOffsets)
            {
                logVidResourceError(2, "cadrShift", frameCount);
                return;
            }
            std::memcpy(privateFrameOffsets, sharedFrameOffsets, frameOffsetBytes);
        }


        const std::uint32_t paletteBytes = static_cast<std::uint32_t>(PaletteSize());
        const std::uint32_t basePaletteBlock =
            (formatFlags() & VID_TYPE_3D) != 0u ? 4u : 1u;
        BYTE* const basePalette = frameStorage() + paletteBytes * basePaletteBlock;

        BYTE*& scratch = gammaPaletteScratchRef();
        if (!scratch)
        {
            scratch = static_cast<BYTE*>(
                ::operator new(static_cast<std::size_t>(paletteBytes), std::nothrow));
            std::memcpy(scratch, basePalette, paletteBytes);
        }

        WORD* const destination = reinterpret_cast<WORD*>(basePalette);
        const WORD* const source = reinterpret_cast<const WORD*>(scratch);
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


        for (std::size_t index = 0; index < 256u; ++index)
        {
            const DWORD original = UnpackRgb565ToBgra(source[index]);
            const int originalRed = static_cast<int>((original >> 16u) & 0xFFu);
            const int originalGreen = static_cast<int>((original >> 8u) & 0xFFu);
            const int originalBlue = static_cast<int>(original & 0xFFu);

            if (originalRed <= 20)
                continue;

            const float selectorScale =
                static_cast<float>(originalRed) * 0.006756756920367479f;
            const int maximumGreen = truncateFloatToInt32Software16(selectorScale * 80.0f);
            if (originalGreen > maximumGreen)
                continue;
            const int maximumRedBlueDelta =
                truncateFloatToInt32Software16(selectorScale * 30.0f);
            if (std::abs(originalRed - originalBlue) > maximumRedBlueDelta)
                continue;

            const float intensity = static_cast<float>(originalRed) * 0.0078125f;
            const int scaledBlue = clampByte(
                truncateFloatToInt32Software16(static_cast<float>(requestedBlue) * intensity));
            const int scaledGreen = clampByte(
                truncateFloatToInt32Software16(static_cast<float>(requestedGreen) * intensity));
            const int scaledRed = clampByte(
                truncateFloatToInt32Software16(static_cast<float>(requestedRed) * intensity));


            const WORD scaledPacked = PackRgb565FromBgra(
                0xFF000000u |
                (static_cast<DWORD>(scaledRed) << 16u) |
                (static_cast<DWORD>(scaledGreen) << 8u) |
                static_cast<DWORD>(scaledBlue));
            const WORD grayPacked = PackRgb565FromBgra(
                0xFF000000u |
                (static_cast<DWORD>(originalGreen) << 16u) |
                (static_cast<DWORD>(originalGreen) << 8u) |
                static_cast<DWORD>(originalGreen));

            unsigned blue = static_cast<unsigned>(scaledPacked & 0x001Fu) +
                            static_cast<unsigned>(grayPacked & 0x001Fu);
            if (blue > 0x001Fu)
                blue = 0x001Fu;

            unsigned green = static_cast<unsigned>(scaledPacked & g_color16GreenMask) +
                             static_cast<unsigned>(grayPacked & g_color16GreenMask);
            if (green > g_color16GreenMask)
                green = g_color16GreenMask;

            unsigned red = static_cast<unsigned>(scaledPacked & g_color16RedMask) +
                           static_cast<unsigned>(grayPacked & g_color16RedMask);
            if (red > g_color16RedMask)
                red = g_color16RedMask;

            WORD out = static_cast<WORD>(blue | green | red);

            if (g_color16RedMask == 0x7C00u)
                out = static_cast<WORD>(out | 0x8000u);
            destination[index] = out;
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


    void VID_SOFTWARE16::ApplyGammaToPaletteRaw(void* palette, const Gamma& rawGamma)
    {
        if (!palette || (rawGamma.first == 0u && rawGamma.second == 0u))
            return;
        if ((formatFlags() & VID_TYPE_ALPHA) != 0u)
        {
            DWORD* entries = static_cast<DWORD*>(palette);
            for (size_t i = 0; i < 256u; ++i)
                entries[i] = GammaRawBlend(rawGamma, entries[i]);
            return;
        }
        WORD* entries = static_cast<WORD*>(palette);
        for (size_t i = 0; i < 256u; ++i)
            entries[i] = PackRgb565FromBgra(
                GammaRawBlend(rawGamma, UnpackRgb565ToBgra(entries[i])));
    }

    void VID_SOFTWARE16::Draw(const SPRITE* sprite)
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
        const int drawLeft = softwareDrawXToInt3216(sprite->X(), cameraX, sizeX / 2);
        int drawTop = softwareDrawYToInt3216(sprite->Y(), sprite->Z(), cameraY, sizeY / 2);

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
        WORD* const colorBase = static_cast<WORD*>(graph->backBufferPixels());
        const int colorPitch = graph->backBufferPitchPixels();
        const WORD typeFlags = formatFlags();
        if ((typeFlags & VID_TYPE_TEXTURE) == 0u)
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
                    WORD* const colorRow = colorBase + static_cast<size_t>(dy) * static_cast<size_t>(colorPitch);
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
                        else if (palettePayload || directPayload)
                        {


                            if (rowDepthInt < static_cast<int>(oldDepth))
                                continue;
                        }

                        if (alphaPalettePayload)
                        {
                            const DWORD source = reinterpret_cast<const DWORD*>(drawPaletteBlock)[paletteIndexes[i]];
                            const DWORD dest = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = source >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (source & 0xFFu) + destWeight * (dest & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((source >> 8u) & 0xFFu) + destWeight * ((dest >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((source >> 16u) & 0xFFu) + destWeight * ((dest >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);
                            continue;
                        }

                        WORD sourceWord = 0u;
                        if (palettePayload)
                        {
                            sourceWord = reinterpret_cast<const WORD*>(drawPaletteBlock)[paletteIndexes[i]];
                        }
                        else
                        {


                            sourceWord = colorWords[i];
                        }

                        depthRow[dx] = z;
                        colorRow[dx] = sourceWord;
                    }
                }
                sourceX += run;
            }


            if (constantDepthStep != 0)
            {
                rowDepthInt += constantDepthStep;
                rowDepth = static_cast<WORD>(rowDepthInt);
                if (alphaPalettePayload)
                    g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }
    }

    void VID_SOFTWARE16::DrawToVid(
        SPRITE* sprite,
        void* texSizeRaw,
        BASE_TEXTURE* texture,
        BASE_TEXTURE* zTexture)
    {
        VID_HARDWARE::TEX_SIZE* const texSize = static_cast<VID_HARDWARE::TEX_SIZE*>(texSizeRaw);

        const DWORD property = properties();
        if ((runtimeAuxFlags() & 0x40u) != 0u)
            return;

        const VID_HARDWARE::TEX_SIZE& tile = *texSize;
        const int clipLeft = tile.sourceX;
        const int clipTop = tile.sourceY;
        const int clipRight = tile.sourceX + tile.width;
        const int clipBottom = tile.sourceY + tile.height;

        const int sizeX = static_cast<std::int16_t>(vidWidth());
        const int sizeY = static_cast<std::int16_t>(vidHeight());

        float drawLeftFloat = sprite->X();
        drawLeftFloat -= static_cast<float>(sizeX / 2);
        drawLeftFloat -= static_cast<float>(tile.destinationX);
        drawLeftFloat -= static_cast<float>(tile.sourceX);
        const int drawLeft = truncateFloatToInt32Software16(drawLeftFloat);

        float drawTopFloat = sprite->Y() - sprite->Z();
        drawTopFloat -= static_cast<float>(sizeY / 2);
        drawTopFloat -= static_cast<float>(tile.destinationY);
        drawTopFloat -= static_cast<float>(tile.sourceY);
        int drawTop = truncateFloatToInt32Software16(drawTopFloat);

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

        int zPitchBytes = 0;
        WORD* const zBits = zTexture->lock16(&zPitchBytes, nullptr);
        int colorPitchBytes = 0;
        WORD* const colorBits = texture->lock16(&colorPitchBytes, nullptr);
        const WORD typeFlags = formatFlags();


        const bool texturePayload = (typeFlags & VID_TYPE_TEXTURE) != 0u;
        const bool palettePayload = (typeFlags & VID_TYPE_PALETTE) != 0u;
        const bool alphaPalettePayload =
            (typeFlags & (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE)) ==
            (VID_TYPE_ALPHA | VID_TYPE_TEXTURE | VID_TYPE_PALETTE);
        const bool zPalettePayload = !alphaPalettePayload && texturePayload && palettePayload &&
                                     (typeFlags & VID_TYPE_ZBUFFER) != 0u;
        const bool constantPalettePayload = texturePayload && palettePayload && !alphaPalettePayload && !zPalettePayload;
        const bool directPayload = texturePayload && !palettePayload;

        if (!texturePayload)
        {
            texture->unlock();
            zTexture->unlock();
            return;
        }

        const BYTE* paletteBlock = nullptr;
        if (palettePayload)
        {
            const std::size_t blockBytes = static_cast<std::size_t>(PaletteSize());
            const std::size_t blockIndex = (typeFlags & VID_TYPE_3D) != 0u
                ? static_cast<std::size_t>((sprite->runtimeFlags() >> 12u) & 3u)
                : 0u;
            paletteBlock = frameStorage() + blockIndex * blockBytes;
            g_softwarePaletteLookup = reinterpret_cast<const DWORD*>(paletteBlock);
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
                    row += static_cast<std::size_t>(run) * 2u;
                }

                const BYTE* paletteIndexes = nullptr;
                const WORD* colorWords = nullptr;
                if (palettePayload)
                {
                    paletteIndexes = row;
                    row += static_cast<std::size_t>(run);
                }
                else
                {
                    colorWords = reinterpret_cast<const WORD*>(row);
                    row += static_cast<std::size_t>(run) * 2u;
                }

                if (visibleRow)
                {

                    WORD* const colorRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(colorBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(colorPitchBytes));
                    WORD* const depthRow = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(zBits) +
                        static_cast<std::ptrdiff_t>(dy) * static_cast<std::ptrdiff_t>(zPitchBytes));

                    for (int i = 0; i < run; ++i)
                    {
                        const int dx = drawLeft + sourceX + i;
                        if (dx < clipLeft || dx >= clipRight)
                            continue;
                        const WORD oldDepth = depthRow[dx];

                        if (alphaPalettePayload)
                        {


                            if (rowDepth < oldDepth)
                                continue;

                            const DWORD sourceColor = reinterpret_cast<const DWORD*>(paletteBlock)[paletteIndexes[i]];
                            const DWORD destination = UnpackRgb565ToBgra(colorRow[dx]);
                            const DWORD alpha = sourceColor >> 24u;
                            const DWORD sourceWeight = alpha + 1u;
                            const DWORD destinationWeight = 256u - sourceWeight;
                            const DWORD b = (sourceWeight * (sourceColor & 0xFFu) + destinationWeight * (destination & 0xFFu)) >> 8u;
                            const DWORD g = (sourceWeight * ((sourceColor >> 8u) & 0xFFu) + destinationWeight * ((destination >> 8u) & 0xFFu)) >> 8u;
                            const DWORD r = (sourceWeight * ((sourceColor >> 16u) & 0xFFu) + destinationWeight * ((destination >> 16u) & 0xFFu)) >> 8u;
                            colorRow[dx] = PackRgb565FromBgra(0xFF000000u | (r << 16u) | (g << 8u) | b);

                            continue;
                        }

                        if (zPalettePayload)
                        {
                            const WORD z = static_cast<WORD>(baseDepthWord + zWords[i]);

                            if (static_cast<std::int16_t>(z) <= static_cast<std::int16_t>(oldDepth))
                                continue;
                            depthRow[dx] = z;
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                            continue;
                        }


                        if (rowDepthInt < static_cast<int>(oldDepth))
                            continue;

                        depthRow[dx] = rowDepth;
                        if (constantPalettePayload)
                            colorRow[dx] = reinterpret_cast<const WORD*>(paletteBlock)[paletteIndexes[i]];
                        else if (directPayload)
                            colorRow[dx] = colorWords[i];
                    }
                }

                sourceX += run;
            }


            if (constantDepthStep != 0)
            {
                rowDepthInt += constantDepthStep;
                rowDepth = static_cast<WORD>(rowDepthInt);
                if (alphaPalettePayload)
                    g_packedSoftwareDepth = (g_packedSoftwareDepth & 0xFFFF0000u) | static_cast<DWORD>(rowDepth);
            }
        }


        texture->unlock();
        zTexture->unlock();
    }

    int VID_SOFTWARE16::PaletteSize() const
    {

        return ((formatFlags() & VID_TYPE_ALPHA) != 0 ? 4 : 2) << 8;
    }
}
