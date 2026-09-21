#include "unit.h"
#include "map.h"
#include "win/application_win.h"
#include "core/application.h"
#include "core/log.h"
#include "graphics/angle.h"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace as1
{

    UNIT::UNIT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : TERRAIN(owner, vid, xyz, direction, parent)
    {

        m_sharedPrimaryState = 0;
        m_sharedSecondaryState = 0;
        m_legacyCommandState2 = -1;
        m_turnTimer = 0;
        m_unitState84 = 0;

        const VID* valueOwner = vid;
        if (childChain())
        {
            VID* const childVid = childChain()->Vid();
            VID* const linkVid = vid->linkedVid();
            if (childVid == linkVid &&
                childVid->hasWeaponChildDescriptor() != 0u &&
                childVid->weaponCount() != 0u)
                valueOwner = linkVid;
        }


        m_behaviorFlags = valueOwner->weaponDefaultBehavior();

        const VID* counterOwner = vid;
        if (VID* const linkVid = vid->linkedVid())
        {
            if (linkVid->hasWeaponChildDescriptor() != 0u &&
                linkVid->weaponCount() != 0u)
                counterOwner = linkVid;
        }
        m_ammoFixedPoint = counterOwner->weaponRecordAmmoCapacity() << 6;
    }


    UNIT::~UNIT()
    {
        win::applicationWinInstance()->transferFrom(this);
    }


    int UNIT::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        const int argument2 = argument2Carrier;
        const int argument3 = argument3Carrier;


        if (opcode == static_cast<int>(ActionCode::ACT_NEXT_COMMAND) && Animation() < 15)
            SetMoveAnimation();

        const int op = opcode;
        switch (op)
        {
        case static_cast<int>(ActionCode::ACT_ADD_AMMO):
        {
            const int weaponValue = m_vid->GetMaxAmmo();
            if (weaponValue == 999999)
            {
                setAmmoFixedPoint(63999936);
                return weaponValue;
            }

            const std::uint32_t sum = static_cast<std::uint32_t>(ammoFixedPoint()) +
                (static_cast<std::uint32_t>(argument1) << 6);
            setAmmoFixedPoint(static_cast<std::int32_t>(sum));
            const int result = ammoFixedPoint();
            if (ammoFixedPoint() < 0)
                setAmmoFixedPoint(0);
            return result;
        }

        case static_cast<int>(ActionCode::ACT_GET_AMMO):
            return ammoCount();

        case static_cast<int>(ActionCode::ACT_SET_BEHAVE):
            setBehaviorFlags(argument1);
            if (m_childChain)
                m_childChain->dispatchVirtualAction(static_cast<std::uint32_t>(op), argument1, argument2, argument3);
            return 0;

        case static_cast<int>(ActionCode::ACT_GET_BEHAVE):
            return behaviorFlags();

        case static_cast<int>(ActionCode::ACT_NEXT_COMMAND):
        {
            if (m_currentAnimation >= 15)
                return 0;


            const DWORD commandBits = m_runtimeFlags & SPRITE::CommandBitsMask;
            if (commandBits == 0x0Cu)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(3, m_goalSprite);
                    }
                }
            }

            if (commandBits == 0x10u)
            {
                SPRITE* const child = m_childChain;
                if (child)
                {
                    VID* const childVid = child->m_vid;
                    if (childVid == m_vid->linkedVid() &&
                        childVid->hasWeaponChildDescriptor() != 0u &&
                        childVid->weaponCount() != 0u &&
                        m_goalSprite != nullptr &&
                        child->m_goalSprite == nullptr)
                    {
                        child->SetCommand(4, m_goalSprite);
                    }
                }
            }

            int decision = 0;
            SPRITE* const linkedWeaponChild = m_childChain;
            if (!(linkedWeaponChild != nullptr &&
                  linkedWeaponChild->m_vid == m_vid->linkedVid() &&
                  linkedWeaponChild->m_vid->hasWeaponChildDescriptor() != 0u &&
                  linkedWeaponChild->m_vid->weaponCount() != 0u) &&
                turnTimer() != 0)
            {
                decision = 2;
            }
            else
            {
                std::uint32_t delta = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                std::uint32_t decisionDelta = static_cast<std::uint32_t>(
                    m_vid->frameSpeedForAnimation(m_currentAnimation));
                if (delta > decisionDelta)
                    decisionDelta = delta;

                decision = AttackTact(decisionDelta);
                setAttackDecisionCode(decision);
            }

            if (decision == 7)
            {
                SetCommand(0, nullptr);
            }
            else if (decision == 1)
            {
                const float runtimeMaxSpeed = MaxSpeed();
                if (runtimeMaxSpeed == 0.0f || std::isnan(runtimeMaxSpeed))
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            child->m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                        else if (m_goalSprite != nullptr)
                        {
                            SetCommand(0, nullptr);
                        }
                    }
                    else if (m_goalSprite != nullptr)
                    {
                        SetCommand(0, nullptr);
                    }
                }
                else if (m_speed == 0.0f || std::isnan(m_speed))
                {
                    StartMove();
                }

                const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                if (postBits == 0x0Cu || postBits == 0x10u)
                {
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            (behaviorFlags() & 1) != 0)
                        {
                            if (child->m_actionTimer != 0u || (std::rand() % 4) == 0)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }
            else if (decision == 0)
            {


                if (m_speed != 0.0f && !std::isnan(m_speed))
                {
                    bool stopMoving = true;
                    SPRITE* const child = m_childChain;
                    if (child)
                    {
                        VID* const childVid = child->m_vid;
                        if (childVid == m_vid->linkedVid() &&
                            childVid->hasWeaponChildDescriptor() != 0u &&
                            childVid->weaponCount() != 0u &&
                            m_goalSprite != child->m_goalSprite)
                        {
                            stopMoving = false;
                        }
                    }

                    if (stopMoving)
                        Stop();
                }
            }
            else if (decision == 3)
            {
                setTurnTimer(0);
                SetCommand(0, nullptr);
            }
            else if (decision == 2 &&
                     (behaviorFlags() & 2) != 0 &&
                     (m_speed == 0.0f || std::isnan(m_speed)))
            {
                StartMove();
            }

            if (decision == 6)
            {
                const int behavior = behaviorFlags();
                if ((behavior & 1) != 0)
                {
                    if ((behavior & 2) != 0)
                    {
                        if (SPRITE* const target = SeekEnemy())
                            SetCommand(4, target);
                    }
                    else
                    {
                        SPRITE* const child = m_childChain;
                        if (child)
                        {
                            VID* const childVid = child->m_vid;
                            if (childVid == m_vid->linkedVid() &&
                                childVid->hasWeaponChildDescriptor() != 0u &&
                                childVid->weaponCount() != 0u)
                            {
                                if (SPRITE* const target = SeekEnemy())
                                    child->SetCommand(4, target);
                            }
                        }
                    }
                }
            }

            if (decision == 2 || decision == 5)
            {
                if ((behaviorFlags() & 1) != 0)
                {
                    const DWORD postBits = m_runtimeFlags & SPRITE::CommandBitsMask;
                    if (postBits == 0u || postBits == 4u || postBits == 0x10u)
                    {


                        bool acquire = m_currentFrameEnd > m_currentFrameBegin;
                        if (!acquire)
                        {
                            SPRITE* const child = m_childChain;
                            std::uint32_t timer = m_actionTimer;
                            if (child)
                            {
                                VID* const childVid = child->m_vid;
                                if (childVid == m_vid->linkedVid() &&
                                    childVid->hasWeaponChildDescriptor() != 0u &&
                                    childVid->weaponCount() != 0u)
                                {
                                    timer = child->m_actionTimer;
                                }
                            }
                            acquire = timer != 0u || (std::rand() % 11) == 0;
                        }

                        if (acquire)
                        {
                            if (SPRITE* const target = SeekEnemy())
                                SetCommand(4, target);
                        }
                    }
                }
            }

            if ((m_runtimeFlags & SPRITE::CommandBitsMask) != 0u ||
                m_goalSprite != nullptr ||
                m_actionTimer != 0u)
            {
                return 0;
            }

            Stop();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_REPAIR):
            setAmmoFixedPoint(static_cast<std::int32_t>(static_cast<std::uint32_t>(m_vid->GetMaxAmmo()) << 6));
            return TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);

        case static_cast<int>(ActionCode::ACT_SAVE):
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int rawBehavior8C = behaviorFlags();
            stream->write(&rawBehavior8C, 4u);

            return 0;
        }

        case static_cast<int>(ActionCode::ACT_RESTORE):
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            int rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 4u);
            setBehaviorFlags(rawBehavior8C);

            const int mapVersion = argument2;
            if (mapVersion == 11)
            {
                std::uint32_t count = 0u;
                stream->read(&count, 4u);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::int32_t value = 0;
                    stream->read(&value, 4u);
                    InsertItem(value);
                }
            }
            else if (mapVersion < 11)
            {
                std::uint32_t count = 0u;
                stream->read(&count, 4u);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::int16_t value = 0;
                    stream->read(&value, 2u);
                    InsertItem(static_cast<std::int32_t>(value));
                }
            }
            return 0;
        }

        case SpriteActConst::ACT_RESTORE_OLD_MAP:
        {
            TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
            BaseStream* const stream = reinterpret_cast<BaseStream*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(argument1)));
            const int mapVersion = argument2;
            if (mapVersion < 7)
            {
                std::uint8_t bucket = 0;
                stream->read(&bucket, 1u);
                ChangeArmy(static_cast<int>(bucket));
            }

            std::uint8_t rawBehavior8C = 0;
            stream->read(&rawBehavior8C, 1u);
            setBehaviorFlags(static_cast<int>(rawBehavior8C));
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_SET_ARMY):


            ChangeArmy(argument1);
            return 0;

        default:
            return TERRAIN::Action(op, static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
    }

    __declspec(safebuffers)

    void UNIT::MoveTact()
    {
        VID* const vid = Vid();
        if (vid->spriteClassId() != B_UNIT && vid->spriteClassId() != B_AVIA)
        {
            SPRITE::MoveTact();
            return;
        }

        const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();

        int remainingTurn = turnTimer();
        if (remainingTurn != 0)
        {


            const unsigned char oldDirection = static_cast<unsigned char>(directionIndex());
            const unsigned char turnOffset = static_cast<unsigned char>(remainingTurn);
            const unsigned char requestedDirection = static_cast<unsigned char>(oldDirection + turnOffset);
            RotateTact(ANGLE(requestedDirection), deltaMs);

            const unsigned char newDirection = static_cast<unsigned char>(directionIndex());
            const unsigned char clockwise = static_cast<unsigned char>(oldDirection - newDirection);
            const unsigned char counterClockwise = static_cast<unsigned char>(newDirection - oldDirection);
            const int rotated = static_cast<int>(clockwise < counterClockwise ? clockwise : counterClockwise);
            remainingTurn += remainingTurn > 0 ? -rotated : rotated;
            setTurnTimer(remainingTurn);
        }
        else if (SPRITE* const target = Goal())
        {
            const float speed = Speed();
            if (speed != 0.0f && !std::isnan(speed))
            {
                const unsigned char reverse = speed < 0.0f ? 0x80u : 0u;
                ANGLE targetDirection = DirectionFromFloatXY(target->X() - X(), target->Y() - Y());
                targetDirection = ANGLE(static_cast<unsigned char>(targetDirection.Int() + reverse));


                const float glideFootprintScale =
                    (static_cast<std::uint32_t>(vid->weaponFlags()) & 0x00080000u) != 0u
                        ? 1.3f
                        : 1.0f;
                RotateTact(GlideDirectionScaled(targetDirection, glideFootprintScale), deltaMs);

                if ((runtimeFlags() & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask)
                    Stop();
            }
        }


        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        const float currentX = X();
        const float currentY = Y();
        const float currentZ = Z();
        const bool positionChanged =
            !(currentX == candidate.x || std::isnan(currentX) || std::isnan(candidate.x)) ||
            !(currentY == candidate.y || std::isnan(currentY) || std::isnan(candidate.y)) ||
            !(currentZ == candidate.z || std::isnan(currentZ) || std::isnan(candidate.z));

        if (positionChanged)
        {
            if (CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
            {
                steerAwayFromMapBoundary(candidate.x, candidate.y);
                ChangeCoor(candidate.x, candidate.y, candidate.z);
            }
            else if (SPRITE* const target = Goal())
            {

                if (sharedPrimaryState() == 0 && sharedSecondaryState() == 0)
                {
                    const unsigned char desired = static_cast<unsigned char>(
                        DirectionFromFloatXY(target->X() - X(), target->Y() - Y()).Int());
                    const unsigned char current = static_cast<unsigned char>(directionIndex());
                    if (desired > current)
                        setSharedSecondaryState(1);
                    else if (desired < current)
                        setSharedPrimaryState(1);
                    else if ((std::rand() & 1) == 0)
                        setSharedSecondaryState(1);
                    else
                        setSharedPrimaryState(1);
                }
                else if (turnTimer() == 0)
                {
                    const float collisionSpeed = Speed();
                    if (collisionSpeed != 0.0f && !std::isnan(collisionSpeed) &&
                        (std::fabs(X() - target->X()) >= 40.0f ||
                          std::fabs(Y() - target->Y()) >= 40.0f))
                    {

                        setTurnTimer(sharedPrimaryState() != 0 ? 75 : -75);
                    }
                }

            }
        }


        SPRITE* const child = childChain();
        if (child && child->Vid() &&
            (static_cast<std::uint32_t>(child->Vid()->weaponFlags()) & 0x00000800u) != 0u)
        {
            VID* const ownVid = Vid();
            if (ownVid && ownVid->declaredAnimationFrameCount(6) != 0)
            {
                const float tailSpeed = Speed();
                const bool tailMoving = tailSpeed != 0.0f && !std::isnan(tailSpeed);
                if (tailMoving || (runtimeFlags() & 0x80u) != 0u)
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

                const float currentSpeed = Speed();
                if (currentSpeed != 0.0f && !std::isnan(currentSpeed))
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


    void UNIT::DrawDebugOverlay()
    {

        SPRITE::DrawDebugOverlay();
    }

}
