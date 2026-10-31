#include "core/snapshot/SnapshotBuilder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace icd::core { namespace {

    // A small volume: root, a directory with two files, one file split in three fragments, one resident.
    struct SmallVolume {
        FileIndex root = 0; // "\".
        FileIndex photos = 0; // "\Photos".
        FileIndex a = 0; // "\Photos\a.jpg": two fragments.
        FileIndex b = 0; // "\Photos\b.jpg": one run.
        FileIndex note = 0; // "\note.txt": resident, no clusters.
        Snapshot snapshot; // The built snapshot.
    };

    // A file entry with a name and parent.
    FileEntry entry(std::string_view name, FileIndex parent, std::uint16_t flags = 0) {
        FileEntry e;
        e.name = name;
        e.parent = parent;
        e.flags = flags;
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

    // Builds SmallVolume; extents are added out of order on purpose.
    SmallVolume smallVolume() {
        SmallVolume v;
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 1000});
        v.root = builder.addFile(entry("", noFile, FileDirectory));
        v.photos = builder.addFile(entry("Photos", v.root, FileDirectory));
        v.a = builder.addFile(entry("a.jpg", v.photos));
        v.b = builder.addFile(entry("b.jpg", v.photos));
        v.note = builder.addFile(entry("note.txt", v.root, FileResident));
        builder.addExtent(extent(v.a, {500, 20}, 10)); // Second fragment of a.
        builder.addExtent(extent(v.b, {100, 50}, 0));
        builder.addExtent(extent(v.a, {10, 10}, 0)); // First fragment of a.
        builder.addExtent(extent(v.photos, {0, 5}, 0));
        builder.addFreeRun({5, 5});
        builder.addFreeRun({20, 80});
        builder.addFreeRun({150, 350});
        builder.addFreeRun({520, 480});
        v.snapshot = std::move(builder).build();
        return v;
    }

    // Records are sorted by LCN, and files get their clusters summed.
    TEST(Snapshot, BuildsSortedIndex) {
        const SmallVolume v = smallVolume();
        const auto records = v.snapshot.extents.records();
        ASSERT_EQ(records.size(), 4u);
        EXPECT_TRUE(std::ranges::is_sorted(records, {}, [](const ExtentRecord& r) { return r.lcn(); }));
        EXPECT_EQ(v.snapshot.files.allocatedClusters(v.a), 30u);
        EXPECT_EQ(v.snapshot.files.allocatedClusters(v.note), 0u);
        EXPECT_TRUE(v.snapshot.files.has(v.note, FileResident));
    }

    // Lookup by LCN finds the owning extent, and nothing in free space or past the end.
    TEST(Snapshot, LookupByLcn) {
        const SmallVolume v = smallVolume();
        const ExtentIndex& index = v.snapshot.extents;
        ASSERT_NE(index.find(10), nullptr);
        EXPECT_EQ(index.find(10)->file(), v.a);
        EXPECT_EQ(index.find(19)->file(), v.a);
        EXPECT_EQ(index.find(20), nullptr);
        EXPECT_EQ(index.find(149)->file(), v.b);
        EXPECT_EQ(index.find(0)->file(), v.photos);
        EXPECT_EQ(index.find(999), nullptr);
        EXPECT_EQ(index.find(5000), nullptr);
    }

    // A range query returns exactly the overlapping records.
    TEST(Snapshot, OverlappingRange) {
        const SmallVolume v = smallVolume();
        const ExtentIndex& index = v.snapshot.extents;
        EXPECT_EQ(index.overlapping({0, 1000}).size(), 4u);
        EXPECT_EQ(index.overlapping({19, 1}).size(), 1u); // Last cluster of a's first fragment.
        EXPECT_EQ(index.overlapping({20, 80}).size(), 0u); // Free gap.
        ASSERT_EQ(index.overlapping({15, 100}).size(), 2u);
        EXPECT_EQ(index.overlapping({15, 100}).back().file(), v.b);
        EXPECT_EQ(index.overlapping({0, 0}).size(), 0u);
    }

    // Lookup by file gives VCN order whatever the LCN order, and counts fragments.
    TEST(Snapshot, LookupByFile) {
        const SmallVolume v = smallVolume();
        const ExtentIndex& index = v.snapshot.extents;
        const auto extentsOfA = index.extentsOf(v.a);
        ASSERT_EQ(extentsOfA.size(), 2u);
        EXPECT_EQ(index.records()[extentsOfA[0]].lcn(), 10u);
        EXPECT_EQ(index.records()[extentsOfA[1]].lcn(), 500u);
        EXPECT_EQ(index.fragmentCount(v.a), 2u);
        EXPECT_EQ(index.fragmentCount(v.b), 1u);
        EXPECT_EQ(index.fragmentCount(v.note), 0u);
        EXPECT_TRUE(index.extentsOf(v.note).empty());
        EXPECT_TRUE(index.extentsOf(12345).empty());
    }

    // Paths are rebuilt from parent indexes; the tree lists children and roots.
    TEST(Snapshot, PathReconstruction) {
        const SmallVolume v = smallVolume();
        EXPECT_EQ(v.snapshot.path(v.root), "\\");
        EXPECT_EQ(v.snapshot.path(v.photos), "\\Photos");
        EXPECT_EQ(v.snapshot.path(v.b), "\\Photos\\b.jpg");
        EXPECT_EQ(v.snapshot.path(v.note), "\\note.txt");
        const auto children = v.snapshot.tree.children(v.photos);
        EXPECT_EQ(std::vector<FileIndex>(children.begin(), children.end()), (std::vector<FileIndex>{v.a, v.b}));
        ASSERT_EQ(v.snapshot.tree.roots().size(), 1u);
        EXPECT_EQ(v.snapshot.tree.roots()[0], v.root);
        EXPECT_TRUE(v.snapshot.tree.children(v.b).empty());
    }

    // A parent cycle in damaged data ends the walk instead of looping.
    TEST(Snapshot, PathSurvivesCycles) {
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 10});
        builder.addFile(entry("x", 1));
        builder.addFile(entry("y", 0));
        const Snapshot snapshot = std::move(builder).build();
        EXPECT_TRUE(snapshot.path(0).starts_with("\\..."));
    }

    // Names are stored once.
    TEST(Snapshot, NamesAreInterned) {
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 10});
        const FileIndex root = builder.addFile(entry("", noFile));
        for (int i = 0; i < 100; ++i) {
            builder.addFile(entry("index.js", root));
        }
        builder.addFile(entry("other.js", root));
        const Snapshot snapshot = std::move(builder).build();
        EXPECT_EQ(snapshot.names.size(), 3u); // "", "index.js", "other.js".
        EXPECT_EQ(snapshot.files.name(1), snapshot.files.name(100));
        EXPECT_EQ(snapshot.names.name(snapshot.files.name(101)), "other.js");
    }

    // A run longer than one record holds is split, but still counts as one fragment.
    TEST(Snapshot, LongRunsAreSplit) {
        const ClusterCount total = 3 * maxExtentLength + 100;
        SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = total});
        const FileIndex file = builder.addFile(entry("huge.vhdx", noFile));
        builder.addExtent(extent(file, {50, 2 * maxExtentLength + 7}, 0));
        const Snapshot snapshot = std::move(builder).build();
        EXPECT_EQ(snapshot.extents.size(), 3u);
        EXPECT_EQ(snapshot.extents.fragmentCount(file), 1u);
        EXPECT_EQ(snapshot.files.allocatedClusters(file), 2 * maxExtentLength + 7);
        EXPECT_EQ(snapshot.extents.find(50 + maxExtentLength)->lcn(), 50 + maxExtentLength);
        EXPECT_EQ(snapshot.extents.records().back().length(), 7u);
    }

    // Inconsistent input is rejected.
    TEST(Snapshot, RejectsBadInput) {
        const auto build = [](auto fill) {
            SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 100});
            builder.addFile(entry("f", noFile));
            fill(builder);
            return std::move(builder).build();
        };
        EXPECT_THROW(build([](SnapshotBuilder& b) { b.addExtent(extent(0, {90, 20}, 0)); }), SnapshotError);
        EXPECT_THROW(build([](SnapshotBuilder& b) { b.addExtent(extent(7, {0, 5}, 0)); }), SnapshotError);
        EXPECT_THROW(build([](SnapshotBuilder& b) {
                         b.addExtent(extent(0, {0, 10}, 0));
                         b.addExtent(extent(0, {9, 5}, 10));
                     }),
                     SnapshotError);
        EXPECT_THROW(build([](SnapshotBuilder& b) { b.addFreeRun({95, 10}); }), SnapshotError);
        EXPECT_THROW(build([](SnapshotBuilder& b) { b.addFile(entry("orphan", 42)); }), SnapshotError);
        EXPECT_NO_THROW(build([](SnapshotBuilder& b) { b.addExtent(extent(0, {0, 100}, 0)); }));
    }

    // Free runs are sorted and merged; queries find runs and the largest one.
    TEST(Snapshot, FreeRuns) {
        const FreeRunList list({{50, 10}, {0, 5}, {5, 5}, {55, 20}, {90, 0}, {200, 3}}, 300);
        ASSERT_EQ(list.size(), 3u);
        EXPECT_EQ(list.runs()[0], (ClusterRun{0, 10}));
        EXPECT_EQ(list.runs()[1], (ClusterRun{50, 25}));
        EXPECT_EQ(list.freeClusters(), 38u);
        EXPECT_EQ(list.largest(), (ClusterRun{50, 25}));
        EXPECT_TRUE(list.isFree(74));
        EXPECT_FALSE(list.isFree(75));
        EXPECT_EQ(list.overlapping({8, 50}).size(), 2u);
        EXPECT_EQ(FreeRunList().largest(), std::nullopt);
        EXPECT_THROW(FreeRunList({{299, 2}}, 300), SnapshotError);
    }

    // The same input in another order builds an identical snapshot (plan.md 6.2 principle 8).
    TEST(Snapshot, InputOrderDoesNotMatter) {
        std::vector<ExtentEntry> extents;
        for (FileIndex file = 0; file < 50; ++file) {
            for (std::uint64_t part = 0; part < 4; ++part) {
                extents.push_back(extent(file, {(part * 50 + file) * 10, 7}, part * 7));
            }
        }
        const auto buildWith = [](std::vector<ExtentEntry> batch) {
            SnapshotBuilder builder({.bytesPerCluster = 4096, .totalClusters = 10000});
            for (int file = 0; file < 50; ++file) {
                builder.addFile(entry("f", noFile));
            }
            builder.addExtents(std::move(batch));
            return std::move(builder).build();
        };
        const Snapshot first = buildWith(extents);
        std::ranges::shuffle(extents, std::mt19937(7));
        const Snapshot second = buildWith(extents);
        EXPECT_TRUE(std::ranges::equal(first.extents.records(), second.extents.records()));
        for (FileIndex file = 0; file < 50; ++file) {
            EXPECT_TRUE(std::ranges::equal(first.extents.extentsOf(file), second.extents.extentsOf(file)));
            EXPECT_EQ(first.extents.fragmentCount(file), 4u);
        }
    }

}} // namespace icd::core
