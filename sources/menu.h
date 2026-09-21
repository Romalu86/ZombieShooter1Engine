#pragma once

#include <cstdint>

#include "base_sprite_list.h"
#include "core/as_string.h"

namespace as1
{
    namespace input { struct InputMessageState; }

    class MENU final : public BaseSpriteList<0>
    {
    public:
        MENU() noexcept;
        ~MENU();

        MENU(const MENU&) = delete;
        MENU& operator=(const MENU&) = delete;

        int Control(input::InputMessageState* input);

        int DeleteFromFile(const STRING& path);
        int Load(const STRING& path);

        int NDirUnderCursor() const noexcept;
        int NVidUnderCursor() const noexcept;
        SPRITE* SpriteWithName(const STRING& name) const noexcept;

        unsigned controlFlags() const noexcept { return m_controlFlags; }
        SPRITE* selectedSprite() const noexcept { return m_selectedSprite; }
        bool clearSelectedSpriteIfMatches(SPRITE* sprite) noexcept
        {
            if (m_selectedSprite != sprite)
                return false;
            m_selectedSprite = nullptr;
            return true;
        }

    private:
        unsigned m_controlFlags;
        SPRITE* m_selectedSprite;
    };

}
