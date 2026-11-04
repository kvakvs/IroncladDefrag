#pragma once

#include "core/model/VolumeLayout.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icd::core {

    // Kind of storage behind a volume; decides whether the drive gets the full workflow or only TRIM
    // (plan.md 10.1, 10.5).
    enum class MediaType : std::uint8_t {
        Unknown, // Detection failed; treated like an HDD but flagged.
        Hdd, // Seek penalty: layout matters, full workflow.
        Ssd, // No seek penalty: TRIM only.
        Virtual, // VHD/VHDX file: full workflow (it is the test bed); its real media is the host's.
    };

    // What the storage stack reports about the disk behind a volume (plan.md 10.1). Kept as raw facts so the
    // classification rule is pure core code and tested.
    struct StorageFacts {
        std::optional<bool> seekPenalty; // StorageDeviceSeekPenaltyProperty; empty when the device did not say.
        std::optional<bool> trimEnabled; // StorageDeviceTrimProperty.
        std::string bus; // "SATA", "NVMe", "USB", "Virtual", ...; empty when unknown.
        bool virtualDisk = false; // File-backed virtual disk (VHD/VHDX).
        bool removable = false; // Removable media (card readers, some USB sticks).
        std::string model; // Product id, e.g. "ST12000NE0008-2PK103".
        bool dynamicDisk = false; // The disk holds LDM partitions (spanned, striped, mirrored or RAID-5 volumes).
        std::optional<bool> encrypted; // BitLocker protects or is converting the volume; empty when not known.
    };

    // Storage setups the app analyses and plans for but never moves files on (plan.md 4 and Phase 9): each one blocks
    // execution and is explained to the user.
    enum class MoveBlock : std::uint8_t {
        Raid, // Behind a RAID controller: clusters are spread over member disks, so zones mean nothing.
        StorageSpaces, // A Storage Spaces disk: slabs map to pool disks, not to one platter's tracks.
        DynamicDisk, // A dynamic disk or a volume over several disks: positions do not map to one disk.
        Removable, // Removable media: it can be pulled out in the middle of a move.
        Encrypted, // BitLocker: moves on encrypted volumes are not tested on the test bed (plan.md 10.2).
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
        StorageFacts storage; // Raw media facts behind `media` (real drives only).
        std::optional<double> fragmentationPercent; // From the last analysis; empty before one.
        std::optional<std::chrono::system_clock::time_point> lastAnalysed; // When it was last analysed.
        // When Windows' optimizer last ran on the volume (Optimize Drives' "Last run"): an SSD's last TRIM.
        std::optional<std::chrono::system_clock::time_point> lastOptimized;
        DriveBadges badges; // Extra facts for the badges column.
        VolumeLayout layout; // Disk placement, clusters and NTFS data (real drives only).

        // Space in use.
        [[nodiscard]] std::uint64_t usedBytes() const { return totalBytes > freeBytes ? totalBytes - freeBytes : 0; }

        // Used share of the volume in [0, 1]; 0 for an empty size.
        [[nodiscard]] double usedFraction() const;
    };

    // Whether analysis and planning apply: an NTFS volume on an HDD or unknown media.
    [[nodiscard]] bool canOptimize(const DriveInfo& drive);

    // The setups that block moves on the drive, in MoveBlock order; empty when moves are allowed.
    [[nodiscard]] std::vector<MoveBlock> moveBlocks(const DriveInfo& drive);

    // Whether the full workflow applies, execution included: canOptimize() and no move block.
    [[nodiscard]] bool canMove(const DriveInfo& drive);

    // Media type from the storage facts: virtual disks first (their seek penalty describes the host file),
    // then the seek penalty, then TRIM support as an SSD hint; Unknown when the device said nothing.
    [[nodiscard]] MediaType classifyMedia(const StorageFacts& facts);

    // Whether the drive gets the TRIM action instead of the workflow.
    [[nodiscard]] bool isSsd(const DriveInfo& drive);

} // namespace icd::core
