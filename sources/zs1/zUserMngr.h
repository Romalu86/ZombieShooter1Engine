#pragma once

#include "core/as_string.h"


#include "zArgList.h"
#include "zUser.h"

namespace zs1 {
using as1::STRING;

#pragma pack(push, 4)
class zUserMngr : public zArgList {
public:
    static constexpr int MAX_USER_CNT = 100;

    explicit zUserMngr(const char* rootPath);

    virtual ~zUserMngr();

    int GetUsersCnt() const;
    const char* GetUserNameByOrdinal(int ordinal) const;
    int FindUser(const char* name) const;
    bool AddUser(const char* name);
    bool DeleteUser(int slot);
    bool RenameUser(int slot, const char* name);
    bool SetCurUser(int slot);


    void SetInt(bool global, const char* name, int value);

    int GetInt(bool global, const char* name, int defaultValue);

    void SetStr(bool global, const char* name, const char* value);

    const char* GetStr(bool global, const char* name, const char* defaultValue);

    void DeleteAllUsers();
    using zArgList::Load;

    void Load();
    void Save();
    void SaveUser(int slot);

    int OrdinalToSlot(int ordinal) const;
    int SlotToOrdinal(int slot) const;
    bool DeleteUserByOrdinal(int ordinal);
    bool RenameUserByOrdinal(int ordinal, const char* name);
    bool SetCurUserByOrdinal(int ordinal);
    bool SelectOrAddUserByName(const char* name);
    int GetCurUserOrdinal() const;


    STRING BuildUserFileName(int slot, bool hash, int suffixIndex) const;
    STRING BuildUserDataPath() const;


    void SetAuxInt(const char* name, int value);
    int GetAuxInt(const char* name, int defaultValue);
    void SetAuxStr(const char* name, const char* value);
    const char* GetAuxStr(const char* name, const char* defaultValue);
    void LoadAux(int suffixIndex);
    void SaveAux(int suffixIndex);
    void ClearAux();

private:
    char* m_rootPath;
    zUser* m_users[MAX_USER_CNT];
    zArgList m_aux;
    int m_curUser;
};
#pragma pack(pop)


extern zUserMngr* g_UserMngr;

}
