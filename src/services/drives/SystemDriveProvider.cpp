#include "services/drives/SystemDriveProvider.h"

#include "core/log/Log.h"
#include "platform/win/LastAccessSetting.h"
#include "platform/win/ScheduledOptimize.h"
#include "platform/win/ShadowCopies.h"
#include "platform/win/StorageProperties.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeTopology.h"

#include <windows.h>

#include <map>

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

    std::vector<std::wstring> namesOf(const platform::VolumeRecord& volume) {
        std::vector<std::wstring> names{volume.volumeName};
        names.insert(names.end(), volume.mountPaths.begin(), volume.mountPaths.end());
        return names;
    }

    std::string SystemDriveProvider::setScheduledOptimization(const core::DriveInfo& drive, bool scheduled) {
        const std::optional<platform::ScheduleState> state = platform::readSchedule();
        if (!state) {
            return "Windows' scheduled optimization (the ScheduledDefrag task) cannot be read";
        }
        if (scheduled && !state->taskEnabled) {
            return "Windows' scheduled optimization is turned off for every drive; turn it on in Optimize Drives";
        }
        const std::vector<std::wstring> names{platform::fromUtf8(drive.layout.volumeName),
                                              platform::fromUtf8(drive.root)};
        const std::optional<platform::ScheduleSelection> changed =
            platform::withScheduled(state->selection, names, scheduled);
        if (!changed) {
            return "this is the only drive Windows optimizes on its schedule; turn the schedule off in Optimize "
                   "Drives instead";
        }
        if (*changed == state->selection) {
            return {};
        }
        const std::string error = platform::toUtf8(platform::writeSchedule(*changed));
        core::logging::info("windows schedule changed",
                            {{"drive", drive.root},
                             {"scheduled", scheduled},
                             {"before", platform::toUtf8(platform::scheduleArguments(state->selection))},
                             {"after", platform::toUtf8(platform::scheduleArguments(*changed))},
                             {"error", error}});
        return error;
    }

    void addTopology(const platform::VolumeRecord& volume, const std::map<std::wstring, bool>& encrypted,
                     core::StorageFacts& storage) {
        storage.removable = storage.removable || volume.driveType == DRIVE_REMOVABLE;
        if (volume.layout.diskNumber) {
            storage.dynamicDisk = platform::isDynamicDisk(*volume.layout.diskNumber).value_or(false);
        }
        if (const auto found = encrypted.find(platform::volumeKey(volume.volumeName)); found != encrypted.end()) {
            storage.encrypted = found->second;
        }
    }

    std::vector<core::DriveInfo> SystemDriveProvider::listDrives() {
        const SystemFacts facts{.elevated = platform::isElevated(),
                                .lastAccess = platform::readLastAccessSetting().updatesEnabled,
                                .encrypted = platform::encryptedVolumes(),
                                .schedule = platform::readSchedule(),
                                .shadows = platform::listShadowCopies()};
        std::vector<core::DriveInfo> drives;
        for (const platform::VolumeRecord& volume : platform::enumerateVolumes()) {
            if (!volume.problem.empty()) {
                core::logging::warning(
                    "volume query incomplete",
                    {{"volume", platform::toUtf8(volume.volumeName)}, {"problem", platform::toUtf8(volume.problem)}});
            }
            drives.push_back(driveOf(volume, facts));
        }
        return drives;
    }

    core::DriveInfo driveOf(const platform::VolumeRecord& volume, const SystemFacts& facts) {
        core::DriveInfo drive = toDriveInfo(volume, facts.elevated);
        drive.storage = volume.layout.diskNumber ? platform::queryDiskStorage(*volume.layout.diskNumber)
                                                 : platform::queryVolumeStorage(volume.volumeName);
        addTopology(volume, facts.encrypted, drive.storage);
        drive.media = core::classifyMedia(drive.storage);
        drive.badges.lastAccessTracking = volume.fileSystem == L"NTFS" ? facts.lastAccess : std::nullopt;
        if (facts.shadows) {
            drive.badges.vssPresent =
                platform::countShadowsInListing({.text = *facts.shadows}, drive.layout.volumeName) > 0;
        }
        if (facts.schedule) {
            drive.badges.scheduledOptimization =
                facts.schedule->taskEnabled && platform::scheduleCovers(facts.schedule->selection, namesOf(volume));
        }
        return drive;
    }

} // namespace icd::services
