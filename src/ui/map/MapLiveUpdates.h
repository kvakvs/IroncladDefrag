#pragma once

#include "services/execute/Executor.h"
#include "services/execute/LiveMap.h"

#include <wx/timer.h>

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
    // instead of falling behind. UI thread only.
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

        // A timer that calls back, so this class needs no window of its own.
        class Ticker : public wxTimer {
        public:
            // Calls tick on every notification.
            explicit Ticker(std::function<void()> tick) : tick_(std::move(tick)) {}

            // Calls the callback.
            void Notify() override { tick_(); }

        private:
            std::function<void()> tick_; // What each tick does.
        };

        // Takes new moves from the feed and starts an update when one is due; stops once a finished run is drained.
        void tick();

        // Starts an update of the pending moves (and the rebase, first time) on the pool.
        void submit();

        // Shows a computed map.
        void show(const Result& result);

        // Takes a finished update: shows it, or stops when it failed or the map changed meanwhile.
        void deliver(const Result& result);

        // Stops following and logs what was done.
        void end(const char* why);

        MapPanel& map_; // Where updates are shown.
        core::Executor& executor_; // Applies moves.
        std::shared_ptr<Mailbox> mailbox_; // Shared with running updates.
        Ticker ticker_; // Drives tick().
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
