#pragma once


struct _iobuf; typedef _iobuf FILE;


namespace zs1 {

#pragma pack(push, 4)
class zDebugLog {
public:
    zDebugLog();
    virtual ~zDebugLog();

    void Open();
    void Write(const char* text);

    const char* GetFileName() const noexcept { return m_fileName; }

private:
    FILE* m_file;
    char* m_fileName;
    unsigned char m_openTried;
    unsigned char m_pad[3];
};
#pragma pack(pop)


extern zDebugLog* g_DebugLog;

}
