#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/job/PauseGate.h"
#include "core/model/DriveInfo.h"
#include "services/execute/ExecutionLog.h"
#include "services/execute/Executor.h"
#include "services/execute/Revalidate.h"
#include "services/job/JobRunner.h"
#include "services/plan/PlanJob.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace icd::services {

    // One execution of a plan on a drive (plan.md 10.3-10.4): a fresh one or a resumed one.
    struct ExecutionRequest {
        core::DriveInfo drive; // The volume.
        core::execute::ExecutionPlan plan; // What to run.
        std::vector<std::uint32_t> order; // Moves to try, in plan order (every move, or what a resume has left).
        ExecuteOptions options; // Chunk size, priority, throughput.
        RevalidateOptions revalidate; // Plan age, how the volume is read.
        std::filesystem::path log; // The execution log: created for a fresh run, appended to for a resume.
        bool resume = false; // Append to `log` instead of starting it.
        std::filesystem::path snapshotDirectory; // Saved snapshots; empty: SnapshotStore::defaultDirectory().
        ExecutionHooks observers; // Called after the log's own hooks (scripted pause, cancel and conflicts in tests).
    };

    // What an execution did.
    struct ExecutionOutcome {
        Revalidation revalidation; // The plan checked against the volume (refused: nothing else happened).
        ExecutionReport report; // The moves that ran.
        std::filesystem::path log; // Where the log is.
        bool snapshotSaved = false; // The saved snapshot now describes the volume after the moves.
        std::string snapshotNote; // What happened to the saved snapshot, in words.
    };

    // The execution plan of one planned level of a request for a drive: identity from the drive and the snapshot
    // (volume GUID name, NTFS serial, geometry, USN cursor, planning time, strategy, level), the MFT zone reserved.
    [[nodiscard]] core::execute::ExecutionPlan executionPlanFor(const PlanOutcome& outcome, const PlanRequest& request,
                                                                const core::DriveInfo& drive,
                                                                core::strategy::Effort level);

    // A request for a whole plan, logged to a new file in the default log directory.
    [[nodiscard]] ExecutionRequest freshExecution(const core::DriveInfo& drive, core::execute::ExecutionPlan plan);

    // A request that resumes the run a log describes (plan.md 10.4): its plan, the moves without a final line.
    [[nodiscard]] ExecutionRequest resumedExecution(const core::DriveInfo& drive, const std::filesystem::path& log);

    // Runs a request: revalidates (a refused plan stops here), logs the plan and what revalidation dropped, removes the
    // saved snapshot (the moves are not journaled, so it would mislead the next catch-up), executes on the volume,
    // closes the log, and saves the snapshot updated from the verified runs (or leaves none, so the next analysis
    // scans in full). Needs administrator rights. A crash at any point leaves a log a resumed run can continue.
    [[nodiscard]] ExecutionOutcome runExecution(const ExecutionRequest& request, JobContext& context,
                                                core::PauseGate* pause = nullptr);

    // How a logged execution runs: options plus cancel, pause and progress.
    struct ExecutionRunControls {
        ExecuteOptions options; // Chunk size, priority, throughput.
        core::CancelToken token; // Cancel.
        core::PauseGate* pause = nullptr; // Pause, if any.
        core::ProgressSink* progress = nullptr; // Progress, if any.
        ExecutionHooks observers; // Called after the log's own hooks.
    };

    // The middle of runExecution() on a given device, for tests: log the drops, execute the kept moves, close the log,
    // and return the report.
    [[nodiscard]] ExecutionReport executeLogged(const core::execute::ExecutionPlan& plan,
                                                const core::execute::ReplayResult& replay, MoveDevice& device,
                                                ExecutionLogWriter& log, const ExecutionRunControls& controls);

} // namespace icd::services
