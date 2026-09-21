#pragma once

#include "core/as_string.h"

#include <cstddef>
#include <cstdint>

namespace as1
{


    class REGISTRY
    {
    public:
        REGISTRY() = default;
        explicit REGISTRY(const STRING& registryPath);

        const STRING* Path() noexcept;
        STRING& mutablePath() noexcept { return m_path; }

    private:
        STRING m_path;
    };
}

namespace as1 { namespace core
{
    constexpr std::uint32_t StartupVSyncFlag = 1u << 1;
    constexpr std::uint32_t StartupDialogFullscreenFlag = 1u << 2;

    struct VideoConfiguration
    {
        int device = 0;
        int screenX = 640;
        int screenY = 480;
        int colorDepth = 32;
        bool fullscreen = true;
        bool vsync = true;
        int windowPositionX = 0;
        int windowPositionY = 0;
    };

    struct FontConfiguration
    {
        STRING face{"Arial"};
        int sizeX = 0;
        int sizeY = 8;
    };

    struct SoundConfiguration
    {
        bool highQuality = false;
    };

    struct ControlConfiguration
    {
        STRING left{"\x25"};
        STRING right{"\x27"};
        STRING up{"\x26"};
        STRING down{"\x04\x38"};
        STRING firstAction{"LBUTTON"};
        STRING secondAction{"RBUTTON"};
        STRING previousWeapon{"["};
        STRING nextWeapon{"]"};
        std::uint32_t relative = 0;
    };

    struct StartupSettingsBlock
    {
        char title[0x100];
        std::uint32_t allowedWidths[32];
        std::uint32_t allowedHeights[32];
        std::uint32_t allowedColorBits[8];
        std::uint32_t flags;
        std::int32_t device;
        std::int32_t screenWidth;
        std::int32_t screenHeight;
        std::int32_t colorDepth;
        std::int32_t fullscreen;
    };


    StartupSettingsBlock& StartupSettings() noexcept;

    struct StartupConfiguration
    {
        STRING configPath;
        STRING stringsPath;
        STRING applicationName{"ZombieShooter"};
        STRING applicationTitle{"ZombieShooter"};
        STRING registryPath{"SOFTWARE\\Sigma\\ZombieShooter"};
        STRING resourceRoot{"."};
        STRING objectsResource{"objects.res"};
        STRING startMap{"maps\\logo.map"};
        bool showStartDialog = false;
        bool startDialogFullscreen = false;
        int debugMode = 0;
        int drawFps = 0;
        int drawPresentation = 1;
        int noSysMenu = 0;
        unsigned int startupFlags = 0;
        VideoConfiguration video;
        FontConfiguration font;
        SoundConfiguration sound;
        ControlConfiguration control;
    };

    extern STRING* g_startupStringsIniPathOwner;
    extern REGISTRY* g_startupRegistryPathOwner;

} }
