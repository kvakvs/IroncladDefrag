#include "precompiled.h"

#include <wx/wx.h>
#include "ui/App.h"

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // AddDllDirectory(L"F:\\Projects\\IroncladDefrag\\3rdparty\\wxWidgets\\lib\\vc14x_x64_dll")

    wxDISABLE_DEBUG_SUPPORT();
    return wxEntry();
}

wxIMPLEMENT_APP(icd::App);
