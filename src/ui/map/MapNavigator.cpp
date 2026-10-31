#include "ui/map/MapNavigator.h"

#include "ui/map/MapCanvas.h"

#include <wx/cursor.h>
#include <wx/event.h>

#include <algorithm>
#include <cmath>

namespace icd::ui {

    namespace {

        // Frame clock interval while moving; the present waits for the display, so this only needs to be
        // shorter than a frame.
        constexpr int timerMilliseconds = 8;

        // Zoom per wheel notch: half a level (plan.md 7.3).
        constexpr double wheelStep = 1.41421356237;

        // Weight of the newest sample in the smoothed drag velocity.
        constexpr double velocitySmoothing = 0.6;

        // A drag that paused longer than this before release gets no inertia.
        constexpr double releaseWindowSeconds = 0.06;

        // A window pixel from a mouse event.
        core::PixelPoint pointerOf(const wxMouseEvent& event) {
            return {static_cast<double>(event.GetX()), static_cast<double>(event.GetY())};
        }

        // Seconds between two times.
        double seconds(core::AnimationClock::time_point from, core::AnimationClock::time_point to) {
            return std::chrono::duration<double>(to - from).count();
        }

    } // namespace

    MapNavigator::MapNavigator(MapCanvas& canvas) : canvas_(canvas), timer_(&canvas) {
        canvas_.Bind(wxEVT_TIMER, &MapNavigator::onTimer, this, timer_.GetId());
        canvas_.Bind(wxEVT_MOUSEWHEEL, &MapNavigator::onWheel, this);
        canvas_.Bind(wxEVT_LEFT_DOWN, &MapNavigator::onLeftDown, this);
        canvas_.Bind(wxEVT_MOTION, &MapNavigator::onMotion, this);
        canvas_.Bind(wxEVT_LEFT_UP, &MapNavigator::onLeftUp, this);
        canvas_.Bind(wxEVT_LEFT_DCLICK, &MapNavigator::onDoubleClick, this);
        canvas_.Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
            dragging_ = false;
            canvas_.minimap().endDrag();
        });
        canvas_.EnableTouchEvents(wxTOUCH_ZOOM_GESTURE | wxTOUCH_PAN_GESTURES);
        canvas_.Bind(wxEVT_GESTURE_ZOOM, &MapNavigator::onGestureZoom, this);
        canvas_.Bind(wxEVT_GESTURE_PAN, &MapNavigator::onGesturePan, this);
    }

    void MapNavigator::stop() {
        zoom_.reset();
        inertia_.reset();
        if (timer_.IsRunning()) {
            timer_.Stop();
            canvas_.endFrameStats();
        }
    }

    void MapNavigator::startMotion() {
        if (!timer_.IsRunning()) {
            canvas_.beginFrameStats();
            timer_.Start(timerMilliseconds);
        }
    }

    void MapNavigator::zoomBy(double factor, core::PixelPoint anchor) {
        if (!canvas_.hasMap() || factor <= 0.0) {
            return;
        }
        const auto now = core::AnimationClock::now();
        const core::ViewLimits limits = canvas_.limits();
        // A zoom already running continues from where it is now towards the combined target.
        const double target = (zoom_ ? zoom_->targetScale : canvas_.viewport().scale) / factor;
        const core::Viewport current = canvas_.viewport();
        inertia_.reset();
        const double clamped = std::clamp(target, limits.minScale, limits.maxScale);
        zoom_ = core::ZoomMotion{.from = current,
                                 .targetScale = clamped,
                                 .anchor = anchor,
                                 .start = now,
                                 .duration = core::zoomDuration(current.scale, clamped)};
        startMotion();
    }

    void MapNavigator::zoomToScale(double scale) {
        const core::Viewport current = canvas_.viewport();
        zoomBy(current.scale / scale, {current.size.x / 2.0, current.size.y / 2.0});
    }

    void MapNavigator::fit() {
        if (const auto target = canvas_.fittedView(); target) {
            animateTo(*target);
        }
    }

    void MapNavigator::animateTo(const core::Viewport& target) {
        const core::Viewport current = canvas_.viewport();
        const double scaleDifference = current.scale - target.scale;
        if (std::abs(scaleDifference) < 1e-12 * current.scale) {
            canvas_.setViewport(target); // Same scale: nothing to animate around.
            return;
        }
        // The pixel a where current.worldAt(a) == target.worldAt(a).
        const core::PixelPoint anchor{current.size.x / 2.0 + (target.center.x - current.center.x) / scaleDifference,
                                      current.size.y / 2.0 + (target.center.y - current.center.y) / scaleDifference};
        zoomBy(current.scale / target.scale, anchor);
    }

    void MapNavigator::panKey(MapKey key) {
        if (!canvas_.hasMap()) {
            return;
        }
        stop();
        const core::Viewport view = canvas_.viewport();
        const core::ViewLimits limits = canvas_.limits();
        const double stepX = view.size.x / 8.0;
        const double stepY = view.size.y / 8.0;
        core::Viewport next = view;
        switch (key) {
        case MapKey::PanLeft:
            next = core::panned(view, {stepX, 0}, limits);
            break;
        case MapKey::PanRight:
            next = core::panned(view, {-stepX, 0}, limits);
            break;
        case MapKey::PanUp:
            next = core::panned(view, {0, stepY}, limits);
            break;
        case MapKey::PanDown:
            next = core::panned(view, {0, -stepY}, limits);
            break;
        case MapKey::PageUp:
            next = core::panned(view, {0, view.size.y * 0.9}, limits);
            break;
        case MapKey::PageDown:
            next = core::panned(view, {0, -view.size.y * 0.9}, limits);
            break;
        case MapKey::Start:
            next.center = {0, 0};
            break;
        case MapKey::End:
            next.center = limits.worldSize;
            break;
        default:
            return; // Zoom keys come through the View commands.
        }
        canvas_.setViewport(core::clampView(next, limits));
    }

    void MapNavigator::onTimer(wxTimerEvent& /*event*/) {
        const auto now = core::AnimationClock::now();
        const core::ViewLimits limits = canvas_.limits();
        if (zoom_) {
            canvas_.setViewport(zoom_->at(now, limits));
            if (zoom_->finished(now)) {
                zoom_.reset();
            }
        }
        else if (inertia_) {
            canvas_.setViewport(inertia_->at(now, limits));
            if (inertia_->finished(now)) {
                inertia_.reset();
            }
        }
        if (!animating()) {
            stop();
        }
    }

    void MapNavigator::onWheel(wxMouseEvent& event) {
        const double notches = static_cast<double>(event.GetWheelRotation()) / std::max(1, event.GetWheelDelta());
        if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
            stop();
            canvas_.setViewport(core::panned(canvas_.viewport(), {-notches * 40.0, 0}, canvas_.limits()));
            return;
        }
        // Precision touchpads send fractional notches, which gives smooth zoom. Over the minimap the pointer is
        // not a place on the map, so the zoom keeps the window center.
        const core::Viewport& view = canvas_.viewport();
        const core::PixelPoint pointer = pointerOf(event);
        const bool overMinimap = canvas_.minimap().contains(pointer);
        zoomBy(std::pow(wheelStep, notches),
               overMinimap ? core::PixelPoint{view.size.x / 2.0, view.size.y / 2.0} : pointer);
    }

    void MapNavigator::onLeftDown(wxMouseEvent& event) {
        event.Skip(); // The canvas also takes focus on click.
        if (!canvas_.hasMap()) {
            return;
        }
        stop();
        if (canvas_.minimap().beginDrag(pointerOf(event), canvas_.viewport())) {
            canvas_.CaptureMouse();
            dragMinimap(pointerOf(event)); // A click beside the marker jumps there at once.
            return;
        }
        dragging_ = true;
        lastPointer_ = pointerOf(event);
        velocity_ = {};
        lastMove_ = core::AnimationClock::now();
        canvas_.CaptureMouse();
    }

    void MapNavigator::onMotion(wxMouseEvent& event) {
        if (canvas_.minimap().dragging()) {
            dragMinimap(pointerOf(event));
            return;
        }
        if (!dragging_) {
            updateCursor(pointerOf(event));
            event.Skip();
            return;
        }
        const core::PixelPoint pointer = pointerOf(event);
        const core::PixelPoint delta{pointer.x - lastPointer_.x, pointer.y - lastPointer_.y};
        const auto now = core::AnimationClock::now();
        const double elapsed = std::max(1e-3, seconds(lastMove_, now));
        velocity_ = {velocitySmoothing * delta.x / elapsed + (1.0 - velocitySmoothing) * velocity_.x,
                     velocitySmoothing * delta.y / elapsed + (1.0 - velocitySmoothing) * velocity_.y};
        lastPointer_ = pointer;
        lastMove_ = now;
        canvas_.setViewport(core::panned(canvas_.viewport(), delta, canvas_.limits()));
    }

    void MapNavigator::onLeftUp(wxMouseEvent& event) {
        event.Skip();
        if (canvas_.HasCapture()) {
            canvas_.ReleaseMouse();
        }
        canvas_.minimap().endDrag(); // Minimap drags stop dead: no inertia.
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        const auto now = core::AnimationClock::now();
        core::PanInertia inertia{.from = canvas_.viewport(), .velocity = velocity_, .start = now};
        if (seconds(lastMove_, now) < releaseWindowSeconds && !inertia.finished(now)) {
            inertia_ = inertia;
            startMotion();
        }
    }

    void MapNavigator::onDoubleClick(wxMouseEvent& event) {
        if (canvas_.minimap().contains(pointerOf(event))) {
            onLeftDown(event); // The second click of a double-click on the minimap is just another press.
            return;
        }
        zoomBy(event.ShiftDown() ? 0.25 : 4.0, pointerOf(event));
    }

    void MapNavigator::dragMinimap(core::PixelPoint pointer) {
        const core::MapData* map = canvas_.map();
        if (map != nullptr) {
            canvas_.setViewport(canvas_.minimap().dragTo(pointer, canvas_.viewport(), map->world(), canvas_.limits()));
        }
    }

    void MapNavigator::updateCursor(core::PixelPoint pointer) {
        const bool overMinimap = canvas_.minimap().contains(pointer);
        if (overMinimap != handCursor_) {
            handCursor_ = overMinimap;
            canvas_.SetCursor(overMinimap ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
        }
    }

    void MapNavigator::onGestureZoom(wxZoomGestureEvent& event) {
        if (!canvas_.hasMap()) {
            return;
        }
        if (event.IsGestureStart()) {
            stop();
            gestureStartScale_ = canvas_.viewport().scale;
        }
        const wxPoint position = event.GetPosition();
        const core::PixelPoint anchor{static_cast<double>(position.x), static_cast<double>(position.y)};
        canvas_.setViewport(
            core::zoomedTo(canvas_.viewport(), gestureStartScale_ / event.GetZoomFactor(), anchor, canvas_.limits()));
    }

    void MapNavigator::onGesturePan(wxPanGestureEvent& event) {
        if (!canvas_.hasMap()) {
            return;
        }
        const wxPoint delta = event.GetDelta();
        canvas_.setViewport(core::panned(
            canvas_.viewport(), {static_cast<double>(delta.x), static_cast<double>(delta.y)}, canvas_.limits()));
    }

} // namespace icd::ui
