#include "core/map/Ruler.h"

#include <gtest/gtest.h>

#include <set>

namespace icd::core { namespace {

    constexpr std::uint64_t gb = std::uint64_t{1} << 30U;
    constexpr std::uint64_t tb = std::uint64_t{1} << 40U;

    // A 30 TB world (4 KB clusters) in a 1600 x 900 window.
    struct RulerScene {
        World world{7'300'000'000, {.width = 16, .height = 9}}; // The world.
        Viewport fitted = fitView(world, {1600, 900}); // Whole volume.

        // Ruler input for a view.
        [[nodiscard]] RulerInput input(const Viewport& view) const {
            return {.world = &world, .view = view, .bytesPerCluster = 4096, .disk = std::nullopt, .minSpacing = 40.0};
        }
    };

    // Checks what every row tick must satisfy: on a step multiple, at the pixel and offset the world math
    // gives, at least minSpacing apart and less than twice that, with distinct labels.
    void expectConsistentRows(const World& world, const RulerInput& input, const RulerTicks& ticks) {
        ASSERT_FALSE(ticks.major.empty());
        std::set<std::string> labels;
        for (std::size_t i = 0; i < ticks.major.size(); ++i) {
            const RulerTick& tick = ticks.major[i];
            EXPECT_EQ(tick.cell % ticks.step, 0U);
            EXPECT_LT(tick.cell, world.rows());
            const Lcn lcn = tick.cell * world.rowWidth();
            EXPECT_EQ(world.cellOf(lcn), (CellPoint{0, tick.cell}));
            EXPECT_EQ(tick.bytes, lcn * input.bytesPerCluster);
            EXPECT_DOUBLE_EQ(tick.percent, static_cast<double>(lcn) / static_cast<double>(world.clusters()) * 100.0);
            EXPECT_DOUBLE_EQ(tick.pixel, input.view.pixelAt({0, static_cast<double>(tick.cell)}).y);
            EXPECT_GE(tick.pixel, -1e-9);
            EXPECT_LE(tick.pixel, input.view.size.y + 1e-9);
            labels.insert(tick.bytesText);
            if (i > 0) {
                const double spacing = tick.pixel - ticks.major[i - 1].pixel;
                EXPECT_GE(spacing, input.minSpacing - 1e-9);
                EXPECT_LT(spacing, 2.0 * input.minSpacing);
            }
        }
        EXPECT_EQ(labels.size(), ticks.major.size()) << "neighbouring labels must differ";
    }

    // Steps are the smallest power of two at least the requested cells.
    TEST(Ruler, TickStep) {
        EXPECT_EQ(tickStep(0.0), 1U);
        EXPECT_EQ(tickStep(0.5), 1U);
        EXPECT_EQ(tickStep(1.0), 1U);
        EXPECT_EQ(tickStep(3.0), 4U);
        EXPECT_EQ(tickStep(4.0), 4U);
        EXPECT_EQ(tickStep(4.1), 8U);
    }

    // Offsets use the unit of the largest offset, a smaller one when the step is fine, and the decimals that
    // write the step exactly (at most three).
    TEST(Ruler, FormatOffset) {
        EXPECT_EQ(formatOffset(3 * tb, {.largest = 30 * tb, .step = tb}), "3 TB");
        EXPECT_EQ(formatOffset(0, {.largest = 30 * tb, .step = tb}), "0 TB");
        EXPECT_EQ(formatOffset(30719 * gb + gb / 2, {.largest = 30 * tb, .step = 32U << 20U}), "30,719.500 GB");
        EXPECT_EQ(formatOffset(3 * gb / 2, {.largest = 4 * gb, .step = 256U << 20U}), "1.50 GB");
        EXPECT_EQ(formatOffset(512, {.largest = 1000, .step = 1}), "512 bytes");
        EXPECT_EQ(formatOffset(704 * gb, {.largest = 4 * tb, .step = 704 * gb}),
                  "704 GB"); // 0.6875 TB is exact one unit down.
        EXPECT_EQ(offsetUnit({.largest = 4 * tb, .step = 704 * gb}), 3U);
        EXPECT_EQ(offsetUnit({.largest = 30 * tb, .step = tb}), 4U);
    }

