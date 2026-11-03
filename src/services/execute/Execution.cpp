#include "services/execute/Execution.h"

#include "core/log/Log.h"
#include "platform/win/TextConvert.h"
#include "services/execute/ExecutedSnapshot.h"
#include "services/execute/VolumeMover.h"
#include "services/snapshot/SnapshotStore.h"

#include <format>
#include <functional>
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

        // Two sets of hooks to call one after the other.
        struct HookPair {
            ExecutionHooks first; // Called first (the log).
            ExecutionHooks then; // Called next (observers).
        };

        // A callback that calls the pair's two callbacks in order, each only when it is set.
        template <typename... Args>
        std::function<void(Args...)> inOrder(std::function<void(Args...)> first, std::function<void(Args...)> then) {
            return [first = std::move(first), then = std::move(then)](Args... args) {
                if (first) {
                    first(args...);
                }
                if (then) {
                    then(args...);
                }
            };
        }

        // Hooks that call the pair's first, then its second, for each callback either has.
        ExecutionHooks chained(const HookPair& pair) {
            ExecutionHooks out;
            out.onBegin = inOrder(pair.first.onBegin, pair.then.onBegin);
            out.onRecord = inOrder(pair.first.onRecord, pair.then.onRecord);
            out.onEvent = inOrder(pair.first.onEvent, pair.then.onEvent);
            return out;
        }

    } // namespace

    core::execute::ExecutionPlan executionPlanFor(const PlanOutcome& outcome, const PlanRequest& request,
                                                  const core::DriveInfo& drive, core::strategy::Effort level) {
        core::execute::PlanIdentity id;
        id.volumeName = drive.layout.volumeName;
        id.volumeSerial = drive.layout.ntfs ? drive.layout.ntfs->serialNumber : 0;
        id.geometry = request.snapshot->geometry;
        id.usn = request.snapshot->usn;
        id.madeAt = request.now;
        id.strategy = request.script.id;
        id.effort = level;
        return core::execute::executionPlanOf(outcome.levels.at(static_cast<std::size_t>(level)).result,
                                              *request.snapshot, std::move(id), request.reserved);
    }

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
        ExecutionControls c{controls.token, controls.pause, controls.progress,
                            chained({log.hooks(), controls.observers})};
        ExecutionReport report = executePlan(plan, replay.kept, device, controls.options, c);
        log.end(report);
        return report;
    }

    ExecutionOutcome runExecution(const ExecutionRequest& request, JobContext& context, core::PauseGate* pause) {
        ExecutionOutcome out;
        out.log = request.log;
        out.revalidation = revalidatePlan(request.drive, request.plan, request.order, context, request.revalidate);
        if (request.onRevalidated) {
            request.onRevalidated(out.revalidation);
        }
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
                                   {request.options, context.token, pause, &context.progress, request.observers});
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
