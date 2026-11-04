#include "core/map/ColorModes.h"

#include "core/classify/Catalog.h"
#include "core/map/MapData.h"
#include "core/map/TileRasterizer.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

namespace icd::core { namespace {

    // FILETIME ticks in a day.
    constexpr std::int64_t day = 864'000'000'000LL;

    // The color modes drawn from file facts.
    constexpr std::array modes{ColorMode::Fragmentation, ColorMode::Age, ColorMode::Size, ColorMode::FileClass};

    // Ages fall into buckets at a day, a week, a month, a year and three years; future times count as new.
    TEST(ColorModes, AgeBuckets) {
        const std::int64_t now = 1'000'000 * day;
        EXPECT_EQ(ageBucket(now, now), 0u);
        EXPECT_EQ(ageBucket(now + day, now), 0u);
        EXPECT_EQ(ageBucket(now - day + 1, now), 0u);
        EXPECT_EQ(ageBucket(now - day, now), 1u);
        EXPECT_EQ(ageBucket(now - 7 * day, now), 2u);
        EXPECT_EQ(ageBucket(now - 30 * day, now), 3u);
        EXPECT_EQ(ageBucket(now - 365 * day, now), 4u);
        EXPECT_EQ(ageBucket(now - 3 * 365 * day + 1, now), 4u);
        EXPECT_EQ(ageBucket(now - 3 * 365 * day, now), 5u);
        EXPECT_EQ(ageBucket(0, now), 5u);
    }

