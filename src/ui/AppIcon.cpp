#include "ui/AppIcon.h"

#include <wx/icon.h>

#include <array>

namespace icd::ui {

    namespace {

        // The sizes res/IroncladDefrag.ico holds (tools/make-icon.ps1).
        constexpr std::array iconSizes{16, 20, 24, 32, 48, 64, 256};

    } // namespace

    const wxIconBundle& appIcons() {
        static const wxIconBundle icons = [] {
            wxIconBundle bundle;
            for (const int size : iconSizes) {
                bundle.AddIcon(wxIcon("APPICON", wxBITMAP_TYPE_ICO_RESOURCE, size, size));
            }
            return bundle;
        }();
        return icons;
    }

} // namespace icd::ui
