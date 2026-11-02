#include "services/execute/Executor.h"

#include "core/execute/MoveCalls.h"
#include "core/log/Log.h"
#include "core/planner/HoleSet.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_set>

namespace icd::services {

    namespace {

        using core::ClusterCount;
        using core::ClusterRun;
        using core::Lcn;
        using core::execute::ExecutionPlan;
        using core::execute::Landing;
        using core::execute::MoveCall;
        using core::execute::MovePiece;
        using core::execute::PlannedMove;
        using core::execute::StreamRun;
        using platform::MoveError;
        using Clock = std::chrono::steady_clock;

        // Appends what is left of run `r` after taking out `b` (sorted by start); `j` skips the runs of `b` that end
        // before r, kept between calls since the runs of `a` come in order.
        void subtractFrom(const ClusterRun& r, std::span<const ClusterRun> b, std::size_t& j,
                          std::vector<ClusterRun>& out) {
            Lcn at = r.start;
            while (j < b.size() && b[j].end() <= at) {
                ++j;
            }
            for (std::size_t k = j; k < b.size() && b[k].start < r.end(); ++k) {
                if (b[k].start > at) {
                    out.push_back({at, b[k].start - at});
                }
                at = std::max(at, b[k].end());
            }
            if (at < r.end()) {
                out.push_back({at, r.end() - at});
            }
        }

        // Runs of `a` (sorted, disjoint) with every cluster of `b` (any order, may overlap) taken out.
        std::vector<ClusterRun> subtractRuns(std::span<const ClusterRun> a, std::vector<ClusterRun> b) {
            std::ranges::sort(b, {}, &ClusterRun::start);
            std::vector<ClusterRun> out;
            std::size_t j = 0;
            for (const ClusterRun& r : a) {
                subtractFrom(r, b, j, out);
            }
            return out;
        }

        // Opens the next move's file on a helper thread while the current one moves (plan.md 10.4: one mover plus
        // a prefetch helper). One slot: a request replaces the previous one; take() waits for a requested file and
        // opens anything else inline.
        class Prefetcher {
        public:
            // A helper over `device`.
            explicit Prefetcher(MoveDevice& device) :
                device_(device), worker_([this](const std::stop_token& stop) { loop(stop); }) {}

            Prefetcher(const Prefetcher&) = delete;
            Prefetcher& operator=(const Prefetcher&) = delete;
            Prefetcher(Prefetcher&&) = delete;
            Prefetcher& operator=(Prefetcher&&) = delete;

            // Stops the helper (the jthread joins).
            ~Prefetcher() {
                worker_.request_stop();
                changed_.notify_all();
            }

            // Starts opening a file.
            void request(std::uint64_t reference) {
                const std::scoped_lock lock(mutex_);
                wanted_ = reference;
                requested_ = true;
                ready_ = false;
                file_.reset();
                changed_.notify_all();
            }

            // The file, from the helper when it was requested, else opened now.
            std::unique_ptr<MovableFile> take(std::uint64_t reference, std::string& why) {
                std::unique_lock lock(mutex_);
                if (!requested_ || wanted_ != reference) {
                    lock.unlock();
                    return device_.open(reference, why);
                }
                changed_.wait(lock, [&] { return ready_; });
                requested_ = false;
                why = why_;
                return std::move(file_);
            }

        private:
            // The helper: opens whatever is requested and not ready yet.
            void loop(const std::stop_token& stop) {
                std::unique_lock lock(mutex_);
                while (changed_.wait(lock, stop, [&] { return requested_ && !ready_; })) {
                    const std::uint64_t reference = wanted_;
                    lock.unlock();
                    std::string why;
                    auto file = device_.open(reference, why);
                    lock.lock();
                    if (requested_ && wanted_ == reference && !ready_) {
                        file_ = std::move(file);
                        why_ = std::move(why);
                        ready_ = true;
                        changed_.notify_all();
                    }
                }
            }

            MoveDevice& device_; // Opens files.
            std::mutex mutex_; // Guards the slot.
            std::condition_variable_any changed_; // Slot changes.
            std::uint64_t wanted_ = 0; // Requested file.
            bool requested_ = false; // A request is outstanding.
            bool ready_ = false; // file_ holds its result.
            std::unique_ptr<MovableFile> file_; // Opened file (null when it failed).
            std::string why_; // Why it failed.
            std::jthread worker_; // Last member: starts after the slot exists, stops before it goes.
        };

