#include "services/execute/ExecutedSnapshot.h"

#include "core/execute/PlanReplay.h"
#include "core/snapshot/SnapshotPatch.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>

namespace icd::services {

    namespace {

        using core::ExtentEntry;
        using core::FileIndex;
        using core::Snapshot;
        using core::execute::StreamRun;

        // VCN given to unmovable extents kept with a moved file: after any data run, in their saved order.
        constexpr std::uint64_t keptVcnBase = std::uint64_t{1} << 40U;

        // Per moved file (by snapshot index): its runs after its last move that moved clusters.
        using MovedRuns = std::map<FileIndex, const std::vector<StreamRun>*>;

        // The moved files and their runs; nullopt when a moved file was not read back or is not in the base.
        std::optional<MovedRuns> movedFiles(const Snapshot& base, const core::execute::ExecutionPlan& plan,
                                            std::span<const MoveRecord> records) {
            const core::execute::SnapshotFiles files(base, plan);
            MovedRuns out;
            for (const MoveRecord& r : records) {
                if (r.clustersMoved == 0) {
                    continue;
                }
                const auto file = files.indexOf(plan.moves[r.move].fileReference);
                if (!file || r.runsAfter.empty()) {
                    return std::nullopt;
                }
                out[*file] = &r.runsAfter;
            }
            return out;
        }

        // Extent flags per stream of a base file's movable extents (directory index, compression unit).
        std::unordered_map<std::uint16_t, std::uint16_t> streamFlags(const Snapshot& base, FileIndex f) {
            std::unordered_map<std::uint16_t, std::uint16_t> out;
            const auto records = base.extents.records();
            for (const std::uint32_t position : base.extents.extentsOf(f)) {
                const core::ExtentRecord& r = records[position];
                if ((r.flags() & core::ExtentUnmovable) == 0) {
                    out.try_emplace(r.stream(), r.flags());
                }
            }
            return out;
        }

        // The new extents of a moved file: its runs now, plus the unmovable extents it keeps.
        void addExtents(const Snapshot& base, FileIndex f, std::span<const StreamRun> now,
                        std::vector<ExtentEntry>& out) {
            const auto flags = streamFlags(base, f);
            for (const StreamRun& r : now) {
                const auto found = flags.find(r.stream);
                out.push_back({.run = {r.lcn, r.length},
                               .vcn = r.vcn,
                               .file = f,
                               .stream = r.stream,
                               .flags = found == flags.end() ? std::uint16_t{0} : found->second});
            }
            const auto records = base.extents.records();
            std::uint64_t vcn = keptVcnBase;
            for (const std::uint32_t position : base.extents.extentsOf(f)) {
                const core::ExtentRecord& r = records[position];
                if ((r.flags() & core::ExtentUnmovable) != 0) {
                    out.push_back({.run = r.run(), .vcn = vcn++, .file = f, .stream = r.stream(), .flags = r.flags()});
                }
            }
        }

    } // namespace

    std::optional<core::Snapshot> snapshotAfter(core::Snapshot& base, const core::execute::ExecutionPlan& plan,
                                                std::span<const MoveRecord> records, core::Executor& executor) {
        const auto moved = movedFiles(base, plan, records);
        if (!moved) {
            return std::nullopt;
        }
        const std::size_t n = base.files.size();
        std::vector<FileIndex> baseOf(n);
        std::vector<FileIndex> parents(n);
        std::vector<std::uint16_t> flags(n);
        for (FileIndex f = 0; f < n; ++f) {
            baseOf[f] = f;
            parents[f] = base.files.parent(f);
            flags[f] = base.files.flags(f);
        }
        std::vector<std::string> names; // Copies: the pool may grow while the patch runs.
        names.reserve(moved->size());
        core::SnapshotPatch patch;
        for (const auto& [f, runs] : *moved) {
            baseOf[f] = core::noFile;
            names.emplace_back(base.names.name(base.files.name(f)));
            addExtents(base, f, *runs, patch.extents);
        }
        std::size_t k = 0;
        for (const auto& entry : *moved) {
            const FileIndex f = entry.first;
            patch.newFiles.push_back({names[k++], parents[f], base.files.sizeBytes(f), flags[f], base.files.times(f),
                                      base.files.fileReference(f)});
        }
        patch.baseOf = baseOf;
        patch.parents = parents;
        patch.flags = flags;
        return core::patchSnapshot(base, std::move(patch), executor);
    }

} // namespace icd::services
