#pragma once
#include "vid_software.h"
#include "vid_hardware.h"

namespace as1
{
DWORD* expandSoftware16ColorToBgra(DWORD* destination, const WORD* source) noexcept;
int drawSoftware16AlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* destinationDepth, WORD* destinationColor, int count) noexcept;

class BASE_TEXTURE;

class VID_SOFTWARE16 : public VID_SOFTWARE
{
public:
    VID_SOFTWARE16() = default;
    VID_SOFTWARE16(const VID_SOFTWARE16& other);
    VID_SOFTWARE16* CreateMirror() override;
    void Draw(const SPRITE* sprite) override;

    void DrawToVid(SPRITE* sprite, void* texSize, BASE_TEXTURE* texture, BASE_TEXTURE* zTexture) override;
    int PaletteSize() const override;
    void SetReColorForArmy(int value) override;
    void ApplyGammaToPaletteRaw(void* palette, const Gamma& rawGamma) override;

private:
    DWORD UnpackRgb565ToBgra(WORD value) const;
    WORD PackRgb565FromBgra(DWORD value) const;

};

}
