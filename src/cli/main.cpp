#include "cli/commands/Analyze.h"
#include "cli/commands/CheckCaps.h"
#include "cli/commands/CheckCatchUp.h"
#include "cli/commands/Churn.h"
#include "cli/commands/CompareScanners.h"
#include "cli/commands/Execute.h"
#include "cli/commands/Plan.h"
#include "cli/commands/Schedule.h"
#include "services/AppInfo.h"
#include "services/AppLogging.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

namespace {

    // One command: its name, entry point and usage line.
    struct Command {
        std::wstring_view name; // First argument.
        int (*run)(std::span<const std::wstring_view>); // Runs it with the remaining arguments.
        const char* (*usage)(); // Its usage line.
    };

    // Every command.
    constexpr std::array commands{
        Command{L"analyze", icd::cli::runAnalyze, icd::cli::analyzeUsage},
        Command{L"compare-scanners", icd::cli::runCompareScanners, icd::cli::compareScannersUsage},
        Command{L"check-catchup", icd::cli::runCheckCatchUp, icd::cli::checkCatchUpUsage},
        Command{L"check-caps", icd::cli::runCheckCaps, icd::cli::checkCapsUsage},
        Command{L"churn", icd::cli::runChurn, icd::cli::churnUsage},
        Command{L"plan", icd::cli::runPlan, icd::cli::planUsage},
        Command{L"execute", icd::cli::runExecute, icd::cli::executeUsage},
        Command{L"schedule", icd::cli::runSchedule, icd::cli::scheduleUsage},
    };

    // Prints the command summary.
    void printUsage() {
        std::printf("Usage: icd-cli [--version] [--help]\n");
        for (const Command& c : commands) {
            std::printf("       %s\n", c.usage());
        }
    }

    // Handles the command line and returns the process exit code.
    int run(int argc, wchar_t** argv) {
        if (argc < 2) {
            printUsage();
            return 0;
        }
        const std::wstring_view arg = argv[1];
        if (arg == L"--version") {
            std::printf("icd-cli %.*s\n", static_cast<int>(icd::services::appVersion().size()),
                        icd::services::appVersion().data());
            return 0;
        }
        if (arg == L"--help" || arg == L"-h") {
            printUsage();
            return 0;
        }
        const std::vector<std::wstring_view> rest(argv + 2, argv + argc);
        const auto command = std::ranges::find(commands, arg, &Command::name);
        if (command != commands.end()) {
            return command->run(rest);
        }
        std::fprintf(stderr, "Unknown argument: %ls\n", argv[1]);
        printUsage();
        return 2;
    }

} // namespace

// Console entry point: logs start and end around the command; wide argv keeps non-ASCII paths intact.
int wmain(int argc, wchar_t** argv) {
    icd::services::initAppLogging("icd-cli");
    const int code = run(argc, argv);
    icd::services::shutdownAppLogging();
    return code;
}
