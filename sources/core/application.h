#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "core/types.h"
#include "base_sprite_list.h"

namespace as1
{
    namespace application_flags
    {
        constexpr std::uint32_t EnemyCanAttackNeutralTrains = 1u << 1;
        constexpr std::uint32_t BucketTimingActive = 1u << 4;
        constexpr std::uint32_t MapLoading = 1u << 5;
        constexpr std::uint32_t PendingCommandOrLoad = 1u << 6;
        constexpr std::uint32_t ScriptControlBit7 = 1u << 7;
        constexpr std::uint32_t DemoWriteToResource = 1u << 8;
        constexpr std::uint32_t DemoUseResource = 1u << 9;
        constexpr std::uint32_t ScriptCallbacksDisabled = 1u << 19;
    }
    class GRAPH;
    class MAP;
    class MENU;
    class SPRITE;
    class VID;
    class SCRIPT;
    class STRING;
    struct WEAPON;
}

namespace as1 { namespace core
{

    namespace application_layout
    {


        constexpr std::size_t ObjectSize = 0x4B2Cu;
        constexpr std::size_t Fps = 0x04u;
        constexpr std::size_t FpsCounter = 0x08u;
        constexpr std::size_t Flags = 0x0Cu;
        constexpr std::size_t Unknown10 = 0x10u;
        constexpr std::size_t TickScale = 0x14u;

        constexpr std::size_t ApplicationTitle = 0x18u;
        constexpr std::size_t CurrentMapName = 0x1Cu;
        constexpr std::size_t PendingCommand = 0x20u;
        constexpr std::size_t PreviousMapName = 0x24u;
        constexpr std::size_t ResourceName = 0x28u;
        constexpr std::size_t RegistryPath = ResourceName;
        constexpr std::size_t WorldFrameCounter = 0x2Cu;
        constexpr std::size_t WorldStartTime = 0x30u;
        constexpr std::size_t Unknown30 = WorldStartTime;
        constexpr std::size_t MapExtentX = 0x34u;
        constexpr std::size_t MapExtentY = 0x38u;
        constexpr std::size_t ScrollType = 0x3Cu;
        constexpr std::size_t ScrollMinX = 0x40u;
        constexpr std::size_t ScrollMaxX = 0x44u;
        constexpr std::size_t ScrollMinY = 0x48u;
        constexpr std::size_t ScrollMaxY = 0x4Cu;
        constexpr std::size_t CameraShiftX = 0x50u;
        constexpr std::size_t CameraShiftY = 0x54u;
        constexpr std::size_t DrawLayerOwners = 0x58u;
        constexpr std::size_t DrawLayerCount = 21u;
        constexpr std::size_t DrawLayerStride = 0x10u;
        constexpr std::size_t ScriptRuntime = 0x1A8u;
        constexpr std::size_t DemoResource = 0xA18u;
        constexpr std::size_t RelationTable = 0xA58u;
        constexpr std::size_t TerrainGrid = 0xA78u;
        constexpr std::size_t TempTerrainGrid = 0xA7Cu;
        constexpr std::size_t TerrainGridWidth = 0xA80u;
        constexpr std::size_t TerrainGridHeight = 0xA84u;
        constexpr std::size_t InstanceHandle = 0xA88u;
        constexpr std::size_t MainWindow = 0xA8Cu;
        constexpr std::size_t Accelerator = 0xA90u;
        constexpr std::size_t ActivePlayerIndex = 0xA94u;
        constexpr std::size_t PlayerSlots = 0xA98u;
        constexpr std::size_t PlayerSlotStride = sizeof(std::uint32_t);
        constexpr std::size_t InputState = 0xAA8u;
        constexpr std::size_t Menu = 0xAC8u;
        constexpr std::size_t BaseSpriteList = Menu;
        constexpr std::size_t Groups = 0xAE0u;
        constexpr std::size_t WeaponCount = 0xB04u;
        constexpr std::size_t WeaponTable = 0xB08u;
        constexpr std::size_t VidCount = 0xB0Cu;
        constexpr std::size_t VidTable = 0xB10u;
        constexpr std::size_t VidTableBytes = 0x4000u;
        constexpr std::size_t VidTableEnd = 0x4B10u;
        constexpr std::size_t MouseTips = 0x4B10u;
        constexpr std::size_t TailSpriteList = 0x4B18u;
        constexpr std::size_t ShellFlags = 0x4B28u;


    }


