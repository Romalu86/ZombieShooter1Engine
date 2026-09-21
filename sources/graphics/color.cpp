#include "graphics/color.h"

namespace as1
{


    Color g_colorBlack(0, 0, 0);
    Color g_colorBlue(0, 0, 255);
    Color g_colorYellow(255, 255, 0);
    Color g_colorRed(255, 0, 0);
    Color g_colorGray(128, 128, 128);
    Color g_colorLightBlue(128, 128, 255);
    Color g_colorGreen(0, 255, 0);
    Color g_colorWhite(255, 255, 255);


    Color::Color(int red, int green, int blue) noexcept
    {
        if (red < 0) red = 0; else if (red > 255) red = 255;
        if (green < 0) green = 0; else if (green > 255) green = 255;
        if (blue < 0) blue = 0; else if (blue > 255) blue = 255;

        color = 0xFF000000u |
                (static_cast<DWORD>(red) << 16u) |
                (static_cast<DWORD>(green) << 8u) |
                static_cast<DWORD>(blue);
    }


    Color::Color(const Color& other) noexcept
        : color(other.color)
    {
    }


    Color::Color(int alpha, int red, int green, int blue) noexcept
    {
        if (alpha < 0) alpha = 0; else if (alpha > 255) alpha = 255;
        if (red < 0) red = 0; else if (red > 255) red = 255;
        if (green < 0) green = 0; else if (green > 255) green = 255;
        if (blue < 0) blue = 0; else if (blue > 255) blue = 255;

        color = (static_cast<DWORD>(alpha) << 24u) |
                (static_cast<DWORD>(red) << 16u) |
                (static_cast<DWORD>(green) << 8u) |
                static_cast<DWORD>(blue);
    }


    Color Color::AlphaAdd(Color other, unsigned int alpha) noexcept
    {
        ++alpha;
        const unsigned int inverse = 256u - alpha;
        const unsigned int red =
            (alpha * ((other.color >> 16u) & 0xFFu) + inverse * ((color >> 16u) & 0xFFu)) >> 8u;
        const unsigned int green =
            (alpha * ((other.color >> 8u) & 0xFFu) + inverse * ((color >> 8u) & 0xFFu)) >> 8u;
        const unsigned int blue =
            (alpha * (other.color & 0xFFu) + inverse * (color & 0xFFu)) >> 8u;

        Color result(static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue));
        *this = &result;
        return *this;
    }
}
