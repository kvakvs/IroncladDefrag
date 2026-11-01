#include "core/format/DriveText.h"
#include "core/format/Duration.h"
#include "core/model/DriveInfo.h"

#include <gtest/gtest.h>

#include <chrono>

namespace icd::core { namespace {

    using namespace std::chrono_literals;

    constexpr std::uint64_t GB = 1024ULL * 1024 * 1024;

    // A 4 GB NTFS HDD with 1 GB free.
    DriveInfo hdd() {
        DriveInfo drive;
        drive.root = "D:\\";
        drive.label = "Data";
        drive.totalBytes = 4 * GB;
        drive.freeBytes = GB;
        drive.fileSystem = "NTFS";
        drive.media = MediaType::Hdd;
        return drive;
    }

    // Used space and its share; free space larger than the size counts as empty, a zero size as 0.
    TEST(DriveInfo, UsedSpace) {
        const DriveInfo drive = hdd();
        EXPECT_EQ(drive.usedBytes(), 3 * GB);
        EXPECT_DOUBLE_EQ(drive.usedFraction(), 0.75);
        DriveInfo overfull;
        overfull.totalBytes = 1;
        overfull.freeBytes = 5;
        EXPECT_EQ(overfull.usedBytes(), 0u);
        EXPECT_DOUBLE_EQ(DriveInfo{}.usedFraction(), 0.0);
    }

    // Only NTFS volumes that are not SSDs get the full workflow; unknown media counts as HDD.
    TEST(DriveInfo, WhatCanBeOptimized) {
        DriveInfo drive = hdd();
        EXPECT_TRUE(canOptimize(drive));
        drive.media = MediaType::Unknown;
        EXPECT_TRUE(canOptimize(drive));
        drive.media = MediaType::Ssd;
        EXPECT_FALSE(canOptimize(drive));
        EXPECT_TRUE(isSsd(drive));
        drive = hdd();
        drive.fileSystem = "exFAT";
        EXPECT_FALSE(canOptimize(drive));
    }

    // Name and space texts.
    TEST(DriveText, NameAndSpace) {
        DriveInfo drive = hdd();
        EXPECT_EQ(driveName(drive), "D: Data");
        EXPECT_EQ(describeSpace(drive), "1.00 GB free of 4.00 GB");
        drive.label.clear();
        EXPECT_EQ(driveName(drive), "D:");
        EXPECT_EQ(describeMedia(MediaType::Ssd), "SSD");
        EXPECT_EQ(describeMedia(MediaType::Unknown), "Unknown");
    }

    // Fragmentation and last-analysis texts for each kind of drive.
    TEST(DriveText, AnalysisColumns) {
        const auto now = std::chrono::system_clock::time_point(1000h);
        DriveInfo drive = hdd();
        EXPECT_EQ(describeFragmentation(drive), "-");
        EXPECT_EQ(describeLastAnalysed(drive, now), "never");
        drive.fragmentationPercent = 12.44;
        drive.lastAnalysed = now - 3h;
        EXPECT_EQ(describeFragmentation(drive), "12.4%");
        EXPECT_EQ(describeLastAnalysed(drive, now), "3 h ago");

        drive.media = MediaType::Ssd;
        EXPECT_EQ(describeFragmentation(drive), "");
        EXPECT_EQ(describeLastAnalysed(drive, now), "");
        drive = hdd();
        drive.fileSystem = "FAT32";
        EXPECT_EQ(describeFragmentation(drive), "not supported");
        EXPECT_EQ(describeLastAnalysed(drive, now), "");
    }

    // Badges follow the drive's facts; last-access tracking only matters where strategies run.
    TEST(DriveText, Badges) {
        const auto now = std::chrono::system_clock::time_point(1000h);
        DriveInfo drive = hdd();
        drive.badges = {};
        drive.badges.adminNeeded = true;
        drive.badges.vssPresent = true;
        drive.badges.snapshotSaved = now - 30h;
        // Facts not detected yet show no badge rather than a guess.
        EXPECT_EQ(badgeLabels(drive, now), (std::vector<std::string>{"Admin needed", "VSS", "Snapshot yesterday"}));
        drive.badges.lastAccessTracking = false;
        drive.badges.scheduledOptimization = false;
        EXPECT_EQ(badgeLabels(drive, now),
                  (std::vector<std::string>{"Admin needed", "VSS", "Last access off", "Auto-optimize off",
                                            "Snapshot yesterday"}));
        drive.media = MediaType::Ssd;
        drive.badges = {};
        drive.badges.scheduledOptimization = true;
        EXPECT_EQ(badgeLabels(drive, now), (std::vector<std::string>{"Auto-optimize on"}));
    }

    // Stopwatch durations.
    TEST(Duration, Stopwatch) {
        EXPECT_EQ(formatDuration(0s), "0:00");
        EXPECT_EQ(formatDuration(42s), "0:42");
        EXPECT_EQ(formatDuration(12min + 5s), "12:05");
        EXPECT_EQ(formatDuration(1h + 2min + 3s), "1:02:03");
        EXPECT_EQ(formatDuration(-5s), "0:00");
    }

    // Ages are coarse and never negative.
    TEST(Duration, Ages) {
        EXPECT_EQ(formatAge(-10s), "just now");
        EXPECT_EQ(formatAge(59s), "just now");
        EXPECT_EQ(formatAge(5min), "5 min ago");
        EXPECT_EQ(formatAge(3h + 59min), "3 h ago");
        EXPECT_EQ(formatAge(30h), "yesterday");
        EXPECT_EQ(formatAge(24h * 12 + 1h), "12 days ago");
    }

}} // namespace icd::core
