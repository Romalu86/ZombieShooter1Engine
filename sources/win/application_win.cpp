#include "win/application_win.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <mmsystem.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#include <new>
#include <cmath>
#include <cstring>
#include <xmmintrin.h>

#include "sprite.h"
#include "player_arcade.h"
#include "unit.h"
#include "rail.h"
#include "depo.h"
#include "building.h"
#include "avia.h"
#include "creature.h"
#include "civ_robot.h"
#include "engine.h"
#include "balloon.h"
#include "ball.h"
#include "map.h"
#include "menu.h"
#include "graph.h"
#include "graphics/base_texture.h"
#include "graphics/color.h"
#include "vid/vid.h"
#include "sprite_collector.h"
#include "mouse.h"
#include "core/log.h"
#include "core/application.h"
#include "core/weak_controller.h"
#include "core/profile_p.h"
#include "core/file_logger.h"
#include "core/configuration.h"
#include "constant.h"
#include "sound/sound_engine.h"
#include "win/resources/resource.h"
#include "win/main_sw.h"
#include "game/startup.h"
#include "zs1/zCommon.h"
#include "zs1/zDebugLog.h"
#include "zs1/zUserMngr.h"

namespace as1 { namespace win
{
    class ApplicationBaseVtableOwner
    {
    public:
        virtual ApplicationWin* deletingDestructor(unsigned char deleteSelfFlag) noexcept
        {
            ApplicationWin* const app = reinterpret_cast<ApplicationWin*>(this);
            app->destroyBaseApplicationState();
            if ((deleteSelfFlag & 1u) != 0u)
                ::operator delete(static_cast<void*>(app));
            return app;
        }

        virtual bool dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result)
        {
            return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::dispatchWindowMessage(
                hwnd, msg, wparam, lparam, result);
        }

        virtual void transferFrom(SPRITE* sprite)
        {
            reinterpret_cast<MAP*>(this)->DeletePointerToSprite(sprite);
        }

        virtual bool pumpOnce()
        {
            return false;
        }

        virtual void drawShellOverlays()
        {
            reinterpret_cast<ApplicationWin*>(this)->drawApplicationDebugPass();
        }

        virtual void deinitialize()
        {
            reinterpret_cast<MAP*>(this)->release();
        }

        virtual void runCommandLine(char* ownedCommandLine)
        {
            reinterpret_cast<ApplicationWin*>(this)->runCommandLineMap(ownedCommandLine);
        }

        virtual void saveMap(const as1::STRING& outputName)
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::saveMap(outputName);
        }

        virtual SPRITE* CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
        {
            return reinterpret_cast<as1::core::Application*>(this)->createSprite(
                vid, xyz, direction, parent);
        }

