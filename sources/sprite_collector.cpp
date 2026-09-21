#include "sprite_collector.h"

#include "core/log.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "vid/vid.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <limits>
#include <xmmintrin.h>

namespace as1
{
    SPRITE_COLLECTOR* g_spriteCollector = nullptr;

    namespace
    {
        int kDepoCanCreateUnitNvids[] = {
            0x000000AA, 0x00000323, 0x00000325, 0x00000326,
            0x00000327, 0x00000329, 0x0000032A, 0x0000032D,
            0x0000034E, 0x00000350, 0x0000035D, 0x0000035F,
            0x00000360, 0x0000036A, 0x0000036B, 0x0000037F,
        };

        constexpr int kBucketListRecordStride = 0x10;
        constexpr unsigned short kFloatCompareLess = 0x0100u;
        constexpr unsigned short kFloatCompareEqual = 0x4000u;
        constexpr unsigned short kFloatCompareUnordered = 0x4500u;
        constexpr unsigned short kFloatCompareLessOrEqualMask =
            kFloatCompareLess | kFloatCompareEqual;
        constexpr std::uint32_t kMovementTraceSampleMask = 0x0Fu;

        int truncateFloatToInt32(float value) noexcept
        {


            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        float multiplyRoundedFloat(float lhs, float rhs) noexcept
        {
            float out = 0.0f;
            _mm_store_ss(&out, _mm_mul_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
            return out;
        }

        float subtractRoundedFloat(float lhs, float rhs) noexcept
        {
            float out = 0.0f;
            _mm_store_ss(&out, _mm_sub_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
            return out;
        }

        float addRoundedFloat(float lhs, float rhs) noexcept
        {
            float out = 0.0f;
            _mm_store_ss(&out, _mm_add_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
            return out;
        }

        float divideRoundedFloat(float lhs, float rhs) noexcept
        {
            float out = 0.0f;
            _mm_store_ss(&out, _mm_div_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
            return out;
        }

        __forceinline unsigned short floatCompareStatus(float lhs, float rhs) noexcept
        {

            if (std::isnan(lhs) || std::isnan(rhs))
                return kFloatCompareUnordered;
            if (lhs < rhs)
                return kFloatCompareLess;
            if (lhs == rhs)
                return kFloatCompareEqual;
            return 0;
        }

        __forceinline bool x87LessThanOrUnordered(float lhs, float rhs) noexcept
        {
            return (floatCompareStatus(lhs, rhs) & kFloatCompareLess) != 0u;
        }

        __forceinline bool x87LessOrEqualOrUnordered(float lhs, float rhs) noexcept
        {
            return (floatCompareStatus(lhs, rhs) & kFloatCompareLessOrEqualMask) != 0u;
        }

        __forceinline int cellIndexFromScaled(float scaled, int limit) noexcept
        {


            if (!std::isnan(scaled) && scaled < 0.0f)
                return 0;

            const float limitFloat = static_cast<float>(limit);
            if (!std::isnan(scaled) && scaled >= limitFloat)
                return limit - 1;

            return truncateFloatToInt32(scaled);
        }

        __forceinline int multiplyToCell(float value, float scale, int limit) noexcept
        {
            return cellIndexFromScaled(multiplyRoundedFloat(value, scale), limit);
        }

        float reciprocalStored(float divisor) noexcept
        {
            return divideRoundedFloat(1.0f, divisor);
        }

        __forceinline int subtractMultiplyToCell(float value, float subtractValue, float scale, int limit) noexcept
        {
            return cellIndexFromScaled(multiplyRoundedFloat(subtractRoundedFloat(value, subtractValue), scale), limit);
        }

        __forceinline int addMultiplyToCell(float value, float addValue, float scale, int limit) noexcept
        {
            return cellIndexFromScaled(multiplyRoundedFloat(addRoundedFloat(value, addValue), scale), limit);
        }

        __forceinline int subtractReciprocalMultiplyToCell(float value, float divisor, int limit) noexcept
        {
            const float reciprocal = divideRoundedFloat(1.0f, divisor);
            return cellIndexFromScaled(multiplyRoundedFloat(subtractRoundedFloat(value, reciprocal), divisor), limit);
        }

        __forceinline int addReciprocalMultiplyToCell(float value, float divisor, int limit) noexcept
        {
            const float reciprocal = divideRoundedFloat(1.0f, divisor);
            return cellIndexFromScaled(multiplyRoundedFloat(addRoundedFloat(value, reciprocal), divisor), limit);
        }

        float inversePowerOfTwo(int shift) noexcept
        {
            const std::int32_t base = static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31));
            return divideRoundedFloat(1.0f, static_cast<float>(base));
        }

        int computeHashHeightCells(float mapHeight, float inverseCellHeight) noexcept
        {


            float value = subtractRoundedFloat(mapHeight, 1.0f);
            value = multiplyRoundedFloat(value, inverseCellHeight);
            value = addRoundedFloat(value, 1.0f);
            value = addRoundedFloat(value, 2.0f);
            return truncateFloatToInt32(value);
        }

        int computeHashWidthShift(float mapWidth, float inverseCellWidth) noexcept
        {
            float widthScaled = subtractRoundedFloat(mapWidth, 1.0f);
            widthScaled = multiplyRoundedFloat(widthScaled, inverseCellWidth);
            widthScaled = addRoundedFloat(widthScaled, 1.0f);

            int shift = 0;
            for (;;)
            {
                const float power = static_cast<float>(
                    static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31)));

                if (std::isnan(widthScaled) || !(widthScaled > power))
                    break;
                ++shift;
            }
            return shift;
        }

        std::uint32_t saturatedArrayBytes16Plus4(std::uint32_t elementCount) noexcept
        {


            const std::uint64_t product = static_cast<std::uint64_t>(elementCount) * 16u;
            if (product > 0xFFFFFFFFull)
                return 0xFFFFFFFFu;

            const std::uint32_t bytes = static_cast<std::uint32_t>(product);
            if (bytes > 0xFFFFFFFFu - 4u)
                return 0xFFFFFFFFu;
            return bytes + 4u;
        }

        std::uint32_t abs32Wrap(std::int32_t value) noexcept
        {
            const std::uint32_t raw = static_cast<std::uint32_t>(value);
            const std::uint32_t sign = raw >> 31u;
            return (raw ^ (0u - sign)) + sign;
        }


        std::int32_t wrapAdd32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
        }

