#include "weak_controller.h"
#include "../graph.h"
#include "../graphics/angle.h"
#include "../graphics/color.h"
#include "../sprite.h"
#include "resource.h"
#include "application.h"
#include "log.h"
#include "file_logger.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>
#include <new>

#include <xmmintrin.h>

namespace as1
{
    namespace core
    {
        int g_pathResultScore = 65535;
        int g_pathSecondaryBestCost = 65535;
        R_DOT* g_pathBestNode = nullptr;
        R_MAP g_rMap;

        namespace
        {
            class R_DOTListVtableOwner
            {
            public:
                virtual void* deletingDestructor(unsigned char flags) noexcept
                {
                    auto* const owner = reinterpret_cast<std::uint32_t*>(this);
                    owner[0] = currentVtable();
                    void* const data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(owner[3]));
                    if (data)
                        ::operator delete(data);
                    owner[3] = 0u;
                    owner[1] = 0u;
                    if ((flags & 1u) != 0u)
                        ::operator delete(static_cast<void*>(this));
                    return this;
                }

                static std::uint32_t currentVtable() noexcept
                {
                    static R_DOTListVtableOwner owner;
                    return *reinterpret_cast<const std::uint32_t*>(&owner);
                }
            };
        }

        namespace
        {
            constexpr int kR_DOTMaxLinks = 6;

            int g_pathDepthLimit = 0;
            R_DOT* g_pathOriginNode = nullptr;
            int g_pathEndpointBIsVid85 = 0;
            int g_pathParity = 0;
            SPRITE* g_pathTargetSprite = nullptr;
            int g_pathEdgeStackDepth = 0;
            std::array<int, 95> g_pathEdgeStack{};
            int g_pathEndpointAIsVid85 = 0;
            int g_pathBranchScore = 0;
            int g_pathTurnPenalty = 0;
            unsigned char* g_pathOutputBuffer = nullptr;
            SPRITE* g_pathRouteOwner = nullptr;
            int g_pathActionBucket = 0;
            int g_pathMinimumDistance = 0;
            int g_pathDepth = 0;
            int g_pathCost = 0;
            int g_pathDurationCost = 0;
            std::array<unsigned char, 2500> g_pathEdgeScratch{};

            std::array<int, 100 * 100 * 30> g_pathSpatialGrid{};

            __forceinline int approxDistanceXY(int ax, int ay, int bx, int by) noexcept
            {
                auto wrappedAbs = [](std::int32_t lhs, std::int32_t rhs) noexcept -> std::int32_t {
                    const std::int32_t value = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
                    const std::uint32_t u = static_cast<std::uint32_t>(value);
                    const std::uint32_t mask = value < 0 ? 0xFFFFFFFFu : 0u;
                    return static_cast<std::int32_t>((u ^ mask) - mask);
                };
                auto div2TowardZero = [](std::int32_t value) noexcept -> std::int32_t {
                    const std::uint32_t adjusted = static_cast<std::uint32_t>(value) + (value < 0 ? 1u : 0u);
                    return static_cast<std::int32_t>(static_cast<std::int32_t>(adjusted) >> 1);
                };
                const std::int32_t dx = wrappedAbs(ax, bx);
                const std::int32_t dy = wrappedAbs(ay, by);
                const std::int32_t major = dx <= dy ? dy : dx;
                const std::int32_t minor = dx <= dy ? dx : dy;
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(major) +
                                                 static_cast<std::uint32_t>(div2TowardZero(minor)));
            }

            __forceinline float approxDistanceSpriteToDot(const SPRITE* sprite, const R_DOT* dot) noexcept
            {
                const float dx = std::fabs(sprite->X() - static_cast<float>(dot->x()));
                const float dy = std::fabs(sprite->Y() - static_cast<float>(dot->y()));


                const float result = !(dx > dy)
                    ? (dx * 0.5f + dy)
                    : (dx + dy * 0.5f);
                return result;
            }

            int routeTruncateFloatToInt32(float value) noexcept
            {
                return _mm_cvtt_ss2si(_mm_set_ss(value));
            }

            std::int32_t sub32Wrap(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
            }

            std::int32_t add32Wrap(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
            }

            std::int32_t abs32Wrapped(std::int32_t value) noexcept
            {
                const std::uint32_t u = static_cast<std::uint32_t>(value);
                const std::uint32_t mask = value < 0 ? 0xFFFFFFFFu : 0u;
                return static_cast<std::int32_t>((u ^ mask) - mask);
            }

            int routeConvertFloatToInt32(float value) noexcept
            {
                const long double d = static_cast<long double>(value);
                if (!std::isfinite(d) ||
                    d < static_cast<long double>(INT64_MIN) ||
                    d > static_cast<long double>(INT64_MAX))
                    return 0;
                const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
                return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
            }

            int routeConvertFloatToInt32(double value) noexcept
            {
                if (!std::isfinite(value) ||
                    value < static_cast<double>(INT64_MIN) ||
                    value > static_cast<double>(INT64_MAX))
                    return 0;
                const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
                return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
            }

