#include "core/application.h"
#include "core/as_string.h"
#include "core/file_logger.h"
#include "core/log.h"

#include "graph.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "builded_terrain.h"
#include "vid/vid_hardware.h"
#include "unit.h"
#include "man.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "depo.h"
#include "avia.h"
#include "vid/vid.h"
#include "script.h"
#include "constant.h"

#include <cmath>
#include <cstring>
#include <new>

namespace as1 { namespace core
{


    std::uint32_t g_currentTimeMilliseconds = 0;
    std::uint32_t g_previousWorldTimeMilliseconds = 0;
    void* g_applicationOwner = nullptr;


    float g_shiftSpeedX = 0.0f;
    float g_shiftSpeedY = 0.0f;
    int EvFunctionNumber[kScriptCallbackSlotCount] = {};
    std::uint32_t PrevRealCurrentTime = 0;
    std::uint32_t RealCurrentTime = 0;
    std::uint32_t g_bucketTimingSnapshotMilliseconds = 0;
    std::uint32_t g_demoRealTimeBaseMilliseconds = 0;
    std::uint32_t g_demoRecordedTimeBaseMilliseconds = 0;
    std::uint32_t g_childRotationCorrectionPending = 0;

    namespace
    {



    }


    namespace
    {
        template <class T>
        T& applicationSlot(void* owner, std::size_t offset) noexcept
        {

            return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(owner) + offset);
        }

