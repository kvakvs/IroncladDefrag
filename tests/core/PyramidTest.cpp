#include "core/map/Pyramid.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/Stripes.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

namespace icd::core { namespace {

    // A small synthetic volume: 4 GB at 4 KB, scattered free space.
    Snapshot smallVolume(std::uint64_t seed) {
        SynthOptions options;
        options.seed = seed;
        options.files = 3000;
        options.geometry.totalClusters = 1ULL << 20U;
        options.freePattern = FreePattern::Scattered;
        return generateSyntheticVolume(options);
    }

    // A made-up heat bucket per file.
    std::vector<std::uint8_t> heatOf(const Snapshot& snapshot) {
        std::vector<std::uint8_t> heat(snapshot.files.size());
        for (std::size_t file = 0; file < heat.size(); ++file) {
            heat[file] = static_cast<std::uint8_t>((file * 37) % 256);
        }
        return heat;
    }

    // A bin computed straight from the extents.
    struct Expected {
        ClusterCount used = 0; // Used clusters.
        double fragmented = 0.0; // 0-255 share of clusters in fragmented files.
        double heat = 0.0; // Used-weighted heat.
        std::uint8_t flags = 0; // MapBinFlag bits.
    };

    // Adds one LCN range's extents to expected totals.
    void addRange(const Snapshot& snapshot, const std::vector<std::uint8_t>& heat, ClusterRun range, Expected& sums,
                  double& fragmented, double& heatSum) {
        for (const ExtentRecord& record : snapshot.extents.overlapping(range)) {
            const ClusterCount part = clip(record.run(), range).length;
            sums.used += part;
            fragmented += snapshot.extents.fragmentCount(record.file()) > 1 ? static_cast<double>(part) : 0.0;
            heatSum += static_cast<double>(part) * heat[record.file()];
            const bool unmovable =
                (record.flags() & ExtentUnmovable) != 0 || snapshot.files.has(record.file(), FileUnmovable);
            sums.flags |= unmovable ? BinUnmovable : 0;
            sums.flags |= (record.flags() & ExtentDirectoryIndex) != 0 ? BinDirectoryIndex : 0;
        }
    }

    // Brute force for the bin at (x, y) of a level: every world row of its cell square, one range each.
    Expected bruteForce(const Snapshot& snapshot, const World& world, const std::vector<std::uint8_t>& heat, int level,
                        std::uint64_t x, std::uint64_t y) {
        const std::uint64_t span = std::uint64_t{1} << static_cast<unsigned>(level);
        const std::uint64_t columns = std::min(world.rowWidth(), (x + 1) * span) - x * span;
        Expected expected;
        double fragmented = 0.0;
        double heatSum = 0.0;
        for (std::uint64_t row = y * span; row < std::min(world.rows(), (y + 1) * span); ++row) {
            const ClusterRun cells{row * world.rowWidth() + x * span, columns};
            addRange(snapshot, heat, clip(cells, {0, world.clusters()}), expected, fragmented, heatSum);
        }
        if (expected.used > 0) {
            expected.fragmented = 255.0 * fragmented / static_cast<double>(expected.used);
            expected.heat = heatSum / static_cast<double>(expected.used);
        }
        return expected;
    }

