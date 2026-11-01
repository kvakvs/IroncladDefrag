#pragma once

#include "platform/win/Volumes.h"
#include "services/drives/IDriveProvider.h"

namespace icd::services {

    // The real drive list (plan.md 10.1): every mounted volume with a file system, with its clusters, disk
    // placement, NTFS data and media (HDD, SSD, virtual). Replaces the Phase 1 fake list in the GUI and the CLI.
    class SystemDriveProvider : public IDriveProvider {
    public:
        // Enumerates the volumes now (a few milliseconds; the queries are answered from cached metadata).
        [[nodiscard]] std::vector<core::DriveInfo> listDrives() override;
    };

    // One enumerated volume as the core model sees it; `elevated` decides the "admin needed" badge.
    [[nodiscard]] core::DriveInfo toDriveInfo(const platform::VolumeRecord& volume, bool elevated);

} // namespace icd::services
