#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli churn <volume> [--json]`: the USN churn statistics of T073. Scans the volume with the MFT scanner, reads
    // the journal's retained window and lists the directories that churn (files created and deleted again) and how many
    // files are flagged as churn. Reads only; needs administrator rights. Returns 0 on success, 1 on failure, 2 on bad
    // arguments.
    int runChurn(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* churnUsage();

} // namespace icd::cli
