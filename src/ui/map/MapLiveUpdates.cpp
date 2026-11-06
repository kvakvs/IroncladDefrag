#include "ui/map/MapLiveUpdates.h"

#include "core/exec/Executor.h"
#include "core/log/Log.h"
#include "core/map/Activity.h"
#include "core/map/MapData.h"
#include "services/execute/ExecuteJob.h"
#include "ui/map/MapActivity.h"
#include "ui/panels/MapPanel.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace icd::ui {

    namespace {

        // How often the feed is looked at.
        constexpr int tickMs = 250;

        // Shortest gap between two updates.
        constexpr std::chrono::milliseconds minimumGap{500};

        // How long after a move began its read pulse starts: one tick, so moves taken together keep their spacing.
        constexpr std::chrono::milliseconds readDelay{tickMs};

        // The clusters of a move's runs read back, in order.
        std::vector<core::ClusterRun> lcnRuns(const services::MoveRecord& record) {
            std::vector<core::ClusterRun> runs;
            runs.reserve(record.runsAfter.size());
            for (const core::execute::StreamRun& run : record.runsAfter) {
                runs.push_back({run.lcn, run.length});
            }
            return runs;
        }

        // Milliseconds of a duration, for the log.
        double millis(std::chrono::steady_clock::duration d) {
            return std::chrono::duration<double, std::milli>(d).count();
        }

    } // namespace

    MapLiveUpdates::MapLiveUpdates(MapPanel& map, core::Executor& executor) :
        map_(map), executor_(executor), mailbox_(std::make_shared<Mailbox>()), ticker_([this] { tick(); }) {
        mailbox_->target = this;
    }

    MapLiveUpdates::~MapLiveUpdates() {
        ticker_.Stop();
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void MapLiveUpdates::follow(std::shared_ptr<services::ExecutionFeed> feed) {
        stop();
        base_ = map_.currentMap();
        if (base_ == nullptr || feed == nullptr) {
            return; // Nothing shown to follow on.
        }
        feed_ = std::move(feed);
        ++generation_;
        updates_ = 0;
        movesApplied_ = 0;
        slowest_ = {};
        nextAt_ = std::chrono::steady_clock::now();
        ticker_.Start(tickMs);
    }

    void MapLiveUpdates::finish() {
        if (feed_ != nullptr) {
            finishing_ = true;
            tick(); // The last moves need not wait for the next tick.
        }
    }

    void MapLiveUpdates::stop() {
        ticker_.Stop();
        feed_.reset();
        base_.reset();
        pending_.clear();
        volumeNow_.reset();
        busy_ = false; // A running update is dropped by its generation.
        finishing_ = false;
        ++generation_;
    }

    void MapLiveUpdates::tick() {
        if (feed_ == nullptr) {
            return;
        }
        pulseReads();
        std::vector<services::MoveRecord> moved = feed_->takeMoved();
        std::ranges::move(moved, std::back_inserter(pending_));
        if (auto now = feed_->takeVolumeNow(); now != nullptr) {
            volumeNow_ = std::move(now);
        }
        if (busy_) {
            return;
        }
        if (pending_.empty() && volumeNow_ == nullptr) {
            if (finishing_) {
                end("run ended");
            }
            return;
        }
        if (finishing_ || std::chrono::steady_clock::now() >= nextAt_) {
            submit();
        }
    }

    MapLiveUpdates::Result MapLiveUpdates::compute(const Work& work, core::Executor& executor) {
        Result r;
        r.base = work.base;
        r.moves = work.records.size();
        std::shared_ptr<const core::MapData> start = work.base;
        if (work.volumeNow != nullptr) {
            if (auto rebased = services::rebaseMap(*work.base, *work.volumeNow, executor); rebased != nullptr) {
                start = std::move(rebased);
                r.full = true;
                r.map = start;
            }
        }
        r.records = work.records;
        if (auto applied = services::applyMoves(*start, *work.plan, work.records, executor); applied) {
            r.map = std::move(applied->map);
            r.dirty = std::move(applied->dirty);
        }
        else {
            r.refused = services::anyApplicable(work.records);
        }
        return r;
    }

    void MapLiveUpdates::submit() {
        auto work = std::make_shared<Work>();
        work->plan = feed_->plan();
        if (work->plan == nullptr) {
            return; // Not built yet; moves cannot have finished either.
        }
        work->base = base_;
        work->volumeNow = std::exchange(volumeNow_, nullptr);
        work->records = std::exchange(pending_, {});
        busy_ = true;
        executor_.submit([mailbox = mailbox_, executor = &executor_, work, generation = generation_] {
            const auto start = std::chrono::steady_clock::now();
            auto result = std::make_shared<Result>();
            try {
                *result = compute(*work, *executor);
            }
            catch (const std::exception& error) {
                core::logging::error("live map update failed", {{"what", error.what()}});
                result->base = work->base;
                result->refused = true;
            }
            result->generation = generation;
            result->took = std::chrono::steady_clock::now() - start;
            const std::scoped_lock lock(mailbox->mutex);
            if (mailbox->target != nullptr) {
                mailbox->target->map_.CallAfter([mailbox, result] {
                    if (MapLiveUpdates* target = mailbox->target; target != nullptr) {
                        target->deliver(*result);
                    }
                });
            }
        });
    }

    void MapLiveUpdates::deliver(const Result& result) {
        if (result.generation != generation_) {
            return; // From a run no longer followed.
        }
        busy_ = false;
        if (map_.currentMap() != result.base) {
            end("another map is shown");
            return;
        }
        if (result.refused) {
            end("the moves do not fit the map (the volume changed in ways the moves do not explain)");
            return;
        }
        show(result);
        if (finishing_) {
            tick();
        }
    }

    void MapLiveUpdates::show(const Result& result) {
        slowest_ = std::max(slowest_, result.took);
        nextAt_ = std::chrono::steady_clock::now() +
            std::max<std::chrono::steady_clock::duration>(minimumGap, 2 * result.took);
        if (result.map == nullptr) {
            return;
        }
        base_ = result.map;
        if (result.full) {
            map_.updateMap(result.map); // Every tile renders again, shown in the old state until then.
        }
        else {
            map_.updateMap(result.map, result.dirty);
        }
        ++updates_;
        movesApplied_ += result.moves;
        pulseWrites(result.records);
    }

    void MapLiveUpdates::pulseReads() {
        const std::vector<services::BegunMove> begun = feed_->takeBegun();
        const auto plan = feed_->plan();
        if (begun.empty() || plan == nullptr) {
            return;
        }
        const auto now = core::AnimationClock::now();
        for (const services::BegunMove& move : begun) {
            if (move.move >= plan->moves.size()) {
                continue;
            }
            const auto sources = plan->sources(plan->moves[move.move]);
            map_.activity().add(core::ActivityKind::Read, {sources.begin(), sources.end()},
                                std::max(now, move.at + readDelay));
        }
    }

    void MapLiveUpdates::pulseWrites(const std::vector<services::MoveRecord>& records) {
        const auto now = core::AnimationClock::now();
        const auto spread = std::max<std::chrono::steady_clock::duration>(nextAt_ - now, minimumGap);
        const auto count = static_cast<std::int64_t>(records.size());
        for (std::int64_t i = 0; i < count; ++i) {
            const services::MoveRecord& record = records[static_cast<std::size_t>(i)];
            if (record.clustersMoved != 0 && !record.runsAfter.empty()) {
                map_.activity().add(core::ActivityKind::Write, lcnRuns(record), now + spread * i / count);
            }
        }
    }

    void MapLiveUpdates::end(const char* why) {
        core::logging::info("live map stopped",
                            {{"why", why},
                             {"updates", updates_},
                             {"moves", movesApplied_},
                             {"slowest_ms", millis(slowest_)},
                             {"left", pending_.size()}});
        stop();
    }

} // namespace icd::ui
