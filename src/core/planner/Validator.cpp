#include "core/planner/Validator.h"

#include "core/planner/HoleSet.h"

#include <algorithm>
#include <format>

namespace icd::core::planner {

    namespace {

        // "No move".
        constexpr std::uint32_t noMove = static_cast<std::uint32_t>(-1);

        // Where a file stands in the replay.
        enum class FileState : std::uint8_t {
            Unmoved, // No move yet.
            Placed, // At its final place.
            Parked, // In scratch space.
        };

        // The replay: free space and the runs of files that moved so far.
        class Replay {
        public:
            // Starts from the snapshot.
            Replay(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options) :
                snapshot_(snapshot), plan_(plan), options_(options), free_(snapshot.freeSpace.runs()),
                last_(snapshot.files.size(), noMove), state_(snapshot.files.size(), FileState::Unmoved) {}

            // Replays every move and checks the plan as a whole.
            Validation run() {
                for (std::size_t i = 0; i < plan_.moves.size() && !full(); ++i) {
                    replay(i, plan_.moves[i]);
                }
                checkParking();
                checkAccounting();
                checkTotals();
                out_.freeClusters = free_.total();
                out_.freeRuns = free_.size();
                return std::move(out_);
            }

        private:
            // Whether enough issues were collected.
            [[nodiscard]] bool full() const { return out_.issues.size() >= options_.maxIssues; }

            // Records an issue about a move.
            void fail(std::size_t index, const Move& m, std::string_view what) {
                if (full()) {
                    return;
                }
                const std::string name =
                    m.file < snapshot_.files.size() ? snapshot_.path(m.file) : std::format("file {}", m.file);
                out_.issues.push_back({index, std::format("move {} ({}): {}", index + 1, name, what)});
            }

            // Records an issue about the plan.
            void failPlan(std::string message) {
                if (!full()) {
                    out_.issues.push_back({std::nullopt, std::move(message)});
                }
            }

            // Where a file's movable clusters are now: its last replayed move's destinations, or where it was (then
            // read into `own`).
            [[nodiscard]] std::span<const ClusterRun> currentRuns(FileIndex file, std::vector<ClusterRun>& own) const {
                if (last_[file] != noMove) {
                    return plan_.destinations(plan_.moves[last_[file]]);
                }
                own = movableRuns(snapshot_, file);
                return own;
            }

            // Checks one move and applies it.
            void replay(std::size_t index, const Move& m) {
                if (m.file >= snapshot_.files.size()) {
                    fail(index, m, "names no file of the snapshot");
                    return;
                }
                const auto sources = plan_.sources(m);
                const auto destinations = plan_.destinations(m);
                std::vector<ClusterRun> own;
                const std::span<const ClusterRun> now = currentRuns(m.file, own);
                if (fixedFile(snapshot_, m.file, clustersOf(now))) { // Moves keep cluster counts (checked below).
                    fail(index, m, "moves a file that must not move");
                }
                if (!std::ranges::equal(sources, now)) {
                    fail(index, m, "its sources are not the file's current movable runs");
                }
                if (clustersOf(sources) != clustersOf(destinations) || m.clusters != clustersOf(sources)) {
                    fail(index, m, "does not keep the file's cluster count");
                }
                checkCount(index, m);
                checkZone(index, m, destinations);
                apply(index, m, now);
            }

            // Takes the move's destinations and frees the clusters the file holds now.
            void apply(std::size_t index, const Move& m, std::span<const ClusterRun> now) {
                for (const ClusterRun& d : plan_.destinations(m)) {
                    if (d.length == 0 || !free_.take(d)) {
                        fail(index, m, std::format("destination [{}, {}) is not free at its move", d.start, d.end()));
                    }
                }
                for (const ClusterRun& s : now) {
                    if (!free_.add(s)) {
                        fail(index, m, "its old clusters were free already");
                    }
                }
                last_[m.file] = static_cast<std::uint32_t>(index);
            }

            // At most one move per file, except a park and its return.
            void checkCount(std::size_t index, const Move& m) {
                FileState& state = state_[m.file];
                if (m.fromParking) {
                    if (state != FileState::Parked) {
                        fail(index, m, "returns from parking without having been parked");
                    }
                    parkedCount_ -= state == FileState::Parked ? 1 : 0;
                    state = FileState::Placed;
                    return;
                }
                if (state != FileState::Unmoved) {
                    fail(index, m, "moves a file a second time");
                }
                parkedCount_ += m.reason == MoveReason::Park && state != FileState::Parked ? 1 : 0;
                state = m.reason == MoveReason::Park ? FileState::Parked : FileState::Placed;
            }

