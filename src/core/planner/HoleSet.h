#pragma once

#include "core/snapshot/Types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::planner {

    // A changing set of free runs (holes): sorted, disjoint and merged, with "first hole of at least N clusters"
    // queries in both directions. The planner allocates destinations from it and the validator replays plans on it.
    // Holes are kept in blocks of a few hundred runs with a max-length tree over the blocks, so a 30 TB volume's ~17 M
    // free runs take 16 bytes each and every operation is logarithmic plus a short block scan.
    class HoleSet {
    public:
        // No holes.
        HoleSet() = default;

        // Holes from runs in any order; overlapping and touching runs are merged.
        explicit HoleSet(std::span<const ClusterRun> runs);

        // Free clusters in total.
        [[nodiscard]] ClusterCount total() const { return total_; }

        // Number of holes.
        [[nodiscard]] std::size_t size() const { return count_; }

        // The hole containing lcn, or nullopt.
        [[nodiscard]] std::optional<ClusterRun> holeAt(Lcn lcn) const;

        // Whether run lies entirely inside one hole (an empty run always does).
        [[nodiscard]] bool contains(ClusterRun run) const;

        // Removes run from the holes; false (and no change) when it is not entirely free.
        bool take(ClusterRun run);

        // Adds run as free space; false (and no change) when any of it is free already.
        bool add(ClusterRun run);

        // The first hole, clipped to window, with at least `clusters` clusters, scanning upwards from window.start.
        [[nodiscard]] std::optional<ClusterRun> nextFit(ClusterCount clusters, ClusterRun window) const;

        // The last hole, clipped to window, with at least `clusters` clusters, scanning downwards from window.end().
        [[nodiscard]] std::optional<ClusterRun> prevFit(ClusterCount clusters, ClusterRun window) const;

        // The holes overlapping window, clipped to it, in LCN order.
        [[nodiscard]] std::vector<ClusterRun> within(ClusterRun window) const;

        // Every hole in LCN order.
        [[nodiscard]] std::vector<ClusterRun> runs() const;


    private:
        // A short sorted run of holes.
        struct Block {
            std::vector<ClusterRun> runs; // Sorted, disjoint, merged with neighbours inside and across blocks.
            ClusterCount longest = 0; // Longest run in the block.
            // At least the end of every run the block holds or held, and at most the start of the next block's runs:
            // blocks stay searchable by LCN while empty (emptied blocks are only dropped in bulk).
            Lcn end = 0;
        };

        // Position of a hole: block and index inside it.
        struct Spot {
            std::size_t block = 0; // Block index.
            std::size_t index = 0; // Run index in the block.
        };

        // The first hole ending after lcn (at or after the hole holding lcn), or end() when there is none.
        [[nodiscard]] Spot locate(Lcn lcn) const;

        // The spot after the last hole.
        [[nodiscard]] Spot end() const { return {blocks_.size(), 0}; }

        // The spot after s, skipping to the next block that holds runs.
        [[nodiscard]] Spot next(Spot s) const;

        // s itself, or the first hole after it when s is past its block's runs (empty blocks are skipped).
        [[nodiscard]] Spot settle(Spot s) const;

        // The hole at a spot.
        [[nodiscard]] const ClusterRun& at(Spot s) const { return blocks_[s.block].runs[s.index]; }

        // The spot before s: the previous hole in its block, else the last hole of the nearest earlier block whose
        // longest run is at least `clusters` (0: any block); nullopt at the start.
        [[nodiscard]] std::optional<Spot> before(Spot s, ClusterCount clusters) const;

        // Grows the hole at prev by run, and by the hole at next when run closes the gap to it.
        void joinPrevious(Spot prev, ClusterRun run, std::optional<Spot> next);

        // Recomputes a block's longest run and its tree leaf.
        void refresh(std::size_t block);

        // Splits oversized blocks, drops empty ones and rebuilds the tree (O(blocks), rare).
        void restructure();

        // Raises a block's end to at least `end`, and the ends of the empty blocks right after it, so block ends never
        // decrease along the vector.
        void raiseEnd(std::size_t block, Lcn end);

        // A block of sorted runs with its longest run and end.
        [[nodiscard]] static Block blockOf(std::vector<ClusterRun> runs);

        // Rebuilds the max-length tree over all blocks.
        void rebuildTree();

        // First block at or after `from` whose longest run is at least `clusters`, or blocks_.size().
        [[nodiscard]] std::size_t firstBlockAtLeast(std::size_t from, ClusterCount clusters) const;

        // Last block at or before `from` whose longest run is at least `clusters`, or npos.
        [[nodiscard]] std::size_t lastBlockAtLeast(std::size_t from, ClusterCount clusters) const;

        // Inserts a hole at a spot (before the run there), restructuring when the block grows too big.
        void insertAt(Spot s, ClusterRun run);

        // Removes the hole at a spot.
        void eraseAt(Spot s);

        std::vector<Block> blocks_; // In LCN order.
        std::vector<ClusterCount> tree_; // Max-length segment tree over blocks; leaves start at leaves_.
        std::size_t leaves_ = 0; // Leaf count (a power of two).
        ClusterCount total_ = 0; // Free clusters.
        std::size_t count_ = 0; // Holes.
        std::size_t emptyBlocks_ = 0; // Blocks without runs, dropped when they reach a quarter.
    };

} // namespace icd::core::planner
