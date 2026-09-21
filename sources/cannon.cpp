#include "cannon.h"
#include "map.h"
#include "constant.h"
#include "core/application.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "graphics/angle.h"
#include "vid/vid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace as1
{

    namespace
    {
        __forceinline bool cannonLessOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs < rhs;
        }

        __forceinline bool cannonLessEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs <= rhs;
        }

        __forceinline bool cannonEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs == rhs;
        }

        __forceinline bool cannonOrderedGreaterEqual(double lhs, double rhs) noexcept
        {

            return !std::isnan(lhs) && !std::isnan(rhs) && lhs >= rhs;
        }

        __forceinline bool cannonOrderedNotEqual(double lhs, double rhs) noexcept
        {

            return !cannonEqualOrUnordered(lhs, rhs);
        }

        __forceinline bool cannonNotEqualOrUnordered(double lhs, double rhs) noexcept
        {
            return std::isnan(lhs) || std::isnan(rhs) || lhs != rhs;
        }

        __forceinline bool cannonOrderedLess(double lhs, double rhs) noexcept
        {
            return !std::isnan(lhs) && !std::isnan(rhs) && lhs < rhs;
        }

        __forceinline bool cannonContinuousZCross(double targetZ, double previousZ, double currentZ) noexcept
        {
            if (cannonLessOrUnordered(previousZ, currentZ))
            {
                if (cannonLessOrUnordered(targetZ, previousZ))
                    return false;
                return cannonLessEqualOrUnordered(targetZ, currentZ);
            }

            if (cannonLessOrUnordered(targetZ, currentZ))
                return false;
            return cannonLessEqualOrUnordered(targetZ, previousZ);
        }
    }


    CANNON::CANNON(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : SPRITE(owner, vid, xyz, direction, parent)
    {


        m_cannonMotionFlags |= 1;

        const float minZSpeed = vid->maximumZSpeed();
        const float maxZSpeed = vid->randomZSpeedValue();
        if (cannonOrderedNotEqual(minZSpeed, maxZSpeed))
        {
            const float random01 = static_cast<float>(std::rand()) / 32767.0f;
            setZSpeedDirect(minZSpeed + (maxZSpeed - minZSpeed) * random01);
            StartMove();
            return;
        }

        setZSpeedDirect(minZSpeed);
        if (parent && parent->Animation() >= 15 &&
            cannonLessOrUnordered(vid->topZValue(), 0.0f))
        {
            ChangeSpeed(MaxSpeed() + parent->Speed());
        }
        StartMove();
    }

    int CANNON::Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier)
    {
        const int argument1 = static_cast<int>(argument1Carrier);
        if (opcode != 0x82)
            return SPRITE::Action(opcode, static_cast<std::intptr_t>(argument1), argument2Carrier, argument3Carrier);

        const int animation = Animation();
        if (animation == 8)
        {
            ChangeAnimation(15);
            return 0;
        }

        if (animation < 15)
        {

            if (cannonOrderedLess(Z(), -100.0f))
            {
                ChangeAnimation(16);
                return 0;
            }


            if (cannonNotEqualOrUnordered(Speed(), 0.0f))
            {
                ChangeAnimation(2);
                return 0;
            }

            if (animation >= 7 && animation != 10)
                ChangeAnimation(0);
        }
        return 0;
    }


    void CANNON::MoveTact()
    {
        VID* const vid = Vid();
        MAP* const owner = mapOwner();
        if (!vid->movementTactEnabled())
            return;

        VECTOR candidate{X(), Y(), Z()};
        computeNextMovementPosition(&candidate.x, &candidate.y, &candidate.z);

        if ((runtimeFlags() & 0x00000400u) != 0u && Animation() < 15)
            ChangeAnimation(15);

        const float currentGround = owner->GetGroundZ(VECTOR2{X(), Y()});
        const float candidateGround = owner->GetGroundZ(VECTOR2{candidate.x, candidate.y});
        const float movePlane = candidateGround + vid->moveUpZ();
        const float previousZ = Z();

        if (cannonLessEqualOrUnordered(candidate.z, candidateGround) &&
            cannonOrderedGreaterEqual(Z(), currentGround))
        {


            candidate.z = Z();

            if (Animation() >= 15)
            {
                Stop();
                setZSpeedDirect(0.0f);
                if (candidateGround > currentGround)
                    (void)CreateChildFor(11, nullptr);
            }
            else if ((vid->properties() & P_BOUNCE) == 0u)
            {
                Stop();
                setZSpeedDirect(0.0f);
                (void)CreateChildFor(candidateGround > currentGround ? 11 : 12, nullptr);
                ChangeAnimation(15);
            }
            else if (candidateGround > currentGround)
            {
                ChangeAnimation(11);
                ChangeDirection(ANGLE(static_cast<unsigned char>(directionIndex() + 0x80)));
            }
            else
            {
                const float zSpeed = ZSpeed();
                if (cannonOrderedLess(zSpeed, -0.022f))
                {
                    ChangeAnimation(12);
                    setZSpeedDirect(zSpeed * -0.5f);
                }
                else if (cannonLessEqualOrUnordered(zSpeed, 0.005f))
                {
                    setZSpeedDirect(0.0f);
                    ChangeAnimation(15);
                }
                else
                {
                    setZSpeedDirect(zSpeed * 0.5f);
                    ChangeSpeed(Speed() * 0.5f);
                    ChangeDirection(ANGLE(static_cast<unsigned char>(directionIndex() + 0x80)));
                }
            }


            candidate.x = X();
            candidate.y = Y();
        }
        else if (cannonOrderedNotEqual(Z(), candidate.z) &&
                 (vid->properties() & P_GRAVITY) == 0u &&
                 cannonOrderedNotEqual(movePlane, 0.0f))
        {
            bool clampToMovePlane = false;
            if (cannonLessOrUnordered(Z(), movePlane))
            {
                clampToMovePlane = !cannonLessOrUnordered(candidate.z, movePlane);
            }
            else if (cannonLessEqualOrUnordered(Z(), movePlane))
            {

                clampToMovePlane = (vid->properties() & P_SELFMOVING) == 0u;
            }
            else
            {
                clampToMovePlane = cannonLessOrUnordered(candidate.z, movePlane);
            }

            if (clampToMovePlane)
            {
                candidate.z = movePlane;
                setZSpeedDirect(0.0f);
            }
        }

        if (cannonOrderedNotEqual(X(), candidate.x) ||
            cannonOrderedNotEqual(Y(), candidate.y))
        {


            const VECTOR start{X(), Y(), Z()};

            const auto pointAt = [&start, &candidate](float fraction) noexcept -> VECTOR
            {
                return VECTOR{
                    start.x + (candidate.x - start.x) * fraction,
                    start.y + (candidate.y - start.y) * fraction,
                    start.z + (candidate.z - start.z) * fraction};
            };

            bool skipFinalCollision = false;
            if (Animation() < 15 &&
                !cannonLessEqualOrUnordered(
                    static_cast<float>(approximatePlanarDistance(
                        candidate.x - start.x, candidate.y - start.y)),
                    30.0f))
            {
                if (cannonLessEqualOrUnordered(
                        static_cast<float>(approximatePlanarDistance(
                            candidate.x - start.x, candidate.y - start.y)),
                        50.0f))
                {
                    const VECTOR half = pointAt(0.5f);
                    if (CanPlaceWithCrush(half.x, half.y, half.z) != nullptr)
                    {
                        ChangeAnimation(15);
                        skipFinalCollision = true;
                    }
                    else
                    {


                        ChangeCoor(half.x, half.y, half.z);
                    }
                }
                else
                {
                    const VECTOR quarter = pointAt(0.25f);
                    if (CanPlaceWithCrush(quarter.x, quarter.y, quarter.z) != nullptr)
                    {


                        ChangeAnimation(15);
                        skipFinalCollision = true;
                    }
                    else
                    {
                        const VECTOR half = pointAt(0.5f);
                        if (CanPlaceWithCrush(half.x, half.y, half.z) != nullptr)
                        {


                            ChangeCoor(quarter.x, quarter.y, quarter.z);
                            ChangeAnimation(15);
                            skipFinalCollision = true;
                        }
                        else
                        {
                            const VECTOR threeQuarter = pointAt(0.75f);
                            if (CanPlaceWithCrush(threeQuarter.x, threeQuarter.y, threeQuarter.z) != nullptr)
                            {


                                ChangeCoor(half.x, half.y, half.z);
                                ChangeAnimation(15);
                                skipFinalCollision = true;
                            }
                            else
                            {


                                ChangeCoor(threeQuarter.x, threeQuarter.y, threeQuarter.z);
                            }
                        }
                    }
                }
            }

            if (!skipFinalCollision)
            {
                if (CanPlaceWithCrush(candidate.x, candidate.y, candidate.z) != nullptr)
                {
                    if (Animation() < 15)
                        ChangeAnimation(15);


                    candidate.z = Z();
                }
                else
                {
                    ChangeCoor(candidate.x, candidate.y, candidate.z);
                    if (Animation() < 15 &&
                        (vid->properties() & P_GRAVITY) == 0u &&
                        (cannonLessOrUnordered(X(), -220.0f) ||
                         cannonLessOrUnordered(Y(), -200.0f) ||
                         cannonLessOrUnordered(owner->SizeX() + 220.0f, X()) ||
                         cannonLessOrUnordered(owner->SizeY() + 200.0f, Y())))
                        ChangeAnimation(15);
                }
            }
        }
        if (cannonOrderedNotEqual(Z(), candidate.z))
            ChangeCoor(X(), Y(), candidate.z);

        SPRITE* const target = Goal();
        if (!target)
        {


            if (cannonOrderedLess(vid->topZValue(), 0.0f) &&
                cannonOrderedLess(ZSpeed(), 0.0f))
            {
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                RotateTact(directionIndex() + 32, deltaMs);
            }
            return;
        }

        const float dx = std::fabs(target->X() - X());
        const float dy = std::fabs(target->Y() - Y());
        const float distance = cannonLessEqualOrUnordered(dx, dy)
            ? static_cast<float>(dy + dx * 0.5f)
            : static_cast<float>(dx + dy * 0.5f);

        if ((vid->properties() & P_SELFMOVING) != 0u)
        {
            if ((m_cannonMotionFlags & 1) != 0)
            {
                const float ground = owner->GetGroundZ(VECTOR2{X(), Y()});
                if ((cannonLessEqualOrUnordered(ground + vid->moveUpZ(), Z()) &&
                     cannonLessEqualOrUnordered(target->Z(), Z())) ||
                    cannonLessEqualOrUnordered(ZSpeed(), 0.0f))
                    m_cannonMotionFlags &= ~1;
                else
                {
                    const CONSTANT* const constants = g_baseConstants;
                    float gravity = 0.0f;
                    std::memcpy(&gravity, &constants->raw[2], sizeof(gravity));
                    const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                    setZSpeedDirect(ZSpeed() - static_cast<float>(deltaMs) * gravity);
                }
            }
            else
            {
                const int reverse = cannonLessOrUnordered(Speed(), 0.0f) ? 0x80 : 0;
                const int desired = (DirectionFromFloatXY(
                    target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
                const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
                const int turn = RotateTact(ANGLE(static_cast<unsigned char>(desired)), deltaMs).Int();
                if (distance > 10.0f && distance < 30.0f && turn > 70)
                {
                    m_cannonMotionFlags |= 1;
                    StartMove();
                }
                else if (target->Z() >= Z() || cannonEqualOrUnordered(distance, 0.0f))
                {
                    setZSpeedDirect(0.0f);
                }
                else
                {
                    float zSpeed = ((target->Z() - Z()) / distance) / 10.0f;
                    if (cannonLessOrUnordered(zSpeed, -vid->maximumZSpeed()))
                        zSpeed = -vid->maximumZSpeed();
                    setZSpeedDirect(zSpeed);
                }
            }
        }
        else if (distance > 30.0f)
        {
            const int previousDir = directionIndex() & 0xFF;
            const int reverse = cannonLessOrUnordered(Speed(), 0.0f) ? 0x80 : 0;
            const int desired = (DirectionFromFloatXY(
                target->X() - X(), target->Y() - Y()).Int() + reverse) & 0xFF;
            const std::uint32_t deltaMs = core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            RotateTact(desired, deltaMs);
            const int currentDir = directionIndex() & 0xFF;
            const int diffA = (previousDir - currentDir) & 0xFF;
            const int diffB = (currentDir - previousDir) & 0xFF;
            if (std::min(diffA, diffB) > 100)
            {
                Stop();
                ChangeAnimation(15);
            }
        }

        const VID* const targetVid = target->Vid();
        const std::uint32_t flags = runtimeFlags();
        bool xyOverlap = (flags & 0x00018000u) == 0x00018000u;
        if (!xyOverlap)
        {
            xyOverlap = vid->halfSizeX() + targetVid->halfSizeX() > std::fabs(X() - target->X()) &&
                        vid->halfSizeY() + targetVid->halfSizeY() > std::fabs(Y() - target->Y());
        }

        bool overlapping = false;
        if (xyOverlap)
        {
            overlapping = vid->sizeZ() + Z() >= target->Z() &&
                          target->Z() + targetVid->sizeZ() >= Z();
        }


        if (xyOverlap && !overlapping &&
            cannonContinuousZCross(target->Z(), previousZ, Z()))
        {
            overlapping = true;
        }

        if (!overlapping && (vid->properties() & P_SELFMOVING) != 0u)
            overlapping = cannonLessOrUnordered(std::fabs(target->Z() - Z()), 20.0f) &&
                          cannonLessOrUnordered(std::fabs(target->X() - X()), 10.0f) &&
                          cannonLessOrUnordered(std::fabs(target->Y() - Y()), 10.0f);
        if (overlapping)
        {
            Stop();
            ChangeAnimation(15);
        }
    }

    void CANNON::DeletePointerToSprite(SPRITE* target)
    {
        if (target && Goal() == target)
        {
            SPRITE* const replacement = new (std::nothrow) SPRITE(
                mapOwner(), EmptyVid, target->xyz(), ANGLE(static_cast<unsigned char>(0)), nullptr);
            setGoalSprite(replacement);
        }
        SPRITE::DeletePointerToSprite(target);
    }


}
