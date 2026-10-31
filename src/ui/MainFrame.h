#pragma once

#include <wx/frame.h>

namespace icd {

    // Top-level window. Panels, menus and the drive map are added in later tasks.
    class MainFrame : public wxFrame {
    public:
        MainFrame();
    };

} // namespace icd
