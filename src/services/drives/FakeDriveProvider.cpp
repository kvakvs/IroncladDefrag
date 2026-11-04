#include "services/drives/FakeDriveProvider.h"

#include <string>
#include <utility>

namespace icd::services {

    namespace {

        constexpr std::uint64_t GB = 1024ULL * 1024 * 1024;
        constexpr std::uint64_t TB = 1024 * GB;

        // Volume size and free space, named at the call site so the two cannot be swapped.
        struct Space {
            std::uint64_t total = 0; // Volume size in bytes.
            std::uint64_t free = 0; // Free bytes.
        };

        // A drive with its basic facts; callers add analysis results and badges.
        core::DriveInfo makeDrive(std::string root, std::string label, Space space, std::string fileSystem,
                                  core::MediaType media) {
            core::DriveInfo drive;
            drive.root = std::move(root);
            drive.label = std::move(label);
            drive.totalBytes = space.total;
            drive.freeBytes = space.free;
            drive.fileSystem = std::move(fileSystem);
            drive.media = media;
            return drive;
        }

    } // namespace

    FakeDriveProvider::FakeDriveProvider(std::chrono::system_clock::time_point now) : now_(now) {}

    std::vector<core::DriveInfo> FakeDriveProvider::listDrives() {
        using namespace std::chrono_literals;
        using core::MediaType;

        core::DriveInfo system =
            makeDrive("C:\\", "System", {.total = 931 * GB, .free = 402 * GB}, "NTFS", MediaType::Ssd);
        system.badges.vssPresent = true;
        system.badges.scheduledOptimization = true;
        system.lastOptimized = now_ - 24h * 6;

        core::DriveInfo data = makeDrive("D:\\", "Data", {.total = 4 * TB, .free = 1126 * GB}, "NTFS", MediaType::Hdd);
        data.fragmentationPercent = 12.4;
        data.lastAnalysed = now_ - 50h;
        data.badges.vssPresent = true;
        data.badges.lastAccessTracking = true;
        data.badges.snapshotSaved = now_ - 50h;

        core::DriveInfo media = makeDrive("E:\\", "Media", {.total = 8 * TB, .free = 389 * GB}, "NTFS", MediaType::Hdd);
        media.fragmentationPercent = 31.7;
        media.lastAnalysed = now_ - 3h;
        media.badges.snapshotSaved = now_ - 3h;

        core::DriveInfo backup =
            makeDrive("F:\\", "Backup", {.total = 2 * TB, .free = 1405 * GB}, "NTFS", MediaType::Hdd);
        backup.badges.scheduledOptimization = true;

        core::DriveInfo transfer =
            makeDrive("G:\\", "Transfer", {.total = 238 * GB, .free = 120 * GB}, "exFAT", MediaType::Hdd);

        // The design target (plan.md 6.1): 30 TB with 18 TB in use, about 20 M files.
        core::DriveInfo vault = makeDrive("H:\\", "Vault", {.total = 30 * TB, .free = 12 * TB}, "NTFS", MediaType::Hdd);
        vault.fragmentationPercent = 25.0;
        vault.lastAnalysed = now_ - 24h * 9;

        std::vector<core::DriveInfo> drives;
        drives.reserve(6);
        for (core::DriveInfo* d : {&system, &data, &media, &backup, &transfer, &vault}) {
            if (d->badges.scheduledOptimization && unscheduled_.contains(d->root)) {
                d->badges.scheduledOptimization = false;
            }
            drives.push_back(std::move(*d));
        }
        return drives;
    }

    std::string FakeDriveProvider::setScheduledOptimization(const core::DriveInfo& drive, bool scheduled) {
        if (!drive.badges.scheduledOptimization) {
            return "the Windows schedule of this drive is not known";
        }
        if (scheduled) {
            unscheduled_.erase(drive.root);
        }
        else {
            unscheduled_.insert(drive.root);
        }
        return {};
    }

} // namespace icd::services
