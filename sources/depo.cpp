#include "depo.h"
#include "constant.h"
#include "core/application.h"
#include "core/log.h"
#include "engine.h"
#include "map.h"
#include "mouse.h"
#include "player.h"
#include "player_arcade.h"
#include "sprite_collector.h"
#include "vid/vid.h"
#include "win/application_win.h"

#include <array>

namespace as1
{
    namespace
    {
        constexpr std::uint32_t kDepoCoarseTimeMask = 0xFFFFC000u;

        PLAYER* playerSlotForBucket(int bucket) noexcept
        {
            return win::applicationWinInstance()->startupPlayerSlotByIndex(bucket);
        }
    }


    DEPO::DEPO(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {


        m_createdEngineSequence = 0;
        m_queuedNvids.fill(0);
        m_queuedBuildTimes.fill(0);
        m_queuedCompletionFlags.fill(0);
        m_activeQueueCursor = 0;
        m_queueCapacity = 0x0A;
        m_queueCount = 0;
    }


    DEPO::~DEPO()
    {
    }


    int DEPO::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {

        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        switch (opcode)
        {
        case 0x49:
            ActionStack()->append(buildCommandRecord(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3));
            (void)dispatchVirtualAction(ActionCode::ACT_NEXT_COMMAND, 0, 0, 0);
            return 0;

        case 0x46:
            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0x40u)
                return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            ActionStack()->append(buildCommandRecord(0x23u, static_cast<int>(m_queuedNvids[0]), 0, 0));
            SetCommand(0, nullptr);
            return 0;

        case 0x23:
            if (!mapOwner()->ValidateVid(argument1))
                return 0;
            AddUnitToQueue(argument1);
            BuildNextUnit();
            return 0;

        case 0x82:
            if (Animation() >= 0x0F)
                return 0;
            if (Animation() == 0x0D)
                ChangeAnimation(0);
            if (m_activeQueueCursor == 0u || m_queuedCompletionFlags[m_activeQueueCursor - 1u] != 0u)
            {
                if (Animation() != 0)
                    ChangeAnimation(0);
                if ((runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                    SetCommand(0, nullptr);
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[14u], reinterpret_cast<intptr_t>(this), 0);
                return 0;
            }
            if (actionTimer() != 0u)
            {
                if (Animation() != 1)
                    ChangeAnimation(1);
                return 0;
            }
            (void)ActionBuildUnit(argument1, argument2);
            return 0;

        case 0x55:
            if (argument1 > 0 && (runtimeFlags() & 2u) == 0u && argument2 != 0)
            {
                SPRITE* const target = reinterpret_cast<SPRITE*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument2)));
#if INTPTR_MAX == INT32_MAX
                if (((runtimeFlags() ^ target->runtimeFlags()) & SPRITE::ArmyBitsMask) != 0u)
                {
                    (void)core::Application::callScriptFunction(core::EvFunctionNumber[13u], reinterpret_cast<intptr_t>(this), reinterpret_cast<intptr_t>(target));
                    setRuntimeFlags(runtimeFlags() | 2u);
                }
#endif
            }
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);

        case 0x61:
        {
            const int oldBucket = armyIndex();
            PLAYER* const oldPlayer = playerSlotForBucket(oldBucket);

            using PlayerDeletePointerFn = std::intptr_t (__thiscall*)(PLAYER*, SPRITE*);
            void** const oldPlayerVtable = *reinterpret_cast<void***>(oldPlayer);
            (void)reinterpret_cast<PlayerDeletePointerFn>(oldPlayerVtable[1])(oldPlayer, this);
            ChangeArmy(static_cast<signed char>(argument1));
            const int newBucket = armyIndex();
            if (oldBucket != newBucket)
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[15u], reinterpret_cast<intptr_t>(this), 0);
            if (PLAYER* const player = playerSlotForBucket(newBucket))
            {
                using PlayerSlot10Fn = void (__thiscall*)(PLAYER*, SPRITE*);
                void** const vtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<PlayerSlot10Fn>(vtable[10])(player, this);
            }
            return 0;
        }

        case 0x51:
        case 0xC8:
        {
            (void)UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            const int bucket = armyIndex();
            if (PLAYER* const player = playerSlotForBucket(bucket))
            {
                using PlayerSlot10Fn = void (__thiscall*)(PLAYER*, SPRITE*);
                void** const vtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<PlayerSlot10Fn>(vtable[10])(player, this);
            }
            static constexpr std::array<std::int16_t, 14> kOrder = {
                5, 10, 20, 25, 80, 85, 45, 30, 35, 82, 97, 90, 75, 62
            };
            int writeIndex = 0;
            for (std::int16_t wanted : kOrder)
            {
                int scan = writeIndex;
                while (scan < static_cast<int>(NoItems()))
                {
                    if (GetItemNumber(scan) == wanted)
                    {
                        const int atWrite = GetItemNumber(writeIndex);
                        std::int32_t* const words = const_cast<std::int32_t*>(commandWordData());
                        words[writeIndex] = static_cast<std::int32_t>(wanted);
                        words[scan] = static_cast<std::int32_t>(atWrite);
                        ++writeIndex;
                    }
                    ++scan;
                }
            }
            return 0;
        }

        case 0x50:
            if (!hasCommandOpcode(0x49u))
                ActionStack()->append(buildCommandRecord(0x49u, 0, 0, 0));
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);

        default:
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
    }


    void DEPO::AddUnitToQueue(int nvid) noexcept
    {
        if (static_cast<std::int32_t>(m_queueCount) >=
            static_cast<std::int32_t>(m_queueCapacity))
            return;
        const int bucket = armyIndex();
        PLAYER* const player = playerSlotForBucket(bucket);
        VID* const vid = mapOwner()->Vid(nvid);
        const int cost = vid->GetBuildTime();
        if (static_cast<int>(player->money()) < cost)
            return;

        player->setMoney(player->money() - static_cast<DWORD>(cost));
        const std::size_t slot = m_queueCount;
        m_queuedNvids[slot] = static_cast<std::uint16_t>(nvid);
        m_queuedBuildTimes[slot] = g_baseConstants->raw[9] * static_cast<DWORD>(cost);
        m_queuedCompletionFlags[slot] = 0;
        ++m_queueCount;
    }


    void DEPO::BuildNextUnit() noexcept
    {
        if (m_activeQueueCursor != 0u)
            return;
        m_activeQueueCursor = 1u;
        while (static_cast<std::int32_t>(m_activeQueueCursor) <=
                   static_cast<std::int32_t>(m_queueCapacity) &&
               m_queuedCompletionFlags[m_activeQueueCursor - 1u] != 0u)
            ++m_activeQueueCursor;

        if (static_cast<std::int32_t>(m_activeQueueCursor) >
            static_cast<std::int32_t>(m_queueCount))
        {
            m_activeQueueCursor = 0;
            SetCommand(0, nullptr);
            ChangeAnimation(0);
            return;
        }

        SetCommand(0x10, nullptr);
        const std::size_t slot = m_activeQueueCursor - 1u;
        const std::uint32_t saved = m_queuedBuildTimes[slot];
        setActionTimer(saved != 0u
            ? saved
            : g_baseConstants->raw[9] * static_cast<DWORD>(mapOwner()->Vid(m_queuedNvids[slot])->GetBuildTime()));
    }


    int DEPO::ActionBuildUnit(int, int) noexcept
    {
        const std::size_t slot = m_activeQueueCursor - 1u;
        VID* const createVid = mapOwner()->Vid(m_queuedNvids[slot]);
        for (;;)
        {
            SPRITE* const collision = GlobalSpriteCollectorCanPlace(*mapOwner(), createVid, X(), Y(), Z());
            if (!collision || collision == mouseSprite())
                break;
            DeleteSpriteThroughVirtualDeletingDestructor(collision);
        }

        SPRITE* const created = mapOwner()->CreateSprite(createVid, xyz(), Direction(), this, false);
        if (!created)
        {
            const VID* const selfVid = Vid();
            LOG::ResourceError("SPRITE %i", 10, "Depo can't create unit",
                               static_cast<int>(m_queuedNvids[slot]),
                               selfVid ? selfVid->nvid() : -1);
            return 0;
        }

        if (created->Vid()->spriteClassId() == 21u)
            static_cast<ENGINE*>(created)->setProductionSequenceId(static_cast<int>(m_createdEngineSequence));

        --m_queueCount;
        for (std::int32_t i = static_cast<std::int32_t>(slot);
             i < static_cast<std::int32_t>(m_queueCount); ++i)
        {
            const std::size_t at = static_cast<std::size_t>(i);
            m_queuedNvids[at] = m_queuedNvids[at + 1u];
            m_queuedBuildTimes[at] = m_queuedBuildTimes[at + 1u];
            m_queuedCompletionFlags[at] = m_queuedCompletionFlags[at + 1u];
        }
        m_activeQueueCursor = 0;
        created->setRuntimeFlags(created->runtimeFlags() | 1u);
        (void)core::Application::callScriptFunction(core::EvFunctionNumber[23u], reinterpret_cast<intptr_t>(created), 0);
        created->PlaySFX(105);

        if (m_queueCount != 0u || (noCommandStackEntry() != 0u && lastCommandOpcode() != 0x49u))
        {
            BuildNextUnit();
            return 0;
        }
        if (created->Vid()->spriteClassId() == 21u)
            static_cast<ENGINE*>(created)->setProductionBatchCompletionPending(1);
        ++m_createdEngineSequence;
        return 0;
    }


    void DEPO::MoveTact()
    {

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & kDepoCoarseTimeMask) > core::PreviousWorldTimeMilliseconds())
            setRuntimeFlags(runtimeFlags() & 0xFFFFFFFDu);
        AddHpPerSecond(static_cast<int>(g_baseConstants->raw[0x38u / sizeof(DWORD)]));
    }
}
