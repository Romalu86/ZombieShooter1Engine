#include "sprite.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "vid/vid_software.h"
#include "map.h"
#include "win/application_win.h"
#include "graph.h"
#include "graphics/gamma.h"
#include "graphics/color.h"
#include "graphics/base_texture.h"
#include "sprite_collector.h"
#include "rail.h"
#include "mouse.h"
#include "core/application.h"
#include "constant.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/configuration.h"
#include "core/profile_p.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/weak_controller.h"
#include "sound/sound_engine.h"
#include <array>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <new>
#include <memory>
#include <utility>
#include <unordered_map>
#include <limits>
#include <intrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>


namespace as1
{


namespace
    {
        class CommandWordListVtableOwner
        {
        public:
            virtual void* deletingDestructor(unsigned char deleteSelfFlag) noexcept
            {
                auto* const raw = reinterpret_cast<std::uint32_t*>(this);
                raw[0] = currentVtable();
                void* const data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw[3]));
                if (data)
                    ::operator delete(data);
                raw[3] = 0u;
                raw[1] = 0u;
                if ((deleteSelfFlag & 1u) != 0u)
                    ::operator delete(static_cast<void*>(this));
                return this;
            }

            static std::uint32_t currentVtable() noexcept
            {
                static CommandWordListVtableOwner owner;
                return *reinterpret_cast<const std::uint32_t*>(&owner);
            }
        };

        class ActListVtableOwner
        {
        public:
            virtual void* deletingDestructor(unsigned char deleteSelfFlag) noexcept
            {
                auto* const raw = reinterpret_cast<std::uint32_t*>(this);
                raw[0] = currentVtable();
                void* const data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw[3]));
                if (data)
                    ::operator delete(data);
                raw[3] = 0u;
                raw[1] = 0u;
                if ((deleteSelfFlag & 1u) != 0u)
                    ::operator delete(static_cast<void*>(this));
                return this;
            }

            static std::uint32_t currentVtable() noexcept
            {
                static ActListVtableOwner owner;
                return *reinterpret_cast<const std::uint32_t*>(&owner);
            }
        };
    }


    int Random(int interval)
    {
        if (interval <= 0)
            return 0;
        return std::rand() % (interval + 1);
    }


    ACT* copyCommandRecord(ACT* destination, const ACT* source) noexcept
    {
        destination->opcode = source->opcode;
        destination->argument1 = source->argument1;
        destination->argument2 = source->argument2;
        destination->argument3 = source->argument3;
        return destination;
    }


    __declspec(noinline)
    int SPRITE::IsActionStackEmpty() const noexcept
    {
        if (m_commandStack.m_commandRecords.count == 0u)
            return 1;
        const std::uint32_t last = m_commandStack.m_commandRecords.count - 1u;
        return m_commandStack.m_commandRecords.records[last].words[0] == 0x49u ? 1 : 0;
    }


    __declspec(noinline)
    core::List<ACT>* SPRITE::ActionStack() noexcept
    {
        return &m_commandStack;
    }

    int g_pathSearchSecondaryBestCost = 0;
    int g_pathSearchResultScore = 0;

    namespace
    {

        const char kEmptyString[] = "";
        const char kCommandRecordDelimiter[] = ";";
        const char kCommandWordPrefixMarker[] = { '\x01', '\0' };
        constexpr unsigned short kTruncateRoundingModeBits = static_cast<unsigned short>(3u << 10);
        constexpr unsigned short kRoundingModeClearMask = static_cast<unsigned short>(~kTruncateRoundingModeBits);

        enum class InternalActionCode : std::uint32_t
        {
            SetAnimationAndDirection = 201u,
            GetAnimation = 202u,
            ChangeCoordinateXY = 203u,
            ChangeCoordinateZ = 204u,
        };

        unsigned char shortestAngleDistance(int lhs, int rhs) noexcept
        {
            const unsigned char a = static_cast<unsigned char>(lhs);
            const unsigned char b = static_cast<unsigned char>(rhs);
            const unsigned char clockwise = static_cast<unsigned char>(a - b);
            const unsigned char counterClockwise = static_cast<unsigned char>(b - a);
            return clockwise < counterClockwise ? clockwise : counterClockwise;
        }


        constexpr char kTrainCollapseBeginLog[] =
            "\361\365\353\340\357\373\342\340\355\350\345 "
            "\342\340\343\356\355\356\342 zm-error - kawabanga - begin";

        constexpr char kMissingTailDot2ResourceError[] =
            "\360\345\353\374\361\373 \355\345\357\360\340\342\350\353\374\355\373\345 "
            "\355\345\362 tail.Dot2()";
        constexpr char kMissingLinkResourceError[] =
            "\360\345\353\374\361\373 \355\345\357\360\340\342\350\353\374\355\373\345 "
            "link>nolink";

        int g_collisionPushRecursionDepth = 0;

        std::array<std::uint32_t, 1024> g_directionTrigWindow = {{
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
            0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
            0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
            0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
            0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
            0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
            0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
            0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
            0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
            0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
            0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
            0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
            0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
            0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
            0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
            0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
            0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
            0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
            0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
            0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
            0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
            0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
            0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
            0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
            0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
            0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
            0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
            0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
            0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
            0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
            0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
            0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
            0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        }};

        float spriteFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }


        float directionSin(int index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionSinUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[index]);
        }

        float directionCos(int index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[256u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosUnchecked(std::uint32_t index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[256u + index]);
        }

        float directionSinAux(int index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[512u + static_cast<std::size_t>(index & 0xFF)]);
        }

        float directionCosAux(int index)
        {
            return spriteFloatFromBits(g_directionTrigWindow[768u + static_cast<std::size_t>(index & 0xFF)]);
        }

        std::int32_t spriteImul32Low(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) *
                    static_cast<std::uint32_t>(b)));
        }

        std::int32_t spriteAdd32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t spriteSub32Wrap(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        int spriteConvertFloatToInt32(long double value) noexcept;

        float spriteFildMulF32(std::int32_t value, float scale) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(value) * static_cast<long double>(scale));
        }

        float spriteFildToF32(std::int32_t value) noexcept
        {
            return static_cast<float>(value);
        }

        float spriteFildAddF32(std::int32_t value, float addend) noexcept
        {
            return static_cast<float>(
                static_cast<long double>(value) + static_cast<long double>(addend));
        }

        float spriteFildSubF32(std::int32_t value, float subtrahend) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(value) - static_cast<long double>(subtrahend));
        }

        float addThenSubtractRounded(float base, float addend, float subtractend) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(base) +
                static_cast<long double>(addend) -
                static_cast<long double>(subtractend));
        }

        float spriteWeightedQuarterF32(float primary, float secondary) noexcept
        {


            const float weighted = primary * 3.0f;
            const float summed = weighted + secondary;
            return summed * 0.25f;
        }

        int spriteAddRoundedFloatAndConvertToInt32(float value, float addend,
                                          float& storedValue) noexcept
        {

            const long double extended =
                static_cast<long double>(value) + static_cast<long double>(addend);
            storedValue = static_cast<float>(extended);
            return spriteConvertFloatToInt32(extended);
        }

        bool spriteFcompC3(float lhs, float rhs) noexcept
        {
            return lhs == rhs;
        }

        int spriteConvertFloatToInt32(long double value) noexcept
        {

            if (!std::isfinite(value) ||
                value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            return static_cast<int>(static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(value)))));
        }

        int spriteSubtractAndConvertToInt32(float lhs, float rhs) noexcept
        {

            return spriteConvertFloatToInt32(static_cast<long double>(lhs) -
                                   static_cast<long double>(rhs));
        }

        int spriteSubtractRoundedFloatAndConvertToInt32(float lhs, float rhs) noexcept
        {
            const float rounded = static_cast<float>(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            return spriteConvertFloatToInt32(static_cast<long double>(rounded));
        }

        int spriteMultiplyAndConvertToInt32(float value, float multiplier) noexcept
        {
            return spriteConvertFloatToInt32(static_cast<long double>(value) *
                                   static_cast<long double>(multiplier));
        }

        int spriteDivideMultiplyAndConvertToInt32(float numerator, float denominator, float multiplier) noexcept
        {
            return spriteConvertFloatToInt32(static_cast<long double>(numerator) /
                                   static_cast<long double>(denominator) *
                                   static_cast<long double>(multiplier));
        }

        float spriteFildMulStoreFloat(int value, float multiplier) noexcept
        {
            return static_cast<float>(static_cast<long double>(value) *
                                      static_cast<long double>(multiplier));
        }

        int pathScaledProgressQuotient(int progress, int delta, int duration) noexcept
        {

            std::uint32_t product =
                static_cast<std::uint32_t>(progress) * static_cast<std::uint32_t>(delta);
            product <<= 8;
            return static_cast<std::int32_t>(product) / duration;
        }

        float pathInterpolateCoordinate(int quotient, int base) noexcept
        {

            return static_cast<float>(
                static_cast<long double>(quotient) +
                static_cast<long double>(base) * 256.0L);
        }

        float pathAverageCoordinate(float lhs, float rhs) noexcept
        {

            return static_cast<float>(
                (static_cast<long double>(lhs) + static_cast<long double>(rhs)) *
                0.001953125L);
        }

        int pathDirectionDeltaXToInt(float lhs, float rhs) noexcept
        {

            return spriteConvertFloatToInt32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                0.00390625L);
        }

        int pathDirectionDeltaYToInt(float lhs, float rhs) noexcept
        {

            return spriteConvertFloatToInt32(
                (static_cast<long double>(lhs) - static_cast<long double>(rhs) + 128.0L) *
                3.0L * 0.001953125L);
        }

        int animationDelayFromSpeed(float speed) noexcept
        {

            const std::uint32_t scaled = static_cast<std::uint32_t>(
                spriteMultiplyAndConvertToInt32(speed, 1000.0f));
            const std::uint32_t sign = 0u - (scaled >> 31);
            const std::uint32_t magnitude = (scaled ^ sign) - sign;
            const std::uint32_t plusTen = magnitude + 10u;
            const std::uint32_t sign2 = 0u - (plusTen >> 31);
            const std::uint32_t adjusted = plusTen - sign2;
            const std::uint32_t half = (adjusted >> 1) | (adjusted & 0x80000000u);
            return static_cast<std::int32_t>(0u - half);
        }

        bool spriteFildIntLessEqualOrUnordered(std::int32_t lhs, float rhs) noexcept
        {

            return std::isnan(rhs) ||
                   static_cast<long double>(lhs) <= static_cast<long double>(rhs);
        }

        void computeCollisionKinematics(float thisSpeedRaw, float targetSpeedRaw,
                                              float thisWeight, float targetWeight, int mode,
                                              float& sharedSpeedOut, float& relativeSpeedOut) noexcept
        {


            const long double thisSpeed = std::fabs(static_cast<long double>(thisSpeedRaw));
            const long double targetSpeed = std::fabs(static_cast<long double>(targetSpeedRaw));
            const long double numerator =
                static_cast<long double>(targetWeight) * targetSpeed +
                static_cast<long double>(thisWeight) * thisSpeed;
            const long double denominator =
                static_cast<long double>(targetWeight) + static_cast<long double>(thisWeight);
            const long double sharedExtended = numerator / denominator;
            sharedSpeedOut = static_cast<float>(sharedExtended);
            if (!std::isnan(sharedExtended) && sharedExtended > 0.001L &&
                (sharedSpeedOut < 0.01f || std::isnan(sharedSpeedOut)))
                sharedSpeedOut = 0.01f;
            const long double relative = (mode == 2 || mode == 3)
                ? std::fabs(thisSpeed - targetSpeed)
                : targetSpeed + thisSpeed;
            relativeSpeedOut = static_cast<float>(relative);
        }

        bool projectVerticalMotionDirection(int direction, float speed, float zSpeed,
                                            int& projectedDirection) noexcept
        {
            const float projectedX =
                (directionSin(direction) * speed) * 1000000.0f;
            const float projectedY =
                ((directionCos(direction) * speed) + zSpeed) * -1000000.0f;
            projectedDirection = DirectionFromFloatXY(projectedX, projectedY).Int();
            return true;
        }

        float trainEndpointMetric(float x, float y, float nodeX, float nodeY) noexcept
        {


            const float dx = std::fabs(x - nodeX);
            const float dy = std::fabs(y - nodeY);
            return !(dx > dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
        }

        bool preferFirstTrainEndpoint(float x, float y,
                                      float prevX, float prevY,
                                      float nextX, float nextY) noexcept
        {
            const float firstMetric = trainEndpointMetric(x, y, prevX, prevY);
            const float lastMetric = trainEndpointMetric(x, y, nextX, nextY);


            return lastMetric > firstMetric;
        }

        bool x87IsZeroOrUnordered(float value) noexcept
        {

            return value == 0.0f || std::isnan(value);
        }

        bool x87EqualOrUnordered(float value, float reference) noexcept
        {

            return value == reference || std::isnan(value) || std::isnan(reference);
        }

        bool x87LessOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        __forceinline bool x87LessEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return lhs <= rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        bool x87OrderedLess(float lhs, float rhs) noexcept
        {
            return lhs < rhs;
        }

        bool x87OrderedGreater(float lhs, float rhs) noexcept
        {

            return lhs > rhs;
        }

        bool x87AbsDiffGreaterOrdered(float lhs, float rhs, float limit) noexcept
        {

            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound = static_cast<long double>(limit);
            return !std::isnan(diff) && !std::isnan(bound) && diff > bound;
        }

        bool metricWithinFromRoundedDeltas(float deltaX, float deltaY, float radius) noexcept
        {


            const long double ax = std::fabs(static_cast<long double>(deltaX));
            const long double ay = std::fabs(static_cast<long double>(deltaY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
        }

        bool metricWithinPositions(float ownerX, float ownerY,
                                             float targetX, float targetY,
                                             float radius) noexcept
        {

            const long double ax = std::fabs(
                static_cast<long double>(ownerX) - static_cast<long double>(targetX));
            const long double ay = std::fabs(
                static_cast<long double>(ownerY) - static_cast<long double>(targetY));
            const long double metric =
                (ax <= ay || std::isnan(ax) || std::isnan(ay))
                    ? ax * 0.5L + ay
                    : ax + ay * 0.5L;
            const long double limit = static_cast<long double>(radius) - 10.0L;
            return metric <= limit || std::isnan(metric) || std::isnan(limit);
        }

        bool spriteBitsEqual(float value, std::uint32_t bits) noexcept
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw == bits;
        }

        bool advanceAccelerationStep(std::int32_t deltaMs, float factor,
                                    float maxSpeed, float& speed) noexcept
        {
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 value = _mm_mul_ss(delta, _mm_set_ss(factor));
            value = _mm_add_ss(value, _mm_set_ss(speed));
            speed = _mm_cvtss_f32(value);
            return speed >= maxSpeed;
        }

        bool advanceDecelerationStep(std::int32_t deltaMs, float factor, float& speed) noexcept
        {
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 amount = _mm_mul_ss(delta, _mm_set_ss(factor));
            __m128 value = _mm_sub_ss(_mm_set_ss(speed), amount);
            speed = _mm_cvtss_f32(value);
            return speed < 0.0f;
        }

        void advancePlanarPosition(std::int32_t deltaMs, float speed,
                                float sinValue, float cosValue,
                                float& x, float& y) noexcept
        {
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 step = _mm_mul_ss(delta, _mm_set_ss(speed));
            const __m128 dx = _mm_mul_ss(step, _mm_set_ss(sinValue));
            const __m128 dy = _mm_mul_ss(step, _mm_set_ss(cosValue));
            x = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(x), dx));
            y = _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(y), dy));
        }

        void applyGravityStep(std::int32_t deltaMs, float gravity, float& zSpeed) noexcept
        {
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 amount = _mm_mul_ss(delta, _mm_set_ss(gravity));
            zSpeed = _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(zSpeed), amount));
        }

        void advanceVerticalPosition(std::int32_t deltaMs, float zSpeed, float& z) noexcept
        {
            __m128 delta = _mm_cvtsi32_ss(_mm_setzero_ps(), deltaMs);
            __m128 step = _mm_mul_ss(delta, _mm_set_ss(zSpeed));
            z = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(z), step));
        }

        float applicationWorldFloatAt(std::size_t offset) noexcept
        {
            if (offset == core::application_layout::MapExtentX)
                return core::ApplicationMapWidth();
            if (offset == core::application_layout::MapExtentY)
                return core::ApplicationMapHeight();
            return 0.0f;
        }

        int computeRegionTileCount(float width, float height,
                                             float childSizeX, float childSizeY) noexcept
        {


            __m128 value = _mm_mul_ss(_mm_set_ss(width), _mm_set_ss(height));
            value = _mm_div_ss(value, _mm_set_ss(childSizeX));
            value = _mm_div_ss(value, _mm_set_ss(childSizeY));
            return _mm_cvtt_ss2si(value);
        }

        std::uint32_t computeChildAnimationCadence(int direction,
                                               float speed,
                                               float zSpeed,
                                               float childMaxZ,
                                               bool subtractGraphMotion,
                                               int graphDirection,
                                               float graphSpeed,
                                               float childSizeX,
                                               float childSizeY) noexcept
        {
            const std::uint32_t rawDirection = static_cast<std::uint32_t>(direction);
            const std::uint32_t rawGraphDirection = static_cast<std::uint32_t>(graphDirection);
            const float dirSin = spriteFloatFromBits(
                g_directionTrigWindow[512u + rawDirection]);
            const float dirCos = spriteFloatFromBits(
                g_directionTrigWindow[768u + rawDirection]);
            const float graphSin = spriteFloatFromBits(
                g_directionTrigWindow[512u + rawGraphDirection]);
            const float graphCos = spriteFloatFromBits(
                g_directionTrigWindow[768u + rawGraphDirection]);


            float projectedX = 0.0f;
            float projectedY = 0.0f;
            const __m128 speedV = _mm_set_ss(speed);
            __m128 xV = _mm_mul_ss(_mm_set_ss(dirSin), speedV);
            __m128 yV = _mm_sub_ss(_mm_set_ss(zSpeed), _mm_set_ss(childMaxZ));
            yV = _mm_add_ss(yV, _mm_mul_ss(_mm_set_ss(dirCos), speedV));
            if (subtractGraphMotion)
            {
                const __m128 windV = _mm_set_ss(graphSpeed);
                xV = _mm_sub_ss(xV, _mm_mul_ss(_mm_set_ss(graphSin), windV));
                yV = _mm_sub_ss(yV, _mm_mul_ss(_mm_set_ss(graphCos), windV));
            }
            projectedX = _mm_cvtss_f32(xV);
            projectedY = _mm_cvtss_f32(yV);


            const float xTime = projectedX == 0.0f
                ? 30000.0f
                : childSizeX / std::fabs(projectedX);
            const float yTime = projectedY == 0.0f
                ? 30000.0f
                : childSizeY / std::fabs(projectedY);


            const __m128 selected = _mm_min_ss(_mm_set_ss(xTime), _mm_set_ss(yTime));
            return static_cast<std::uint32_t>(_mm_cvtt_ss2si(selected));
        }

        std::int32_t spriteNeg32Wrap(std::int32_t value) noexcept
        {
            return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(value));
        }

        std::int32_t spriteAbs32Wrap(std::int32_t value) noexcept
        {
            const std::int32_t sign = value < 0 ? -1 : 0;
            return static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(value) ^ static_cast<std::uint32_t>(sign)) -
                static_cast<std::uint32_t>(sign));
        }

        bool x87SumGreaterThanAbsDiffOrdered(float boundA, float boundB,
                                             float lhs, float rhs) noexcept
        {
            const long double diff = std::fabs(
                static_cast<long double>(lhs) - static_cast<long double>(rhs));
            const long double bound =
                static_cast<long double>(boundA) + static_cast<long double>(boundB);
            return !std::isnan(diff) && !std::isnan(bound) && bound > diff;
        }

        bool x87SumLessOrUnordered(float lhsA, float lhsB, float rhs) noexcept
        {

            const long double sum =
                static_cast<long double>(lhsA) + static_cast<long double>(lhsB);
            const long double right = static_cast<long double>(rhs);
            return sum < right || std::isnan(sum) || std::isnan(right);
        }

        __forceinline bool f32SumGreaterThanAbsDiffOrdered(float boundA, float boundB,
                                             float lhs, float rhs) noexcept
        {


            const float delta = lhs - rhs;
            const float diff = std::fabs(delta);
            const float bound = boundA + boundB;
            return !std::isnan(diff) && !std::isnan(bound) && bound > diff;
        }

        __forceinline bool f32SumLessOrUnordered(float lhsA, float lhsB, float rhs) noexcept
        {


            const float sum = lhsA + lhsB;
            return sum < rhs || std::isnan(sum) || std::isnan(rhs);
        }

        int spriteTruncateFloatToInt32(float value) noexcept
        {

            if (!std::isfinite(value) ||
                value < -2147483648.0f || value >= 2147483648.0f)
            {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<std::int32_t>(value);
        }

        bool shouldSuppressFlagmanCommand(std::int32_t x, std::int32_t y,
                                                std::int32_t range,
                                                float controlledX,
                                                float controlledY) noexcept
        {
            const long double dx = std::fabs(
                static_cast<long double>(x) - static_cast<long double>(controlledX));
            const long double dy = std::fabs(
                static_cast<long double>(y) - static_cast<long double>(controlledY));
            const long double metric =
                (dx <= dy || std::isnan(dx) || std::isnan(dy))
                    ? dx * 0.5L + dy
                    : dx + dy * 0.5L;
            const long double threshold = static_cast<long double>(range);
            return metric < threshold || std::isnan(metric) || std::isnan(threshold);
        }

        bool computeFalloffDamage(float sourceX, float sourceY,
                                       float candidateX, float candidateY,
                                       float deathRange, std::int32_t damageRaw,
                                       int& damageOut) noexcept
        {


            const float dxRaw = candidateX - sourceX;
            const float dyRaw = candidateY - sourceY;
            const float dx = std::fabs(dxRaw);
            const float dy = std::fabs(dyRaw);
            const float metric = (dx <= dy || std::isnan(dx) || std::isnan(dy))
                ? dx * 0.5f + dy
                : dy * 0.5f + dx;


            if (std::isnan(deathRange) || std::isnan(metric) || deathRange < metric)
                return false;

            const float damage = static_cast<float>(damageRaw);
            const float product = damage * metric;
            const float quotient = product / deathRange;
            const float scaledDamage = damage - quotient;
            damageOut = spriteTruncateFloatToInt32(scaledDamage);
            return true;
        }

        void appendU32LE(std::vector<BYTE>& out, std::uint32_t v)
        {
            out.push_back(static_cast<BYTE>(v & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 8) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 16) & 0xFF));
            out.push_back(static_cast<BYTE>((v >> 24) & 0xFF));
        }

        std::vector<BYTE> wordsToBytes(const std::vector<std::uint32_t>& words)
        {
            std::vector<BYTE> out;
            out.reserve(words.size() * 4);
            for (std::uint32_t v : words)
                appendU32LE(out, v);
            return out;
        }

    }

    void SPRITE::initializeStartupTrigTables() noexcept
    {
        const float kScale4096 = 4096.0f;
        const float kScaleRadians = 0.00017262212f;
        for (std::size_t i = 0; i < 256u; ++i)
        {
            const float sourceSin = spriteFloatFromBits(g_directionTrigWindow[i]);
            const float sourceCosWindow = spriteFloatFromBits(g_directionTrigWindow[256u + i]);
            const float derivedSin = static_cast<float>(
                static_cast<long double>(sourceSin) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            const float derivedCosWindow = static_cast<float>(
                static_cast<long double>(sourceCosWindow) *
                static_cast<long double>(kScale4096) *
                static_cast<long double>(kScaleRadians));
            std::memcpy(&g_directionTrigWindow[512u + i], &derivedSin, sizeof(derivedSin));
            std::memcpy(&g_directionTrigWindow[768u + i], &derivedCosWindow, sizeof(derivedCosWindow));
        }
    }

    float SPRITE::rawDirectionSin(int index) noexcept
    {
        return directionSin(index);
    }

    float SPRITE::rawDirectionSinUnchecked(DWORD index) noexcept
    {
        return directionSinUnchecked(index);
    }

    float SPRITE::rawDirectionCos(int index) noexcept
    {
        return directionCos(index);
    }

    float SPRITE::rawDirectionSinAux(int index) noexcept
    {
        return directionSinAux(index);
    }

    float SPRITE::rawDirectionCosAux(int index) noexcept
    {
        return directionCosAux(index);
    }

    namespace
    {
        __forceinline
        std::uint32_t currentCommandWordListVtable() noexcept
        {
            return CommandWordListVtableOwner::currentVtable();
        }

        __forceinline
        std::uint32_t currentActListVtable() noexcept
        {
            return ActListVtableOwner::currentVtable();
        }
    }


    __declspec(noinline)
    int core::List<ACT>::No() const noexcept
    {
        return static_cast<int>(m_commandRecords.count);
    }


    __declspec(noinline)
    ACT* core::List<ACT>::operator[](int index) noexcept
    {
        return reinterpret_cast<ACT*>(m_commandRecords.records + index);
    }


    __forceinline void core::List<ACT>::releaseCommandRecords()
    {
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.vtableTag = currentActListVtable();
        m_commandRecords.count = 0;
        m_commandRecords.capacity = 0;
    }

    __forceinline void core::List<ACT>::releaseCommandRecordsTail()
    {
        m_commandRecords.vtableTag = currentActListVtable();
        if (m_commandRecords.records)
            ::operator delete(m_commandRecords.records);
        m_commandRecords.records = nullptr;
        m_commandRecords.count = 0;
    }

    __forceinline void core::List<ACT>::ensureCommandRecordCapacity(std::uint32_t requiredCapacity)
    {
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(m_commandRecords.capacity))
            return;

        CommandRecordStorage* const oldArray = m_commandRecords.records;
        const std::uint32_t oldCapacity = m_commandRecords.capacity;
        const std::uint32_t allocationBytes = requiredCapacity << 4;
        CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
            ::operator new(static_cast<std::size_t>(allocationBytes), std::nothrow));
        m_commandRecords.records = newArray;
        if (!m_commandRecords.records)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(requiredCapacity));
        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
        {


            for (std::uint32_t i = 0; i < oldCapacity; ++i)
                m_commandRecords.records[i] = oldArray[i];
        }
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.capacity = requiredCapacity;
    }

    __forceinline void core::List<ACT>::writeCommandRecord(std::size_t index, const ACT& command)
    {
        CommandRecordStorage& raw = m_commandRecords.records[index];
        raw.words[0] = command.opcode;
        raw.words[1] = command.argument1;
        raw.words[2] = command.argument2;
        raw.words[3] = command.argument3;
    }

    __forceinline void core::List<ACT>::copyCommandRecordsFrom(const core::List<ACT>& other)
    {
        releaseCommandRecords();
        m_commandRecords.capacity = 0u;
        m_commandRecords.vtableTag = other.m_commandRecords.vtableTag;
        if (other.m_commandRecords.count == 0)
            return;
        ensureCommandRecordCapacity(other.m_commandRecords.count);
        std::memcpy(m_commandRecords.records, other.m_commandRecords.records, other.m_commandRecords.count * sizeof(CommandRecordStorage));
        m_commandRecords.count = other.m_commandRecords.count;
    }


    __declspec(noinline)
    void core::List<ACT>::resize(std::uint32_t requiredCapacity)
    {
        if (static_cast<std::int32_t>(requiredCapacity) <=
            static_cast<std::int32_t>(m_commandRecords.capacity))
            return;

        CommandRecordStorage* const oldArray = m_commandRecords.records;
        const std::uint32_t oldCapacity = m_commandRecords.capacity;
        CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
            ::operator new(static_cast<std::size_t>(requiredCapacity) << 4, std::nothrow));
        m_commandRecords.records = newArray;
        if (!newArray)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                          static_cast<int>(requiredCapacity));

        if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
        {
            for (std::uint32_t i = 0; i < oldCapacity; ++i)
                newArray[i] = oldArray[i];
        }
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.capacity = requiredCapacity;
    }

    __forceinline void core::List<ACT>::setCommandRecordCount(std::uint32_t count)
    {

        m_commandRecords.count = count;
    }


    core::List<ACT>::List()
    {
        m_commandRecords.vtableTag = currentActListVtable();
    }

    core::List<ACT>::List(const core::List<ACT>& other)
    {
        copyCommandRecordsFrom(other);
    }

    core::List<ACT>& core::List<ACT>::operator=(const core::List<ACT>& other)
    {
        if (this != &other)
            copyCommandRecordsFrom(other);
        return *this;
    }

    core::List<ACT>::List(core::List<ACT>&& other) noexcept
        : m_commandRecords(other.m_commandRecords)
    {
        other.m_commandRecords.vtableTag = currentActListVtable();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
    }

    core::List<ACT>& core::List<ACT>::operator=(core::List<ACT>&& other) noexcept
    {
        if (this == &other)
            return *this;
        releaseCommandRecords();
        m_commandRecords = other.m_commandRecords;
        other.m_commandRecords.vtableTag = currentActListVtable();
        other.m_commandRecords.count = 0;
        other.m_commandRecords.capacity = 0;
        other.m_commandRecords.records = nullptr;
        return *this;
    }

    core::List<ACT>::~List()
    {
        releaseCommandRecords();
    }

    __forceinline void core::List<ACT>::clearTargetReferences(SPRITE* target)
    {
        if (!target || !m_commandRecords.records)
            return;

        const std::uint32_t targetBits = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(target) & 0xFFFFFFFFu);
        const std::uint32_t count = m_commandRecords.count;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            CommandRecordStorage& raw = m_commandRecords.records[i];
            if (raw.words[1] != targetBits)
                continue;

            const std::uint32_t opcode = raw.words[0];
            if (opcode != 32u && opcode != 34u && opcode != 74u &&
                opcode != 150u && opcode != 151u && opcode != 152u)
                continue;

            raw.words[0] = 255u;
            raw.words[1] = 0u;
        }
    }

    __forceinline void core::List<ACT>::clear()
    {
        CommandRecordStorage* const oldArray = m_commandRecords.records;
        m_commandRecords.capacity = 0;
        m_commandRecords.count = 0;
        if (oldArray)
            ::operator delete(oldArray);
        m_commandRecords.records = nullptr;
    }


    __declspec(noinline)
    void core::List<ACT>::append(ACT action)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
            {
                CommandRecordStorage* const oldArray = m_commandRecords.records;
                CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
                    ::operator new(static_cast<std::size_t>(grownCapacity) << 4, std::nothrow));
                m_commandRecords.records = newArray;
                if (!newArray)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                                  static_cast<int>(grownCapacity));
                if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
                {
                    for (std::uint32_t i = 0; i < oldCapacity; ++i)
                        newArray[i] = oldArray[i];
                }
                if (oldArray)
                    ::operator delete(oldArray);
                m_commandRecords.capacity = grownCapacity;
            }
        }

        CommandRecordStorage& raw = m_commandRecords.records[m_commandRecords.count];
        raw.words[0] = action.opcode;
        raw.words[1] = action.argument1;
        raw.words[2] = action.argument2;
        raw.words[3] = action.argument3;
        ++m_commandRecords.count;
    }


    __declspec(noinline)
    void core::List<ACT>::insertAt(std::uint32_t rawIndex, ACT action)
    {
        if (static_cast<std::int32_t>(m_commandRecords.count) >=
            static_cast<std::int32_t>(m_commandRecords.capacity))
        {
            const std::uint32_t oldCapacity = m_commandRecords.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
            {
                CommandRecordStorage* const oldArray = m_commandRecords.records;
                CommandRecordStorage* const newArray = static_cast<CommandRecordStorage*>(
                    ::operator new(static_cast<std::size_t>(grownCapacity) << 4, std::nothrow));
                m_commandRecords.records = newArray;
                if (!newArray)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                                  static_cast<int>(grownCapacity));
                if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
                {
                    for (std::uint32_t i = 0; i < oldCapacity; ++i)
                        newArray[i] = oldArray[i];
                }
                if (oldArray)
                    ::operator delete(oldArray);
                m_commandRecords.capacity = grownCapacity;
            }
        }

        const std::uint32_t oldCount = m_commandRecords.count;
        m_commandRecords.count = oldCount + 1u;
        if (static_cast<std::int32_t>(oldCount) > static_cast<std::int32_t>(rawIndex))
        {
            for (std::uint32_t i = oldCount; i > rawIndex; --i)
                m_commandRecords.records[i] = m_commandRecords.records[i - 1u];
        }
        CommandRecordStorage& raw = m_commandRecords.records[rawIndex];
        raw.words[0] = action.opcode;
        raw.words[1] = action.argument1;
        raw.words[2] = action.argument2;
        raw.words[3] = action.argument3;
    }


    __forceinline void core::List<ACT>::serializeCommandRecordsText(STRING& out) const
    {
        STRING serializedText;
        const int recordCount = static_cast<int>(m_commandRecords.count);
        for (int i = 0; i < recordCount; ++i)
        {
            const CommandRecordStorage& raw = m_commandRecords.records[static_cast<std::size_t>(i)];
            const unsigned char encoded = static_cast<unsigned char>((raw.words[0] + SpriteLayout::CommandSerializationBias) & 0xFFu);
            STRING recordText;
            constructFormattedString(recordText, "%c%i,%i,%i;",
                static_cast<char>(encoded),
                static_cast<int>(raw.words[1]),
                static_cast<int>(raw.words[2]),
                static_cast<int>(raw.words[3]));
            appendStringOwner(serializedText, recordText);
            recordText.ReleaseOwnedStorage();
        }
        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    __forceinline std::string core::List<ACT>::serializeCommandRecordsText() const
    {
        STRING out;
        serializeCommandRecordsText(out);
        return out.str();
    }

    __forceinline void core::List<ACT>::parseCommandRecordsText(const STRING& text)
    {
        STRING remainingText(text);
        std::uint32_t encodedOpcodeWord = 0;
        int parsedArgument1;
        int parsedArgument2;
        int parsedArgument3;
        while (std::strcmp(remainingText.c_str(), kEmptyString) != 0)
        {
            char* const encodedOpcodeByte = reinterpret_cast<char*>(&encodedOpcodeWord);
            std::sscanf(remainingText.c_str(), "%c%i,%i,%i", encodedOpcodeByte, &parsedArgument1, &parsedArgument2, &parsedArgument3);

            ACT rec = SPRITE::buildCommandRecord(
                (encodedOpcodeWord & 0xFFu) - 0x3Cu,
                parsedArgument1, parsedArgument2, parsedArgument3);
            append(rec);

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandRecordDelimiter);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();
        }
        remainingText.ReleaseOwnedStorage();
    }

    __forceinline void core::List<ACT>::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        std::uint32_t esi = m_commandRecords.count;
        if (esi != 0 && m_commandRecords.records)
        {
            while (esi != 0)
            {
                const std::uint32_t idx = esi - 1u;
                const CommandRecordStorage& raw = m_commandRecords.records[idx];
                --esi;
                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && raw.words[1] == 0u && raw.words[2] == 0u && raw.words[3] == 0u)
                {
                    ACT rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
                    insertAt(esi + 1u, rec);
                    return;
                }
            }
        }

        ACT rec = SPRITE::buildCommandRecord(opcode, argument1, argument2, argument3);
        insertAt(0u, rec);
    }

    __forceinline void core::List<ACT>::saveCommandRecordsToStream(BaseStream* stream)
    {
        if (m_commandRecords.count == 1u
            && m_commandRecords.records[0].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
        {
            clear();
            }

        const std::uint32_t count = m_commandRecords.count;
        stream->write(&count, 4u);
        const std::uint32_t bytes = count << 4;
        stream->write(m_commandRecords.records, bytes);
    }

    __forceinline void core::List<ACT>::restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite)
    {
        std::uint32_t count = 0;
        stream->read(&count, 4u);
        m_commandRecords.count = count;
        if (static_cast<std::int32_t>(count) > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(count);

        const std::uint32_t bytes = count << 4;
        stream->read(m_commandRecords.records, bytes);

        const std::uint32_t rawLastIndex = m_commandRecords.count - 1u;
        std::int32_t index = static_cast<std::int32_t>(rawLastIndex);
        while (index >= 0)
        {
            CommandRecordStorage& raw = m_commandRecords.records[index];
            const std::uint32_t opcode = raw.words[0];
            if (opcode == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) ||
                opcode == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) ||
                opcode == 0x4Au || opcode == 0x4Bu ||
                opcode == static_cast<std::uint32_t>(ActionCode::ACT_SET_LINK) ||
                opcode == static_cast<std::uint32_t>(ActionCode::ACT_SET_UPLINK) ||
                opcode == 0x96u || opcode == 0x97u || opcode == 0x98u)
            {


                {
                    MAP* const ownerMap = ownerSprite ? ownerSprite->mapOwner() : nullptr;
                    SPRITE* const resolved = ownerMap
                        ? ownerMap->ResolveOldSpriteHandle(static_cast<int>(raw.words[1]))
                        : nullptr;
                    raw.words[1] = resolved
                        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved))
                        : 0u;
                }
            }
            else if (index != 0 && opcode == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK) && m_commandRecords.records[index - 1].words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
            {
                const std::uint32_t oldCount = m_commandRecords.count;
                if (index < static_cast<std::int32_t>(oldCount))
                {
                    const std::uint32_t newCount = oldCount - 1u;
                    m_commandRecords.count = newCount;
                    m_commandRecords.records[index] = m_commandRecords.records[newCount];
                }
            }
            --index;
        }
    }

    __forceinline bool core::List<ACT>::restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket)
    {
        if (armyBucket)
            *armyBucket = 0;

        std::int16_t signedCount = 0;
        stream->read(&signedCount, 2u);
        const std::int32_t count = static_cast<std::int32_t>(signedCount);
        m_commandRecords.count = static_cast<std::uint32_t>(count);

        if (count > static_cast<std::int32_t>(m_commandRecords.capacity))
            ensureCommandRecordCapacity(static_cast<std::uint32_t>(count));

        const std::uint32_t legacyBytes = static_cast<std::uint32_t>(count) * 12u;
        stream->read(m_commandRecords.records, legacyBytes);

        if (mapVersion < 7)
            clear();

        {
            std::int32_t i = 0;
            const std::int32_t normalizedCount =
                static_cast<std::int32_t>(m_commandRecords.count);
            while (i < normalizedCount)
            {
                CommandRecordStorage& raw =
                    m_commandRecords.records[static_cast<std::uint32_t>(i)];
                raw.words[0] &= 0xFFu;
                raw.words[3] = 0u;
                if (raw.words[0] == 0x28u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_MOVE);
                else if (raw.words[0] == 0x27u)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_ATTACK);
                else if (raw.words[0] == 0x2Fu)
                    raw.words[0] = static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK);
                else
                {
                    const int nvid = ownerSprite->Vid() ? ownerSprite->Vid()->nVid : -1;
                    LOG::ResourceError("SPRITE %i", 14, "actionStack.act restore", static_cast<int>(raw.words[0]), nvid);
                }

                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_ATTACK) || raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO) || raw.words[0] == 0x4Au
                    || raw.words[0] == 0x96u || raw.words[0] == 0x97u || raw.words[0] == 0x98u)
                {
                    MAP* const ownerMap = ownerSprite ? ownerSprite->mapOwner() : nullptr;
                    SPRITE* const resolved = ownerMap
                        ? ownerMap->ResolveOldSpriteHandle(static_cast<int>(raw.words[1]))
                        : nullptr;
                    raw.words[1] = resolved
                        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved))
                        : 0u;
                }
                ++i;
            }
        }

        bool hasArmyBucket = false;
        if (mapVersion >= 7)
        {
            std::uint32_t rawArmy = 0u;
            stream->read(&rawArmy, 1u);
            if (armyBucket)
                *armyBucket = static_cast<int>(rawArmy & 0xFFu);
            hasArmyBucket = true;
        }
        return hasArmyBucket;
    }

    void SPRITE::serializeCommandWordsText(STRING& out) const
    {
        static const char kCommandWordFormat[] = { '\x01', '%', 'i', '\0' };
        static const char kCommandSectionDelimiter[] = { '\x02', '\0' };

        STRING serializedText;
        const std::uint32_t count = NoItems();
        const std::int32_t* const values = commandWordData();
        for (std::uint32_t i = 0; i < count; ++i)
        {
            STRING wordText;
            constructFormattedString(wordText, kCommandWordFormat, values[i]);
            appendStringOwner(serializedText, wordText);
            wordText.ReleaseOwnedStorage();
        }
        if (count != 0u)
            appendCStringToString(serializedText, kCommandSectionDelimiter);

        out.AssignAllocatedCopyWithoutRelease(serializedText.c_str());
        serializedText.ReleaseOwnedStorage();
    }

    std::string SPRITE::serializeCommandWordsText() const
    {
        STRING out;
        serializeCommandWordsText(out);
        return out.str();
    }

    void SPRITE::parseCommandWordsText(STRING remainingText)
    {
        constructRightOfFirstMarker(remainingText, remainingText, kCommandWordPrefixMarker);
        const char* parserPointer = remainingText.c_str();
        int parsedWord;
        while (std::strcmp(parserPointer, kEmptyString) != 0)
        {
            std::sscanf(parserPointer, "%i", &parsedWord);
            InsertItem(static_cast<std::int32_t>(parsedWord));

            STRING remainingTail;
            constructRightOfFirstMarker(remainingText, remainingTail, kCommandWordPrefixMarker);
            assignStringFromString(remainingText, remainingTail);
            remainingTail.ReleaseOwnedStorage();
            parserPointer = remainingText.c_str();
        }
        remainingText.ReleaseOwnedStorage();
    }



    PTR_SPRITE& PTR_SPRITE::operator=(SPRITE* value)
    {
        if (value)
            value->AddListReference();
        if (sprite)
            sprite->Release();
        sprite = value;
        return *this;
    }


    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir)
        : SPRITE(owner, vid, xyz, dir, nullptr)
    {
    }

    SPRITE::SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : m_vid(vid), m_xyz(xyz), m_direction(dir)
    {
        (void)owner;
        initializeBaseSprite(vid, xyz.x, xyz.y, xyz.z, dir.Int() & 0xFF, parent);
    }


    SPRITE* SPRITE::initializeBaseSprite(VID* vid, float x, float y, float z, int direction, SPRITE* parent) noexcept
    {
        m_vid = vid;
        m_childChain = nullptr;
        m_childBacklink = nullptr;
        m_goalSprite = nullptr;
        m_bestTargetSprite = nullptr;
        m_actionTimer = 0;
        m_exData = nullptr;
        m_commandStack.releaseCommandRecordsTail();

        int dirByte = direction & 0xFF;
        VECTOR next{x, y, z};

        if ((vid->properties() & P_NOISE) != 0)
        {
            next.x += static_cast<float>(8 - (std::rand() % 17));
            next.y += static_cast<float>(8 - (std::rand() % 17));
        }
        if ((vid->properties() & P_ZEROZ) != 0)
        {
            const float groundZ = mapOwner()->GetGroundZ(vid, VECTOR2{next.x, next.y});
            next.z = parent ? (z - parent->Z() + groundZ) : groundZ;
        }

        m_xyz = next;

        m_direction = ANGLE(static_cast<unsigned char>(0));

        int bucket = 0;
        if (parent)
            bucket = parent->armyIndex();
        else
        {
            const VID* bucketOwner = vid;
            if (vid->weaponCount() == 0)
            {
                if (VID* linkVid = vid->linkedVid())
                    bucketOwner = linkVid;
            }
            bucket = bucketOwner ? (bucketOwner->weaponDefaultArmy() & 3) : 0;
        }

        DWORD nextFlags = static_cast<DWORD>(bucket & 3) << ArmyBitsShift;
        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            const std::uint32_t appBucket = core::ActivePlayerIndex();
            if (static_cast<std::uint32_t>(bucket & 3) != appBucket)
                nextFlags |= DrawSuppressedFlag;
        }
        m_runtimeFlags = nextFlags;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int randomDelay = 0;
        if ((vid->properties() & P_ONEPHASE) == 0)
        {
            const int speedDefault = static_cast<int>(vid->defaultFrameSpeed());
            randomDelay = std::rand() % (speedDefault + 1);
        }
        m_applicationBucketTime = now - static_cast<std::uint32_t>(randomDelay);
        m_createTime = now;
        m_listReferenceCount = 0;
        m_speed = 0.0f;
        m_zSpeed = 0.0f;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = 0;

        if (vid->actionAuxStateRequired() != 0)
        {
            void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
            m_exData = storage ? new (storage) EX_SPRITE_DATA(this) : nullptr;
        }


        if (m_vid->noAnimCadr[0] == 0 &&
            m_vid->noAnimCadr[2] == 0 &&
            m_vid->noAnimCadr[15] != 0)
        {
            m_currentAnimation = 15;
        }
        else if (m_vid->noAnimCadr[14] != 0 &&
                 (core::ApplicationFlags() & application_flags::MapLoading) == 0u)
        {
            m_currentAnimation = 14;
        }
        else
        {
            m_currentAnimation = 0;
        }

        const int baseFrame = static_cast<int>(m_vid->animationBaseFrame[m_currentAnimation]);
        const int frameCount = static_cast<int>(m_vid->animationFrameCount[m_currentAnimation]);
        m_currentFrameBegin = baseFrame;
        m_currentFrame = baseFrame;
        m_currentFrameEnd = spriteAdd32Wrap(spriteAdd32Wrap(baseFrame, frameCount), -1);

        m_hp = vid->GetMaxHp(armyIndex());

        ChangeDirection(dirByte);

        if (m_currentAnimation == 0 && m_currentFrameEnd > m_currentFrame)
        {
            const int span = m_currentFrameEnd - m_currentFrameBegin;
            if (span > 0 && (!vid || (vid->properties() & P_ONEPHASE) == 0))
                m_currentFrame += std::rand() % (span + 1);
        }

        if (m_vid != EmptyVid)
        {
            ++m_listReferenceCount;
            Insert();
        }

        ensureLinkedVidChild();
        GlobalSpriteCollector()->Insert(this);

        m_vid->setLastSpriteCountChangeTimestamp(core::RealCurrentTime);
        m_vid->incrementSpriteCountForArmy(armyIndex());

        if (m_currentAnimation != 14 && (core::ApplicationFlags() & application_flags::MapLoading) == 0)
        {
            const int child238Gate = m_vid->birthChildVid() ? 1 : 0;
            if (child238Gate)
            {
                const int savedAnimation = m_currentAnimation;
                m_currentAnimation = 14;
                spawnAnimationChild();
                m_currentAnimation = savedAnimation;
            }

            const int constructorSfx = m_vid->constructorSfxId();
            if (constructorSfx != 0)
            {
                PlaySFX(constructorSfx);
            }
        }

        if ((m_vid->properties() & 0x28u) != 0u && m_vid->gridDotCount() > 0)
            m_vid->SetGridZ(this);

        return this;
    }


    LINKER::LINKER(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent)
    {


        constexpr float kLinkerYScale = 1.414306640625f;
        const int directionByte = dir.Int() & 0xFF;
        if (parent)
        {
            parent->appendChildChain(this);
            setLinkerState(
                xyz.x - parent->X(),
                (xyz.y - parent->Y()) * kLinkerYScale,
                xyz.z - parent->Z(),
                directionByte,
                parent);
            return;
        }

        setLinkerState(0.0f, 0.0f, 0.0f, directionByte, nullptr);
    }

    LINKER::~LINKER()
    {


        VID* const ownerVid = Vid();
        VID* const linkedVid = ownerVid ? ownerVid->linkedVid() : nullptr;
        if (linkedVid)
        {
            for (SPRITE* child = childChain(); child; child = child->childChain())
            {
                if (child->Animation() >= 15)
                    continue;
                if (child->Vid() == linkedVid)
                {
                    child->ChangeAnimation(15);
                    break;
                }
            }
        }

        detachFromChildChain();
    }


    FRAME::FRAME(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner,
                 vid,
                 VECTOR{xyz.x + core::GlobalApplicationDrawDispatcherState().cameraShiftX(),
                        xyz.y + core::GlobalApplicationDrawDispatcherState().cameraShiftY(),
                        xyz.z},
                 dir,
                 parent)
    {

        applicationFrameSpriteList().append(this);


        const float z0 = vid->maximumZSpeed();
        const float z1 = vid->randomZSpeedValue();
        if (z0 != z1)
        {
            constexpr float kRandScale = 3.0518509447574615e-05f;
            const float delta = z1 - z0;
            setZSpeedDirect(z0 + static_cast<float>(std::rand()) * delta * kRandScale);
        }

        if (vid->maxSpeedValue() != 0.0f)
            StartMove();
    }

    FRAME::~FRAME()
    {


        applicationMenu().clearSelectedSpriteIfMatches(this);
        applicationFrameSpriteList().removeSorted(this);
        win::applicationWinInstance()->transferFrom(this);
    }


    int SPRITE::insertChildChainHead(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* oldHead = childChain();
        if (oldHead)
        {
            oldHead->setChildBacklink(nullptr);
            child->appendChildChain(oldHead);
        }

        setChildChain(child);
        child->setChildBacklink(this);
        return 0;
    }


    int SPRITE::appendChildChain(SPRITE* child)
    {
        if (!child)
            return 1;
        if (child->childBacklink())
            return 1;

        SPRITE* node = this;
        while (SPRITE* next = node->childChain())
            node = next;

        node->setChildChain(child);
        child->setChildBacklink(node);
        return 0;
    }


    void SPRITE::detachFromChildChain()
    {
        SPRITE* next = childChain();
        if (next)
            next->setChildBacklink(childBacklink());

        SPRITE* previous = childBacklink();
        if (previous)
        {
            previous->setChildChain(childChain());
            setChildBacklink(nullptr);
        }

        setChildChain(nullptr);
    }


    int SPRITE::deleteChildByVid(VID* childVid)
    {
        SPRITE* previous = this;
        SPRITE* child = childChain();

        while (child)
        {
            if (child->Vid() == childVid)
                break;
            previous = child;
            child = child->childChain();
        }

        if (!child)
            return 0;

        SPRITE* const next = child->childChain();
        previous->setChildChain(next);
        if (next)
            next->setChildBacklink(previous);

        child->setChildChain(nullptr);
        child->setChildBacklink(nullptr);

        DeleteSpriteThroughVirtualDeletingDestructor(child);
        return 1;
    }

    void SPRITE::syncExDataMaxSpeedFromVid() noexcept
    {
        if (!m_exData || !m_vid)
            return;
        const float maxSpeed = m_vid->maxSpeedValue();
        std::memcpy(&m_exData->maxSpeedBits, &maxSpeed, sizeof(maxSpeed));
    }


    float SPRITE::MaxSpeed() const noexcept
    {
        float value = m_exData
            ? spriteFloatFromBits(m_exData->maxSpeedBits)
            : m_vid->maxSpeedValue();
        if ((m_runtimeFlags & 0x00000800u) != 0u)
            value = -value;
        return value;
    }


    __declspec(noinline)
    EX_SPRITE_DATA::EX_SPRITE_DATA(SPRITE* source) noexcept
    {
        gammaRaw0 = 0u;
        gammaRaw1 = 0u;
        items.values = nullptr;
        items.vtableTag = currentCommandWordListVtable();
        items.count = 0u;
        items.capacity = 0u;
        childCadence = 1u;
        gridFrame = -1;

        lifetimeRemaining = static_cast<std::uint32_t>(source->m_vid->lifetimeValue());
        effectCurvePosition = 0.0f;
        effectTimestamp = core::CurrentTimeMilliseconds();
        sourceX = source->m_xyz.x;
        sourceY = source->m_xyz.y;
        sourceZ = source->m_xyz.z;

        const float speed0 = source->m_vid->maxSpeedValue();
        const float speed1 = source->m_vid->randomSpeedValue();
        float runtimeMaxSpeed = speed0;
        if (speed0 != speed1)
        {
            constexpr float kRandScale = 3.0518509447574615e-05f;
            runtimeMaxSpeed = speed0 +
                static_cast<float>(std::rand()) * (speed1 - speed0) * kRandScale;
        }
        std::memcpy(&maxSpeedBits, &runtimeMaxSpeed, sizeof(runtimeMaxSpeed));
    }

    STRING SPRITE::GetName() const
    {
        return m_exData ? m_exData->name : STRING("");
    }


    void SPRITE::SetName(const STRING* name)
    {


        STRING requested(*name);
        const bool hadName = m_exData && !m_exData->name.isEmpty();
        const bool wantsName = !requested.isEmpty();

        if (hadName && !wantsName)
        {
            reinterpret_cast<core::Application*>(core::ApplicationOwner())->removeNamedSprite(this);
        }

        if (!m_exData && wantsName)
        {
            void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
            EX_SPRITE_DATA* created = nullptr;
            if (storage)
            {
                created = new (storage) EX_SPRITE_DATA(this);
            }
            m_exData = created;
        }

        const bool insertAfter = !hadName && wantsName;
        if (m_exData)
            m_exData->name = requested;


        if (insertAfter)
        {
            reinterpret_cast<core::Application*>(core::ApplicationOwner())->appendNamedSprite(this);
        }
    }


    void SPRITE::SetGamma(const Gamma& raw) noexcept
    {


        if (!m_exData)
        {
            void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
            EX_SPRITE_DATA* created = nullptr;
            if (storage)
            {
                created = new (storage) EX_SPRITE_DATA(this);
            }
            m_exData = created;
        }


        m_exData->gammaRaw0 = raw.first;
        m_exData->gammaRaw1 = raw.second;
    }


    void SPRITE::setGoalSprite(SPRITE* goal) noexcept
    {
        SPRITE* const oldOwner = m_goalSprite;
        if (oldOwner == goal)
            return;

        if (oldOwner)
            (void)oldOwner->Release();

        m_goalSprite = goal;
        if (goal)
            goal->setListReferenceCount(goal->listReferenceCount() + 1);
    }


    void SPRITE::logSpriteResourceError(int severity, const char* detail, int value) const noexcept
    {
        const int nvid = m_vid ? m_vid->nVid : -1;
        (void)logFileLoggerResourceError(
            g_fileLogger,
            "SPRITE %i",
            severity,
            detail,
            value,
            nvid);
    }


    int SPRITE::attackTerrainGate(float targetX, float targetY, float targetZ) const noexcept
    {
        VID* projectileVid = m_vid->fightChildVid();
        while (projectileVid && projectileVid->spriteClassId() == 12u)
            projectileVid = projectileVid->fightChildVid();

        if (!projectileVid)
            return 1;
        if (projectileVid->movementMask() == 0u)
            return 0;

        return Map->isLineUnderGroundWithBullet(
            X(), Y(), Z() + m_vid->childZ[8],
            targetX, targetY, targetZ);
    }


    int SPRITE::IsXYCross(const VID* vid, float x, float y) const noexcept
    {
        if (std::fabs(X() - x) > Vid()->halfSizeX() + vid->halfSizeX())
            return 0;
        if (std::fabs(Y() - y) > Vid()->halfSizeY() + vid->halfSizeY())
            return 0;
        return 1;
    }


    void TERRAIN::AddHpPerSecond(int delta) noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & 0xFFFFFC00u) <= core::PreviousWorldTimeMilliseconds())
            return;

        const int currentHp = Hp();
        if (currentHp <= 0)
            return;

        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int maxHp = vid->GetMaxHp(bucket);
        const std::uint32_t nextBits =
            static_cast<std::uint32_t>(currentHp) + static_cast<std::uint32_t>(delta);
        int nextHp = static_cast<std::int32_t>(nextBits);
        if (nextHp > maxHp)
            nextHp = maxHp;
        ChangeHp(nextHp);
    }

    __declspec(safebuffers)

    int SPRITE::SetCommand(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0;

        setGoalSprite(goal);

        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->Vid();
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                child->SetCommand(argument1, goal);
            }
        }

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        const DWORD commandBits = (static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift;
        m_runtimeFlags = preserved | commandBits;
        return 0;
    }


    int SPRITE::SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept
    {
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u && argument1 != 0x12)
            m_actionTimer = 0u;

        setGoalSprite(goal);

        const DWORD preserved = m_runtimeFlags & ~CommandBitsMask;
        if (argument1 < 0x10 && m_goalSprite == nullptr)
        {
            m_runtimeFlags = preserved;
            return 1;
        }

        m_runtimeFlags = preserved | ((static_cast<DWORD>(argument1) & CommandValueMask) << CommandBitsShift);
        return 0;
    }


    void SPRITE::Move(SPRITE* goal) noexcept
    {
        if (SetCommandWithoutLink(1, goal) == 0 && StartMove() == 0)
        {
            SetCommand(0, nullptr);
            return;
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return;

        VID* const childVid = child->Vid();
        if (childVid != m_vid->linkedVid() ||
            childVid->hasWeaponChildDescriptor() == 0u ||
            childVid->weaponCount() == 0u)
            return;

        const int commandBits = static_cast<int>(child->runtimeFlags() & SPRITE::CommandBitsMask);
        if (commandBits != 0 && commandBits != 0x10)
            child->SetCommand(0, nullptr);
    }

    int SPRITE::ammoCount() const noexcept
    {
        int value = ammoFixedPoint();
        const int signBits = value < 0 ? -1 : 0;
        value += (signBits & 0x3F);
        return value >> 6;
    }


    int SPRITE::MaxAmmo() const noexcept
    {
        return m_vid->GetMaxAmmo();
    }


    int SPRITE::refillAmmoByCapacityFraction(int divisor) noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int units = metricVid->weaponRecordAmmoCapacity();

        const int maxFixed = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(units) << 6u);
        if (maxFixed == 0 || maxFixed <= ammoFixedPoint())
            return 0;

        int next = maxFixed;
        if (divisor != 0)
        {
            next = spriteAdd32Wrap(ammoFixedPoint(), maxFixed / divisor);
            if (next > maxFixed)
                next = maxFixed;
        }
        setAmmoFixedPoint(next);
        return 1;
    }


    int SPRITE::ammoMissingPercent() const noexcept
    {
        VID* const vid = Vid();
        VID* metricVid = vid;
        if (VID* const link = vid->linkedVid())
        {
            if (link->hasWeaponChildDescriptor() != 0u && link->weaponCount() != 0u)
                metricVid = link;
        }
        const int capacity = metricVid->weaponRecordAmmoCapacity();
        if (capacity == 0)
            return 0;
        const int scaled = spriteImul32Low(ammoCount(), 100);
        return spriteSub32Wrap(100, scaled / capacity);
    }


    int SPRITE::NeedRepairByRepair() const noexcept
    {
        VID* const vid = Vid();
        const int bucket = armyIndex();
        const int hp = Hp();
        const int ownMaxHp = vid->GetMaxHp(bucket);
        VID* const link = vid->linkedVid();
        SPRITE* const child = childChain();
        const bool linkedChild = link && child && child->Vid() == link;

        if (hp < ownMaxHp)
        {
            int total = ownMaxHp;
            if (link && !linkedChild)
                total = spriteAdd32Wrap(total, link->GetMaxHp(bucket));
            const int scaledHp = spriteImul32Low(hp, 100);
            return spriteSub32Wrap(100, scaledHp / total);
        }

        if (link && !linkedChild)
            return vid->nvid() != 35 ? 50 : 0;

        if (linkedChild && child->Vid()->spriteClassId() != 9u)
        {
            const int childHp = child->Hp();
            const int childBucket = child->armyIndex();
            const int childMaxHp = child->Vid()->GetMaxHp(childBucket);
            if (childHp < childMaxHp)
            {
                const int total = spriteAdd32Wrap(ownMaxHp, childMaxHp);
                const int combinedHp = spriteAdd32Wrap(childHp, hp);
                const int scaledHp = spriteImul32Low(combinedHp, 100);
                return spriteSub32Wrap(100, scaledHp / total);
            }
        }
        return 0;
    }


    __declspec(noinline)
    int SPRITE::InsertItem(std::int32_t word) noexcept
    {
        if (!m_exData)
        {
            void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
            EX_SPRITE_DATA* created = nullptr;
            if (storage)
            {
                created = new (storage) EX_SPRITE_DATA(this);
            }
            m_exData = created;
        }


        EX_SPRITE_DATA::ItemList& list = m_exData->items;
        if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
        {
            const std::uint32_t oldCapacity = list.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
            {
                std::int32_t* const oldArray = list.values;
                std::int32_t* const newArray = static_cast<std::int32_t*>(
                    ::operator new(static_cast<std::size_t>(grownCapacity) * sizeof(std::int32_t), std::nothrow));
                list.values = newArray;
                if (!newArray)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                                  static_cast<int>(grownCapacity));
                if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
                {
                    for (std::uint32_t i = 0; i < oldCapacity; ++i)
                        newArray[i] = oldArray[i];
                }
                if (oldArray)
                    ::operator delete(oldArray);
                list.capacity = grownCapacity;
            }
        }
        list.values[list.count++] = word;
        return static_cast<int>(list.count);
    }


    __declspec(noinline)
    int SPRITE::insertUniqueItem(std::int32_t word) noexcept
    {
        if (!m_exData)
        {
            void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
            EX_SPRITE_DATA* created = nullptr;
            if (storage)
            {
                created = new (storage) EX_SPRITE_DATA(this);
            }
            m_exData = created;
        }

        EX_SPRITE_DATA::ItemList& list = m_exData->items;
        for (std::uint32_t i = list.count; i != 0u; --i)
        {
            if (list.values[i - 1u] == word)
                return 1;
        }

        if (static_cast<std::int32_t>(list.count) >= static_cast<std::int32_t>(list.capacity))
        {
            const std::uint32_t oldCapacity = list.capacity;
            const std::uint32_t grownCapacity = oldCapacity * 2u + 4u;
            if (static_cast<std::int32_t>(grownCapacity) > static_cast<std::int32_t>(oldCapacity))
            {
                std::int32_t* const oldArray = list.values;
                std::int32_t* const newArray = static_cast<std::int32_t*>(
                    ::operator new(static_cast<std::size_t>(grownCapacity) * sizeof(std::int32_t), std::nothrow));
                list.values = newArray;
                if (!newArray)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                                  static_cast<int>(grownCapacity));
                if (oldArray && static_cast<std::int32_t>(oldCapacity) > 0)
                {
                    for (std::uint32_t i = 0; i < oldCapacity; ++i)
                        newArray[i] = oldArray[i];
                }
                if (oldArray)
                    ::operator delete(oldArray);
                list.capacity = grownCapacity;
            }
        }
        list.values[list.count++] = word;
        return 0;
    }

    int SPRITE::GetItemNumber(int index) const noexcept
    {
        if (index < 0 || !m_exData)
            return 0;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        if (!m_exData->items.values || rawIndex >= m_exData->items.count)
            return 0;
        return m_exData->items.values[rawIndex];
    }

    int SPRITE::findLastCommandWord(std::int32_t word) const noexcept
    {
        if (!m_exData || m_exData->items.count == 0u || !m_exData->items.values)
            return -1;

        std::uint32_t index = m_exData->items.count;
        const std::int32_t* cursor = m_exData->items.values + index;
        while (index != 0u)
        {
            --cursor;
            --index;
            if (*cursor == word)
                return static_cast<int>(index);
        }
        return -1;
    }

    int SPRITE::removeCommandWordValue(std::int32_t word) noexcept
    {
        if (!m_exData)
            return 0;
        const int index = findLastCommandWord(word);
        if (index < 0)
            return 0;

        EX_SPRITE_DATA::ItemList& list = m_exData->items;
        const std::uint32_t rawIndex = static_cast<std::uint32_t>(index);
        --list.count;
        list.values[rawIndex] = list.values[list.count];
        return 1;
    }

    int SPRITE::hasCommandOpcode(std::uint32_t opcode) const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        for (std::uint32_t i = 0; i < owner.count; ++i)
        {
            if (owner.records[i].words[0] == opcode)
                return 1;
        }
        return 0;
    }


    std::uint32_t SPRITE::lastCommandOpcode() const noexcept
    {
        const auto& owner = m_commandStack.m_commandRecords;
        if (owner.count == 0u)
            return 0u;
        return owner.records[owner.count - 1u].words[0];
    }

    int SPRITE::clearCommandWordList() noexcept
    {
        if (!m_exData)
            return 0;
        EX_SPRITE_DATA::ItemList& list = m_exData->items;
        std::int32_t* const old = list.values;
        list.capacity = 0u;
        list.count = 0u;
        if (old)
            ::operator delete(old);
        list.values = nullptr;
        return 0;
    }


    int SPRITE::derivedStateValue(int index) const noexcept
    {
        return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * SpriteLayout::WordStride);
    }

    int SPRITE::setDerivedStateValue(int index, int value) noexcept
    {
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::DerivedStateBase + static_cast<std::size_t>(index) * SpriteLayout::WordStride) = value;
        return value;
    }

    namespace
    {
        float addIntegerToFloatRounded(float base, int addend) noexcept
        {

            return static_cast<float>(static_cast<long double>(base) +
                                      static_cast<long double>(addend));
        }
    }


    int SPRITE::repairLinkedChildState(int createMissingLinker) noexcept
    {
        VID* const vid = m_vid;

        const int bucket = armyIndex();
        const int maxHp = vid->GetMaxHp(bucket);
        ChangeHp(maxHp);

        VID* const linkerVid = vid->linkedVid();
        SPRITE* const existingChild = childChain();
        const bool hasLinkerChild = existingChild && existingChild->Vid() == linkerVid;

        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (!hasLinkerChild && createMissingLinker != 0)
        {
            bool allowCreate = true;
            if (vid->nvid() == 35)
            {
                const int rawCount = table.count();
                VID* const gateA = (rawCount > 0x28) ? table.slot(0x28) : nullptr;
                VID* const gateB = (rawCount > 0x23) ? table.slot(0x23) : nullptr;
                VID* const leftVid = gateA ? gateA : EmptyVid;
                VID* const rightVid = gateB ? gateB : EmptyVid;
                const int leftCounter = leftVid->NoSprites(bucket);
                const int rightCounter = rightVid->NoSprites(bucket);
                allowCreate = leftCounter < rightCounter;
            }

            if (allowCreate)
            {
                ensureLinkedVidChild();
                const int rawCount = table.count();
                VID* const createVid = (rawCount > 0x24E) ? table.slot(0x24E) : nullptr;
                VID* const resolvedCreateVid = createVid ? createVid : EmptyVid;
                static_cast<void>(mapOwner()->CreateSprite(resolvedCreateVid, m_xyz, ANGLE(static_cast<unsigned char>(0)), this, false));
                return 1;
            }
        }

        if (SPRITE* const child = childChain())
        {
            if (child->Vid() == linkerVid)
                (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
        }
        return 1;
    }


    SPRITE* SPRITE::probeMovementFootprint(float x, float y) noexcept
    {
        const float mapSizeX = applicationWorldFloatAt(core::application_layout::MapExtentX);
        const float mapSizeY = applicationWorldFloatAt(core::application_layout::MapExtentY);
        if (x87LessOrUnordered(x, 0.0f) ||
            !x87LessOrUnordered(x, mapSizeX) ||
            x87LessOrUnordered(y, 0.0f) ||
            !x87LessOrUnordered(y, mapSizeY))
        {
            return this;
        }

        MAP* const map = mapOwner();
        const float groundZ = map->GetGroundZ(
            Vid(), VECTOR2{X(), Y()});
        if (!x87LessOrUnordered(groundZ, Z()))
            return this;

        return CanPlace(x, y, Z());
    }


    void SPRITE::PlaySFX(int nsfx) noexcept
    {
        const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        GRAPH* const graph = Graph;
        const int graphSizeX = static_cast<int>(graph->SizeX());
        const int graphSizeY = static_cast<int>(graph->SizeY());
        const float halfScreenX = static_cast<float>(graphSizeX) * 0.5f;
        const float halfScreenY = static_cast<float>(graphSizeY) * 0.5f;
        const float soundX = m_xyz.x - drawState.cameraShiftX() - halfScreenX;
        const float soundY = m_xyz.y - m_xyz.z - drawState.cameraShiftY() - halfScreenY;
        sound::g_globalSoundEngine->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);

    }

    int SPRITE::IsInside(float x, float y) const noexcept
    {
        VID* const vid = m_vid;
        const float halfX = vid->halfSizeX();
        if (!(x >= m_xyz.x - halfX))
            return 0;
        if (!(m_xyz.x + halfX >= x))
            return 0;

        const float baseY = m_xyz.y - m_xyz.z;
        const float halfY = vid->halfSizeY();
        if (!(y > baseY - vid->sizeZ() - halfY))
            return 0;
        if (!(baseY + halfY > y))
            return 0;
        return 1;
    }

    __declspec(safebuffers)


    SPRITE* SPRITE::CanPlace(float x, float y, float z)
    {
        VID* const vid = m_vid;
        const DWORD movementMask = static_cast<DWORD>(vid->movementMask());
        if (movementMask == 0u)
            return nullptr;

        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
        SPRITE* const terrainSentinel = mouseSprite();

        if ((vid->properties() & P_ZEROZ) != 0u)
        {
            const float moveUp = vid->moveUpZ();
            const float moveDown = vid->moveDownZ();
            float ground = map->GetGroundZ(vid, VECTOR2{x, y});
            if ((ground - z > moveUp) || (z - ground > moveDown))
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                ground = map->GetGroundZ(VECTOR2{left, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{left, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
            }
        }
        else
        {
            if (map->GetGroundZ(vid, VECTOR2{x, y}) > z)
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                if (map->GetGroundZ(VECTOR2{left, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{left, bottom}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, bottom}) > z)
                    return terrainSentinel;
            }
        }

        const float probeHalfX = vid->halfSizeX();
        const float probeHalfY = vid->halfSizeY();
        SPRITE_COLLECTOR* const spatialHash = GlobalSpriteCollector();
        for (SPRITE* candidate = spatialHash->FirstHashInBox(
                 x - probeHalfX, y - probeHalfY, x + probeHalfX, y + probeHalfY);
             candidate;
             candidate = spatialHash->NextHashInBox())
        {
            if (candidate == this || candidate->Animation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->m_vid;
            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + vid->sizeZ() < candidate->m_xyz.z)
                continue;
            if ((static_cast<DWORD>(candidateVid->movementMask()) & movementMask) == 0u)
                continue;


            if (candidateVid->spriteClassId() != 7u)
                return candidate;
            if (((candidate->m_runtimeFlags ^ m_runtimeFlags) & 0x00003000u) != 0u)
                return candidate;
            if ((vid->properties() & P_HASH) != 0u)
                return candidate;
        }
        return nullptr;
    }

    __declspec(safebuffers)

    SPRITE* SPRITE::CanPlaceWithCrush(float x, float y, float z)
    {
        VID* const vid = m_vid;
        const DWORD movementMask = static_cast<DWORD>(vid->movementMask());
        if (movementMask == 0u)
            return nullptr;

        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
        SPRITE* const terrainSentinel = mouseSprite();

        if ((vid->properties() & P_ZEROZ) != 0u)
        {
            const float moveUp = vid->moveUpZ();
            const float moveDown = vid->moveDownZ();
            float ground = map->GetGroundZ(vid, VECTOR2{x, y});
            if ((ground - z > moveUp) || (z - ground > moveDown))
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                ground = map->GetGroundZ(VECTOR2{left, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{left, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, top});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
                ground = map->GetGroundZ(VECTOR2{right, bottom});
                if ((ground - z > moveUp) || (z - ground > moveDown))
                    return terrainSentinel;
            }
        }
        else
        {
            if (map->GetGroundZ(vid, VECTOR2{x, y}) > z)
                return terrainSentinel;

            if (vid->spriteClassId() != 7u)
            {
                const float halfX = vid->halfSizeX() - 2.0f;
                const float halfY = vid->halfSizeY() - 2.0f;
                const float left = x - halfX;
                const float right = x + halfX;
                const float top = y - halfY;
                const float bottom = y + halfY;

                if (map->GetGroundZ(VECTOR2{left, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{left, bottom}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, top}) > z)
                    return terrainSentinel;
                if (map->GetGroundZ(VECTOR2{right, bottom}) > z)
                    return terrainSentinel;
            }
        }

        const float probeHalfX = vid->halfSizeX();
        const float probeHalfY = vid->halfSizeY();
        SPRITE_COLLECTOR* const spatialHash = GlobalSpriteCollector();
        for (SPRITE* candidate = spatialHash->FirstHashInBox(
                 x - probeHalfX, y - probeHalfY, x + probeHalfX, y + probeHalfY);
             candidate;
             candidate = spatialHash->NextHashInBox())
        {
            if (candidate == this || candidate->Animation() >= 0x0F)
                continue;

            VID* const candidateVid = candidate->m_vid;
            if (!(candidateVid->halfSizeX() + probeHalfX > std::fabs(candidate->m_xyz.x - x)))
                continue;
            if (!(candidateVid->halfSizeY() + probeHalfY > std::fabs(candidate->m_xyz.y - y)))
                continue;
            if (candidateVid->sizeZ() + candidate->m_xyz.z < z)
                continue;
            if (z + vid->sizeZ() < candidate->m_xyz.z)
                continue;


            const int collisionFunction = vid->collisionScriptFunction();
            if (collisionFunction >= 0)
            {
                const int selfArgument = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(this)));
                const int candidateArgument = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(candidate)));
                if (core::Application::callScriptFunction(
                        collisionFunction, selfArgument, candidateArgument, 0) != 0)
                {
                    return nullptr;
                }
            }

            if ((static_cast<DWORD>(candidateVid->movementMask()) & movementMask) != 0u)
            {


                if (candidateVid->spriteClassId() != 7u)
                    return candidate;
                if (((candidate->m_runtimeFlags ^ m_runtimeFlags) & 0x00003000u) != 0u)
                    return candidate;
                if ((vid->properties() & P_HASH) != 0u)
                    return candidate;
                continue;
            }


            if ((candidateVid->properties() & P_CRUSH) != 0u)
            {
                const int selfArgument = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(this)));
                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, selfArgument, 0);
            }
        }
        return nullptr;
    }


    SPRITE* SPRITE::CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut)
    {
        if (m_vid->movementMask() == 0)
            return 0;

        SPRITE* const blocker = CanPlaceWithCrush(*xOut, *yOut, *zOut);
        if (!blocker)
        {
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut});
            }
            return 0;
        }


        if (blocker != mouseSprite())
        {
            VID* const blockerVid = blocker->Vid();
            if ((static_cast<std::uint32_t>(blockerVid->weaponFlags()) & 0x00000040u) != 0u)
            {


                const float ownPushValue = m_vid->weaponFloatAt(0x0C);
                const float blockerPushValue = blockerVid->weaponFloatAt(0x0C);
                if (x87OrderedGreater(ownPushValue * 2.0f, blockerPushValue))
                {
                    const float pushedXInitial = addThenSubtractRounded(
                        blocker->X(), *xOut, X());
                    const float pushedYInitial = addThenSubtractRounded(
                        blocker->Y(), *yOut, Y());
                    float pushedX = pushedXInitial;
                    float pushedY = pushedYInitial;
                    float pushedZ = blocker->Z();


                    g_collisionPushRecursionDepth = spriteAdd32Wrap(g_collisionPushRecursionDepth, 1);
                    if (g_collisionPushRecursionDepth < 5 &&
                        blocker->CanPlaceWithCrushAndGlide(&pushedX, &pushedY, &pushedZ) == nullptr)
                    {
                        bool allowPush = true;
                        if ((static_cast<std::uint32_t>(m_vid->weaponFlags()) & 0x00000040u) != 0u)
                        {
                            const std::uint8_t ownDir = static_cast<std::uint8_t>(directionIndex());
                            const std::uint8_t shifted = static_cast<std::uint8_t>(ownDir - 0x80u);
                            const std::uint8_t blockerDir = static_cast<std::uint8_t>(blocker->directionIndex());
                            const std::uint8_t d1 = static_cast<std::uint8_t>(shifted - blockerDir);
                            const std::uint8_t d2 = static_cast<std::uint8_t>(blockerDir - shifted);
                            const std::uint8_t delta = d1 < d2 ? d1 : d2;
                            allowPush = delta >= 0x10u || ownDir < 0x80u;
                        }
                        if (allowPush)
                            blocker->ChangeCoor(pushedX, pushedY, pushedZ);
                    }
                    if (g_collisionPushRecursionDepth != 0)
                        g_collisionPushRecursionDepth = spriteSub32Wrap(g_collisionPushRecursionDepth, 1);
                }
            }
        }

        const float savedX = X();
        const float savedY = Y();
        const float savedZ = Z();

        if (CanPlace(savedX, *yOut, *zOut) == nullptr)
        {
            *xOut = savedX;
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut});
            }
            return 0;
        }

        if (CanPlace(*xOut, savedY, *zOut) == nullptr)
        {
            *yOut = savedY;
            if ((m_vid->properties() & P_ZEROZ) != 0u)
            {
                MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
                *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut});
            }
            return 0;
        }

        if (blocker != mouseSprite())
        {
            VID* const blockerVid = blocker->Vid();
            const bool overlapsOld =
                blocker->Animation() < 15 &&
                blockerVid->halfSizeX() + m_vid->halfSizeX() > std::fabs(blocker->X() - savedX) &&
                blockerVid->halfSizeY() + m_vid->halfSizeY() > std::fabs(blocker->Y() - savedY) &&
                blockerVid->sizeZ() + blocker->Z() >= savedZ &&
                m_vid->sizeZ() + savedZ >= blocker->Z();
            if (overlapsOld)
            {
                if ((m_vid->properties() & P_ZEROZ) != 0u)
                {
                    MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
                    *zOut = map->GetGroundZ(m_vid, VECTOR2{*xOut, *yOut});
                }
                return nullptr;
            }
        }

        *xOut = savedX;
        *yOut = savedY;
        *zOut = savedZ;
        return blocker;
    }


    ANGLE SPRITE::GlideDirection(ANGLE value) noexcept
    {
        return GlideDirectionScaled(value, 1.0f);
    }

    ANGLE SPRITE::GlideDirectionScaled(ANGLE value, float footprintScale) noexcept
    {
        const int direction = value.Int();
        const float halfX = m_vid->sizeX() * 0.5f * footprintScale;
        const float halfY = m_vid->sizeY() * 0.5f * footprintScale;
        const float x = m_xyz.x;
        const float y = m_xyz.y;
        const float z = m_xyz.z;

        if (direction > 0x60 && direction < 0xA0)
        {
            if (CanPlace(x - halfX, y + halfY, z) != nullptr)
            {
                if (CanPlace(x + halfX, y, z) == nullptr)
                    return ANGLE(0x58u);
            }

            if (CanPlace(x + halfX, y + halfY, z) != nullptr)
            {
                if (CanPlace(x - halfX, y, z) == nullptr)
                    return ANGLE(0xA8u);
            }
            return value;
        }

        if (direction >= 0x20 && direction <= 0xE0)
        {
            if (direction > 0xB0 && direction < 0xD0)
            {
                if (CanPlace(x - halfX, y + halfY, z) != nullptr)
                {
                    if (CanPlace(x, y - halfY, z) == nullptr)
                        return ANGLE(0xD8u);
                }

                if (CanPlace(x - halfX, y - halfY, z) != nullptr)
                {
                    if (CanPlace(x, y + halfY, z) == nullptr)
                        return ANGLE(0xA8u);
                }
            }
            else if (direction > 0x30 && direction < 0x50)
            {
                if (CanPlace(x + halfX, y + halfY, z) != nullptr)
                {
                    if (CanPlace(x, y - halfY, z) == nullptr)
                        return ANGLE(0x28u);
                }

                if (CanPlace(x + halfX, y - halfY, z) != nullptr)
                {
                    if (CanPlace(x, y + halfY, z) == nullptr)
                        return ANGLE(0x58u);
                }
            }
            return value;
        }

        if (CanPlace(x - halfX, y - halfY, z) != nullptr)
        {
            if (CanPlace(x + halfX, y, z) == nullptr)
                return ANGLE(0x28u);
        }

        if (CanPlace(x + halfX, y - halfY, z) == nullptr)
            return value;

        if (CanPlace(x - halfX, y, z) == nullptr)
            return ANGLE(0xD8u);

        return value;
    }


    void SPRITE::setDirectionFrameOnly(ANGLE direction) noexcept
    {
        const unsigned char requested = static_cast<unsigned char>(direction.Int());
        if (static_cast<unsigned char>(m_direction.Int()) == requested)
            return;

        if (m_vid->noDir != 1)
        {
            const int oldFrameDelta = m_currentFrame - m_currentFrameBegin;
            const int animationSlot = m_currentAnimation;
            int beginFrame = m_vid->animationBaseFrame[animationSlot];
            beginFrame += m_vid->RealDirection(direction) * m_vid->animationFrameCount[animationSlot];
            m_currentFrameBegin = beginFrame;
            m_currentFrameEnd = beginFrame + m_vid->animationFrameCount[animationSlot] - 1;
            m_currentFrame = beginFrame + oldFrameDelta;
            if (m_currentFrame > m_currentFrameEnd)
                m_currentFrame = m_currentFrameEnd;
        }
        m_direction = direction;
    }


    void SPRITE::updateLinkerCoordinateForDirection(ANGLE direction) noexcept
    {
        ANGLE stepped = direction;
        SPRITE* const owner = linkerOwner();
        if (owner)
            stepped = owner->Vid()->SteppedDirection(direction);

        const int delta = (stepped.Int() - linkerDirection()) & 0xFF;
        const float rotatedX = directionCos(delta) * linkerX() - directionSin(delta) * linkerY();
        const float rotatedY = directionSinAux(delta) * linkerX() + directionCosAux(delta) * linkerY();
        SPRITE* const base = owner ? owner : childBacklink();
        ChangeCoor(base->X() + rotatedX, base->Y() + rotatedY);
    }


    void SPRITE::ChangeDirection(ANGLE direction) noexcept
    {


        const unsigned char requested = static_cast<unsigned char>(direction.Int());
        const unsigned char oldRootDirection = static_cast<unsigned char>(m_direction.Int());
        if (oldRootDirection == requested)
            return;

        if (m_vid->noDir != 1)
        {
            const int oldFrameDelta = m_currentFrame - m_currentFrameBegin;
            const int animationSlot = m_currentAnimation;
            int beginFrame = m_vid->animationBaseFrame[animationSlot];
            beginFrame += m_vid->RealDirection(direction) * m_vid->animationFrameCount[animationSlot];
            m_currentFrameBegin = beginFrame;
            m_currentFrameEnd = beginFrame + m_vid->animationFrameCount[animationSlot] - 1;
            m_currentFrame = beginFrame + oldFrameDelta;
            if (m_currentFrame > m_currentFrameEnd)
                m_currentFrame = m_currentFrameEnd;
        }

        bool ordinaryLinkRouteAvailable = true;
        SPRITE* parent = this;
        for (SPRITE* child = m_childChain; child != nullptr; child = child->m_childChain)
        {
            VID* const childVid = child->m_vid;
            VID* const parentVid = parent->m_vid;

            if (childVid->spriteClassId() == B_LINKER)
            {


                if (child->linkerOwner() == this)
                {
                    if ((childVid->properties() & P_NOTCHANGELINKERCOOR) == 0u)
                    {
                        const ANGLE parentStepped = parentVid->SteppedDirection(direction);
                        child->updateLinkerCoordinateForDirection(parentStepped);
                    }
                    if (spriteFcompC3(childVid->rotationSpeedValue(), 0.0f))
                        child->setDirectionFrameOnly(direction);
                }
                parent = child;
                continue;
            }

            if (ordinaryLinkRouteAvailable &&
                (childVid->properties() & P_NOTCHANGELINKERCOOR) == 0u &&
                parentVid->linkedVid() == childVid)
            {
                const VECTOR link = parentVid->linkOffset();
                if (!spriteFcompC3(link.x, 0.0f) || !spriteFcompC3(link.y, 0.0f))
                {
                    const ANGLE stepped = parentVid->SteppedDirection(direction);
                    const int steppedDirection = stepped.Int() & 0xFF;
                    const float offsetX = directionCos(steppedDirection) * link.x -
                                          directionSin(steppedDirection) * link.y;
                    const float offsetY = directionSinAux(steppedDirection) * link.x +
                                          directionCosAux(steppedDirection) * link.y;
                    child->ChangeCoor(parent->m_xyz.x + offsetX, parent->m_xyz.y + offsetY);
                }
            }

            if (spriteFcompC3(childVid->rotationSpeedValue(), 0.0f))
            {
                child->setDirectionFrameOnly(direction);
            }
            else
            {
                bool keepOrdinaryRoute = false;
                if ((static_cast<std::uint32_t>(childVid->weaponFlags()) & 0x00000800u) != 0u &&
                    parentVid->declaredAnimationFrameCount(6) == 0)
                {
                    const unsigned char childDirection = static_cast<unsigned char>(child->m_direction.Int());
                    const unsigned char clockwise = static_cast<unsigned char>(requested - childDirection);
                    const unsigned char counterClockwise = static_cast<unsigned char>(childDirection - requested);
                    const unsigned char shortest = clockwise < counterClockwise ? clockwise : counterClockwise;
                    if (shortest > 0x20u)
                    {
                        const ANGLE delta = direction - m_direction;
                        child->setDirectionFrameOnly(child->Direction() + delta);
                        keepOrdinaryRoute = true;
                    }
                }
                ordinaryLinkRouteAvailable = keepOrdinaryRoute;
            }
            parent = child;
        }

        m_direction = direction;


        if (SPRITE* const backlink = m_childBacklink)
        {
            if ((static_cast<std::uint32_t>(m_vid->weaponFlags()) & 0x00000800u) != 0u)
            {
                const unsigned char other = static_cast<unsigned char>(backlink->m_direction.Int());
                const unsigned char clockwise = static_cast<unsigned char>(other - requested);
                const unsigned char counterClockwise = static_cast<unsigned char>(requested - other);
                const unsigned char shortest = clockwise < counterClockwise ? clockwise : counterClockwise;
                if (backlink->m_currentAnimation == 2 && shortest < 0x23u)
                {
                    if (m_currentAnimation == 0)
                        ChangeAnimation(2);
                }
                else if (m_currentAnimation == 2)
                {
                    ChangeAnimation(0);
                }
            }
        }

        if (SPRITE* const child = m_childChain)
        {
            if ((static_cast<std::uint32_t>(child->m_vid->weaponFlags()) & 0x00000800u) != 0u)
            {
                const unsigned char other = static_cast<unsigned char>(child->m_direction.Int());
                const unsigned char clockwise = static_cast<unsigned char>(other - requested);
                const unsigned char counterClockwise = static_cast<unsigned char>(requested - other);
                const unsigned char shortest = clockwise < counterClockwise ? clockwise : counterClockwise;
                if (m_currentAnimation == 2 && shortest < 0x23u)
                {
                    if (child->m_currentAnimation == 0)
                        child->ChangeAnimation(2);
                }
                else if (child->m_currentAnimation == 2)
                {
                    child->ChangeAnimation(0);
                }
            }
        }
    }

    ANGLE SPRITE::RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept
    {


        unsigned char target = static_cast<unsigned char>(value.Int());
        unsigned char current = static_cast<unsigned char>(m_direction.Int());


        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid &&
                (static_cast<std::uint32_t>(childVid->weaponFlags()) & 0x00000800u) != 0u &&
                m_vid->declaredAnimationFrameCount(6) != 0)
            {
                const unsigned char clockwise = static_cast<unsigned char>(target - current);
                const unsigned char counterClockwise = static_cast<unsigned char>(current - target);
                const unsigned char shortest = clockwise < counterClockwise ? clockwise : counterClockwise;
                if (shortest > 0x40u)
                {
                    ChangeDirection(ANGLE(static_cast<unsigned char>(current + 0x80u)));
                    current = static_cast<unsigned char>(m_direction.Int());
                }
            }
        }

        if (target == current)
            return ANGLE(static_cast<unsigned char>(0));

        const float rotationSpeed = m_vid->rotationSpeedValue();
        if (rotationSpeed == 0.0f)
        {
            const unsigned char clockwise = static_cast<unsigned char>(target - current);
            const unsigned char counterClockwise = static_cast<unsigned char>(current - target);
            return ANGLE(clockwise < counterClockwise ? clockwise : counterClockwise);
        }

        if (rotationSpeed == 999999.0f)
        {
            ChangeDirection(value);
            return ANGLE(1u);
        }

        int step = static_cast<int>(static_cast<float>(static_cast<std::int32_t>(deltaMs)) * rotationSpeed + 0.5f);
        if (step == 0)
        {


            const int period = static_cast<int>(1.0f / rotationSpeed);
            if (period > 0)
            {
                const std::uint32_t now = core::CurrentTimeMilliseconds();
                const std::uint32_t phase = now % static_cast<std::uint32_t>(period);
                const std::uint32_t boundary = now - phase;
                const std::uint32_t previous = now - deltaMs;
                if (boundary > previous)
                    step = 1;
            }

            if (step == 0)
            {
                const unsigned char clockwise = static_cast<unsigned char>(target - current);
                const unsigned char counterClockwise = static_cast<unsigned char>(current - target);
                return ANGLE(clockwise < counterClockwise ? clockwise : counterClockwise);
            }
        }

        const int directDelta = std::abs(static_cast<int>(current) - static_cast<int>(target));
        const int wrapDelta = 0x100 - directDelta;
        bool subtractStep = false;
        bool addStep = false;

        if (current > target)
        {
            if (directDelta < wrapDelta)
                subtractStep = true;
            else
                addStep = true;
        }
        else if (current < target)
        {
            if (directDelta > wrapDelta)
                subtractStep = true;
            else
                addStep = true;
        }

        const int shortest = directDelta < wrapDelta ? directDelta : wrapDelta;
        unsigned char nextDirection = target;
        if (step < shortest)
        {
            nextDirection = current;
            if (subtractStep)
                nextDirection = static_cast<unsigned char>(current - step);
            else if (addStep)
                nextDirection = static_cast<unsigned char>(current + step);
        }


        if (SPRITE* const parent = m_childBacklink)
        {
            VID* const parentVid = parent->m_vid;
            if ((static_cast<std::uint32_t>(m_vid->weaponFlags()) & 0x00000800u) != 0u &&
                parentVid && parentVid->declaredAnimationFrameCount(6) == 0)
            {
                const unsigned char parentDirection = static_cast<unsigned char>(parent->m_direction.Int());
                const unsigned char proposedClockwise = static_cast<unsigned char>(nextDirection - parentDirection);
                const unsigned char proposedCounter = static_cast<unsigned char>(parentDirection - nextDirection);
                const unsigned char proposedDistance =
                    proposedClockwise < proposedCounter ? proposedClockwise : proposedCounter;

                const unsigned char targetClockwise = static_cast<unsigned char>(target - parentDirection);
                const unsigned char targetCounter = static_cast<unsigned char>(parentDirection - target);
                const unsigned char targetDistance =
                    targetClockwise < targetCounter ? targetClockwise : targetCounter;

                if (proposedDistance > 0x20u && targetDistance > 0x20u)
                {
                    if (subtractStep)
                        nextDirection = static_cast<unsigned char>(parentDirection - 0x20u);
                    else if (addStep)
                        nextDirection = static_cast<unsigned char>(parentDirection + 0x20u);
                }
            }
        }

        ChangeDirection(ANGLE(nextDirection));

        if (nextDirection == target)
            return ANGLE(static_cast<unsigned char>(0));

        current = static_cast<unsigned char>(m_direction.Int());
        const unsigned char clockwise = static_cast<unsigned char>(target - current);
        const unsigned char counterClockwise = static_cast<unsigned char>(current - target);
        return ANGLE(clockwise < counterClockwise ? clockwise : counterClockwise);
    }


    void SPRITE::ResetActionStack() noexcept
    {


        const std::int32_t count = static_cast<std::int32_t>(
            m_commandStack.m_commandRecords.count);
        core::List<ACT>::CommandRecordStorage* const raw = m_commandStack.m_commandRecords.records;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(i);
            if ((raw[index].words[0] & 0xFFu) != 0x4Au || raw[index].words[1] == 0u)
                continue;

            SPRITE* target = nullptr;
            target = reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(raw[index].words[1]));
            if (!target)
                continue;

            (void)target->Release();
        }
        m_commandStack.clear();
    }

    float SPRITE::NearDistanceTo(const SPRITE* other) const noexcept
    {
        const float dx = std::fabs(other->m_xyz.x - m_xyz.x);
        const float dy = std::fabs(other->m_xyz.y - m_xyz.y);
        if (dx > dy)
            return dx + dy * 0.5f;
        return dy + dx * 0.5f;
    }

    float SPRITE::BattleRange() const noexcept
    {
        VID* const vid = m_vid;
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                const float parentRange = vid->weaponBattleRange();
                if (parentRange == 0.0f)
                    return childVid->weaponBattleRange();
            }
        }
        return vid->weaponBattleRange();
    }

    int SPRITE::resolvedWeaponEnemyPriority() const noexcept
    {
        VID* vid = m_vid;
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                vid = childVid;
            }
        }
        return vid->weaponEnemyPriority();
    }

    __declspec(safebuffers)


    int SPRITE::enemyPriority(float candidateMetric,
                            float selectedMetric,
                            SPRITE* candidate,
                            SPRITE* selected) noexcept
    {
        if (!selected)
            return 1;

        if (SPRITE* const currentTarget = bestTargetSprite())
        {
            if (selected == currentTarget)
                return 0;
            if (candidate == currentTarget)
                return 1;
        }

        const DWORD candidateBucket = candidate->armyBits();
        const DWORD selectedBucket = selected->armyBits();
        if (candidateBucket == (2u << ArmyBitsShift) && selectedBucket != (2u << ArmyBitsShift))
            return 0;
        if (selectedBucket != (2u << ArmyBitsShift) && candidateBucket == (2u << ArmyBitsShift))
            return 1;

        const DWORD selectedType = selected->Vid()->spriteTypeId();
        const DWORD candidateType = candidate->Vid()->spriteTypeId();
        if ((selectedType & 0x08u) != 0u && (candidateType & 0x08u) == 0u)
            return 0;
        if ((selectedType & 0x08u) == 0u && (candidateType & 0x08u) != 0u)
            return 1;

        const float nearRange = BattleRange();
        if (selectedMetric > nearRange && candidateMetric <= nearRange)
            return 1;

        int candidateAction92 = 0;
        if ((candidateType & 0x04u) != 0u)
            candidateAction92 = candidate->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);

        if ((selectedType & 0x04u) != 0u &&
            selected->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0) != 0)
        {
            if (candidateAction92 == 0)
                return 0;
        }
        else if (candidateAction92 != 0)
        {
            return 1;
        }

        const int candidatePriority = candidate->resolvedWeaponEnemyPriority();
        const int selectedPriority = selected->resolvedWeaponEnemyPriority();
        if (candidatePriority > selectedPriority)
            return 1;
        if (candidatePriority >= selectedPriority && selectedMetric > candidateMetric)
            return 1;
        return 0;
    }

    __declspec(safebuffers)

    SPRITE* SPRITE::SeekEnemy()
    {
        SPRITE* scanOwner = this;
        while (SPRITE* const child = scanOwner->childChain())
        {
            VID* const ownerVid = scanOwner->Vid();
            VID* const childVid = child->Vid();
            if (childVid != ownerVid->linkedVid() ||
                childVid->hasWeaponChildDescriptor() == 0u ||
                childVid->weaponCount() == 0u)
            {
                break;
            }
            scanOwner = child;
        }

        VID* const ownerVid = scanOwner->Vid();
        const DWORD ownerTypeMask = static_cast<DWORD>(ownerVid->weaponTypeMask());
        const DWORD ownerWeaponFlags = static_cast<DWORD>(ownerVid->weaponFlags());
        const float maxRange = ownerVid->weaponDetectRange();
        const float nearRange = ownerVid->weaponBattleRange();
        const float minRange = ownerVid->weaponMinimumRange();
        if (maxRange == 0.0f || ownerTypeMask == 0u)
            return nullptr;

        float selectedMetric = maxRange + 1.0f;
        SPRITE* selected = nullptr;
        const DWORD ownerBucket = scanOwner->armyBits();

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();


        SPRITE* candidate = overflow.BeginIterate(cursor);

        while (candidate)
        {
            VID* const candidateVid = candidate->Vid();
            const DWORD candidateType = candidateVid->spriteTypeId();
            const DWORD candidateBucket = candidate->armyBits();

            if ((candidateType & ownerTypeMask) != 0u &&
                candidateVid->maxHp != 0 &&
                (((scanOwner->runtimeFlags() ^ candidate->runtimeFlags()) & ArmyBitsMask) != 0u ||
                 (ownerWeaponFlags & 0x80u) != 0u) &&
                (candidateVid->properties() & P_INVISIBLEFORENEMY) == 0u &&
                !(candidateBucket == (2u << ArmyBitsShift) &&
                  (ownerWeaponFlags & 0x80u) == 0u &&
                  (((core::ApplicationFlags() & application_flags::EnemyCanAttackNeutralTrains) == 0u) ||
                   ownerBucket != (1u << ArmyBitsShift) ||
                   candidateVid->spriteClassId() != 21u)) &&
                !((candidateType & 0x08u) != 0u && candidate->childBacklink() != nullptr))
            {
                if ((ownerWeaponFlags & 0x02u) == 0u)
                {

                }
                else
                {
                    const unsigned char wanted = DirectionFromFloatXY(
                        candidate->X() - scanOwner->X(),
                        candidate->Y() - scanOwner->Y()).value;
                    const unsigned char current = static_cast<unsigned char>(scanOwner->directionIndex());
                    const unsigned char d1 = static_cast<unsigned char>(wanted - current);
                    const unsigned char d2 = static_cast<unsigned char>(current - wanted);
                    if ((d1 < d2 ? d1 : d2) >= 0x20u)
                        goto next_candidate;
                }

                {
                    const float metric = scanOwner->NearDistanceTo(candidate);
                    if (metric <= maxRange && metric >= minRange)
                    {
                        if ((ownerVid->spriteTypeId() & 0x08u) != 0u &&
                            scanOwner->bestTargetSprite() == nullptr)
                        {
                            if (metric < selectedMetric)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((flags & ArmyBitsMask) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                        else if (candidateVid->nvid() != 104)
                        {
                            bool acceptCandidate = false;
                            if (selectedMetric <= nearRange)
                            {
                                if (metric <= nearRange)
                                    acceptCandidate = scanOwner->enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }
                            else if (metric <= nearRange)
                            {
                                acceptCandidate = true;
                            }
                            else
                            {
                                acceptCandidate = scanOwner->enemyPriority(metric, selectedMetric, candidate, selected) != 0;
                            }

                            if (acceptCandidate)
                            {
                                const DWORD flags = scanOwner->runtimeFlags();
                                if ((flags & ArmyBitsMask) != 0u ||
                                    candidateVid->spriteClassId() != 21u ||
                                    !static_cast<ENGINE*>(candidate)->engineChainContainsArmy(0))
                                {
                                    selectedMetric = metric;
                                    selected = candidate;
                                    if ((ownerWeaponFlags & 0x08u) != 0u && (std::rand() % 3) == 0)
                                        return candidate;
                                }
                            }
                        }
                    }
                }
            }

        next_candidate:
            candidate = overflow.NextIterate(cursor);
        }

        return selected;
    }


    Gamma SPRITE::GetGamma() const noexcept
    {
        Gamma out{};
        if (m_exData &&
            (m_exData->gammaRaw0 != 0u || m_exData->gammaRaw1 != 0u))
        {
            out.first = m_exData->gammaRaw0;
            out.second = m_exData->gammaRaw1;
        }
        else
        {
            out = m_vid->armyGammaOverride(static_cast<unsigned>(armyIndex()));
        }

        if ((m_vid->runtimeAuxFlags() & 0x01u) == 0u)
            return out;

        const float position = m_exData->effectCurvePosition;
        const int segment = static_cast<int>(position);

        const auto curveValue = [this, position, segment](int baseOffset) noexcept -> int
        {
            if (segment >= 7)
                return m_vid->weaponIntAt(baseOffset + 7 * 4);

            const int first = m_vid->weaponIntAt(baseOffset + segment * 4);
            const int second = m_vid->weaponIntAt(baseOffset + (segment + 1) * 4);
            const float interpolated =
                static_cast<float>(second - first) * (position - static_cast<float>(segment)) +
                static_cast<float>(first);
            return static_cast<int>(interpolated);
        };

        const int blue = curveValue(static_cast<int>(VID::WeaponFieldOffset::EffectGammaBlue));
        const int green = curveValue(static_cast<int>(VID::WeaponFieldOffset::EffectGammaGreen));
        const int red = curveValue(static_cast<int>(VID::WeaponFieldOffset::EffectGammaRed));
        int alpha = curveValue(static_cast<int>(VID::WeaponFieldOffset::EffectGammaAlpha));
        if (alpha < -255) alpha = -255;
        if (alpha > 255) alpha = 255;

        Gamma effect{};
        if (alpha < 0)
            effect.first = (static_cast<DWORD>(-alpha) & 0xFFu) << 24u;
        else
            effect.second = (static_cast<DWORD>(alpha) & 0xFFu) << 24u;
        effect.setRedDelta(red);
        effect.setGreenDelta(green);
        effect.setBlueDelta(blue);
        out.setSaturatingAdd(effect, out);
        return out;
    }


    void SPRITE::Tact()
    {
        const std::uint32_t previousFrameClock = core::PreviousWorldTimeMilliseconds();
        const std::uint32_t now = core::CurrentTimeMilliseconds();

        if ((m_vid->runtimeAuxFlags() & 0x0Fu) != 0u && m_exData)
        {
            EX_SPRITE_DATA* const aux = m_exData;
            const std::uint32_t elapsed = now - aux->effectTimestamp;
            std::uint32_t duration = static_cast<std::uint32_t>(
                m_vid->weaponIntAt(static_cast<int>(VID::WeaponFieldOffset::EffectRefreshInterval)));

            if (duration == 999999u)
            {


                if (m_currentAnimation >= 15 &&
                    m_currentFrameEnd != m_currentFrameBegin)
                {
                    duration = static_cast<std::uint32_t>(spriteImul32Low(
                        m_vid->frameSpeedForAnimation(m_currentAnimation),
                        spriteSub32Wrap(m_currentFrameEnd, m_currentFrameBegin)));
                }
                else
                {
                    const std::uint32_t lifetime = aux->lifetimeRemaining;
                    if (lifetime != 999999u)
                        duration = elapsed + lifetime;
                    else
                        duration = 0u;
                }
            }

            if (duration != 0u)
            {


                int segment = 0;
                segment = _mm_cvtt_ss2si(_mm_set_ss(aux->effectCurvePosition));
                segment = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(segment) + 1u);

                const float elapsedF = static_cast<float>(elapsed);
                const float durationF = static_cast<float>(duration);
                while (segment < 7)
                {
                    const float threshold = m_vid->weaponFloatAt(static_cast<int>(VID::WeaponFieldOffset::EffectCurveThresholds) + segment * 4);
                    if (threshold * durationF > elapsedF)
                        break;
                    ++segment;
                }

                const int previousSegment = segment - 1;
                const float previousPoint = m_vid->weaponFloatAt(static_cast<int>(VID::WeaponFieldOffset::EffectCurveThresholds) + previousSegment * 4);
                const float nextPoint = m_vid->weaponFloatAt(static_cast<int>(VID::WeaponFieldOffset::EffectCurveThresholds) + segment * 4);
                aux->effectCurvePosition =
                    ((elapsedF - previousPoint * durationF) /
                     ((nextPoint - previousPoint) * durationF)) +
                    static_cast<float>(previousSegment);
            }
        }

        if (!m_childBacklink)
        {


            const bool doubleLightTiming = (m_vid->properties() & P_DBLLIGHT) != 0u;
            const std::uint32_t savedClock14 = m_applicationBucketTime;
            const bool movementDue =
                !doubleLightTiming ||
                (now - savedClock14 >= static_cast<std::uint32_t>(
                    m_vid->frameSpeedForAnimation(m_currentAnimation)));

            if (movementDue)
            {
                if (!doubleLightTiming)
                    m_applicationBucketTime = previousFrameClock;
                MoveTact();
                m_applicationBucketTime = savedClock14;
            }
        }

        VID* const vid = m_vid;
        const int animation = m_currentAnimation;
        VID* const childVid = vid->childVid[animation];

        if (childVid)
        {
            const bool class23ZeroOffset =
                vid->spriteClassId() == 23u &&
                vid->childX[animation] == 0.0f &&
                vid->childY[animation] == 0.0f;

            if (class23ZeroOffset)
            {
                const REGION* const region = static_cast<const REGION*>(this);
                const bool applicationSized = (region->regionFlags() & REGION::FullViewportFlag) != 0u;
                const float width = applicationSized
                    ? applicationWorldFloatAt(core::application_layout::MapExtentX)
                    : region->regionWidth();
                const float height = applicationSized
                    ? applicationWorldFloatAt(core::application_layout::MapExtentY)
                    : region->regionHeight();
                const std::uint32_t tileCount = static_cast<std::uint32_t>(
                    computeRegionTileCount(width, height,
                                               childVid->sizeX(),
                                               childVid->sizeY()));
                const std::uint32_t delta = now - previousFrameClock;
                if (tileCount != 0u && delta != 0u)
                {

                    const std::uint32_t divisor = 1000u / delta / tileCount;
                    const int modulo = static_cast<int>(divisor + 1u);
                    if ((std::rand() % modulo) == 0)
                        spawnAnimationChild();
                }
            }
            else if ((childVid->properties() & P_BIRTHASSMOKE) != 0u)
            {
                EX_SPRITE_DATA* const cadenceOwner = m_exData;
                std::uint32_t cadence = cadenceOwner->childCadence;
                if (now - now % cadence > previousFrameClock)
                {
                    const bool subtractGraphMotion =
                        (childVid->properties() & P_WIND) != 0u;
                    int graphDirection = 0;
                    float graphSpeed = 0.0f;
                    if (subtractGraphMotion)
                    {
                        GRAPH* const graph = Graph;
                        graphDirection = static_cast<int>(graph->windDirection());
                        graphSpeed = graph->windSpeed();
                    }

                    cadence = computeChildAnimationCadence(
                        m_direction.Int(), m_speed, m_zSpeed,
                        childVid->maximumZSpeed(),
                        subtractGraphMotion, graphDirection, graphSpeed,
                        childVid->sizeX(), childVid->sizeY());

                    if (animation == 8 &&
                        (vid->weaponFlags() & 0x10) != 0)
                    {
                        cadence >>= 1;
                    }
                    if (cadence > 30000u)
                        cadence = 30000u;
                    if (cadence == 0u)
                        cadence = 1u;
                    cadenceOwner->childCadence = cadence;
                    spawnAnimationChild();
                }
            }
        }

        if (m_createTime == now)
            return;

        const std::uint32_t frameInterval = static_cast<std::uint32_t>(
            vid->frameSpeedForAnimation(m_currentAnimation));


        if (now - m_applicationBucketTime < frameInterval)
            return;

        DWORD flags = m_runtimeFlags;
        if ((flags & CommandBitsMask) != 0u)
        {
            const int action = static_cast<int>((flags >> CommandBitsShift) & CommandValueMask);
            if (action < 16 && !m_goalSprite)
            {
                LOG::ResourceError("SPRITE %i", 10, "command need goal, but goal==NULL",
                                   action, vid ? vid->nVid : -1);
                SetCommand(0, nullptr);
            }
        }

        if (m_actionTimer != 0u)
        {
            if (now - m_applicationBucketTime < m_actionTimer)
            {
                m_actionTimer =
                    m_applicationBucketTime + m_actionTimer - now;
            }
            else
            {
                m_actionTimer = 0;
                if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x48u)
                    SetCommand(0, nullptr);
            }
        }

        for (;;)
        {
            const int callbackAnimation = m_currentAnimation;
            VID* const callbackVid = m_vid;
            const int functionIndex = callbackVid->scriptFunctionAt(callbackAnimation);


            const DWORD callbackRepeatProperties = P_TRACK | P_RANDSPEED;
            if (functionIndex < 0 ||
                (m_currentFrame != m_currentFrameBegin &&
                 (callbackVid->properties() & callbackRepeatProperties) == 0u))
            {
                break;
            }

            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            if (core::Application::callScriptFunction(functionIndex, spriteArg, 0) != 0)
                return;
            if (callbackAnimation == m_currentAnimation)
                break;
        }

        VID* const postCallbackVid = m_vid;
        const int postCallbackAnimation = m_currentAnimation;
        const int sfx = postCallbackVid->sfxForAnimation(postCallbackAnimation);
        if (sfx != 0)
        {
            const bool firstSfx = (m_runtimeFlags & 0x00000200u) == 0u;
            bool repeatSfx = false;
            if (!firstSfx)
            {


                const std::uint8_t* const soundOwner = reinterpret_cast<const std::uint8_t*>(
                    sound::g_globalSoundEngine);
                const int loadedSfxCount = *reinterpret_cast<const int*>(soundOwner + 0x04u);
                const std::uint8_t* const soundTable =
                    *reinterpret_cast<const std::uint8_t* const*>(soundOwner + 0x08u);
                if (soundTable != nullptr && sfx >= 0 && sfx <= loadedSfxCount)
                {
                    constexpr std::size_t kSfxEntrySize = 0x70u;
                    constexpr std::size_t kSfxPropertyOffset = 0x40u;
                    constexpr std::size_t kSfxLoadedFileCountOffset = 0x6Cu;
                    const std::uint8_t* const entry =
                        soundTable + static_cast<std::size_t>(sfx) * kSfxEntrySize;
                    repeatSfx =
                        *reinterpret_cast<const int*>(entry + kSfxLoadedFileCountOffset) != 0 &&
                        (*reinterpret_cast<const std::uint32_t*>(entry + kSfxPropertyOffset) & 1u) != 0u;
                }
            }
            if (firstSfx || repeatSfx)
            {
                m_runtimeFlags |= 0x00000200u;
                PlaySFX(sfx);
            }
        }

        VID* const postCallbackChildVid =
            postCallbackVid->childVid[postCallbackAnimation];


        if (postCallbackChildVid &&
            (postCallbackChildVid->properties() & P_BIRTHASSMOKE) == 0u)
        {
            bool createChild = false;
            if (postCallbackAnimation == 8 &&
                (postCallbackVid->weaponFlags() & 0x10) != 0)
            {
                const std::int32_t midpointNumerator = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(m_currentFrameEnd) +
                    static_cast<std::uint32_t>(m_currentFrameBegin) + 1u);
                const int midpoint = midpointNumerator >= 0
                    ? midpointNumerator / 2
                    : -static_cast<int>(
                        (0u - static_cast<std::uint32_t>(midpointNumerator)) / 2u);
                if (m_currentFrame == midpoint)
                    createChild = true;
                else
                {
                    const int alternateFrame =
                        (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                            ? m_currentFrameEnd
                            : m_currentFrameBegin;
                    createChild = m_currentFrame == alternateFrame;
                }
            }
            else
            {
                if ((postCallbackVid->properties() & P_TRACK) != 0u)
                {
                    createChild = true;
                }
                else
                {
                    const int triggerFrame =
                        (postCallbackVid->properties() & P_CREATECHILDEND) != 0u
                            ? m_currentFrameEnd
                            : m_currentFrameBegin;
                    createChild = m_currentFrame == triggerFrame;
                }
            }

            if (createChild)
                spawnAnimationChild();
        }

        m_currentFrame = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(m_currentFrame) + 1u);


        if (m_currentAnimation >= 15 &&
            (m_currentFrame > m_currentFrameEnd ||
             m_vid->declaredAnimationFrameCount(m_currentAnimation) == 0))
        {
            m_currentFrame = m_currentFrameEnd;
            dispatchVirtualAction(15u, 0, 0, 0);
            DeleteSpriteThroughVirtualDeletingDestructor(this);
            return;
        }

        const int actionMask = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (actionMask == 0x44 || actionMask == 0x48)
        {
            if (m_currentAnimation < 15 && m_currentAnimation != 10 &&
                m_currentFrame > m_currentFrameEnd)
            {
                if (m_speed == 0.0f)
                {
                    if (m_currentAnimation == 2 || m_currentAnimation >= 7)
                        ChangeAnimation(0);
                }
                else if (m_currentAnimation != 2)
                {
                    ChangeAnimation(2);
                }
            }
        }
        else if (m_currentFrame > m_currentFrameEnd ||
                 (m_vid->properties() & P_TRACK) != 0u)
        {
            const std::size_t commandCount = m_commandStack.size();
            if (commandCount == 0 || actionMask != 0)
            {
                dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            }
            else
            {
                const core::List<ACT>::CommandRecordStorage command =
                    m_commandStack.m_commandRecords.records[commandCount - 1];
                const int opcode = static_cast<int>(command.words[0]);

                const bool resetTransientAnimation =
                    (m_currentAnimation < 15 && m_currentAnimation >= 7 && m_currentAnimation != 10) ||
                    (m_currentAnimation == 2 && m_speed == 0.0f);
                if (resetTransientAnimation && opcode >= 17)
                    ChangeAnimation(0);

                const int argument1 = static_cast<int>(command.words[1]);
                const int argument2 = static_cast<int>(command.words[2]);
                const int argument3 = static_cast<int>(command.words[3]);

                if (opcode == static_cast<int>(ActionCode::ACT_WHILE_NOT_SCRIPT_VAR))
                {


                    if (core::ApplicationScriptRuntime()->getActionVariableInt(argument1) == argument2)
                    {
                        m_commandStack.setCommandRecordCount(
                            static_cast<std::uint32_t>(commandCount - 1));
                    }
                }
                else if (opcode != static_cast<int>(ActionCode::ACT_STOP_STACK))
                {
                    m_commandStack.setCommandRecordCount(
                        static_cast<std::uint32_t>(commandCount - 1));


                    if (opcode == static_cast<int>(ActionCode::ACT_DESTROY_UNIT) &&
                        argument1 == -1)
                    {
                        m_currentFrame = m_currentFrameEnd;
                        DeleteSpriteThroughVirtualDeletingDestructor(this);
                        return;
                    }

                    if (opcode < 17)
                    {
                        ChangeAnimation(opcode);
                    }
                    else
                    {
                        dispatchVirtualAction(static_cast<std::uint32_t>(opcode),
                                              argument1, argument2, argument3);
                    }
                }
            }
        }

        if (m_exData)
        {
            std::uint32_t& terminalTimer = m_exData->lifetimeRemaining;
            if (terminalTimer != 999999u && m_currentAnimation < 15)
            {
                if (now - m_applicationBucketTime >= terminalTimer)
                    ChangeAnimation(15);
                else
                    terminalTimer = terminalTimer + m_applicationBucketTime - now;
            }

            const std::uint32_t refreshInterval = static_cast<std::uint32_t>(
                m_vid->weaponIntAt(static_cast<int>(VID::WeaponFieldOffset::EffectRefreshInterval)));
            if (refreshInterval != 999999u &&
                now - m_exData->effectTimestamp > refreshInterval)
            {
                m_exData->effectTimestamp = now;
            }
        }

        m_applicationBucketTime = now;
        if (m_currentFrame > m_currentFrameEnd)
            m_currentFrame = m_currentFrameBegin;


        if ((m_vid->properties() & 0x28u) != 0u &&
            m_vid->gridDotCount() != 0 &&
            m_exData != nullptr)
        {
            if (m_exData->gridFrame != m_currentFrame ||
                now - m_createTime < 1000u)
            {
                m_vid->ResetGridZ(this);
                m_vid->SetGridZ(this);
            }
        }
    }

    int SPRITE::advancePrimitiveFrame() noexcept
    {
        const std::uint32_t now = core::CurrentTimeMilliseconds();
        int result = static_cast<int>(now);
        const std::uint32_t frameInterval =
            static_cast<std::uint32_t>(m_vid->frameSpeed[m_currentAnimation]);

        if (now - m_applicationBucketTime >= frameInterval)
        {
            m_applicationBucketTime = now;
            m_currentFrame = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(m_currentFrame) + 1u);
            result = m_currentFrame;
            if (m_currentFrame > m_currentFrameEnd)
            {
                m_currentFrame = m_currentFrameBegin;
                result = m_currentFrame;
            }
        }
        return result;
    }


    int SPRITE::AttackTact(int deltaTime) noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid == m_vid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                int result = child->AttackTact(deltaTime);
                if (result == 5 && m_goalSprite != nullptr)
                    result = 6;
                return result;
            }
        }

        if (m_vid->hasWeaponChildDescriptor() == 0u || m_vid->weaponCount() == 0u)
            return 8;

        SPRITE* const target = m_goalSprite;
        if (!target)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (uplink->Vid()->spriteClassId() != 7 && m_actionTimer == 0u)
                    RotateTact(uplink->Direction(), deltaTime);
            }
            return 5;
        }

        const std::uint32_t waitTimer = m_actionTimer;
        if (waitTimer > 5000u || (m_currentAnimation == 8 && m_currentFrame <= m_currentFrameEnd))
        {
            float radius = 0.0f;
            const int targetDirection = DirectionTo(Goal(), &radius).Int();
            RotateTact(targetDirection, deltaTime);
            return 4;
        }

        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits != 0x14u && commandBits != 0x0Cu && commandBits != 0x10u)
        {
            if (SPRITE* const uplink = m_childBacklink)
            {
                if (uplink->Vid()->spriteClassId() != 7 && waitTimer == 0u)
                    RotateTact(uplink->Direction(), deltaTime);
            }
            return 6;
        }

        const float battleRange = m_vid->weaponBattleRange();
        const float minimumRange = m_vid->weaponMinimumRange();
        const float metricDx = std::fabs(target->m_xyz.x - m_xyz.x);
        const float metricDy = std::fabs(target->m_xyz.y - m_xyz.y);
        const float metric = !(metricDx > metricDy)
            ? metricDx * 0.5f + metricDy
            : metricDx + metricDy * 0.5f;
        SPRITE* const uplink = m_childBacklink;
        const bool uplinkByBattleRange = x87LessEqualOrUnordered(battleRange, metric);
        const bool uplinkByMinimumRange = x87LessEqualOrUnordered(metric, minimumRange);
        if (uplink && commandBits != 0x14u &&
            (uplinkByBattleRange || uplinkByMinimumRange))
        {
            if (uplink->Vid()->spriteClassId() != 7)
                RotateTact(uplink->Direction(), deltaTime);
        }
        else
        {
            float radius = 0.0f;
            const ANGLE targetDirection = DirectionTo(Goal(), &radius);
            const int rotateResult = RotateTact(targetDirection, deltaTime).Int();
            if (rotateResult == 0 || (m_vid->weaponFlags() & 1) != 0)
            {
                if (x87LessEqualOrUnordered(metric, battleRange))
                {
                    const std::int32_t scale = static_cast<std::int32_t>(
                        m_vid->fightNoChildValue());
                    const int ammo = dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                    if (ammo < spriteAbs32Wrap(scale))
                        return 7;


                    const SPRITE* const parent = m_childBacklink;
                    const std::uint32_t weaponFlags =
                        static_cast<std::uint32_t>(m_vid->weaponFlags());
                    if ((!parent || parent->Vid()->spriteClassId() != 7u) &&
                        metric > 115.0f &&
                        (weaponFlags & 0x00000001u) == 0u &&
                        (weaponFlags & 0x00040000u) != 0u)
                    {
                        if (attackTerrainGate(target->X(), target->Y(), target->Z()))
                            return 2;
                    }

                    dispatchVirtualAction(ActionCode::ACT_ADD_AMMO, spriteNeg32Wrap(scale), 0, 0);
                    ChangeAnimation(8);


                    m_actionTimer = static_cast<std::uint32_t>(
                        static_cast<std::uint32_t>(m_vid->weaponReloadTime()) +
                        5000u);
                    return 0;
                }

                if (commandBits == 0x0Cu)
                    return 1;
                const float detectRange = m_vid->weaponDetectRange();
                return x87LessEqualOrUnordered(detectRange + detectRange, metric) ? 3 : 2;
            }
        }

        const float postDx = std::fabs(target->m_xyz.x - m_xyz.x);
        const float postDy = std::fabs(target->m_xyz.y - m_xyz.y);
        const float postMetric = !(postDx > postDy)
            ? postDx * 0.5f + postDy
            : postDx + postDy * 0.5f;
        if (x87LessEqualOrUnordered(postMetric, battleRange))
            return 4;
        if (commandBits == 0x0Cu)
            return 1;
        const float detectRange = m_vid->weaponDetectRange();
        return x87LessEqualOrUnordered(detectRange + detectRange, postMetric) ? 3 : 2;
    }


    int SPRITE::PercentHp() noexcept
    {
        if (SPRITE* const child = m_childChain)
        {
            VID* const thisVid = m_vid;
            VID* const childVid = child->m_vid;
            if (childVid == thisVid->linkedVid() &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
            {
                const int bucket = child->armyIndex();
                const int duration = childVid->GetMaxHp(bucket);
                if (duration != 0)
                {
                    const std::uint32_t rawNumerator =
                        (static_cast<std::uint32_t>(child->m_hp) << 8) -
                        static_cast<std::uint32_t>(child->m_hp);
                    const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
                    return numerator / duration;
                }
            }
        }

        VID* const vid = m_vid;
        const int bucket = armyIndex();
        const int maxHp = vid->GetMaxHp(bucket);
        if (maxHp == 0)
            return 0;

        const std::uint32_t rawNumerator =
            (static_cast<std::uint32_t>(m_hp) << 8) -
            static_cast<std::uint32_t>(m_hp);
        const std::int32_t numerator = static_cast<std::int32_t>(rawNumerator);
        return numerator / maxHp;
    }


    void SPRITE::Stop()
    {


        const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
        if (commandBits == 0u || commandBits == 4u)
            (void)SetCommandWithoutLink(0, nullptr);

        VID* const vid = m_vid;
        m_zSpeed = 0.0f;


        m_runtimeFlags &= 0xFFFE7F7Fu;

        if (vid->slowValue() == 999999.0f)
            m_speed = 0.0f;
    }


    int SPRITE::traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept
    {
        return GlobalSpriteCollector()->traceVidMovementCollision(
            m_vid, m_xyz.x, m_xyz.y, m_xyz.z,
            xOut, yOut, zOut) ? 1 : 0;
    }

    __declspec(safebuffers)
    int SPRITE::StartMove() noexcept
    {
        VID* const vid = m_vid;
        const float runtimeMaxSpeed = MaxSpeed();
        if (runtimeMaxSpeed == 0.0f)
            return 0;

        SPRITE* const target = m_goalSprite;
        float projectedLength = 0.0f;

        if (target)
        {
            if (m_xyz.x == target->m_xyz.x && m_xyz.y == target->m_xyz.y)
                return 0;

            if ((vid->directionCount() == 1 || (vid->properties() & P_RANDBIRTH) != 0u) &&
                (vid->properties() & P_MOVEWITHANYDIRECTION) == 0u)
            {
                const float dy = target->m_xyz.y - m_xyz.y;
                const float dx = target->m_xyz.x - m_xyz.x;
                const ANGLE direction(dx, dy, &projectedLength);
                ChangeDirection(direction.Int());
            }

            if ((vid->spriteTypeId() & 0x00000200u) == 0u && vid->spriteClassId() != B_CANNON)
            {
                if (target->m_xyz.z > m_xyz.z)
                    m_zSpeed = vid->maximumZSpeed();
                else if (target->m_xyz.z < m_xyz.z)
                    m_zSpeed = -vid->maximumZSpeed();
                else
                    m_zSpeed = 0.0f;
            }
            else
            {
                if (projectedLength == 0.0f)
                {
                    const float dy = target->m_xyz.y - m_xyz.y;
                    const float dx = target->m_xyz.x - m_xyz.x;
                    const ANGLE direction(dx, dy, &projectedLength);
                    (void)direction;
                    if (projectedLength == 0.0f)
                        return 0;
                }

                m_zSpeed = vid->calculateMoveUpZ(
                    target->m_xyz.z - m_xyz.z, projectedLength);
            }
        }


        if ((m_runtimeFlags & MovementStartedFlag) == 0u &&
            m_currentAnimation < 15 &&
            m_currentAnimation != 3)
        {
            const bool hasAnimation3Content =
                vid->declaredAnimationFrameCount(3) != 0 ||
                vid->childVidForDataCode(3) != nullptr ||
                vid->sfx[3] != 0;


            if (m_speed == 0.0f && hasAnimation3Content)
            {
                ChangeAnimation(3);
                if ((vid->properties() & P_NOISE) != 0u)
                {
                    const int span = m_currentFrameEnd - m_currentFrameBegin;
                    if (span > 0)
                        m_currentFrame += std::rand() % (span + 1);
                }
            }
            else
            {
                bool useTurnAnimation = false;
                if (SPRITE* const child = m_childChain)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid && childVid->weapon &&
                        (static_cast<std::uint32_t>(childVid->weaponFlags()) & 0x800u) != 0u &&
                        vid->declaredAnimationFrameCount(6) != 0)
                    {
                        const unsigned char selfDirection = static_cast<unsigned char>(m_direction.Int());
                        const unsigned char childDirection = static_cast<unsigned char>(child->m_direction.Int());
                        const unsigned char clockwise = static_cast<unsigned char>(selfDirection - childDirection);
                        const unsigned char counterClockwise = static_cast<unsigned char>(childDirection - selfDirection);
                        const unsigned char delta = clockwise < counterClockwise ? clockwise : counterClockwise;
                        useTurnAnimation = delta > 0x40u;
                    }
                }
                ChangeAnimation(useTurnAnimation ? 6 : 2);
            }
        }


        m_runtimeFlags = (m_runtimeFlags & 0xFFFE7FFFu) | MovementStartedFlag;

        if (vid->accelerationValue() == 999999.0f)
            m_speed = runtimeMaxSpeed;

        return 1;
    }


    void SPRITE::DrawDebugOverlay()
    {
        GRAPH* const graph = Graph;
        if (!graph)
            return;

        const float left = static_cast<float>(graph->ViewXMin());
        float y = static_cast<float>(graph->ViewYMin());
        const int goalNvid = m_goalSprite ? m_goalSprite->Vid()->nvid() : 0;


        graph->DrawText(
            left + 22.0f, y,
            "Ref=%-3i cmd=%1i ani=%-2i hp=%-3i AT=%i goal=%-3i spd=%-3i,%-3i timer=%i ammo=%i mvE=%1u%1u %i,%i,%i",
            listReferenceCount(),
            commandIndex(),
            m_currentAnimation,
            m_hp,
            m_attackDecisionCode,
            goalNvid,
            spriteMultiplyAndConvertToInt32(m_speed, 1000.0f),
            spriteMultiplyAndConvertToInt32(m_zSpeed, 1000.0f),
            static_cast<int>(m_actionTimer),
            dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0),
            static_cast<unsigned>((m_runtimeFlags & CrossedGoalXFlag) != 0u),
            static_cast<unsigned>((m_runtimeFlags & CrossedGoalYFlag) != 0u),
            static_cast<int>(X()),
            static_cast<int>(Y()),
            static_cast<int>(Z()));

        if (HaveLink())
        {
            SPRITE* const child = m_childChain;
            y += 12.0f;
            const int childGoalNvid = child->m_goalSprite ? child->m_goalSprite->Vid()->nvid() : 0;
            graph->DrawText(
                left + 22.0f, y,
                "Ref=%-3i cmd=%1i ani=%-2i hp=%-3i AT=%i goal=%-3i timer=%i ammo=%i",
                child->listReferenceCount(),
                child->commandIndex(),
                child->m_currentAnimation,
                child->m_hp,
                child->m_attackDecisionCode,
                childGoalNvid,
                static_cast<int>(child->m_actionTimer),
                child->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0));
        }


        if (m_childChain && m_childChain->m_childChain)
        {
            y += 12.0f;
            STRING chainText;
            for (SPRITE* cursor = this; cursor; cursor = cursor->m_childChain)
            {
                const STRING item = STRING::Format("%i ", cursor->m_vid->nvid());
                chainText.append(item);
            }
            graph->drawStringColored(left + 30.0f, y, chainText, 0xFFFFFFFFu);
        }

        const std::uint32_t commandCount = m_commandStack.m_commandRecords.count;
        if (commandCount != 0u)
        {
            y += 12.0f;
            STRING commandText = STRING::Format("%i - ", static_cast<int>(commandCount));
            const auto* const records = m_commandStack.m_commandRecords.records;

            for (std::uint32_t cursor = commandCount; cursor != 0u; --cursor)
            {
                const std::uint32_t* const words = records[cursor - 1u].words;
                const STRING item = STRING::Format("%i(%i,%i,%i) ",
                    static_cast<int>(words[0]), static_cast<int>(words[1]),
                    static_cast<int>(words[2]), static_cast<int>(words[3]));
                commandText.append(item);
            }

            graph->drawStringColored(left + 30.0f, y, commandText, 0xFFFFFFFFu);
        }

        DrawRelationDebugOverlay();
    }


    void SPRITE::DrawRelationDebugOverlay()
    {
        GRAPH* const graph = Graph;

        if (SPRITE* const goal = Goal())
        {
            graph->Line(ScreenX(), ScreenY(),
                        goal->ScreenX(), goal->ScreenY(),
                        g_colorGreen.color);
        }

        if (SPRITE* const child = childChain())
        {
            if (SPRITE* const goal = child->Goal())
            {
                graph->Line(child->ScreenX(), child->ScreenY(),
                            goal->ScreenX(), goal->ScreenY(),
                            g_colorRed.color);
            }
        }
    }


    void SPRITE::Remove()
    {
        if (SPRITE* child = childChain())
            child->Remove();
        if (auto* const application = reinterpret_cast<as1::core::Application*>(as1::core::ApplicationOwner()))
            application->removeSpriteFromApplicationLists(this);
        else
            (void)as1::core::Application::removeSpriteFromDrawBucket(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }


    void SPRITE::Insert()
    {
        if (SPRITE* child = childChain())
            child->Insert();
        if (auto* const application = reinterpret_cast<as1::core::Application*>(as1::core::ApplicationOwner()))
            (void)application->appendSpriteToApplicationListsAndReleaseReference(this);
        else
            (void)as1::core::Application::appendSpriteToDrawBucketAndRelease(as1::core::GlobalApplicationDrawDispatcherState(), this);
    }


    unsigned int SPRITE::serializeSpriteRecord(RESOURCE* resource) noexcept
    {
        const DWORD flags = m_runtimeFlags;
        if ((flags & 0x00000100u) != 0u)
            return flags;

        const std::uint32_t armyBits = (flags >> ArmyBitsShift) & ArmyValueMask;
        const std::uint32_t rawSprite = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
        resource->write(&rawSprite, 4u);
        resource->write(&m_vid->nVid, 4u);
        resource->write(&m_xyz.x, 4u);
        resource->write(&m_xyz.y, 4u);
        resource->write(&m_xyz.z, 4u);

        const int direction = m_direction.Int();
        resource->write(&direction, 4u);
        return static_cast<unsigned int>(resource->write(&armyBits, 4u));
    }

    namespace
    {
        int signedHalfTowardZero(int value) noexcept
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int rescaleHpForArmyChange(int frameTime, int oldDuration, int nextDuration) noexcept
        {
            const std::int32_t numerator = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(frameTime) << 8u);
            const std::int32_t quotient = numerator / oldDuration;
            const std::int32_t product = spriteImul32Low(quotient, nextDuration);
            const std::int32_t sign = product < 0 ? -1 : 0;
            const std::int32_t remainderBias = sign & 0xFF;
            const std::int32_t biased = spriteAdd32Wrap(product, remainderBias);
            return biased >> 8;
        }
    }


    void SPRITE::suppressDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags |= DrawSuppressedFlag;
    }


    void SPRITE::restoreDrawRecursive() noexcept
    {
        for (SPRITE* node = this; node; node = node->m_childChain)
            node->m_runtimeFlags &= ~DrawSuppressedFlag;
    }


    void SPRITE::ChangeHp(int newHp) noexcept
    {
        VID* const vid = m_vid;
        const int army = armyIndex();

        if (newHp <= 0 && vid->maximumHp() != 0)
        {
            if (m_currentAnimation < 15)
            {
                vid->incrementKilledUnitCountForArmy(army);

                const int damage = spriteSub32Wrap(m_hp, newHp);
                const int maxHp = vid->GetMaxHp(army);
                const int triple = spriteImul32Low(maxHp, 3);
                const int sign = triple < 0 ? -1 : 0;
                const int death2Threshold = (triple - sign) >> 1;
                const bool hasDeath2Route =
                    vid->childVidForDataCode(16) != nullptr ||
                    vid->declaredAnimationFrameCount(16) != 0;

                ChangeAnimation((damage > death2Threshold && hasDeath2Route) ? 16 : 15);
            }
            return;
        }

        const int maxHp = vid->GetMaxHp(army);
        const int halfHp = signedHalfTowardZero(maxHp);

        if (newHp > halfHp && m_hp <= halfHp)
            (void)deleteChildByVid(vid->woundChildVid());

        if (newHp <= halfHp && m_hp > halfHp)
        {
            if (vid->declaredAnimationFrameCount(13) != 0 &&
                (m_currentAnimation == 0 || m_currentAnimation == 2))
            {
                ChangeAnimation(13);
                m_hp = newHp;
                return;
            }

            (void)CreateChildFor(13, nullptr);
        }

        m_hp = newHp;
    }


    int SPRITE::ChangeArmy(int bucketIndex) noexcept
    {
        const int oldBucket = armyIndex();
        const int nextBucket = bucketIndex & static_cast<int>(ArmyValueMask);
        m_runtimeFlags = (m_runtimeFlags & ~ArmyBitsMask) | (static_cast<DWORD>(nextBucket) << ArmyBitsShift);

        if (SPRITE* const child = m_childChain)
            (void)child->ChangeArmy(nextBucket);

        VID* const vid = m_vid;
        const int nextDuration = vid->GetMaxHp(nextBucket);
        const int oldDuration = vid->GetMaxHp(oldBucket);
        if (nextDuration != oldDuration)
            ChangeHp(rescaleHpForArmyChange(m_hp, oldDuration, nextDuration));

        if ((vid->properties() & P_INVISIBLEFORENEMY) != 0)
        {
            if (nextBucket == static_cast<int>(core::ActivePlayerIndex()))
            {
                m_runtimeFlags &= ~DrawSuppressedFlag;
                if (SPRITE* const child = m_childChain)
                    child->restoreDrawRecursive();
            }
            else
            {
                m_runtimeFlags |= DrawSuppressedFlag;
                if (SPRITE* const child = m_childChain)
                    child->suppressDrawRecursive();
            }
        }

        if (vid->NoSprites(oldBucket) != 0u)
            vid->DecreaseNoSprites(oldBucket);
        vid->setLastSpriteCountChangeTimestamp(core::RealCurrentTime);
        vid->incrementSpriteCountForArmy(nextBucket);
        return nextBucket;
    }


    __declspec(safebuffers)
    void SPRITE::ensureLinkedVidChild() noexcept
    {
        VID* const vid = m_vid;
        VID* const linkVid = vid->linkedVid();
        if (!linkVid)
            return;

        if (SPRITE* child = childChain())
        {
            if (child->Vid() == linkVid)
                return;
        }

        if (linkVid->isNotCreateAsChild())
            return;

        const int direction = directionIndex();
        const int directionByte = direction & 0xFF;
        const VECTOR& link = vid->linkOffset();
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if ((linkVid->properties() & P_NOTCHANGELINKERCOOR) != 0u)
        {
            offsetX = link.x;
            offsetY = -link.y;
        }
        else
        {
            offsetX = (directionCos(directionByte) * link.x) +
                      (directionSin(directionByte) * link.y);
            offsetY = (directionSinAux(directionByte) * link.x) -
                      (directionCosAux(directionByte) * link.y);
        }
        const VECTOR target(m_xyz.x + offsetX, m_xyz.y + offsetY, m_xyz.z + link.z);

        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
        SPRITE* const created = map->CreateSprite(
            linkVid, target, ANGLE(direction), nullptr, false);
        insertChildChainHead(created);

        if (SPRITE* child = childChain())
        {
            child->ChangeArmy(armyIndex());
            return;
        }

        LOG::ResourceError("SPRITE %i", 3, "link", 0, vid->nVid);
    }

    int SPRITE::spawnAnimationChild() noexcept
    {
        const int animationSlot = m_currentAnimation;

        VID* const childVid = m_vid->childVid[animationSlot];
        if (!childVid)
            return 0;
        if (childVid->isNotCreateAsChild() != 0)
            return 0;

        CreateChild();
        return m_currentAnimation;
    }


    void SPRITE::SetMoveAnimation()
    {
        const int animation = Animation();
        if (animation >= 15)
            return;

        if (animation == 8 && currentFrame() <= currentFrameEnd())
            return;

        SPRITE* const backlink = childBacklink();
        const bool backlinkMoving = backlink && backlink->Speed() != 0.0f;
        const float speed = Speed();

        if (speed == 0.0f)
        {
            if (backlinkMoving)
            {
                if (animation == 10)
                    return;

                const unsigned char angleDistance = shortestAngleDistance(
                    directionIndex(), backlink->directionIndex());
                if (backlink->Animation() != 6 &&
                    (angleDistance < 0x23u || angleDistance > 0x60u))
                    ChangeAnimation(2);
                else
                    ChangeAnimation(0);
                return;
            }

            if (animation != 0 && animation != 10)
                ChangeAnimation(0);

            if (SPRITE* const child = childChain())
            {
                if (child->Animation() == 2)
                    child->ChangeAnimation(0);
            }
            return;
        }

        if ((runtimeFlags() & 0x80u) == 0u)
        {
            const float quarterMaximum = std::fabs(MaxSpeed()) * 0.25f;
            if (std::fabs(speed) <= quarterMaximum)
            {
                if (animation != 1)
                    ChangeAnimation(1);
                return;
            }
        }

        if (animation == 6)
        {
            SPRITE* const child = childChain();
            if (child && child->Vid() &&
                (static_cast<std::uint32_t>(child->Vid()->weaponFlags()) & 0x00000800u) != 0u &&
                shortestAngleDistance(directionIndex(), child->directionIndex()) > 0x40u)
            {
                const float maximum = MaxSpeed();
                if (std::fabs(maximum) < std::fabs(speed))
                    setSpeedDirect(maximum);
                return;
            }
        }

        if (Animation() != 2)
            ChangeAnimation(2);
    }


    int SPRITE::CreateChildFor(int forAnimation, SPRITE* eventObject) noexcept
    {
        const int savedAnimation = m_currentAnimation;
        m_currentAnimation = forAnimation;

        if (m_vid->childVidForDataCode(forAnimation))
            CreateChild();

        const int sfx = m_vid->sfxForAnimation(forAnimation);
        if (sfx != 0)
            PlaySFX(sfx);

        int result = 0;
        const int functionIndex = m_vid->scriptFunctionAt(forAnimation);
        if (functionIndex >= 0 && forAnimation != 14)
        {
            const int selfArgument = static_cast<int>(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(this)));
            const int eventArgument = static_cast<int>(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(eventObject)));
            result = core::Application::callScriptFunction(
                functionIndex, selfArgument, eventArgument, 0);
        }

        m_currentAnimation = savedAnimation;
        return result;
    }


    int SPRITE::HaveLink() const noexcept
    {
        SPRITE* const child = childChain();
        return child && child->Vid() == Vid()->linkedVid() ? 1 : 0;
    }


    int SPRITE::CanAttackThisSprite(const SPRITE* owner) const noexcept
    {
        if (!owner)
            return 0;

        VID* const vid = m_vid;
        const std::uint32_t ownerType = owner->m_vid->spriteTypeId();

        if (vid->hasWeaponChildDescriptor() != 0u &&
            vid->weaponCount() != 0u &&
            (static_cast<std::uint32_t>(vid->weaponTypeMask()) & ownerType) != 0u)
        {
            return 1;
        }

        SPRITE* const child = m_childChain;
        if (!child)
            return 0;

        VID* const childVid = child->m_vid;
        VID* const linkVid = vid->linkedVid();
        if (childVid != linkVid)
            return 0;
        if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
            return 0;

        return (static_cast<std::uint32_t>(childVid->weaponTypeMask()) & ownerType) != 0u ? 1 : 0;
    }


    int SPRITE::isCanShotEnemy(SPRITE*) const noexcept
    {
        return 1;
    }

    int SPRITE::canWeaponAffectTarget(SPRITE* owner) noexcept
    {
        return CanAttackThisSprite(owner);
    }
    __declspec(safebuffers)


    int SPRITE::Attack(SPRITE* owner) noexcept
    {
        VID* const vid = m_vid;
        const int spriteClass = static_cast<int>(vid->spriteClassId());

        if (spriteClass == B_LINKER && vid->hasWeaponChildDescriptor() != 0u)
        {
            const int savedAnimation = m_currentAnimation;
            m_currentAnimation = 8;
            SetCommand(4, owner);

            CreateChild();
            m_currentAnimation = savedAnimation;


            SetCommand(0, nullptr);
            return 0;
        }

        if (owner &&
            owner->m_vid != EmptyVid &&
            CanAttackThisSprite(owner) == 0 &&
            spriteClass != B_CANNON &&
            dispatchVirtualAction(ActionCode::ACT_IS_TRAIN, 0, 0, 0) == 0)
        {
            return 0;
        }


        (void)SetCommand(3, owner);
        return 0;
    }


    int SPRITE::GetFireDamage() noexcept
    {
        int result = 0;
        for (SPRITE* node = this; node; node = node->childChain())
        {
            VID* const nodeVid = node->Vid();
            VID* const metricVid = nodeVid->fightChildVid();
            if (metricVid && nodeVid->weaponCount() != 0u)
                result += metricVid->GetFireDamage();
        }
        return result;
    }


    SPRITE* SPRITE::engineChainHead() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            result = node;
        return result;
    }


    bool SPRITE::isInEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return false;

        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            if (node == target)
                return true;
        }
        for (SPRITE* node = engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
        {
            if (node == target)
                return true;
        }
        return false;
    }

    int SPRITE::minimumEngineWeaponRange() noexcept
    {
        if (!Goal())
            return 0;

        const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
        if (actionBits != 0x70u && actionBits != 0x74u)
            return 0;

        const auto candidateHeight = [](SPRITE* node) noexcept -> float
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return 10000.0f;

            VID* const nodeVid = node->Vid();
            VID* const childVid = child->Vid();
            if (childVid != nodeVid->linkedVid())
                return 10000.0f;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return 10000.0f;
            if (node->ammoCount() <= 0)
                return 10000.0f;

            VID* valueOwner = childVid;
            if (nodeVid->nvid() == 35)
                valueOwner = nodeVid;

            const float value = valueOwner->weaponBattleRange();

            return x87LessOrUnordered(value, 10000.0f) ? value : 10000.0f;
        };

        float result = 10000.0f;
        if (SPRITE* const refOwner = engineCommandReferenceOwnerRef())
        {
            result = candidateHeight(refOwner);
        }
        else
        {
            for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            {
                const float value = candidateHeight(node);
                if (result > value)
                    result = value;
            }
        }

        if (x87EqualOrUnordered(result, 10000.0f))
            result = 0.0f;

        return spriteConvertFloatToInt32(static_cast<long double>(result));
    }


    void ENGINE::PullTail(const R_POS* oldHead) noexcept
    {
        using PathNode = core::R_DOT;
        using PathEdge = core::R_DOT::Link;

        const int primaryProgress = primaryPathProgressRef();
        VID* const vid = Vid();
        const float radiusFloat = vid->weaponRadius();
        const int radiusLimit = spriteConvertFloatToInt32(static_cast<long double>(radiusFloat));

        auto edgeAt = [](PathNode* node, int index) noexcept -> PathEdge&
        {
            return *node->linkAt(index);
        };

        if (!spriteFildIntLessEqualOrUnordered(primaryProgress, radiusFloat))
        {
            PathEdge& edge = edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef());
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(static_cast<int>(edge.length), primaryProgress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(
                spriteAdd32Wrap(static_cast<int>(edge.length), radiusLimit), primaryProgress);
            secondaryPathProgressRef() = result;
            return;
        }

        if (secondaryPathNodeRef() == primaryPathNodeRef())
        {
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return;
        }

        PathNode* const argumentNode = oldHead->node;
        if (argumentNode == primaryPathNodeRef())
        {
            int bridgeIndex = primaryPathNodeRef()->GetLink(secondaryPathNodeRef());
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail 1", 0,
                                   vid ? vid->nvid() : -1);
                const core::R_POS primaryPath{
                    primaryPathNodeRef(), primaryPathProgressRef(),
                    primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
                const unsigned char facing = primaryPath.Direct().value;
                bridgeIndex = primaryPathNodeRef()->GetLink(ANGLE(static_cast<unsigned char>(facing - 128)));
                secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), bridgeIndex).target;
            }

            const int duration = static_cast<int>(edgeAt(primaryPathNodeRef(), bridgeIndex).length);
            int result = 0;
            const int durationPlusPrimary = spriteAdd32Wrap(duration, primaryProgress);
            if (spriteFildIntLessEqualOrUnordered(durationPlusPrimary, radiusFloat))
            {
                result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, duration), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else if (primaryPathEdgeIndexRef() == bridgeIndex)
            {
                result = spriteSub32Wrap(spriteAdd32Wrap(duration, radiusLimit), primaryProgress);
                secondaryPathProgressRef() = result;
            }
            else
            {
                secondaryPathNodeRef() = primaryPathNodeRef();
                secondaryPathEdgeIndexRef() = bridgeIndex;
                result = spriteSub32Wrap(radiusLimit, primaryProgress);
                secondaryPathProgressRef() = result;
            }
            return;
        }

        if (argumentNode == secondaryPathNodeRef())
        {
            const int duration = argumentNode
                ? static_cast<int>(edgeAt(argumentNode, oldHead->edgeIndex).length)
                : 0;
            const int primaryPlusDuration = spriteAdd32Wrap(primaryProgress, duration);
            if (spriteFildIntLessEqualOrUnordered(primaryPlusDuration, radiusFloat))
            {
                const int result = spriteSub32Wrap(spriteSub32Wrap(radiusLimit, primaryProgress), duration);
                secondaryPathProgressRef() = result;
            return;
            }

            PathEdge& edge = edgeAt(argumentNode, oldHead->edgeIndex);
            secondaryPathNodeRef() = edge.target;
            secondaryPathProgressRef() = spriteSub32Wrap(
                static_cast<int>(edge.length), oldHead->progress);
            secondaryPathAuxiliaryRef() = 0;
            secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
            const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
            secondaryPathProgressRef() = result;
            return;
        }

        const int argumentDuration = argumentNode
            ? static_cast<int>(edgeAt(argumentNode, oldHead->edgeIndex).length)
            : 0;
        const int argumentPlusPrimary = spriteAdd32Wrap(argumentDuration, primaryProgress);
        if (spriteFildIntLessEqualOrUnordered(argumentPlusPrimary, radiusFloat))
        {
            writeLogLine(g_fileLogger, "zmdots6");
            int bridgeIndex = argumentNode->GetLink(secondaryPathNodeRef());
            secondaryPathEdgeIndexRef() = bridgeIndex;
            secondaryPathNodeRef() = argumentNode;
            const int result = spriteSub32Wrap(
                spriteSub32Wrap(radiusLimit, argumentDuration), primaryProgress);
            secondaryPathProgressRef() = result;
            if (bridgeIndex < 0)
            {
                LOG::ResourceError("ENGINE %i", 10, "rail 2", 0,
                                   vid ? vid->nvid() : -1);
                const unsigned char facing = oldHead->Direct().value;
                bridgeIndex = secondaryPathNodeRef()->GetLink(ANGLE(static_cast<unsigned char>(facing - 128)));
                secondaryPathEdgeIndexRef() = bridgeIndex;
                return;
            }
            return;
        }

        writeLogLine(g_fileLogger, "zmdots5");
        PathEdge& edge = edgeAt(argumentNode, oldHead->edgeIndex);
        secondaryPathNodeRef() = edge.target;
        secondaryPathProgressRef() = spriteSub32Wrap(
            static_cast<int>(edge.length), oldHead->progress);
        secondaryPathAuxiliaryRef() = 0;
        secondaryPathEdgeIndexRef() = edge.reciprocalIndex;
        const int result = spriteSub32Wrap(radiusLimit, primaryProgress);
        secondaryPathProgressRef() = result;
            return;
    }


    ENGINE* ENGINE::GetIntersecting() noexcept
    {
        auto edgeTarget = [](core::R_DOT* node, int index) noexcept -> core::R_DOT*
        {
            return node ? node->linkAt(index)->target : nullptr;
        };

        ENGINE* candidate = static_cast<ENGINE*>(primaryPathNodeRef()->ownerSprite());
        if (candidate && IsTouch(candidate, 0))
            return static_cast<ENGINE*>(primaryPathNodeRef()->ownerSprite());

        core::R_DOT* target = edgeTarget(primaryPathNodeRef(),
                                                   primaryPathEdgeIndexRef());
        if (target->ownerSprite() && IsTouch(static_cast<ENGINE*>(target->ownerSprite()), 0))
            return static_cast<ENGINE*>(edgeTarget(primaryPathNodeRef(),
                              primaryPathEdgeIndexRef())->ownerSprite());

        candidate = static_cast<ENGINE*>(secondaryPathNodeRef()->ownerSprite());
        if (candidate && IsTouch(candidate, 0))
            return static_cast<ENGINE*>(secondaryPathNodeRef()->ownerSprite());

        target = edgeTarget(secondaryPathNodeRef(), secondaryPathEdgeIndexRef());
        if (target->ownerSprite() && IsTouch(static_cast<ENGINE*>(target->ownerSprite()), 0))
            return static_cast<ENGINE*>(edgeTarget(secondaryPathNodeRef(),
                              secondaryPathEdgeIndexRef())->ownerSprite());
        return nullptr;
    }


    int ENGINE::IsTouch(ENGINE* target, int strictProgressGate) noexcept
    {
        if (!target || isInEngineChain(target))
            return 0;

        auto edgeTarget = [](core::R_DOT* node, int index) noexcept -> core::R_DOT*
        {
            return node ? node->links()[static_cast<std::size_t>(index)].target : nullptr;
        };
        auto edgeDuration = [](core::R_DOT* node, int index) noexcept -> int
        {
            return node ? static_cast<int>(node->links()[static_cast<std::size_t>(index)].length) : 0;
        };
        auto progressPasses = [strictProgressGate](int progress, int duration, int otherProgress) noexcept -> bool
        {
            const int threshold = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(duration) -
                static_cast<std::uint32_t>(otherProgress));
            return strictProgressGate ? progress > threshold : progress >= threshold;
        };

        core::R_DOT* const primary = primaryPathNodeRef();
        if (primary)
        {
            core::R_DOT* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 1;
            }

            core::R_DOT* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(primary, primaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == primary &&
                progressPasses(primaryPathProgressRef(),
                               edgeDuration(primary, primaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 2;
            }
        }

        core::R_DOT* const secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::R_DOT* const targetPrimary = target->primaryPathNodeRef();
            if (targetPrimary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetPrimary &&
                edgeTarget(targetPrimary, target->primaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->primaryPathProgressRef()))
            {
                return 3;
            }

            core::R_DOT* const targetSecondary = target->secondaryPathNodeRef();
            if (targetSecondary &&
                edgeTarget(secondary, secondaryPathEdgeIndexRef()) == targetSecondary &&
                edgeTarget(targetSecondary, target->secondaryPathEdgeIndexRef()) == secondary &&
                progressPasses(secondaryPathProgressRef(),
                               edgeDuration(secondary, secondaryPathEdgeIndexRef()),
                               target->secondaryPathProgressRef()))
            {
                return 4;
            }
        }

        if (!strictProgressGate)
        {
            if (primary)
            {
                if (primary == target->primaryPathNodeRef())
                    return 100;
                if (primary == target->secondaryPathNodeRef())
                    return 200;
            }
            if (secondary)
            {
                if (secondary == target->primaryPathNodeRef())
                    return 300;
                if (secondary == target->secondaryPathNodeRef())
                    return 400;
            }
            return 0;
        }

        if (primary)
        {
            if (primary == target->primaryPathNodeRef())
                return primaryPathEdgeIndexRef() == target->primaryPathEdgeIndexRef() ? 2 : 1;
            if (primary == target->secondaryPathNodeRef())
                return 200;
        }
        if (secondary)
        {
            if (secondary == target->primaryPathNodeRef())
                return 300;
            if (secondary == target->secondaryPathNodeRef())
                return 400;
        }
        return 0;
    }


    SPRITE* SPRITE::engineChainTail() noexcept
    {
        SPRITE* result = this;
        for (SPRITE* node = engineChainNextRef(); node; node = node->engineChainNextRef())
            result = node;
        return result;
    }


    int SPRITE::attachEngineChain(SPRITE* target) noexcept
    {
        if (!target || isInEngineChain(target))
            return 1;

        auto edgeAt = [](core::R_DOT* node, int index) noexcept -> core::R_DOT::Link&
        {
            return *node->linkAt(index);
        };
        auto distance3 = [this](core::R_DOT* node) noexcept -> double
        {
            const double dx = static_cast<double>(node->x()) - static_cast<double>(m_xyz.x);
            const double dy = static_cast<double>(node->y()) - static_cast<double>(m_xyz.y);
            const double dz = static_cast<double>(node->id()) - static_cast<double>(m_xyz.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };

        if (!target->engineChainPreviousRef() && !target->engineChainNextRef())
        {
            core::R_DOT* const primaryEnd =
                edgeAt(target->primaryPathNodeRef(),
                       target->primaryPathEdgeIndexRef()).target;
            core::R_DOT* const secondaryEnd =
                edgeAt(target->secondaryPathNodeRef(),
                       target->secondaryPathEdgeIndexRef()).target;
            if (distance3(secondaryEnd) > distance3(primaryEnd))
                static_cast<ENGINE*>(target)->ReverseTrain();
        }
        else
        {
            SPRITE* const first = target->engineChainHead();
            const double firstDx = static_cast<double>(first->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double firstDy = static_cast<double>(first->m_xyz.y) - static_cast<double>(m_xyz.y);
            SPRITE* const last = target->engineChainTail();
            const double lastDx = static_cast<double>(last->m_xyz.x) - static_cast<double>(m_xyz.x);
            const double lastDy = static_cast<double>(last->m_xyz.y) - static_cast<double>(m_xyz.y);
            if (std::sqrt(lastDx * lastDx + lastDy * lastDy) >
                std::sqrt(firstDx * firstDx + firstDy * firstDy))
            {
                static_cast<ENGINE*>(target)->ReverseTrain();
            }
        }

        SPRITE* const tail = target->engineChainTail();
        tail->engineChainNextRef() = this;
        engineChainPreviousRef() = tail;

        core::R_DOT::Link& source =
            edgeAt(tail->secondaryPathNodeRef(), tail->secondaryPathEdgeIndexRef());
        primaryPathNodeRef() = source.target;
        primaryPathProgressRef() = static_cast<int>(source.length) - tail->secondaryPathProgressRef();
        primaryPathAuxiliaryRef() = 0;
        primaryPathEdgeIndexRef() = source.reciprocalIndex;

        const core::R_POS tailSecondary{
            tail->secondaryPathNodeRef(), tail->secondaryPathProgressRef(),
            tail->secondaryPathAuxiliaryRef(), tail->secondaryPathEdgeIndexRef()};
        const unsigned char facing = tailSecondary.Direct().value;
        const int secondarySourceIndex = primaryPathNodeRef()->GetLink(ANGLE(facing));
        secondaryPathNodeRef() = edgeAt(primaryPathNodeRef(), secondarySourceIndex).target;
        secondaryPathEdgeIndexRef() = secondaryPathNodeRef()->GetLink(ANGLE(facing));

        unsigned char currentFacing = 0;
        if (primaryPathNodeRef())
            currentFacing = static_cast<unsigned char>(
                edgeAt(primaryPathNodeRef(), primaryPathEdgeIndexRef()).facing);
        const unsigned char deltaA = static_cast<unsigned char>(directionIndex() - currentFacing);
        const unsigned char deltaB = static_cast<unsigned char>(currentFacing - directionIndex());
        const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
        if (delta > 127)
            setDerivedStateValue(0, derivedStateValue(0) | 1);

        core::R_POS primary{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        static_cast<ENGINE*>(this)->PullTail(&primary);
        updatePositionFromPathEndpoints();
        return 0;
    }


    void ENGINE::ClearDotBusy() noexcept
    {
        if (primaryPathNodeRef() && primaryPathNodeRef()->ownerSprite() == this)
            primaryPathNodeRef()->setOwnerSprite(nullptr);

        if (secondaryPathNodeRef() && secondaryPathNodeRef()->ownerSprite() == this)
            secondaryPathNodeRef()->setOwnerSprite(nullptr);

        core::R_DOT* primary = primaryPathNodeRef();
        if (primary)
        {
            core::R_DOT* const target =
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

        core::R_DOT* secondary = secondaryPathNodeRef();
        if (secondary)
        {
            core::R_DOT* const target =
                secondary->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())].target;
            if (target && target->ownerSprite() == this)
                target->setOwnerSprite(nullptr);
        }

    }


    void ENGINE::SetDotBusy() noexcept
    {
        ClearDotBusy();
        if (primaryPathNodeRef())
            primaryPathNodeRef()->setOwnerSprite(this);
        core::R_DOT* const tailNode = secondaryPathNodeRef();
        if (tailNode && tailNode != primaryPathNodeRef())
            tailNode->setOwnerSprite(this);
    }


    int SPRITE::isSpriteClass(int spriteClass) const noexcept
    {
        return m_vid->spriteClassId() == static_cast<DWORD>(spriteClass) ? 1 : 0;
    }


    TRAIN_INFO::TRAIN_INFO(const ENGINE* engine) noexcept
    {

        flags &= 0xFFFFFFFCu;
        maxBattleRange = 0.0f;
        minBattleRange = 999999.0f;
        power = 0.0f;
        weight = 0.0f;
        trainweight = 0.0f;
        speed = 10000;
        no = 0;
        hp = 0;
        max_hp = 0;
        weapon = 0;
        build_time = 0;
        noAmmo = 0;
        ammo = 0;
        maxAmmo = 0;
        percentAmmo = 0;

        for (const ENGINE* node = engine; node; node = static_cast<const ENGINE*>(node->engineChainNext()))
            AddEngine(node);
        if (engine)
        {
            for (const ENGINE* node = static_cast<const ENGINE*>(engine->engineChainPrevious());
                 node;
                 node = static_cast<const ENGINE*>(node->engineChainPrevious()))
            {
                AddEngine(node);
            }
        }

        if (noAmmo != 0)
            percentAmmo /= noAmmo;
        else
            percentAmmo = 100;

        if (minBattleRange == 999999.0f)
            minBattleRange = 0.0f;

        const double poweredReserve = static_cast<double>(power) - static_cast<double>(trainweight);
        if (!std::isnan(poweredReserve) && poweredReserve != 0.0)
        {
            const double nonPoweredWeight = static_cast<double>(weight) - static_cast<double>(trainweight);
            const int projected = static_cast<int>(
                ((poweredReserve - nonPoweredWeight) * static_cast<double>(speed)) / poweredReserve);
            speed = projected;
            if (projected < 5)
                speed = 0;
        }

        if (speed == 10000)
            speed = 0;
    }


    void TRAIN_INFO::AddEngine(const ENGINE* engine) noexcept
    {
        SPRITE* const sprite = const_cast<ENGINE*>(engine);
        VID* const vid = sprite->Vid();
        const float enginePower = vid->weaponFloatAt(0x10);
        const float engineWeight = vid->weaponFloatAt(0x0C);

        if (!x87EqualOrUnordered(enginePower, 0.0f))
        {
            const double candidateSpeed = static_cast<double>(vid->maxSpeedValue()) * 1000.0;
            const double currentSpeed = static_cast<double>(speed);
            if (candidateSpeed < currentSpeed || std::isnan(candidateSpeed) || std::isnan(currentSpeed))
                speed = spriteConvertFloatToInt32(static_cast<long double>(candidateSpeed));
        }

        power += enginePower;
        weight += engineWeight;
        if (enginePower > 0.0f)
            trainweight += engineWeight;

        if (vid->nvid() == 45)
            flags |= 2u;
        else
            flags |= 1u;

        hp += sprite->Hp();

        const int currentAmmo = sprite->ammoFixedPoint() / 64;
        const int maximumAmmo = sprite->MaxAmmo();
        if (currentAmmo > 0)
        {
            float battleRange = vid->weaponBattleRange();
            if (SPRITE* const child = sprite->childChain())
            {
                VID* const link = vid->linkedVid();
                VID* const childVid = child->Vid();
                if (childVid == link && link->hasWeaponChildDescriptor() != 0u &&
                    link->weaponCount() != 0u && x87EqualOrUnordered(battleRange, 0.0f))
                {
                    battleRange = link->weaponBattleRange();
                }
            }

            if (battleRange > maxBattleRange)
                maxBattleRange = battleRange;
            if (battleRange != 0.0f && battleRange < minBattleRange)
                minBattleRange = battleRange;
        }

        if (maximumAmmo != 0 && maximumAmmo != 999999 && vid->nvid() != 85)
        {
            ++noAmmo;
            maxAmmo += maximumAmmo;
            ammo += currentAmmo;
            percentAmmo += (100 * currentAmmo) / maximumAmmo;
        }

        const int army = sprite->armyIndex();
        max_hp += vid->GetMaxHp(army);
        build_time += vid->GetBuildTime();

        int fireDamage = 0;
        if (currentAmmo != 0)
        {
            if (vid->nvid() == 82)
            {
                const core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                VID* metricVid = EmptyVid;
                if (table.count() > 70)
                {
                    if (VID* const slot70 = table.slot(70))
                        metricVid = slot70;
                }
                fireDamage = metricVid->GetFireDamage();
            }
            else
            {
                fireDamage = sprite->GetFireDamage();
            }
        }
        weapon += fireDamage;

        if (VID* const link = vid->linkedVid())
        {
            max_hp += link->GetMaxHp(army);
            SPRITE* const child = sprite->childChain();
            VID* const childVid = child ? child->Vid() : nullptr;
            if (childVid != link &&
                static_cast<int>(link->NoSprites(army)) >=
                    static_cast<int>(vid->NoSprites(army)))
            {
                hp += link->GetMaxHp(army);
            }

            if (childVid == link)
            {
                weight += childVid->weaponFloatAt(0x0C);
                if (childVid->spriteClassId() != 9u)
                    hp += child->Hp();
                if (enginePower > 0.0f)
                    trainweight += childVid->weaponFloatAt(0x0C);
            }
        }

        ++no;
    }

    int TRAIN_INFO::CanMove() const noexcept
    {
        return Acceleration() > 7;
    }

    int TRAIN_INFO::HaveAmmo() const noexcept
    {
        return weapon > 0;
    }

    int TRAIN_INFO::IsDamaged() const noexcept
    {
        return hp < max_hp;
    }

    int TRAIN_INFO::NeedAmmo() const noexcept
    {
        return percentAmmo < 100;
    }


    int TRAIN_INFO::Acceleration() const noexcept
    {
        if (weight == 0.0f || std::isnan(weight))
            return 0;
        const long double scaled =
            (static_cast<long double>(power) / static_cast<long double>(weight)) * 8.0L;
        if (!std::isfinite(scaled) ||
            scaled >= 9223372036854775808.0L || scaled < -9223372036854775808.0L)
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
        return static_cast<int>(static_cast<std::uint32_t>(converted));
    }


    SPRITE* SPRITE::findCrossingConstraintOwner() noexcept
    {
        struct RawConstraintPath
        {
            core::R_DOT* node;
            int pad04;
            int edgeIndex08;
        };

        auto constraint = [](std::uint32_t rawValue) noexcept -> RawConstraintPath*
        {
            return reinterpret_cast<RawConstraintPath*>(static_cast<std::uintptr_t>(rawValue));
        };
        auto acceptableConstraintOwner = [this](RawConstraintPath* path) noexcept -> SPRITE*
        {
            SPRITE* owner = path->node->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;

            core::R_DOT* const target =
                path->node->links()[static_cast<std::size_t>(path->edgeIndex08)].target;
            owner = target->ownerSprite();
            if (owner && !owner->isInEngineChain(this))
                return owner;
            return nullptr;
        };

        core::R_DOT* const primary = primaryPathNodeRef();
        if (primary)
        {
            RawConstraintPath* const direct = constraint(
                primary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].crossingLinkToken);
            if (direct)
            {
                if (SPRITE* const owner = acceptableConstraintOwner(direct))
                    return owner;
            }
        }

        core::R_DOT* const primaryAgain = primaryPathNodeRef();
        if (!primaryAgain)
            return nullptr;

        const core::R_DOT::Link& current =
            primaryAgain->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
        if (!current.target)
            return nullptr;

        RawConstraintPath* const reciprocal = constraint(
            current.target->links()[static_cast<std::size_t>(current.reciprocalIndex)].crossingLinkToken);
        if (!reciprocal)
            return nullptr;
        return acceptableConstraintOwner(reciprocal);
    }


    SPRITE* SPRITE::resolvePathOwnerRelation(int* relationOut) noexcept
    {
        core::R_DOT* const primary = primaryPathNodeRef();
        SPRITE* owner = primary->ownerSprite();
        if (owner)
        {
            const int relation = static_cast<ENGINE*>(this)->IsTouch(static_cast<ENGINE*>(owner), 1);
            *relationOut = relation;
            if (relation != 0)
                return primary->ownerSprite();
        }

        core::R_DOT* next = nullptr;
        core::R_DOT* const currentPrimary = primaryPathNodeRef();
        if (currentPrimary)
            next = currentPrimary->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

        owner = next->ownerSprite();
        if (owner)
        {
            core::R_DOT* currentNext = nullptr;
            core::R_DOT* const source = primaryPathNodeRef();
            if (source)
                currentNext = source->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;

            const int relation = static_cast<ENGINE*>(this)->IsTouch(static_cast<ENGINE*>(currentNext->ownerSprite()), 1);
            *relationOut = relation;
            if (relation != 0)
            {
                core::R_DOT* const returnSource = primaryPathNodeRef();
                core::R_DOT* returnNode = nullptr;
                if (returnSource)
                    returnNode = returnSource->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].target;
                return returnNode->ownerSprite();
            }
        }

        *relationOut = 0;
        return nullptr;
    }


    int SPRITE::canLinkEngineChain(SPRITE* target) noexcept
    {
        if (!target)
            return 0;

        const int thisAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        const int targetAction = static_cast<int>(target->m_runtimeFlags & SPRITE::CommandBitsMask);

        if (target->isInEngineChain(Goal()) && thisAction == 0x68)
            return 1;

        if (isInEngineChain(target->Goal()) && targetAction == 0x68)
            return 1;

        if ((pushLineActiveRef() != 0 || target->pushLineActiveRef() != 0) &&
            (((target->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) == 0))
            return 1;

        return 0;
    }


    float SPRITE::resolveEngineChainCollision(SPRITE* target, int mode) noexcept
    {
        TRAIN_INFO thisRange(static_cast<ENGINE*>(this));
        TRAIN_INFO targetRange(static_cast<ENGINE*>(target));

        float sharedSpeed = 0.0f;
        float relativeSpeed = 0.0f;
        computeCollisionKinematics(
            m_speed, target->m_speed,
            thisRange.weight, targetRange.weight,
            mode, sharedSpeed, relativeSpeed);

        if (mode == 1 || mode == 3)
            static_cast<ENGINE*>(target)->ReverseTrain();

        for (SPRITE* node = target->engineChainHead(); node; node = node->engineChainNextRef())
            node->m_speed = (node->derivedStateValue(0) & 1) ? -sharedSpeed : sharedSpeed;

        float collisionLimit = 0.0f;
        const CONSTANT* const constants = g_baseConstants;
        std::memcpy(&collisionLimit, &constants->raw[24], sizeof(collisionLimit));

        const bool damageRoute =
            ((m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && target->isInEngineChain(Goal())) ||
            ((target->m_runtimeFlags & SPRITE::CommandBitsMask) == 108u && isInEngineChain(target->Goal()));
        if (relativeSpeed > collisionLimit && damageRoute)
        {
            ChangeAnimation(12);
            PlaySFX(16);

            const int damage = spriteMultiplyAndConvertToInt32(relativeSpeed, 1500.0f);
            int thisDamage = damage / 2;
            int targetDamage = damage / 2;

            VID* const targetVid = target->Vid();
            int targetActionValue = targetDamage;
            if (targetVid->nvid() == 97)
            {
                const int bucket = target->armyIndex();
                targetActionValue = targetVid->GetMaxHp(bucket) + 10;
            }
            target->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetActionValue, 0, 0);

            for (SPRITE* node = target->engineChainPreviousRef(); node; node = node->engineChainPreviousRef())
            {
                if (targetDamage < 2)
                    break;
                targetDamage /= 2;
                node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, targetDamage, 0, 0);
            }

            VID* const thisVid = Vid();
            int thisActionValue = thisDamage;
            if (thisVid->nvid() == 97)
            {
                const int bucket = armyIndex();
                thisActionValue = thisVid->GetMaxHp(bucket) + 10;
            }
            dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisActionValue, 0, 0);

            SPRITE* node = engineChainPreviousRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainPreviousRef();
                }
                return 0.0f;
            }

            node = engineChainNextRef();
            if (node)
            {
                while (node)
                {
                    if (thisDamage < 2)
                        return 0.0f;
                    thisDamage /= 2;
                    node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, thisDamage, 0, 0);
                    node = node->engineChainNextRef();
                }
                return 0.0f;
            }
        }
        else
        {
            PlaySFX(19);
        }
        return 0.0f;
    }


    int SPRITE::resolveEngineChainPathInteraction(core::R_POS* pathPair, float* distanceOut) noexcept
    {
        int relation = -1;
        int result = 0;
        SPRITE* resolved = resolvePathOwnerRelation(&relation);
        if (resolved)
        {
            primaryPathNodeRef() = pathPair->node;
            primaryPathProgressRef() = pathPair->progress;
            primaryPathAuxiliaryRef() = pathPair->auxiliary;
            primaryPathEdgeIndexRef() = pathPair->edgeIndex;

            if (canLinkEngineChain(resolved))
            {
                if (resolved->engineChainNextRef() || relation == 1 || relation == 4)
                    static_cast<ENGINE*>(resolved)->ReverseTrain();

                if (((m_runtimeFlags ^ resolved->m_runtimeFlags) & ArmyBitsMask) == 0)
                {
                    if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 0x68u &&
                        resolved->isInEngineChain(Goal()))
                    {
                        if ((resolved->m_runtimeFlags & SPRITE::CommandBitsMask) != 0x68u ||
                            !isInEngineChain(resolved->Goal()))
                        {
                            resolved->dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                        }
                    }
                    else
                    {
                        dispatchVirtualAction(ActionCode::ACT_BACKUP_COMMAND, 0, 0, 0);
                    }
                }

                if (resolved->engineChainNextRef())
                {
                    const int nvid = Vid() ? Vid()->nvid() : -1;
                    LOG::ResourceError("ENGINE %i", 10, "can't link", 0, nvid);
                }
                else
                {
                    resolved->engineChainNextRef() = this;
                    engineChainPreviousRef() = resolved;
                }

                static_cast<ENGINE*>(this)->SetCommandToTrain(0, 0, 0, 0);
                ChangeAnimation(0x0B);

                if (((resolved->m_runtimeFlags ^ m_runtimeFlags) & ArmyBitsMask) != 0)
                {
                    for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                    {
                        if ((node->m_runtimeFlags & ArmyBitsMask) != 0)
                            node->clearCommandsTargetingThisSprite();
                    }

                    SPRITE* callbackSprite = this;
                    if ((resolved->m_runtimeFlags & ArmyBitsMask) == (1u << ArmyBitsShift))
                        callbackSprite = resolved;
                    const int callbackArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(callbackSprite)));
                    (void)core::Application::callScriptFunction(core::EvFunctionNumber[21u], callbackArg, 0);
                    return 0;
                }
            }
            else
            {
                result = 1;
                *distanceOut = resolveEngineChainCollision(resolved, relation);
                const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
                const int resolvedArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolved)));
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[22u], selfArg, resolvedArg);
            }
            return result;
        }

        if (!findCrossingConstraintOwner())
            return result;

        if (*distanceOut > 0.2f)
            PlaySFX(0x94);

        primaryPathNodeRef() = pathPair->node;
        primaryPathProgressRef() = pathPair->progress;
        primaryPathAuxiliaryRef() = pathPair->auxiliary;
        primaryPathEdgeIndexRef() = pathPair->edgeIndex;
        *distanceOut = 0.0f;
        engineTargetSpeedRef() = -engineTargetSpeedRef();
        return 1;
    }


    void SPRITE::applyEngineChainPathMovement(core::R_POS* pathPair, float speed, int delay) noexcept
    {
        SPRITE* const root = this;
        for (SPRITE* node = this; node; node = node->engineChainNextRef())
        {
            static_cast<ENGINE*>(node)->ClearDotBusy();
            static_cast<ENGINE*>(node)->PullTail(pathPair);

            SPRITE* const next = node->engineChainNextRef();
            if (next)
            {
                pathPair->node = next->primaryPathNodeRef();
                pathPair->progress = next->primaryPathProgressRef();
                pathPair->auxiliary = next->primaryPathAuxiliaryRef();
                pathPair->edgeIndex = next->primaryPathEdgeIndexRef();

                core::R_DOT* const secondaryNode = node->secondaryPathNodeRef();
                const core::R_DOT::Link& secondaryEdge =
                    secondaryNode->links()[static_cast<std::size_t>(node->secondaryPathEdgeIndexRef())];
                next->primaryPathNodeRef() = secondaryEdge.target;
                next->primaryPathProgressRef() = static_cast<int>(secondaryEdge.length) - node->secondaryPathProgressRef();
                next->primaryPathAuxiliaryRef() = 0;
                next->primaryPathEdgeIndexRef() = secondaryEdge.reciprocalIndex;
            }

            node->updatePositionFromPathEndpoints();
            if ((!x87EqualOrUnordered(node->previousPathXRef(), 0.0f) ||
                 !x87EqualOrUnordered(node->previousPathYRef(), 0.0f)) &&
                x87AbsDiffGreaterOrdered(node->previousPathXRef(), node->m_xyz.x, 30.0f))
            {
                writeLogLine(g_fileLogger, kTrainCollapseBeginLog);
            }

            node->previousPathXRef() = node->m_xyz.x;
            node->previousPathYRef() = node->m_xyz.y;
            node->previousPathZRef() = node->m_xyz.z;
            static_cast<ENGINE*>(node)->SetDotBusy();

            node->m_speed = (node->derivedStateValue(0) & 1) ? -speed : speed;
            node->engineAccelerationDelayRef() = delay;
        }

        if (!x87EqualOrUnordered(speed, 0.0f))
        {
            VID* const rootVid = root->Vid();
            const float rootX = root->m_xyz.x;
            const float rootY = root->m_xyz.y;
            const float rootZ = root->m_xyz.z;
            const float minX = rootX - rootVid->halfSizeX();
            const float minY = rootY - rootVid->halfSizeY();
            const float maxX = rootX + rootVid->halfSizeX();
            const float maxY = rootY + rootVid->halfSizeY();

            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(minX, minY, maxX, maxY);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                if (candidate == root)
                    continue;

                if (candidate->m_currentAnimation >= 0x0F)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeX(), rootVid->halfSizeX(),
                        candidate->m_xyz.x, rootX))
                    continue;
                if (!x87SumGreaterThanAbsDiffOrdered(
                        candidateVid->halfSizeY(), rootVid->halfSizeY(),
                        candidate->m_xyz.y, rootY))
                    continue;
                if (x87SumLessOrUnordered(candidateVid->sizeZ(), candidate->m_xyz.z, rootZ))
                    continue;
                if (x87SumLessOrUnordered(rootZ, rootVid->sizeZ(), candidate->m_xyz.z))
                    continue;

                if ((candidateVid->properties() & P_CRUSH) != 0)
                    candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 5, 0, 0);
            }
        }
    }


    void SPRITE::clearCommandsTargetingThisSprite() noexcept
    {
        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& list = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();

        for (SPRITE* candidate = list.BeginIterate(cursor);
             candidate;
             candidate = list.NextIterate(cursor))
        {
            SPRITE* actionSprite = candidate;
            if (candidate->Goal() == this)
            {
                const int action = static_cast<int>(candidate->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (candidate->Vid()->spriteClassId() == 0x15u &&
                    (action == 0x70 || action == 0x74))
                {
                    static_cast<ENGINE*>(candidate)->SetCommandToTrain(0, 0, 0, 0);
                    continue;
                }

                if (action != 0x14 && action != 0x0C && action != 0x10)
                    continue;
            }
            else
            {
                actionSprite = candidate->childChain();
                if (!actionSprite || actionSprite->Goal() != this)
                    continue;

                const int action = static_cast<int>(actionSprite->m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action != 0x14 && action != 0x0C && action != 0x10 &&
                    action != 0x70 && action != 0x74)
                    continue;
            }

            actionSprite->SetCommand(0, nullptr);
        }
    }


    int SPRITE::createRouteMarkerSprites(core::R_DOT* pathNode) noexcept
    {
        BaseSpriteList<0>& list = g_spriteWorkList;
        createPathSpritesFromBuffer(pathNode, &list, 603);

        int result = list.activeCount();
        for (int i = 0; i < list.activeCount(); ++i)
        {
            SPRITE* const value = list.data()[i];
            const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
            switch (action)
            {
            case 112:
            case 116:
                value->ChangeArmy(1);
                break;
            case 108:
                value->ChangeArmy(3);
                break;
            case 104:
                value->ChangeArmy(2);
                break;
            default:
                break;
            }
            result = list.activeCount();
        }
        return result;
    }


    int SPRITE::createPathSpritesFromBuffer(core::R_DOT* pathNode, BaseSpriteList<0>* list, int nvid) noexcept
    {
        g_pathSearchResultScore = core::g_pathResultScore;
        g_pathSearchSecondaryBestCost = core::g_pathSecondaryBestCost;
        list->deleteAllSprites();

        int result = pathBufferSizeRef();
        for (int i = 0; i < pathBufferSizeRef(); ++i)
        {
            const int edgeIndex = static_cast<int>(pathBufferData()[static_cast<std::size_t>(i)]);
            if (edgeIndex < pathNode->linkCount())
            {
                pathNode = pathNode->links()[static_cast<std::size_t>(edgeIndex)].target;

                VID* createVid = nullptr;
                core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                if (nvid < 0 || nvid >= table.count() ||
                    (createVid = table.slot(nvid)) == nullptr)
                {
                    createVid = EmptyVid;
                }

                SPRITE* const created = mapOwner()->CreateSprite(
                    createVid,
                    VECTOR(static_cast<float>(pathNode->x()),
                           static_cast<float>(pathNode->y()),
                           static_cast<float>(pathNode->id())),
                    ANGLE(static_cast<unsigned char>(0)), nullptr, false, false);
                list->append(created);
            }
            result = pathBufferSizeRef();
        }
        return result;
    }


    int SPRITE::pathBufferReachesSecondaryTarget(core::R_DOT* pathNode) noexcept
    {
        SPRITE* const tail = engineChainTail();
        core::R_DOT* const secondaryNode = tail->secondaryPathNode();
        core::R_DOT* const secondaryTarget = secondaryNode
            ? secondaryNode->links()[static_cast<std::size_t>(tail->secondaryPathEdgeIndex())].target
            : nullptr;

        core::R_DOT* walker = pathNode;
        const int count = pathBufferSizeRef();
        for (int index = 0; index < count; ++index)
        {
            const unsigned int edgeIndex = pathBufferData()[static_cast<std::size_t>(index)];
            if (edgeIndex < static_cast<unsigned int>(walker->linkCount()))
            {
                walker = walker->links()[edgeIndex].target;
                if (walker == secondaryTarget)
                    return 1;
            }
        }
        return 0;
    }


    int SPRITE::evaluateEngineTargetRangeState() noexcept
    {
        SPRITE* const owner = Goal();
        if (!owner)
            return 2;

        const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
        if (action != 112 && action != 116)
            return 2;

        auto linkedChild = [](SPRITE* node) noexcept -> SPRITE*
        {
            SPRITE* const child = node->childChain();
            if (!child)
                return nullptr;
            VID* const childVid = child->Vid();
            VID* const nodeVid = node->Vid();
            if (childVid != nodeVid->linkedVid())
                return nullptr;
            if (childVid->hasWeaponChildDescriptor() == 0u || childVid->weaponCount() == 0u)
                return nullptr;
            return child;
        };

        if (SPRITE* const ref = engineCommandReferenceOwnerRef())
        {
            if (SPRITE* const child = linkedChild(ref))
            {
                VID* const childVid = child->Vid();


                const float dx = owner->m_xyz.x - child->m_xyz.x;
                const float dy = owner->m_xyz.y - child->m_xyz.y;
                return metricWithinFromRoundedDeltas(
                           dx, dy, childVid->weaponBattleRange()) ? 1 : 0;
            }

            VID* const refVid = ref->Vid();
            if (refVid->nvid() != 35)
                return 2;
            return metricWithinPositions(
                       owner->m_xyz.x, owner->m_xyz.y,
                       ref->m_xyz.x, ref->m_xyz.y,
                       refVid->weaponBattleRange()) ? 1 : 0;
        }

        int result = 2;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            if (SPRITE* const child = linkedChild(node))
            {
                VID* const childVid = child->Vid();


                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        child->m_xyz.x, child->m_xyz.y,
                        childVid->weaponBattleRange()))
                    return 0;
                result = 1;
                continue;
            }

            VID* const nodeVid = node->Vid();
            if (nodeVid->nvid() == 35)
            {
                if (!metricWithinPositions(
                        owner->m_xyz.x, owner->m_xyz.y,
                        node->m_xyz.x, node->m_xyz.y,
                        nodeVid->weaponBattleRange()))
                    return 0;
                result = 1;
            }
        }
        return result;
    }


    void ENGINE::MoveEngineTact() noexcept
    {
        core::R_POS pathSnapshot{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};

        auto liveNode = [this]() noexcept -> core::R_DOT* { return primaryPathNodeRef(); };
        auto liveIndex = [this]() noexcept -> int { return primaryPathEdgeIndexRef(); };

        if (!liveNode() || !secondaryPathNodeRef() || engineChainPreviousRef())
            return;

        VID* const vid = Vid();
        if (vid->nvid() != 85)
        {
            core::R_DOT* const node = liveNode();
            core::R_DOT::Link& edge =
                node->links()[static_cast<std::size_t>(liveIndex())];
            if (edge.target &&
                static_cast<int>(edge.target->routeClassTag()) - 4 ==
                    armyIndex() &&
                primaryPathProgressRef() > static_cast<int>(edge.length) / 2)
            {
                Stop();
                m_speed = 0.0f;
                static_cast<ENGINE*>(this)->ReverseTrain();
                return;
            }
        }

        int seenBit0 = 0;
        SPRITE* scan = this;
        while (scan)
        {
            core::R_DOT* const node = scan->primaryPathNodeRef();
            if (node &&
                node->selectedLinkIndex() == scan->primaryPathEdgeIndexRef() &&
                node->pushLineValue() != 0u)
            {
                for (SPRITE* mark = this; mark; mark = mark->engineChainNextRef())
                    mark->pushLineActiveRef() = 1;
                break;
            }

            bool reciprocalSelected = false;
            if (node)
            {
                core::R_DOT::Link& edge =
                    node->links()[static_cast<std::size_t>(scan->primaryPathEdgeIndexRef())];
                if (edge.target)
                    reciprocalSelected = edge.target->selectedLinkIndex() == edge.reciprocalIndex;
            }

            if (reciprocalSelected)
            {
                if (m_speed != 0.0f)
                {
                    const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if (action == 92)
                    {
                        bool resetAction = pathBufferSizeRef() == 0;
                        if (!resetAction)
                        {
                            const unsigned char bufferedIndex = pathBufferData()[0];
                            core::R_DOT* const bufferedTarget =
                                liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target;
                            core::R_DOT* const currentTarget =
                                liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                            resetAction = bufferedTarget == currentTarget;
                        }
                        if (resetAction)
                            SetCommandToTrain(0, 0, 0, 0);
                        else
                            m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    else
                    {
                        m_runtimeFlags &= ~MovementStartedFlag;
                    }
                    m_speed = 0.0f;
                    PlaySFX(148);
                }

                if (engineTargetSpeedRef() > 0.0f)
                    engineTargetSpeedRef() = -engineTargetSpeedRef();
                static_cast<ENGINE*>(this)->ReverseTrain();
                return;
            }

            scan->pushLineActiveRef() = 0;
            seenBit0 |= static_cast<int>(scan->m_runtimeFlags & 1u);
            scan = scan->engineChainNextRef();
        }

        if (!scan && seenBit0 != 0)
        {
            for (SPRITE* node = this; node; node = node->engineChainNextRef())
            {
                if ((node->m_runtimeFlags & 1u) == 0)
                    continue;
                node->m_runtimeFlags &= ~1u;

                if (node->Vid()->weaponFloatAt(16) != 0.0f)
                {


                    void* const player = *reinterpret_cast<void**>(
                        reinterpret_cast<unsigned char*>(Map) +
                        core::application_layout::PlayerSlots +
                        static_cast<std::size_t>(armyIndex() & 3) * sizeof(std::uint32_t));
                    using AddUnitToStateBarFn = void (__thiscall*)(void*, SPRITE*);
                    void** const playerVtable = *reinterpret_cast<void***>(player);
                    reinterpret_cast<AddUnitToStateBarFn>(playerVtable[10])(player, node);
                }

                ENGINE* const engineNode = static_cast<ENGINE*>(node);
                if (engineNode->productionBatchCompletionPending() != 0)
                {
                    const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(node));
                    (void)core::Application::callScriptFunction(
                        core::EvFunctionNumber[4u], spriteArg, 0);
                    engineNode->setProductionBatchCompletionPending(0);
                }
            }
        }

        if (pushLineActiveRef() == 0)
        {
            if ((m_runtimeFlags & MovementStartedFlag) == 0 && m_speed == 0.0f)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92 || action == 104 || action == 108 || action == 100)
                    StartMove();
                if (action == 96)
                {
                    SPRITE* node = engineChainHead();
                    while (node &&
                           (node->Vid()->nvid() != 85 || node->routeActionReadyRef() != 0))
                        node = node->engineChainNextRef();
                    if (node)
                        StartMove();
                }
            }

            if ((m_runtimeFlags & MovementStartedFlag) == 0)
            {
                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if ((action == 112 || action == 116) && (std::rand() % 5) == 0 && evaluateEngineTargetRangeState() != 1)
                    StartMove();
            }

            if (engineTargetSpeedRef() == 0.0f && (m_runtimeFlags & MovementStartedFlag) != 0)
            {
                static_cast<ENGINE*>(this)->ReCalcMoveParameters();
                if (engineTargetSpeedRef() == 0.0f && m_speed == 0.0f)
                    SetCommandToTrain(0, 0, 0, 0);
            }
        }

        SPRITE* const routeTarget = engineCommandArgument0Ref() != 0 ? nullptr : Goal();
        float speed = std::fabs(m_speed);
        approachEngineTargetSpeed(&speed);
        if (speed < 0.0f)
        {
            m_speed = 0.0f;
            static_cast<ENGINE*>(this)->ReverseTrain();
            static_cast<ENGINE*>(this)->ReCalcMoveParameters();
            return;
        }

        core::R_DOT::Link& currentEdge =
            liveNode()->links()[static_cast<std::size_t>(liveIndex())];

        CONSTANT* const constants = g_baseConstants;
        float speedLimit = 0.0f;
        std::memcpy(&speedLimit, &constants->raw[6], sizeof(speedLimit));
        if (liveNode()->pathEventFlag() != 0 && speed > speedLimit)
        {
            if (Vid()->nvid() != 85)
                speed = speedLimit;

            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            VID* effectVid = nullptr;
            if (table.count() > 588)
                effectVid = table.slot(588);
            if (!effectVid)
                effectVid = EmptyVid;
            mapOwner()->CreateSprite(effectVid, m_xyz, ANGLE(static_cast<unsigned char>(0)), this, false, false);
        }

        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int64_t deltaFixed = static_cast<std::int64_t>(
            static_cast<double>(static_cast<std::int32_t>(deltaMs)) *
            static_cast<double>(speed) * 64000.0);
        const std::int64_t accumulated = deltaFixed + static_cast<std::int64_t>(primaryPathAuxiliaryRef());
        primaryPathAuxiliaryRef() = static_cast<int>(accumulated);
        if (primaryPathAuxiliaryRef() < 0)
        {
            primaryPathAuxiliaryRef() = 0;
        }
        else if (primaryPathAuxiliaryRef() > 65535)
        {
            primaryPathProgressRef() += primaryPathAuxiliaryRef() >> 16;
            primaryPathAuxiliaryRef() &= 65535;
        }

        if (primaryPathProgressRef() > static_cast<int>(currentEdge.length))
        {
            if (liveNode()->pathEventFlag() != 0)
            {
                const float minX = static_cast<float>(liveNode()->x() - 100);
                const float minY = static_cast<float>(liveNode()->y() - 60);
                const float maxX = static_cast<float>(liveNode()->x() + 100);
                const float maxY = static_cast<float>(liveNode()->y() + 60);
                SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
                for (SPRITE* candidate = hash->FirstHashInBox(minX, minY, maxX, maxY);
                     candidate;
                     candidate = hash->NextHashInBox())
                {
                    if (candidate->Vid()->spriteClassId() == 22u)
                        static_cast<RAIL*>(candidate)->handleRailNodeReleased(reinterpret_cast<std::uintptr_t>(liveNode()));
                }
                liveNode()->setPathEventFlag(0);
            }

            core::R_DOT* const targetNode = currentEdge.target;
            if (targetNode->linkCount() < 2)
            {
                speed = 0.0f;
                primaryPathProgressRef() = currentEdge.length;

                const int action = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (action == 92)
                {
                    if (pathBufferSizeRef() != 0)
                    {
                        const unsigned char bufferedIndex = pathBufferData()[0];
                        const bool sameTarget =
                            liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == targetNode;
                        if (!sameTarget)
                            Stop();
                        else
                            SetCommandToTrain(0, 0, 0, 0);
                    }
                    else
                    {
                        SetCommandToTrain(0, 0, 0, 0);
                    }
                }
                else
                {
                    Stop();
                }
            }
            else
            {
                core::R_POS livePath{
                    primaryPathNodeRef(), primaryPathProgressRef(),
                    primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
                const int routeResult =
                    livePath.DoStep(engineCommandArgument0Node(), routeTarget, this);
                primaryPathNodeRef() = livePath.node;
                primaryPathProgressRef() = livePath.progress;
                primaryPathAuxiliaryRef() = livePath.auxiliary;
                primaryPathEdgeIndexRef() = livePath.edgeIndex;

                SPRITE* const controlled =
                    mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (isInEngineChain(controlled) && (controlled->m_runtimeFlags & ArmyBitsMask) == 0)
                    createRouteMarkerSprites(liveNode());

                const int actionBeforeOwnerCheck = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (actionBeforeOwnerCheck != 108)
                {
                    SPRITE* const nodeOwner = liveNode()->ownerSprite();
                    if (nodeOwner)
                        nodeOwner->isInEngineChain(Goal());
                }

                core::R_DOT* const currentTarget =
                    liveNode()->links()[static_cast<std::size_t>(liveIndex())].target;
                SPRITE* const currentTargetOwner = currentTarget->ownerSprite();
                if (!currentTargetOwner || currentTargetOwner->isInEngineChain(Goal()))
                {
                    const int actionBeforeSign = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                    if ((actionBeforeSign == 108 || routeResult != 0) && routeResult < 0 && engineTargetSpeedRef() > 0.0f)
                        engineTargetSpeedRef() = -engineTargetSpeedRef();
                }

                if (Vid()->nvid() != 85 && currentTarget &&
                    static_cast<int>(currentTarget->routeClassTag()) - 4 ==
                        armyIndex())
                {
                    speed *= 0.5f;
                    Stop();
                }

                core::R_DOT* const actionTarget = engineCommandArgument0Node();
                const bool primaryRoute =
                    (actionTarget && actionTarget == currentTarget) ||
                    routeResult == 0 || currentTarget == core::g_pathBestNode;

                const int finalAction = static_cast<int>(m_runtimeFlags & SPRITE::CommandBitsMask);
                if (primaryRoute)
                {
                    if (finalAction == 96)
                    {
                        for (SPRITE* node = this; node; node = node->engineChainNextRef())
                        {
                            if (node->Vid()->nvid() == 85)
                            {
                                node->routeActionReadyRef() = 1;
                                Stop();
                            }
                        }
                        if ((m_runtimeFlags & MovementStartedFlag) != 0)
                            SetCommandToTrain(0, 0, 0, 0);
                    }
                    else if (finalAction == 92)
                    {
                        if (Vid()->nvid() != 85 || !liveNode() || !currentTarget ||
                            static_cast<int>(currentTarget->routeClassTag()) - 4 !=
                                armyIndex())
                        {
                            SetCommandToTrain(0, 0, 0, 0);
                            const int selfArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this));
                            (void)core::Application::callScriptFunction(
                                core::EvFunctionNumber[8u], selfArg, 0);
                        }
                    }
                    else if (finalAction == 100)
                    {
                        Stop();
                        StartMove();
                    }
                    else if ((finalAction == 112 || finalAction == 116) && evaluateEngineTargetRangeState() == 1)
                    {
                        Stop();
                    }
                }
                else
                {
                    if (finalAction == 112 || finalAction == 116)
                    {
                        if (evaluateEngineTargetRangeState() == 1)
                            Stop();
                    }
                    else if (liveNode()->linkCount() < 2 ||
                             currentTarget->linkCount() < 2)
                    {
                        if (finalAction == 92)
                        {
                            if (pathBufferSizeRef() != 0)
                            {
                                const unsigned char bufferedIndex = pathBufferData()[0];
                                if (liveNode()->links()[static_cast<std::size_t>(bufferedIndex)].target == currentTarget)
                                    SetCommandToTrain(0, 0, 0, 0);
                                else
                                    Stop();
                            }
                            else
                            {
                                SetCommandToTrain(0, 0, 0, 0);
                            }
                        }
                        else
                        {
                            Stop();
                        }
                    }
                    else if (currentTarget->ownerSprite() &&
                             !currentTarget->ownerSprite()->isInEngineChain(Goal()))
                    {
                        Stop();
                    }
                }
            }
        }

        resolveEngineChainPathInteraction(&pathSnapshot, &speed);
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
            static_cast<ENGINE*>(node)->ClearDotBusy();
        applyEngineChainPathMovement(&pathSnapshot, speed, engineAccelerationDelayRef());
    }


    void SPRITE::initializeEnginePathEndpoints() noexcept
    {
        VID* const vid = Vid();
        const float radius = vid->weaponRadius() * 0.5f;
        const int baseDirection = directionIndex();

        core::R_DOT* const seed =
            core::g_rMap.GetNearestDot(
                             spriteConvertFloatToInt32(static_cast<long double>(m_xyz.x)),
                             spriteConvertFloatToInt32(static_cast<long double>(m_xyz.y)),
                             spriteConvertFloatToInt32(static_cast<long double>(m_xyz.z)));
        if (!seed || seed->linkCount() == 0)
            return;

        int facing = 0;
        int edgeIndex = seed->linkCount() - 1;
        while (edgeIndex >= 0)
        {
            const core::R_DOT::Link& entry =
                seed->links()[static_cast<std::size_t>(edgeIndex)];
            const unsigned char edgeFacing = static_cast<unsigned char>(entry.facing);
            const unsigned char deltaA =
                static_cast<unsigned char>(baseDirection - edgeFacing);
            const unsigned char deltaB =
                static_cast<unsigned char>(edgeFacing - baseDirection);
            const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;

            if (delta > 108 || delta < 20)
                break;
            --edgeIndex;
        }

        if (edgeIndex >= 0)
            facing = baseDirection;
        else
            facing = seed->firstLinkFacing();

        auto loadPair = [](core::R_DOT* node, int progress, int pad, int index) noexcept
        {
            core::R_POS pair;
            pair.node = node;
            pair.progress = progress;
            pair.auxiliary = pad;
            pair.edgeIndex = index;
            return pair;
        };

        auto storePrimary = [this](const core::R_POS& pair) noexcept
        {
            primaryPathNodeRef() = pair.node;
            primaryPathProgressRef() = pair.progress;
            primaryPathAuxiliaryRef() = pair.auxiliary;
            primaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto storeSecondary = [this](const core::R_POS& pair) noexcept
        {
            secondaryPathNodeRef() = pair.node;
            secondaryPathProgressRef() = pair.progress;
            secondaryPathAuxiliaryRef() = pair.auxiliary;
            secondaryPathEdgeIndexRef() = pair.edgeIndex;
        };

        auto repairToCloserTarget = [this](core::R_POS& pair) noexcept
        {
            core::R_DOT* const node = pair.node;
            const core::R_DOT::Link& entry =
                node->links()[static_cast<std::size_t>(pair.edgeIndex)];
            core::R_DOT* const target = entry.target;

            const double nodeDx = static_cast<double>(node->x()) - m_xyz.x;
            const double nodeDy = static_cast<double>(node->y()) - m_xyz.y;
            const double nodeDz = static_cast<double>(node->id()) - m_xyz.z;
            const double targetDx = static_cast<double>(target->x()) - m_xyz.x;
            const double targetDy = static_cast<double>(target->y()) - m_xyz.y;
            const double targetDz = static_cast<double>(target->id()) - m_xyz.z;

            const double nodeDistance = std::sqrt(nodeDx * nodeDx + nodeDy * nodeDy + nodeDz * nodeDz);
            const double targetDistance = std::sqrt(targetDx * targetDx + targetDy * targetDy + targetDz * targetDz);

            if (targetDistance < nodeDistance ||
                std::isnan(targetDistance) || std::isnan(nodeDistance))
            {
                pair.progress = static_cast<int>(entry.length) - pair.progress;
                pair.node = target;
                pair.edgeIndex = entry.reciprocalIndex;
            }
        };

        core::R_POS primary =
            loadPair(primaryPathNodeRef(),
                     primaryPathProgressRef(),
                     primaryPathAuxiliaryRef(),
                     primaryPathEdgeIndexRef());

        seed->SetNearestPos(
                         spriteConvertFloatToInt32(static_cast<long double>(radius) *
                                             directionSin(facing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteConvertFloatToInt32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(facing)),
                         spriteConvertFloatToInt32(static_cast<long double>(m_xyz.z)),
                         &primary);
        repairToCloserTarget(primary);
        storePrimary(primary);

        const int reverseFacing = static_cast<unsigned char>(facing - 128);
        core::R_POS secondary =
            loadPair(secondaryPathNodeRef(),
                     secondaryPathProgressRef(),
                     secondaryPathAuxiliaryRef(),
                     secondaryPathEdgeIndexRef());

        seed->SetNearestPos(
                         spriteConvertFloatToInt32(static_cast<long double>(radius) *
                                             directionSin(reverseFacing) +
                                         static_cast<long double>(m_xyz.x)),
                         spriteConvertFloatToInt32(static_cast<long double>(m_xyz.y) -
                                         static_cast<long double>(radius) *
                                             directionCos(reverseFacing)),
                         spriteConvertFloatToInt32(static_cast<long double>(m_xyz.z)),
                         &secondary);
        repairToCloserTarget(secondary);
        storeSecondary(secondary);

        core::R_POS primaryForBCF0{
            primaryPathNodeRef(),
            primaryPathProgressRef(),
            primaryPathAuxiliaryRef(),
            primaryPathEdgeIndexRef()};
        static_cast<ENGINE*>(this)->PullTail(&primaryForBCF0);
        updatePositionFromPathEndpoints();
        SPRITE* const owner = static_cast<ENGINE*>(this)->GetIntersecting();
        attachEngineChain(owner);
        static_cast<ENGINE*>(this)->SetDotBusy();
    }


    void SPRITE::updatePositionFromPathEndpoints() noexcept
    {
        core::R_DOT* const firstNode = primaryPathNodeRef();
        core::R_DOT* firstTarget = nullptr;
        int firstDuration = 0;
        if (firstNode)
        {
            const core::R_DOT::Link& firstEdge =
                firstNode->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())];
            firstTarget = firstEdge.target;
            firstDuration = static_cast<int>(firstEdge.length);
        }

        const int firstProgress = primaryPathProgressRef();
        const int firstTargetX = firstTarget->x();
        const int firstNodeX = firstNode->x();
        const float firstX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetX - firstNodeX,
                                          firstDuration),
            firstNodeX);

        const int firstTargetY = firstTarget->y();
        const int firstNodeY = firstNode->y();
        const float firstY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetY - firstNodeY,
                                          firstDuration),
            firstNodeY);

        const int firstTargetZ = firstTarget->id();
        const int firstNodeZ = firstNode->id();
        const float firstZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(firstProgress,
                                          firstTargetZ - firstNodeZ,
                                          firstDuration),
            firstNodeZ);

        core::R_DOT* const secondNode = secondaryPathNodeRef();
        if (secondaryPathEdgeIndexRef() >= secondNode->linkCount())
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingLinkResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            secondaryPathEdgeIndexRef() = secondNode->linkCount() - 1;
        }

        const core::R_DOT::Link& secondEdge =
            secondNode->links()[static_cast<std::size_t>(secondaryPathEdgeIndexRef())];
        core::R_DOT* const secondTarget = secondEdge.target;
        if (!secondTarget)
        {
            LOG::ResourceError("ENGINE %i", 10, kMissingTailDot2ResourceError, 0,
                               Vid() ? Vid()->nvid() : -1);
            return;
        }

        const int secondDuration = static_cast<int>(secondEdge.length);
        const int secondProgress = secondaryPathProgressRef();
        const int secondNodeX = secondNode->x();
        const int secondNodeY = secondNode->y();
        const int secondNodeZ = secondNode->id();

        const float secondX = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->x() - secondNodeX,
                                          secondDuration),
            secondNodeX);
        const float secondY = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->y() - secondNodeY,
                                          secondDuration),
            secondNodeY);
        const float secondZ = pathInterpolateCoordinate(
            pathScaledProgressQuotient(secondProgress,
                                          secondTarget->id() - secondNodeZ,
                                          secondDuration),
            secondNodeZ);

        ChangeCoor(pathAverageCoordinate(secondX, firstX),
                   pathAverageCoordinate(secondY, firstY),
                   pathAverageCoordinate(secondZ, firstZ));

        const bool reverseDirection = (derivedStateValue(0) & 1) != 0;
        const int directionY = pathDirectionDeltaYToInt(
            reverseDirection ? secondY : firstY,
            reverseDirection ? firstY : secondY);
        const int directionX = pathDirectionDeltaXToInt(
            reverseDirection ? secondX : firstX,
            reverseDirection ? firstX : secondX);
        ChangeDirection(Decart2Polar(directionX, directionY, nullptr));
    }


    void SPRITE::splitEngineChainAtPosition(float x, float y) noexcept
    {
        SPRITE* const first = engineChainHead();
        SPRITE* const last = engineChainTail();
        PlaySFX(15);

        bool preferFirst = false;
        if (engineChainPreviousRef() && engineChainNextRef())
        {
            preferFirst = preferFirstTrainEndpoint(
                x, y,
                engineChainPreviousRef()->X(), engineChainPreviousRef()->Y(),
                engineChainNextRef()->X(), engineChainNextRef()->Y());
        }

        if (!engineChainNextRef() || preferFirst)
        {
            if (engineChainPreviousRef())
            {
                engineChainPreviousRef()->engineChainNextRef() = nullptr;
                engineChainPreviousRef() = nullptr;
            }
        }
        else
        {
            engineChainNextRef()->engineChainPreviousRef() = nullptr;
            engineChainNextRef() = nullptr;
        }

        if (last != first)
        {
            static_cast<ENGINE*>(last)->Stop();
            if (x87IsZeroOrUnordered(last->m_speed))
            {
                static_cast<ENGINE*>(last)->ReverseTrain();
                SPRITE* const head = last->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }

            if (x87IsZeroOrUnordered(first->m_speed))
            {
                SPRITE* const head = first->engineChainHead();
                head->m_speed = (static_cast<std::uint32_t>(head->derivedStateValue(0)) & 1u) != 0u ? -0.01f : 0.01f;
            }
            else
            {
                static_cast<ENGINE*>(first)->ReCalcMoveParameters();
            }
        }
    }

    int SPRITE::scaledEngineChainLength() noexcept
    {
        int count = 0;
        SPRITE* walker = engineChainHead();
        while (walker)
        {
            walker = walker->engineChainNextRef();
            ++count;
        }

        const long double value =
            static_cast<long double>(count) * static_cast<long double>(1.33f) +
            static_cast<long double>(0.5f);
        return spriteConvertFloatToInt32(value);
    }


    void ENGINE::ReCalcMoveParameters() noexcept
    {
        ENGINE* head = this;
        if (head->engineChainPreviousRef())
        {
            do
            {
                head = static_cast<ENGINE*>(head->engineChainHead());
            }
            while (head->engineChainPreviousRef());
        }

        TRAIN_INFO range(head);

        if (x87EqualOrUnordered(head->engineTargetSpeedRef(), 0.0f) &&
            range.CanMove())
        {
            const std::uint32_t flags = head->m_runtimeFlags;
            if ((flags & MovementStartedFlag) != 0u && ENGINE::globaldeleting == 0u)
            {
                core::R_DOT* const routeNode = head->engineCommandArgument0Node();
                SPRITE* const routeSprite = routeNode ? nullptr : head->Goal();

                core::R_POS path{
                    head->primaryPathNodeRef(),
                    head->primaryPathProgressRef(),
                    head->primaryPathAuxiliaryRef(),
                    head->primaryPathEdgeIndexRef()};
                const int projection = path.NoStepToTarget(
                    routeNode,
                    routeSprite,
                    static_cast<unsigned int>((flags >> CommandBitsShift) & CommandValueMask),
                    head);

                SPRITE* const controlled =
                    head->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                if (head->isInEngineChain(controlled))
                {
                    SPRITE* const controlledAgain =
                        head->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
                    if ((controlledAgain->m_runtimeFlags & ArmyBitsMask) == 0u)
                    {
                        core::R_DOT* pathTarget = nullptr;
                        if (head->primaryPathNodeRef())
                        {
                            pathTarget = head->primaryPathNodeRef()->links()
                                [static_cast<std::size_t>(head->primaryPathEdgeIndexRef())].target;
                        }
                        head->createRouteMarkerSprites(pathTarget);
                    }
                }

                head->engineTargetSpeedRef() = projection >= 0 ? 0.001f : -0.001f;
            }
        }

        if (x87LessOrUnordered(head->engineTargetSpeedRef(), 0.0f))
        {
            const int negativeDelay = static_cast<int>(0u - static_cast<std::uint32_t>(range.speed));
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(negativeDelay, 0.001f);
            return;
        }

        if (x87OrderedGreater(head->engineTargetSpeedRef(), 0.0f))
        {
            head->engineTargetSpeedRef() = spriteFildMulStoreFloat(range.speed, 0.001f);
            const int delay = range.Acceleration();
            head->engineAccelerationDelayRef() = delay;
            if (delay == 0)
                head->m_runtimeFlags &= ~MovementStartedFlag;
        }
    }


    void SPRITE::approachEngineTargetSpeed(float* speedOut) noexcept
    {
        constexpr float immediateSpeed = 0.03500000014901161f;
        constexpr float tickScale = 0.000001f;

        if (pushLineActiveRef() != 0 &&
            x87EqualOrUnordered(engineTargetSpeedRef(), 0.0f))
        {
            *speedOut = immediateSpeed;
            engineAccelerationDelayRef() = 0;
            return;
        }

        const float target = engineTargetSpeedRef();


        if (!x87LessEqualOrUnordered(target, *speedOut))
        {
            if (engineAccelerationDelayRef() == 0)
                static_cast<ENGINE*>(this)->ReCalcMoveParameters();

            const int delay = engineAccelerationDelayRef();
            if (delay != 0)
            {
                const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const std::uint32_t product =
                    static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
                const double step = static_cast<double>(product) * static_cast<double>(tickScale);
                *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);
            }

            if (*speedOut >= target)
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        if (x87LessOrUnordered(target, *speedOut))
        {
            const int delay = spriteConvertFloatToInt32(
                (static_cast<long double>(*speedOut) * 1000.0L + 10.0L) * -0.5L);
            engineAccelerationDelayRef() = delay;

            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const std::uint32_t product =
                static_cast<std::uint32_t>(delta) * static_cast<std::uint32_t>(delay);
            const std::int32_t signedProduct = static_cast<std::int32_t>(product);
            const double step = static_cast<double>(signedProduct) * static_cast<double>(tickScale);
            *speedOut = static_cast<float>(static_cast<double>(*speedOut) + step + step);

            if (x87LessEqualOrUnordered(*speedOut, target))
            {
                *speedOut = target;
                engineAccelerationDelayRef() = 0;
            }
            return;
        }

        engineAccelerationDelayRef() = 0;
    }


    void ENGINE::Stop() noexcept
    {
        SPRITE* node = this;
        for (;;)
        {
            SPRITE* const controlledPlayer =
                node->mapOwner()->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
            if (node->isInEngineChain(controlledPlayer))
                g_spriteWorkList.deleteAllSprites();

            if (!node->engineChainPreviousRef())
                break;
            node = node->engineChainHead();
        }

        const DWORD flags = node->m_runtimeFlags;
        if ((flags & CommandBitsMask) == 0x64u)
        {
            if ((flags & MovementStartedFlag) != 0u)
            {
                node->engineCommandArgument0Ref() = node->engineCommandArgument1Ref();
                node->m_runtimeFlags = flags | MovementStartedFlag;
                node->engineCommandArgument1Ref() = node->engineCommandArgument2Ref();
                node->engineCommandArgument2Ref() = node->engineCommandArgument0Ref();
                node->engineTargetSpeedRef() = 0.0f;
                node->engineAccelerationDelayRef() = 0;

                for (SPRITE* child = node->engineChainNextRef();
                     child;
                     child = child->engineChainNextRef())
                {
                    child->engineCommandArgument0Ref() = node->engineCommandArgument0Ref();
                    child->engineCommandArgument1Ref() = node->engineCommandArgument1Ref();
                    child->engineCommandArgument2Ref() = node->engineCommandArgument2Ref();
                    child->m_runtimeFlags |= MovementStartedFlag;
                    child->engineTargetSpeedRef() = 0.0f;
                    child->engineAccelerationDelayRef() = 0;
                }
            }
            return;
        }

        for (SPRITE* iter = node; iter; iter = iter->engineChainNextRef())
        {
            iter->engineTargetSpeedRef() = 0.0f;
            iter->m_runtimeFlags &= ~MovementStartedFlag;

            if (!x87EqualOrUnordered(node->Speed(), 0.0f))
                iter->engineAccelerationDelayRef() = animationDelayFromSpeed(node->Speed());
        }
    }


    void ENGINE::SetCommandToTrain(int command, int x, int y) noexcept
    {
        core::R_DOT* const node =
            core::g_rMap.GetNearestDot(x, y);
        SetCommandToTrain(command, 0,
            static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node))), 0);
    }


    void ENGINE::SetCommandToTrain(int opcode, int argument1, int argument2, int argument3) noexcept
    {
        int action = opcode;
        SPRITE* const owner = this;
        int forcedTerminalZero = 0;

        if (action == 0x1E)
        {
            action = 0;
            forcedTerminalZero = 1;
        }

        SPRITE* target = reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(argument1));
        int actionArgument2 = argument2;
        const int actionArgument3 = argument3;

        if (!target && actionArgument2 == 0 && action != 0x1D)
        {
            action = 0;
        }
        else if (action == 0x18)
        {
            SPRITE* scan = engineChainHead();
            while (scan)
            {
                if (scan->m_vid->nvid() == 85 && scan->ammoFixedPoint() / 64 > 0)
                    break;
                scan = scan->engineChainNextRef();
            }
            if (!scan)
            {
                action = 0;
                target = nullptr;
                actionArgument2 = 0;
            }
        }

        core::R_DOT* resolvedB4 = nullptr;
        int resolvedB8 = 0;
        if (action == 0x19)
        {
            resolvedB4 = actionArgument3
                ? reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(actionArgument3)))
                : core::g_rMap.GetNearestDot(
                                   spriteConvertFloatToInt32(static_cast<long double>(m_xyz.x)),
                                   spriteConvertFloatToInt32(static_cast<long double>(m_xyz.y)),
                                   spriteConvertFloatToInt32(static_cast<long double>(m_xyz.z)));
            resolvedB8 = actionArgument2;
        }

        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
        {
            SPRITE* const refOwner = node->engineCommandReferenceOwnerRef();
            if (refOwner)
            {
                const int nextRef = refOwner->m_listReferenceCount - 1;
                refOwner->m_listReferenceCount = nextRef;
                if (nextRef < 0)
                {
                    const int nvid = refOwner->m_vid ? refOwner->m_vid->nvid() : -1;
                    LOG::ResourceError("SPRITE %i", 4, "noRef at Release", nextRef, nvid);
                }
                else if (nextRef == 0)
                {
                    DeleteSpriteThroughVirtualDeletingDestructor(refOwner);
                }
                node->engineCommandReferenceOwnerRef() = nullptr;
            }

            if ((action == 0x1C || action == 0x1D) &&
                owner->Vid()->nvid() != 45 &&
                owner->Vid()->weaponFloatAt(0x10) == 0.0f)
            {
                node->engineCommandReferenceOwnerRef() = owner;
                ++owner->m_listReferenceCount;
            }

            node->SetCommandWithoutLink(action, target);
            node->engineCommandArgument1Ref() = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(resolvedB4)));
            node->engineCommandArgument0Ref() = actionArgument2;
            node->engineCommandArgument2Ref() = resolvedB8;
            node->engineTargetSpeedRef() = 0.0f;
            node->routeActionStartTimeRef() = 0;
            node->routeActionReadyRef() = 0;

            SPRITE* const child = node->m_childChain;
            if (!child)
                continue;

            VID* const linkVid = node->m_vid->linkedVid();
            if (child->m_vid != linkVid ||
                child->m_vid->hasWeaponChildDescriptor() == 0u ||
                child->m_vid->weaponCount() == 0u ||
                forcedTerminalZero != 0)
            {
                continue;
            }

            if (owner->Vid()->nvid() != 45 &&
                owner->Vid()->weaponFloatAt(0x10) == 0.0f &&
                node != owner)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (owner->canWeaponAffectTarget(target) == 0)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            if (action == 0x1C)
            {
                child->SetCommandWithoutLink(3, target);
                continue;
            }

            if (action != 0x1D)
            {
                child->SetCommandWithoutLink(0, nullptr);
                continue;
            }

            VID* weaponOwner = node->m_vid;
            if (child && child->m_vid == node->m_vid->linkedVid() &&
                child->m_vid->hasWeaponChildDescriptor() != 0u &&
                child->m_vid->weaponCount() != 0u)
            {
                weaponOwner = child->m_vid;
            }

            if (weaponOwner->weaponTypeMask() == 8 && target)
            {
                SPRITE* helper = new (std::nothrow) SPRITE(
                    mapOwner(),
                    EmptyVid,
                    VECTOR(target->m_xyz.x, target->m_xyz.y + 70.0f, target->m_xyz.z + 70.0f),
                    ANGLE(static_cast<unsigned char>(0)),
                    nullptr);
                child->SetCommandWithoutLink(4, helper);
            }
            else
            {
                child->SetCommandWithoutLink(4, target);
            }
        }

        if (action == 0x17 || action == 0x1A || action == 0x1B || action == 0x19)
        {
            engineChainHead()->StartMove();
            return;
        }

        if (action == 0x18)
        {
            const std::uint32_t c8Low = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(primaryPathNodeRef()));
            if (c8Low == static_cast<std::uint32_t>(engineCommandArgument0Ref()))
            {
                static_cast<ENGINE*>(engineChainHead())->Stop();
                for (SPRITE* node = engineChainHead(); node; node = node->engineChainNextRef())
                {
                    if (node->m_vid->nvid() == 85 && ammoFixedPoint() / 64 != 0)
                        node->routeActionReadyRef() = 1;
                }
                return;
            }

            engineChainHead()->StartMove();
            return;
        }

        if (action == 0)
            static_cast<ENGINE*>(engineChainHead())->Stop();
    }

    int SPRITE::inheritAdjacentEngineCommand() noexcept
    {
        SPRITE* const next = engineChainNextRef();
        if (next && (next->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
            return SetCommand(next->commandIndex(), next->m_goalSprite);

        SPRITE* const previous = engineChainPreviousRef();
        if (previous && (previous->m_runtimeFlags & SPRITE::CommandBitsMask) != 0u)
        {
            return SetCommand(next->commandIndex(),
                              next->m_goalSprite);
        }

        return SetCommand(0, nullptr);
    }


    void SPRITE::CreateChild()
    {
        VID* childVid = m_vid->childVid[m_currentAnimation];
        const std::int32_t rawChildCount = static_cast<std::int32_t>(m_vid->noChild[m_currentAnimation]);
        const std::int32_t sign = rawChildCount < 0 ? -1 : 0;
        int childCount = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(rawChildCount) ^ static_cast<std::uint32_t>(sign)) -
            static_cast<std::uint32_t>(sign));


        if (!childVid)
            return;
        if (childVid->isNotCreateAsChild())
            return;

        MAP* const map = mapOwner();

        const int steppedDirection =
            (childVid->properties() & P_NOTCHANGELINKERCOOR) != 0u
                ? 0
                : m_vid->SteppedDirection(m_direction).Int();

        int startOrdinal = 0;
        const WEAPON* const weaponRecord = m_vid->weaponRecord();
        if (childCount == 2 &&
            (*reinterpret_cast<const std::int32_t*>(weaponRecord->raw.data() + 4) & 0x10) != 0)
        {
            DWORD nextFlags = m_runtimeFlags;
            if ((nextFlags & ChildSpawnToggleFlag) != 0)
            {
                nextFlags &= ~0x00000200u;
                startOrdinal = 1;
            }
            else
                childCount = 1;
            nextFlags ^= ChildSpawnToggleFlag;
            m_runtimeFlags = nextFlags;
        }

        for (int ordinal = startOrdinal; ordinal < childCount; ++ordinal)
        {
            VECTOR offset{};
            float projectileBaseX = 0.0f;
            float projectileBaseY = 0.0f;
            const float childX = m_vid->childX[m_currentAnimation];
            const float childY = m_vid->childY[m_currentAnimation];
            const float primarySin = directionSin(steppedDirection);
            const float primaryCos = directionCos(steppedDirection);
            const float auxiliarySin = directionSinAux(steppedDirection);
            const float auxiliaryCos = directionCosAux(steppedDirection);
            if (static_cast<std::int32_t>(m_vid->nChildVid[m_currentAnimation]) >= 0)
            {


                if (ordinal == 1 && childCount == 2)
                {
                    projectileBaseX = -(primaryCos * childX);
                    projectileBaseY = -(auxiliarySin * childX);
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                else if (ordinal == 2 && childCount == 3)
                {
                    offset.x = primarySin * childY;
                    offset.y = auxiliaryCos * childY;
                }
                else
                {
                    projectileBaseX = primaryCos * childX;
                    projectileBaseY = auxiliarySin * childX;
                    offset.x = projectileBaseX + primarySin * childY;
                    offset.y = projectileBaseY - auxiliaryCos * childY;
                }
                offset.z = m_vid->childZ[m_currentAnimation];
            }
            else
            {
                constexpr float kRandDivisor = 32767.0f;
                if (m_vid->spriteClassId() == 23u &&
                    childX == 0.0f &&
                    childY == 0.0f)
                {
                    const REGION* const region = static_cast<const REGION*>(this);
                    if ((region->regionFlags() & REGION::FullViewportFlag) == 0u)
                    {
                        const float width = region->regionWidth();
                        const float height = region->regionHeight();


                        const float randomWidth = static_cast<float>(std::rand()) * width;
                        const float randomHeight = static_cast<float>(std::rand()) * height;
                        offset.x = width * 0.5f - randomWidth / kRandDivisor;
                        offset.y = height * 0.5f - randomHeight / kRandDivisor + Z();
                    }
                    else
                    {
                        const float randomWidth = static_cast<float>(std::rand()) * map->SizeX();
                        const float randomHeight = static_cast<float>(std::rand()) * map->SizeY();
                        offset.x = randomWidth / kRandDivisor - X();
                        offset.y = randomHeight / kRandDivisor - Y() + Z();
                    }
                    offset.z = m_vid->childZ[m_currentAnimation];
                }
                else
                {
                    const float childXSpan = childX + childX;
                    const float childYSpan = childY + childY;
                    const float randomX = static_cast<float>(std::rand()) * childXSpan;
                    const float randomY = static_cast<float>(std::rand()) * childYSpan;
                    const float localX = childX - randomX / kRandDivisor;
                    const float localY = childY - randomY / kRandDivisor;
                    projectileBaseX = -(localX * primaryCos);
                    projectileBaseY = -(localX * auxiliarySin);
                    offset.x = localY * primarySin + projectileBaseX;
                    offset.y = projectileBaseY - localY * auxiliaryCos;
                    offset.z = m_vid->childZ[m_currentAnimation];
                }
            }
            const VECTOR target(m_xyz.x + offset.x, m_xyz.y + offset.y, m_xyz.z + offset.z);

            if (childVid->spriteClassId() == B_UNIT &&
                GlobalSpriteCollectorCanPlace(*map, childVid, target.x, target.y, target.z) != nullptr)
            {
                continue;
            }

            if (m_currentAnimation == 8 && Goal() == nullptr)
            {
                continue;
            }

            ANGLE childDirection = m_direction;
            if ((childVid->property & P_RANDBIRTH) != 0)
            {


                childDirection = ANGLE(std::rand() % 256);
            }
            else if ((childVid->properties() & P_VERTDIR) != 0u &&
                     m_currentAnimation != 8 && m_exData)
            {


                constexpr float kIsoProjection = 1.414306640625f;
                if (m_xyz.x == m_exData->sourceX &&
                    m_xyz.y == m_exData->sourceY)
                {
                    const float projectedX = directionSin(m_direction.Int()) * m_speed;
                    const float projectedY =
                        (directionCos(m_direction.Int()) * m_speed + m_zSpeed) * -kIsoProjection;
                    childDirection = DirectionFromFloatXY(projectedX, projectedY);
                }
                else
                {
                    const float projectedX = m_xyz.x - m_exData->sourceX;
                    const float projectedY =
                        (m_xyz.y - m_xyz.z - m_exData->sourceY +
                         m_exData->sourceZ) * kIsoProjection;
                    childDirection = DirectionFromFloatXY(projectedX, projectedY);
                }
            }

            SPRITE* child = map->CreateSprite(childVid,
                                                                    target,
                                                                    childDirection,
                                                                    this,
                                                                    false);
            if (child)
            {
                if (m_currentAnimation == 8)
                {
                    SPRITE* const goal = Goal();
                    const bool hasOwner = goal != nullptr;
                    const bool ownerHasRealVid = hasOwner && goal->Vid() != EmptyVid;
                    const int parentWeaponFlags =
                        *reinterpret_cast<const std::int32_t*>(weaponRecord->raw.data() + 0x04);
                    const bool weaponActionBit20 = (parentWeaponFlags & 0x20) != 0;
                    if (hasOwner && weaponActionBit20 && ownerHasRealVid)
                    {
                        child->Attack(goal);
                        child->StartMove();
                    }
                    else if (hasOwner)
                    {


                        constexpr float kScatterScale = 0.004999999888241291f;
                        constexpr float kRandScatterScale = 6.103701889514923e-05f;
                        const float projectileRadius =
                            NearDistanceTo(goal) * m_vid->weaponAim() * kScatterScale;
                        float helperX = goal->X() + projectileRadius + projectileBaseX;
                        float helperY = goal->Y() + projectileRadius + projectileBaseY;
                        const float ownerZ = goal->Z();

                        for (int attempt = 0; attempt < 5; ++attempt)
                        {
                            const float randomX =
                                static_cast<float>(std::rand()) * projectileRadius * kRandScatterScale;
                            const float candidateX =
                                goal->X() + projectileBaseX + projectileRadius - randomX;
                            const float randomY =
                                static_cast<float>(std::rand()) * projectileRadius * kRandScatterScale;
                            const float candidateY =
                                goal->Y() + projectileBaseY + projectileRadius - randomY;

                            const float candidateGround = map->GetGroundZ(VECTOR2{candidateX, candidateY});
                            if (ownerZ > candidateGround)
                            {
                                const float midX = (goal->X() + candidateX) * 0.5f;
                                const float midY = (goal->Y() + candidateY) * 0.5f;
                                const float midpointGround = map->GetGroundZ(VECTOR2{midX, midY});
                                if (ownerZ > midpointGround)
                                {
                                    helperX = candidateX;
                                    helperY = candidateY;
                                    break;
                                }
                            }

                            helperX = candidateX;
                            helperY = candidateY;
                        }

                        SPRITE* const projectileOwner = new (std::nothrow) SPRITE(
                            map, EmptyVid, VECTOR(helperX, helperY, ownerZ), ANGLE(static_cast<unsigned char>(0)), nullptr);

                        child->Attack(projectileOwner);
                        child->StartMove();
                    }
                }
            }
        }

        if (m_currentAnimation == 8 && (m_runtimeFlags & ChildSpawnToggleFlag) == 0u)
        {
            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x10u || commandBits == 0x14u)
            {
                const bool requireEndFrame =
                    ((m_vid->properties() & P_TRACK) != 0u) ||
                    ((childVid->properties() & P_BIRTHASSMOKE) != 0u);
                if (!requireEndFrame || m_currentFrame == m_currentFrameEnd)
                {
                    SPRITE* const goal = Goal();
                    SPRITE* const parent = childBacklink();
                    if (goal && parent && parent->Goal() == goal)
                    {
                        const DWORD parentCommandBits =
                            parent->m_runtimeFlags & SPRITE::CommandBitsMask;
                        if (parentCommandBits == 0x10u || parentCommandBits == 0x14u)
                            parent->SetCommand(0, nullptr);

                        if (parent->Vid()->spriteClassId() == B_ENGINE &&
                            ((parent->m_runtimeFlags & SPRITE::CommandBitsMask) == 0x74u))
                        {
                            static_cast<ENGINE*>(parent)->SetCommandToTrain(0x1E, 0, 0, 0);
                        }
                    }


                    (void)SetCommand(0, nullptr);
                }
            }
        }

        return;
    }

    SPRITE::~SPRITE()
    {
        destroyBaseSpriteState();
    }

    void DeleteSpriteThroughVirtualDeletingDestructor(SPRITE* sprite) noexcept
    {
        if (!sprite)
            return;
        delete sprite;
    }


    void SPRITE::destroyBaseSpriteState()
    {
        VID* const vid = m_vid;
        const bool isEmptyVid = vid == EmptyVid;
        const int nvid = vid->nVid;


        const int destroyFunction = vid->destroyScriptFunction();
        if (destroyFunction >= 0)
        {
            const int spriteArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu);
            (void)core::Application::callScriptFunction(destroyFunction, spriteArg, 0);
        }

        if ((vid->properties() & 0x28u) != 0u && vid->gridDotCount() > 0)
            vid->ResetGridZ(this);

        if (isEmptyVid && m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 4, "noRef for SPRITE with EmptyVid", m_listReferenceCount, nvid);

        if (m_listReferenceCount > 1)
        {
            GlobalSpriteCollector()->Delete(this);
            if (m_listReferenceCount > 1)
            {
                win::applicationWinInstance()->transferFrom(this);
            }
        }

        vid->DecreaseNoSprites(armyIndex());
        setGoalSprite(nullptr);


        if (m_bestTargetSprite)
        {
            SPRITE* const bestTarget = m_bestTargetSprite;
            bestTarget->m_listReferenceCount = spriteSub32Wrap(bestTarget->m_listReferenceCount, 1);
            const int refs = bestTarget->m_listReferenceCount;
            if (refs <= 0)
            {
                if (refs < 0)
                    bestTarget->logSpriteResourceError(4, "noRef at Release", refs);
                else
                    DeleteSpriteThroughVirtualDeletingDestructor(bestTarget);
            }
            m_bestTargetSprite = nullptr;
        }


        if (m_childBacklink)
        {
            VID* const linkedVid = m_vid->linkedVid();
            while (m_childChain && m_childChain->Vid() == linkedVid)
                DeleteSpriteThroughVirtualDeletingDestructor(m_childChain);

            m_childBacklink->m_childChain = m_childChain;
            if (m_childChain)
                m_childChain->m_childBacklink = m_childBacklink;
        }
        else
        {
            while (m_childChain)
                DeleteSpriteThroughVirtualDeletingDestructor(m_childChain);
        }

        if (!isEmptyVid)
        {
            Remove();
            --m_listReferenceCount;
        }

        if (m_listReferenceCount != 0)
            LOG::ResourceError("SPRITE %i", 10, "Reference count non zero after delete", m_listReferenceCount, nvid);


        if (m_exData)
        {
            EX_SPRITE_DATA* const aux = m_exData;
            aux->name.~STRING();
            aux->items.vtableTag = currentCommandWordListVtable();
            if (aux->items.values)
                ::operator delete(aux->items.values);
            aux->items.values = nullptr;
            aux->items.count = 0u;
            ::operator delete(aux);
        }

        if (m_bestTargetSprite)
        {
            const int ptrNvid = m_bestTargetSprite->Vid()->nVid;
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, ptrNvid);
        }

        m_commandStack.releaseCommandRecordsTail();

    }


    void SPRITE::MoveTact()
    {
        VID* const vid = m_vid;
        if (!vid->movementTactEnabled())
            return;

        float candidateX = X();
        float candidateY = Y();
        float candidateZ = Z();
        computeNextMovementPosition(&candidateX, &candidateY, &candidateZ);

        MAP* const map = reinterpret_cast<MAP*>(core::ApplicationOwner());
        const float currentGroundZ = map->GetGroundZ(VECTOR2{X(), Y()});
        const float candidateGroundZ = map->GetGroundZ(VECTOR2{candidateX, candidateY});
        const float moveUpLimitZ = candidateGroundZ + vid->topZValue();

        const DWORD property = vid->properties();
        if ((vid->spriteTypeId() & 0x00000200u) != 0u &&
            (property & P_GRAVITY) != 0u &&
            candidateGroundZ >= candidateZ &&
            Z() >= currentGroundZ)
        {
            candidateZ = Z();
        }
        else if (Z() != candidateZ)
        {
            if ((property & P_GRAVITY) == 0u && moveUpLimitZ != 0.0f)
            {
                bool clampCandidate = false;
                bool clearZSpeed = false;
                if (x87OrderedLess(Z(), moveUpLimitZ))
                {
                    if (!(candidateZ < moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if (x87OrderedGreater(Z(), moveUpLimitZ))
                {
                    if (!(candidateZ >= moveUpLimitZ))
                    {
                        clampCandidate = true;
                        clearZSpeed = true;
                    }
                }
                else if ((property & 0x08000000u) == 0u)
                {

                    clearZSpeed = true;
                }

                if (clampCandidate)
                    candidateZ = moveUpLimitZ;
                if (clearZSpeed)
                    setZSpeedDirect(0.0f);
            }
        }


        if (m_goalSprite != nullptr && m_speed == 0.0f &&
            (m_runtimeFlags & CrossedGoalAxesMask) == CrossedGoalAxesMask)
        {
            Stop();
        }

        if (X() != candidateX || Y() != candidateY)
        {
            if (CanPlaceWithCrush(candidateX, candidateY, candidateZ) != nullptr)
            {
                m_runtimeFlags |= 0x00000400u;
                setZSpeedDirect(0.0f);
                setSpeedDirect(0.0f);
            }
            else
            {
                ChangeCoor(candidateX, candidateY, candidateZ);
            }
        }

        if (Z() != candidateZ)
            ChangeCoor(X(), Y(), candidateZ);
    }


    int SPRITE::steerAwayFromMapBoundary(float x, float y) noexcept
    {
        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        if (x < 0.0f)
        {
            RotateTact(ANGLE(0x40u), deltaMs);
            return 1;
        }
        if (y < 0.0f)
        {
            RotateTact(ANGLE(0x80u), deltaMs);
            return 1;
        }

        const float appWidth = applicationWorldFloatAt(core::application_layout::MapExtentX);
        const float appHeight = applicationWorldFloatAt(core::application_layout::MapExtentY);
        if (x >= appWidth)
        {
            RotateTact(ANGLE(0xC0u), deltaMs);
            return 1;
        }
        if (y >= appHeight)
        {
            RotateTact(ANGLE(static_cast<unsigned char>(0)), deltaMs);
            return 1;
        }
        return 0;
    }


    void SPRITE::computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept
    {
        m_runtimeFlags &= ~0x00000400u;

        *xOut = m_xyz.x;
        *yOut = m_xyz.y;
        *zOut = m_xyz.z;

        VID* const vid = m_vid;
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && (m_runtimeFlags & MovementStartedFlag) == 0u)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without StartMove()", 0,
                               vid ? vid->nvid() : -1);
            StartMove();
        }
        if ((m_runtimeFlags & SPRITE::CommandBitsMask) == 4u && m_goalSprite == nullptr)
        {
            LOG::ResourceError("SPRITE %i", 10, "Move without goal", 0,
                               vid ? vid->nvid() : -1);
            Stop();
        }


        const std::uint32_t rawFrameDelta =
            core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
        const std::int32_t frameDeltaMs = static_cast<std::int32_t>(rawFrameDelta);
        constexpr std::uint32_t kNoSpeedBits = 0x497423F0u;

        if ((m_runtimeFlags & MovementStartedFlag) != 0u)
        {
            const float maxSpeed = MaxSpeed();
            if (std::fabs(maxSpeed) > std::fabs(m_speed))
            {
                const float accel = vid->accelerationValue();
                if (spriteBitsEqual(accel, kNoSpeedBits))
                {
                    m_speed = maxSpeed;
                }
                else
                {


                    const float sign = (m_runtimeFlags & 0x00000800u) != 0u ? -1.0f : 1.0f;
                    m_speed = static_cast<float>(frameDeltaMs) * accel * sign + m_speed;
                    if (std::fabs(m_speed) >= std::fabs(maxSpeed))
                        m_speed = maxSpeed;
                }
            }
            else if (vid->spriteClassId() == B_CANNON && maxSpeed < m_speed)
            {


                const float slow = vid->slowValue();
                if (spriteBitsEqual(slow, kNoSpeedBits))
                {
                    m_speed = maxSpeed;
                }
                else
                {
                    const float sign = (m_runtimeFlags & 0x00000800u) != 0u ? -1.0f : 1.0f;
                    m_speed -= sign * static_cast<float>(frameDeltaMs) * slow;
                    if (std::fabs(maxSpeed) > std::fabs(m_speed))
                        m_speed = maxSpeed;
                }
            }
        }
        else if (x87OrderedGreater(m_speed, 0.0f))
        {
            const float slow = vid->slowValue();
            if (spriteBitsEqual(slow, kNoSpeedBits) ||
                advanceDecelerationStep(
                    static_cast<std::int32_t>(core::CurrentTimeMilliseconds() - m_applicationBucketTime),
                    slow, m_speed))
            {
                m_speed = 0.0f;
            }
        }

        const std::int32_t movementDeltaMs = static_cast<std::int32_t>(
            core::CurrentTimeMilliseconds() - m_applicationBucketTime);

        GRAPH* const graph = Graph;
        const bool speedIsZeroRoute = (m_speed == 0.0f);
        const float windSpeed = graph->windSpeed();
        const bool windIsZeroRoute = (windSpeed == 0.0f);
        const DWORD property = vid->properties();
        const bool windProperty = (property & P_WIND) != 0u;
        if (!speedIsZeroRoute || (!windIsZeroRoute && windProperty))
        {
            if (spriteBitsEqual(m_speed, kNoSpeedBits))
            {
                SPRITE* const target = m_goalSprite;
                if (target && ((m_runtimeFlags & CrossedGoalXFlag) == 0u ||
                               (m_runtimeFlags & CrossedGoalYFlag) == 0u))
                {
                    float targetX = target->m_xyz.x;
                    float targetY = target->m_xyz.y;
                    float targetZ = target->m_xyz.z;
                    const int traceHit = traceMovementCollisionTo(&targetX, &targetY, &targetZ);
                    ChangeCoor(targetX, targetY, targetZ);
                    if (traceHit)
                        m_runtimeFlags |= 0x00000400u;
                    *xOut = targetX;
                    *yOut = targetY;
                    *zOut = targetZ;
                    m_runtimeFlags |= CrossedGoalAxesMask;
                    return;
                }
                m_runtimeFlags |= CrossedGoalAxesMask;
            }
            else
            {
                int movementDirection = m_direction.Int();
                if ((property & P_MOVEWITHANYDIRECTION) != 0u && m_goalSprite != nullptr)
                {
                    movementDirection = DirectionFromFloatXY(
                        m_goalSprite->X() - X(), m_goalSprite->Y() - Y()).Int();
                }
                const std::uint32_t direction = static_cast<std::uint32_t>(movementDirection) & 0xFFu;


                advancePlanarPosition(movementDeltaMs, m_speed,
                                    spriteFloatFromBits(g_directionTrigWindow[direction]),
                                    spriteFloatFromBits(g_directionTrigWindow[256u + direction]),
                                    *xOut, *yOut);
                if (windProperty)
                {
                    const int windDirection = static_cast<int>(graph->windDirection());
                    advancePlanarPosition(movementDeltaMs, windSpeed,
                                        directionSin(windDirection), directionCos(windDirection),
                                        *xOut, *yOut);
                }
            }
        }

        if ((property & P_GRAVITY) != 0u)
        {
            CONSTANT* const constants = g_baseConstants;
            const float gravity = spriteFloatFromBits(constants->raw[2]);
            applyGravityStep(movementDeltaMs, gravity, m_zSpeed);
        }
        else if ((property & P_GRAVITY2) != 0u)
        {
            CONSTANT* const constants = g_baseConstants;
            const float gravity = spriteFloatFromBits(constants->raw[3]);
            applyGravityStep(movementDeltaMs, gravity, m_zSpeed);
        }
        advanceVerticalPosition(movementDeltaMs, m_zSpeed, *zOut);

        SPRITE* const target = m_goalSprite;
        if (!target)
            return;

        constexpr float kGoalTolerance = 0.5f;
        const float finishX = *xOut;
        const float goalX = target->m_xyz.x;
        if (finishX > m_xyz.x)
        {
            if (goalX >= m_xyz.x - kGoalTolerance &&
                goalX <= finishX + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalXFlag;
        }
        else
        {
            if (goalX >= finishX - kGoalTolerance &&
                goalX <= m_xyz.x + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalXFlag;
        }

        const float finishY = *yOut;
        const float goalY = target->m_xyz.y;
        if (finishY > m_xyz.y)
        {
            if (goalY >= m_xyz.y - kGoalTolerance &&
                goalY <= finishY + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalYFlag;
        }
        else
        {
            if (goalY >= finishY - kGoalTolerance &&
                goalY <= m_xyz.y + kGoalTolerance)
                m_runtimeFlags |= CrossedGoalYFlag;
        }

    }


    void SPRITE::GotoNearestMoveAction()
    {
        int nearest=-1; float nearestDistance=10000000.0f;
        for(std::uint32_t i=0;i<m_commandStack.m_commandRecords.count;++i){
            const core::List<ACT>::CommandRecordStorage& raw=m_commandStack.m_commandRecords.records[i];
            if(raw.words[0]!=0x21u) continue;
            const float dx=X()-(float)(int)raw.words[1], dy=Y()-(float)(int)raw.words[2];
            const float distance=std::sqrt(dx*dx+dy*dy);
            if(distance<nearestDistance){nearestDistance=distance;nearest=(int)i;}
        }
        if(nearest<0)return;
        const core::List<ACT>::CommandRecordStorage& raw=m_commandStack.m_commandRecords.records[nearest];
        ChangeCoor((float)(int)raw.words[1],(float)(int)raw.words[2],(float)(int)raw.words[3]);
        Action(0x47,nearest,0,0);
    }


    void SPRITE::InsertPauseBeforeMoveActions(int pauseValue)
    {
        std::uint32_t index=0;
        while(index<m_commandStack.m_commandRecords.count){
            if(m_commandStack.m_commandRecords.records[index].words[0]==0x21u){
                m_commandStack.insertAt(static_cast<std::uint32_t>(index), buildCommandRecord(0x28u, pauseValue, 0, 0));
                ++index;
            }
            ++index;
        }
        if(m_commandStack.m_commandRecords.count!=0u && m_commandStack.m_commandRecords.records[0].words[0]==0x47u)
            m_commandStack.m_commandRecords.records[0].words[1]=m_commandStack.m_commandRecords.count-1u;
    }


    void SPRITE::ChangeCoor(float x, float y) noexcept
    {
        const float deltaX = x - m_xyz.x;
        const float deltaY = y - m_xyz.y;
        const DWORD property = m_vid->properties();
        if ((property & P_NOTCHANGELINKERCOOR) != 0u)
            return;

        const bool ownsTerrainGrid =
            (property & (P_BUILDSIZETOGRIDZ | P_BUILDVIDZTOGRIDZ)) != 0u &&
            m_vid->gridDotCount() != 0;
        if (ownsTerrainGrid)
            m_vid->ResetGridZ(this);

        if ((property & P_HASH) != 0u)
            GlobalSpriteCollector()->ChangeCoor(this, m_xyz.x + deltaX, m_xyz.y + deltaY);

        const std::uint32_t realTime = core::RealCurrentTime;
        if (m_exData && m_exData->changeCoorTime != realTime)
        {
            m_exData->changeCoorTime = realTime;
            m_exData->sourceX = m_xyz.x;
            m_exData->sourceY = m_xyz.y;
        }

        m_xyz.x += deltaX;
        m_xyz.y += deltaY;

        if (ownsTerrainGrid)
            m_vid->SetGridZ(this);
    }


    void SPRITE::ChangeCoor(float x, float y, float z) noexcept
    {
        const float deltaX = x - m_xyz.x;
        const float deltaY = y - m_xyz.y;
        const float deltaZ = z - m_xyz.z;

        for (SPRITE* node = this; node; node = node->m_childChain)
        {
            const VECTOR before = node->m_xyz;
            const VECTOR target(before.x + deltaX, before.y + deltaY, before.z + deltaZ);

            const DWORD property = node->m_vid->property;
            const bool ownsTerrainGrid =
                (property & (P_BUILDSIZETOGRIDZ | P_BUILDVIDZTOGRIDZ)) != 0u &&
                node->m_vid->gridDotCount() > 0;
            if (ownsTerrainGrid)
            {


                node->m_vid->ResetGridZ(this);
            }

            if ((property & P_HASH) != 0)
                GlobalSpriteCollector()->ChangeCoor(node, target.x, target.y);

            const std::uint32_t realTime = core::RealCurrentTime;
            if (node->m_exData && node->m_exData->changeCoorTime != realTime)
            {
                node->m_exData->changeCoorTime = realTime;
                node->m_exData->sourceX = node->m_xyz.x;
                node->m_exData->sourceY = node->m_xyz.y;
                node->m_exData->sourceZ = node->m_xyz.z;
            }

            node->m_xyz = target;

            if (ownsTerrainGrid)
            {


                node->m_vid->SetGridZ(this);
            }
        }
    }

    int SPRITE::AddListReference()
    {
        m_listReferenceCount = spriteAdd32Wrap(m_listReferenceCount, 1);
        return m_listReferenceCount;
    }

    int SPRITE::Release()
    {
        m_listReferenceCount = spriteSub32Wrap(m_listReferenceCount, 1);
        const int refs = m_listReferenceCount;
        if (refs > 0)
            return refs;
        if (refs >= 0)
        {
            DeleteSpriteThroughVirtualDeletingDestructor(this);
            return 0;
        }


        logSpriteResourceError(4, "noRef at Release", refs);
        return 0;
    }


    void SPRITE::ChangeAnimation(int animationId)
    {
        if (animationId >= VID::NO_ANIMATION)
        {
            const int nvid = m_vid ? m_vid->nVid : -1;


            LOG::ResourceError("SPRITE %i", 4,
                               "new_animation in ChangeAnimation",
                               animationId, nvid);
            return;
        }


        if (SPRITE* const child = m_childChain)
        {
            VID* const childVid = child->m_vid;
            if (childVid && childVid->spriteClassId() != 12u)
            {
                bool follow = false;
                if (childVid->weaponCount() == 0u &&
                    (childVid->properties() & P_WIND) == 0u &&
                    child->m_currentAnimation != 8 &&
                    child->m_currentAnimation < 15)
                {
                    follow = true;
                }
                else if (animationId == 15 || animationId == 16)
                {
                    follow = true;
                }

                if (follow)
                    child->ChangeAnimation(animationId);
            }
        }

        const int previousAnimation = m_currentAnimation;


        if (previousAnimation == animationId && previousAnimation != 8)
            return;


        m_runtimeFlags &= ~0x00000200u;


        if ((animationId == 15 || animationId == 16) &&
            m_vid->noAnimCadr[animationId] == 0)
        {
            if (m_currentFrame > m_currentFrameEnd)
            {
                if ((m_vid->properties() & P_CREATECHILDEND) != 0u)
                    m_currentFrame = m_currentFrameEnd;
                else
                    m_currentFrame = m_currentFrameBegin;
            }

            m_currentFrameBegin = m_currentFrame;
            m_currentFrameEnd = m_currentFrame;
            m_currentAnimation = animationId;
            m_applicationBucketTime = core::CurrentTimeMilliseconds();
            return;
        }


        int frameCarry = 0;
        const int previousBase =
            (previousAnimation >= 0 && previousAnimation < VID::NO_ANIMATION)
                ? m_vid->animationBaseFrame[previousAnimation]
                : 0;
        const int nextBase = m_vid->animationBaseFrame[animationId];
        const int nextCount = m_vid->animationFrameCount[animationId];
        if (previousAnimation >= 0 && previousAnimation < VID::NO_ANIMATION &&
            previousBase == nextBase &&
            m_vid->animationFrameCount[previousAnimation] == nextCount)
        {
            const int liveOffset = spriteSub32Wrap(m_currentFrame, m_currentFrameBegin);
            if (liveOffset < nextCount)
                frameCarry = liveOffset;
        }

        int frameDirection = m_direction.Int() & 0xFF;
        if (m_zSpeed != 0.0f && (m_vid->properties() & P_VERTDIR) != 0u)
        {
            int projectedDirection = frameDirection;
            (void)projectVerticalMotionDirection(
                frameDirection, m_speed, m_zSpeed, projectedDirection);
            frameDirection = projectedDirection;
        }


        const int realDirection = m_vid->RealDirection(ANGLE(frameDirection));
        const int begin = spriteAdd32Wrap(
            nextBase,
            spriteImul32Low(realDirection, nextCount));
        m_currentFrameBegin = begin;

        if (animationId >= 13 && m_vid->noAnimCadr[animationId] == 0)
            m_currentFrameEnd = begin;
        else
            m_currentFrameEnd = spriteAdd32Wrap(spriteAdd32Wrap(begin, nextCount), -1);

        m_currentFrame = begin;


        bool synchronizedFromBacklink = false;
        if (SPRITE* const backlink = m_childBacklink)
        {
            if (previousAnimation == 2 &&
                (static_cast<std::uint32_t>(m_vid->weaponFlags()) & 0x00000800u) != 0u)
            {
                const int ownSpan = spriteSub32Wrap(m_currentFrameEnd, m_currentFrameBegin);
                const int backlinkSpan = spriteSub32Wrap(
                    backlink->m_currentFrameEnd, backlink->m_currentFrameBegin);
                if (ownSpan == backlinkSpan)
                {
                    m_currentFrame = spriteAdd32Wrap(
                        m_currentFrameBegin,
                        spriteSub32Wrap(backlink->m_currentFrame, backlink->m_currentFrameBegin));
                    synchronizedFromBacklink = true;
                }
            }
        }

        if (!synchronizedFromBacklink)
            m_currentFrame = spriteAdd32Wrap(m_currentFrame, frameCarry);

        if (m_currentFrame > m_currentFrameEnd)
            m_currentFrame = m_currentFrameBegin;


        m_currentAnimation = animationId;
        m_applicationBucketTime = core::CurrentTimeMilliseconds();
    }


    void SPRITE::ChangeSpeed(float value)
    {
        m_speed = value;
    }

    void SPRITE::Draw()
    {
        m_vid->Draw(this);
    }


    void SPRITE::DrawRectangle()
    {
        GRAPH* const graph = Graph;
        const VID* const vid = Vid();
        if (!graph || !vid)
            return;


        if (vid->spriteClassId() == 23u)
        {
            DrawDebugOverlay();
            return;
        }

        const DWORD spriteType = vid->spriteTypeId();


        DWORD color = 0u;
        bool drawRectangle = true;
        if ((spriteType & U_TERRAIN) != 0u && (vid->properties() & P_HASH) != 0u)
            color = g_colorBlack.color;
        else if (vid->renderLayer() == 11)
            color = g_colorWhite.color;
        else if ((spriteType & U_UNIT) != 0u)
            color = g_colorGreen.color;
        else if ((spriteType & U_AVIA) != 0u)
            color = g_colorLightBlue.color;
        else if ((spriteType & U_OBJECT) != 0u)
            color = g_colorYellow.color;
        else if ((spriteType & U_RAILWAY) != 0u)
            color = g_colorRed.color;
        else if ((spriteType & U_CANNON) != 0u)
            color = g_colorGray.color;
        else if (vid->spriteClassId() == 10u || vid->spriteClassId() == 19u)
            color = g_colorWhite.color;
        else
            drawRectangle = false;

        if (!drawRectangle)
            return;

        const core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        const float screenX = X() - drawState.cameraShiftX();
        const float screenY = Y() - Z() - drawState.cameraShiftY();


        graph->Box(
            X() - vid->halfSizeX() - drawState.cameraShiftX(),
            Y() - Z() - vid->halfSizeY() - drawState.cameraShiftY(),
            X() + vid->halfSizeX() - drawState.cameraShiftX(),
            Y() - Z() + vid->halfSizeY() - drawState.cameraShiftY(),
            color);


        if ((spriteType & (U_OBJECT | U_UNIT)) != 0u)
            graph->Line(screenX, screenY - vid->sizeZ(), screenX, screenY, g_colorBlue.color);


        graph->PrintfXY(screenX + 1.0f, screenY, "%i", vid->nvid());
    }


    float SPRITE::ScreenX() const
    {
        return X() - core::GlobalApplicationDrawDispatcherState().cameraShiftX();
    }


    float SPRITE::ScreenY() const
    {
        return Y() - Z() - core::GlobalApplicationDrawDispatcherState().cameraShiftY();
    }


    ANGLE SPRITE::Direction() const
    {
        return m_direction;
    }


    SPRITE* SPRITE::Goal() const noexcept
    {
        return m_goalSprite;
    }

    int SPRITE::SizeTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return Sqrt(dx * dx + dy * dy);
    }

    ANGLE SPRITE::DirectionTo(const VECTOR2& target) const
    {
        const int dx = static_cast<int>(target.x - m_xyz.x);
        const int dy = static_cast<int>(target.y - m_xyz.y);
        return Decart2Polar(dx, dy);
    }


    ANGLE SPRITE::DirectionTo(const SPRITE* sprite) const
    {


        return DirectionFromFloatXY(sprite->X() - X(), sprite->Y() - Y());
    }


    ANGLE SPRITE::DirectionTo(const SPRITE* sprite, float* radius) const
    {
        return ANGLE(sprite->X() - X(), sprite->Y() - Y(), radius);
    }


    ACT SPRITE::buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        ACT out{};
        out.opcode = opcode;
        out.argument1 = static_cast<std::uint32_t>(argument1);
        out.argument2 = static_cast<std::uint32_t>(argument2);
        out.argument3 = static_cast<std::uint32_t>(argument3);
        return out;
    }


    STRING SPRITE::GetTextActions() const
    {
        STRING result;
        serializeCommandRecordsText(result);
        return result;
    }


    STRING SPRITE::GetTextItems() const
    {
        STRING result;
        serializeCommandWordsText(result);
        return result;
    }


    void SPRITE::SetTextActions(const STRING* text)
    {
        if (text)
            parseCommandRecordsText(*text);
    }


    void SPRITE::SetTextItems(const STRING* text)
    {
        if (!text || text->isEmpty())
            return;
        parseCommandWordsText(*text);
    }


    void SPRITE::AddActionAfterStop(int action, int var1, int var2, int var3)
    {
        queueCommandBeforeStopSentinel(static_cast<std::uint32_t>(action), var1, var2, var3);
    }


    int SPRITE::ActionStackHaveCommand(int command) const noexcept
    {
        return hasCommandOpcode(static_cast<std::uint32_t>(command));
    }

    void SPRITE::serializeCommandRecordsText(STRING& out) const
    {
        m_commandStack.serializeCommandRecordsText(out);
    }

    std::string SPRITE::serializeCommandRecordsText() const
    {
        return m_commandStack.serializeCommandRecordsText();
    }

    void SPRITE::parseCommandRecordsText(const STRING& text)
    {
        m_commandStack.parseCommandRecordsText(text);
    }

    void SPRITE::queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3)
    {
        m_commandStack.queueCommandBeforeStopSentinel(opcode, argument1, argument2, argument3);
    }

    int SPRITE::Action(int opcodeCarrier, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const std::uint32_t opcode = static_cast<std::uint32_t>(opcodeCarrier);
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        int returnValue = 0;


        switch (opcode & 0xFFu)
        {
        case static_cast<std::uint32_t>(AnimationCode::ANI_DEATH):
        {
            VID* const sourceVid = m_vid;
            const int damageRaw = sourceVid->deathDamageMinimumRawBits();

            returnValue = 0;
            if (damageRaw == 0)
            {

                break;
            }

            setHpRaw(0);
            const float deathRange = sourceVid->deathRangeValue();
            const float rangeX = sourceVid->halfSizeX() + deathRange;
            const float rangeY = sourceVid->halfSizeY() + deathRange;
            const float sourceSizeZ = sourceVid->sizeZ();
            const float rangeZ = x87LessEqualOrUnordered(20.0f, sourceSizeZ)
                ? sourceSizeZ
                : 20.0f;

            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(
                     X() - rangeX, Y() - rangeY, X() + rangeX, Y() + rangeY);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                if (candidate == this)
                    continue;

                VID* const candidateVid = candidate->Vid();
                if (candidateVid->maximumHp() == 0)
                    continue;

                if ((sourceVid->properties() & P_NOTDAMAGEFORFRIEND) != 0u &&
                    (sameArmy(*candidate)))
                {
                    continue;
                }

                if (!f32SumGreaterThanAbsDiffOrdered(
                        rangeX, candidateVid->halfSizeX(), X(), candidate->X()) ||
                    !f32SumGreaterThanAbsDiffOrdered(
                        rangeY, candidateVid->halfSizeY(), Y(), candidate->Y()) ||
                    !f32SumGreaterThanAbsDiffOrdered(
                        rangeZ, candidateVid->sizeZ(), Z(), candidate->Z()))
                {
                    continue;
                }

                const float midpointX = (candidate->X() + X()) * 0.5f;
                const float midpointY = (candidate->Y() + Y()) * 0.5f;
                const float midpointGround =
                    mapOwner()->GetGroundZ(VECTOR2{midpointX, midpointY});
                if (f32SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), midpointGround))
                    continue;

                const float nearSourceX = spriteWeightedQuarterF32(X(), candidate->X());
                const float nearSourceY = spriteWeightedQuarterF32(Y(), candidate->Y());
                const float nearSourceGround =
                    mapOwner()->GetGroundZ(VECTOR2{nearSourceX, nearSourceY});
                if (f32SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearSourceGround))
                    continue;

                const float nearCandidateX = spriteWeightedQuarterF32(candidate->X(), X());
                const float nearCandidateY = spriteWeightedQuarterF32(candidate->Y(), Y());
                const float nearCandidateGround =
                    mapOwner()->GetGroundZ(VECTOR2{nearCandidateX, nearCandidateY});
                if (f32SumLessOrUnordered(
                        candidate->Z(), candidateVid->sizeZ(), nearCandidateGround))
                    continue;

                int damage = damageRaw;
                if ((sourceVid->properties() & P_RADIALDAMAGE) != 0u &&
                    deathRange != 0.0f)
                {


                    if (!computeFalloffDamage(
                            X(), Y(), candidate->X(), candidate->Y(),
                            deathRange, damageRaw, damage))
                        continue;
                }

                candidate->dispatchVirtualAction(ActionCode::ACT_DAMAGE,
                    damage,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                    0);
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ATTACK):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            Attack(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE):
        {
            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), EmptyVid,
                VECTOR(spriteFildToF32(argument1), spriteFildToF32(argument2), spriteFildToF32(argument3)),
                ANGLE(static_cast<unsigned char>(0)), nullptr);
            Move(helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_MOVE_TO):
        {
            SPRITE* const owner = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            Move(owner);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BUILD_UNIT):
        {
            int createNvid = argument1;
            if (createNvid == 0)
                createNvid = dispatchVirtualAction(
                    static_cast<std::uint32_t>(ActionCode::ACT_GET_ITEM_TYPE),
                    4, 0, 0);

            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (createNvid <= 0 || createNvid >= table.count())
                return 0;

            VID* const createVid = table.slot(createNvid);
            if (!createVid)
                return 0;

            const auto spawnCoordinate = [](float ownerCoordinate, int rawArgument) noexcept -> float
            {
                if (rawArgument == 0)
                    return ownerCoordinate;
                if (rawArgument > 0)
                    return spriteFildToF32(rawArgument);

                const std::uint32_t span =
                    static_cast<std::uint32_t>(1) - static_cast<std::uint32_t>(rawArgument);
                const std::uint32_t randomPart = span != 0u
                    ? static_cast<std::uint32_t>(std::rand()) % span
                    : 0u;
                const std::int32_t doubled = static_cast<std::int32_t>(randomPart * 2u);
                return ownerCoordinate - spriteFildToF32(rawArgument) - spriteFildToF32(doubled);
            };

            const float createX = spawnCoordinate(m_xyz.x, argument2);
            const float createY = spawnCoordinate(m_xyz.y, argument3);
            SPRITE* const created = mapOwner()->CreateSprite(
                createVid,
                VECTOR(createX, createY, m_xyz.z),
                m_direction,
                this,
                false);
            if (created &&
                ActionStackHaveCommand(static_cast<int>(ActionCode::ACT_STOP_STACK)) != 0)
            {


                (void)dispatchVirtualAction(
                    ActionCode::ACT_COPY_STACK_TO,
                    static_cast<int>(reinterpret_cast<std::uintptr_t>(created) & 0xFFFFFFFFu),
                    0, 0);
            }
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_COOR_ATTACK):
        {
            VID* metricVid = m_vid;
            if (SPRITE* const child = childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == m_vid->linkedVid() &&
                    childVid->hasWeaponChildDescriptor() != 0u &&
                    childVid->weaponCount() != 0u)
                {
                    metricVid = childVid;
                }
            }

            const int weaponType = metricVid->weaponTypeMask();
            const float x = spriteFildToF32(argument1);
            float yProbe = spriteFildToF32(argument2);
            float roundedGround = 0.0f;
            int helperY = argument2;
            if (weaponType == 8)
            {
                yProbe = spriteFildAddF32(argument2, 80.0f);
                const float ground = mapOwner()->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddRoundedFloatAndConvertToInt32(ground, 80.0f, roundedGround);
                helperY = spriteAdd32Wrap(helperY, zAsInt);
            }
            else
            {
                const float ground = mapOwner()->GetGroundZ(VECTOR2{x, yProbe});
                const int zAsInt = spriteAddRoundedFloatAndConvertToInt32(ground, 19.0f, roundedGround);
                helperY = spriteAdd32Wrap(helperY, spriteAdd32Wrap(zAsInt, -19));
            }


            SPRITE* const helper = new (std::nothrow) SPRITE(
                mapOwner(), EmptyVid,
                VECTOR(x, spriteFildToF32(helperY), roundedGround),
                ANGLE(static_cast<unsigned char>(0)), nullptr);
            SetCommand(4, helper);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ADD_ITEM):
            InsertItem(argument1);
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_DELETE_ITEM):
            return removeCommandWordValue(argument1);

        case static_cast<std::uint32_t>(ActionCode::ACT_HAVE_ITEM):
            return findLastCommandWord(argument1) >= 0 ? 1 : 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_DELETE_ALL_ITEM):
            clearCommandWordList();
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ITEM):
            return GetItemNumber(argument1);

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_NAME):
        {


            static STRING str;
            str = GetName();
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(&str));
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ITEM_TYPE):
        {
            const std::uint32_t mask = argument1 != 0
                ? static_cast<std::uint32_t>(argument1)
                : 0x00FFFFFFu;
            if (!m_exData || !m_exData->items.values ||
                m_exData->items.count == 0u)
                return -1;

            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            std::uint32_t matchCount = 0u;
            for (std::uint32_t i = 0; i < m_exData->items.count; ++i)
            {
                const int nvid = m_exData->items.values[i];
                VID* itemVid = EmptyVid;
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        itemVid = resolved;
                }
                if ((itemVid->spriteTypeId() & mask) != 0u)
                    ++matchCount;
            }


            std::uint32_t selected = static_cast<std::uint32_t>(std::rand()) % matchCount;
            for (std::uint32_t i = 0; i < m_exData->items.count; ++i)
            {
                const int nvid = m_exData->items.values[i];
                VID* itemVid = EmptyVid;
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        itemVid = resolved;
                }
                if ((itemVid->spriteTypeId() & mask) == 0u)
                    continue;
                if (selected-- == 0u)
                    return nvid;
            }
            return -1;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_FLAGMAN_TRIGGER):
        {
            MAP* const firstOwner = Map;
            SPRITE* const firstControlled = firstOwner->flagmanSpriteForPlayer(
                static_cast<int>(core::ActivePlayerIndex()));
            if (firstControlled)
            {
                MAP* const secondOwner = Map;
                SPRITE* const controlled = secondOwner->flagmanSpriteForPlayer(
                    static_cast<int>(core::ActivePlayerIndex()));
                if (shouldSuppressFlagmanCommand(
                        argument1, argument2, argument3, controlled->X(), controlled->Y()))
                {

                    break;
                }
            }
            ACT command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.append(command);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_RESTORE_COMMAND):
        {
            SPRITE* const target = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            SetCommand(static_cast<int>((opcode >> 8) & 0xFFu), target);
            if (target)
            {
                (void)target->Release();
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_DESTROY_UNIT):
        {


            if (argument1 == -1)
            {
                DeleteSpriteThroughVirtualDeletingDestructor(this);
                return 0;
            }

            core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            if (argument1 >= 0 &&
                argument1 < vidTable.count() &&
                vidTable.slot(argument1) != nullptr &&
                argument1 > 0)
            {
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());


                if (argument2 == 0 && argument3 == 0)
                {
                    for (;;)
                    {
                        SPRITE* const hit = application
                            ? application->findNearestSpriteByFilter(
                                argument1, 0.0f, 0.0f, 40000.0f, nullptr)
                            : core::Application::findNearestSpriteByFilter(
                                *mapOwner(), core::GlobalApplicationDrawDispatcherState(),
                                argument1, 0.0f, 0.0f, 40000.0f, nullptr);
                        if (!hit)
                            break;
                        DeleteSpriteThroughVirtualDeletingDestructor(hit);
                    }
                }
                else
                {


                    SPRITE* const hit = application
                        ? application->findSpriteAtPointByFilter(
                            argument1,
                            spriteFildToF32(argument2),
                            spriteFildToF32(argument3))
                        : core::Application::findSpriteAtPointByFilter(
                            *mapOwner(), core::GlobalApplicationDrawDispatcherState(),
                            argument1,
                            spriteFildToF32(argument2), spriteFildToF32(argument3));
                    if (hit)
                        DeleteSpriteThroughVirtualDeletingDestructor(hit);
                }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SCRIPT_VAR):
        {


            SCRIPT* const scriptRuntime = core::ApplicationScriptRuntime();
            if (scriptRuntime)
            {
                int value = argument2Carrier;
                if (argument3Carrier == 1)
                    value += scriptRuntime->getActionVariableInt(argument1);
                scriptRuntime->SetVariableActionInt(argument1, value);
            }

            break;
        }

        case static_cast<std::uint32_t>(AnimationCode::ANI_SALUT):
            if (m_currentAnimation != 8)
                ChangeAnimation(9);

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_RANDOM):
        {
            if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(0);
            }
            else if ((std::rand() % 5) == 0)
            {
                ChangeAnimation(12);
            }
            else
            {
                if (m_currentAnimation == 4 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->frameSpeed[m_currentAnimation]);
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteSub32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if (m_currentAnimation == 5 && (std::rand() % 3) != 0)
                {
                    VID* const vid = m_vid;
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->frameSpeed[m_currentAnimation]);
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(spriteAdd32Wrap(m_direction.Int(), 64), stepMs);
                    }
                else if ((std::rand() & 0x3) == 0)
                {
                    const int nextDirection = (std::rand() & 1) != 0
                        ? spriteSub32Wrap(m_direction.Int(), 64)
                        : spriteAdd32Wrap(m_direction.Int(), 64);
                    VID* const vid = m_vid;
                    const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameDefault =
                        static_cast<std::uint32_t>(vid->frameSpeed[m_currentAnimation]);
                    const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
                    const std::uint32_t delta = now - previous;
                    const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
                    RotateTact(nextDirection, stepMs);
                    }
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP):
            Stop();
            if (argument1 != 0)
                m_speed = 0.0f;

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_PAUSE):
        {
            if (!spriteFcompC3(m_speed, 0.0f))
                Stop();

            const int pauseRange = spriteAdd32Wrap(argument2, 1);
            const int pauseDelta = pauseRange != 0 ? std::rand() % pauseRange : 0;
            const int pauseTimer = spriteAdd32Wrap(argument1, pauseDelta);
            m_actionTimer = static_cast<DWORD>(pauseTimer);
            if (pauseTimer == 0)
                SetCommand(0, nullptr);
            else
                SetCommand(18, nullptr);
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_ROTATE):
        {
            VID* const vid = m_vid;
            const std::uint32_t now = as1::core::CurrentTimeMilliseconds();
            const std::uint32_t previous = as1::core::PreviousWorldTimeMilliseconds();
            const std::uint32_t frameDefault =
                static_cast<std::uint32_t>(vid->frameSpeed[m_currentAnimation]);
            const std::uint32_t delta = now - previous;
            const std::uint32_t stepMs = delta > frameDefault ? delta : frameDefault;
            RotateTact(argument1, stepMs);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_COMMAND):
        {
            SetCommand(0, nullptr);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_COPY_STACK_TO):
        {
            SPRITE* const target = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            if (!target || target == this)
                return 0;


            target->ResetActionStack();
            const std::uint32_t sourceCount = m_commandStack.m_commandRecords.count;
            const core::List<ACT>::CommandRecordStorage* const sourceRecords =
                m_commandStack.m_commandRecords.records;
            if (!sourceRecords)
                return 0;

            for (std::uint32_t index = 0u; index < sourceCount; ++index)
            {
                const core::List<ACT>::CommandRecordStorage& raw = sourceRecords[index];
                if (raw.words[0] == static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK))
                    break;

                const ACT command = buildCommandRecord(
                    raw.words[0],
                    static_cast<int>(raw.words[1]),
                    static_cast<int>(raw.words[2]),
                    static_cast<int>(raw.words[3]));
                target->m_commandStack.append(command);
            }
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_NO_STACK):
            return static_cast<int>(m_commandStack.m_commandRecords.count);

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_DIRECTION):
        {
            ChangeDirection(static_cast<unsigned char>(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_ANIMATION):
        {
            ChangeAnimation(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_VID):
        {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        if (argument1 < 0 || argument1 >= table.count())
            return 0;

        VID* nextVid = table.slot(argument1);
        if (!nextVid)
            return 0;

        VID* const oldVid = m_vid;
        if (oldVid->nVid == argument1)
            return 0;

        const int oldDirection = m_direction.Int();
        const int oldAnimation = m_currentAnimation;
        const DWORD oldClass = oldVid->spriteClassId();
        const DWORD nextClass = nextVid->spriteClassId();

        if (oldClass != nextClass)
        {
            LOG::ResourceError("SPRITE %i", 4, "ACT_CHANGE_VID", argument1, oldVid->nVid);
        }

        for (VID* link = oldVid->linkedVid(); link; link = link->linkedVid())
            deleteChildByVid(link);

        m_runtimeFlags &= ~ChildSpawnToggleFlag;
        DeleteSpriteFromCollectorForActionSwitch(this);
        Remove();

        oldVid->DecreaseNoSprites(armyIndex());

        VID* swapVid = nullptr;
        if (argument1 < table.count())
            swapVid = table.slot(argument1);
        m_vid = swapVid ? swapVid : EmptyVid;
        m_vid->setLastSpriteCountChangeTimestamp(core::RealCurrentTime);
        m_vid->incrementSpriteCountForArmy(armyIndex());

        const int requestedAnimation = argument2 >= 0 ? argument2 : oldAnimation;
        m_direction = ANGLE(static_cast<unsigned char>(0));
        m_currentAnimation = 0;
        m_currentFrame = 0;
        m_currentFrameBegin = 0;
        m_currentFrameEnd = m_vid->animationFrameCountFor(0) - 1;

        if (m_vid->actionAuxStateRequired() != 0)
        {
            if (!m_exData)
            {
                void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
                m_exData = storage ? new (storage) EX_SPRITE_DATA(this) : nullptr;
            }
            else
            {
                m_exData->lifetimeRemaining = static_cast<std::uint32_t>(m_vid->lifetimeValue());
            }
        }

        Insert();
        InsertSpriteIntoCollectorForActionSwitch(this);
        ensureLinkedVidChild();
        ChangeAnimation(requestedAnimation);
        ChangeDirection(oldDirection);

        return 0;

        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CHANGE_COOR):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       spriteFildToF32(argument3));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::SetAnimationAndDirection):
        {
            ChangeAnimation(argument1);
            ChangeDirection(static_cast<unsigned char>(argument2));

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateXY):
        {
            ChangeCoor(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       m_xyz.z);

            break;
        }

        case static_cast<std::uint32_t>(InternalActionCode::ChangeCoordinateZ):
        {
            ChangeCoor(m_xyz.x,
                       m_xyz.y,
                       spriteFildToF32(argument1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_INVULNERABLE):
            m_runtimeFlags = (m_runtimeFlags & ~InvulnerableFlag) |
                (argument1 != 0 ? InvulnerableFlag : 0u);
            return 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_INVULNERABLE):
            return (m_runtimeFlags & InvulnerableFlag) != 0u ? 1 : 0;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_PARENT):
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(parentSprite()) & 0xFFFFFFFFu);

        case static_cast<std::uint32_t>(ActionCode::ACT_DAMAGE):
        {
            VID* const vid = m_vid;
            int damage = argument1;
            const int maxHp = vid->GetMaxHp(armyIndex());
            returnValue = 0;

            if (Hp() >= maxHp && damage < 0)
                return 1;
            if (m_currentAnimation >= 15)
                return 0;

            const int interceptScript = vid->damageInterceptScriptFunction();
            if (interceptScript >= 0)
            {
                const int interceptResult =
                    reinterpret_cast<core::Application*>(core::ApplicationOwner())->callScriptFunctionInternal(
                        interceptScript,
                        static_cast<int>(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFFFFu),
                        argument2,
                        damage);


                if (interceptResult > 0)
                    return 0;
                if (interceptResult < 0)
                    damage = spriteNeg32Wrap(interceptResult);
            }


            if (vid->maximumHp() != 0)
                ChangeHp(spriteSub32Wrap(Hp(), damage));

            if (Hp() > maxHp && damage < 0)
                setHpRaw(maxHp);

            if (damage <= 0)
                return 0;

            if (vid->declaredAnimationFrameCount(7) != 0 &&
                (m_currentAnimation == 0 || m_currentAnimation == 2))
            {
                ChangeAnimation(7);
                return 0;
            }


            (void)CreateChildFor(7, reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument2))));
            return 0;
        }


        case static_cast<std::uint32_t>(ActionCode::ACT_SET_BEHAVE):
        {

            returnValue = 0;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_REPAIR):
        {
            VID* const vid = m_vid;
            VID* const linkVid = vid->linkedVid();

            (void)deleteChildByVid(linkVid);
            ChangeHp(vid->GetMaxHp(armyIndex()));

            SPRITE* const child = childChain();
            if (child && child->Vid() == linkVid)
                (void)child->dispatchVirtualAction(ActionCode::ACT_REPAIR, 0, 0, 0);
            else
                ensureLinkedVidChild();

            return 0;
        }


        case static_cast<std::uint32_t>(ActionCode::ACT_GET_HP):
        {

            returnValue = Hp();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_HP):
        {

            int targetFrameTime = argument1;
            if (targetFrameTime == 0)
            {
                const int bucket = armyIndex();
                targetFrameTime = spriteImul32Low(
                    m_vid->GetMaxHp(bucket), argument2) / 100;
            }
            ChangeHp(targetFrameTime);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_PERCENT_HP):
        {
            const int raw255 = PercentHp();
            const int percent = raw255 * 100 / 255;

            returnValue = percent;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_GOAL):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(Goal()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_AMMO):
        case static_cast<std::uint32_t>(ActionCode::ACT_ADD_AMMO):
        {
            SPRITE* const uplink = childBacklink();
            if (!uplink)
            {

                returnValue = 0;

                break;
            }

            const int partResult = uplink->Action(static_cast<int>(opcode), argument1, argument2, argument3);

            returnValue = partResult;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_BATTLE_RANGE):
            return spriteConvertFloatToInt32(static_cast<long double>(BattleRange()));

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_GOAL_COOR):
        {
            SPRITE* const goal = new (std::nothrow) SPRITE(
                mapOwner(),
                EmptyVid,
                VECTOR(spriteFildToF32(argument1),
                       spriteFildToF32(argument2),
                       spriteFildToF32(argument3)),
                ANGLE(static_cast<unsigned char>(0)),
                nullptr);
            setGoalSprite(goal);
            return 0;
        }

        case static_cast<std::uint32_t>(InternalActionCode::GetAnimation):
            return m_currentAnimation;

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ARMY):
        {

            returnValue = armyIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ARMY):
        {
            ChangeArmy(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_INVISIBLE):
        {
            m_runtimeFlags = argument1 != 0 ? (m_runtimeFlags | DrawSuppressedFlag) : (m_runtimeFlags & ~DrawSuppressedFlag);
            if (SPRITE* child = childChain())
            {
                if (argument1 != 0)
                    child->suppressDrawRecursive();
                else
                    child->restoreDrawRecursive();
            }

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_LINK):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childChain()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_UPLINK):
        {

            returnValue = static_cast<int>(reinterpret_cast<std::uintptr_t>(childBacklink()) & 0xFFFFFFFFu);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_TIMER):
        {

            returnValue = static_cast<int>(m_actionTimer);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_TIMER):
        {
            m_actionTimer = static_cast<DWORD>(argument1);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_ZSPEED):
        {

            returnValue = spriteConvertFloatToInt32(
                static_cast<long double>(m_zSpeed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_ZSPEED):
        {
            m_zSpeed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_SPEED):
        {

            returnValue = spriteConvertFloatToInt32(
                static_cast<long double>(m_speed) * 1000.0L);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_SPEED):
        {
            m_speed = spriteFildMulF32(argument1, 0.001f);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GET_COMMAND):
        {

            returnValue = commandIndex();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_DEATH_TIMER):
        {
            const std::uint32_t requestedLifetime = static_cast<std::uint32_t>(argument1);

            if (!m_exData)
            {
                void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
                m_exData = storage ? new (storage) EX_SPRITE_DATA(this) : nullptr;
            }

            m_exData->lifetimeRemaining = requestedLifetime;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_BACKUP_COMMAND):
        {
            SPRITE* const goal = Goal();
            const std::uint32_t backupOpcode = (static_cast<std::uint32_t>(commandIndex()) << 8) + 74u;
            const int goalArg = static_cast<int>(reinterpret_cast<std::uintptr_t>(goal) & 0xFFFFFFFFu);
            const ACT command = buildCommandRecord(backupOpcode, goalArg, 0, 0);
            m_commandStack.append(command);

            if (goal)
                goal->setListReferenceCount(
                    spriteAdd32Wrap(goal->listReferenceCount(), 1));

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_GOTO_STACK):
        {
            const std::uint32_t nextCount = static_cast<std::uint32_t>(argument1) + 1u;
            m_commandStack.setCommandRecordCount(nextCount);
            if (static_cast<std::int32_t>(nextCount) >
                static_cast<std::int32_t>(m_commandStack.m_commandRecords.capacity))
            {
                m_commandStack.resize(nextCount);
            }
            m_currentFrameBegin = m_currentFrameEnd;

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_CLEAR_STACK):
            m_commandStack.clear();

            break;

        case static_cast<std::uint32_t>(ActionCode::ACT_STOP_STACK):
        {
            const ACT command = buildCommandRecord(opcode, argument1, argument2, argument3);
            m_commandStack.append(command);

            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SAVE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.saveCommandRecordsToStream(stream);

            EX_SPRITE_DATA::ItemList temporary{
                currentCommandWordListVtable(), 0u, 0u, nullptr};

            const auto destroyTemporary = [](EX_SPRITE_DATA::ItemList& list) noexcept
            {
                if (list.values)
                    ::operator delete(list.values);
                list.values = nullptr;
                list.count = 0u;
            };

            const auto copyAssign = [](EX_SPRITE_DATA::ItemList& destination,
                                             const EX_SPRITE_DATA::ItemList& source) noexcept
            {
                if (&destination == &source)
                    return;

                if (destination.values)
                    ::operator delete(destination.values);
                destination.values = nullptr;
                destination.count = 0u;
                destination.capacity = 0u;

                destination.count = source.count;
                destination.capacity = source.capacity;
                const std::size_t allocationBytes = source.capacity > 0x3FFFFFFFu
                    ? static_cast<std::size_t>(-1)
                    : static_cast<std::size_t>(source.capacity) * sizeof(std::int32_t);
                destination.values = static_cast<std::int32_t*>(
                    ::operator new(allocationBytes, std::nothrow));
                if (!destination.values)
                    fatalLogError(g_fileLogger,
                                  "!!!ERROR!!!::LIST: Not enough memory for = %i",
                                  static_cast<int>(destination.capacity));

                for (std::uint32_t i = 0; i < destination.count; ++i)
                    destination.values[i] = source.values[i];
            };

            if (m_exData)
                copyAssign(temporary, m_exData->items);

            stream->write(&temporary.count, 4u);
            stream->write(temporary.values, temporary.count << 2);
            destroyTemporary(temporary);


            STRING savedName = GetName();
            savedName.Write(stream);
            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_RESTORE):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            m_commandStack.restoreCommandRecordsFromStream(stream, this);

            if (argument2 >= 12)
            {
                EX_SPRITE_DATA::ItemList temporary{
                    currentCommandWordListVtable(), 0u, 0u, nullptr};

                stream->read(&temporary.count, 4u);
                if (static_cast<std::int32_t>(temporary.count) >
                    static_cast<std::int32_t>(temporary.capacity))
                {
                    const std::size_t allocationBytes = temporary.count > 0x3FFFFFFFu
                        ? static_cast<std::size_t>(-1)
                        : static_cast<std::size_t>(temporary.count) * sizeof(std::int32_t);
                    temporary.values = static_cast<std::int32_t*>(
                        ::operator new(allocationBytes, std::nothrow));
                    if (!temporary.values)
                        fatalLogError(g_fileLogger,
                                      "!!!ERROR!!!::LIST: Not enough memory %i",
                                      static_cast<int>(temporary.count));
                    temporary.capacity = temporary.count;
                }
                stream->read(temporary.values, temporary.count << 2);

                if (temporary.count != 0u)
                {
                    if (!m_exData)
                    {
                        void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
                        EX_SPRITE_DATA* created = nullptr;
                        if (storage)
                        {
                            created = new (storage) EX_SPRITE_DATA(this);
                        }
                        m_exData = created;
                    }
                    if (m_exData)
                    {
                    EX_SPRITE_DATA::ItemList& destination = m_exData->items;
                    if (&destination != &temporary)
                    {
                        if (destination.values)
                            ::operator delete(destination.values);
                        destination.values = nullptr;
                        destination.count = 0u;
                        destination.capacity = 0u;

                        destination.count = temporary.count;
                        destination.capacity = temporary.capacity;
                        const std::size_t allocationBytes = temporary.capacity > 0x3FFFFFFFu
                            ? static_cast<std::size_t>(-1)
                            : static_cast<std::size_t>(temporary.capacity) * sizeof(std::int32_t);
                        destination.values = static_cast<std::int32_t*>(
                            ::operator new(allocationBytes, std::nothrow));
                        if (!destination.values)
                            fatalLogError(g_fileLogger,
                                          "!!!ERROR!!!::LIST: Not enough memory for = %i",
                                          static_cast<int>(destination.capacity));
                        for (std::uint32_t i = 0; i < destination.count; ++i)
                            destination.values[i] = temporary.values[i];
                    }
                    }
                }

                if (temporary.values)
                    ::operator delete(temporary.values);
                temporary.values = nullptr;
                temporary.count = 0u;
            }
            if (argument2 >= 13)
            {
                STRING restoredName;
                restoredName.Read(stream);
                SetName(&restoredName);
            }
            break;
        }

        case static_cast<std::uint32_t>(SpriteActConst::ACT_RESTORE_OLD_MAP):
        {
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int armyBucket = 0;
            const bool hasArmyBucket = m_commandStack.restoreOldMapCommandRecordsFromStream(stream, argument2, this, &armyBucket);
            if (hasArmyBucket)
                ChangeArmy(armyBucket);

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_REMOVE):
        {
            m_runtimeFlags |= SpatialHashRemovedFlag;
            DeleteSpriteFromCollectorForActionSwitch(this);
            Remove();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_UNDO_INSERT):
        {
            m_runtimeFlags &= ~SpatialHashRemovedFlag;
            InsertSpriteIntoCollectorForActionSwitch(this);
            Insert();

            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_GAMMA):
        {


            const std::uint32_t packed = static_cast<std::uint32_t>(argument1);
            Gamma raw{};
            for (unsigned shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t encoded = (packed >> shift) & 0xFFu;
                const std::uint32_t magnitude = ((encoded & 0x80u) != 0u)
                    ? (((~encoded) & 0x7Fu) << 1)
                    : ((encoded & 0x7Fu) << 1);
                if ((encoded & 0x80u) != 0u)
                    raw.second |= (magnitude & 0xFFu) << shift;
                else
                    raw.first |= (magnitude & 0xFFu) << shift;
            }
            SetGamma(raw);
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_COMMAND):
        {


            SPRITE* const goal = reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument2)));
            SetCommand(argument1, goal);
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_SET_MAX_SPEED):
        {


            if (!m_exData)
            {
                void* const storage = ::operator new(sizeof(EX_SPRITE_DATA), std::nothrow);
                EX_SPRITE_DATA* created = nullptr;
                if (storage)
                {
                    created = new (storage) EX_SPRITE_DATA(this);
                }
                m_exData = created;
            }


            const float maxSpeed = static_cast<float>(argument1) * 0.001f;
            std::memcpy(&m_exData->maxSpeedBits, &maxSpeed, sizeof(maxSpeed));
            if (!std::isnan(m_speed) && !std::isnan(maxSpeed) && m_speed > maxSpeed)
                m_speed = maxSpeed;
            return 0;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                break;

            const float runtimeMaxSpeed = m_exData
                ? spriteFloatFromBits(m_exData->maxSpeedBits)
                : m_vid->maxSpeedValue();
            const bool windFacingAllowed =
                m_childBacklink != nullptr || runtimeMaxSpeed == 0.0f;
            if (windFacingAllowed && (m_vid->properties() & P_WIND) != 0u)
            {
                GRAPH* const graph = Graph;
                const float windSpeed = graph->windSpeed();
                if (windSpeed != 0.0f)
                {
                    const std::uint32_t deltaMs =
                        core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                    const std::uint32_t frameMs = static_cast<std::uint32_t>(
                        m_vid->frameSpeedForAnimation(m_currentAnimation));
                    const std::uint32_t stepMs = deltaMs > frameMs ? deltaMs : frameMs;
                    RotateTact(graph->WindDirectionAngle(), stepMs);
                }
            }


            SetMoveAnimation();
            break;
        }

        case static_cast<std::uint32_t>(ActionCode::ACT_PLAY_SFX):
        {
            const int requestSfx = argument1;
            PlaySFX(requestSfx);

            break;
        }

        default:
        {
            const int nvid = m_vid ? m_vid->nvid() : -1;
            LOG::ResourceError(
                "SPRITE %i", 10, "Action() have not this act",
                static_cast<int>(opcode), nvid);
            break;
        }
        }

        return returnValue;
    }


    void SPRITE::DeletePointerToSprite(SPRITE* sprite)
    {
        if (!sprite)
            return;

        if (m_childChain)
            m_childChain->DeletePointerToSprite(sprite);

        if (m_goalSprite == sprite)
        {
            if (m_currentAnimation == 8)
            {
                SPRITE* const replacement = new (std::nothrow) SPRITE(
                    mapOwner(), EmptyVid, sprite->xyz(), ANGLE(static_cast<unsigned char>(0)), nullptr);
                SetCommand(4, replacement);
            }
            else
                SetCommand(0, nullptr);
        }

        m_commandStack.clearTargetReferences(sprite);
    }

}

