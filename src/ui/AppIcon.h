#pragma once

#include <wx/iconbndl.h>

namespace icd::ui {

    // Every size of the app icon (T141, res/IroncladDefrag.ico embedded as "APPICON"), loaded once, so the frame and
    // the dialogs pick the sharpest one for the title bar, taskbar and Alt+Tab at any DPI.
    [[nodiscard]] const wxIconBundle& appIcons();

} // namespace icd::ui
