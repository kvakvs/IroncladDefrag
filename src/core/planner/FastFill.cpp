#include "core/planner/FastFill.h"

#include <algorithm>

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
            // Sorts the items.
            explicit SizeIndex(std::vector<Item> items) : items_(std::move(items)), parent_(items_.size() + 1) {
                std::ranges::sort(items_, [](const Item& a, const Item& b) {
                    return a.clusters != b.clusters ? a.clusters < b.clusters : a.file < b.file;
                });
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
                pool_(pool), options_(options), index_(itemsOf(snapshot, files)),
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

        private:
            // The items of the files, with their runs.
            static std::vector<Item> itemsOf(const Snapshot& snapshot, std::span<const FileIndex> files) {
                std::vector<Item> items;
                items.reserve(files.size());
                for (const FileIndex file : files) {
                    std::vector<ClusterRun> runs = movableRuns(snapshot, file);
                    std::vector<ClusterRun> own = sortedRuns(runs);
                    items.push_back({clustersOf(runs), file, std::move(runs), std::move(own)});
                }
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

            // The largest item that fits what is left of a hole without overlapping its own runs, or 0.
            std::size_t pick(ClusterRun left) {
                std::size_t slot = index_.largestUpTo(left.length);
                for (std::size_t tries = 0; slot != 0 && tries < triesPerPosition; ++tries) {
                    const Item& item = index_.at(slot);
                    if (!overlaps(spotIn(left, item.clusters, options_.from), item.own)) {
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

            // Places a file in the first (or last) single run of the region that holds it.
            bool placeInOneRun(const Item& item, std::span<const ClusterRun> region) {
                const SpaceRequest request{.clusters = item.clusters, .own = item.own, .window = spanOf(region)};
                const auto run =
                    options_.from == PackFrom::Front ? findFirst(pool_, request) : findLast(pool_, request);
                if (run) {
                    place(item, {*run});
                }
                return run.has_value();
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
                out_.placed.push_back({.file = item.file,
                                       .runs = std::move(runs),
                                       .reason = reasonFor(item.runs, options_.home),
                                       .zone = options_.zone});
            }

            HoleSet& pool_; // Space free after the plan.
            const FillOptions& options_; // How to fill.
            SizeIndex index_; // Files waiting.
            std::uint64_t bytesPerCluster_ = 1; // Cluster size, for fragmentOk in clusters.
            FillResult out_; // Result.
        };

    } // namespace

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
        FillResult result = Filler(snapshot, files, slice, options).run(region);
        releaseRuns(pool, slice.runs());
        return result;
    }

} // namespace icd::core::planner
