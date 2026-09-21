#include "vid/vid_font.h"

#include "graph.h"
#include "map.h"
#include "sprite.h"
#include "core/log.h"

#include <cmath>
#include <new>

namespace as1
{
    VID_FONT::VID_FONT() = default;


    VID_FONT::VID_FONT(const VID_FONT& other)
        : VID()
    {
        nextMirror = other.nextMirrorVid();
        const_cast<VID_FONT&>(other).nextMirror = this;
        layer = other.layer;
        type = other.type;
        frameSpeedDefault = other.frameSpeedDefault;
        noCadr = other.noCadr;
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));
        m_fontOwner = other.m_fontOwner;
    }


    VID_FONT::~VID_FONT()
    {
        if (isMirrorChainOwner())
            destroyVidFontOwner(m_fontOwner);
        else
            m_fontOwner = nullptr;
    }

    VID_FONT* VID_FONT::CreateMirror()
    {
        return new (std::nothrow) VID_FONT(*this);
    }

    void VID_FONT::Load(RESOURCE*)
    {
        const int sizeX = static_cast<int>(sizeXYZ.x);
        const int sizeY = static_cast<int>(sizeXYZ.y);

        frameSpeedDefault = 71u;
        noCadr = 256;
        setVidWidth(static_cast<short>(sizeX));
        setVidHeight(static_cast<short>(sizeY));
        type = static_cast<WORD>(VID_TYPE_FONT);

        m_fontOwner = createVidFontOwner(vidName, sizeX, sizeY);
    }

    void VID_FONT::Draw(const SPRITE* sprite)
    {
        if (!m_fontOwner)
            return;

        Gamma selected{};
        selected = sprite->GetGamma();

        MAP* const map = Map;
        const float x = map->ToScreenX(sprite->X());
        const float y = map->ToScreenY(sprite->Y(), sprite->Z());


        const DWORD color = selected.second | 0xFF000000u;
        (void)drawVidFontOwner(m_fontOwner, x, y, color, "", 0u);
    }

    int VID_FONT::HaveShadow() const
    {
        return 0;
    }

    void VID_FONT::SetLayer()
    {


        layer = 18;
    }


    int VID_FONT::InvalidateDeviceObjects() noexcept
    {
        return invalidateVidFontOwner(m_fontOwner);
    }


    int VID_FONT::RestoreDeviceObjects() noexcept
    {
        return restoreVidFontOwner(m_fontOwner);
    }
}
