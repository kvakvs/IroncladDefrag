#include "ui/Accelerators.h"

#include "ui/Commands.h"

#include <wx/accel.h>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/menu.h>

#include <algorithm>
#include <array>

namespace icd::ui {

    namespace {

        constexpr std::array shortcuts{
            Shortcut{ID_ANALYZE, wxACCEL_NORMAL, WXK_F5, "Analyze the selected drive"},
            Shortcut{ID_QUICK, wxACCEL_SHIFT, WXK_F5, "Analyze and build a Fast plan, stopping at review"},
            Shortcut{ID_REFRESH_DRIVES, wxACCEL_CTRL, 'R', "Refresh the drive list"},
            Shortcut{ID_TRIM, wxACCEL_CTRL, 'T', "TRIM the selected SSD"},
            Shortcut{ID_CHOOSE_STRATEGY, wxACCEL_NORMAL, WXK_F6, "Choose the strategy"},
            Shortcut{ID_BUILD_PLAN, wxACCEL_NORMAL, WXK_F7, "Build the plan"},
            Shortcut{ID_REVIEW_PLAN, wxACCEL_NORMAL, WXK_F8, "Review the plan on the map"},
            Shortcut{ID_EXECUTE, wxACCEL_NORMAL, WXK_F9, "Execute the plan (asks first)"},
            Shortcut{ID_DEFRAG_FILE, wxACCEL_CTRL, 'D', "Defragment the selected file (asks first)"},
            Shortcut{ID_PAUSE, wxACCEL_NORMAL, WXK_PAUSE, "Pause or resume execution"},
            Shortcut{ID_CANCEL, wxACCEL_NORMAL, WXK_ESCAPE, "Cancel the running job"},
            Shortcut{ID_COLOR_CLASS, wxACCEL_ALT, '1', "Color the map by file class"},
            Shortcut{ID_COLOR_FRAGMENTATION, wxACCEL_ALT, '2', "Color the map by fragmentation"},
            Shortcut{ID_COLOR_AGE, wxACCEL_ALT, '3', "Color the map by file age"},
            Shortcut{ID_COLOR_SIZE, wxACCEL_ALT, '4', "Color the map by file size"},
            Shortcut{ID_COLOR_ZONE_FIT, wxACCEL_ALT, '5', "Color the map by zone fit"},
            Shortcut{ID_COLOR_PLAN, wxACCEL_ALT, '6', "Color the map by the planned moves"},
            Shortcut{ID_SHOW_MINIMAP, wxACCEL_CTRL, 'M', "Show or hide the minimap"},
            Shortcut{ID_SHOW_LEGEND, wxACCEL_CTRL, 'L', "Show the map colors tab (color mode and legend)"},
            Shortcut{ID_ZOOM_IN, wxACCEL_CTRL, '+', "Zoom in"},
            Shortcut{ID_ZOOM_OUT, wxACCEL_CTRL, '-', "Zoom out"},
            Shortcut{ID_ZOOM_FIT, wxACCEL_CTRL, '0', "Fit the whole volume"},
            Shortcut{ID_ZOOM_CLUSTER, wxACCEL_CTRL, '1', "One cluster per pixel"},
            Shortcut{ID_GOTO_FILE, wxACCEL_CTRL, 'F', "Find a file and show it on the map"},
            Shortcut{ID_NEXT_FRAGMENT, wxACCEL_NORMAL, WXK_F3, "Next fragment of the selected file"},
            Shortcut{ID_PREVIOUS_FRAGMENT, wxACCEL_SHIFT, WXK_F3, "Previous fragment of the selected file"},
            Shortcut{wxID_PREFERENCES, wxACCEL_CTRL, ',', "Open the preferences"},
            Shortcut{ID_SHORTCUTS, wxACCEL_NORMAL, WXK_F1, "List the keyboard shortcuts"},
        };

