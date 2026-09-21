#include "zArg.h"
#include "zCommon.h"

namespace zs1 {


zArg::~zArg()
{
    AssignCString(&m_name, nullptr);
    AssignCString(&m_strValue, nullptr);
}


void zArg::SetInt(int value)
{
    m_intValue = value;
    m_type = AT_INT;
}


int zArg::GetInt() const
{
    if (m_type != AT_INT)
        Assert(5, "m_eArgType == AT_INT", "zUserMngr.cpp", 36);
    return m_intValue;
}


void zArg::SetStr(const char* value)
{
    AssignCString(&m_strValue, value);
    m_type = AT_STR;
}


const char* zArg::GetStr() const
{
    if (m_type != AT_STR)
        Assert(5, "m_eArgType == AT_STR", "zUserMngr.cpp", 52);
    return m_strValue;
}


}
