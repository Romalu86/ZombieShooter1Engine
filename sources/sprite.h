#pragma once
#include "sprite_ptr.h"
#include "core/types.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include <vector>
#include <string>
#include <array>
#include <map>
#include <cstddef>
#include <cstdint>
#include <memory>


namespace as1
{
    int Random(int interval);

    class INPUT;


    extern int g_pathSearchSecondaryBestCost;
    extern int g_pathSearchResultScore;
    class SPRITE;
    template <int Indexed> class BaseSpriteList;
    namespace core { template <class T> class List; class R_DOT; struct R_POS; }
    class VID;
    class MAP;
    class MENU;
    extern MAP* Map;
    class GRAPH;
    class STRING;
    class BaseStream;

    namespace SpriteLayout
    {
        constexpr std::size_t CommandSerializationBias = 0x3Cu;
        constexpr std::size_t SharedPrimaryState = 0x70u;
        constexpr std::size_t SharedSecondaryState = 0x74u;
        constexpr std::size_t ExtendedStateBase = 0x84u;
        constexpr std::size_t LegacyCommandState1 = 0x84u;
        constexpr std::size_t AmmoFixedPoint = 0x78u;
        constexpr std::size_t LegacyCommandState2 = 0x7Cu;
        constexpr std::size_t TurnTimer = 0x80u;
        constexpr std::size_t BehaviorFlags = 0x88u;

        constexpr std::size_t LinkerX = 0x70u;
        constexpr std::size_t LinkerY = 0x74u;
        constexpr std::size_t LinkerZ = 0x78u;
        constexpr std::size_t LinkerDirection = 0x7Cu;
        constexpr std::size_t LinkerOwner = 0x80u;
        constexpr std::size_t DerivedStateBase = 0x8Cu;


        constexpr std::size_t EngineChainPrevious = 0x90u;
        constexpr std::size_t EngineChainNext = 0x94u;
        constexpr std::size_t EngineCommandReferenceOwner = 0x98u;
        constexpr std::size_t EngineCommandArgument0 = 0x9Cu;
        constexpr std::size_t EngineCommandArgument1 = 0xA0u;
        constexpr std::size_t EngineCommandArgument2 = 0xA4u;
        constexpr std::size_t EngineAccelerationDelay = 0xA8u;
        constexpr std::size_t EngineTargetSpeed = 0xACu;
        constexpr std::size_t PushLineActive = 0xB0u;
        constexpr std::size_t EngineRepairLinkHp = 0xB4u;
        constexpr std::size_t PrimaryPathNode = 0xB8u;
        constexpr std::size_t PrimaryPathProgress = 0xBCu;
        constexpr std::size_t PrimaryPathAuxiliary = 0xC0u;
        constexpr std::size_t PrimaryPathEdgeIndex = 0xC4u;
        constexpr std::size_t SecondaryPathNode = 0xC8u;
        constexpr std::size_t SecondaryPathProgress = 0xCCu;
        constexpr std::size_t SecondaryPathAuxiliary = 0xD0u;
        constexpr std::size_t SecondaryPathEdgeIndex = 0xD4u;
        constexpr std::size_t EngineBusy = 0xD8u;
        constexpr std::size_t ProductionBatchCompletionPending = 0xDCu;
        constexpr std::size_t PreviousPathX = 0xE0u;
        constexpr std::size_t PreviousPathY = 0xE4u;
        constexpr std::size_t PreviousPathZ = 0xE8u;
        constexpr std::size_t RouteActionReady = 0xECu;
        constexpr std::size_t RouteActionStartTime = 0xF0u;
        constexpr std::size_t ProductionSequenceId = 0xF4u;
        constexpr std::size_t PathBuffer = 0xF8u;
        constexpr std::size_t PathBufferSize = 0xABCu;
        constexpr std::size_t WordStride = sizeof(std::uint32_t);
    }

    enum SpriteTypeMask : DWORD
    {
        U_TERRAIN = 1,
        U_OBJECT = 2,
        U_UNIT = 4,
        U_MONSTER = 4,
        U_AVIA = 8,
        U_MENU = 16,
        U_RAILWAY = 32,
        U_REGION = 64,
        U_CANNON = 512,
        U_SPRITE = 1024
    };

    enum SpriteClassId : DWORD
    {

        B_TERRAIN = 0,
        B_OBJECT = 1,
        B_UNIT = 2,
        B_BUILDING = 3,
        B_AVIA = 4,
        B_CANNON = 5,
        B_PRIMITIVE = 6,
        B_MAN = 7,
        B_BUILDEDTERRAIN = 8,
        B_SPRITE = 9,
        B_FRAME = 10,
        B_BALL = 11,
        B_LINKER = 12,
        B_TEXT = 19,
        B_CIV_ROBOT = 20,
        B_ENGINE = 21,
        B_RAIL = 22,
        B_REGION = 23,
        B_DEPO = 24,
        B_CREATURE = 25,
        B_BALLOON = 26,
        B_MISSILE = 27
    };

