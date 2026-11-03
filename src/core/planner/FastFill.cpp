#include "core/planner/FastFill.h"

#include "core/planner/CycleGuard.h"

#include <algorithm>
#include <numeric>

namespace icd::core::planner {

    namespace {

        // Files tried per hole position before giving up on it (skipping files whose own runs are in the way).
        constexpr std::size_t triesPerPosition = 16;

        // A file waiting for a hole.
        struct Item {
            ClusterCount clusters = 0; // Movable clusters.
            FileIndex file = noFile; // The file.
            std::vector<ClusterRun> runs; // Its runs in VCN order.
            std::vector<ClusterRun> own; // Its runs by LCN.
        };

        // Items sorted by size, with "largest remaining item of at most N clusters" in near-constant time: a
        // union-find over positions where a taken position points at the one before it.
        class SizeIndex {
        public:
            // Sorts the items (by size, then file: a total order, so the parallel sort of their positions gives the
            // same order).
            SizeIndex(std::vector<Item> items, Executor* executor) : parent_(items.size() + 1) {
                std::vector<std::uint32_t> order(items.size());
                std::iota(order.begin(), order.end(), 0U);
                InlineExecutor inline_;
                parallelSort(executor != nullptr ? *executor : inline_, order, [&](std::uint32_t a, std::uint32_t b) {
                    const Item& x = items[a];
                    const Item& y = items[b];
                    return x.clusters != y.clusters ? x.clusters < y.clusters
                                                    : (x.file != y.file ? x.file < y.file : a < b);
                });
                items_.reserve(items.size());
                for (const std::uint32_t i : order) {
                    items_.push_back(std::move(items[i]));
                }
                for (std::size_t i = 0; i < parent_.size(); ++i) {
                    parent_[i] = i;
                }
            }

            // The largest remaining item in a slot at or before `slot` (slots are 1-based; 0 means none).
            [[nodiscard]] std::size_t largestAtMost(std::size_t slot) {
                while (parent_[slot] != slot) {
                    parent_[slot] = parent_[parent_[slot]];
                    slot = parent_[slot];
                }
                return slot;
            }

            // The slot of the largest remaining item of at most `clusters` clusters, or 0.
            [[nodiscard]] std::size_t largestUpTo(ClusterCount clusters) {
                const auto it = std::ranges::upper_bound(items_, clusters, {}, &Item::clusters);
                return largestAtMost(static_cast<std::size_t>(it - items_.begin()));
            }

            // The item in a slot (1-based).
            [[nodiscard]] const Item& at(std::size_t slot) const { return items_[slot - 1]; }

            // Removes an item.
            void remove(std::size_t slot) { parent_[slot] = slot - 1; }

            // The items still waiting, smallest first.
            [[nodiscard]] std::vector<const Item*> remaining() {
                std::vector<const Item*> out;
                for (std::size_t slot = 1; slot < parent_.size(); ++slot) {
                    if (largestAtMost(slot) == slot) {
                        out.push_back(&items_[slot - 1]);
                    }
                }
                return out;
            }

        private:
            std::vector<Item> items_; // Sorted by size.
            std::vector<std::size_t> parent_; // Union-find; slot 0 is "none".
        };

        // Whether a run overlaps any of the (LCN-sorted) runs.
        bool overlaps(ClusterRun run, std::span<const ClusterRun> sorted) {
            const auto it = std::ranges::upper_bound(sorted, run.start, {}, &ClusterRun::end);
            return it != sorted.end() && it->start < run.end();
        }

        // Where an item would go in what is left of a hole.
        ClusterRun spotIn(ClusterRun left, ClusterCount clusters, PackFrom from) {
            return from == PackFrom::Front ? ClusterRun{left.start, clusters}
                                           : ClusterRun{left.end() - clusters, clusters};
        }

