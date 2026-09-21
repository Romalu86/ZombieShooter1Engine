#include "resource.h"
#include "log.h"
#include "file_logger.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#include <io.h>

namespace as1
{
    namespace
    {
        void fourccText(RESOURCE::ResTypes::Type type, char (&out)[5])
        {
            out[0] = static_cast<char>(type & 0xFFu);
            out[1] = static_cast<char>((type >> 8) & 0xFFu);
            out[2] = static_cast<char>((type >> 16) & 0xFFu);
            out[3] = static_cast<char>((type >> 24) & 0xFFu);
            out[4] = '\0';
        }

        void logResourceError(const STRING& name, RESOURCE::ResTypes::Type type, int code, const char* detail, int value)
        {
            char typ[5];
            fourccText(type, typ);
            LOG::ResourceError("RES '%s' '%.4s'", code, detail, value, name.c_str(), typ);
        }

        long fileLength(std::FILE* file)
        {
            if (!file)
                return -1;
            return static_cast<long>(::_filelength(::_fileno(file)));
        }
    }


    RESOURCE::RESOURCE()
    {
        m_file = nullptr;
        m_sectionPosition = 0;
        m_sectionSize = 0;
        m_rootBegin = 0;
        m_rootEnd = 0;
        m_state = st_seek;
        m_flags &= ~1u;
    }


    RESOURCE::~RESOURCE()
    {

        close();
        m_name.ReleaseOwnedStorage();
    }


    void RESOURCE::close()
    {
        if (m_file)
        {
            if ((m_flags & 1u) != 0)
            {
                m_state = st_seek;
                std::fseek(m_file, static_cast<long>(m_rootBegin + 4u), SEEK_SET);
                m_rootEnd += static_cast<std::uint32_t>(-8 - static_cast<std::int32_t>(m_rootBegin));
                write(&m_rootEnd, 4);
            }
            std::fclose(m_file);
        }
        m_file = nullptr;
        m_rootEnd = 0;
        assignStringFromCString(m_name, "Not opened");
    }


    int RESOURCE::open(std::FILE* file, RESOURCE::ResTypes::Type res_type)
    {
        if (m_file)
            close();

        m_file = file;
        if (!file)
        {
            logResourceError(m_name, m_type, 7, "file is NULL", 0);
            return 1;
        }

        m_rootBegin = static_cast<std::uint32_t>(std::ftell(file));
        if (read(&m_signature, 4) != 0)
        {
            logResourceError(m_name, m_type, 5, "empty file", 0);
            close();
            return 4;
        }

        if (m_signature != RESOURCE::ResTypes::RES && m_signature != RESOURCE::ResTypes::RIFF)
        {
            logResourceError(m_name, m_signature, 4, "resource signature", 0);
            close();
            return 2;
        }

        read(&m_rootEnd, 4);
        m_rootEnd += m_rootBegin + 8u;
        const long realLength = fileLength(m_file);
        if (realLength < static_cast<long>(m_rootEnd))
        {
            logResourceError(m_name,
                             m_type,
                             10,
                             "Invalid filelength",
                             static_cast<int>(realLength - static_cast<long>(m_rootEnd)));
        }

        RESOURCE::ResTypes::Type rootType = 0;
        read(&rootType, 4);
        if (rootType != res_type && res_type != RESOURCE::ResTypes::ANY)
        {
            m_type = rootType;
            logResourceError(m_name, rootType, 4, "resource type", 0);
            close();
            return 3;
        }

        GoBegin(RESOURCE::ResTypes::ANY);
        return 0;
    }


    int RESOURCE::openFile(const STRING* name, RESOURCE::ResTypes::Type res_type)
    {
        if (m_file)
            close();

        std::FILE* file = nullptr;
        if (name->c_str()[0] != '\0')
            file = std::fopen(name->c_str(), "rb");
        if (!file)
        {
            logResourceError(m_name, m_type, 7, name->c_str(), 0);
            return 1;
        }

        assignStringFromString(m_name, *name);
        return open(file, res_type);
    }


