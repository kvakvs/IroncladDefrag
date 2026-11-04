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

    // The simulated Windows schedule: a drive taken out shows "off" in the next listing and comes back on restore;
    // a drive whose schedule is unknown is refused.
    TEST(FakeDriveProvider, SimulatesTheWindowsSchedule) {
        FakeDriveProvider provider;
        const auto find = [&](const char* root) {
            const auto drives = provider.listDrives();
            return *std::ranges::find(drives, std::string(root), &core::DriveInfo::root);
        };
        const core::DriveInfo backup = find("F:\\");
        ASSERT_EQ(backup.badges.scheduledOptimization, true);
        EXPECT_EQ(provider.setScheduledOptimization(backup, false), "");
        EXPECT_EQ(find("F:\\").badges.scheduledOptimization, false);
        EXPECT_EQ(find("C:\\").badges.scheduledOptimization, true); // Other drives keep theirs.
        EXPECT_EQ(provider.setScheduledOptimization(find("F:\\"), true), "");
        EXPECT_EQ(find("F:\\").badges.scheduledOptimization, true);
        EXPECT_NE(provider.setScheduledOptimization(find("E:\\"), false), "");
    }

}} // namespace icd::services
