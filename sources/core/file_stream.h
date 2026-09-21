#pragma once
#include "base_stream.h"
#include <cstdio>
#include <string>

namespace as1
{
    class FSTREAM final : public BaseStream
    {
    public:
        FSTREAM() = default;
        FSTREAM(const std::string& path, const char* mode);
        ~FSTREAM() override;

        void close();
        bool isOpen() const { return m_file != nullptr; }
        bool isWritable() const;
        size_t seek(size_t pos);
        size_t shift(int delta);
        size_t position() const;
        size_t length() const;

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;

        std::FILE* nativeFile() const { return m_file; }

    private:

        std::FILE* m_file = nullptr;
    };


}
