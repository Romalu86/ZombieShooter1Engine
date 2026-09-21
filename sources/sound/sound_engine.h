#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/resource.h"
#include "win/sound/sound_engine_p_win.h"
#include <cstddef>
#include <type_traits>

namespace as1 { namespace sound
{

    using Engine = win::sound::SoundEngineWin;


    extern Engine* g_globalSoundEngine;
} }
