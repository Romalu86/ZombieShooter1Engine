#include "zUserMngr.h"
#include "zCommon.h"


#include <string.h>
#include <new>

struct HWND__;
struct HKEY__;
class STREAM;

#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace zs1 {

zUserMngr* g_UserMngr = nullptr;


zUserMngr::zUserMngr(const char* rootPath)
    : zArgList(), m_rootPath(nullptr), m_aux(), m_curUser(-1)
{
    memset(m_users,0,sizeof(m_users));
    AssignCString(&m_rootPath, rootPath);
    Load();
}


zUserMngr::~zUserMngr()
{
    Save();
    DeleteAllUsers();


}


int zUserMngr::GetUsersCnt() const
{
    int count = 0;
    for (int i = 0; i < MAX_USER_CNT; ++i) {
        if (m_users[i])
            ++count;
    }
    return count;
}


const char* zUserMngr::GetUserNameByOrdinal(int ordinal) const
{
    if (ordinal < 0 || ordinal >= GetUsersCnt())
        Assert(5, "_iNum >= 0 && _iNum < GetUsersCnt()", "zUserMngr.cpp", 222);
    const int slot = OrdinalToSlot(ordinal);
    return slot >= 0 ? m_users[slot]->GetName() : nullptr;
}


int zUserMngr::FindUser(const char* name) const
{
    if (!name)
        Assert(5, "_sName", "zUserMngr.cpp", 231);
    for (int i = 0; i < MAX_USER_CNT; ++i) {
        if (m_users[i] && std::strcmp(m_users[i]->GetName(), name) == 0)
            return i;
    }
    return -1;
}


bool zUserMngr::AddUser(const char* name)
{

    if (FindUser(name) != -1 || !name || !*name)
        return false;

    int slot = 0;
    while (slot < MAX_USER_CNT && m_users[slot])
        ++slot;
    if (slot == MAX_USER_CNT)
        return false;

    zUser* user = new zUser();
    user->SetName(name);
    m_users[slot] = user;
    SetCurUser(slot);
    SaveUser(slot);
    return true;
}


bool zUserMngr::DeleteUser(int slot)
{
    if (slot < 0 || slot >= MAX_USER_CNT)
        Assert(5, "_iNum >= 0 && _iNum < MAX_USER_CNT", "zUserMngr.cpp", 281);
    if (!m_users[slot])
        Assert(5, "m_aUsers[_iNum]", "zUserMngr.cpp", 282);

    delete m_users[slot];
    m_users[slot] = nullptr;
    SetCurUserByOrdinal(0);
    SaveUser(slot);
    return true;
}


bool zUserMngr::RenameUser(int slot, const char* name)
{
    if (slot < 0 || slot >= MAX_USER_CNT)
        Assert(5, "_iNum >= 0 && _iNum < MAX_USER_CNT", "zUserMngr.cpp", 296);
    m_users[slot]->SetName(name);
    SaveUser(slot);
    return true;
}


bool zUserMngr::SetCurUser(int slot)
{
    if (slot != -1 && (slot < 0 || slot >= MAX_USER_CNT))
        Assert(5, "_iNum == -1 || _iNum >= 0 && _iNum < MAX_USER_CNT", "zUserMngr.cpp", 307);


    if (slot >= 0 && !m_users[slot])
        slot = -1;

    m_curUser = slot;
    zArgList::SetInt("m_iCurUser", slot);
    Save();
    return true;
}


void zUserMngr::SetInt(bool global, const char* name, int value)
{
    if (!global && m_curUser >= 0 && m_curUser < MAX_USER_CNT)
        m_users[m_curUser]->SetInt(name, value);
    else
        zArgList::SetInt(name, value);
}


int zUserMngr::GetInt(bool global, const char* name, int defaultValue)
{
    int result = defaultValue;
    if (!global && m_curUser >= 0 && m_curUser < MAX_USER_CNT)
        m_users[m_curUser]->GetInt(name, &result);
    else
        zArgList::GetInt(name, &result);
    return result;
}


void zUserMngr::SetStr(bool global, const char* name, const char* value)
{
    if (!global && m_curUser >= 0 && m_curUser < MAX_USER_CNT)
        m_users[m_curUser]->SetStr(name, value);
    else
        zArgList::SetStr(name, value);
}


const char* zUserMngr::GetStr(bool global, const char* name, const char* defaultValue)
{
    const char* result = defaultValue;
    if (!global && m_curUser >= 0 && m_curUser < MAX_USER_CNT)
        m_users[m_curUser]->GetStr(name, &result);
    else
        zArgList::GetStr(name, &result);
    return result;
}


void zUserMngr::DeleteAllUsers()
{
    for (int i = 0; i < MAX_USER_CNT; ++i) {
        delete m_users[i];
        m_users[i] = nullptr;
    }
}


