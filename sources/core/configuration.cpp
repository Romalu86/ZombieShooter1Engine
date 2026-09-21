#include "core/configuration.h"


namespace as1
{
    REGISTRY::REGISTRY(const STRING& registryPath)
        : m_path(registryPath)
    {
    }


    const STRING* REGISTRY::Path() noexcept
    {


        return &m_path;
    }
}

namespace as1 { namespace core
{
    STRING* g_startupStringsIniPathOwner = nullptr;
    REGISTRY* g_startupRegistryPathOwner = nullptr;

    namespace
    {

        StartupSettingsBlock g_startupSettings = {
            {},
            {1024u},
            {768u},
            {16u, 32u},
            0u, 0, 1024, 768, 32, 1
        };
    }

    StartupSettingsBlock& StartupSettings() noexcept
    {
        return g_startupSettings;
    }


} }