        // State shared by every move of one run.
        class MoveRunner;

        // One move from open to verification.
        class MoveAttempt {
        public:
            // A move at a position of the order.
            MoveAttempt(MoveRunner& runner, std::size_t position);

            // Runs it; the record says how it ended. False when cancel stopped it before it touched the disk.
            bool run();

            // The record.
            [[nodiscard]] MoveRecord& record() { return record_; }

        private:
            // Opens the file and finds what is left to move; false when the move is over before any call.
            bool prepare();

            // The pieces of the move from the file as it is now: at its sources or part-way, or remapped from wherever
            // it lies for the return of a parked file and for files whose earlier move deviated.
            [[nodiscard]] std::optional<std::vector<MovePiece>> piecesNow(std::span<const StreamRun> now) const;

            // Issues the calls; stops at a failure or cancel.
            void issueCalls();

            // What issueCalls() does after a refused call.
            enum class Refusal : std::uint8_t {
                NextCall, // The call landed after all: go on with the next one.
                RetryCall, // The call has a replacement target: issue it again.
                Stop, // The move ends here (the record says why).
            };

            // Keeps what a refused call moved anyway, then finds a replacement target or ends the move.
            Refusal afterRefusal(std::size_t k, const platform::MoveCallResult& result);

            // Records clusters that reached their target.
            void landed(const Landing& landing, ClusterCount clusters);

            // After a refused call: keeps what NTFS moved anyway (it moves large calls in pieces and keeps the pieces
            // done before refusing) and shrinks the call to the rest; true when nothing is left of it.
            bool keepLandedPrefix(std::size_t k);

            // One call with retries for transient refusals.
            platform::MoveCallResult callWithRetries(const MoveCall& call);

            // What became of a refused call's target.
            enum class Replacement : std::uint8_t {
                Done, // Taken now; the rest of the move goes to a replacement destination.
                NoRoom, // Taken now, and no free run in the move's region holds the rest.
                NotTried, // Not a stale bitmap (target free, other error), or replacements off or used up.
            };

            // After a refused call: whether its target is taken now and a replacement was found for the rest.
            Replacement replaceTarget(std::size_t from, const platform::MoveCallResult& result);

            // Logs a refused call with what it tried, whether its target is free and where the file is now.
            void logRefusal(const MoveCall& call, const platform::MoveCallResult& result) const;

            // Reads the runs back and settles the outcome.
            void finish();

            // Ends the move with nothing more to do and a reason.
            void stop(std::string reason, unsigned long code = 0);

            MoveRunner& runner_; // Shared state.
            std::size_t position_; // Position in the order.
            const PlannedMove& move_; // The move.
            MoveRecord record_; // Output.
            std::unique_ptr<MovableFile> file_; // Open file.
            std::vector<MovePiece> pieces_; // What the move consists of now.
            std::vector<MoveCall> calls_; // Calls for the pending pieces.
            std::vector<Landing> landings_; // Where moved and done clusters must be.
            int replacements_ = 0; // Replacement destinations taken.
            bool cancelled_ = false; // Cancel stopped it.
        };

        // See ExecutionReport: the loop over the order and what every move shares.
        class MoveRunner {
        public:
            // A run of `order` on `device`.
            MoveRunner(const ExecutionPlan& plan, std::span<const std::uint32_t> order, MoveDevice& device,
                       const ExecuteOptions& options, const ExecutionControls& controls) :
                plan_(plan), order_(order), device_(device), options_(options), controls_(controls),
                chunkClusters_(std::max<std::uint64_t>(
                    options.chunkBytes / std::max<std::uint32_t>(plan.identity.geometry.bytesPerCluster, 1), 1)) {
                if (options.prefetch) {
                    prefetch_.emplace(device);
                }
                indexDestinations();
            }

            // Runs every move.
            ExecutionReport run() {
                const auto start = Clock::now();
                start_ = start;
                lastLog_ = start;
                const bool background = options_.lowPriority && platform::setBackgroundIo(true);
                core::logging::info("moving files", {{"moves", order_.size()}, {"clusters", plannedClusters()}});
                for (std::size_t p = 0; p < order_.size() && !report_.cancelled; ++p) {
                    runOne(p);
                }
                if (background) {
                    platform::setBackgroundIo(false);
                }
                report_.seconds = std::chrono::duration<double>(Clock::now() - start).count();
                logTotals();
                if (report_.cancelled && controls_.hooks.onEvent) {
                    controls_.hooks.onEvent("cancelled");
                }
                return std::move(report_);
            }

