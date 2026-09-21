#pragma once

#include "core/as_string.h"
#include "zs1/zCommon.h"


namespace zs1 {
using as1::STRING;

#pragma pack(push, 4)
class zHighScoreRecord {
public:


    __forceinline
    zHighScoreRecord() noexcept : m_name(nullptr), m_value1(0), m_value2(0), m_invalid(0) {}
    __forceinline
    virtual ~zHighScoreRecord() { AssignCString(&m_name, nullptr); }

    int Checksum() const;

    char* m_name;
    int m_value1;
    int m_value2;
    unsigned char m_invalid;
    unsigned char m_pad[3];
};
#pragma pack(pop)


#pragma pack(push, 4)
class zHighScores {
public:

    zHighScores() noexcept;


    virtual ~zHighScores();

    void Clear();
    zHighScoreRecord* AppendRecord();
    int Add(const char* name, int value1, int value2);
    const char* GetName(int index) const;
    int GetValue1(int index) const;
    int GetValue2(int index) const;

    STRING BuildRecordsFileName(bool defaultFile) const;
    bool Save();
    bool Load();
    void ResetRecordsFile(bool reload);

    bool IsLoaded() const noexcept { return m_loaded != 0; }
    int GetCount() const noexcept { return m_dataSize; }

private:
    zHighScoreRecord** m_records;
    int m_dataSize;
    int m_capacity;
    unsigned char m_loaded;
    unsigned char m_pad[3];
};
#pragma pack(pop)


extern zHighScores* g_HighScores;

}
