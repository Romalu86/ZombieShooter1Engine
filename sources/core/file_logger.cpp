#include "file_logger.h"
#include "logger.h"
#include "logger_p.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdlib.h>

namespace as1
{
    namespace
    {
        std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs);
    }

    FileLogger::FileLogger(const char* logBasePath, bool rewriteLog)
    {


        m_messageWindowToken = 0;

        const char* const base = (logBasePath ? logBasePath : "");
        std::strcpy(mutableLogBasePathStorage(), base);

        STRING logName;
        if (rewriteLog)
        {
            STRING date;
            constructCurrentDateString(date);
            STRING suffix("\\error", date.c_str());
            suffix += " ";

            STRING time;
            constructCurrentTimeString(time);
            suffix += time.c_str();
            suffix += ".log";


            suffix.Replace(":", "h");
            suffix.Replace(":", "m");

            STRING fullPath(base, suffix.c_str());
            logName = fullPath;
        }
        else
        {
            STRING fullPath(base, "\\error.log");
            logName = fullPath;
        }

        const char* const mode = rewriteLog ? "wt" : "at";
        mutableFileHandle() = FOpen(&logName, mode);
        if (!fileHandle())
        {


            STRING fallback = logName.After("Logs\\");
            mutableFileHandle() = FOpen(&fallback, mode);
            logName = fallback;
        }

        std::strcpy(mutablePathStorage(), logName.c_str());

        STRING executablePath((g_executablePath ? g_executablePath : ""));
        STRING executableTimes;
        constructFileTimestampString(executableTimes, executablePath);
        STRING date;
        constructCurrentDateString(date);
        STRING time;
        constructCurrentTimeString(time);
        writeLogLine(this, "----< %s %s >----< %s (%s) >----",
                     date.c_str(), time.c_str(), executablePath.c_str(), executableTimes.c_str());
    }

    void FileLogger::FormatText(char* destination, size_t capacity, const char* format, va_list args)
    {
        LOGGER::Format(destination, capacity, format, args);
    }

    void FileLogger::SetMessageWindow(HWND window)
    {

        mutableMessageWindowToken() = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window));
    }

    void FileLogger::WriteLine(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        WriteLineV(format, args);
        va_end(args);
    }

    void FileLogger::WriteLineV(const char* format, va_list args)
    {
        char text[MessageCapacity];
        FormatText(text, sizeof(text), format, args);
        writeLogLine(this, "%s", text);
    }

    void FileLogger::RewriteLineV(const char* format, va_list args)
    {
        char text[MessageCapacity];
        FormatText(text, sizeof(text), format, args);
        rewriteLogLine(this, "%s", text);
    }

    void FileLogger::ShowMessage(const char* format, ...)
    {
        char text[MessageCapacity];
        va_list args;
        va_start(args, format);
        FormatText(text, sizeof(text), format, args);
        va_end(args);

        logAndShowError(this, "%s", text);
    }

    [[noreturn]] void FileLogger::Fatal(const char* format, ...)
    {
        char text[MessageCapacity];
        va_list args;
        va_start(args, format);
        FormatText(text, sizeof(text), format, args);
        va_end(args);
        fatalLogError(this, "%s", text);
    }

    void FileLogger::ResourceError(const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        ResourceErrorV(contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
    }

    void FileLogger::ResourceErrorV(const char* contextFormat, int errorCode, const char* detailText, int detailValue, va_list contextArgs)
    {
        va_list args;
        va_copy(args, contextArgs);
        formatResourceErrorLog(this, contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
    }

    FileLogger* g_fileLogger = nullptr;
    char* g_executablePath = nullptr;

    namespace
    {

        std::intptr_t formatResourceErrorLog(FileLogger* logger,
                                      const char* contextFormat,
                                      int errorCode,
                                      const char* detailText,
                                      int detailValue,
                                      va_list contextArgs)
        {

            STRING timeText04;
            constructCurrentTimeString(timeText04);

            char timeText00[0x400] = {};
            std::sprintf(timeText00, "!!!ERROR %s!!!", timeText04.c_str());

            timeText04.ReleaseOwnedStorage();

            char* const contextWrite = timeText00 + std::strlen(timeText00);
            std::vsprintf(contextWrite, contextFormat, contextArgs);

            std::strcat(timeText00, ": ");

            const char* suffix = nullptr;
            switch (errorCode)
            {
            case 0: suffix = "0x%X Couldn't lock %s"; break;
            case 1: suffix = "0x%X Couldn't copy %s"; break;
            case 2: suffix = "%i There was not enough memory for %s"; break;
            case 3: suffix = "0x%X Couldn't create the %s"; break;
            case 4: suffix = "0x%X Invalid %s"; break;
            case 5: suffix = "0x%X Load %s"; break;
            case 6: suffix = "0x%X Save %s"; break;
            case 7: suffix = "0x%X Couldn't open '%s'"; break;
            case 8: suffix = "0x%X Couldn't set the %s"; break;
            case 9: suffix = "0x%X Couldn't get the %s"; break;
            case 10: suffix = "%i %s"; break;
            case 11: suffix = "0x%X Section can't found (%s)"; break;
            case 12: suffix = "0x%X Unable initialize %s"; break;
            case 13: suffix = "%i Missing %s"; break;
            case 14: suffix = "%i Unknownn %s"; break;
            default: break;
            }
            if (suffix)
                std::strcat(timeText00, suffix);


            return writeLogLine(logger, timeText00, detailValue, detailText);
        }
    }

    std::intptr_t logFileLoggerResourceError(FileLogger* logger, const char* contextFormat, int errorCode, const char* detailText, int detailValue, ...)
    {
        va_list args;
        va_start(args, detailValue);
        const std::intptr_t result = formatResourceErrorLog(logger, contextFormat, errorCode, detailText, detailValue, args);
        va_end(args);
        return result;
    }

    FileLogger::~FileLogger()
    {


        FILE* file = fileHandle();
        if (file)
            std::fclose(file);
        clearFileHandle();

        STRING active(pathStorage());
        STRING afterLogs = active.After("Logs\\");
        STRING basename = active.AfterLast("\\");
        if (std::strcmp(afterLogs.c_str(), logger_detail::kFallbackErrorLog) == 0 ||
            std::strcmp(basename.c_str(), logger_detail::kFallbackErrorLog) == 0)
            return;

        STRING finalName(logBasePathStorage());
        finalName += "\\error.log";

        ::DeleteFileA(finalName.c_str());
        if (!::MoveFileExA(active.c_str(), finalName.c_str(), 9u))
        {
            STRING fallback(logger_detail::kFallbackErrorLog);
            ::DeleteFileA(fallback.c_str());
            (void)::MoveFileExA(active.c_str(), fallback.c_str(), 9u);
        }
    }

}
