
#pragma once

#include "sprite.h"
#include <array>
#include <cstddef>

namespace as1
{
    class MOUSE : public SPRITE
    {
    public:
        static constexpr std::size_t CursorCount = 36u;
        static constexpr int AnimatedCursorCount = 17;

        MOUSE();
        MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent);
        virtual ~MOUSE();


        int Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3) override;
        void Draw() override;
        void Enable();
        void Disable();
        void HardwareOn();
        void HardwareOff();
        void ChangeAnimation(int animationId);

        void enableHardwareCursorMode() { HardwareOn(); }
        void disableHardwareCursorMode() { HardwareOff(); }
        void setCursorId(int cursorId) { ChangeAnimation(cursorId); }

        int currentCursorId() const noexcept { return Animation(); }
        void setCurrentCursorIdDirect(int value) noexcept { setCurrentAnimationDirect(value); }
        int hardwareCursorEnabled() const noexcept { return m_hardwareCursorEnabled; }
        void setHardwareCursorEnabled(int value) noexcept { m_hardwareCursorEnabled = value; }
        int cursorHandlesLoaded() const noexcept { return m_cursorHandlesLoaded; }
        void setCursorHandlesLoaded(int value) noexcept { m_cursorHandlesLoaded = value; }

    private:


        int m_hardwareCursorEnabled;
        std::array<void*, CursorCount> m_cursorHandles;
        int m_cursorHandlesLoaded;
    };


    extern MOUSE* Mouse;
    inline MOUSE*& mouseInstanceRef() noexcept { return Mouse; }
    inline SPRITE* mouseSprite() noexcept { return static_cast<SPRITE*>(Mouse); }

}