            // A move into a zone lands inside it.
            void checkZone(std::size_t index, const Move& m, std::span<const ClusterRun> destinations) {
                if (options_.layout == nullptr || m.zone == noZone || m.reason == MoveReason::Park) {
                    return;
                }
                const ZoneTarget* target = options_.layout->target(m.zone);
                const bool inside = target != nullptr &&
                    std::ranges::all_of(destinations, [&](const ClusterRun& d) { return target->holds(d); });
                if (!inside) {
                    fail(index, m, "its destination is outside its zone");
                }
            }

            // Every parked file came back.
            void checkParking() {
                if (parkedCount_ != 0) {
                    failPlan(std::format("{} parked file(s) never return from parking", parkedCount_));
                }
            }

            // Moved clusters against the lower bound, skipped and deferred clusters and the extra.
            void checkAccounting() {
                ClusterCount moved = 0;
                ClusterCount extra = 0;
                for (const Move& m : plan_.moves) {
                    moved += m.clusters;
                    extra += m.extra ? m.clusters : 0;
                }
                ClusterCount skipped = 0;
                for (const SkippedFile& s : plan_.skipped) {
                    skipped += s.clusters;
                }
                if (skipped != plan_.skippedClusters) {
                    failPlan(std::format("skipped files hold {} clusters, the plan reports {}", skipped,
                                         plan_.skippedClusters));
                }
                if (extra != plan_.extra) {
                    failPlan(std::format("moves marked extra move {} clusters, the plan reports {} extra", extra,
                                         plan_.extra));
                }
                const ClusterCount reached = plan_.skippedClusters + plan_.deferredClusters;
                const ClusterCount expected =
                    (plan_.lowerBound >= reached ? plan_.lowerBound - reached : 0) + plan_.extra;
                if (moved != expected || reached > plan_.lowerBound) {
                    failPlan(std::format(
                        "the plan moves {} clusters; lower bound {} - skipped {} - deferred {} + extra "
                        "{} gives {}",
                        moved, plan_.lowerBound, plan_.skippedClusters, plan_.deferredClusters, plan_.extra, expected));
                }
            }

            // The plan's totals against the replay.
            void checkTotals() {
                PlanTotals t;
                for (const Move& m : plan_.moves) {
                    t.clustersMoved += m.clusters;
                }
                t.moves = plan_.moves.size();
                for (std::size_t f = 0; f < last_.size(); ++f) {
                    if (last_[f] == noMove) {
                        continue;
                    }
                    ++t.files;
                    t.fragmentsBefore += fragmentsOf(movableRuns(snapshot_, static_cast<FileIndex>(f)));
                    t.fragmentsAfter += fragmentsOf(plan_.destinations(plan_.moves[last_[f]]));
                }
                out_.totals = t;
                if (t != plan_.totals) {
                    failPlan(
                        std::format("the plan's totals ({} clusters, {} moves, {} files, fragments {} -> {}) differ "
                                    "from the replay ({}, {}, {}, {} -> {})",
                                    plan_.totals.clustersMoved, plan_.totals.moves, plan_.totals.files,
                                    plan_.totals.fragmentsBefore, plan_.totals.fragmentsAfter, t.clustersMoved, t.moves,
                                    t.files, t.fragmentsBefore, t.fragmentsAfter));
                }
            }

            const Snapshot& snapshot_; // The volume before the plan.
            const Plan& plan_; // The plan.
            const ValidateOptions& options_; // Extra checks.
            HoleSet free_; // Free space as the replay goes.
            std::vector<std::uint32_t> last_; // Per file: its last replayed move (where its clusters are), or noMove.
            std::vector<FileState> state_; // Per file: unmoved, placed or parked.
            std::size_t parkedCount_ = 0; // Files in scratch space now.
            Validation out_; // Result.
        };

    } // namespace

    Validation validatePlan(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options) {
        return Replay(snapshot, plan, options).run();
    }

} // namespace icd::core::planner