        std::int32_t wrapSub32(std::int32_t a, std::int32_t b) noexcept
        {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        }

        std::int32_t signedDivide32(std::int32_t numerator, std::int32_t denominator) noexcept
        {
            if (denominator == 0)
                return 0;
            if (numerator == static_cast<std::int32_t>(0x80000000u) && denominator == -1)
                return static_cast<std::int32_t>(0x80000000u);
            return numerator / denominator;
        }

        float x87IntToFloat(std::int32_t value) noexcept
        {
            return static_cast<float>(value);
        }
    }

    SPRITE_COLLECTOR::SPRITE_COLLECTOR(float mapWidth,
                                                         float mapHeight,
                                                         VID* const* vids,
                                                         int vidCount)
    {


        m_overflowListStorage.vtable = BaseSpriteList<0>::CurrentImageBaseSpriteListVtable();
        m_overflowListStorage.count = 0;
        m_overflowListStorage.capacity = 0;
        m_overflowListStorage.items = nullptr;
        (void)initializeHashGrid(mapWidth, mapHeight, vids, vidCount);
    }

    SPRITE_COLLECTOR::~SPRITE_COLLECTOR()
    {

        DeleteAll();
    }

    __forceinline SPRITE_COLLECTOR* SPRITE_COLLECTOR::initializeHashGrid(float mapWidth,
                                                                    float mapHeight,
                                                                    VID* const* vids,
                                                                    int vidCount)
    {
        m_querySpriteIndex = 0;
        m_reverseCursor = 0;

        float maxObjectSizeX = 0.0f;
        float maxObjectSizeY = 0.0f;
        if (vidCount > 0)
        {
            for (int i = 0; i < vidCount; ++i)
            {
                VID* const vid = vids[i];
                if (vid && (vid->properties() & P_HASH) != 0)
                {
                    const float sizeX = vid->sizeX();
                    const float sizeY = vid->sizeY();
                    if (!x87LessOrEqualOrUnordered(sizeX, maxObjectSizeX))
                        maxObjectSizeX = sizeX;
                    if (!x87LessOrEqualOrUnordered(sizeY, maxObjectSizeY))
                        maxObjectSizeY = sizeY;
                }
            }
        }

        allocateBucketsFromMaxObjectSize(mapWidth, mapHeight, maxObjectSizeX, maxObjectSizeY);
        return this;
    }

