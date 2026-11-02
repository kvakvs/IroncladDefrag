#include "core/planner/Validator.h"

#include "core/planner/HoleSet.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace icd::core::planner {

    namespace {

        // The replay: free space and the runs of files that moved so far.
        class Replay {
        public:
            // Starts from the snapshot.
            Replay(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options) :
                snapshot_(snapshot), plan_(plan), options_(options), free_(snapshot.freeSpace.runs()) {}

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

            // Where a file's movable clusters are now.
            [[nodiscard]] std::vector<ClusterRun> currentRuns(FileIndex file) const {
                const auto it = current_.find(file);
                return it != current_.end() ? it->second : movableRuns(snapshot_, file);
            }

            // Checks one move and applies it.
            void replay(std::size_t index, const Move& m) {
                if (m.file >= snapshot_.files.size()) {
                    fail(index, m, "names no file of the snapshot");
                    return;
                }
                const auto sources = plan_.sources(m);
                const auto destinations = plan_.destinations(m);
                const std::vector<ClusterRun> now = currentRuns(m.file);
                if (fixedFile(snapshot_, m.file, clustersOf(movableRuns(snapshot_, m.file)))) {
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
                apply(index, m, now, destinations);
            }

            // Takes the destinations and frees the sources.
            void apply(std::size_t index, const Move& m, const std::vector<ClusterRun>& now,
                       std::span<const ClusterRun> destinations) {
                for (const ClusterRun& d : destinations) {
                    if (d.length == 0 || !free_.take(d)) {
                        fail(index, m, std::format("destination [{}, {}) is not free at its move", d.start, d.end()));
                    }
                }
                for (const ClusterRun& s : now) {
                    if (!free_.add(s)) {
                        fail(index, m, "its old clusters were free already");
                    }
                }
                current_[m.file].assign(destinations.begin(), destinations.end());
            }

            // At most one move per file, except a park and its return.
            void checkCount(std::size_t index, const Move& m) {
                if (m.fromParking) {
                    if (parked_.erase(m.file) == 0) {
                        fail(index, m, "returns from parking without having been parked");
                    }
                    placed_.insert(m.file);
                    return;
                }
                if (placed_.contains(m.file) || parked_.contains(m.file)) {
                    fail(index, m, "moves a file a second time");
                }
                (m.reason == MoveReason::Park ? parked_ : placed_).insert(m.file);
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
                if (!parked_.empty()) {
                    failPlan(std::format("{} parked file(s) never return from parking", parked_.size()));
                }
            }

            // Moved clusters against the lower bound, skipped and deferred clusters and the extra.
            void checkAccounting() {
                ClusterCount moved = 0;
                ClusterCount extra = 0;
                for (const Move& m : plan_.moves) {
                    moved += m.clusters;
                    extra += m.reason == MoveReason::Park || m.reason == MoveReason::Evict ? m.clusters : 0;
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
                    failPlan(std::format("parking and evictions move {} clusters, the plan reports {} extra", extra,
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
                t.files = current_.size();
                for (const auto& [file, runs] : current_) {
                    t.fragmentsBefore += fragmentsOf(movableRuns(snapshot_, file));
                    t.fragmentsAfter += fragmentsOf(runs);
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
            std::unordered_map<FileIndex, std::vector<ClusterRun>> current_; // Runs of files moved so far.
            std::unordered_set<FileIndex> placed_; // Files at their final place.
            std::unordered_set<FileIndex> parked_; // Files in scratch space.
            Validation out_; // Result.
        };

    } // namespace

    Validation validatePlan(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options) {
        return Replay(snapshot, plan, options).run();
    }

} // namespace icd::core::planner
