#include "zCommon.h"
#include "core/as_string.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <cstdint>

namespace as1 {
class FileLogger;
extern FileLogger* g_fileLogger;
std::intptr_t writeLogLine(FileLogger* logger, const char* format, ...);
}


struct HWND__;
struct HKEY__;
class STREAM;

namespace zs1 {


as1::STRING g_WindowsUserPath;

namespace {


char g_readLineBuffer[1000];
char* g_readLine = g_readLineBuffer;
}


void AssignCString(char** destination, const char* source)
{
    if (*destination) {
        ::operator delete(*destination);
        *destination = nullptr;
    }

    if (!source)
        return;

    const unsigned int size = std::strlen(source) + 1;
    char* copy = static_cast<char*>(::operator new(size));
    std::memcpy(copy, source, size);
    *destination = copy;
}


bool SplitLine(char* text, char** left, char** right, char delimiter)
{
    char* split = std::strchr(text, delimiter);
    if (!split)
        return false;

    *split = '\0';
    *left = text;
    *right = split + 1;
    return true;
}


char* ReadLine(FILE* file)
{


    char* result = fgets(g_readLine, 1000, file);


    if (std::feof(file))
        return nullptr;

    while (strlen(g_readLine) != 0 &&
           (g_readLine[strlen(g_readLine) - 1] == '\n' ||
            g_readLine[strlen(g_readLine) - 1] == '\r')) {
        g_readLine[strlen(g_readLine) - 1] = '\0';
    }
    return result;
}


void BackupFile(const char* fileName)
{


    STRING source(fileName);
    STRING backup(source);
    backup += ".bak";
    ::MoveFileExA(source.c_str(), backup.c_str(), 9u);
}

void Assert(int severity, const char* expression, const char* fileName, int line)
{


    (void)severity;
    if (as1::g_fileLogger)
        as1::writeLogLine(as1::g_fileLogger, "Assert(%s) in file %s line %d", expression, fileName, line);
}


unsigned int Adler32(const char* text)
{
    unsigned int a = 1;
    unsigned int b = 0;
    while (*text) {
        a = (a + static_cast<unsigned char>(*text++)) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) + a;
}

}
