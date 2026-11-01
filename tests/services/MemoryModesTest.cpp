// Memory modes (T066): the size estimate, the give-way ladder, memory-mapped storage, and snapshots built into mapped
// files that equal heap-built ones. The volume check (2 workers, 2 GB, mapped = uncapped) is `icd-cli check-caps`.

#include "core/resources/MemoryPlanner.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"
#include "platform/win/MappedMemory.h"
#include "services/scan/ScanCompare.h"
#include "services/snapshot/MappedStorage.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <numeric>

namespace icd::services { namespace {

    // A fresh directory for mapped files.
    std::filesystem::path scratch() {
        const auto dir = std::filesystem::temp_directory_path() / "icd-mapped-test";
        std::filesystem::remove_all(dir);
        return dir;
    }

    TEST(MemoryPlanner, EstimateGrowsWithTheMft) {
        const auto small = core::estimateSnapshot(10'000);
        const auto big = core::estimateSnapshot(20'000'000);
        EXPECT_EQ(big.files, 20'000'000U);
        EXPECT_EQ(big.extents, 60'000'000U); // The design ratio.
        EXPECT_LT(small.snapshotBytes, big.snapshotBytes);
        EXPECT_LT(big.largeArrayBytes, big.snapshotBytes);
        EXPECT_LT(big.snapshotBytes, big.scanPeakBytes);
        EXPECT_LT(big.snapshotBytes, std::uint64_t{8} << 30U); // plan.md 6.5: 8 GB or less at the design target.
    }

    TEST(MemoryPlanner, LadderFollowsTheBudget) {
        const auto e = core::estimateSnapshot(20'000'000);
        EXPECT_EQ(core::planMemory(e, 0).rung, core::MemoryRung::Full); // No limit.
        EXPECT_EQ(core::planMemory(e, std::uint64_t{64} << 30U).rung, core::MemoryRung::Full);
        const core::MemoryPlan tight = core::planMemory(e, std::uint64_t{2} << 30U);
        EXPECT_EQ(tight.rung, core::MemoryRung::MappedStorage); // Nothing else fits 2 GB.
        EXPECT_TRUE(tight.mappedStorage());
        EXPECT_FALSE(tight.parallelLevels());
        EXPECT_FALSE(tight.speedExtras());
        // Each rung needs less than the one before, and planning picks the first that fits.
        core::MemoryPlan plan = core::planMemory(e, 0);
        while (!plan.mappedStorage()) {
            const core::MemoryPlan next = core::giveWay(plan, e);
            EXPECT_LT(next.neededBytes, plan.neededBytes);
            EXPECT_EQ(core::planMemory(e, next.neededBytes).rung, next.rung);
            plan = next;
        }
        EXPECT_EQ(core::giveWay(plan, e).rung, core::MemoryRung::MappedStorage); // The last rung stays.
        EXPECT_NE(core::describeMemoryPlan(tight).find("mapped files"), std::string::npos);
    }

    TEST(MappedStorage, LargeArraysLiveInMappedFiles) {
        const auto dir = scratch();
        const auto storage = std::make_shared<platform::MappedFileResource>(dir, 4096);
        platform::MappedFileResource& resource = *storage;
        {
            core::LargeVector<std::uint64_t> values{core::LargeArrayAllocator<std::uint64_t>(storage)};
            values.resize(100'000);
            std::iota(values.begin(), values.end(), std::uint64_t{7});
            EXPECT_GE(resource.mappedBytes(), values.size() * sizeof(std::uint64_t));
            EXPECT_FALSE(std::filesystem::is_empty(dir));
            core::LargeVector<std::uint64_t> moved = std::move(values); // Moving keeps the mapped storage.
            EXPECT_EQ(moved.get_allocator().resource(), &resource);
            EXPECT_EQ(moved[99'999], 100'006U);
            const core::LargeVector<std::uint64_t> copy = moved; // Copies go to the heap.
            EXPECT_NE(copy.get_allocator().resource(), &resource);
        }
        EXPECT_EQ(resource.mappedBytes(), 0U);
        EXPECT_TRUE(std::filesystem::is_empty(dir)); // Files are deleted with their mapping.
        std::filesystem::remove_all(dir);
    }

    TEST(MappedStorage, SnapshotsBuiltIntoMappedFilesAreTheSame) {
        core::SynthOptions options;
        options.files = 3000;
        const core::Snapshot heap = core::generateSyntheticVolume(options);
        const auto dir = scratch();
        auto resource = std::make_shared<platform::MappedFileResource>(dir, 4096);
        core::SnapshotBuilder builder(heap.geometry, resource);
        for (core::FileIndex f = 0; f < heap.files.size(); ++f) {
            builder.addFile({heap.names.name(heap.files.name(f)), heap.files.parent(f), heap.files.sizeBytes(f),
                             heap.files.flags(f), heap.files.times(f), heap.files.fileReference(f)});
            std::uint64_t vcn = 0;
            for (const std::uint32_t position : heap.extents.extentsOf(f)) {
                const core::ExtentRecord& r = heap.extents.records()[position];
                builder.addExtent({.run = r.run(), .vcn = vcn, .file = f, .stream = r.stream(), .flags = r.flags()});
                vcn += r.length();
            }
        }
        for (const core::ClusterRun& run : heap.freeSpace.runs()) {
            builder.addFreeRun(run);
        }
        core::Snapshot mapped = std::move(builder).build();
        EXPECT_GT(resource->mappedAllocations(), 0U);
        EXPECT_TRUE(compareExact(heap, mapped).same());
        core::Snapshot assigned;
        assigned = std::move(mapped); // Move assignment keeps the storage too.
        EXPECT_TRUE(compareExact(heap, assigned).same());
        resource.reset(); // The snapshot's arrays keep the files alive.
        EXPECT_EQ(assigned.path(42), heap.path(42));
    }

    TEST(MappedStorage, StorageFollowsThePlan) {
        const auto e = core::estimateSnapshot(1000);
        EXPECT_EQ(storageFor(core::planMemory(e, 0)), nullptr);
        core::MemoryPlan mapped = core::planMemory(e, 0);
        while (!mapped.mappedStorage()) {
            mapped = core::giveWay(mapped, e);
        }
        EXPECT_NE(storageFor(mapped, scratch()), nullptr);
        MemoryGovernor governor(e, core::planMemory(e, 0));
        (void)governor.check(); // Whatever the machine's state: never more than one rung per check.
        EXPECT_LE(static_cast<int>(governor.plan().rung), 1);
    }

}} // namespace icd::services