    // Sizes fall into buckets at 64 KB and every 16x above it.
    TEST(ColorModes, SizeBuckets) {
        EXPECT_EQ(sizeBucket(0), 0u);
        EXPECT_EQ(sizeBucket(65'535), 0u);
        EXPECT_EQ(sizeBucket(65'536), 1u);
        EXPECT_EQ(sizeBucket((1ULL << 20U) - 1), 1u);
        EXPECT_EQ(sizeBucket(1ULL << 20U), 2u);
        EXPECT_EQ(sizeBucket(16ULL << 20U), 3u);
        EXPECT_EQ(sizeBucket(256ULL << 20U), 4u);
        EXPECT_EQ(sizeBucket((4ULL << 30U) - 1), 4u);
        EXPECT_EQ(sizeBucket(4ULL << 30U), 5u);
        EXPECT_EQ(sizeBucket(~0ULL), 5u);
    }

    // Bucket values survive the round trip, and averages land on the nearest bucket.
    TEST(ColorModes, BucketValues) {
        for (std::size_t bucket = 0; bucket < valueBuckets; ++bucket) {
            EXPECT_EQ(bucketOfValue(bucketValue(bucket)), bucket);
        }
        EXPECT_EQ(bucketValue(valueBuckets - 1), 255);
        EXPECT_EQ(bucketOfValue(25), 0u);
        EXPECT_EQ(bucketOfValue(26), 1u);
        for (const ColorMode mode : modes) {
            EXPECT_EQ(legendOf(mode).front().label, "Free");
        }
        EXPECT_EQ(legendOf(ColorMode::FileClass).size(), classify::builtinCatalog().classes.size() + 1);
        EXPECT_EQ(legendOf(ColorMode::FileClass)[15].label, "Photos"); // ClassId 14, after free space.
    }

    // A small synthetic volume with a wide spread of sizes, ages and classes.
    Snapshot smallVolume() {
        SynthOptions options;
        options.seed = 11;
        options.geometry = {.bytesPerCluster = 4096, .totalClusters = 50'000};
        options.files = 400;
        options.fillPercent = 70.0;
        return generateSyntheticVolume(options);
    }

    // The classes of the small volume's files under the built-in catalog.
    const std::vector<std::uint8_t>& smallClasses() {
        static const std::vector<std::uint8_t> classes = [] {
            InlineExecutor serial;
            return classify::RuleEngine(classify::builtinCatalog().rules).classify(smallVolume(), {}, serial);
        }();
        return classes;
    }

    // The small volume, mapped with its classes.
    std::shared_ptr<const MapData> smallMap(Executor& executor) {
        return MapData::build(smallVolume(), {.width = 16, .height = 9}, executor, {}, smallClasses());
    }

    // The legend entry of a used cluster, worked out from the file table alone (independent of the map code).
    std::size_t expectedEntry(const MapData& map, ColorMode mode, const ExtentRecord& record) {
        const Snapshot& snapshot = map.snapshot();
        const FileIndex file = record.file();
        switch (mode) {
        case ColorMode::Age:
            return ageBucket(snapshot.files.times(file).modified, map.ageReference()) + 1;
        case ColorMode::Size:
            return sizeBucket(snapshot.files.sizeBytes(file)) + 1;
        case ColorMode::FileClass:
            return std::size_t{smallClasses()[file]} + 1;
        case ColorMode::Fragmentation:
        case ColorMode::ZoneFit:
        case ColorMode::PlanPreview:
            break;
        }
        if ((record.flags() & ExtentUnmovable) != 0 || snapshot.files.has(file, FileUnmovable)) {
            return 3;
        }
        return snapshot.extents.fragmentCount(file) > 1 ? 2 : 1;
    }

    // Counts a cell rectangle cluster by cluster.
    LegendCounts bruteForce(const MapData& map, ColorMode mode, CellRect cells) {
        const World& world = map.world();
        LegendCounts counts{.clusters = std::vector<ClusterCount>(legendOf(mode).size(), 0), .total = 0};
        for (std::uint64_t y = cells.y; y < cells.y + cells.height; ++y) {
            for (std::uint64_t x = cells.x; x < cells.x + cells.width; ++x) {
                const auto lcn = world.lcnAt({x, y});
                if (!lcn) {
                    continue;
                }
                const ExtentRecord* record = map.snapshot().extents.find(*lcn);
                ++counts.clusters[record == nullptr ? 0 : expectedEntry(map, mode, *record)];
                ++counts.total;
            }
        }
        return counts;
    }

    // Random rectangles (and whole rows, and the whole world) count the same as cluster by cluster, serially and
    // on a pool.
    TEST(ColorModes, LegendCountsMatchBruteForce) {
        InlineExecutor serial;
        services::ThreadPool pool(4);
        const auto map = smallMap(serial);
        const World& world = map->world();
        const MapFileFacts facts = map->source().facts;
        std::mt19937 random(5);
        std::vector<CellRect> rects{{0, 0, world.rowWidth(), world.rows()}, {0, 3, world.rowWidth(), 40}};
        for (int sample = 0; sample < 20; ++sample) {
            const std::uint64_t x = random() % world.rowWidth();
            const std::uint64_t y = random() % world.rows();
            rects.push_back({x, y, 1 + random() % (world.rowWidth() - x), 1 + random() % (world.rows() - y)});
        }
        for (const ColorMode mode : modes) {
            for (const CellRect& rect : rects) {
                const LegendCounts expected = bruteForce(*map, mode, rect);
                const LegendCounts counted = countLegend(world, facts, mode, rect, serial);
                EXPECT_EQ(counted.clusters, expected.clusters)
                    << static_cast<int>(mode) << " " << rect.x << "," << rect.y;
                EXPECT_EQ(counted.total, expected.total);
                EXPECT_EQ(countLegend(world, facts, mode, rect, pool).clusters, expected.clusters);
            }
            // The volume spreads over several entries of every mode (all four of fragmentation), so the counts are
            // not trivially all in one entry. A 200 MB volume has no files of 256 MB and up.
            const LegendCounts whole = bruteForce(*map, mode, rects.front());
            const auto filled = std::ranges::count_if(whole.clusters, [](ClusterCount count) { return count > 0; });
            EXPECT_GE(filled, 4) << static_cast<int>(mode);
            EXPECT_EQ(whole.total, world.clusters());
        }
    }

    // Checks that tiles of every mode use exactly the legend colors of scheme.
    void expectTileColorsAreLegendColors(const ColorScheme& scheme);

    // At one cluster per cell, every tile pixel of a used cell has exactly its legend entry's color, in every mode and
    // every color scheme: each preset, and one with the user's own colors (T125).
    TEST(ColorModes, TileColorsAreLegendColors) {
        std::vector<ColorScheme> schemes;
        for (const ColorPreset preset : colorPresets()) {
            schemes.emplace_back(preset);
        }
        schemes.emplace_back(ColorPreset::Dark,
                             ColorScheme::Custom{{"free", rgb(1, 2, 3)},
                                                 {"class.photos", rgb(4, 5, 6)},
                                                 {"age.2", rgb(7, 8, 9)},
                                                 {"fragmentation.fragmented", rgb(10, 11, 12)}});
        for (const ColorScheme& scheme : schemes) {
            SCOPED_TRACE(presetId(scheme.preset()));
            expectTileColorsAreLegendColors(scheme);
        }
    }

    // The standard preset is the map's original colors (the legend tables' own), so nothing changed for users who never
    // pick another one.
    TEST(ColorModes, StandardPresetIsTheOriginalColors) {
        const ColorScheme standard;
        for (const ColorMode mode : modes) {
            const std::span<const LegendEntry> legend = legendOf(mode);
            const std::vector<Rgba> colors = legendColors(mode, standard);
            ASSERT_EQ(colors.size(), legend.size());
            for (std::size_t entry = 0; entry < legend.size(); ++entry) {
                EXPECT_EQ(colors[entry], legend[entry].color) << legend[entry].key;
            }
        }
    }

    // Every preset has its own color for every legend key, and the colors of a mode are told apart.
    TEST(ColorModes, PresetsColorEveryKeyDistinctly) {
        for (const ColorPreset preset : colorPresets()) {
            EXPECT_EQ(presetFromId(presetId(preset)), preset);
            EXPECT_FALSE(presetName(preset).empty());
            for (const ColorMode mode : modes) {
                std::vector<Rgba> colors = legendColors(mode, ColorScheme(preset));
                for (const LegendEntry& entry : legendOf(mode)) {
                    EXPECT_NE(presetColor(preset, entry.key), rgb(128, 128, 128))
                        << presetId(preset) << " " << entry.key;
                }
                std::ranges::sort(colors);
                EXPECT_EQ(std::ranges::adjacent_find(colors), colors.end()) << presetId(preset);
            }
        }
        EXPECT_FALSE(presetFromId("neon"));
        const ColorScheme custom(ColorPreset::ColorblindSafe, {{"size.1", rgb(9, 9, 9)}});
        EXPECT_EQ(custom.color("size.1"), rgb(9, 9, 9));
        EXPECT_EQ(custom.color("size.2"), presetColor(ColorPreset::ColorblindSafe, "size.2"));
        EXPECT_NE(ColorScheme(ColorPreset::Dark).outside(), ColorScheme().outside());
    }

    // The body of TileColorsAreLegendColors for one scheme.
    void expectTileColorsAreLegendColors(const ColorScheme& scheme) {
        InlineExecutor serial;
        const auto map = smallMap(serial);
        const World& world = map->world();
        const int level = -1; // Two pixels per cell.
        for (const ColorMode mode : modes) {
            const auto palette = paletteFor(mode, scheme);
            const std::vector<Rgba> legend = legendColors(mode, scheme);
            const TileId tile{level, 1, 0};
            const std::vector<Rgba> pixels = renderTile(map->source(), tile, *palette);
            const CellRect rect = world.tileCells(tile);
            for (std::uint64_t y = 0; y < rect.height; ++y) {
                for (std::uint64_t x = 0; x < rect.width; ++x) {
                    const auto lcn = world.lcnAt({rect.x + x, rect.y + y});
                    if (!lcn) {
                        continue;
                    }
                    const ExtentRecord* record = map->snapshot().extents.find(*lcn);
                    const std::size_t entry = record == nullptr ? 0 : expectedEntry(*map, mode, *record);
                    ASSERT_EQ(pixels[(2 * y) * World::tileSize + 2 * x], legend[entry])
                        << static_cast<int>(mode) << " cell " << x << "," << y;
                }
            }
        }
    }

    // The visible cells of a view cover every partly shown cell and stay inside the world.
    TEST(ColorModes, VisibleCells) {
        const World world(10'000, {.width = 1, .height = 1}); // About 100 x 100.
        const Viewport inside{.scale = 1.0 / 10.0, .center = {50.5, 50.5}, .size = {100, 100}};
        const CellRect cells = visibleCells(world, inside);
        EXPECT_EQ(cells.x, 45u);
        EXPECT_EQ(cells.y, 45u);
        EXPECT_EQ(cells.width, 11u);
        EXPECT_EQ(cells.height, 11u);
        const Viewport wide{.scale = 4.0, .center = {50, 50}, .size = {100, 100}};
        const CellRect all = visibleCells(world, wide);
        EXPECT_EQ(all.x, 0u);
        EXPECT_EQ(all.y, 0u);
        EXPECT_EQ(all.width, world.rowWidth());
        EXPECT_EQ(all.height, world.rows());
    }

}} // namespace icd::core