    __forceinline void SPRITE_COLLECTOR::allocateBucketsFromMaxObjectSize(float mapWidth,
                                                                     float mapHeight,
                                                                     float maxObjectSizeX,
                                                                     float maxObjectSizeY)
    {


        const float halfMaxX = maxObjectSizeX * 0.5f;
        const float halfMaxY = maxObjectSizeY * 0.5f;
        const int shiftX = constructorPowerShiftFor(halfMaxX);
        const int shiftY = constructorPowerShiftFor(halfMaxY);
        m_inverseCellWidth = inversePowerOfTwo(shiftX);
        m_inverseCellHeight = inversePowerOfTwo(shiftY);

        m_bucketHeight = computeHashHeightCells(mapHeight, m_inverseCellHeight);
        m_bucketRowShift = computeHashWidthShift(mapWidth, m_inverseCellWidth);
        m_bucketWidth = static_cast<int>(std::uint32_t{1} << (m_bucketRowShift & 31));

        const int total = bucketTableElementCount();
        m_bucketTable = nullptr;

        const std::uint32_t bytes32 =
            saturatedArrayBytes16Plus4(static_cast<std::uint32_t>(total));
        unsigned char* raw = static_cast<unsigned char*>(
            ::operator new(static_cast<std::size_t>(bytes32), std::nothrow));
        if (!raw)
        {
            LOG::Write("!!!ERROR!!!HASH_MAP: Enough memory %i,%i", m_bucketWidth, m_bucketHeight);
            return;
        }

        *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(total);
        m_bucketTable = reinterpret_cast<BaseSpriteList<0>*>(raw + sizeof(std::uint32_t));
        for (int i = 0; i < total; ++i)
        {


            auto* const words = reinterpret_cast<std::uint32_t*>(
                raw + sizeof(std::uint32_t) + static_cast<std::size_t>(i) * 0x10u);
            words[3] = 0u;
            words[0] = core::List<SPRITE*>::CurrentImageCoreListVtable();
            words[1] = 0u;
            words[2] = 0u;
            words[0] = BaseSpriteList<0>::CurrentImageBaseSpriteListVtable();
        }
    }


    void SPRITE_COLLECTOR::DeleteAll()
    {
        reinterpret_cast<BaseSpriteList<0>*>(&m_overflowListStorage)->clear();

        const int bucketCount = bucketReleaseCount();
        for (int byteOffset = bucketRecordByteOffsetFromIndex(bucketCount - 1);
             byteOffset >= 0;
             byteOffset -= kBucketListRecordStride)
        {
            bucketAtByteOffsetUnchecked(byteOffset)->clear();
        }


        if (m_bucketTable)
        {
            BaseSpriteList<0>* const table = m_bucketTable;
            using DeletingDestructor = void* (__fastcall*)(void*, void*, unsigned char);
            auto* const vtable = reinterpret_cast<DeletingDestructor const*>(
                static_cast<std::uintptr_t>(table->m_vtableToken));
            (*vtable)(table, nullptr, 3u);
            m_bucketTable = nullptr;
        }


        m_overflowListStorage.vtable = core::List<SPRITE*>::CurrentImageCoreListVtable();
        ::operator delete(m_overflowListStorage.items);
        m_overflowListStorage.items = nullptr;
        m_overflowListStorage.count = 0;
    }


    void SPRITE_COLLECTOR::Insert(SPRITE* sprite)
    {
        if (hashEligible(sprite))
        {

            const int x = ConvX(sprite->X());
            const int y = ConvY(sprite->Y());
            const int byteOffset = bucketRecordByteOffsetForCell(x, y);
            bucketAtByteOffsetUnchecked(byteOffset)->append(sprite);
        }

        if (overflowEligible(sprite))
            reinterpret_cast<BaseSpriteList<0>*>(&m_overflowListStorage)->append(sprite);
    }


