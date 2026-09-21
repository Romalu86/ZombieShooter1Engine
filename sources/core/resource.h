#pragma once
#include "types.h"
#include "as_string.h"
#include "base_stream.h"
#include "resource_filter.h"

#include <cstdio>

namespace as1
{
    class Filter;

    class RESOURCE : public BaseStream
    {
    public:
        struct ResTypes
        {
            using Type = std::uint32_t;
            static constexpr Type DATA       = AS_FOURCC('D', 'A', 'T', 'A');
            static constexpr Type ANY        = AS_FOURCC('A', 'N', 'Y', ' ');
            static constexpr Type CONSTANT   = AS_FOURCC('C', 'N', 'S', 'T');
            static constexpr Type DEMO       = AS_FOURCC('D', 'E', 'M', 'O');
            static constexpr Type MAP        = AS_FOURCC('M', 'A', 'P', ' ');
            static constexpr Type GRAPH      = AS_FOURCC('G', 'R', 'P', 'H');
            static constexpr Type HEAD       = AS_FOURCC('H', 'E', 'A', 'D');
            static constexpr Type GRID       = AS_FOURCC('G', 'R', 'I', 'D');
            static constexpr Type SPRITE     = AS_FOURCC('S', 'P', 'R', ' ');
            static constexpr Type SPRITEDATA = AS_FOURCC('S', 'P', 'R', 'D');
            static constexpr Type PLAY       = AS_FOURCC('P', 'L', 'A', 'Y');
            static constexpr Type GROUP      = AS_FOURCC('G', 'R', 'O', 'U');
            static constexpr Type WEAPON     = AS_FOURCC('W', 'E', 'A', 'P');
            static constexpr Type OBJECT     = AS_FOURCC('O', 'B', 'J', ' ');
            static constexpr Type VID        = AS_FOURCC('V', 'I', 'D', ' ');
            static constexpr Type MENU       = AS_FOURCC('M', 'E', 'N', 'U');
            static constexpr Type SPRI       = AS_FOURCC('S', 'P', 'R', 'I');
            static constexpr Type SURFACE    = AS_FOURCC('S', 'U', 'R', 'F');
            static constexpr Type SHADOW     = AS_FOURCC('S', 'H', 'A', 'D');
            static constexpr Type PALETTE    = AS_FOURCC('P', 'A', 'L', ' ');
            static constexpr Type RES        = AS_FOURCC('R', 'E', 'S', ' ');
            static constexpr Type RIFF       = AS_FOURCC('R', 'I', 'F', 'F');
            static constexpr Type SFX        = AS_FOURCC('S', 'F', 'X', ' ');
            static constexpr Type WAVE       = AS_FOURCC('W', 'A', 'V', 'E');
            static constexpr Type WAVE_FMT   = AS_FOURCC('f', 'm', 't', ' ');
            static constexpr Type WAVE_DATA  = AS_FOURCC('d', 'a', 't', 'a');
            static constexpr Type CADR       = AS_FOURCC('C', 'A', 'D', 'R');
            static constexpr Type UNKNOWN    = 0;
        };

        RESOURCE();
        ~RESOURCE() override;

        const STRING& name() const { return m_name; }
        ResTypes::Type type() const { return m_type; }

        int SubSize() const
        {
            return static_cast<int>(m_signature == ResTypes::RIFF ? m_sectionSize : m_currentSubresourceSize);
        }


        int open(std::FILE* file, ResTypes::Type res_type);
        int openFile(const STRING* name, ResTypes::Type res_type);
        int OpenForWrite(const STRING* name, ResTypes::Type res_type);
        void close();

        int GoBegin(ResTypes::Type typ = ResTypes::ANY);
        int GoNext(ResTypes::Type typ = ResTypes::ANY);

        int GoNextSub(ResTypes::Type typ = ResTypes::ANY);
        int GetNoSubRes(ResTypes::Type typ);
        int GetBytesToEndSub() const;
        int ReadPacked(void* data, unsigned size, Filter* packer = nullptr);
        int WritePacked(const void* data, unsigned size, Filter* packer = nullptr);
        int SubLoad(void** data, Filter* packer = nullptr);
        int Load(ResTypes::Type typ, void** data, int elementSize);
        int Copy(RESOURCE* source, ResTypes::Type typ);

        int BeginSection(ResTypes::Type typ, std::uint32_t options = 0);
        int EndSection();

        std::uint32_t CurrentSubCount() const { return m_currentSubresourceCount; }
        std::uint32_t CurrentOptions() const { return m_options; }
        std::uint32_t CurrentPackedDiff() const { return m_packedDiff; }
        size_t CurrentResourceSize() const { return m_sectionSize; }
        size_t CurrentResourcePosition() const { return m_sectionPosition; }

        int read(void* buf, unsigned size) override;
        int write(const void* buf, unsigned size) override;

        size_t seek(size_t pos);
        size_t shift(int delta);
        __forceinline void shiftCurrentUnchecked(int delta) noexcept
        {
            m_state = st_seek;
            std::fseek(m_file, delta, SEEK_CUR);
        }
        size_t position() const;
        size_t length() const { return m_sectionSize; }
        bool isOpen() const { return m_file != nullptr; }
        bool isWritable() const { return m_file != nullptr && (m_flags & 1u) != 0; }
        std::FILE* nativeFile() const { return m_file; }

    private:

        static constexpr std::uint32_t OPT_OPT_EXIST = 0x80000000u;
        enum State : std::uint32_t { st_write = 0, st_read = 1, st_seek = 2 };

        std::uint32_t m_flags;
        std::uint32_t m_state;
        STRING m_name;
        ResTypes::Type m_signature;
        std::uint32_t m_sectionSize;
        std::uint32_t m_sectionPosition;
        std::uint32_t m_rootBegin;
        std::uint32_t m_rootEnd;
        std::uint32_t m_options;
        std::uint32_t m_currentSubresourceCount;
        std::uint32_t m_currentSubresourcePosition;
        std::uint32_t m_currentSubresourceSize;
        std::uint32_t m_packedDiff;
        std::FILE* m_file;
        ResTypes::Type m_type;
    };


}
