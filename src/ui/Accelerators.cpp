#include "ui/Accelerators.h"

#include "ui/Commands.h"

#include <wx/accel.h>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/menu.h>

#include <array>

namespace icd::ui {

    namespace {

        constexpr std::array shortcuts{
            Shortcut{ID_ANALYZE, wxACCEL_NORMAL, WXK_F5},
            Shortcut{ID_QUICK, wxACCEL_SHIFT, WXK_F5},
            Shortcut{ID_REFRESH_DRIVES, wxACCEL_CTRL, 'R'},
            Shortcut{ID_TRIM, wxACCEL_CTRL, 'T'},
            Shortcut{ID_CHOOSE_STRATEGY, wxACCEL_NORMAL, WXK_F6},
            Shortcut{ID_BUILD_PLAN, wxACCEL_NORMAL, WXK_F7},
            Shortcut{ID_REVIEW_PLAN, wxACCEL_NORMAL, WXK_F8},
            Shortcut{ID_EXECUTE, wxACCEL_NORMAL, WXK_F9}, // Always asks for confirmation.
            Shortcut{ID_PAUSE, wxACCEL_NORMAL, WXK_PAUSE},
            Shortcut{ID_CANCEL, wxACCEL_NORMAL, WXK_ESCAPE},
            Shortcut{ID_COLOR_CLASS, wxACCEL_ALT, '1'},
            Shortcut{ID_COLOR_FRAGMENTATION, wxACCEL_ALT, '2'},
            Shortcut{ID_COLOR_AGE, wxACCEL_ALT, '3'},
            Shortcut{ID_COLOR_SIZE, wxACCEL_ALT, '4'},
            Shortcut{ID_COLOR_ZONE_FIT, wxACCEL_ALT, '5'},
            Shortcut{ID_COLOR_PLAN, wxACCEL_ALT, '6'},
            Shortcut{ID_SHOW_MINIMAP, wxACCEL_CTRL, 'M'},
            Shortcut{ID_SHOW_LEGEND, wxACCEL_CTRL, 'L'},
            Shortcut{ID_ZOOM_IN, wxACCEL_CTRL, '+'},
            Shortcut{ID_ZOOM_OUT, wxACCEL_CTRL, '-'},
            Shortcut{ID_ZOOM_FIT, wxACCEL_CTRL, '0'},
            Shortcut{ID_ZOOM_CLUSTER, wxACCEL_CTRL, '1'},
            Shortcut{wxID_PREFERENCES, wxACCEL_CTRL, ','},
        };

        // A map key and the character codes that produce it.
        struct MapKeyBinding {
            int keyCode = 0; // wx key code from a character event.
            MapKey key = MapKey::Fit; // What it does on the map.
        };

        constexpr std::array mapKeys{
            MapKeyBinding{'+', MapKey::ZoomIn},
            MapKeyBinding{WXK_NUMPAD_ADD, MapKey::ZoomIn},
            MapKeyBinding{'-', MapKey::ZoomOut},
            MapKeyBinding{WXK_NUMPAD_SUBTRACT, MapKey::ZoomOut},
            MapKeyBinding{WXK_LEFT, MapKey::PanLeft},
            MapKeyBinding{WXK_RIGHT, MapKey::PanRight},
            MapKeyBinding{WXK_UP, MapKey::PanUp},
            MapKeyBinding{WXK_DOWN, MapKey::PanDown},
            MapKeyBinding{WXK_PAGEUP, MapKey::PageUp},
            MapKeyBinding{WXK_PAGEDOWN, MapKey::PageDown},
            MapKeyBinding{WXK_HOME, MapKey::Start},
            MapKeyBinding{WXK_END, MapKey::End},
            MapKeyBinding{'0', MapKey::Fit},
            MapKeyBinding{WXK_NUMPAD0, MapKey::Fit},
            MapKeyBinding{'1', MapKey::OneClusterPerPixel},
            MapKeyBinding{WXK_NUMPAD1, MapKey::OneClusterPerPixel},
        };

    } // namespace

    std::span<const Shortcut> menuShortcuts() { return shortcuts; }

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
