#pragma once


struct _iobuf; typedef _iobuf FILE;


namespace zs1 {
class zArg;

#pragma pack(push, 4)
class zArgList {
public:


    __forceinline
    zArgList() noexcept : m_items(nullptr), m_size(0), m_capacity(0), m_dirty(0) {}

    ~zArgList();
    virtual bool Load(FILE* file);
    virtual const char* GetName() const;


    void Clear();

    void SetInt(const char* name, int value);

    bool GetInt(const char* name, int* outValue);

    void SetStr(const char* name, const char* value);

    bool GetStr(const char* name, const char** outValue);

    zArg* FindArg(const char* name);

    zArg* AddArg(const char* name);
    void Save(const char* fileName);

    bool IsDirty() const noexcept { return m_dirty != 0; }
    int GetArgCount() const noexcept { return m_size; }

protected:
    zArg** m_items;
    int m_size;
    int m_capacity;
    unsigned char m_dirty;
    unsigned char m_pad[3];
};
#pragma pack(pop)


}
