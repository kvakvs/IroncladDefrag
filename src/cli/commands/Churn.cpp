#include "cli/commands/Churn.h"

#include "cli/commands/CliCommon.h"
#include "core/format/Json.h"
#include "core/job/CancelToken.h"
#include "platform/win/Console.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/activity/ChurnStats.h"
#include "services/scan/MftScanner.h"

#include <cstdio>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>

namespace icd::cli {

    namespace {

        // A full MFT scan of the volume as it is now.
        core::Snapshot scan(HANDLE volume, const platform::VolumeRecord& v, core::Executor& executor,
                            const core::CancelToken& token) {
            const core::VolumeGeometry geometry{v.layout.bytesPerCluster, v.layout.totalClusters};
            const auto free = platform::readFreeRuns(volume, geometry.totalClusters, executor, token);
            return services::scanFilesMft(volume, v.volumeName, geometry, *v.layout.ntfs,
                                          {free.runs().begin(), free.runs().end()}, executor, token, nullptr)
                .snapshot;
        }

        // The report: journal figures, flagged files and churning directories.
        void report(const services::ChurnStats& stats, const std::vector<std::string>& directories,
                    std::uint64_t flagged, bool json) {
            if (!json) {
                std::printf("Journal: %llu records, %llu sessions; %llu files flagged as churn\n",
                            static_cast<unsigned long long>(stats.records),
                            static_cast<unsigned long long>(stats.sessions), static_cast<unsigned long long>(flagged));
                for (const std::string& d : directories) {
                    std::printf("  churn: %s\n", d.c_str());
                }
                return;
            }
            core::JsonWriter w;
            w.beginObject().key("journalRecords").value(stats.records).key("sessions").value(stats.sessions);
            w.key("churnFiles").value(flagged).key("churnDirectories").beginArray();
            for (const std::string& d : directories) {
                w.value(d);
            }
            w.endArray().endObject();
            std::printf("%s\n", w.str().c_str());
        }

        // Every directory with journal events and its counts (--all), to see why one does or does not churn.
        void listAll(const core::Snapshot& snapshot, const services::ChurnStats& stats) {
            for (core::FileIndex f = 0; f < snapshot.files.size(); ++f) {
                const auto it = stats.directories.find(snapshot.files.fileReference(f));
                if (it == stats.directories.end() || !snapshot.files.has(f, core::FileDirectory)) {
                    continue;
                }
                const services::ChurnCounts& c = it->second;
                std::printf("  %6u created %6u deleted %6u rewritten  %s\n", c.creates, c.deletes, c.overwrites,
                            snapshot.path(f).c_str());
            }
        }

        // What the command prints.
        enum class Output : std::uint8_t {
            Text, // Journal figures and churning directories.
            Json, // The same as JSON.
            All, // Text plus every directory's counts.
        };

        // Scans the volume, reads its journal and prints the statistics.
        void churnOf(const core::DriveInfo& drive, services::AppServices& app, Output output) {
            core::CancelSource cancel;
            const platform::ConsoleCancel ctrlC(cancel);
            const platform::VolumeRecord v = platform::queryVolume(platform::fromUtf8(drive.layout.volumeName));
            if (!v.layout.ntfs) {
                throw std::runtime_error("no NTFS data for the volume");
            }
            const platform::UniqueHandle volume = platform::openVolume(v.volumeName, GENERIC_READ);
            const core::Snapshot snapshot = scan(volume.get(), v, app.pool(), cancel.token());
            const services::ChurnStats stats = services::collectChurn(volume.get(), cancel.token());
            const auto flags = services::churnFlags(snapshot, stats, {}, app.pool(), cancel.token());
            const auto flagged = std::accumulate(flags.begin(), flags.end(), std::uint64_t{0});
            report(stats, services::churnDirectories(snapshot, stats, {}), flagged, output == Output::Json);
            if (output == Output::All) {
                listAll(snapshot, stats);
            }
        }

        // The output an argument list asks for, or nullopt for bad arguments.
        std::optional<Output> outputOf(std::span<const std::wstring_view> args) {
            if (args.empty() || args[0].starts_with(L"--") || args.size() > 2) {
                return std::nullopt;
            }
            if (args.size() == 1) {
                return Output::Text;
            }
            if (args[1] == L"--json" || args[1] == L"--all") {
                return args[1] == L"--json" ? Output::Json : Output::All;
            }
            return std::nullopt;
        }

    } // namespace

    const char* churnUsage() { return "icd-cli churn <volume> [--json | --all]"; }

    int runChurn(std::span<const std::wstring_view> args) {
        const auto output = outputOf(args);
        if (!output) {
            std::fprintf(stderr, "Usage: %s\n", churnUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const auto drive = findDrive(app, normalizeRoot(std::wstring(args[0])));
        const char* why = refusal(drive);
        if (why != nullptr || !drive) {
            std::fprintf(stderr, "icd-cli churn: %s\n", why != nullptr ? why : "no drive");
            return 1;
        }
        try {
            churnOf(*drive, app, *output);
            return 0;
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "icd-cli churn: %s\n", e.what());
        }
        return 1;
    }

} // namespace icd::cli
