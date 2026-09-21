#pragma once

#include <cstddef>
#include <cstdint>

namespace as1
{
    class BaseStream;

    struct ANGLE
    {
        unsigned char value;

        ANGLE() noexcept {}


        ANGLE(unsigned char v) noexcept : value(v) {}

        ANGLE(float x, float y) noexcept;


        ANGLE(float x, float y, float* projectedLength) noexcept;


        ANGLE(const ANGLE& other) noexcept : value(other.value) {}


        explicit ANGLE(const ANGLE* other) noexcept { if (this != other) value = other->value; }
        ANGLE& operator=(const ANGLE& other) noexcept
        {
            value = other.value;
            return *this;
        }


        ANGLE operator+(const ANGLE& rhs) const noexcept
        {
            return ANGLE(static_cast<unsigned char>(value + rhs.value));
        }


        ANGLE operator-(const ANGLE& rhs) const noexcept
        {
            return ANGLE(static_cast<unsigned char>(value - rhs.value));
        }

        int Int() const noexcept { return static_cast<int>(value); }

        ANGLE GetInversed() const noexcept;

        float Sin() const noexcept;

        float Cos() const noexcept;
        void Read(BaseStream* stream);
    };


    ANGLE Decart2Polar(int x, int y, int* projectedLength = nullptr);
    ANGLE DirectionFromFloatXY(float x, float y) noexcept;


    int Sqrt(int value);
}
