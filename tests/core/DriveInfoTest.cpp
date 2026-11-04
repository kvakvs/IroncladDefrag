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

    // Virtual disks keep the full workflow whatever their host file sits on; the seek penalty decides
    // otherwise, TRIM is only a hint, and silence means unknown.
    TEST(DriveInfo, ClassifiesMedia) {
        StorageFacts facts;
        EXPECT_EQ(classifyMedia(facts), MediaType::Unknown);
        facts.trimEnabled = false;
        EXPECT_EQ(classifyMedia(facts), MediaType::Unknown);
        facts.trimEnabled = true;
        EXPECT_EQ(classifyMedia(facts), MediaType::Ssd);
        facts.seekPenalty = true;
        EXPECT_EQ(classifyMedia(facts), MediaType::Hdd);
        facts.seekPenalty = false;
        EXPECT_EQ(classifyMedia(facts), MediaType::Ssd);
        facts.virtualDisk = true;
        EXPECT_EQ(classifyMedia(facts), MediaType::Virtual);
        DriveInfo drive = hdd();
        drive.media = MediaType::Virtual;
        EXPECT_TRUE(canOptimize(drive));
        EXPECT_FALSE(isSsd(drive));
    }

    // The media column adds the bus (not for virtual disks, whose bus is implied) and a removable note.
    TEST(DriveText, Storage) {
        DriveInfo drive = hdd();
        EXPECT_EQ(describeStorage(drive), "HDD");
        drive.storage.bus = "SATA";
        EXPECT_EQ(describeStorage(drive), "HDD · SATA");
        drive.storage.removable = true;
        EXPECT_EQ(describeStorage(drive), "HDD · SATA · removable");
        drive.media = MediaType::Virtual;
        drive.storage = {};
        drive.storage.bus = "Virtual";
        EXPECT_EQ(describeStorage(drive), "Virtual");
        EXPECT_EQ(describeMedia(MediaType::Virtual), "Virtual");
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
        EXPECT_EQ(describeLastAnalysed(drive, now), "never trimmed");
        drive.lastOptimized = now - 50h;
        EXPECT_EQ(describeLastAnalysed(drive, now), "trimmed 2 days ago");
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

    // Each storage setup the app does not move files on blocks moves (in MoveBlock order), but not analysis.
    TEST(DriveInfo, MoveBlocks) {
        DriveInfo drive = hdd();
        EXPECT_TRUE(moveBlocks(drive).empty());
        EXPECT_TRUE(canMove(drive));
        drive.storage.bus = "SATA";
        drive.storage.encrypted = false;
        drive.layout.diskExtentCount = 1;
        EXPECT_TRUE(canMove(drive));
        const auto blocked = [](DriveInfo d) {
            EXPECT_TRUE(canOptimize(d));
            EXPECT_FALSE(canMove(d));
            return moveBlocks(d);
        };
        DriveInfo raid = drive;
        raid.storage.bus = "RAID";
        EXPECT_EQ(blocked(raid), std::vector{MoveBlock::Raid});
        DriveInfo spaces = drive;
        spaces.storage.bus = "Storage Spaces";
        EXPECT_EQ(blocked(spaces), std::vector{MoveBlock::StorageSpaces});
        DriveInfo dynamic = drive;
        dynamic.storage.dynamicDisk = true;
        EXPECT_EQ(blocked(dynamic), std::vector{MoveBlock::DynamicDisk});
        DriveInfo spanned = drive;
        spanned.layout.diskExtentCount = 2; // Spanned or striped: no single disk.
        EXPECT_EQ(blocked(spanned), std::vector{MoveBlock::DynamicDisk});
        DriveInfo removable = drive;
        removable.storage.removable = true;
        EXPECT_EQ(blocked(removable), std::vector{MoveBlock::Removable});
        DriveInfo encrypted = drive;
        encrypted.storage.encrypted = true;
        EXPECT_EQ(blocked(encrypted), std::vector{MoveBlock::Encrypted});
        encrypted.storage.removable = true;
        EXPECT_EQ(blocked(encrypted), (std::vector{MoveBlock::Removable, MoveBlock::Encrypted}));
        // An SSD or a non-NTFS volume is not optimizable at all, blocked or not.
        encrypted.media = MediaType::Ssd;
        EXPECT_FALSE(canMove(encrypted));
    }

    // A blocked drive says so in one badge and explains every reason where Execute would be.
    TEST(DriveText, MovesBlocked) {
        const auto now = std::chrono::system_clock::time_point(1000h);
        DriveInfo drive = hdd();
        EXPECT_EQ(movesBlockedText(drive), "");
        drive.storage.bus = "RAID";
        drive.storage.encrypted = true;
        drive.badges.vssPresent = true;
        EXPECT_EQ(badgeLabels(drive, now), (std::vector<std::string>{"No moves (RAID, BitLocker)", "VSS"}));
        const std::string text = movesBlockedText(drive);
        EXPECT_TRUE(text.starts_with("Moves are blocked on this drive: it sits behind a RAID controller")) << text;
        EXPECT_NE(text.find("; it is encrypted with BitLocker"), std::string::npos) << text;
        EXPECT_TRUE(text.ends_with("Analysis and plans still work, so the layout can be reviewed.")) << text;
        for (const MoveBlock block : {MoveBlock::Raid, MoveBlock::StorageSpaces, MoveBlock::DynamicDisk,
                                      MoveBlock::Removable, MoveBlock::Encrypted}) {
            EXPECT_FALSE(blockName(block).empty());
            EXPECT_FALSE(blockReason(block).empty());
        }
        // SSDs only get TRIM, so a block means nothing there.
        drive.media = MediaType::Ssd;
        EXPECT_EQ(movesBlockedText(drive), "");
        EXPECT_EQ(badgeLabels(drive, now), (std::vector<std::string>{"VSS"}));
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
