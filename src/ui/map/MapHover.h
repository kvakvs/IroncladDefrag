#pragma once

#include "core/map/Viewport.h"

#include <wx/string.h>
#include <wx/timer.h>

#include <chrono>

class wxKeyEvent;
class wxMouseEvent;
class wxPopupWindow;
class wxStaticText;
class wxTimerEvent;

namespace icd::ui {

    class MapCanvas;

    // The map's hover tooltip (plan.md 7.4): after the pointer rests briefly on the map, a popup next to it
    // says what lies there (core::hitTest / describeHit: cluster, byte offset, file, stream, fragment i/n,
    // size, type, times, or the free run). It then follows the pointer and updates as it moves, and hides on
    // a press, a drag, the wheel, a key or leaving the map. A wxPopupWindow, so it never takes focus.
    class MapHover {
    public:
        // Watches pointer events on canvas, which owns the hover; it handles them before the navigator and
        // always passes them on.
        explicit MapHover(MapCanvas& canvas);

        MapHover(const MapHover&) = delete;
        MapHover& operator=(const MapHover&) = delete;

        // Hides the tooltip and forgets a pending one (new map, view jumps).
        void hide();

    private:
        // Pointer moved: update a visible tooltip, or wait for the pointer to rest.
        void onMotion(wxMouseEvent& event);

        // The pointer rested: show the tooltip.
        void onTimer(wxTimerEvent& event);

        // Any press, wheel turn or leaving the map hides the tooltip.
        void onDismiss(wxMouseEvent& event);

        // A key hides the tooltip (the view may move).
        void onKey(wxKeyEvent& event);

        // Shows or updates the tooltip for a window pixel; hides it off the volume.
        void showAt(core::PixelPoint pointer);

        // Creates the popup on first use.
        void createPopup();

        // Moves the popup next to the pointer, flipped to stay on the pointer's screen.
        void placePopup(core::PixelPoint pointer);

        MapCanvas& canvas_; // The map described.
        wxTimer restTimer_; // Fires when the pointer has rested long enough.
        wxPopupWindow* popup_ = nullptr; // The tooltip; owned by wx (a child of the canvas), created lazily.
        wxStaticText* text_ = nullptr; // Its text; child of popup_.
        wxString shownText_; // Text on display, so an unchanged hit does not re-layout the popup.
        core::PixelPoint pointer_; // Last pointer position on the map.
        const std::chrono::time_zone* zone_ = nullptr; // Local time zone for file times; null shows UTC.
    };

} // namespace icd::ui
