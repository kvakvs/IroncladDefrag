#include "services/execute/LiveMap.h"

#include "core/execute/MovePieces.h"
#include "core/execute/PlanReplay.h"
#include "core/map/PlanColorMode.h"
#include "services/execute/ExecutedSnapshot.h"

#include <algorithm>
#include <unordered_map>

namespace icd::services {

    namespace {

        // The records that can be applied: those that moved clusters and were read back afterwards.
        std::vector<MoveRecord> applicable(std::span<const MoveRecord> records) {
            std::vector<MoveRecord> out;
            for (const MoveRecord& r : records) {
                if (r.clustersMoved != 0 && !r.runsAfter.empty()) {
                    out.push_back(r);
                }
            }
            return out;
        }

        // The clusters the records change: every extent the moved files have in the snapshot, and their runs now.
        std::vector<core::ClusterRun> dirtyRuns(const core::Snapshot& snapshot,
                                                const core::execute::ExecutionPlan& plan,
                                                std::span<const MoveRecord> records) {
            const core::execute::SnapshotFiles files(snapshot, plan);
            const auto extents = snapshot.extents.records();
            std::vector<core::ClusterRun> out;
            for (const MoveRecord& r : records) {
                if (const auto file = files.indexOf(plan.moves[r.move].fileReference); file) {
                    for (const std::uint32_t position : snapshot.extents.extentsOf(*file)) {
                        out.push_back(extents[position].run());
                    }
                }
                const auto now = core::execute::lcnRunsOf(r.runsAfter);
                out.insert(out.end(), now.begin(), now.end());
            }
            return core::mergedRuns(std::move(out));
        }

        // Whether two snapshots lay out the same files on the same clusters (then the map of one fits the other).
        bool sameLayout(const core::Snapshot& a, const core::Snapshot& b) {
            return a.files.size() == b.files.size() && std::ranges::equal(a.extents.records(), b.extents.records()) &&
                std::ranges::equal(a.freeSpace.runs(), b.freeSpace.runs());
        }

        // The shown map's classes for the files of another snapshot, by file reference; new files get class 0.
        std::vector<std::uint8_t> classesFor(const core::MapData& shown, const core::Snapshot& now) {
            const auto classes = shown.fileClasses();
            if (classes.empty()) {
                return {};
            }
            const core::FileTable& before = shown.snapshot().files;
            std::unordered_map<std::uint64_t, std::uint8_t> byReference;
            byReference.reserve(before.size());
            for (core::FileIndex f = 0; f < before.size() && f < classes.size(); ++f) {
                byReference.emplace(before.fileReference(f), classes[f]);
            }
            std::vector<std::uint8_t> out(now.files.size(), 0);
            for (core::FileIndex f = 0; f < now.files.size(); ++f) {
                if (const auto found = byReference.find(now.files.fileReference(f)); found != byReference.end()) {
                    out[f] = found->second;
                }
            }
            return out;
        }

    } // namespace

    std::shared_ptr<const core::MapData> rebaseMap(const core::MapData& shown, const core::Snapshot& now,
                                                   core::Executor& executor) {
        if (sameLayout(shown.snapshot(), now)) {
            return nullptr;
        }
        const std::vector<std::uint8_t> classes = classesFor(shown, now);
        const std::vector<core::ClusterRun> everything{{0, now.geometry.totalClusters}};
        return core::MapData::update(shown, now, everything, executor, {}, classes);
    }

    bool anyApplicable(std::span<const MoveRecord> records) {
        return std::ranges::any_of(records,
                                   [](const MoveRecord& r) { return r.clustersMoved != 0 && !r.runsAfter.empty(); });
    }

    std::optional<LiveMapUpdate> applyMoves(const core::MapData& map, const core::execute::ExecutionPlan& plan,
                                            std::span<const MoveRecord> records, core::Executor& executor) {
        const std::vector<MoveRecord> moved = applicable(records);
        if (moved.empty()) {
            return std::nullopt;
        }
        core::Snapshot base = map.snapshot();
        std::vector<core::ClusterRun> dirty = dirtyRuns(base, plan, moved);
        std::optional<core::Snapshot> after = snapshotAfter(base, plan, moved, executor);
        if (!after) {
            return std::nullopt;
        }
        auto next = core::MapData::update(map, std::move(*after), dirty, executor, {}, map.fileClasses());
        return LiveMapUpdate{.map = std::move(next), .dirty = std::move(dirty)};
    }

} // namespace icd::services
