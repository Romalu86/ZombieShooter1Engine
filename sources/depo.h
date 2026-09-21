#pragma once

#include "unit.h"

#include <array>
#include <cstdint>

namespace as1
{
    class DEPO : public UNIT
    {
    public:
        DEPO(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~DEPO() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void MoveTact() override;
        void AddUnitToQueue(int nvid) noexcept;
        void BuildNextUnit() noexcept;
        int ActionBuildUnit(int actionArgument1, int actionArgument2) noexcept;


    private:
        std::uint32_t m_createdEngineSequence;
        std::array<std::uint16_t, 20> m_queuedNvids;
        std::array<std::uint8_t, 0xA0> m_reservedAfterQueuedNvids;
        std::array<std::uint32_t, 20> m_queuedBuildTimes;
        std::array<std::uint8_t, 0x140> m_reservedAfterBuildTimes;
        std::array<std::uint32_t, 20> m_queuedCompletionFlags;
        std::array<std::uint8_t, 0x140> m_reservedAfterCompletionFlags;
        std::uint32_t m_activeQueueCursor;
        std::uint32_t m_queueCapacity;
        std::uint32_t m_queueCount;
    };


}
