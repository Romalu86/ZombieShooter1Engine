#include "sprite.h"
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
#include "menu.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>

namespace as1
{

    TERRAIN::TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent)
    {

    }

    int TERRAIN::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;


        if (opcode == static_cast<int>(ActionCode::ACT_REPAIR))
        {
            ChangeHp(Vid()->GetMaxHp(armyIndex()));
            return 0;
        }

        if (opcode == SpriteActConst::ACT_RESTORE_OLD_MAP)
        {
            SPRITE::Action(opcode, argument1Carrier, argument2, argument3);
            int localArgC = argument3;
            BaseStream* const stream = reinterpret_cast<BaseStream*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            stream->read(&localArgC, static_cast<unsigned>((argument2 > 7) + 1));
            return 0;
        }

        return SPRITE::Action(opcode, argument1Carrier, argument2, argument3);
    }

}
