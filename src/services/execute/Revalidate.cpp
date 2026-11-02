#include "services/execute/Revalidate.h"

#include "core/log/Log.h"
#include "services/plan/PlanJob.h"

#include <chrono>
#include <format>

namespace icd::services {

    namespace {

        // Why a snapshot's geometry does not fit the plan, or nullopt.
        std::optional<std::string> geometryRefusal(core::VolumeGeometry plan, core::VolumeGeometry now) {
            if (plan.bytesPerCluster != now.bytesPerCluster) {
                return std::format("the cluster size changed ({} -> {} bytes): analyse and plan again",
                                   plan.bytesPerCluster, now.bytesPerCluster);
            }
            if (plan.totalClusters != now.totalClusters) {
                return std::format("the volume size changed ({} -> {} clusters): analyse and plan again",
                                   plan.totalClusters, now.totalClusters);
            }
            return std::nullopt;
        }

        // Why the plan is too old, or nullopt.
        std::optional<std::string> ageRefusal(std::int64_t madeAt, PlanAge age) {
            if (age.maxAgeTicks <= 0 || age.now - madeAt <= age.maxAgeTicks) {
                return std::nullopt;
            }
            const double hours = static_cast<double>(age.now - madeAt) / static_cast<double>(fileTimeTicksPerHour);
            return std::format("the plan is {:.1f} hours old (at most {:.1f} allowed): analyse and plan again", hours,
                               static_cast<double>(age.maxAgeTicks) / static_cast<double>(fileTimeTicksPerHour));
        }

        // Logs what revalidation kept and dropped.
        void logReplay(const core::execute::ReplayResult& r, const Revalidation& out) {
            core::logging::info("plan revalidated",
                                {{"kept", r.kept.size()},
                                 {"dropped", r.dropped.size()},
                                 {"already_done", r.alreadyDone},
                                 {"pending_clusters", r.pendingClusters},
                                 {"caught_up", out.caughtUp},
                                 {"how", out.how},
                                 {"seconds", out.seconds}});
        }

    } // namespace

    std::optional<std::string> planRefusal(const core::execute::PlanIdentity& plan, const core::DriveInfo& drive,
                                           PlanAge age) {
        if (plan.volumeName != drive.layout.volumeName) {
            return "the plan was made for another volume";
        }
        if (!drive.layout.ntfs || drive.layout.ntfs->serialNumber != plan.volumeSerial) {
            return std::string("the volume was formatted again since the plan was made");
        }
        if (auto why = geometryRefusal(plan.geometry, {drive.layout.bytesPerCluster, drive.layout.totalClusters})) {
            return why;
        }
        return ageRefusal(plan.madeAt, age);
    }

    core::execute::ReplayResult replayOnSnapshot(const core::execute::ExecutionPlan& plan,
                                                 std::span<const std::uint32_t> order, const core::Snapshot& current) {
        const core::execute::SnapshotFiles files(current, plan);
        return core::execute::replayPlan(plan, order, current.freeSpace.runs(), files);
    }

    Revalidation revalidatePlan(const core::DriveInfo& drive, const core::execute::ExecutionPlan& plan,
                                std::span<const std::uint32_t> order, JobContext& context,
                                const RevalidateOptions& options) {
        const auto start = std::chrono::steady_clock::now();
        Revalidation out;
        out.refused = planRefusal(plan.identity, drive, {fileTimeNow(), options.maxAgeTicks});
        if (out.refused) {
            core::logging::warning("plan refused", {{"reason", *out.refused}});
            return out;
        }
        out.scanTime = fileTimeNow();
        AnalysisResult now = analyzeVolume(drive, context, {}, options.analysis);
        out.scanner = static_cast<std::uint8_t>(now.scan.scanner);
        out.aspect = now.aspect;
        out.refused = geometryRefusal(plan.identity.geometry, now.snapshot.geometry);
        out.caughtUp = now.caughtUp;
        out.how = now.catchUp;
        out.current = std::make_shared<core::Snapshot>(std::move(now.snapshot));
        if (!out.refused) {
            context.progress.report({"Revalidating the plan", 0, 0, {}});
            out.replay = replayOnSnapshot(plan, order, *out.current);
        }
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        logReplay(out.replay, out);
        return out;
    }

} // namespace icd::services
