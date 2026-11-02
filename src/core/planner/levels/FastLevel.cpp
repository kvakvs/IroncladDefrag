#include "core/planner/levels/FastLevel.h"

#include "core/planner/FastFill.h"
#include "core/planner/MoveOrdering.h"
#include "core/planner/Placement.h"
#include "core/snapshot/Stripes.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <unordered_set>

namespace icd::core::planner {

    namespace {

        // FILETIME ticks per day.
        constexpr std::int64_t ticksPerDay = 864'000'000'000;

        // A big long-lived file (plan.md 14, decision 6).
        constexpr std::uint64_t bigFileBytes = 64ULL << 20U;

        // Days unchanged that make a big file long-lived.
        constexpr std::int64_t longLivedDays = 30;

        // Budget share of used space at x1 (plan.md 9.2).
        constexpr double fastShare = 0.02;

        // Time budget at x1.
        constexpr double fastSeconds = 15.0 * 60.0;

        // Longest piece of a hot spot planned as one, in stripes: a volume that is hot nearly everywhere becomes
        // pieces that compact on their own and can be ranked.
        constexpr std::size_t maxSpotStripes = 8;

        // Hot-spot pieces are planned, hottest first, until their movers hold this many times the cluster budget,
        // so the budget still has a choice while the rest of a large volume is left for the next run.
        constexpr ClusterCount budgetReach = 2;

        // A piece of a hot spot and how hot it is.
        struct ScoredSpot {
            ClusterRun run; // Its clusters.
            double score = 0.0; // Mean score of its stripes.
        };

        // Whether each stripe is hot: at or above the threshold, or one cold stripe between two hot ones.
        std::vector<bool> hotStripes(const std::vector<StripeScore>& scores, double threshold) {
            std::vector<bool> hot(scores.size());
            for (std::size_t i = 0; i < scores.size(); ++i) {
                hot[i] = scores[i].score >= threshold;
            }
            for (std::size_t i = 1; i + 1 < hot.size(); ++i) {
                hot[i] = hot[i] || (hot[i - 1] && hot[i + 1]); // Bridge one cold stripe between hot ones.
            }
            return hot;
        }

        // Consecutive hot stripes as pieces of at most `longest` stripes, with their mean scores, in LCN order.
        std::vector<ScoredSpot> hotPieces(const StripeLayout& layout, const std::vector<StripeScore>& scores,
                                          const std::vector<bool>& hot, std::size_t longest) {
            std::vector<ScoredSpot> out;
            std::size_t stripes = 0;
            for (std::size_t i = 0; i < hot.size(); ++i) {
                const bool joins = hot[i] && i > 0 && hot[i - 1] && stripes < longest;
                if (!hot[i]) {
                    continue;
                }
                if (joins) {
                    out.back().run.length += layout.range(i).length;
                    out.back().score += scores[i].score;
                    ++stripes;
                    continue;
                }
                if (!out.empty()) {
                    out.back().score /= static_cast<double>(stripes);
                }
                out.push_back({layout.range(i), scores[i].score});
                stripes = 1;
            }
            if (!out.empty()) {
                out.back().score /= static_cast<double>(stripes);
            }
            return out;
        }

        // Whether a file churns: flagged by the journal or kept in place by the strategy.
        bool churns(const ZoneAssignment& a, std::span<const std::uint8_t> churn, FileIndex file) {
            return a.roles[file] == FileRole::InPlace || (file < churn.size() && churn[file] != 0);
        }

