#include "building.h"
#include "map.h"
#include "sprite_collector.h"
#include "vid/vid.h"

#include <cstdlib>
#include <xmmintrin.h>
#include <emmintrin.h>

namespace as1
{
    BUILDING::BUILDING(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {


        m_serviceActivityFlag = 0;
        m_pendingBuildVidHandle = 0;
        m_lastServiceTargetHandle = 0;
    }

    int BUILDING::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {

        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        switch (opcode)
        {
        case 0x49:
            ActionStack()->append(buildCommandRecord(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3));
            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            return 0;

        case 0x46:
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u || m_pendingBuildVidHandle == 0)
                return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            ActionStack()->append(buildCommandRecord(0x23u,
                                  static_cast<VID*>(reinterpret_cast<void*>(m_pendingBuildVidHandle))->nvid(),
                                  0,
                                  0));
            SetCommand(0, nullptr);
            return 0;

        case 0x23:
        {
            int buildNvid = argument1;
            if (buildNvid == 0)
                buildNvid = dispatchVirtualAction(0x3Bu, 4, 0, 0);
            if (buildNvid > 0 && (runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return 0;
            MAP* const map = mapOwner();
            if (!map->ValidateVid(buildNvid))
                return 0;
            VID* const buildVid = map->Vid(buildNvid);
            m_pendingBuildVidHandle = reinterpret_cast<std::uintptr_t>(buildVid);
            m_buildPositionX = argument2;
            m_buildPositionY = argument3;
            setActionTimer(static_cast<std::uint32_t>(buildVid->weaponBuildTime()));
            SetCommand(0x10, nullptr);
            if (Animation() < 15)
                ChangeAnimation(1);
            return 0;
        }

        case 0x82:
        {
            if (Animation() >= 0x0F)
                return 0;
            if ((runtimeFlags() & SPRITE::CommandBitsMask) == 0x40u)
                ChangeAnimation(1);
            else if (Animation() != 10)
                ChangeAnimation(0);

            VID* const buildVid = static_cast<VID*>(reinterpret_cast<void*>(m_pendingBuildVidHandle));
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u ||
                actionTimer() != 0u || !buildVid)
                return 0;

            int nextAnimation = 1;
            float buildX = static_cast<float>(m_buildPositionX);
            float buildY = static_cast<float>(m_buildPositionY);
            if (m_buildPositionX == 0 && m_buildPositionY == 0)
            {
                buildX = X();
                buildY = Y();
            }
            if (m_buildPositionX < 0)
            {
                const int divisor = 1 - m_buildPositionX;
                const int doubledRemainder = 2 * (std::rand() % divisor);
                __m128 value = _mm_sub_ss(
                    _mm_set_ss(X()),
                    _mm_cvtepi32_ps(_mm_cvtsi32_si128(m_buildPositionX)));
                value = _mm_sub_ss(
                    value,
                    _mm_cvtepi32_ps(_mm_cvtsi32_si128(doubledRemainder)));
                buildX = _mm_cvtss_f32(value);
            }
            if (m_buildPositionY < 0)
            {
                const int divisor = 1 - m_buildPositionY;
                nextAnimation = divisor;
                const int doubledRemainder = 2 * (std::rand() % divisor);
                __m128 value = _mm_sub_ss(
                    _mm_set_ss(Y()),
                    _mm_cvtepi32_ps(_mm_cvtsi32_si128(m_buildPositionY)));
                value = _mm_sub_ss(
                    value,
                    _mm_cvtepi32_ps(_mm_cvtsi32_si128(doubledRemainder)));
                buildY = _mm_cvtss_f32(value);
            }

            if (GlobalSpriteCollectorCanPlace(*mapOwner(), buildVid, buildX, buildY, Z()) != nullptr)
                return 0;

            SetCommand(0, nullptr);
            SPRITE* const created = mapOwner()->CreateSprite(
                buildVid, VECTOR{buildX, buildY, Z()}, Direction(), this, false);
            if (created)
            {
                (void)dispatchVirtualAction(0x4Bu,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(created) & 0xFFFFFFFFu), 0, 0);
            }
            m_pendingBuildVidHandle = 0;
            ChangeAnimation(nextAnimation);
            return 0;
        }

        case 0x50:
            if (!hasCommandOpcode(0x49u))
                ActionStack()->append(buildCommandRecord(0x49u, 0, 0, 0));
            (void)UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            return 0;

        default:
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
    }

void BUILDING::MoveTact()
    {

    }

}
