#include "core/planner/HoleSet.h"

#include <algorithm>
#include <bit>

namespace icd::core::planner {

    namespace {

        // Runs per block when blocks are built or split.
        constexpr std::size_t blockRuns = 256;

        // A block is split when it grows past this.
        constexpr std::size_t maxBlockRuns = 4 * blockRuns;

        // "Not found" for block searches.
        constexpr std::size_t npos = static_cast<std::size_t>(-1);

        // The part of run inside window (length 0 when they do not overlap).
        ClusterRun clipRun(ClusterRun run, ClusterRun window) {
            const Lcn start = std::max(run.start, window.start);
            const Lcn end = std::min(run.end(), window.end());
            return end > start ? ClusterRun{start, end - start} : ClusterRun{start, 0};
        }

        // Sorts and merges runs; empty runs are dropped.
        std::vector<ClusterRun> merged(std::span<const ClusterRun> runs) {
            std::vector<ClusterRun> sorted(runs.begin(), runs.end());
            std::erase_if(sorted, [](const ClusterRun& r) { return r.length == 0; });
            std::ranges::sort(sorted, {}, &ClusterRun::start);
            std::vector<ClusterRun> out;
            for (const ClusterRun& r : sorted) {
                if (!out.empty() && r.start <= out.back().end()) {
                    out.back().length = std::max(out.back().end(), r.end()) - out.back().start;
                }
                else {
                    out.push_back(r);
                }
            }
            return out;
        }

        // Leftmost leaf in [from, ...) under node whose value is at least `clusters`.
        std::size_t firstAtLeast(const std::vector<ClusterCount>& tree, ClusterRun nodeRange, std::size_t node,
                                 std::size_t from, ClusterCount clusters) {
            if (nodeRange.end() <= from || tree[node] < clusters) {
                return npos;
            }
            if (nodeRange.length == 1) {
                return nodeRange.start;
            }
            const ClusterCount half = nodeRange.length / 2;
            const std::size_t left = firstAtLeast(tree, {nodeRange.start, half}, 2 * node, from, clusters);
            return left != npos ? left
                                : firstAtLeast(tree, {nodeRange.start + half, half}, 2 * node + 1, from, clusters);
        }

        // Rightmost leaf in [..., upto] under node whose value is at least `clusters`.
        std::size_t lastAtLeast(const std::vector<ClusterCount>& tree, ClusterRun nodeRange, std::size_t node,
                                std::size_t upto, ClusterCount clusters) {
            if (nodeRange.start > upto || tree[node] < clusters) {
                return npos;
            }
            if (nodeRange.length == 1) {
                return nodeRange.start;
            }
            const ClusterCount half = nodeRange.length / 2;
            const std::size_t right = lastAtLeast(tree, {nodeRange.start + half, half}, 2 * node + 1, upto, clusters);
            return right != npos ? right : lastAtLeast(tree, {nodeRange.start, half}, 2 * node, upto, clusters);
        }

    } // namespace

    HoleSet::HoleSet(std::span<const ClusterRun> runs) {
        const std::vector<ClusterRun> all = merged(runs);
        for (std::size_t i = 0; i < all.size(); i += blockRuns) {
            blocks_.push_back(
                blockOf({all.begin() + static_cast<std::ptrdiff_t>(i),
                         all.begin() + static_cast<std::ptrdiff_t>(std::min(i + blockRuns, all.size()))}));
        }
        for (const ClusterRun& r : all) {
            total_ += r.length;
        }
        count_ = all.size();
        restructure();
    }

    HoleSet::Spot HoleSet::locate(Lcn lcn) const {
        auto block = std::ranges::partition_point(blocks_, [&](const Block& b) { return b.end <= lcn; });
        // Later blocks may hold runs below an emptied block's end (inserted after it emptied): search each in turn.
        for (; block != blocks_.end(); ++block) {
            const auto run =
                std::ranges::partition_point(block->runs, [&](const ClusterRun& r) { return r.end() <= lcn; });
            if (run != block->runs.end()) {
                return {static_cast<std::size_t>(block - blocks_.begin()),
                        static_cast<std::size_t>(run - block->runs.begin())};
            }
        }
        return end();
    }

    HoleSet::Spot HoleSet::settle(Spot s) const {
        while (s.block < blocks_.size() && s.index >= blocks_[s.block].runs.size()) {
            s = {s.block + 1, 0};
        }
        return s;
    }

    HoleSet::Spot HoleSet::next(Spot s) const { return settle({s.block, s.index + 1}); }

    std::optional<ClusterRun> HoleSet::holeAt(Lcn lcn) const {
        const Spot s = locate(lcn);
        if (s.block == blocks_.size() || !at(s).contains(lcn)) {
            return std::nullopt;
        }
        return at(s);
    }