    int SPRITE_COLLECTOR::Delete(SPRITE* sprite)
    {

        int errorMask = 0;

        if (m_bucketTable && hashEligible(sprite))
        {
            const int x = ConvX(sprite->X());
            const int y = ConvY(sprite->Y());

            const int byteOffset = bucketRecordByteOffsetForCell(x, y);
            BaseSpriteList<0>* const list = bucketAtByteOffsetUnchecked(byteOffset);

            if (m_queryColumn == x && m_queryRow == y && m_querySpriteIndex > 0 &&
                m_querySpriteIndex < list->activeCount() &&
                list->data()[m_querySpriteIndex - 1] == sprite)
            {
                --m_querySpriteIndex;
            }
            if (list->removeSorted(sprite) != 0)
                errorMask |= 1;
        }

        if (overflowEligible(sprite))
        {
            if (reinterpret_cast<BaseSpriteList<0>*>(&m_overflowListStorage)->removeSorted(sprite) != 0)
                errorMask |= 2;
        }

        if (errorMask != 0)
        {

            SPRITE* const ground = mouseSprite();
            SPRITE* const groundChild = ground->childChain();
            if (sprite != ground && sprite != groundChild)
            {
                const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
                LOG::ResourceError("SPRITE %i", 10, "hash can't delete", errorMask, nvid);
            }
        }
        return errorMask;
    }

    void SPRITE_COLLECTOR::ChangeCoor(SPRITE* sprite, float newX, float newY)
    {

        const int oldX = ConvX(sprite->X());
        const int oldY = ConvY(sprite->Y());
        const int nextX = ConvX(newX);
        const int nextY = ConvY(newY);
        if (oldX == nextX && oldY == nextY)
            return;

        const int oldByteOffset = bucketRecordByteOffsetForCell(oldX, oldY);
        BaseSpriteList<0>* const oldBucket = bucketAtByteOffsetUnchecked(oldByteOffset);
        if (oldBucket->removeSorted(sprite) != 0)
            return;

        const int newByteOffset = bucketRecordByteOffsetForCell(nextX, nextY);
        bucketAtByteOffsetUnchecked(newByteOffset)->append(sprite);
    }


    SPRITE* SPRITE_COLLECTOR::FirstHashInBox(float minX, float minY, float maxX, float maxY)
    {
        configureBoxQueryWindow(minX, minY, maxX, maxY);
        return NextHashInBox();
    }


    SPRITE* SPRITE_COLLECTOR::NextHashInBox()
    {
        while (m_queryRow <= m_queryMaxY)
        {
            const int rowBase = bucketRowBase(m_queryRow);
            while (m_queryColumn <= m_queryMaxX)
            {
                const int byteOffset = bucketRecordByteOffsetFromRowBaseAndColumn(rowBase, m_queryColumn);
                BaseSpriteList<0>* const list = bucketAtByteOffsetUnchecked(byteOffset);
                if (m_querySpriteIndex < list->activeCount())
                {
                    const int cursor = m_querySpriteIndex++;
                    return list->data()[cursor];
                }

                ++m_queryColumn;
                m_querySpriteIndex = 0;
            }

            ++m_queryRow;
            m_queryColumn = m_queryMinX;
            m_querySpriteIndex = 0;
        }
        return nullptr;
    }


