// Raw MFT scanner end to end on the `special` test volume (T061): the whole pipeline (map of $MFT, planned reads,
// parser tasks, merge) gives one consistent snapshot, the same for 1 and 4 workers, with every cluster accounted for.
// Needs administrator rights and a test volume made with tools/testvol (gen-scenario special T); skipped otherwise.

#include "core/exec/Executor.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftScanner.h"
#include "services/scan/ScanCompare.h"

#include <gtest/gtest.h>

#include <optional>

namespace icd::services { namespace {

    // A mounted test volume holding the special scenario, or nothing.
    std::optional<platform::VolumeRecord> specialVolume() {
        for (auto& v : platform::enumerateVolumes()) {
            if (v.label.starts_with(L"ICD-TEST") &&
                GetFileAttributesW((v.mountPaths.front() + L"special").c_str()) != INVALID_FILE_ATTRIBUTES) {
                return v;
            }
        }
        return std::nullopt;
    }

    // One MFT scan of the volume with an executor.
    MftScanResult scanWith(const platform::VolumeRecord& v, core::Executor& executor) {
        const auto handle = platform::openVolume(v.volumeName, GENERIC_READ);
        const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
        const auto free = platform::readFreeRuns(handle.get(), geometry.totalClusters, executor);
        return scanFilesMft(handle.get(), v.volumeName, geometry, *v.layout.ntfs,
                            {free.runs().begin(), free.runs().end()}, executor, {}, nullptr);
    }

    // The volume, when the test can run.
    std::optional<platform::VolumeRecord> usableVolume() {
        if (!platform::isElevated()) {
            return std::nullopt;
        }
        auto v = specialVolume();
        return v && v->layout.ntfs ? v : std::nullopt;
    }

    TEST(MftScanner, ScansSpecialVolumeConsistently) {
        const auto v = usableVolume();
        if (!v) {
            GTEST_SKIP() << "needs administrator rights and the special test volume";
        }
        core::InlineExecutor serial;
        const MftScanResult one = scanWith(*v, serial);
        ThreadPool pool(4);
        const MftScanResult four = scanWith(*v, pool);
        const ScanComparison same = compareExact(one.snapshot, four.snapshot);
        EXPECT_TRUE(same.same()) << (same.samples.empty() ? "" : same.samples.front());

        ASSERT_TRUE(one.stats.mft);
        const MftReadFigures& f = *one.stats.mft;
        EXPECT_GT(f.recordsInUse, 3000U); // gen-scenario special writes about 3,200 files.
        EXPECT_EQ(f.badRecords, 0U);
        EXPECT_GT(f.extensionRecords, 0U); // The 1,324-fragment file needs several records.
        EXPECT_GT(f.bytesRead, 0U);
        EXPECT_EQ(one.stats.scanner, ScannerKind::Mft);

        // Extents and free space tile the volume; the placeholder owns little (no unreadable files on this path).
        const core::Snapshot& s = one.snapshot;
        core::ClusterCount used = 0;
        for (const auto& r : s.extents.records()) {
            used += r.length();
        }
        EXPECT_EQ(used + s.freeSpace.freeClusters(), s.geometry.totalClusters);
        EXPECT_EQ(one.stats.reconcile.droppedExtents, 0U);
        // $MFT's own extents start where FSCTL_GET_NTFS_VOLUME_DATA says.
        const auto* first = s.extents.find(v->layout.ntfs->mftStartLcn);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(s.path(first->file()), "\\$MFT");
    }

}} // namespace icd::services
