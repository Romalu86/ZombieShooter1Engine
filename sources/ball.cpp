#include "ball.h"

#include "core/application.h"
#include "core/log.h"
#include "graph.h"
#include "map.h"
#include "mouse.h"
#include "vid/vid.h"
#include "zs1/zScriptExec_engine.h"

#include <cmath>
#include <cstdint>

namespace as1
{

    namespace
    {
        __forceinline int ballFloorToInt(double value) noexcept
        {
            int result = static_cast<int>(value);
            if (static_cast<double>(result) > value)
                --result;
            return result;
        }

        __forceinline float ballAbs(float value) noexcept
        {


            return std::fabs(value);
        }


        __declspec(noinline) std::uint8_t ballDirectionFromRadians(float radians) noexcept
        {
            constexpr float kPi = 3.1415927410125732421875f;
            const double scaled = static_cast<double>(radians / kPi * 128.0f + 0.5f);
            int value = ballFloorToInt(scaled);
            if (value >= 0x100)
                value -= ((value >> 8) << 8);
            if (value < 0)
                value += (((0xFF - value) >> 8) << 8);
            return static_cast<std::uint8_t>(value);
        }


        __declspec(noinline) float ballQuantizedBounceRadians(float deltaX, float span) noexcept
        {
            using namespace zs1::script_engine;
            const float requested = g_angleRadians * deltaX / span;
            if (g_angleSecond < 2.0f)
                g_angleSecond = 2.0f;

            const float outputStep = g_angleRadians / (g_angleSecond - 1.0f);
            const float thresholdStep = g_angleRadians / g_angleSecond;
            float output = 0.0f;
            float threshold = 0.0f;
            const int count = static_cast<int>(g_angleSecond);
            for (int i = 0; i < count; ++i)
            {
                threshold += thresholdStep;
                if (threshold >= ballAbs(requested))
                    return requested >= 0.0f ? output : -output;
                output += outputStep;
            }
            return output;
        }

        __forceinline SPRITE* activeFlagman(MAP* map) noexcept
        {
            if (!map)
                return nullptr;
            return map->flagmanSpriteForPlayer(static_cast<int>(core::ActivePlayerIndex()));
        }

        __forceinline std::uint8_t ballAngleDifference(std::uint8_t a, std::uint8_t b) noexcept
        {
            const std::uint8_t ab = static_cast<std::uint8_t>(a - b);
            const std::uint8_t ba = static_cast<std::uint8_t>(b - a);
            return ab < ba ? ab : ba;
        }

        __forceinline bool ballIsOneShotVid(int nvid) noexcept
        {
            return nvid == 0x1FA || nvid == 0x1FB || nvid == 0x1FC;
        }

        __forceinline std::uint8_t ballNudgeDirection(std::uint8_t direction) noexcept
        {
            if (direction < 0x7D)
                ++direction;
            else if (direction > 0x82)
                --direction;
            return direction;
        }

        __forceinline bool ballSpecialUnderObject(const BALL* ball, const SPRITE* blocker) noexcept
        {
            if (!blocker || !blocker->Vid() || !ball->Vid())
                return false;
            if (blocker->Vid()->nvid() != 0x5F8 || ball->Vid()->deathDamageMinimumRawBits() >= 30)
                return false;
            const ANGLE toBlocker = DirectionFromFloatXY(
                blocker->X() - ball->X(), blocker->Y() - ball->Y());
            return ballAngleDifference(toBlocker.value, blocker->Direction().value) < 0x46;
        }

        __forceinline bool ballUseAmplifiedImpactDamage(const BALL* ball, const SPRITE* blocker) noexcept
        {
            if (!blocker || !blocker->Vid() || !ball->Vid() || blocker->Vid()->nvid() != 0x5F8)
                return false;
            if (ball->Vid()->deathDamageMinimumRawBits() >= 30)
                return true;
            const ANGLE toBlocker = DirectionFromFloatXY(
                blocker->X() - ball->X(), blocker->Y() - ball->Y());
            return ballAngleDifference(toBlocker.value, blocker->Direction().value) >= 0x46;
        }

