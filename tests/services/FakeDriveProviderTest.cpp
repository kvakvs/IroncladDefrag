#include "services/drives/FakeDriveProvider.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

namespace icd::services { namespace {

    // The fake list covers every row style the panel must show: exactly one SSD, several NTFS HDDs (one of
    // them never analysed) and a drive that cannot be optimized; roots are unique and sorted.
    TEST(FakeDriveProvider, CoversEveryRowStyle) {
        FakeDriveProvider provider(std::chrono::system_clock::time_point(std::chrono::hours(1000)));
        const auto drives = provider.listDrives();
        ASSERT_GE(drives.size(), 4u);
        EXPECT_EQ(std::ranges::count_if(drives, [](const auto& d) { return core::isSsd(d); }), 1);
        EXPECT_GE(std::ranges::count_if(drives, [](const auto& d) { return core::canOptimize(d); }), 3);
        EXPECT_TRUE(
            std::ranges::any_of(drives, [](const auto& d) { return !core::canOptimize(d) && !core::isSsd(d); }));
        EXPECT_TRUE(std::ranges::any_of(drives, [](const auto& d) { return core::canOptimize(d) && !d.lastAnalysed; }));
        EXPECT_TRUE(std::ranges::is_sorted(drives, {}, &core::DriveInfo::root));
        EXPECT_EQ(std::ranges::adjacent_find(drives, {}, &core::DriveInfo::root), drives.end());
        for (const auto& drive : drives) {
            EXPECT_LE(drive.freeBytes, drive.totalBytes) << drive.root;
        }
    }

}} // namespace icd::services
