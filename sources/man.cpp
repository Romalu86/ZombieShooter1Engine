#include "man.h"

#include "core/application.h"
#include "map.h"
#include "vid/vid.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

namespace
{

    __forceinline std::int32_t manImul32Low(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) *
                static_cast<std::uint32_t>(b)));
    }

    __forceinline std::int32_t manAdd32Wrap(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    }

    __forceinline std::int32_t manSub32Wrap(std::int32_t a, std::int32_t b) noexcept
    {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
    }

    __forceinline float manFildToF32(std::int32_t value) noexcept
    {
        return static_cast<float>(value);
    }

    __forceinline float manFildAddF32(std::int32_t value, float addend) noexcept
    {
        return static_cast<float>(
            static_cast<long double>(value) + static_cast<long double>(addend));
    }

    __forceinline int subtractFloatToIntTruncated(float lhs, float rhs) noexcept
    {
        const long double value = static_cast<long double>(lhs) - static_cast<long double>(rhs);
        if (!std::isfinite(value) ||
            value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
            value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
            return 0;
        const std::int64_t converted = static_cast<std::int64_t>(std::trunc(value));
        return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(converted)));
    }

    __forceinline int subtractRoundedFloatToInt(float lhs, float rhs) noexcept
    {
        const float rounded = static_cast<float>(static_cast<long double>(lhs) - static_cast<long double>(rhs));
        return subtractFloatToIntTruncated(rounded, 0.0f);
    }

}

namespace as1
{

    MAN::MAN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : UNIT(owner, vid, xyz, ANGLE(direction.Int() & 0xFF), parent)
    {


        insertUniqueItem(0x0105);
        for (int i = 2; i < 10; ++i)
            weaponAmmo[i] = 0;
    }


    MAN::~MAN()
    {
    }


    int MAN::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;

