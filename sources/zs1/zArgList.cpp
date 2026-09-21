#include "zArgList.h"
#include "zArg.h"
#include "zCommon.h"

#include <string.h>
#include <new>

struct HWND__;
struct HKEY__;
class STREAM;

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zs1 {


zArgList::~zArgList()
{
    Clear();
}

const char* zArgList::GetName() const
{
    return nullptr;
}


void zArgList::Clear()
{
    if (!m_items)
        return;

    for (int i = 0; i < m_size; ++i)
        delete m_items[i];

    std::free(m_items);
    m_items = nullptr;
    m_capacity = 0;
    m_size = 0;
}


void zArgList::SetInt(const char* name, int value)
{
    zArg* arg = FindArg(name);
    if (!arg)
        arg = AddArg(name);

    if (arg->GetType() != AT_INT || arg->GetInt() != value)
        m_dirty = 1;
    arg->SetInt(value);
}


bool zArgList::GetInt(const char* name, int* outValue)
{
    zArg* arg = FindArg(name);
    if (!arg)
        return false;
    *outValue = arg->GetInt();
    return true;
}


void zArgList::SetStr(const char* name, const char* value)
{
    zArg* arg = FindArg(name);
    if (!arg)
        arg = AddArg(name);

    if (arg->GetType() != AT_STR || std::strcmp(arg->GetStr(), value) != 0)
        m_dirty = 1;
    arg->SetStr(value);
}


bool zArgList::GetStr(const char* name, const char** outValue)
{
    zArg* arg = FindArg(name);
    if (!arg)
        return false;
    *outValue = arg->GetStr();
    return true;
}


zArg* zArgList::FindArg(const char* name)
{
    if (!name)
        Assert(5, "_sParamName", "zUserMngr.cpp", 152);

    for (int i = 0; i < m_size; ++i) {
        zArg* arg = m_items[i];

        if (std::strcmp(arg->GetName(), name) == 0)
            return arg;
    }
    return nullptr;
}


zArg* zArgList::AddArg(const char* name)
{
    zArg* arg = new zArg();
    AssignCString(&arg->m_name, name);

    if (m_size == m_capacity) {
        m_capacity += 8;
        m_items = static_cast<zArg**>(std::realloc(m_items, static_cast<unsigned int>(m_capacity) * sizeof(zArg*)));
    }
    m_items[m_size++] = arg;
    return arg;
}


bool zArgList::Load(FILE* file)
{


    while (!std::feof(file)) {
        char* line = ReadLine(file);
        if (!line)
            break;

        char* left = nullptr;
        char* right = nullptr;
        if (!SplitLine(line, &left, &right, '='))
            continue;

        zArgType type = AT_NONE;
        if (left[0] == 'i')
            type = AT_INT;
        else if (left[0] == 's')
            type = AT_STR;

        if (type == AT_NONE)
            Assert(5, "type != zArg::AT_NONE", "zUserMngr.cpp", 408);


        ++left;
        if (type == AT_INT)
            SetInt(left, std::atoi(right));
        else if (type == AT_STR)
            SetStr(left, right);
    }
    return true;
}


void zArgList::Save(const char* fileName)
{
    STRING path;
    path = fileName;
    BackupFile(path.c_str());
    FILE* file = std::fopen(path.c_str(), "w");
    if (file) {

        if (GetName())
            std::fprintf(file, "%s\n", GetName());

        for (int i = 0; i < m_size; ++i) {
            zArg* arg = m_items[i];
            if (arg->GetType() == AT_INT)
                std::fprintf(file, "i%s=%d\n", arg->GetName(), arg->GetInt());
            else if (arg->GetType() == AT_STR)
                std::fprintf(file, "s%s=%s\n", arg->GetName(), arg->GetStr());
            else
                Assert(5, "0", "zUserMngr.cpp", 538);
        }
        std::fclose(file);
    }
    m_dirty = 0;
}

}
