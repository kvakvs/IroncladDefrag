#pragma once

#include "services/execute/Executor.h"
#include "services/execute/LiveMap.h"
#include "ui/CallbackTimer.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace icd::core {
    class Executor;
    class MapData;
} // namespace icd::core

namespace icd::services {
    class ExecutionFeed;
}

namespace icd::ui {

    class MapPanel;

    // Follows an execution on the map (plan.md 10.4: the live map updates through the dirty-tile path): takes the
    // finished moves from the run's feed, applies them to the shown map on the pool (services::applyMoves()) and shows
    // the result with only the tiles over the moved clusters rendered again. One update at a time, the next one at
    // least twice the last one's duration later (500 ms at least), so on a huge volume the map updates less often
    // instead of falling behind. It also feeds the map's read and write pulses (T114): a read over a move's sources
    // when it begins, a write over where it went once the map shows it there, spread until the next update. UI thread
    // only.
    class MapLiveUpdates {
    public:
        // Shows updates on map; applies moves on executor, which must outlive this object.
        MapLiveUpdates(MapPanel& map, core::Executor& executor);

        // Stops; an update still computing is dropped.
        ~MapLiveUpdates();

        MapLiveUpdates(const MapLiveUpdates&) = delete;
        MapLiveUpdates& operator=(const MapLiveUpdates&) = delete;

        // Starts following a run from the map shown now; nothing happens without a shown map.
        void follow(std::shared_ptr<services::ExecutionFeed> feed);

        // The run ended: what is left is applied at once, then following stops.
        void finish();

        // Stops at once and drops what is left (another map is about to be shown).
        void stop();

    private:
        // Where a finished update goes; the worker posts to the target while it is set.
        struct Mailbox {
            std::mutex mutex; // Guards target.
            MapLiveUpdates* target = nullptr; // Cleared when the object goes away.
        };

        // What one update computed, back on the UI thread.
        struct Result {
            std::uint64_t generation = 0; // The run it belongs to.
            std::shared_ptr<const core::MapData> base; // The map it started from.
            std::shared_ptr<const core::MapData> map; // The new map; null when nothing changed.
            std::vector<core::ClusterRun> dirty; // Clusters that may look different (unless `full`).
            bool full = false; // Rebased on the volume as revalidation read it: every tile may look different.
            bool refused = false; // The moves do not fit the map: stop following.
            std::chrono::steady_clock::duration took{}; // How long it took.
            std::size_t moves = 0; // Moves it applied.
            std::vector<services::MoveRecord> records; // Those moves, for the write pulses.
        };

        // What an update starts from and applies (shared with the pool task).
        struct Work {
            std::shared_ptr<const core::MapData> base; // The map shown.
            std::shared_ptr<const core::Snapshot> volumeNow; // Revalidation's volume to rebase on first, or null.
            std::shared_ptr<const core::execute::ExecutionPlan> plan; // The run's plan.
            std::vector<services::MoveRecord> records; // Finished moves to apply.
        };

        // Computes one update on the pool: rebase if asked, then the moves.
        static Result compute(const Work& work, core::Executor& executor);

        // Takes new moves from the feed and starts an update when one is due; stops once a finished run is drained.
        void tick();

        // Starts an update of the pending moves (and the rebase, first time) on the pool.
        void submit();

        // Shows a computed map.
        void show(const Result& result);

        // Read pulses over the sources of the moves begun since the last tick, at the time each began (a tick later).
        void pulseReads();

        // Write pulses over where the moves of a shown update went, spread until the next update may come.
        void pulseWrites(const std::vector<services::MoveRecord>& records);

        // Takes a finished update: shows it, or stops when it failed or the map changed meanwhile.
        void deliver(const Result& result);

        // Stops following and logs what was done.
        void end(const char* why);

        MapPanel& map_; // Where updates are shown.
        core::Executor& executor_; // Applies moves.
        std::shared_ptr<Mailbox> mailbox_; // Shared with running updates.
        CallbackTimer ticker_; // Drives tick().
        std::shared_ptr<services::ExecutionFeed> feed_; // The followed run; null when none.
        std::shared_ptr<const core::MapData> base_; // The map shown by the last update (the next one starts there).
        std::vector<services::MoveRecord> pending_; // Moves taken from the feed, not applied yet.
        std::shared_ptr<const core::Snapshot> volumeNow_; // Revalidation's volume, until an update rebases on it.
        std::uint64_t generation_ = 0; // Bumped per run, so a late update of an earlier run is dropped.
        bool busy_ = false; // An update is computing.
        bool finishing_ = false; // The run ended; apply the rest and stop.
        std::chrono::steady_clock::time_point nextAt_{}; // Earliest start of the next update.
        std::size_t updates_ = 0; // Updates shown this run (log).
        std::size_t movesApplied_ = 0; // Moves they applied (log).
        std::chrono::steady_clock::duration slowest_{}; // Longest update this run (log).
    };

} // namespace icd::ui
