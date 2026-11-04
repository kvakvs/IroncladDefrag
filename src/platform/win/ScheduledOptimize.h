#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace icd::platform {

    // Windows' scheduled optimization ("Optimize Drives", plan.md 10.4) as Windows itself stores it (T120, found by
    // diffing what the Optimize Drives dialog writes): the task \Microsoft\Windows\Defrag\ScheduledDefrag runs
    // "defrag.exe <arguments>", and the arguments carry the drive choice:
    //   all drives (and new ones)      "-c -h -o -$"
    //   all but some (and new ones)    "\\?\Volume{a}\ \\?\Volume{b}\ -e -h -o -$"
    //   only some (new ones not)       "\\?\Volume{a}\ \\?\Volume{b}\ -h -o -$"
    // The dialog reads its check boxes back from HKLM\SOFTWARE\Microsoft\Dfrg\TaskSettings: fAllVolumes (DWORD 1/0),
    // fExclude (DWORD 1: Volumes are left out, 0: only Volumes) and Volumes (REG_SZ, GUID paths separated by one
    // space; " " when none). Both are written together, as the dialog does.

    // How the schedule picks its drives.
    enum class ScheduleMode : std::uint8_t {
        All, // Every drive, new ones too ("-c").
        AllExcept, // Every drive but the listed ones, new ones too ("-e").
        Only, // Only the listed drives.
    };

    // The drive choice of the schedule.
    struct ScheduleSelection {
        ScheduleMode mode = ScheduleMode::All; // How `volumes` is read.
        std::vector<std::wstring> volumes; // Volume GUID paths (or drive letters), as written; empty for All.
        std::wstring flags = L"-h -o -$"; // The other defrag arguments, kept as found.

        // Selections compare as values (tests).
        friend bool operator==(const ScheduleSelection&, const ScheduleSelection&) = default;
    };

    // The selection in a task's defrag arguments ("-c" wins over listed volumes; no "-c", "-e" or volume lists
    // nothing). Kept apart for tests.
    [[nodiscard]] ScheduleSelection parseScheduleArguments(std::wstring_view arguments);

    // The defrag arguments for a selection, in the form the Optimize Drives dialog writes.
    [[nodiscard]] std::wstring scheduleArguments(const ScheduleSelection& selection);

    // Whether the selection covers a volume known by any of `names` (its GUID path and mount paths, "D:\" or "D:").
    [[nodiscard]] bool scheduleCovers(const ScheduleSelection& selection, std::span<const std::wstring> names);

    // The selection with the volume (`names`, the first being its GUID path) taken out of the schedule (scheduled
    // false) or put back (true), changing as little as possible: All becomes AllExcept that volume, an AllExcept list
    // that empties becomes All, Only gains or loses it. nullopt when the volume is the last one of an Only list
    // (taking it out would leave "defrag" without a drive: turn the schedule off instead).
    [[nodiscard]] std::optional<ScheduleSelection> withScheduled(ScheduleSelection selection,
                                                                 std::span<const std::wstring> names, bool scheduled);

    // What the scheduled optimization is set to now.
    struct ScheduleState {
        bool taskEnabled = false; // The task is enabled ("Run on a schedule").
        ScheduleSelection selection; // Its drives.
    };

    // The schedule from the task; nullopt when the task or its defrag action cannot be read.
    [[nodiscard]] std::optional<ScheduleState> readSchedule();

    // Writes a selection to the task's arguments and to TaskSettings (administrators only); the task's trigger,
    // settings and principal stay as they are. Returns an error text, empty on success.
    [[nodiscard]] std::wstring writeSchedule(const ScheduleSelection& selection);

} // namespace icd::platform
