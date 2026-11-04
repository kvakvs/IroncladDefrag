#pragma once

#include "core/job/PauseGate.h"
#include "core/model/DriveInfo.h"
#include "core/planner/LevelPlanner.h"
#include "core/snapshot/Snapshot.h"
#include "services/execute/ExecuteJob.h"
#include "services/job/JobRunner.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace icd::services {

    // Smallest piece a file may be split into when no free run holds it whole (T128): fewer, large fragments still
    // read nearly as fast as one, and the move is only planned when it leaves fewer fragments than now.
    inline constexpr std::uint64_t fileDefragPieceBytes = std::uint64_t{64} << 20U;

    // Why a file cannot be defragmented on its own, in words; nullopt when it can be tried: NTFS metadata never
    // moves, and a file whose movable data is in one piece (or none) has nothing to gain.
    [[nodiscard]] std::optional<std::string> fileDefragRefusal(const core::Snapshot& snapshot, core::FileIndex file);

    // A plan that defragments one file, or why there is none.
    struct FileDefragPlan {
        core::planner::PlanResult result; // One move (no layout, no zones); empty with a refusal.
        std::optional<std::string> refusal; // Why the file is left as it is.
    };

    // Plans one file (T128): its movable clusters into the free run nearest its first cluster that holds them whole,
    // else into free pieces of at least fileDefragPieceBytes when that leaves fewer fragments; never into `reserved`
    // (the MFT zone). The plan is checked by the validator like every level's (plan.md 9.5).
    [[nodiscard]] FileDefragPlan planFileDefrag(const core::Snapshot& snapshot, core::FileIndex file,
                                                std::span<const core::ClusterRun> reserved);

    // What Defragment this file runs.
    struct FileDefragRequest {
        core::DriveInfo drive; // The volume.
        std::shared_ptr<const core::Snapshot> snapshot; // The shown map's snapshot (the analysis or the live map).
        core::FileIndex file = core::noFile; // The file.
        std::int64_t now = 0; // FILETIME of the request, for the plan's identity.
        ExecuteOptions options; // Priority and throughput limit.
    };

    // A job that plans one file and runs the move like a planned level (revalidation against the volume now, log,
    // pause, cancel, snapshot after), reporting into `feed`; fails with the reason when there is nothing to do, no
    // room, or the volume refuses the plan. Needs administrator rights.
    [[nodiscard]] JobFunction makeFileDefragJob(FileDefragRequest request, std::shared_ptr<ExecutionFeed> feed,
                                                std::shared_ptr<core::PauseGate> pause);

} // namespace icd::services
