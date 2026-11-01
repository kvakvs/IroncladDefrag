// Snapshot statistics (T048): fragmentation and free-space figures on a hand-made volume, and parallel = serial on a
// synthetic one.

#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/SnapshotStats.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

namespace {

    using namespace icd::core;

    TEST(SnapshotStats, HandMadeVolume) {
        // 1 MB clusters, so a 64 MB free run is 64 clusters.
        SnapshotBuilder builder({1U << 20U, 1000});
        const FileIndex root = builder.addFile({"", noFile, 0, FileDirectory, {}, 0});
        const FileIndex whole = builder.addFile({"whole.bin", root, 0, 0, {}, 0});
        const FileIndex split = builder.addFile({"split.bin", root, 0, 0, {}, 0});
        builder.addFile({"tiny.txt", root, 10, FileResident, {}, 0});
        const FileIndex meta = builder.addFile({"$MFT", root, 0, FileMetadata | FileUnmovable, {}, 0});
        builder.addExtent({{0, 10}, 0, whole, 0, 0});
        builder.addExtent({{20, 5}, 0, split, 0, 0});
        builder.addExtent({{40, 5}, 5, split, 0, 0});
        builder.addExtent({{50, 3}, 0, meta, 0, ExtentUnmovable});
        builder.addFreeRun({100, 100}); // Large run.
        builder.addFreeRun({300, 10}); // Small run.
        const Snapshot snapshot = std::move(builder).build();
        InlineExecutor inline_;
        const VolumeStats stats = summarize(snapshot, inline_);
        EXPECT_EQ(stats.files, 3U); // whole, split, tiny; not the root, not $MFT.
        EXPECT_EQ(stats.directories, 1U);
        EXPECT_EQ(stats.filesWithClusters, 2U);
        EXPECT_EQ(stats.fragmentedFiles, 1U);
        EXPECT_EQ(stats.extraFragments, 1U);
        EXPECT_DOUBLE_EQ(stats.fragmentedFilePercent(), 50.0);
        EXPECT_DOUBLE_EQ(stats.fragmentedClusterPercent(), 50.0); // 10 of 20 file clusters.
        EXPECT_EQ(stats.unmovableClusters, 3U);
        EXPECT_EQ(stats.freeClusters, 110U);
        EXPECT_EQ(stats.freeRuns, 2U);
        EXPECT_EQ(stats.largestFreeRun, 100U);
        EXPECT_EQ(stats.freeInLargeRuns, 100U);
    }

    TEST(SnapshotStats, ParallelEqualsSerial) {
        SynthOptions options;
        options.geometry.totalClusters = 1ULL << 22U;
        options.files = 60'000;
        options.fragmentedShare = 0.3;
        const Snapshot snapshot = generateSyntheticVolume(options);
        InlineExecutor inline_;
        icd::services::ThreadPool pool(6);
        const VolumeStats serial = summarize(snapshot, inline_);
        const VolumeStats parallel = summarize(snapshot, pool);
        EXPECT_EQ(serial.fragmentedFiles, parallel.fragmentedFiles);
        EXPECT_EQ(serial.extraFragments, parallel.extraFragments);
        EXPECT_EQ(serial.fileClusters, parallel.fileClusters);
        EXPECT_EQ(serial.files, parallel.files);
        EXPECT_GT(serial.fragmentedFiles, 0U);
        EXPECT_EQ(serial.freeClusters, snapshot.freeSpace.freeClusters());
    }

} // namespace