void zUserMngr::Load()
{
    if (GetUsersCnt() != 0)
        Assert(5, "GetUsersCnt() == 0", "zUserMngr.cpp", 425);


    STRING fileName;
    for (int slot = 0; slot < MAX_USER_CNT; ++slot) {
        fileName = BuildUserFileName(slot, false, -1);
        FILE* file = std::fopen(fileName.c_str(), "r");
        if (!file)
            continue;

        zUser* user = new zUser();
        if (!user->Load(file)) {
            delete user;
            user = nullptr;
        }
        m_users[slot] = user;
        std::fclose(file);
    }


    fileName = STRING::Format("%s/_global.dat", BuildUserDataPath().c_str());
    FILE* globalFile = std::fopen(fileName.c_str(), "r");
    if (globalFile) {

        static_cast<zArgList*>(this)->Load(globalFile);
        std::fclose(globalFile);
    }

    zArgList::GetInt("m_iCurUser", &m_curUser);
    if (m_curUser < 0 || m_curUser >= MAX_USER_CNT || !m_users[m_curUser])
        m_curUser = -1;
}


void zUserMngr::Save()
{
    for (int slot = 0; slot < MAX_USER_CNT; ++slot) {
        if (m_users[slot] && m_users[slot]->IsDirty())
            SaveUser(slot);
    }

    if (IsDirty()) {
        STRING fileName;
        fileName = STRING::Format("%s/_global.dat", BuildUserDataPath().c_str());
        zArgList::Save(fileName.c_str());
    }
}


void zUserMngr::SaveUser(int slot)
{


    STRING normal;
    STRING hash;
    normal = BuildUserFileName(slot, false, -1);
    hash = BuildUserFileName(slot, true, -1);

    if (!m_users[slot]) {
        BackupFile(normal.c_str());
        BackupFile(hash.c_str());
        return;
    }
    m_users[slot]->Save(normal.c_str());
}


int zUserMngr::OrdinalToSlot(int ordinal) const
{
    if (ordinal < 0 || ordinal >= GetUsersCnt())
        Assert(5, "_iNum >= 0 && _iNum < GetUsersCnt()", "zUserMngr.cpp", 552);
    for (int slot = 0; slot < MAX_USER_CNT; ++slot) {
        if (!m_users[slot])
            continue;
        if (ordinal-- == 0)
            return slot;
    }
    return -1;
}


int zUserMngr::SlotToOrdinal(int slot) const
{
    if (slot < 0 || slot >= MAX_USER_CNT)
        Assert(5, "_iNum >= 0 && _iNum < MAX_USER_CNT", "zUserMngr.cpp", 569);
    if (!m_users[slot])
        Assert(5, "m_aUsers[_iNum]", "zUserMngr.cpp", 570);

    int ordinal = 0;
    for (int i = 0; i < slot; ++i) {
        if (m_users[i])
            ++ordinal;
    }
    return ordinal;
}


bool zUserMngr::DeleteUserByOrdinal(int ordinal)
{
    return DeleteUser(OrdinalToSlot(ordinal));
}


bool zUserMngr::RenameUserByOrdinal(int ordinal, const char* name)
{
    return RenameUser(OrdinalToSlot(ordinal), name);
}


bool zUserMngr::SetCurUserByOrdinal(int ordinal)
{
    if (ordinal == -1)
        return SetCurUser(-1);
    return SetCurUser(OrdinalToSlot(ordinal));
}


bool zUserMngr::SelectOrAddUserByName(const char* name)
{
    const int slot = FindUser(name);
    if (slot == -1)
        return AddUser(name);
    return SetCurUser(slot);
}


int zUserMngr::GetCurUserOrdinal() const
{
    if (m_curUser < 0)
        return -1;
    return SlotToOrdinal(m_curUser);
}


STRING zUserMngr::BuildUserFileName(int slot, bool hash, int suffixIndex) const
{
    STRING base = BuildUserDataPath();
    STRING result = STRING::Format("%s/user%03d%s", base.c_str(), slot + 1, hash ? "_hash" : "");
    if (suffixIndex != -1) {
        STRING suffix = STRING::Format("_%02d", suffixIndex);
        result += suffix;
    }
    result += ".dat";
    return result;
}


STRING zUserMngr::BuildUserDataPath() const
{
    STRING result(g_WindowsUserPath);
    result += m_rootPath;

    ::CreateDirectoryA(result.c_str(), nullptr);
    return result;
}


void zUserMngr::SetAuxInt(const char* name, int value)
{
    m_aux.SetInt(name, value);
}


int zUserMngr::GetAuxInt(const char* name, int defaultValue)
{
    int result = defaultValue;
    m_aux.GetInt(name, &result);
    return result;
}


void zUserMngr::SetAuxStr(const char* name, const char* value)
{
    m_aux.SetStr(name, value);
}


const char* zUserMngr::GetAuxStr(const char* name, const char* defaultValue)
{
    const char* result = defaultValue;
    m_aux.GetStr(name, &result);
    return result;
}


void zUserMngr::LoadAux(int suffixIndex)
{
    m_aux.Clear();
    if (m_curUser < 0)
        return;

    STRING fileName;
    fileName = BuildUserFileName(m_curUser, false, suffixIndex);
    FILE* file = std::fopen(fileName.c_str(), "r");
    if (file) {
        m_aux.Load(file);
        std::fclose(file);
    }
}


void zUserMngr::SaveAux(int suffixIndex)
{
    if (!m_aux.IsDirty() || m_curUser < 0)
        return;
    STRING fileName;
    fileName = BuildUserFileName(m_curUser, false, suffixIndex);
    m_aux.Save(fileName.c_str());
}


void zUserMngr::ClearAux()
{
    m_aux.Clear();
}

}
