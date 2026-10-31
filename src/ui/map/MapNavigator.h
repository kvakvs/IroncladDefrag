#pragma once

#include "core/map/Viewport.h"
#include "ui/Accelerators.h"

#include <wx/timer.h>

#include <optional>

class wxMouseEvent;
class wxPanGestureEvent;
class wxZoomGestureEvent;
class wxTimerEvent;

namespace icd::ui {

    class MapCanvas;

    // Turns input into view changes for the map (plan.md 7.3-7.4): drag to pan with inertia, wheel and
    // pinch to zoom at the pointer, double-click to zoom in (Shift: out), map keys, and the View menu's zoom
    // commands. Zooms animate by elapsed time from a fast timer that only computes the view and asks for a
    // repaint; the Direct2D present paces frames to the display.
    class MapNavigator {
    public:
        // Binds input events on canvas, which owns the navigator.
        explicit MapNavigator(MapCanvas& canvas);

        // Zooms by factor (above 1 zooms in) around a window pixel, animated; compounds with a running zoom.
        void zoomBy(double factor, core::PixelPoint anchor);

        // Animates to a scale around the window center.
        void zoomToScale(double scale);

        // Animates to the view that shows the whole volume.
        void fit();

        // Moves by a map key (arrows: an eighth of the window, PgUp/PgDn: a window, Home/End: the volume's ends).
        void panKey(MapKey key);

        // Whether a zoom or inertia motion is running.
        [[nodiscard]] bool animating() const { return zoom_.has_value() || inertia_.has_value(); }

        // Stops any motion (new map, drag start).
        void stop();

    private:
        // Animates from the current view to target, keeping fixed the pixel that shows the same world point in
        // both views (a zoom with a pan folded in).
        void animateTo(const core::Viewport& target);

        // Starts the frame timer and frame statistics if idle.
        void startMotion();

        // Advances the running motion.
        void onTimer(wxTimerEvent& event);

        // Wheel: vertical zooms at the pointer, horizontal pans.
        void onWheel(wxMouseEvent& event);

        // Drag start.
        void onLeftDown(wxMouseEvent& event);

        // Drag move.
        void onMotion(wxMouseEvent& event);

        // Drag end; starts inertia when the pointer was still moving.
        void onLeftUp(wxMouseEvent& event);

        // Double-click zooms in four times at the pointer, Shift+double-click zooms out.
        void onDoubleClick(wxMouseEvent& event);

        // Touchpad pinch.
        void onGestureZoom(wxZoomGestureEvent& event);

        // Touchpad two-finger pan.
        void onGesturePan(wxPanGestureEvent& event);

        MapCanvas& canvas_; // The map being navigated.
        wxTimer timer_; // Frame clock while moving.
        std::optional<core::ZoomMotion> zoom_; // Running zoom.
        std::optional<core::PanInertia> inertia_; // Running inertia.
        bool dragging_ = false; // Left button held on the map.
        core::PixelPoint lastPointer_; // Pointer position at the last drag event.
        core::PixelPoint velocity_; // Smoothed drag velocity in pixels per second.
        core::AnimationClock::time_point lastMove_; // Time of the last drag event.
        double gestureStartScale_ = 1.0; // Scale when the current pinch started.
    };

} // namespace icd::ui