    enum ObjectPropertyFlag : DWORD
    {
        P_RANDBIRTH = 1u << 0,
        P_GRAVITY = 1u << 1,
        P_GRAVITY2 = 1u << 2,
        P_BUILDSIZETOGRIDZ = 1u << 3,
        P_TRACK = 1u << 4,
        P_BUILDVIDZTOGRIDZ = 1u << 5,
        P_HASH = 1u << 6,
        P_MAP = 1u << 6,
        P_BIRTHASSMOKE = 1u << 7,
        P_NOISE = 1u << 8,
        P_ZEROZ = 1u << 9,
        P_RANDSPEED = 1u << 10,
        P_GAMMA = 1u << 11,
        P_WIND = 1u << 12,
        P_SKIPMAPED = 1u << 13,
        P_CRUSH = 1u << 14,
        P_ALWAYSTOP = 1u << 15,
        P_WAVE = 1u << 16,
        P_INVISIBLEFORENEMY = 1u << 17,
        P_CREATECHILDEND = 1u << 18,
        P_VERTDIR = 1u << 19,
        P_MOVEWITHANYDIRECTION = 1u << 20,
        P_BLUR = 1u << 21,
        P_RANDZSPEED = 1u << 22,
        P_DBLLIGHT = 1u << 23,
        P_ONEPHASE = 1u << 24,
        P_NOTCHANGELINKERCOOR = 1u << 25,
        P_RADIALDAMAGE = 1u << 26,
        P_SELFMOVING = 1u << 27,
        P_BOUNCE = 1u << 28,
        P_HARDWAREDIRECT = 1u << 29,
        P_GROUND = 1u << 30,
        P_MAPPEDBUILD = 1u << 30,
        P_NOTDAMAGEFORFRIEND = 1u << 31
    };

    struct RestoreCommand
    {
        std::string layout;
        std::uint32_t opcode = 0;
        std::array<std::uint32_t, 4> raw{};
        std::array<SPRITE*, 3> resolvedSpriteArgs{};
        bool opcodeFromExportLgc = false;
        bool opcodeLooksLikeAnimation = false;
    };

    struct ACT
    {
        std::uint32_t opcode = 0;
        std::uint32_t argument1 = 0;
        std::uint32_t argument2 = 0;
        std::uint32_t argument3 = 0;
    };
    ACT* copyCommandRecord(ACT* destination, const ACT* source) noexcept;


    namespace core
    {


        template <>
        class List<ACT>
        {
        public:
            List();
            List(const List& other);
            List& operator=(const List& other);
            List(List&& other) noexcept;
            List& operator=(List&& other) noexcept;
            ~List();

            void clear();
            __forceinline
            void releaseCommandRecordsTail();
            void clearTargetReferences(SPRITE* target);


            void append(ACT action);

            void insertAt(std::uint32_t index, ACT action);

            void resize(std::uint32_t requiredCapacity);


            __forceinline
            void setCommandRecordCount(std::uint32_t count);
            void serializeCommandRecordsText(STRING& out) const;
            std::string serializeCommandRecordsText() const;
            void parseCommandRecordsText(const STRING& text);
            void queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3);
            void saveCommandRecordsToStream(BaseStream* stream);
            void restoreCommandRecordsFromStream(BaseStream* stream, const SPRITE* ownerSprite);
            bool restoreOldMapCommandRecordsFromStream(BaseStream* stream, int mapVersion, const SPRITE* ownerSprite, int* armyBucket);

            struct CommandRecordStorage
            {
                std::uint32_t words[4];
            };

            struct CommandRecordList
            {
                std::uint32_t vtableTag = 0;
                std::uint32_t count = 0;
                std::uint32_t capacity = 0;
                CommandRecordStorage* records = nullptr;
            };


            int No() const noexcept;

            ACT* operator[](int index) noexcept;
            const ACT* operator[](int index) const noexcept
            {
                return reinterpret_cast<const ACT*>(m_commandRecords.records + index);
            }
            size_t size() const { return m_commandRecords.count; }
            bool empty() const { return m_commandRecords.count == 0; }

        private:
            friend class ::as1::SPRITE;

            __forceinline
            void releaseCommandRecords();
            __forceinline
            void copyCommandRecordsFrom(const List& other);
            __forceinline
            void ensureCommandRecordCapacity(std::uint32_t requiredCapacity);
            __forceinline
            void writeCommandRecord(std::size_t index, const ACT& command);

