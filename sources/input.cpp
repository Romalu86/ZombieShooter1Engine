#include "input.h"
#include "mouse.h"
#include "graph.h"
#include "core/application.h"
#include <algorithm>
#include <windows.h>

namespace as1 { namespace input
{
    namespace
    {
        constexpr std::uint32_t WM_MOVE_MESSAGE = 0x0003;
        constexpr std::uint32_t WM_ACTIVATEAPP_MESSAGE = 0x001C;
        constexpr std::uint32_t WM_NCHITTEST_MESSAGE = 0x0084;
        constexpr std::uint32_t WM_KEYDOWN_MESSAGE = 0x0100;
        constexpr std::uint32_t WM_KEYUP_MESSAGE = 0x0101;
        constexpr std::uint32_t WM_CHAR_MESSAGE = 0x0102;
        constexpr std::uint32_t WM_SYSKEYDOWN_MESSAGE = 0x0104;
        constexpr std::uint32_t WM_SYSKEYUP_MESSAGE = 0x0105;
        constexpr std::uint32_t WM_INITMENU_MESSAGE = 0x0116;
        constexpr std::uint32_t WM_LBUTTONDOWN_MESSAGE = 0x0201;
        constexpr std::uint32_t WM_LBUTTONUP_MESSAGE = 0x0202;
        constexpr std::uint32_t WM_RBUTTONDOWN_MESSAGE = 0x0204;
        constexpr std::uint32_t WM_RBUTTONUP_MESSAGE = 0x0205;
        constexpr std::uint32_t WM_MBUTTONDOWN_MESSAGE = 0x0207;
        constexpr std::uint32_t WM_MOUSEWHEEL_MESSAGE = 0x020A;

        constexpr std::uint32_t VK_SHIFT_KEY = 0x10;
        constexpr std::uint32_t VK_CONTROL_KEY = 0x11;
        constexpr std::uint32_t VK_MENU_KEY = 0x12;

        constexpr std::uint32_t FLAG_LEFT_KEY = 0x00000080;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_EVENT = 0x00000001;
        constexpr std::uint32_t FLAG_MIDDLE_BUTTON_EVENT = 0x00000002;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_EVENT = 0x00000004;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_RELEASE = 0x00000008;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_RELEASE = 0x00000010;
        constexpr std::uint32_t FLAG_LEFT_BUTTON_DOWN = 0x00000020;
        constexpr std::uint32_t FLAG_RIGHT_BUTTON_DOWN = 0x00000040;

        constexpr std::uint32_t FLAG_RIGHT_KEY = 0x00000100;
        constexpr std::uint32_t FLAG_DOWN_KEY = 0x00000200;
        constexpr std::uint32_t FLAG_UP_KEY = 0x00000400;
        constexpr std::uint32_t FLAG_SHIFT_KEY = 0x00000800;
        constexpr std::uint32_t FLAG_CONTROL_KEY = 0x00001000;
        constexpr std::uint32_t FLAG_ALT_KEY = 0x00002000;
        constexpr std::uint32_t FLAG_FIRST_ACTION = 0x00004000;
        constexpr std::uint32_t FLAG_SECOND_ACTION = 0x00008000;

        constexpr std::uint32_t RESET_KEEP_MASK = 0xFFFFFFE0u;


        constexpr std::uint32_t INITIALIZE_KEEP_MASK = 0xFFFF0000u;
        constexpr std::uint32_t DEACTIVATE_KEEP_MASK = 0xFFFFC7FFu;


    }


    InputControlKeys g_inputControlKeys = {
        0x25u, 0x25u, 0x27u, 0x27u,
        0x26u, 0x26u, 0x28u, 0x28u,
        1u, 1u, 2u, 2u, 0x5Bu, 0x5Du, 1u, 1u
    };
    std::uint32_t g_relativeControlEnabled = 0u;
    InputWindowGlobals g_inputWindowGlobals = {0, 0};


    InputMessageState* InputMessageState::initializePreservingPersistentFlags() noexcept
    {

        flags &= INITIALIZE_KEEP_MASK;
        wheelDelta = 0;
        clientX = 0.0f;
        worldX = 0.0f;
        clientY = 0.0f;
        worldY = 0.0f;
        lastCode = 0;
        rawKeyCode = 0;
        return this;
    }


    void InputMessageState::resetFrameState()
    {

        flags &= RESET_KEEP_MASK;
        lastCode = 0;
        rawKeyCode = 0;
        wheelDelta = 0;
        const InputControlKeys& keys = g_inputControlKeys;
        if (keys.firstMouseReleaseClears == 0)
            flags &= ~FLAG_FIRST_ACTION;
        if (keys.secondMouseReleaseClears == 0)
            flags &= ~FLAG_SECOND_ACTION;
    }