    bool HoleSet::contains(ClusterRun run) const {
        if (run.length == 0) {
            return true;
        }
        const auto hole = holeAt(run.start);
        return hole && run.end() <= hole->end();
    }

    bool HoleSet::take(ClusterRun run) {
        if (run.length == 0) {
            return true;
        }
        if (!contains(run)) {
            return false;
        }
        const Spot s = locate(run.start);
        const ClusterRun hole = at(s);
        const ClusterRun left{hole.start, run.start - hole.start};
        const ClusterRun right{run.end(), hole.end() - run.end()};
        total_ -= run.length;
        if (left.length == 0 && right.length == 0) {
            eraseAt(s);
            return true;
        }
        blocks_[s.block].runs[s.index] = left.length != 0 ? left : right;
        if (left.length != 0 && right.length != 0) {
            insertAt({s.block, s.index + 1}, right);
        }
        else {
            refresh(s.block);
        }
        return true;
    }

    bool HoleSet::add(ClusterRun run) {
        if (run.length == 0) {
            return true;
        }
        const Spot s = locate(run.start);
        const bool hasNext = s.block < blocks_.size();
        if (hasNext && at(s).start < run.end()) {
            return false; // Overlaps a hole.
        }
        const std::optional<Spot> prev = before(s, 0);
        total_ += run.length;
        const bool joinNext = hasNext && at(s).start == run.end();
        if (prev && at(*prev).end() == run.start) {
            joinPrevious(*prev, run, joinNext ? std::optional(s) : std::nullopt);
        }
        else if (joinNext) {
            ClusterRun& n = blocks_[s.block].runs[s.index];
            n = {run.start, run.length + n.length};
            refresh(s.block);
        }
        else {
            insertAt(s, run);
        }
        return true;
    }

    void HoleSet::joinPrevious(Spot prev, ClusterRun run, std::optional<Spot> next) {
        ClusterRun& p = blocks_[prev.block].runs[prev.index];
        p.length += run.length + (next ? at(*next).length : 0);
        raiseEnd(prev.block, p.end());
        refresh(prev.block);
        if (next) {
            eraseAt(*next);
        }
    }

    std::optional<ClusterRun> HoleSet::nextFit(ClusterCount clusters, ClusterRun window) const {
        clusters = std::max<ClusterCount>(clusters, 1);
        Spot s = locate(window.start);
        while (s.block < blocks_.size()) {
            if (blocks_[s.block].longest < clusters) {
                const std::size_t b = firstBlockAtLeast(s.block + 1, clusters);
                s = {b, 0};
                continue;
            }
            const ClusterRun& hole = at(s);
            if (hole.start >= window.end()) {
                return std::nullopt;
            }
            const ClusterRun clipped = clipRun(hole, window);
            if (clipped.length >= clusters) {
                return clipped;
            }
            s = next(s);
        }
        return std::nullopt;
    }

    std::optional<HoleSet::Spot> HoleSet::before(Spot s, ClusterCount clusters) const {
        if (s.block < blocks_.size() && s.index > 0) {
            return Spot{s.block, s.index - 1};
        }
        const std::size_t b = s.block == 0 ? npos : lastBlockAtLeast(s.block - 1, std::max<ClusterCount>(clusters, 1));
        return b == npos ? std::nullopt : std::optional(Spot{b, blocks_[b].runs.size() - 1});
    }

    std::optional<ClusterRun> HoleSet::prevFit(ClusterCount clusters, ClusterRun window) const {
        clusters = std::max<ClusterCount>(clusters, 1);
        if (window.length == 0 || blocks_.empty()) {
            return std::nullopt;
        }
        // The last hole starting before the window's end.
        const Spot first = locate(window.end() - 1);
        std::optional<Spot> s =
            first.block < blocks_.size() && at(first).start < window.end() ? first : before(first, 0);
        while (s && at(*s).end() > window.start) {
            if (blocks_[s->block].longest < clusters) {
                s = before({s->block, 0}, clusters);
                continue;
            }
            const ClusterRun clipped = clipRun(at(*s), window);
            if (clipped.length >= clusters) {
                return clipped;
            }
            s = before(*s, clusters);
        }
        return std::nullopt;
    }

    std::vector<ClusterRun> HoleSet::within(ClusterRun window) const {
        std::vector<ClusterRun> out;
        for (Spot s = locate(window.start); s.block < blocks_.size() && at(s).start < window.end(); s = next(s)) {
            out.push_back(clipRun(at(s), window));
        }
        return out;
    }

    std::vector<ClusterRun> HoleSet::runs() const {
        std::vector<ClusterRun> out;
        out.reserve(count_);
        for (const Block& b : blocks_) {
            out.insert(out.end(), b.runs.begin(), b.runs.end());
        }
        return out;
    }

