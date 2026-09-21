#pragma once
#include <cstdint>

namespace as1
{
    class SPRITE;

    class PTR_SPRITE
    {
    public:
        PTR_SPRITE() noexcept : sprite(nullptr) {}
        PTR_SPRITE& operator=(SPRITE* value);


        operator SPRITE*() const noexcept { return sprite; }
        SPRITE* operator->() const noexcept { return sprite; }
        ~PTR_SPRITE();

    private:
        SPRITE* sprite;
    };


}
