#pragma once
#include "core/types.h"
#include <array>
#include <cstddef>

namespace as1
{
    class RESOURCE;

    struct CONSTANT
    {
        static constexpr std::size_t DWORD_COUNT = 26;
        static constexpr std::size_t RECORD_SIZE = DWORD_COUNT * sizeof(DWORD);

        std::array<DWORD, DWORD_COUNT> raw;

        CONSTANT* Load(RESOURCE* res);
    };

    extern CONSTANT* g_baseConstants;
}
