#pragma once
#include "types.h"
#include "as_string.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace as1
{

    class FileLogger
    {
    public:
        FileLogger(const char* logBasePath, bool rewriteLog);
        virtual ~FileLogger();

        bool IsOpen() const { return fileHandle() != nullptr; }
        const char* Path() const { return pathStorage(); }

        void SetMessageWindow(HWND window);

        void WriteLine(const char* format, ...);
        void WriteLineV(const char* format, va_list args);
        void RewriteLineV(const char* format, va_list args);
        void ShowMessage(const char* format, ...);
        [[noreturn]] void Fatal(const char* format, ...);
        void ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
        void ResourceErrorV(const char* contextFormat, int errorCode, const char* detailText, int detailValue, va_list contextArgs);

        std::uint32_t messageWindowToken() const { return m_messageWindowToken; }
        std::uint32_t& mutableMessageWindowToken() { return m_messageWindowToken; }
        FILE* fileHandle() const { return m_file; }
        FILE*& mutableFileHandle() { return m_file; }
        void clearFileHandle() { m_file = nullptr; }
        const char* pathStorage() const { return m_path; }
        char* mutablePathStorage() { return m_path; }
        const char* logBasePathStorage() const { return m_logBasePath; }
        char* mutableLogBasePathStorage() { return m_logBasePath; }
    private:
        static constexpr size_t PathCapacity = 0x400;
        static constexpr size_t MessageCapacity = 1024;

        static void FormatText(char* destination, size_t capacity, const char* format, va_list args);


        std::uint32_t m_messageWindowToken;
        FILE* m_file;
        char m_path[PathCapacity];


        char m_logBasePath[PathCapacity];
    };


    extern FileLogger* g_fileLogger;
    extern char* g_executablePath;
    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...);
}
