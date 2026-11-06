// Read and write activity on the map (T114): how a pulse looks over its 0.4 s, which bars a run gives, which MFT
// clusters a read between two progress reports covered, and the bounded pulse list.

#include "core/map/Activity.h"

#include <gtest/gtest.h>

namespace icd::core { namespace {

    using namespace std::chrono_literals;

    // A read grows to 1.2x as wide and from 1 to 16 pixels tall (whatever the zoom) while it fades from 0.5 to 0.05; a
    // write runs the other way; both last 0.4 s.
    TEST(Activity, PulseLooksOverItsLife) {
        const auto start = pulseLook(ActivityKind::Read, 0ms);
        ASSERT_TRUE(start.has_value());
        EXPECT_DOUBLE_EQ(start->scaleX, 1.0);
        EXPECT_DOUBLE_EQ(start->height, 1.0);
        EXPECT_DOUBLE_EQ(start->opacity, 0.5);
        const auto middle = pulseLook(ActivityKind::Read, 200ms);
        ASSERT_TRUE(middle.has_value());
        EXPECT_DOUBLE_EQ(middle->scaleX, 1.1);
        EXPECT_DOUBLE_EQ(middle->height, 8.5);
        EXPECT_DOUBLE_EQ(middle->opacity, 0.275);
        const auto late = pulseLook(ActivityKind::Read, 399ms);
        ASSERT_TRUE(late.has_value());
        EXPECT_NEAR(late->scaleX, 1.2, 0.001);
        EXPECT_NEAR(late->height, 16.0, 0.04);
        EXPECT_NEAR(late->opacity, 0.05, 0.002);
        EXPECT_FALSE(pulseLook(ActivityKind::Read, 400ms).has_value());
        EXPECT_FALSE(pulseLook(ActivityKind::Read, -1ms).has_value());

        const auto landing = pulseLook(ActivityKind::Write, 0ms);
        ASSERT_TRUE(landing.has_value());
        EXPECT_DOUBLE_EQ(landing->scaleX, 1.2);
        EXPECT_DOUBLE_EQ(landing->height, 16.0);
        EXPECT_DOUBLE_EQ(landing->opacity, 0.05);
        const auto landed = pulseLook(ActivityKind::Write, 399ms);
        ASSERT_TRUE(landed.has_value());
        EXPECT_NEAR(landed->height, 1.0, 0.04);
        EXPECT_NEAR(landed->opacity, 0.5, 0.002);
    }

    // Reads and writes get their own colors in every preset, so source and destination are told apart; the user's
    // colors replace them, and the legend lists both under their scheme keys.
    TEST(Activity, ReadsAndWritesHaveTheirOwnColors) {
        for (const ColorPreset preset : colorPresets()) {
            const ActivityColors colors = activityColors(ColorScheme(preset));
            EXPECT_NE(colors.read, colors.write);
            EXPECT_NE(colors.read, presetColor(preset, "no.such.key")); // Each preset lists them.
            EXPECT_NE(colors.write, presetColor(preset, "no.such.key"));
        }
        EXPECT_NE(activityColors(ColorScheme(ColorPreset::Dark)).read,
                  activityColors(ColorScheme(ColorPreset::Standard)).read);
        const ColorScheme custom(ColorPreset::Dark, {{std::string(writeKey), rgb(1, 2, 3)}});
        EXPECT_EQ(activityColors(custom).write, rgb(1, 2, 3));
        EXPECT_EQ(activityColors(custom).read, presetColor(ColorPreset::Dark, readKey));
        const auto legend = activityLegend();
        ASSERT_EQ(legend.size(), 2U);
        EXPECT_EQ(legend[0].key, readKey);
        EXPECT_EQ(legend[1].key, writeKey);
        EXPECT_EQ(legend[0].color, presetColor(ColorPreset::Standard, readKey));
    }

