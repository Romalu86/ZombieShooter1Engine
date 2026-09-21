#include "zHelpParser.h"
#include "zCommon.h"
#include "map.h"
#include "vid/vid.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

struct HWND__;
struct HKEY__;
class STREAM;

namespace zs1 {

zHelpParser* g_HelpParser = nullptr;

namespace {

void AppendCStringLine(char** destination, const char* line)
{
    const char* oldText = *destination ? *destination : "";
    const char* newLine = line ? line : "";
    const unsigned int oldLen = std::strlen(oldText);
    const unsigned int lineLen = std::strlen(newLine);

    char* merged = static_cast<char*>(::operator new(oldLen + lineLen + 2));
    std::memcpy(merged, oldText, oldLen);
    std::memcpy(merged + oldLen, newLine, lineLen);
    merged[oldLen + lineLen] = '\n';
    merged[oldLen + lineLen + 1] = '\0';

    if (*destination)
        ::operator delete(*destination);
    *destination = merged;
}

}


zHelpLevel::zHelpLevel(int levelNum) noexcept
    : m_items(nullptr), m_size(0), m_capacity(0), m_levelNum(levelNum), m_loaded(0)
{

}


zHelpLevel::~zHelpLevel()
{
    Clear();
}


void zHelpLevel::Clear()
{

    if (!m_items) {
        m_loaded = 0;
        return;
    }

    for (int i = 0; i < m_size; ++i)
        delete m_items[i];

    std::free(m_items);
    m_items = nullptr;
    m_size = 0;
    m_capacity = 0;
    m_loaded = 0;
}


zHelpItem* zHelpLevel::GetItem(int item)
{
    Load();
    if (item < 0 || item >= m_size)
        Assert(5, "_iItemNum >= 0 && _iItemNum < m_iSize", "zHelpParser.cpp", 174);
    return m_items[item];
}


zHelpItem* zHelpLevel::AppendItem()
{
    if (m_size >= 200)
        Assert(5, "m_iSize < 200", "zHelpParser.cpp", 182);

    if (m_size == m_capacity) {
        m_capacity += 8;
        m_items = static_cast<zHelpItem**>(
            std::realloc(m_items, static_cast<unsigned int>(m_capacity) * sizeof(zHelpItem*)));
    }

    zHelpItem* item = new zHelpItem();
    m_items[m_size++] = item;
    return item;
}


bool zHelpLevel::Load()
{
    if (m_levelNum < -1)
        Assert(5, "m_iLevNum >= -1", "zHelpParser.cpp", 202);
    if (m_loaded)
        return true;
    if (m_size != 0)
        Assert(5, "m_iSize==0", "zHelpParser.cpp", 205);
    if (m_capacity != 0)
        Assert(5, "m_iSizeAlloc==0", "zHelpParser.cpp", 206);


    STRING levelTag;
    levelTag = STRING::Format("<Level_%02d>", m_levelNum % 100 + 1);


    STRING fileName(g_HelpParser->m_rootPath);
    {
        STRING shortName = STRING::Format("help_l%02d.txt", m_levelNum / 100 + 1);
        fileName += shortName;
    }

    FILE* file = std::fopen(fileName.c_str(), "r");
    if (!file) {
        m_loaded = 1;
        return true;
    }

    bool foundLevel = false;
    zHelpItem* current = nullptr;
    int currentType = 0;

    while (!std::feof(file)) {
        char* line = ReadLine(file);
        if (!line)
            break;

        const char* wantedLevel = levelTag.c_str();
        if (!foundLevel && std::strcmp(line, wantedLevel) == 0)
            foundLevel = true;


        if (std::strcmp(line, "<EndLevel>") == 0)
            break;
        if (!foundLevel)
            continue;

        if (std::strcmp(line, "<Text>") == 0) {
            current = AppendItem();
            currentType = 1;
            current->m_type = currentType;
            continue;
        }

        if (std::strcmp(line, "<Img>") == 0) {
            current = AppendItem();
            currentType = 2;
            current->m_type = currentType;
            continue;
        }

        if (currentType == 2) {
            char* key = nullptr;
            char* value = nullptr;
            if (!SplitLine(line, &key, &value, '='))
                continue;
            if (std::strcmp(key, "vid") == 0)
                current->m_value1 = std::atoi(value);
            else if (std::strcmp(key, "dir") == 0)
                current->m_value2 = std::atoi(value);
        }
        else if (currentType == 1) {
            STRING* text = reinterpret_cast<STRING*>(&current->m_text);
            *text += line;
            *text += "\n";
        }
    }

    std::fclose(file);
    m_loaded = 1;
    return true;
}


zHelpParser::zHelpParser(const char* rootPath) noexcept
    : m_levels(nullptr), m_levelSize(0), m_capacity(0), m_rootPath(nullptr)
{


    STRING normalizedRoot(rootPath ? rootPath : "");
    normalizedRoot += "\\";
    AssignCString(&m_rootPath, normalizedRoot.c_str());
}


zHelpParser::~zHelpParser()
{
    for (int i = 0; i < m_levelSize; ++i)
        delete m_levels[i];
    std::free(m_levels);
    AssignCString(&m_rootPath, nullptr);
}


zHelpLevel* zHelpParser::GetLevel(int level, bool create)
{
    for (int i = 0; i < m_levelSize; ++i) {

        if (m_levels[i]->m_levelNum == level)
            return m_levels[i];
    }

    if (!create)
        return nullptr;

    zHelpLevel* result = new zHelpLevel(level);

    if (m_levelSize >= MAX_LEVEL_CNT)
        Assert(5, "m_iLevSize < 200", "zHelpParser.cpp", 51);

    if (m_levelSize == m_capacity) {
        m_capacity += 8;
        m_levels = static_cast<zHelpLevel**>(
            std::realloc(m_levels, static_cast<unsigned int>(m_capacity) * sizeof(zHelpLevel*)));
    }

    m_levels[m_levelSize++] = result;
    return result;
}


int zHelpParser::LoadLevel(int level)
{
    zHelpLevel* helpLevel = GetLevel(level, true);
    if (!helpLevel)
        return 0;
    helpLevel->Clear();
    helpLevel->Load();
    return helpLevel->m_size;
}


int zHelpParser::GetItemType(int level, int item)
{
    zHelpLevel* helpLevel = GetLevel(level, false);
    if (!helpLevel)
        Assert(5, "lev", "zHelpParser.cpp", 84);
    return helpLevel->GetItem(item)->m_type;
}


int zHelpParser::GetItemMetric(int level, int item)
{
    zHelpLevel* helpLevel = GetLevel(level, false);
    if (!helpLevel)
        Assert(5, "lev", "zHelpParser.cpp", 93);
    zHelpItem* helpItem = helpLevel->GetItem(item);

    if (helpItem->m_type == 1)
        return helpItem->m_value3;
    if (helpItem->m_type == 2) {


        as1::VID* vid = as1::EmptyVid;
        if (as1::Map)
            vid = as1::Map->Vid(helpItem->m_value1);
        if (!vid)
            vid = as1::EmptyVid;
        return static_cast<int>(*reinterpret_cast<const short*>(
            reinterpret_cast<const unsigned char*>(vid) + 0x300));
    }
    return 0;
}


const zStringField* zHelpParser::GetItemText(int level, int item)
{
    zHelpLevel* helpLevel = GetLevel(level, false);
    if (!helpLevel)
        Assert(5, "lev", "zHelpParser.cpp", 110);
    return &helpLevel->GetItem(item)->m_text;
}


int zHelpParser::GetItemValue1(int level, int item)
{
    zHelpLevel* helpLevel = GetLevel(level, false);
    if (!helpLevel)
        Assert(5, "lev", "zHelpParser.cpp", 119);
    return helpLevel->GetItem(item)->m_value1;
}


int zHelpParser::GetItemValue2(int level, int item)
{
    zHelpLevel* helpLevel = GetLevel(level, false);
    if (!helpLevel)
        Assert(5, "lev", "zHelpParser.cpp", 128);
    return helpLevel->GetItem(item)->m_value2;
}

}
