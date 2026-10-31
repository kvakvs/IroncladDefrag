#include "services/AppInfo.h"

#include <cstdio>
#include <string_view>

namespace {

    // Prints the command summary; real commands (analyze, plan, ...) arrive in later tasks.
    void printUsage() { std::printf("Usage: icd-cli [--version] [--help]\n"); }

} // namespace

int wmain(int argc, wchar_t** argv) {
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
    std::fprintf(stderr, "Unknown argument: %ls\n", argv[1]);
    printUsage();
    return 2;
}
