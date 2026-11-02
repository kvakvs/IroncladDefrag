#include "core/planner/SortZone.h"

#include "core/planner/CycleGuard.h"
#include "core/planner/Placement.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace icd::core::planner {

    namespace {

        using strategy::ZoneOrder;

        // Most significant time first: newer files get smaller keys.
        std::uint64_t newestFirst(std::int64_t time) {
            return std::numeric_limits<std::uint64_t>::max() -
                static_cast<std::uint64_t>(std::max<std::int64_t>(time, 0));
        }

        // ASCII case-insensitive comparison, then bytes, so "IMG_2.jpg" < "img_3.jpg" and the order is total.
        bool nameLess(std::string_view a, std::string_view b) {
            const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
            const std::size_t common = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < common; ++i) {
                if (lower(a[i]) != lower(b[i])) {
                    return lower(a[i]) < lower(b[i]);
                }
            }
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        }

        // The rank of every name in the pool.
        std::vector<std::uint32_t> nameRanks(const NamePool& names) {
            std::vector<std::uint32_t> ids(names.size());
            std::iota(ids.begin(), ids.end(), 0U);
            std::ranges::sort(ids,
                              [&](std::uint32_t a, std::uint32_t b) { return nameLess(names.name(a), names.name(b)); });
            std::vector<std::uint32_t> rank(names.size());
            for (std::uint32_t r = 0; r < ids.size(); ++r) {
                rank[ids[r]] = r;
            }
            return rank;
        }

        // Every file's position in a depth-first walk that visits children in name order.
        std::vector<std::uint32_t> treeRanks(const Snapshot& snapshot, const std::vector<std::uint32_t>& names) {
            std::vector<std::uint32_t> rank(snapshot.files.size(), 0);
            std::vector<FileIndex> stack(snapshot.tree.roots().rbegin(), snapshot.tree.roots().rend());
            std::uint32_t next = 0;
            std::vector<FileIndex> children;
            while (!stack.empty()) {
                const FileIndex file = stack.back();
                stack.pop_back();
                rank[file] = next++;
                const auto kids = snapshot.tree.children(file);
                children.assign(kids.begin(), kids.end());
                std::ranges::sort(children, [&](FileIndex a, FileIndex b) {
                    const auto ra = names[snapshot.files.name(a)];
                    const auto rb = names[snapshot.files.name(b)];
                    return ra != rb ? ra > rb : a > b; // Reversed: the stack pops the first name first.
                });
                stack.insert(stack.end(), children.begin(), children.end());
            }
            return rank;
        }

        // The key of one file in a time or size order.
        std::uint64_t simpleKey(const Snapshot& snapshot, ZoneOrder order, FileIndex file) {
            const FileTimes& t = snapshot.files.times(file);
            switch (order) {
            case ZoneOrder::Size:
                return snapshot.files.sizeBytes(file);
            case ZoneOrder::Age:
                return newestFirst(std::min(t.modified, t.created));
            case ZoneOrder::Modified:
                return newestFirst(t.modified);
            case ZoneOrder::Created:
                return newestFirst(t.created);
            case ZoneOrder::Accessed:
                return newestFirst(t.accessed);
            default:
                return 0;
            }
        }

        // An anchor's place: where its clusters start and end.
        struct AnchorSpan {
            FileIndex file = noFile; // The file.
            ClusterRun span; // From its first to one past its last cluster.
        };

        // A file to place in a sorted zone.
        struct Mover {
            FileIndex file = noFile; // The file.
            ClusterCount clusters = 0; // Movable clusters.
            std::vector<ClusterRun> runs{}; // Its runs in VCN order.
            std::vector<ClusterRun> own{}; // Its runs by LCN.
            bool extra = false; // A given-up anchor.
        };

        // Where packing a group stands: the hole and the next free cluster in it.
        struct PackCursor {
            std::size_t hole = 0; // Index into the group's holes.
            Lcn at = 0; // Next position.
        };

        // A group's holes with the room left from each one on, so packing knows how much it may skip.
        struct Holes {
            std::vector<ClusterRun> runs; // In LCN order.
            std::vector<ClusterCount> after; // Per hole: clusters of the holes after it.

            // Takes the holes.
            explicit Holes(std::vector<ClusterRun> holes) : runs(std::move(holes)), after(runs.size(), 0) {
                for (std::size_t i = runs.size(); i-- > 1;) {
                    after[i - 1] = after[i] + runs[i].length;
                }
            }

            // Free clusters from the cursor on.
            [[nodiscard]] ClusterCount left(const PackCursor& c) const {
                return c.hole < runs.size() ? runs[c.hole].end() - c.at + after[c.hole] : 0;
            }
        };

        // Consecutive gaps between anchors packed together after the anchors between them gave up.
        struct Group {
            std::size_t firstGap = 0; // First gap.
            std::size_t lastGap = 0; // Last gap.
            ClusterCount demand = 0; // Clusters of its movers.
            ClusterCount capacity = 0; // Free clusters of its gaps (and of given-up anchors).
            std::vector<Mover> movers{}; // In key order.
        };

        // The sorted-zone planner of one zone.
        class Sorter {
        public:
            // Prepares anchors, movers and gaps.
            Sorter(const Snapshot& snapshot, const ZoneTarget& target, const SortedZone& decision, HoleSet& pool,
                   const FillOptions& options) :
                snapshot_(snapshot), target_(target), decision_(decision), pool_(pool), options_(options),
                own_(snapshot), guard_(options.guard != nullptr ? *options.guard : own_) {
                for (const FileIndex file : decision.anchors) {
                    const auto runs = sortedRuns(movableRuns(snapshot, file));
                    anchors_.push_back({file, {runs.front().start, runs.back().end() - runs.front().start}});
                }
            }

            // Places the movers.
            FillResult run(std::span<const FileIndex> files) {
                std::vector<std::vector<Mover>> perGap(anchors_.size() + 1);
                for (const FileIndex file : files) {
                    perGap[gapOf(file)].push_back(moverOf(file, false));
                }
                for (auto& gap : perGap) {
                    std::ranges::sort(gap,
                                      [&](const Mover& a, const Mover& b) { return keys().before(a.file, b.file); });
                }
                std::vector<Group> groups;
                for (std::size_t gap = 0; gap < perGap.size(); gap = groups.back().lastGap + 1) {
                    groups.push_back(growGroup(gap, perGap));
                }
                while (groups.size() > 1 && groups.back().demand > groups.back().capacity) {
                    Group last = std::move(groups.back());
                    groups.pop_back();
                    mergeInto(groups.back(), std::move(last));
                }
                for (Group& group : groups) {
                    pack(group);
                }
                return std::move(out_);
            }

        private:
            // The zone's keys.
            [[nodiscard]] const SortKeys& keys() const { return *decision_.keys; }

            // The gap whose bracketing anchors' keys hold the file's key.
            [[nodiscard]] std::size_t gapOf(FileIndex file) const {
                const auto it = std::ranges::partition_point(
                    anchors_, [&](const AnchorSpan& a) { return keys().before(a.file, file); });
                return static_cast<std::size_t>(it - anchors_.begin());
            }

            // A mover for a file.
            [[nodiscard]] Mover moverOf(FileIndex file, bool extra) const {
                std::vector<ClusterRun> runs = movableRuns(snapshot_, file);
                std::vector<ClusterRun> own = sortedRuns(runs);
                return {file, clustersOf(runs), std::move(runs), std::move(own), extra};
            }

            // The LCN range of a gap.
            [[nodiscard]] ClusterRun gapRange(std::size_t gap) const {
                const Lcn start = gap == 0 ? target_.firstLcn().value_or(0) : anchors_[gap - 1].span.end();
                const Lcn end = gap == anchors_.size() ? target_.endLcn() : anchors_[gap].span.start;
                return {start, end > start ? end - start : 0};
            }

            // The free holes of the zone inside a range, in LCN order.
            [[nodiscard]] std::vector<ClusterRun> holesIn(ClusterRun range) const {
                std::vector<ClusterRun> out;
                const auto& pieces = target_.pieces;
                auto it = std::ranges::upper_bound(pieces, range.start, {}, &ClusterRun::end);
                for (; it != pieces.end() && it->start < range.end(); ++it) {
                    const Lcn start = std::max(it->start, range.start);
                    const Lcn end = std::min(it->end(), range.end());
                    if (end > start) {
                        const auto inside = pool_.within({start, end - start});
                        out.insert(out.end(), inside.begin(), inside.end());
                    }
                }
                return out;
            }

            // Free clusters of a gap.
            [[nodiscard]] ClusterCount capacityOf(std::size_t gap) const { return clustersOf(holesIn(gapRange(gap))); }

            // Starts a group at a gap and gives up anchors to the right until its movers fit (or the zone ends).
            Group growGroup(std::size_t gap, std::vector<std::vector<Mover>>& perGap) {
                Group group{.firstGap = gap, .lastGap = gap, .capacity = capacityOf(gap)};
                addMovers(group, perGap[gap]);
                while (group.demand > group.capacity && group.lastGap < anchors_.size()) {
                    giveUp(group);
                    ++group.lastGap;
                    group.capacity += capacityOf(group.lastGap);
                    addMovers(group, perGap[group.lastGap]);
                }
                return group;
            }

            // Gives up the anchor that ends `left` and merges `right` (the group after it) into `left`: the zone's last
            // gap can only grow towards the front.
            void mergeInto(Group& left, Group right) {
                giveUp(left);
                left.lastGap = right.lastGap;
                left.demand += right.demand;
                left.capacity += right.capacity;
                for (Mover& m : right.movers) {
                    left.movers.push_back(std::move(m));
                }
            }

            // Gives up the anchor after a group's last gap: it moves too, and its clusters become room.
            void giveUp(Group& group) {
                Mover anchor = moverOf(anchors_[group.lastGap].file, true);
                releaseRuns(pool_, anchor.runs);
                group.demand += anchor.clusters;
                group.capacity += anchor.clusters;
                group.movers.push_back(std::move(anchor));
            }

            // Appends a gap's movers to a group.
            static void addMovers(Group& group, std::vector<Mover>& movers) {
                for (Mover& m : movers) {
                    group.demand += m.clusters;
                    group.movers.push_back(std::move(m));
                }
            }

            // Packs a group's movers in key order from the start of its range.
            void pack(Group& group) {
                const ClusterRun first = gapRange(group.firstGap);
                const ClusterRun last = gapRange(group.lastGap);
                const Holes holes(holesIn({first.start, last.end() - first.start}));
                PackCursor cursor{.hole = 0, .at = holes.runs.empty() ? 0 : holes.runs.front().start};
                ClusterCount demand = group.demand;
                for (Mover& m : group.movers) {
                    if (settle(m, holes, cursor, demand)) {
                        place(m, {cursor.at, m.clusters});
                        cursor.at += m.clusters;
                    }
                    else {
                        overflow(m);
                    }
                    demand -= m.clusters;
                }
            }

            // Finds the mover's position: the next one that fits, moved past files that would close a dependency cycle
            // while the group's spare room (free clusters left beyond the demand left) covers the skip.
            bool settle(const Mover& m, const Holes& holes, PackCursor& c, ClusterCount demand) {
                constexpr int skips = 16;
                for (int i = 0; i < skips; ++i) {
                    if (!advanceTo(m, holes.runs, c)) {
                        return false;
                    }
                    const auto end = guard_.cycleEnd(m.file, {c.at, m.clusters});
                    const ClusterCount left = holes.left(c);
                    if (!end || left < demand || *end - c.at > left - demand) {
                        return true;
                    }
                    c.at = *end;
                }
                return advanceTo(m, holes.runs, c);
            }

            // Moves the cursor to the first position at or after it where the mover fits a hole without overlapping its
            // own runs; false when the holes run out.
            static bool advanceTo(const Mover& m, const std::vector<ClusterRun>& holes, PackCursor& c) {
                while (c.hole < holes.size()) {
                    const ClusterRun& h = holes[c.hole];
                    c.at = std::max(c.at, h.start);
                    if (c.at + m.clusters > h.end()) {
                        ++c.hole;
                        continue;
                    }
                    const auto it = std::ranges::upper_bound(m.own, c.at, {}, &ClusterRun::end);
                    if (it != m.own.end() && it->start < c.at + m.clusters) {
                        c.at = it->end();
                        continue;
                    }
                    return true;
                }
                return false;
            }

            // Records a destination.
            void place(const Mover& m, ClusterRun run) {
                (void)pool_.take(run);
                guard_.record(m.file, std::span(&run, 1));
                const MoveReason reason = reasonFor(m.runs, options_.home);
                out_.placed.push_back({.file = m.file,
                                       .runs = {run},
                                       .reason = reason == MoveReason::Compact ? MoveReason::Sort : reason,
                                       .zone = options_.zone,
                                       .extra = m.extra});
            }

            // Records a mover that found no room.
            void overflow(const Mover& m) {
                out_.overflow.push_back(m.file);
                if (m.extra) {
                    out_.released.push_back(m.file);
                }
            }

            const Snapshot& snapshot_; // The volume.
            const ZoneTarget& target_; // The zone.
            const SortedZone& decision_; // Anchors and keys.
            HoleSet& pool_; // Space free after the plan.
            const FillOptions& options_; // Zone and home.
            std::vector<AnchorSpan> anchors_; // In LCN and key order.
            CycleGuard own_; // The zone's own guard when the options bring none.
            CycleGuard& guard_; // Keeps destinations from closing cycles where there is room to.
            FillResult out_; // Result.
        };

        // Combines two Fenwick entries: higher weight, then the earlier item.
        struct Best {
            ClusterCount weight = 0; // Subsequence weight ending here.
            std::size_t item = static_cast<std::size_t>(-1); // Its last item.
        };

        // The better of two entries.
        Best better(Best a, Best b) {
            if (a.weight != b.weight) {
                return a.weight > b.weight ? a : b;
            }
            return a.item < b.item ? a : b;
        }

        // The placed files of every sorted zone (all of them end up in the zone's order).
        std::vector<std::vector<FileIndex>> zoneFiles(const ZoneAssignment& assignment,
                                                      const std::vector<bool>& sorting) {
            std::vector<std::vector<FileIndex>> out(sorting.size());
            for (std::size_t f = 0; f < assignment.zoneOf.size(); ++f) {
                const ZoneIndex z = assignment.zoneOf[f];
                if (assignment.roles[f] == FileRole::Placed && z < sorting.size() && sorting[z] &&
                    assignment.movable[f] != 0) {
                    out[z].push_back(static_cast<FileIndex>(f));
                }
            }
            return out;
        }

        // A file that is in place and may stay in a sorted zone: how far it sits behind the position the packed order
        // gives it, at its first cluster (in) and after its last (out; larger when its span holds other clusters).
        struct StayOffset {
            std::size_t index = 0; // Position in the zone's key order.
            std::int64_t in = 0; // Offset of its first cluster.
            std::int64_t out = 0; // Offset after its last cluster.
            ClusterCount weight = 0; // Its movable clusters.
        };

        // What deciding one sorted zone reads.
        struct DecideContext {
            const Snapshot& snapshot; // The volume.
            const ZoneAssignment& assignment; // Zones and roles.
            const ZoneTarget& target; // The zone's place.
            const AvailableAxis& axis; // Usable cluster numbers.
            const LowerBound& bound; // Files that must move already.
        };

        // The in-place files that can stay where the zone's order puts them, in key order: a file at offset h from its
        // packed position (the zone's start plus every file before it in key order) stays only when 0 <= h <= room,
        // since the zone holds at most `room` free clusters.
        std::vector<StayOffset> stayOffsets(const DecideContext& c, const std::vector<FileIndex>& files) {
            const auto& t = c.target;
            const auto room = static_cast<std::int64_t>(t.length > t.need ? t.length - t.need : 0);
            const AvailableAxis& axis = c.axis;
            std::vector<StayOffset> out;
            std::int64_t before = static_cast<std::int64_t>(t.start);
            for (std::size_t i = 0; i < files.size(); ++i) {
                const FileIndex f = files[i];
                const auto weight = static_cast<std::int64_t>(c.assignment.movable[f]);
                const auto span = c.bound.mustMove[f] == 0 ? usableSpan(c.snapshot, f, axis) : std::nullopt;
                if (span && span->first >= before && span->second - before - weight <= room) {
                    out.push_back({i, span->first - before, span->second - before - weight, c.assignment.movable[f]});
                }
                before += weight;
            }
            return out;
        }

        // The heaviest chain of stay offsets in key order whose offsets never fall (each file's in at least the
        // previous one's out), so the files keep their order and the room between them holds the files between their
        // keys. O(n log n) with a Fenwick tree of best weights over the out offsets; ties go to earlier files.
        LisResult heaviestChain(std::span<const StayOffset> items) {
            std::vector<std::int64_t> outs(items.size());
            for (std::size_t i = 0; i < items.size(); ++i) {
                outs[i] = items[i].out;
            }
            std::ranges::sort(outs);
            outs.erase(std::ranges::unique(outs).begin(), outs.end());
            std::vector<Best> tree(outs.size() + 1);
            std::vector<std::size_t> previous(items.size(), static_cast<std::size_t>(-1));
            Best overall;
            for (std::size_t i = 0; i < items.size(); ++i) {
                Best prior;
                const auto upTo = static_cast<std::size_t>(std::ranges::upper_bound(outs, items[i].in) - outs.begin());
                for (std::size_t k = upTo; k > 0; k -= k & (~k + 1)) {
                    prior = better(prior, tree[k]);
                }
                const Best here{prior.weight + items[i].weight, i};
                previous[i] = prior.item;
                const auto at = static_cast<std::size_t>(std::ranges::lower_bound(outs, items[i].out) - outs.begin());
                for (std::size_t k = at + 1; k <= outs.size(); k += k & (~k + 1)) {
                    tree[k] = better(tree[k], here);
                }
                overall = better(overall, here);
            }
            LisResult out{.keep = std::vector<std::uint8_t>(items.size(), 0), .weight = overall.weight};
            for (std::size_t i = overall.item; i < items.size(); i = previous[i]) {
                out.keep[i] = 1;
            }
            return out;
        }

        // Decides one sorted zone and extends the bound: the heaviest chain stays, every other file must move.
        SortedZone decideZone(const DecideContext& c, std::vector<FileIndex> files,
                              std::shared_ptr<const SortKeys> keys, double tolerance, LowerBound& bound) {
            std::ranges::sort(files, [&](FileIndex a, FileIndex b) { return keys->before(a, b); });
            const std::vector<StayOffset> offsets = stayOffsets(c, files);
            const LisResult chain = heaviestChain(offsets);
            ClusterCount inPlace = 0;
            for (const FileIndex f : files) {
                inPlace += bound.mustMove[f] == 0 ? c.assignment.movable[f] : 0;
            }
            const ZoneIndex zone = c.target.zone;
            SortedZone out{.zone = zone, .keys = std::move(keys)};
            out.sorted = static_cast<double>(inPlace - chain.weight) <= tolerance * static_cast<double>(inPlace);
            if (!out.sorted) {
                return out;
            }
            std::vector<std::uint8_t> stays(files.size(), 0);
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                stays[offsets[i].index] = chain.keep[i];
            }
            for (std::size_t i = 0; i < files.size(); ++i) {
                const FileIndex f = files[i];
                if (stays[i] != 0) {
                    out.anchors.push_back(f);
                }
                else if (bound.mustMove[f] == 0) {
                    bound.mustMove[f] = 1;
                    bound.clusters += c.assignment.movable[f];
                    bound.perZone[zone] += c.assignment.movable[f];
                }
            }
            return out;
        }

    } // namespace

    SortKeys::SortKeys(const Snapshot& snapshot, strategy::ZoneOrder order) : keys_(snapshot.files.size(), 0) {
        if (order == ZoneOrder::Name || order == ZoneOrder::DirectoryName) {
            nameKeys(snapshot, order == ZoneOrder::DirectoryName);
            return;
        }
        for (std::size_t f = 0; f < keys_.size(); ++f) {
            keys_[f] = simpleKey(snapshot, order, static_cast<FileIndex>(f));
        }
    }

    void SortKeys::nameKeys(const Snapshot& snapshot, bool byDirectory) {
        const auto names = nameRanks(snapshot.names);
        const auto tree = byDirectory ? treeRanks(snapshot, names) : std::vector<std::uint32_t>{};
        for (std::size_t f = 0; f < keys_.size(); ++f) {
            const auto file = static_cast<FileIndex>(f);
            const FileIndex parent = snapshot.files.parent(file);
            const std::uint64_t dir = tree.empty() || parent == noFile ? 0 : tree[parent];
            keys_[f] = (dir << 32U) | names[snapshot.files.name(file)];
        }
    }

    LisResult weightedLis(std::span<const std::uint32_t> ranks, std::span<const ClusterCount> weights) {
        const std::size_t n = ranks.size();
        std::uint32_t top = 0;
        for (const std::uint32_t r : ranks) {
            top = std::max(top, r + 1);
        }
        std::vector<Best> tree(static_cast<std::size_t>(top) + 1); // Fenwick tree of prefix maxima over ranks.
        std::vector<std::size_t> previous(n, static_cast<std::size_t>(-1));
        Best overall;
        for (std::size_t i = 0; i < n; ++i) {
            Best before;
            for (std::size_t k = ranks[i]; k > 0; k -= k & (~k + 1)) {
                before = better(before, tree[k]);
            }
            const Best here{before.weight + weights[i], i};
            previous[i] = before.item;
            for (std::size_t k = static_cast<std::size_t>(ranks[i]) + 1; k <= top; k += k & (~k + 1)) {
                tree[k] = better(tree[k], here);
            }
            overall = better(overall, here);
        }
        LisResult out{.keep = std::vector<std::uint8_t>(n, 0), .weight = overall.weight};
        for (std::size_t i = overall.item; i < n; i = previous[i]) {
            out.keep[i] = 1;
        }
        return out;
    }

    bool sortsAt(const strategy::Zone& zone, strategy::Effort effort) {
        const bool orderedAction =
            zone.action == strategy::ZoneAction::Sort || zone.action == strategy::ZoneAction::PackToBack;
        return effort != strategy::Effort::Fast && orderedAction && zone.orderAt(effort) != ZoneOrder::None;
    }

    std::vector<SortedZone> applySortBounds(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                            const ZoneAssignment& assignment, const TargetLayout& layout,
                                            strategy::Effort effort, double tolerance, LowerBound& bound) {
        std::vector<bool> sorting(script.zones.size(), false);
        for (std::size_t z = 0; z < script.zones.size(); ++z) {
            sorting[z] = sortsAt(script.zones[z], effort) && layout.target(static_cast<ZoneIndex>(z)) != nullptr;
        }
        auto files = zoneFiles(assignment, sorting);
        std::vector<SortedZone> out;
        for (std::size_t z = 0; z < script.zones.size(); ++z) {
            if (!sorting[z]) {
                continue;
            }
            const ZoneOrder order = script.zones[z].orderAt(effort);
            auto keys = std::make_shared<const SortKeys>(snapshot, order);
            const DecideContext context{snapshot, assignment, *layout.target(static_cast<ZoneIndex>(z)), layout.axis,
                                        bound};
            SortedZone decided = decideZone(context, std::move(files[z]), std::move(keys), tolerance, bound);
            decided.order = order;
            out.push_back(std::move(decided));
        }
        return out;
    }

    FillResult sortZone(const Snapshot& snapshot, const ZoneTarget& target, const SortedZone& decision,
                        std::span<const FileIndex> movers, HoleSet& pool, const FillOptions& options) {
        if (!decision.sorted || decision.keys == nullptr) {
            return fastFill(snapshot, target.pieces, movers, pool, options);
        }
        return Sorter(snapshot, target, decision, pool, options).run(movers);
    }

} // namespace icd::core::planner
