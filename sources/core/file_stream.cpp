#include "file_stream.h"

#include <io.h>

namespace as1
{

    FSTREAM::~FSTREAM()
    {
        if (m_file)
            std::fclose(m_file);
    }

    FSTREAM::FSTREAM(const std::string& path, const char* modeText)
    {
        m_file = path.empty() ? nullptr : std::fopen(path.c_str(), modeText ? modeText : "rb");
    }

    void FSTREAM::close()
    {
        if (m_file)
        {
            std::fclose(m_file);
            m_file = nullptr;
        }
    }

    bool FSTREAM::isWritable() const
    {
        if (!m_file)
            return false;
        return true;
    }

    size_t FSTREAM::seek(size_t pos)
    {
        if (!m_file)
            return 0;
        std::fseek(m_file, static_cast<long>(pos), SEEK_SET);
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t FSTREAM::shift(int delta)
    {
        if (!m_file)
            return 0;
        std::fseek(m_file, delta, SEEK_CUR);
        return position();
    }

    size_t FSTREAM::position() const
    {
        if (!m_file)
            return 0;
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t FSTREAM::length() const
    {
        if (!m_file)
            return 0;
        const int fd = _fileno(m_file);
        if (fd < 0)
            return 0;
        const long value = _filelength(fd);
        return value > 0 ? static_cast<size_t>(value) : 0;
    }

    int FSTREAM::read(void* buf, unsigned size)
    {
        return static_cast<int>(size - std::fread(buf, 1, size, m_file));
    }

    int FSTREAM::write(const void* buf, unsigned size)
    {
        return static_cast<int>(size - std::fwrite(buf, 1, size, m_file));
    }
}