        constexpr std::array mapKeys{
            MapKeyBinding{'+', MapKey::ZoomIn, "Zoom in"},
            MapKeyBinding{WXK_NUMPAD_ADD, MapKey::ZoomIn, "Zoom in"},
            MapKeyBinding{'-', MapKey::ZoomOut, "Zoom out"},
            MapKeyBinding{WXK_NUMPAD_SUBTRACT, MapKey::ZoomOut, "Zoom out"},
            MapKeyBinding{WXK_LEFT, MapKey::PanLeft, "Pan left"},
            MapKeyBinding{WXK_RIGHT, MapKey::PanRight, "Pan right"},
            MapKeyBinding{WXK_UP, MapKey::PanUp, "Pan up"},
            MapKeyBinding{WXK_DOWN, MapKey::PanDown, "Pan down"},
            MapKeyBinding{WXK_PAGEUP, MapKey::PageUp, "One screen back"},
            MapKeyBinding{WXK_PAGEDOWN, MapKey::PageDown, "One screen forward"},
            MapKeyBinding{WXK_HOME, MapKey::Start, "Start of the volume"},
            MapKeyBinding{WXK_END, MapKey::End, "End of the volume"},
            MapKeyBinding{'0', MapKey::Fit, "Fit the whole volume"},
            MapKeyBinding{WXK_NUMPAD0, MapKey::Fit, "Fit the whole volume"},
            MapKeyBinding{'1', MapKey::OneClusterPerPixel, "One cluster per pixel"},
            MapKeyBinding{WXK_NUMPAD1, MapKey::OneClusterPerPixel, "One cluster per pixel"},
        };

        // A numpad key the map uses and its name; wx writes them as "KP_ADD".
        struct NumpadName {
            int keyCode = 0; // wx key code.
            const char* name = ""; // How the shortcut list writes it.
        };

        constexpr std::array numpadNames{
            NumpadName{WXK_NUMPAD_ADD, "Num +"},
            NumpadName{WXK_NUMPAD_SUBTRACT, "Num -"},
            NumpadName{WXK_NUMPAD0, "Num 0"},
            NumpadName{WXK_NUMPAD1, "Num 1"},
        };

    } // namespace

    std::span<const Shortcut> menuShortcuts() { return shortcuts; }

    std::optional<Shortcut> shortcutFor(int id) {
        const auto found = std::ranges::find(shortcuts, id, &Shortcut::id);
        return found == shortcuts.end() ? std::nullopt : std::optional<Shortcut>(*found);
    }

    wxString shortcutText(const Shortcut& shortcut) {
        return wxAcceleratorEntry(shortcut.flags, shortcut.keyCode, shortcut.id).ToString();
    }

    wxString withShortcut(int id, const wxString& label) {
        const auto shortcut = shortcutFor(id);
        return shortcut ? label + "\t" + shortcutText(*shortcut) : label;
    }

    wxString commandTip(int id) {
        const auto shortcut = shortcutFor(id);
        if (!shortcut) {
            return {};
        }
        return wxString::FromUTF8(shortcut->description) + " (" + shortcutText(*shortcut) + ")";
    }

    std::span<const MapKeyBinding> mapKeyBindings() { return mapKeys; }

    wxString mapKeyText(int keyCode) {
        const auto numpad = std::ranges::find(numpadNames, keyCode, &NumpadName::keyCode);
        if (numpad != numpadNames.end()) {
            return numpad->name;
        }
        return wxAcceleratorEntry(wxACCEL_NORMAL, keyCode).ToString();
    }

    void applyMenuShortcuts(wxMenuBar& bar) {
        for (const Shortcut& shortcut : shortcuts) {
            if (wxMenuItem* item = bar.FindItem(shortcut.id); item != nullptr) {
                wxAcceleratorEntry entry(shortcut.flags, shortcut.keyCode, shortcut.id);
                item->SetAccel(&entry); // Copies the entry and adds "\tCtrl+R" to the label.
            }
        }
    }

    std::optional<MapKey> mapKeyFor(const wxKeyEvent& event) {
        if ((event.GetModifiers() & (wxMOD_CONTROL | wxMOD_ALT)) != 0) {
            return std::nullopt; // Ctrl and Alt combinations belong to the menu.
        }
        for (const MapKeyBinding& binding : mapKeys) {
            if (binding.keyCode == event.GetKeyCode()) {
                return binding.key;
            }
        }
        return std::nullopt;
    }

    std::optional<int> commandForMapKey(MapKey key) {
        switch (key) {
        case MapKey::ZoomIn:
            return ID_ZOOM_IN;
        case MapKey::ZoomOut:
            return ID_ZOOM_OUT;
        case MapKey::Fit:
            return ID_ZOOM_FIT;
        case MapKey::OneClusterPerPixel:
            return ID_ZOOM_CLUSTER;
        default:
            return std::nullopt; // Panning has no menu command; the map engine handles it.
        }
    }

} // namespace icd::ui
