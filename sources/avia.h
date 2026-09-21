#pragma once

#include "unit.h"

namespace as1
{
    class AVIA : public UNIT
    {
    public:
        AVIA(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;

        ANGLE RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept override;


        virtual void updateFlightBehavior(int behaviorArgument1 = 0, int behaviorArgument2 = 0) noexcept;
        virtual void updateFlightSlot10() noexcept;
        virtual void updateFlightIdleBehavior() noexcept;

    private:

        int m_flightState8C;
    };

}
