#include "core/format/DriveText.h"

#include "core/format/ByteSize.h"
#include "core/format/Duration.h"

#include <format>

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
        case MediaType::Unknown:
            break;
        }
        return "Unknown";
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

    std::vector<std::string> badgeLabels(const DriveInfo& drive, std::chrono::system_clock::time_point now) {
        const DriveBadges& badges = drive.badges;
        std::vector<std::string> labels;
        if (badges.adminNeeded) {
            labels.emplace_back("Admin needed");
        }
        if (badges.vssPresent) {
            labels.emplace_back("VSS");
        }
        if (canOptimize(drive)) {
            labels.emplace_back(badges.lastAccessTracking ? "Last access on" : "Last access off");
        }
        labels.emplace_back(badges.scheduledOptimization ? "Auto-optimize on" : "Auto-optimize off");
        if (badges.snapshotSaved) {
            labels.push_back("Snapshot " + formatAge(ageOf(*badges.snapshotSaved, now)));
        }
        return labels;
    }

} // namespace icd::core
