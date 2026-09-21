#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1 { namespace win
{
    void __cdecl destroyDialogBloodPasswordStorage();
    INT_PTR CALLBACK DialogFunc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
} }
