#include "services/execute/Execution.h"

#include "core/log/Log.h"
#include "platform/win/TextConvert.h"
#include "services/execute/ExecutedSnapshot.h"
#include "services/execute/VolumeMover.h"
#include "services/snapshot/SnapshotStore.h"

#include <format>
#include <optional>

namespace icd::services {

    namespace {

        // The snapshot store a request names.
        SnapshotStore storeOf(const ExecutionRequest& request) {
            return SnapshotStore(request.snapshotDirectory.empty() ? SnapshotStore::defaultDirectory()
                                                                   : request.snapshotDirectory);
        }

        // Saves the snapshot updated from the verified runs; returns what happened.
        std::string saveAfter(const ExecutionRequest& request, ExecutionOutcome& out, core::Executor& executor) {
            Revalidation& r = out.revalidation;
            auto after = snapshotAfter(*r.current, request.plan, out.report.records, executor);
            if (!after) {
                return "the saved snapshot was removed: the volume changed in ways the moves do not explain; the next "
                       "analysis scans in full";
            }
            const core::SnapshotMeta meta{.volumeName = request.drive.layout.volumeName,
                                          .volumeSerial = request.plan.identity.volumeSerial,
                                          .scanTime = r.scanTime,
                                          .aspect = r.aspect,
                                          .scanner = r.scanner,
                                          .files = after->files.size(),
                                          .extents = after->extents.size()};
            try {
                storeOf(request).save(*after, meta, executor);
            }
            catch (const std::exception& e) {
                return std::string("the snapshot could not be saved: ") + e.what();
            }
            out.snapshotSaved = true;
            return "the saved snapshot now includes the moves";
        }

    } // namespace

    ExecutionRequest freshExecution(const core::DriveInfo& drive, core::execute::ExecutionPlan plan) {
        ExecutionRequest r;
        r.drive = drive;
        r.order = core::execute::allMoves(plan);
        r.log = newExecutionLogPath(executionLogDirectory(), plan.identity.volumeName);
        r.plan = std::move(plan);
        return r;
    }

    ExecutionRequest resumedExecution(const core::DriveInfo& drive, const std::filesystem::path& log) {
        ExecutionLogContents contents = readExecutionLog(log);
        ExecutionRequest r;
        r.drive = drive;
        r.order = movesToResume(contents);
        r.plan = std::move(contents.plan);
        r.log = log;
        r.resume = true;
        r.revalidate.maxAgeTicks = 0; // A resumed run is judged by the volume now, not by the plan's age.
        return r;
    }

    ExecutionReport executeLogged(const core::execute::ExecutionPlan& plan, const core::execute::ReplayResult& replay,
                                  MoveDevice& device, ExecutionLogWriter& log, const ExecutionRunControls& controls) {
        log.event("run-started");
        for (const core::execute::DroppedMove& d : replay.dropped) {
            log.dropped(d);
        }
        ExecutionControls c{controls.token, controls.pause, controls.progress, log.hooks()};
        ExecutionReport report = executePlan(plan, replay.kept, device, controls.options, c);
        log.end(report);
        return report;
    }

    ExecutionOutcome runExecution(const ExecutionRequest& request, JobContext& context, core::PauseGate* pause) {
        ExecutionOutcome out;
        out.log = request.log;
        out.revalidation = revalidatePlan(request.drive, request.plan, request.order, context, request.revalidate);
        if (out.revalidation.refused) {
            return out;
        }
        std::optional<ExecutionLogWriter> log;
        if (request.resume) {
            log.emplace(request.log);
        }
        else {
            log.emplace(request.log, request.plan);
        }
        storeOf(request).remove(request.drive.layout.volumeName);
        VolumeMover device(platform::fromUtf8(request.drive.layout.volumeName));
        out.report = executeLogged(request.plan, out.revalidation.replay, device, *log,
                                   {request.options, context.token, pause, &context.progress});
        out.snapshotNote = saveAfter(request, out, context.executor);
        const auto& t = out.report.totals;
        core::logging::info("execution finished",
                            {{"moved", t.moved},
                             {"skipped", t.skipped},
                             {"partial", t.partial},
                             {"cancelled", out.report.cancelled},
                             {"log", out.log.string()},
                             {"snapshot", out.snapshotNote}});
        return out;
    }

} // namespace icd::services