        template <class T>
        const T& applicationSlot(const void* owner, std::size_t offset) noexcept
        {

            return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(owner) + offset);
        }
    }










    namespace
    {
        int drawPassConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawPassMultiplyAddAndConvertToInt32(float value, float scale, float addend) noexcept
        {
            const long double d = static_cast<long double>(value) * static_cast<long double>(scale) + static_cast<long double>(addend);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawPassSubtractAndConvertToInt32(float lhs, float rhs) noexcept
        {
            const long double d = static_cast<long double>(lhs) - static_cast<long double>(rhs);
            if (!std::isfinite(d) || d >= 9223372036854775808.0L || d < -9223372036854775808.0L)
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        int drawSpriteAndCaptureReturnValue(SPRITE* sprite) noexcept
        {
            sprite->Draw();
            return 0;
        }

        bool spriteVisibleForDrawPass(const SPRITE* sprite) noexcept
        {
            return sprite && !sprite->isDrawSuppressed();
        }
    }


    int Application::beginBucketTimingSnapshot()
    {
        auto* const raw = reinterpret_cast<std::uint8_t*>(this);
        auto& flags = *reinterpret_cast<std::uint32_t*>(raw + application_layout::Flags);
        constexpr std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        if ((flags & flag) == 0u)
            g_bucketTimingSnapshotMilliseconds = CurrentTimeMilliseconds();
        flags |= flag;
        return static_cast<int>(flag);
    }


    int Application::endBucketTimingSnapshot()
    {
        auto* const raw = reinterpret_cast<std::uint8_t*>(this);
        auto& flags = *reinterpret_cast<std::uint32_t*>(raw + application_layout::Flags);
        constexpr std::uint32_t flag = ApplicationDrawDispatcherState::BucketTimingFlag;
        if ((flags & flag) != 0u)
        {
            for (int pass = 0; pass < ApplicationDrawDispatcherState::PassCount; ++pass)
            {
                auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
                    raw + application_layout::DrawLayerOwners +
                    static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
                int cursor = bucket->count() - 1;
                while (cursor >= 0)
                {
                    if (SPRITE* const sprite = bucket->spriteAt(cursor))
                        sprite->setApplicationBucketTime(g_bucketTimingSnapshotMilliseconds);
                    --cursor;
                }
            }
            SetCurrentTimeMilliseconds(g_bucketTimingSnapshotMilliseconds);
            SetPreviousWorldTimeMilliseconds(g_bucketTimingSnapshotMilliseconds - 10u);
        }
        flags &= ~flag;
        return static_cast<int>(flags);
    }


    void Application::removeSpriteFromApplicationLists(SPRITE* sprite)
    {
        if (!sprite)
            return;
        removeNamedSprite(sprite);
        const int layer = sprite->Vid()->renderLayer();
        auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * application_layout::DrawLayerStride);
        (void)bucket->findAndNull(sprite);
    }


    char* Application::appendSpriteToApplicationListsAndReleaseReference(SPRITE* sprite)
    {
        if (!sprite)
            return nullptr;
        appendNamedSprite(sprite);
        const int layer = sprite->Vid()->renderLayer();
        auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * application_layout::DrawLayerStride);
        bucket->append(sprite);

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs > 0)
            return reinterpret_cast<char*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(refs)));
        if (refs == 0)
        {
            SPRITE* const deletedOwner = sprite;
            DeleteSpriteThroughVirtualDeletingDestructor(sprite);
            return reinterpret_cast<char*>(deletedOwner);
        }
        const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
        const std::intptr_t logged = logFileLoggerResourceError(
            g_fileLogger, "SPRITE %i", 4, "noRef at Release", refs, nvid);
        return reinterpret_cast<char*>(static_cast<std::uintptr_t>(logged));
    }


    SPRITE* Application::previousSpriteInDrawPassByVidProperties(
        int pass, int* cursor, std::uint32_t requiredVidProperties)
    {
        if (!cursor)
            return nullptr;
        auto* const bucket = reinterpret_cast<ApplicationDrawPassBucket*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::DrawLayerOwners +
            static_cast<std::size_t>(pass) * application_layout::DrawLayerStride);
        int index = --(*cursor);
        while (index >= 0)
        {
            SPRITE* candidate = bucket->spriteAt(index);
            while (!candidate)
            {
                index = --(*cursor);
                if (index < 0)
                    return nullptr;
                candidate = bucket->spriteAt(index);
            }
            if ((candidate->Vid()->properties() & requiredVidProperties) != 0u)
                return candidate;
            index = --(*cursor);
        }
        return nullptr;
    }


    SPRITE* Application::findSpriteAtPointByBounds(int filter, float x, float y, SPRITE* previous)
    {
        SPRITE* const candidate = findNearestSpriteByFilter(filter, x, y, 300.0f, previous);
        if (!candidate)
            return nullptr;
        VID* const vid = candidate->Vid();
        const float halfX = vid->halfSizeX();
        if (candidate->X() - halfX > x || x > candidate->X() + halfX)
            return nullptr;
        const float halfY = vid->halfSizeY();
        if (candidate->Y() - halfY > y || y > candidate->Y() + halfY)
            return nullptr;
        return candidate;
    }


    SPRITE* Application::findSpriteAtPointByFilter(int filter, float x, float y)
    {
        if (!Map)
            return nullptr;
        MAP& map = *Map;
        ApplicationDrawDispatcherState& state = GlobalApplicationDrawDispatcherState();


        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = EmptyVid;
        const int exactNvid = filter & 0x0FFF;
        const bool exactNvidFilter = exactNvid != 0 && (filter & 0x00001000) == 0;
        int typeMask = 0;
        if (exactNvidFilter)
        {


            if (exactNvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(exactNvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        auto preferCandidate = [&](SPRITE* candidate) noexcept
        {


            if (!selected ||
                selected->Vid()->sizeX() > candidate->Vid()->sizeX() ||
                selected->Vid()->sizeY() > candidate->Vid()->sizeY())
            {
                selected = candidate;
            }
        };

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if (exactNvidFilter && candidateVid->nvid() != exactNvid)
                return false;
            return true;
        };

        auto standardHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();


            const float centerX = candidate->X();
            const float halfX = candidateVid->halfSizeX();
            const float lowerX = centerX - halfX;
            const float upperX = centerX + halfX;
            if (!(x >= lowerX) || !(upperX >= x))
                return false;

            const float baseY = candidate->Y() - candidate->Z();
            const float halfY = candidateVid->halfSizeY();
            const float lowerY = baseY - candidateVid->sizeZ() - halfY;
            const float upperY = baseY + halfY;
            return y > lowerY && upperY > y;
        };

        auto regionAwareHit = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if (candidateVid->spriteClassId() == 23u)
            {
                const REGION* const region = static_cast<const REGION*>(candidate);


                const float centerX = candidate->X();
                const float halfX = region->regionWidth() * 0.5f;
                const float lowerX = centerX - halfX;
                const float upperX = centerX + halfX;
                if (!(x >= lowerX) || !(upperX >= x))
                    return false;

                const float baseY = candidate->Y() - candidate->Z();
                const float halfY = region->regionHeight() * 0.5f;
                const float lowerY = baseY - candidateVid->sizeZ() - halfY;
                const float upperY = baseY + halfY;
                return y > lowerY && upperY > y;
            }
            return standardHit(candidate);
        };

        if ((filter & 0x8000) != 0)
        {
            const float maxY = map.GetGroundZ(VECTOR2{x, y}) + y + 300.0f;
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(x - 300.0f, y - 300.0f, x + 300.0f, maxY);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                if (candidate->childBacklink() != nullptr)
                    continue;
                if (!passesFilter(candidate) || !standardHit(candidate))
                    continue;
                preferCandidate(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {


            if (exactNvidFilter &&
                (requestedVid->spriteClassId() == 10u || requestedVid->spriteClassId() == 19u))
            {
                BaseSpriteList<0>& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                while (index >= 0)
                {
                    SPRITE* const candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                    if (candidate->childBacklink() == nullptr && passesFilter(candidate) && standardHit(candidate))
                        preferCandidate(candidate);
                    --index;
                }
                return selected;
            }

            int firstPass = 0;


            int endPass = ApplicationDrawDispatcherState::PassCount - 1;
            if (exactNvidFilter)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            if ((typeMask & 0x40) == 0)
            {
                for (int pass = firstPass; pass < endPass; ++pass)
                {
                    int cursor = state.drawPassBucket(pass).count();
                    for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                         candidate;
                         candidate = previousSpriteInDrawPass(state, pass, &cursor))
                    {
                        if (candidate->childBacklink() != nullptr)
                            continue;
                        if (!passesFilter(candidate) || !standardHit(candidate))
                            continue;
                        preferCandidate(candidate);
                    }
                }
                return selected;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(state, pass, &cursor))
                {
                    if (candidate->childBacklink() != nullptr)
                        continue;
                    if (!passesFilter(candidate) || !regionAwareHit(candidate))
                        continue;
                    preferCandidate(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.BeginIterate(cursor);
             candidate;
             candidate = overflow.NextIterate(cursor))
        {
            if (!passesFilter(candidate) || !standardHit(candidate))
                continue;
            preferCandidate(candidate);
        }
        (void)originalFilter;
        return selected;

    }


    SPRITE* Application::findNearestSpriteByFilter(int filter, float x, float y, float radius, SPRITE* previous)
    {
        if (!Map)
            return nullptr;
        MAP& map = *Map;
        ApplicationDrawDispatcherState& state = GlobalApplicationDrawDispatcherState();


        const int originalFilter = filter;
        int bucketMask = filter & 0x000F0000;
        if (bucketMask == 0)
            bucketMask = 0x000F0000;

        ApplicationVidTable& vidTable = GlobalApplicationVidTable();
        VID* requestedVid = EmptyVid;
        const int exactNvid = filter & 0x0FFF;
        const bool exactNvidFilter = exactNvid != 0 && (filter & 0x00001000) == 0;
        int typeMask = 0;
        if (exactNvidFilter)
        {


            if (exactNvid < vidTable.count())
            {
                if (VID* const slot = vidTable.slot(exactNvid))
                    requestedVid = slot;
            }
            if ((requestedVid->properties() & 0x40u) != 0u)
                filter |= 0x00008000;
            typeMask = static_cast<int>(requestedVid->spriteTypeId());
        }
        else
        {
            typeMask = (filter >> 20) & 0x67F;
            if (typeMask == 0)
                typeMask = 1663;
        }

        SPRITE* selected = nullptr;
        const long double previousDistance = previous
            ? as1::approximatePlanarDistance(x - previous->X(), y - previous->Y())
            : -1.0L;

        float bestDistance = radius;

        auto passesFilter = [&](SPRITE* candidate) noexcept -> bool
        {
            VID* const candidateVid = candidate->Vid();
            if ((typeMask & static_cast<int>(candidateVid->spriteTypeId())) == 0)
                return false;
            const int candidateBucketBit = 0x10000 << candidate->armyIndex();
            if ((candidateBucketBit & bucketMask) == 0)
                return false;
            if ((filter & static_cast<int>(0x80000000u)) != 0 && (candidate->runtimeFlags() & SPRITE::CommandBitsMask) != 0u)
                return false;
            if ((filter & 0x1000) != 0 && candidateVid->spriteClassId() != static_cast<DWORD>(filter & 0x7FF))
                return false;
            if (exactNvidFilter && candidateVid->nvid() != exactNvid)
                return false;
            return true;
        };

        auto acceptMetric = [&](SPRITE* candidate, long double metric) noexcept
        {
            if (!(metric > previousDistance))
                return;
            if (metric < static_cast<long double>(bestDistance) ||
                std::isnan(bestDistance))
            {
                bestDistance = static_cast<float>(metric);
                selected = candidate;
            }
        };

        auto considerDistance = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;

            const float dx = x - candidate->X();
            const float dy = y - candidate->Y();
            acceptMetric(candidate, as1::approximatePlanarDistance(dx, dy));
        };

        auto considerWeighted = [&](SPRITE* candidate) noexcept
        {
            if (candidate->childBacklink() != nullptr || !passesFilter(candidate))
                return;


            const float dx = x - candidate->X();
            const float dy = y - candidate->Y();
            acceptMetric(candidate, as1::approximatePlanarDistance(dx, dy));
        };

        if ((filter & 0x8000) != 0)
        {
            SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
            for (SPRITE* candidate = hash->FirstHashInBox(x - radius, y - radius, x + radius, y + radius);
                 candidate;
                 candidate = hash->NextHashInBox())
            {
                considerDistance(candidate);
            }
            return selected;
        }

        if ((typeMask & 0x0C) == 0 || (typeMask & 0x673) != 0)
        {


            if (exactNvidFilter &&
                (requestedVid->spriteClassId() == 10u || requestedVid->spriteClassId() == 19u))
            {
                BaseSpriteList<0>& frameList = applicationFrameSpriteList();
                int index = static_cast<int>(frameList.count()) - 1;
                if (index < 0)
                    return selected;
                SPRITE* candidate = frameList.at(static_cast<std::size_t>(index));
                if (!candidate)
                    return selected;
                for (;;)
                {
                    considerWeighted(candidate);
                    --index;
                    if (index < 0)
                        break;
                    candidate = frameList.at(static_cast<std::size_t>(index));
                    if (!candidate)
                        return selected;
                }
                return selected;
            }

            int firstPass = 0;


            int endPass = ApplicationDrawDispatcherState::PassCount - 1;
            if (exactNvidFilter)
            {
                firstPass = requestedVid->renderLayer();
                endPass = firstPass + 1;
            }

            for (int pass = firstPass; pass < endPass; ++pass)
            {
                int cursor = state.drawPassBucket(pass).count();
                for (SPRITE* candidate = previousSpriteInDrawPass(state, pass, &cursor);
                     candidate;
                     candidate = previousSpriteInDrawPass(state, pass, &cursor))
                {
                    considerWeighted(candidate);
                }
            }
            return selected;
        }

        SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
        core::List<SPRITE*>& overflow = hash->mutableOverflowList();
        int* const cursor = hash->reverseCursorAddress();
        for (SPRITE* candidate = overflow.BeginIterate(cursor);
             candidate;
             candidate = overflow.NextIterate(cursor))
        {
            considerWeighted(candidate);
        }
        (void)originalFilter;
        return selected;

    }



    SPRITE* Application::previousSpriteInDrawPass(ApplicationDrawDispatcherState& state, int pass, int* cursor)
    {

        int index = --(*cursor);
        if (index < 0)
            return nullptr;
        const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
        while (!bucket.spriteAt(index))
        {
            index = --(*cursor);
            if (index < 0)
                return nullptr;
        }
        return bucket.spriteAt(index);
    }







    int Application::drawSpritePass(ApplicationDrawDispatcherState& state, int pass)
    {
        const int currentPass = pass;
        int cursor = 0;
        if (pass != 0 && pass != 10)
        {
            GRAPH* const graph = Graph;
            constexpr float half = 0.5f;
            const int viewCenterX = drawPassMultiplyAddAndConvertToInt32(
                graph->screenWidth(), half, state.cameraShiftX());
            const int viewCenterY = drawPassMultiplyAddAndConvertToInt32(
                graph->screenHeight(), half, state.cameraShiftY());

            cursor = state.drawPassBucket(pass).count();
            SPRITE* sprite = previousSpriteInDrawPass(state, pass, &cursor);
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                {
                    const std::uint32_t xMaskValue =
                        static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->X())) -
                        static_cast<std::uint32_t>(viewCenterX) + 0x400u;

                    bool draw = false;
                    if ((xMaskValue & 0xFFFFF800u) != 0u)
                    {
                        const std::int32_t topYDelta = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->Y())) -
                            static_cast<std::uint32_t>(viewCenterY));
                        if (topYDelta >= 0x200)
                            draw = true;
                    }
                    else
                    {
                        const std::uint32_t baseYMaskValue =
                            static_cast<std::uint32_t>(drawPassSubtractAndConvertToInt32(sprite->Y(), sprite->Z())) -
                            static_cast<std::uint32_t>(viewCenterY) + 0x200u;
                        if ((baseYMaskValue & 0xFFFFFC00u) == 0u)
                        {
                            draw = true;
                        }
                        else
                        {
                            const std::int32_t topYDelta = static_cast<std::int32_t>(
                                static_cast<std::uint32_t>(drawPassConvertFloatToInt32(sprite->Y())) -
                                static_cast<std::uint32_t>(viewCenterY));
                            if (topYDelta >= 0x200)
                                draw = true;
                        }
                    }

                    if (draw)
                        sprite->Draw();
                }

                --cursor;
                const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            }
        }
        else
        {
            const ApplicationDrawPassBucket& bucket = state.drawPassBucket(pass);
            cursor = bucket.count() - 1;
            while (cursor >= 0 && !bucket.spriteAt(cursor))
                --cursor;
            SPRITE* sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            while (sprite)
            {
                if (spriteVisibleForDrawPass(sprite))
                    sprite->Draw();
                --cursor;
                while (cursor >= 0 && !bucket.spriteAt(cursor))
                    --cursor;
                sprite = cursor >= 0 ? bucket.spriteAt(cursor) : nullptr;
            }
        }

        MOUSE* const mouse = mouseInstanceRef();
        int result = mouse->hardwareCursorEnabled();
        VID* const rootMouseVid = mouse->Vid();
        if (result == 0 && (rootMouseVid->properties() & 0x00008000u) == 0u && mouse)
        {
            for (SPRITE* node = mouse; node; node = node->childChain())
            {
                VID* const vid = node->Vid();
                if (vid->renderLayer() != currentPass)
                    continue;

                result = static_cast<int>(node->runtimeFlags());
                if (!node->isDrawSuppressed())
                    result = drawSpriteAndCaptureReturnValue(node);
            }
        }
        return result;
    }





    int Application::callScriptFunctionInternal(int functionIndex, int firstArgument, int secondArgument, int thirdArgument)
    {
        auto* const raw = reinterpret_cast<std::uint8_t*>(this);
        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            raw + application_layout::Flags);
        if ((flags & application_flags::ScriptCallbacksDisabled) != 0u)
            return 0;

        auto* const scriptOwner = reinterpret_cast<SCRIPT*>(
            raw + application_layout::ScriptRuntime);
        return scriptOwner->callFunction(functionIndex, "ppi", firstArgument, secondArgument, thirdArgument);
    }



    void Application::removeNamedSprite(SPRITE* sprite) noexcept
    {
        struct RawNamedSpriteList
        {
            std::uint32_t vtableToken;
            std::int32_t count;
            std::int32_t capacity;
            SPRITE** items;
        };

        auto* const list = reinterpret_cast<RawNamedSpriteList*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::TailSpriteList);
        const int originalCount = list->count;
        int index = originalCount;
        if (index == 0)
            return;
        do
        {
            --index;
            if (list->items[index] != sprite)
                continue;
            if (index < 0 || index >= originalCount)
                return;
            const int newCount = originalCount - 1;
            list->count = newCount;
            list->items[index] = list->items[newCount];
            return;
        } while (index != 0);
    }


    void Application::appendNamedSprite(SPRITE* sprite)
    {
        if (!sprite)
            return;
        void* const exData = sprite->ExData();
        if (!exData)
            return;
        const char* const nameText = *reinterpret_cast<const char* const*>(
            static_cast<const std::uint8_t*>(exData) + 0x40u);
        if (!nameText || *nameText == '\0')
            return;
        struct RawNamedSpriteList
        {
            std::uint32_t vtableToken;
            std::int32_t count;
            std::int32_t capacity;
            SPRITE** items;
        };
        auto* const list = reinterpret_cast<RawNamedSpriteList*>(
            reinterpret_cast<std::uint8_t*>(this) + application_layout::TailSpriteList);
        if (list->count >= list->capacity)
        {
            const int oldCapacity = list->capacity;
            const int newCapacity = oldCapacity * 2 + 4;
            if (newCapacity > oldCapacity)
            {
                SPRITE** const oldItems = list->items;
                SPRITE** const replacement = static_cast<SPRITE**>(
                    ::operator new(sizeof(SPRITE*) * static_cast<std::size_t>(newCapacity), std::nothrow));
                list->items = replacement;
                if (!replacement)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        replacement[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                list->capacity = newCapacity;
            }
        }
        list->items[list->count++] = sprite;
    }

    SPRITE* Application::createSprite(VID* vid, VECTOR xyz, ANGLE direction, SPRITE* parent)
    {
        MAP* const owner = Map;
        if (!owner || !vid)
            return nullptr;

        VID* selectedVid = vid;
        if ((selectedVid->runtimeAuxFlags() & 0x10u) != 0u)
            selectedVid = resolveRegionMappedVid(selectedVid, xyz.x, xyz.y, xyz.z);

        SPRITE* sprite = nullptr;
        switch (selectedVid->spriteClassId())
        {
        case B_UNIT:
            sprite = new UNIT(owner, selectedVid, xyz, direction, parent);
            break;
        case B_AVIA:
            sprite = new AVIA(owner, selectedVid, xyz, direction, parent);
            break;
        case B_CANNON:
            sprite = new CANNON(owner, selectedVid, xyz, direction, parent);
            break;
        case B_PRIMITIVE:
            sprite = new PRIMITIVE(owner, selectedVid, xyz, direction, parent);
            break;
        case B_MAN:
            sprite = new MAN(owner, selectedVid, xyz, direction, parent);
            break;
        case B_BUILDEDTERRAIN:


            sprite = new BUILDED_TERRAIN(owner, selectedVid, xyz, direction, parent);
            break;
        case B_SPRITE:
            sprite = new SPRITE(owner, selectedVid, xyz, direction, parent);
            break;
        case B_FRAME:
            sprite = new FRAME(owner, selectedVid, xyz, direction, parent);
            break;
        case B_LINKER:
            sprite = new LINKER(owner, selectedVid, xyz, direction, parent);
            break;
        case B_TEXT:
            sprite = new STEXT(owner, selectedVid, xyz, direction, parent);
            break;
        case B_REGION:
            sprite = new REGION(owner, selectedVid, xyz, direction, parent);
            break;
        default:
            return nullptr;
        }

        const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(
            reinterpret_cast<const std::uint8_t*>(this) + application_layout::Flags);


        if (sprite && (flags & 0x21u) == 0u)
        {
            const int functionIndex = sprite->Vid()->birthScriptFunction();
            if (functionIndex >= 0)
            {
                const int spriteArg = static_cast<int>(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(sprite)));
                callScriptFunctionInternal(functionIndex, spriteArg, 0);
            }
        }
        return sprite;
    }


} }
