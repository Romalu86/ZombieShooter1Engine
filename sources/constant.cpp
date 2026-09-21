#include "constant.h"
#include "core/resource.h"
#include "core/log.h"

namespace as1
{
    CONSTANT* g_baseConstants = nullptr;

    CONSTANT* CONSTANT::Load(RESOURCE* res)
    {
        if (res->GoBegin(RESOURCE::ResTypes::CONSTANT))
        {
            LOG::Write("!!!ERROR!!! CNST Load Constant section not found");
            return this;
        }

        (void)res->read(&raw[0], 4u);
        (void)res->read(&raw[1], 4u);
        (void)res->read(&raw[2], 4u);
        (void)res->read(&raw[3], 4u);
        (void)res->read(&raw[4], 4u);
        (void)res->read(&raw[5], 4u);
        (void)res->read(&raw[6], 4u);
        (void)res->read(&raw[7], 4u);
        (void)res->read(&raw[8], 4u);
        (void)res->read(&raw[9], 4u);

        DWORD discardedCnstValue;
        (void)res->read(&discardedCnstValue, 4u);

        (void)res->read(&raw[11], 4u);
        (void)res->read(&raw[12], 4u);
        (void)res->read(&raw[13], 4u);
        (void)res->read(&raw[14], 4u);
        (void)res->read(&raw[15], 4u);
        (void)res->read(&raw[16], 4u);
        (void)res->read(&raw[17], 4u);
        (void)res->read(&raw[18], 4u);
        (void)res->read(&raw[19], 4u);
        (void)res->read(&raw[20], 4u);
        (void)res->read(&raw[21], 4u);
        (void)res->read(&raw[22], 4u);
        (void)res->read(&raw[23], 4u);
        (void)res->read(&raw[24], 4u);
        (void)res->read(&raw[25], 4u);

        float* const values = reinterpret_cast<float*>(raw.data());
        values[0] /= 1000.0f;
        values[1] /= 1000.0f;
        values[2] /= 1000000.0f;
        values[3] /= 1000000.0f;
        values[7] /= 1000.0f;
        values[6] /= 1000.0f;
        values[24] /= 1000.0f;
        return this;
    }
}