        // The fill of one region.
        class Filler {
        public:
            // Prepares the items.
            Filler(const Snapshot& snapshot, std::span<const FileIndex> files, HoleSet& pool,
                   const FillOptions& options) :
                pool_(pool), options_(options), index_(itemsOf(snapshot, files, options.executor), options.executor),
                own_(snapshot), guard_(options.guard != nullptr ? *options.guard : own_),
                bytesPerCluster_(std::max<std::uint32_t>(snapshot.geometry.bytesPerCluster, 1)) {}

            // Fills every hole of the region, then places what is left wherever the region still has room.
            FillResult run(std::span<const ClusterRun> region) {
                std::vector<ClusterRun> holes;
                for (const ClusterRun& piece : region) {
                    const auto inside = pool_.within(piece);
                    holes.insert(holes.end(), inside.begin(), inside.end());
                }
                if (options_.from == PackFrom::Back) {
                    std::ranges::reverse(holes);
                }
                for (ClusterRun hole : holes) {
                    fillHole(hole);
                }
                for (const Item* item : index_.remaining()) {
                    placeAnywhere(*item, region);
                }
                return std::move(out_);
            }

            // The files' items, to be freed elsewhere (millions of small allocations in a large zone).
            [[nodiscard]] SizeIndex takeIndex() { return std::move(index_); }

        private:
            // The items of the files, with their runs (read in parallel).
            static std::vector<Item> itemsOf(const Snapshot& snapshot, std::span<const FileIndex> files,
                                             Executor* executor) {
                std::vector<Item> items(files.size());
                forChunks(executor, files.size(), [&](std::size_t begin, std::size_t end) {
                    for (std::size_t i = begin; i < end; ++i) {
                        std::vector<ClusterRun> runs = movableRuns(snapshot, files[i]);
                        std::vector<ClusterRun> own = sortedRuns(runs);
                        items[i] = {clustersOf(runs), files[i], std::move(runs), std::move(own)};
                    }
                });
                return items;
            }

            // Fills one hole with the largest items that fit, until none does.
            void fillHole(ClusterRun left) {
                while (left.length > 0) {
                    const std::size_t slot = pick(left);
                    if (slot == 0) {
                        return;
                    }
                    const Item& item = index_.at(slot);
                    const ClusterRun spot = spotIn(left, item.clusters, options_.from);
                    place(item, {spot});
                    index_.remove(slot);
                    left = options_.from == PackFrom::Front ? ClusterRun{spot.end(), left.length - item.clusters}
                                                            : ClusterRun{left.start, left.length - item.clusters};
                }
            }

            // The largest item that fits what is left of a hole without overlapping its own runs or closing a
            // dependency cycle, or 0.
            std::size_t pick(ClusterRun left) {
                std::size_t slot = index_.largestUpTo(left.length);
                for (std::size_t tries = 0; slot != 0 && tries < triesPerPosition; ++tries) {
                    const Item& item = index_.at(slot);
                    const ClusterRun spot = spotIn(left, item.clusters, options_.from);
                    if (!overlaps(spot, item.own) && !guard_.closesCycle(item.file, std::span(&spot, 1))) {
                        return slot;
                    }
                    slot = index_.largestAtMost(slot - 1);
                }
                return 0;
            }

            // A file the hole walk could not place: any single run of the region, else large pieces, else overflow.
            void placeAnywhere(const Item& item, std::span<const ClusterRun> region) {
                if (!placeInOneRun(item, region) && !placeInPieces(item, region)) {
                    out_.overflow.push_back(item.file);
                }
            }

            // Places a file in the first (or last) single run of the region that holds it without closing a cycle; when
            // every run tried closes one, in the first run found (ordering breaks that cycle).
            bool placeInOneRun(const Item& item, std::span<const ClusterRun> region) {
                const auto first = findInRegion(item, spanOf(region));
                if (!first) {
                    return false;
                }
                std::optional<ClusterRun> run = first;
                ClusterRun window = spanOf(region);
                for (std::size_t tries = 0; run && tries < triesPerPosition; ++tries) {
                    if (!guard_.closesCycle(item.file, std::span(&*run, 1))) {
                        place(item, {*run});
                        return true;
                    }
                    window = options_.from == PackFrom::Front ? ClusterRun{run->end(), window.end() - run->end()}
                                                              : ClusterRun{window.start, run->start - window.start};
                    run = findInRegion(item, window);
                }
                place(item, {*first});
                return true;
            }

