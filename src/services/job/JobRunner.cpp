#include "services/job/JobRunner.h"

#include "core/log/Log.h"

#include <exception>
#include <utility>

namespace icd::services {

    namespace {

        // Passes a job's (already throttled) progress to its observer with the job id attached.
        class ObserverProgress : public core::ProgressSink {
        public:
            // Binds the observer to one job.
            ObserverProgress(std::shared_ptr<JobObserver> observer, JobId id) :
                observer_(std::move(observer)), id_(id) {}

            // Hands one snapshot to the observer.
            void report(const core::Progress& progress) override {
                if (observer_) {
                    observer_->onProgress(id_, progress);
                }
            }

        private:
            std::shared_ptr<JobObserver> observer_; // May be null: progress is then dropped.
            JobId id_; // Job the progress belongs to.
        };

        // Runs the job body and turns its way of ending into a result.
        JobResult execute(JobFunction& work, JobContext& context) {
            JobResult result;
            try {
                work(context);
                result.outcome = context.token.cancelled() ? JobOutcome::Cancelled : JobOutcome::Succeeded;
            }
            catch (const core::OperationCancelled&) {
                result.outcome = JobOutcome::Cancelled;
            }
            catch (const std::exception& e) {
                result.outcome = JobOutcome::Failed;
                result.error = e.what();
            }
            catch (...) {
                result.outcome = JobOutcome::Failed;
                result.error = "unknown exception";
            }
            return result;
        }

        // Log spelling of an outcome.
        const char* outcomeName(JobOutcome outcome) {
            switch (outcome) {
            case JobOutcome::Succeeded:
                return "succeeded";
            case JobOutcome::Cancelled:
                return "cancelled";
            case JobOutcome::Failed:
                return "failed";
            }
            return "unknown";
        }

    } // namespace

    JobRunner::JobRunner(core::Executor& executor) : executor_(executor) {}

    JobRunner::~JobRunner() {
        cancelAll();
        waitAll();
    }

    JobId JobRunner::start(std::string name, JobFunction job, std::shared_ptr<JobObserver> observer) {
        auto state = std::make_shared<Job>();
        state->name = std::move(name);
        state->work = std::move(job);
        state->observer = std::move(observer);
        {
            const std::scoped_lock lock(mutex_);
            state->id = nextId_++;
            jobs_.emplace(state->id, state);
        }
        executor_.submit([this, state] { run(state); });
        return state->id;
    }

    void JobRunner::run(const std::shared_ptr<Job>& job) {
        const auto started = std::chrono::steady_clock::now();
        core::logging::info("job start", {{"job", job->name}, {"id", job->id}});
        auto forward = std::make_shared<ObserverProgress>(job->observer, job->id);
        core::ThrottledProgress throttled(forward);
        JobContext context{job->cancel.token(), throttled, executor_};
        JobResult result = execute(job->work, context);
        throttled.flush();
        result.elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        core::logging::info("job end",
                            {{"job", job->name},
                             {"id", job->id},
                             {"outcome", outcomeName(result.outcome)},
                             {"ms", result.elapsed.count()},
                             {"error", result.error.empty() ? std::string("-") : result.error}});
        if (job->observer) {
            job->observer->onFinished(job->id, result);
        }
        finish(job->id);
    }

    void JobRunner::finish(JobId id) {
        {
            const std::scoped_lock lock(mutex_);
            jobs_.erase(id);
        }
        idle_.notify_all();
    }

    void JobRunner::cancel(JobId id) {
        const std::scoped_lock lock(mutex_);
        if (const auto it = jobs_.find(id); it != jobs_.end()) {
            it->second->cancel.cancel();
        }
    }

    void JobRunner::cancelAll() {
        const std::scoped_lock lock(mutex_);
        for (const auto& [id, job] : jobs_) {
            job->cancel.cancel();
        }
    }

    void JobRunner::waitAll() {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return jobs_.empty(); });
    }

    std::size_t JobRunner::runningCount() const {
        const std::scoped_lock lock(mutex_);
        return jobs_.size();
    }

} // namespace icd::services