    // The base level is the first with at most the allowed bins: 16 x 16 cells (256 clusters) at 30 TB.
    TEST(Pyramid, BaseLevelSelection) {
        EXPECT_EQ(Pyramid::baseLevelFor(World(1000, {}), 1U << 25U), 0);
        EXPECT_EQ(Pyramid::baseLevelFor(World(7'300'000'000, {.width = 16, .height = 9}), 1U << 25U), 4);
        EXPECT_EQ(Pyramid::baseLevelFor(World(1ULL << 20U, {.width = 1, .height = 1}), 64), 7); // 8 x 8 bins.
        EXPECT_EQ(Pyramid::usedShift(11), 0u); // 2^22 clusters fit the 24-bit count.
        EXPECT_EQ(Pyramid::usedShift(12), 1u);
        EXPECT_EQ(Pyramid::usedShift(17), 11u);
    }

    // Every bin of every level matches brute force: counts and flags exactly, averages within rounding.
    TEST(Pyramid, MatchesBruteForce) {
        const Snapshot snapshot = smallVolume(1);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
        const auto heat = heatOf(snapshot);
        InlineExecutor serial;
        const Pyramid pyramid =
            Pyramid::build({.snapshot = snapshot, .world = world, .fileHeat = heat, .maxBaseBins = 1U << 13U}, serial);
        ASSERT_EQ(pyramid.baseLevel(), 4);
        ASSERT_EQ(pyramid.level(pyramid.topLevel()).size(), 1u);
        for (int level = pyramid.baseLevel(); level <= pyramid.topLevel(); ++level) {
            // Each merge rounds once, so averages may drift by half a step per level above the base.
            const double tolerance = 0.5 + 0.5 * (level - pyramid.baseLevel());
            for (std::uint64_t y = 0; y < pyramid.binsDown(level); ++y) {
                for (std::uint64_t x = 0; x < pyramid.binsAcross(level); ++x) {
                    const Expected expected = bruteForce(snapshot, world, heat, level, x, y);
                    const MapBin bin = pyramid.bin({level, x, y});
                    ASSERT_EQ(pyramid.usedClusters({level, x, y}), expected.used) << level << " " << x << " " << y;
                    ASSERT_EQ(bin.flags, expected.flags) << level << " " << x << " " << y;
                    ASSERT_NEAR(bin.fragmented, expected.fragmented, tolerance) << level << " " << x << " " << y;
                    ASSERT_NEAR(bin.heat, expected.heat, tolerance) << level << " " << x << " " << y;
                }
            }
        }
        EXPECT_EQ(pyramid.usedClusters({pyramid.topLevel(), 0, 0}),
                  snapshot.geometry.totalClusters - snapshot.freeSpace.freeClusters());
    }

    // The largest contributor decides a base bin's class; a bin is a square of cells over several rows.
    TEST(Pyramid, DominantKindAndSquareBins) {
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 64}); // 8 x 8 cells.
        for (int file = 0; file < 3; ++file) {
            FileEntry entry;
            entry.name = "f";
            builder.addFile(entry);
        }
        const auto addRun = [&](FileIndex file, ClusterRun run) {
            ExtentEntry extent;
            extent.file = file;
            extent.run = run;
            builder.addExtent(extent);
        };
        addRun(0, {0, 2}); // Row 0, columns 0-1.
        addRun(1, {8, 4}); // Row 1, columns 0-3: largest piece in bin (0, 0).
        addRun(2, {4, 4}); // Row 0, columns 4-7: bin (1, 0).
        const Snapshot snapshot = std::move(builder).build();
        const World world(64, {.width = 1, .height = 1});
        ASSERT_EQ(world.rowWidth(), 8u);
        const std::vector<std::uint8_t> kinds{7, 9, 11};
        InlineExecutor serial;
        const Pyramid pyramid =
            Pyramid::build({.snapshot = snapshot, .world = world, .fileKinds = kinds, .maxBaseBins = 4}, serial);
        ASSERT_EQ(pyramid.baseLevel(), 2); // 2 x 2 bins of 4 x 4 cells.
        EXPECT_EQ(pyramid.bin({2, 0, 0}).kind, 9);
        EXPECT_EQ(pyramid.bin({2, 0, 0}).used, 6u);
        EXPECT_EQ(pyramid.bin({2, 1, 0}).used, 4u);
        EXPECT_EQ(pyramid.bin({2, 1, 0}).kind, 11);
        EXPECT_EQ(pyramid.bin({2, 0, 1}).used, 0u);
        EXPECT_EQ(pyramid.bin({3, 0, 0}).used, 10u);
        EXPECT_EQ(pyramid.bin({3, 0, 0}).kind, 9);
    }

    // A pool of any size builds exactly the same bins as the inline executor.
    TEST(Pyramid, ParallelEqualsSerial) {
        const Snapshot snapshot = smallVolume(2);
        const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 10});
        const auto heat = heatOf(snapshot);
        const PyramidInputs inputs{.snapshot = snapshot, .world = world, .fileHeat = heat, .maxBaseBins = 1U << 14U};
        InlineExecutor serial;
        const Pyramid expected = Pyramid::build(inputs, serial);
        for (const std::size_t threads : {3U, 8U}) {
            services::ThreadPool pool(threads);
            const Pyramid actual = Pyramid::build(inputs, pool);
            for (int level = expected.baseLevel(); level <= expected.topLevel(); ++level) {
                EXPECT_TRUE(std::ranges::equal(actual.level(level), expected.level(level))) << threads << " " << level;
            }
        }
    }