            // The first (or last) single run of a window that holds the file.
            [[nodiscard]] std::optional<ClusterRun> findInRegion(const Item& item, ClusterRun window) const {
                const SpaceRequest request{.clusters = item.clusters, .own = item.own, .window = window};
                return options_.from == PackFrom::Front ? findFirst(pool_, request) : findLast(pool_, request);
            }

            // Places a file in pieces of at least fragmentOk inside one piece of the region, when allowed.
            bool placeInPieces(const Item& item, std::span<const ClusterRun> region) {
                if (!options_.fragmentOk) {
                    return false;
                }
                const ClusterCount minPiece = (*options_.fragmentOk + bytesPerCluster_ - 1) / bytesPerCluster_;
                const SpaceRequest request{.clusters = item.clusters, .own = item.own, .window = spanOf(region)};
                auto pieces = findPieces(pool_, request, minPiece);
                if (pieces) {
                    place(item, std::move(*pieces));
                }
                return pieces.has_value();
            }

            // The region from its first to one past its last cluster (the slice holds nothing else).
            static ClusterRun spanOf(std::span<const ClusterRun> region) {
                return region.empty() ? ClusterRun{}
                                      : ClusterRun{region.front().start, region.back().end() - region.front().start};
            }

            // Records a destination and takes it from the pool.
            void place(const Item& item, std::vector<ClusterRun> runs) {
                takeRuns(pool_, runs);
                guard_.record(item.file, runs);
                out_.placed.push_back({.file = item.file,
                                       .runs = std::move(runs),
                                       .reason = reasonFor(item.runs, options_.home),
                                       .zone = options_.zone});
            }

            HoleSet& pool_; // Space free after the plan.
            const FillOptions& options_; // How to fill.
            SizeIndex index_; // Files waiting.
            CycleGuard own_; // The fill's own guard when the options bring none.
            CycleGuard& guard_; // Keeps destinations from closing cycles.
            std::uint64_t bytesPerCluster_ = 1; // Cluster size, for fragmentOk in clusters.
            FillResult out_; // Result.
        };

    } // namespace

    void forChunks(Executor* executor, std::size_t count, const Executor::RangeBody& body) {
        InlineExecutor inline_;
        (executor != nullptr ? *executor : inline_).parallelFor({.count = count, .grain = 4096}, body);
    }

    MoveReason reasonFor(std::span<const ClusterRun> runs, ClusterRun home) {
        const bool inside = home.length > 0 && std::ranges::all_of(runs, [&](const ClusterRun& r) {
                                return r.start >= home.start && r.end() <= home.end();
                            });
        if (!inside) {
            return MoveReason::Relocate;
        }
        return fragmentsOf(runs) > 1 ? MoveReason::Defragment : MoveReason::Compact;
    }

    FillResult fastFill(const Snapshot& snapshot, std::span<const ClusterRun> region, std::span<const FileIndex> files,
                        HoleSet& pool, const FillOptions& options) {
        // The region's part of the pool as a slice of its own, so a search over the region's span never lands between
        // its pieces; what is left goes back.
        std::vector<ClusterRun> parts;
        for (const ClusterRun& piece : region) {
            const auto inside = pool.within(piece);
            parts.insert(parts.end(), inside.begin(), inside.end());
        }
        takeRuns(pool, parts);
        HoleSet slice(parts);
        Filler filler(snapshot, files, slice, options);
        FillResult result = filler.run(region);
        releaseRuns(pool, slice.runs());
        if (options.executor != nullptr) { // Another worker frees the items while placement goes on.
            options.executor->submit([index = filler.takeIndex()] { (void)index; });
        }
        return result;
    }

} // namespace icd::core::planner
