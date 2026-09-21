#include "balloon.h"
#include "core/application.h"
#include "constant.h"
#include "engine.h"
#include "graphics/angle.h"
#include "sprite_collector.h"
#include "map.h"
#include "vid/vid.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace as1
{

    namespace
    {
        __forceinline bool balloonNotEqualOrUnordered(double lhs, double rhs) noexcept;
    }


    BALLOON::BALLOON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : AVIA(owner, vid, xyz, direction, parent)
    {

        m_attachmentPhase = 0;
        m_attachmentTransitionPending = 0;
    }


    void BALLOON::MoveTact()
    {

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        if (attachmentPhase() != 0u)
        {
            ChangeCoor(X(), Y(), candidate.z);
            return;
        }

        if (SPRITE* const target = Goal())
        {
            if (balloonNotEqualOrUnordered(Speed(), 0.0))
            {
                const int reverse = Speed() < 0.0f ? 0x80 : 0;
                const int desired = (DirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
                const int turn = GlideDirection(ANGLE(static_cast<unsigned char>(desired))).Int();
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                RotateTact(turn, deltaMs);
            }
        }

        if ((balloonNotEqualOrUnordered(X(), candidate.x) ||
             balloonNotEqualOrUnordered(Y(), candidate.y)) &&
            CanPlaceWithCrushAndGlide(&candidate.x, &candidate.y, &candidate.z) == nullptr)
        {
            steerAwayFromMapBoundary(candidate.x, candidate.y);
            ChangeCoor(candidate.x, candidate.y, candidate.z);
        }
    }

    namespace
    {
        __forceinline bool balloonTargetMatches(const BALLOON* self, const SPRITE* target) noexcept
        {
            if (!target)
                return false;
            VID* const targetVid = target->Vid();
            if (targetVid->linkedVid() != self->Vid())
                return false;
            return self->sameArmy(*target);
        }

        __forceinline bool balloonTargetHasMatchingLinkChild(const SPRITE* target) noexcept
        {
            SPRITE* const child = target->childChain();
            return child && child->Vid() == target->Vid()->linkedVid();
        }

        __forceinline bool balloonLessOrUnordered(long double lhs, long double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        __forceinline bool balloonEqualOrUnordered(long double lhs, long double rhs) noexcept
        {

            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        __forceinline bool balloonLessOrUnordered(double lhs, double rhs) noexcept
        {
            return balloonLessOrUnordered(static_cast<long double>(lhs), static_cast<long double>(rhs));
        }

        __forceinline bool balloonEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return balloonEqualOrUnordered(static_cast<long double>(lhs), static_cast<long double>(rhs));
        }

        __forceinline bool balloonNotEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs != rhs;
        }

        __forceinline bool balloonOrderedGreaterEqual(double lhs, double rhs) noexcept
        {
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs >= rhs;
        }

        __forceinline float balloonTargetDistance(const BALLOON* self, const SPRITE* candidate) noexcept
        {


            const float dx = std::fabs(candidate->X() - self->X());
            const float dy = std::fabs(candidate->Y() - self->Y());
            return !(dx > dy) ? dx * 0.5f + dy : dx + dy * 0.5f;
        }
    }


    void BALLOON::acquireNearestAttachmentTarget() noexcept
    {

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        SPRITE* best = nullptr;
        int cursor = hash->overflowCount() - 1;
        hash->setReverseCursor(cursor);
        while (cursor >= 0)
        {
            SPRITE* const candidate = hash->overflowSpriteAt(cursor);
            if (!candidate)
                break;

            if (balloonTargetMatches(this, candidate) &&
                !balloonTargetHasMatchingLinkChild(candidate) &&
                static_cast<ENGINE*>(candidate)->isBusy() == 0)
            {
                if (!best)
                {
                    best = candidate;
                }
                else
                {
                    const float candidateDistance = balloonTargetDistance(this, candidate);
                    const float bestDistance = balloonTargetDistance(this, best);


                    if (candidateDistance < bestDistance)
                        best = candidate;
                }
            }

            cursor = hash->reverseCursor() - 1;
            hash->setReverseCursor(cursor);
        }

        if (!best)
            return;
        static_cast<ENGINE*>(best)->setBusy(1);
        Move(best);
    }


    void BALLOON::attachToGoalTarget() noexcept
    {

        SPRITE* const target = Goal();
        if (balloonTargetMatches(this, target))
        {
            if (balloonTargetHasMatchingLinkChild(target))
            {
                SetCommand(0, nullptr);
                setAttachmentPhase(1u);
                return;
            }

            setZSpeedDirect(0.0f);
            const VECTOR& link = target->Vid()->linkOffset();
            ChangeCoor(target->X() + link.x, target->Y() + link.y, target->Z() + link.z);

            setBehaviorFlags(target->Action(static_cast<int>(ActionCode::ACT_GET_BEHAVE), 0, 0, 0));
            target->insertChildChainHead(this);
            static_cast<ENGINE*>(target)->setBusy(1);
            SetCommand(0, nullptr);
            return;
        }

        ChangeAnimation(15);
    }

    void BALLOON::updateAltitudeAndAttachmentState() noexcept
    {

        VID* const vid = Vid();
        const float ground = mapOwner()->GetGroundZ(vid, VECTOR2{X(), Y()});

        if (childBacklink())
        {
            setAttachmentPhase(0u);
        }
        else if (SPRITE* const target = Goal())
        {
            if (balloonTargetMatches(this, target))
                static_cast<ENGINE*>(target)->setBusy(1);
        }

        if (attachmentPhase() != 0u)
        {
            if (attachmentPhase() == 1u)
            {
                DWORD flags = runtimeFlags() & ~SPRITE::MovementStartedFlag;
                setZSpeedDirect(vid->maximumZSpeed());
                setSpeedDirect(0.0f);
                if (balloonOrderedGreaterEqual(
                        static_cast<double>(Z()),
                        static_cast<double>(ground + vid->moveUpZ())))
                {
                    flags |= SPRITE::MovementStartedFlag;
                    setAttachmentPhase(0u);
                }
                setRuntimeFlags(flags);
                return;
            }

            if (attachmentPhase() == 2u)
            {
                setRuntimeFlags(runtimeFlags() & ~SPRITE::MovementStartedFlag);
                setSpeedDirect(0.0f);
                setZSpeedDirect(-vid->maximumZSpeed());
                SPRITE* const target = Goal();
                if (!balloonTargetMatches(this, target) || balloonTargetHasMatchingLinkChild(target))
                {
                    setAttachmentPhase(1u);
                    return;
                }

                ChangeCoor(target->X(), target->Y(), Z());
                const std::uint32_t delta = std::max(
                    core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                    static_cast<std::uint32_t>(vid->frameSpeed[Animation()]));
                RotateTact(target->directionIndex(), delta);
                if (target->Z() + target->Vid()->linkOffset().z >= Z())
                    attachToGoalTarget();
                return;
            }
        }

        if (childBacklink())
        {
            setZSpeedDirect(0.0f);
            return;
        }

        const float center = ground + vid->moveUpZ();
        if (center - 10.0f > Z())
            setZSpeedDirect(vid->maximumZSpeed());


        else if (Z() <= center + 10.0f)
            setZSpeedDirect(0.0f);
        else
            setZSpeedDirect(-vid->maximumZSpeed());
    }


    int BALLOON::isAtGoalOrPathComplete() const noexcept
    {

        SPRITE* const target = Goal();
        if (target)
        {
            const float dx = std::fabs(target->X() - X());
            if (dx < 10.0f)
            {
                const float dy = std::fabs(target->Y() - Y());
                if (dy < 10.0f)
                    return 1;
            }
        }
        const DWORD flags = runtimeFlags();
        return (flags & SPRITE::CrossedGoalAxesMask) == SPRITE::CrossedGoalAxesMask ? 1 : 0;
    }


    void BALLOON::updateFlightSlot10() noexcept
    {


        SPRITE* const carrier = childBacklink();
        if (carrier)
        {
            if ((core::CurrentTimeMilliseconds() & 0xFFFFFC00u) > applicationBucketTime())
                refillAmmoByCapacityFraction(static_cast<int>(g_baseConstants->raw[19]));

            SPRITE* const target = Goal();
            bool rotateOnly = (target == nullptr);
            if (!rotateOnly)
            {
                if (carrier->Goal() == target)
                {
                    const DWORD carrierMode = carrier->runtimeFlags() & SPRITE::CommandBitsMask;
                    if (carrierMode == 0x6Cu || carrierMode == 0x68u)
                        rotateOnly = true;
                }
                if (!rotateOnly)
                {
                    const long double distance = approximatePlanarDistance(
                        target->X() - carrier->X(),
                        target->Y() - carrier->Y());
                    if (!balloonLessOrUnordered(
                            distance,
                            static_cast<long double>(carrier->Vid()->weaponBattleRange())))
                        rotateOnly = true;
                }
                if (!rotateOnly && actionTimer() != 0u)
                    rotateOnly = true;
            }

            if (rotateOnly)
            {
                const std::uint32_t delta = std::max(
                    core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                    static_cast<std::uint32_t>(Vid()->frameSpeed[Animation()]));
                if (RotateTact(ANGLE(static_cast<unsigned char>(carrier->directionIndex())), delta).Int() == 0)
                    ChangeAnimation(0);
                return;
            }

            if ((runtimeFlags() & SPRITE::CommandBitsMask) == 0x20u)
            {
                SetCommand(3, target);
                m_attachmentTransitionPending = 1u;
            }

            (void)deleteChildByVid(carrier->Vid()->woundChildVid());
            carrier->setChildChain(nullptr);
            static_cast<ENGINE*>(carrier)->setBusy(0);

            SPRITE* const best = bestTargetSprite();
            if (best && best != target)
            {
                const long double distance = approximatePlanarDistance(best->X() - X(), best->Y() - Y());
                if (balloonLessOrUnordered(
                        distance,
                        static_cast<long double>(Vid()->weaponBattleRange())))
                    Attack(best);
            }

            if (target && target->Vid() == EmptyVid)
            {
                carrier->SetCommand(0, nullptr);
                SPRITE* const helper = new (std::nothrow) SPRITE(
                    mapOwner(), EmptyVid, target->xyz(), ANGLE(static_cast<unsigned char>(0)), nullptr);
                Attack(helper);
            }

            ChangeAnimation(2);
            m_attachmentPhase = 1u;
            setRuntimeFlags(runtimeFlags() & ~SPRITE::MovementStartedFlag);
            setChildBacklink(nullptr);
            return;
        }

        SPRITE* const best = bestTargetSprite();
        SPRITE* target = Goal();
        if (best && best != target)
        {
            const double distance = balloonTargetDistance(this, best);
            if (balloonLessOrUnordered(
                    static_cast<long double>(distance),
                    static_cast<long double>(Vid()->weaponDetectRange())) &&
                ammoCount() > 0)
                Attack(best);
        }

        if (ammoCount() <= 0 || !target)
        {
            bool keepTarget = false;
            if (target)
            {
                VID* const linkVid = target->Vid()->linkedVid();
                const bool sameLink = linkVid == Vid();
                const bool sameArmy = this->sameArmy(*target);
                SPRITE* const targetChild = target->childChain();
                keepTarget = sameLink && sameArmy && (!targetChild || targetChild->Vid() != linkVid);
                if (!keepTarget)
                    SetCommand(0, nullptr);
            }
            if (!keepTarget)
                acquireNearestAttachmentTarget();
        }

        target = Goal();
        if (target)
        {
            VID* const linkVid = target->Vid()->linkedVid();
            if (linkVid == Vid() &&
                this->sameArmy(*target))
            {
                SPRITE* const targetChild = target->childChain();
                if (targetChild && targetChild->Vid() == linkVid)
                {
                    SetCommand(0, nullptr);
                    acquireNearestAttachmentTarget();
                }
            }
        }

        if (m_attachmentPhase != 2u && (runtimeFlags() & SPRITE::CommandBitsMask) == 0x04u && isAtGoalOrPathComplete())
        {
            target = Goal();
            if (target)
            {
                VID* const linkVid = target->Vid()->linkedVid();
                if (linkVid == Vid() &&
                    this->sameArmy(*target))
                {
                    SPRITE* const targetChild = target->childChain();
                    if (!targetChild || targetChild->Vid() != linkVid)
                    {
                        m_attachmentPhase = 2u;
                        updateAltitudeAndAttachmentState();
                    }
                }
            }
        }
    }

    void BALLOON::updateBalloonCombatBehavior() noexcept
    {

        SPRITE* const target = Goal();
        if (!balloonTargetMatches(this, target))
        {
            const std::uint32_t delta = std::max(
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds(),
                static_cast<std::uint32_t>(Vid()->frameSpeed[Animation()]));
            setAttackDecisionCode(AttackTact(delta));
        }

        SPRITE* const current = Goal();
        if (attachmentTransitionPending() != 0u && current &&
            std::fabs(current->X() - X()) < 10.0f &&
            std::fabs(current->Y() - Y()) < 10.0f)
        {
            ChangeAnimation(15);
            return;
        }

        if (balloonTargetMatches(this, current) &&
            ammoCount() > 0 &&
            (behaviorFlags() & 1) != 0)
        {
            if (SPRITE* const candidate = SeekEnemy())
                SetCommand(4, candidate);
        }
    }


    int BALLOON::SetCommand(int action, SPRITE* target) noexcept
    {

        SPRITE* const previous = Goal();
        m_attachmentTransitionPending = 0;
        if (balloonTargetMatches(this, previous))
            static_cast<ENGINE*>(previous)->setBusy(0);
        if (balloonTargetMatches(this, target))
            static_cast<ENGINE*>(target)->setBusy(1);
        return SPRITE::SetCommand(action, target);
    }



}
