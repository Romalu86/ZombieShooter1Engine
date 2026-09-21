#include "zHighScores.h"
#include "zCommon.h"
#include "zUserMngr.h"

#include <string.h>
#include <new>

struct HWND__;
struct HKEY__;
class STREAM;

#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace zs1 {

zHighScores* g_HighScores = nullptr;


int zHighScoreRecord::Checksum() const
{
    if (m_invalid)
        return -999;


    STRING text;
    text += m_name;
    text += " ";
    {
        STRING number = STRING::Format("%d", m_value1);
        text += number;
    }
    text += " ";
    {
        STRING number = STRING::Format("%d", m_value2);
        text += number;
    }
    return static_cast<int>(Adler32(text.c_str()));
}


zHighScores::zHighScores() noexcept
    : m_records(nullptr), m_dataSize(0), m_capacity(0), m_loaded(0)
{


    Load();
}


zHighScores::~zHighScores()
{
    Save();
    Clear();
}


void zHighScores::Clear()
{
    for (int i = 0; i < m_dataSize; ++i)
        delete m_records[i];
    if (m_records)
        std::free(m_records);
    m_records = nullptr;
    m_dataSize = 0;
    m_capacity = 0;
}


zHighScoreRecord* zHighScores::AppendRecord()
{
    if (m_dataSize == m_capacity) {
        m_capacity += 8;
        m_records = static_cast<zHighScoreRecord**>(
            std::realloc(m_records, static_cast<unsigned int>(m_capacity) * sizeof(zHighScoreRecord*)));
    }

    ++m_dataSize;
    zHighScoreRecord* record = new zHighScoreRecord();
    m_records[m_dataSize - 1] = record;
    return record;
}


int zHighScores::Add(const char* name, int value1, int value2)
{
    if (!m_loaded)
        Assert(5, "m_bLoaded", "zHighScores.cpp", 63);
    Load();

    int insertAt = 0;
    while (insertAt < m_dataSize) {
        zHighScoreRecord* current = m_records[insertAt];
        if (value1 > current->m_value1)
            break;
        if (value1 == current->m_value1 && value2 > current->m_value2)
            break;
        ++insertAt;
    }

    AppendRecord();
    for (int i = m_dataSize - 1; i > insertAt; --i) {
        zHighScoreRecord* dst = m_records[i];
        zHighScoreRecord* src = m_records[i - 1];
        AssignCString(&dst->m_name, src->m_name);
        dst->m_value1 = src->m_value1;
        dst->m_value2 = src->m_value2;
        dst->m_invalid = src->m_invalid;
    }

    zHighScoreRecord* record = m_records[insertAt];
    AssignCString(&record->m_name, name);
    record->m_value1 = value1;
    record->m_value2 = value2;
    record->m_invalid = 0;
    Save();
    return insertAt;
}


const char* zHighScores::GetName(int index) const
{
    if (!m_loaded)
        Assert(5, "m_bLoaded", "zHighScores.cpp", 96);
    if (index < 0 || index >= m_dataSize)
        Assert(5, "_iNum >= 0 && _iNum < m_iDataSize", "zHighScores.cpp", 97);
    return m_records[index]->m_name;
}


int zHighScores::GetValue1(int index) const
{
    if (!m_loaded)
        Assert(5, "m_bLoaded", "zHighScores.cpp", 105);
    if (index < 0 || index >= m_dataSize)
        Assert(5, "_iNum >= 0 && _iNum < m_iDataSize", "zHighScores.cpp", 106);
    return m_records[index]->m_value1;
}


int zHighScores::GetValue2(int index) const
{
    if (!m_loaded)
        Assert(5, "m_bLoaded", "zHighScores.cpp", 114);
    if (index < 0 || index >= m_dataSize)
        Assert(5, "_iNum >= 0 && _iNum < m_iDataSize", "zHighScores.cpp", 115);
    return m_records[index]->m_value2;
}


STRING zHighScores::BuildRecordsFileName(bool defaultFile) const
{
    if (defaultFile)
        return STRING("Maps\\_records.dat_default");
    STRING result = g_UserMngr->BuildUserDataPath();
    result += "\\_records.dat";
    return result;
}


bool zHighScores::Save()
{
    if (!m_loaded)
        Assert(5, "m_bLoaded", "zHighScores.cpp", 139);


    {
        STRING fileName = BuildRecordsFileName(false);
        BackupFile(fileName.c_str());
    }
    FILE* file;
    {
        STRING fileName = BuildRecordsFileName(false);
        file = std::fopen(fileName.c_str(), "w");
    }
    if (file) {
        for (int i = 0; i < m_dataSize; ++i) {
            const zHighScoreRecord* record = m_records[i];
            std::fprintf(file, "%s\n", record->m_name);
            std::fprintf(file, "%d\n", record->m_value1);
            std::fprintf(file, "%d\n", record->m_value2);
            std::fprintf(file, "%d\n", record->Checksum());
        }
        std::fclose(file);
    }
    return true;
}


bool zHighScores::Load()
{
    Clear();
    if (m_dataSize != 0)
        Assert(5, "m_iDataSize == 0", "zHighScores.cpp", 164);

    FILE* file;
    {
        STRING fileName = BuildRecordsFileName(false);
        file = std::fopen(fileName.c_str(), "r");
    }
    if (!file) {
        ResetRecordsFile(false);
        {
            STRING fileName = BuildRecordsFileName(false);
            file = std::fopen(fileName.c_str(), "r");
        }
        if (!file) {
            m_loaded = 1;
            return true;
        }
    }

    while (!std::feof(file)) {
        char* nameLine = ReadLine(file);
        if (!nameLine)
            break;

        STRING nameCopy(nameLine);
        char* value1Line = ReadLine(file);
        if (!value1Line)
            break;
        const int value1 = std::atoi(value1Line);

        char* value2Line = ReadLine(file);
        if (!value2Line)
            break;
        const int value2 = std::atoi(value2Line);

        char* checksumLine = ReadLine(file);
        if (!checksumLine)
            break;
        const int storedChecksum = std::atoi(checksumLine);

        zHighScoreRecord* record = AppendRecord();
        AssignCString(&record->m_name, nameCopy.c_str());
        record->m_value1 = value1;
        record->m_value2 = value2;
        record->m_invalid = 0;

        if (record->Checksum() != storedChecksum) {
            if (storedChecksum != -999) {


                AssignCString(&record->m_name, nameCopy.c_str());
                record->m_value1 = 0;
                record->m_value2 = 0;
            }
            record->m_invalid = 1;
        }
    }

    std::fclose(file);
    m_loaded = 1;
    return true;
}


void zHighScores::ResetRecordsFile(bool reload)
{
    Clear();

    {
        STRING current=BuildRecordsFileName(false);
        BackupFile(current.c_str());
    }

    int copied;
    {
        STRING current=BuildRecordsFileName(false);
        STRING defaults=BuildRecordsFileName(true);
        copied=::CopyFileA(defaults.c_str(),current.c_str(),0);
    }

    if (!copied) {


        STRING current=BuildRecordsFileName(false);
        STRING defaults=BuildRecordsFileName(true);
        STRING errorText=STRING::Format("error file %s to %s",defaults.c_str(),current.c_str());
        (void)errorText;
    }

    if (reload)
        Load();
}

}
