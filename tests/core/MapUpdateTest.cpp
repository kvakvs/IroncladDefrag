// Map update after a USN catch-up (T065): recomputing only the dirty ranges gives the same pyramid, flags and values as
// building the map of the new snapshot from scratch.

#include "core/map/MapData.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace icd::core { namespace {

    // The snapshot with the extents of some files moved: each loses its clusters (they become free), as if the files
    // were deleted from inside `range`; their old runs are the dirty ranges.
    Snapshot withoutFiles(const Snapshot& s, const std::set<FileIndex>& dropped, std::vector<ClusterRun>& dirty) {
        SnapshotBuilder builder(s.geometry);
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            builder.addFile({s.names.name(s.files.name(f)), s.files.parent(f), s.files.sizeBytes(f), s.files.flags(f),
                             s.files.times(f), s.files.fileReference(f)});
        }
        std::vector<ClusterRun> free(s.freeSpace.runs().begin(), s.freeSpace.runs().end());
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            std::uint64_t vcn = 0;
            for (const std::uint32_t position : s.extents.extentsOf(f)) {
                const ExtentRecord& r = s.extents.records()[position];
                if (dropped.contains(f)) {
                    free.push_back(r.run());
                    dirty.push_back(r.run());
                    continue;
                }
                builder.addExtent({.run = r.run(), .vcn = vcn, .file = f, .stream = r.stream(), .flags = r.flags()});
                vcn += r.length();
            }
        }
        for (const ClusterRun& run : FreeRunList(std::move(free), s.geometry.totalClusters).runs()) {
            builder.addFreeRun(run);
        }
        return std::move(builder).build();
    }

    // Every stored level of two pyramids is equal.
    void expectSamePyramid(const Pyramid& a, const Pyramid& b) {
        ASSERT_EQ(a.baseLevel(), b.baseLevel());
        ASSERT_EQ(a.topLevel(), b.topLevel());
        for (int level = a.baseLevel(); level <= a.topLevel(); ++level) {
            EXPECT_TRUE(std::ranges::equal(a.level(level), b.level(level))) << "level " << level;
        }
    }

    TEST(MapUpdate, DirtyRangesGiveTheFullBuild) {
        SynthOptions options;
        options.files = 4000;
        const Snapshot before = generateSyntheticVolume(options);
        InlineExecutor serial;
        const ScreenAspect aspect{16, 9};
        const auto previous = MapData::build(before, aspect, serial);
        std::set<FileIndex> dropped;
        for (FileIndex f = 10; f < before.files.size(); f += 97) {
            dropped.insert(f);
        }
        std::vector<ClusterRun> dirty;
        const Snapshot after = withoutFiles(before, dropped, dirty);
        const auto updated = MapData::update(*previous, after, dirty, serial);
        const auto rebuilt = MapData::build(after, aspect, serial);
        EXPECT_EQ(updated->ageReference(), rebuilt->ageReference());
        expectSamePyramid(updated->pyramid(), rebuilt->pyramid());
        EXPECT_EQ(updated->world().rowWidth(), rebuilt->world().rowWidth());
        EXPECT_EQ(updated->memoryBytes(), rebuilt->memoryBytes());
    }

    TEST(MapUpdate, RefusesAnotherVolumeSize) {
        SynthOptions options;
        options.files = 500;
        InlineExecutor serial;
        const auto previous = MapData::build(generateSyntheticVolume(options), {16, 9}, serial);
        options.geometry.totalClusters *= 2;
        EXPECT_THROW((void)MapData::update(*previous, generateSyntheticVolume(options), {}, serial), SnapshotError);
    }

}} // namespace icd::core
