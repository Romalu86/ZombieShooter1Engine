#include "core/profile_p.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1 { namespace core { namespace profile_p
{

    unsigned int readProfileIntValue(const STRING& fileName, const STRING& section, const STRING& key, int defaultValue)
    {

        return static_cast<unsigned int>(::GetPrivateProfileIntA(section.c_str(), key.c_str(), defaultValue, fileName.c_str()));
    }


    STRING& readProfileStringInto(STRING& out, const STRING& fileName, const STRING& section, const STRING& key, const STRING& defaultValue)
    {

        char returnedString[0x8000];
        ::GetPrivateProfileStringA(section.c_str(),
                                   key.c_str(),
                                   defaultValue.c_str(),
                                   returnedString,
                                   0x7FFFu,
                                   fileName.c_str());
        out.AssignAllocatedCopyWithoutRelease(returnedString);
        return out;
    }
} } }