    // Copies a snapshot without some files' extents (the files stay, with no clusters).
    Snapshot withoutExtentsOf(const Snapshot& source, const std::set<FileIndex>& dropped) {
        SnapshotBuilder builder(source.geometry);
        for (FileIndex file = 0; file < source.files.size(); ++file) {
            FileEntry entry;
            entry.name = source.names.name(source.files.name(file));
            entry.parent = source.files.parent(file);
            entry.flags = source.files.flags(file);
            builder.addFile(entry);
            if (dropped.contains(file)) {
                continue;
            }
            std::uint64_t vcn = 0;
            for (const std::uint32_t position : source.extents.extentsOf(file)) {
                const ExtentRecord& record = source.extents.records()[position];
                ExtentEntry extent;
                extent.file = file;
                extent.run = record.run();
                extent.vcn = vcn;
                extent.stream = record.stream();
                extent.flags = record.flags();
                builder.addExtent(extent);
                vcn += record.length();
            }
        }
        return std::move(builder).build();
    }

    // After files inside a range are deleted, updating that range gives the same pyramid as a full rebuild.
    TEST(Pyramid, DirtyRangeUpdate) {
        const Snapshot before = smallVolume(3);
        const ClusterCount total = before.geometry.totalClusters;
        const World world(total, {.width = 16, .height = 9});
        const ClusterRun dirty{total / 4 + 777, total / 4};
        std::set<FileIndex> dropped;
        for (FileIndex file = 0; file < before.files.size(); ++file) {
            const auto extents = before.extents.extentsOf(file);
            const bool inside = !extents.empty() && std::ranges::all_of(extents, [&](std::uint32_t position) {
                const ClusterRun run = before.extents.records()[position].run();
                return run.start >= dirty.start && run.end() <= dirty.end();
            });
            if (inside && file % 2 == 0) {
                dropped.insert(file);
            }
        }
        ASSERT_GT(dropped.size(), 50u);
        const Snapshot after = withoutExtentsOf(before, dropped);
        const auto heat = heatOf(before);
        InlineExecutor serial;
        Pyramid pyramid =
            Pyramid::build({.snapshot = before, .world = world, .fileHeat = heat, .maxBaseBins = 1U << 14U}, serial);
        const PyramidInputs afterInputs{.snapshot = after, .world = world, .fileHeat = heat, .maxBaseBins = 1U << 14U};
        pyramid.update(afterInputs, dirty, serial);
        const Pyramid rebuilt = Pyramid::build(afterInputs, serial);
        for (int level = rebuilt.baseLevel(); level <= rebuilt.topLevel(); ++level) {
            EXPECT_TRUE(std::ranges::equal(pyramid.level(level), rebuilt.level(level))) << level;
        }
        EXPECT_LT(rebuilt.usedClusters({rebuilt.topLevel(), 0, 0}), total - before.freeSpace.freeClusters());
    }

}} // namespace icd::core