            CommandRecordList m_commandRecords;
        };
    }


    struct SpriteRestoreState
    {
        std::vector<BYTE> payload;
        std::vector<std::uint32_t> words;
        std::vector<BYTE> tailBytes;
        std::string layout;
        std::vector<RestoreCommand> commands;
        std::vector<std::uint32_t> objectRefs;
        std::vector<std::uint32_t> extraValues;
        std::string textLabel;
        std::string note;
        int actionOpcode = 0;
        int mapVersion = 0;
        bool appliedThroughSpriteAction = false;
        bool actionExecutionEnabled = false;
        bool decodedUsingExportLgcConstants = false;
    };


    struct EX_SPRITE_DATA
    {


        explicit EX_SPRITE_DATA(SPRITE* source) noexcept;

        struct ItemList
        {
            std::uint32_t vtableTag;
            std::uint32_t count;
            std::uint32_t capacity;
            std::int32_t* values;
        };

        float sourceX;
        float sourceY;
        float sourceZ;
        std::int32_t gridFrame;
        std::uint32_t changeCoorTime;
        std::uint32_t lifetimeRemaining;
        std::uint32_t childCadence;
        std::uint32_t effectTimestamp;
        float effectCurvePosition;
        std::uint32_t maxSpeedBits;
        std::uint32_t gammaRaw0;
        std::uint32_t gammaRaw1;
        ItemList items;
        STRING name;
    };



    class SPRITE
    {
    public:
        SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir);
        SPRITE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent);
        virtual ~SPRITE();
        void destroyBaseSpriteState();


        VID* Vid() const { return m_vid; }

        float X() const { return m_xyz.x; }

        float Y() const { return m_xyz.y; }

        float Z() const { return m_xyz.z; }

        float ScreenX() const;

        float ScreenY() const;
        float xCoordinateValue() const noexcept { return m_xyz.x; }
        float yCoordinateValue() const noexcept { return m_xyz.y; }
        const VECTOR& xyz() const { return m_xyz; }


        __declspec(noinline) ANGLE Direction() const;
        int directionIndex() const noexcept { return m_direction.Int(); }
        int directionIndexValue() const noexcept { return m_direction.Int(); }
        int SizeTo(const VECTOR2& target) const;
        ANGLE DirectionTo(const VECTOR2& target) const;

        ANGLE DirectionTo(const SPRITE* sprite) const;
        __declspec(noinline)
        ANGLE DirectionTo(const SPRITE* sprite, float* radius) const;
        int Animation() const { return m_currentAnimation; }
        void setCurrentAnimationDirect(int value) noexcept { m_currentAnimation = value; }
        int currentFrame() const { return m_currentFrame; }
        void setCurrentFrameDirect(int value) noexcept { m_currentFrame = value; }
        void setXPosition(float value) noexcept { m_xyz.x = value; }
        void setYPosition(float value) noexcept { m_xyz.y = value; }
        int currentFrameBegin() const { return m_currentFrameBegin; }

        int currentFrameEnd() const { return m_currentFrameEnd; }
        void ChangeAnimation(int animationId);
        virtual int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier);
        virtual void Tact();
        virtual void MoveTact();
        virtual void DeletePointerToSprite(SPRITE* sprite);
        virtual void Draw();
        virtual void DrawDebugOverlay();
        virtual void DrawRelationDebugOverlay();

        void DrawRectangle();

        float Speed() const { return m_speed; }
        void setSpeedDirect(float value) noexcept { m_speed = value; }
        void ChangeSpeed(float value);
        float ZSpeed() const { return m_zSpeed; }
        void setZSpeedDirect(float value) noexcept { m_zSpeed = value; }
        void syncExDataMaxSpeedFromVid() noexcept;

        float MaxSpeed() const noexcept;

        void SetMoveAnimation();
        DWORD Timer() const { return m_actionTimer; }
        int AddListReference();
        int Release();
        int listReferenceCount() const { return m_listReferenceCount; }
        __forceinline MAP* mapOwner() const noexcept
        {
            return Map;
        }
        VID* vidPointer() const noexcept { return m_vid; }
        void setVidPointerDirect(VID* value) noexcept { m_vid = value; }
        int isSpriteClass(int spriteClass) const noexcept;
        void setListReferenceCount(int value) noexcept { m_listReferenceCount = value; }
        SPRITE* bestTargetSprite() const noexcept { return m_bestTargetSprite; }
        SPRITE* parentSprite() const noexcept { return m_childBacklink; }
        void setBestTargetSprite(SPRITE* value) noexcept { m_bestTargetSprite = value; }
        SPRITE* initializeBaseSprite(VID* vid, float x, float y, float z, int direction, SPRITE* parent) noexcept;
        void SetGamma(const Gamma& raw) noexcept;
        __forceinline int dispatchVirtualAction(std::uint32_t opcode, int argument1, int argument2, int argument3) noexcept
        {
            return Action(static_cast<int>(opcode), static_cast<std::intptr_t>(argument1), argument2, argument3);
        }
        int dispatchVirtualAction(ActionCode opcode, int argument1, int argument2, int argument3) noexcept
        {
            return dispatchVirtualAction(static_cast<std::uint32_t>(opcode), argument1, argument2, argument3);
        }
        void setGoalSprite(SPRITE* goal) noexcept;
        void logSpriteResourceError(int severity, const char* detail, int value) const noexcept;
        __declspec(noinline) int attackTerrainGate(float targetX, float targetY, float targetZ) const noexcept;
        int IsXYCross(const VID* vid, float x, float y) const noexcept;
        int SetCommand(int argument1, SPRITE* goal) noexcept;
        int SetCommandWithoutLink(int argument1, SPRITE* goal) noexcept;

        void Move(SPRITE* goal) noexcept;
        void ChangeDirection(ANGLE direction) noexcept;
        void setDirectionFrameOnly(ANGLE direction) noexcept;
        void updateLinkerCoordinateForDirection(ANGLE direction) noexcept;
        SPRITE* CanPlace(float x, float y, float z);
        SPRITE* CanPlaceWithCrush(float x, float y, float z);
        SPRITE* probeMovementFootprint(float x, float y) noexcept;
        static void initializeStartupTrigTables() noexcept;
        static float rawDirectionSin(int index) noexcept;
        static float rawDirectionSinUnchecked(DWORD index) noexcept;
        static float rawDirectionCos(int index) noexcept;
        static float rawDirectionSinAux(int index) noexcept;
        static float rawDirectionCosAux(int index) noexcept;

        ANGLE GlideDirection(ANGLE value) noexcept;
        ANGLE GlideDirectionScaled(ANGLE value, float footprintScale) noexcept;


        virtual ANGLE RotateTact(ANGLE value, std::uint32_t deltaMs) noexcept;
        int advancePrimitiveFrame() noexcept;

        int AttackTact(int deltaTime) noexcept;

        SPRITE* SeekEnemy();
        float NearDistanceTo(const SPRITE* other) const noexcept;
        float BattleRange() const noexcept;
        int resolvedWeaponEnemyPriority() const noexcept;
        int enemyPriority(float candidateMetric, float selectedMetric, SPRITE* candidate, SPRITE* selected) noexcept;
        void ResetActionStack() noexcept;
        int traceMovementCollisionTo(float* xOut, float* yOut, float* zOut) noexcept;
        void Stop();
        int StartMove() noexcept;
        int PercentHp() noexcept;
        void ChangeHp(int newHp) noexcept;

        void PlaySFX(int nsfx) noexcept;
        int spawnAnimationChild() noexcept;
        int CreateChildFor(int forAnimation, SPRITE* eventObject) noexcept;
        int CanAttackThisSprite(const SPRITE* owner) const noexcept;
        int isCanShotEnemy(SPRITE* owner) const noexcept;
        int canWeaponAffectTarget(SPRITE* owner) noexcept;
        int HaveLink() const noexcept;
        int Attack(SPRITE* owner) noexcept;
        int GetFireDamage() noexcept;
        int inheritAdjacentEngineCommand() noexcept;
        SPRITE* engineChainHead() noexcept;
        SPRITE* engineChainTail() noexcept;
        bool isInEngineChain(SPRITE* target) noexcept;
        SPRITE* findCrossingConstraintOwner() noexcept;
        SPRITE* resolvePathOwnerRelation(int* relationOut) noexcept;
        int scaledEngineChainLength() noexcept;
        void approachEngineTargetSpeed(float* speedOut) noexcept;
        int minimumEngineWeaponRange() noexcept;
        void updatePositionFromPathEndpoints() noexcept;
        void splitEngineChainAtPosition(float x, float y) noexcept;
        void initializeEnginePathEndpoints() noexcept;
        int attachEngineChain(SPRITE* value) noexcept;
        int canLinkEngineChain(SPRITE* target) noexcept;
        float resolveEngineChainCollision(SPRITE* target, int mode) noexcept;
        int resolveEngineChainPathInteraction(core::R_POS* pathPair, float* distanceOut) noexcept;
        void applyEngineChainPathMovement(core::R_POS* pathPair, float speed, int delay) noexcept;
        void clearCommandsTargetingThisSprite() noexcept;
        int createRouteMarkerSprites(core::R_DOT* pathNode) noexcept;
        int createPathSpritesFromBuffer(core::R_DOT* pathNode, BaseSpriteList<0>* list, int nvid) noexcept;
        int evaluateEngineTargetRangeState() noexcept;
        int pathBufferReachesSecondaryTarget(core::R_DOT* pathNode) noexcept;
        void suppressDrawRecursive() noexcept;
        void restoreDrawRecursive() noexcept;
        int Hp() const noexcept { return m_hp; }
        void setHpRaw(int value) noexcept { m_hp = value; }

        EX_SPRITE_DATA* ExData() noexcept { return m_exData; }
        const EX_SPRITE_DATA* ExData() const noexcept { return m_exData; }

        int IsActionStackEmpty() const noexcept;

        core::List<ACT>* ActionStack() noexcept;
        bool hasExData() const noexcept { return m_exData != nullptr; }
        STRING GetName() const;
        void SetName(const STRING* name);
        std::uint32_t exDataLifeTime() const noexcept { return m_exData ? m_exData->lifetimeRemaining : 0; }
        std::uint32_t exDataChildCadence() const noexcept { return m_exData ? m_exData->childCadence : 0; }

        Gamma GetGamma() const noexcept;
        bool spriteGammaOverride(Gamma& out) const noexcept
        {
            if (!m_exData ||
                (m_exData->gammaRaw0 == 0u && m_exData->gammaRaw1 == 0u))
                return false;
            out.first = m_exData->gammaRaw0;
            out.second = m_exData->gammaRaw1;
            return true;
        }
        float exDataX() const noexcept { return m_exData ? m_exData->sourceX : 0.0f; }
        float exDataY() const noexcept { return m_exData ? m_exData->sourceY : 0.0f; }
        float exDataZ() const noexcept { return m_exData ? m_exData->sourceZ : 0.0f; }
        float exDataEffectCurvePosition() const noexcept { return m_exData->effectCurvePosition; }
        float blurHistoryX() const noexcept { return m_exData->sourceX; }
        float blurHistoryY() const noexcept { return m_exData->sourceY; }
        float blurHistoryZ() const noexcept { return m_exData->sourceZ; }
        int groundGridFrame() const noexcept { return m_exData ? m_exData->gridFrame : -1; }
        void setGroundGridFrame(int value) noexcept { if (m_exData) m_exData->gridFrame = value; }
        std::uint32_t applicationBucketTime() const noexcept { return m_applicationBucketTime; }
        void setApplicationBucketTime(std::uint32_t value) noexcept { m_applicationBucketTime = value; }
        std::uint32_t createTime() const noexcept { return m_createTime; }
        void setCreateTime(std::uint32_t value) noexcept { m_createTime = value; }
        static constexpr unsigned CommandBitsShift = 2u;
        static constexpr DWORD CommandValueMask = 31u;
        static constexpr DWORD CommandBitsMask = CommandValueMask << CommandBitsShift;
        static constexpr unsigned ArmyBitsShift = 12u;
        static constexpr DWORD ArmyValueMask = 3u;
        static constexpr DWORD ArmyBitsMask = ArmyValueMask << ArmyBitsShift;
        static constexpr DWORD InvulnerableFlag = 0x00000800u;
        static constexpr DWORD MovementStartedFlag = 0x00000080u;
        static constexpr DWORD SpatialHashRemovedFlag = 0x00000100u;
        static constexpr DWORD ChildSpawnToggleFlag = 0x00004000u;
        static constexpr DWORD CrossedGoalXFlag = 0x00008000u;
        static constexpr DWORD CrossedGoalYFlag = 0x00010000u;
        static constexpr DWORD CrossedGoalAxesMask = 0x00018000u;
        static constexpr DWORD DrawSuppressedFlag = 0x00020000u;

        DWORD runtimeFlags() const noexcept { return m_runtimeFlags; }
        DWORD commandBits() const noexcept { return m_runtimeFlags & CommandBitsMask; }
        int commandIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> CommandBitsShift) & CommandValueMask); }
        DWORD armyBits() const noexcept { return m_runtimeFlags & ArmyBitsMask; }
        int armyIndex() const noexcept { return static_cast<int>((m_runtimeFlags >> ArmyBitsShift) & ArmyValueMask); }
        bool sameArmy(const SPRITE& other) const noexcept { return armyIndex() == other.armyIndex(); }

        int IsCommand(int value) const noexcept { return value == commandIndex() ? 1 : 0; }
        void setRuntimeFlags(DWORD value) noexcept { m_runtimeFlags = value; }
        int attackDecisionCode() const noexcept { return m_attackDecisionCode; }
        void setAttackDecisionCode(int value) noexcept { m_attackDecisionCode = value; }

        __declspec(noinline) SPRITE* Goal() const noexcept;
        void setGoalSpriteDirect(SPRITE* value) noexcept { m_goalSprite = value; }
        std::uint32_t actionTimer() const noexcept { return m_actionTimer; }
        void setActionTimer(std::uint32_t value) noexcept { m_actionTimer = value; }
        __forceinline float linkerX() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerX);
        }
        __forceinline float linkerY() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerY);
        }
        __forceinline float linkerZ() const noexcept
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerZ);
        }
        __forceinline int linkerDirection() const noexcept
        {
            return static_cast<int>(*reinterpret_cast<const unsigned char*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerDirection));
        }
        __forceinline SPRITE* linkerOwner() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::LinkerOwner);
        }
        __forceinline void setLinkerState(float x74, float y78, float z7C, int direction80, SPRITE* owner84) noexcept
        {
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::LinkerX) = x74;
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::LinkerY) = y78;
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::LinkerZ) = z7C;
            *reinterpret_cast<unsigned char*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::LinkerDirection) = static_cast<unsigned char>(direction80);
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::LinkerOwner) = owner84;
        }
        int sharedPrimaryState() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SharedPrimaryState);
        }
        void setSharedPrimaryState(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SharedPrimaryState) = value;
        }
        int sharedSecondaryState() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SharedSecondaryState);
        }
        void setSharedSecondaryState(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SharedSecondaryState) = value;
        }
        __forceinline int ammoFixedPoint() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::AmmoFixedPoint);
        }
        __forceinline int turnTimer() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::TurnTimer);
        }
        __forceinline void setTurnTimer(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::TurnTimer) = value;
        }
        __forceinline int behaviorFlags() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::BehaviorFlags);
        }
        __forceinline void setBehaviorFlags(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::BehaviorFlags) = value;
        }
        bool isDrawSuppressed() const noexcept { return (m_runtimeFlags & DrawSuppressedFlag) != 0; }

        SPRITE* childChain() const noexcept { return m_childChain; }
        SPRITE* childChainDebugAccessor() const noexcept { return m_childChain; }
        SPRITE* goalSpriteDebugAccessor() const noexcept { return m_goalSprite; }
        SPRITE* engineChainPrevious() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineChainPrevious);
        }

        SPRITE* engineChainNext() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineChainNext);
        }
        SPRITE* engineCommandReferenceOwner() const noexcept
        {
            return *reinterpret_cast<SPRITE* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner);
        }
        SPRITE* childBacklink() const noexcept { return m_childBacklink; }
        void setChildChain(SPRITE* value) noexcept { m_childChain = value; }
        void setEngineChainPrevious(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainPrevious) = value;
        }
        void setEngineChainNext(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainNext) = value;
        }
        void setEngineCommandReferenceOwner(SPRITE* value) noexcept
        {
            *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner) = value;
        }

        SPRITE*& engineChainPreviousRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainPrevious);
        }
        SPRITE*& engineChainNextRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineChainNext);
        }
        SPRITE*& engineCommandReferenceOwnerRef() noexcept
        {
            return *reinterpret_cast<SPRITE**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandReferenceOwner);
        }
        int& engineCommandArgument0Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument0);
        }
        int& engineCommandArgument1Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument1);
        }
        int& engineCommandArgument2Ref() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument2);
        }
        int& engineAccelerationDelayRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineAccelerationDelay);
        }
        float& engineTargetSpeedRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineTargetSpeed);
        }
        int& pushLineActiveRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PushLineActive);
        }
        core::R_DOT*& primaryPathNodeRef() noexcept
        {
            return *reinterpret_cast<core::R_DOT**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathNode);
        }
        int& primaryPathProgressRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathProgress);
        }
        int& primaryPathAuxiliaryRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathAuxiliary);
        }
        int& primaryPathEdgeIndexRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PrimaryPathEdgeIndex);
        }
        core::R_DOT*& secondaryPathNodeRef() noexcept
        {
            return *reinterpret_cast<core::R_DOT**>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathNode);
        }
        int& secondaryPathProgressRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathProgress);
        }
        int& secondaryPathAuxiliaryRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathAuxiliary);
        }
        int& secondaryPathEdgeIndexRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::SecondaryPathEdgeIndex);
        }
        float& previousPathXRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathX);
        }
        float& previousPathYRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathY);
        }
        float& previousPathZRef() noexcept
        {
            return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PreviousPathZ);
        }
        int& routeActionReadyRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::RouteActionReady);
        }
        int& routeActionStartTimeRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::RouteActionStartTime);
        }
        int& pathBufferSizeRef() noexcept
        {
            return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBufferSize);
        }
        core::R_DOT* primaryPathNode() const noexcept
        {
            return *reinterpret_cast<core::R_DOT* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PrimaryPathNode);
        }
        int primaryPathEdgeIndex() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PrimaryPathEdgeIndex);
        }
        core::R_DOT* secondaryPathNode() const noexcept
        {
            return *reinterpret_cast<core::R_DOT* const*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SecondaryPathNode);
        }
        int secondaryPathEdgeIndex() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::SecondaryPathEdgeIndex);
        }
        int engineCommandArgument0Value() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument0);
        }
        void setEngineCommandArgument0(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument0) = value;
        }
        int engineCommandArgument1Value() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument1);
        }
        void setEngineCommandArgument1(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::EngineCommandArgument1) = value;
        }
        core::R_DOT* engineCommandArgument0Node() const noexcept { return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument0Value()))); }
        core::R_DOT* engineCommandArgument1Node() const noexcept { return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(engineCommandArgument1Value()))); }
        core::R_DOT* engineCommandArgument2Node() const noexcept
        {
            const int value = *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::EngineCommandArgument2);
            return reinterpret_cast<core::R_DOT*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }
        std::uint32_t commandRecordCount() const noexcept { return m_commandStack.m_commandRecords.count; }
        std::uint32_t commandRecordWord(std::size_t index, std::size_t word) const noexcept { return m_commandStack.m_commandRecords.records[index].words[word]; }
        unsigned char* pathBufferData() noexcept
        {
            return reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBuffer;
        }
        const unsigned char* pathBufferData() const noexcept
        {
            return reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PathBuffer;
        }
        int pathBufferSize() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(this) + SpriteLayout::PathBufferSize);
        }
        void setPathBufferSize(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::PathBufferSize) = value;
        }
        void setChildBacklink(SPRITE* value) noexcept { m_childBacklink = value; }
        int steerAwayFromMapBoundary(float x, float y) noexcept;
        void computeNextMovementPosition(float* xOut, float* yOut, float* zOut) noexcept;
        SPRITE* CanPlaceWithCrushAndGlide(float* xOut, float* yOut, float* zOut);
        void ChangeCoor(float x, float y) noexcept;
        void ChangeCoor(float x, float y, float z) noexcept;
        void GotoNearestMoveAction();
        void InsertPauseBeforeMoveActions(int pauseValue);
        int IsInside(float x, float y) const noexcept;

        void CreateChild();


        void Remove();

        void Insert();
        unsigned int serializeSpriteRecord(RESOURCE* resource) noexcept;
        int ChangeArmy(int bucketIndex) noexcept;
        void ensureLinkedVidChild() noexcept;

        int insertChildChainHead(SPRITE* child);
        int appendChildChain(SPRITE* child);
        void detachFromChildChain();
        int deleteChildByVid(VID* childVid);

        static ACT buildCommandRecord(std::uint32_t opcode, int argument1, int argument2, int argument3);
        STRING GetTextActions() const;
        STRING GetTextItems() const;
        void SetTextActions(const STRING* text);
        void SetTextItems(const STRING* text);
        void AddActionAfterStop(int action, int var1, int var2, int var3);
        int ActionStackHaveCommand(int command) const noexcept;

        void serializeCommandRecordsText(STRING& out) const;
        std::string serializeCommandRecordsText() const;
        void parseCommandRecordsText(const STRING& text);
        void queueCommandBeforeStopSentinel(std::uint32_t opcode, int argument1, int argument2, int argument3);

        void serializeCommandWordsText(STRING& out) const;
        std::string serializeCommandWordsText() const;
        void parseCommandWordsText(STRING text);
        std::uint32_t NoItems() const noexcept { return m_exData ? m_exData->items.count : 0u; }
        const std::int32_t* commandWordData() const noexcept { return m_exData ? m_exData->items.values : nullptr; }
        int findLastCommandWord(std::int32_t word) const noexcept;
        int removeCommandWordValue(std::int32_t word) noexcept;
        int GetItemNumber(int index) const noexcept;
        int InsertItem(std::int32_t word) noexcept;
        int insertUniqueItem(std::int32_t word) noexcept;
        int clearCommandWordList() noexcept;
        int hasCommandOpcode(std::uint32_t opcode) const noexcept;
        std::uint32_t lastCommandOpcode() const noexcept;

        int ammoCount() const noexcept;

        int MaxAmmo() const noexcept;
        __forceinline void setAmmoFixedPoint(int value) noexcept
        {
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(this) + SpriteLayout::AmmoFixedPoint) = value;
        }
        int refillAmmoByCapacityFraction(int divisor) noexcept;
        int NeedRepairByRepair() const noexcept;
        int ammoMissingPercent() const noexcept;
        int derivedStateValue(int index) const noexcept;
        int setDerivedStateValue(int index, int value) noexcept;
        int repairLinkedChildState(int createMissingLinker) noexcept;

        const core::List<ACT>& commandStack() const { return m_commandStack; }
        size_t noCommandStackEntry() const { return m_commandStack.size(); }

    private:
        friend class PRIMITIVE;
        friend class UNIT;
        friend class ENGINE;
        friend class BALL;
        friend class MENU;
        friend struct EX_SPRITE_DATA;
        friend struct SpritePrefixProbe;


        int m_attackDecisionCode = 0;
        int m_currentFrameBegin = 0;
        int m_currentFrame = 0;
        int m_currentFrameEnd = 0;
        std::uint32_t m_applicationBucketTime = 0;
        std::uint32_t m_createTime = 0;
        VID* m_vid = nullptr;
        float m_speed = 0.0f;
        float m_zSpeed = 0.0f;
        DWORD m_runtimeFlags;
        int m_listReferenceCount = 0;
        VECTOR m_xyz;
        SPRITE* m_goalSprite = nullptr;
        SPRITE* m_childChain = nullptr;
        SPRITE* m_childBacklink = nullptr;
        int m_currentAnimation = 0;
        ANGLE m_direction;
        std::uint32_t m_actionTimer = 0;
        core::List<ACT> m_commandStack;
        EX_SPRITE_DATA* m_exData = nullptr;
        int m_hp = 0;
        PTR_SPRITE m_bestTargetSprite;

    };



    struct SpritePrefixProbe
    {
        static constexpr std::size_t slot04 = offsetof(SPRITE, m_attackDecisionCode);
        static constexpr std::size_t frameBegin08 = offsetof(SPRITE, m_currentFrameBegin);
        static constexpr std::size_t frame0C = offsetof(SPRITE, m_currentFrame);
        static constexpr std::size_t frameEnd10 = offsetof(SPRITE, m_currentFrameEnd);
        static constexpr std::size_t bucketTime14 = offsetof(SPRITE, m_applicationBucketTime);
        static constexpr std::size_t createTime18 = offsetof(SPRITE, m_createTime);
        static constexpr std::size_t vid1C = offsetof(SPRITE, m_vid);
        static constexpr std::size_t speed20 = offsetof(SPRITE, m_speed);
        static constexpr std::size_t zSpeed24 = offsetof(SPRITE, m_zSpeed);
        static constexpr std::size_t flags28 = offsetof(SPRITE, m_runtimeFlags);
        static constexpr std::size_t refCount2C = offsetof(SPRITE, m_listReferenceCount);
        static constexpr std::size_t xyz30 = offsetof(SPRITE, m_xyz);
        static constexpr std::size_t xyz34 = offsetof(SPRITE, m_xyz) + sizeof(float);
        static constexpr std::size_t xyz38 = offsetof(SPRITE, m_xyz) + sizeof(float) * 2u;
        static constexpr std::size_t target3C = offsetof(SPRITE, m_goalSprite);
        static constexpr std::size_t child40 = offsetof(SPRITE, m_childChain);
        static constexpr std::size_t backlink44 = offsetof(SPRITE, m_childBacklink);
        static constexpr std::size_t animation48 = offsetof(SPRITE, m_currentAnimation);
        static constexpr std::size_t direction4C = offsetof(SPRITE, m_direction);
        static constexpr std::size_t timer50 = offsetof(SPRITE, m_actionTimer);
        static constexpr std::size_t commandList54 = offsetof(SPRITE, m_commandStack);
        static constexpr std::size_t commandOwner64 = offsetof(SPRITE, m_exData);
        static constexpr std::size_t hp68 = offsetof(SPRITE, m_hp);
        static constexpr std::size_t ptrSprite6C = offsetof(SPRITE, m_bestTargetSprite);
    };




    class TERRAIN : public SPRITE
    {
    public:
        TERRAIN(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;

        void AddHpPerSecond(int hpToAdd) noexcept;

    };

    class LINKER : public SPRITE
    {
    public:
        LINKER(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~LINKER() override;

    private:


        float m_linkOffsetX;
        float m_linkOffsetY;
        float m_linkOffsetZ;
        int m_linkDirection;
        SPRITE* m_linkOwner;
    };
    class PRIMITIVE : public SPRITE
    {
    public:
        PRIMITIVE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        using SPRITE::Draw;
        void Tact() override;
        void MoveTact() override {}
        void DeletePointerToSprite(SPRITE*) override {}
        void DrawDebugOverlay() override {}


        virtual void Draw() const;
        virtual void Control(INPUT*) {}
    };
    class REGION : public SPRITE
    {
    public:
        REGION(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~REGION() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void Draw() override;
        void DrawDebugOverlay() override;

        float regionScreenLeft() const noexcept;
        float regionScreenTop() const noexcept;
        float regionScreenRight() const noexcept;
        float regionScreenBottom() const noexcept;
        int rebuildRegionFogRamp(int start, int end, int color);

        static constexpr std::uint32_t FogAnimatedFlag = 1u << 0;
        static constexpr std::uint32_t FogBlendFlag = 1u << 1;
        static constexpr std::uint32_t FullViewportFlag = 1u << 3;

        std::uint32_t regionFlags() const noexcept { return m_regionFlags; }
        float regionWidth() const noexcept { return m_regionWidth; }
        float regionHeight() const noexcept { return m_regionHeight; }
        VID* sourceMappedVid(int index) const noexcept { return m_sourceVidMap[index]; }
        VID* targetMappedVid(int index) const noexcept { return m_targetVidMap[index]; }

    private:
        int m_fogRampPhase;
        int m_lastFogRampPhase;
        void* m_fogRamp;
        std::uint32_t m_regionFlags;
        int m_fogEnd;
        int m_fogStart;
        std::uint32_t m_fogColor;
        int m_reservedRegionState8C;
        float m_regionWidth;
        float m_regionHeight;
        VID* m_savedRegionVid;
        int m_persistedRegionState;
        VID* m_sourceVidMap[6];
        VID* m_targetVidMap[6];
    };

    VID* resolveRegionMappedVid(VID* sourceVid, float x, float y, float z) noexcept;

    void DeleteSpriteThroughVirtualDeletingDestructor(SPRITE* sprite) noexcept;

    class FRAME : public SPRITE
    {
    public:
        FRAME(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~FRAME() override;
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
    };


    class STEXT : public FRAME
    {
    public:
        STEXT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent = nullptr);
        ~STEXT() override;

        const char* text() const noexcept { return m_text; }
        const char* textClass() const noexcept { return m_textClass; }
        int textLength() const noexcept { return m_textLength; }
        int textFlags() const noexcept { return m_textFlags; }
        void assignText(const char* text);
        void assignTextClass(const char* text);
        int Action(int opcode, std::intptr_t argument1Carrier, int argument2Carrier, int argument3Carrier) override;
        void Draw() override;
        int CalcTextProperty() noexcept;

    private:


        char* m_text;
        char* m_textClass;
        int m_textLength;
        int m_textFlags;
        int m_textState84;
        int m_textState88;
    };

}