        static std::uintptr_t currentVtable() noexcept
        {
            static ApplicationBaseVtableOwner owner;
            return *reinterpret_cast<const std::uintptr_t*>(&owner);
        }
    };

    class ApplicationDerivedVtableOwner
    {
    public:
        virtual ApplicationWin* deletingDestructor(unsigned char deleteSelfFlag) noexcept
        {
            ApplicationWin* const app = reinterpret_cast<ApplicationWin*>(this);
            app->destroyDerivedApplicationState();
            if ((deleteSelfFlag & 1u) != 0u)
                ::operator delete(static_cast<void*>(app));
            return app;
        }

        virtual bool dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result)
        {
            return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::dispatchWindowMessage(
                hwnd, msg, wparam, lparam, result);
        }

        virtual void transferFrom(SPRITE* sprite)
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::transferFrom(sprite);
        }

        virtual bool pumpOnce()
        {
            return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::pumpOnce();
        }

        virtual void drawShellOverlays()
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::drawShellOverlays();
        }

        virtual void deinitialize()
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::deinitialize();
        }

        virtual void runCommandLine(char* ownedCommandLine)
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::runCommandLine(ownedCommandLine);
        }

        virtual void saveMap(const as1::STRING& outputName)
        {
            reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::saveMap(outputName);
        }

        virtual SPRITE* CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
        {
            return reinterpret_cast<ApplicationWin*>(this)->ApplicationWin::CreateSprite(
                vid, xyz, direction, parent);
        }

        static std::uintptr_t currentVtable() noexcept
        {
            static ApplicationDerivedVtableOwner owner;
            return *reinterpret_cast<const std::uintptr_t*>(&owner);
        }
    };

    namespace
    {
        constexpr std::uint32_t kApplicationCleanupBusyFlag = 0x00000002u;
        constexpr std::uint32_t kApplicationInitializedFlag = 0x00000004u;
        constexpr std::uint32_t kApplicationFramePumpFlag = 0x00000008u;
        constexpr std::uint32_t kApplicationModalDispatchFlag = 0x00000010u;
        constexpr std::uint32_t kApplicationCommandLinePendingFlag = application_flags::PendingCommandOrLoad;
        constexpr std::uint32_t kApplicationRenderControlsFlag = 0x00000080u;
        constexpr std::uint32_t kMapSelectSpriteUnderCursorFlag = 0x00100000u;
        constexpr std::uint32_t kApplicationStartupForcedFlags = 0x00111080u;


        constexpr std::uint32_t kShellLowNibbleMask = 0x0000000Fu;
        constexpr std::uint32_t kShellTogglePause = 0x00000001u;
        constexpr std::uint32_t kShellForceFrame = 0x00000002u;
        constexpr std::uint32_t kShellDrawLabels = 0x00000004u;
        constexpr std::uint32_t kShellDispatchOverlayList = 0x00000008u;


        constexpr std::uint32_t kDebugDrawTerrainGrid = 0x00000800u;
        constexpr std::uint32_t kDebugDrawCurrentSprite = 0x00001000u;
        constexpr std::uint32_t kDebugDrawAuxiliaryList = 0x00002000u;
        constexpr std::uint32_t kDebugDrawScrollBox = 0x00004000u;
        constexpr std::uint32_t kDebugDrawSpriteBuckets = 0x00008000u;
        constexpr std::uint32_t kDebugShowSoundCount = 0x00010000u;
        constexpr std::uint32_t kDebugShowFps = 0x00020000u;

        int decodeControlKeyName(STRING name)
        {
            char* const mutableText = const_cast<char*>(name.c_str());
            for (char* p = mutableText; p && *p; ++p)
            {
                if (*p >= 'a' && *p <= 'z')
                    *p = static_cast<char>(*p - ('a' - 'A'));
            }

            const char* const text = name.c_str();
            if (std::strcmp(text, "LBUTTON") == 0)
                return 1;
            if (std::strcmp(text, "RBUTTON") == 0)
                return 2;
            if (std::strcmp(text, "[") == 0)
                return 91;
            if (std::strcmp(text, "]") == 0)
                return 92;
            if (std::strcmp(text, "LEFT") == 0)
                return 37;
            if (std::strcmp(text, "RIGHT") == 0)
                return 39;
            if (std::strcmp(text, "UP") == 0)
                return 38;
            if (std::strcmp(text, "DOWN") == 0)
                return 40;
            if (std::strcmp(text, "INSERT") == 0)
                return 45;
            if (std::strcmp(text, "DELETE") == 0)
                return 46;
            if (std::strcmp(text, "HOME") == 0)
                return 36;
            if (std::strcmp(text, "END") == 0)
                return 35;
            if (std::strcmp(text, "PGUP") == 0)
                return 33;
            if (std::strcmp(text, "PGDN") == 0)
                return 34;
            if (std::strcmp(text, "SHIFT") == 0)
                return 16;
            if (std::strcmp(text, "CTRL") == 0)
                return 17;
            if (std::strcmp(text, "F1") == 0)
                return 112;
            if (std::strcmp(text, "F2") == 0)
                return 113;
            if (std::strcmp(text, "F3") == 0)
                return 114;
            if (std::strcmp(text, "F4") == 0)
                return 115;
            if (std::strcmp(text, "F5") == 0)
                return 116;
            if (std::strcmp(text, "F6") == 0)
                return 117;
            if (std::strcmp(text, "F7") == 0)
                return 118;
            if (std::strcmp(text, "F8") == 0)
                return 119;
            if (std::strcmp(text, "F9") == 0)
                return 120;
            if (std::strcmp(text, "F10") == 0)
                return 121;
            if (std::strcmp(text, "F11") == 0)
                return 122;
            if (std::strcmp(text, "F12") == 0)
                return 123;
            return static_cast<signed char>(text[0]);
        }

        std::uint32_t g_presentationVersionLastSpawnMs = 0u;


        std::uint32_t g_cursorActivationState = 0u;
        std::uint32_t g_cursorRefreshPending = 0u;

        bool debugModeEnabled() noexcept
        {
            const as1::CONSTANT* const constants = as1::g_baseConstants;
            return constants && constants->raw[10] != 0u;
        }

        constexpr std::uint32_t kFrameClampMs = 71u;
        constexpr std::uint32_t kDemoFrameToleranceMs = 20u;
        constexpr float kCameraEdgeThreshold = 5.0f;
        constexpr float kCameraAccelerationX = 0.039999999f;
        constexpr float kCameraAccelerationY = 0.029999999f;
        constexpr float kCameraTargetFollowScale = 0.001f;
        constexpr float kCameraPointerFollowScale = -0.0040000002f;

        float dwordAsFloat(std::uint32_t value) noexcept
        {
            float out = 0.0f;
            std::memcpy(&out, &value, sizeof(out));
            return out;
        }

        bool cameraLessOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        bool cameraLessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        bool cameraEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        float cameraMinss(float destination, float source) noexcept
        {

            if (std::isnan(destination) || std::isnan(source))
                return source;
            return destination < source ? destination : source;
        }

        std::int32_t convertCameraElapsedScaleToInt32(std::uint32_t elapsed, float scale) noexcept
        {


            const float value = static_cast<float>(elapsed) * scale;
            if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return static_cast<std::int32_t>(0x80000000u);
            return static_cast<std::int32_t>(value);
        }

        int terrainGridDimension(float extent) noexcept
        {
            const int raw = static_cast<int>(extent + 7.0f);
            return (raw + (raw < 0 ? 7 : 0)) >> 3;
        }

        constexpr DWORD kMainWindowExStyle = 0x00040000u;
        constexpr DWORD kWindowedStyle = 0x90CA0000u;
        constexpr DWORD kBorderlessStyle = 0x90000000u;
        constexpr const char* kMenuResourceName = "AppMenu";
        constexpr const char* kIconResourceName = "AppIcon";
        constexpr const char* kAcceleratorResourceName = "AppAccel";

        std::uint32_t g_tooltipLastUpdateTime = 0;
        float g_tooltipLastClientX = 0.0f;
        float g_tooltipLastClientY = 0.0f;
        alignas(as1::STRING) unsigned char g_tooltipCachedTextStorage[sizeof(as1::STRING)];
        unsigned char g_tooltipInitFlags = 0u;

        void __cdecl cleanupCachedTooltipText()
        {

            as1::STRING& owner = *reinterpret_cast<as1::STRING*>(g_tooltipCachedTextStorage);
            char* const raw = const_cast<char*>(owner.c_str());
            if (raw != as1::STRING::SharedEmptyText())
                ::operator delete(raw);
        }

        as1::STRING& cachedTooltipText()
        {

            if ((g_tooltipInitFlags & 1u) == 0u)
            {
                new (g_tooltipCachedTextStorage) as1::STRING();
                g_tooltipInitFlags |= 1u;
                std::atexit(cleanupCachedTooltipText);
            }
            return *reinterpret_cast<as1::STRING*>(g_tooltipCachedTextStorage);
        }


        as1::input::InputMessageState& inputState(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<as1::input::InputMessageState*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::InputState);
        }

        const as1::input::InputMessageState& inputState(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const as1::input::InputMessageState*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::InputState);
        }

        MENU& embeddedMenu(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<MENU*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::Menu);
        }

        const MENU& embeddedMenu(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const MENU*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::Menu);
        }

        __forceinline
        BaseSpriteList<0>& embeddedFrameSpriteList(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<BaseSpriteList<0>*>(
                reinterpret_cast<std::uint8_t*>(app) + core::application_layout::BaseSpriteList);
        }

        __forceinline
        const BaseSpriteList<0>& embeddedFrameSpriteList(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const BaseSpriteList<0>*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::BaseSpriteList);
        }

        __forceinline
        core::ApplicationDrawPassBucket& embeddedDrawPassBucket(ApplicationWin* app, int pass) noexcept
        {
            return *reinterpret_cast<core::ApplicationDrawPassBucket*>(
                reinterpret_cast<std::uint8_t*>(app) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
        }

        __forceinline
        const core::ApplicationDrawPassBucket& embeddedDrawPassBucket(const ApplicationWin* app, int pass) noexcept
        {
            return *reinterpret_cast<const core::ApplicationDrawPassBucket*>(
                reinterpret_cast<const std::uint8_t*>(app) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
        }

        __forceinline
        int physicalIntSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        __forceinline
        std::uint32_t physicalDwordSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        __forceinline
        short* physicalTerrainGrid(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<short* const*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::TerrainGrid);
        }

        __forceinline
        int physicalVidCount(const ApplicationWin* app) noexcept
        {
            return physicalIntSlot(app, core::application_layout::VidCount);
        }

        __forceinline
        VID* physicalVidSlotUnchecked(const ApplicationWin* app, int index) noexcept
        {
            return *reinterpret_cast<VID* const*>(
                reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::VidTable +
                static_cast<std::size_t>(index) * sizeof(std::uint32_t));
        }

        HINSTANCE& applicationInstanceHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::InstanceHandle);
        }

        HINSTANCE applicationInstanceHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HINSTANCE const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::InstanceHandle);
        }

        HWND& mainWindowHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::MainWindow);
        }

        HWND mainWindowHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HWND const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::MainWindow);
        }

        HACCEL& acceleratorHandle(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::Accelerator);
        }

        HACCEL acceleratorHandle(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<HACCEL const*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::Accelerator);
        }

        std::uint32_t& shellFlagsStorage(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(app) + core::application_layout::ShellFlags);
        }

        std::uint32_t shellFlagsStorage(const ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(app) + core::application_layout::ShellFlags);
        }

        float& physicalFloatSlot(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }

        float physicalFloatSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        constexpr std::size_t kApplicationTitleOffset = core::application_layout::ApplicationTitle;
        constexpr std::size_t kCurrentMapNameOffset = core::application_layout::CurrentMapName;
        constexpr std::size_t kPendingCommandOffset = core::application_layout::PendingCommand;
        constexpr std::size_t kPreviousMapNameOffset = core::application_layout::PreviousMapName;
        constexpr std::size_t kResourceNameOffset = core::application_layout::ResourceName;

        as1::STRING& applicationStringAt(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<as1::STRING*>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }
        const as1::STRING& applicationStringAt(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<const as1::STRING*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }
        as1::STRING& applicationTitle(ApplicationWin* app) noexcept { return applicationStringAt(app, kApplicationTitleOffset); }
        const as1::STRING& applicationTitle(const ApplicationWin* app) noexcept { return applicationStringAt(app, kApplicationTitleOffset); }
        as1::STRING& currentMapName(ApplicationWin* app) noexcept { return applicationStringAt(app, kCurrentMapNameOffset); }
        const as1::STRING& currentMapName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kCurrentMapNameOffset); }
        as1::STRING& pendingCommand(ApplicationWin* app) noexcept { return applicationStringAt(app, kPendingCommandOffset); }
        const as1::STRING& pendingCommand(const ApplicationWin* app) noexcept { return applicationStringAt(app, kPendingCommandOffset); }
        as1::STRING& previousMapName(ApplicationWin* app) noexcept { return applicationStringAt(app, kPreviousMapNameOffset); }
        const as1::STRING& previousMapName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kPreviousMapNameOffset); }
        as1::STRING& resourceName(ApplicationWin* app) noexcept { return applicationStringAt(app, kResourceNameOffset); }
        const as1::STRING& resourceName(const ApplicationWin* app) noexcept { return applicationStringAt(app, kResourceNameOffset); }

        PLAYER*& playerPointerSlot(ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<PLAYER**>(reinterpret_cast<std::uint8_t*>(app) + offset);
        }

        PLAYER* playerPointerSlot(const ApplicationWin* app, std::size_t offset) noexcept
        {
            return *reinterpret_cast<PLAYER* const*>(reinterpret_cast<const std::uint8_t*>(app) + offset);
        }

        constexpr std::size_t playerSlotOffset(int index) noexcept
        {
            return core::application_layout::PlayerSlots +
                static_cast<std::size_t>(index & 3) * core::application_layout::PlayerSlotStride;
        }

        void deleteVidThroughVirtualDestructor(VID* vid) noexcept
        {
            if (!vid)
                return;

            delete vid;
        }

        void releaseShellOwnedSpriteSlot(void* ownerStorage) noexcept
        {
            auto** const objectSlot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(ownerStorage) + 4u);
            void* const object = *objectSlot;
            if (object)
            {
                auto* const sprite = static_cast<SPRITE*>(object);
                delete sprite;
            }
            *objectSlot = nullptr;
        }

        class MOUSETIPS
        {
        public:
            MOUSETIPS() noexcept : tip(nullptr) {}
            virtual ~MOUSETIPS()
            {
                Clear();
            }


            void Clear() noexcept
            {

                releaseShellOwnedSpriteSlot(this);
            }


            void DeletePointerToSprite(SPRITE* sprite) noexcept
            {
                if (tip == sprite)
                    tip = nullptr;
            }


            void Tact(as1::input::InputMessageState* input) noexcept;

            SPRITE* tip;
        };

        MOUSETIPS& mouseTipsOwner(ApplicationWin* app) noexcept
        {
            return *reinterpret_cast<MOUSETIPS*>(reinterpret_cast<std::uint8_t*>(app) +
                core::application_layout::MouseTips);
        }

        ApplicationWin* applicationFromMouseTips(MOUSETIPS* owner) noexcept
        {
            return reinterpret_cast<ApplicationWin*>(reinterpret_cast<std::uint8_t*>(owner) -
                core::application_layout::MouseTips);
        }


        std::string copyCString(const char* text)
        {
            return (text && *text) ? std::string(text) : std::string();
        }

        void toggleFlag(std::uint32_t& flags, std::uint32_t mask) noexcept
        {
            flags ^= mask;
        }

        __forceinline
        std::uintptr_t currentBaseApplicationVtable() noexcept
        {
            return ApplicationBaseVtableOwner::currentVtable();
        }

        __forceinline
        void installBaseApplicationVtable(ApplicationWin* app) noexcept
        {
            *reinterpret_cast<std::uintptr_t*>(app) = currentBaseApplicationVtable();
        }

        __forceinline
        void installDerivedApplicationVtable(ApplicationWin* app) noexcept
        {
            *reinterpret_cast<std::uintptr_t*>(app) = ApplicationDerivedVtableOwner::currentVtable();
        }


    }


    ApplicationWin* CreateApplicationWin(const ApplicationWinInit& init)
    {


        void* const storage = ::operator new(core::application_layout::ObjectSize, std::nothrow);
        if (!storage)
            return nullptr;
        ApplicationWin* const app = new (storage) ApplicationWin(init);


        as1::core::g_applicationOwner = app;
        return app;
    }

    void DestroyApplicationWin(ApplicationWin* app)
    {
        if (!app)
            return;

        app->~ApplicationWin();


        if (as1::Map == reinterpret_cast<as1::MAP*>(app))
            as1::Map = nullptr;
        if (as1::core::ApplicationOwner() == app)
            as1::core::g_applicationOwner = nullptr;
        ::operator delete(app);


    }

    ApplicationWin::ApplicationWin(const ApplicationWinInit& init)
    {

        (void)init;
    }

    ApplicationWin::~ApplicationWin()
    {
        destroyDerivedApplicationState();
        delete MAP::s_data;
        MAP::s_data = nullptr;
    }


    void ApplicationWin::destroyDerivedApplicationState()
    {
        installDerivedApplicationVtable(this);


        reinterpret_cast<MOUSETIPS*>(reinterpret_cast<std::uint8_t*>(this) +
            core::application_layout::MouseTips)->Clear();
        deinitialize();
        destroyBaseApplicationState();
    }



    ApplicationWin* ApplicationWin::initializeDerivedApplicationStartup(HINSTANCE instance,
                                                     HINSTANCE previousInstance,
                                                     const char** commandLineOwner,
                                                     int showCmd,
                                                     as1::core::StartupSettingsBlock* startupSettings)
    {

        (void)initializeBaseApplicationStartup(instance, previousInstance, commandLineOwner, showCmd, startupSettings);


        installDerivedApplicationVtable(this);
        if (!initialized())
            return this;

        shellFlagsStorage(this) &= ~kShellLowNibbleMask;
        as1::STRING startupCommand(pendingCommand(this).c_str());
        runCommandLineMap(startupCommand.DetachOwnedStorage());
        return this;
    }


    ApplicationWin* ApplicationWin::initializeBaseApplicationStartup(HINSTANCE instance,
                                    HINSTANCE previousInstance,
                                    const char** commandLineOwner,
                                    int showCmd,
                                    as1::core::StartupSettingsBlock* startupSettings)
    {



        const std::uint32_t startupFlags = startupSettings->flags;



        new (&applicationTitle(this)) as1::STRING();
        new (&currentMapName(this)) as1::STRING();
        new (&pendingCommand(this)) as1::STRING();
        new (&previousMapName(this)) as1::STRING();
        new (&resourceName(this)) as1::STRING();


        for (int pass = 0; pass < as1::core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            void* const slot = reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride;
            new (slot) core::ApplicationDrawPassBucket();
        }

        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime) SCRIPT();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::DemoResource) RESOURCE();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::RelationTable) RelationTable();
        inputState(this).initializePreservingPersistentFlags();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Menu) MENU();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups) GROUPS();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::MouseTips) MOUSETIPS();
        new (reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TailSpriteList) core::List<SPRITE*>();
        *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(this) +
            core::application_layout::ShellFlags) = 0u;

        installBaseApplicationVtable(this);




        const char* const commandLine = *commandLineOwner;
        char startupCurrentDirectory[0x1000] = {};
        if (::GetCurrentDirectoryA(static_cast<DWORD>(sizeof(startupCurrentDirectory)), startupCurrentDirectory) == 0)
            startupCurrentDirectory[0] = '\0';


        as1::core::StartupConfiguration windowConfig;
        as1::core::StartupSettingsBlock& initialStartupSettings = as1::core::StartupSettings();
        windowConfig.startupFlags = initialStartupSettings.flags;
        windowConfig.video.device = initialStartupSettings.device;
        windowConfig.video.screenX = initialStartupSettings.screenWidth;
        windowConfig.video.screenY = initialStartupSettings.screenHeight;
        windowConfig.video.colorDepth = initialStartupSettings.colorDepth;
        windowConfig.video.fullscreen = initialStartupSettings.fullscreen != 0;

        as1::STRING executablePath;
        if (as1::g_executablePath && *as1::g_executablePath)
            constructStringFromBytes(executablePath, as1::g_executablePath, std::strlen(as1::g_executablePath));

        as1::STRING executableFileName;
        constructRightOfLastMarker(executablePath, executableFileName, "\\");
        constructLeftOfLastMarker(executableFileName, windowConfig.applicationName, ".");
        windowConfig.applicationTitle = windowConfig.applicationName;

        as1::STRING defaultRegistryPath;
        constructConcatenatedString(defaultRegistryPath, "SOFTWARE\\Sigma\\", windowConfig.applicationName.c_str());
        windowConfig.registryPath = defaultRegistryPath;

        const as1::STRING currentDirectory(startupCurrentDirectory);
        const as1::STRING configDirectory = currentDirectory.isEmpty() ? as1::STRING(".") : currentDirectory;
        as1::STRING directoryPrefix;
        constructConcatenatedString(directoryPrefix, configDirectory.c_str(), "\\");

        as1::STRING configBasePath;
        constructConcatenatedString(configBasePath, directoryPrefix.c_str(), windowConfig.applicationName.c_str());
        as1::STRING resolvedConfigPath;
        constructConcatenatedString(resolvedConfigPath, configBasePath.c_str(), ".cfg");

        if (std::strstr(commandLine, ".cfg"))
            constructConcatenatedString(resolvedConfigPath, directoryPrefix.c_str(), commandLine);

        windowConfig.configPath = resolvedConfigPath;
        windowConfig.resourceRoot = configDirectory;


        const int localSavesMode = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("debug"), as1::STRING("zLocalSaves"), 0));
        if (localSavesMode == 0)
        {
            char personalPath[0x104] = {};
            const HRESULT result = ::SHGetFolderPathA(
                nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, personalPath);
            if (SUCCEEDED(result))
            {
                zs1::g_WindowsUserPath = personalPath;
                as1::STRING title;
                as1::core::profile_p::readProfileStringInto(
                    title, windowConfig.configPath, as1::STRING("common"),
                    as1::STRING("Title"), as1::STRING("Unknown"));
                as1::STRING suffix("\\");
                suffix += title;
                suffix += " Saves\\";
                zs1::g_WindowsUserPath += suffix;
                ::CreateDirectoryA(zs1::g_WindowsUserPath.c_str(), nullptr);
            }

            as1::STRING logs(zs1::g_WindowsUserPath);
            logs += "Logs";
            ::CreateDirectoryA(logs.c_str(), nullptr);

            as1::STRING screens(zs1::g_WindowsUserPath);
            screens += "Screens";
            ::CreateDirectoryA(screens.c_str(), nullptr);
        }

        const int localLogsMode = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("debug"), as1::STRING("zLocalLogs"), 0));
        as1::STRING errorLogBase = localLogsMode != 0
            ? as1::STRING("Logs")
            : as1::STRING(zs1::g_WindowsUserPath.c_str(), "Logs");


        as1::g_fileLogger = new (std::nothrow) as1::FileLogger(errorLogBase.c_str(), true);
        if (as1::g_fileLogger)
            as1::writeLogLine(as1::g_fileLogger, "WindowsUserPath is '%s'", zs1::g_WindowsUserPath.c_str());


        zs1::g_DebugLog = new (std::nothrow) zs1::zDebugLog();
        zs1::g_UserMngr = new (std::nothrow) zs1::zUserMngr("userdata");


        ::timeBeginPeriod(1u);
        const std::uint32_t initialRealTime = static_cast<std::uint32_t>(::timeGetTime());
        core::RealCurrentTime = initialRealTime;
        core::PrevRealCurrentTime = initialRealTime - 10u;
        SPRITE::initializeStartupTrigTables();

        std::uint32_t targetFlags = flags() & 0xFFF7FFFDu;
        targetFlags =
            ((targetFlags & 0xFFFF34A2u) ^ (startupFlags & 1u)) |
            0x00010080u;
        setFlags(targetFlags);

        const unsigned int drawUnitInfo = as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath,
            as1::STRING("debug"),
            as1::STRING("zDrawUnitInfo"),
            1);
        targetFlags = flags();
        targetFlags &= ~0x00001000u;
        targetFlags |= (drawUnitInfo & 1u) << 12;
        targetFlags = (targetFlags & 0xFFFDDBDFu) | 0x00100000u;
        setFlags(targetFlags);


        *reinterpret_cast<short**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGrid) = nullptr;
        *reinterpret_cast<short**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TempTerrainGrid) = nullptr;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGridWidth) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGridHeight) = 0;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponCount) = 0;
        *reinterpret_cast<WEAPON**>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponTable) = nullptr;
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidCount) = 0;
        physicalFloatSlot(this, core::application_layout::MapExtentX) = 640.0f;
        physicalFloatSlot(this, core::application_layout::MapExtentY) = 480.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ActivePlayerIndex) = 0u;
        physicalFloatSlot(this, core::application_layout::CameraShiftX) = 0.0f;
        physicalFloatSlot(this, core::application_layout::CameraShiftY) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponTable) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WeaponCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidCount) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Fps) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::FpsCounter) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WorldFrameCounter) = 0u;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::WorldStartTime) =
            initialRealTime;
        physicalFloatSlot(this, core::application_layout::TickScale) = 1.0f;
        *reinterpret_cast<std::uint32_t*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScrollType) = 1u;
        std::memset(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::VidTable,
            0,
            core::application_layout::VidTableBytes);
        playerPointerSlot(this, playerSlotOffset(0)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(1)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(2)) = nullptr;
        playerPointerSlot(this, playerSlotOffset(3)) = nullptr;
        applicationInstanceHandle(this) = instance;
        {
            short*& grid = *reinterpret_cast<short**>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TerrainGrid);
            short*& tempGrid = *reinterpret_cast<short**>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TempTerrainGrid);
            if (grid)
                ::operator delete(static_cast<void*>(grid));
            if (tempGrid)
                ::operator delete(static_cast<void*>(tempGrid));

            const int gridX = terrainGridDimension(
                physicalFloatSlot(this, core::application_layout::MapExtentX));
            const int gridY = terrainGridDimension(
                physicalFloatSlot(this, core::application_layout::MapExtentY));
            *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::TerrainGridWidth) = gridX;
            *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(this) +
                core::application_layout::TerrainGridHeight) = gridY;

            const std::size_t gridBytes =
                2u * static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
            grid = static_cast<short*>(::operator new(gridBytes));
            tempGrid = static_cast<short*>(::operator new(gridBytes));
            std::memset(grid, 0, gridBytes);
            std::memset(tempGrid, 0, gridBytes);
        }

        const HRESULT comInitializeResult = ::CoInitialize(nullptr);
        if (FAILED(comInitializeResult))
        {
            as1::LOG::ResourceError("MAP", 12, "COM", static_cast<int>(comInitializeResult));
            return this;
        }


        as1::core::g_startupStringsIniPathOwner = new (std::nothrow) as1::STRING();
        if (as1::core::g_startupStringsIniPathOwner)
        {
            const char* const rootText = windowConfig.resourceRoot.isEmpty()
                ? "."
                : windowConfig.resourceRoot.c_str();
            as1::STRING directoryPath(rootText);
            as1::STRING stringsIniPath;
            constructConcatenatedString(stringsIniPath, directoryPath.c_str(), "\\Strings.ini");
            resetAndAssignString(*as1::core::g_startupStringsIniPathOwner, stringsIniPath);
            windowConfig.stringsPath = *as1::core::g_startupStringsIniPathOwner;
        }

        {
            as1::STRING configuredTitle;
            as1::core::profile_p::readProfileStringInto(
                configuredTitle,
                windowConfig.configPath,
                as1::STRING("common"),
                as1::STRING("Title"),
                windowConfig.applicationName);
            assignStringFromString(windowConfig.applicationTitle, configuredTitle);
        }

        as1::core::g_startupRegistryPathOwner = new (std::nothrow) as1::REGISTRY();
        if (as1::core::g_startupRegistryPathOwner)
        {
            as1::STRING defaultRegistryPath;
            constructConcatenatedString(
                defaultRegistryPath, "SOFTWARE\\Sigma\\", windowConfig.applicationName.c_str());
            as1::STRING configuredRegistryPath;
            as1::core::profile_p::readProfileStringInto(
                configuredRegistryPath,
                windowConfig.configPath,
                as1::STRING("common"),
                as1::STRING("RegPath"),
                defaultRegistryPath);
            assignStringFromString(
                as1::core::g_startupRegistryPathOwner->mutablePath(), configuredRegistryPath);
            windowConfig.registryPath = *as1::core::g_startupRegistryPathOwner->Path();
        }

        as1::core::StartupSettingsBlock& startupProfileSettings = as1::core::StartupSettings();
        windowConfig.startupFlags = startupProfileSettings.flags;
        int startupProfileValue = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("VSync"), 1));
        startupProfileSettings.flags |= startupProfileValue ? as1::core::StartupVSyncFlag : 0u;
        windowConfig.startupFlags = startupProfileSettings.flags;
        windowConfig.video.vsync = (windowConfig.startupFlags & as1::core::StartupVSyncFlag) != 0u;

        startupProfileValue = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("StartDialogIsFull"), 0));
        startupProfileSettings.flags |= startupProfileValue ? as1::core::StartupDialogFullscreenFlag : 0u;
        windowConfig.startupFlags = startupProfileSettings.flags;
        windowConfig.startDialogFullscreen =
            (windowConfig.startupFlags & as1::core::StartupDialogFullscreenFlag) != 0u;

        windowConfig.noSysMenu = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("debug"), as1::STRING("zNoSysMenu"), 0));
        startupProfileSettings.screenWidth = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("DefaultScreenX"), 1024));
        startupProfileSettings.screenHeight = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("DefaultScreenY"), 768));
        startupProfileSettings.colorDepth = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("DefaultColorBPP"), 32));

        if (zs1::g_UserMngr)
        {
            startupProfileSettings.device = zs1::g_UserMngr->GetInt(true, "Device", startupProfileSettings.device);
            startupProfileSettings.screenWidth = zs1::g_UserMngr->GetInt(true, "ScreenX", startupProfileSettings.screenWidth);
            startupProfileSettings.screenHeight = zs1::g_UserMngr->GetInt(true, "ScreenY", startupProfileSettings.screenHeight);
            startupProfileSettings.colorDepth = zs1::g_UserMngr->GetInt(true, "BPP", startupProfileSettings.colorDepth);
            startupProfileSettings.fullscreen = zs1::g_UserMngr->GetInt(true, "FullScreen", startupProfileSettings.fullscreen);
        }
        if (startupProfileSettings.screenWidth != 1024 || startupProfileSettings.screenHeight != 768)
        {
            startupProfileSettings.screenWidth = 1024;
            startupProfileSettings.screenHeight = 768;
        }
        if (startupProfileSettings.colorDepth != 16 && startupProfileSettings.colorDepth != 32)
            startupProfileSettings.colorDepth = 32;
        windowConfig.video.device = startupProfileSettings.device;
        windowConfig.video.screenX = startupProfileSettings.screenWidth;
        windowConfig.video.screenY = startupProfileSettings.screenHeight;
        windowConfig.video.colorDepth = startupProfileSettings.colorDepth;
        windowConfig.video.fullscreen = startupProfileSettings.fullscreen != 0;

        assignStringFromString(applicationTitle(this), windowConfig.applicationTitle);


        void* const graphStorage = ::operator new(0x0E34u, std::nothrow);
        if (!graphStorage)
        {


            as1::Graph = nullptr;
            return this;
        }
        as1::GRAPH* const graph = new (graphStorage) as1::GRAPH();

        std::strcpy(startupSettings->title, applicationTitle(this).c_str());
        graph->initializeGraphState(*startupSettings);

        as1::Graph = graph;

        windowConfig.showStartDialog = as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("StartDialog"), 0) != 0u;
        if (windowConfig.showStartDialog)
        {


            if (::DialogBoxParamA(instance, "START_DIALOG", nullptr, as1::win::DialogFunc, 0) == 0)
                return this;
        }

        if (!previousInstance)
        {
            WNDCLASSA wc{};
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = applicationWindowProc;
            wc.hInstance = applicationInstanceHandle(this);
            wc.hIcon = LoadIconA(applicationInstanceHandle(this), kIconResourceName);
            wc.hCursor = nullptr;
            wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(4));
            wc.lpszClassName = windowConfig.applicationName.c_str();
            wc.lpszMenuName = (flags() & 1u) != 0u ? kMenuResourceName : nullptr;
            if (RegisterClassA(&wc) == 0)
                return this;
        }

        const bool fullscreenWindow = graph->fullscreenRequested();
        if (fullscreenWindow)
        {
            windowConfig.video.windowPositionX = 0;
            windowConfig.video.windowPositionY = 0;
        }
        else
        {


            windowConfig.video.windowPositionX =
                zs1::g_UserMngr->GetInt(true, "WindowPositionX", 0);
            windowConfig.video.windowPositionY =
                zs1::g_UserMngr->GetInt(true, "WindowPositionY", 0);
        }

        const DWORD windowStyle = (!fullscreenWindow && windowConfig.noSysMenu == 0)
            ? kWindowedStyle
            : kBorderlessStyle;
        as1::Map = reinterpret_cast<as1::MAP*>(this);
        HWND hwnd = CreateWindowExA(
            kMainWindowExStyle,
            windowConfig.applicationName.c_str(),
            applicationTitle(this).c_str(),
            windowStyle,
            fullscreenWindow ? 0 : windowConfig.video.windowPositionX,
            fullscreenWindow ? 0 : windowConfig.video.windowPositionY,
            static_cast<int>(graph->SizeX()),
            static_cast<int>(graph->SizeY()),
            nullptr,
            nullptr,
            applicationInstanceHandle(this),
            nullptr);
        mainWindowHandle(this) = hwnd;
        ShowWindow(hwnd, showCmd);
        UpdateWindow(hwnd);
        HACCEL accel = LoadAcceleratorsA(applicationInstanceHandle(this), kAcceleratorResourceName);
        acceleratorHandle(this) = accel;
        ShowCursor(FALSE);

        if (graph->initializeWindowDevice(hwnd) != 0)
            return this;
        windowConfig.font.sizeY = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("FontSizeY"), 8));
        windowConfig.font.sizeX = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("graph"), as1::STRING("FontSizeX"), 0));
        {
            as1::STRING configuredFontFace;
            as1::core::profile_p::readProfileStringInto(
                configuredFontFace,
                windowConfig.configPath,
                as1::STRING("graph"),
                as1::STRING("Font"),
                as1::STRING("Arial"));
            assignStringFromString(windowConfig.font.face, configuredFontFace);
        }
        graph->rebuildTextFont(windowConfig.font.face, windowConfig.font.sizeX, windowConfig.font.sizeY);

        as1::RESOURCE startupObjectsResource;
        {
            as1::STRING configuredResourceName;
            as1::core::profile_p::readProfileStringInto(
                configuredResourceName,
                windowConfig.configPath,
                as1::STRING("game"),
                as1::STRING("Resource"),
                as1::STRING("objects.res"));
            assignStringFromString(windowConfig.objectsResource, configuredResourceName);
        }
        assignStringFromString(resourceName(this), windowConfig.objectsResource);
        if (startupObjectsResource.openFile(&windowConfig.objectsResource,
                                               as1::RESOURCE::ResTypes::DATA) != 0)
        {
            as1::LOG::ResourceError("%s", 7, "resource file", 0, "");
            return this;
        }

        windowConfig.sound.highQuality = true;

        void* const soundStorage = ::operator new(sizeof(as1::sound::Engine), std::nothrow);
        as1::sound::Engine* const soundEngine = static_cast<as1::sound::Engine*>(soundStorage);
        if (soundEngine)
        {
            as1::sound::g_globalSoundEngine = soundEngine->initializeSoundState(
                hwnd, &startupObjectsResource, windowConfig.sound.highQuality ? 1 : 0);
        }
        else
        {
            as1::sound::g_globalSoundEngine = nullptr;
        }

        as1::CONSTANT* baseConstants = new (std::nothrow) as1::CONSTANT;
        if (baseConstants)
            as1::g_baseConstants = baseConstants ? baseConstants->Load(&startupObjectsResource) : nullptr;
        else
            as1::g_baseConstants = nullptr;

        windowConfig.debugMode = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DebugMode"), 0));
        windowConfig.drawFps = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DrawFPS"), 0));
        windowConfig.drawPresentation = static_cast<int>(as1::core::profile_p::readProfileIntValue(
            windowConfig.configPath, as1::STRING("game"), as1::STRING("DrawPresentation"), 1));
        if (as1::g_baseConstants)
            as1::g_baseConstants->raw[10] = static_cast<DWORD>(windowConfig.debugMode);

        std::uint32_t startupApplicationFlags = flags();
        startupApplicationFlags = (startupApplicationFlags & ~0x00020000u) |
            (windowConfig.drawFps ? 0x00020000u : 0u);
        startupApplicationFlags = (startupApplicationFlags & ~0x00040000u) |
            (windowConfig.drawPresentation ? 0x00040000u : 0u);
        setFlags(startupApplicationFlags);

        delete as1::MAP::s_data;
        as1::MAP::s_data = new (std::nothrow) as1::MAP::Data;
        if (!as1::MAP::s_data)
            return this;
        as1::MAP::s_data->graph = graph;
        as1::MAP::s_data->objectsResource = windowConfig.objectsResource;
        as1::MAP& map = *as1::Map;
        (void)map.loadVids(&startupObjectsResource);
        ::ShowCursor(TRUE);


        {
            as1::core::ApplicationVidTable& appVidTable = as1::core::GlobalApplicationVidTable();
            appVidTable.setWeaponSentinel(map.weaponTable());
            as1::EmptyVid->setWeaponRecord(appVidTable.weaponSentinel());
            void* const storage = ::operator new(0x44u, std::nothrow);
            as1::SPRITE_COLLECTOR* const created = storage
                ? new (storage) as1::SPRITE_COLLECTOR(as1::core::ApplicationMapWidth(),
                                                              as1::core::ApplicationMapHeight(),
                                                              appVidTable.slotData(),
                                                              appVidTable.count())
                : nullptr;
            as1::SetGlobalSpriteCollector(created);
        }
        startupObjectsResource.close();
        ::SetCursor(nullptr);

        as1::Mouse = new (std::nothrow) as1::MOUSE(
            as1::EmptyVid,
            graph->screenWidth() * 0.5f,
            graph->screenHeight() * 0.5f,
            0.0f,
            0,
            nullptr);
        as1::Mouse->Enable();
        const auto readControlString = [&windowConfig](const char* keyName, const as1::STRING& defaultValue) {
            as1::STRING configuredText;
            as1::core::profile_p::readProfileStringInto(
                configuredText,
                windowConfig.configPath,
                as1::STRING("control"),
                as1::STRING(keyName),
                defaultValue);
            return configuredText;
        };
        {
            const char leftBytes[2] = { static_cast<char>(0x25), '\0' };
            const char upBytes[2] = { static_cast<char>(0x26), '\0' };
            const char rightBytes[2] = { static_cast<char>(0x27), '\0' };
            const unsigned char downBytes[2] = { 0x04u, 0x38u };
            as1::STRING leftDefault(leftBytes);
            as1::STRING upDefault(upBytes);
            as1::STRING rightDefault(rightBytes);
            as1::STRING downDefault;
            downDefault.AssignBytes(downBytes, 2);
            windowConfig.control.left = readControlString("Left", leftDefault);
            windowConfig.control.up = readControlString("Up", upDefault);
            windowConfig.control.right = readControlString("Right", rightDefault);
            windowConfig.control.down = readControlString("Down", downDefault);
            windowConfig.control.relative = as1::core::profile_p::readProfileIntValue(
                windowConfig.configPath, as1::STRING("control"), as1::STRING("Relative"), 0);
            windowConfig.control.firstAction = readControlString("First", as1::STRING("LBUTTON"));
            windowConfig.control.secondAction = readControlString("Second", as1::STRING("RBUTTON"));
            windowConfig.control.previousWeapon = readControlString("Prev", as1::STRING("["));
            windowConfig.control.nextWeapon = readControlString("Next", as1::STRING("]"));
        }


        as1::input::InputControlKeys& controlKeys = as1::input::g_inputControlKeys;
        controlKeys.left1 = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.left.c_str()[0])));
        controlKeys.up1 = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.up.c_str()[0])));
        controlKeys.right1 = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.right.c_str()[0])));
        controlKeys.down1 = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<signed char>(windowConfig.control.down.c_str()[0])));
        as1::input::g_relativeControlEnabled = windowConfig.control.relative;
        controlKeys.first0 = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.firstAction));
        controlKeys.second0 = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.secondAction));
        controlKeys.previousWeapon = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.previousWeapon));
        controlKeys.nextWeapon = static_cast<std::uint32_t>(
            decodeControlKeyName(windowConfig.control.nextWeapon));

        PLAYER_ARCADE* const player0 = new (std::nothrow) PLAYER_ARCADE(1, 0);
        playerPointerSlot(this, playerSlotOffset(0)) = player0;
        PLAYER_ARCADE* const player2 = new (std::nothrow) PLAYER_ARCADE(0, 2);
        playerPointerSlot(this, playerSlotOffset(2)) = player2;
        PLAYER_ARCADE* const player1 = new (std::nothrow) PLAYER_ARCADE(2, 1);
        playerPointerSlot(this, playerSlotOffset(1)) = player1;
        PLAYER_ARCADE* const player3 = new (std::nothrow) PLAYER_ARCADE(0, 3);
        playerPointerSlot(this, playerSlotOffset(3)) = player3;

        physicalFloatSlot(this, core::application_layout::ScrollMaxX) = as1::core::ApplicationMapWidth();
        physicalFloatSlot(this, core::application_layout::ScrollMaxY) = as1::core::ApplicationMapHeight();
        physicalFloatSlot(this, core::application_layout::ScrollMinX) = 0.0f;
        physicalFloatSlot(this, core::application_layout::ScrollMinY) = 0.0f;

        const as1::STRING startupCommandLine(commandLine);
        if (commandLine[0] != '\0' && std::strstr(commandLine, ".cfg") == nullptr)
        {
            assignStringFromString(windowConfig.startMap, startupCommandLine);
        }
        else
        {
            as1::STRING configuredMapName;
            as1::core::profile_p::readProfileStringInto(
                configuredMapName,
                windowConfig.configPath,
                as1::STRING("game"),
                as1::STRING("StartMap"),
                as1::STRING("maps\\logo.map"));
            assignStringFromString(windowConfig.startMap, configuredMapName);
        }
        map.setResourceRoot(windowConfig.resourceRoot);
        map.setObjectsResource(windowConfig.objectsResource);


        assignStringFromString(pendingCommand(this), windowConfig.startMap);
        setFlags(flags() | 0x00000004u);
        return this;
    }


    void ApplicationWin::setPendingCommand(const as1::STRING& value)
    {

        assignStringFromString(pendingCommand(this), value);
    }

    void ApplicationWin::setCurrentMapName(const as1::STRING& value)
    {

        assignStringFromString(currentMapName(this), value);
    }


    void ApplicationWin::transferFrom(SPRITE* sprite)
    {

        mouseTipsOwner(this).DeletePointerToSprite(sprite);
        reinterpret_cast<MAP*>(this)->DeletePointerToSprite(sprite);
    }

    bool ApplicationWin::pumpOnce()
    {


        if (pumpNativeMessages())
            return true;

        if (debugModeEnabled())
        {
            const std::uint32_t lastCode = inputState(this).lastCode;
            switch (lastCode)
            {
            case 'O': setFlags(flags() ^ 0x00008800u); break;
            case 'P': toggleFlag(shellFlagsStorage(this), kShellTogglePause); break;
            case 'I': setFlags(flags() ^ 0x00001000u); break;
            case 'H': toggleFlag(shellFlagsStorage(this), kShellDrawLabels); break;
            case 'R': setFlags(flags() ^ 0x00002000u); break;
            case 'G': toggleFlag(shellFlagsStorage(this), kShellDispatchOverlayList); break;
            default: break;
            }
        }

        bool waitForMessage = true;
        if ((flags() & kApplicationFramePumpFlag) || (shellFlagsStorage(this) & kShellForceFrame))
        {
            GRAPH* const waitGraph = Graph;
            waitForMessage = waitGraph && waitGraph->isModalRenderStateActive();
            if (!waitForMessage && debugModeEnabled() &&
                (shellFlagsStorage(this) & kShellTogglePause) && inputState(this).lastCode != 0x70u)
                waitForMessage = true;
        }
        if (waitForMessage)
        {
            as1::core::SetCurrentTimeMilliseconds(as1::core::PreviousWorldTimeMilliseconds());
            WaitMessage();
            return false;
        }

        GRAPH* const preFrameGraph = Graph;


        if (!preFrameGraph)
            return false;
        const std::uint32_t preFrameGraphFlags =
            *reinterpret_cast<const std::uint32_t*>(
                reinterpret_cast<const std::uint8_t*>(preFrameGraph) + 0x38u);
        const bool sceneRequested =
            ((flags() & kApplicationFramePumpFlag) != 0u) ||
            ((preFrameGraphFlags & 0x80u) == 0u);
        bool sceneActive = sceneRequested && preFrameGraph->movieComObject(0) == nullptr;
        if (sceneActive && preFrameGraph->PreTact() != 0)
            return false;

        as1::core::SetApplicationWorldFrameCounter(
            as1::core::ApplicationWorldFrameCounter() + 1u);
        const int demoResult = processDemoFrame();
        GRAPH* const activeGraph = Graph;
        if (demoResult == 999999)
        {
            if (activeGraph)
                activeGraph->PostTact(1);
            return false;
        }
        bool worldTick = sceneActive && demoResult != 0;

        (void)embeddedMenu(this).Control(&inputState(this));

        (void)reinterpret_cast<as1::core::Application*>(this)->callScriptFunctionInternal(
            -1, 0, 0, 0);

        GRAPH* const postScriptGraph = Graph;
        if (worldTick && postScriptGraph && postScriptGraph->movieComObject(0) != nullptr)
        {
            worldTick = false;
            sceneActive = false;
        }

        {
            SPRITE* target = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));
            if (target && target->armyBits() == 0u)
            {
                const CONSTANT* const constants = as1::g_baseConstants;
                if (constants && constants->raw[20] != 0u)
                {
                    const std::uint32_t packed = constants->raw[20];
                    std::uint32_t subtractive = 0;
                    std::uint32_t additive = 0;
                    const std::uint32_t gammaMasks[4] = {0x0000007Fu, 0x00007F80u, 0x007F8000u, 0xFF800000u};
                    const std::uint32_t gammaSignBits[4] = {0x00000080u, 0x00008000u, 0x00800000u, 0x80000000u};
                    for (int gammaIndex = 0; gammaIndex < 4; ++gammaIndex)
                    {
                        if (packed & gammaSignBits[gammaIndex])
                            additive |= ((~packed) & gammaMasks[gammaIndex]) << 1;
                        else
                            subtractive |= (packed & gammaMasks[gammaIndex]) << 1;
                    }
                    const std::uint32_t gammaRaw[2] = {subtractive, additive};
                    target->SetGamma(Gamma{gammaRaw[0], gammaRaw[1]});
                    if (SPRITE* child = target->childChain())
                    {
                        VID* const targetVid = target->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{gammaRaw[0], gammaRaw[1]});
                    }
                }
            }
        }

        {
            SPRITE* target = activeAuxiliarySprite();
            const CONSTANT* const constants = as1::g_baseConstants;
            if (target && constants && (flags() & kMapSelectSpriteUnderCursorFlag) != 0u &&
                (constants->raw[21] != 0u || constants->raw[22] != 0u))
            {
                const std::uint32_t maskedFlags = target->armyBits();
                const std::uint32_t packed = (maskedFlags == 0x00001000u)
                    ? constants->raw[21]
                    : constants->raw[22];
                std::uint32_t subtractive = 0;
                std::uint32_t additive = 0;
                const std::uint32_t gammaMasks[4] = {0x0000007Fu, 0x00007F80u, 0x007F8000u, 0xFF800000u};
                const std::uint32_t gammaSignBits[4] = {0x00000080u, 0x00008000u, 0x00800000u, 0x80000000u};
                for (int gammaIndex = 0; gammaIndex < 4; ++gammaIndex)
                {
                    if (packed & gammaSignBits[gammaIndex])
                        additive |= ((~packed) & gammaMasks[gammaIndex]) << 1;
                    else
                        subtractive |= (packed & gammaMasks[gammaIndex]) << 1;
                }
                const std::uint32_t gammaRaw[2] = {subtractive, additive};
                target->SetGamma(Gamma{gammaRaw[0], gammaRaw[1]});
                if (SPRITE* child = target->childChain())
                {
                    VID* const targetVid = target->Vid();
                    if (child->Vid() == targetVid->linkedVid())
                        child->SetGamma(Gamma{gammaRaw[0], gammaRaw[1]});
                }
            }
        }

        if (GRAPH* const frameGraph = Graph)
            frameGraph->Tact(worldTick ? 1 : 0);

        {
            const std::uint32_t neutralGamma[2] = {0u, 0u};
            const CONSTANT* const constants = as1::g_baseConstants;

            SPRITE* primary = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));
            if (primary && constants && constants->raw[20] != 0u)
            {
                primary = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));
                if (primary->armyBits() == 0u)
                {
                    primary = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));
                    primary->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});

                    primary = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));
                    if (SPRITE* child = primary ? primary->childChain() : nullptr)
                    {
                        VID* const targetVid = primary->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});
                    }
                }
            }

            if (constants && (constants->raw[21] != 0u || constants->raw[22] != 0u))
            {
                SPRITE* auxiliary = activeAuxiliarySprite();
                if (auxiliary)
                {
                    auxiliary = activeAuxiliarySprite();
                    auxiliary->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});

                    auxiliary = activeAuxiliarySprite();
                    if (SPRITE* child = auxiliary ? auxiliary->childChain() : nullptr)
                    {
                        VID* const targetVid = auxiliary->Vid();
                        if (child->Vid() == targetVid->linkedVid())
                            child->SetGamma(Gamma{neutralGamma[0], neutralGamma[1]});
                    }
                }
            }
        }
        if ((flags() & kApplicationModalDispatchFlag) != 0u)
        {
            BaseSpriteList<0>& list = embeddedFrameSpriteList(this);
            for (int cursor = list.activeCount() - 1; cursor >= 0; --cursor)
            {
                if (SPRITE* const sprite = list.at(static_cast<std::size_t>(cursor)))
                    sprite->Tact();
            }
        }
        else
        {
            for (int pass = 0; pass < 20; ++pass)
            {
                const as1::core::ApplicationDrawPassBucket& bucket = embeddedDrawPassBucket(this, pass);
                for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
                {
                    if (SPRITE* const sprite = bucket.spriteAt(cursor))
                        sprite->Tact();
                }
            }
        }

        mouseTipsOwner(this).Tact(&inputState(this));
        updateCameraFromInput();

        const std::uint32_t applicationFlags = flags();
        const bool controlPath = ((applicationFlags & kApplicationModalDispatchFlag) == 0u) &&
                                 ((applicationFlags & kApplicationRenderControlsFlag) != 0u);
        if (controlPath && (applicationFlags & 0x00040000u) != 0u)
        {
            const std::uint32_t now = as1::core::RealCurrentTime;
            if (now - g_presentationVersionLastSpawnMs > 2000u)
            {
                GRAPH* const graph = Graph;
                MAP* const map = Map;
                if (graph && map)
                {
                    as1::core::ApplicationVidTable& vidTable =
                        as1::core::GlobalApplicationVidTable();
                    VID* presentationVid = EmptyVid;
                    if (vidTable.count() > 2)
                    {
                        if (VID* const slot2 = vidTable.slot(2))
                            presentationVid = slot2;
                    }

                    SPRITE* const presentation = CreateSprite(
                        presentationVid,
                        VECTOR(graph->screenWidth() * 0.5f, 32.0f, 0.0f),
                        ANGLE(static_cast<unsigned char>(0)),
                        nullptr);
                    if (presentation)
                    {
                        STRING presentationText("Presentation version. Not for sale!");
                        g_presentationVersionLastSpawnMs = now;
                        presentation->Action(0x5F, 1, 0, 0);
                        presentation->Action(0x78,
                            reinterpret_cast<std::intptr_t>(&presentationText), 0, 0);
                        presentation->Action(0x28, 1000, 0, 0);
                        presentation->ActionStack()->insertAt(0u, ACT{15u, 0u, 0u, 0u});
                    }
                }
            }
        }

        if ((flags() & kApplicationModalDispatchFlag) == 0u &&
            (flags() & kApplicationRenderControlsFlag) != 0u)
        {
            for (int index = 0; index < 4; ++index)
            {
                PLAYER* const player = playerPointerSlot(this, playerSlotOffset(index));
                using PlayerInputFn = void (__thiscall*)(as1::PLAYER*, as1::input::InputMessageState*);
                void** const playerVtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<PlayerInputFn>(playerVtable[6])(player, &inputState(this));
            }
        }

        if (worldTick)
            drawShellOverlays();

        if (sceneActive)
        {
            if (GRAPH* const presentGraph = Graph)
                presentGraph->PostTact(1);
        }

        if (as1::sound::Engine* const engine = as1::sound::g_globalSoundEngine)
            engine->updateSoundRequestQueue();
        return false;
    }


    void ApplicationWin::drawShellOverlays()
    {
        drawApplicationDebugPass();


        if (shellFlagsStorage(this) & kShellDrawLabels)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            core::List<SPRITE*>& list = hash->mutableOverflowList();
            int* const cursor = hash->reverseCursorAddress();
            for (SPRITE* sprite = list.BeginIterate(cursor);
                 sprite;
                 sprite = list.NextIterate(cursor))
            {
                if (sprite->childBacklink() != nullptr)
                    continue;

                const core::ApplicationDrawDispatcherState& drawState =
                    core::GlobalApplicationDrawDispatcherState();
                Graph->PrintfXY(
                    sprite->X() - drawState.cameraShiftX(),
                    sprite->Y() - sprite->Z() - drawState.cameraShiftY(),
                    "%i", sprite->Hp());
            }
        }


        if (shellFlagsStorage(this) & kShellDispatchOverlayList)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            core::List<SPRITE*>& list = hash->mutableOverflowList();
            int* const cursor = hash->reverseCursorAddress();
            for (SPRITE* sprite = list.BeginIterate(cursor);
                 sprite;
                 sprite = list.NextIterate(cursor))
            {
                sprite->DrawRelationDebugOverlay();
            }
        }
    }


    void ApplicationWin::deinitialize()
    {
        setFlags(flags() & ~kApplicationCleanupBusyFlag);
        g_spriteWorkList.deleteAllSprites();
        reinterpret_cast<MOUSETIPS*>(reinterpret_cast<std::uint8_t*>(this) +
            core::application_layout::MouseTips)->Clear();
        if (MAP* const releaseMap = Map)
            releaseMap->release();
        else
        {
            if (GRAPH* const graph = Graph)
            {
                graph->releaseMoviePlayback();
                graph->SetWind(25, ANGLE(static_cast<unsigned char>(200)));
                graph->SetEnvironment(0xFFFFFFFFu);
            }
            as1::DeleteGlobalSpriteCollector();
        }
    }


    void ApplicationWin::destroyBaseApplicationState()
    {

        installBaseApplicationVtable(this);

        core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            drawState.drawPassBucket(pass).list.deleteAllSprites();

        if (Mouse)
        {
            MOUSE* const mouse = Mouse;


            delete mouse;
        }

        for (int index = 0; index < 4; ++index)
        {
            PLAYER*& player = playerPointerSlot(this, playerSlotOffset(index));
            if (player)
            {

                PLAYER* const owned = player;


                using PlayerDeletingDestructorFn = PLAYER* (__thiscall*)(PLAYER*, unsigned char);
                void** const vtable = *reinterpret_cast<void***>(owned);
                (void)reinterpret_cast<PlayerDeletingDestructorFn>(vtable[0])(owned, 1u);
            }
        }

        DestroyGlobalSpriteCollectorForApplicationDestructor();


        delete core::g_startupStringsIniPathOwner;

        if (as1::sound::Engine* const soundEngine = as1::sound::g_globalSoundEngine)
        {


            soundEngine->Destroy();
            ::operator delete(soundEngine);
        }

        if (CONSTANT* const constants = g_baseConstants)
            delete constants;

        delete core::g_startupRegistryPathOwner;

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        for (int index = appVidTable.count() - 1; index >= 0; --index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid)
                continue;
            deleteVidThroughVirtualDestructor(vid);
            appVidTable.setSlotCell(index, nullptr);
        }
        appVidTable.setStoredCount(0);

        LOG::Write("Vid    release %i %i",
                   static_cast<int>(BASE_TEXTURE::TextureMemoryBytes()),
                   g_vidMemoryInUse);

        if (GRAPH* const graph = Graph)
        {
            graph->~GRAPH();
            ::operator delete(static_cast<void*>(graph));
        }

        if (short* const grid = core::ApplicationTerrainGrid())
            ::operator delete(static_cast<void*>(grid));
        if (short* const tempGrid = core::ApplicationTempTerrainGrid())
            ::operator delete(static_cast<void*>(tempGrid));

        if (WEAPON* const weapons = core::ApplicationWeaponTable())
            ::operator delete(static_cast<void*>(weapons));

        delete as1::g_fileLogger;


        delete zs1::g_UserMngr;
        zs1::g_UserMngr = nullptr;
        delete zs1::g_DebugLog;
        zs1::g_DebugLog = nullptr;

        ::CoUninitialize();
        ::timeEndPeriod(1u);

        using TailSpriteList = core::List<SPRITE*>;
        reinterpret_cast<TailSpriteList*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::TailSpriteList)->~TailSpriteList();
        reinterpret_cast<MOUSETIPS*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::MouseTips)->~MOUSETIPS();
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups)->~GROUPS();


        {
            auto* const menuWords = reinterpret_cast<std::uint32_t*>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Menu);
            menuWords[0] = core::List<SPRITE*>::CurrentImageCoreListVtable();
            void* const menuItems = reinterpret_cast<void*>(static_cast<std::uintptr_t>(menuWords[3]));
            if (menuItems)
                ::operator delete(menuItems);
            menuWords[3] = 0u;
            menuWords[1] = 0u;
        }
        reinterpret_cast<RelationTable*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::RelationTable)->~RelationTable();
        reinterpret_cast<RESOURCE*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::DemoResource)->~RESOURCE();
        reinterpret_cast<SCRIPT*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime)->~SCRIPT();


        for (int pass = as1::core::ApplicationDrawDispatcherState::PassCount - 1; pass >= 0; --pass)
        {
            auto* const bucket = reinterpret_cast<core::ApplicationDrawPassBucket*>(
                reinterpret_cast<std::uint8_t*>(this) + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            bucket->~ApplicationDrawPassBucket();
        }

        resourceName(this).~STRING();
        previousMapName(this).~STRING();
        pendingCommand(this).~STRING();
        currentMapName(this).~STRING();
        applicationTitle(this).~STRING();
    }


    void ApplicationWin::drawApplicationDebugPass()
    {
        GRAPH* const graph = Graph;
        MAP* const map = Map;
        if (!graph || !map)
            return;

        const std::uint32_t applicationFlags = flags();

        if (applicationFlags & kDebugShowFps)
        {
            graph->DrawText(static_cast<float>(g_softwareClipLeft),
                            g_softwareClipTop + 1.0f,
                            "%i",
                            static_cast<int>(as1::core::DisplayedFramesPerSecond()));
        }


        if (applicationFlags & kDebugDrawSpriteBuckets)
        {
            for (int pass = 0; pass < 20; ++pass)
            {
                const as1::core::ApplicationDrawPassBucket& bucket =
                    embeddedDrawPassBucket(this, pass);
                for (int index = bucket.count() - 1; index >= 0; --index)
                {
                    SPRITE* const sprite = bucket.spriteAt(index);
                    if (sprite)
                        sprite->DrawRectangle();
                }
            }
        }

        if (!debugModeEnabled())
            return;

        if (applicationFlags & kDebugShowSoundCount)
        {
            const as1::sound::Engine* const engine = as1::sound::g_globalSoundEngine;
            const int playing = engine ? engine->playingSoundCount() : 0;
            graph->DrawText(g_softwareClipRight - 20.0f,
                            g_softwareClipTop + 1.0f,
                            "%2i", playing);
        }

        if (applicationFlags & kDebugDrawTerrainGrid)
        {


            const short* const permanent = map->terrainGrid();
            const short* const temporary = core::ApplicationTempTerrainGrid();
            const int gridX = map->terrainGridWidth();
            const int gridY = map->terrainGridHeight();
            if (gridX > 1 && gridY > 1 && permanent)
            {
                const core::ApplicationDrawDispatcherState& drawState =
                    core::GlobalApplicationDrawDispatcherState();
                const float cameraX = drawState.cameraShiftX();
                const float cameraY = drawState.cameraShiftY();
                const float viewLeft = static_cast<float>(graph->ViewXMin());
                const float viewRight = static_cast<float>(graph->ViewXMax());
                const float viewTop = static_cast<float>(graph->ViewYMin());
                const float viewBottom = static_cast<float>(graph->ViewYMax());

                for (int y = 1; y < gridY; ++y)
                {
                    for (int x = 1; x < gridX; ++x)
                    {
                        const std::size_t index =
                            static_cast<std::size_t>(x + y * gridX);
                        int z1 = static_cast<int>(permanent[index]);
                        int z0 = static_cast<int>(permanent[index - 1u]);
                        if (temporary)
                        {
                            const int temp1 = static_cast<int>(temporary[index]);
                            const int temp0 = static_cast<int>(temporary[index - 1u]);
                            if (temp1 > z1) z1 = temp1;
                            if (temp0 > z0) z0 = temp0;
                        }

                        const float x1 = static_cast<float>(8 * x + 4) - cameraX;
                        const float y1 = static_cast<float>(8 * y + 4 - z1) - cameraY;
                        const float x0 = static_cast<float>(8 * x - 4) - cameraX;
                        const float y0 = static_cast<float>(8 * y + 4 - z0) - cameraY;

                        const bool firstVisible =
                            x1 >= viewLeft && x1 < viewRight &&
                            y1 >= viewTop && y1 < viewBottom;
                        const bool secondVisible =
                            x0 >= viewLeft && x0 < viewRight &&
                            y0 >= viewTop && y0 < viewBottom;
                        if (firstVisible || secondVisible)
                            graph->Line(x1, y1, x0, y0, g_colorGray.color);
                    }
                }
            }
        }

        if (applicationFlags & kDebugDrawCurrentSprite)
        {


            const float mouseX = inputState(this).clientX;
            const float mouseY = inputState(this).clientY;
            auto* const application = reinterpret_cast<as1::core::Application*>(this);
            SPRITE* selectedSprite = application->findSpriteAtPointByFilter(
                0x00400000, mouseX, mouseY);
            if (!selectedSprite)
                selectedSprite = application->findSpriteAtPointByFilter(
                    0x00008000, mouseX, mouseY);
            if (!selectedSprite)
                selectedSprite = embeddedMenu(this).selectedSprite();
            if (!selectedSprite)
                selectedSprite = map->flagmanSpriteForPlayer(
                    static_cast<int>(activeStartupPlayerIndex()));
            if (selectedSprite)
                selectedSprite->DrawDebugOverlay();
        }

        if (applicationFlags & kDebugDrawScrollBox)
        {

            map->groupOwner().DrawNumber();
        }

        if (applicationFlags & kDebugDrawAuxiliaryList)
        {


            core::g_rMap.DebugDraw();
        }
    }


    void ApplicationWin::runCommandLineMap(char* ownedCommandLine)
    {

        STRING loadName;
        loadName.AdoptOwnedStorage(ownedCommandLine);

        setFlags(flags() | application_flags::MapLoading);
        if (loadName.isEmpty())
            return;

        MAP* const map = Map;
        if (!map)
            return;

        if (core::ApplicationMapWidth() != 0.0f ||
            core::ApplicationMapHeight() != 0.0f)
        {
            if (debugModeEnabled())
            {
                if (GRAPH* const graph = Graph)
                    graph->DrawDebugText("Release previous map");
            }
            map->release();
        }

        if (!map->demoResource().isOpen() &&
            map->demoResource().openFile(&loadName, RESOURCE::ResTypes::DEMO) == 0)
        {
            readStringLineFromStream(loadName, &map->demoResource());
            setFlags(flags() | application_flags::DemoUseResource);
        }

        RESOURCE mapResource;


        const GamePath mapPath = loadName;
        if (mapResource.openFile(&mapPath, RESOURCE::ResTypes::MAP) != 0)
        {
            LOG::Write("!!!ERROR!!!LOAD: Invalid map file %s", loadName.c_str());
            return;
        }

        if (Mouse)
            Mouse->Disable();

        assignStringFromString(previousMapName(this), currentMapName(this));
        assignStringFromString(currentMapName(this), loadName);
        map->data().fileName = loadName;


        while (!map->data().sprites.empty())
        {
            (void)map->data().sprites.back().release();
            map->data().sprites.pop_back();
        }
        map->data().scrollMinXY = VECTOR2{};
        map->data().scrollMaxXY = VECTOR2{};
        map->data().originalShiftXY = VECTOR2{};
        map->data().shiftDeltaXY = VECTOR2{};

        std::uint32_t demoStart = static_cast<std::uint32_t>(::timeGetTime());
        if ((flags() & application_flags::DemoUseResource) != 0u)
            map->demoResource().read(&demoStart, sizeof(demoStart));
        std::srand(static_cast<unsigned int>(demoStart));

        if (debugModeEnabled())
        {
            if (GRAPH* const graph = Graph)
                graph->DrawDebugText("Load extra vid");
        }

        (void)map->loadVids(&mapResource);


        SPRITE* const endSpriteMarker = reinterpret_cast<SPRITE*>(~static_cast<std::uintptr_t>(0));


        const bool hasGraphSection = (mapResource.GoBegin(RESOURCE::ResTypes::GRAPH) == 0);
        map->data().useLegacyCompactSpriteRecords = !hasGraphSection;
        if (hasGraphSection)
        {
            if (debugModeEnabled() && map->data().graph)
                map->data().graph->DrawDebugText("Load graph parameters");
            if (map->data().graph)
                map->data().graph->LoadParameters(&mapResource);
        }

        const int headResult = map->data().useLegacyCompactSpriteRecords
            ? mapResource.GoBegin(RESOURCE::ResTypes::HEAD)
            : mapResource.GoNext(RESOURCE::ResTypes::HEAD);
        if (headResult != 0)
        {
            LOG::ResourceError("MAP", 11, "HEAD", 0);
            return;
        }

        if (!map->data().useLegacyCompactSpriteRecords)
        {
            mapResource.read(&map->data().sizeXY.x, 4);
            mapResource.read(&map->data().sizeXY.y, 4);
            mapResource.read(&map->data().shiftXY.x, 4);
            mapResource.read(&map->data().shiftXY.y, 4);
            mapResource.read(&map->data().currentTime, 4);
            mapResource.read(&map->data().version, 4);

            if (map->data().version <= 9)
            {
                int ix = 0, iy = 0, sx = 0, sy = 0;
                std::memcpy(&ix, &map->data().sizeXY.x, 4);
                std::memcpy(&iy, &map->data().sizeXY.y, 4);
                std::memcpy(&sx, &map->data().shiftXY.x, 4);
                std::memcpy(&sy, &map->data().shiftXY.y, 4);
                map->data().sizeXY.x = static_cast<float>(ix);
                map->data().sizeXY.y = static_cast<float>(iy);
                map->data().shiftXY.x = static_cast<float>(sx);
                map->data().shiftXY.y = static_cast<float>(sy);
            }
        }
        else
        {
            int ix = 0;
            int iy = 0;
            std::int16_t sx = 0;
            std::int16_t sy = 0;
            mapResource.read(&ix, 4);
            mapResource.read(&iy, 4);
            mapResource.read(&sx, 2);
            mapResource.read(&sy, 2);
            mapResource.read(&map->data().currentTime, 4);
            mapResource.read(&map->data().version, 4);
            if (map->data().graph)
                map->data().graph->OldLoadParameters(&mapResource);
            map->data().sizeXY.x = static_cast<float>(ix);
            map->data().sizeXY.y = static_cast<float>(iy);
            map->data().shiftXY.x = static_cast<float>(sx);
            map->data().shiftXY.y = static_cast<float>(sy);
            map->data().useLegacyCompactSpriteRecords = true;
        }

        core::SetApplicationMapWidth(map->data().sizeXY.x);
        core::SetApplicationMapHeight(map->data().sizeXY.y);

        if (!map->data().useLegacyCompactSpriteRecords)
        {
            core::SetPreviousWorldTimeMilliseconds(1u);
            core::SetCurrentTimeMilliseconds(10u);

            std::uint32_t loadFlags = flags();
            if ((loadFlags & application_flags::DemoUseResource) == 0u && map->demoResource().isOpen())
            {
                loadFlags |= application_flags::DemoWriteToResource;
                setFlags(loadFlags);
                map->demoResource().BeginSection(RESOURCE::ResTypes::DEMO);
                const unsigned mapNameBytes = static_cast<unsigned>(map->data().fileName.str().size() + 1u);
                map->demoResource().write(map->data().fileName.c_str(), mapNameBytes);
                map->demoResource().write(&demoStart, sizeof(demoStart));
                const std::uint32_t worldClock = core::CurrentTimeMilliseconds();
                map->demoResource().write(&worldClock, sizeof(worldClock));
            }

            if ((loadFlags & application_flags::DemoUseResource) != 0u)
            {
                std::uint32_t playbackTime = core::CurrentTimeMilliseconds();
                map->demoResource().read(&playbackTime, sizeof(playbackTime));
                core::SetCurrentTimeMilliseconds(playbackTime);
            }
        }
        else
        {
            core::SetCurrentTimeMilliseconds(map->data().currentTime);
            core::SetPreviousWorldTimeMilliseconds(map->data().currentTime);
        }

        setWorldStartTime(core::CurrentTimeMilliseconds());
        if (!map->data().useLegacyCompactSpriteRecords)
        {
            LOG::Write("CurrentTime   =%-15u   sizeof(SPRITE)=%-8i Map version   =%i",
                       core::CurrentTimeMilliseconds(), static_cast<int>(sizeof(SPRITE)), map->data().version);
        }

        map->data().originalShiftXY = map->data().shiftXY;
        core::GlobalApplicationDrawDispatcherState().setCameraShiftX(map->data().shiftXY.x);
        core::GlobalApplicationDrawDispatcherState().setCameraShiftY(map->data().shiftXY.y);
        map->SetScrollBox(0.0f, 0.0f, map->data().sizeXY.x, map->data().sizeXY.y);
        map->data().shiftDeltaXY = VECTOR2{};
        map->setTerrainGridDimensions(terrainGridDimension(map->data().sizeXY.x),
                                      terrainGridDimension(map->data().sizeXY.y));

        if (!map->data().useLegacyCompactSpriteRecords && debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Create new hash table");
        {
            core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
            ReinitGlobalSpriteCollectorFromVidTable(core::ApplicationMapWidth(),
                                                  core::ApplicationMapHeight(),
                                                  appVidTable.slotData(),
                                                  appVidTable.count());
        }

        if (map->data().graph && !map->data().useLegacyCompactSpriteRecords)
        {
            map->SetShiftCoor(map->data().shiftXY.x + map->data().graph->screenWidth() * 0.5f,
                              map->data().shiftXY.y + map->data().graph->screenHeight() * 0.5f,
                              0);
        }


        if (!map->data().useLegacyCompactSpriteRecords && debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Load gridZ");
        map->ResetGroundZ();
        const int gridX = map->terrainGridWidth();
        const int gridY = map->terrainGridHeight();
        if (mapResource.GoNext(RESOURCE::ResTypes::GRID))
        {
            (void)mapResource.GoBegin(RESOURCE::ResTypes::ANY);
        }
        else
        {
            const int expectedBytes = 2 * gridX * gridY;
            if (short* const oldGrid = core::ApplicationTerrainGrid())
                ::operator delete(static_cast<void*>(oldGrid));
            core::SetApplicationTerrainGrid(nullptr);
            void* loadedGrid = nullptr;
            const int actualBytes = mapResource.SubLoad(&loadedGrid, nullptr);
            core::SetApplicationTerrainGrid(static_cast<short*>(loadedGrid));
            if (actualBytes != expectedBytes)
            {
                LOG::ResourceError("MAP", 4, "grid", actualBytes);
                map->ResetGroundZ();
            }
        }

        if (map->data().useLegacyCompactSpriteRecords)
        {
            if (map->data().graph)
                (void)map->data().graph->restoreDeviceRenderStates();
        }


        if (!map->data().useLegacyCompactSpriteRecords && debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Load hardware terrain");
        if (mapResource.GoNext(RESOURCE::ResTypes::SPRITE))
        {
            LOG::ResourceError("MAP", 11, "SPR ", 0);
            return;
        }
        int previousLayer = 0;
        while (true)
        {
            SPRITE* sprite = map->data().useLegacyCompactSpriteRecords
                ? map->OldLoadSprite(&mapResource)
                : map->LoadSprite(&mapResource, map->data().version);
            if (sprite == endSpriteMarker)
                break;

            if (!map->data().useLegacyCompactSpriteRecords)
            {
                if (debugModeEnabled() && sprite && map->data().graph)
                {
                    if (VID* const vid = sprite->Vid())
                    {
                        const int layer = vid->renderLayer();
                        if (layer != previousLayer)
                        {
                            previousLayer = layer;
                            const char* stage = "Load sprites";
                            if (layer == 0)
                                stage = "Load hardware terrain";
                            else if (layer == 1)
                                stage = "Build sprites in terrain";
                            else if (layer == 2)
                                stage = "Build sprites with alpha in terrain";
                            map->data().graph->DrawDebugText(stage);
                        }
                    }
                }
                if (mapResource.CurrentResourceSize() > 1000u &&
                    map->data().graph->DrawLoadBar(core::GlobalApplicationVidTable().slot(0), nullptr, 0, 0))
                {
                    if (as1::sound::Engine* const soundEngine = as1::sound::g_globalSoundEngine)
                        soundEngine->updateMusicStreamState();
                }
            }
        }

        as1::core::g_rMap.CreateAdditionalDots();


        if (!map->data().useLegacyCompactSpriteRecords && debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Load data for sprite");
        if (mapResource.GoNext(RESOURCE::ResTypes::SPRITEDATA))
        {
            LOG::ResourceError("MAP", 11, "SPRD", 0);
            return;
        }
        const int restoreOpcode = map->data().useLegacyCompactSpriteRecords
            ? SpriteActConst::ACT_RESTORE_OLD_MAP
            : SpriteActConst::ACT_RESTORE;
        while (true)
        {
            SPRITE* const sprite = map->ReadPointer(&mapResource);
            if (sprite == endSpriteMarker)
                break;

            if (!map->data().useLegacyCompactSpriteRecords &&
                mapResource.CurrentResourceSize() > 1000u &&
                map->data().graph->DrawLoadBar(core::GlobalApplicationVidTable().slot(0), nullptr, 0, 0))
            {
                if (as1::sound::Engine* const soundEngine = as1::sound::g_globalSoundEngine)
                    soundEngine->updateMusicStreamState();
            }

            if (sprite)
            {
                sprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(restoreOpcode),
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(&mapResource)),
                    map->data().version,
                    0);
            }
            if (mapResource.GoNextSub(RESOURCE::ResTypes::SPRITEDATA))
                break;
        }

        if (!map->data().useLegacyCompactSpriteRecords)
        {
            if (debugModeEnabled() && map->data().graph)
                map->data().graph->DrawDebugText("Load players info");
            if (mapResource.GoNext(RESOURCE::ResTypes::PLAY))
            {
                LOG::ResourceError("MAP", 11, "PLAY", 0);
                return;
            }
            PLAYER* const players[4] = {
                playerSlotByIndex(0), playerSlotByIndex(1),
                playerSlotByIndex(2), playerSlotByIndex(3)
            };
            for (PLAYER* const player : players)
            {
                void** const vtable = *reinterpret_cast<void***>(player);
                typedef int (__thiscall *LoadPlayerMethod)(PLAYER*, RESOURCE*);
                (void)reinterpret_cast<LoadPlayerMethod>(vtable[3])(player, &mapResource);
            }

            if (debugModeEnabled() && map->data().graph)
                map->data().graph->DrawDebugText("Load groups info");
            if (mapResource.GoNext(RESOURCE::ResTypes::GROUP))
            {
                LOG::ResourceError("MAP", 11, "GROU", 0);
                return;
            }
            map->groupOwner().Load(&mapResource);
        }

        setFlags(flags() & ~application_flags::MapLoading);
        mapResource.close();
        map->relationTable().clear();

        LOG::Write("Vid    release %i %i",
                   static_cast<int>(BASE_TEXTURE::TextureMemoryBytes()),
                   g_vidMemoryInUse);

        if (debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Load script file");


        const STRING scriptBase = map->data().fileName.BeforeLast(".");
        STRING requestedLgc = scriptBase + ".lgc";
        STRING requestedLgd = scriptBase + ".lgd";
        STRING chosenPath(requestedLgc);
        std::FILE* probe = std::fopen(requestedLgc.c_str(), "rb");
        const bool hasLgc = (probe != nullptr);
        if (probe)
            std::fclose(probe);
        if (!hasLgc)
        {
            probe = std::fopen(requestedLgd.c_str(), "rb");
            if (probe)
            {
                std::fclose(probe);
                assignStringFromString(chosenPath, requestedLgd);
            }
        }
        (void)map->scriptRuntime().Load(chosenPath);

        if (debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Connect script function with VID");


        static constexpr const char* kGlobalFunctionNames[] = {
            "main",
            "TrainNotAmmo",
            "TrainNotPower",
            "TrainDamage",
            "TrainCreated",
            "TrainSplit",
            "TrainDestroy",
            "TrainDestroyPower",
            "TrainArrive",
            "???TrainNotArrive",
            "TrainAttacked",
            "DepoDestroy",
            "DepoBirth",
            "DepoAttacked",
            "DepoFree",
            "BuildingCapture",
            "MasterDestroy",
            "???",
            "???SuperWeaponWounded",
            "MineBlast",
            "MineRemove",
            "EnemyLinked",
            "TrainClash",
            "UnitCreated",
            "UnitDestroy"
        };

        const int functionCount = map->scriptRuntime().functionCount();
        for (int functionIndex = 0; functionIndex < functionCount; ++functionIndex)
        {
            const script::LogicFunctionRecord* const fnPtr = map->scriptRuntime().functionRecordAt(functionIndex);
            if (!fnPtr)
                continue;
            const script::LogicFunctionRecord& fn = *fnPtr;
            if (fn.flags != 3)
                continue;

            const std::string name = fn.name.str();
            for (std::size_t globalIndex = 0;
                 globalIndex < (sizeof(kGlobalFunctionNames) / sizeof(kGlobalFunctionNames[0]));
                 ++globalIndex)
            {
                if (name == kGlobalFunctionNames[globalIndex])
                    core::EvFunctionNumber[globalIndex] = functionIndex;
            }

            if (name.size() < 4 || name[0] != 'F' ||
                !std::isdigit(static_cast<unsigned char>(name[1])) ||
                !std::isdigit(static_cast<unsigned char>(name[2])) ||
                !std::isdigit(static_cast<unsigned char>(name[3])))
                continue;

            const int nVid3 = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
            int nVid = nVid3;
            std::size_t suffix = 0;
            if (name.size() >= 6 && name[4] == '_')
            {
                suffix = 5;
            }
            else if (name.size() >= 7 &&
                     std::isdigit(static_cast<unsigned char>(name[4])) &&
                     name[5] == '_')
            {
                nVid = nVid3 * 10 + (name[4] - '0');
                suffix = 6;
            }
            else
            {
                continue;
            }

            const std::string callbackName = name.substr(suffix);
            if (callbackName == "DAMAGE")
            {
                if (fn.value2 != 3)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    LOG::ResourceError("MAP", 4, detail.c_str(), fn.value2);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setDamageInterceptScriptFunction(functionIndex);
                continue;
            }
            if (callbackName == "DESTROY")
            {
                if (fn.value2 != 1)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    LOG::ResourceError("MAP", 4, detail.c_str(), fn.value2);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setScriptFunctionAt(VID::DestroyScriptFunctionIndex, functionIndex);
                continue;
            }
            if (callbackName == "COLLISION")
            {
                if (fn.value2 != 2)
                {
                    STRING detail("no parameters in functions '");
                    detail += fn.name;
                    detail += "'";
                    LOG::ResourceError("MAP", 4, detail.c_str(), fn.value2);
                    continue;
                }
                if (map->ValidateVid(nVid))
                    map->Vid(nVid)->setCollisionScriptFunction(functionIndex);
                continue;
            }

            if (callbackName.empty() ||
                !std::isdigit(static_cast<unsigned char>(callbackName[0])))
                continue;

            int nAnim = callbackName[0] - '0';
            if (callbackName.size() >= 2 &&
                std::isdigit(static_cast<unsigned char>(callbackName[1])))
                nAnim = nAnim * 10 + (callbackName[1] - '0');

            if (fn.value2 != 1)
            {
                STRING detail("no parameters in functions '");
                detail += fn.name;
                detail += "'";
                LOG::ResourceError("MAP", 4, detail.c_str(), fn.value2);
                continue;
            }
            if (map->ValidateVid(nVid) && nAnim < 0x11)
                map->Vid(nVid)->setScriptFunctionAt(nAnim, functionIndex);
        }

        const std::uint32_t stackFlags = flags();
        if ((stackFlags & application_flags::DemoUseResource) != 0u)
            map->scriptRuntime().readExecutionStackFromStream(&map->demoResource());
        else if ((stackFlags & application_flags::DemoWriteToResource) != 0u)
            map->scriptRuntime().writeExecutionStackToStream(&map->demoResource());

        core::RealCurrentTime = static_cast<std::uint32_t>(::timeGetTime());
        if (debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("Run scripts for create sprites");

        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        if ((flags() & 0x00000001u) == 0u)
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
                {
                    SPRITE* const sprite = bucket.spriteAt(cursor);
                    if (!sprite)
                        continue;
                    VID* const vid = sprite->Vid();
                    const int functionIndex = vid->birthScriptFunction();
                    if (functionIndex >= 0 &&
                        (flags() & application_flags::ScriptCallbacksDisabled) == 0u)
                    {
                        const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(sprite)));
                        (void)core::Application::callScriptFunction(functionIndex, spriteArg, 0, 0);
                    }
                }
            }
        }

        if ((flags() & application_flags::DemoUseResource) == 0u && Mouse)
            Mouse->Enable();
        if (debugModeEnabled() && map->data().graph)
            map->data().graph->DrawDebugText("");
    }

    void ApplicationWin::runCommandLine(char* ownedCommandLine)
    {
        runCommandLineMap(ownedCommandLine);
    }


    SPRITE* ApplicationWin::CreateSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
    {
        if (!vid || vid == EmptyVid)
            return nullptr;

        MAP* const owner = Map;
        if (!owner)
            return nullptr;

        VID* selectedVid = vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, xyz.x, xyz.y, xyz.z);

        const int liveLimit = selectedVid->unitLimit(0);
        if (liveLimit >= 0 && static_cast<int>(selectedVid->NoSprites()) >= liveLimit)
            return nullptr;

        SPRITE* sprite = nullptr;
        switch (selectedVid->spriteClassId())
        {
        case B_TERRAIN:
        case B_OBJECT:
            sprite = new TERRAIN(owner, selectedVid, xyz, direction, parent);
            break;
        case B_BUILDING:
            sprite = new BUILDING(owner, selectedVid, xyz, direction, parent);
            break;
        case B_BALL:
            sprite = new BALL(owner, selectedVid, xyz, direction, parent);
            break;
        case B_RAIL:
            sprite = new RAIL(owner, selectedVid, xyz, direction, parent);
            break;
        case B_DEPO:
            sprite = new DEPO(owner, selectedVid, xyz, direction, parent);
            break;
        case B_CIV_ROBOT:
            sprite = new CIV_ROBOT(owner, selectedVid, xyz, direction, parent);
            break;
        case B_ENGINE:
            sprite = new ENGINE(owner, selectedVid, xyz, direction, parent);
            break;
        case B_CREATURE:
            sprite = new CREATURE(owner, selectedVid, xyz, direction, parent);
            break;
        case B_BALLOON:
            sprite = new BALLOON(owner, selectedVid, xyz, direction, parent);
            break;
        default:
            return reinterpret_cast<as1::core::Application*>(this)->createSprite(
                selectedVid, xyz, direction, parent);
        }

        const std::uint32_t appFlags = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) + as1::core::application_layout::Flags);
        if (sprite && (appFlags & application_flags::MapLoading) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite)));
                reinterpret_cast<as1::core::Application*>(this)->callScriptFunctionInternal(
                    functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }


    bool ApplicationWin::dispatchWindowMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result)
    {

        result = 0;

        if ((flags() & kApplicationFramePumpFlag) != 0u)
        {


            const int inputHandled = inputState(this).handleWindowMessage(
                reinterpret_cast<std::uintptr_t>(hwnd),
                static_cast<std::uint32_t>(msg),
                static_cast<std::uint32_t>(wparam),
                static_cast<std::uint32_t>(lparam));
            if (inputHandled != 0)
            {
                result = 1;
                return true;
            }
        }

        GRAPH* const graph = Graph;

        if (msg > 0x211u)
        {
            if (msg == WM_EXITMENULOOP || msg == WM_EXITSIZEMOVE)
            {
                graph->leaveModalRenderState();
                as1::sound::g_globalSoundEngine->resumeMusicAndRestoreActiveBuffers();
            }
            else if (msg == WM_ENTERSIZEMOVE)
            {
                graph->enterModalRenderState();
                as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
            }
            return false;
        }

        if (msg == WM_ENTERMENULOOP)
        {
            graph->enterModalRenderState();
            as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
            return false;
        }

        if (msg > WM_ACTIVATEAPP)
        {
            if (msg == WM_SETCURSOR)
            {


                if ((flags() & kApplicationFramePumpFlag) != 0u && g_cursorRefreshPending != 0u)
                {
                    g_cursorRefreshPending = 0u;
                    if (Mouse && Mouse->cursorHandlesLoaded() == 0 && ::GetCursor() != nullptr)
                        ::SetCursor(nullptr);
                }
                return false;
            }

            if (msg == WM_SYSCOMMAND)
            {
                const std::uintptr_t command = static_cast<std::uintptr_t>(wparam);
                if ((command == 0xF000u || command == 0xF010u || command == 0xF030u || command == 0xF170u) &&
                    graph->fullscreenRequested())
                {
                    result = 1;
                    return true;
                }
            }
            return false;
        }

        if (msg == WM_ACTIVATEAPP)
        {
            const std::uint32_t oldFlags = flags();
            const bool wasActive = (oldFlags & kApplicationFramePumpFlag) != 0u;
            const bool becomesActive = wparam != 0;

            if (as1::sound::g_globalSoundEngine)
            {
                if (becomesActive && !wasActive)
                    as1::sound::g_globalSoundEngine->resumeMusicAndRestoreActiveBuffers();
                else if (!becomesActive && wasActive)
                    as1::sound::g_globalSoundEngine->pauseMusicAndStopActiveBuffers();
            }

            if (Mouse)
            {
                if (becomesActive && !wasActive)
                {
                    if (g_cursorActivationState == 0u)
                        Mouse->Enable();
                    else
                        ::SetCursor(nullptr);
                }
                else if (!becomesActive && wasActive)
                {
                    g_cursorActivationState = Mouse->cursorHandlesLoaded() == 0 ? 1u : 0u;
                    Mouse->Disable();
                }
            }

            setFlags((oldFlags & ~kApplicationFramePumpFlag) |
                     (becomesActive ? kApplicationFramePumpFlag : 0u));
            g_cursorRefreshPending = 1u;
            return false;
        }

        if (msg == WM_DESTROY)
        {
            if (!graph->fullscreenRequested())
            {
                RECT rect;
                ::GetWindowRect(mainWindowHandle(this), &rect);
                if (zs1::g_UserMngr)
                {


                    zs1::g_UserMngr->SetInt(true, "WindowPositionX", rect.left);
                    zs1::g_UserMngr->SetInt(true, "WindowPositionY", rect.top);
                }
            }
            mainWindowHandle(this) = nullptr;
            ::PostQuitMessage(0);
            return false;
        }

        if (msg == WM_PAINT)
        {
            LOG::Write("WM_PAINT");
            return false;
        }

        return false;
    }

    LRESULT CALLBACK applicationWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {

        ApplicationWin* const app = applicationWinInstance();
        LRESULT handledResult = 0;
        if (app->dispatchWindowMessage(hwnd, msg, wparam, lparam, handledResult))
        {
            return 1;
        }

        if (msg != WM_COMMAND)
        {
            const LRESULT defResult = ::DefWindowProcA(hwnd, msg, wparam, lparam);
            return defResult;
        }

        switch (static_cast<unsigned int>(LOWORD(wparam)))
        {
        case IDM_FILE_LOAD:
            if (debugModeEnabled())
            {
                static const char mapOpenFilter[] =
                    "Map Files\0*.map\0"
                    "Save Files\0*.sav\0"
                    "Demo Files\0*.dem\0"
                    "All Files\0*.*\0\0";

                char fileName[4096] = {};
                OPENFILENAMEA ofn{};
                ofn.lStructSize = 76u;
                ofn.hwndOwner = mainWindowHandle(app);
                ofn.hInstance = applicationInstanceHandle(app);
                ofn.lpstrFilter = mapOpenFilter;
                ofn.nFilterIndex = 1u;
                ofn.lpstrFile = fileName;
                ofn.nMaxFile = 4096u;
                ofn.lpstrInitialDir = "maps";
                ofn.Flags = 0x0008180Cu;
                ofn.lpstrDefExt = "map";

                GRAPH* const graph = Graph;
                graph->enterModalRenderState();
                const BOOL selectedFile = ::GetOpenFileNameA(&ofn);
                graph->leaveModalRenderState();

                STRING selected(selectedFile ? fileName : "");

                app->runCommandLine(selected.DetachOwnedStorage());
            }
            return 0;
        case IDM_FILE_SAVE:
            if (debugModeEnabled())
                app->saveMap(STRING("current.sav"));
            return 0;
        case IDM_FILE_EXIT:
            ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_OPTIONS_ACCEL:
        {
            STRING date;
            constructCurrentDateString(date);
            STRING screensAndDate;
            constructConcatenatedString(screensAndDate, "Screens\\", date.c_str());

            STRING datedPrefix;
            constructConcatenatedString(datedPrefix, screensAndDate.c_str(), " ");

            STRING time;
            constructCurrentTimeString(time);
            STRING dateAndTime;
            constructConcatenatedString(dateAndTime, datedPrefix.c_str(), time.c_str());

            STRING outputPath;
            constructConcatenatedString(outputPath, dateAndTime.c_str(), ".tga");
            outputPath.Replace(":", "h");
            outputPath.Replace(":", "m");
            outputPath.Replace(":", "s");

            STRING rootedOutputPath;
            constructConcatenatedString(rootedOutputPath, zs1::g_WindowsUserPath.c_str(), outputPath.c_str());
            assignStringFromString(outputPath, rootedOutputPath);

            GRAPH* const graph = Graph;
            graph->SaveTGA(&outputPath,
                           0,
                           0,
                           static_cast<int>(graph->screenWidth()),
                           static_cast<int>(graph->screenHeight()));
            return 0;
        }
        default:
            return 0;
        }
    }


    void ApplicationWin::saveMap(const as1::STRING& outputName)
    {

        RESOURCE output;
        if (std::strcmp(outputName.c_str(), STRING::SharedEmptyText()) == 0)
            return;

        static const char kTemporaryMapName[] = "tmp_del!.map";
        STRING& activeMapName = currentMapName(this);
        const bool replacingCurrentMap = std::strcmp(outputName.c_str(), activeMapName.c_str()) == 0;
        if (replacingCurrentMap)
        {


            ::MoveFileExA(activeMapName.c_str(), kTemporaryMapName, 9u);
            assignStringFromCString(activeMapName, kTemporaryMapName);
        }

        if (output.OpenForWrite(&outputName, RESOURCE::ResTypes::MAP) != 0)
        {
            LOG::Write("Can't open file %s", outputName.c_str());
            return;
        }

        bool copyObjectSections = false;
        for (int i = 0; i < physicalVidCount(this); ++i)
        {
            VID* const vid = physicalVidSlotUnchecked(this, i);
            if (vid && (vid->formatFlags() & 0x0200u) != 0u)
            {
                copyObjectSections = true;
                break;
            }
        }
        if (copyObjectSections)
        {
            RESOURCE previous;
            if (previous.openFile(&activeMapName, RESOURCE::ResTypes::MAP) == 0)
            {
                output.Copy(&previous, RESOURCE::ResTypes::WEAPON);
                output.Copy(&previous, RESOURCE::ResTypes::OBJECT);
                previous.close();
            }
            else
            {
                LOG::Write("Can't open file '%s', needed for save map", activeMapName.c_str());
            }
        }

        output.BeginSection(RESOURCE::ResTypes::GRAPH);
        Graph->SaveParameters(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::HEAD);
        const float sizeX = physicalFloatSlot(this, core::application_layout::MapExtentX);
        const float sizeY = physicalFloatSlot(this, core::application_layout::MapExtentY);
        const float shiftX = physicalFloatSlot(this, core::application_layout::CameraShiftX);
        const float shiftY = physicalFloatSlot(this, core::application_layout::CameraShiftY);
        output.write(&sizeX, 4u);
        output.write(&sizeY, 4u);
        output.write(&shiftX, 4u);
        output.write(&shiftY, 4u);
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        output.write(&now, 4u);

        const int storedVersion = 13;
        output.write(&storedVersion, 4u);
        output.EndSection();


        if (const short* const grid = physicalTerrainGrid(this))
        {
            const int gridWidth = physicalIntSlot(this, core::application_layout::TerrainGridWidth);
            const int gridHeight = physicalIntSlot(this, core::application_layout::TerrainGridHeight);
            bool anyGridValue = false;
            const int cells = gridWidth * gridHeight;
            for (int i = 0; i < cells; ++i)
            {
                if (grid[i] != 0)
                {
                    anyGridValue = true;
                    break;
                }
            }
            if (anyGridValue)
            {
                output.BeginSection(RESOURCE::ResTypes::GRID);
                output.write(grid, static_cast<unsigned>(2u * static_cast<unsigned>(cells)));
                output.EndSection();
            }
        }

        const BaseSpriteList<0>& frameList = embeddedFrameSpriteList(this);
        auto forEachSaveSprite = [&](const auto& fn)
        {
            for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                const core::ApplicationDrawPassBucket& bucket = embeddedDrawPassBucket(this, pass);
                for (int index = bucket.count() - 1; index >= 0; --index)
                {
                    SPRITE* const sprite = bucket.spriteAt(index);
                    if (!sprite || sprite->childBacklink() || frameList.contains(sprite))
                        continue;
                    fn(sprite);
                }
            }
        };

        output.BeginSection(RESOURCE::ResTypes::SPRITE);
        forEachSaveSprite([&](SPRITE* sprite) { sprite->serializeSpriteRecord(&output); });
        const std::int32_t spriteTerminator = -1;
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        forEachSaveSprite([&](SPRITE* sprite)
        {
            const std::size_t begin = output.position();
            output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
            const std::uint32_t spritePointerValue = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sprite) & 0xFFFFFFFFu);
            output.write(&spritePointerValue, 4u);
            sprite->Action(static_cast<int>(ActionCode::ACT_SAVE), reinterpret_cast<std::intptr_t>(&output), 0, 0);
            if (output.position() > begin + 5u)
                output.EndSection();
        });
        output.BeginSection(RESOURCE::ResTypes::SPRITEDATA);
        output.write(&spriteTerminator, 4u);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::PLAY);
        PLAYER* const players[4] = {
            playerPointerSlot(this, playerSlotOffset(0)),
            playerPointerSlot(this, playerSlotOffset(1)),
            playerPointerSlot(this, playerSlotOffset(2)),
            playerPointerSlot(this, playerSlotOffset(3))
        };
        for (PLAYER* const player : players)
            player->saveControlledSpriteReference(&output);
        output.EndSection();

        output.BeginSection(RESOURCE::ResTypes::GROUP);
        reinterpret_cast<GROUPS*>(reinterpret_cast<std::uint8_t*>(this) + core::application_layout::Groups)->Save(&output);
        output.EndSection();
        output.close();

        if (std::strcmp(activeMapName.c_str(), kTemporaryMapName) == 0)
        {

            ::DeleteFileA(kTemporaryMapName);
            assignStringFromString(activeMapName, outputName);
        }
    }



    bool ApplicationWin::pumpNativeMessages()
    {


        const std::uint32_t previousRealTime = as1::core::RealCurrentTime;
        std::uint32_t sampleForDelta = 0;
        do
        {
            sampleForDelta = static_cast<std::uint32_t>(::timeGetTime());
        }
        while (sampleForDelta == previousRealTime);

        std::uint32_t elapsed = sampleForDelta - previousRealTime;
        as1::core::PrevRealCurrentTime = previousRealTime;
        as1::core::RealCurrentTime = static_cast<std::uint32_t>(::timeGetTime());

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        as1::core::SetPreviousWorldTimeMilliseconds(current);
        if (elapsed > kFrameClampMs)
            elapsed = kFrameClampMs;

        const float tickScale = physicalFloatSlot(this, core::application_layout::TickScale);
        const long double scaled = static_cast<long double>(elapsed) * static_cast<long double>(tickScale);
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
        const std::uint32_t increment = static_cast<std::uint32_t>(converted);
        as1::core::SetCurrentTimeMilliseconds(current + increment);


        auto* const selfBytes = reinterpret_cast<std::uint8_t*>(this);
        std::uint32_t& fpsCounter = *reinterpret_cast<std::uint32_t*>(
            selfBytes + core::application_layout::FpsCounter);
        const std::uint32_t framesThisWindow = ++fpsCounter;
        const std::uint32_t fpsNow = as1::core::RealCurrentTime;
        std::uint32_t& lastFpsSample = as1::prev_second_time;
        if (fpsNow - lastFpsSample >= 1000u)
        {
            lastFpsSample = fpsNow;
            *reinterpret_cast<std::uint32_t*>(
                selfBytes + core::application_layout::Fps) = framesThisWindow;
            fpsCounter = 0u;
        }

        inputState(this).resetFrameState();

        if ((as1::core::CurrentTimeMilliseconds() & 3u) == 0u)
        {
            for (int pass = 0; pass < as1::core::ApplicationDrawDispatcherState::PassCount; ++pass)
                embeddedDrawPassBucket(this, pass).compactSparse();
        }

        if (flags() & kApplicationCommandLinePendingFlag)
        {

            setFlags(flags() & ~kApplicationCommandLinePendingFlag);
            STRING pendingCopy(pendingCommand(this));
            runCommandLine(pendingCopy.DetachOwnedStorage());
        }

        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return true;

            const bool translatedByAccelerator = mainWindowHandle(this) &&
                TranslateAcceleratorA(mainWindowHandle(this), acceleratorHandle(this), &msg) != 0;
            if (translatedByAccelerator)
                continue;

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return false;
    }


    int ApplicationWin::processDemoFrame()
    {

        int result = 1;
        MAP* const map = Map;
        RESOURCE& demo = map->demoResource();

        if ((flags() & application_flags::DemoUseResource) != 0u)
        {
            std::int32_t frameTime = -1;
            demo.read(&frameTime, sizeof(frameTime));

            if (frameTime != -1 && inputState(this).lastCode == 0u &&
                (inputState(this).flags & 0x05u) == 0u)
            {
                inputState(this).readRawState(&demo);

                const std::uint32_t recorded = static_cast<std::uint32_t>(frameTime);
                std::uint32_t baseRecorded = as1::core::g_demoRecordedTimeBaseMilliseconds;
                if (recorded - baseRecorded > kFrameClampMs)
                {
                    baseRecorded = recorded - kFrameClampMs;
                    as1::core::g_demoRecordedTimeBaseMilliseconds = baseRecorded;
                }

                const std::uint32_t realBase = as1::core::g_demoRealTimeBaseMilliseconds;
                std::uint32_t realElapsed = static_cast<std::uint32_t>(::timeGetTime()) - realBase;
                const std::uint32_t recordedElapsed = recorded - baseRecorded;
                if (recordedElapsed <= realElapsed)
                {
                    result = (as1::core::CurrentTimeMilliseconds() - recorded) <= kDemoFrameToleranceMs ? 1 : 0;
                }
                else
                {
                    do
                    {
                        realElapsed = static_cast<std::uint32_t>(::timeGetTime()) - realBase;
                    } while (recordedElapsed >= realElapsed);
                }

                as1::core::SetCurrentTimeMilliseconds(recorded);
                as1::core::g_demoRecordedTimeBaseMilliseconds = recorded;
                as1::core::g_demoRealTimeBaseMilliseconds = static_cast<std::uint32_t>(::timeGetTime());
            }
            else
            {
                STRING nextMap;

                (void)demo.GoNext(RESOURCE::ResTypes::DEMO);
                demo.shift(static_cast<int>(demo.CurrentResourceSize()));
                readStringLineFromStream(nextMap, &demo);

                if (nextMap.isEmpty())
                {
                    PostMessageA(mainWindowHandle(this), WM_CLOSE, 0, 0);
                }
                else
                {
                    demo.close();
                    Mouse->Enable();
                    setFlags(flags() & ~application_flags::DemoUseResource);
                    setFlags(flags() | kApplicationCommandLinePendingFlag);
                    assignStringFromString(pendingCommand(this), nextMap);
                }
                return 0;
            }
        }

        if ((flags() & application_flags::DemoWriteToResource) != 0u)
        {
            const std::uint32_t worldTime = as1::core::CurrentTimeMilliseconds();
            demo.write(&worldTime, sizeof(worldTime));
            inputState(this).writeRawState(&demo);
        }
        return result;
    }

    float ApplicationWin::mapExtentX() const noexcept
    {

        return physicalFloatSlot(this, core::application_layout::MapExtentX);
    }

    float ApplicationWin::mapExtentY() const noexcept
    {

        return physicalFloatSlot(this, core::application_layout::MapExtentY);
    }

    PLAYER* ApplicationWin::playerSlotByIndex(int index) const noexcept
    {

        return playerPointerSlot(this, playerSlotOffset(index));
    }



    SPRITE* ApplicationWin::activeAuxiliarySprite() const noexcept
    {


        const std::uint32_t index = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) +
            core::application_layout::ActivePlayerIndex);
        PLAYER* const player = *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const std::uint8_t*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(index & 3u) * core::application_layout::PlayerSlotStride);
        return *reinterpret_cast<SPRITE* const*>(
            reinterpret_cast<const std::uint8_t*>(player) + 0x24u);
    }


    as1::STRING* ApplicationWin::buildMouseTipText(as1::STRING* out)
    {

        as1::STRING text;
        if ((flags() & kApplicationModalDispatchFlag) == 0u)
        {
            PLAYER* const player = startupPlayerSlotByIndex(
                static_cast<int>(activeStartupPlayerIndex()));
            as1::STRING playerText;
            player->getAuxiliaryUnitName(&playerText);
            copyConstructString(text, playerText);
        }

        MENU& list = embeddedMenu(this);
        if (text.isEmpty() && list.selectedSprite())
        {
            char number[0x80]{};
            _itoa(list.NVidUnderCursor(), number, 10);

            as1::STRING nvidText;
            constructStringFromCString(nvidText, number);
            as1::STRING menuVid;
            constructConcatenatedString(menuVid, "MenuVid", nvidText.c_str());

            as1::STRING defaultValue;
            as1::STRING section("MouseTips");
            as1::STRING allDirKey;
            constructConcatenatedString(allDirKey, menuVid.c_str(), "AllDir");
            as1::STRING profileValue;
            as1::core::profile_p::readProfileStringInto(
                profileValue,
                *as1::core::g_startupStringsIniPathOwner,
                section,
                allDirKey,
                defaultValue);
            copyConstructString(text, profileValue);

            if (text.isEmpty())
            {
                _itoa(list.NDirUnderCursor(), number, 10);
                as1::STRING directionText;
                constructStringFromCString(directionText, number);
                as1::STRING dirPrefix;
                constructConcatenatedString(dirPrefix, menuVid.c_str(), "Dir");
                as1::STRING dirKey;
                constructConcatenatedString(dirKey, dirPrefix.c_str(), directionText.c_str());
                as1::STRING dirProfileValue;
                as1::core::profile_p::readProfileStringInto(
                    dirProfileValue,
                    *as1::core::g_startupStringsIniPathOwner,
                    section,
                    dirKey,
                    defaultValue);
                copyConstructString(text, dirProfileValue);
            }
        }

        copyConstructString(*out, text);
        return out;
    }


    void MOUSETIPS::Tact(as1::input::InputMessageState* input) noexcept
    {
        ApplicationWin* const app = applicationFromMouseTips(this);
        (void)cachedTooltipText();
        const std::uint32_t now = as1::core::RealCurrentTime;
        if (g_tooltipLastClientX != input->clientX ||
            g_tooltipLastClientY != input->clientY)
        {
            g_tooltipLastUpdateTime = now;
            g_tooltipLastClientX = input->clientX;
            g_tooltipLastClientY = input->clientY;
        }

        const CONSTANT* const constants = as1::g_baseConstants;
        const std::uint32_t idleThreshold = constants->raw[0x34u / sizeof(DWORD)];
        if (now - g_tooltipLastUpdateTime <= idleThreshold ||
            (input->flags & 1u) != 0u ||
            input->lastCode != 0u ||
            (applicationMenu().controlFlags() & 1u) != 0u ||
            (app->flags() & kMapSelectSpriteUnderCursorFlag) == 0u)
        {
            Clear();
            return;
        }

        if (!tip)
        {
            as1::core::ApplicationVidTable& table = as1::core::GlobalApplicationVidTable();
            VID* tooltipVid = EmptyVid;
            if (table.count() > 6)
            {
                if (VID* const tooltipTemplateVid = table.slot(6))
                    tooltipVid = tooltipTemplateVid;
            }

            as1::STRING text;
            app->buildMouseTipText(&text);
            copyConstructString(cachedTooltipText(), text);
            if (tooltipVid == EmptyVid || cachedTooltipText().isEmpty())
                return;

            float x = input->clientX + 5.0f;
            float y = input->clientY - tooltipVid->sizeY() + 3000.0f - 10.0f;
            GRAPH* const graph = Graph;
            const float halfHeight = tooltipVid->sizeY() * 0.5f;
            if (!(static_cast<float>(graph->ViewYMin()) < (halfHeight + y - 3000.0f)))
                y = halfHeight + input->clientY + 3010.0f;

            const std::size_t glyphCount = std::strlen(cachedTooltipText().c_str()) + 2u;
            const float lineRight = static_cast<float>(glyphCount) * tooltipVid->sizeX() + x;
            if (lineRight > static_cast<float>(graph->ViewXMax()))
                x = static_cast<float>(graph->ViewXMax()) -
                    static_cast<float>(glyphCount) * tooltipVid->sizeX();

            MAP* const map = Map;
            SPRITE* const created = map->CreateSprite(
                tooltipVid, VECTOR{x, y, 3000.0f}, ANGLE(static_cast<unsigned char>(0)), nullptr, false);
            tip = created;
            if (!created)
                return;

            as1::STRING open;
            constructConcatenatedString(open, "{", cachedTooltipText().c_str());
            as1::STRING wrapped;
            constructConcatenatedString(wrapped, open.c_str(), "}");
            created->dispatchVirtualAction(ActionCode::ACT_SET_TEXT,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(&wrapped) & 0xFFFFFFFFu),
                0,
                0);
            return;
        }

        if (now - g_tooltipLastUpdateTime > idleThreshold + 500u)
        {
            g_tooltipLastUpdateTime += 500u;
            as1::STRING current;
            app->buildMouseTipText(&current);
            if (std::strcmp(current.c_str(), cachedTooltipText().c_str()) != 0)
                Clear();
        }
    }


    void ApplicationWin::updateCameraFromInput()
    {

        GRAPH* graph = Graph;
        MAP* map = Map;

        const CONSTANT* constants = as1::g_baseConstants;
        const float maxX = dwordAsFloat(constants->raw[0]);
        const float maxY = dwordAsFloat(constants->raw[1]);
        const float graphWidth = graph->screenWidth();
        const float graphHeight = graph->screenHeight();
                const float graphRight = graph->viewportRight();
        const float graphBottom = graph->viewportBottom();

        const float cameraShiftX = physicalFloatSlot(this, core::application_layout::CameraShiftX);
        const float cameraShiftY = physicalFloatSlot(this, core::application_layout::CameraShiftY);

        const std::uint32_t mode = physicalDwordSlot(this, core::application_layout::ScrollType);
        float& velocityX = as1::core::g_shiftSpeedX;
        float& velocityY = as1::core::g_shiftSpeedY;
        const std::uint32_t inputFlags = inputState(this).flags;
        const float clientX = inputState(this).clientX;
        const float clientY = inputState(this).clientY;

        if ((mode & 0x21u) != 0)
        {
            if (cameraLessEqualOrUnordered(clientX, kCameraEdgeThreshold) && (mode & 0x01u) != 0)
            {
                if (cameraLessOrUnordered(-maxX, velocityX))
                    velocityX -= kCameraAccelerationX;
            }
            else if ((graphRight - kCameraEdgeThreshold) > clientX && (mode & 0x01u) != 0)
            {

                if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraLessOrUnordered(-maxX, velocityX))
                        velocityX -= kCameraAccelerationX;
                }
                else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraLessOrUnordered(velocityX, maxX))
                        velocityX += kCameraAccelerationX;
                }
                else
                    velocityX = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (cameraLessOrUnordered(velocityX, maxX))
                    velocityX += kCameraAccelerationX;
            }
            else if ((inputFlags & 0x80u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraLessOrUnordered(-maxX, velocityX))
                    velocityX -= kCameraAccelerationX;
            }
            else if ((inputFlags & 0x0100u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraLessOrUnordered(velocityX, maxX))
                    velocityX += kCameraAccelerationX;
            }
            else
                velocityX = 0.0f;

            if (cameraLessEqualOrUnordered(clientY, kCameraEdgeThreshold) && (mode & 0x01u) != 0)
            {
                if (cameraLessOrUnordered(-maxY, velocityY))
                    velocityY -= kCameraAccelerationY;
            }
            else if ((graphBottom - kCameraEdgeThreshold) > clientY && (mode & 0x01u) != 0)
            {
                if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraLessOrUnordered(-maxY, velocityY))
                        velocityY -= kCameraAccelerationY;
                }
                else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
                {
                    if (cameraLessOrUnordered(velocityY, maxY))
                        velocityY += kCameraAccelerationY;
                }
                else
                    velocityY = 0.0f;
            }
            else if ((mode & 0x01u) != 0)
            {
                if (cameraLessOrUnordered(velocityY, maxY))
                    velocityY += kCameraAccelerationY;
            }
            else if ((inputFlags & 0x0400u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraLessOrUnordered(-maxY, velocityY))
                    velocityY -= kCameraAccelerationY;
            }
            else if ((inputFlags & 0x0200u) != 0 && (mode & 0x20u) != 0)
            {
                if (cameraLessOrUnordered(velocityY, maxY))
                    velocityY += kCameraAccelerationY;
            }
            else
                velocityY = 0.0f;
        }
        else
        {
            velocityX = 0.0f;
            velocityY = 0.0f;
        }

        SPRITE* target = reinterpret_cast<MAP*>(this)->flagmanSpriteForPlayer(static_cast<int>(activeStartupPlayerIndex()));


        const bool noVelocity = velocityX == 0.0f && velocityY == 0.0f;
        if (target && (mode & 0x04u) != 0 && noVelocity)
        {
            float targetDeltaX = target->X() - cameraShiftX;
            targetDeltaX -= graphWidth * 0.5f;
            velocityX = targetDeltaX / 1000.0f;

            float targetDeltaY = target->Y() - target->Z();
            targetDeltaY -= cameraShiftY;
            targetDeltaY -= graphHeight * 0.5f;
            velocityY = targetDeltaY / 1000.0f;
        }
        else if (target && (mode & 0x08u) != 0 && noVelocity)
        {


            float pointerX = graphWidth - 640.0f;
            if (!(pointerX > clientX))
                pointerX = cameraMinss(640.0f, clientX);
            float pointerY = graphHeight - 480.0f;
            if (!(pointerY > clientY))
                pointerY = cameraMinss(480.0f, clientY);

            float pointerTargetX = target->X() - cameraShiftX;
            pointerTargetX += pointerX;
            pointerTargetX *= 0.5f;
            float pointerTargetY = target->Y() - target->Z();
            pointerTargetY -= cameraShiftY;
            pointerTargetY += pointerY;
            pointerTargetY *= 0.5f;

            velocityX = graphWidth * 0.5f - pointerTargetX;
            velocityX /= -1000.0f;
            velocityX *= 4.0f;
            velocityY = graphHeight * 0.5f - pointerTargetY;
            velocityY /= -1000.0f;
            velocityY *= 4.0f;
        }
        else if (target && (mode & 0x50u) == 0x50u && noVelocity)
        {


            map->SetShiftCoor(target->X(),
                              target->Y() - target->Z(),
                              0);
            return;
        }
        else if (target && (mode & 0x10u) != 0 && noVelocity)
        {


            map->SetShiftCoor(target->X(), cameraShiftY, 0);
            return;
        }
        else if (target && (mode & 0x40u) != 0 && noVelocity)
        {

            map->SetShiftCoor(cameraShiftX, target->Y() - target->Z(), 0);
            return;
        }

        const std::uint32_t current = as1::core::CurrentTimeMilliseconds();
        const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
        const std::uint32_t deltaMs = current - previous;
        const std::int32_t dx = convertCameraElapsedScaleToInt32(deltaMs, velocityX);
        const std::int32_t dy = convertCameraElapsedScaleToInt32(deltaMs, velocityY);
        float centerX = graphWidth * 0.5f;
        centerX += cameraShiftX;
        centerX += static_cast<float>(dx);
        float centerY = graphHeight * 0.5f;
        centerY += cameraShiftY;
        centerY += static_cast<float>(dy);
        map->SetShiftCoor(centerX, centerY, 0);
    }

} }
