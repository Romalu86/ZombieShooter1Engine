#pragma once

#include "sprite.h"
#include "map.h"
#include "core/application.h"
#include "core/log.h"

namespace as1
{
    class BUILDED_TERRAIN : public SPRITE
    {
    public:


        BUILDED_TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr)
            : SPRITE(owner, vid, xyz, dir, parent)
        {
            MAP* const map = mapOwner();
            if (!map)
                return;

            VID* ground = map->Vid(1024);
            if (ground == EmptyVid)
            {
                map->CreateEmptyHardwareGround();
                ground = map->Vid(1024);
            }

            if (ground != EmptyVid && ground->directionCount() == 1)
            {
                ground->AddVidToVid(this);

                if ((Vid()->properties() & 0x00000040u) == 0u)
                    ChangeAnimation(15);
            }
        }

        void drawBuiltTerrain()
        {

            MAP* const map = mapOwner();
            VID* const ground = map ? map->Vid(1024) : EmptyVid;
            if (ground == EmptyVid || ground->directionCount() != 1)
                SPRITE::Draw();
        }

        void Draw() override { drawBuiltTerrain(); }
    };
}