            // The plan.
            [[nodiscard]] const ExecutionPlan& plan() const { return plan_; }

            // The move at a position.
            [[nodiscard]] const PlannedMove& moveAt(std::size_t position) const {
                return plan_.moves[order_[position]];
            }

            // The plan index at a position.
            [[nodiscard]] std::uint32_t indexAt(std::size_t position) const { return order_[position]; }

            // The options.
            [[nodiscard]] const ExecuteOptions& options() const { return options_; }

            // The device.
            [[nodiscard]] MoveDevice& device() { return device_; }

            // Largest call in clusters.
            [[nodiscard]] ClusterCount chunkClusters() const { return chunkClusters_; }

            // The file of a move, from the prefetch helper when it has it.
            std::unique_ptr<MovableFile> open(std::size_t position, std::string& why) {
                const std::uint64_t reference = moveAt(position).fileReference;
                auto file = prefetch_ ? prefetch_->take(reference, why) : device_.open(reference, why);
                if (prefetch_ && position + 1 < order_.size()) {
                    prefetch_->request(moveAt(position + 1).fileReference);
                }
                return file;
            }

            // Whether a file's earlier move in this run went elsewhere or stopped, so later moves may remap it.
            [[nodiscard]] bool deviated(std::uint64_t reference) const { return deviated_.contains(reference); }

            // Holds while paused, then tells whether to go on (false on cancel).
            bool between() {
                if (controls_.pause != nullptr && controls_.pause->paused()) {
                    event("paused");
                    controls_.pause->wait(controls_.token);
                    event(controls_.token.cancelled() ? "cancelled while paused" : "resumed");
                }
                return !controls_.token.cancelled();
            }

            // Accounts for clusters moved and waits as long as the throughput limit asks.
            void moved(ClusterCount clusters) {
                bytes_ += clusters * plan_.identity.geometry.bytesPerCluster;
                const double wait = throttleDelaySeconds(
                    {bytes_, std::chrono::duration<double>(Clock::now() - start_).count()}, options_.throughputLimit);
                // In slices of 50 ms, so cancel stays prompt.
                for (auto left = std::chrono::milliseconds(static_cast<long long>(wait * 1000.0));
                     left.count() > 0 && !controls_.token.cancelled(); left -= std::chrono::milliseconds(50)) {
                    std::this_thread::sleep_for(std::min(left, std::chrono::milliseconds(50)));
                }
            }

            // A replacement destination for the calls left in the move at `position`, in its region: free now, outside
            // the MFT zone and every destination of the moves after `position`.
            [[nodiscard]] std::optional<Lcn> replacement(std::size_t position, std::span<const MoveCall> rest) {
                const ClusterCount clusters = core::execute::allocatedOf(rest);
                const ClusterRun region = moveAt(position).region;
                std::vector<ClusterRun> avoid(plan_.reserved.begin(), plan_.reserved.end());
                for (const auto& [run, at] : destinations_) {
                    if (at > position && run.start < region.end() && run.end() > region.start) {
                        avoid.push_back(run);
                    }
                }
                const auto free = subtractRuns(device_.freeRunsIn(region), std::move(avoid));
                const core::planner::HoleSet holes(free);
                const auto fit = holes.nextFit(clusters, region);
                return fit ? std::optional(fit->start) : std::nullopt;
            }

        private:
            // Runs the move at a position and records it. A move cancelled before its first call is not recorded:
            // it did not touch the disk, so a resumed run starts it again.
            void runOne(std::size_t position) {
                if (!between()) {
                    report_.cancelled = true;
                    return;
                }
                if (controls_.hooks.onBegin) {
                    controls_.hooks.onBegin(order_[position]);
                }
                MoveAttempt attempt(*this, position);
                const bool finished = attempt.run();
                report_.cancelled = !finished;
                if (finished || attempt.record().clustersMoved != 0) {
                    settle(position, std::move(attempt.record()));
                }
            }

