#pragma once
#include "core/as_string.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace as1 { namespace win
{
    struct DialogItemRef
    {
        HWND dialog = nullptr;
        int controlId = 0;


        LRESULT sendControlMessage(UINT message, WPARAM wparam, LPARAM lparam) const
        {
            return ::SendDlgItemMessageA(dialog, controlId, message, wparam, lparam);
        }

        bool isChecked() const;
        LRESULT currentSelection() const;
    };


    STRING& readDialogItemText(const DialogItemRef& item, STRING& out);
    inline void SetDlgItemVisible(HWND dialog, int controlId, bool visible)
    {
        ::ShowWindow(::GetDlgItem(dialog, controlId), visible ? SW_SHOW : SW_HIDE);
    }

    inline void SetDlgItemEnabled(HWND dialog, int controlId, bool enabled)
    {
        ::EnableWindow(::GetDlgItem(dialog, controlId), enabled ? TRUE : FALSE);
    }
} }
