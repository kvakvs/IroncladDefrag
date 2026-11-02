#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli plan <volume> [--strategy ID] [--level fast|moderate|max] [--json] [--moves] [--threads N]`: analyses a
    // volume (the saved snapshot is caught up when possible), plans the three effort levels of a built-in strategy with
    // the same service the GUI uses, and prints the plan (text, or JSON with --json; --moves adds every move of the
    // reported levels). --level reports one level only. `--synthetic N` plans a seeded synthetic volume of N files
    // instead of a real one (no administrator rights needed), for checks and scripts. Progress goes to stderr, Ctrl+C
    // cancels. Returns 0 done, 1 failed or cancelled (or a plan failed validation), 2 bad arguments.
    int runPlan(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* planUsage();

} // namespace icd::cli
