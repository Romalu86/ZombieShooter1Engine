#include "gamma.h"
#include "graphics/color.h"

#include <mmintrin.h>

namespace as1
{
    namespace
    {
        BYTE clampByteImpl(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return static_cast<BYTE>(value);
        }

        DWORD packARGB(BYTE a, BYTE r, BYTE g, BYTE b)
        {
            return (static_cast<DWORD>(a) << 24u) |
                   (static_cast<DWORD>(r) << 16u) |
                   (static_cast<DWORD>(g) << 8u)  |
                   static_cast<DWORD>(b);
        }

        BYTE satAddByte(BYTE a, BYTE b)
        {
            const unsigned int v = static_cast<unsigned int>(a) + static_cast<unsigned int>(b);
            return static_cast<BYTE>(v > 255u ? 255u : v);
        }
    }


    Gamma::Gamma(const Gamma& other) noexcept
        : first(other.first), second(other.second)
    {
    }


    Gamma::Gamma(Color diffuseColor, Color specularColor) noexcept
        : first(~diffuseColor.color), second(specularColor.color)
    {
    }

    Gamma::Gamma(GAMMA_CREATE , DWORD packed) noexcept
        : first(0u), second(0u)
    {


        if (packed & 0x00000080u) second |= ((~packed) & 0x0000007Fu) << 1u;
        else                      first  |= (packed & 0x0000007Fu) << 1u;
        if (packed & 0x00008000u) second |= ((~packed) & 0x00007F80u) << 1u;
        else                      first  |= (packed & 0x00007F80u) << 1u;
        if (packed & 0x00800000u) second |= ((~packed) & 0x007F8000u) << 1u;
        else                      first  |= (packed & 0x007F8000u) << 1u;
        if (packed & 0x80000000u) second |= ((~packed) & 0xFF800000u) << 1u;
        else                      first  |= (packed & 0xFF800000u) << 1u;
    }

    DWORD Gamma::toDword() const noexcept
    {
        DWORD out = (first >> 1u) & 0x7F7F7F7Fu;
        const DWORD add = second;
        if (add & 0x000000FFu) out |= ((~add >> 1u) & 0x0000007Fu) | 0x00000080u;
        if (add & 0x0000FF00u) out |= ((~add >> 1u) & 0x00007F00u) | 0x00008000u;
        if (add & 0x00FF0000u) out |= ((~add >> 1u) & 0x007F0000u) | 0x00800000u;
        if (add & 0xFF000000u) out |= ((~add >> 1u) & 0x7F000000u) | 0x80000000u;
        return out;
    }


    Gamma interpolateGamma(const Gamma& from, const Gamma& to, float t) noexcept
    {
        const auto signedLane = [](const Gamma& value, unsigned int shift) noexcept -> int
        {
            const int negative = static_cast<int>((value.first >> shift) & 0xFFu);
            if (negative != 0)
                return -negative;
            return static_cast<int>((value.second >> shift) & 0xFFu);
        };

        const auto lane = [&from, &to, t, &signedLane](unsigned int shift) noexcept -> int
        {
            const int firstValue = signedLane(from, shift);
            const int secondValue = signedLane(to, shift);
            const float value =
                static_cast<float>(secondValue - firstValue) * t + static_cast<float>(firstValue);
            return static_cast<int>(value);
        };

        return Gamma(lane(24u), lane(16u), lane(8u), lane(0u));
    }


    Gamma* Gamma::setSaturatingAdd(Gamma lhs, Gamma rhs) noexcept
    {
        const __m64 left = *reinterpret_cast<const __m64*>(&lhs);
        const __m64 right = *reinterpret_cast<const __m64*>(&rhs);
        const __m64 sum = _m_paddusb(left, right);
        *reinterpret_cast<__m64*>(this) = sum;
        _mm_empty();
        return this;
    }

