#pragma once

#include "player.h"
#include "message.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;


    class PLAYER_ARCADE final : public PLAYER
    {
    public:
        static constexpr std::size_t ObjectSize = 0x330u;
        static DWORD CurrentImageVtable() noexcept;

        PLAYER_ARCADE(int controlMode, int playerSlot) noexcept;
        PLAYER_ARCADE* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        std::intptr_t DeletePointerToSprite(SPRITE* sprite) noexcept;
        void processInput(input::InputMessageState* inputState) noexcept;


        int PutMessage(STRING* text, float x, float y) noexcept;


        DWORD SetCleverEnemyAttack(int value) noexcept;


    private:
        MESSAGE m_message;
    };


}