            std::int32_t imul32Low(std::int32_t a, std::int32_t b) noexcept
            {
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) * static_cast<std::uint32_t>(b)));
            }

            constexpr std::uint32_t kR_DOTSinTableBits[256] =
            {
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
            };

            constexpr std::uint32_t kR_DOTCosTableBits[256] =
            {
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
            };

            float routeFloatFromBits(std::uint32_t bits) noexcept
            {
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            std::int32_t signedDiv2TowardZero(std::int32_t value) noexcept
            {
                const std::int32_t bias = value < 0 ? 1 : 0;
                return static_cast<std::int32_t>((value + bias) >> 1);
            }

            std::int32_t signedDiv4TowardZero(std::int32_t value) noexcept
            {

                const std::int32_t bias = value < 0 ? 3 : 0;
                return static_cast<std::int32_t>((value + bias) >> 2);
            }
        }


        R_DOT::R_DOT() noexcept
        {

            m_pathEventFlag = 0;
            m_routeClassTag = 0;
            m_pushLineValue = 0;
            m_selectedLinkIndex = -1;
            m_refCount = 0;
            m_ownerSprite = nullptr;
            m_linkCount = 0;
        }


        R_DOT::~R_DOT() noexcept
        {
            Release();
        }


        void R_MAP::AddDotToArray(int x, int y, int width, int height, int dotIndex) noexcept
        {

            int result = x;
            if (x < width && y < height && x >= 0 && y >= 0)
            {
                const std::int32_t xTimes5 = add32Wrap(x, static_cast<std::int32_t>(static_cast<std::uint32_t>(x) << 2));
                const std::int32_t xTimes25 = add32Wrap(xTimes5, static_cast<std::int32_t>(static_cast<std::uint32_t>(xTimes5) << 2));
                const std::int32_t cell = add32Wrap(y, static_cast<std::int32_t>(static_cast<std::uint32_t>(xTimes25) << 2));
                const std::uint32_t baseIndex = static_cast<std::uint32_t>(cell) * 30u;
                const std::size_t base = static_cast<std::size_t>(baseIndex);

                const std::int32_t newCount = add32Wrap(g_pathSpatialGrid[base], 1);
                g_pathSpatialGrid[base] = newCount;
                if (newCount < 30)
                {
                    const std::uint32_t storageIndex =
                        static_cast<std::uint32_t>(newCount) + static_cast<std::uint32_t>(cell) * 30u;
                    result = static_cast<std::int32_t>(storageIndex);
                    g_pathSpatialGrid[static_cast<std::size_t>(storageIndex)] = dotIndex;
                }
                else
                {
                    LOG::ResourceError("R_MAP", 10, "AddDotToArray() a[x][y][0]>=ARR_SIZE", 0);
                    result = sub32Wrap(g_pathSpatialGrid[base], 1);
                    g_pathSpatialGrid[base] = result;
                }
            }
            (void)result;
        }


        void R_MAP::CreateIntersectedDot(R_DOT* a1, R_DOT* a2,
                                                     R_DOT* a3, R_DOT* a4) noexcept
        {

            if (a1 == a3 || a1 == a4 || a2 == a3 || a2 == a4)
                return;
            if (a1->m_pathDepthByEdge[0] || a2->m_pathDepthByEdge[0] ||
                a3->m_pathDepthByEdge[0] || a4->m_pathDepthByEdge[0])
                return;

            const std::int32_t x1 = a1->m_x;
            const std::int32_t y1 = a1->m_y;
            const std::int32_t x2 = a2->m_x;
            const std::int32_t y2 = a2->m_y;
            const std::int32_t x3 = a3->m_x;
            const std::int32_t y3 = a3->m_y;
            const std::int32_t x4 = a4->m_x;
            const std::int32_t y4 = a4->m_y;

            const std::int32_t dx12 = sub32Wrap(x2, x1);
            const std::int32_t dy12 = sub32Wrap(y2, y1);
            const std::int32_t aLine1 = sub32Wrap(imul32Low(y1, dx12), imul32Low(x1, dy12));
            const std::int32_t dy34Reverse = sub32Wrap(y3, y4);
            const std::int32_t dx34 = sub32Wrap(x4, x3);
            const std::int32_t dy34 = sub32Wrap(y4, y3);
            const std::int32_t aLine2 = sub32Wrap(imul32Low(y3, dx34), imul32Low(x3, dy34));
            const std::int32_t denominator = sub32Wrap(
                imul32Low(sub32Wrap(y1, y2), dx34),
                imul32Low(dx12, dy34Reverse));
            if (denominator == 0)
                return;

            const std::int32_t xNumerator = sub32Wrap(imul32Low(aLine1, dx34), imul32Low(dx12, aLine2));

            int ix = 0;
            int iy = 0;
            const long double intersectionX = static_cast<long double>(xNumerator) / static_cast<long double>(denominator);
            long double intersectionY = 0.0L;
            if (dx12 != 0)
            {
                intersectionY = (static_cast<long double>(aLine1) -
                                 static_cast<long double>(dy12) * intersectionX) /
                                static_cast<long double>(dx12);
            }
            else if (dx34 != 0)
            {
                intersectionY = (static_cast<long double>(aLine2) -
                                 static_cast<long double>(dy34Reverse) * intersectionX) /
                                static_cast<long double>(dx34);
            }
            ix = static_cast<int>(intersectionX);
            iy = static_cast<int>(intersectionY);

            if (x1 >= x2)
            {
                if (ix < x2 || ix > x1) return;
            }
            else if (ix < x1 || ix > x2) return;

            if (y1 >= y2)
            {
                if (iy < y2 || iy > y1) return;
            }
            else if (iy < y1 || iy > y2) return;

            if (x3 >= x4)
            {
                if (ix < x4 || ix > x3) return;
            }
            else if (ix < x3 || ix > x4) return;

            if (y3 >= y4)
            {
                if (iy < y4 || iy > y3) return;
            }
            else if (iy < y3 || iy > y4) return;

            const int i12 = a1->GetLink(a2);
            const int i21 = a2->GetLink(a1);
            const int i34 = a3->GetLink(a4);
            const int i43 = a4->GetLink(a3);
            if (i12 >= 0 && i21 >= 0 && i34 >= 0 && i43 >= 0)
            {
                R_DOT::Link* const cross34 = &a3->m_links[static_cast<std::size_t>(i34)];
                const std::uint32_t cross34Raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cross34) & 0xFFFFFFFFu);
                a1->m_links[static_cast<std::size_t>(i12)].crossingLinkToken = cross34Raw;
                a2->m_links[static_cast<std::size_t>(i21)].crossingLinkToken = cross34Raw;

                R_DOT::Link* const cross12 = &a1->m_links[static_cast<std::size_t>(i12)];
                const std::uint32_t cross12Raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cross12) & 0xFFFFFFFFu);
                a3->m_links[static_cast<std::size_t>(i34)].crossingLinkToken = cross12Raw;
                a4->m_links[static_cast<std::size_t>(i43)].crossingLinkToken = cross12Raw;
            }
        }


        void R_MAP::CreateAdditionalDots() noexcept
        {
            R_MAP* const self = this;

            int xCount = 2 * (self->m_maxX / 150) + 10;
            int yCount = 2 * (self->m_maxY / 150) + 10;
            if (xCount >= 100) xCount = 100;
            if (yCount >= 100) yCount = 100;

            if (xCount > 0)
            {
                for (int x = 0; x < xCount; ++x)
                {
                    if (yCount > 0)
                    {
                        for (int y = 0; y < yCount; ++y)
                            g_pathSpatialGrid[(static_cast<std::size_t>(x) * 100u + static_cast<std::size_t>(y)) * 30u] = 0;
                    }
                }
            }

            int result = self->dotCount();
            for (int i = 0; i < result; ++i)
            {
                R_DOT* const dot = self->m_dots[static_cast<std::size_t>(i)];
                dot->m_pathDepthByEdge[0] = 0;
                const int cellX = dot->m_x / 75;
                const int cellY = dot->m_y / 75;
                self->AddDotToArray(cellX,     cellY,     xCount, yCount, i);
                self->AddDotToArray(cellX - 1, cellY,     xCount, yCount, i);
                self->AddDotToArray(cellX,     cellY - 1, xCount, yCount, i);
                self->AddDotToArray(cellX - 1, cellY - 1, xCount, yCount, i);
                result = self->dotCount();
            }

            if (xCount > 0)
            {
                result = 0;
                for (int x = 0; x < xCount; ++x)
                {
                    if (yCount > 0)
                    {
                        for (int y = 0; y < yCount; ++y)
                        {
                            const std::size_t base = (static_cast<std::size_t>(x) * 100u + static_cast<std::size_t>(y)) * 30u;
                            const int count = g_pathSpatialGrid[base];
                            if (count >= 1)
                            {
                                for (int firstSlot = 1; firstSlot < count; ++firstSlot)
                                {
                                    const int firstIndex = g_pathSpatialGrid[base + static_cast<std::size_t>(firstSlot)];
                                    for (int secondSlot = firstSlot + 1; secondSlot <= count; ++secondSlot)
                                    {
                                        const int secondIndex = g_pathSpatialGrid[base + static_cast<std::size_t>(secondSlot)];
                                        R_DOT* const first = self->m_dots[static_cast<std::size_t>(firstIndex)];
                                        R_DOT* const second = self->m_dots[static_cast<std::size_t>(secondIndex)];
                                        for (int firstEdge = 0; firstEdge < first->m_linkCount; ++firstEdge)
                                        {
                                            for (int secondEdge = 0; secondEdge < second->m_linkCount; ++secondEdge)
                                            {
                                                self->CreateIntersectedDot(first,
                                                           first->m_links[static_cast<std::size_t>(firstEdge)].target,
                                                           second,
                                                           second->m_links[static_cast<std::size_t>(secondEdge)].target);
                                            }
                                        }
                                    }
                                }
                            }
                            result += 30;
                        }
                    }
                    result = (x + 1) * 3000;
                }
            }
            (void)result;
        }


        int R_DOT::FindNewDotWithoutBusyDots() noexcept
        {
            R_DOT* const self = this;

            int result = -2;
            if (self->m_pathDepthByEdge[0] <= g_pathBranchScore)
                return result;
            self->m_pathDepthByEdge[0] = g_pathBranchScore;

            R_DOT* const origin = g_pathOriginNode;
            if (self != origin)
            {
                SPRITE* const testSprite = g_pathTargetSprite;
                if (testSprite && self->m_ownerSprite && self->m_ownerSprite->isInEngineChain(testSprite))
                {
                    g_pathBestNode = self;
                    g_pathResultScore = g_pathBranchScore;
                    return -1;
                }

                if (self->m_ownerSprite && g_pathResultScore >= 0xFFFF)
                {
                    bool replace = false;
                    if (origin)
                    {
                        if (!g_pathBestNode)
                            replace = true;
                        else
                            replace = approxDistanceXY(origin->m_x, origin->m_y,
                                                       g_pathBestNode->m_x, g_pathBestNode->m_y) >
                                      approxDistanceXY(origin->m_x, origin->m_y,
                                                       self->m_x, self->m_y);
                    }
                    else if (testSprite)
                    {
                        if (!g_pathBestNode)
                            replace = true;
                        else
                            replace = approxDistanceSpriteToDot(testSprite, g_pathBestNode) >
                                      approxDistanceSpriteToDot(testSprite, self);
                    }
                    if (replace)
                    {
                        g_pathBestNode = self;
                        result = -1;
                    }
                }

                if (g_pathBranchScore < g_pathResultScore)
                {
                    ++g_pathBranchScore;
                    for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
                    {
                        if (self->m_links[static_cast<std::size_t>(edgeIndex)].target->FindNewDotWithoutBusyDots() >= -1)
                            result = edgeIndex;
                    }
                    --g_pathBranchScore;
                }
                return result;
            }

            g_pathBestNode = self;
            g_pathResultScore = g_pathBranchScore;
            return -1;
        }


        void R_MAP::SetPushLine(int x1, int y1, int x2, int y2, int value) noexcept
        {
            R_MAP* const self = this;

            R_DOT* start = self->GetNearestDot(x1, y1);
            R_DOT* end = self->GetNearestDot(x2, y2);
            if (!start)
            {
                (void)writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't found dot in %i,%i", x1, y1);
                return;
            }
            if (!end)
            {
                (void)writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't found dot in %i,%i", x2, y2);
                return;
            }
            if (start->m_linkCount <= 0)
            {
                (void)writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't SetPushLine in %i,%i", x1, y1);
                return;
            }

            if (end == start)
            {
                std::int32_t bestDistance = 999999;
                for (int i = 0; i < start->m_linkCount; ++i)
                {
                    R_DOT* const target = start->m_links[static_cast<std::size_t>(i)].target;
                    const std::int32_t dx = sub32Wrap(target->x(), x2);
                    std::int32_t dy = sub32Wrap(target->y(), target->id());
                    dy = sub32Wrap(dy, y2);
                    const std::int32_t distance = add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy));
                    if (distance < bestDistance)
                    {
                        end = target;
                        bestDistance = distance;
                    }
                }
            }

            self->PrepareForFindDot(start, nullptr, 0u, nullptr);
            int result = end->FindNewDotWithoutBusyDots();
            if (result < 0)
            {
                (void)writeLogLine(g_fileLogger, "!!!ERROR!!!R_MAP: Can't PushLineFindDot in %i,%i", x1, y1);
                return;
            }

            R_DOT* current = start;
            for (;;)
            {
                if (current == end)
                    return;

                const int count = current->m_linkCount;
                result = 0;
                if (count > 0)
                {
                    const int currentPath = current->m_pathDepthByEdge[0];
                    int edgeIndex = 0;
                    for (; edgeIndex < count; ++edgeIndex)
                    {
                        R_DOT* const target = current->m_links[static_cast<std::size_t>(edgeIndex)].target;
                        if (currentPath == add32Wrap(target->m_pathDepthByEdge[0], 1))
                            break;
                    }
                    if (edgeIndex < count)
                    {
                        result = edgeIndex;
                        current->m_selectedLinkIndex = edgeIndex;
                        current->m_pushLineValue = static_cast<std::uint32_t>(value);
                        current = current->m_links[static_cast<std::size_t>(edgeIndex)].target;
                    }
                }

                if (!current)
                    return;
            }
        }


        int R_DOT::GetLink(R_DOT* target) noexcept
        {
            for (int index = 0; index < m_linkCount; ++index)
            {
                if (m_links[static_cast<std::size_t>(index)].target == target)
                    return index;
            }
            return -1;
        }


        void R_DOT::UnLink(R_DOT* target) noexcept
        {
            R_DOT* const self = this;

            const int count = self->m_linkCount;
            if (count <= 0)
                return;

            int found = -1;
            for (int index = 0; index < count; ++index)
            {
                if (self->m_links[static_cast<std::size_t>(index)].target == target)
                {
                    found = index;
                    break;
                }
            }
            if (found < 0)
                return;

            if (self->m_selectedLinkIndex == found)
                self->m_selectedLinkIndex = -1;

            const int newCount = count - 1;
            self->m_linkCount = newCount;
            const std::size_t foundPos = static_cast<std::size_t>(found);
            const std::size_t lastPos = static_cast<std::size_t>(newCount);
            self->m_links[foundPos] = self->m_links[lastPos];
            R_DOT::Link& moved = self->m_links[foundPos];
            moved.target->m_links[static_cast<std::size_t>(moved.reciprocalIndex)].reciprocalIndex = found;

            if (self->m_selectedLinkIndex == self->m_linkCount)
                self->m_selectedLinkIndex = found;
        }


        void R_DOT::LinkTo(R_DOT* target) noexcept
        {
            R_DOT* const self = this;


            if (!target || self->GetLink(target) >= 0)
                return;

            const std::int32_t dirX = sub32Wrap(target->x(), self->m_x);
            const std::int32_t dirY = sub32Wrap(target->y(), self->m_y);
            const std::uint32_t angleByte = static_cast<std::uint32_t>(Decart2Polar(dirX, dirY, nullptr).Int());

            const std::int32_t metricDy = sub32Wrap(self->m_y, target->y());
            const std::int32_t metricDySquare = imul32Low(metricDy, metricDy);
            const std::int32_t nineDySquare = add32Wrap(metricDySquare,
                static_cast<std::int32_t>(static_cast<std::uint32_t>(metricDySquare) * 8u));
            const std::int32_t quarterNineDySquare = signedDiv4TowardZero(nineDySquare);
            const std::int32_t metricDx = sub32Wrap(self->m_x, target->x());
            const std::int32_t metricDxSquare = imul32Low(metricDx, metricDx);
            const int distance = Sqrt(add32Wrap(quarterNineDySquare, metricDxSquare));

            R_DOT::Link link{};
            link.target = target;
            link.length = static_cast<std::uint32_t>(distance);
            link.reciprocalIndex = target->m_linkCount;
            link.crossingLinkToken = 0;
            link.facing = angleByte;

            if (self->m_linkCount >= kR_DOTMaxLinks)
            {
                LOG::Write("!!!ERROR!!!R_DOT: Too many links in %i,%i,%i",
                    self->m_x, self->m_y, self->m_id);
            }
            else
            {
                self->m_links[static_cast<std::size_t>(self->m_linkCount)] = link;
                ++self->m_linkCount;
            }

            link.target = self;
            link.reciprocalIndex = self->m_linkCount - 1;
            link.facing = (angleByte - 0x80u) & 0xFFu;

            if (target->m_linkCount >= kR_DOTMaxLinks)
            {
                LOG::Write("!!!ERROR!!!R_DOT: Too many links2 in %i,%i,%i",
                    target->x(), target->y(), target->id());
            }
            else
            {
                target->m_links[static_cast<std::size_t>(target->m_linkCount)] = link;
                ++target->m_linkCount;
            }
        }


        int R_DOT::GetPos(int x, int y, int z, int edgeIndex) noexcept
        {
            R_DOT* const self = this;
            (void)z;

            const R_DOT::Link& edge =
                self->m_links[static_cast<std::size_t>(edgeIndex)];
            const unsigned int tableIndex =
                (static_cast<unsigned char>(edge.facing) - 64u) & 255u;

            const std::int32_t dy = sub32Wrap(static_cast<std::int32_t>(y), self->m_y);
            const std::int32_t threeDy = add32Wrap(dy, add32Wrap(dy, dy));
            const std::int32_t dxValue = sub32Wrap(static_cast<std::int32_t>(x), self->m_x);


            float projectedY = static_cast<float>(threeDy);
            projectedY *= SPRITE::rawDirectionSin(static_cast<int>(tableIndex));
            projectedY *= 0.5f;
            float projectedX = static_cast<float>(dxValue);
            projectedX *= SPRITE::rawDirectionCos(static_cast<int>(tableIndex));
            return routeTruncateFloatToInt32(projectedY + projectedX);
        }


        int R_DOT::GetDistance(int x, int y, int z, int edgeIndex) noexcept
        {
            R_DOT* const self = this;

            const R_DOT::Link& edge =
                self->m_links[static_cast<std::size_t>(edgeIndex)];
            R_DOT* const target = edge.target;
            const int projected = self->GetPos(x, y, z, edgeIndex);

            if (projected < 0 || projected >= static_cast<int>(edge.length))
            {
                const std::int32_t zSum = add32Wrap(self->m_id, target->id());
                const std::int32_t zMid = signedDiv2TowardZero(zSum);
                const std::int32_t dz = add32Wrap(sub32Wrap(zMid, z), 4);

                const std::int32_t ySum = add32Wrap(self->m_y, target->y());
                const std::int32_t yMid = signedDiv2TowardZero(ySum);
                const std::int32_t dy = sub32Wrap(yMid, y);

                const std::int32_t xSum = add32Wrap(self->m_x, target->x());
                const std::int32_t xMid = signedDiv2TowardZero(xSum);
                const std::int32_t dx = sub32Wrap(xMid, x);

                const std::int32_t metric = add32Wrap(
                    add32Wrap(imul32Low(dz, dz), imul32Low(dy, dy)),
                    imul32Low(dx, dx));
                return Sqrt(metric);
            }

            const std::int32_t baseX = self->m_x;
            const std::int32_t baseY = self->m_y;
            const std::int32_t baseZ = self->m_id;
            const std::int32_t pointX = sub32Wrap(x, baseX);
            const std::int32_t pointY = sub32Wrap(y, baseY);
            const std::int32_t pointZ = sub32Wrap(z, baseZ);
            const std::int32_t edgeX = sub32Wrap(target->x(), baseX);
            const std::int32_t edgeY = sub32Wrap(target->y(), baseY);
            const std::int32_t edgeZ = sub32Wrap(target->id(), baseZ);

            const std::int32_t crossZ = sub32Wrap(imul32Low(edgeY, pointX), imul32Low(edgeX, pointY));
            const std::int32_t crossX = sub32Wrap(imul32Low(pointY, edgeZ), imul32Low(edgeY, pointZ));
            const std::int32_t crossY = sub32Wrap(imul32Low(edgeX, pointZ), imul32Low(pointX, edgeZ));

            const std::int32_t lineMetric = add32Wrap(
                add32Wrap(imul32Low(edgeZ, edgeZ), imul32Low(edgeY, edgeY)),
                imul32Low(edgeX, edgeX));
            const int lineLength = Sqrt(lineMetric);

            const std::int32_t crossMetric = add32Wrap(
                add32Wrap(imul32Low(crossY, crossY), imul32Low(crossX, crossX)),
                imul32Low(crossZ, crossZ));
            const int crossLength = Sqrt(crossMetric);
            return crossLength / lineLength;
        }


        void R_DOT::SetNearestPos(int x, int y, int z, R_POS* out) noexcept
        {
            R_DOT* const self = this;

            int result = self->m_linkCount;
            if (!result)
                return;

            int bestMetric = 65535;
            for (int outer = 0; outer < self->m_linkCount; ++outer)
            {
                R_DOT* const node =
                    self->m_links[static_cast<std::size_t>(outer)].target;
                for (int edgeIndex = 0; edgeIndex < node->linkCount(); ++edgeIndex)
                {
                    const int metric = node->GetDistance(x, y, z, edgeIndex);
                    if (metric < bestMetric)
                    {
                        bestMetric = metric;
                        out->node = node;
                        out->edgeIndex = edgeIndex;
                    }
                }
            }

            const int projected = out->node->GetPos(x, y, z, out->edgeIndex);
            out->progress = projected;
            if (projected < 0)
                out->progress = 0;

            result = static_cast<int>(reinterpret_cast<std::intptr_t>(out->node));
            int duration = 0;
            if (out->node)
                duration = static_cast<int>(
                    out->node->m_links[static_cast<std::size_t>(out->edgeIndex)].length);

            if (out->progress >= duration)
            {
                if (out->node)
                {
                    result = duration - 1;
                    out->progress = result;
                }
                else
                {
                    result = -1;
                    out->progress = -1;
                }
            }
            (void)result;
        }


        int R_DOT::GetLink(ANGLE direct) noexcept
        {
            int bestIndex = 0;
            if (m_linkCount <= 1)
                return bestIndex;

            unsigned char currentFacing = static_cast<unsigned char>(m_links[0].facing);
            unsigned char bestDeltaA = static_cast<unsigned char>(direct.value - currentFacing);
            unsigned char bestDeltaB = static_cast<unsigned char>(currentFacing - direct.value);
            unsigned char bestDelta = bestDeltaA < bestDeltaB ? bestDeltaA : bestDeltaB;

            for (int index = 1; index < m_linkCount; ++index)
            {
                currentFacing = static_cast<unsigned char>(m_links[static_cast<std::size_t>(index)].facing);
                const unsigned char deltaA = static_cast<unsigned char>(direct.value - currentFacing);
                const unsigned char deltaB = static_cast<unsigned char>(currentFacing - direct.value);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta < bestDelta)
                {
                    bestDelta = delta;
                    bestIndex = index;
                }
            }
            return bestIndex;
        }


        void R_DOT::SetIfIsBetter(int len, int noStep, int unused, int* out) noexcept
        {
            g_pathBestNode = this;
            g_pathResultScore = noStep;
            *out = -1;
            if (g_pathOutputBuffer && len < 2500)
            {
                if (g_pathRouteOwner)
                    g_pathRouteOwner->setPathBufferSize(len);
                std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), static_cast<unsigned int>(len));
            }
            (void)unused;
        }


        int R_DOT::CanEnginePassTo(int edgeIndex, SPRITE* routeOwner) noexcept
        {
            R_DOT* const self = this;
            if (edgeIndex < 0 || edgeIndex >= self->m_linkCount)
                return 0;

            const R_DOT::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
            if (routeOwner)
            {
                const int routeBucket = static_cast<int>(((routeOwner->runtimeFlags() >> 12) & 3u) + 4u);
                if (static_cast<int>(self->m_routeClassTag) == routeBucket &&
                    static_cast<int>(edge.target->m_routeClassTag) == static_cast<int>(self->m_routeClassTag))
                    return 0;

                SPRITE* const edgeOwner = edge.target->m_ownerSprite;
                if (edgeOwner && !routeOwner->isInEngineChain(edgeOwner))
                {
                    if (g_pathActionBucket == 26 && edgeOwner->isInEngineChain(g_pathTargetSprite))
                        return 0;

                    R_DOT* ownerC8Target = nullptr;
                    R_DOT* const ownerC8 = edgeOwner->primaryPathNode();
                    const int ownerC8Index = edgeOwner->primaryPathEdgeIndex();
                    if (ownerC8)
                        ownerC8Target = ownerC8->m_links[static_cast<std::size_t>(ownerC8Index)].target;

                    if ((std::fabs(static_cast<double>(edgeOwner->Speed())) < 0.03 || ownerC8Target == self) &&
                        (!edgeOwner->Goal() || edgeOwner->Goal() != routeOwner->Goal()) &&
                        (edgeOwner->engineCommandArgument0Value() == 0 || edgeOwner->engineCommandArgument0Value() != routeOwner->engineCommandArgument0Value()))
                        return 0;
                }
            }

            return edge.target->m_selectedLinkIndex != edge.reciprocalIndex ? 1 : 0;
        }


        int R_DOT::SearchFacingLimitedPath(int depth, R_DOT* excludedA, R_DOT* excludedB,
                                                    SPRITE* routeOwner, unsigned char facing) noexcept
        {
            R_DOT* const self = this;
            if (!depth)
                return 1;

            for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
            {
                const R_DOT::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
                if (edge.target == excludedA || edge.target == excludedB || !routeOwner)
                    continue;
                if (!self->CanEnginePassTo(edgeIndex, routeOwner))
                    continue;
                if (!edge.target->CanEnginePassTo(edge.reciprocalIndex, routeOwner))
                    continue;

                const unsigned char edgeFacing = static_cast<unsigned char>(edge.facing);
                const unsigned char deltaA = static_cast<unsigned char>(facing - edgeFacing);
                const unsigned char deltaB = static_cast<unsigned char>(edgeFacing - facing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u)
                    continue;

                if (g_pathEdgeStackDepth >= 95)
                {
                    LOG::ResourceError("R_DOT %i,%i", 10, "dots_num is large",
                                       g_pathEdgeStackDepth, self->m_x, self->m_y);
                }
                else
                {
                    g_pathEdgeStack[static_cast<std::size_t>(g_pathEdgeStackDepth++)] = edgeIndex;
                }

                if (edge.target->SearchFacingLimitedPath(depth - 1, self, excludedB, routeOwner, edgeFacing))
                    return 1;
                --g_pathEdgeStackDepth;
            }
            return 0;
        }


        int R_DOT::FindNewDot(int incomingEdgeIndex, ANGLE direct) noexcept
        {
            const int incomingFacing = static_cast<int>(direct.value);
            R_DOT* const self = this;
            const int savedPathParity = g_pathParity;
            int resultIndex = -2;
            if (g_pathDepth >= g_pathDepthLimit)
            {
                g_pathParity = savedPathParity;
                return resultIndex;
            }

            if (incomingEdgeIndex >= 0)
            {
                self->m_pathDepthByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathDepth;
                self->m_pathCostByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathCost;
                self->m_pathDurationByEdge[static_cast<std::size_t>(incomingEdgeIndex)] = g_pathDurationCost;
            }

            if (self == g_pathOriginNode)
            {
                self->SetIfIsBetter(g_pathDepth, g_pathCost, g_pathMinimumDistance, &resultIndex);
            }
            else
            {
                if (g_pathTargetSprite)
                {
                    SPRITE* const owner = self->m_ownerSprite;
                    if (owner && owner->isInEngineChain(g_pathTargetSprite))
                    {
                        if (g_pathActionBucket != 26)
                        {
                            self->SetIfIsBetter(g_pathDepth, g_pathCost, g_pathMinimumDistance, &resultIndex);
                        }
                        else if (incomingEdgeIndex >= 0)
                        {
                            const R_DOT::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                            bool blocked = owner->engineChainPrevious() != nullptr;
                            R_DOT* c8Target = nullptr;
                            if (owner->primaryPathNode())
                                c8Target = owner->primaryPathNode()->m_links[static_cast<std::size_t>(owner->primaryPathEdgeIndex())].target;
                            if (!blocked && c8Target != incoming.target && c8Target != self)
                                blocked = true;
                            if (blocked)
                            {
                                if (owner->engineChainNext())
                                {
                                    g_pathParity = savedPathParity;
                                    return -2;
                                }
                                R_DOT* d8Target = nullptr;
                                if (owner->secondaryPathNode())
                                    d8Target = owner->secondaryPathNode()->m_links[static_cast<std::size_t>(owner->secondaryPathEdgeIndex())].target;
                                if (d8Target != incoming.target && d8Target != self)
                                {
                                    g_pathParity = savedPathParity;
                                    return -2;
                                }
                            }
                            self->SetIfIsBetter(g_pathDepth, g_pathCost, g_pathMinimumDistance, &resultIndex);
                        }
                    }
                }

                if ((g_pathActionBucket == 28 || g_pathActionBucket == 29) && g_pathTargetSprite &&
                    (!self->m_ownerSprite || (g_pathRouteOwner && g_pathRouteOwner->isInEngineChain(self->m_ownerSprite))))
                {
                    const int currentDistance = routeTruncateFloatToInt32(
                        approxDistanceSpriteToDot(g_pathTargetSprite, self));
                    if (currentDistance <= g_pathMinimumDistance && g_pathResultScore > g_pathCost)
                    {


                        if (g_pathBestNode)
                        {
                            volatile float unusedBestDistance =
                                approxDistanceSpriteToDot(g_pathTargetSprite, g_pathBestNode);
                            (void)unusedBestDistance;
                        }
                        self->SetIfIsBetter(g_pathDepth, g_pathCost, g_pathMinimumDistance, &resultIndex);
                    }
                }
            }

            if (g_pathResultScore >= 65535)
            {
                if (g_pathOriginNode)
                {
                    bool keepBest = false;
                    if (g_pathBestNode)
                    {
                        const int oldDistance = approxDistanceXY(g_pathOriginNode->m_x, g_pathOriginNode->m_y,
                                                                 g_pathBestNode->m_x, g_pathBestNode->m_y);
                        const int newDistance = approxDistanceXY(g_pathOriginNode->m_x, g_pathOriginNode->m_y,
                                                                 self->m_x, self->m_y);
                        keepBest = oldDistance <= newDistance &&
                                   (g_pathBestNode != self || g_pathSecondaryBestCost <= g_pathCost);
                    }
                    if (!keepBest)
                    {
                        g_pathBestNode = self;
                        g_pathSecondaryBestCost = g_pathCost;
                        if (g_pathOutputBuffer && g_pathDepth < 2500)
                        {
                            if (g_pathRouteOwner)
                                g_pathRouteOwner->setPathBufferSize(g_pathDepth);
                            std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), static_cast<std::size_t>(g_pathDepth));
                        }
                        resultIndex = -1;
                    }
                }

                if (g_pathTargetSprite)
                {
                    bool replaceBest = g_pathBestNode == nullptr;
                    if (g_pathBestNode)
                    {
                        const float oldDistance = approxDistanceSpriteToDot(g_pathTargetSprite, g_pathBestNode);
                        const float newDistance = approxDistanceSpriteToDot(g_pathTargetSprite, self);
                        replaceBest = oldDistance > newDistance ||
                                      (g_pathBestNode == self && g_pathSecondaryBestCost > g_pathCost);
                    }
                    if (replaceBest)
                    {
                        g_pathBestNode = self;
                        g_pathSecondaryBestCost = g_pathCost;
                        if (g_pathOutputBuffer && g_pathDepth < 2500)
                        {
                            if (g_pathRouteOwner)
                                g_pathRouteOwner->setPathBufferSize(g_pathDepth);
                            std::memcpy(g_pathOutputBuffer, g_pathEdgeScratch.data(), static_cast<std::size_t>(g_pathDepth));
                        }
                        resultIndex = -1;
                    }
                }
            }

            int incomingPass = 1;
            if (incomingEdgeIndex >= 0 && g_pathRouteOwner)
            {
                const R_DOT::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                incomingPass = incoming.target->CanEnginePassTo(incoming.reciprocalIndex, g_pathRouteOwner);
            }

            if (g_pathCost < g_pathResultScore)
            {
                ++g_pathDepth;
                ++g_pathCost;
                for (int edgeIndex = 0; edgeIndex < self->m_linkCount; ++edgeIndex)
                {
                    if (!incomingPass && !(g_pathDepth == 1 && incomingEdgeIndex == edgeIndex))
                        continue;
                    if (incomingEdgeIndex == edgeIndex && g_pathDepth >= 3)
                        continue;

                    const R_DOT::Link& edge = self->m_links[static_cast<std::size_t>(edgeIndex)];
                    if (incomingEdgeIndex >= 0 &&
                        edge.target->m_pathCostByEdge[static_cast<std::size_t>(edge.reciprocalIndex)] <= g_pathCost)
                        continue;

                    int addedBudget = 0;
                    g_pathParity = savedPathParity;
                    int facingPass = 1;
                    if (incomingEdgeIndex >= 0 && incomingFacing >= 0)
                    {
                        const unsigned char edgeFacing = static_cast<unsigned char>(edge.facing);
                        const unsigned char deltaA = static_cast<unsigned char>(incomingFacing - edgeFacing);
                        const unsigned char deltaB = static_cast<unsigned char>(edgeFacing - incomingFacing);
                        const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                        if (delta > 31u)
                        {
                            if ((g_pathDepth > 1 || !g_pathRouteOwner || g_pathRouteOwner->Speed() != 0.0f) &&
                                incomingEdgeIndex != edgeIndex)
                            {
                                if (g_pathTurnPenalty)
                                {
                                    g_pathEdgeStackDepth = 0;
                                    const R_DOT::Link& incoming = self->m_links[static_cast<std::size_t>(incomingEdgeIndex)];
                                    facingPass = self->SearchFacingLimitedPath(g_pathTurnPenalty, incoming.target, edge.target,
                                                          g_pathRouteOwner, static_cast<unsigned char>(incomingFacing));
                                }
                                addedBudget = 1;
                            }
                            g_pathParity ^= 1u;
                        }
                    }

                    g_pathDurationCost += static_cast<int>(edge.length);
                    if (addedBudget)
                        g_pathCost += g_pathTurnPenalty;

                    int constraintPass = 1;
                    if (edge.crossingLinkToken != 0u)
                    {
                        struct RawConstraintPath { R_DOT* node; int pad04; int edgeIndex08; };
                        const RawConstraintPath* const constraint = reinterpret_cast<const RawConstraintPath*>(static_cast<std::uintptr_t>(edge.crossingLinkToken));
                        R_DOT* const constraintNode = constraint->node;
                        R_DOT* const constraintTarget =
                            constraintNode->m_links[static_cast<std::size_t>(constraint->edgeIndex08)].target;
                        SPRITE* const constraintOwner = constraintNode->m_ownerSprite;
                        SPRITE* const targetOwner = constraintTarget->m_ownerSprite;
                        if ((constraintOwner && std::fabs(static_cast<double>(constraintOwner->Speed())) < 0.03 &&
                             !constraintOwner->isInEngineChain(g_pathRouteOwner)) ||
                            (targetOwner && std::fabs(static_cast<double>(targetOwner->Speed())) < 0.03 &&
                             !targetOwner->isInEngineChain(g_pathRouteOwner)))
                            constraintPass = 0;
                    }

                    if (facingPass && constraintPass)
                    {
                        if (g_pathDepth - 1 < 2500)
                            g_pathEdgeScratch[static_cast<std::size_t>(g_pathDepth - 1)] = static_cast<unsigned char>(edgeIndex);
                        if (edge.target->FindNewDot(edge.reciprocalIndex, ANGLE(static_cast<unsigned char>(edge.facing))) >= -1)
                            resultIndex = edgeIndex;
                    }

                    g_pathDurationCost -= static_cast<int>(edge.length);
                    if (addedBudget)
                        g_pathCost -= g_pathTurnPenalty;
                }
                --g_pathDepth;
                --g_pathCost;
            }

            g_pathParity = savedPathParity;
            return resultIndex;
        }


        int R_POS::DoStep(R_DOT* firstNode, SPRITE* secondSprite, SPRITE* routeOwner) noexcept
        {
            R_POS* const self = this;
            R_DOT* const oldNode = self->node;
            unsigned char oldFacing = 0;
            if (oldNode)
                oldFacing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].facing);

            int duration = 0;
            if (oldNode)
                duration = static_cast<int>(oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].length);
            if (self->progress > duration)
                self->progress -= duration;

            self->node = oldNode
                ? oldNode->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                : nullptr;

            if (firstNode || secondSprite)
            {
                g_rMap.PrepareForFindDot(firstNode, secondSprite,
                    static_cast<unsigned int>((routeOwner->runtimeFlags() >> 2) & 31u), routeOwner);


                const float dx = firstNode
                    ? std::fabs(routeOwner->X() - static_cast<float>(firstNode->m_x))
                    : std::fabs(routeOwner->X() - secondSprite->X());
                const float dy = firstNode
                    ? std::fabs(routeOwner->Y() - static_cast<float>(firstNode->m_y))
                    : std::fabs(routeOwner->Y() - secondSprite->Y());
                const float approx = !(dx > dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                g_pathDepthLimit = routeTruncateFloatToInt32(approx / 10.0f);

                const int oldToNew = oldNode->GetLink(self->node);
                const unsigned char facing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(oldToNew)].facing);
                const int newToOld = self->node->GetLink(oldNode);
                self->edgeIndex = self->node->FindNewDot(newToOld, ANGLE(facing));

                if (g_pathResultScore == 65535)
                {
                    g_rMap.PrepareForFindDot(firstNode, secondSprite,
                        static_cast<unsigned int>((routeOwner->runtimeFlags() >> 2) & 31u), routeOwner);
                    const int retryOldToNew = oldNode->GetLink(self->node);
                    const unsigned char retryFacing = static_cast<unsigned char>(oldNode->m_links[static_cast<std::size_t>(retryOldToNew)].facing);
                    const int retryNewToOld = self->node->GetLink(oldNode);
                    self->edgeIndex = self->node->FindNewDot(retryNewToOld, ANGLE(retryFacing));
                }
            }
            else
            {
                self->edgeIndex = -1;
            }

            if (self->edgeIndex < 0)
            {
                self->edgeIndex = self->node->GetLink(ANGLE(oldFacing));
            }
            else
            {
                R_DOT* const currentNode = self->node;
                const unsigned char selectedFacing = currentNode
                    ? static_cast<unsigned char>(currentNode->m_links[static_cast<std::size_t>(self->edgeIndex)].facing)
                    : 0;
                const unsigned char deltaA = static_cast<unsigned char>(oldFacing - selectedFacing);
                const unsigned char deltaB = static_cast<unsigned char>(selectedFacing - oldFacing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u)
                {
                    R_DOT* const excludedB = currentNode
                        ? currentNode->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                        : nullptr;
                    self->edgeIndex = currentNode->GetLink(ANGLE(oldFacing));
                    if (g_pathTurnPenalty)
                    {
                        g_pathEdgeStackDepth = 0;
                        if (currentNode->SearchFacingLimitedPath(g_pathTurnPenalty, oldNode, excludedB, routeOwner, oldFacing))
                            self->edgeIndex = g_pathEdgeStack[0];
                        else
                            g_pathResultScore = 65535;
                    }
                    if (routeOwner && g_pathBestNode && routeOwner->pathBufferReachesSecondaryTarget(currentNode))
                        g_pathResultScore = -g_pathResultScore;
                }
            }

            int selectedDuration = 0;
            if (self->node)
                selectedDuration = static_cast<int>(self->node->m_links[static_cast<std::size_t>(self->edgeIndex)].length);
            if (self->progress > selectedDuration)
                self->progress = self->node ? selectedDuration : 0;
            return g_pathResultScore;
        }


        int R_POS::NoStepToTarget(R_DOT* firstNode, SPRITE* secondSprite,
                                         unsigned int actionBucket, SPRITE* routeOwner) noexcept
        {
            R_POS* const self = this;
            if (!routeOwner)
                return 65536;
            if (!firstNode && !secondSprite)
                return 65536;

            g_rMap.PrepareForFindDot(firstNode, secondSprite,
                actionBucket, routeOwner);
            R_DOT* const node = self->node;
            const unsigned char facing = node
                ? static_cast<unsigned char>(node->m_links[static_cast<std::size_t>(self->edgeIndex)].facing)
                : 0;
            R_DOT* const nextNode = node
                ? node->m_links[static_cast<std::size_t>(self->edgeIndex)].target
                : nullptr;
            const int reciprocal = nextNode->GetLink(node);
            const int selected = nextNode->FindNewDot(reciprocal, ANGLE(facing));
            if (selected >= 0)
            {
                const unsigned char selectedFacing = static_cast<unsigned char>(nextNode->m_links[static_cast<std::size_t>(selected)].facing);
                const unsigned char deltaA = static_cast<unsigned char>(facing - selectedFacing);
                const unsigned char deltaB = static_cast<unsigned char>(selectedFacing - facing);
                const unsigned char delta = deltaA < deltaB ? deltaA : deltaB;
                if (delta > 31u && g_pathBestNode && routeOwner->pathBufferReachesSecondaryTarget(nextNode))
                    g_pathResultScore = -g_pathResultScore;
            }
            return g_pathResultScore;
        }

        namespace
        {
#pragma pack(push, 4)
            struct R_DOTPointerListStorage
            {
                std::uint32_t vtableToken;
                std::int32_t count;
                std::int32_t capacity;
                void* data;
            };
#pragma pack(pop)

        }

        namespace
        {
            __forceinline
            std::uint32_t currentR_DOTPointerListVtable() noexcept
            {
                return R_DOTListVtableOwner::currentVtable();
            }
        }

        R_MAP::R_MAP() noexcept
            : m_minX(10000),
              m_minY(10000),
              m_maxX(0),
              m_maxY(0),
              m_dotListOwnerVtableToken(currentR_DOTPointerListVtable()),
              m_dotCount(0),
              m_dotCapacity(0),
              m_dots(nullptr)
        {


        }

        R_MAP::~R_MAP() noexcept
        {

            (void)reinterpret_cast<R_DOTListVtableOwner*>(
                &m_dotListOwnerVtableToken)->deletingDestructor(0u);
        }


        void R_MAP::PrepareForFindDot(R_DOT* goalDot, SPRITE* target,
                                                  unsigned int command, SPRITE* engine) noexcept
        {
            (void)goalDot;
            (void)target;
            (void)command;
            (void)engine;
        }


        ANGLE R_POS::Direct() const noexcept
        {
            if (!node)
                return ANGLE(static_cast<unsigned char>(0));
            return ANGLE(static_cast<unsigned char>(
                node->links()[static_cast<std::size_t>(edgeIndex)].facing));
        }


        R_DOT* R_POS::Dot2() const noexcept
        {
            if (!node)
                return nullptr;
            return node->links()[static_cast<std::size_t>(edgeIndex)].target;
        }


        void R_POS::Write(RESOURCE* resource) noexcept
        {
            R_POS* const self = this;


            R_DOT* const node = self->node;
            std::int16_t value = static_cast<std::int16_t>(node->x());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(node->y());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(node->id());
            resource->write(&value, 2);

            R_DOT* const target = node->links()[static_cast<std::size_t>(self->edgeIndex)].target;
            value = static_cast<std::int16_t>(target->x());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(target->y());
            resource->write(&value, 2);
            value = static_cast<std::int16_t>(target->id());
            resource->write(&value, 2);

            std::uint32_t fixedProgress = static_cast<std::uint32_t>(self->progress) << 16;
            (void)resource->write(&fixedProgress, 4);
        }


        void R_POS::Read(RESOURCE* resource) noexcept
        {
            R_POS* const self = this;


            std::int16_t x = 0;
            std::int16_t y = 0;
            std::int16_t id = 0;
            resource->read(&x, 2);
            resource->read(&y, 2);
            resource->read(&id, 2);
            self->node = g_rMap.FindDot(x, y, id);

            std::int16_t targetX = 0;
            std::int16_t targetY = 0;
            std::int16_t targetId = 0;
            resource->read(&targetX, 2);
            resource->read(&targetY, 2);
            resource->read(&targetId, 2);
            if (self->node)
            {
                R_DOT* const target = g_rMap.FindDot(targetX, targetY, targetId);
                self->edgeIndex = self->node->GetLink(target);
            }
            if (self->edgeIndex < 0)
                LOG::Write("!!!ERROR!!!RAIL: Read error");

            resource->read(&self->progress, 4);
            self->progress >>= 16;
            (void)self->progress;
        }


        R_DOT* R_MAP::FindDot(int x, int y, int id) noexcept
        {
            R_MAP* const self = this;

            const int count = self->dotCount();
            for (int index = 0; index < count; ++index)
            {
                R_DOT* dot = self->m_dots[static_cast<std::size_t>(index)];
                const std::int32_t dx = sub32Wrap(dot->x(), x);
                if (abs32Wrapped(dx) > 1)
                    continue;

                const std::int32_t dy = sub32Wrap(dot->y(), y);
                if (abs32Wrapped(dy) > 1)
                    continue;

                if (dot->m_id == id)
                    return dot;
            }
            return nullptr;
        }


        R_DOT* R_MAP::CreateDot(float x, float y, float id)
        {
            R_MAP* const self = this;

            const int ix = routeConvertFloatToInt32(x);
            const int iy = routeConvertFloatToInt32(y);
            const int iid = routeConvertFloatToInt32(id);

            if (R_DOT* existing = self->FindDot(ix, iy, iid))
            {
                existing->m_refCount += 1;
                return existing;
            }

            void* const rawDot = ::operator new(sizeof(R_DOT), std::nothrow);
            if (!rawDot)
                fatalLogError(g_fileLogger, "!!!R_MAP::CreateDot- Not enough memory");
            R_DOT* const dot = new (rawDot) R_DOT();
            dot->m_x = ix;
            dot->m_y = iy;
            dot->m_id = iid;

            bool alreadyInList = false;
            for (int index = self->dotCount() - 1; index >= 0; --index)
            {
                if (self->m_dots[index] == dot)
                {
                    alreadyInList = true;
                    break;
                }
            }
            if (!alreadyInList)
            {
                if (self->dotCount() >= self->m_dotCapacity)
                {
                    const int oldCapacity = self->m_dotCapacity;
                    const int nextCapacity = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(oldCapacity) * 2u + 4u);
                    if (nextCapacity > oldCapacity)
                    {
                        R_DOT** const oldList = self->m_dots;
                        const std::uint32_t allocationBytes = static_cast<std::uint32_t>(nextCapacity) * 4u;
                        R_DOT** const newList = static_cast<R_DOT**>(
                            ::operator new(static_cast<std::size_t>(allocationBytes), std::nothrow));
                        if (!newList)
                            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);
                        self->m_dots = newList;
                        for (int index = 0; index < oldCapacity; ++index)
                            newList[index] = oldList[index];
                        ::operator delete(oldList);
                        self->m_dotCapacity = nextCapacity;
                    }
                }
                self->m_dots[self->m_dotCount++] = dot;
            }

            if (dot->x() > self->m_maxX)
                self->m_maxX = dot->x();
            else if (dot->x() < self->m_minX)
                self->m_minX = dot->x();

            if (dot->y() > self->m_maxY)
                self->m_maxY = dot->y();
            else if (dot->y() < self->m_minY)
                self->m_minY = dot->y();

            dot->m_refCount += 1;
            return dot;
        }


        R_DOT* R_MAP::GetNearestDot(int x, int y) noexcept
        {
            R_DOT* result = nullptr;
            std::int32_t best = 0x0FFFFFFF;
            const int count = dotCount();
            for (int index = 0; index < count; ++index)
            {
                R_DOT* const dot = m_dots[static_cast<std::size_t>(index)];
                if (dot->linkCount() <= 0)
                    continue;

                const std::int32_t dx = sub32Wrap(dot->x(), x);
                const std::int32_t dy = sub32Wrap(sub32Wrap(dot->y(), dot->id()), y);
                const std::int32_t metric = add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy));
                if (metric < best)
                {
                    best = metric;
                    result = dot;
                }
            }
            return result;
        }


        R_DOT* R_MAP::GetNearestDot(int x, int y, int z) noexcept
        {
            R_MAP* const self = this;

            R_DOT* result = nullptr;
            std::int32_t best = 0x0FFFFFFF;
            const int count = self->dotCount();
            for (int index = 0; index < count; ++index)
            {
                R_DOT* const dot = self->m_dots[static_cast<std::size_t>(index)];
                if (dot->linkCount() <= 0)
                    continue;

                const std::int32_t dx = sub32Wrap(dot->x(), x);
                const std::int32_t dy = sub32Wrap(dot->y(), y);
                const std::int32_t dz = sub32Wrap(z, dot->m_id);
                const std::int32_t metric = add32Wrap(add32Wrap(imul32Low(dx, dx), imul32Low(dy, dy)), imul32Low(dz, dz));
                if (metric < best)
                {
                    best = metric;
                    result = dot;
                }
            }
            return result;
        }


        void R_DOT::Release() noexcept
        {
            R_DOT* const self = this;

            if (self->m_refCount == 0)
                return;

            --self->m_refCount;
            if (self->m_refCount != 0)
                return;

            self->m_ownerSprite = nullptr;
            const int count = self->m_linkCount;
            for (int index = 0; index < count; ++index)
            {
                R_DOT* const other = self->m_links[static_cast<std::size_t>(index)].target;
                other->UnLink(self);
            }
            self->m_linkCount = 0;

            R_MAP& mapOwner = g_rMap;
            int registryIndex = mapOwner.m_dotCount;
            if (registryIndex != 0)
            {
                while (registryIndex != 0)
                {
                    --registryIndex;
                    if (mapOwner.m_dots[registryIndex] == self)
                    {
                        if (registryIndex >= 0 && registryIndex < mapOwner.m_dotCount)
                            mapOwner.m_dots[registryIndex] =
                                mapOwner.m_dots[--mapOwner.m_dotCount];
                        break;
                    }
                }
            }

            self->~R_DOT();
            ::operator delete(self);
        }


        void R_MAP::DebugDraw() noexcept
        {
            R_MAP* const self = this;


            GRAPH* const graph = Graph;
            const auto& drawState = as1::core::GlobalApplicationDrawDispatcherState();

            auto pack16 = [](DWORD color) noexcept -> std::uint16_t {
                return static_cast<std::uint16_t>(
                    (static_cast<unsigned char>(color) >> 3u) |
                    (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
                    (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
            };
            auto expand16 = [](std::uint16_t color) noexcept -> DWORD {
                return static_cast<DWORD>(
                    8u * (color & 0x1Fu) |
                    ((static_cast<DWORD>(color) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                    ((static_cast<DWORD>(color) << (16u - g_color16RedShift)) & 0x00FF0000u));
            };

            const std::uint16_t ownerColor16 = pack16(g_colorRed.color);
            int result = self->dotCount();
            for (int nodeIndex = 0; nodeIndex < result; ++nodeIndex)
            {
                R_DOT* const node = self->dots()[static_cast<std::size_t>(nodeIndex)];
                const float x = static_cast<float>(node->x()) - drawState.cameraShiftX();
                const float y = static_cast<float>(node->y() - node->id()) - drawState.cameraShiftY();
                if (x > -50.0f && y > -50.0f && x < 1000.0f && y < 1000.0f)
                {
                    graph->Line(x - 1.0f, y, x - 1.0f, y + static_cast<float>(node->id()), g_colorBlue.color);
                    graph->Line(x - 2.0f, y + static_cast<float>(node->id()), x, y + static_cast<float>(node->id()), g_colorBlue.color);

                    const std::uint16_t nodeColor16 = node->ownerSprite() ? ownerColor16 : pack16(g_colorWhite.color);
                    for (int edgeIndex = 0; edgeIndex < node->linkCount(); ++edgeIndex)
                    {
                        const R_DOT::Link& edge = node->links()[static_cast<std::size_t>(edgeIndex)];
                        R_DOT* const target = edge.target;
                        std::uint16_t edgeColor16 = pack16(g_colorWhite.color);
                        if (node->routeClassTag() > 3u && target->routeClassTag() > 3u)
                            edgeColor16 = pack16(g_colorBlack.color);
                        if (node->pathEventFlag() != 0u && target->pathEventFlag() != 0u)
                            edgeColor16 = ownerColor16;
                        if (node->selectedLinkIndex() == edgeIndex || target->selectedLinkIndex() == edge.reciprocalIndex)
                            edgeColor16 = pack16(0xFFFF8080u);

                        const float tx = static_cast<float>(target->x()) - drawState.cameraShiftX();
                        const float ty = static_cast<float>(target->y() - target->id()) - drawState.cameraShiftY();
                        graph->Line(x, y, tx, ty, expand16(edgeColor16));

                        if (edge.crossingLinkToken != 0u)
                        {
                            const R_DOT* const cross = reinterpret_cast<const R_DOT*>(static_cast<std::uintptr_t>(edge.crossingLinkToken));
                            graph->Line(x, y,
                                            static_cast<float>(cross->ScreenX()),
                                            static_cast<float>(cross->ScreenY()),
                                            g_colorGreen.color);
                        }
                    }

                    const DWORD nodeColor = expand16(nodeColor16);
                    graph->Line(x - 2.0f, y - 2.0f, x + 2.0f, y + 2.0f, nodeColor);
                    graph->Line(x - 2.0f, y + 2.0f, x + 2.0f, y - 2.0f, nodeColor);
                }
                result = self->dotCount();
            }
            (void)result;
        }


        float R_DOT::ScreenX() const noexcept
        {
            return static_cast<float>(x()) -
                   as1::core::GlobalApplicationDrawDispatcherState().cameraShiftX();
        }


        float R_DOT::ScreenY() const noexcept
        {
            const std::int32_t yMinusId = sub32Wrap(y(), id());
            return static_cast<float>(yMinusId) -
                   as1::core::GlobalApplicationDrawDispatcherState().cameraShiftY();
        }


    }
}
