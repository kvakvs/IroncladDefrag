#include "cli/commands/CheckCatchUp.h"

#include "cli/commands/CliCommon.h"
#include "core/job/CancelToken.h"
#include "platform/win/Console.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/scan/MftScanner.h"
#include "services/scan/ScanCompare.h"
#include "services/snapshot/SnapshotStore.h"
#include "services/snapshot/UsnCatchUp.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace icd::cli {

    namespace {

        using Clock = std::chrono::steady_clock;

        // Seconds since start.
        double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

        // A full MFT scan of the volume as it is now.
        core::Snapshot fullScan(HANDLE volume, const platform::VolumeRecord& v, const core::NtfsLayout& ntfs,
                                core::Executor& executor, const core::CancelToken& token) {
            const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
            const auto free = platform::readFreeRuns(volume, geometry.totalClusters, executor, token);
            return services::scanFilesMft(volume, v.volumeName, geometry, ntfs,
                                          {free.runs().begin(), free.runs().end()}, executor, token, nullptr)
                .snapshot;
        }

        // Prints the catch-up's outcome; false when it needed a rescan.
        bool printCatchUp(const services::CatchUpResult& c, double seconds) {
            if (c.outcome == services::CatchUpOutcome::RescanNeeded) {
                std::printf("Catch-up impossible: %s\n", c.reason.c_str());
                return false;
            }
            std::printf("Catch-up: %s, %llu journal records, %llu records named, %llu records read, %llu dirty ranges, "
                        "%.2f s\n",
                        c.outcome == services::CatchUpOutcome::UpToDate ? "up to date" : "caught up",
                        static_cast<unsigned long long>(c.journalRecords),
                        static_cast<unsigned long long>(c.changedRecords),
                        static_cast<unsigned long long>(c.recordsRead), static_cast<unsigned long long>(c.dirty.size()),
                        seconds);
            return true;
        }

        // Catch-up, full scan and comparison; true when they agree.
        bool check(services::AppServices& app, const platform::VolumeRecord& v, const core::NtfsLayout& ntfs,
                   const core::CancelToken& token) {
            const auto saved = services::SnapshotStore(services::SnapshotStore::defaultDirectory())
                                   .load(platform::toUtf8(v.volumeName), app.pool());
            if (!saved) {
                throw std::runtime_error("no saved snapshot of the volume; run icd-cli analyze first");
            }
            const platform::UniqueHandle volume = platform::openVolume(v.volumeName, GENERIC_READ);
            const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
            auto start = Clock::now();
            const services::CatchUpResult caught = services::catchUpSnapshot(
                volume.get(), v.volumeName, saved->snapshot, ntfs, geometry, app.pool(), token);
            if (!printCatchUp(caught, since(start))) {
                return false;
            }
            start = Clock::now();
            const core::Snapshot full = fullScan(volume.get(), v, ntfs, app.pool(), token);
            std::printf("Full MFT scan: %llu files, %llu extents, %.2f s\n",
                        static_cast<unsigned long long>(full.files.size()),
                        static_cast<unsigned long long>(full.extents.size()), since(start));
            // Access-time updates are not journaled, so a catch-up keeps the saved ones (plan.md 8.2 treats them as a
            // hint anyway); everything else must be exact.
            const services::ScanComparison c = services::compareExact(full, caught.snapshot, {.accessTimes = false});
            std::printf("Catch-up against full scan: %s (%llu files, %llu differences)\n",
                        c.same() ? "same" : "DIFFERENT", static_cast<unsigned long long>(c.filesCompared),
                        static_cast<unsigned long long>(c.differences));
            for (const std::string& line : c.samples) {
                std::printf("    %s\n", line.c_str());
            }
            return c.same();
        }

    } // namespace

    const char* checkCatchUpUsage() { return "icd-cli check-catchup <volume>"; }

    int runCheckCatchUp(std::span<const std::wstring_view> args) {
        if (args.size() != 1 || args[0].starts_with(L"--")) {
            std::fprintf(stderr, "Usage: %s\n", checkCatchUpUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const auto drive = findDrive(app, normalizeRoot(std::wstring(args[0])));
        const char* why = refusal(drive);
        if (why != nullptr || !drive) { // refusal() covers a missing drive; the check keeps the access below safe.
            std::fprintf(stderr, "icd-cli check-catchup: %s\n", why != nullptr ? why : "no drive");
            return 1;
        }
        const core::DriveInfo& found = *drive;
        try {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            const platform::VolumeRecord v = platform::queryVolume(platform::fromUtf8(found.layout.volumeName));
            if (!v.layout.ntfs) {
                throw std::runtime_error("no NTFS data for the volume");
            }
            const bool same = check(app, v, v.layout.ntfs.value(), cancel.token());
            std::printf("%s\n", same ? "Catch-up equals a full rescan." : "Catch-up DIFFERS from a full rescan.");
            return same ? 0 : 1;
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli check-catchup: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
