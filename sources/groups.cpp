#include "groups.h"

#include "core/resource.h"
#include "core/application.h"
#include "graph.h"
#include "map.h"
#include "sprite.h"

#include <cstdint>
#include <new>

namespace as1
{
    namespace
    {
        constexpr std::int32_t END_GROUP_INT = -1;

        class GroupVtableOwner
        {
        public:
            virtual Group* deletingDestructor(unsigned char flags) noexcept
            { return reinterpret_cast<Group*>(this)->scalarDeletingDestructor(flags); }
        };
    }

    std::uint32_t Group::CurrentImageGroupVtable() noexcept
    {
        static GroupVtableOwner owner;
        return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
    }


    Group::Group(Group* insertAfter, SPRITE* firstSprite) noexcept
    {
        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        setVtableToken(CurrentImageGroupVtable());
        if (insertAfter)
        {
            m_next = insertAfter->m_next;
            insertAfter->m_next = this;
        }
        else
        {
            m_next = this;
        }
        if (firstSprite)
            Insert(firstSprite);
    }


    Group::~Group()
    {
        setVtableToken(CurrentImageGroupVtable());
        Group* const next = m_next;
        Group* predecessor = next;
        for (Group* cursor = next->m_next; cursor != this; cursor = cursor->m_next)
            predecessor = cursor;
        predecessor->m_next = next;
        setVtableToken(BaseSpriteList<0>::CurrentImageRelationListVtable());
        if (m_items)
            ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
    }


    Group* Group::scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept
    {
        Group* const self = this;
        self->~Group();
        if ((deleteSelfFlag & 1u) != 0u)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void Group::DrawNumber(int number)
    {
        for (int index = 0; index < m_count; ++index)
        {
            SPRITE* const sprite = m_items[static_cast<std::size_t>(index)];
            Graph->DrawText(sprite->ScreenX(), sprite->ScreenY(), "%i", number);
        }
    }


    void Group::Insert(SPRITE* sprite)
    {
        if (m_count != 0)
        {
            m_centerX = (sprite->X() + m_centerX) * 0.5f;
            m_centerY = (sprite->Y() + m_centerY) * 0.5f;
        }
        else
        {
            m_centerX = sprite->X();
            m_centerY = sprite->Y();
        }


        reinterpret_cast<BaseSpriteList<0>*>(this)->append(sprite);
    }


    Group* GROUPS::Next(const Group* group) noexcept
    {
        if (!group)
            return nullptr;
        Group* const next = group->nextGroup();
        return next != this ? next : nullptr;
    }


    void GROUPS::Load(RESOURCE* resource)
    {
        for (;;)
        {
            SPRITE* const firstSprite = Map->ReadPointer(resource);
            if (firstSprite == reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
                break;

            void* const memory = ::operator new(sizeof(Group), std::nothrow);
            Group* const group = memory ? new (memory) Group(this, firstSprite) : nullptr;
            if (!group)
                return;

            for (;;)
            {
                SPRITE* const sprite = Map->ReadPointer(resource);
                if (sprite == reinterpret_cast<SPRITE*>(static_cast<std::intptr_t>(-1)))
                    break;
                group->Insert(sprite);
            }
        }
    }


    void GROUPS::Save(RESOURCE* resource)
    {
        for (Group* group = first(); group;)
        {
            if (group->m_count != 0)
            {
                for (int index = 0; index < group->m_count; ++index)
                {
                    const std::uint32_t raw = static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(group->m_items[index]) & 0xFFFFFFFFu);
                    resource->write(&raw, 4u);
                }
                const std::int32_t end = END_GROUP_INT;
                resource->write(&end, 4u);
            }

            Group* const next = group->m_next;
            group = next != this ? next : nullptr;
        }

        const std::int32_t end = END_GROUP_INT;
        resource->write(&end, 4u);
    }


    void GROUPS::DeletePointerToSprite(SPRITE* sprite)
    {
        Group* group = first();
        while (group)
        {


            if (reinterpret_cast<BaseSpriteList<0>*>(group)->removeSorted(sprite) == 0 && group->m_count == 0)
            {
                Group* const dead = group;
                Group* const next = group->m_next;
                group = next != this ? next : nullptr;
                delete dead;
            }
            else
            {
                Group* const next = group->m_next;
                group = next != this ? next : nullptr;
            }
        }
    }


    void GROUPS::DrawNumber()
    {
        Group* group = first();
        int number = 0;
        while (group)
        {
            group->DrawNumber(number++);
            Group* const next = group->m_next;
            group = next != this ? next : nullptr;
        }
    }

}