        // Score of one stripe.
        StripeScore scoreOf(const Snapshot& snapshot, const ZoneAssignment& a, std::span<const std::uint8_t> churn,
                            const HotSpotOptions& options, const StripeView& view) {
            ClusterCount churning = 0;
            ClusterCount fragmented = 0;
            ClusterCount used = 0;
            for (const ExtentRecord& r : view.extents) {
                const ClusterCount part = clip(r.run(), view.range).length;
                used += part;
                churning += churns(a, churn, r.file()) ? part : 0;
                fragmented += snapshot.extents.fragmentCount(r.file()) > 1 ? part : 0;
            }
            const ClusterCount small =
                options.smallHoleBytes / std::max<std::uint32_t>(snapshot.geometry.bytesPerCluster, 1);
            std::uint32_t holes = 0;
            for (const ClusterRun& f : view.freeRuns) {
                holes += clip(f, view.range).length < small ? 1 : 0;
            }
            StripeScore s;
            s.churn = static_cast<double>(churning) / static_cast<double>(std::max<ClusterCount>(view.range.length, 1));
            s.holes = std::min(
                1.0, static_cast<double>(holes) / std::max(1.0, static_cast<double>(options.holesForFullScore)));
            s.fragments = static_cast<double>(fragmented) / static_cast<double>(std::max<ClusterCount>(used, 1));
            s.score = 0.4 * s.churn + 0.3 * s.holes + 0.3 * s.fragments;
            return s;
        }

        // The movable runs of a file sorted by LCN.
        std::vector<ClusterRun> lcnRuns(const Snapshot& snapshot, FileIndex file) {
            return sortedRuns(movableRuns(snapshot, file));
        }

        // Whether runs lie inside a range.
        bool inside(const std::vector<ClusterRun>& runs, ClusterRun range) {
            return !runs.empty() && runs.front().start >= range.start && runs.back().end() <= range.end();
        }

        // Whether a run lies inside one of the LCN-sorted, disjoint runs.
        bool holds(const std::vector<ClusterRun>& sorted, ClusterRun run) {
            const auto it = std::ranges::upper_bound(sorted, run.start, {}, &ClusterRun::start);
            return it != sorted.begin() && run.start >= std::prev(it)->start && run.end() <= std::prev(it)->end();
        }

        // What Fast plans for one hot spot.
        struct SpotPlan {
            ClusterRun spot; // The hot spot.
            std::vector<ClusterRun>
                packed{}; // Its packed region: the first usable clusters, as many as its files need.
            std::vector<FileIndex> movers{}; // Files that must move within it.
            std::vector<FileIndex> bigs{}; // Big long-lived files that leave for their zone.
        };

        // The Fast planner.
        class FastPlanner {
        public:
            // Keeps the inputs.
            FastPlanner(const PlanInput& input, const ZoneAssignment& assignment, const HotSpotOptions& options) :
                input_(input), snapshot_(*input.snapshot), assignment_(assignment), options_(options),
                policy_(fastPolicy(input)) {}

            // Plans the level.
            PlanResult run(Executor& executor, const CancelToken& token) {
                executor_ = &executor;
                result_.layout = resolveLayout(snapshot_, *input_.script, assignment_, input_.classes, *input_.catalog,
                                               {.reserved = input_.reserved});
                const std::vector<SpotPlan> spots = hottestSpots(executor);
                for (const SpotPlan& s : spots) {
                    result_.hotSpots.push_back(s.spot);
                }
                token.throwIfCancelled();
                for (const SpotPlan& s : spots) {
                    for (const auto* list : {&s.movers, &s.bigs}) {
                        for (const FileIndex f : *list) {
                            result_.plan.lowerBound += assignment_.movable[f];
                        }
                    }
                }
                std::vector<Destination> destinations = placeRounds(spots);
                finish(destinations);
                return std::move(result_);
            }

        private:
            // The hot-spot pieces Fast plans: the hottest first until their movers reach budgetReach times the
            // cluster budget; in LCN order.
            [[nodiscard]] std::vector<SpotPlan> hottestSpots(Executor& executor) const {
                const StripeLayout layout(snapshot_.geometry.totalClusters);
                const auto scores = scoreStripes(snapshot_, assignment_, input_.churn, options_, executor);
                std::vector<ScoredSpot> pieces =
                    hotPieces(layout, scores, hotStripes(scores, options_.threshold), maxSpotStripes);
                std::ranges::stable_sort(pieces,
                                         [](const ScoredSpot& a, const ScoredSpot& b) { return a.score > b.score; });
                const ClusterCount reach = policy_.budget.maxClusters ? *policy_.budget.maxClusters * budgetReach
                                                                      : std::numeric_limits<ClusterCount>::max();
                std::vector<SpotPlan> out;
                ClusterCount planned = 0;
                for (const ScoredSpot& piece : pieces) {
                    if (planned >= reach) {
                        break;
                    }
                    out.push_back(planSpot(piece.run));
                    planned += moverClusters(out.back());
                }
                std::ranges::sort(out, {}, [](const SpotPlan& s) { return s.spot.start; });
                return out;
            }

