#pragma once
#include "types.h"
#include <cstdarg>
#include <cstdint>

namespace as1
{
    class FileLogger;


    std::intptr_t logAndShowError(FileLogger* logger, const char* format, ...);


    std::intptr_t rewriteLogLine(FileLogger* logger, const char* format, ...);


    std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...);


    [[noreturn]] void fatalLogError(FileLogger* logger, const char* format, ...);


    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);

    namespace LOG
    {
        void Write(const char* format, ...);
        void WriteV(const char* format, va_list args);
        void Rewrite(const char* format, ...);
        void RewriteV(const char* format, va_list args);
        void ShowMessage(const char* format, ...);
        [[noreturn]] void Fatal(const char* format, ...);
        void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
    }
}
