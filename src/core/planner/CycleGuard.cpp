#include "core/planner/CycleGuard.h"

#include <algorithm>

namespace icd::core::planner {

    namespace {

        // "No node".
        constexpr std::uint32_t noNode = static_cast<std::uint32_t>(-1);

    } // namespace

    std::uint32_t CycleGuard::find(FileIndex file) const { return file < slot_.size() ? slot_[file] : noNode; }

    void CycleGuard::blockersOf(FileIndex file, std::span<const ClusterRun> runs) {
        blockers_.clear();
        for (const ClusterRun& r : runs) {
            for (const ExtentRecord& e : snapshot_.extents.overlapping(r)) {
                if (e.file() != file) {
                    blockers_.push_back(e.file());
                }
            }
        }
        std::ranges::sort(blockers_);
        blockers_.erase(std::ranges::unique(blockers_).begin(), blockers_.end());
    }

    CycleGuard::Search CycleGuard::collect(const Query& q, std::vector<std::uint32_t>& out) {
        ++search_;
        out.clear();
        work_.assign(1, q.start);
        nodes_[q.start].seen = search_;
        const std::size_t allowed = std::max<std::size_t>(minLimit, std::min<std::uint64_t>(q.limit, credit_));
        Search result = Search::Done;
        while (!work_.empty() && result == Search::Done) {
            if (out.size() >= allowed) {
                result = Search::GaveUp;
                break;
            }
            const std::uint32_t v = work_.back();
            work_.pop_back();
            out.push_back(v);
            result = expand(v, q) ? Search::Found : Search::Done;
        }
        credit_ -= std::min<std::uint64_t>(credit_, out.size());
        return result;
    }

    bool CycleGuard::expand(std::uint32_t v, const Query& q) {
        for (const std::uint32_t w : q.forward ? nodes_[v].waiters : nodes_[v].waitsFor) {
            if (w == q.stop) {
                return true;
            }
            Node& n = nodes_[w];
            if (n.seen != search_ && n.order > q.low && n.order < q.high) {
                n.seen = search_;
                work_.push_back(w);
            }
        }
        return false;
    }

    bool CycleGuard::reaches(FileIndex file, std::span<const FileIndex> targets, std::size_t limit) {
        ++search_;
        const std::uint32_t start = find(file);
        if (start == noNode) {
            return false; // Nothing waits for the file yet.
        }
        const std::int64_t from = nodes_[start].order;
        std::int64_t high = from;
        for (const FileIndex t : targets) {
            const std::uint32_t n = find(t);
            high = n != noNode ? std::max(high, nodes_[n].order) : high;
        }
        const Query q{.start = start, .stop = noNode, .forward = true, .low = from, .high = high + 1, .limit = limit};
        if (high == from || collect(q, found_) == Search::GaveUp) {
            return false; // Every target comes before the file (none can wait for it), or unknown: let it through.
        }
        return std::ranges::any_of(targets, [&](FileIndex t) { return visited(t); });
    }

    bool CycleGuard::visited(FileIndex file) const {
        const std::uint32_t n = find(file);
        return n != noNode && nodes_[n].seen == search_;
    }

    std::uint32_t CycleGuard::nodeOf(FileIndex file, bool front) {
        if (slot_.empty()) {
            slot_.assign(snapshot_.files.size(), noNode);
        }
        std::uint32_t& slot = slot_[file];
        if (slot == noNode) {
            slot = static_cast<std::uint32_t>(nodes_.size());
            nodes_.push_back({.order = front ? front_-- : back_++});
        }
        return slot;
    }

    bool CycleGuard::addWait(std::uint32_t owner, std::uint32_t waiter) {
        const std::int64_t low = nodes_[waiter].order;
        const std::int64_t high = nodes_[owner].order;
        if (high > low && !repairOrder(owner, waiter, low, high)) {
            return false;
        }
        nodes_[owner].waiters.push_back(waiter);
        nodes_[waiter].waitsFor.push_back(owner);
        return true;
    }

    bool CycleGuard::repairOrder(std::uint32_t owner, std::uint32_t waiter, std::int64_t low, std::int64_t high) {
        if (collect({.start = waiter, .stop = owner, .forward = true, .low = low, .high = high}, found_) !=
            Search::Done) {
            return false; // The owner already waits for the waiter, or the order cannot be repaired cheaply.
        }
        if (collect({.start = owner, .stop = noNode, .forward = false, .low = low, .high = high}, behind_) !=
            Search::Done) {
            return false;
        }
        const auto byOrder = [&](std::uint32_t a, std::uint32_t b) { return nodes_[a].order < nodes_[b].order; };
        std::ranges::sort(behind_, byOrder);
        std::ranges::sort(found_, byOrder);
        behind_.insert(behind_.end(), found_.begin(), found_.end());
        orders_.clear();
        for (const std::uint32_t n : behind_) {
            orders_.push_back(nodes_[n].order);
        }
        std::ranges::sort(orders_);
        for (std::size_t i = 0; i < behind_.size(); ++i) {
            nodes_[behind_[i]].order = orders_[i];
        }
        return true;
    }

    bool CycleGuard::closesCycle(FileIndex file, std::span<const ClusterRun> runs, std::size_t limit) {
        blockersOf(file, runs);
        return !blockers_.empty() && reaches(file, blockers_, limit);
    }

    std::optional<Lcn> CycleGuard::cycleEnd(FileIndex file, ClusterRun run) {
        blockersOf(file, std::span(&run, 1));
        if (blockers_.empty() || !reaches(file, blockers_, defaultLimit)) {
            return std::nullopt;
        }
        for (const ExtentRecord& e : snapshot_.extents.overlapping(run)) {
            if (e.file() != file && visited(e.file())) {
                return std::min(e.run().end(), run.end());
            }
        }
        return std::nullopt;
    }

    void CycleGuard::record(FileIndex file, std::span<const ClusterRun> runs) {
        credit_ += perRecord;
        blockersOf(file, runs);
        if (blockers_.empty()) {
            return;
        }
        const std::uint32_t waiter = nodeOf(file, false);
        for (const FileIndex b : blockers_) {
            (void)addWait(nodeOf(b, true), waiter);
        }
    }

} // namespace icd::core::planner