            // Counts a record, tells the hooks and the progress sink, and keeps it.
            void settle(std::size_t position, MoveRecord r) {
                const bool deviates =
                    r.replaced || (r.outcome != MoveOutcome::Moved && r.outcome != MoveOutcome::AlreadyDone);
                if (deviates) {
                    deviated_.insert(moveAt(position).fileReference);
                }
                count(r);
                logRecord(r);
                logRate(position + 1);
                if (controls_.hooks.onRecord) {
                    controls_.hooks.onRecord(r);
                }
                if (controls_.progress != nullptr) {
                    controls_.progress->report({"Moving files", position + 1, order_.size(), plan_.paths[r.move]});
                }
                report_.records.push_back(std::move(r));
            }

            // Adds a record to the totals.
            void count(const MoveRecord& r) {
                ExecutionTotals& t = report_.totals;
                std::array<std::uint64_t*, 4> byOutcome{&t.moved, &t.alreadyDone, &t.partial, &t.skipped};
                ++*byOutcome.at(static_cast<std::size_t>(r.outcome));
                t.replaced += r.replaced ? 1 : 0;
                t.unverified += r.outcome != MoveOutcome::Skipped && !r.verified ? 1 : 0;
                t.clustersMoved += r.clustersMoved;
            }

            // Clusters of the moves to run.
            [[nodiscard]] ClusterCount plannedClusters() const {
                ClusterCount sum = 0;
                for (std::size_t p = 0; p < order_.size(); ++p) {
                    sum += moveAt(p).clusters;
                }
                return sum;
            }

            // Logs a move that did not complete, or was not verified, with its reason (the Log tab reads the same).
            void logRecord(const MoveRecord& r) const {
                const bool fine =
                    r.verified && (r.outcome == MoveOutcome::Moved || r.outcome == MoveOutcome::AlreadyDone);
                if (!fine) {
                    core::logging::warning("move not completed",
                                           {{"file", plan_.paths[r.move]},
                                            {"outcome", outcomeKey(r.outcome)},
                                            {"reason", r.reason},
                                            {"verified", r.verified},
                                            {"error", std::uint64_t{r.win32Error}}});
                }
            }

            // Megabytes per second moved since the start.
            [[nodiscard]] double rate() const {
                const double seconds = std::chrono::duration<double>(Clock::now() - start_).count();
                const double bytes = static_cast<double>(report_.totals.clustersMoved) *
                    static_cast<double>(plan_.identity.geometry.bytesPerCluster);
                return seconds > 0 ? bytes / seconds / 1e6 : 0.0;
            }

            // Every 10 seconds: moves done, clusters moved and throughput, so a long run can be followed in the log.
            void logRate(std::size_t done) {
                const auto now = Clock::now();
                if (now - lastLog_ < std::chrono::seconds(10)) {
                    return;
                }
                lastLog_ = now;
                core::logging::info("moving files",
                                    {{"done", done},
                                     {"of", order_.size()},
                                     {"clusters", report_.totals.clustersMoved},
                                     {"mb_per_s", rate()}});
            }

            // The summary line at the end of a run.
            void logTotals() const {
                const ExecutionTotals& t = report_.totals;
                core::logging::info("moves done",
                                    {{"moved", t.moved},
                                     {"already_done", t.alreadyDone},
                                     {"partial", t.partial},
                                     {"skipped", t.skipped},
                                     {"replaced", t.replaced},
                                     {"unverified", t.unverified},
                                     {"clusters", t.clustersMoved},
                                     {"seconds", report_.seconds},
                                     {"mb_per_s", rate()},
                                     {"cancelled", report_.cancelled}});
            }

            // Every destination run with the position of its move, for replacement searches.
            void indexDestinations() {
                for (std::size_t p = 0; p < order_.size(); ++p) {
                    for (const ClusterRun& run : plan_.destinations(moveAt(p))) {
                        destinations_.emplace_back(run, p);
                    }
                }
            }

            // Tells the log about pause, resume and cancel.
            void event(std::string_view what) {
                if (controls_.hooks.onEvent) {
                    controls_.hooks.onEvent(what);
                }
            }