    std::uint32_t InputMessageState::clearLeftButtonState()
    {

        std::uint32_t eax = flags;
        eax &= ~FLAG_LEFT_BUTTON_EVENT;
        flags = eax;
        const InputControlKeys& keys = g_inputControlKeys;
        if (keys.first0 == 1)
        {
            eax &= ~FLAG_FIRST_ACTION;
            flags = eax;
        }
        if (keys.second0 == 1)
        {
            eax = flags;
            eax &= ~FLAG_SECOND_ACTION;
            flags = eax;
        }
        return eax;
    }


    std::uint32_t InputMessageState::clearRightButtonState()
    {

        std::uint32_t eax = flags;
        eax &= ~FLAG_RIGHT_BUTTON_EVENT;
        flags = eax;
        const InputControlKeys& keys = g_inputControlKeys;
        if (keys.first0 == 2)
        {
            eax &= ~FLAG_FIRST_ACTION;
            flags = eax;
        }
        if (keys.second0 == 2)
        {
            eax = flags;
            eax &= ~FLAG_SECOND_ACTION;
            flags = eax;
        }
        return eax;
    }


    int InputMessageState::writeRawState(BaseStream* stream) const
    {
        return stream->write(this, 0x20u);
    }


    int InputMessageState::readRawState(BaseStream* stream)
    {
        return stream->read(this, 0x20u);
    }


