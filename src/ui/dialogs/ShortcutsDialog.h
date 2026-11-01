#pragma once

#include <wx/dialog.h>
#include <wx/string.h>

#include <vector>

class wxMenuBar;

namespace icd::ui {

    // One line of the shortcut list: a group heading (keys empty) or a shortcut.
    struct ShortcutLine {
        wxString keys; // "Ctrl+R", "+ / Num +"; empty for a heading.
        wxString action; // What it does, or the heading text.
    };

    // The shortcut list, generated from the Accelerators tables: menu shortcuts grouped under the menu that holds
    // them (in menu order), then the map keys, keys of one action on one line.
    [[nodiscard]] std::vector<ShortcutLine> shortcutLines(const wxMenuBar& bar);

    // Help > Keyboard shortcuts (requested at Gate G1): makes every shortcut discoverable in one place, so the
    // list can never drift from what the keys do.
    class ShortcutsDialog : public wxDialog {
    public:
        // Lists the shortcuts of bar's menus and the map keys.
        ShortcutsDialog(wxWindow* parent, const wxMenuBar& bar);
    };

} // namespace icd::ui
