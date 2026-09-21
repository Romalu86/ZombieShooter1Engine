#include "base_sprite_list.h"

#include <cmath>
#include <algorithm>
#include <new>
#include <cstring>
#include "sprite.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "menu.h"
#include "input.h"
#include "vid/vid.h"

namespace as1
{


    namespace
    {
        class CoreSpriteListVtableOwner
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            { return reinterpret_cast<core::List<SPRITE*>*>(this)->pointerListDeletingDestructor(flags); }
        };

        class BaseSpriteListVtableOwner
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            { return reinterpret_cast<BaseSpriteList<0>*>(this)->baseSpriteListDeletingDestructor(flags); }
        };

        template <class T>
        std::uint32_t currentImageListVtable(std::uint32_t portableToken) noexcept
        {
            static T owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
        }
    }

    std::uint32_t core::List<SPRITE*>::CurrentImageCoreListVtable() noexcept
    {
        return currentImageListVtable<CoreSpriteListVtableOwner>(0x00001001u);
    }

    std::uint32_t BaseSpriteList<0>::CurrentImageBaseSpriteListVtable() noexcept
    {
        return currentImageListVtable<BaseSpriteListVtableOwner>(0x00001002u);
    }


    core::List<SPRITE*>::List() noexcept
    {
        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setVtableToken(CurrentImageCoreListVtable());
    }


    core::List<SPRITE*>::~List()
    {
        setVtableToken(CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
    }


    BaseSpriteList<0>::BaseSpriteList() noexcept
    {
        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setVtableToken(CurrentImageBaseSpriteListVtable());
    }

    BaseSpriteList<0>::~BaseSpriteList() = default;


    void BaseSpriteList<0>::append(SPRITE* sprite)
    {
        if (!sprite)
            return;
        (void)sprite->AddListReference();

        if (m_count >= m_capacity)
        {
            const int nextCapacity = m_capacity * 2 + 4;
            if (nextCapacity > m_capacity)
            {
                SPRITE** const oldItems = m_items;
                SPRITE** const nextItems = static_cast<SPRITE**>(
                    ::operator new(sizeof(SPRITE*) * static_cast<std::size_t>(nextCapacity), std::nothrow));
                m_items = nextItems;
                if (!nextItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", nextCapacity);

                if (oldItems)
                {
                    for (int i = 0; i < m_capacity; ++i)
                        nextItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_capacity = nextCapacity;
            }
        }

        m_items[m_count++] = sprite;
    }


    int BaseSpriteList<0>::removeSorted(SPRITE* sprite)
    {
        if (!sprite)
            return 1;

        int index = m_count;
        while (index != 0)
        {
            --index;
            if (m_items[index] == sprite)
                break;
        }
        if (index < 0 || index >= m_count || m_items[index] != sprite)
            return 1;

        --m_count;
        m_items[index] = m_items[m_count];

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs > 0)
            return 0;
        if (refs < 0)
        {
            VID* const vid = sprite->Vid();
            LOG::ResourceError("SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
            return 0;
        }
        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        return 0;
    }


    int BaseSpriteList<0>::DeleteSpriteNumber(int index)
    {
        if (index < 0 || index >= m_count)
            return 1;

        SPRITE* const sprite = m_items[index];
        --m_count;
        m_items[index] = m_items[m_count];

        const int refs = sprite->listReferenceCount() - 1;
        sprite->setListReferenceCount(refs);
        if (refs < 0)
        {
            VID* const vid = sprite->Vid();
            LOG::ResourceError("SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
            return 0;
        }
        if (sprite)
            DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        return 0;
    }


    void BaseSpriteList<0>::deleteAllSprites()
    {
        for (int first = 0; first < m_count; ++first)
        {
            for (int scan = m_count - 1; scan > first; --scan)
            {
                SPRITE* const sprite = m_items[first];
                if (!sprite || sprite != m_items[scan])
                    continue;

                const int refs = sprite->listReferenceCount() - 1;
                sprite->setListReferenceCount(refs);
                if (refs < 0)
                {
                    VID* const vid = sprite->Vid();
                    LOG::ResourceError("SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
                }
                else if (refs == 0)
                {
                    DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                }

                --m_count;
                m_items[scan] = m_items[m_count];
            }
        }

        for (int index = m_count - 1; index >= 0; --index)
        {
            if (m_items[index])
                (void)DeleteSpriteNumber(index);
        }


        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }


    void BaseSpriteList<0>::clear()
    {
        for (int index = m_count - 1; index >= 0; --index)
        {
            SPRITE* const sprite = m_items[index];
            if (!sprite)
                continue;

            const int refs = sprite->listReferenceCount() - 1;
            sprite->setListReferenceCount(refs);
            if (refs > 0)
            {
                --m_count;
                m_items[index] = m_items[m_count];
                continue;
            }

            if (refs < 0)
            {
                VID* const vid = sprite->Vid();
                LOG::ResourceError("SPRITE %i", 4, "noRef at Release", refs, vid ? vid->nvid() : -1);
                continue;
            }

            DeleteSpriteThroughVirtualDeletingDestructor(sprite);
        }


        SPRITE** const storage = m_items;
        m_capacity = 0;
        m_count = 0;
        if (storage)
            ::operator delete(storage);
        m_items = nullptr;
    }


    BaseSpriteList<0> g_spriteWorkList;



    SPRITE* core::List<SPRITE*>::BeginIterate(int* cursor) const noexcept
    {

        if (m_count == 0)
            return nullptr;
        const int index = m_count - 1;
        *cursor = index;
        return m_items[index];
    }


    SPRITE* core::List<SPRITE*>::NextIterate(int* cursor) const noexcept
    {

        if (*cursor > m_count)
            *cursor = m_count;
        const int index = *cursor - 1;
        *cursor = index;
        return index >= 0 ? m_items[index] : nullptr;
    }


    void* core::List<SPRITE*>::pointerListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        core::List<SPRITE*>* const self = this;
        setVtableToken(CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void* BaseSpriteList<0>::baseSpriteListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        if ((deletingDestructorFlags & 2u) != 0u)
        {
            unsigned char* const first = reinterpret_cast<unsigned char*>(this);
            auto* const cookie = reinterpret_cast<std::uint32_t*>(first) - 1;
            const std::uint32_t count = *cookie;
            for (std::uint32_t i = count; i != 0u; --i)
            {
                auto* const record = reinterpret_cast<BaseSpriteList<0>*>(
                    first + static_cast<std::size_t>(i - 1u) * 0x10u);
                record->setVtableToken(core::List<SPRITE*>::CurrentImageCoreListVtable());
                ::operator delete(record->m_items);
                record->m_items = nullptr;
                record->m_count = 0;
            }
            void* const allocation = static_cast<void*>(cookie);
            if ((deletingDestructorFlags & 1u) != 0u)
                ::operator delete(allocation);
            return allocation;
        }

        BaseSpriteList<0>* const self = this;
        setVtableToken(core::List<SPRITE*>::CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if ((deletingDestructorFlags & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }



}