    Gamma::Gamma(int alphaDelta, int redDelta, int greenDelta, int blueDelta) noexcept
        : first(0u), second(0u)
    {

        auto clampSigned = [](int value) -> int
        {
            if (value < -255)
                return -255;
            if (value > 255)
                return 255;
            return value;
        };

        const int a = clampSigned(alphaDelta);
        if (a >= 0)
            second |= (static_cast<DWORD>(a) & 0xFFu) << 24u;
        else
            first |= (static_cast<DWORD>(-a) & 0xFFu) << 24u;

        setRedDelta(redDelta);
        setGreenDelta(greenDelta);
        setBlueDelta(blueDelta);
    }

    namespace
    {
        int clampGammaSignedLane(int value) noexcept
        {
            if (value < -255)
                return -255;
            if (value > 255)
                return 255;
            return value;
        }
    }


    Gamma* Gamma::setRedDelta(int value) noexcept
    {
        const int lane = clampGammaSignedLane(value);
        first &= 0xFF00FFFFu;
        second &= 0xFF00FFFFu;
        if (lane < 0)
            first |= (static_cast<DWORD>(-lane) & 0xFFu) << 16u;
        else
            second |= (static_cast<DWORD>(lane) & 0xFFu) << 16u;
        return this;
    }


    Gamma* Gamma::setGreenDelta(int value) noexcept
    {
        const int lane = clampGammaSignedLane(value);
        first &= 0xFFFF00FFu;
        second &= 0xFFFF00FFu;
        if (lane < 0)
            first |= (static_cast<DWORD>(-lane) & 0xFFu) << 8u;
        else
            second |= (static_cast<DWORD>(lane) & 0xFFu) << 8u;
        return this;
    }


    Gamma* Gamma::setBlueDelta(int value) noexcept
    {
        const int lane = clampGammaSignedLane(value);
        first &= 0xFFFFFF00u;
        second &= 0xFFFFFF00u;
        if (lane < 0)
            first |= static_cast<DWORD>(-lane) & 0xFFu;
        else
            second |= static_cast<DWORD>(lane) & 0xFFu;
        return this;
    }

    DWORD* packOpaqueRgbClamped(DWORD* destination, int red, int green, int blue) noexcept
    {

        *destination = packARGB(255, clampByteImpl(red), clampByteImpl(green), clampByteImpl(blue));
        return destination;
    }

    DWORD GammaRawCreateOpaque(int red, int green, int blue)
    {
        DWORD out = 0;
        packOpaqueRgbClamped(&out, red, green, blue);
        return out;
    }

    DWORD* packArgbClamped(DWORD* destination, int alpha, int red, int green, int blue) noexcept
    {

        *destination = packARGB(clampByteImpl(alpha),
                                clampByteImpl(red),
                                clampByteImpl(green),
                                clampByteImpl(blue));
        return destination;
    }

    DWORD* blendGammaRawPairWithColor(DWORD* destination, const Gamma* maskAndColor, const DWORD* baseColor) noexcept
    {

        if (maskAndColor->first == 0u && maskAndColor->second == 0u)
        {
            *destination = *baseColor;
            return destination;
        }

        const DWORD mask = ~maskAndColor->first;
        const DWORD src = maskAndColor->second;
        const unsigned int baseB = *baseColor & 0xFFu;
        const unsigned int baseG = (*baseColor >> 8u) & 0xFFu;
        const unsigned int baseR = (*baseColor >> 16u) & 0xFFu;
        const unsigned int maskB = (mask & 0xFFu) + 1u;
        const unsigned int maskG = ((mask >> 8u) & 0xFFu) + 1u;
        const unsigned int maskR = ((mask >> 16u) & 0xFFu) + 1u;
        const int outB = static_cast<int>((src & 0xFFu) + ((baseB * maskB) >> 8u));
        const int outG = static_cast<int>(((src >> 8u) & 0xFFu) + ((baseG * maskG) >> 8u));
        const int outR = static_cast<int>(((src >> 16u) & 0xFFu) + ((baseR * maskR) >> 8u));
        *destination = packARGB(255, clampByteImpl(outR), clampByteImpl(outG), clampByteImpl(outB));
        return destination;
    }

    DWORD GammaRawBlend(const Gamma& maskAndColor, DWORD baseColor)
    {
        DWORD out = 0;
        blendGammaRawPairWithColor(&out, &maskAndColor, &baseColor);
        return out;
    }
}
