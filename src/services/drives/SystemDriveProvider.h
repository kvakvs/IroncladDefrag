#pragma once

#include "platform/win/Volumes.h"
#include "services/drives/IDriveProvider.h"

#include <map>
#include <string>

namespace icd::services {

    // The real drive list (plan.md 10.1): every mounted volume with a file system, with its clusters, disk
    // placement, NTFS data and media (HDD, SSD, virtual). Replaces the Phase 1 fake list in the GUI and the CLI.
    class SystemDriveProvider : public IDriveProvider {
    public:
        // Enumerates the volumes now (a few milliseconds for the cached-metadata queries, plus one WMI query for
        // BitLocker).
        [[nodiscard]] std::vector<core::DriveInfo> listDrives() override;

        // Changes the ScheduledDefrag task and the Optimize Drives settings (platform/win/ScheduledOptimize).
        [[nodiscard]] std::string setScheduledOptimization(const core::DriveInfo& drive, bool scheduled) override;
    };

    // One enumerated volume as the core model sees it; `elevated` decides the "admin needed" badge.
    [[nodiscard]] core::DriveInfo toDriveInfo(const platform::VolumeRecord& volume, bool elevated);

    // Adds the setups that block moves to a volume's storage facts (T122): removable drive type, a dynamic disk, and
    // its BitLocker state from `encrypted` (platform::encryptedVolumes(), asked once per listing).
    void addTopology(const platform::VolumeRecord& volume, const std::map<std::wstring, bool>& encrypted,
                     core::StorageFacts& storage);

    // The names Windows' schedule may list a volume by: its GUID path and its mount paths.
    [[nodiscard]] std::vector<std::wstring> namesOf(const platform::VolumeRecord& volume);

} // namespace icd::services
