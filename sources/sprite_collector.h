#pragma once

#include "core/types.h"
#include "base_sprite_list.h"
#include <cstddef>

namespace as1
{
    class MAP;
    class SPRITE;
    class VID;

    class SPRITE_COLLECTOR
    {
    public:
        SPRITE_COLLECTOR(float mapWidth, float mapHeight, VID* const* vids, int vidCount);
        ~SPRITE_COLLECTOR();

        SPRITE_COLLECTOR(const SPRITE_COLLECTOR&) = delete;
        SPRITE_COLLECTOR& operator=(const SPRITE_COLLECTOR&) = delete;

        void DeleteAll();

        void Insert(SPRITE* sprite);
        int Delete(SPRITE* sprite);
        void ChangeCoor(SPRITE* sprite, float newX, float newY);


        SPRITE* FirstHashInBox(float minX, float minY, float maxX, float maxY);

        SPRITE* NextHashInBox();


        SPRITE* CanPlace(const VID* probeVid, float x, float y, float z);
        bool traceVidMovementCollision(const VID* probeVid,
                                                  float startX,
                                                  float startY,
                                                  float startZ,
                                                  float* targetX,
                                                  float* targetY,
                                                  float* targetZ);

        int bucketWidth() const noexcept { return m_bucketWidth; }
        int bucketHeight() const noexcept { return m_bucketHeight; }
        int bucketRowShift() const noexcept { return m_bucketRowShift; }
        float inverseCellWidth() const noexcept { return m_inverseCellWidth; }
        float inverseCellHeight() const noexcept { return m_inverseCellHeight; }
        const core::List<SPRITE*>& overflowList() const noexcept
        {
            return *reinterpret_cast<const BaseSpriteList<0>*>(&m_overflowListStorage);
        }

        int reverseCursor() const noexcept { return m_reverseCursor; }
        void setReverseCursor(int value) noexcept { m_reverseCursor = value; }
        int* reverseCursorAddress() noexcept { return &m_reverseCursor; }
        core::List<SPRITE*>& mutableOverflowList() noexcept
        {
            return *reinterpret_cast<BaseSpriteList<0>*>(&m_overflowListStorage);
        }
        const core::List<SPRITE*>& mutableOverflowList() const noexcept
        {
            return *reinterpret_cast<const BaseSpriteList<0>*>(&m_overflowListStorage);
        }
        int overflowCount() const noexcept
        {
            return reinterpret_cast<const BaseSpriteList<0>*>(&m_overflowListStorage)->activeCount();
        }
        SPRITE* overflowSpriteAt(int index) const noexcept
        {
            const auto& overflow = *reinterpret_cast<const BaseSpriteList<0>*>(&m_overflowListStorage);
            if (index < 0 || index >= overflow.activeCount())
                return nullptr;
            SPRITE* const* const raw = overflow.data();
            return raw ? raw[static_cast<std::size_t>(index)] : nullptr;
        }

    private:
        friend class SPRITE;

        SPRITE_COLLECTOR* initializeHashGrid(float mapWidth, float mapHeight, VID* const* vids, int vidCount);

        static bool hashEligible(const SPRITE* sprite) noexcept;
        static bool overflowEligible(const SPRITE* sprite) noexcept;
        static bool iteratorCandidateAllowed(const SPRITE* sprite) noexcept;
        static float objectHalfX(const SPRITE* sprite) noexcept;
        static float objectHalfY(const SPRITE* sprite) noexcept;
        static float objectTopZ(const SPRITE* sprite) noexcept;
        static DWORD objectMoveMask(const SPRITE* sprite) noexcept;
        static float objectHalfX(const VID* vid) noexcept;
        static float objectHalfY(const VID* vid) noexcept;
        static float objectTopZ(const VID* vid) noexcept;
        static DWORD objectMoveMask(const VID* vid) noexcept;
        void allocateBucketsFromMaxObjectSize(float mapWidth, float mapHeight, float maxObjectSizeX, float maxObjectSizeY);
        static int bucketTableElementCount(int bucketWidth, int bucketHeight) noexcept;
        int bucketTableElementCount() const noexcept;
        int bucketReleaseCount() const noexcept;
        static int constructorPowerShiftFor(float value) noexcept;

