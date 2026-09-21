#pragma once
#include "vid_software.h"

namespace as1
{
int composeHardwareZOpaqueSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept;
int composeHardwareZAlphaSpan(const WORD* sourceZ, const WORD* sourceColor, const WORD* destinationZ, WORD* destinationColor, int count) noexcept;
class VID_HARDWARE_Z : public VID_SOFTWARE
{
public:
    VID_HARDWARE_Z() = default;
    VID_HARDWARE_Z(const VID_HARDWARE_Z& other);
    VID_HARDWARE_Z* CreateMirror() override;
    void SetLayer() override;
    void SetGamma(const Gamma& rawGamma, unsigned n_gamma) override;
    void Draw(const SPRITE* sprite) override;
};

}
