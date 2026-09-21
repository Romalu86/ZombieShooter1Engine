#pragma once

#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1
{
    class Color
    {
    public:
        DWORD color;

        Color() noexcept = default;


        Color(int alpha, int red, int green, int blue) noexcept;


        Color(int red, int green, int blue) noexcept;


        Color(const Color& other) noexcept;

        unsigned int Red() const noexcept { return (color >> 16u) & 0xFFu; }
        unsigned int Green() const noexcept { return (color >> 8u) & 0xFFu; }
        unsigned int Blue() const noexcept { return color & 0xFFu; }
        unsigned int Alpha() const noexcept { return color >> 24u; }


        Color AlphaAdd(Color other, unsigned int alpha) noexcept;

        const Color* operator=(const Color* other) noexcept
        {
            color = other->color;
            return this;
        }
    };


    extern Color g_colorBlack;
    extern Color g_colorBlue;
    extern Color g_colorYellow;
    extern Color g_colorRed;
    extern Color g_colorGray;
    extern Color g_colorLightBlue;
    extern Color g_colorGreen;
    extern Color g_colorWhite;


}