    SPRITE* SPRITE_COLLECTOR::CanPlace(const VID* probeVid, float x, float y, float z)
    {
        const MAP& map = *Map;

        if (map.GetGroundZ(probeVid, VECTOR2{x, y}) > z)
            return mouseSprite();

        const DWORD probeMask = objectMoveMask(probeVid);
        if (probeMask == 0)
            return nullptr;

        const float hx = objectHalfX(probeVid);
        const float hy = objectHalfY(probeVid);
        for (SPRITE* candidate = FirstHashInBox(x - hx, y - hy, x + hx, y + hy);
             candidate;
             candidate = NextHashInBox())
        {
            if (!iteratorCandidateAllowed(candidate))
                continue;
            const VID* const cvid = candidate->Vid();
            const float absDx = std::fabs(candidate->X() - x);
            const float sumHalfX = objectHalfX(cvid) + hx;
            if (x87LessOrEqualOrUnordered(sumHalfX, absDx))
                continue;
            const float absDy = std::fabs(candidate->Y() - y);
            const float sumHalfY = objectHalfY(cvid) + hy;
            if (x87LessOrEqualOrUnordered(sumHalfY, absDy))
                continue;
            const float candidateTop = candidate->Z() + objectTopZ(cvid);
            if (x87LessThanOrUnordered(candidateTop, z))
                continue;
            const float probeTop = z + objectTopZ(probeVid);
            if (x87LessThanOrUnordered(probeTop, candidate->Z()))
                continue;
            if ((objectMoveMask(cvid) & probeMask) == 0)
                continue;
            return candidate;
        }
        return nullptr;
    }

    bool SPRITE_COLLECTOR::traceVidMovementCollision(const VID* probeVid,
                                                                         float startX,
                                                                         float startY,
                                                                         float startZ,
                                                                         float* targetX,
                                                                         float* targetY,
                                                                         float* targetZ)
    {

        if (!probeVid || objectMoveMask(probeVid) == 0)
            return false;

        std::int32_t x = truncateFloatToInt32(startX);
        std::int32_t y = truncateFloatToInt32(startY);
        std::int32_t currentZ = truncateFloatToInt32(startZ);
        const std::int32_t targetXi = truncateFloatToInt32(*targetX);
        const std::int32_t targetYi = truncateFloatToInt32(*targetY);
        const std::int32_t targetZi = truncateFloatToInt32(*targetZ);

        std::uint32_t major = abs32Wrap(wrapSub32(targetXi, x));
        std::uint32_t minor = abs32Wrap(wrapSub32(targetYi, y));
        std::int32_t sx = x87LessOrEqualOrUnordered(*targetX, startX) ? -1 : 1;
        std::int32_t sy = x87LessOrEqualOrUnordered(*targetY, startY) ? -1 : 1;
        bool swapped = false;
        if (minor > major)
        {
            std::swap(x, y);
            std::swap(major, minor);
            std::swap(sx, sy);
            swapped = true;
        }

        std::int32_t zStepPer16 = 0;
        if (major != 0u)
        {
            const std::uint32_t rawDeltaZ = static_cast<std::uint32_t>(targetZi) -
                                            static_cast<std::uint32_t>(currentZ);
            const std::int32_t scaledDeltaZ = static_cast<std::int32_t>(rawDeltaZ << 4u);
            zStepPer16 = signedDivide32(scaledDeltaZ, static_cast<std::int32_t>(major));
        }

        const std::int32_t errStep = static_cast<std::int32_t>(minor << 1u);
        std::int32_t err = wrapSub32(errStep, static_cast<std::int32_t>(major));
        for (std::uint32_t i = 0; i < major; ++i)
        {
            if ((i & kMovementTraceSampleMask) == 0u && i != 0u)
            {
                currentZ = wrapAdd32(currentZ, zStepPer16);
                const float qx = x87IntToFloat(swapped ? y : x);
                const float qy = x87IntToFloat(swapped ? x : y);
                const float qz = x87IntToFloat(currentZ);
                if (CanPlace(probeVid, qx, qy, qz))
                {
                    if (swapped)
                    {
                        *targetX = x87IntToFloat(y);
                        *targetY = x87IntToFloat(x);
                    }
                    else
                    {
                        *targetX = x87IntToFloat(x);
                        *targetY = x87IntToFloat(y);
                    }
                    *targetZ = x87IntToFloat(currentZ);
                    return true;
                }
            }

            if (err >= 0)
            {
                const std::int32_t twiceMajor = static_cast<std::int32_t>(major << 1u);
                do
                {
                    y = wrapAdd32(y, sy);
                    err = wrapSub32(err, twiceMajor);
                }
                while (err >= 0);
            }
            x = wrapAdd32(x, sx);
            err = wrapAdd32(err, errStep);
        }
        return false;
    }

    __forceinline bool SPRITE_COLLECTOR::iteratorCandidateAllowed(const SPRITE* sprite) noexcept
    {

        return sprite && sprite->Animation() < 0x0F;
    }

