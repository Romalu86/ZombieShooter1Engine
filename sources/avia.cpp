#include "avia.h"
#include "map.h"
#include "core/application.h"
#include "graphics/angle.h"
#include "zs1/zScriptExec_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>

namespace as1
{
    AVIA::AVIA(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        m_flightState8C = 0;
        StartMove();
        setZSpeedDirect(Vid()->maximumZSpeed());
    }


    ANGLE AVIA::RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept
    {


        const unsigned char before = static_cast<unsigned char>(directionIndex());
        const ANGLE remaining = SPRITE::RotateTact(value, deltaMs);
        const unsigned char after = static_cast<unsigned char>(directionIndex());
        const unsigned int rawDelta = static_cast<unsigned char>(after - before);

        const bool positiveTurn = rawDelta > 0u && rawDelta < 0x40u;
        const bool negativeTurn = rawDelta > 0xC0u && rawDelta < 0x100u;
        const std::uint32_t elapsed = core::CurrentTimeMilliseconds() -
            static_cast<std::uint32_t>(m_flightState8C);

        const int animation = Animation();
        if (animation != 8)
        {
            int desiredAnimation = 2;
            if (negativeTurn)
                desiredAnimation = 4;
            else if (positiveTurn)
                desiredAnimation = 5;

            if (animation != desiredAnimation && elapsed >= 1000u)
            {
                ChangeAnimation(desiredAnimation);
                m_flightState8C = static_cast<int>(core::CurrentTimeMilliseconds());
            }
        }

        return remaining;
    }

    int AVIA::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {


        if (opcode == 0x82)
        {
            updateFlightBehavior(static_cast<int>(argument1Carrier), argument2Carrier);
            return 0;
        }
        if (opcode == 0x55)
            return TERRAIN::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
        return UNIT::Action(opcode, argument1Carrier, argument2Carrier, argument3Carrier);
    }

    void AVIA::updateFlightIdleBehavior() noexcept
    {


        const int animation = Animation();
        if (animation == 4)
        {
            if ((std::rand() % 3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() - 0x20);
            return;
        }
        if (animation == 5)
        {
            if ((std::rand() % 3) == 0)
                ChangeAnimation(2);
            else
                ChangeDirection(directionIndex() + 0x20);
            return;
        }
        if (animation == 2)
        {
            if ((std::rand() % 11) == 0)
                ChangeAnimation((std::rand() % 2) == 0 ? 5 : 4);
            return;
        }
        ChangeAnimation(2);
    }

    void AVIA::updateFlightBehavior(int behaviorArgument1, int behaviorArgument2) noexcept
    {


        (void)behaviorArgument1;
        (void)behaviorArgument2;

        int animation = Animation();
        if (animation >= 0x0F || animation == 0x0C)
            return;

        if (animation >= 7 && animation != 0x0A)
            ChangeAnimation(0);

        if (childBacklink())
            return;

        animation = Animation();
        const std::uint32_t frameDelta =
            as1::core::CurrentTimeMilliseconds() - as1::core::PreviousWorldTimeMilliseconds();
        const std::uint32_t frameSpeed =
            static_cast<std::uint32_t>(Vid()->frameSpeed[animation]);
        const std::uint32_t deltaMs = std::max(frameDelta, frameSpeed);
        (void)AttackTact(deltaMs);

        if ((runtimeFlags() & 0x00000080u) == 0u)
            setRuntimeFlags(runtimeFlags() | 0x00000080u);


        if (zs1::script_engine::GetToggleFlag() != 0u)
            DrawRelationDebugOverlay();
    }

void AVIA::updateFlightSlot10() noexcept
    {

    }

}
