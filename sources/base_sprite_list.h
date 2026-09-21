#pragma once

#include <cstddef>
#include <cstdint>

#include "core/as_string.h"

namespace as1
{
    class SPRITE;
    class SPRITE_COLLECTOR;
    namespace input { struct InputMessageState; }

    template <int Indexed>
    class BaseSpriteList;


    namespace core
    {
        struct ApplicationDrawPassBucket;

        template <class T>
        class List;


        template <>
        class List<SPRITE*>
        {
        public:
            List() noexcept;
            ~List();

            List(const List&) = delete;
            List& operator=(const List&) = delete;

            SPRITE* BeginIterate(int* cursor) const noexcept;
            SPRITE* NextIterate(int* cursor) const noexcept;

            __forceinline
            std::size_t count() const noexcept
            {
                return m_count > 0 ? static_cast<std::size_t>(m_count) : 0u;
            }
            __forceinline
            bool empty() const noexcept { return m_count == 0; }
            __forceinline
            SPRITE* at(std::size_t index) const noexcept
            {
                return m_items && index < static_cast<std::size_t>(m_count) ? m_items[index] : nullptr;
            }
            __forceinline
            int activeCount() const noexcept { return m_count; }
            __forceinline
            SPRITE* const* data() const noexcept { return m_items; }
            __forceinline
            bool contains(SPRITE* sprite) const noexcept
            {
                if (!m_items)
                    return false;
                for (int i = 0; i < m_count; ++i)
                    if (m_items[i] == sprite)
                        return true;
                return false;
            }


            void* pointerListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept;

            static std::uint32_t CurrentImageCoreListVtable() noexcept;

        protected:
            friend class ::as1::SPRITE_COLLECTOR;
            friend struct ApplicationDrawPassBucket;
            template <int> friend class ::as1::BaseSpriteList;
            void setVtableToken(std::uint32_t token) noexcept { m_vtableToken = token; }
            std::uint32_t m_vtableToken = 0;
            int m_count = 0;
            int m_capacity = 0;
            SPRITE** m_items = nullptr;
        };
    }


    template <>
    class BaseSpriteList<0> : public core::List<SPRITE*>
    {
    public:
        BaseSpriteList() noexcept;
        ~BaseSpriteList();

        BaseSpriteList(const BaseSpriteList&) = delete;
        BaseSpriteList& operator=(const BaseSpriteList&) = delete;


        void append(SPRITE* sprite);

        int removeSorted(SPRITE* sprite);


        int DeleteSpriteNumber(int index);

        void deleteAllSprites();

        void clear();


        void* baseSpriteListDeletingDestructor(unsigned char deletingDestructorFlags) noexcept;

        static std::uint32_t CurrentImageBaseSpriteListVtable() noexcept;


        static
        __forceinline
        std::uint32_t CurrentImageRelationListVtable() noexcept
        {
            return core::List<SPRITE*>::CurrentImageCoreListVtable();
        }
    };


    extern BaseSpriteList<0> g_spriteWorkList;

}