    // A run within a row is one bar; a run over several rows is its first-row piece, the full rows as one block, and
    // its last-row piece; the end of the volume clips it.
    TEST(Activity, RunsBecomeBars) {
        const World world(1000, {.width = 1.0, .height = 1.0});
        const std::uint64_t w = world.rowWidth();
        ASSERT_GT(w, 4U);
        const auto one = activityBars(world, {w + 2, 3});
        ASSERT_EQ(one.size(), 1U);
        EXPECT_EQ(one[0].x, 2U);
        EXPECT_EQ(one[0].y, 1U);
        EXPECT_EQ(one[0].width, 3U);
        EXPECT_EQ(one[0].height, 1U);

        const auto two = activityBars(world, {w - 2, 5}); // End of row 0, start of row 1.
        ASSERT_EQ(two.size(), 2U);
        EXPECT_EQ(two[0].x, w - 2);
        EXPECT_EQ(two[0].width, 2U);
        EXPECT_EQ(two[1].y, 1U);
        EXPECT_EQ(two[1].x, 0U);
        EXPECT_EQ(two[1].width, 3U);

        const auto three = activityBars(world, {1, 3 * w}); // Rows 0 to 3.
        ASSERT_EQ(three.size(), 3U);
        EXPECT_EQ(three[0].width, w - 1);
        EXPECT_EQ(three[1].y, 1U);
        EXPECT_EQ(three[1].height, 2U);
        EXPECT_EQ(three[1].width, w);
        EXPECT_EQ(three[2].y, 3U);
        EXPECT_EQ(three[2].width, 1U);

        EXPECT_TRUE(activityBars(world, {5, 0}).empty());
        EXPECT_TRUE(activityBars(world, {2000, 4}).empty());
        const auto clipped = activityBars(world, {998, 50});
        ASSERT_EQ(clipped.size(), 1U);
        EXPECT_EQ(clipped[0].x + clipped[0].width, world.cellOf(999).x + 1);
    }

    // Positions along a sequence of runs map to the clusters they cover, across run boundaries.
    TEST(Activity, RunsBetweenFollowTheSequence) {
        const std::vector<ClusterRun> mft{{100, 10}, {500, 20}, {900, 5}};
        EXPECT_EQ(runsBetween(mft, {0, 4}), (std::vector<ClusterRun>{{100, 4}}));
        EXPECT_EQ(runsBetween(mft, {8, 13}), (std::vector<ClusterRun>{{108, 2}, {500, 3}}));
        EXPECT_EQ(runsBetween(mft, {10, 35}), (std::vector<ClusterRun>{{500, 20}, {900, 5}}));
        EXPECT_TRUE(runsBetween(mft, {7, 7}).empty());
        EXPECT_TRUE(runsBetween(mft, {40, 50}).empty());
    }

    // The layer drops pulses that are over, keeps a bounded number and a bounded number of runs per pulse.
    TEST(Activity, LayerIsBounded) {
        const AnimationClock::time_point t0{};
        ActivityLayer layer;
        EXPECT_FALSE(layer.add({.runs = {}, .kind = ActivityKind::Read, .start = t0}));
        EXPECT_FALSE(layer.add({.runs = {{4, 0}}, .kind = ActivityKind::Read, .start = t0}));
        std::vector<ClusterRun> many;
        for (Lcn i = 0; i < 40; ++i) {
            many.push_back({i * 10, 2});
        }
        ASSERT_TRUE(layer.add({.runs = many, .kind = ActivityKind::Write, .start = t0}));
        EXPECT_EQ(layer.pulses().front().runs.size(), ActivityLayer::maxRunsPerPulse);
        for (std::size_t i = 1; i < ActivityLayer::maxPulses; ++i) {
            ASSERT_TRUE(layer.add({.runs = {{i, 1}}, .kind = ActivityKind::Read, .start = t0 + 300ms}));
        }
        EXPECT_FALSE(layer.add({.runs = {{1, 1}}, .kind = ActivityKind::Read, .start = t0}));
        EXPECT_EQ(layer.dropped(), 1U);
        layer.prune(t0 + 400ms); // The first is over; the others started 300 ms later.
        EXPECT_EQ(layer.pulses().size(), ActivityLayer::maxPulses - 1);
        layer.prune(t0 + 700ms);
        EXPECT_TRUE(layer.empty());
    }

}} // namespace icd::core
