#include "cli/commands/Analyze.h"
#include "services/AppInfo.h"
#include "services/AppLogging.h"

#include <cstdio>
#include <string_view>
#include <vector>

namespace {

    // Prints the command summary; plan and execute arrive in later phases.
    void printUsage() { std::printf("Usage: icd-cli [--version] [--help]\n       %s\n", icd::cli::analyzeUsage()); }

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
        if (arg == L"analyze") {
            const std::vector<std::wstring_view> rest(argv + 2, argv + argc);
            return icd::cli::runAnalyze(rest);
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
