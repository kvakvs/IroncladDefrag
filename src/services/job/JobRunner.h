#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/job/Progress.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace icd::services {

    // Identifies a started job, for cancel requests and for matching progress events to a job.
    using JobId = std::uint64_t;

    // How a job ended.
    enum class JobOutcome : std::uint8_t {
        Succeeded, // Returned normally without being cancelled.
        Cancelled, // Stopped because cancellation was requested.
        Failed, // Threw an exception; JobResult::error has the text.
    };

    // Final report of a job, delivered once to its observer.
    struct JobResult {
        JobOutcome outcome = JobOutcome::Succeeded; // How it ended.
        std::string error; // Exception text when Failed.
        std::chrono::milliseconds elapsed{}; // Wall time from start to end.
    };

    // What a running job gets: its cancellation token, a progress sink (already throttled) and the pool for
    // parallel work.
    struct JobContext {
        core::CancelToken token; // Poll between work items.
        core::ProgressSink& progress; // Report freely; the runner limits the rate.
        core::Executor& executor; // For parallelFor inside the job.
    };

    // The work itself.
    using JobFunction = std::move_only_function<void(JobContext&)>;

    // Receives a job's progress and result. Called on worker threads; implementations hand the data to their
    // own thread (the UI bridge uses wxQueueEvent).
    class JobObserver {
    public:
        virtual ~JobObserver() = default;

        // A throttled progress snapshot.
        virtual void onProgress(JobId id, const core::Progress& progress) = 0;

        // The final result; no progress follows it.
        virtual void onFinished(JobId id, const JobResult& result) = 0;
    };

    // Runs long operations (scan, plan, execute) on the shared pool so the UI thread never blocks, with
    // cancellation, throttled progress and one final result per job.
    class JobRunner {
    public:
        // Jobs run as tasks on executor, which must outlive the runner.
        explicit JobRunner(core::Executor& executor);

        // Cancels every job and waits for all of them to end.
        ~JobRunner();

        JobRunner(const JobRunner&) = delete;
        JobRunner& operator=(const JobRunner&) = delete;

        // Starts a job; observer may be null. Returns its id at once.
        JobId start(std::string name, JobFunction job, std::shared_ptr<JobObserver> observer);

        // Requests cancellation of one job; unknown or finished ids are ignored.
        void cancel(JobId id);

        // Requests cancellation of every running job.
        void cancelAll();

        // Blocks until no job is running.
        void waitAll();

        // Number of jobs that have not ended yet.
        [[nodiscard]] std::size_t runningCount() const;

    private:
        // A started job and what it needs while running.
        struct Job {
            JobId id = 0; // Assigned by start().
            std::string name; // For the log.
            core::CancelSource cancel; // Cancelled by cancel()/cancelAll().
            JobFunction work; // The job body.
            std::shared_ptr<JobObserver> observer; // Receiver of progress and result; may be null.
        };

        // Body of the pool task that runs one job from start to its final report.
        void run(const std::shared_ptr<Job>& job);

        // Removes a finished job and wakes waitAll().
        void finish(JobId id);

        core::Executor& executor_; // Where jobs run.
        mutable std::mutex mutex_; // Guards jobs_ and nextId_.
        std::condition_variable idle_; // Signalled when a job ends.
        std::map<JobId, std::shared_ptr<Job>> jobs_; // Running jobs.
        JobId nextId_ = 1; // Next id to hand out.
    };

} // namespace icd::services
