#pragma once
#include "vid.h"

namespace as1
{
class BASE_TEXTURE;
class VID_HARDWARE;
class RESOURCE;

class VID_HARDWARE : public VID
{
public:

    struct TEX_SIZE
    {
        int marker = 0;
        int textureIndex = 0;
        int sourceX = 0;
        int sourceY = 0;
        int width = 0;
        int height = 0;
        int destinationX = 0;
        int destinationY = 0;
        int next = 0;
    };


    VID_HARDWARE();
    VID_HARDWARE(const VID_HARDWARE& other);
    VID_HARDWARE(int nvid, int width, int height);
    ~VID_HARDWARE() override;

    VID_HARDWARE* CreateMirror() override;
    void SetLayer() override;
    void Draw(const SPRITE* sprite) override;

    void Load(RESOURCE* resource) override;

    void AddVidToVid(SPRITE* sprite) override;

    WORD texturePageCount() const noexcept { return m_texturePageCount; }
    TEX_SIZE* textureLayout() const noexcept { return m_textureLayout; }
    BASE_TEXTURE** texturePages() const noexcept { return m_texturePages; }

private:

    TEX_SIZE* m_textureLayout = nullptr;
    WORD m_texturePageCount = 0;
    BASE_TEXTURE** m_texturePages = nullptr;

};

}
