#include "services/exec/ThreadPool.h"
#include "services/job/FakeJob.h"
#include "services/job/JobRunner.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace icd::services { namespace {

    using namespace std::chrono_literals;

    // Records callbacks and lets a test wait for the final result.
    class RecordingObserver : public JobObserver {
    public:
        // Keeps every progress snapshot.
        void onProgress(JobId /*id*/, const core::Progress& progress) override {
            const std::scoped_lock lock(mutex_);
            progress_.push_back(progress);
        }

        // Stores the result and wakes waitResult().
        void onFinished(JobId id, const JobResult& result) override {
            {
                const std::scoped_lock lock(mutex_);
                finishedId_ = id;
                result_ = result;
                ++finishCount_;
            }
            finished_.notify_all();
        }

        // Waits for onFinished (or fails the test after 10 s).
        JobResult waitResult() {
            std::unique_lock lock(mutex_);
            EXPECT_TRUE(finished_.wait_for(lock, 10s, [this] { return result_.has_value(); }));
            return result_.value_or(JobResult{JobOutcome::Failed, "timeout", {}});
        }

        // Copy of the progress received so far.
        std::vector<core::Progress> progress() {
            const std::scoped_lock lock(mutex_);
            return progress_;
        }

        // How often onFinished was called.
        int finishCount() {
            const std::scoped_lock lock(mutex_);
            return finishCount_;
        }

    private:
        std::mutex mutex_; // Guards everything below.
        std::condition_variable finished_; // Signalled by onFinished.
        std::vector<core::Progress> progress_; // Every progress callback.
        std::optional<JobResult> result_; // Set by onFinished.
        JobId finishedId_ = 0; // Id passed to onFinished.
        int finishCount_ = 0; // Number of onFinished calls.
    };

    // A job that returns normally succeeds, reports its progress and ends exactly once.
    TEST(JobRunner, SucceedsWithProgress) {
        ThreadPool pool(4);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        runner.start("ok", [](JobContext& context) { context.progress.report({"work", 1, 1, ""}); }, observer);
        const JobResult result = observer->waitResult();
        runner.waitAll();
        EXPECT_EQ(result.outcome, JobOutcome::Succeeded);
        ASSERT_FALSE(observer->progress().empty());
        EXPECT_TRUE(observer->progress().back().finished());
        EXPECT_EQ(observer->finishCount(), 1);
        EXPECT_EQ(runner.runningCount(), 0u);
    }

    // An exception becomes a Failed result with its message.
    TEST(JobRunner, ExceptionFails) {
        ThreadPool pool(2);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        runner.start("boom", [](JobContext&) { throw std::runtime_error("disk on fire"); }, observer);
        const JobResult result = observer->waitResult();
        EXPECT_EQ(result.outcome, JobOutcome::Failed);
        EXPECT_EQ(result.error, "disk on fire");
    }

    // cancel() stops a job that polls its token, and the result says Cancelled.
    TEST(JobRunner, CancelStopsJob) {
        ThreadPool pool(2);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        const JobId id = runner.start(
            "loop",
            [](JobContext& context) {
                while (true) {
                    context.token.throwIfCancelled();
                    std::this_thread::sleep_for(1ms);
                }
            },
            observer);
        std::this_thread::sleep_for(20ms);
        runner.cancel(id);
        EXPECT_EQ(observer->waitResult().outcome, JobOutcome::Cancelled);
    }

    // Destroying the runner cancels running jobs and waits for them.
    TEST(JobRunner, DestructorCancelsAndWaits) {
        ThreadPool pool(2);
        auto observer = std::make_shared<RecordingObserver>();
        {
            JobRunner runner(pool);
            runner.start(
                "loop",
                [](JobContext& context) {
                    while (!context.token.cancelled()) {
                        std::this_thread::sleep_for(1ms);
                    }
                },
                observer);
            std::this_thread::sleep_for(10ms);
        }
        EXPECT_EQ(observer->finishCount(), 1);
        EXPECT_EQ(observer->waitResult().outcome, JobOutcome::Cancelled);
    }

    // Rapid progress is throttled, but the last snapshot still arrives.
    TEST(JobRunner, ProgressIsThrottled) {
        ThreadPool pool(2);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        runner.start(
            "fast",
            [](JobContext& context) {
                for (std::uint64_t i = 0; i < 100'000; ++i) {
                    context.progress.report({"work", i, 1'000'000, ""});
                }
                context.progress.report({"work", 999'999, 1'000'000, ""});
            },
            observer);
        observer->waitResult();
        const auto progress = observer->progress();
        EXPECT_LT(progress.size(), 50u);
        ASSERT_FALSE(progress.empty());
        EXPECT_EQ(progress.back().done, 999'999u);
    }

    // The fake job runs its parallel stage on the pool and reports both stages.
    TEST(FakeJob, RunsBothStages) {
        ThreadPool pool(4);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        runner.start("fake", makeFakeParallelJob({.chunks = 64, .spinIterations = 1000, .sleepPerChunk = 50us}),
                     observer);
        EXPECT_EQ(observer->waitResult().outcome, JobOutcome::Succeeded);
        const auto progress = observer->progress();
        ASSERT_FALSE(progress.empty());
        EXPECT_EQ(progress.front().stage, "Preparing");
        EXPECT_EQ(progress.back().stage, "Crunching");
        EXPECT_EQ(progress.back().done, 64u);
    }

    // The fake job stops promptly when cancelled in its parallel stage.
    TEST(FakeJob, CancelsPromptly) {
        ThreadPool pool(4);
        JobRunner runner(pool);
        auto observer = std::make_shared<RecordingObserver>();
        const JobId id = runner.start("fake", makeFakeParallelJob(), observer); // About 25 s at 4 workers.
        std::this_thread::sleep_for(500ms);
        const auto cancelAt = std::chrono::steady_clock::now();
        runner.cancel(id);
        EXPECT_EQ(observer->waitResult().outcome, JobOutcome::Cancelled);
        EXPECT_LT(std::chrono::steady_clock::now() - cancelAt, 1s);
    }

}} // namespace icd::services