            // Clusters of the files a spot plan moves.
            [[nodiscard]] ClusterCount moverClusters(const SpotPlan& s) const {
                ClusterCount out = 0;
                for (const auto* list : {&s.movers, &s.bigs}) {
                    for (const FileIndex f : *list) {
                        out += assignment_.movable[f];
                    }
                }
                return out;
            }

            // Whether a file is a big long-lived file.
            [[nodiscard]] bool bigLongLived(FileIndex file) const {
                const std::int64_t age = (input_.now - snapshot_.files.times(file).modified) / ticksPerDay;
                return assignment_.movable[file] * snapshot_.geometry.bytesPerCluster >= bigFileBytes &&
                    age >= longLivedDays && assignment_.roles[file] == FileRole::Placed &&
                    result_.layout.target(assignment_.zoneOf[file]) != nullptr;
            }

            // Whether there is free space on both sides of runs inside the spot.
            [[nodiscard]] bool splitsFreeSpace(const std::vector<ClusterRun>& runs, ClusterRun spot) const {
                const ClusterRun before{spot.start, runs.front().start - spot.start};
                const ClusterRun after{runs.back().end(), spot.end() - runs.back().end()};
                return !snapshot_.freeSpace.overlapping(before).empty() &&
                    !snapshot_.freeSpace.overlapping(after).empty() && before.length > 0 && after.length > 0;
            }

            // The files that may move and lie wholly inside the spot.
            [[nodiscard]] std::vector<FileIndex> membersOf(ClusterRun spot) const {
                std::unordered_set<FileIndex> seen;
                std::vector<FileIndex> out;
                for (const ExtentRecord& r : snapshot_.extents.overlapping(spot)) {
                    const FileIndex f = r.file();
                    const FileRole role = assignment_.roles[f];
                    const bool movable = role == FileRole::Placed || role == FileRole::Defragment;
                    if (movable && !churns(assignment_, input_.churn, f) && seen.insert(f).second &&
                        inside(lcnRuns(snapshot_, f), spot)) {
                        out.push_back(f);
                    }
                }
                return out;
            }

            // The spot's usable pieces: its clusters minus everything that is not a staying member.
            [[nodiscard]] std::vector<ClusterRun> piecesOf(ClusterRun spot,
                                                           const std::unordered_set<FileIndex>& members) const {
                std::vector<ClusterRun> out;
                Lcn cursor = spot.start;
                for (const ExtentRecord& r : snapshot_.extents.overlapping(spot)) {
                    const bool usable = members.contains(r.file()) && (r.flags() & ExtentUnmovable) == 0;
                    const ClusterRun part = clip(r.run(), spot);
                    if (usable || part.length == 0) {
                        continue;
                    }
                    if (part.start > cursor) {
                        out.push_back({cursor, part.start - cursor});
                    }
                    cursor = std::max(cursor, part.end());
                }
                if (cursor < spot.end()) {
                    out.push_back({cursor, spot.end() - cursor});
                }
                return out;
            }

            // The first `need` clusters of the pieces.
            static std::vector<ClusterRun> headOf(const std::vector<ClusterRun>& pieces, ClusterCount need) {
                std::vector<ClusterRun> out;
                for (const ClusterRun& p : pieces) {
                    if (need == 0) {
                        break;
                    }
                    const ClusterCount take = std::min(need, p.length);
                    out.push_back({p.start, take});
                    need -= take;
                }
                return out;
            }

