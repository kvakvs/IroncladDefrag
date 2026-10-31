#include "core/map/HitTest.h"

#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <utility>

namespace icd::core { namespace {

    // A 1000-cluster volume: "\Photos\a.jpg" in two fragments (the first split into two continuing runs),
    // "\Photos\b.jpg" in one, the directory's index, and free gaps.
    struct HitVolume {
        FileIndex photos = 0; // "\Photos".
        FileIndex a = 0; // Two fragments: [10, 25) as two runs, then [500, 520).
        FileIndex b = 0; // One run [100, 150).
        Snapshot snapshot; // The built snapshot.
        World world{1000, {.width = 1, .height = 1}}; // Square layout.
    };

    // A file entry.
    FileEntry entry(std::string_view name, FileIndex parent, std::uint16_t flags = 0) {
        FileEntry e;
        e.name = name;
        e.parent = parent;
        e.flags = flags;
        e.sizeBytes = 100'000;
        e.times.modified = 134'116'992'000'000'000; // 2026-01-01 00:00 UTC.
        return e;
    }

    // An extent entry.
    ExtentEntry extent(FileIndex file, ClusterRun run, std::uint64_t vcn) {
        ExtentEntry e;
        e.file = file;
        e.run = run;
        e.vcn = vcn;
        return e;
    }