        int ConvX(float x) const noexcept;
        int ConvY(float y) const noexcept;
        void setIteratorCellWindow(int minX, int minY, int maxX, int maxY) noexcept;
        void configureBoxQueryWindow(float minX, float minY, float maxX, float maxY) noexcept;
        static int bucketRecordByteOffsetFromIndex(int index) noexcept;
        int bucketRecordByteOffsetForCell(int x, int y) const noexcept;
        int bucketRowBase(int y) const noexcept;
        static int bucketRecordByteOffsetFromRowBaseAndColumn(int rowBase, int x) noexcept;
        BaseSpriteList<0>* bucketAtByteOffsetUnchecked(int byteOffset) noexcept;
        const BaseSpriteList<0>* bucketAtByteOffsetUnchecked(int byteOffset) const noexcept;
        BaseSpriteList<0>* bucketAtCellBoundary(int x, int y) noexcept;
        const BaseSpriteList<0>* bucketAtCellBoundary(int x, int y) const noexcept;
        BaseSpriteList<0>* bucketAt(int x, int y) noexcept;
        const BaseSpriteList<0>* bucketAt(int x, int y) const noexcept;

        int m_queryMinX;
        int m_queryRow;
        int m_queryMaxX;
        int m_queryMaxY;
        int m_queryColumn;
        int m_querySpriteIndex;
        int m_reverseCursor;
        int m_bucketRowShift;
        int m_bucketWidth;
        int m_bucketHeight;
        float m_inverseCellWidth;
        float m_inverseCellHeight;
        struct SpriteListStorage
        {
            std::uint32_t vtable;
            int count;
            int capacity;
            SPRITE** items;
        };

        BaseSpriteList<0>* m_bucketTable;
        SpriteListStorage m_overflowListStorage;
    };


    extern SPRITE_COLLECTOR* g_spriteCollector;

    __forceinline
    SPRITE_COLLECTOR* GlobalSpriteCollector() noexcept
    {
        return g_spriteCollector;
    }

    __forceinline
    void SetGlobalSpriteCollector(SPRITE_COLLECTOR* value) noexcept
    {
        g_spriteCollector = value;
    }

    void DeleteGlobalSpriteCollector();
    void DestroyGlobalSpriteCollectorForApplicationDestructor();
    bool ReinitGlobalSpriteCollectorFromVidTable(float mapWidth, float mapHeight, VID* const* vids, int vidCount);

    __forceinline
    SPRITE* GlobalSpriteCollectorFirstHashInBox(float minX, float minY, float maxX, float maxY)
    {
        return g_spriteCollector ? g_spriteCollector->FirstHashInBox(minX, minY, maxX, maxY) : nullptr;
    }

    __forceinline
    SPRITE* GlobalSpriteCollectorNextHashInBox()
    {
        return g_spriteCollector ? g_spriteCollector->NextHashInBox() : nullptr;
    }

    __forceinline
    SPRITE* GlobalSpriteCollectorCanPlace(const MAP&, const VID* probeVid, float x, float y, float z)
    {
        return g_spriteCollector ? g_spriteCollector->CanPlace(probeVid, x, y, z) : nullptr;
    }

    __forceinline
    bool DeleteSpriteFromCollectorForActionSwitch(SPRITE* sprite)
    {
        return (sprite && g_spriteCollector) ? g_spriteCollector->Delete(sprite) == 0 : false;
    }

    __forceinline
    bool InsertSpriteIntoCollectorForActionSwitch(SPRITE* sprite)
    {
        if (!sprite || !g_spriteCollector)
            return false;
        g_spriteCollector->Insert(sprite);
        return true;
    }

    bool DepoCanCreateUnitFilter(const SPRITE* candidate);
}