        __forceinline std::uint8_t ballLowByteOfFloat(float value) noexcept
        {
            union { float f; std::uint32_t u; } bits{};
            bits.f = value;
            return static_cast<std::uint8_t>(bits.u);
        }

    }


    __declspec(noinline) float BALL::flagmanSpan() const noexcept
    {
        MAP* const map = mapOwner();
        SPRITE* const flagman = activeFlagman(map);
        SPRITE* const child = flagman ? flagman->childChain() : nullptr;
        if (!child || !child->Vid() || !Vid())
            return 0.0f;
        return child->Vid()->halfSizeX() + Vid()->halfSizeX();
    }

    BALL::BALL(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent)
        : SPRITE(owner, vid, xyz, direction, parent)
    {

        m_ballLastX = m_xyz.x;
        m_ballLastY = m_xyz.y;
        m_ballUnderTime = 0u;
        m_ballState = 0;
        m_ballImpactCount = 0;
    }


    void BALL::MoveTact()
    {
        using namespace zs1::script_engine;

        constexpr float kLeftLimit = 7.0f;
        constexpr float kLeftReturn = 17.0f;
        constexpr float kRightLimit = 778.0f;
        constexpr float kRightReturn = 768.0f;
        constexpr float kTopLimit = 10.0f;
        constexpr float kProbeRadius = 12.0f;

        GRAPH* const graph = Graph;
        MAP* const map = mapOwner();
        const float cameraY = graph ? graph->cameraY() : 0.0f;

        if (m_xyz.x <= kLeftLimit)
        {
            ChangeCoor(kLeftReturn, m_xyz.y, m_xyz.z);
            ChangeDirection(ANGLE(static_cast<std::uint8_t>(0x40)));
        }
        if (m_xyz.x >= kRightLimit)
        {
            ChangeCoor(kRightReturn, m_xyz.y, m_xyz.z);
            ChangeDirection(ANGLE(static_cast<std::uint8_t>(0xC0)));
        }

        if (m_xyz.y - m_xyz.z - cameraY <= kTopLimit)
        {
            const std::uint8_t oldDirection = m_direction.value;
            std::uint8_t newDirection = static_cast<std::uint8_t>(-oldDirection);
            if (m_xyz.y - rawDirectionCos(newDirection) * kProbeRadius - m_xyz.z - cameraY <= kTopLimit)
                newDirection = static_cast<std::uint8_t>(0x80 - oldDirection);
            newDirection = ballNudgeDirection(newDirection);

            (void)CreateChildFor(11, nullptr);
            ChangeCoor(m_xyz.x, m_xyz.y + 1.0f, m_xyz.z);
            ChangeDirection(ANGLE(newDirection));

            const int nvid = m_vid ? m_vid->nvid() : -1;
            if (nvid == 0x5F3)
            {
                Action(0x6E, 0, 0, 0);
                ChangeAnimation(15);
            }
            else if (ballIsOneShotVid(nvid))
            {
                SetBonusBallToChange(static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(this))));
            }
        }

        float newX = 0.0f;
        float newY = 0.0f;
        float newZ = 0.0f;
        computeNextMovementPosition(&newX, &newY, &newZ);

        if (m_xyz.x != newX || m_xyz.y != newY)
        {
            SPRITE* const under = CanPlace(m_xyz.x, m_xyz.y, m_xyz.z);
            if (under)
            {
                SPRITE* const flagman = activeFlagman(map);
                if (!flagman || under->parentSprite() != flagman)
                {
                    LOG::ResourceError("SPRITE %i", 10, "ball under object",
                        directionIndex(), m_vid ? m_vid->nvid() : -1);
                    m_ballUnderTime = core::CurrentTimeMilliseconds();

                    const bool special = ballSpecialUnderObject(this, under);
                    if (under != mouseSprite() && under->Vid() && under->Vid()->maximumHp() != 0 && !special)
                    {
                        under->Action(0x55, under->Vid()->maximumHp(),
                            static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this))), 0);
                    }
                    ChangeCoor(newX, newY, newZ);
                    goto post_move;
                }
            }
        }

        if (m_xyz.x == newX && m_xyz.y == newY)
            goto post_move;

        if (static_cast<std::uint32_t>(core::CurrentTimeMilliseconds() - m_ballUnderTime) < 1000u)
            LOG::ResourceError("SPRITE %i", 10, "after under",
                directionIndex(), m_vid ? m_vid->nvid() : -1);

        (void)traceMovementCollisionTo(&newX, &newY, &newZ);

        {
            const float dx = newX - m_xyz.x;
            const float dy = newY - m_xyz.y;
            const float dz = newZ - m_xyz.z;
            float candidateX = 0.0f;
            float candidateY = 0.0f;
            float candidateZ = 0.0f;
            SPRITE* blocker = nullptr;

            candidateX = m_xyz.x + dx * 0.25f;
            candidateY = m_xyz.y + dy * 0.25f;
            candidateZ = m_xyz.z + dz * 0.25f;
            blocker = CanPlaceWithCrush(candidateX, candidateY, candidateZ);
            if (!blocker)
            {
                ChangeCoor(candidateX, candidateY, candidateZ);

                candidateX = m_xyz.x + dx * 0.5f;
                candidateY = m_xyz.y + dy * 0.5f;
                candidateZ = m_xyz.z + dz * 0.5f;
                blocker = CanPlaceWithCrush(candidateX, candidateY, candidateZ);
                if (!blocker)
                {
                    ChangeCoor(candidateX, candidateY, candidateZ);

                    candidateX = m_xyz.x + dx * 0.75f;
                    candidateY = m_xyz.y + dy * 0.75f;
                    candidateZ = m_xyz.z + dz * 0.75f;
                    blocker = CanPlaceWithCrush(candidateX, candidateY, candidateZ);
                    if (!blocker)
                    {
                        ChangeCoor(candidateX, candidateY, candidateZ);

                        candidateX = m_xyz.x + dx;
                        candidateY = m_xyz.y + dy;
                        candidateZ = m_xyz.z + dz;
                        blocker = CanPlaceWithCrush(candidateX, candidateY, candidateZ);
                        if (!blocker)
                        {
                            ChangeCoor(candidateX, candidateY, candidateZ);
                            m_ballState = 0;
                            goto post_move;
                        }
                    }
                }
            }

            if (CanPlace(m_xyz.x, m_xyz.y, m_xyz.z))
            {
                SPRITE* const flagman = activeFlagman(map);
                if (flagman && blocker && blocker->parentSprite() == flagman)
                {
                    ChangeCoor(m_xyz.x + dx,
                        blocker->Y() - blocker->Vid()->sizeY() * 0.5f -
                        m_vid->sizeY() * 0.5f - 1.0f,
                        m_xyz.z + dz);
                    m_ballState = 0;
                    goto post_move;
                }
            }

            if (static_cast<std::uint32_t>(core::CurrentTimeMilliseconds() - m_ballUnderTime) < 1000u)
                LOG::ResourceError("SPRITE %i", 10, "block after under",
                    directionIndex(), m_vid ? m_vid->nvid() : -1);

            SPRITE* const flagman = activeFlagman(map);
            if (flagman && blocker && (blocker == flagman || blocker->parentSprite() == flagman))
            {
                m_ballState = 0;
                if (AnimateVehicleEnabled())
                {
                    blocker->ChangeAnimation(7);
                }
                else
                {
                    const int eventFunction = m_vid->scriptFunctionAt(7);
                    if (eventFunction >= 0)
                    {
                        const int selfHandle = static_cast<int>(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(this)));
                        (void)core::Application::callScriptFunction(eventFunction, selfHandle, 0, 0);
                    }
                }

                const float deltaX = candidateX - blocker->X();
                std::uint8_t newDirection = ballDirectionFromRadians(
                    ballQuantizedBounceRadians(deltaX, flagmanSpan()));

                if (newDirection < 1)
                {
                    newDirection = 1;
                    (void)CreateChildFor(9, nullptr);
                }
                else if (newDirection > 0xFE)
                {
                    newDirection = 0xFE;
                    (void)CreateChildFor(9, nullptr);
                }
                else if (newDirection > 0x37 && newDirection <= 0x80)
                {
                    newDirection = 0x37;
                    (void)CreateChildFor(9, nullptr);
                }
                else
                {
                    if (newDirection > 0x80 && newDirection < 0xC8)
                        newDirection = 0xC8;
                    (void)CreateChildFor(9, nullptr);
                }

                newDirection = ballNudgeDirection(newDirection);
                ChangeDirection(ANGLE(newDirection));
                goto post_move;
            }

            std::uint8_t newDirection = 0;
            if (m_xyz.x == m_ballLastX && m_xyz.y == m_ballLastY)
            {
                LOG::ResourceError("SPRITE %i", 10, "same coor",
                    directionIndex(), m_vid ? m_vid->nvid() : -1);
                if (m_ballState == 2)
                    newDirection = static_cast<std::uint8_t>(m_direction.value + 0x22);
                else
                    newDirection = ballLowByteOfFloat(dy);
                m_ballState = 2;
                newDirection = ballNudgeDirection(newDirection);
                ChangeDirection(ANGLE(newDirection));
                goto post_move;
            }

            m_ballLastX = m_xyz.x;
            m_ballLastY = m_xyz.y;
            newDirection = static_cast<std::uint8_t>(-m_direction.value);
            if (CanPlace(
                    m_xyz.x + rawDirectionSin(newDirection) * kProbeRadius,
                    m_xyz.y - rawDirectionCos(newDirection) * kProbeRadius,
                    m_xyz.z))
            {
                newDirection = static_cast<std::uint8_t>(0x80 - m_direction.value);
            }

            const bool amplifiedDamage = ballUseAmplifiedImpactDamage(this, blocker);
            if (blocker && blocker != mouseSprite() && blocker->Vid() && blocker->Vid()->maximumHp() != 0)
            {
                int damage = m_vid->deathDamageMinimumRawBits();
                if (amplifiedDamage)
                    damage *= 100;
                blocker->Action(0x55, damage,
                    static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this))), 0);
            }

            if ((blocker != mouseSprite() || m_ballImpactCount >= 3) && m_vid->nvid() == 0x5F3)
            {
                Action(0x6E, 0, 0, 0);
                ChangeAnimation(15);
            }

            if (blocker == mouseSprite())
            {
                (void)CreateChildFor(11, nullptr);
                ++m_ballImpactCount;
            }
            else if (blocker)
            {
                (void)CreateChildFor(12, nullptr);
                if (blocker->Vid() && blocker->Vid()->declaredAnimationFrameCount(7) != 0 &&
                    (blocker->Animation() == 0 || blocker->Animation() == 2))
                {
                    blocker->ChangeAnimation(12);
                }
                else
                {
                    (void)blocker->CreateChildFor(12, nullptr);
                }
            }

            if (ballIsOneShotVid(m_vid->nvid()))
            {
                SetBonusBallToChange(static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(this))));
            }

            m_ballState = 1;
            newDirection = ballNudgeDirection(newDirection);
            ChangeDirection(ANGLE(newDirection));
        }

    post_move:
        {
            if (!graph || !m_vid)
                return;
            const int nvid = m_vid->nvid();
            const float screenY = m_xyz.y - m_xyz.z - graph->cameraY();
            const float bottom = graph->viewportBottom() - 25.0f;

            if (HasEnergyShield() && nvid != 0x5CA && nvid != 0x5F3 && screenY >= bottom)
            {
                ClearEnergyShield();
                LOG::Write("was direction = %i", static_cast<int>(m_direction.value));
                ChangeCoor(m_xyz.x, m_xyz.y - 1.0f, m_xyz.z);
                ChangeDirection(ANGLE(static_cast<std::uint8_t>(0x80 - m_direction.value)));
                LOG::Write("new direction = %i", static_cast<int>(m_direction.value));
                return;
            }

            if (screenY < bottom || nvid == 0x5CA || m_direction.value < 0x40 || m_direction.value > 0xC0)
                return;

            LOG::Write("ball death direction = %i", static_cast<int>(m_direction.value));
            ChangeCoor(m_xyz.x, graph->viewportBottom() - 5.0f + graph->cameraY(), m_xyz.z);
            Stop();
            ChangeAnimation(15);
        }
    }

}
