#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli analyze <volume> [--json] [--dump-extents]`: analyses an NTFS volume with the same service the GUI
    // uses and prints the summary (text, or JSON with --json; --dump-extents adds every file's runs and implies
    // --json). Progress goes to stderr, Ctrl+C cancels. Needs administrator rights. Returns the exit code:
    // 0 done, 1 failed or cancelled, 2 bad arguments.
    int runAnalyze(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* analyzeUsage();

} // namespace icd::cli
