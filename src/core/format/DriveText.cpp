#include "core/format/DriveText.h"

#include "core/format/ByteSize.h"
#include "core/format/Duration.h"

#include <format>
#include <optional>
#include <utility>

namespace icd::core {

    namespace {

        // Whole seconds from then to now.
        std::chrono::seconds ageOf(std::chrono::system_clock::time_point then,
                                   std::chrono::system_clock::time_point now) {
            return std::chrono::duration_cast<std::chrono::seconds>(now - then);
        }

        // "D:" from "D:\".
        std::string letterOf(const DriveInfo& drive) {
            std::string letter = drive.root;
            while (!letter.empty() && (letter.back() == '\\' || letter.back() == '/')) {
                letter.pop_back();
            }
            return letter;
        }

        // The texts of the drive's move blocks, joined by `separator`; empty without blocks.
        std::string joinBlocks(const DriveInfo& drive, std::string_view (*text)(MoveBlock),
                               std::string_view separator) {
            std::string joined;
            for (const MoveBlock block : moveBlocks(drive)) {
                joined += joined.empty() ? "" : separator;
                joined += text(block);
            }
            return joined;
        }

        // "No moves (RAID, BitLocker)" for a drive that is analysed but never moved; empty otherwise.
        std::string blockBadge(const DriveInfo& drive) {
            const std::string names = canOptimize(drive) ? joinBlocks(drive, blockName, ", ") : std::string();
            return names.empty() ? names : "No moves (" + names + ")";
        }

        // The two labels of an on/off badge.
        struct ToggleLabels {
            const char* on = ""; // Shown when the fact is true.
            const char* off = ""; // Shown when it is false.
        };

        // Adds an on/off badge for a detected fact; an undetected one shows nothing rather than a guess.
        void addToggle(std::vector<std::string>& labels, std::optional<bool> fact, ToggleLabels text) {
            if (fact) {
                labels.emplace_back(*fact ? text.on : text.off);
            }
        }

    } // namespace

    std::string driveName(const DriveInfo& drive) {
        std::string name = letterOf(drive);
        if (!drive.label.empty()) {
            name += " " + drive.label;
        }
        return name;
    }

    std::string describeSpace(const DriveInfo& drive) {
        return formatBytes(drive.freeBytes) + " free of " + formatBytes(drive.totalBytes);
    }

    std::string describeMedia(MediaType media) {
        switch (media) {
        case MediaType::Hdd:
            return "HDD";
        case MediaType::Ssd:
            return "SSD";
        case MediaType::Virtual:
            return "Virtual";
        case MediaType::Unknown:
            break;
        }
        return "Unknown";
    }

    std::string describeStorage(const DriveInfo& drive) {
        std::string text = describeMedia(drive.media);
        if (!drive.storage.bus.empty() && drive.media != MediaType::Virtual) {
            text += " · " + drive.storage.bus;
        }
        if (drive.storage.removable) {
            text += " · removable";
        }
        return text;
    }

    std::string describeFragmentation(const DriveInfo& drive) {
        if (isSsd(drive)) {
            return {};
        }
        if (!canOptimize(drive)) {
            return "not supported";
        }
        if (!drive.fragmentationPercent) {
            return "-";
        }
        return std::format("{:.1f}%", *drive.fragmentationPercent);
    }

    std::string describeLastAnalysed(const DriveInfo& drive, std::chrono::system_clock::time_point now) {
        if (!canOptimize(drive)) {
            return {};
        }
        return drive.lastAnalysed ? formatAge(ageOf(*drive.lastAnalysed, now)) : "never";
    }

    std::string_view blockName(MoveBlock block) {
        switch (block) {
        case MoveBlock::Raid:
            return "RAID";
        case MoveBlock::StorageSpaces:
            return "Storage Spaces";
        case MoveBlock::DynamicDisk:
            return "dynamic disk";
        case MoveBlock::Removable:
            return "removable";
        case MoveBlock::Encrypted:
            return "BitLocker";
        }
        return "unsupported";
    }

    std::string_view blockReason(MoveBlock block) {
        switch (block) {
        case MoveBlock::Raid:
            return "it sits behind a RAID controller, which spreads its clusters over the member disks, so zone "
                   "placement has no meaning";
        case MoveBlock::StorageSpaces:
            return "it is a Storage Spaces disk, whose slabs map to the pool's disks, not to one disk's tracks";
        case MoveBlock::DynamicDisk:
            return "it is on a dynamic disk or spans several disks, so its positions do not map to one disk";
        case MoveBlock::Removable:
            return "it is removable media, which can be pulled out in the middle of a move";
        case MoveBlock::Encrypted:
            return "it is encrypted with BitLocker, and moves on encrypted volumes are not tested yet";
        }
        return "the setup is not supported";
    }

    std::string movesBlockedText(const DriveInfo& drive) {
        if (!canOptimize(drive)) {
            return {};
        }
        const std::string reasons = joinBlocks(drive, blockReason, "; ");
        if (reasons.empty()) {
            return {};
        }
        return "Moves are blocked on this drive: " + reasons +
            ". Analysis and plans still work, so the layout can be reviewed.";
    }

    std::vector<std::string> badgeLabels(const DriveInfo& drive, std::chrono::system_clock::time_point now) {
        const DriveBadges& badges = drive.badges;
        std::vector<std::string> labels;
        if (std::string block = blockBadge(drive); !block.empty()) {
            labels.push_back(std::move(block));
        }
        if (badges.adminNeeded) {
            labels.emplace_back("Admin needed");
        }
        if (badges.vssPresent) {
            labels.emplace_back("VSS");
        }
        if (canOptimize(drive)) {
            addToggle(labels, badges.lastAccessTracking, {.on = "Last access on", .off = "Last access off"});
        }
        addToggle(labels, badges.scheduledOptimization, {.on = "Auto-optimize on", .off = "Auto-optimize off"});
        if (badges.snapshotSaved) {
            labels.push_back("Snapshot " + formatAge(ageOf(*badges.snapshotSaved, now)));
        }
        return labels;
    }

} // namespace icd::core
