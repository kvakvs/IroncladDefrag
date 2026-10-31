#pragma once

#include <wx/frame.h>

namespace icd {

    // Top-level window. Panels, menus and the drive map are added in later tasks.
    class MainFrame : public wxFrame {
    public:
        // Creates the window with the product title and a DPI-scaled default size.
        MainFrame();
    };

} // namespace icd
