#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class RESOURCE;
    class SPRITE;
    class STRING;
    namespace input { struct InputMessageState; }


    class PLAYER
    {
    public:
        static constexpr std::size_t ObjectSize = 0x2Cu;
        static DWORD CurrentImageVtable() noexcept;

        PLAYER(int controlMode, int playerSlot) noexcept;
        ~PLAYER() noexcept;

        PLAYER* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        std::intptr_t DeletePointerToSprite(SPRITE* sprite) noexcept;
        void reset() noexcept;
        void SetFlagman(SPRITE* sprite) noexcept;

        int saveControlledSpriteReference(RESOURCE* mapResource) noexcept;
        int loadControlledSpriteReference(RESOURCE* mapResource) noexcept;
        STRING* getAuxiliaryUnitName(STRING* out) noexcept;


        void PutMessage(STRING* text, float x, float y) noexcept;


        void processInput(input::InputMessageState*) noexcept {}

        __forceinline int controlMode() const noexcept { return m_state.controlMode; }
        __forceinline int playerSlot() const noexcept { return m_state.playerSlot; }

        SPRITE* controlledSprite() const noexcept;
        __forceinline void setControlledSprite(SPRITE* value) noexcept
        {
            m_state.controlledSprite = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(value));
        }

        __forceinline SPRITE* auxiliarySprite() const noexcept
        {
            return reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(m_state.auxiliarySprite));
        }

        __forceinline void setAuxiliarySprite(SPRITE* value) noexcept
        {
            m_state.auxiliarySprite = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(value));
        }

        DWORD money() const noexcept { return m_state.money; }
        DWORD getMoney() const noexcept { return m_state.money; }

        void SetMoney(int value) noexcept;
        void setMoney(DWORD value) noexcept { SetMoney(static_cast<int>(value)); }

    protected:
        struct BaseOwnerLayout
        {
            DWORD vtable;
            DWORD money;
            int controlMode;
            int playerSlot;
            DWORD controlledSprite;
            DWORD listVtable;
            int listCount;
            int listCapacity;
            DWORD listItems;
            DWORD auxiliarySprite;
            BYTE state28;
            BYTE reserved29[3];
        };


        BaseOwnerLayout m_state;
    };


}
