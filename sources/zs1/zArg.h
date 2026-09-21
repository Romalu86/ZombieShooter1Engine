#pragma once


namespace zs1 {

enum zArgType {
    AT_NONE = 0,
    AT_INT = 1,
    AT_STR = 2,
};

#pragma pack(push, 4)
class zArg {
public:


    __forceinline
    zArg() noexcept : m_name(nullptr), m_intValue(0), m_strValue(nullptr), m_type(AT_NONE) {}

    virtual ~zArg();


    void SetInt(int value);

    int GetInt() const;

    void SetStr(const char* value);

    const char* GetStr() const;

    const char* GetName() const noexcept { return m_name; }
    zArgType GetType() const noexcept { return m_type; }

private:
    friend class zArgList;
    char* m_name;
    int m_intValue;
    char* m_strValue;
    zArgType m_type;
};
#pragma pack(pop)


}
