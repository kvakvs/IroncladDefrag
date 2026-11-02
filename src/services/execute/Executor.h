#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/execute/MovePieces.h"
#include "core/job/CancelToken.h"
#include "core/job/PauseGate.h"
#include "core/job/Progress.h"
#include "services/execute/MoveDevice.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace icd::services {

    // How the executor moves (plan.md 10.4).
    struct ExecuteOptions {
        std::uint64_t chunkBytes = std::uint64_t{16} << 20U; // Largest move call; pause and cancel act between calls.
        bool lowPriority = false; // Background I/O priority for the mover thread.
        std::uint64_t throughputLimit = 0; // Bytes per second at most; 0: no limit.
        bool replaceTakenDestinations = true; // Stale bitmap: pick a new destination in the move's region.
        int transientRetries = 3; // Retries of a call the file system refused for the moment.
        bool prefetch = true; // Open the next file on a helper thread while the current one moves.
    };

    // How one move ended.
    enum class MoveOutcome : std::uint8_t {
        Moved, // Every cluster went where intended (the plan's destination or a replacement).
        AlreadyDone, // The clusters already lay at the destination (a resumed or repeated plan).
        Partial, // Some calls went through, then the move stopped (failure or cancel): the file is valid, half moved.
        Skipped, // Nothing moved; `reason` says why.
    };

    // Stable name of an outcome ("moved", "already-done", "partial", "skipped").
    [[nodiscard]] std::string_view outcomeKey(MoveOutcome outcome);

    // What happened to one move, for the log, the Log tab and the snapshot update.
    struct MoveRecord {
        std::uint32_t move = 0; // Index in the plan.
        MoveOutcome outcome = MoveOutcome::Skipped; // How it ended.
        std::string reason; // Why it stopped or was skipped ("file-changed", "locked", "cancelled", ...); else empty.
        bool replaced = false; // Went to a replacement destination because the planned one was taken.
        bool verified = false; // The runs read back after the move put every moved cluster where intended.
        core::ClusterCount clustersMoved = 0; // Clusters actually moved.
        unsigned long win32Error = 0; // Code of the failing call, if any.
        std::vector<core::execute::StreamRun> runsAfter; // The file's runs read back afterwards (empty if unread).
    };

    // Counts over the records.
    struct ExecutionTotals {
        std::uint64_t moved = 0; // Moves that completed.
        std::uint64_t alreadyDone = 0; // Moves with nothing left to do.
        std::uint64_t partial = 0; // Moves cut short.
        std::uint64_t skipped = 0; // Moves skipped.
        std::uint64_t replaced = 0; // Moves sent to a replacement destination.
        std::uint64_t unverified = 0; // Moves whose runs did not read back as intended.
        core::ClusterCount clustersMoved = 0; // Clusters moved in all.

        // Totals compare as values (tests).
        friend bool operator==(const ExecutionTotals&, const ExecutionTotals&) = default;
    };

    // The result of running (part of) a plan.
    struct ExecutionReport {
        std::vector<MoveRecord> records; // One per move that ran, in execution order.
        ExecutionTotals totals; // Counts.
        bool cancelled = false; // Stopped by cancel; the moves after the last record did not run.
        double seconds = 0.0; // Wall time.
    };

    // Callbacks around moves, for the execution log (all optional, called on the mover thread).
    struct ExecutionHooks {
        std::function<void(std::uint32_t move)> onBegin; // Before a move touches the disk.
        std::function<void(const MoveRecord&)> onRecord; // After a move ended.
        std::function<void(std::string_view event)> onEvent; // "paused", "resumed", "cancelled".
    };

    // What steers a running execution from outside.
    struct ExecutionControls {
        core::CancelToken token; // Cancel: stops after the current move call.
        core::PauseGate* pause = nullptr; // Pause: holds between move calls.
        core::ProgressSink* progress = nullptr; // Moves done of all, current file as the message.
        ExecutionHooks hooks; // Log callbacks.
    };

    // Runs the moves `order` names (plan indexes, in order) on a device (plan.md 10.4). Before each move the file is
    // opened by reference and its runs read again: a file found neither at its sources nor part-way to its destinations
    // is skipped as changed (the return of a parked file, or of a file whose earlier move deviated, may start from
    // wherever it lies). The pieces still pending go out in calls of at most chunkBytes; pause holds and cancel stops
    // between calls. A call refused because its target is taken now (stale bitmap) gets a replacement destination
    // in the move's region that avoids the MFT zone and every later move's destination, or the move stops there.
    // Locked, unsupported and failing files are skipped with a reason; execution never stops for one bad file. After
    // each move the runs are read back and verified. Cancel ends the run with `cancelled` set.
    [[nodiscard]] ExecutionReport executePlan(const core::execute::ExecutionPlan& plan,
                                              std::span<const std::uint32_t> order, MoveDevice& device,
                                              const ExecuteOptions& options, const ExecutionControls& controls);

    // Bytes moved and time spent so far.
    struct ThroughputSample {
        std::uint64_t bytes = 0; // Moved so far.
        double seconds = 0.0; // Elapsed so far.
    };

    // Seconds to wait so that `sample` stays within `bytesPerSecond` (0: no limit, no wait).
    [[nodiscard]] double throttleDelaySeconds(ThroughputSample sample, std::uint64_t bytesPerSecond);

} // namespace icd::services
