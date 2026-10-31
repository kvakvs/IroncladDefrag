#pragma once

#include "core/map/World.h"

#include <chrono>

namespace icd::core {

    // A point or offset in window pixels.
    struct PixelPoint {
        double x = 0.0; // Right.
        double y = 0.0; // Down.
    };

    // A point in world cells (fractional while zooming).
    struct WorldPoint {
        double x = 0.0; // Column.
        double y = 0.0; // Row.
    };

    // What part of the world a map window shows: a continuous scale and the world point at the window's
    // center. Pure data, so navigation is tested without a window (plan.md 7.3-7.4).
    struct Viewport {
        double scale = 1.0; // World cells per window pixel; below 1 a cell spans several pixels.
        WorldPoint center; // World point at the middle of the window.
        PixelPoint size{1.0, 1.0}; // Window size in pixels.

        // The world point under a window pixel.
        [[nodiscard]] WorldPoint worldAt(PixelPoint pixel) const;

        // The window pixel showing a world point.
        [[nodiscard]] PixelPoint pixelAt(WorldPoint point) const;
    };

    // How far a view may zoom and move for one world and window.
    struct ViewLimits {
        double minScale = 1.0 / 32.0; // Deepest zoom: 32 px per cluster (plan.md 7.1).
        double maxScale = 1.0; // Widest zoom: a bit more than the whole volume.
        WorldPoint worldSize; // World width and height in cells.
    };

    // Limits for a world shown in a window of size pixels.
    [[nodiscard]] ViewLimits limitsFor(const World& world, PixelPoint size);

    // The view that shows the whole volume with a small margin.
    [[nodiscard]] Viewport fitView(const World& world, PixelPoint size);

    // Keeps scale within the limits and the center inside the world.
    [[nodiscard]] Viewport clampView(Viewport view, const ViewLimits& limits);

    // The view zoomed so the world point under anchor stays under it; scale ends up as targetScale (clamped).
    [[nodiscard]] Viewport zoomedTo(const Viewport& view, double targetScale, PixelPoint anchor,
                                    const ViewLimits& limits);

    // The view moved so the world follows the pointer by delta pixels (dragging right shows what is left).
    [[nodiscard]] Viewport panned(const Viewport& view, PixelPoint delta, const ViewLimits& limits);

    // Clock for animations; steady, and fine-grained on Windows (QueryPerformanceCounter).
    using AnimationClock = std::chrono::steady_clock;

    // A zoom that eases from one scale to another over a duration while keeping an anchor pixel fixed. Its
    // position comes from elapsed time, not from timer ticks, so a jittery timer does not make it jerky.
    struct ZoomMotion {
        Viewport from; // View when the zoom started.
        double targetScale = 1.0; // Scale at the end.
        PixelPoint anchor; // Window pixel that stays on the same world point.
        AnimationClock::time_point start; // When it started.
        std::chrono::duration<double> duration{0.15}; // How long it takes (plan.md 7.3: about 150 ms per step).

        // The view at a time (the end view once the duration has passed).
        [[nodiscard]] Viewport at(AnimationClock::time_point now, const ViewLimits& limits) const;

        // Whether the motion has finished at a time.
        [[nodiscard]] bool finished(AnimationClock::time_point now) const { return now - start >= duration; }
    };

    // Duration of a zoom by a factor: 150 ms for a wheel step, longer for big jumps, at most 1.2 s.
    [[nodiscard]] std::chrono::duration<double> zoomDuration(double fromScale, double toScale);

    // Pan inertia after a drag: the view keeps moving at the release velocity and slows down exponentially.
    struct PanInertia {
        Viewport from; // View at release.
        PixelPoint velocity; // Pixels per second at release.
        AnimationClock::time_point start; // Release time.

        // Velocity decay rate per second (half the speed every ~0.17 s).
        static constexpr double decay = 4.0;

        // Below this speed (pixels per second) the motion stops.
        static constexpr double stopSpeed = 20.0;

        // The view at a time.
        [[nodiscard]] Viewport at(AnimationClock::time_point now, const ViewLimits& limits) const;

        // Whether the motion has slowed below stopSpeed at a time.
        [[nodiscard]] bool finished(AnimationClock::time_point now) const;
    };

} // namespace icd::core
