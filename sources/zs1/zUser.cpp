#include "zUser.h"
#include "zCommon.h"

namespace zs1 {


zUser::~zUser()
{
    AssignCString(&m_name, nullptr);
}


bool zUser::Load(FILE* file)
{
    char* name = ReadLine(file);
    if (!name)
        return false;
    SetName(name);
    return zArgList::Load(file);
}


const char* zUser::GetName() const
{
    return m_name;
}


void zUser::SetName(const char* name)
{
    if (!name)
        Assert(5, "_sName", "zUserMngr.cpp", 69);
    AssignCString(&m_name, name);
}

}
