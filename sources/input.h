#pragma once

#include "core/base_stream.h"
#include "core/types.h"
#include <cstdint>
#include <cstddef>

namespace as1 { namespace input
{
    struct InputMessageState
    {
        std::uint32_t flags = 0;
        int wheelDelta = 0;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float clientX = 0.0f;
        float clientY = 0.0f;
        std::uint32_t lastCode = 0;
        std::uint32_t rawKeyCode = 0;

        InputMessageState* initializePreservingPersistentFlags() noexcept;
        void resetFrameState();
        std::uint32_t clearLeftButtonState();
        std::uint32_t clearRightButtonState();


        int handleWindowMessage(std::uintptr_t hwnd, std::uint32_t message,
                       std::uint32_t wParam, std::uint32_t lParam);

        int writeRawState(BaseStream* stream) const;
        int readRawState(BaseStream* stream);
    };


    struct InputControlKeys
    {

        std::uint32_t left0;
        std::uint32_t left1;
        std::uint32_t right0;
        std::uint32_t right1;
        std::uint32_t up0;
        std::uint32_t up1;
        std::uint32_t down0;
        std::uint32_t down1;
        std::uint32_t first0;
        std::uint32_t first1;
        std::uint32_t second0;
        std::uint32_t second1;


        std::uint32_t previousWeapon;
        std::uint32_t nextWeapon;
        std::uint32_t firstMouseReleaseClears;
        std::uint32_t secondMouseReleaseClears;
    };


    struct InputWindowGlobals
    {
        int windowPositionX;
        int windowPositionY;
    };


    extern InputControlKeys g_inputControlKeys;
    extern std::uint32_t g_relativeControlEnabled;
    extern InputWindowGlobals g_inputWindowGlobals;

} }
