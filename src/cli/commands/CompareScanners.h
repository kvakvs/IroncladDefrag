#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli compare-scanners <volume> [--threads 1,2,8]`: the scanner cross-check of plan.md 10.2 and 12. Scans the
    // volume with the API path (the reference), then with the MFT path once per worker count (0 means the app's pool),
    // compares each MFT result with the API one field by field and with the first MFT result exactly, and prints the
    // differences and the scan times. Needs administrator rights; meant for quiet (read-only mounted) test volumes,
    // where any difference is a bug. Returns 0 when everything agrees, 1 on differences or failure, 2 on bad
    // arguments.
    int runCompareScanners(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* compareScannersUsage();

} // namespace icd::cli
