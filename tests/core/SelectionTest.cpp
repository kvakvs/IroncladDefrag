#include "core/map/Selection.h"

#include "core/map/TileRasterizer.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

namespace icd::core { namespace {

    // An extent entry.
    ExtentEntry extent(FileIndex file, ClusterRun run, std::uint64_t vcn) {
        ExtentEntry e;
        e.file = file;
        e.run = run;
        e.vcn = vcn;
        return e;
    }

    // Adds a file.
    FileIndex add(SnapshotBuilder& builder, std::string_view name, FileIndex parent, std::uint16_t flags = 0) {
        FileEntry entry;
        entry.name = name;
        entry.parent = parent;
        entry.flags = flags;
        return builder.addFile(entry);
    }

    // A 1000-cluster volume: file a has [10, 25) as two continuing runs then [500, 520); file b is stored back to
    // front: VCN 0 at [60, 70), VCN 10 at [40, 60), so its fragments touch on disk but not in file order.
    struct HandVolume {
        FileIndex a = 0; // Two fragments in LCN order.
        FileIndex b = 0; // Two fragments, touching on disk in reverse.
        FileIndex empty = 0; // No clusters.
        Snapshot snapshot; // The built snapshot.
    };

    // Builds the volume.
    HandVolume handVolume() {
        HandVolume v;
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 1000});
        const FileIndex root = add(builder, "", noFile, FileDirectory);
        v.a = add(builder, "a", root);
        v.b = add(builder, "b", root);
        v.empty = add(builder, "empty", root, FileResident);
        builder.addExtent(extent(v.a, {10, 10}, 0));
        builder.addExtent(extent(v.a, {20, 5}, 10));
        builder.addExtent(extent(v.a, {500, 20}, 15));
        builder.addExtent(extent(v.b, {60, 10}, 0));
        builder.addExtent(extent(v.b, {40, 20}, 10));
        v.snapshot = std::move(builder).build();
        return v;
    }

    // Fragments follow the file (VCN) order and join continuing runs; the overlay sorts by LCN and merges runs
    // that touch.
    TEST(Selection, FragmentsAndOverlay) {
        const HandVolume v = handVolume();
        const ExtentIndex& extents = v.snapshot.extents;
        EXPECT_EQ(fileFragments(extents, v.a), (std::vector<ClusterRun>{{10, 15}, {500, 20}}));
        EXPECT_EQ(fileFragments(extents, v.a).size(), extents.fragmentCount(v.a));
        EXPECT_EQ(fileFragments(extents, v.b), (std::vector<ClusterRun>{{60, 10}, {40, 20}}));
        EXPECT_TRUE(fileFragments(extents, v.empty).empty());

        const SelectionOverlay a = selectionOf(extents, v.a);
        EXPECT_EQ(a.file, v.a);
        EXPECT_EQ(a.runs, (std::vector<ClusterRun>{{10, 15}, {500, 20}}));
        EXPECT_EQ(selectionOf(extents, v.b).runs, (std::vector<ClusterRun>{{40, 30}}));
        EXPECT_TRUE(selectionOf(extents, v.empty).runs.empty());
    }

    // Shapes: one row, two partial rows, and partial rows around a block of full ones.
    TEST(Selection, RunShapes) {
        const World world(10'000, {.width = 1, .height = 1});
        const std::uint64_t w = world.rowWidth();
        EXPECT_EQ(shapeOf(world, {0, 0}).count, 0U);

        const RunShape inRow = shapeOf(world, {w + 3, 4});
        ASSERT_EQ(inRow.count, 1U);
        EXPECT_EQ(inRow.rects[0].x, 3U);
        EXPECT_EQ(inRow.rects[0].y, 1U);
        EXPECT_EQ(inRow.rects[0].width, 4U);
        EXPECT_EQ(inRow.rects[0].height, 1U);

        const RunShape wraps = shapeOf(world, {w - 2, 5});
        ASSERT_EQ(wraps.count, 2U);
        EXPECT_EQ(wraps.rects[0].x, w - 2);
        EXPECT_EQ(wraps.rects[0].width, 2U);
        EXPECT_EQ(wraps.rects[1].y, 1U);
        EXPECT_EQ(wraps.rects[1].width, 3U);

        const RunShape block = shapeOf(world, {w - 1, 3 * w + 2});
        ASSERT_EQ(block.count, 3U);
        EXPECT_EQ(block.rects[1].y, 1U);
        EXPECT_EQ(block.rects[1].height, 3U);
        EXPECT_EQ(block.rects[1].width, w);
        EXPECT_EQ(block.rects[2].y, 4U);
        EXPECT_EQ(block.rects[2].width, 1U);
    }

    // The clusters of a set of sorted runs, one flag per cluster.
    std::vector<bool> clusterFlags(const World& world, const std::vector<ClusterRun>& runs) {
        std::vector<bool> selected(world.clusters(), false);
        for (const ClusterRun& run : runs) {
            for (Lcn lcn = run.start; lcn < run.end(); ++lcn) {
                selected[lcn] = true;
            }
        }
        return selected;
    }

    // Whether a tile pixel should be highlighted, by checking every cell it covers.
    bool expectedHighlight(const World& world, TileId tile, std::uint64_t px, std::uint64_t py,
                           const std::vector<bool>& selected) {
        const CellRect rect = world.tileCells(tile);
        CellRect cells{};
        if (tile.level >= 0) {
            const std::uint64_t span = std::uint64_t{1} << static_cast<unsigned>(tile.level);
            cells = {rect.x + px * span, rect.y + py * span, span, span};
        }
        else {
            const std::uint64_t cellPixels = std::uint64_t{1} << static_cast<unsigned>(-tile.level);
            cells = {rect.x + px / cellPixels, rect.y + py / cellPixels, 1, 1};
        }
        for (std::uint64_t y = cells.y; y < cells.y + cells.height; ++y) {
            for (std::uint64_t x = cells.x; x < cells.x + cells.width; ++x) {
                const auto lcn = world.lcnAt({x, y});
                if (lcn && selected[*lcn]) {
                    return true;
                }
            }
        }
        return false;
    }

    // Random disjoint runs, sorted, some long enough to wrap over several rows.
    std::vector<ClusterRun> randomRuns(const World& world, std::mt19937& random) {
        std::vector<ClusterRun> runs;
        std::uniform_int_distribution<std::uint64_t> gap(0, world.rowWidth() * 2);
        std::uniform_int_distribution<std::uint64_t> length(1, world.rowWidth() * 3);
        for (Lcn at = gap(random); at < world.clusters();) {
            const ClusterCount count =
                std::min(length(random) / (runs.size() % 3 == 0 ? 1 : 16) + 1, world.clusters() - at);
            runs.push_back({at, count});
            at += count + 1 + gap(random);
        }
        return runs;
    }

    // At every level, a pixel is highlighted exactly when one of the cells it covers is selected, and runsTouch()
    // says which tiles hold any.
    TEST(Selection, HighlightMatchesBruteForce) {
        const World world(5'000, {.width = 16, .height = 9});
        std::mt19937 random(7);
        const std::vector<ClusterRun> runs = randomRuns(world, random);
        const std::vector<bool> selected = clusterFlags(world, runs);
        constexpr Rgba base = rgb(1, 2, 3);
        constexpr Rgba mark = rgb(255, 196, 0);
        for (int level = World::minLevel; level <= world.maxLevel(); ++level) {
            for (std::uint64_t ty = 0; ty < world.tilesDown(level); ++ty) {
                for (std::uint64_t tx = 0; tx < world.tilesAcross(level); ++tx) {
                    const TileId tile{level, tx, ty};
                    std::vector<Rgba> pixels(tilePixels, base);
                    highlightRuns(world, tile, runs, mark, pixels);
                    bool any = false;
                    for (std::uint64_t py = 0; py < World::tileSize; ++py) {
                        for (std::uint64_t px = 0; px < World::tileSize; ++px) {
                            const bool expected = expectedHighlight(world, tile, px, py, selected);
                            any = any || expected;
                            ASSERT_EQ(pixels[py * World::tileSize + px] == mark, expected)
                                << "level " << level << " tile " << tx << "," << ty << " pixel " << px << "," << py;
                        }
                    }
                    EXPECT_EQ(runsTouch(world, runs, world.tileCells(tile)), any) << level << " " << tx << "," << ty;
                }
            }
        }
    }

    // Tiles of a level to check: all of them on small levels, else a random sample plus every tile touched by
    // the given runs.
    std::vector<TileId> tilesToCheck(const World& world, int level, const std::vector<ClusterRun>& runs,
                                     std::mt19937& random) {
        std::vector<TileId> tiles;
        const std::uint64_t across = world.tilesAcross(level);
        const std::uint64_t down = world.tilesDown(level);
        const bool all = across * down <= 400;
        std::bernoulli_distribution sample(all ? 1.0 : 300.0 / static_cast<double>(across * down));
        for (std::uint64_t y = 0; y < down; ++y) {
            for (std::uint64_t x = 0; x < across; ++x) {
                const TileId tile{level, x, y};
                if (sample(random) || runsTouch(world, runs, world.tileCells(tile))) {
                    tiles.push_back(tile);
                }
            }
        }
        return tiles;
    }

    // The canvas refreshes a tile on a selection change when the old or the new selection touches it. Rendered
    // tiles do not depend on the selection (it is painted on top), so a tile's pixels change exactly when the
    // highlights differ there: the refreshed tiles are precisely the ones that change, on a synthetic volume at
    // every level, for switching files and for clearing the selection.
    TEST(Selection, OnlyTouchedTilesChange) {
        SynthOptions options;
        options.seed = 21;
        options.files = 1500;
        options.geometry.totalClusters = 1ULL << 18U;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        std::vector<FileIndex> fragmented;
        for (FileIndex file = 0; file < snapshot.files.size() && fragmented.size() < 2; ++file) {
            if (snapshot.extents.fragmentCount(file) >= 3) {
                fragmented.push_back(file);
            }
        }
        ASSERT_EQ(fragmented.size(), 2U);
        const SelectionOverlay first = selectionOf(snapshot.extents, fragmented[0]);
        const SelectionOverlay second = selectionOf(snapshot.extents, fragmented[1]);
        const std::vector<std::pair<std::vector<ClusterRun>, std::vector<ClusterRun>>> changes{
            {first.runs, second.runs}, {second.runs, {}}};
        std::mt19937 random(5);
        constexpr Rgba base = rgb(1, 2, 3);
        constexpr Rgba mark = rgb(255, 196, 0);
        for (const auto& [before, after] : changes) {
            std::vector<ClusterRun> both = before;
            both.insert(both.end(), after.begin(), after.end());
            for (int level = World::minLevel; level <= world.maxLevel(); ++level) {
                for (const TileId tile : tilesToCheck(world, level, both, random)) {
                    std::vector<Rgba> old(tilePixels, base);
                    std::vector<Rgba> now(tilePixels, base);
                    highlightRuns(world, tile, before, mark, old);
                    highlightRuns(world, tile, after, mark, now);
                    const CellRect cells = world.tileCells(tile);
                    const bool refreshed = runsTouch(world, before, cells) || runsTouch(world, after, cells);
                    EXPECT_EQ(refreshed, old != now) << "level " << level << " tile " << tile.x << "," << tile.y;
                }
            }
        }
    }

    // Focusing centers the fragment and zooms so it fills half the window, but never closer than 8 px per
    // cluster; a run over several rows shows whole rows across most of the width.
    TEST(Selection, FocusView) {
        const World world(1'000'000, {.width = 16, .height = 9});
        const Viewport view{.scale = 100.0, .center = {0, 0}, .size = {800, 600}};
        const ViewLimits limits = limitsFor(world, view.size);
        const std::uint64_t w = world.rowWidth();

        const Viewport small = focusView(world, view, {5 * w + 10, 4}, limits);
        EXPECT_DOUBLE_EQ(small.scale, 1.0 / 8.0);
        EXPECT_DOUBLE_EQ(small.center.x, 12.0);
        EXPECT_DOUBLE_EQ(small.center.y, 5.5);
        EXPECT_EQ(small.size.x, view.size.x);

        const Viewport wide = focusView(world, view, {7 * w, 800}, limits);
        EXPECT_DOUBLE_EQ(wide.scale, 2.0); // 800 cells over 400 px.
        EXPECT_DOUBLE_EQ(wide.center.x, 400.0);

        const Viewport rows = focusView(world, view, {10 * w + 5, 2 * w}, limits);
        EXPECT_DOUBLE_EQ(rows.scale, static_cast<double>(w) / 720.0); // Whole rows fill 90% of the width.
        EXPECT_DOUBLE_EQ(rows.center.x, static_cast<double>(w) / 2.0);
        EXPECT_DOUBLE_EQ(rows.center.y, 10.0 + 1.5); // Rows 10 to 12.

        const Viewport none = focusView(world, view, {0, 0}, limits);
        EXPECT_DOUBLE_EQ(none.scale, view.scale);
    }

}} // namespace icd::core