    extern void* g_applicationOwner;
    __forceinline void* ApplicationOwner() noexcept { return g_applicationOwner; }



    __forceinline short* ApplicationTerrainGrid() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<short**>(owner + application_layout::TerrainGrid) : nullptr;
    }
    __forceinline void SetApplicationTerrainGrid(short* value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<short**>(owner + application_layout::TerrainGrid) = value;
    }
    __forceinline short* ApplicationTempTerrainGrid() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<short**>(owner + application_layout::TempTerrainGrid) : nullptr;
    }
    __forceinline int ApplicationTerrainGridWidth() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<int*>(owner + application_layout::TerrainGridWidth) : 0;
    }
    __forceinline int ApplicationTerrainGridHeight() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<int*>(owner + application_layout::TerrainGridHeight) : 0;
    }
    __forceinline int ApplicationWeaponCount() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<int*>(owner + application_layout::WeaponCount) : 0;
    }
    __forceinline WEAPON* ApplicationWeaponTable() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<WEAPON**>(owner + application_layout::WeaponTable) : nullptr;
    }
    __forceinline void SetApplicationWeaponTable(WEAPON* value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<WEAPON**>(owner + application_layout::WeaponTable) = value;
    }

    class ApplicationVidTable
    {
    public:
        static constexpr std::size_t kCapacity = 0x1000u;
        static constexpr std::uint32_t kCountOffset = application_layout::VidCount;
        static constexpr std::uint32_t kFirstSlotOffset = application_layout::VidTable;
        static constexpr std::uint32_t kEndSlotOffset = application_layout::VidTableEnd;

        __forceinline void clear() noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            if (!owner) return;
            *reinterpret_cast<int*>(owner + kCountOffset) = 0;
            std::memset(owner + kFirstSlotOffset, 0, kEndSlotOffset - kFirstSlotOffset);
        }
        __forceinline void setWeaponSentinel(WEAPON* weapon) noexcept { SetApplicationWeaponTable(weapon); }
        __forceinline WEAPON* weaponSentinel() const noexcept { return ApplicationWeaponTable(); }
        __forceinline int count() const noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? *reinterpret_cast<int*>(owner + kCountOffset) : 0;
        }
        __forceinline VID* slot(int index) const noexcept
        {
            if (index < 0 || static_cast<std::size_t>(index) >= kCapacity) return nullptr;
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? *reinterpret_cast<VID**>(owner + kFirstSlotOffset + static_cast<std::size_t>(index) * 4u) : nullptr;
        }
        __forceinline void setStoredCount(int value) noexcept
        {
            if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
                *reinterpret_cast<int*>(owner + kCountOffset) = value;
        }
        __forceinline void setSlotCell(int index, VID* vid) noexcept
        {
            if (index < 0 || static_cast<std::size_t>(index) >= kCapacity) return;
            if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
                *reinterpret_cast<VID**>(owner + kFirstSlotOffset + static_cast<std::size_t>(index) * 4u) = vid;
        }
        __forceinline VID* const* slotData() const noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? reinterpret_cast<VID* const*>(owner + kFirstSlotOffset) : nullptr;
        }
        __forceinline std::size_t capacity() const noexcept { return kCapacity; }

    };

    __forceinline ApplicationVidTable& GlobalApplicationVidTable() noexcept
    {
        return *reinterpret_cast<ApplicationVidTable*>(g_applicationOwner);
    }


    struct ApplicationDrawPassBucket
    {


        static constexpr std::uint32_t CountOffset =
            static_cast<std::uint32_t>(application_layout::DrawLayerOwners + 0x04u);
        static constexpr std::uint32_t ListOffset =
            static_cast<std::uint32_t>(application_layout::DrawLayerOwners + 0x0Cu);
        static constexpr std::uint32_t Stride =
            static_cast<std::uint32_t>(application_layout::DrawLayerStride);

        BaseSpriteList<0> list;

        int count() const noexcept { return list.activeCount(); }
        SPRITE* const* data() const noexcept { return list.data(); }
        SPRITE* spriteAt(int index) const noexcept
        {
            return (index >= 0 && index < list.m_count)
                ? list.m_items[static_cast<std::size_t>(index)]
                : nullptr;
        }
        int findAndNull(SPRITE* sprite) noexcept
        {
            int index = list.m_count - 1;
            while (index >= 0 && list.m_items[index] != sprite)
                --index;
            if (index >= 0)
                list.m_items[index] = nullptr;
            return index;
        }
        void append(SPRITE* sprite) { list.append(sprite); }
        void compactSparse() noexcept
        {
            const int oldCount = list.m_count;
            if (oldCount <= 0)
                return;

            int firstNull = 0;
            while (firstNull < oldCount && list.m_items[firstNull] != nullptr)
                ++firstNull;
            if (firstNull >= oldCount)
                return;

            int holes = 1;
            for (int source = firstNull + 1; source < oldCount; ++source)
            {
                if (list.m_items[source])
                    list.m_items[source - holes] = list.m_items[source];
                else
                    ++holes;
            }

            const int newCount = oldCount - holes;
            if (newCount > 0)
            {
                if (newCount < oldCount)
                    list.m_count = newCount;
                return;
            }

            SPRITE** const storage = list.m_items;
            list.m_capacity = 0;
            list.m_count = 0;
            if (storage)
                ::operator delete(storage);
            list.m_items = nullptr;
        }
        void clear() noexcept { list.deleteAllSprites(); }
    };

    struct ApplicationDrawDispatcherState
    {
        static constexpr int PassCount = 21;
        static constexpr std::uint32_t ScrollMinXOffset = 0x40u;
        static constexpr std::uint32_t ScrollMaxXOffset = 0x44u;
        static constexpr std::uint32_t ScrollMinYOffset = 0x48u;
        static constexpr std::uint32_t ScrollMaxYOffset = 0x4Cu;
        static constexpr std::uint32_t CameraXOffset = 0x50u;
        static constexpr std::uint32_t CameraYOffset = 0x54u;
        static constexpr std::uint32_t BucketCountBaseOffset = ApplicationDrawPassBucket::CountOffset;
        static constexpr std::uint32_t BucketListBaseOffset = ApplicationDrawPassBucket::ListOffset;
        static constexpr std::uint32_t BucketStride = ApplicationDrawPassBucket::Stride;


        static constexpr std::uint32_t FlagsOffset = 0x0Cu;
        static constexpr std::uint32_t BucketTimingFlag = application_flags::BucketTimingActive;

        __forceinline std::uint32_t flags() const noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::Flags) : 0u;
        }
        __forceinline void setFlags(std::uint32_t value) noexcept
        {
            if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
                *reinterpret_cast<std::uint32_t*>(owner + application_layout::Flags) = value;
        }
        __forceinline bool bucketTimingEnabled() const noexcept { return (flags() & BucketTimingFlag) != 0; }
        __forceinline float cameraShiftX() const noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? *reinterpret_cast<float*>(owner + application_layout::CameraShiftX) : 0.0f;
        }
        __forceinline float cameraShiftY() const noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return owner ? *reinterpret_cast<float*>(owner + application_layout::CameraShiftY) : 0.0f;
        }
        __forceinline void setCameraShiftX(float value) noexcept
        {
            if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
                *reinterpret_cast<float*>(owner + application_layout::CameraShiftX) = value;
        }
        __forceinline void setCameraShiftY(float value) noexcept
        {
            if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
                *reinterpret_cast<float*>(owner + application_layout::CameraShiftY) = value;
        }
        __forceinline ApplicationDrawPassBucket& drawPassBucket(int pass) noexcept
        {
            auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
            return *reinterpret_cast<ApplicationDrawPassBucket*>(
                owner + application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
        }
        __forceinline const ApplicationDrawPassBucket& drawPassBucket(int pass) const noexcept
        {
            auto* const owner = static_cast<const std::uint8_t*>(ApplicationOwner());
            return *reinterpret_cast<const ApplicationDrawPassBucket*>(
                owner + application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
        }
        __forceinline void clear() noexcept
        {
            setFlags(0u);
            setCameraShiftX(0.0f);
            setCameraShiftY(0.0f);
            for (int pass = 0; pass < PassCount; ++pass) drawPassBucket(pass).clear();
        }
    };

    __forceinline ApplicationDrawDispatcherState& GlobalApplicationDrawDispatcherState() noexcept
    {
        return *reinterpret_cast<ApplicationDrawDispatcherState*>(g_applicationOwner);
    }



    extern std::uint32_t g_currentTimeMilliseconds;
    extern std::uint32_t g_previousWorldTimeMilliseconds;
    __forceinline std::uint32_t CurrentTimeMilliseconds() noexcept { return g_currentTimeMilliseconds; }
    __forceinline void SetCurrentTimeMilliseconds(std::uint32_t value) noexcept { g_currentTimeMilliseconds = value; }

    __forceinline float ApplicationTickScale() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<float*>(owner + application_layout::TickScale) : 1.0f;
    }
    __forceinline void SetApplicationTickScale(float value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<float*>(owner + application_layout::TickScale) = value;
    }
    __forceinline std::uint32_t ApplicationWorldFrameCounter() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::WorldFrameCounter) : 0u;
    }
    __forceinline void SetApplicationWorldFrameCounter(std::uint32_t value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<std::uint32_t*>(owner + application_layout::WorldFrameCounter) = value;
    }
    __forceinline std::uint32_t ApplicationWorldStartTime() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::WorldStartTime) : 0u;
    }
    __forceinline void SetApplicationWorldStartTime(std::uint32_t value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<std::uint32_t*>(owner + application_layout::WorldStartTime) = value;
    }

    __forceinline float ApplicationMapWidth() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<float*>(owner + application_layout::MapExtentX) : 640.0f;
    }
    __forceinline void SetApplicationMapWidth(float value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<float*>(owner + application_layout::MapExtentX) = value;
    }
    __forceinline float ApplicationMapHeight() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<float*>(owner + application_layout::MapExtentY) : 480.0f;
    }
    __forceinline void SetApplicationMapHeight(float value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<float*>(owner + application_layout::MapExtentY) = value;
    }
    __forceinline const STRING& ApplicationCurrentMapName() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return *reinterpret_cast<STRING*>(owner + application_layout::CurrentMapName);
    }
    __forceinline const STRING& ApplicationPreviousMapName() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return *reinterpret_cast<STRING*>(owner + application_layout::PreviousMapName);
    }
    __forceinline void SetApplicationCurrentMapName(const STRING& value)
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        if (!owner)
            return;
        STRING& current = *reinterpret_cast<STRING*>(owner + application_layout::CurrentMapName);
        STRING& previous = *reinterpret_cast<STRING*>(owner + application_layout::PreviousMapName);
        previous = current;
        current = value;
    }
    __forceinline std::uint32_t ApplicationScrollType() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::ScrollType) : 1u;
    }
    __forceinline void SetApplicationScrollType(std::uint32_t value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<std::uint32_t*>(owner + application_layout::ScrollType) = value;
    }
    __forceinline std::uint32_t PreviousWorldTimeMilliseconds() noexcept { return g_previousWorldTimeMilliseconds; }
    __forceinline void SetPreviousWorldTimeMilliseconds(std::uint32_t value) noexcept { g_previousWorldTimeMilliseconds = value; }


    extern float g_shiftSpeedX;
    extern float g_shiftSpeedY;

    __forceinline std::uint32_t& DisplayedFramesPerSecond() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return *reinterpret_cast<std::uint32_t*>(owner + application_layout::Fps);
    }
    __forceinline std::uint32_t& AccumulatedFpsFrameCount() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return *reinterpret_cast<std::uint32_t*>(owner + application_layout::FpsCounter);
    }


    extern std::uint32_t g_bucketTimingSnapshotMilliseconds;
    extern std::uint32_t g_demoRealTimeBaseMilliseconds;
    extern std::uint32_t g_demoRecordedTimeBaseMilliseconds;
    extern std::uint32_t g_childRotationCorrectionPending;

    static constexpr std::size_t kScriptCallbackSlotCount = 64u;
    extern int EvFunctionNumber[kScriptCallbackSlotCount];

    extern std::uint32_t PrevRealCurrentTime;
    extern std::uint32_t RealCurrentTime;

    __forceinline std::uint32_t ApplicationFlags() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::Flags) : 0u;
    }
    __forceinline void SetApplicationFlags(std::uint32_t value) noexcept
    {
        if (auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner()))
            *reinterpret_cast<std::uint32_t*>(owner + application_layout::Flags) = value;
    }
    __forceinline std::uint32_t ActivePlayerIndex() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? *reinterpret_cast<std::uint32_t*>(owner + application_layout::ActivePlayerIndex) : 0u;
    }
    __forceinline SCRIPT* ApplicationScriptRuntime() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(ApplicationOwner());
        return owner ? reinterpret_cast<SCRIPT*>(owner + application_layout::ScriptRuntime) : nullptr;
    }

    struct ApplicationCreateSpriteRequest
    {
        MAP* owner = nullptr;
        VID* vid = nullptr;
        VECTOR xyz{};
        ANGLE direction{};
        SPRITE* parent = nullptr;
        bool remoteControlled = false;
    };

    class Application
    {
    public:

        static std::unique_ptr<SPRITE> CreateSprite(const ApplicationCreateSpriteRequest& request);
        SPRITE* createSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent);



        int callScriptFunctionInternal(int functionIndex, int firstArgument, int secondArgument, int thirdArgument = 0);


        static __forceinline int callScriptFunction(int functionIndex, int firstArgument, int secondArgument, int thirdArgument = 0)
        {
            auto* const owner = reinterpret_cast<Application*>(ApplicationOwner());
            return owner->callScriptFunctionInternal(functionIndex, firstArgument, secondArgument, thirdArgument);
        }


        void removeNamedSprite(SPRITE* sprite) noexcept;
        void appendNamedSprite(SPRITE* sprite);

        static int drawSpritePass(ApplicationDrawDispatcherState& state, int pass);


        int beginBucketTimingSnapshot();
        int endBucketTimingSnapshot();
        void removeSpriteFromApplicationLists(SPRITE* sprite);
        char* appendSpriteToApplicationListsAndReleaseReference(SPRITE* sprite);
        SPRITE* previousSpriteInDrawPassByVidProperties(int pass, int* cursor, std::uint32_t requiredVidProperties);
        SPRITE* findSpriteAtPointByBounds(int filter, float x, float y, SPRITE* previous = nullptr);
        SPRITE* findSpriteAtPointByFilter(int filter, float x, float y);
        SPRITE* findNearestSpriteByFilter(int filter, float x, float y, float radius, SPRITE* previous = nullptr);



        static __forceinline int beginBucketTimingSnapshot(ApplicationDrawDispatcherState&) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->beginBucketTimingSnapshot() : 0;
        }
        static __forceinline int endBucketTimingSnapshot(ApplicationDrawDispatcherState&) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->endBucketTimingSnapshot() : 0;
        }
        static __forceinline int removeSpriteFromDrawBucket(ApplicationDrawDispatcherState&, SPRITE* sprite) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            if (!app) return 0;
            app->removeSpriteFromApplicationLists(sprite);
            return 0;
        }
        static __forceinline char* appendSpriteToDrawBucketAndRelease(ApplicationDrawDispatcherState&, SPRITE* sprite) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->appendSpriteToApplicationListsAndReleaseReference(sprite) : nullptr;
        }
        static __forceinline SPRITE* findSpriteAtPointByBounds(MAP&, ApplicationDrawDispatcherState&, int filter, float x, float y,
                                                                SPRITE* previous = nullptr) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->findSpriteAtPointByBounds(filter, x, y, previous) : nullptr;
        }
        static __forceinline SPRITE* findSpriteAtPointByFilter(MAP&, ApplicationDrawDispatcherState&, int filter, float x, float y) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->findSpriteAtPointByFilter(filter, x, y) : nullptr;
        }
        static __forceinline SPRITE* findNearestSpriteByFilter(MAP&, ApplicationDrawDispatcherState&, int filter, float x, float y, float radius,
                                                                SPRITE* previous = nullptr) {
            auto* const app = reinterpret_cast<Application*>(ApplicationOwner());
            return app ? app->findNearestSpriteByFilter(filter, x, y, radius, previous) : nullptr;
        }

        static SPRITE* previousSpriteInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor);

    };
} }

namespace as1
{


    __forceinline MENU& applicationMenu() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(core::ApplicationOwner());
        return *reinterpret_cast<MENU*>(owner + core::application_layout::Menu);
    }

    __forceinline BaseSpriteList<0>& applicationFrameSpriteList() noexcept
    {
        auto* const owner = static_cast<std::uint8_t*>(core::ApplicationOwner());
        return *reinterpret_cast<BaseSpriteList<0>*>(owner + core::application_layout::Menu);
    }
}
