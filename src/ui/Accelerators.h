#pragma once

#include <cstdint>
#include <optional>
#include <span>

class wxKeyEvent;
class wxMenuBar;

namespace icd::ui {

    // Keyboard shortcut of one menu command. All menu shortcuts live in one table, so they are easy to review
    // for clashes and the menu shows exactly what works.
    struct Shortcut {
        int id = 0; // Command id.
        int flags = 0; // wxACCEL_CTRL, wxACCEL_ALT, wxACCEL_SHIFT or wxACCEL_NORMAL.
        int keyCode = 0; // wx key code ('R', WXK_F5, ...).
    };

    // The menu shortcuts.
    [[nodiscard]] std::span<const Shortcut> menuShortcuts();

    // Puts the shortcuts on their menu items; wxMSW builds the frame's accelerator table from them and
    // ignores the shortcut while the item is disabled, so the workflow's enable rules apply to keys too.
    void applyMenuShortcuts(wxMenuBar& bar);

    // Map keys from plan.md 7.4. They act only while the map has focus, so plain keys like + and 0 still
    // type into text fields elsewhere. Panning keys are reserved for the map engine (Phase 2).
    enum class MapKey : std::uint8_t {
        ZoomIn, // + : zoom in at the centre.
        ZoomOut, // - : zoom out.
        PanLeft, // Left arrow.
        PanRight, // Right arrow.
        PanUp, // Up arrow.
        PanDown, // Down arrow.
        PageUp, // PgUp: one screen back.
        PageDown, // PgDn: one screen forward.
        Start, // Home: start of the volume.
        End, // End: end of the volume.
        Fit, // 0 : whole volume.
        OneClusterPerPixel, // 1 : one cluster per pixel.
    };

    // The map key a character event stands for, if it is one (Ctrl and Alt combinations never are).
    [[nodiscard]] std::optional<MapKey> mapKeyFor(const wxKeyEvent& event);

    // The menu command a map key shares (zoom keys and the View menu), if any.
    [[nodiscard]] std::optional<int> commandForMapKey(MapKey key);

} // namespace icd::ui
