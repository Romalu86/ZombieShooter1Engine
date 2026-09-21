#pragma once

#include "unit.h"

namespace as1
{

    class MAN final : public UNIT
    {
    public:
        MAN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~MAN() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;

        int ChangeWeapon(int weapon) noexcept;

    private:
        friend class SPRITE;
        int weaponAmmo[10];
    };

}
