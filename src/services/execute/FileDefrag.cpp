#include "services/execute/FileDefrag.h"

#include "core/execute/ExecutionPlan.h"
#include "core/planner/DefragOnly.h"
#include "core/planner/HoleSet.h"
#include "core/planner/Placement.h"
#include "core/planner/Validator.h"

#include <format>
#include <stdexcept>
#include <vector>

namespace icd::services {

    namespace {

        // Strategy id the execution log records for a single-file run.
        constexpr const char* fileDefragStrategy = "single-file";

        // Free space for the move: the snapshot's free runs without the reserved ones.
        core::planner::HoleSet freePool(const core::Snapshot& snapshot, std::span<const core::ClusterRun> reserved) {
            const auto free = snapshot.freeSpace.runs();
            core::planner::HoleSet pool(std::vector<core::ClusterRun>(free.begin(), free.end()));
            for (const core::ClusterRun& r : reserved) {
                core::planner::takeRuns(pool, pool.within(r));
            }
            return pool;
        }

        // One attempt: the file whole (pieces nullopt) or in pieces of at least `pieces` bytes.
        core::planner::Plan attempt(const core::Snapshot& snapshot, core::FileIndex file,
                                    std::span<const core::ClusterRun> reserved, std::optional<std::uint64_t> pieces) {
            core::planner::Plan plan;
            plan.effort = core::strategy::Effort::Maximum;
            plan.lowerBound = core::planner::movableClusters(snapshot, file); // The file must move.
            core::planner::HoleSet pool = freePool(snapshot, reserved);
            const std::array files{file};
            core::planner::planDefragment(snapshot, files, pool, {.fragmentOk = pieces, .reuseVacated = false}, plan);
            return plan;
        }

        // The plan whole, else in large pieces when that leaves fewer fragments; nullopt when neither works.
        std::optional<core::planner::Plan> bestPlan(const core::Snapshot& snapshot, core::FileIndex file,
                                                    std::span<const core::ClusterRun> reserved) {
            core::planner::Plan whole = attempt(snapshot, file, reserved, std::nullopt);
            if (!whole.moves.empty()) {
                return whole;
            }
            core::planner::Plan pieces = attempt(snapshot, file, reserved, fileDefragPieceBytes);
            const std::uint64_t now = core::planner::movableFragments(snapshot, file);
            if (pieces.moves.empty() || pieces.moves.front().destinationCount >= now) {
                return std::nullopt;
            }
            return pieces;
        }

    } // namespace

    std::optional<std::string> fileDefragRefusal(const core::Snapshot& snapshot, core::FileIndex file) {
        if (file >= snapshot.files.size()) {
            return "the file is not on the shown map";
        }
        if (snapshot.files.has(file, core::FileMetadata)) {
            return "NTFS metadata files are never moved";
        }
        if (core::planner::movableFragments(snapshot, file) < 2) {
            return "the file is not fragmented";
        }
        return std::nullopt;
    }

    FileDefragPlan planFileDefrag(const core::Snapshot& snapshot, core::FileIndex file,
                                  std::span<const core::ClusterRun> reserved) {
        FileDefragPlan out;
        if (auto why = fileDefragRefusal(snapshot, file); why) {
            out.refusal = std::move(why);
            return out;
        }
        std::optional<core::planner::Plan> plan = bestPlan(snapshot, file, reserved);
        if (!plan) {
            out.refusal = "no free space can hold the file in fewer pieces than now";
            return out;
        }
        core::planner::finishTotals(*plan, snapshot);
        const core::planner::Validation validation = core::planner::validatePlan(snapshot, *plan);
        if (!validation.ok()) {
            out.refusal = "the plan failed validation: " + validation.issues.front().message;
            return out;
        }
        out.result.plan = std::move(*plan);
        return out;
    }

    JobFunction makeFileDefragJob(FileDefragRequest request, std::shared_ptr<ExecutionFeed> feed,
                                  std::shared_ptr<core::PauseGate> pause) {
        return [r = std::move(request), feed = std::move(feed), pause = std::move(pause)](JobContext& context) {
            context.progress.report({.stage = "Planning the file", .done = 0, .total = 0, .message = {}});
            std::vector<core::ClusterRun> reserved;
            if (const auto& ntfs = r.drive.layout.ntfs; ntfs && ntfs->mftZoneEnd > ntfs->mftZoneStart) {
                reserved.push_back({ntfs->mftZoneStart, ntfs->mftZoneEnd - ntfs->mftZoneStart});
            }
            const FileDefragPlan planned = planFileDefrag(*r.snapshot, r.file, reserved);
            if (planned.refusal) {
                throw std::runtime_error(*planned.refusal);
            }
            core::execute::PlanIdentity id;
            id.volumeName = r.drive.layout.volumeName;
            id.volumeSerial = r.drive.layout.ntfs ? r.drive.layout.ntfs->serialNumber : 0;
            id.geometry = r.snapshot->geometry;
            id.usn = r.snapshot->usn;
            id.madeAt = r.now;
            id.strategy = fileDefragStrategy;
            id.effort = core::strategy::Effort::Maximum;
            runGuiExecution(r.drive,
                            core::execute::executionPlanOf(planned.result, *r.snapshot, std::move(id), reserved),
                            r.options, *feed, pause.get(), context);
        };
    }

} // namespace icd::services
