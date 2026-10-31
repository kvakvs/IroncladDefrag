#include "core/map/Overview.h"

#include <gtest/gtest.h>

namespace icd::core { namespace {

    // A 30 TB world in a 1600 x 900 window, with its minimap.
    struct OverviewScene {
        World world{7'300'000'000, {.width = 16, .height = 9}}; // The world.
        PixelPoint size{1600, 900}; // Window.
        ViewLimits limits = limitsFor(world, size); // View limits.
        Viewport fitted = fitView(world, size); // Whole volume.
        Overview overview = placeOverview(world, size).value(); // The minimap.
    };

    // The minimap keeps the world's shape, its longest edge is maxEdge, and it sits in the bottom-right corner.
    TEST(Overview, Placement) {
        const OverviewScene s;
        const OverviewStyle style;
        const Overview& o = s.overview;
        EXPECT_DOUBLE_EQ(o.box.width, style.maxEdge); // The world is wider than high.
        EXPECT_NEAR(o.box.width / o.box.height,
                    static_cast<double>(s.world.rowWidth()) / static_cast<double>(s.world.rows()), 1e-9);
        EXPECT_DOUBLE_EQ(o.box.x + o.box.width, s.size.x - style.margin);
        EXPECT_DOUBLE_EQ(o.box.y + o.box.height, s.size.y - style.margin);
    }

    // A window too small to spare the room shows no minimap; an empty world neither.
    TEST(Overview, HiddenWhenTooSmall) {
        const World world{7'300'000'000, {.width = 16, .height = 9}};
        EXPECT_FALSE(placeOverview(world, {500, 900}).has_value());
        EXPECT_FALSE(placeOverview(world, {1600, 300}).has_value());
        EXPECT_FALSE(placeOverview(World{0, {}}, {1600, 900}).has_value());
    }

    // Minimap pixels and world points convert both ways; the box corners are the world corners.
    TEST(Overview, PixelWorldRoundTrip) {
        const OverviewScene s;
        const Overview& o = s.overview;
        const WorldPoint topLeft = o.worldAt({o.box.x, o.box.y}, s.world);
        EXPECT_DOUBLE_EQ(topLeft.x, 0.0);
        EXPECT_DOUBLE_EQ(topLeft.y, 0.0);
        const WorldPoint bottomRight = o.worldAt({o.box.x + o.box.width, o.box.y + o.box.height}, s.world);
        EXPECT_NEAR(bottomRight.x, static_cast<double>(s.world.rowWidth()), 1e-6);
        EXPECT_NEAR(bottomRight.y, static_cast<double>(s.world.rows()), 1e-6);
        const PixelPoint middle = o.pixelAt(o.worldAt(o.box.center(), s.world));
        EXPECT_NEAR(middle.x, o.box.center().x, 1e-9);
        EXPECT_NEAR(middle.y, o.box.center().y, 1e-9);
        // Outside the box clamps to the world.
        const WorldPoint outside = o.worldAt({0, 0}, s.world);
        EXPECT_DOUBLE_EQ(outside.x, 0.0);
        EXPECT_DOUBLE_EQ(outside.y, 0.0);
    }

    // The whole-volume view marks the whole minimap; a deep zoom marks a small box around the view center.
    TEST(Overview, ViewBox) {
        const OverviewScene s;
        const Overview& o = s.overview;
        const PixelBox whole = o.viewBox(s.fitted, 6.0);
        EXPECT_NEAR(whole.x, o.box.x, 1e-9);
        EXPECT_NEAR(whole.y, o.box.y, 1e-9);
        EXPECT_NEAR(whole.width, o.box.width, 1e-9);
        EXPECT_NEAR(whole.height, o.box.height, 1e-9);

        Viewport deep = s.fitted;
        deep.scale = 1.0;
        deep.center = {5000, 30000};
        const PixelBox small = o.viewBox(deep, 6.0);
        EXPECT_DOUBLE_EQ(small.width, 6.0);
        EXPECT_DOUBLE_EQ(small.height, 6.0);
        const PixelPoint center = o.pixelAt(deep.center);
        EXPECT_NEAR(small.center().x, center.x, 1e-9);
        EXPECT_NEAR(small.center().y, center.y, 1e-9);
    }

    // The marker of a view at the world's edge stays inside the minimap.
    TEST(Overview, ViewBoxStaysInside) {
        const OverviewScene s;
        const Overview& o = s.overview;
        Viewport corner = s.fitted;
        corner.scale = 1.0;
        corner.center = {0, 0};
        const PixelBox marked = o.viewBox(corner, 6.0);
        EXPECT_DOUBLE_EQ(marked.x, o.box.x);
        EXPECT_DOUBLE_EQ(marked.y, o.box.y);
        EXPECT_DOUBLE_EQ(marked.width, 6.0);
    }

    // Dragging moves the view center with the pointer at the minimap's scale; a click without grab jumps there.
    TEST(Overview, DragPansTheView) {
        const OverviewScene s;
        const Overview& o = s.overview;
        Viewport view = s.fitted;
        view.scale = 4.0;
        view.center = {50000, 30000};
        const PixelPoint start = o.pixelAt(view.center);
        const PixelPoint grab{2.0, -1.0}; // Took hold slightly off the marker's center.
        const PixelPoint pointer{start.x + grab.x, start.y + grab.y};

        const Viewport same = o.dragTo(view, pointer, grab, s.world, s.limits);
        EXPECT_NEAR(same.center.x, view.center.x, 1e-6);
        EXPECT_NEAR(same.center.y, view.center.y, 1e-6);
        EXPECT_DOUBLE_EQ(same.scale, view.scale);

        const Viewport moved = o.dragTo(view, {pointer.x + 10.0, pointer.y + 5.0}, grab, s.world, s.limits);
        EXPECT_NEAR(moved.center.x - view.center.x, 10.0 * o.scale, 1e-6);
        EXPECT_NEAR(moved.center.y - view.center.y, 5.0 * o.scale, 1e-6);

        const Viewport jumped = o.dragTo(view, o.box.center(), {}, s.world, s.limits);
        EXPECT_NEAR(jumped.center.x, static_cast<double>(s.world.rowWidth()) / 2.0, 1e-6);
        EXPECT_NEAR(jumped.center.y, static_cast<double>(s.world.rows()) / 2.0, 1e-6);

        const Viewport beyond = o.dragTo(view, {s.size.x, s.size.y}, {}, s.world, s.limits);
        EXPECT_DOUBLE_EQ(beyond.center.x, s.limits.worldSize.x);
        EXPECT_DOUBLE_EQ(beyond.center.y, s.limits.worldSize.y);
    }

}} // namespace icd::core