        switch (opcode)
        {
        case static_cast<int>(ActionCode::ACT_COOR_ATTACK):
        {


            SPRITE* const child = childChain();
            if (!child)
                return 1;

            VID* const childVid = child->Vid();
            if (!childVid)
                return 1;
            if (child->Animation() >= 15)
                return 1;
            if (childVid->hasWeaponChildDescriptor() == 0u)
                return 1;
            if (childVid->weaponCount() == 0u)
                return 1;
            if (child->commandBits() != 0u)
                return 1;
            if (child->actionTimer() >= 5000u)
                return 1;

            const float targetX = manFildToF32(argument1);
            const float targetZ = manFildToF32(argument3);
            const float targetY = manFildAddF32(argument2, targetZ);


            const float dx = std::fabs(targetX - X());
            const float dy = std::fabs(targetY - Y());
            const float planar = (dx > dy)
                ? dx + dy * 0.5f
                : dy + dx * 0.5f;
            const float distance = static_cast<float>(
                approximatePlanarDistance(planar, targetZ - Z()));


            if (distance > childVid->weaponBattleRange())
                return 1;

            SPRITE* marker = new (std::nothrow) SPRITE(
                mapOwner(),
                EmptyVid,
                VECTOR{targetX, targetY, targetZ},
                ANGLE(static_cast<unsigned char>(0)),
                nullptr);


            child->SetCommand(4, marker);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (Animation() >= 15)
                return 0;

            SPRITE* const child = childChain();
            if (Goal() || (child && child->Goal()))
            {
                std::uint32_t delta = static_cast<std::uint32_t>(
                    Vid()->frameSpeedForAnimation(Animation()));
                const std::uint32_t frameDelta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                if (frameDelta > delta)
                    delta = frameDelta;
                setAttackDecisionCode(AttackTact(delta));
            }


            SetMoveAnimation();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_ADD_ITEM):
        {
            const std::int32_t word = argument1;
            if (argument1 == 301 || argument1 == 235)
            {
                InsertItem(word);
                return 0;
            }

            if (insertUniqueItem(word) != 0)
                return 0;
            if (argument1 < 260 || argument1 > 269)
                return 0;

            if (UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0) != 0 &&
                argument1 - 260 <= Vid()->linkedVid()->nvid() - 10 &&
                argument1 != 260)
            {
                return 0;
            }
            ChangeWeapon(argument1 - 260);
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
        {


            const int index = argument1;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
                return weaponAmmo[index];
            return UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {

            const int index = argument2;
            if (index > 9)
                return 0;
            if (index != 0 && index != Vid()->linkedVid()->nvid() - 10)
            {
                MAN* const man = this;
                const int value = manAdd32Wrap(man->weaponAmmo[index], argument1);
                man->weaponAmmo[index] = value;
                return value;
            }
            return UNIT::Action(static_cast<int>(ActionCode::ACT_ADD_AMMO), static_cast<std::intptr_t>(argument1), 0, 0);
        }

        case static_cast<int>(ActionCode::ACT_DAMAGE):
        {
            int damage = argument1;
            if (damage > 0)
            {
                VID* const ownVid = Vid();
                const int ownNvid = ownVid->nvid();
                if (ownNvid != 350 && ownNvid != 1987)
                {


                    for (SPRITE* child = childChain(); child; child = child->childChain())
                    {
                        const int childNvid = child->Vid()->nvid();
                        if (childNvid == 203 || childNvid == 181)
                            return 0;
                    }


                    static constexpr int kDamagePercent[3] = {50, 70, 90};
                    SPRITE* armor = childChain();
                    while (armor)
                    {
                        const int armorNvid = armor->Vid()->nvid();
                        if (armorNvid >= 200 && armorNvid <= 202)
                            break;
                        armor = armor->childChain();
                    }

                    if (armor)
                    {
                        const int armorHp = armor->Hp();
                        if (armorHp > 0)
                        {
                            int protectionIndex = 0;
                            if (armorHp >= 100)
                                protectionIndex = armorHp >= 150 ? 2 : 1;

                            const int percent = kDamagePercent[protectionIndex];
                            const int scaledDamage = manImul32Low(damage, percent);
                            const int armorDamage = manAdd32Wrap(scaledDamage, 50) / 100;
                            if (armorDamage >= armorHp)
                            {
                                (void)armor->CreateChildFor(15, nullptr);
                                DeleteSpriteThroughVirtualDeletingDestructor(armor);
                            }
                            else
                            {
                                armor->setHpRaw(manSub32Wrap(armorHp, armorDamage));
                            }

                            damage = manAdd32Wrap(damage, scaledDamage / -100);
                        }
                    }
                }

                if (damage >= Hp() &&
                    dispatchVirtualAction(ActionCode::ACT_HAVE_ITEM, 230, 0, 0) != 0)
                {
                    static_cast<void>(dispatchVirtualAction(ActionCode::ACT_DELETE_ITEM, 230, 0, 0));
                    const int bucket = armyIndex();
                    ChangeHp(Vid()->GetMaxHp(bucket));

                    core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
                    VID* createVid = EmptyVid;
                    if (table.count() > 181)
                    {
                        if (VID* const raw = table.slot(181))
                            createVid = raw;
                    }
                    static_cast<void>(mapOwner()->CreateSprite(
                        createVid,
                        VECTOR{X(), Y(), Z() + 22.0f},
                        ANGLE(static_cast<unsigned char>(0)),
                        this,
                        false));
                    return 0;
                }
            }
            return SPRITE::Action(static_cast<int>(ActionCode::ACT_DAMAGE), static_cast<std::intptr_t>(damage), argument2, argument3);
        }

        case static_cast<int>(ActionCode::ACT_CHANGE_VID):
        {


            VID* const previousVid = Vid();
            VID* const previousLink = previousVid->linkedVid();
            MAN* const man = this;
            if (previousVid->nvid() < 20)
            {
                const int currentAmmo = UNIT::Action(
                    static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
                man->weaponAmmo[previousLink->nvid() - 10] = currentAmmo;
            }

            static_cast<void>(UNIT::Action(
                static_cast<int>(ActionCode::ACT_CHANGE_VID),
                static_cast<std::intptr_t>(argument1), argument2, argument3));

            VID* const vid = Vid();
            if (vid->nvid() > 20)
            {
                const int weaponValue = vid->GetMaxAmmo();
                const int currentAmmo = UNIT::Action(
                    static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
                static_cast<void>(UNIT::Action(
                    static_cast<int>(ActionCode::ACT_ADD_AMMO),
                    static_cast<std::intptr_t>(manSub32Wrap(weaponValue, currentAmmo)),
                    0, 0));
                return 0;
            }

            VID* const link = vid->linkedVid();
            const int currentAmmo = UNIT::Action(
                static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
            const int storedAmmo = man->weaponAmmo[link->nvid() - 10];
            static_cast<void>(UNIT::Action(
                static_cast<int>(ActionCode::ACT_ADD_AMMO),
                static_cast<std::intptr_t>(manSub32Wrap(storedAmmo, currentAmmo)),
                0, 0));
            return 0;
        }

        default:


            return UNIT::Action(
                opcode, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }

    }


    void MAN::MoveTact()
    {
        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        const float applicationSizeX = core::ApplicationMapWidth();
        const float applicationSizeY = core::ApplicationMapHeight();

        const bool changedXY = candidate.x != X() || candidate.y != Y();
        if (changedXY &&
            candidate.x >= 0.0f && candidate.x < applicationSizeX &&
            candidate.y >= 0.0f && candidate.y < applicationSizeY &&
            CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            ChangeCoor(candidate.x, candidate.y, candidate.z);
        }

        SPRITE* const target = Goal();
        if (target && (runtimeFlags() & SPRITE::CommandBitsMask) == 4u)
        {
            const int reverse = Speed() >= 0.0f ? 0 : 128;
            const int dx = subtractRoundedFloatToInt(target->X(), X());
            const int dy = subtractFloatToIntTruncated(target->Y(), Y());
            const int desired = Decart2Polar(dx, dy, nullptr).Int() + reverse;
            const std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const int turn = GlideDirection(ANGLE(static_cast<unsigned char>(desired))).Int();
            RotateTact(turn, delta);

            VID* const targetVid = target->Vid();
            const bool stoppedByFlags =
                (runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask;
            const bool overlap =
                IsXYCross(targetVid, target->X(), target->Y()) != 0;
            if (stoppedByFlags || overlap)
                Stop();
        }


        SPRITE* const child = childChain();
        if (child && child->Vid() &&
            (static_cast<std::uint32_t>(child->Vid()->weaponFlags()) & 0x00000800u) != 0u)
        {
            VID* const ownVid = Vid();
            if (ownVid && ownVid->declaredAnimationFrameCount(6) != 0)
            {
                const float tailSpeed = Speed();
                if (tailSpeed != 0.0f || (runtimeFlags() & 0x80u) != 0u)
                {
                    const unsigned char selfDirection = static_cast<unsigned char>(directionIndex());
                    const unsigned char childDirection = static_cast<unsigned char>(child->directionIndex());
                    const unsigned char clockwise = static_cast<unsigned char>(selfDirection - childDirection);
                    const unsigned char counterClockwise = static_cast<unsigned char>(childDirection - selfDirection);
                    const unsigned char angleDistance =
                        clockwise < counterClockwise ? clockwise : counterClockwise;
                    if (angleDistance > 0x40u)
                    {
                        const int animation = Animation();
                        if (animation == 0 || animation == 2 || animation == 3)
                            ChangeAnimation(6);
                        return;
                    }

                    if ((runtimeFlags() & 0x80u) != 0u)
                        return;
                }

                if (Animation() == 6)
                {
                    const float quarterMaximum = std::fabs(MaxSpeed()) * 0.25f;
                    if (quarterMaximum > std::fabs(tailSpeed))
                    {
                        ChangeAnimation(0);
                        ChangeDirection(ANGLE(static_cast<unsigned char>(directionIndex() + 0x80)));
                    }
                }

                if (Speed() != 0.0f)
                    return;

                if (m_unitState84 != 0)
                {
                    const std::uint32_t tailDeltaMs =
                        core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                    const ANGLE remaining = RotateTact(ANGLE(child->directionIndex()), tailDeltaMs);
                    if (static_cast<unsigned char>(remaining.Int()) < 0x10u)
                        m_unitState84 = 0;
                    return;
                }

                const unsigned char selfDirection = static_cast<unsigned char>(directionIndex());
                const unsigned char childDirection = static_cast<unsigned char>(child->directionIndex());
                const unsigned char clockwise = static_cast<unsigned char>(selfDirection - childDirection);
                const unsigned char counterClockwise = static_cast<unsigned char>(childDirection - selfDirection);
                m_unitState84 =
                    (clockwise < counterClockwise ? clockwise : counterClockwise) > 0x37u ? 1 : 0;
            }
        }

    }


    int MAN::ChangeWeapon(int weapon) noexcept
    {
        VID* const currentLinkVid = Vid()->linkedVid();
        if (!currentLinkVid || currentLinkVid->nvid() > 20)
            return 0;

        if (weapon == 10)
            weapon = 0;

        if (findLastCommandWord(weapon + 0x104) < 0)
            return 0;

        SPRITE* const link = childChain();
        if (!link || link->Vid() != currentLinkVid)
            return 0;

        const int currentWeapon = currentLinkVid->nvid() - 10;
        weaponAmmo[currentWeapon] = UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);

        link->dispatchVirtualAction(ActionCode::ACT_CHANGE_VID, weapon + 10, 0, 0);
        Vid()->setLinkedVid(mapOwner()->Vid(weapon + 10));

        const int currentAmmo = UNIT::Action(static_cast<int>(ActionCode::ACT_GET_AMMO), 0, 0, 0);
        UNIT::Action(static_cast<int>(ActionCode::ACT_ADD_AMMO),
                     static_cast<std::intptr_t>(weaponAmmo[weapon] - currentAmmo), 0, 0);
        return 1;
    }
}
