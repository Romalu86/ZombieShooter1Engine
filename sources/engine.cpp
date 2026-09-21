#include "engine.h"
#include "base_sprite_list.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/weak_controller.h"
#include "player.h"
#include "player_arcade.h"
#include "win/application_win.h"
#include "constant.h"
#include "map.h"
#include "sprite_collector.h"
#include "vid/vid.h"
#include "graph.h"
#include "graphics/color.h"
#include "core/as_string.h"
#include "core/weak_controller.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>

namespace as1
{
    int ENGINE::globaldeleting = 0;

    namespace
    {
        constexpr std::size_t kEngineCallbackPeerFound = 4u;
        constexpr std::size_t kEngineCallbackPair = 5u;
        constexpr std::size_t kEngineCallbackNoPair = 6u;
        constexpr std::size_t kEngineCallbackWeapon = 7u;
        constexpr std::size_t kEngineCallbackDestroy = 24u;
        constexpr std::size_t kEngineCallbackMoveRange = 1u;
        constexpr std::size_t kEngineCallbackMoveFrame = 3u;
        constexpr std::size_t kEngineCallbackDamageEnemy = 10u;

        int pointerToInt(const void* ptr) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ptr)));
        }

        template <class T>
        T* intToPointer(int value) noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        int engineConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(INT64_MIN) ||
                d > static_cast<long double>(INT64_MAX))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
        }

        int engineSub32Wrap(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
        }

        int engineAdd32Wrap(int lhs, int rhs) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        float engineFildToF32(int value) noexcept
        {
            return static_cast<float>(value);
        }

        bool engineFcompEqualOrUnorderedZero(float value) noexcept
        {

            return value == 0.0f || std::isnan(value);
        }

        bool engineLessEqualOrUnordered(float lhs, float rhs) noexcept
        {

            return lhs <= rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        float engineRawConstantFloat(DWORD bits) noexcept
        {
            float value = 0.0f;

            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        bool engineSpeedExceedsStopThreshold(float speed, float threshold) noexcept
        {
            return std::isnan(speed) || std::isnan(threshold) || threshold < std::fabs(speed);
        }

        VID* engineActionApplicationVid(int index) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            VID* vid = table.count() > index ? table.slot(index) : nullptr;
            return vid ? vid : EmptyVid;
        }
    }


    ENGINE::ENGINE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, direction, parent)
    {
        setDerivedStateValue(0, derivedStateValue(0) & ~1);
        setChainPrevious(nullptr);
        setChainNext(nullptr);
        setEngineCommandReferenceOwner(nullptr);
        setEngineCommandArgument0(0);
        setEngineCommandArgument1(0);
        engineCommandArgument2Ref() = 0;
        engineAccelerationDelayRef() = 0;
        engineTargetSpeedRef() = 0.0f;
        pushLineActiveRef() = 0;
        primaryPathNodeRef() = nullptr;
        primaryPathAuxiliaryRef() = 0;
        secondaryPathNodeRef() = nullptr;
        secondaryPathAuxiliaryRef() = 0;
        setBusy(1);
        setRepairLinkHp(-1);
        setProductionBatchCompletionPending(0);
        previousPathXRef() = 0.0f;
        previousPathYRef() = 0.0f;
        previousPathZRef() = 0.0f;
        routeActionReadyRef() = 0;
        routeActionStartTimeRef() = 0;
        setProductionSequenceId(-1);
        pathBufferSizeRef() = 0;

        if ((core::ApplicationFlags() & application_flags::MapLoading) == 0u)
        {
            initializeEnginePathEndpoints();
            if (SPRITE* const child = childChain())
            {
                VID* const linkVid = vid ? vid->linkedVid() : nullptr;
                if (linkVid && child->Vid() == linkVid)
                    child->ChangeDirection(directionIndex());
            }
        }
    }

    ENGINE::~ENGINE()
    {
        const bool bulkDelete = ENGINE::globaldeleting != 0u;

        if (!bulkDelete)
            (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackDestroy],
                                                static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this))),
                                                0);

        {
            SPRITE* const ref = engineCommandReferenceOwner();
            if (ref)
            {
                if (ref == this)
                    SetCommandToTrain(0, 0, 0, 0);
                else
                {
                    (void)ref->Release();
                    setEngineCommandReferenceOwner(nullptr);
                }
            }
        }

        if (MAP* const owner = mapOwner())
        {
            if (this == owner->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex())))
                g_spriteWorkList.deleteAllSprites();
        }

        if (!bulkDelete)
            ClearDotBusy();


        SPRITE* const first = chainPrevious();
        SPRITE* const second = chainNext();
        if (first)
        {
            first->setEngineChainNext(nullptr);
            static_cast<ENGINE*>(first)->ReCalcMoveParameters();
        }
        if (second)
        {
            second->setEngineChainPrevious(nullptr);
            static_cast<ENGINE*>(second)->ReCalcMoveParameters();
        }
        if (!bulkDelete)
        {
            const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
            SPRITE* const currentFirst = chainPrevious();
            SPRITE* const currentSecond = chainNext();
            if (currentFirst && currentSecond)
            {
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackPair],
                                                    static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(currentFirst))),
                                                    static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(currentSecond))));
            }

            VID* const vid = Vid();
            if (vid->weaponFloatAt(16) - vid->weaponFloatAt(12) > 1.0f)
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackWeapon], selfArg, 0);

            if (!chainPrevious() && !chainNext())
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackNoPair], selfArg, 0);

            if (productionBatchCompletionPending() != 0)
            {
                SPRITE_COLLECTOR* const collector = GlobalSpriteCollector();
                const int count = collector->overflowCount();
                if (count != 0)
                {
                    int cursor = count - 1;
                    collector->setReverseCursor(cursor);
                    SPRITE* candidate = collector->overflowSpriteAt(cursor);
                    while (candidate)
                    {
                        VID* const candidateVid = candidate->Vid();
                        if (candidateVid->spriteClassId() == B_ENGINE &&
                            (candidate->sameArmy(*this)) &&
                            static_cast<ENGINE*>(candidate)->productionSequenceId() == productionSequenceId())
                        {
                            (void)core::Application::callScriptFunction(
                                core::EvFunctionNumber[kEngineCallbackPeerFound],
                                static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(candidate))),
                                0);
                            break;
                        }

                        const int currentCount = collector->overflowCount();
                        cursor = collector->reverseCursor();
                        if (cursor > currentCount)
                            cursor = currentCount;
                        --cursor;
                        collector->setReverseCursor(cursor);
                        if (cursor < 0)
                            break;
                        candidate = collector->overflowSpriteAt(cursor);
                    }
                }
            }
        }
    }


    int ENGINE::RepairByRepair(ENGINE* target) noexcept
    {
        if (!target)
            return 0;

        const CONSTANT* const constants = g_baseConstants;
        const int step = static_cast<int>(constants->raw[4]);
        const int negativeStep = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(step));

        const int actionResult = target->dispatchVirtualAction(ActionCode::ACT_DAMAGE, negativeStep, 0, 0);
        const bool zeroResult = (actionResult == 0);
        if (actionResult != 0)
        {
            VID* const targetVid = target->Vid();
            VID* const linkVid = targetVid->linkedVid();
            if (linkVid)
            {
                SPRITE* const child = target->childChain();
                if (!child || child->Vid() != linkVid)
                {
                    int progress = target->repairLinkHp();
                    if (progress < 0)
                        progress = 0;
                    progress = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(progress) + static_cast<std::uint32_t>(step));
                    target->setRepairLinkHp(progress);

                    const int bucket = target->armyIndex();
                    if (progress >= linkVid->GetMaxHp(bucket))
                    {
                        target->repairLinkedChildState(1);
                        setRepairLinkHp(-1);
                    }
                }
            }
        }
        return zeroResult ? 1 : 0;
    }


    void ENGINE::RepairTact() noexcept
    {
        ENGINE* best = nullptr;
        int bestMetric = 0;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
        {
            if (!sameArmy(*node))
                continue;
            const int metric = node->NeedRepairByRepair();
            if (metric > bestMetric)
            {
                bestMetric = metric;
                best = static_cast<ENGINE*>(node);
            }
        }

        if (best)
        {
            (void)RepairByRepair(best);
            if (Animation() != static_cast<int>(AnimationCode::ANI_OPEN))
                ChangeAnimation(static_cast<int>(AnimationCode::ANI_OPEN));
        }
        else if (Animation() == static_cast<int>(AnimationCode::ANI_OPEN))
        {
            ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
        }
    }


    int ENGINE::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;
        const unsigned int action = static_cast<unsigned int>(opcode) & 0xFFu;
        switch (action)
        {
        case static_cast<unsigned int>(ActionCode::ACT_REPAIR):
        {
            VID* const vid = Vid();
            VID* const link = vid->linkedVid();
            const int capacity = (link && link->hasWeaponChildDescriptor() && link->weaponCount())
                ? link->weaponRecordAmmoCapacity()
                : vid->weaponRecordAmmoCapacity();
            if (vid->nvid() != 82 || ammoCount() >= capacity)
                return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);

            const int bucket = armyIndex();
            const DWORD selfCount = vid->NoSprites(bucket);
            VID* const slot70 = engineActionApplicationVid(70);
            const DWORD slot70Count = slot70->NoSprites(bucket);
            int sameArmySum = 0;
            SPRITE_COLLECTOR* const collector = GlobalSpriteCollector();
            const int count = collector->overflowCount();
            SPRITE* const* const raw = collector->mutableOverflowList().data();
            for (int index = count - 1; index >= 0; --index)
            {
                SPRITE* const candidate = raw[index];
                if (!candidate)
                    break;
                if (candidate->Vid()->nvid() == 82 &&
                    sameArmy(*candidate))
                {
                    sameArmySum += candidate->ammoCount();
                }
                if (index > collector->overflowCount())
                    index = collector->overflowCount();
            }

            VID* const currentLink = vid->linkedVid();
            const int currentCapacity = (currentLink && currentLink->hasWeaponChildDescriptor() && currentLink->weaponCount())
                ? currentLink->weaponRecordAmmoCapacity()
                : vid->weaponRecordAmmoCapacity();
            if (static_cast<int>(slot70Count) + sameArmySum >= static_cast<int>(selfCount) * currentCapacity)
                return TERRAIN::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
        case static_cast<unsigned int>(ActionCode::ACT_UNDO_REMOVE):
            (void)SPRITE::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            ClearDotBusy();
            if (SPRITE* const prev = engineChainPrevious())
                prev->setEngineChainNext(nullptr);
            if (SPRITE* const next = engineChainNext())
                next->setEngineChainPrevious(nullptr);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_UNDO_INSERT):
            (void)SPRITE::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            SetDotBusy();
            if (SPRITE* const prev = engineChainPrevious())
                prev->setEngineChainNext(this);
            if (SPRITE* const next = engineChainNext())
                next->setEngineChainPrevious(this);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_SET_ARMY):
        {


            PLAYER* const oldPlayer = *reinterpret_cast<PLAYER**>(
                reinterpret_cast<unsigned char*>(Map) +
                core::application_layout::PlayerSlots +
                static_cast<std::size_t>(armyIndex() & 3) * sizeof(std::uint32_t));
            using PlayerDeletePointerFn = std::intptr_t (__thiscall*)(PLAYER*, SPRITE*);
            void** const playerVtable = *reinterpret_cast<void***>(oldPlayer);
            (void)reinterpret_cast<PlayerDeletePointerFn>(playerVtable[1])(oldPlayer, this);
            ChangeArmy(static_cast<signed char>(argument1));
            if (!engineFcompEqualOrUnorderedZero(Vid()->weaponFloatAt(16)))
            {

                PLAYER* const player = *reinterpret_cast<PLAYER**>(
                    reinterpret_cast<unsigned char*>(Map) +
                    core::application_layout::PlayerSlots +
                    static_cast<std::size_t>(armyIndex() & 3) * sizeof(std::uint32_t));
                using AddUnitToStateBarFn = void (__thiscall*)(PLAYER*, SPRITE*);
                void** const playerVtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<AddUnitToStateBarFn>(playerVtable[10])(player, this);
            }
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_GET_GOAL):
            return pointerToInt(engineChainHead()->Goal());

        case 0x4Au:
        {
            SetCommandToTrain((static_cast<unsigned int>(opcode) >> 8) & 0xFFu, argument1, argument2, argument3);
            SPRITE* const ref = intToPointer<SPRITE>(argument1);
            if (!ref)
                return 0;
            (void)ref->Release();
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_BACKUP_COMMAND):
        {
            const std::uint32_t commandOpcode = (static_cast<std::uint32_t>(commandIndex()) << 8) + 74u;
            const ACT command = SPRITE::buildCommandRecord(
                commandOpcode,
                pointerToInt(Goal()),
                engineCommandArgument0Ref(),
                engineCommandArgument1Ref());
            m_commandStack.append(command);
            if (SPRITE* const target = Goal())
                target->AddListReference();
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_NEXT_COMMAND):
        {
            const int animation = Animation();
            if (animation >= 15)
                return 0;
            if (engineAccelerationDelayRef() > 0)
            {
                if (animation != 3)
                    ChangeAnimation(3);
            }
            else if (engineAccelerationDelayRef() < 0)
            {
                if (animation != 1)
                    ChangeAnimation(1);
            }
            else if (m_speed == 0.0f)
            {
                if (animation != 0)
                    ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
            }
            else if (animation != 2)
            {
                ChangeAnimation(2);
            }

            VID* const vid = Vid();
            bool decisionOwner = vid->hasWeaponChildDescriptor() && vid->weaponCount();
            if (!decisionOwner)
            {
                SPRITE* const child = childChain();
                decisionOwner = child && child->Vid() == vid->linkedVid() &&
                    child->Vid()->hasWeaponChildDescriptor() && child->Vid()->weaponCount();
            }
            if (decisionOwner)
                (void)updateCombatDecision();

            if ((runtimeFlags() & SPRITE::CommandBitsMask) != 96u || vid->nvid() != 85 ||
                routeActionReadyRef() == 0 || m_speed != 0.0f)
            {
                return 0;
            }

            const std::uint32_t now = core::CurrentTimeMilliseconds();
            if (routeActionStartTimeRef() != 0)
            {
                if (now - static_cast<std::uint32_t>(routeActionStartTimeRef()) > g_baseConstants->raw[17])
                {
                    if (ammoCount() > 0)
                    {
                        core::R_DOT* const actionNode = engineCommandArgument0Node();
                        (void)mapOwner()->CreateSprite(
                            engineActionApplicationVid(595),
                            VECTOR(static_cast<float>(actionNode->x()),
                                   static_cast<float>(actionNode->y()),
                                   static_cast<float>(actionNode->id())),
                            ANGLE(static_cast<unsigned char>(0)), this, false);
                        SPRITE* const created = mapOwner()->CreateSprite(
                            engineActionApplicationVid(86), xyz(), ANGLE(directionIndex()), this, false);
                        if (created)
                        {
                            (void)created->dispatchVirtualAction(ActionCode::ACT_SET_BEHAVE, 0, 0, 0);
                            (void)dispatchVirtualAction(ActionCode::ACT_ADD_AMMO, -1, 0, 0);
                        }
                    }
                    SetCommandToTrain(0, 0, 0, 0);
                }
            }
            else
            {
                routeActionStartTimeRef() = static_cast<int>(now);
            }

            (void)mapOwner()->CreateSprite(
                engineActionApplicationVid(588), xyz(), ANGLE(static_cast<unsigned char>(0)), this, false);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_DAMAGE):
            if (argument1 > 0)
            {
                SPRITE* const head = engineChainHead();
                SPRITE* const target = intToPointer<SPRITE>(argument2);
                if ((head->runtimeFlags() & 2u) == 0u && target &&
                    !sameArmy(*target))
                {
                    (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackDamageEnemy],
                                                        pointerToInt(this), pointerToInt(target));
                    head->setRuntimeFlags(head->runtimeFlags() | 2u);
                }
            }
            if (SPRITE* const child = childChain())
            {
                VID* const childVid = child->Vid();
                if (childVid == Vid()->linkedVid() &&
                    childVid->GetMaxHp(child->armyIndex()) != 0)
                {
                    return child->dispatchVirtualAction(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
                }
            }
            return SPRITE::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);

        case static_cast<unsigned int>(ActionCode::ACT_SAVE):
        {
            RESOURCE* const resource = intToPointer<RESOURCE>(argument1);
            (void)UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            core::R_POS c8{primaryPathNodeRef(), primaryPathProgressRef(), primaryPathAuxiliaryRef(), primaryPathEdgeIndexRef()};
            core::R_POS d8{secondaryPathNodeRef(), secondaryPathProgressRef(), secondaryPathAuxiliaryRef(), secondaryPathEdgeIndexRef()};
            c8.Write(resource);
            d8.Write(resource);
            const std::uint32_t prev = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(engineChainPrevious()));
            const std::uint32_t next = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(engineChainNext()));
            resource->write(&prev, 4u);
            resource->write(&next, 4u);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_RESTORE):
        case 0xC8u:
        {
            RESOURCE* const resource = intToPointer<RESOURCE>(argument1);
            (void)UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
            if (argument2 < 6)
            {
                initializeEnginePathEndpoints();
            }
            else
            {
                core::R_POS c8{};
                core::R_POS d8{};
                c8.Read(resource);
                d8.Read(resource);
                primaryPathNodeRef() = c8.node;
                primaryPathProgressRef() = c8.progress;
                primaryPathAuxiliaryRef() = c8.auxiliary;
                primaryPathEdgeIndexRef() = c8.edgeIndex;
                secondaryPathNodeRef() = d8.node;
                secondaryPathProgressRef() = d8.progress;
                secondaryPathAuxiliaryRef() = d8.auxiliary;
                secondaryPathEdgeIndexRef() = d8.edgeIndex;

                setEngineChainPrevious(mapOwner()->ReadPointer(resource));
                setEngineChainNext(mapOwner()->ReadPointer(resource));
                SetDotBusy();

                std::uint8_t facing = 0;
                if (primaryPathNodeRef())
                    facing = primaryPathNodeRef()->links()[static_cast<std::size_t>(primaryPathEdgeIndexRef())].facing;
                const std::uint8_t direction = static_cast<std::uint8_t>(directionIndex());
                const std::uint8_t d1 = static_cast<std::uint8_t>(direction - facing);
                const std::uint8_t d2 = static_cast<std::uint8_t>(facing - direction);
                if ((d1 < d2 ? d1 : d2) > 127u)
                    setDerivedStateValue(0, derivedStateValue(0) | 1);
            }
            if (!engineFcompEqualOrUnorderedZero(Vid()->weaponFloatAt(16)))
            {

                PLAYER* const player = *reinterpret_cast<PLAYER**>(
                    reinterpret_cast<unsigned char*>(Map) +
                    core::application_layout::PlayerSlots +
                    static_cast<std::size_t>(armyIndex() & 3) * sizeof(std::uint32_t));
                using AddUnitToStateBarFn = void (__thiscall*)(PLAYER*, SPRITE*);
                void** const playerVtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<AddUnitToStateBarFn>(playerVtable[10])(player, this);
            }
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_COOR_ATTACK):
        {
            const int nvid = Vid()->nvid();
            if (nvid == 85)
            {
                core::R_DOT* const node = core::g_rMap.GetNearestDot(argument1, argument2);
                SetCommandToTrain(24, 0, pointerToInt(node), 0);
                return 0;
            }
            if (nvid != 97 || engineChainNext() || engineChainPrevious())
            {
                const float x = engineFildToF32(argument1);
                const float y = engineFildToF32(argument2);
                const float groundPlus19 = mapOwner()->GetGroundZ(VECTOR2{x, y}) + 19.0f;
                const int groundInt = engineConvertFloatToInt32(groundPlus19);
                const int helperY = engineAdd32Wrap(engineAdd32Wrap(groundInt, argument2), -19);
                SPRITE* const created = new (std::nothrow) SPRITE(
                    mapOwner(), EmptyVid,
                    VECTOR(x, engineFildToF32(helperY), groundPlus19),
                    ANGLE(static_cast<unsigned char>(0)), nullptr);
                SetCommandToTrain(29, pointerToInt(created), 0, 0);
                return 0;
            }
            core::R_DOT* const node = core::g_rMap.GetNearestDot(argument1, argument2);
            SetCommandToTrain(27, 0, pointerToInt(node), 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_MOVE):
            setEnginePathGoalFromCoordinates(engineFildToF32(argument1), engineFildToF32(argument2), 0.0f, 0, 0);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_ATTACK):
        {
            if (argument1 == 0)
                return 0;
            const int actionOpcode = Vid()->nvid() == 97 && !engineChainNext() && !engineChainPrevious() ? 27 : 28;
            SetCommandToTrain(actionOpcode, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_LINK_ENGINE):
        case static_cast<unsigned int>(ActionCode::ACT_FORCELINK_ENGINE):
        {
            SPRITE* const target = intToPointer<SPRITE>(argument1);
            if (!target || target->Vid()->spriteClassId() != 21u || isInEngineChain(target))
                return 0;
            SetCommandToTrain(26, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_CLASH_ENGINE):
        {
            SPRITE* const target = intToPointer<SPRITE>(argument1);
            if (!target || target->Vid()->spriteClassId() != 21u || isInEngineChain(target))
                return 0;
            VID* const vid = Vid();
            SPRITE* const child = childChain();
            if (vid->nvid() == 35 && child && child->Vid() == vid->linkedVid())
                (void)child->SetCommand(8, target);
            else
                SetCommandToTrain(27, argument1, 0, 0);
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_TRAIN_BEHAVE):
        {
            for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
            {
                if (argument1 == 1)
                {
                    node->setBehaviorFlags(0);
                    continue;
                }
                node->setBehaviorFlags(1);
                VID* const nodeVid = node->Vid();
                VID* const link = nodeVid->linkedVid();
                const int capacity = (link && link->hasWeaponChildDescriptor() && link->weaponCount())
                    ? link->weaponRecordAmmoCapacity()
                    : nodeVid->weaponRecordAmmoCapacity();
                if (capacity <= 5 && (argument1 == 2 || argument1 == 4))
                    node->setBehaviorFlags(0);
                if (argument1 == 4 || argument1 == 5)
                    node->setBehaviorFlags(node->behaviorFlags() | 2);
                if (argument1 == 3 || argument1 == 5)
                    node->setBehaviorFlags(node->behaviorFlags() | 0x10);
            }
            return 0;
        }

        case static_cast<unsigned int>(ActionCode::ACT_STOP):
            if (argument1 != 0)
                stopEngineChain();
            SetCommandToTrain(0, 0, 0, 0);
            return 0;

        case static_cast<unsigned int>(ActionCode::ACT_IS_TRAIN):
            return Vid()->weaponFloatAt(16) - Vid()->weaponFloatAt(12) > 1.0f ? 1 : 0;
        case static_cast<unsigned int>(ActionCode::ACT_FIRST_ENGINE):
            return pointerToInt(engineChainHead());
        case static_cast<unsigned int>(ActionCode::ACT_LAST_ENGINE):
            return pointerToInt(engineChainTail());
        case static_cast<unsigned int>(ActionCode::ACT_NEXT_ENGINE):
            return pointerToInt(engineChainNext());
        case static_cast<unsigned int>(ActionCode::ACT_IS_FIRST):
            return engineChainPrevious() == nullptr ? 1 : 0;
        case static_cast<unsigned int>(ActionCode::ACT_IN_TRAIN):
            return isInEngineChain(intToPointer<SPRITE>(argument1));
        default:
            return UNIT::Action(opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
    }


    SPRITE* ENGINE::findEngineChainSpecialWeaponNode() noexcept
    {
        SPRITE* node = engineChainHead();
        while (node)
        {
            VID* const vid = node->Vid();
            if (vid->weaponFloatAt(16) - vid->weaponFloatAt(12) > 1.0f)
                return node;
            node = node->engineChainNext();
        }
        return nullptr;
    }


    int ENGINE::engineChainContainsArmy(int bucket) noexcept
    {
        for (SPRITE* node = this; node; node = node->engineChainNext())
            if (node->armyIndex() == bucket)
                return 1;
        for (SPRITE* node = engineChainPrevious(); node; node = node->engineChainPrevious())
            if (node->armyIndex() == bucket)
                return 1;
        return 0;
    }


    int ENGINE::updateCombatDecision() noexcept
    {
        int result = 0;
        if (HaveLink())
        {
            SPRITE* const child = childChain();
            VID* const childVid = child->Vid();
            if (childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
            {
                SPRITE* const target = child->Goal();
                if (target && sameArmy(*target))
                {
                    const std::uint32_t childAction = child->runtimeFlags() & SPRITE::CommandBitsMask;
                    if (childAction == 12u || childAction == 16u)
                        result = child->SetCommand(0, nullptr);
                }
            }
        }

        VID* const vid = Vid();
        bool runDecision = vid->nvid() != 35;
        if (!runDecision && HaveLink())
        {
            SPRITE* const child = childChain();
            VID* const childVid = child->Vid();
            if (childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
            {
                result = child->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
                runDecision = result >= childVid->weaponRecordAmmoCapacity();
            }
        }

        if (runDecision && (runtimeFlags() & 1u) == 0u)
        {
            const std::uint32_t actionBits = runtimeFlags() & SPRITE::CommandBitsMask;
            if (actionBits == 112u || actionBits == 116u)
            {
                SPRITE* const refOwner = engineCommandReferenceOwner();
                if (!refOwner || refOwner == this)
                {
                    SPRITE* const target = Goal();
                    SPRITE* const child = childChain();
                    if (target != child->Goal() &&
                        (target->Vid() == EmptyVid || canWeaponAffectTarget(target)))
                    {
                        const float dx = std::fabs(target->X() - X());
                        const float dy = std::fabs(target->Y() - Y());
                        const float distance = engineLessEqualOrUnordered(dx, dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                        VID* const childVid = child->Vid();
                        const bool childLinkReady = childVid == vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() && childVid->weaponCount() &&
                            engineFcompEqualOrUnorderedZero(vid->weaponBattleRange());
                        VID* const thresholdVid = childLinkReady ? childVid : vid;
                        if (distance < thresholdVid->weaponBattleRange())
                            result = child->SetCommandWithoutLink(4, target);
                    }
                }
            }
            else
            {
                SPRITE* const child = childChain();
                if (child)
                {
                    VID* const childVid = child->Vid();
                    if (childVid == vid->linkedVid() && childVid->hasWeaponChildDescriptor() && childVid->weaponCount())
                    {
                        SPRITE* const target = child->Goal();
                        if (target)
                        {
                            const float dx = std::fabs(target->X() - X());
                            const float dy = std::fabs(target->Y() - Y());
                            const float distance = engineLessEqualOrUnordered(dx, dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
                            if (distance > childVid->weaponDetectRange())
                                result = child->SetCommandWithoutLink(0, nullptr);
                        }
                    }
                }
            }

            int delta = vid->frameSpeed[Animation()];
            const std::uint32_t elapsed = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            if (elapsed > static_cast<std::uint32_t>(static_cast<unsigned short>(delta)))
                delta = static_cast<int>(elapsed);
            result = AttackTact(static_cast<std::uint32_t>(delta));
            setAttackDecisionCode(result);

            if (result == 1 || result == 2 || result == 5 || result == 6)
            {
                SPRITE* const child = childChain();
                if ((behaviorFlags() & 1) != 0)
                {
                    if (child->actionTimer() != 0 || (static_cast<std::uint32_t>(std::rand()) & 0x80000003u) == 0u)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            result = child->SetCommand(5, target);
                    }
                    else if (SPRITE* const target = bestTargetSprite())
                    {
                        result = child->SetCommand(5, target);
                    }
                }
                else if (SPRITE* const target = bestTargetSprite())
                {
                    result = child->SetCommand(5, target);
                }
            }
        }
        return result;
    }


    void ENGINE::stopEngineChain() noexcept
    {
        SPRITE* head = this;
        if (engineChainPrevious())
            head = engineChainHead();
        if (engineSpeedExceedsStopThreshold(head->m_speed, engineRawConstantFloat(g_baseConstants->raw[24])))
        {
            for (SPRITE* node = head; node; node = node->engineChainNext())
                (void)node->dispatchVirtualAction(ActionCode::ACT_DAMAGE, 2, 0, 0);
        }
        head->engineTargetSpeedRef() = 0.0f;
        head->m_speed = 0.0f;
    }


    void ENGINE::setEnginePathGoalFromCoordinates(float x, float y, float z, int, int project2D) noexcept
    {
        core::R_DOT* node = nullptr;
        if (project2D)
        {
            const int iy = engineConvertFloatToInt32(y);
            const int iz = engineConvertFloatToInt32(z);
            const int ix = engineConvertFloatToInt32(x);
            node = core::g_rMap.GetNearestDot(ix, engineSub32Wrap(iy, iz));
        }
        else
        {
            const int iz = engineConvertFloatToInt32(z);
            const int iy = engineConvertFloatToInt32(y);
            const int ix = engineConvertFloatToInt32(x);
            node = core::g_rMap.GetNearestDot(ix, iy, iz);
        }
        SetCommandToTrain(23, 0, static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node))), 0);
    }


    void ENGINE::ReverseTrain() noexcept
    {
        SPRITE* const head = engineChainHead();
        for (SPRITE* node = head; node; )
        {
            SPRITE* const oldNext = node->engineChainNextRef();
            node->engineChainNextRef() = node->engineChainPreviousRef();
            node->engineChainPreviousRef() = oldNext;

            std::swap(node->primaryPathNodeRef(), node->secondaryPathNodeRef());
            std::swap(node->primaryPathProgressRef(), node->secondaryPathProgressRef());
            std::swap(node->primaryPathAuxiliaryRef(), node->secondaryPathAuxiliaryRef());
            std::swap(node->primaryPathEdgeIndexRef(), node->secondaryPathEdgeIndexRef());
            node->setDerivedStateValue(0, node->derivedStateValue(0) ^ 1);

            if (!oldNext)
            {
                if (node == head)
                {
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                }
                else
                {
                    node->m_runtimeFlags = (node->m_runtimeFlags & ~MovementStartedFlag) | (head->m_runtimeFlags & MovementStartedFlag);
                    node->m_speed = head->m_speed;
                    node->engineTargetSpeedRef() = -head->engineTargetSpeedRef();
                    node->engineAccelerationDelayRef() = head->engineAccelerationDelayRef();
                    const int pathByteCount = head->pathBufferSizeRef();
                    std::memcpy(node->pathBufferData(),
                                head->pathBufferData(),
                                static_cast<std::size_t>(pathByteCount));
                    node->pathBufferSizeRef() = pathByteCount;
                    head->pathBufferSizeRef() = 0;
                }
            }
            node = oldNext;
        }

    }


    void ENGINE::MoveTact()
    {
        MoveEngineTact();

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((now & 0xFFFFC000u) > core::PreviousWorldTimeMilliseconds())
            setRuntimeFlags(runtimeFlags() & ~0x2u);

        if ((now & 0xFFFFFC00u) > core::PreviousWorldTimeMilliseconds() && !engineChainPrevious())
        {
            TRAIN_INFO range(this);
            const int selfArg = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
            if (range.percentAmmo <= 10)
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackMoveRange], selfArg, 0);
            if (range.hp < range.max_hp / 2)
                (void)core::Application::callScriptFunction(core::EvFunctionNumber[kEngineCallbackMoveFrame], selfArg, 0);
        }

        if ((now & 0xFFFFFC00u) > core::PreviousWorldTimeMilliseconds())
        {
            VID* const vid = Vid();
            const int nvid = vid->nvid();
            switch (nvid)
            {
            case 85:
                RepairTact();
                break;
            case 45:
                AddAmmoTact();
                break;
            case 35:
                setBusy(0);
                break;
            default:
                break;
            }
        }
    }


    void ENGINE::AddAmmoTact() noexcept
    {
        SPRITE* best = nullptr;
        int bestMetric = 0;
        for (SPRITE* node = engineChainHead(); node; node = node->engineChainNext())
        {
            if (!sameArmy(*node))
                continue;
            const int metric = node->ammoMissingPercent();
            VID* const nodeVid = node->Vid();
            if (metric > bestMetric && nodeVid->nvid() != 85)
            {
                bestMetric = metric;
                best = node;
            }
        }

        if (best)
        {
            const CONSTANT* const constants = g_baseConstants;
            const int divisor = static_cast<int>(constants->raw[5]);
            best->refillAmmoByCapacityFraction(divisor);
            if (Animation() != static_cast<int>(AnimationCode::ANI_OPEN))
                ChangeAnimation(static_cast<int>(AnimationCode::ANI_OPEN));
        }
        else if (Animation() == static_cast<int>(AnimationCode::ANI_OPEN))
        {
            ChangeAnimation(static_cast<int>(AnimationCode::ANI_STAND));
        }
    }


    void ENGINE::DeletePointerToSprite(SPRITE* sprite)
    {
        if (engineCommandReferenceOwner() == sprite)
            SetCommandToTrain(0, 0, 0, 0);

        VID* const targetVid = sprite->Vid();
        if (targetVid->spriteClassId() == B_ENGINE &&
            Goal() == sprite && (runtimeFlags() & SPRITE::CommandBitsMask) == 0x68u)
        {
            ENGINE* const targetEngine = static_cast<ENGINE*>(sprite);
            SPRITE* const peer = targetEngine->chainNext() ? targetEngine->chainNext() : targetEngine->chainPrevious();
            if (peer)
                SetCommandToTrain(26, static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(peer))), 0, 0);
        }

        if (Goal() == sprite)
            SetCommandToTrain(0, 0, 0, 0);

        SPRITE::DeletePointerToSprite(sprite);
    }


    void ENGINE::DrawDebugOverlay()
    {
        GRAPH* const graph = Graph;
        float y = static_cast<float>(graph->ViewYMin());


        const float statusX = static_cast<float>(graph->ViewXMin()) + 22.0f;

        auto nvidOf = [](SPRITE* sprite) noexcept -> int {
            return sprite ? sprite->Vid()->nvid() : 0;
        };
        auto drawStatus = [&](SPRITE* sprite, bool includeSpeed) {
            const int ammo = sprite->dispatchVirtualAction(ActionCode::ACT_GET_AMMO, 0, 0, 0);
            if (includeSpeed)
            {
                graph->DrawText(
                    statusX, y,
                    "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i speed=%-3i timer=%i",
                    sprite->listReferenceCount(),
                    sprite->commandIndex(),
                    sprite->Animation(), ammo, sprite->Hp(),
                    sprite->attackDecisionCode(), nvidOf(sprite->Goal()),
                    nvidOf(sprite->bestTargetSprite()),
                    static_cast<int>(sprite->Speed() * 1000.0f),
                    static_cast<int>(sprite->actionTimer()));
            }
            else
            {
                graph->DrawText(
                    statusX, y,
                    "Ref=%-3i cmd=%1i ani=%-2i ammo=%-3i hp=%-3i AT=%i goal=%-3i best=%-3i timer=%i",
                    sprite->listReferenceCount(),
                    sprite->commandIndex(),
                    sprite->Animation(), ammo, sprite->Hp(),
                    sprite->attackDecisionCode(), nvidOf(sprite->Goal()),
                    nvidOf(sprite->bestTargetSprite()),
                    static_cast<int>(sprite->actionTimer()));
            }
        };

        drawStatus(this, true);
        y += 12.0f;
        graph->DrawText(
            statusX,
            y,
            "IS_PBJMF(%i%i%i%i%i) Accel=%i MaxSpeed=%i NoStep=%i NoStepNotF=%i",
            pushLineActiveRef(),
            isBusy(),
            static_cast<int>(runtimeFlags() & 1u),
            routeActionReadyRef(),
            static_cast<int>((runtimeFlags() >> 7u) & 1u),
            engineAccelerationDelayRef(),
            static_cast<int>(engineTargetSpeedRef() * 1000.0f),
            g_pathSearchResultScore,
            g_pathSearchSecondaryBestCost);

        if (SPRITE* const child = childChain())
        {
            VID* const linkVid = Vid()->linkedVid();
            if (child->Vid() == linkVid)
            {
                y += 12.0f;
                drawStatus(child, false);
            }
        }

        const std::uint32_t commandCount = commandRecordCount();
        if (commandCount != 0u)
        {
            y += 12.0f;
            STRING commandText;
            STRING part;
            constructFormattedString(part, "%i - ", static_cast<int>(commandCount));
            appendStringOwner(commandText, part);
            for (std::uint32_t i = 0; i < commandCount; ++i)
            {
                constructFormattedString(part,
                           "%i(%i,%i,%i) ",
                           static_cast<int>(commandRecordWord(i, 0)),
                           static_cast<int>(commandRecordWord(i, 1)),
                           static_cast<int>(commandRecordWord(i, 2)),
                           static_cast<int>(commandRecordWord(i, 3)));
                appendStringOwner(commandText, part);
            }
            graph->drawStringColored(
                static_cast<float>(graph->ViewXMin()) + 30.0f,
                y, commandText, 0xFFFFFFFFu);
        }

        DrawRelationDebugOverlay();

        if (SPRITE* const ref = engineCommandReferenceOwner())
        {
            const auto& drawState = core::GlobalApplicationDrawDispatcherState();
            const float refX = ref->X() - drawState.cameraShiftX();
            const float refY = ref->Y() - ref->Z() - drawState.cameraShiftY();
            graph->drawBackBufferPixel2x2(refX, refY, g_colorRed.color);
        }
    }

    void ENGINE::DrawRelationDebugOverlay()
    {
        GRAPH* const graph = Graph;
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();
        auto screenX = [&](const SPRITE* sprite) noexcept -> float {
            return sprite->X() - drawState.cameraShiftX();
        };
        auto screenY = [&](const SPRITE* sprite) noexcept -> float {
            return sprite->Y() - sprite->Z() - drawState.cameraShiftY();
        };
        auto drawWeak = [&](core::R_DOT* node, float offset, DWORD color) {
            if (!node)
                return;
            graph->Line(screenX(this) + offset,
                            screenY(this) + offset,
                            static_cast<float>(node->ScreenX()) + offset,
                            static_cast<float>(node->ScreenY()) + offset,
                            color);
        };

        drawWeak(engineCommandArgument1Node(), 2.0f, g_colorWhite.color);
        drawWeak(engineCommandArgument2Node(), 2.0f, g_colorWhite.color);

        if (SPRITE* const target = Goal())
            graph->Line(screenX(this), screenY(this), screenX(target), screenY(target), g_colorGreen.color);

        if (SPRITE* const child = childChain())
        {
            if (SPRITE* const childTarget = child->Goal())
                graph->Line(screenX(child), screenY(child), screenX(childTarget), screenY(childTarget), g_colorRed.color);
        }

        drawWeak(engineCommandArgument0Node(), 0.0f, g_colorBlue.color);
        (void)graph->unlockBackBufferIfLocked();
    }


}
