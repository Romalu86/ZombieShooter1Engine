#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "base_sprite_list.h"
#include "input.h"
#include "player.h"
#include "core/application.h"
#include "core/configuration.h"

namespace as1
{
    class GRAPH;
    class MAP;
    class RESOURCE;
    class VID;
}

namespace as1 { namespace win
{

    class ApplicationBaseVtableOwner;
    class ApplicationDerivedVtableOwner;

    struct ApplicationWinInit
    {
        HINSTANCE hInstance = nullptr;
        HINSTANCE previousInstance = nullptr;
        const char* commandLine = nullptr;
        int showCmd = SW_SHOWDEFAULT;
        const char* shellString = "";
        std::uint32_t startupFlags = 0u;
    };

    class ApplicationWin
    {
        friend class ApplicationBaseVtableOwner;
        friend class ApplicationDerivedVtableOwner;
    public:
        explicit ApplicationWin(const ApplicationWinInit& init);
        virtual ~ApplicationWin();

        virtual bool dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result);

        ApplicationWin(const ApplicationWin&) = delete;
        ApplicationWin& operator=(const ApplicationWin&) = delete;

        virtual void transferFrom(SPRITE* sprite);


        virtual bool pumpOnce();
        __forceinline int pumpFrame() { return pumpOnce() ? 1 : 0; }

        virtual void drawShellOverlays();

        virtual void deinitialize();

        virtual void runCommandLine(char* ownedCommandLine);
        void runCommandLineMap(char* ownedCommandLine);

        ApplicationWin* initializeDerivedApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                                   as1::core::StartupSettingsBlock* startupSettings);
        ApplicationWin* initializeBaseApplicationStartup(HINSTANCE instance, HINSTANCE previousInstance, const char** commandLineOwner, int showCmd,
                       as1::core::StartupSettingsBlock* startupSettings);

        virtual void saveMap(const as1::STRING& outputName);

        virtual SPRITE* CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent);


        void destroyDerivedApplicationState();
        void destroyBaseApplicationState();

        __forceinline bool initialized() const noexcept
        {
            return (flags() & 0x00000004u) != 0u;
        }

        __forceinline HINSTANCE instance() const noexcept
        {
            return *reinterpret_cast<const HINSTANCE*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::InstanceHandle);
        }

        __forceinline HWND nativeWindow() const noexcept
        {
            return *reinterpret_cast<const HWND*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::MainWindow);
        }

        __forceinline HACCEL accelerator() const noexcept
        {
            return *reinterpret_cast<const HACCEL*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Accelerator);
        }

        __forceinline std::uint32_t flags() const noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Flags);
        }

        __forceinline void setFlags(std::uint32_t value) noexcept
        {


            *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(this) + as1::core::application_layout::Flags) = value;
        }

        __forceinline void setWorldStartTime(std::uint32_t value) noexcept
        {


            *reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(this) +
                as1::core::application_layout::WorldStartTime) = value;
        }

        void setPendingCommand(const as1::STRING& value);
        void setCurrentMapName(const as1::STRING& value);


        float mapExtentX() const noexcept;
        float mapExtentY() const noexcept;
        PLAYER* playerSlotByIndex(int index) const noexcept;

        __forceinline PLAYER* startupPlayerSlotByIndex(int index) const noexcept
        {
            return playerSlotByIndex(index);
        }

        __forceinline std::uint32_t activeStartupPlayerIndex() const noexcept
        {


            return *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(this) +
                as1::core::application_layout::ActivePlayerIndex);
        }


        as1::STRING* buildMouseTipText(as1::STRING* out);
    private:
        void drawApplicationDebugPass();
        bool pumpNativeMessages();
        int processDemoFrame();
        SPRITE* activeAuxiliarySprite() const noexcept;
        void updateCameraFromInput();

    };


    LRESULT CALLBACK applicationWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    __forceinline ApplicationWin* applicationWinInstance() noexcept
    {
        return static_cast<ApplicationWin*>(as1::core::ApplicationOwner());
    }
    ApplicationWin* CreateApplicationWin(const ApplicationWinInit& init);
    void DestroyApplicationWin(ApplicationWin* app);
} }
