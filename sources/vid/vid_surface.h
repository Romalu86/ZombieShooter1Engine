#pragma once
#include "vid.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include <array>
#include <cstddef>

namespace as1
{
class VID_SURFACE : public VID
{
public:
    VID_SURFACE();
    VID_SURFACE(const VID_SURFACE& other);
    ~VID_SURFACE() override;
    VID_SURFACE* CreateMirror() override;
    void SetLayer() override;

    void Draw(const SPRITE* sprite) override;

    void Load(RESOURCE* globalRes) override;
    BASE_TEXTURE* const* surfaceTextureOwners() const { return m_surfaceTextureOwners; }
    VID_TEXCOOR* const* surfaceTexcoordOwners() const { return m_surfaceTexcoordOwners; }

private:



    std::array<BYTE, 0x28> m_rawSurfaceTail490_4B7;


    mutable VID_TEXCOOR** m_surfaceTexcoordOwners;
    mutable BASE_TEXTURE** m_surfaceTextureOwners;


    DWORD m_surfaceSourceFormat;
};
}
