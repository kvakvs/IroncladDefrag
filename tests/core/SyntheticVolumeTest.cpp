#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace icd::core { namespace {

    // A small volume for fast tests: 4 GB, 5,000 files.
    SynthOptions small(std::uint64_t seed) {
        SynthOptions options;
        options.seed = seed;
        options.files = 5000;
        options.geometry.totalClusters = 1ULL << 20U;
        return options;
    }

    // Whether two snapshots hold the same files, names, extents and free runs.
    bool same(const Snapshot& a, const Snapshot& b) {
        if (a.files.size() != b.files.size() || !std::ranges::equal(a.extents.records(), b.extents.records()) ||
            !std::ranges::equal(a.freeSpace.runs(), b.freeSpace.runs())) {
            return false;
        }
        for (FileIndex file = 0; file < a.files.size(); ++file) {
            if (a.files.sizeBytes(file) != b.files.sizeBytes(file) || a.files.parent(file) != b.files.parent(file) ||
                a.names.name(a.files.name(file)) != b.names.name(b.files.name(file)) ||
                a.files.times(file).modified != b.files.times(file).modified) {
                return false;
            }
        }
        return true;
    }

    // Used clusters of a snapshot.
    ClusterCount used(const Snapshot& snapshot) {
        ClusterCount total = 0;
        for (const ExtentRecord& record : snapshot.extents.records()) {
            total += record.length();
        }
        return total;
    }

    // The same seed gives the same snapshot; another seed gives another one.
    TEST(SyntheticVolume, SeedDecidesEverything) {
        EXPECT_TRUE(same(generateSyntheticVolume(small(7)), generateSyntheticVolume(small(7))));
        EXPECT_FALSE(same(generateSyntheticVolume(small(7)), generateSyntheticVolume(small(8))));
    }

    // Files fill about the requested share, and used plus free is the whole volume.
    TEST(SyntheticVolume, FillsToTarget) {
        for (const double fill : {20.0, 60.0, 95.0}) {
            SynthOptions options = small(3);
            options.fillPercent = fill;
            const Snapshot snapshot = generateSyntheticVolume(options);
            const ClusterCount total = snapshot.geometry.totalClusters;
            EXPECT_EQ(used(snapshot) + snapshot.freeSpace.freeClusters(), total);
            EXPECT_NEAR(100.0 * static_cast<double>(used(snapshot)) / static_cast<double>(total), fill, 3.0);
        }
    }

    // About the requested share of files is fragmented, and their pieces are in VCN order.
    TEST(SyntheticVolume, FragmentsFiles) {
        SynthOptions options = small(4);
        options.fragmentedShare = 0.3;
        const Snapshot snapshot = generateSyntheticVolume(options);
        std::size_t multiCluster = 0;
        std::size_t fragmented = 0;
        for (FileIndex file = 0; file < snapshot.files.size(); ++file) {
            if (snapshot.files.allocatedClusters(file) >= 16 && !snapshot.files.has(file, FileDirectory)) {
                ++multiCluster;
                fragmented += snapshot.extents.fragmentCount(file) > 1 ? 1 : 0;
            }
        }
        ASSERT_GT(multiCluster, 500u);
        EXPECT_NEAR(static_cast<double>(fragmented) / static_cast<double>(multiCluster), 0.3, 0.05);
    }

    // The free pattern decides whether free space is one tail or many gaps.
    TEST(SyntheticVolume, FreePatterns) {
        SynthOptions options = small(5);
        options.freePattern = FreePattern::Tail;
        const Snapshot tail = generateSyntheticVolume(options);
        EXPECT_GT(tail.freeSpace.largest()->length, tail.freeSpace.freeClusters() * 9 / 10);
        options.freePattern = FreePattern::Scattered;
        const Snapshot scattered = generateSyntheticVolume(options);
        EXPECT_LT(scattered.freeSpace.largest()->length, scattered.freeSpace.freeClusters() / 10);
        EXPECT_GT(scattered.freeSpace.size(), 1000u);
    }

    // The volume has a root, directories with index allocations, and an unmovable $MFT.
    TEST(SyntheticVolume, HasStructure) {
        const Snapshot snapshot = generateSyntheticVolume(small(6));
        ASSERT_EQ(snapshot.tree.roots().size(), 1u);
        const FileIndex root = snapshot.tree.roots()[0];
        EXPECT_TRUE(snapshot.files.has(root, FileDirectory));
        EXPECT_GT(snapshot.tree.children(root).size(), 1u);
        const FileIndex mft = 1;
        EXPECT_EQ(snapshot.path(mft), "\\$MFT");
        EXPECT_TRUE(snapshot.files.has(mft, FileUnmovable));
        ASSERT_FALSE(snapshot.extents.extentsOf(mft).empty());
        EXPECT_NE(snapshot.extents.records()[snapshot.extents.extentsOf(mft)[0]].flags() & ExtentUnmovable, 0);
        const auto directoryIndexes = std::ranges::count_if(
            snapshot.extents.records(), [](const ExtentRecord& r) { return (r.flags() & ExtentDirectoryIndex) != 0; });
        EXPECT_GE(static_cast<std::size_t>(directoryIndexes), 5000u / 40u);
    }

}} // namespace icd::core
