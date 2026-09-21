
#include "mouse.h"

#include "core/log.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "sprite_collector.h"

#include <algorithm>
#include <new>
#include <cstring>
#include <string>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1
{
    namespace
    {
        constexpr const char* kCursorDirectory = "cursores\\";
        constexpr const char* kCursorExtension = ".ani";

        constexpr const char* kCursorNames[MOUSE::CursorCount] = {
            "arrow",
            "noammo",
            "move",
            "clash",
            "repair",
            "attack",
            "farattack",
            "select",
            "nomove",
            "cycle",
            "link",
            "unlink",
            "cantmove",
            "patrol",
            "delete",
            "capture",
            "mine",
            "unmine",
            "small-arrow",
            "small-noammo",
            "small-move",
            "small-taran",
            "small-repair",
            "small-attack",
            "small-farattack",
            "small-select",
            "small-nomove",
            "cycle",
            "small-link",
            "small-unlink",
            "small-cantmove",
            "small-patrol",
            "delete",
            "small-capture",
            "small-mine",
            "small-unmine",
        };


        HCURSOR asCursor(void* handle) noexcept
        {
            return static_cast<HCURSOR>(handle);
        }

        void* fromCursor(HCURSOR handle) noexcept
        {
            return static_cast<void*>(handle);
        }
    }

    MOUSE* Mouse = nullptr;

    MOUSE::MOUSE()
        : MOUSE(EmptyVid, 0.0f, 0.0f, 0.0f, 0, nullptr)
    {
    }


    MOUSE::MOUSE(VID* vid, float x, float y, float z, int direction, SPRITE* parent)
        : SPRITE(nullptr, vid, VECTOR{x, y, z}, ANGLE(direction), parent)
    {

        setCursorHandlesLoaded(0);
        m_cursorHandles.fill(nullptr);
        setHardwareCursorEnabled(1);

        if (Vid() != EmptyVid || childChain() != nullptr)
            Remove();

        if (Vid() == EmptyVid)
            setListReferenceCount(listReferenceCount() + 1);
    }


    MOUSE::~MOUSE()
    {


        LOG::Write("Mouse  release");
    }


    void MOUSE::Enable()
    {

        if (m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 1;
        ::SetCursor(nullptr);
        if (!m_hardwareCursorEnabled)
            return;

        for (std::size_t i = 0; i < m_cursorHandles.size(); ++i)
        {
            if (m_cursorHandles[i])
            {
                ::DestroyCursor(asCursor(m_cursorHandles[i]));
                m_cursorHandles[i] = nullptr;
            }

            const char* name = kCursorNames[i];
            if (name && name[0])
            {
                std::string fileName;
                fileName.reserve(std::strlen(kCursorDirectory) + std::strlen(name) + std::strlen(kCursorExtension) + 1);
                fileName += kCursorDirectory;
                fileName += name;
                fileName += kCursorExtension;
                m_cursorHandles[i] = fromCursor(::LoadCursorFromFileA(fileName.c_str()));
            }

            if (!m_cursorHandles[i])
                m_cursorHandles[i] = fromCursor(::LoadCursorA(nullptr, IDC_ARROW));
        }

        const int cursorId = currentCursorId();


        ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(cursorId)]));
    }


    void MOUSE::Disable()
    {

        if (!m_cursorHandlesLoaded)
            return;

        m_cursorHandlesLoaded = 0;
        if (!m_hardwareCursorEnabled)
            return;

        ::SetCursor(nullptr);
        for (void*& slot : m_cursorHandles)
        {
            if (slot)
            {
                ::DestroyCursor(asCursor(slot));
                slot = nullptr;
            }
        }
    }


    void MOUSE::Draw()
    {


        if (m_hardwareCursorEnabled != 0 || m_cursorHandlesLoaded == 0)
            return;

        Vid()->Draw(this);
    }

    int MOUSE::Action(int opcode, std::intptr_t rawVar1, int rawVar2, int rawVar3)
    {
        if (opcode == 0x3D)
        {
            setCursorId(static_cast<int>(rawVar1));
            return 0;
        }

        if (opcode == 0x3F)
        {
            RECT rect;


            auto* const graphBytes = reinterpret_cast<std::uint8_t*>(Graph);
            const HWND hwnd = *reinterpret_cast<HWND*>(graphBytes + 0x0E04u);
            ::GetWindowRect(hwnd, &rect);
            ::SetCursorPos(rect.left + static_cast<int>(rawVar1), rect.top + rawVar2);
            (void)rawVar3;
            return 0;
        }

        if (opcode != 0x3E)
            return SPRITE::Action(opcode, rawVar1, rawVar2, rawVar3);

        const int vidIndex = static_cast<int>(rawVar1);
        auto* const applicationOwner =
            static_cast<std::uint8_t*>(core::ApplicationOwner());
        const int vidCount = *reinterpret_cast<int*>(
            applicationOwner + core::application_layout::VidCount);
        if (vidIndex < 0 || vidIndex >= vidCount)
            return 0;
        VID* const replacement = *reinterpret_cast<VID**>(
            applicationOwner + core::application_layout::VidTable +
            static_cast<std::size_t>(vidIndex) * sizeof(VID*));
        if (!replacement)
            return 0;

        VID* const current = Vid();
        if (current && current->nvid() == vidIndex)
            return 0;

        Insert();
        (void)SPRITE::Action(static_cast<int>(ActionCode::ACT_CHANGE_VID), static_cast<std::intptr_t>(vidIndex), rawVar2, rawVar3);
        for (SPRITE* node = this; node; node = node->childChain())
            (void)DeleteSpriteFromCollectorForActionSwitch(node);
        Remove();
        return 0;
    }


    void MOUSE::HardwareOn()
    {
        if (m_hardwareCursorEnabled != 0)
            return;
        Disable();
        setVidPointerDirect(EmptyVid);
        m_hardwareCursorEnabled = 1;
        Enable();
    }


    void MOUSE::HardwareOff()
    {
        if (m_hardwareCursorEnabled == 0)
            return;
        Disable();
        VID* softwareVid = EmptyVid;
        auto* const applicationOwner =
            static_cast<std::uint8_t*>(core::ApplicationOwner());
        if (*reinterpret_cast<int*>(applicationOwner + core::application_layout::VidCount) > 1)
        {
            VID* const slot1 = *reinterpret_cast<VID**>(
                applicationOwner + core::application_layout::VidTable + sizeof(VID*));
            if (slot1)
                softwareVid = slot1;
        }
        const int savedAnimation = currentCursorId();
        setVidPointerDirect(softwareVid);
        SPRITE::ChangeAnimation(savedAnimation == 0 ? 1 : 0);
        SPRITE::ChangeAnimation(savedAnimation);
        m_hardwareCursorEnabled = 0;
        Enable();
    }


    void MOUSE::ChangeAnimation(int cursorId)
    {
        if (m_hardwareCursorEnabled)
        {
            if (currentCursorId() != cursorId && m_cursorHandlesLoaded)
                ::SetCursor(asCursor(m_cursorHandles[static_cast<std::size_t>(cursorId)]));
            if (cursorId < AnimatedCursorCount)
            {
                SPRITE::ChangeAnimation(cursorId);
                return;
            }
            setCurrentCursorIdDirect(cursorId);
            return;
        }
        SPRITE::ChangeAnimation(cursorId);
    }

}
