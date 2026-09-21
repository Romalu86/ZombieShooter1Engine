#pragma once

#include "sprite.h"
#include "core/weak_controller.h"

namespace as1
{
    class RAIL : public TERRAIN
    {
    public:
        RAIL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~RAIL() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void handleRailNodeReleased(std::uintptr_t ownerHandle) noexcept;
        core::R_DOT* firstRailNode() const noexcept { return m_firstRailNode; }
        core::R_DOT* secondRailNode() const noexcept { return m_secondRailNode; }

    private:

        core::R_DOT* m_firstRailNode = nullptr;
        core::R_DOT* m_secondRailNode = nullptr;
    };
}
