#include "core/map/TileRasterizer.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>

namespace icd::core { namespace {

    // FNV-1a over a tile's pixels, for golden checks.
    std::uint64_t checksum(const std::vector<Rgba>& pixels) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const Rgba pixel : pixels) {
            hash = (hash ^ pixel) * 1099511628211ULL;
        }
        return hash;
    }

    // An 8 x 8 world with 60 clusters: a contiguous file at 0-3, a fragmented one at 10-11 and 20-21, an
    // unmovable one at 30, the rest free; cells 60-63 lie past the end.
    Snapshot handVolume() {
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 60});
        const auto addFile = [&](std::uint16_t flags) {
            FileEntry entry;
            entry.name = "f";
            entry.flags = flags;
            return builder.addFile(entry);
        };
        const auto addRun = [&](FileIndex file, ClusterRun run, std::uint64_t vcn) {
            ExtentEntry extent;
            extent.file = file;
            extent.run = run;
            extent.vcn = vcn;
            builder.addExtent(extent);
        };
        const FileIndex whole = addFile(0);
        const FileIndex split = addFile(0);
        const FileIndex pinned = addFile(FileUnmovable);
        addRun(whole, {0, 4}, 0);
        addRun(split, {10, 2}, 0);
        addRun(split, {20, 2}, 2);
        addRun(pinned, {30, 1}, 0);
        return std::move(builder).build();
    }

    // Clusters in a rectangle stop at the world edge and at the last cluster.
    TEST(TileRasterizer, ClustersInRect) {
        const World world(60, {.width = 1, .height = 1});
        ASSERT_EQ(world.rowWidth(), 8u);
        EXPECT_EQ(clustersInRect(world, {0, 0, 8, 8}), 60u);
        EXPECT_EQ(clustersInRect(world, {0, 0, 100, 100}), 60u);
        EXPECT_EQ(clustersInRect(world, {0, 7, 8, 1}), 4u);
        EXPECT_EQ(clustersInRect(world, {4, 7, 4, 1}), 0u);
        EXPECT_EQ(clustersInRect(world, {2, 6, 4, 2}), 6u);
        EXPECT_EQ(clustersInRect(world, {8, 0, 4, 4}), 0u);
    }

    // At 32 px per cell every cell shows its own state over its whole square (no grid lines: the canvas draws
    // those), and the area past the end is outside.
    TEST(TileRasterizer, DeepZoomCells) {
        const Snapshot snapshot = handVolume();
        const World world(60, {.width = 1, .height = 1});
        InlineExecutor serial;
        const auto extentFlags = extentMapFlags(snapshot, serial);
        const Pyramid pyramid = Pyramid::build({.snapshot = snapshot, .world = world, .maxBaseBins = 4}, serial);
        const MapSource source{
            .world = world, .pyramid = pyramid, .facts = {.snapshot = snapshot, .extentFlags = extentFlags}};
        const OccupancyPalette palette;
        const auto pixels = renderTile(source, {World::minLevel, 0, 0}, palette);
        // The center pixel of cell (x, y).
        const auto cell = [&](std::uint64_t x, std::uint64_t y) { return pixels[(y * 32 + 5) * 256 + x * 32 + 5]; };
        EXPECT_EQ(cell(0, 0), OccupancyPalette::usedColor);
        EXPECT_EQ(cell(3, 0), OccupancyPalette::usedColor);
        EXPECT_EQ(cell(4, 0), OccupancyPalette::freeColor);
        EXPECT_EQ(cell(2, 1), OccupancyPalette::fragmentedColor); // LCN 10.
        EXPECT_EQ(cell(4, 2), OccupancyPalette::fragmentedColor); // LCN 20.
        EXPECT_EQ(cell(6, 3), OccupancyPalette::unmovableColor); // LCN 30.
        EXPECT_EQ(cell(3, 7), OccupancyPalette::freeColor); // LCN 59, the last cluster.
        EXPECT_EQ(cell(4, 7), palette.outside()); // LCN 60 does not exist.
        EXPECT_EQ(pixels[5 * 256 + 31], OccupancyPalette::usedColor); // Right edge of cell (0, 0).
        EXPECT_EQ(pixels[31 * 256 + 5], OccupancyPalette::usedColor); // Bottom edge of cell (0, 0).
        EXPECT_EQ(pixels[(7 * 32 + 5) * 256 + 4 * 32 + 31], palette.outside()); // Past the end.
    }

    // At the base level, tiles from the pyramid and from the extent index are pixel for pixel the same.
    TEST(TileRasterizer, PyramidMatchesExtents) {
        SynthOptions options;
        options.seed = 9;
        options.files = 4000;
        options.geometry.totalClusters = 1ULL << 22U;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        InlineExecutor serial;
        const auto extentFlags = extentMapFlags(snapshot, serial);
        const Pyramid pyramid =
            Pyramid::build({.snapshot = snapshot, .world = world, .maxBaseBins = 1U << 14U}, serial);
        const MapSource source{
            .world = world, .pyramid = pyramid, .facts = {.snapshot = snapshot, .extentFlags = extentFlags}};
        const OccupancyPalette palette;
        const int level = pyramid.baseLevel();
        ASSERT_GT(level, 0);
        for (std::uint64_t y = 0; y < world.tilesDown(level); ++y) {
            for (std::uint64_t x = 0; x < world.tilesAcross(level); ++x) {
                const TileId tile{level, x, y};
                EXPECT_EQ(renderTile(source, tile, palette, TileSource::Pyramid),
                          renderTile(source, tile, palette, TileSource::Extents))
                    << x << " " << y;
            }
        }
    }

    // Golden checksums: a coarse and a fine tile of a fixed synthetic volume never change by accident.
    TEST(TileRasterizer, GoldenTiles) {
        SynthOptions options;
        options.seed = 11;
        options.files = 4000;
        options.geometry.totalClusters = 1ULL << 22U;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        InlineExecutor serial;
        const auto extentFlags = extentMapFlags(snapshot, serial);
        const Pyramid pyramid =
            Pyramid::build({.snapshot = snapshot, .world = world, .maxBaseBins = 1U << 14U}, serial);
        const MapSource source{
            .world = world, .pyramid = pyramid, .facts = {.snapshot = snapshot, .extentFlags = extentFlags}};
        const OccupancyPalette palette;
        const std::uint64_t coarse = checksum(renderTile(source, {world.maxLevel(), 0, 0}, palette));
        const std::uint64_t fine = checksum(renderTile(source, {-2, 3, 1}, palette));
        EXPECT_EQ(coarse, 0xa10d1818f4c65a74ULL) << std::hex << "coarse 0x" << coarse;
        EXPECT_EQ(fine, 0xa391484b50ce8525ULL) << std::hex << "fine 0x" << fine;
    }

    // The record block index points at the first record ending after each block start, and fine tiles drawn with it
    // equal tiles drawn without it at every level below the pyramid, also with per-record color values.
    TEST(TileRasterizer, RecordBlocksMatchSearch) {
        SynthOptions options;
        options.seed = 13;
        options.files = 4000;
        options.geometry.totalClusters = (1ULL << 22U) + 1000; // A partial last block.
        const Snapshot snapshot = generateSyntheticVolume(options);
        InlineExecutor serial;
        const std::vector<std::uint32_t> blocks = recordBlocks(snapshot, serial);
        const auto records = snapshot.extents.records();
        ASSERT_EQ(blocks.size(), (snapshot.geometry.totalClusters >> recordBlockShift) + 1);
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            const Lcn start = Lcn{block} << recordBlockShift;
            const auto first =
                std::ranges::partition_point(records, [start](const ExtentRecord& r) { return r.end() <= start; });
            ASSERT_EQ(blocks[block], static_cast<std::size_t>(first - records.begin())) << block;
        }

        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        const auto extentFlags = extentMapFlags(snapshot, serial);
        const Pyramid pyramid =
            Pyramid::build({.snapshot = snapshot, .world = world, .maxBaseBins = 1U << 14U}, serial);
        std::vector<std::uint8_t> values(records.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = packExtentValues(index % 6, (index / 7) % 6);
        }
        const MapSource plain{.world = world,
                              .pyramid = pyramid,
                              .facts = {.snapshot = snapshot, .extentFlags = extentFlags, .extentValues = values}};
        MapSource indexed = plain;
        indexed.facts.recordBlocks = blocks;
        const OccupancyPalette palette;
        for (int level = pyramid.baseLevel() - 1; level >= World::minLevel; --level) {
            for (const TileId tile : {TileId{level, 0, 0}, TileId{level, 1, 0}, TileId{level, 0, 1}}) {
                EXPECT_EQ(renderTile(plain, tile, palette), renderTile(indexed, tile, palette)) << level;
            }
        }
    }

}} // namespace icd::core
