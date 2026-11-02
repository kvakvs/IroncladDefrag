#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/execute/PlanReplay.h"
#include "core/model/DriveInfo.h"
#include "core/snapshot/Snapshot.h"
#include "services/job/JobRunner.h"
#include "services/scan/AnalysisJob.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace icd::services {

    // FILETIME ticks per hour, for plan ages.
    inline constexpr std::int64_t fileTimeTicksPerHour = 36'000'000'000LL;

    // How old a plan may be and how the volume is read again.
    struct RevalidateOptions {
        std::int64_t maxAgeTicks = 24 * fileTimeTicksPerHour; // Older plans are refused (plan.md 10.3); 0: any age.
        AnalysisOptions analysis = quietAnalysis(); // How the volume is read now.

        // Catch-up when the journal allows, else a full scan; nothing saved (execution saves the result itself).
        [[nodiscard]] static AnalysisOptions quietAnalysis() {
            AnalysisOptions o;
            o.saveSnapshot = false;
            return o;
        }
    };

    // When a plan is judged: now and the age it may have.
    struct PlanAge {
        std::int64_t now = 0; // FILETIME ticks.
        std::int64_t maxAgeTicks = 0; // 0: any age.
    };

    // Why a plan may not run on a drive at all, or nullopt when it may (plan.md 10.3): another volume, a reformatted
    // one (other NTFS serial), another cluster size or count, or a plan older than the allowed age. A stale plan is
    // never executed; the user analyses and plans again.
    [[nodiscard]] std::optional<std::string> planRefusal(const core::execute::PlanIdentity& plan,
                                                         const core::DriveInfo& drive, PlanAge age);

    // A plan checked against the volume as it is now.
    struct Revalidation {
        std::optional<std::string> refused; // Why nothing may run; then replay is empty.
        core::execute::ReplayResult replay; // The moves still valid, and the dropped ones with reasons.
        std::shared_ptr<core::Snapshot> current; // The volume now; null when refused before reading it.
        bool caughtUp = false; // `current` came from the saved snapshot and the journal, not a full scan.
        std::string how; // What happened to the saved snapshot, for the log ("caught up ...", rescan reason).
        std::uint8_t scanner = 0; // ScannerKind of `current`, saved with it (only MFT snapshots are caught up later).
        core::ScreenAspect aspect; // The drive's map aspect, saved with the snapshot after execution.
        std::int64_t scanTime = 0; // When `current` was read (FILETIME ticks).
        double seconds = 0.0; // Wall time.
    };

    // Revalidates a plan before execution (plan.md 10.3): refuses stale or foreign plans, reads the volume again (USN
    // catch-up of the saved snapshot when the journal allows, else a full scan), then replays the moves `order` names
    // on it: files changed or deleted since planning, destinations taken since, and everything that waited on those
    // are dropped. Reads only. Throws like analyzeVolume() and OperationCancelled on cancel.
    [[nodiscard]] Revalidation revalidatePlan(const core::DriveInfo& drive, const core::execute::ExecutionPlan& plan,
                                              std::span<const std::uint32_t> order, JobContext& context,
                                              const RevalidateOptions& options = {});

    // The replay alone, on a snapshot of the volume now (tests, and revalidatePlan() after reading the volume).
    [[nodiscard]] core::execute::ReplayResult replayOnSnapshot(const core::execute::ExecutionPlan& plan,
                                                               std::span<const std::uint32_t> order,
                                                               const core::Snapshot& current);

} // namespace icd::services
