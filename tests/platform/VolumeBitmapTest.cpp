// Volume bitmap on real volumes (T045). On quiet test volumes (label ICD-TEST*) the free runs must add up to
// NTFS's free cluster count exactly, and to "fsutil volume diskfree" once the reserved clusters are taken off.
// The Windows drive changes while it is read, so it is only checked loosely. Needs administrator rights.

#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

    using icd::platform::VolumeRecord;

    // Free runs of a volume read with a given bitmap piece size.
    std::vector<icd::core::ClusterRun> runsWithChunk(HANDLE volume, std::size_t chunkBytes) {
        std::vector<icd::core::ClusterRun> runs;
        icd::platform::readVolumeBitmap(volume, chunkBytes, [&](const icd::core::BitmapChunk& chunk) {
            icd::core::stitchRuns(runs, icd::core::freeRunsOf(chunk));
        });
        return runs;
    }

    // Free bytes as GetDiskFreeSpaceExW (and fsutil volume diskfree) report them.
    std::uint64_t diskFree(const std::wstring& root) {
        ULARGE_INTEGER freeBytes{};
        GetDiskFreeSpaceExW(root.c_str(), nullptr, nullptr, &freeBytes);
        return freeBytes.QuadPart;
    }

    TEST(VolumeBitmap, FreeRunsMatchNtfsOnTestVolumes) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        icd::services::ThreadPool pool(4);
        int checked = 0;
        for (const VolumeRecord& v : icd::platform::enumerateVolumes()) {
            if (v.fileSystem != L"NTFS" || !v.label.starts_with(L"ICD-TEST") || !v.layout.ntfs) {
                continue;
            }
            SCOPED_TRACE(icd::platform::toUtf8(v.mountPaths.front()));
            const auto volume = icd::platform::openVolume(v.volumeName, GENERIC_READ);
            ASSERT_TRUE(volume.valid());
            const auto runs = icd::platform::readFreeRuns(volume.get(), v.layout.totalClusters, pool);
            const auto now = icd::platform::queryVolume(v.volumeName); // Fresh numbers, after the read.
            EXPECT_EQ(runs.freeClusters(), now.layout.freeClusters);
            EXPECT_EQ((runs.freeClusters() - now.layout.ntfs->totalReserved) * now.layout.bytesPerCluster,
                      diskFree(v.mountPaths.front()));
            // Small pieces exercise ERROR_MORE_DATA and stitching on real data.
            EXPECT_EQ(runsWithChunk(volume.get(), 4096), std::vector(runs.runs().begin(), runs.runs().end()));
            ++checked;
        }
        if (checked == 0) {
            GTEST_SKIP() << "no ICD-TEST volume mounted (tools/testvol/make-testvol.ps1)";
        }
    }

    TEST(VolumeBitmap, WindowsDriveIsClose) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        const std::wstring root = std::wstring(windows, 3);
        const auto record = icd::platform::queryVolume(root);
        ASSERT_TRUE(record.layout.ntfs.has_value());
        const auto volume = icd::platform::openVolume(root, GENERIC_READ);
        ASSERT_TRUE(volume.valid());
        icd::services::ThreadPool pool(4);
        const auto runs = icd::platform::readFreeRuns(volume.get(), record.layout.totalClusters, pool);
        const double expected = static_cast<double>(record.layout.freeClusters);
        EXPECT_LT(std::abs(static_cast<double>(runs.freeClusters()) - expected), expected * 0.01 + 1e5);
    }

} // namespace
