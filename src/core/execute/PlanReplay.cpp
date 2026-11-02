#include "core/execute/PlanReplay.h"

#include "core/planner/HoleSet.h"

#include <array>
#include <numeric>
#include <unordered_set>

namespace icd::core::execute {

    namespace {

        // What the replay knows about a file it has seen.
        enum class FileState : std::uint8_t {
            Kept, // Its last move was kept: it is (virtually) at that move's destinations.
            Dropped, // A move of it was dropped: its later moves cannot follow.
        };

        // Replays moves one at a time on a mutable copy of the free space.
        class Replayer {
        public:
            // A replay of `plan` from the free space and files as they are now.
            Replayer(const ExecutionPlan& plan, std::span<const ClusterRun> freeNow, const CurrentFiles& files) :
                plan_(plan), holes_(freeNow), files_(files) {}

            // Keeps or drops one move.
            void step(std::uint32_t index) {
                const PlannedMove& m = plan_.moves[index];
                const auto state = states_.find(m.fileReference);
                if (state != states_.end() && state->second == FileState::Dropped) {
                    drop(index, DropReason::EarlierMoveDropped);
                    return;
                }
                DropReason why = DropReason::FileChanged;
                const auto pieces = state == states_.end() ? firstPieces(m, why) : followingPieces(m);
                if (!pieces) {
                    drop(index, why);
                    return;
                }
                if (!fits(*pieces)) {
                    drop(index, DropReason::DestinationInUse);
                    return;
                }
                apply(*pieces);
                keep(index, *pieces);
            }

            // The result.
            [[nodiscard]] ReplayResult finish() && { return std::move(result_); }

        private:
            // Pieces of a file's first move: from where the file is now.
            std::optional<std::vector<MovePiece>> firstPieces(const PlannedMove& m, DropReason& why) const {
                const auto now = files_.runsOf(m.fileReference);
                if (!now) {
                    why = DropReason::FileGone;
                    return std::nullopt;
                }
                auto pieces = matchMove(*now, {plan_.sources(m), plan_.destinations(m)});
                if (!pieces && m.fromParking) {
                    pieces = remapMove(*now, plan_.destinations(m));
                }
                return pieces;
            }

            // Pieces of a later move: the file is where its previous kept move put it, which is these sources.
            std::optional<std::vector<MovePiece>> followingPieces(const PlannedMove& m) const {
                const auto from = plan_.sources(m);
                return matchMove(asStreamRuns(from), {from, plan_.destinations(m)});
            }

            // Whether every destination still to fill is free now.
            [[nodiscard]] bool fits(std::span<const MovePiece> pieces) const {
                return std::ranges::all_of(
                    pieces, [&](const MovePiece& p) { return p.done || holes_.contains({p.to, p.length}); });
            }

            // Fills the destinations and frees the sources of the pieces still to move.
            void apply(std::span<const MovePiece> pieces) {
                for (const MovePiece& p : pieces) {
                    if (!p.done) {
                        holes_.take({p.to, p.length});
                    }
                }
                for (const MovePiece& p : pieces) {
                    if (!p.done) {
                        holes_.add({p.from, p.length});
                    }
                }
            }

            // Records a kept move.
            void keep(std::uint32_t index, std::span<const MovePiece> pieces) {
                const ClusterCount pending = pendingClusters(pieces);
                result_.kept.push_back(index);
                result_.pendingClusters += pending;
                result_.alreadyDone += pending == 0 ? 1 : 0;
                result_.partlyDone += pending != 0 && !std::ranges::none_of(pieces, &MovePiece::done) ? 1 : 0;
                states_[plan_.moves[index].fileReference] = FileState::Kept;
            }

            // Records a dropped move.
            void drop(std::uint32_t index, DropReason reason) {
                result_.dropped.push_back({index, reason});
                result_.droppedClusters += plan_.moves[index].clusters;
                states_[plan_.moves[index].fileReference] = FileState::Dropped;
            }

            const ExecutionPlan& plan_; // Replayed.
            planner::HoleSet holes_; // Free space at the current point of the replay.
            const CurrentFiles& files_; // Files now.
            std::unordered_map<std::uint64_t, FileState> states_; // Files seen so far.
            ReplayResult result_; // Output.
        };

    } // namespace

    std::string_view dropKey(DropReason reason) {
        constexpr std::array<std::string_view, 4> keys{"file-gone", "file-changed", "destination-in-use",
                                                       "earlier-move-dropped"};
        return keys.at(static_cast<std::size_t>(reason));
    }

    SnapshotFiles::SnapshotFiles(const Snapshot& snapshot, const ExecutionPlan& plan) : snapshot_(snapshot) {
        std::unordered_set<std::uint64_t> wanted;
        for (const PlannedMove& m : plan.moves) {
            wanted.insert(m.fileReference);
        }
        for (FileIndex f = 0; f < snapshot.files.size(); ++f) {
            if (wanted.contains(snapshot.files.fileReference(f))) {
                index_.emplace(snapshot.files.fileReference(f), f);
            }
        }
    }

    std::optional<FileIndex> SnapshotFiles::indexOf(std::uint64_t fileReference) const {
        const auto found = index_.find(fileReference);
        return found == index_.end() ? std::nullopt : std::optional(found->second);
    }

    std::optional<std::vector<StreamRun>> SnapshotFiles::runsOf(std::uint64_t fileReference) const {
        const auto file = indexOf(fileReference);
        if (!file) {
            return std::nullopt;
        }
        std::vector<StreamRun> out;
        const auto records = snapshot_.extents.records();
        std::uint64_t vcn = 0;
        for (const std::uint32_t position : snapshot_.extents.extentsOf(*file)) {
            const ExtentRecord& r = records[position];
            if ((r.flags() & ExtentUnmovable) != 0) {
                continue;
            }
            vcn = !out.empty() && out.back().stream == r.stream() ? vcn : 0;
            out.push_back({r.stream(), vcn, r.lcn(), r.length()});
            vcn += r.length();
        }
        return out;
    }

    ReplayResult replayPlan(const ExecutionPlan& plan, std::span<const std::uint32_t> order,
                            std::span<const ClusterRun> freeNow, const CurrentFiles& files) {
        Replayer replayer(plan, freeNow, files);
        for (const std::uint32_t index : order) {
            replayer.step(index);
        }
        return std::move(replayer).finish();
    }

    std::vector<std::uint32_t> allMoves(const ExecutionPlan& plan) {
        std::vector<std::uint32_t> out(plan.moves.size());
        std::iota(out.begin(), out.end(), 0U);
        return out;
    }

} // namespace icd::core::execute