    int RESOURCE::OpenForWrite(const STRING* name, RESOURCE::ResTypes::Type res_type)
    {
        if (m_file)
            close();

        m_file = name->c_str()[0] != '\0' ? std::fopen(name->c_str(), "w+b") : nullptr;
        if (!m_file)
        {
            logResourceError(m_name, m_type, 3, name->c_str(), 0);
            return 1;
        }

        m_rootBegin = 0;
        m_rootEnd = 12;
        m_type = 0;
        assignStringFromString(m_name, *name);
        m_signature = RESOURCE::ResTypes::RES;
        write(&m_signature, 4);
        std::uint32_t rootSize = 4;
        write(&rootSize, 4);
        write(&res_type, 4);
        GoBegin(RESOURCE::ResTypes::ANY);
        return 0;
    }


    int RESOURCE::ReadPacked(void* data, unsigned size, Filter* filter)
    {
        if (!m_file || size == 0)
            return static_cast<int>(size);
        if (!filter)
            return read(data, size);
        return static_cast<int>(size) - filter->Decode(data, static_cast<unsigned long>(size), m_file);
    }


    int RESOURCE::WritePacked(const void* data, unsigned size, Filter* filter)
    {
        if (!m_file || size == 0)
            return static_cast<int>(size);
        if (!filter)
            return write(data, size);
        const int encoded = filter->Encode(data, static_cast<unsigned long>(size), m_file);
        m_packedDiff += static_cast<std::uint32_t>(static_cast<int>(size) - encoded);
        return 0;
    }


    int RESOURCE::GetNoSubRes(RESOURCE::ResTypes::Type typ)
    {
        if (!m_file)
            return 0;
        int count = 0;
        if (GoBegin(typ) == 0)
        {
            do
                count += static_cast<int>(m_currentSubresourceCount);
            while (GoNext(typ) == 0);
        }
        return count;
    }


    int RESOURCE::Load(RESOURCE::ResTypes::Type typ, void** data, int elementSize)
    {
        if (!m_file)
        {
            logResourceError(m_name, m_type, 5, "file not opened", 0);
            std::exit(1);
        }

        const int count = GetNoSubRes(typ);
        if (count == 0)
        {
            logResourceError(m_name, typ, 11, "Load", static_cast<int>(typ));
            std::exit(1);
        }
        GoBegin(typ);

        if (*data)
        {
            logResourceError(m_name, m_type, 5, "Already loaded", 0);
        }
        else
        {
            *data = ::operator new(static_cast<std::size_t>(elementSize) * static_cast<std::size_t>(count), std::nothrow);
        }

        if (!*data)
        {
            char typText[5];
            fourccText(typ, typText);
            fatalLogError(g_fileLogger, "ResLoad::type=%.4s no_sub=%i Not enough Memory", typText, count);
        }

        BYTE* out = static_cast<BYTE*>(*data);
        for (int i = 0; i < count; ++i)
        {
            read(out + static_cast<std::size_t>(i) * static_cast<std::size_t>(elementSize), static_cast<unsigned>(elementSize));
            GoNextSub(typ);
        }
        return count;
    }


    int RESOURCE::Copy(RESOURCE* sourceOwner, RESOURCE::ResTypes::Type typ)
    {
        RESOURCE& source = *sourceOwner;
        if (!m_file || !source.m_file)
            return 1;
        if (source.GoBegin(typ) != 0)
            return 0;

        BeginSection(typ, 0);
        m_currentSubresourceCount = 0;
        for (;;)
        {
            const std::uint32_t copySize = source.m_sectionSize;
            m_currentSubresourceCount += source.m_currentSubresourceCount;
            void* buffer = ::operator new(copySize, std::nothrow);
            if (!buffer)
                return 1;
            source.read(buffer, copySize);
            write(buffer, copySize);
            ::operator delete(buffer);
            if (source.GoNext(typ) != 0)
                break;
        }
        --m_currentSubresourceCount;
        EndSection();
        return 0;
    }


