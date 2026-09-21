#pragma once
#include "vid.h"
#include <array>

namespace as1
{
int drawOpaquePaletteSpanWithDepth(BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept;
int drawAlphaPaletteSpanWithDepth(const BYTE* paletteIndexes, WORD* depth, DWORD* color, int count) noexcept;

class VID_SOFTWARE : public VID
{
public:
    VID_SOFTWARE();
    VID_SOFTWARE(const VID_SOFTWARE& other);
    ~VID_SOFTWARE() override;
    VID_SOFTWARE* CreateMirror() override;
    void Load(RESOURCE* resource) override;
    void SetLayer() override;
    void Draw(const SPRITE* sprite) override;
    int DrawShadow(const SPRITE* sprite) const override;
    int HaveShadow() const override;
    virtual int PaletteSize() const;

    DWORD* frameOffsets() const noexcept { return m_frameOffsets; }
    DWORD frameStorageBytes() const noexcept { return m_frameStorageBytes; }
    BYTE* frameStorage() const noexcept { return m_frameStorage; }
    void setFrameOffsets(DWORD* value) noexcept { m_frameOffsets = value; }
    void setFrameStorageBytes(DWORD value) noexcept { m_frameStorageBytes = value; }
    void setFrameStorage(BYTE* value) noexcept { m_frameStorage = value; }

    void SetGamma(const Gamma& rawGamma, unsigned n_gamma) override;
    void SetReColorForArmy(int value) override;

    virtual void ApplyGammaToPaletteRaw(void* palette, const Gamma& rawGamma);


protected:
    BYTE*& gammaPaletteScratchRef() noexcept { return m_gammaPaletteScratch; }

private:


    DWORD* m_frameOffsets = nullptr;
    DWORD m_frameStorageBytes = 0;
    BYTE* m_frameStorage = nullptr;


    BYTE* m_gammaPaletteScratch = nullptr;
};

}
