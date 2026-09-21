#pragma once
#include "types.h"
#include <cstdarg>
#include <cstddef>

namespace as1
{
    class LOGGER
    {
    public:
        static void Format(char* destination, std::size_t capacity, const char* format, va_list args);
        static const char* ResourceErrorSuffix(int errorCode);
    };
}
