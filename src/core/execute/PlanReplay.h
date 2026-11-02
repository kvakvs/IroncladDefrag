#pragma once

#include "core/execute/ExecutionPlan.h"
#include "core/execute/MovePieces.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace icd::core::execute {

    // Why revalidation takes a move out of a plan (plan.md 10.3).
    enum class DropReason : std::uint8_t {
        FileGone, // The file no longer exists (deleted, or its record reused by another file).
        FileChanged, // Its clusters are neither where the plan found them nor where it sends them.
        DestinationInUse, // Clusters it goes to are taken by something the plan did not move away.
        EarlierMoveDropped, // An earlier move of the same file was dropped, so this one cannot follow it.
    };

    // Stable name of a drop reason ("file-gone", ...).
    [[nodiscard]] std::string_view dropKey(DropReason reason);

    // A move revalidation took out.
    struct DroppedMove {
        std::uint32_t move = 0; // Index in the plan.
        DropReason reason = DropReason::FileChanged; // Why.
    };

    // What is left of a plan after replaying it on the volume as it is now: the moves that are still valid in order.
    struct ReplayResult {
        std::vector<std::uint32_t> kept; // Moves still valid, in plan order.
        std::vector<DroppedMove> dropped; // Moves taken out, in plan order.
        std::uint64_t alreadyDone = 0; // Kept moves whose clusters already lie at their destinations.
        std::uint64_t partlyDone = 0; // Kept moves with some clusters already there (cut short, then resumed).
        ClusterCount pendingClusters = 0; // Clusters the kept moves still have to move.
        ClusterCount droppedClusters = 0; // Clusters of the dropped moves.
    };

    // The runs of a plan's files now, by file reference (a snapshot, or the file system itself).
    class CurrentFiles {
    public:
        CurrentFiles() = default;
        CurrentFiles(const CurrentFiles&) = delete;
        CurrentFiles& operator=(const CurrentFiles&) = delete;
        CurrentFiles(CurrentFiles&&) = delete;
        CurrentFiles& operator=(CurrentFiles&&) = delete;
        virtual ~CurrentFiles() = default;

        // The file's movable runs in (stream, VCN) order, or nullopt when there is no such file.
        [[nodiscard]] virtual std::optional<std::vector<StreamRun>> runsOf(std::uint64_t fileReference) const = 0;
    };

    // A plan's files looked up in a snapshot taken after planning (a catch-up or a rescan): only the plan's files are
    // indexed, so a 20 M-file snapshot costs one pass, not a map of every file.
    class SnapshotFiles final : public CurrentFiles {
    public:
        // Indexes the plan's files in the snapshot; both must outlive this object.
        SnapshotFiles(const Snapshot& snapshot, const ExecutionPlan& plan);

        // See CurrentFiles: the snapshot's movable extents of the file, VCNs counted along per stream.
        [[nodiscard]] std::optional<std::vector<StreamRun>> runsOf(std::uint64_t fileReference) const override;

        // The file's index in the snapshot, if it is there.
        [[nodiscard]] std::optional<FileIndex> indexOf(std::uint64_t fileReference) const;

    private:
        const Snapshot& snapshot_; // Looked up.
        std::unordered_map<std::uint64_t, FileIndex> index_; // Plan file -> snapshot file.
    };

    // Replays the moves `order` names (plan indexes, increasing) on the free space `freeNow` and the files as they are
    // now (plan.md 10.3). A file's first move must find it at its sources (or part-way to its destinations); the return
    // of a parked file may find it anywhere with the right size. Every destination cluster still to fill must be free
    // at that point of the replay; a kept move frees its sources for the moves after it, a dropped one keeps them, so
    // moves that waited on a dropped one are dropped too. The kept moves are valid one after another from now.
    [[nodiscard]] ReplayResult replayPlan(const ExecutionPlan& plan, std::span<const std::uint32_t> order,
                                          std::span<const ClusterRun> freeNow, const CurrentFiles& files);

    // Every move index of a plan, in order.
    [[nodiscard]] std::vector<std::uint32_t> allMoves(const ExecutionPlan& plan);

} // namespace icd::core::execute
