#include "zDebugLog.h"
#include "zCommon.h"
#include "core/as_string.h"

#include <cstdio>

namespace zs1 {

zDebugLog* g_DebugLog = nullptr;


zDebugLog::zDebugLog()
    : m_file(nullptr), m_fileName(nullptr), m_openTried(0)
{


    STRING date;
    as1::constructCurrentDateString(date);
    STRING suffix("_dbglog", date.c_str());
    suffix += " ";

    STRING time;
    as1::constructCurrentTimeString(time);
    suffix += time.c_str();
    suffix += ".txt";
    suffix.Replace(":", "h");
    suffix.Replace(":", "m");

    STRING fullName(g_WindowsUserPath.c_str());
    fullName += "Logs\\";
    fullName += suffix;
    AssignCString(&m_fileName, fullName.c_str());
}


zDebugLog::~zDebugLog()
{
    AssignCString(&m_fileName, nullptr);
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}


void zDebugLog::Open()
{
    if (!m_openTried && !m_file)
        m_file = std::fopen(m_fileName, "w");
    m_openTried = 1;
}


void zDebugLog::Write(const char* text)
{
    Open();
    if (!text || !m_file)
        return;
    std::fputs(text, m_file);
    std::fputs("\n", m_file);
    std::fflush(m_file);
}

}