    __forceinline bool SPRITE_COLLECTOR::hashEligible(const SPRITE* sprite) noexcept
    {
        const VID* const vid = sprite->Vid();
        return (vid->properties() & P_HASH) != 0;
    }

    __forceinline bool SPRITE_COLLECTOR::overflowEligible(const SPRITE* sprite) noexcept
    {
        const VID* const vid = sprite->Vid();

        return (vid->spriteTypeId() & 0x0Cu) != 0u;
    }

    __forceinline float SPRITE_COLLECTOR::objectHalfX(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectHalfX(vid);
    }

    __forceinline float SPRITE_COLLECTOR::objectHalfY(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectHalfY(vid);
    }

    __forceinline float SPRITE_COLLECTOR::objectTopZ(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectTopZ(vid);
    }

    __forceinline DWORD SPRITE_COLLECTOR::objectMoveMask(const SPRITE* sprite) noexcept
    {
        const VID* vid = sprite ? sprite->Vid() : nullptr;
        return objectMoveMask(vid);
    }

    __forceinline float SPRITE_COLLECTOR::objectHalfX(const VID* vid) noexcept
    {

        return vid ? vid->halfSizeX() : 0.0f;
    }

    __forceinline float SPRITE_COLLECTOR::objectHalfY(const VID* vid) noexcept
    {

        return vid ? vid->halfSizeY() : 0.0f;
    }

    __forceinline float SPRITE_COLLECTOR::objectTopZ(const VID* vid) noexcept
    {

        return vid ? vid->sizeZ() : 0.0f;
    }

    __forceinline DWORD SPRITE_COLLECTOR::objectMoveMask(const VID* vid) noexcept
    {

        return vid ? vid->movementMask() : 0;
    }

    __forceinline int SPRITE_COLLECTOR::bucketTableElementCount(int bucketWidth, int bucketHeight) noexcept
    {

        const std::int32_t h = static_cast<std::int32_t>(bucketHeight);
        const std::int32_t w = static_cast<std::int32_t>(bucketWidth);
        const std::uint32_t raw = static_cast<std::uint32_t>(h) * static_cast<std::uint32_t>(w);
        return static_cast<int>(raw);
    }

    __forceinline int SPRITE_COLLECTOR::bucketTableElementCount() const noexcept
    {
        return bucketTableElementCount(m_bucketWidth, m_bucketHeight);
    }

    __forceinline int SPRITE_COLLECTOR::bucketReleaseCount() const noexcept
    {

        return m_bucketTable ? bucketTableElementCount() : 0;
    }

    __forceinline int SPRITE_COLLECTOR::constructorPowerShiftFor(float value) noexcept
    {

        int shift = 0;
        for (;;)
        {
            const float power = static_cast<float>(
                static_cast<std::int32_t>(std::uint32_t{1} << (shift & 31)));

            if (std::isnan(value) || !(value > power))
                break;
            ++shift;
        }
        return shift;
    }

    __forceinline int SPRITE_COLLECTOR::ConvX(float x) const noexcept
    {

        return multiplyToCell(x, m_inverseCellWidth, m_bucketWidth);
    }

    __forceinline int SPRITE_COLLECTOR::ConvY(float y) const noexcept
    {
        return multiplyToCell(y, m_inverseCellHeight, m_bucketHeight);
    }

    __forceinline void SPRITE_COLLECTOR::setIteratorCellWindow(int minX, int minY, int maxX, int maxY) noexcept
    {

        m_queryMinX = minX;
        m_queryRow = minY;
        m_queryMaxX = maxX;
        m_queryMaxY = maxY;
        m_queryColumn = m_queryMinX;
        m_querySpriteIndex = 0;
    }

    __forceinline void SPRITE_COLLECTOR::configureBoxQueryWindow(float minX,
                                                                         float minY,
                                                                         float maxX,
                                                                         float maxY) noexcept
    {

        const float cellW = reciprocalStored(m_inverseCellWidth);

        const int rawMinX = subtractMultiplyToCell(minX, cellW, m_inverseCellWidth, m_bucketWidth);
        const int rawMinY = subtractReciprocalMultiplyToCell(minY, m_inverseCellHeight, m_bucketHeight);
        const int rawMaxX = addMultiplyToCell(maxX, cellW, m_inverseCellWidth, m_bucketWidth);
        const int rawMaxY = addReciprocalMultiplyToCell(maxY, m_inverseCellHeight, m_bucketHeight);

        setIteratorCellWindow(rawMinX, rawMinY, rawMaxX, rawMaxY);
    }

