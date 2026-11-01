#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace icd::core {

    // NTFS facts from FSCTL_GET_NTFS_VOLUME_DATA (plan.md 10.1). The MFT and its zone are fixed obstacles
    // for the planner, and the record size drives the snapshot size estimate.
    struct NtfsLayout {
        std::uint64_t serialNumber = 0; // NTFS volume serial (64-bit, as fsutil shows it).
        std::uint16_t majorVersion = 0; // NTFS version, 3.1 on current Windows.
        std::uint16_t minorVersion = 0; // NTFS minor version.
        std::uint32_t bytesPerFileRecord = 0; // MFT record size (1 KB or 4 KB).
        std::uint64_t mftValidDataLength = 0; // Bytes of $MFT in use; records = this / record size.
        std::uint64_t mftStartLcn = 0; // First cluster of $MFT.
        std::uint64_t mft2StartLcn = 0; // First cluster of $MFTMirr.
        std::uint64_t mftZoneStart = 0; // Reserved MFT zone [start, end) in clusters.
        std::uint64_t mftZoneEnd = 0; // End of the MFT zone.
        std::uint64_t totalReserved = 0; // Clusters NTFS keeps back (reserved, not free for moves).

        // Layouts compare as values (tests).
        friend bool operator==(const NtfsLayout&, const NtfsLayout&) = default;
    };

    // Where a volume sits and how it is cut into clusters (plan.md 10.1). Fields stay zero or empty when
    // Windows did not answer, so a drive with partial facts can still be listed.
    struct VolumeLayout {
        std::string volumeName; // "\\?\Volume{GUID}\": stable id, also without a drive letter.
        std::uint32_t bytesPerSector = 0; // Logical sector size.
        std::uint32_t bytesPerCluster = 0; // Cluster size.
        std::uint64_t totalClusters = 0; // Clusters on the volume.
        std::uint64_t freeClusters = 0; // Free clusters when listed.
        std::optional<std::uint32_t> diskNumber; // Physical disk, when the volume sits on exactly one.
        std::uint64_t partitionOffset = 0; // Byte offset of the volume on that disk (fast outer tracks first).
        std::uint64_t diskBytes = 0; // Size of that disk, for "% of disk" positions.
        std::uint32_t diskExtentCount = 0; // Disk extents; more than one means spanned or striped.
        std::optional<NtfsLayout> ntfs; // NTFS data; empty on other file systems or without access.
    };

} // namespace icd::core
