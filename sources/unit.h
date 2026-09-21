#pragma once

#include "sprite.h"

namespace as1
{
    class UNIT : public TERRAIN
    {
    public:
        UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~UNIT() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DrawDebugOverlay() override;

    private:
        friend class MAN;
        int m_sharedPrimaryState;
        int m_sharedSecondaryState;
        int m_ammoFixedPoint;
        int m_legacyCommandState2;
        int m_turnTimer;
        int m_unitState84;
        int m_behaviorFlags;
    };

}