            const ExecutionPlan& plan_; // The plan.
            std::span<const std::uint32_t> order_; // Moves to run.
            MoveDevice& device_; // Where.
            const ExecuteOptions& options_; // How.
            const ExecutionControls& controls_; // Pause, cancel, progress, hooks.
            ClusterCount chunkClusters_; // Largest call.
            std::optional<Prefetcher> prefetch_; // Opens the next file ahead.
            std::vector<std::pair<ClusterRun, std::size_t>> destinations_; // Destination runs and their positions.
            std::unordered_set<std::uint64_t> deviated_; // Files whose earlier move went elsewhere or stopped.
            std::uint64_t bytes_ = 0; // Bytes moved, for the throughput limit.
            Clock::time_point start_; // When the run started.
            Clock::time_point lastLog_; // When the last progress line was logged.
            ExecutionReport report_; // Output.
        };

        MoveAttempt::MoveAttempt(MoveRunner& runner, std::size_t position) :
            runner_(runner), position_(position), move_(runner.moveAt(position)) {
            record_.move = runner.indexAt(position);
        }

        bool MoveAttempt::run() {
            if (prepare()) {
                issueCalls();
            }
            if (file_) {
                finish();
            }
            return !cancelled_;
        }

        bool MoveAttempt::prepare() {
            std::string why;
            file_ = runner_.open(position_, why);
            std::vector<StreamRun> now;
            if (!file_ || !file_->readRuns(now)) {
                stop(file_ ? "read-failed" : why);
                file_.reset();
                return false;
            }
            auto pieces = piecesNow(now);
            if (!pieces) {
                stop("file-changed");
                record_.runsAfter = std::move(now);
                file_.reset();
                return false;
            }
            pieces_ = std::move(*pieces);
            for (const MovePiece& p : pieces_) {
                if (p.done) {
                    landings_.push_back(core::execute::landingOf(p));
                }
            }
            calls_ = core::execute::moveCalls(pieces_,
                                              {runner_.chunkClusters(), (move_.fileFlags & core::FileCompressed) != 0});
            return !calls_.empty();
        }

        std::optional<std::vector<MovePiece>> MoveAttempt::piecesNow(std::span<const StreamRun> now) const {
            const ExecutionPlan& plan = runner_.plan();
            auto pieces = core::execute::matchMove(now, {plan.sources(move_), plan.destinations(move_)});
            if (!pieces && (move_.fromParking || runner_.deviated(move_.fileReference))) {
                pieces = core::execute::remapMove(now, plan.destinations(move_));
            }
            return pieces;
        }

        void MoveAttempt::issueCalls() {
            for (std::size_t k = 0; k < calls_.size();) {
                if (!runner_.between()) {
                    cancelled_ = true;
                    stop("cancelled");
                    return;
                }
                const platform::MoveCallResult result = callWithRetries(calls_[k]);
                if (result.error == MoveError::None) {
                    landed(core::execute::landingOf(calls_[k]), calls_[k].allocated);
                    ++k;
                    continue;
                }
                const Refusal next = afterRefusal(k, result);
                if (next == Refusal::Stop) {
                    return;
                }
                k += next == Refusal::NextCall ? 1 : 0;
            }
        }

        MoveAttempt::Refusal MoveAttempt::afterRefusal(std::size_t k, const platform::MoveCallResult& result) {
            if (keepLandedPrefix(k)) {
                return Refusal::NextCall; // The whole call landed although NTFS refused it.
            }
            const Replacement replaced = replaceTarget(k, result);
            if (replaced == Replacement::Done) {
                return Refusal::RetryCall;
            }
            logRefusal(calls_[k], result);
            const bool taken = replaced == Replacement::NoRoom;
            stop(taken ? "destination-in-use" : std::string(platform::moveErrorKey(result.error)), result.win32Error);
            return Refusal::Stop;
        }

        void MoveAttempt::landed(const Landing& landing, ClusterCount clusters) {
            landings_.push_back(landing);
            record_.clustersMoved += clusters;
            runner_.moved(clusters);
        }

        bool MoveAttempt::keepLandedPrefix(std::size_t k) {
            std::vector<StreamRun> now;
            if (!file_->readRuns(now)) {
                return false;
            }
            MoveCall& call = calls_[k];
            const core::execute::LandedPrefix prefix = core::execute::landedPrefix(now, call);
            if (prefix.allocated == 0) {
                return false;
            }
            core::logging::info(
                "refused call moved part",
                {{"file", runner_.plan().paths[record_.move]}, {"clusters", prefix.allocated}, {"of", call.allocated}});
            landed({call.stream, call.vcn, prefix.vcnEnd - call.vcn, call.to, prefix.allocated}, prefix.allocated);
            call = core::execute::remainderOf(call, prefix);
            return call.allocated == 0;
        }

