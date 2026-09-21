#pragma once

#include "core/as_string.h"
#include <new>


namespace zs1 {
using as1::STRING;

struct zStringField {
    char* m_str;
};

#pragma pack(push, 4)
class zHelpItem {
public:


    __forceinline
    zHelpItem() noexcept : m_value1(0), m_value2(0), m_text{as1::STRING::SharedEmptyText()}, m_value3(0), m_type(0) {}
    __forceinline
    virtual ~zHelpItem()
    {
        if (m_text.m_str != as1::STRING::SharedEmptyText())
            ::operator delete(m_text.m_str);
    }

    int m_value1;
    int m_value2;
    zStringField m_text;
    int m_value3;
    int m_type;
};

class zHelpLevel {
public:
    explicit zHelpLevel(int levelNum) noexcept;

    virtual ~zHelpLevel();

    void Clear();
    zHelpItem* GetItem(int item);
    zHelpItem* AppendItem();
    bool Load();

    zHelpItem** m_items;
    int m_size;
    int m_capacity;
    int m_levelNum;
    unsigned char m_loaded;
    unsigned char m_pad[3];
};

class zHelpParser {
public:
    static constexpr int MAX_LEVEL_CNT = 200;


    explicit zHelpParser(const char* rootPath = nullptr) noexcept;

    virtual ~zHelpParser();

    zHelpLevel* GetLevel(int level, bool create);
    int LoadLevel(int level);
    int GetItemType(int level, int item);
    int GetItemMetric(int level, int item);

    const zStringField* GetItemText(int level, int item);
    int GetItemValue1(int level, int item);
    int GetItemValue2(int level, int item);

    const char* GetRootPath() const noexcept { return m_rootPath ? m_rootPath : ""; }

private:
    friend class zHelpLevel;

    zHelpLevel** m_levels;
    int m_levelSize;
    int m_capacity;
    char* m_rootPath;
};
#pragma pack(pop)


extern zHelpParser* g_HelpParser;

}