    int InputMessageState::handleWindowMessage(std::uintptr_t hwnd,
                                               std::uint32_t message,
                                               std::uint32_t wParam,
                                               std::uint32_t lParam)
    {
        switch (message)
        {
        case WM_NCHITTEST_MESSAGE:
        {
            int rectLeft = 0;
            int rectTop = 0;
            RECT rect;

            ::GetWindowRect(reinterpret_cast<HWND>(hwnd), &rect);
            rectLeft = rect.left;
            rectTop = rect.top;
            g_inputWindowGlobals.windowPositionX = rectLeft;
            g_inputWindowGlobals.windowPositionY = rectTop;


            const int screenX = static_cast<int>(lParam & 0xFFFFu);
            const int screenY = static_cast<int>((lParam >> 16) & 0xFFFFu);
            const float clientXValue = static_cast<float>(screenX - rectLeft);
            const float clientYValue = static_cast<float>(screenY - rectTop);
            clientX = clientXValue;
            clientY = clientYValue;

            GRAPH* const graph = Graph;
            const float viewportMinX = graph->viewportLeft();
            const float viewportMaxX = graph->viewportRight();
            const float viewportMinY = graph->viewportTop();
            const float viewportMaxY = graph->viewportBottom();

            float clampedX = clientXValue;
            float clampedY = clientYValue;
            if (clampedX < viewportMinX)
                clampedX = viewportMinX;
            if (clampedX >= viewportMaxX)
                clampedX = viewportMaxX - 1.0f;
            if (clampedY < viewportMinY)
                clampedY = viewportMinY;
            if (clampedY >= viewportMaxY)
                clampedY = viewportMaxY - 1.0f;

            const auto* const applicationOwner =
                static_cast<const std::uint8_t*>(core::ApplicationOwner());
            worldX = clampedX + *reinterpret_cast<const float*>(
                applicationOwner + core::application_layout::CameraShiftX);
            worldY = clampedY + *reinterpret_cast<const float*>(
                applicationOwner + core::application_layout::CameraShiftY);

            if (Mouse)
                Mouse->ChangeCoor(worldX, worldY, Mouse->Z());

            return clientXValue >= viewportMinX && clientXValue < viewportMaxX &&
                   clientYValue >= viewportMinY && clientYValue < viewportMaxY ? 1 : 0;
        }

        case WM_ACTIVATEAPP_MESSAGE:
            if (wParam != 0)
                return 0;
            flags &= DEACTIVATE_KEEP_MASK;
            lastCode = 0;
            return 0;

        case WM_INITMENU_MESSAGE:
            flags &= ~(FLAG_SHIFT_KEY | FLAG_CONTROL_KEY | FLAG_ALT_KEY);
            lastCode = 0;
            return 0;

        case WM_MOVE_MESSAGE:
        {
            RECT rect;
            ::GetWindowRect(reinterpret_cast<HWND>(hwnd), &rect);
            g_inputWindowGlobals.windowPositionX = rect.left;
            g_inputWindowGlobals.windowPositionY = rect.top;
            return 0;
        }

        case WM_KEYDOWN_MESSAGE:
            lastCode = wParam << 8;
            rawKeyCode = wParam;
            if (wParam == g_inputControlKeys.left0 || wParam == g_inputControlKeys.left1)
                flags |= FLAG_LEFT_KEY;
            else if (wParam == g_inputControlKeys.right0 || wParam == g_inputControlKeys.right1)
                flags |= FLAG_RIGHT_KEY;
            else if (wParam == g_inputControlKeys.up0 || wParam == g_inputControlKeys.up1)
                flags |= FLAG_UP_KEY;
            else if (wParam == g_inputControlKeys.down0 || wParam == g_inputControlKeys.down1)
                flags |= FLAG_DOWN_KEY;
            else if (wParam == g_inputControlKeys.first0 || wParam == g_inputControlKeys.first1)
                flags |= FLAG_FIRST_ACTION;
            else if (wParam == g_inputControlKeys.second0 || wParam == g_inputControlKeys.second1)
                flags |= FLAG_SECOND_ACTION;
            else if (wParam == VK_SHIFT_KEY)
                flags |= FLAG_SHIFT_KEY;
            else if (wParam == VK_CONTROL_KEY)
                flags |= FLAG_CONTROL_KEY;
            return 0;

        case WM_KEYUP_MESSAGE:
            if (wParam == g_inputControlKeys.left0 || wParam == g_inputControlKeys.left1)
                flags &= ~FLAG_LEFT_KEY;
            else if (wParam == g_inputControlKeys.right0 || wParam == g_inputControlKeys.right1)
                flags &= ~FLAG_RIGHT_KEY;
            else if (wParam == g_inputControlKeys.up0 || wParam == g_inputControlKeys.up1)
                flags &= ~FLAG_UP_KEY;
            else if (wParam == g_inputControlKeys.down0 || wParam == g_inputControlKeys.down1)
                flags &= ~FLAG_DOWN_KEY;
            else if (wParam == g_inputControlKeys.first0 || wParam == g_inputControlKeys.first1)
                flags &= ~FLAG_FIRST_ACTION;
            else if (wParam == g_inputControlKeys.second0 || wParam == g_inputControlKeys.second1)
                flags &= ~FLAG_SECOND_ACTION;
            else if (wParam == VK_SHIFT_KEY)
                flags &= ~FLAG_SHIFT_KEY;
            else if (wParam == VK_CONTROL_KEY)
                flags &= ~FLAG_CONTROL_KEY;
            return 0;

        case WM_SYSKEYDOWN_MESSAGE:
            if (wParam == VK_MENU_KEY)
                flags |= FLAG_ALT_KEY;
            return 0;

        case WM_SYSKEYUP_MESSAGE:
            if (wParam == VK_MENU_KEY)
                flags &= ~FLAG_ALT_KEY;
            return 0;

        case WM_CHAR_MESSAGE:
            lastCode = wParam & 0xFFu;
            return 0;

        case WM_LBUTTONDOWN_MESSAGE:
            flags |= FLAG_LEFT_BUTTON_EVENT | FLAG_LEFT_BUTTON_DOWN;
            if (g_inputControlKeys.first0 == 1)
                flags |= FLAG_FIRST_ACTION;
            if (g_inputControlKeys.second0 == 1)
                flags |= FLAG_SECOND_ACTION;
            return 0;

        case WM_LBUTTONUP_MESSAGE:
            flags &= ~FLAG_LEFT_BUTTON_DOWN;
            flags |= FLAG_LEFT_BUTTON_RELEASE;
            if (g_inputControlKeys.first0 == 1 && g_inputControlKeys.firstMouseReleaseClears)
                flags &= ~FLAG_FIRST_ACTION;
            if (g_inputControlKeys.second0 == 1 && g_inputControlKeys.secondMouseReleaseClears)
                flags &= ~FLAG_SECOND_ACTION;
            return 0;

        case WM_RBUTTONDOWN_MESSAGE:
            flags |= FLAG_RIGHT_BUTTON_EVENT | FLAG_RIGHT_BUTTON_DOWN;
            if (g_inputControlKeys.first0 == 2)
                flags |= FLAG_FIRST_ACTION;
            if (g_inputControlKeys.second0 == 2)
                flags |= FLAG_SECOND_ACTION;
            return 0;

        case WM_RBUTTONUP_MESSAGE:
            flags &= ~FLAG_RIGHT_BUTTON_DOWN;
            flags |= FLAG_RIGHT_BUTTON_RELEASE;
            if (g_inputControlKeys.first0 == 2 && g_inputControlKeys.firstMouseReleaseClears)
                flags &= ~FLAG_FIRST_ACTION;
            if (g_inputControlKeys.second0 == 2 && g_inputControlKeys.secondMouseReleaseClears)
                flags &= ~FLAG_SECOND_ACTION;
            return 0;

        case WM_MBUTTONDOWN_MESSAGE:
            flags |= FLAG_MIDDLE_BUTTON_EVENT;
            return 0;

        case WM_MOUSEWHEEL_MESSAGE:
            wheelDelta = static_cast<int>(static_cast<short>((wParam >> 16) & 0xFFFFu)) / 120;
            return 0;

        default:
            return 0;
        }
    }

} }
