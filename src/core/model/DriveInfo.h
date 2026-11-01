#pragma once

#include "core/model/VolumeLayout.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace icd::core {

    // Kind of storage behind a volume; decides whether the drive gets the full workflow or only TRIM
    // (plan.md 10.1, 10.5).
    enum class MediaType : std::uint8_t {
        Unknown, // Detection failed; treated like an HDD but flagged.
        Hdd, // Seek penalty: layout matters, full workflow.
        Ssd, // No seek penalty: TRIM only.
    };

    // Facts shown as badges in the all-disks panel (plan.md 10.1); each one changes what the app can do or
    // how much a plan can rely on.
    struct DriveBadges {
        bool adminNeeded = false; // Volume access needs elevation the process does not have.
        bool vssPresent = false; // Shadow copies exist; moves can grow them (plan.md 9.4).
        // NTFS updates last-access times, so access-based strategies work; empty until detected (T072).
        std::optional<bool> lastAccessTracking;
        // Windows' own scheduled defrag runs on this volume; empty until detected (T120).
        std::optional<bool> scheduledOptimization;
        std::optional<std::chrono::system_clock::time_point> snapshotSaved; // When a saved snapshot was taken.
    };

    // One volume as the all-disks panel and the workflow see it. Filled by a drive provider: a fake one in
    // Phase 1, real enumeration in Phase 3.
    struct DriveInfo {
        std::string root; // Volume root, "D:\"; identifies the drive across refreshes.
        std::string label; // Volume label, may be empty.
        std::uint64_t totalBytes = 0; // Volume size.
        std::uint64_t freeBytes = 0; // Free space.
        std::string fileSystem; // "NTFS", "exFAT", ...
        MediaType media = MediaType::Unknown; // HDD or SSD.
        std::optional<double> fragmentationPercent; // From the last analysis; empty before one.
        std::optional<std::chrono::system_clock::time_point> lastAnalysed; // When it was last analysed.
        DriveBadges badges; // Extra facts for the badges column.
        VolumeLayout layout; // Disk placement, clusters and NTFS data (real drives only).

        // Space in use.
        [[nodiscard]] std::uint64_t usedBytes() const { return totalBytes > freeBytes ? totalBytes - freeBytes : 0; }

        // Used share of the volume in [0, 1]; 0 for an empty size.
        [[nodiscard]] double usedFraction() const;
    };

    // Whether the full workflow (analyze, plan, execute) applies: an NTFS volume on an HDD or unknown media.
    [[nodiscard]] bool canOptimize(const DriveInfo& drive);

    // Whether the drive gets the TRIM action instead of the workflow.
    [[nodiscard]] bool isSsd(const DriveInfo& drive);

} // namespace icd::core
