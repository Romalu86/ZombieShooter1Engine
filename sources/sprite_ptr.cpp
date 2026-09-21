#include "sprite.h"
#include "vid/vid.h"
#include "core/log.h"

namespace as1
{
    PTR_SPRITE::~PTR_SPRITE()
    {
        if (sprite)
        {
            const int nvid = sprite->Vid() ? sprite->Vid()->nVid : -1;
            LOG::ResourceError("SPRITE %i", 10, "PTR_SPRITE with this sprite not clear", 0, nvid);
        }
    }
}
