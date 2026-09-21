#pragma once

#include "avia.h"

namespace as1
{
    class BALLOON : public AVIA
    {
    public:
        BALLOON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~BALLOON() override = default;
        void MoveTact() override;


        void updateFlightSlot10() noexcept override;
        virtual void updateAltitudeAndAttachmentState() noexcept;
        virtual void updateBalloonCombatBehavior() noexcept;
        void acquireNearestAttachmentTarget() noexcept;
        void attachToGoalTarget() noexcept;
        int isAtGoalOrPathComplete() const noexcept;

        int SetCommand(int action, SPRITE* target) noexcept;

        std::uint32_t attachmentTransitionPending() const noexcept { return m_attachmentTransitionPending; }
        std::uint8_t attachmentPhase() const noexcept { return m_attachmentPhase; }
        void setAttachmentPhase(std::uint8_t value) noexcept { m_attachmentPhase = value; }

    private:

        std::uint32_t m_attachmentTransitionPending;
        std::uint8_t m_attachmentPhase;
    };

}
