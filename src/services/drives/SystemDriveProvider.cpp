#include "services/drives/SystemDriveProvider.h"

#include "core/log/Log.h"
#include "platform/win/LastAccessSetting.h"
#include "platform/win/StorageProperties.h"
#include "platform/win/TextConvert.h"

namespace icd::services {

    core::DriveInfo toDriveInfo(const platform::VolumeRecord& volume, bool elevated) {
        core::DriveInfo drive;
        drive.root = volume.mountPaths.empty() ? platform::toUtf8(volume.volumeName)
                                               : platform::toUtf8(volume.mountPaths.front());
        drive.label = platform::toUtf8(volume.label);
        drive.totalBytes = volume.totalBytes;
        drive.freeBytes = volume.freeBytes;
        drive.fileSystem = platform::toUtf8(volume.fileSystem);
        drive.layout = volume.layout;
        drive.layout.volumeName = platform::toUtf8(volume.volumeName);
        drive.badges.adminNeeded = !elevated && volume.fileSystem == L"NTFS" && !volume.layout.ntfs;
        return drive;
    }

    std::vector<core::DriveInfo> SystemDriveProvider::listDrives() {
        const bool elevated = platform::isElevated();
        const std::optional<bool> lastAccess = platform::readLastAccessSetting().updatesEnabled; // System-wide.
        std::vector<core::DriveInfo> drives;
        for (const platform::VolumeRecord& volume : platform::enumerateVolumes()) {
            if (!volume.problem.empty()) {
                core::logging::warning(
                    "volume query incomplete",
                    {{"volume", platform::toUtf8(volume.volumeName)}, {"problem", platform::toUtf8(volume.problem)}});
            }
            core::DriveInfo drive = toDriveInfo(volume, elevated);
            drive.storage = volume.layout.diskNumber ? platform::queryDiskStorage(*volume.layout.diskNumber)
                                                     : platform::queryVolumeStorage(volume.volumeName);
            drive.media = core::classifyMedia(drive.storage);
            drive.badges.lastAccessTracking = volume.fileSystem == L"NTFS" ? lastAccess : std::nullopt;
            drives.push_back(std::move(drive));
        }
        return drives;
    }

} // namespace icd::services
