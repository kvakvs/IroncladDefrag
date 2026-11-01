// Reconciling a live scan (T047): overlapping extents keep the newer read, extents take clusters out of stale free
// runs, and every cluster nobody claims goes to the placeholder, so extents and free space tile the volume.

#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/ScanReconcile.h"
#include "core/synth/SynthRandom.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {

    using icd::core::ClusterRun;
    using icd::core::ExtentEntry;
    using icd::core::reconcileScan;

    constexpr icd::core::FileIndex placeholder = 999;

    // An extent of a file.
    ExtentEntry extent(icd::core::Lcn start, icd::core::ClusterCount length, icd::core::FileIndex file) {
        return {{start, length}, 0, file, 0, 0};
    }

    // Checks that extents (sorted) and free runs (sorted) are disjoint and cover [0, total) exactly.
    void expectTiling(const std::vector<ExtentEntry>& extents, const std::vector<ClusterRun>& freeRuns,
                      icd::core::ClusterCount total) {
        std::vector<ClusterRun> all;
        for (const auto& e : extents) {
            all.push_back(e.run);
        }
        all.insert(all.end(), freeRuns.begin(), freeRuns.end());
        std::ranges::sort(all, {}, &ClusterRun::start);
        icd::core::Lcn pos = 0;
        for (const ClusterRun& run : all) {
            ASSERT_EQ(run.start, pos);
            ASSERT_GT(run.length, 0U);
            pos = run.end();
        }
        EXPECT_EQ(pos, total);
    }

    TEST(ScanReconcile, ConsistentInputOnlyGetsThePlaceholder) {
        std::vector<ExtentEntry> extents{extent(10, 5, 1), extent(0, 4, 0)};
        std::vector<ClusterRun> freeRuns{{4, 6}, {20, 80}};
        const auto stats = reconcileScan(extents, freeRuns, 100, placeholder);
        EXPECT_EQ(stats.droppedExtents, 0U);
        EXPECT_EQ(stats.freeTaken, 0U);
        EXPECT_EQ(stats.unattributed, 5U); // [15, 20): used but nobody's.
        EXPECT_EQ(stats.unattributedRuns, 1U);
        ASSERT_EQ(extents.size(), 3U);
        EXPECT_EQ(extents[2].file, placeholder);
        EXPECT_EQ(extents[2].run, (ClusterRun{15, 5}));
        EXPECT_NE(extents[2].flags & icd::core::ExtentUnmovable, 0);
        expectTiling(extents, freeRuns, 100);
    }

    TEST(ScanReconcile, NewerReadWinsAndStaleFreeSpaceShrinks) {
        // File 1 was read first and still claims [10, 20); file 7, read later, now owns [15, 25).
        std::vector<ExtentEntry> extents{extent(10, 10, 1), extent(15, 10, 7), extent(40, 10, 3)};
        // The bitmap, read before the files, still had [40, 60) free.
        std::vector<ClusterRun> freeRuns{{0, 10}, {40, 20}};
        const auto stats = reconcileScan(extents, freeRuns, 60, placeholder);
        EXPECT_EQ(stats.droppedExtents, 1U);
        EXPECT_EQ(stats.freeTaken, 10U);
        ASSERT_EQ(freeRuns.size(), 2U);
        EXPECT_EQ(freeRuns[1], (ClusterRun{50, 10}));
        EXPECT_TRUE(std::ranges::none_of(extents, [](const ExtentEntry& e) { return e.file == 1; }));
        expectTiling(extents, freeRuns, 60);
    }

    TEST(ScanReconcile, ClipsRunsPastTheVolume) {
        std::vector<ExtentEntry> extents{extent(90, 20, 0), extent(200, 5, 1), extent(5, 0, 2)};
        std::vector<ClusterRun> freeRuns{{0, 90}};
        reconcileScan(extents, freeRuns, 100, placeholder);
        ASSERT_EQ(extents.size(), 1U);
        EXPECT_EQ(extents[0].run, (ClusterRun{90, 10}));
        expectTiling(extents, freeRuns, 100);
    }

    TEST(ScanReconcile, RandomScansAlwaysTile) {
        icd::core::SynthRandom rng(5);
        for (int round = 0; round < 300; ++round) {
            const icd::core::ClusterCount total = 50 + rng.below(2000);
            std::vector<ExtentEntry> extents;
            for (std::uint64_t i = 0, n = rng.below(60); i < n; ++i) {
                const auto start = rng.below(total + 20);
                extents.push_back(extent(start, rng.below(80), static_cast<icd::core::FileIndex>(rng.below(30))));
            }
            std::vector<ClusterRun> freeRuns;
            for (icd::core::Lcn pos = rng.below(10); pos < total;) {
                const auto length = std::min<icd::core::ClusterCount>(1 + rng.below(100), total - pos);
                freeRuns.push_back({pos, length});
                pos += length + 1 + rng.below(100);
            }
            reconcileScan(extents, freeRuns, total, placeholder);
            expectTiling(extents, freeRuns, total);
            ASSERT_FALSE(HasFatalFailure()) << "round " << round;
        }
    }

} // namespace
