#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli check-caps <volume> [--threads N] [--memory-budget BYTES]`: the memory-mode check of T066 (plan.md 6.6:
    // caps change only speed, never results). Scans the volume fully twice with the MFT scanner, once with the app's
    // pool and no memory limit, once with N workers (default 2), the budget (default 2 GB) and the snapshot's large
    // arrays in memory-mapped files, and compares the snapshots exactly. Saves nothing. Needs administrator rights.
    // Returns 0 when they are identical, 1 when not or on failure, 2 on bad arguments.
    int runCheckCaps(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* checkCapsUsage();

} // namespace icd::cli
