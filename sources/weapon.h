#pragma once

#include "core/types.h"

#include <array>
#include <cstddef>

namespace as1
{


    struct WEAPON
    {
        static constexpr std::size_t SERIALIZED_RECORD_SIZE = 0x280u;
        static constexpr std::size_t RUNTIME_RECORD_SIZE = 0x23Cu;

        std::array<BYTE, RUNTIME_RECORD_SIZE> raw{};
    };

}
