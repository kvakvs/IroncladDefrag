#include "services/drives/SystemDriveProvider.h"

#include "core/log/Log.h"
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
        std::vector<core::DriveInfo> drives;
        for (const platform::VolumeRecord& volume : platform::enumerateVolumes()) {
            if (!volume.problem.empty()) {
                core::logging::warning(
                    "volume query incomplete",
                    {{"volume", platform::toUtf8(volume.volumeName)}, {"problem", platform::toUtf8(volume.problem)}});
            }
            drives.push_back(toDriveInfo(volume, elevated));
        }
        return drives;
    }

} // namespace icd::services
