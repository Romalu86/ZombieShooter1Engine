#include "region.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "map.h"
#include "win/application_win.h"
#include "graph.h"
#include "core/application.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "graphics/base_texture.h"
#include "menu.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>

namespace as1
{
    namespace
    {
        __forceinline int regionTruncateFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<std::int32_t>(value);
        }

        __forceinline bool regionOrderedLess(float lhs, float rhs) noexcept
        {
            return lhs < rhs;
        }
    }


    REGION::REGION(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent),
          m_fogRampPhase(0),
          m_lastFogRampPhase(0),
          m_fogRamp(nullptr),
          m_regionFlags(0),
          m_fogEnd(0),
          m_fogStart(0),
          m_fogColor(0xFF000000u),
          m_reservedRegionState8C(0),
          m_regionWidth(0.0f),
          m_regionHeight(0.0f),
          m_savedRegionVid(EmptyVid)
    {
        for (int i = 0; i < 6; ++i)
        {
            m_sourceVidMap[i] = nullptr;
            m_targetVidMap[i] = nullptr;
        }
    }


    REGION::~REGION()
    {
        win::applicationWinInstance()->transferFrom(this);
        if (m_fogRamp)
        {
            ::operator delete(m_fogRamp);
        }
    }


    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept
    {
        const core::ApplicationDrawPassBucket& bucket =
            core::GlobalApplicationDrawDispatcherState().drawPassBucket(7);

        for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
        {
            SPRITE* const candidate = bucket.spriteAt(cursor);
            if (!candidate)
                continue;

            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() != B_REGION)
                continue;

            const float zGate = candidate->Z() + 25.0f;
            if (!(zGate > z))
                continue;

            REGION* const region = static_cast<REGION*>(candidate);
            if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
            {
                const float cx = candidate->X();
                const float cy = candidate->Y();
                const float halfWidth = region->regionWidth() * 0.5f;
                const float halfHeight = region->regionHeight() * 0.5f;
                if (!(x >= cx - halfWidth) || !(cx + halfWidth >= x) ||
                    !(y >= cy - halfHeight) || !(cy + halfHeight >= y))
                {
                    continue;
                }
            }

            for (int index = 0; index < 6; ++index)
            {
                if (region->sourceMappedVid(index) == sourceVid)
                    return region->targetMappedVid(index);
            }
        }
        return sourceVid;
    }

    void REGION::Draw()
    {
        const int savedFrame = currentFrame();
        const float savedX = X();
        const float savedY = Y();
        VID* const regionVid = Vid();
        GRAPH* const graph = Graph;

        if (regionVid != EmptyVid)
        {
            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                graph->rawSetSoftwareClipBounds(
                    regionTruncateFloatToInt32(regionScreenLeft()),
                    regionTruncateFloatToInt32(regionScreenTop()),
                    regionTruncateFloatToInt32(regionScreenRight()),
                    regionTruncateFloatToInt32(regionScreenBottom()));
            }

            const float halfHeight = m_regionHeight * 0.5f;
            float tileY = savedY - halfHeight;
            const float tileYEnd = savedY + halfHeight;
            int tileIndex = 0;
            while (regionOrderedLess(tileY, tileYEnd))
            {
                const float halfWidth = m_regionWidth * 0.5f;
                float tileX = savedX - halfWidth;
                const float tileXEnd = savedX + halfWidth;
                while (regionOrderedLess(tileX, tileXEnd))
                {
                    if ((regionVid->properties() & P_ONEPHASE) == 0u)
                    {
                        const int noCadr = static_cast<int>(regionVid->totalFrames());
                        setCurrentFrameDirect((savedFrame + 2 * tileIndex++) % noCadr);
                    }
                    setXPosition(tileX + static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()) / 2));
                    setYPosition(tileY + static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()) / 2));
                    regionVid->Draw(this);
                    tileX += static_cast<float>(static_cast<std::int16_t>(regionVid->vidWidth()));
                }
                tileY += static_cast<float>(static_cast<std::int16_t>(regionVid->vidHeight()));
            }

            if ((m_regionFlags & FullViewportFlag) == 0u)
            {
                graph->rawSetSoftwareClipBounds(
                    regionTruncateFloatToInt32(graph->viewportLeft()),
                    regionTruncateFloatToInt32(graph->viewportTop()),
                    regionTruncateFloatToInt32(graph->viewportRight()),
                    regionTruncateFloatToInt32(graph->viewportBottom()));
            }
        }

        setXPosition(savedX);
        setYPosition(savedY);
        setCurrentFrameDirect(savedFrame);

        if (m_fogStart >= m_fogEnd)
        {
            m_fogRampPhase = 0;
            return;
        }

        if ((m_regionFlags & FogAnimatedFlag) != 0u)
        {
            const int phase = static_cast<int>(core::CurrentTimeMilliseconds() & 7u);
            if (static_cast<unsigned>(phase) < static_cast<unsigned>(m_lastFogRampPhase))
            {
                const int limit = 8 * m_fogEnd;
                if (m_fogRampPhase < limit)
                {
                    m_fogRampPhase += 2;
                    m_lastFogRampPhase = phase;
                }
                else if (m_fogRampPhase > limit)
                {
                    m_fogRampPhase = 0;
                    m_lastFogRampPhase = phase;
                }
            }
            m_lastFogRampPhase = phase;
        }
        else
        {
            m_fogRampPhase = 8 * m_fogEnd;
        }

        const DWORD color = m_fogColor;
        const WORD* const ramp = static_cast<const WORD*>(m_fogRamp);
        const int blend = static_cast<int>(m_regionFlags & FogBlendFlag);
        if ((m_regionFlags & FullViewportFlag) != 0u)
        {
            graph->drawFogBufferOverlay(static_cast<float>(graph->ViewXMin()),
                              static_cast<float>(graph->ViewYMin()),
                              static_cast<float>(graph->ViewXMax()),
                              static_cast<float>(graph->ViewYMax()),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
        else
        {
            graph->drawFogBufferOverlay(regionScreenLeft(),
                              regionScreenTop(),
                              regionScreenRight(),
                              regionScreenBottom(),
                              m_fogStart, m_fogEnd, color, ramp,
                              m_fogRampPhase, blend);
        }
        }


    float REGION::regionScreenLeft() const noexcept
    {

        const float halfWidth = m_regionWidth * 0.5f;
        float result = X() - halfWidth;
        result -= core::GlobalApplicationDrawDispatcherState().cameraShiftX();
        return result;
    }


    float REGION::regionScreenTop() const noexcept
    {
        const float halfHeight = m_regionHeight * 0.5f;
        float result = Y() - Z();
        result -= halfHeight;
        result -= core::GlobalApplicationDrawDispatcherState().cameraShiftY();
        return result;
    }


    float REGION::regionScreenRight() const noexcept
    {
        const float halfWidth = m_regionWidth * 0.5f;
        float result = X() + halfWidth;
        result -= core::GlobalApplicationDrawDispatcherState().cameraShiftX();
        return result;
    }


    float REGION::regionScreenBottom() const noexcept
    {
        const float halfHeight = m_regionHeight * 0.5f;
        float result = Y() - Z();
        result += halfHeight;
        result -= core::GlobalApplicationDrawDispatcherState().cameraShiftY();
        return result;
    }

    void REGION::DrawDebugOverlay()
    {
        GRAPH* const graph = Graph;
        const DWORD white = GammaRawCreateOpaque(255, 255, 255);
        graph->Box(regionScreenLeft() - 1.0f,
                        regionScreenTop() - 1.0f,
                        regionScreenRight() + 1.0f,
                        regionScreenBottom() + 1.0f,
                        white);
        }


    int REGION::rebuildRegionFogRamp(int start, int end, int color)
    {
        m_fogEnd = end;
        m_fogStart = start;
        m_fogColor = static_cast<std::uint32_t>(color);
        if (m_fogRamp)
            ::operator delete(m_fogRamp);

        int eaxCarrier = start;
        if (start >= end)
            return eaxCarrier;

        const std::uint32_t difference =
            static_cast<std::uint32_t>(end) - static_cast<std::uint32_t>(start);
        const std::uint32_t allocationSize = difference * 16u + 2u;
        m_fogRamp = ::operator new(static_cast<std::size_t>(allocationSize), std::nothrow);
        if (!m_fogRamp)
        {
            fatalLogError(g_fileLogger, "Enough memory for DrawFog",
                       static_cast<int>(difference * 8u + 1u));
        }

        const std::int32_t count = static_cast<std::int32_t>(difference * 8u);
        if (count < 0)
            return eaxCarrier;

        WORD* const ramp = static_cast<WORD*>(m_fogRamp);
        std::int32_t index = count;
        std::int32_t numerator = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(count) * 255u);
        do
        {
            const std::int32_t signedDifference = static_cast<std::int32_t>(difference);
            std::int32_t quotient = numerator / signedDifference;

            if (quotient < 0)
                quotient += 7;
            const int intensity = quotient >> 3;
            eaxCarrier = intensity;
            if (Graph->lightBuffer()->format() != 41u)
            {
                const WORD palette = Graph->intensityPaletteEntry(
                    static_cast<std::size_t>(intensity));
                eaxCarrier = (eaxCarrier & ~0xFFFF) | static_cast<int>(palette);
            }
            ramp[index] = static_cast<WORD>(eaxCarrier);
            --index;
            numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(numerator) - 255u);
        }
        while (index >= 0);
        return eaxCarrier;
    }

    int REGION::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        RESOURCE* const resource = reinterpret_cast<RESOURCE*>(static_cast<std::uintptr_t>(argument1Carrier));
        if (opcode == 80)
        {
            (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
            resource->write(&m_regionFlags, 4u);
            resource->write(&m_fogEnd, 4u);
            resource->write(&m_fogStart, 4u);
            resource->write(&m_fogColor, 4u);
            resource->write(&m_regionWidth, 4u);
            resource->write(&m_regionHeight, 4u);
            resource->write(&m_persistedRegionState, 4u);
            int nvid = m_savedRegionVid ? m_savedRegionVid->nvid() : -1;
            resource->write(&nvid, 4u);
            for (int i = 0; i < 6; ++i)
            {
                nvid = m_sourceVidMap[i] ? m_sourceVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
                nvid = m_targetVidMap[i] ? m_targetVidMap[i]->nvid() : -1;
                resource->write(&nvid, 4u);
            }
            return 0;
        }
        if (opcode != SpriteActConst::ACT_RESTORE && opcode != SpriteActConst::ACT_RESTORE_OLD_MAP)
            return SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);

        const int routedVersion = argument2Carrier;
        (void)SPRITE::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
        resource->read(&m_regionFlags, 4u);
        int slot80 = 0, slot84 = 0, color88 = 0;
        resource->read(&slot80, 4u);
        resource->read(&slot84, 4u);
        resource->read(&color88, 4u);
        rebuildRegionFogRamp(slot84, slot80, color88);
        if (routedVersion <= 9)
        {
            BYTE legacyByte = 0;
            resource->read(&legacyByte, 1u);
            m_regionWidth = static_cast<float>(color88);
            int legacyHeight = 0;
            resource->read(&legacyHeight, 4u);
            m_regionHeight = static_cast<float>(legacyHeight);
        }
        else
        {
            resource->read(&m_regionWidth, 4u);
            resource->read(&m_regionHeight, 4u);
        }
        resource->read(&m_persistedRegionState, 4u);
        core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        const auto resolveVid = [&vidTable](int nvid) -> VID*
        {
            if (nvid < 0 || nvid >= vidTable.count())
                return nullptr;
            return vidTable.slot(nvid);
        };
        int nvid = -1;
        resource->read(&nvid, 4u);
        m_savedRegionVid = resolveVid(nvid);
        if (!m_savedRegionVid)
            m_savedRegionVid = EmptyVid;
        for (int i = 0; i < 6; ++i)
        {
            resource->read(&nvid, 4u);
            m_sourceVidMap[i] = resolveVid(nvid);
            resource->read(&nvid, 4u);
            m_targetVidMap[i] = resolveVid(nvid);
            if (m_sourceVidMap[i])
            {
                m_sourceVidMap[i]->setRuntimeAuxFlags(
                    m_sourceVidMap[i]->runtimeAuxFlags() | 0x10u);
            }
        }
        return 0;
    }


}
