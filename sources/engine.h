#pragma once

#include "unit.h"

namespace as1
{
    using R_POS = core::R_POS;

    struct TRAIN_INFO;

    class ENGINE : public UNIT
    {
    public:

        static int globaldeleting;
        ENGINE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& direction, SPRITE* parent = nullptr);
        ~ENGINE() override;
        void MoveTact() override;
        void MoveEngineTact() noexcept;
        void Stop() noexcept;
        void SetCommandToTrain(int command, int targetCarrier, int dotTargetCarrier, int dotTarget2Carrier) noexcept;
        void SetCommandToTrain(int command, int x, int y) noexcept;

        void ReverseTrain() noexcept;

        void ReCalcMoveParameters() noexcept;

        ENGINE* GetIntersecting() noexcept;

        int IsTouch(ENGINE* engine, int fromMoveTact) noexcept;

        void ClearDotBusy() noexcept;

        void SetDotBusy() noexcept;

        void PullTail(const R_POS* oldHead) noexcept;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void DeletePointerToSprite(SPRITE* sprite) override;
        void DrawDebugOverlay() override;
        void DrawRelationDebugOverlay() override;

        int RepairByRepair(ENGINE* target) noexcept;
        void RepairTact() noexcept;
        void AddAmmoTact() noexcept;
        SPRITE* findEngineChainSpecialWeaponNode() noexcept;
        int engineChainContainsArmy(int bucket) noexcept;
        int updateCombatDecision() noexcept;
        void stopEngineChain() noexcept;
        void setEnginePathGoalFromCoordinates(float x, float y, float z, int unused, int project2D) noexcept;

        SPRITE* chainPrevious() const noexcept { return engineChainPrevious(); }
        SPRITE* chainNext() const noexcept { return engineChainNext(); }
        int productionBatchCompletionPending() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::ProductionBatchCompletionPending);
        }

        int isBusy() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineBusy);
        }
        void setBusy(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineBusy) = value;
        }

        int repairLinkHp() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineRepairLinkHp);
        }
        void setRepairLinkHp(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineRepairLinkHp) = value;
        }

        int productionSequenceId() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::ProductionSequenceId);
        }

        void setChainPrevious(SPRITE* value) noexcept { setEngineChainPrevious(value); }
        void setChainNext(SPRITE* value) noexcept { setEngineChainNext(value); }
        void setProductionBatchCompletionPending(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::ProductionBatchCompletionPending) = value;
        }
        void setProductionSequenceId(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::ProductionSequenceId) = value;
        }

    private:

        std::array<std::uint8_t, 0xA34> m_reservedTail8C;
    };


    struct TRAIN_INFO
    {
        std::uint32_t flags = 0;
        float maxBattleRange = 0.0f;
        float minBattleRange = 999999.0f;
        float power = 0.0f;
        float weight = 0.0f;
        float trainweight = 0.0f;
        int speed = 10000;
        int no = 0;
        int hp = 0;
        int max_hp = 0;
        int weapon = 0;
        int build_time = 0;
        int noAmmo = 0;
        int ammo = 0;
        int maxAmmo = 0;
        int percentAmmo = 0;


        explicit TRAIN_INFO(const ENGINE* engine) noexcept;

        void AddEngine(const ENGINE* engine) noexcept;
        int CanMove() const noexcept;
        int HaveAmmo() const noexcept;
        int IsDamaged() const noexcept;
        int NeedAmmo() const noexcept;
        int HaveAmmoWagon() const noexcept { return (flags & 1u) != 0u; }
        int HaveRepair() const noexcept { return (flags & 2u) != 0u; }
        int Acceleration() const noexcept;
    };


}
