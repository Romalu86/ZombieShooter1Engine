#pragma once


#include "zArgList.h"

namespace zs1 {

#pragma pack(push, 4)
class zUser : public zArgList {
public:


    __forceinline
    zUser() noexcept : zArgList(), m_name(nullptr) {}
    bool Load(FILE* file) override;
    const char* GetName() const override;


    virtual ~zUser();


    void SetName(const char* name);

private:
    char* m_name;
};
#pragma pack(pop)


}
