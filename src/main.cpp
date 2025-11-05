#include "precompiled.h"

#include <wx/wx.h>
#include "ui/App.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wxDISABLE_DEBUG_SUPPORT();
    return wxEntry();
}

wxIMPLEMENT_APP(icd::App);
