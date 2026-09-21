#pragma once

#include "creature.h"

namespace as1
{
    class CIV_ROBOT : public CREATURE
    {
    public:
        CIV_ROBOT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~CIV_ROBOT() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        SPRITE* findNearbyEligibleTarget() noexcept;

        void ChangeAnimation(int animation) noexcept;

        void RotateHead(ANGLE direction) noexcept;


        std::uint32_t behaviorState() const noexcept { return m_behaviorState; }
        void setBehaviorState(std::uint32_t value) noexcept { m_behaviorState = value; }
        SPRITE* retainedTargetSprite() const noexcept { return m_retainedTargetSprite; }
        void setRetainedTargetSprite(SPRITE* value) noexcept { m_retainedTargetSprite = value; }

    private:
        std::uint32_t m_behaviorState;
        SPRITE* m_retainedTargetSprite;
        std::uint32_t m_damageReactionPending;
        std::uint32_t m_targetRefreshPending;
    };


}
