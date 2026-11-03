#include "core/planner/Validator.h"

#include "core/planner/HoleSet.h"

#include <algorithm>
#include <format>
#include <optional>

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

        // Moved clusters against the lower bound, skipped and deferred clusters and the extra: what is wrong, if
        // anything.
        std::vector<std::string> accountingIssues(const Plan& plan) {
            std::vector<std::string> out;
            ClusterCount moved = 0;
            ClusterCount extra = 0;
            for (const Move& m : plan.moves) {
                moved += m.clusters;
                extra += m.extra ? m.clusters : 0;
            }
            ClusterCount skipped = 0;
            for (const SkippedFile& s : plan.skipped) {
                skipped += s.clusters;
            }
            if (skipped != plan.skippedClusters) {
                out.push_back(
                    std::format("skipped files hold {} clusters, the plan reports {}", skipped, plan.skippedClusters));
            }
            if (extra != plan.extra) {
                out.push_back(
                    std::format("moves marked extra move {} clusters, the plan reports {} extra", extra, plan.extra));
            }
            const ClusterCount reached = plan.skippedClusters + plan.deferredClusters;
            const ClusterCount expected = (plan.lowerBound >= reached ? plan.lowerBound - reached : 0) + plan.extra;
            if (moved != expected || reached > plan.lowerBound) {
                out.push_back(std::format(
                    "the plan moves {} clusters; lower bound {} - skipped {} - deferred {} + extra {} gives {}", moved,
                    plan.lowerBound, plan.skippedClusters, plan.deferredClusters, plan.extra, expected));
            }
            return out;
        }

        // Whether the plan's accounting adds up.
        bool accountingHolds(const Plan& plan) { return accountingIssues(plan).empty(); }

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
                for (std::string& message : accountingIssues(plan_)) {
                    failPlan(std::move(message));
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

        // Every move linked to the same file's move before it, and each file to its last move.
        struct MoveLinks {
            std::vector<std::uint32_t>
                previous; // Per move: the file's move before it (where its clusters are), or noMove.
            std::vector<std::uint32_t> last; // Per file: its last move, or noMove.
        };

        // Links the moves; nullopt when a move names no file of the snapshot.
        std::optional<MoveLinks> linkMoves(const Snapshot& snapshot, const Plan& plan) {
            MoveLinks links{.previous = std::vector<std::uint32_t>(plan.moves.size(), noMove),
                            .last = std::vector<std::uint32_t>(snapshot.files.size(), noMove)};
            for (std::size_t i = 0; i < plan.moves.size(); ++i) {
                const FileIndex f = plan.moves[i].file;
                if (f >= snapshot.files.size()) {
                    return std::nullopt;
                }
                links.previous[i] = links.last[f];
                links.last[f] = static_cast<std::uint32_t>(i);
            }
            return links;
        }

        // The state a move leaves its file in, valid or not (as Replay::checkCount() sets it).
        FileState stateAfter(const Move& m) {
            return !m.fromParking && m.reason == MoveReason::Park ? FileState::Parked : FileState::Placed;
        }

        // Whether runs are exactly the file's movable runs in the snapshot (movableRuns() without building them).
        bool sameAsMovable(const Snapshot& snapshot, FileIndex file, std::span<const ClusterRun> runs) {
            const auto records = snapshot.extents.records();
            std::size_t i = 0;
            for (const std::uint32_t position : snapshot.extents.extentsOf(file)) {
                const ExtentRecord& r = records[position];
                if ((r.flags() & ExtentUnmovable) != 0) {
                    continue;
                }
                if (i == runs.size() || runs[i] != r.run()) {
                    return false;
                }
                ++i;
            }
            return i == runs.size();
        }

        // The part of run inside window (length 0 when they do not overlap).
        ClusterRun clipTo(ClusterRun run, ClusterRun window) {
            const Lcn start = std::max(run.start, window.start);
            const Lcn end = std::min(run.end(), window.end());
            return end > start ? ClusterRun{start, end - start} : ClusterRun{start, 0};
        }

        // The checks of one move that need only the file's previous move, so moves are checked on any thread: the
        // conditions of Replay::replay(), checkCount() and checkZone().
        class MoveCheck {
        public:
            // Keeps the inputs.
            MoveCheck(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options,
                      const MoveLinks& links) : snapshot_(snapshot), plan_(plan), options_(options), links_(links) {}

            // Whether move i passes every check; adds its change of the parked file count to `parked`.
            [[nodiscard]] bool passes(std::size_t i, std::int64_t& parked) const {
                const Move& m = plan_.moves[i];
                const auto sources = plan_.sources(m);
                const auto destinations = plan_.destinations(m);
                const std::uint32_t prev = links_.previous[i];
                const bool current = prev == noMove
                    ? sameAsMovable(snapshot_, m.file, sources)
                    : std::ranges::equal(sources, plan_.destinations(plan_.moves[prev]));
                const ClusterCount clusters = clustersOf(sources);
                if (!current || fixedFile(snapshot_, m.file, clusters) || clusters != clustersOf(destinations) ||
                    m.clusters != clusters) {
                    return false;
                }
                if (!runsFit(sources, destinations)) {
                    return false;
                }
                const FileState before = prev == noMove ? FileState::Unmoved : stateAfter(plan_.moves[prev]);
                return countPasses(m, before, parked) && zonePasses(m, destinations);
            }

        private:
            // Whether no destination is empty and every run lies inside the volume (the stripes cover only that).
            [[nodiscard]] bool runsFit(std::span<const ClusterRun> sources,
                                       std::span<const ClusterRun> destinations) const {
                const auto inside = [&](const ClusterRun& r) { return r.end() <= snapshot_.geometry.totalClusters; };
                return std::ranges::none_of(destinations, [](const ClusterRun& d) { return d.length == 0; }) &&
                    std::ranges::all_of(destinations, inside) && std::ranges::all_of(sources, inside);
            }

            // At most one move per file, except a park and its return.
            static bool countPasses(const Move& m, FileState before, std::int64_t& parked) {
                if (m.fromParking) {
                    parked -= before == FileState::Parked ? 1 : 0;
                    return before == FileState::Parked;
                }
                parked += m.reason == MoveReason::Park ? 1 : 0;
                return before == FileState::Unmoved;
            }

            // A move into a zone lands inside it.
            [[nodiscard]] bool zonePasses(const Move& m, std::span<const ClusterRun> destinations) const {
                if (options_.layout == nullptr || m.zone == noZone || m.reason == MoveReason::Park) {
                    return true;
                }
                const ZoneTarget* target = options_.layout->target(m.zone);
                return target != nullptr &&
                    std::ranges::all_of(destinations, [&](const ClusterRun& d) { return target->holds(d); });
            }

            const Snapshot& snapshot_; // The volume before the plan.
            const Plan& plan_; // The plan.
            const ValidateOptions& options_; // Extra checks.
            const MoveLinks& links_; // Previous moves.
        };

        // One LCN stripe after replaying the parts of every move inside it.
        struct StripeEnd {
            bool ok = true; // Every destination part was free at its move and no source part was.
            ClusterCount free = 0; // Free clusters at the end.
            std::size_t runs = 0; // Free runs at the end (split at the stripe's edges).
            bool freeAtStart =
                false; // Its first cluster is free at the end (a run may continue from the stripe before).
            bool freeAtEnd = false; // Its last cluster is free at the end.
        };

        // The free runs of the snapshot inside a stripe, clipped to it.
        HoleSet freeInStripe(std::span<const ClusterRun> freeRuns, ClusterRun stripe) {
            const auto first =
                std::ranges::partition_point(freeRuns, [&](const ClusterRun& r) { return r.end() <= stripe.start; });
            std::vector<ClusterRun> inside;
            for (auto it = first; it != freeRuns.end() && it->start < stripe.end(); ++it) {
                inside.push_back(clipTo(*it, stripe));
            }
            return HoleSet(inside);
        }

        // Replays the parts of one move inside a stripe: its destination parts are taken (they must be free), then its
        // source parts freed (they must not be free yet); false when either fails.
        bool replayInStripe(HoleSet& free, const Plan& plan, const Move& m, ClusterRun stripe) {
            for (const ClusterRun& d : plan.destinations(m)) {
                const ClusterRun part = clipTo(d, stripe);
                if (part.length != 0 && !free.take(part)) {
                    return false;
                }
            }
            for (const ClusterRun& s : plan.sources(m)) {
                const ClusterRun part = clipTo(s, stripe);
                if (part.length != 0 && !free.add(part)) {
                    return false;
                }
            }
            return true;
        }

        // Replays the free space of one stripe over the moves touching it (in plan order): a destination is free when
        // every part of it is free in its stripe, and a source is not free when no part of it is, so stripes replay
        // independently.
        StripeEnd replayStripe(std::span<const ClusterRun> freeRuns, const Plan& plan, ClusterRun stripe,
                               std::span<const std::uint32_t> moves) {
            HoleSet free = freeInStripe(freeRuns, stripe);
            for (const std::uint32_t i : moves) {
                if (!replayInStripe(free, plan, plan.moves[i], stripe)) {
                    return {.ok = false};
                }
            }
            return {.ok = true,
                    .free = free.total(),
                    .runs = free.size(),
                    .freeAtStart = free.contains({stripe.start, 1}),
                    .freeAtEnd = free.contains({stripe.end() - 1, 1})};
        }

        // Appends where a move's destinations and sources start.
        void appendStarts(const Plan& plan, const Move& m, std::vector<Lcn>& starts) {
            for (const auto runs : {plan.destinations(m), plan.sources(m)}) {
                for (const ClusterRun& r : runs) {
                    starts.push_back(r.start);
                }
            }
        }

        // Stripe boundaries that split the moves' runs (destinations taken and sources freed) about evenly: hot zones
        // hold most destinations, the areas they empty most sources.
        std::vector<Lcn> stripeEdges(const Snapshot& snapshot, const Plan& plan) {
            constexpr std::size_t movesPerStripe = 256; // Test-sized plans get a few stripes too.
            constexpr std::size_t maxStripes = 256;
            constexpr std::size_t sampleEvery = 16;
            const std::size_t stripes = std::clamp<std::size_t>(plan.moves.size() / movesPerStripe, 1, maxStripes);
            std::vector<Lcn> starts;
            for (std::size_t i = 0; i < plan.moves.size(); i += sampleEvery) {
                appendStarts(plan, plan.moves[i], starts);
            }
            std::ranges::sort(starts);
            std::vector<Lcn> edges{0};
            for (std::size_t s = 1; s < stripes && !starts.empty(); ++s) {
                const Lcn edge = starts[s * starts.size() / stripes];
                if (edge > edges.back() && edge < snapshot.geometry.totalClusters) {
                    edges.push_back(edge);
                }
            }
            edges.push_back(std::max<Lcn>(snapshot.geometry.totalClusters, 1));
            return edges;
        }

        // Adds move i to the list of every stripe a run of it reaches into, once per stripe (its runs come one after
        // another).
        void addToStripes(std::vector<std::vector<std::uint32_t>>& lists, std::span<const Lcn> edges, std::uint32_t i,
                          ClusterRun run) {
            if (run.length == 0) {
                return;
            }
            const auto stripeOf = [&](Lcn lcn) {
                const auto after = static_cast<std::size_t>(std::ranges::upper_bound(edges, lcn) - edges.begin());
                return std::min(after - 1, lists.size() - 1); // Past the volume's end: the last stripe.
            };
            for (std::size_t s = stripeOf(run.start); s <= stripeOf(run.end() - 1); ++s) {
                if (lists[s].empty() || lists[s].back() != i) {
                    lists[s].push_back(i);
                }
            }
        }

        // Adds move i to the lists of the stripes its runs reach into.
        void addMoveToStripes(std::vector<std::vector<std::uint32_t>>& lists, std::span<const Lcn> edges,
                              const Plan& plan, std::uint32_t i) {
            const Move& m = plan.moves[i];
            for (const auto runs : {plan.destinations(m), plan.sources(m)}) {
                for (const ClusterRun& r : runs) {
                    addToStripes(lists, edges, i, r);
                }
            }
        }

        // The moves whose runs reach into each stripe, in plan order: each chunk of moves fills lists of its own,
        // joined per stripe in chunk order.
        std::vector<std::vector<std::uint32_t>> movesByStripe(const Plan& plan, std::span<const Lcn> edges,
                                                              Executor& executor) {
            const std::size_t stripes = edges.size() - 1;
            const ChunkedRange range{.count = plan.moves.size(), .grain = 1U << 16U};
            std::vector<std::vector<std::vector<std::uint32_t>>> parts(range.chunks());
            executor.parallelFor(range, [&](std::size_t begin, std::size_t end) {
                auto& lists = parts[begin / range.chunkSize()];
                lists.resize(stripes);
                for (std::size_t i = begin; i < end; ++i) {
                    addMoveToStripes(lists, edges, plan, static_cast<std::uint32_t>(i));
                }
            });
            std::vector<std::vector<std::uint32_t>> out(stripes);
            executor.parallelFor({.count = stripes, .grain = 1}, [&](std::size_t begin, std::size_t end) {
                for (std::size_t s = begin; s < end; ++s) {
                    for (const auto& lists : parts) {
                        out[s].insert(out[s].end(), lists[s].begin(), lists[s].end());
                    }
                }
            });
            return out;
        }

        // The free space at the end, from every stripe; nullopt when a stripe's replay failed.
        std::optional<StripeEnd> replayFreeSpace(const Snapshot& snapshot, const Plan& plan, Executor& executor) {
            const std::vector<Lcn> edges = stripeEdges(snapshot, plan);
            const auto moves = movesByStripe(plan, edges, executor);
            std::vector<StripeEnd> ends(edges.size() - 1);
            executor.parallelFor({.count = ends.size(), .grain = 1}, [&](std::size_t begin, std::size_t end) {
                for (std::size_t s = begin; s < end; ++s) {
                    ends[s] =
                        replayStripe(snapshot.freeSpace.runs(), plan, {edges[s], edges[s + 1] - edges[s]}, moves[s]);
                }
            });
            StripeEnd total;
            for (std::size_t s = 0; s < ends.size(); ++s) {
                if (!ends[s].ok) {
                    return std::nullopt;
                }
                const bool joined = s > 0 && ends[s - 1].freeAtEnd && ends[s].freeAtStart; // One run across the edge.
                total.free += ends[s].free;
                total.runs += ends[s].runs - (joined ? 1 : 0);
            }
            return total;
        }

        // The plan's totals recomputed from the last move of every file (as Replay::checkTotals() does).
        PlanTotals totalsOf(const Snapshot& snapshot, const Plan& plan, const MoveLinks& links, Executor& executor) {
            PlanTotals t = parallelReduce(
                executor, {.count = links.last.size(), .grain = 1U << 16U}, PlanTotals{},
                [&](std::size_t begin, std::size_t end) {
                    PlanTotals part;
                    for (std::size_t f = begin; f < end; ++f) {
                        if (links.last[f] != noMove) {
                            ++part.files;
                            part.fragmentsBefore += movableFragments(snapshot, static_cast<FileIndex>(f));
                            part.fragmentsAfter += fragmentsOf(plan.destinations(plan.moves[links.last[f]]));
                        }
                    }
                    return part;
                },
                [](PlanTotals a, const PlanTotals& b) {
                    a.files += b.files;
                    a.fragmentsBefore += b.fragmentsBefore;
                    a.fragmentsAfter += b.fragmentsAfter;
                    return a;
                });
            for (const Move& m : plan.moves) {
                t.clustersMoved += m.clusters;
            }
            t.moves = plan.moves.size();
            return t;
        }

        // Whether every move passes its own checks and every parked file returns.
        bool movesPass(const MoveCheck& check, std::size_t moves, Executor& executor) {
            struct Part {
                bool ok = true; // Every move of the chunk passed.
                std::int64_t parked = 0; // Change of the parked file count.
            };
            const Part all = parallelReduce(
                executor, {.count = moves, .grain = 1U << 15U}, Part{},
                [&](std::size_t begin, std::size_t end) {
                    Part part;
                    for (std::size_t i = begin; i < end && part.ok; ++i) {
                        part.ok = check.passes(i, part.parked);
                    }
                    return part;
                },
                [](Part a, const Part& b) { return Part{.ok = a.ok && b.ok, .parked = a.parked + b.parked}; });
            return all.ok && all.parked == 0;
        }

        // The validation of a valid plan, checked in parallel; nullopt when anything is off (the caller then replays
        // the plan in order for its issues).
        std::optional<Validation> validInParallel(const Snapshot& snapshot, const Plan& plan,
                                                  const ValidateOptions& options, Executor& executor) {
            const auto links = linkMoves(snapshot, plan);
            if (!links || !movesPass(MoveCheck(snapshot, plan, options, *links), plan.moves.size(), executor)) {
                return std::nullopt;
            }
            const auto free = replayFreeSpace(snapshot, plan, executor);
            if (!free) {
                return std::nullopt;
            }
            Validation out{.issues = {},
                           .totals = totalsOf(snapshot, plan, *links, executor),
                           .freeClusters = free->free,
                           .freeRuns = free->runs};
            return out.totals == plan.totals ? std::optional(std::move(out)) : std::nullopt;
        }

    } // namespace

    Validation validatePlan(const Snapshot& snapshot, const Plan& plan, const ValidateOptions& options) {
        InlineExecutor inline_;
        Executor& executor = options.executor != nullptr ? *options.executor : inline_;
        if (auto valid = validInParallel(snapshot, plan, options, executor); valid && accountingHolds(plan)) {
            return std::move(*valid);
        }
        return Replay(snapshot, plan, options).run();
    }

} // namespace icd::core::planner
