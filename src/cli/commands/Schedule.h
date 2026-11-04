#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli schedule <volume> [--remove | --restore]`: Windows' scheduled optimization of a volume (T120, plan.md
    // 10.4). Prints whether the schedule covers it; --remove takes it out, --restore puts it back, exactly as the GUI's
    // Drive > Windows schedule does (the flag is the confirmation). Needs administrator rights to change it. Returns 0
    // on success, 1 on failure, 2 on bad arguments.
    int runSchedule(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* scheduleUsage();

} // namespace icd::cli