    __forceinline int SPRITE_COLLECTOR::bucketRecordByteOffsetFromIndex(int index) noexcept
    {

        return static_cast<int>(static_cast<std::uint32_t>(index) << 4u);
    }

    __forceinline int SPRITE_COLLECTOR::bucketRecordByteOffsetForCell(int x, int y) const noexcept
    {

        return bucketRecordByteOffsetFromRowBaseAndColumn(bucketRowBase(y), x);
    }

    __forceinline int SPRITE_COLLECTOR::bucketRowBase(int y) const noexcept
    {

        return static_cast<int>(static_cast<std::uint32_t>(y) << (m_bucketRowShift & 31));
    }

    __forceinline int SPRITE_COLLECTOR::bucketRecordByteOffsetFromRowBaseAndColumn(int rowBase, int x) noexcept
    {

        const std::uint32_t raw = static_cast<std::uint32_t>(rowBase) + static_cast<std::uint32_t>(x);
        return static_cast<int>(raw << 4u);
    }

    __forceinline BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAtByteOffsetUnchecked(int byteOffset) noexcept
    {
        return reinterpret_cast<BaseSpriteList<0>*>(
            reinterpret_cast<unsigned char*>(m_bucketTable) + byteOffset);
    }

    __forceinline const BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAtByteOffsetUnchecked(int byteOffset) const noexcept
    {
        return reinterpret_cast<const BaseSpriteList<0>*>(
            reinterpret_cast<const unsigned char*>(m_bucketTable) + byteOffset);
    }

    __forceinline BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAtCellBoundary(int x, int y) noexcept
    {
        return bucketAtByteOffsetUnchecked(bucketRecordByteOffsetForCell(x, y));
    }

    __forceinline const BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAtCellBoundary(int x, int y) const noexcept
    {
        return bucketAtByteOffsetUnchecked(bucketRecordByteOffsetForCell(x, y));
    }

    __forceinline BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAt(int x, int y) noexcept
    {
        return bucketAtCellBoundary(x, y);
    }

    __forceinline const BaseSpriteList<0>* SPRITE_COLLECTOR::bucketAt(int x, int y) const noexcept
    {
        return bucketAtCellBoundary(x, y);
    }

    void DeleteGlobalSpriteCollector()
    {

        SPRITE_COLLECTOR* current = g_spriteCollector;
        if (!current)
            return;
        g_spriteCollector = nullptr;
        current->DeleteAll();

        ::operator delete(current);
    }

    void DestroyGlobalSpriteCollectorForApplicationDestructor()
    {

        SPRITE_COLLECTOR* const current = g_spriteCollector;
        if (!current)
            return;
        current->DeleteAll();
        ::operator delete(current);
    }

    bool ReinitGlobalSpriteCollectorFromVidTable(float mapWidth,
                                               float mapHeight,
                                               VID* const* vids,
                                               int vidCount)
    {

        DeleteGlobalSpriteCollector();
        void* const storage = ::operator new(0x44u, std::nothrow);
        SPRITE_COLLECTOR* const created = storage
            ? new (storage) SPRITE_COLLECTOR(mapWidth, mapHeight, vids, vidCount)
            : nullptr;
        g_spriteCollector = created;
        return created != nullptr;
    }


    int __stdcall depoCanCreateUnitCandidate(const SPRITE* candidate)
    {

        if (!candidate)
            return 0;
        const int nvid = candidate->Vid()->nVid;
        for (int allowed : kDepoCanCreateUnitNvids)
        {
            if (allowed == nvid)
                return 1;
        }
        return 0;
    }

    bool DepoCanCreateUnitFilter(const SPRITE* candidate)
    {
        return depoCanCreateUnitCandidate(candidate) != 0;
    }

#undef __forceinline
}
