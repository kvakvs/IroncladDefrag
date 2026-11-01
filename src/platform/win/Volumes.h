#pragma once

#include "core/model/VolumeLayout.h"
#include "platform/win/Handle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icd::platform {

    // One volume as Windows reports it (plan.md 10.1): names, sizes, disk placement and NTFS data. The
    // drive provider turns it into the core model; tests compare it with fsutil.
    struct VolumeRecord {
        std::wstring volumeName; // "\\?\Volume{GUID}\".
        std::vector<std::wstring> mountPaths; // "C:\", or folders the volume is mounted on.
        std::wstring label; // Volume label.
        std::wstring fileSystem; // "NTFS", "exFAT", ...; empty when there is no medium.
        unsigned driveType = 0; // GetDriveTypeW result (DRIVE_FIXED, DRIVE_REMOVABLE, ...).
        std::uint64_t totalBytes = 0; // Volume size.
        std::uint64_t freeBytes = 0; // Free space.
        core::VolumeLayout layout; // Clusters, disk extents, NTFS data.
        std::wstring problem; // First query that failed (empty when everything answered).
    };

    // All volume GUID names on the machine (FindFirstVolumeW), mounted or not.
    std::vector<std::wstring> listVolumeNames();

    // Drive letters and folders a volume is mounted on.
    std::vector<std::wstring> mountPathsOf(const std::wstring& volumeName);

    // Opens a volume device ("\\?\Volume{GUID}\" or "C:\"). access 0 is enough for storage and extent
    // queries; GENERIC_READ (administrators only) for bitmap, USN and NTFS data. Invalid on failure, with
    // GetLastError() set.
    UniqueHandle openVolume(const std::wstring& volume, unsigned long access);

    // Everything Windows reports about one volume; missing facts are noted in `problem`, never thrown.
    VolumeRecord queryVolume(const std::wstring& volumeName);

    // Volumes that are mounted somewhere and have a file system, ordered by their first mount path.
    std::vector<VolumeRecord> enumerateVolumes();

    // Whether this process runs with administrator rights (volume handles with read access need them).
    bool isElevated();

} // namespace icd::platform