            // Members, big files leaving and the packed region of one hot spot, and the files that must move.
            SpotPlan planSpot(ClusterRun spot) const {
                SpotPlan plan{.spot = spot};
                std::unordered_set<FileIndex> members;
                std::vector<FileIndex> memberList; // In LCN order, so the plan does not depend on hashing.
                ClusterCount need = 0;
                for (const FileIndex f : membersOf(spot)) {
                    const auto runs = lcnRuns(snapshot_, f);
                    if (bigLongLived(f) && splitsFreeSpace(runs, spot)) {
                        plan.bigs.push_back(f);
                        continue;
                    }
                    members.insert(f);
                    memberList.push_back(f);
                    need += assignment_.movable[f];
                }
                plan.packed = headOf(piecesOf(spot, members), need);
                const auto okRuns = [&](FileIndex f) {
                    return fragmentsOk(movableRuns(snapshot_, f),
                                       fragmentOkFor(*input_.script, assignment_.zoneOf[f], {}),
                                       snapshot_.geometry.bytesPerCluster);
                };
                for (const FileIndex f : memberList) {
                    const auto runs = lcnRuns(snapshot_, f);
                    const bool packedHolds =
                        std::ranges::all_of(runs, [&](const ClusterRun& r) { return holds(plan.packed, r); });
                    if (!packedHolds || !okRuns(f)) {
                        plan.movers.push_back(f);
                    }
                }
                return plan;
            }

            // Rounds of placement: files that find no place stay where they are and the round is planned again without
            // them, so no other file is promised their clusters.
            std::vector<Destination> placeRounds(const std::vector<SpotPlan>& spots) {
                constexpr int rounds = 3;
                std::vector<Destination> destinations;
                for (int round = 0;; ++round) {
                    buildPool(spots);
                    noSpace_.clear();
                    destinations.clear();
                    for (const SpotPlan& s : spots) {
                        placeSpot(s, destinations);
                    }
                    if (noSpace_.empty() || round + 1 == rounds) {
                        break;
                    }
                    stuck_.insert(noSpace_.begin(), noSpace_.end());
                }
                stuck_.insert(noSpace_.begin(), noSpace_.end());
                for (const FileIndex f : stuck_) {
                    result_.plan.skipped.push_back(
                        {.file = f, .reason = SkipReason::NoSpace, .clusters = assignment_.movable[f]});
                    result_.plan.skippedClusters += assignment_.movable[f];
                }
                return destinations;
            }

            // The files of a list that are still moving (not stuck in an earlier round).
            [[nodiscard]] std::vector<FileIndex> moving(const std::vector<FileIndex>& files) const {
                std::vector<FileIndex> out;
                std::ranges::copy_if(files, std::back_inserter(out), [&](FileIndex f) { return !stuck_.contains(f); });
                return out;
            }

            // After-plan free space: free runs plus the moving files' runs, minus the reserved runs.
            void buildPool(const std::vector<SpotPlan>& spots) {
                std::vector<ClusterRun> runs(snapshot_.freeSpace.runs().begin(), snapshot_.freeSpace.runs().end());
                for (const SpotPlan& s : spots) {
                    for (const auto* list : {&s.movers, &s.bigs}) {
                        for (const FileIndex f : moving(*list)) {
                            const auto vacated = movableRuns(snapshot_, f);
                            runs.insert(runs.end(), vacated.begin(), vacated.end());
                        }
                    }
                }
                pool_ = HoleSet(runs);
                for (const ClusterRun& r : input_.reserved) {
                    takeRuns(pool_, pool_.within(r));
                }
            }

            // Destinations of one hot spot: big files to their zone, the rest packed towards its head.
            void placeSpot(const SpotPlan& s, std::vector<Destination>& out) {
                for (const FileIndex f : moving(s.bigs)) {
                    placeBig(f, out);
                }
                const FillOptions options{.zone = noZone, .home = s.spot, .fragmentOk = std::nullopt};
                FillResult fill = fastFill(snapshot_, s.packed, moving(s.movers), pool_, options);
                out.insert(out.end(), fill.placed.begin(), fill.placed.end());
                for (const FileIndex f : fill.overflow) {
                    skip(f);
                }
            }

