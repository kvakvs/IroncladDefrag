#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/execute/PlanReplay.h"
#include "core/job/PauseGate.h"
#include "core/model/DriveInfo.h"
#include "core/strategy/ZoneScript.h"
#include "services/execute/Execution.h"
#include "services/execute/Executor.h"
#include "services/job/JobRunner.h"
#include "services/plan/PlanJob.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icd::services {

    // What the GUI executes (plan.md 11, Execute): one planned level of the shown map on its drive.
    struct ExecuteJobRequest {
        core::DriveInfo drive; // The volume.
        std::shared_ptr<const PlanOutcome> outcome; // The three planned levels.
        std::shared_ptr<const PlanRequest> plan; // What they were planned from (snapshot, script, planning time).
        core::strategy::Effort level = core::strategy::Effort::Fast; // The level to run.
        ExecuteOptions options; // Priority and throughput limit chosen in the confirmation.
    };

    // Counts of a running execution, for the Progress tab.
    struct ExecutionCounters {
        bool moving = false; // Revalidation is over and the moves run.
        std::uint64_t moves = 0; // Moves revalidation kept, to run.
        std::uint64_t dropped = 0; // Moves revalidation took out.
        std::uint64_t finished = 0; // Moves that ended, whatever the outcome.
        std::uint64_t moved = 0; // Moves that completed or had nothing left to do.
        std::uint64_t skipped = 0; // Moves skipped.
        std::uint64_t partial = 0; // Moves cut short.
        std::uint64_t bytesMoved = 0; // Bytes moved so far.
        std::uint64_t bytesPending = 0; // Bytes the kept moves had to move when the moves started.
        std::string currentFile; // Path of the move under way.
    };

    // A line of the Log tab: a file that did not move as planned, or a line about the whole run.
    struct ExecutionLogLine {
        std::string file; // The file's path; empty for a line about the run.
        std::string result; // "skipped", "partly moved", "dropped", "moved elsewhere", "not verified", "run".
        std::string details; // Why, in words.
    };

    // How an execution ended, for the status line and the Log tab.
    struct ExecutionSummary {
        std::optional<std::string> refused; // Why the plan could not run at all.
        ExecutionTotals totals; // Counts of the moves that ran.
        std::uint64_t dropped = 0; // Moves revalidation took out.
        bool cancelled = false; // Stopped by Cancel; the rest of the plan stays in the log.
        double seconds = 0.0; // Wall time of the moves.
        std::uint32_t bytesPerCluster = 4096; // Of the volume, for byte figures.
        std::filesystem::path log; // The execution log.
        std::string snapshotNote; // What happened to the saved snapshot.
    };

    // A move reason key of the executor, the move primitive or revalidation ("file-changed", "locked", ...) in words.
    [[nodiscard]] std::string reasonText(std::string_view key);

    // The Log tab line of a finished move, or nullopt for a move that went as planned.
    [[nodiscard]] std::optional<ExecutionLogLine> recordLine(const MoveRecord& record,
                                                             const core::execute::ExecutionPlan& plan);

    // The Log tab line of a move revalidation took out.
    [[nodiscard]] ExecutionLogLine dropLine(const core::execute::DroppedMove& drop,
                                            const core::execute::ExecutionPlan& plan);

    // The Log tab lines about a whole run: totals, cancel or refusal, the snapshot, where the log is.
    [[nodiscard]] std::vector<ExecutionLogLine> summaryLines(const ExecutionSummary& summary);

    // The status line after a run.
    [[nodiscard]] std::string summaryStatus(const ExecutionSummary& summary, std::string_view drive);

    // The live side of a running execution: the job writes from its threads (revalidation, mover), the UI takes
    // counters, Log tab lines and finished moves (for the map) from its own. Thread-safe.
    class ExecutionFeed {
    public:
        // Log tab lines kept per run; the rest are counted and pointed to the execution log.
        static constexpr std::size_t maxLines = 5000;

        // The plan being run, once the job has built it (null before).
        [[nodiscard]] std::shared_ptr<const core::execute::ExecutionPlan> plan() const;

        // Counts so far.
        [[nodiscard]] ExecutionCounters counters() const;

        // Log tab lines since the last call.
        [[nodiscard]] std::vector<ExecutionLogLine> takeLines();

        // Moves that moved clusters since the last call, with the runs read back (the live map applies them).
        [[nodiscard]] std::vector<MoveRecord> takeMoved();

        // How the run ended; nullopt while it runs (and after a failure before the end).
        [[nodiscard]] std::optional<ExecutionSummary> summary() const;

        // Job side: the plan is built.
        void setPlan(std::shared_ptr<const core::execute::ExecutionPlan> plan);

        // Job side: revalidation judged the plan; its drops become Log tab lines.
        void revalidated(const Revalidation& revalidation);

        // Job side: hooks that follow the mover (current file, counts, records).
        [[nodiscard]] ExecutionHooks hooks();

        // Job side: the run ended.
        void finish(const ExecutionOutcome& outcome);

    private:
        // Adds a Log tab line, or counts it past the cap. Called with mutex_ held.
        void addLine(ExecutionLogLine line);

        // A move began. Mover thread.
        void begin(std::uint32_t move);

        // A move ended. Mover thread.
        void record(const MoveRecord& record);

        mutable std::mutex mutex_; // Guards everything below.
        std::shared_ptr<const core::execute::ExecutionPlan> plan_; // The plan, for paths.
        ExecutionCounters counters_; // Counts so far.
        std::vector<ExecutionLogLine> lines_; // Lines not taken yet.
        std::size_t linesAdded_ = 0; // Lines added this run (taken or not), for the cap.
        std::size_t linesOver_ = 0; // Lines past the cap.
        std::vector<MoveRecord> moved_; // Moves with moved clusters not taken yet.
        std::optional<ExecutionSummary> summary_; // How the run ended.
    };

    // A job that executes a planned level (plan.md 10.3-10.4): builds the execution plan, refuses one that failed
    // validation, then runExecution() (revalidation, log, moves, snapshot after) with `pause`, reporting into `feed`.
    // A plan the volume refuses fails the job with the reason. Needs administrator rights.
    [[nodiscard]] JobFunction makeExecuteJob(ExecuteJobRequest request, std::shared_ptr<ExecutionFeed> feed,
                                             std::shared_ptr<core::PauseGate> pause);

} // namespace icd::services
