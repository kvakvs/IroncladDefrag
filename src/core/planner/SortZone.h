#pragma once

#include "core/exec/Executor.h"
#include "core/planner/FastFill.h"
#include "core/planner/LowerBound.h"
#include "core/planner/TargetLayout.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace icd::core::planner {

    // Every file's position in one zone order (plan.md 8.1): smaller keys go first, ties go to the lower file index,
    // so the order is total and the same on every run.
    class SortKeys {
    public:
        // Keys for an order; DirectoryName ranks directories in name order depth first and names case-insensitively.
        SortKeys(const Snapshot& snapshot, strategy::ZoneOrder order);

        // The key of a file.
        [[nodiscard]] std::uint64_t key(FileIndex file) const { return keys_[file]; }

        // Whether a goes before b.
        [[nodiscard]] bool before(FileIndex a, FileIndex b) const {
            return keys_[a] != keys_[b] ? keys_[a] < keys_[b] : a < b;
        }

    private:
        // Name keys: the rank of the file's name, under its directory's rank in a name-ordered walk when byDirectory.
        void nameKeys(const Snapshot& snapshot, bool byDirectory);

        std::vector<std::uint64_t> keys_; // Per file.
    };

    // Sort keys shared by the zones and levels of one planning run on one snapshot: each order's keys (seconds of work
    // on 20 M files) are built once, by the first planner that needs them, while the others wait for them.
    class SortKeyCache {
    public:
        // The keys of an order, built on first use; the snapshot must be the same on every call.
        [[nodiscard]] std::shared_ptr<const SortKeys> get(const Snapshot& snapshot, strategy::ZoneOrder order);

    private:
        std::mutex mutex_; // Guards keys_.
        std::map<strategy::ZoneOrder, std::shared_future<std::shared_ptr<const SortKeys>>> keys_; // Built or building.
    };

    // A heaviest increasing subsequence.
    struct LisResult {
        std::vector<std::uint8_t> keep{}; // Per position: 1 when it is part of the subsequence.
        ClusterCount weight = 0; // Its total weight.
    };

    // The byte-weighted longest increasing subsequence (plan.md 9.3.4): of the items in their current order, the set
    // already in the right relative order whose total weight is largest. `ranks` are distinct order positions.
    // O(n log n); ties resolve towards earlier items, so the result is deterministic.
    [[nodiscard]] LisResult weightedLis(std::span<const std::uint32_t> ranks, std::span<const ClusterCount> weights);

    // How one sorted zone is planned at a level.
    struct SortedZone {
        ZoneIndex zone = noZone; // Script index.
        strategy::ZoneOrder order = strategy::ZoneOrder::None; // The order at this level.
        bool sorted = false; // False when the zone is too far from its order for the level's tolerance (fast-fill).
        std::vector<FileIndex> anchors{}; // Files that stay, in LCN (and key) order.
        std::shared_ptr<const SortKeys> keys{}; // The order's keys, shared with the destination step.
    };

    // Whether a zone is sorted at a level: sort zones and pack-to-back zones with an order at that level, never at
    // Fast (plan.md 9.2).
    [[nodiscard]] bool sortsAt(const strategy::Zone& zone, strategy::Effort effort);

    // Decides every sorted zone at a level and adds what sorting needs to the lower bound. In the target every file of
    // the zone sits at its packed position in key order (the zone's start plus the files before it) plus the free
    // clusters before it, which never fall along the order and stay within the zone's room (length beyond need). So
    // a file already in place can stay only at an offset from its packed position in [0, room], and the files that
    // stay must have offsets that never fall: the heaviest such chain stays (the anchors) and the rest must move. The
    // gaps between anchors then always hold the files whose keys fall between them. A zone is sorted only when that
    // moves at most `tolerance` of its in-place bytes (1 = always, as at Maximum); otherwise it is filled without
    // order and the bound is unchanged.
    // Keys come from `cache` (null: a cache of this call, so zones of one order share them); the zones' files are
    // sorted on `executor` (null: inline).
    [[nodiscard]] std::vector<SortedZone> applySortBounds(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                                          const ZoneAssignment& assignment, const TargetLayout& layout,
                                                          strategy::Effort effort, double tolerance, LowerBound& bound,
                                                          SortKeyCache* cache = nullptr, Executor* executor = nullptr);

    // Destinations for a sorted zone: the movers (the zone's candidates) in key order go into the gaps between the
    // anchors that bracket their keys, packed from each gap's start. A gap without room for its movers gives up the
    // anchor that ends it, which then moves too (marked extra, listed in FillResult::released when it overflows), and
    // takes in the next gap; movers that still find no room overflow. Zones that are not sorted at the level are
    // fast-filled.
    [[nodiscard]] FillResult sortZone(const Snapshot& snapshot, const ZoneTarget& target, const SortedZone& decision,
                                      std::span<const FileIndex> movers, HoleSet& pool, const FillOptions& options);

} // namespace icd::core::planner
