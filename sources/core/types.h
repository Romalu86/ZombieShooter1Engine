#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <stdexcept>
#include <sstream>


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "fourcc.h"
#include "../graphics/vector.h"
#include "../graphics/angle.h"
#include "../graphics/gamma.h"