    // Builds the volume.
    HitVolume hitVolume() {
        HitVolume v;
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 1000});
        const FileIndex root = builder.addFile(entry("", noFile, FileDirectory));
        v.photos = builder.addFile(entry("Photos", root, FileDirectory));
        v.a = builder.addFile(entry("a.jpg", v.photos, FileCompressed));
        v.b = builder.addFile(entry("b.jpg", v.photos));
        builder.addExtent(extent(v.photos, {0, 5}, 0));
        builder.addExtent(extent(v.a, {10, 10}, 0));
        builder.addExtent(extent(v.a, {20, 5}, 10)); // Continues the run before: same fragment.
        builder.addExtent(extent(v.a, {500, 20}, 15));
        builder.addExtent(extent(v.b, {100, 50}, 0));
        builder.addFreeRun({5, 5});
        builder.addFreeRun({25, 75});
        builder.addFreeRun({150, 350});
        builder.addFreeRun({520, 480});
        v.snapshot = std::move(builder).build();
        return v;
    }

    // A view of the world at a scale, centered on a world point, in an 800 x 600 window.
    Viewport viewAt(double scale, WorldPoint center) { return {.scale = scale, .center = center, .size = {800, 600}}; }

    // The middle of a cell's area on screen.
    PixelPoint pixelOfCell(const Viewport& view, CellPoint cell) {
        return view.pixelAt({static_cast<double>(cell.x) + 0.5, static_cast<double>(cell.y) + 0.5});
    }

    // Fragment i/n joins runs that continue on disk, like fragmentCount().
    TEST(HitTest, FragmentOf) {
        const HitVolume v = hitVolume();
        const auto records = v.snapshot.extents.records();
        ASSERT_EQ(records.size(), 5U); // [0,5) [10,20) [20,25) [100,150) [500,520).
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 1).index, 1U);
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 2).index, 1U);
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 4).index, 2U);
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 4).count, 2U);
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 4).count, v.snapshot.extents.fragmentCount(v.a));
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 3).count, 1U);
        EXPECT_EQ(fragmentOf(v.snapshot.extents, 99).count, 0U);
    }

    // At deep zoom the pointer finds the cluster, its extent and fragment; the pixel covers one cluster.
    TEST(HitTest, DeepZoomFindsTheExtent) {
        const HitVolume v = hitVolume();
        const CellPoint cell = v.world.cellOf(505);
        const Viewport view = viewAt(1.0 / 8.0, {static_cast<double>(cell.x), static_cast<double>(cell.y)});
        const auto hit = hitTest(v.snapshot, v.world, view, pixelOfCell(view, cell));
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->lcn, 505U);
        ASSERT_TRUE(hit->record.has_value());
        EXPECT_EQ(v.snapshot.extents.records()[*hit->record].file(), v.a);
        EXPECT_EQ(hit->fragment.index, 2U);
        EXPECT_EQ(hit->fragment.count, 2U);
        EXPECT_EQ(hit->pixel.clusters, 1U);
        EXPECT_EQ(hit->pixel.used, 1U);
        EXPECT_FALSE(hit->freeRun.has_value());
    }

    // A free cluster reports its free run.
    TEST(HitTest, FreeClusterReportsItsRun) {
        const HitVolume v = hitVolume();
        const CellPoint cell = v.world.cellOf(300);
        const Viewport view = viewAt(1.0 / 8.0, {static_cast<double>(cell.x), static_cast<double>(cell.y)});
        const auto hit = hitTest(v.snapshot, v.world, view, pixelOfCell(view, cell));
        ASSERT_TRUE(hit.has_value());
        EXPECT_FALSE(hit->record.has_value());
        ASSERT_TRUE(hit->freeRun.has_value());
        EXPECT_EQ(*hit->freeRun, (ClusterRun{150, 350}));
    }

    // Off the world, or past the last cluster in the last row, nothing is hit.
    TEST(HitTest, OffTheVolume) {
        const HitVolume v = hitVolume();
        const Viewport view = viewAt(0.25, {0, 0});
        EXPECT_FALSE(hitTest(v.snapshot, v.world, view, {10, 10}).has_value()); // Left of and above the world.
        if (v.world.rowWidth() * v.world.rows() > v.world.clusters()) {
            const CellPoint pastEnd{v.world.rowWidth() - 1, v.world.rows() - 1};
            const Viewport end = viewAt(0.25, {static_cast<double>(pastEnd.x), static_cast<double>(pastEnd.y)});
            EXPECT_FALSE(hitTest(v.snapshot, v.world, end, pixelOfCell(end, pastEnd)).has_value());
        }
    }

    // At every zoom, random pointers hit what a brute-force lookup finds, and the pixel's coverage matches a
    // count over its cells.
    TEST(HitTest, MatchesBruteForceAtEveryZoom) {
        const HitVolume v = hitVolume();
        const auto records = v.snapshot.extents.records();
        const WorldPoint middle{static_cast<double>(v.world.rowWidth()) / 2.0,
                                static_cast<double>(v.world.rows()) / 2.0};
        std::mt19937 random(7);
        std::uniform_real_distribution<double> x(0.0, 800.0);
        std::uniform_real_distribution<double> y(0.0, 600.0);
        for (const double scale : {1.0 / 32.0, 1.0 / 5.0, 1.0, 0.07, 0.3}) {
            const Viewport view = viewAt(scale, middle);
            for (int sample = 0; sample < 300; ++sample) {
                const PixelPoint pointer{x(random), y(random)};
                const auto hit = hitTest(v.snapshot, v.world, view, pointer);
                const WorldPoint point = view.worldAt(pointer);
                const bool inWorld = point.x >= 0 && point.y >= 0 &&
                    point.x < static_cast<double>(v.world.rowWidth()) && point.y < static_cast<double>(v.world.rows());
                const auto lcn = inWorld
                    ? v.world.lcnAt({static_cast<std::uint64_t>(point.x), static_cast<std::uint64_t>(point.y)})
                    : std::nullopt;
                ASSERT_EQ(hit.has_value(), lcn.has_value());
                if (!hit) {
                    continue;
                }
                EXPECT_EQ(hit->lcn, *lcn);
                std::optional<std::size_t> expected;
                for (std::size_t i = 0; i < records.size(); ++i) {
                    if (records[i].run().contains(*lcn)) {
                        expected = i;
                    }
                }
                EXPECT_EQ(hit->record, expected);
                ClusterCount clusters = 0;
                ClusterCount used = 0;
                const CellRect& cells = hit->pixel.cells;
                for (std::uint64_t row = cells.y; row < cells.y + cells.height; ++row) {
                    for (std::uint64_t column = cells.x; column < cells.x + cells.width; ++column) {
                        if (const auto at = v.world.lcnAt({column, row}); at) {
                            ++clusters;
                            used += v.snapshot.extents.find(*at) != nullptr ? 1U : 0U;
                        }
                    }
                }
                EXPECT_EQ(hit->pixel.clusters, clusters);
                EXPECT_EQ(hit->pixel.used, used);
                EXPECT_GE(hit->cell.x, cells.x);
                EXPECT_LT(hit->cell.x, cells.x + cells.width);
                EXPECT_GE(hit->cell.y, cells.y);
                EXPECT_LT(hit->cell.y, cells.y + cells.height);
            }
        }
    }

    // On a synthetic volume, at every zoom level from 32 px per cluster to beyond the whole volume, the pointer
    // hits the extent a linear search finds, with its file's fragment count, and the pixel's usage matches a
    // per-cell count.
    TEST(HitTest, SyntheticVolumeAtEveryLevel) {
        SynthOptions options;
        options.seed = 11;
        options.files = 3000;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        const auto records = snapshot.extents.records();
        std::mt19937 random(3);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        for (int level = World::minLevel; level <= world.maxLevel() + 1; ++level) {
            const Viewport view{.scale = World::cellsPerPixel(level),
                                .center = {unit(random) * static_cast<double>(world.rowWidth()),
                                           unit(random) * static_cast<double>(world.rows())},
                                .size = {640, 480}};
            for (int sample = 0; sample < 40; ++sample) {
                const PixelPoint pointer{unit(random) * 640.0, unit(random) * 480.0};
                const auto hit = hitTest(snapshot, world, view, pointer);
                if (!hit) {
                    continue;
                }
                const auto found =
                    std::ranges::find_if(records, [&](const ExtentRecord& r) { return r.run().contains(hit->lcn); });
                ASSERT_EQ(hit->record.has_value(), found != records.end()) << "level " << level;
                if (hit->record) {
                    EXPECT_EQ(*hit->record, static_cast<std::size_t>(found - records.begin()));
                    EXPECT_EQ(hit->fragment.count, snapshot.extents.fragmentCount(found->file()));
                    EXPECT_GE(hit->fragment.index, 1U);
                    EXPECT_LE(hit->fragment.index, hit->fragment.count);
                }
                ClusterCount used = 0;
                const CellRect& cells = hit->pixel.cells;
                for (std::uint64_t row = cells.y; row < cells.y + cells.height; ++row) {
                    for (std::uint64_t column = cells.x; column < cells.x + cells.width; ++column) {
                        const auto at = world.lcnAt({column, row});
                        used += at && snapshot.extents.find(*at) != nullptr ? 1U : 0U;
                    }
                }
                EXPECT_EQ(hit->pixel.used, used) << "level " << level;
            }
        }
    }

    // FILETIMEs format in UTC without a zone; unset times show a dash.
    TEST(HitTest, FormatFileTime) {
        EXPECT_EQ(formatFileTime(134'116'992'000'000'000, nullptr), "2026-01-01 00:00 UTC");
        EXPECT_EQ(formatFileTime(134'116'992'000'000'000 + 600'000'000LL * 90, nullptr), "2026-01-01 01:30 UTC");
        EXPECT_EQ(formatFileTime(0, nullptr), "-");
    }

    // The tooltip names the file, stream, fragment, type and times, or the free run.
    TEST(HitTest, DescribeHit) {
        const HitVolume v = hitVolume();
        const CellPoint cell = v.world.cellOf(505);
        const Viewport view = viewAt(1.0 / 8.0, {static_cast<double>(cell.x), static_cast<double>(cell.y)});
        const auto used = hitTest(v.snapshot, v.world, view, pixelOfCell(view, cell));
        ASSERT_TRUE(used.has_value());
        const std::vector<std::string> lines = describeHit(v.snapshot, *used, nullptr);
        ASSERT_EQ(lines.size(), 7U);
        EXPECT_EQ(lines[0], "LCN 505; byte offset 2,068,480");
        EXPECT_EQ(lines[1], "\\Photos\\a.jpg");
        EXPECT_EQ(lines[2], "Stream: main data");
        EXPECT_EQ(lines[3], "Fragment 2 of 2; this extent 20 clusters (80.0 KB)");
        EXPECT_EQ(lines[4], "Size 97.7 KB; allocated 140 KB");
        EXPECT_EQ(lines[5], "Type: file, compressed");
        EXPECT_EQ(lines[6], "Modified 2026-01-01 00:00 UTC; created -; accessed -");

        const CellPoint freeCell = v.world.cellOf(300);
        const Viewport freeView = viewAt(1.0 / 8.0, {static_cast<double>(freeCell.x), static_cast<double>(freeCell.y)});
        const auto free = hitTest(v.snapshot, v.world, freeView, pixelOfCell(freeView, freeCell));
        ASSERT_TRUE(free.has_value());
        const std::vector<std::string> freeLines = describeHit(v.snapshot, *free, nullptr);
        ASSERT_EQ(freeLines.size(), 2U);
        EXPECT_EQ(freeLines[1], "Free: run of 350 clusters (1.37 MB) from LCN 150");

        const Viewport coarse = viewAt(3.0, {10, 10});
        const auto wide = hitTest(v.snapshot, v.world, coarse, coarse.pixelAt({1.5, 1.5}));
        ASSERT_TRUE(wide.has_value());
        const std::vector<std::string> wideLines = describeHit(v.snapshot, *wide, nullptr);
        EXPECT_EQ(wideLines.back().rfind("This pixel: ", 0), 0U);
    }

}} // namespace icd::core
