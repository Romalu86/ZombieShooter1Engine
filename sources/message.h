#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class PLAYER_ARCADE;
    class SPRITE;
    class STRING;


    struct MESSAGE_STACK
    {
        DWORD text;
        float x;
        float y;
        DWORD time;
    };


    struct MESSAGE_STACK_LIST
    {
        DWORD vtable;
        int count;
        int capacity;
        DWORD entries;

        static DWORD CurrentImageVtable() noexcept;
        MESSAGE_STACK_LIST* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        void clear() noexcept;
    };


    class MESSAGE
    {
    public:
        static constexpr std::size_t ObjectSize = 0x304u;

        static DWORD CurrentImageVtable() noexcept;


        MESSAGE* initialize(int textVid, int markerVid,
                            DWORD baseXBits, DWORD baseYBits,
                            int lineCount, DWORD updateInterval) noexcept;


        ~MESSAGE() noexcept;

        MESSAGE* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;


        void DeletePointerToSprite(SPRITE* sprite) noexcept;
        void Tact() noexcept;
        int Put(STRING* text, float targetX, float targetY) noexcept;
        void Shift() noexcept;
        int font_height() const noexcept;

    private:
        friend class PLAYER_ARCADE;

        bool validateText(const STRING* text, int* fontHeightOut = nullptr) noexcept;
        int enqueueSound() noexcept;

        DWORD vtable;
        int lineCount;
        DWORD updateInterval;
        int textVid;
        int markerVid;
        float baseX;
        float baseY;
        int rowDirection;
        DWORD lastUpdate;
        DWORD textSprites[45];
        DWORD markerSprites[45];
        float targetX[45];
        float targetY[45];
        MESSAGE_STACK_LIST stack;
    };


}