            // A big long-lived file goes straight into its zone, or stays.
            void placeBig(FileIndex file, std::vector<Destination>& out) {
                const ZoneTarget* t = result_.layout.target(assignment_.zoneOf[file]);
                const auto runs = movableRuns(snapshot_, file);
                const auto own = sortedRuns(runs);
                for (const ClusterRun& piece : t->pieces) {
                    const SpaceRequest request{.clusters = clustersOf(runs), .own = own, .window = piece};
                    const auto run = t->back ? findLast(pool_, request) : findFirst(pool_, request);
                    if (run) {
                        takeRuns(pool_, std::vector<ClusterRun>{*run});
                        out.push_back({.file = file, .runs = {*run}, .reason = MoveReason::Relocate, .zone = t->zone});
                        return;
                    }
                }
                skip(file);
            }

            // Records a file Fast cannot place this round.
            void skip(FileIndex file) { noSpace_.push_back(file); }

            // Orders, budgets and measures.
            void finish(std::vector<Destination>& destinations) {
                Plan& plan = result_.plan;
                plan.effort = strategy::Effort::Fast;
                (void)orderMoves(snapshot_, destinations, {.allowParking = false, .scratch = {}, .executor = executor_},
                                 plan);
                applyBudget(snapshot_, plan, policy_.budget);
                finishTotals(plan, snapshot_);
                plan.notes.emplace_back(
                    "Fast works on the busiest areas first: run it again to keep improving the drive.");
                result_.metrics = measurePlan(input_, assignment_, result_.layout, plan, *executor_);
            }

            const PlanInput& input_; // Inputs.
            const Snapshot& snapshot_; // The volume.
            const ZoneAssignment& assignment_; // Zones and roles.
            const HotSpotOptions& options_; // Hot-spot rules.
            LevelPolicy policy_; // Budget.
            HoleSet pool_; // After-plan free space.
            std::vector<FileIndex> noSpace_; // Files this round could not place.
            std::set<FileIndex> stuck_; // Files earlier rounds could not place; they stay.
            PlanResult result_; // Output.
            Executor* executor_ = nullptr; // The executor of run().
        };

    } // namespace

    std::vector<StripeScore> scoreStripes(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                          std::span<const std::uint8_t> churn, const HotSpotOptions& options,
                                          Executor& executor) {
        const Stripes stripes(snapshot, StripeLayout(snapshot.geometry.totalClusters));
        return mapStripes(executor, stripes,
                          [&](const StripeView& view) { return scoreOf(snapshot, assignment, churn, options, view); });
    }

    std::vector<ClusterRun> findHotSpots(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                         std::span<const std::uint8_t> churn, const HotSpotOptions& options,
                                         Executor& executor) {
        const StripeLayout layout(snapshot.geometry.totalClusters);
        const auto scores = scoreStripes(snapshot, assignment, churn, options, executor);
        std::vector<ClusterRun> out;
        for (const ScoredSpot& s : hotPieces(layout, scores, hotStripes(scores, options.threshold),
                                             std::numeric_limits<std::size_t>::max())) {
            out.push_back(s.run);
        }
        return out;
    }

    LevelPolicy fastPolicy(const PlanInput& input) {
        const double multiplier = std::max(1.0, input.fastBudgetMultiplier);
        const auto used = static_cast<double>(usedClusters(*input.snapshot));
        return {.effort = strategy::Effort::Fast,
                .goodEnough = {},
                .sortTolerance = 0.0,
                .allowParking = false,
                .makeRoom = false,
                .budget = {.maxClusters = static_cast<ClusterCount>(std::ceil(fastShare * multiplier * used)),
                           .maxSeconds = fastSeconds * multiplier,
                           .time = input.time}};
    }

    PlanResult planFast(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                        const CancelToken& token, const HotSpotOptions& options) {
        return FastPlanner(input, assignment, options).run(executor, token);
    }

} // namespace icd::core::planner
