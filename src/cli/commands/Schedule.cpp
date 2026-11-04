#include "cli/commands/Schedule.h"

#include "cli/commands/CliCommon.h"
#include "services/AppServices.h"

#include <cstdio>
#include <optional>
#include <string>

namespace icd::cli {

    namespace {

        // "on", "off" or "unknown" for a schedule badge.
        const char* stateText(const std::optional<bool>& scheduled) {
            if (!scheduled) {
                return "unknown";
            }
            return *scheduled ? "on" : "off";
        }

        // What the command line asks for.
        struct Wanted {
            bool bad = false; // An unknown argument.
            std::optional<bool> scheduled; // True to restore, false to remove; empty to only print.
        };

        // Reads the flags after the volume.
        Wanted wantedOf(std::span<const std::wstring_view> flags) {
            Wanted wanted;
            for (const std::wstring_view flag : flags) {
                if (flag == L"--remove" || flag == L"--restore") {
                    wanted.scheduled = flag == L"--restore";
                }
                else {
                    wanted.bad = true;
                }
            }
            return wanted;
        }

    } // namespace

    const char* scheduleUsage() { return "icd-cli schedule <volume> [--remove | --restore]"; }

    int runSchedule(std::span<const std::wstring_view> args) {
        const Wanted wanted = args.empty() ? Wanted{.bad = true, .scheduled = std::nullopt} : wantedOf(args.subspan(1));
        if (wanted.bad || args[0].starts_with(L"--")) {
            std::fprintf(stderr, "Usage: %s\n", scheduleUsage());
            return 2;
        }
        services::AppServices app(services::DriveSource::System);
        const std::string root = normalizeRoot(std::wstring(args[0]));
        const auto drive = findDrive(app, root);
        if (!drive) {
            std::fprintf(stderr, "icd-cli schedule: no mounted volume at that path\n");
            return 1;
        }
        if (wanted.scheduled) {
            const std::string error = app.drives().setScheduledOptimization(*drive, *wanted.scheduled);
            if (!error.empty()) {
                std::fprintf(stderr, "icd-cli schedule: %s\n", error.c_str());
                return 1;
            }
        }
        const auto after = findDrive(app, root);
        std::printf("Windows scheduled optimization of %s: %s\n", root.c_str(),
                    stateText(after ? after->badges.scheduledOptimization : std::nullopt));
        return 0;
    }

} // namespace icd::cli
