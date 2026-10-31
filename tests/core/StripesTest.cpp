#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/Stripes.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <random>
#include <vector>

namespace icd::core { namespace {

    // A random volume: alternating free gaps and extents of random files, covering every cluster.
    Snapshot randomVolume(std::uint32_t seed, ClusterCount total, FileIndex files) {
        std::mt19937_64 random(seed);
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = total});
        for (FileIndex file = 0; file < files; ++file) {
            FileEntry entry;
            entry.name = "f";
            builder.addFile(entry);
        }
        std::vector<std::uint64_t> nextVcn(files, 0);
        Lcn lcn = 0;
        while (lcn < total) {
            const ClusterCount gap = std::min<ClusterCount>(random() % 3000, total - lcn);
            builder.addFreeRun({lcn, gap});
            lcn += gap;
            const ClusterCount length = std::min<ClusterCount>(1 + random() % 9000, total - lcn);
            if (length == 0) {
                break;
            }
            ExtentEntry extent;
            extent.file = static_cast<FileIndex>(random() % files);
            extent.run = {lcn, length};
            extent.vcn = nextVcn[extent.file];
            nextVcn[extent.file] += length;
            builder.addExtent(extent);
            lcn += length;
        }
        return std::move(builder).build();
    }

    // Stripes are contiguous, disjoint and cover the volume exactly once, for small and huge volumes.
    TEST(Stripes, LayoutCoversVolumeOnce) {
        for (const ClusterCount total : {ClusterCount{0}, ClusterCount{1}, ClusterCount{4095}, ClusterCount{4096},
                                         ClusterCount{4097}, ClusterCount{1'000'000}, ClusterCount{7'300'000'000}}) {
            const StripeLayout layout(total);
            EXPECT_GE(layout.count(), 1u);
            EXPECT_LE(layout.count(), StripeLayout::defaultStripes);
            EXPECT_EQ(layout.stripeClusters() % StripeLayout::alignment, 0u);
            Lcn expectedStart = 0;
            for (std::size_t stripe = 0; stripe < layout.count(); ++stripe) {
                const ClusterRun range = layout.range(stripe);
                EXPECT_EQ(range.start, expectedStart) << total;
                EXPECT_TRUE(range.length > 0 || total == 0);
                expectedStart = range.end();
                if (range.length > 0) {
                    EXPECT_EQ(layout.stripeOf(range.start), stripe);
                    EXPECT_EQ(layout.stripeOf(range.end() - 1), stripe);
                }
            }
            EXPECT_EQ(expectedStart, total);
        }
        EXPECT_EQ(StripeLayout(7'300'000'000).count(), StripeLayout::defaultStripes); // 30 TB at 4 KB.
    }

    // Clipping keeps only the part inside the range.
    TEST(Stripes, Clip) {
        EXPECT_EQ(clip({10, 10}, {0, 15}), (ClusterRun{10, 5}));
        EXPECT_EQ(clip({10, 10}, {12, 3}), (ClusterRun{12, 3}));
        EXPECT_EQ(clip({10, 10}, {20, 5}).length, 0u);
        EXPECT_EQ(clip({10, 10}, {0, 10}).length, 0u);
    }

    // Views list exactly the overlapping records and runs; clipped, they add up to each stripe's size.
    TEST(Stripes, ViewsCoverEveryClusterOnce) {
        const Snapshot snapshot = randomVolume(1, 3'000'000, 200);
        const Stripes stripes(snapshot, StripeLayout(snapshot.geometry.totalClusters, {.stripes = 64}));
        ASSERT_GT(stripes.count(), 10u);
        ClusterCount used = 0;
        ClusterCount free = 0;
        std::vector<int> seen(snapshot.extents.size(), 0);
        for (std::size_t stripe = 0; stripe < stripes.count(); ++stripe) {
            const StripeView view = stripes.view(stripe);
            ClusterCount inStripe = 0;
            for (const ExtentRecord& record : view.extents) {
                const ClusterCount part = clip(record.run(), view.range).length;
                EXPECT_GT(part, 0u); // Only overlapping records are listed.
                inStripe += part;
                ++seen[static_cast<std::size_t>(&record - snapshot.extents.records().data())];
            }
            for (const ClusterRun& run : view.freeRuns) {
                inStripe += clip(run, view.range).length;
            }
            EXPECT_EQ(inStripe, view.range.length) << stripe; // Used + free = the whole stripe.
            for (const ExtentRecord& record : view.extents) {
                used += clip(record.run(), view.range).length;
            }
            for (const ClusterRun& run : view.freeRuns) {
                free += clip(run, view.range).length;
            }
        }
        EXPECT_EQ(free, snapshot.freeSpace.freeClusters());
        EXPECT_EQ(used + free, snapshot.geometry.totalClusters);
        for (std::size_t i = 0; i < seen.size(); ++i) {
            const ExtentRecord& record = snapshot.extents.records()[i];
            const auto expected =
                stripes.layout().stripeOf(record.end() - 1) - stripes.layout().stripeOf(record.lcn()) + 1;
            EXPECT_EQ(static_cast<std::size_t>(seen[i]), expected); // In every stripe it touches, nowhere else.
        }
    }

    // Per-stripe statistics computed on a pool equal the serial ones for any thread count.
    TEST(Stripes, ParallelEqualsSerial) {
        const Snapshot snapshot = randomVolume(2, 5'000'000, 500);
        const Stripes stripes(snapshot, StripeLayout(snapshot.geometry.totalClusters));
        // Used clusters and a floating-point score per stripe; the score makes the order of summing matter.
        const auto stats = [](const StripeView& view) {
            ClusterCount used = 0;
            double score = 0.0;
            for (const ExtentRecord& record : view.extents) {
                const ClusterCount part = clip(record.run(), view.range).length;
                used += part;
                score += std::sqrt(static_cast<double>(part)) / (1.0 + record.file());
            }
            return std::pair(used, score);
        };
        const auto sumScores = [&](Executor& executor) {
            return reduceStripes(
                executor, stripes, 0.0, [&](const StripeView& view) { return stats(view).second; },
                [](double a, double b) { return a + b; });
        };
        InlineExecutor serial;
        const auto expectedStats = mapStripes(serial, stripes, stats);
        const double expectedScore = sumScores(serial);
        for (const std::size_t threads : {2U, 5U, 16U}) {
            services::ThreadPool pool(threads);
            EXPECT_EQ(mapStripes(pool, stripes, stats), expectedStats) << threads;
            EXPECT_EQ(sumScores(pool), expectedScore) << threads; // Bit-identical, not just close.
        }
    }

}} // namespace icd::core
