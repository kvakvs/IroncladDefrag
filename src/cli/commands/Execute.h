#pragma once

#include <span>
#include <string_view>

namespace icd::cli {

    // `icd-cli execute <volume> [--strategy ID] [--level fast|moderate|max] | [--plan FILE] | [--resume [--log FILE]]`
    // (plan.md 10.3-10.4): analyses and plans the volume (or reads a plan saved by `icd-cli plan --save`, or resumes
    // the newest unfinished execution log of the volume), revalidates it against the volume as it is now, executes it
    // with FSCTL_MOVE_FILE, logs every move, and saves the snapshot updated from the verified runs. Options: --json,
    // --moves (every record), --chunk-mb N, --low-priority, --limit-mbps N, --max-age-hours H, --no-prefetch,
    // --threads N, --check-rescan (scan again and compare every moved file with its prediction), --max-moves N (only the
    // first N moves left; the rest stay in the log for a resume). Scripted test hooks:
    // --pause-after N [--pause-ms M], --cancel-after N, --take-destination MOVE (another file takes that move's first
    // destination cluster just before it starts: a stale bitmap). Needs administrator rights. Ctrl+C cancels between
    // move calls. Returns 0 done, 1 refused or failed, 2 bad arguments, 3 cancelled.
    int runExecute(std::span<const std::wstring_view> args);

    // One usage line for the command.
    [[nodiscard]] const char* executeUsage();

} // namespace icd::cli