    int RESOURCE::GoBegin(ResTypes::Type typ)
    {
        if (!m_file)
            return 1;
        m_sectionPosition = m_rootBegin + 4u;
        m_sectionSize = 0;
        return GoNext(typ);
    }


    int RESOURCE::GoNext(ResTypes::Type typ)
    {

        if (!m_file)
            return 1;

        for (;;)
        {
            const std::uint32_t aligned = (m_sectionSize + 1u) & ~1u;
            const std::uint32_t next = m_sectionPosition + aligned + 8u;
            m_state = RESOURCE::st_seek;
            m_sectionPosition = next;
            std::fseek(m_file, static_cast<long>(next), SEEK_SET);


            if (static_cast<std::int32_t>(m_sectionPosition) >=
                static_cast<std::int32_t>(m_rootEnd))
            {
                m_sectionPosition -= aligned + 8u;
                m_state = RESOURCE::st_seek;
                if (m_signature == RESOURCE::ResTypes::RES)
                    std::fseek(m_file, static_cast<long>(m_sectionPosition + 24u), SEEK_SET);
                else
                    std::fseek(m_file, static_cast<long>(m_sectionPosition + 8u), SEEK_SET);
                return 2;
            }

            read(&m_type, 4);
            read(&m_sectionSize, 4);
            if (m_signature == RESOURCE::ResTypes::RES)
            {
                read(&m_options, 4);
                if ((m_options & RESOURCE::OPT_OPT_EXIST) != 0)
                {
                    read(&m_packedDiff, 4);
                    read(&m_currentSubresourceCount, 4);
                }
                else
                {
                    m_currentSubresourceCount = m_options;
                    m_options = 0;
                }
                m_currentSubresourcePosition = static_cast<std::uint32_t>(std::ftell(m_file));
                read(&m_currentSubresourceSize, 4);
            }

            if (typ == m_type || typ == RESOURCE::ResTypes::ANY)
                return 0;
        }
    }


    int RESOURCE::GoNextSub(ResTypes::Type typ)
    {
        if (!m_file)
            return -1;

        m_currentSubresourcePosition += m_currentSubresourceSize + 4u;
        const std::uint32_t sectionEnd = m_sectionPosition + m_sectionSize + 8u;

        if (static_cast<std::int32_t>(m_currentSubresourcePosition) <
            static_cast<std::int32_t>(sectionEnd))
        {
            m_state = RESOURCE::st_seek;
            std::fseek(m_file, static_cast<long>(m_currentSubresourcePosition), SEEK_SET);
            read(&m_currentSubresourceSize, 4);
            return 0;
        }
        return GoNext(typ);
    }

    int RESOURCE::GetBytesToEndSub() const
    {
        if (!m_file)
            return 0;
        const long pos = std::ftell(m_file);
        const std::uint32_t end = m_currentSubresourcePosition + 4u + m_currentSubresourceSize;
        return pos >= 0 && static_cast<std::uint32_t>(pos) < end ? static_cast<int>(end - static_cast<std::uint32_t>(pos)) : 0;
    }


    int RESOURCE::SubLoad(void** data, Filter* packer)
    {
        (void)packer;
        if (!m_file)
        {
            logResourceError(m_name, m_type, 5, "file not opened", 0);
            return 0;
        }
        if (static_cast<std::int32_t>(m_currentSubresourceSize) <= 0)
        {
            logResourceError(m_name, m_type, 11, "SubLoad", 0);
            return 0;
        }

        void* loaded = ::operator new(m_currentSubresourceSize, std::nothrow);
        *data = loaded;
        if (!loaded)
        {
            logResourceError(m_name, m_type, 2, "Subload data", static_cast<int>(m_currentSubresourceSize));
            return 0;
        }
        if (read(loaded, m_currentSubresourceSize) != 0)
            logResourceError(m_name, m_type, 5, "Subload", 0);
        return static_cast<int>(m_currentSubresourceSize);
    }


