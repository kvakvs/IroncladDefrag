#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli check-catchup <volume>`: the USN catch-up check of T065. Loads the volume's saved snapshot (written by
    // `icd-cli analyze` or the GUI), catches it up from the journal, scans the volume fully with the MFT scanner and
    // compares the two exactly; prints the outcome, counts and times. Saves nothing. Needs administrator rights.
    // Returns 0 when the catch-up equals the full scan, 1 when it differs, needed a rescan or failed, 2 on bad
    // arguments.
    int runCheckCatchUp(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* checkCatchUpUsage();

} // namespace icd::cli
