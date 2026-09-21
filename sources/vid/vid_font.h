#pragma once

#include "vid/vid.h"

namespace as1
{
    class CD3DFont;

    class VID_FONT : public VID
    {
    public:
        VID_FONT();
        VID_FONT(const VID_FONT& other);
        ~VID_FONT() override;

        VID_FONT* CreateMirror() override;
        void Draw(const SPRITE* sprite) override;
        void Load(RESOURCE* resource) override;
        int HaveShadow() const override;
        void SetLayer() override;

        int InvalidateDeviceObjects() noexcept;
        int RestoreDeviceObjects() noexcept;

    private:
        CD3DFont* m_fontOwner = nullptr;
    };


}
