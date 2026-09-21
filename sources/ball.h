#pragma once

#include "sprite.h"

namespace as1
{


    class BALL : public SPRITE
    {
    public:

        BALL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~BALL() override = default;
        void MoveTact() override;

    private:
        __declspec(noinline)
        float flagmanSpan() const noexcept;

        int m_ballState;
        float m_ballLastX;
        float m_ballLastY;
        std::uint32_t m_ballUnderTime;
        int m_ballImpactCount;
    };

}
