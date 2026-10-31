#include "core/map/Viewport.h"

#include <gtest/gtest.h>

namespace icd::core { namespace {

    using namespace std::chrono_literals;

    // A 30 TB world in a 1600 x 900 window.
    struct MapScene {
        World world{7'300'000'000, {.width = 16, .height = 9}}; // The world.
        PixelPoint size{1600, 900}; // Window.
        ViewLimits limits = limitsFor(world, size); // Its limits.
        Viewport fitted = fitView(world, size); // Whole volume.
    };

    // Pixel and world coordinates convert both ways.
    TEST(Viewport, PixelWorldRoundTrip) {
        const Viewport view{.scale = 3.0, .center = {1000, 500}, .size = {800, 600}};
        const WorldPoint point = view.worldAt({100, 50});
        EXPECT_DOUBLE_EQ(point.x, 1000 + (100 - 400) * 3.0);
        const PixelPoint back = view.pixelAt(point);
        EXPECT_DOUBLE_EQ(back.x, 100);
        EXPECT_DOUBLE_EQ(back.y, 50);
    }

    // The fitted view shows the whole world; the limits allow 32 px per cluster up to beyond the fit.
    TEST(Viewport, FitAndLimits) {
        const MapScene s;
        const WorldPoint topLeft = s.fitted.worldAt({0, 0});
        const WorldPoint bottomRight = s.fitted.worldAt(s.size);
        EXPECT_LE(topLeft.x, 0.0);
        EXPECT_LE(topLeft.y, 0.0);
        EXPECT_GE(bottomRight.x, static_cast<double>(s.world.rowWidth()));
        EXPECT_GE(bottomRight.y, static_cast<double>(s.world.rows()));
        EXPECT_DOUBLE_EQ(s.limits.minScale, 1.0 / 32.0);
        EXPECT_GT(s.limits.maxScale, s.fitted.scale);
    }

    // Zooming keeps the world point under the anchor in place, and the scale stays within the limits.
    TEST(Viewport, ZoomKeepsAnchor) {
        const MapScene s;
        const PixelPoint anchor{400, 300};
        const WorldPoint before = s.fitted.worldAt(anchor);
        const Viewport zoomed = zoomedTo(s.fitted, s.fitted.scale / 8, anchor, s.limits);
        const WorldPoint after = zoomed.worldAt(anchor);
        EXPECT_NEAR(after.x, before.x, 1e-6 * before.x);
        EXPECT_NEAR(after.y, before.y, 1e-6 * before.y);
        EXPECT_DOUBLE_EQ(zoomedTo(s.fitted, 1e-9, anchor, s.limits).scale, s.limits.minScale);
        EXPECT_DOUBLE_EQ(zoomedTo(s.fitted, 1e12, anchor, s.limits).scale, s.limits.maxScale);
    }

    // Dragging moves the world with the pointer; the center never leaves the world.
    TEST(Viewport, PanFollowsPointerAndClamps) {
        const MapScene s;
        const Viewport deep = zoomedTo(s.fitted, 1.0, {800, 450}, s.limits);
        const Viewport moved = panned(deep, {100, 0}, s.limits);
        EXPECT_DOUBLE_EQ(moved.center.x, deep.center.x - 100 * deep.scale); // Content moved right.
        const Viewport far = panned(deep, {1e12, -1e12}, s.limits);
        EXPECT_DOUBLE_EQ(far.center.x, 0.0);
        EXPECT_DOUBLE_EQ(far.center.y, static_cast<double>(s.world.rows()));
    }

    // A zoom motion eases from start to target, monotonically, with the anchor fixed throughout.
    TEST(Viewport, ZoomMotionIsTimeBased) {
        const MapScene s;
        const auto start = AnimationClock::time_point{} + 10s;
        const ZoomMotion motion{.from = s.fitted,
                                .targetScale = 1.0,
                                .anchor = {800, 450},
                                .start = start,
                                .duration = zoomDuration(s.fitted.scale, 1.0)};
        EXPECT_DOUBLE_EQ(motion.at(start, s.limits).scale, s.fitted.scale);
        EXPECT_DOUBLE_EQ(motion.at(start + 5s, s.limits).scale, 1.0);
        EXPECT_TRUE(motion.finished(start + 5s));
        EXPECT_FALSE(motion.finished(start + 10ms));
        double previous = s.fitted.scale;
        const WorldPoint anchored = s.fitted.worldAt({800, 450});
        for (int ms = 10; ms <= 1500; ms += 10) {
            const Viewport view = motion.at(start + std::chrono::milliseconds(ms), s.limits);
            EXPECT_LE(view.scale, previous);
            EXPECT_NEAR(view.worldAt({800, 450}).x, anchored.x, 1e-3);
            previous = view.scale;
        }
    }

    // Zoom durations: 150 ms per wheel step, longer for big jumps, capped.
    TEST(Viewport, ZoomDurations) {
        EXPECT_DOUBLE_EQ(zoomDuration(1.0, 0.7071).count(), 0.15);
        EXPECT_NEAR(zoomDuration(128.0, 1.0).count(), 1.05, 1e-9);
        EXPECT_DOUBLE_EQ(zoomDuration(1.0, 1e9).count(), 1.2);
    }

    // Inertia keeps moving in the release direction, slows down and stops.
    TEST(Viewport, PanInertiaSlowsAndStops) {
        const MapScene s;
        const Viewport deep = zoomedTo(s.fitted, 1.0, {800, 450}, s.limits);
        const auto start = AnimationClock::time_point{} + 1s;
        const PanInertia inertia{.from = deep, .velocity = {1000, 0}, .start = start};
        const double early = inertia.at(start + 100ms, s.limits).center.x;
        const double late = inertia.at(start + 2s, s.limits).center.x;
        const double later = inertia.at(start + 10s, s.limits).center.x;
        EXPECT_LT(early, deep.center.x);
        EXPECT_LT(late, early);
        EXPECT_NEAR(later, deep.center.x - 1000.0 / PanInertia::decay * deep.scale, 1e-6);
        EXPECT_FALSE(inertia.finished(start + 100ms));
        EXPECT_TRUE(inertia.finished(start + 2s));
    }

}} // namespace icd::core