    int RESOURCE::BeginSection(ResTypes::Type typ, std::uint32_t options)
    {
        if (!m_file)
            return -1;

        if (options)
            m_options |= 0x00000100u;

        if (GoNext(RESOURCE::ResTypes::ANY) == 0)
        {
            while (GoNext(RESOURCE::ResTypes::ANY) == 0)
            {
            }
        }

        if (m_type != typ)
        {
            m_type = typ;
            m_rootEnd += 20u;
            m_sectionPosition += ((m_sectionSize + 1u) & ~1u) + 8u;
            m_currentSubresourceCount = 0;
            m_options = 0;
            m_sectionSize = 12;
        }

        m_currentSubresourceSize = 0;
        m_packedDiff = 0;
        m_currentSubresourcePosition = m_sectionPosition + m_sectionSize + 8u;
        m_state = RESOURCE::st_seek;
        std::fseek(m_file, static_cast<long>(m_sectionPosition + m_sectionSize + 12u), SEEK_SET);
        return 0;
    }


    int RESOURCE::EndSection()
    {
        m_options |= RESOURCE::OPT_OPT_EXIST;
        ++m_currentSubresourceCount;
        const std::uint32_t payloadBegin = m_currentSubresourcePosition;
        const long filePos = std::ftell(m_file);
        m_flags |= 1u;
        m_currentSubresourceSize = static_cast<std::uint32_t>(filePos - static_cast<long>(payloadBegin) - 4L);
        m_state = RESOURCE::st_seek;
        m_sectionSize += m_currentSubresourceSize + 4u;
        m_rootEnd += m_currentSubresourceSize + 4u;

        std::fseek(m_file, static_cast<long>(payloadBegin), SEEK_SET);
        m_currentSubresourcePosition += m_currentSubresourceSize + 4u;
        write(&m_currentSubresourceSize, 4);

        m_state = RESOURCE::st_seek;
        std::fseek(m_file, static_cast<long>(m_sectionPosition), SEEK_SET);
        write(&m_type, 4);
        write(&m_sectionSize, 4);
        write(&m_options, 4);
        write(&m_packedDiff, 4);
        const int result = write(&m_currentSubresourceCount, 4);
        m_options &= ~0x00000100u;
        return result;
    }

    size_t RESOURCE::seek(size_t pos)
    {
        if (!m_file)
            return 0;
        m_state = st_seek;
        std::fseek(m_file, static_cast<long>(pos), SEEK_SET);
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }

    size_t RESOURCE::shift(int delta)
    {
        if (!m_file)
            return 0;
        m_state = st_seek;
        std::fseek(m_file, delta, SEEK_CUR);
        return position();
    }

    size_t RESOURCE::position() const
    {
        if (!m_file)
            return 0;
        const long current = std::ftell(m_file);
        return current >= 0 ? static_cast<size_t>(current) : 0;
    }


    int RESOURCE::read(void* buf, unsigned size)
    {
        unsigned missing = size;
        if (m_file && size)
        {
            if (m_state == st_write)
            {
                m_state = st_seek;
                std::fseek(m_file, 0, SEEK_CUR);
            }
            m_state = st_read;
            missing -= static_cast<unsigned>(std::fread(buf, 1, size, m_file));
        }
        return static_cast<int>(missing);
    }

    int RESOURCE::write(const void* buf, unsigned size)
    {
        unsigned missing = size;
        if (m_file && size)
        {
            if (m_state == st_read)
            {
                m_state = st_seek;
                std::fseek(m_file, 0, SEEK_CUR);
            }
            m_state = st_write;
            missing -= static_cast<unsigned>(std::fwrite(buf, 1, size, m_file));
        }
        return static_cast<int>(missing);
    }
}