    // Percentages get decimals only when the step needs them.
    TEST(Ruler, FormatPercent) {
        EXPECT_EQ(formatPercent(42.0, 5.0), "42%");
        EXPECT_EQ(formatPercent(4.2, 0.5), "4.2%");
        EXPECT_EQ(formatPercent(12.3456, 0.001), "12.346%");
    }

    // The whole-volume view: ticks match the world math and start at the volume start.
    TEST(Ruler, RowTicksOfFittedView) {
        const RulerScene s;
        const RulerInput input = s.input(s.fitted);
        const RulerTicks ticks = rowTicks(input);
        expectConsistentRows(s.world, input, ticks);
        EXPECT_EQ(ticks.major.front().cell, 0U);
        EXPECT_EQ(ticks.major.front().bytesText, "0.00 TB"); // A 1.75 TB step.
        EXPECT_EQ(ticks.major.front().percentText, "0%");
        EXPECT_TRUE(ticks.major.front().diskText.empty());
    }

    // Deep in the volume at 32 px per cluster: every row is a tick and the labels still differ.
    TEST(Ruler, RowTicksAtDeepZoom) {
        const RulerScene s;
        const Viewport view{
            .scale = 1.0 / 32.0, .center = {100.0, static_cast<double>(s.world.rows()) * 0.7}, .size = {1600, 900}};
        const RulerInput input = s.input(view);
        const RulerTicks ticks = rowTicks(input);
        EXPECT_EQ(ticks.step, 2U);
        expectConsistentRows(s.world, input, ticks);
        EXPECT_GT(ticks.major.front().percent, 69.0);
        EXPECT_LT(ticks.major.back().percent, 71.0);
    }

    // Minor ticks fall between labelled ones, never on them.
    TEST(Ruler, MinorTicksBetweenMajor) {
        const RulerScene s;
        const RulerTicks ticks = rowTicks(s.input(s.fitted));
        ASSERT_FALSE(ticks.minor.empty());
        for (const double minor : ticks.minor) {
            for (const RulerTick& major : ticks.major) {
                EXPECT_NE(minor, major.pixel);
            }
        }
        EXPECT_GE(ticks.minor.size(), 3 * (ticks.major.size() - 1));
    }

    // Column ticks mark offsets within a row, up to the row end.
    TEST(Ruler, ColumnTicks) {
        const RulerScene s;
        const RulerInput input = s.input(s.fitted);
        const RulerTicks ticks = columnTicks(input);
        ASSERT_FALSE(ticks.major.empty());
        EXPECT_EQ(ticks.major.front().cell, 0U);
        for (const RulerTick& tick : ticks.major) {
            EXPECT_EQ(tick.cell % ticks.step, 0U);
            EXPECT_LE(tick.cell, s.world.rowWidth());
            EXPECT_EQ(tick.bytes, tick.cell * 4096U);
            EXPECT_DOUBLE_EQ(tick.pixel, s.fitted.pixelAt({static_cast<double>(tick.cell), 0}).x);
            EXPECT_EQ(tick.bytesText.front(), '+');
            EXPECT_TRUE(tick.percentText.empty());
        }
        EXPECT_EQ(ticks.major.back().cell, s.world.rowWidth()); // The fitted view shows the row end.
    }

    // With a known partition placement, row ticks also give the share of the whole disk.
    TEST(Ruler, DiskPercent) {
        const RulerScene s;
        RulerInput input = s.input(s.fitted);
        const std::uint64_t volumeBytes = s.world.clusters() * 4096U;
        input.disk = DiskPlacement{.partitionOffset = volumeBytes, .diskBytes = 2 * volumeBytes};
        const RulerTicks ticks = rowTicks(input);
        ASSERT_FALSE(ticks.major.empty());
        EXPECT_EQ(ticks.major.front().diskText, "disk 50%");
        for (const RulerTick& tick : ticks.major) {
            EXPECT_EQ(tick.diskText.rfind("disk ", 0), 0U);
        }
    }

    // No world, no ticks.
    TEST(Ruler, EmptyWithoutWorld) {
        const RulerInput input{};
        EXPECT_TRUE(rowTicks(input).major.empty());
        EXPECT_TRUE(columnTicks(input).major.empty());
    }

}} // namespace icd::core