    void HoleSet::refresh(std::size_t block) {
        Block& b = blocks_[block];
        b.longest = 0;
        for (const ClusterRun& r : b.runs) {
            b.longest = std::max(b.longest, r.length);
        }
        std::size_t node = leaves_ + block;
        tree_[node] = b.longest;
        for (node /= 2; node > 0; node /= 2) {
            tree_[node] = std::max(tree_[2 * node], tree_[2 * node + 1]);
        }
    }

    void HoleSet::restructure() {
        std::vector<Block> out;
        out.reserve(blocks_.size());
        for (Block& b : blocks_) {
            if (b.runs.size() <= maxBlockRuns) {
                if (!b.runs.empty()) {
                    out.push_back(std::move(b));
                }
                continue;
            }
            for (std::size_t i = 0; i < b.runs.size(); i += 2 * blockRuns) {
                out.push_back(blockOf(
                    {b.runs.begin() + static_cast<std::ptrdiff_t>(i),
                     b.runs.begin() + static_cast<std::ptrdiff_t>(std::min(i + 2 * blockRuns, b.runs.size()))}));
            }
        }
        blocks_ = std::move(out);
        emptyBlocks_ = 0;
        rebuildTree();
    }

    void HoleSet::raiseEnd(std::size_t block, Lcn end) {
        // Empty blocks after it may keep an older, smaller end; block ends must not decrease along the vector.
        for (std::size_t b = block; b < blocks_.size() && blocks_[b].end < end; ++b) {
            if (b != block && !blocks_[b].runs.empty()) {
                break;
            }
            blocks_[b].end = end;
        }
    }

    HoleSet::Block HoleSet::blockOf(std::vector<ClusterRun> runs) {
        Block b{.runs = std::move(runs), .longest = 0, .end = 0};
        for (const ClusterRun& r : b.runs) {
            b.longest = std::max(b.longest, r.length);
        }
        b.end = b.runs.empty() ? 0 : b.runs.back().end();
        return b;
    }

    void HoleSet::rebuildTree() {
        leaves_ = std::bit_ceil(std::max<std::size_t>(blocks_.size(), 1));
        tree_.assign(2 * leaves_, 0);
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            tree_[leaves_ + i] = blocks_[i].longest;
        }
        for (std::size_t node = leaves_ - 1; node > 0; --node) {
            tree_[node] = std::max(tree_[2 * node], tree_[2 * node + 1]);
        }
    }

    std::size_t HoleSet::firstBlockAtLeast(std::size_t from, ClusterCount clusters) const {
        if (from >= blocks_.size()) {
            return blocks_.size();
        }
        const std::size_t found = firstAtLeast(tree_, {0, leaves_}, 1, from, clusters);
        return found == npos || found >= blocks_.size() ? blocks_.size() : found;
    }

    std::size_t HoleSet::lastBlockAtLeast(std::size_t from, ClusterCount clusters) const {
        return lastAtLeast(tree_, {0, leaves_}, 1, std::min(from, blocks_.size() - 1), clusters);
    }

    void HoleSet::insertAt(Spot s, ClusterRun run) {
        if (blocks_.empty()) {
            blocks_.push_back(blockOf({run}));
            ++count_;
            rebuildTree();
            return;
        }
        if (s.block == blocks_.size()) {
            s = {blocks_.size() - 1, blocks_.back().runs.size()};
        }
        Block& b = blocks_[s.block];
        if (b.runs.empty()) {
            --emptyBlocks_;
        }
        b.runs.insert(b.runs.begin() + static_cast<std::ptrdiff_t>(s.index), run);
        raiseEnd(s.block, run.end());
        ++count_;
        if (b.runs.size() <= maxBlockRuns) {
            refresh(s.block);
            return;
        }
        // Split this block in two; the tree is rebuilt from the blocks' stored maxima, without rescanning runs.
        std::vector<ClusterRun> tail(b.runs.begin() + static_cast<std::ptrdiff_t>(blockRuns * 2), b.runs.end());
        b.runs.resize(blockRuns * 2);
        b = blockOf(std::move(b.runs));
        blocks_.insert(blocks_.begin() + static_cast<std::ptrdiff_t>(s.block) + 1, blockOf(std::move(tail)));
        rebuildTree();
    }

    void HoleSet::eraseAt(Spot s) {
        auto& runs = blocks_[s.block].runs;
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(s.index));
        --count_;
        if (runs.empty()) {
            ++emptyBlocks_; // Kept (still searchable by its end) until a quarter of the blocks are empty.
        }
        if (emptyBlocks_ * 4 > blocks_.size()) {
            restructure();
            return;
        }
        refresh(s.block);
    }

} // namespace icd::core::planner
