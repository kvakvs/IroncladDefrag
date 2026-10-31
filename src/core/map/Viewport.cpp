#include "core/map/Viewport.h"

#include <algorithm>
#include <cmath>

namespace icd::core {

    namespace {

        // Margin around the volume in the fitted view, and how much further out the user may zoom.
        constexpr double fitMargin = 1.04;
        constexpr double zoomOutBeyondFit = 1.5;

        // Ease-out cubic: fast start, gentle stop.
        double easeOut(double progress) {
            const double rest = 1.0 - std::clamp(progress, 0.0, 1.0);
            return 1.0 - rest * rest * rest;
        }

        // Seconds between two times.
        double secondsBetween(AnimationClock::time_point from, AnimationClock::time_point to) {
            return std::max(0.0, std::chrono::duration<double>(to - from).count());
        }

    } // namespace

    WorldPoint Viewport::worldAt(PixelPoint pixel) const {
        return {center.x + (pixel.x - size.x / 2.0) * scale, center.y + (pixel.y - size.y / 2.0) * scale};
    }

    PixelPoint Viewport::pixelAt(WorldPoint point) const {
        return {(point.x - center.x) / scale + size.x / 2.0, (point.y - center.y) / scale + size.y / 2.0};
    }

    ViewLimits limitsFor(const World& world, PixelPoint size) {
        ViewLimits limits;
        limits.worldSize = {static_cast<double>(world.rowWidth()), static_cast<double>(world.rows())};
        const double fit = fitMargin *
            std::max(limits.worldSize.x / std::max(size.x, 1.0), limits.worldSize.y / std::max(size.y, 1.0));
        limits.maxScale = std::max(limits.minScale, fit * zoomOutBeyondFit);
        return limits;
    }

    Viewport fitView(const World& world, PixelPoint size) {
        Viewport view;
        view.size = size;
        view.center = {static_cast<double>(world.rowWidth()) / 2.0, static_cast<double>(world.rows()) / 2.0};
        view.scale = fitMargin *
            std::max(view.center.x * 2.0 / std::max(size.x, 1.0), view.center.y * 2.0 / std::max(size.y, 1.0));
        return view;
    }

    Viewport clampView(Viewport view, const ViewLimits& limits) {
        view.scale = std::clamp(view.scale, limits.minScale, limits.maxScale);
        view.center.x = std::clamp(view.center.x, 0.0, limits.worldSize.x);
        view.center.y = std::clamp(view.center.y, 0.0, limits.worldSize.y);
        return view;
    }

    Viewport zoomedTo(const Viewport& view, double targetScale, PixelPoint anchor, const ViewLimits& limits) {
        const WorldPoint fixed = view.worldAt(anchor);
        Viewport out = view;
        out.scale = std::clamp(targetScale, limits.minScale, limits.maxScale);
        // Put the center where it must be for fixed to stay under anchor.
        out.center = {fixed.x - (anchor.x - view.size.x / 2.0) * out.scale,
                      fixed.y - (anchor.y - view.size.y / 2.0) * out.scale};
        return clampView(out, limits);
    }

    Viewport panned(const Viewport& view, PixelPoint delta, const ViewLimits& limits) {
        Viewport out = view;
        out.center = {view.center.x - delta.x * view.scale, view.center.y - delta.y * view.scale};
        return clampView(out, limits);
    }

    Viewport ZoomMotion::at(AnimationClock::time_point now, const ViewLimits& limits) const {
        const double total = std::max(duration.count(), 1e-6);
        const double progress = easeOut(secondsBetween(start, now) / total);
        // Interpolate in log space, so every step of the zoom feels the same.
        const double scale = std::exp(std::log(from.scale) + (std::log(targetScale) - std::log(from.scale)) * progress);
        return zoomedTo(from, scale, anchor, limits);
    }

    std::chrono::duration<double> zoomDuration(double fromScale, double toScale) {
        const double steps = std::abs(std::log2(toScale / fromScale)); // One wheel step is half a level.
        return std::chrono::duration<double>(std::clamp(0.15 * std::max(1.0, steps), 0.15, 1.2));
    }

    Viewport PanInertia::at(AnimationClock::time_point now, const ViewLimits& limits) const {
        const double elapsed = secondsBetween(start, now);
        // Distance travelled with speed v0 * e^(-decay t).
        const double travelled = (1.0 - std::exp(-decay * elapsed)) / decay;
        return panned(from, {velocity.x * travelled, velocity.y * travelled}, limits);
    }

    bool PanInertia::finished(AnimationClock::time_point now) const {
        const double speed = std::hypot(velocity.x, velocity.y) * std::exp(-decay * secondsBetween(start, now));
        return speed < stopSpeed;
    }

} // namespace icd::core
