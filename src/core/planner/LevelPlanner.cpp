#include "core/planner/LevelPlanner.h"

#include "core/planner/CandidateFilter.h"
#include "core/planner/DefragOnly.h"
#include "core/planner/MakeRoom.h"
#include "core/planner/MoveOrdering.h"
#include "core/planner/PackToBack.h"
#include "core/planner/Placement.h"
#include "core/planner/SortZone.h"
#include "core/planner/VssAlign.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace icd::core::planner {

    namespace {

        // A file's runs at the end of a plan: its last move's destinations, or where it is.
        class FinalRuns {
        public:
            // Indexes the plan's last move per file.
            FinalRuns(const Snapshot& snapshot, const Plan& plan) : snapshot_(snapshot), plan_(plan) {
                for (std::size_t i = 0; i < plan.moves.size(); ++i) {
                    last_[plan.moves[i].file] = i;
                }
            }

            // Whether the plan moves the file.
            [[nodiscard]] bool moved(FileIndex file) const { return last_.contains(file); }

            // The file's movable runs after the plan.
            [[nodiscard]] std::vector<ClusterRun> of(FileIndex file) const {
                const auto it = last_.find(file);
                if (it == last_.end()) {
                    return movableRuns(snapshot_, file);
                }
                const auto d = plan_.destinations(plan_.moves[it->second]);
                return {d.begin(), d.end()};
            }

        private:
            const Snapshot& snapshot_; // The volume before.
            const Plan& plan_; // The plan.
            std::unordered_map<FileIndex, std::size_t> last_; // Last move per file.
        };

        // Clusters inside their zone and placed clusters in total.
        struct FitCount {
            ClusterCount inZone = 0; // Clusters of placed files inside their zone.
            ClusterCount total = 0; // Clusters of placed files.

            // As a percentage (100 without placed files).
            [[nodiscard]] double percent() const {
                return total == 0 ? 100.0 : 100.0 * static_cast<double>(inZone) / static_cast<double>(total);
            }
        };

        // Fragmented files and files with clusters.
        struct FragCount {
            std::uint64_t fragmented = 0; // Files in more than one fragment.
            std::uint64_t files = 0; // Files with clusters.

            // As a percentage.
            [[nodiscard]] double percent() const {
                return files == 0 ? 0.0 : 100.0 * static_cast<double>(fragmented) / static_cast<double>(files);
            }
        };

        // Free runs after replaying a plan without checks.
        HoleSet freeAfter(const Snapshot& snapshot, const Plan& plan) {
            HoleSet free(snapshot.freeSpace.runs());
            for (const Move& m : plan.moves) {
                takeRuns(free, plan.destinations(m));
                releaseRuns(free, plan.sources(m));
            }
            return free;
        }

        // Pool of after-plan free space: free runs plus what must-move files vacate, without the reserved runs.
        HoleSet afterPlanPool(const Snapshot& snapshot, const LowerBound& bound, std::span<const ClusterRun> reserved) {
            HoleSet pool(snapshot.freeSpace.runs());
            for (std::size_t f = 0; f < bound.mustMove.size(); ++f) {
                if (bound.mustMove[f] != 0) {
                    releaseRuns(pool, movableRuns(snapshot, static_cast<FileIndex>(f)));
                }
            }
            for (const ClusterRun& r : reserved) {
                takeRuns(pool, pool.within(r));
            }
            return pool;
        }

        // The LCN span of a zone target.
        ClusterRun spanOf(const ZoneTarget& t) {
            const Lcn first = t.firstLcn().value_or(0);
            return {first, t.endLcn() - first};
        }

        // What all zones' placement produced.
        struct Placed {
            std::vector<Destination> destinations; // Every destination.
            std::vector<FileIndex> overflow; // Files no zone held.
            std::unordered_set<FileIndex> extraOverflow; // Overflowing files that move only as extra.
        };

        // The level planner of one level.
        class ZonePlanner {
        public:
            // Keeps the inputs.
            ZonePlanner(const PlanInput& input, const ZoneAssignment& assignment, const LevelPolicy& policy) :
                input_(input), snapshot_(*input.snapshot), script_(*input.script), assignment_(assignment),
                policy_(policy) {}

            // Plans the level.
            PlanResult run(Executor& executor, const CancelToken& token) {
                LayoutInputs base{.reserved = input_.reserved};
                base = fitTarget(snapshot_, script_, assignment_, input_.classes, *input_.catalog, base,
                                 policy_.goodEnough, executor);
                result_.layout = resolveLayout(snapshot_, script_, assignment_, input_.classes, *input_.catalog, base);
                bound_ =
                    lowerBound(snapshot_, script_, assignment_, result_.layout, policy_.goodEnough, executor, token);
                sorted_ =
                    applySortBounds(snapshot_, script_, assignment_, policy_.effort, policy_.sortTolerance, bound_);
                reserved_ = result_.layout.reservedUsed ? std::span<const ClusterRun>() : input_.reserved;
                Placed placed = placeRounds(executor, token);
                finish(placed);
                return std::move(result_);
            }

        private:
            // Rounds of placement: files that find no space at all stay where they are and the round is planned again
            // without them, so no other file is promised their clusters.
            Placed placeRounds(Executor& executor, const CancelToken& token) {
                constexpr int rounds = 3;
                for (int round = 0;; ++round) {
                    const Candidates candidates = filterCandidates(bound_, assignment_, script_.zones.size());
                    pool_ = afterPlanPool(snapshot_, bound_, reserved_);
                    evicted_.clear();
                    noSpace_.clear();
                    Placed placed = placeZones(candidates, executor, token);
                    placeDefragment(candidates, placed);
                    reconcile(placed);
                    if (noSpace_.empty() || round + 1 == rounds) {
                        recordStuck();
                        return placed;
                    }
                    for (const FileIndex f : noSpace_) {
                        bound_.mustMove[f] = 0;
                        stuck_.push_back(f);
                    }
                }
            }

            // Files that found no space in any round, as skipped.
            void recordStuck() {
                stuck_.insert(stuck_.end(), noSpace_.begin(), noSpace_.end());
                for (const FileIndex f : stuck_) {
                    result_.plan.skipped.push_back(
                        {.file = f, .reason = SkipReason::NoSpace, .clusters = assignment_.movable[f]});
                    result_.plan.skippedClusters += assignment_.movable[f];
                }
            }

            // The sort decision of a zone, or an unsorted one.
            [[nodiscard]] const SortedZone& decisionFor(ZoneIndex zone) const {
                static const SortedZone unsorted;
                const auto it = std::ranges::find(sorted_, zone, &SortedZone::zone);
                return it == sorted_.end() ? unsorted : *it;
            }

            // fragment_ok of a zone at this level.
            [[nodiscard]] std::optional<std::uint64_t> fragmentOk(ZoneIndex zone) const {
                return fragmentOkFor(script_, zone, policy_.goodEnough);
            }

            // Places one zone's candidates in its own slice of the pool.
            FillResult placeZone(const ZoneTarget& t, std::span<const FileIndex> movers, HoleSet& slice) const {
                const FillOptions options{.zone = t.zone, .home = spanOf(t), .fragmentOk = fragmentOk(t.zone)};
                const SortedZone& decision = decisionFor(t.zone);
                if (t.back) {
                    return packToBack(snapshot_, t, decision, movers, slice, options);
                }
                return sortZone(snapshot_, t, decision, movers, slice, options);
            }

            // Places every zone in parallel, each on the part of the pool inside its pieces; then puts what is left
            // of each part back into the pool.
            Placed placeZones(const Candidates& candidates, Executor& executor, const CancelToken& token) {
                const auto& zones = result_.layout.zones;
                std::vector<HoleSet> slices(zones.size());
                for (std::size_t i = 0; i < zones.size(); ++i) {
                    std::vector<ClusterRun> parts;
                    for (const ClusterRun& piece : zones[i].pieces) {
                        const auto inside = pool_.within(piece);
                        parts.insert(parts.end(), inside.begin(), inside.end());
                    }
                    takeRuns(pool_, parts);
                    slices[i] = HoleSet(parts);
                }
                std::vector<FillResult> results(zones.size());
                executor.parallelFor(
                    {.count = zones.size(), .grain = 1},
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t i = begin; i < end; ++i) {
                            results[i] = placeZone(zones[i], candidates.perZone[zones[i].zone], slices[i]);
                        }
                    },
                    token);
                Placed placed;
                for (std::size_t i = 0; i < zones.size(); ++i) {
                    releaseRuns(pool_, slices[i].runs());
                    collect(results[i], placed);
                }
                return placed;
            }

            // Adds one zone's result.
            static void collect(FillResult& result, Placed& placed) {
                placed.destinations.insert(placed.destinations.end(), result.placed.begin(), result.placed.end());
                placed.overflow.insert(placed.overflow.end(), result.overflow.begin(), result.overflow.end());
                placed.extraOverflow.insert(result.released.begin(), result.released.end());
            }

            // Defragment-only files near where they are.
            void placeDefragment(const Candidates& candidates, Placed& placed) {
                const DefragOptions options{.fragmentOk = policy_.goodEnough.wholeFiles
                                                ? std::nullopt
                                                : std::optional(strategy::Zone::defaultFragmentOk)};
                FillResult result = defragmentDestinations(snapshot_, candidates.defragment, pool_, options);
                collect(result, placed);
            }

            // Overflow: room made by evicting small files, else free space nearest the zone, else skipped.
            void reconcile(Placed& placed) {
                std::vector<FileIndex> order = placed.overflow;
                std::ranges::sort(order, [&](FileIndex a, FileIndex b) {
                    return assignment_.movable[a] != assignment_.movable[b]
                        ? assignment_.movable[a] > assignment_.movable[b]
                        : a < b;
                });
                for (const FileIndex file : order) {
                    const bool extra = placed.extraOverflow.contains(file);
                    if (!evictFor(file, extra, placed) && !placeNear(file, extra, placed)) {
                        skip(file, extra);
                    }
                }
            }

            // The zone target of a placed file, or null.
            [[nodiscard]] const ZoneTarget* targetOf(FileIndex file) const {
                return assignment_.roles[file] == FileRole::Placed ? result_.layout.target(assignment_.zoneOf[file])
                                                                   : nullptr;
            }

            // Whether a file may be moved out of a large file's way.
            [[nodiscard]] bool evictable(FileIndex file, FileIndex big, ZoneIndex zone) const {
                return file != big && assignment_.roles[file] == FileRole::Placed && assignment_.zoneOf[file] == zone &&
                    bound_.mustMove[file] == 0 && evicted_.count(file) == 0 &&
                    std::ranges::find(stuck_, file) == stuck_.end() &&
                    assignment_.movable[file] * 4 <= assignment_.movable[big];
            }

            // Makes room for a file inside its zone by evicting small files (plan.md 9.3.6).
            bool evictFor(FileIndex file, bool extra, Placed& placed) {
                const ZoneTarget* t = targetOf(file);
                if (!policy_.makeRoom || t == nullptr || decisionFor(t->zone).sorted) {
                    return false;
                }
                const auto runs = movableRuns(snapshot_, file);
                const auto own = sortedRuns(runs);
                const ClusterCount clusters = clustersOf(runs);
                const RoomRequest request{
                    .clusters = clusters, .own = own, .region = t->pieces, .maxEvict = clusters / 4};
                const auto room = findRoom(snapshot_, pool_, request,
                                           [&](FileIndex other) { return evictable(other, file, t->zone); });
                if (!room) {
                    return false;
                }
                return applyRoom(*room,
                                 {.file = file,
                                  .runs = {room->window},
                                  .reason = reasonFor(runs, spanOf(*t)),
                                  .zone = t->zone,
                                  .extra = extra},
                                 *t, placed);
            }

            // Evicts the blockers of a room and gives the window to the file; undone when a blocker fits nowhere.
            bool applyRoom(const Room& room, const Destination& big, const ZoneTarget& t, Placed& placed) {
                std::vector<ClusterRun> blockerRuns;
                for (const FileIndex b : room.blockers) {
                    const auto runs = movableRuns(snapshot_, b);
                    blockerRuns.insert(blockerRuns.end(), runs.begin(), runs.end());
                }
                releaseRuns(pool_, blockerRuns);
                takeRuns(pool_, std::vector<ClusterRun>{room.window});
                std::vector<Destination> moved;
                for (const FileIndex b : room.blockers) {
                    auto d = placeBlocker(b, t);
                    if (!d) {
                        for (const Destination& undo : moved) {
                            releaseRuns(pool_, undo.runs);
                        }
                        releaseRuns(pool_, std::vector<ClusterRun>{room.window});
                        takeRuns(pool_, blockerRuns);
                        return false;
                    }
                    moved.push_back(std::move(*d));
                }
                for (Destination& d : moved) {
                    evicted_.insert(d.file);
                    placed.destinations.push_back(std::move(d));
                }
                placed.destinations.push_back(big);
                return true;
            }

            // A destination for an evicted file: inside its zone, else nearest free space.
            std::optional<Destination> placeBlocker(FileIndex file, const ZoneTarget& t) {
                const auto runs = movableRuns(snapshot_, file);
                const auto own = sortedRuns(runs);
                const ClusterCount clusters = clustersOf(runs);
                for (const ClusterRun& piece : t.pieces) {
                    if (const auto run = findFirst(pool_, {.clusters = clusters, .own = own, .window = piece})) {
                        takeRuns(pool_, std::vector<ClusterRun>{*run});
                        return Destination{
                            .file = file, .runs = {*run}, .reason = MoveReason::Evict, .zone = t.zone, .extra = true};
                    }
                }
                const auto run = findNear(pool_, {.clusters = clusters, .own = own, .window = whole()}, t.endLcn());
                if (!run) {
                    return std::nullopt;
                }
                takeRuns(pool_, std::vector<ClusterRun>{*run});
                return Destination{
                    .file = file, .runs = {*run}, .reason = MoveReason::Evict, .zone = noZone, .extra = true};
            }

            // The whole volume as a window.
            [[nodiscard]] ClusterRun whole() const { return {0, snapshot_.geometry.totalClusters}; }

            // Places a file in the free space nearest its zone's end (or itself), in one run or large pieces.
            bool placeNear(FileIndex file, bool extra, Placed& placed) {
                const ZoneTarget* t = targetOf(file);
                const auto runs = movableRuns(snapshot_, file);
                const auto own = sortedRuns(runs);
                const SpaceRequest request{.clusters = clustersOf(runs), .own = own, .window = whole()};
                const Lcn near = t != nullptr ? t->endLcn() : runs.front().start;
                std::vector<ClusterRun> destination;
                if (const auto run = findNear(pool_, request, near)) {
                    destination.push_back(*run);
                }
                else if (const auto ok = fragmentOk(assignment_.zoneOf[file]); ok && !runs.empty()) {
                    const ClusterCount cluster = std::max<std::uint32_t>(snapshot_.geometry.bytesPerCluster, 1);
                    destination = findPieces(pool_, request, (*ok + cluster - 1) / cluster).value_or(destination);
                }
                if (destination.empty()) {
                    return false;
                }
                takeRuns(pool_, destination);
                const MoveReason reason =
                    fragmentsOf(runs) > fragmentsOf(destination) ? MoveReason::Defragment : MoveReason::Relocate;
                placed.destinations.push_back(
                    {.file = file, .runs = std::move(destination), .reason = reason, .zone = noZone, .extra = extra});
                return true;
            }

            // Records a file that cannot move.
            void skip(FileIndex file, bool extra) {
                if (!extra) {
                    noSpace_.push_back(file); // An extra mover was only going to make room; it simply stays.
                }
            }

            // Aligns, orders, budgets and measures.
            void finish(Placed& placed) {
                Plan& plan = result_.plan;
                plan.effort = policy_.effort;
                plan.lowerBound = bound_.clusters;
                if (input_.shadowCopies) {
                    (void)alignDestinations(snapshot_, placed.destinations, pool_, &result_.layout);
                }
                (void)orderMoves(snapshot_, placed.destinations, {.allowParking = policy_.allowParking}, plan);
                plan.extra = 0;
                for (const Move& m : plan.moves) {
                    plan.extra += m.extra ? m.clusters : 0;
                }
                applyBudget(snapshot_, plan, policy_.budget);
                finishTotals(plan, snapshot_);
                plan.notes.insert(plan.notes.end(), result_.layout.notes.begin(), result_.layout.notes.end());
                result_.metrics = measurePlan(input_, assignment_, result_.layout, plan);
            }

            const PlanInput& input_; // Inputs.
            const Snapshot& snapshot_; // The volume.
            const strategy::ZoneScript& script_; // The strategy.
            const ZoneAssignment& assignment_; // Zones and roles.
            const LevelPolicy& policy_; // The level.
            LowerBound bound_; // Must-move files.
            std::vector<SortedZone> sorted_; // Sorted zones at this level.
            HoleSet pool_; // After-plan free space not yet given out.
            std::unordered_set<FileIndex> evicted_; // Files moved out of the way.
            std::span<const ClusterRun> reserved_; // Reserved runs kept out of the pool.
            std::vector<FileIndex> noSpace_; // Files this round could not place.
            std::vector<FileIndex> stuck_; // Files earlier rounds could not place; they stay.
            PlanResult result_; // Output.
        };

        // Whether a file is fragmented after the plan.
        bool fragmentedAfter(const Snapshot& snapshot, const FinalRuns& final, FileIndex file) {
            return final.moved(file) ? fragmentsOf(final.of(file)) > 1 : snapshot.extents.fragmentCount(file) > 1;
        }

        // Fragment counts before and after.
        std::pair<FragCount, FragCount> fragmentation(const Snapshot& snapshot, const FinalRuns& final) {
            FragCount before;
            FragCount after;
            for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
                const auto file = static_cast<FileIndex>(f);
                if (snapshot.files.allocatedClusters(file) == 0) {
                    continue;
                }
                ++before.files;
                ++after.files;
                before.fragmented += snapshot.extents.fragmentCount(file) > 1 ? 1 : 0;
                after.fragmented += fragmentedAfter(snapshot, final, file) ? 1 : 0;
            }
            return {before, after};
        }

        // Zone fit before and after.
        std::pair<FitCount, FitCount> zoneFit(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                              const TargetLayout& layout, const FinalRuns& final) {
            FitCount before;
            FitCount after;
            for (std::size_t f = 0; f < assignment.roles.size(); ++f) {
                const ZoneTarget* t =
                    assignment.roles[f] == FileRole::Placed ? layout.target(assignment.zoneOf[f]) : nullptr;
                if (t == nullptr) {
                    continue;
                }
                const auto file = static_cast<FileIndex>(f);
                const auto holds = [&](const std::vector<ClusterRun>& runs) {
                    return std::ranges::all_of(runs, [&](const ClusterRun& r) { return t->holds(r); });
                };
                const ClusterCount clusters = assignment.movable[f];
                before.total += clusters;
                after.total += clusters;
                before.inZone += holds(movableRuns(snapshot, file)) ? clusters : 0;
                after.inZone += holds(final.of(file)) ? clusters : 0;
            }
            return {before, after};
        }

    } // namespace

    ZoneAssignment assignInput(const PlanInput& input, Executor& executor, const CancelToken& token) {
        return assignZones(*input.snapshot, *input.script, *input.catalog,
                           {.classes = input.classes, .churn = input.churn, .now = input.now}, executor, token);
    }

    PlanResult planZones(const PlanInput& input, const ZoneAssignment& assignment, const LevelPolicy& policy,
                         Executor& executor, const CancelToken& token) {
        return ZonePlanner(input, assignment, policy).run(executor, token);
    }

    PlanMetrics measurePlan(const PlanInput& input, const ZoneAssignment& assignment, const TargetLayout& layout,
                            const Plan& plan) {
        const Snapshot& snapshot = *input.snapshot;
        const FinalRuns final(snapshot, plan);
        const auto [fragBefore, fragAfter] = fragmentation(snapshot, final);
        const auto [fitBefore, fitAfter] = zoneFit(snapshot, assignment, layout, final);
        const HoleSet free = freeAfter(snapshot, plan);
        ClusterCount largest = 0;
        for (const ClusterRun& r : free.runs()) {
            largest = std::max(largest, r.length);
        }
        return {.fragmentedBefore = fragBefore.percent(),
                .fragmentedAfter = fragAfter.percent(),
                .zoneFitBefore = fitBefore.percent(),
                .zoneFitAfter = fitAfter.percent(),
                .freeRunsBefore = snapshot.freeSpace.size(),
                .freeRunsAfter = free.size(),
                .largestFreeAfter = largest,
                .seconds = planSeconds(plan, snapshot.geometry.bytesPerCluster, input.time)};
    }

    ClusterCount usedClusters(const Snapshot& snapshot) {
        return snapshot.geometry.totalClusters - snapshot.freeSpace.freeClusters();
    }

} // namespace icd::core::planner