        platform::MoveCallResult MoveAttempt::callWithRetries(const MoveCall& call) {
            platform::MoveCallResult result = file_->move(call);
            for (int attempt = 0; result.error == MoveError::Retry && attempt < runner_.options().transientRetries;
                 ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                result = file_->move(call);
            }
            return result;
        }

        MoveAttempt::Replacement MoveAttempt::replaceTarget(std::size_t from, const platform::MoveCallResult& result) {
            const bool maybeTaken =
                result.error == MoveError::AccessDenied || result.error == MoveError::DestinationInUse;
            if (!maybeTaken || !runner_.options().replaceTakenDestinations || replacements_ >= 2) {
                return Replacement::NotTried;
            }
            const MoveCall& call = calls_[from];
            if (core::planner::clustersOf(runner_.device().freeRunsIn({call.to, call.allocated})) == call.allocated) {
                return Replacement::NotTried; // The target is free: the refusal had another cause.
            }
            const auto rest = std::span(calls_).subspan(from);
            const auto start = runner_.replacement(position_, rest);
            if (!start) {
                return Replacement::NoRoom;
            }
            core::execute::retarget(rest, *start);
            ++replacements_;
            record_.replaced = true;
            core::logging::info("destination taken, moved elsewhere",
                                {{"file", runner_.plan().paths[record_.move]}, {"from", call.to}, {"to", *start}});
            return Replacement::Done;
        }

        void MoveAttempt::logRefusal(const MoveCall& call, const platform::MoveCallResult& result) const {
            const auto free = core::planner::clustersOf(runner_.device().freeRunsIn({call.to, call.allocated}));
            std::string runs;
            if (std::vector<StreamRun> now; file_->readRuns(now)) {
                for (std::size_t i = 0; i < now.size() && i < 8; ++i) {
                    runs +=
                        std::format("{}{}:{}+{}", runs.empty() ? "" : " ", now[i].stream, now[i].lcn, now[i].length);
                }
            }
            core::logging::warning("move call refused",
                                   {{"file", runner_.plan().paths[record_.move]},
                                    {"error", std::uint64_t{result.win32Error}},
                                    {"stream", call.stream},
                                    {"vcn", call.vcn},
                                    {"to", call.to},
                                    {"clusters", call.allocated},
                                    {"free_at_target", free},
                                    {"replacements", replacements_},
                                    {"runs_now", runs}});
        }

        void MoveAttempt::finish() {
            std::vector<StreamRun> after;
            const bool read = file_->readRuns(after);
            record_.verified = read && core::execute::landed(after, landings_);
            record_.runsAfter = std::move(after);
            const bool allMoved = record_.reason.empty();
            if (calls_.empty() && allMoved) {
                record_.outcome = MoveOutcome::AlreadyDone;
            }
            else if (allMoved) {
                record_.outcome = MoveOutcome::Moved;
            }
            else {
                record_.outcome = record_.clustersMoved != 0 ? MoveOutcome::Partial : MoveOutcome::Skipped;
            }
        }

        void MoveAttempt::stop(std::string reason, unsigned long code) {
            record_.reason = std::move(reason);
            record_.win32Error = code;
            record_.outcome = MoveOutcome::Skipped;
        }

    } // namespace

    std::string_view outcomeKey(MoveOutcome outcome) {
        constexpr std::array<std::string_view, 4> keys{"moved", "already-done", "partial", "skipped"};
        return keys.at(static_cast<std::size_t>(outcome));
    }

    double throttleDelaySeconds(ThroughputSample sample, std::uint64_t bytesPerSecond) {
        if (bytesPerSecond == 0) {
            return 0.0;
        }
        const double due = static_cast<double>(sample.bytes) / static_cast<double>(bytesPerSecond);
        return std::max(0.0, due - sample.seconds);
    }

    ExecutionReport executePlan(const core::execute::ExecutionPlan& plan, std::span<const std::uint32_t> order,
                                MoveDevice& device, const ExecuteOptions& options, const ExecutionControls& controls) {
        MoveRunner runner(plan, order, device, options, controls);
        return runner.run();
    }

} // namespace icd::services
