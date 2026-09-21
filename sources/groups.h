#pragma once

#include "base_sprite_list.h"
#include "core/types.h"
#include <cstddef>
#include <cstdint>

namespace as1
{
    class MAP;
    class SPRITE;
    class Group;
    class RESOURCE;

    class Group : public core::List<SPRITE*>
    {
    public:
        Group(Group* insertAfter = nullptr, SPRITE* firstSprite = nullptr) noexcept;
        ~Group();

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

        Group* scalarDeletingDestructor(unsigned char deleteSelfFlag) noexcept;
        void DrawNumber(int number);
        void Insert(SPRITE* sprite);

        float centerX() const noexcept { return m_centerX; }
        float centerY() const noexcept { return m_centerY; }
        Group* nextGroup() const noexcept { return m_next; }
        void setNextGroup(Group* next) noexcept { m_next = next; }

        static std::uint32_t CurrentImageGroupVtable() noexcept;

    private:
        friend class GROUPS;

        float m_centerX;
        float m_centerY;
        std::uint32_t m_raw18;
        std::uint32_t m_raw1C;
        Group* m_next;
    };

    class GROUPS : public Group
    {
    public:
        __forceinline GROUPS() noexcept : Group(nullptr, nullptr) {}
        __forceinline ~GROUPS() = default;

        GROUPS(const GROUPS&) = delete;
        GROUPS& operator=(const GROUPS&) = delete;

        void Load(RESOURCE* map);
        void Save(RESOURCE* map);
        void DeletePointerToSprite(SPRITE* sprite);
        void DrawNumber();
        Group* Next(const Group* group) noexcept;

        __forceinline
        Group* first() noexcept
        {
            Group* const node = nextGroup();
            return node != this ? node : nullptr;
        }

        __forceinline
        const Group* first() const noexcept
        {
            const Group* const node = nextGroup();
            return node != this ? node : nullptr;
        }

        __forceinline
        std::size_t size() const noexcept
        {
            std::size_t result = 0;
            for (const Group* node = first(); node; ++result)
            {
                const Group* const next = node->nextGroup();
                node = next != this ? next : nullptr;
            }
            return result;
        }

        __forceinline
        bool empty() const noexcept
        {
            return nextGroup() == this;
        }

        __forceinline
        std::size_t refCount() const noexcept
        {
            std::size_t result = 0;
            for (const Group* node = first(); node;)
            {
                result += static_cast<std::size_t>(node->count());
                const Group* const next = node->nextGroup();
                node = next != this ? next : nullptr;
            }
            return result;
        }
    };

}
