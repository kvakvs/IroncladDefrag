#pragma once

#include "core/exec/Executor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace icd::services {

    // Shared work-stealing pool for all jobs, sized from the resource caps (plan.md 6.6). Each worker has its
    // own queue (newest first, for cache locality); idle workers steal the oldest task of another worker.
    // A manual thread cap from Settings lowers the active worker count without restarting the pool, since the map
    // and the jobs hold references to it; workers above the count finish their task and then sleep.
    // Pending tasks are dropped on destruction, so owners cancel and wait for their jobs first.
    class ThreadPool : public core::Executor {
    public:
        // Starts workers threads (at least 1), all active.
        explicit ThreadPool(std::size_t workers);

        // Stops the workers after their current task and joins them.
        ~ThreadPool() override;

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        // Number of active worker threads.
        [[nodiscard]] std::size_t concurrency() const override { return active_.load(std::memory_order_relaxed); }

        // Worker threads started, the most concurrency() can be.
        [[nodiscard]] std::size_t capacity() const { return queues_.size(); }

        // Lets only the first workers (clamped to 1..capacity()) take tasks, for a changed thread cap. Meant
        // between jobs: a job that is running keeps the chunking it chose, only fewer or more workers serve it.
        void setConcurrency(std::size_t workers);

        // Queues a task: on the calling worker's own queue, or round-robin when called from outside the pool.
        void submit(Task task) override;

        // Splits the range into grain-sized chunks that workers and the calling thread claim one by one. The
        // caller takes part, so nested calls from pool tasks never deadlock.
        void parallelFor(core::ChunkedRange range, const RangeBody& body, const core::CancelToken& token = {}) override;

        // Worker time spent in tasks since the pool started, summed over the workers (running tasks count up to now).
        // Two samples give the workers' utilisation between them, which the Progress tab shows (plan.md 11).
        [[nodiscard]] std::chrono::nanoseconds busyTime() const;

    private:
        // One worker's task queue and when its running task started.
        struct WorkerQueue {
            std::mutex mutex; // Guards tasks; owners and thieves both lock it.
            std::deque<Task> tasks; // Owner pops from the back, thieves from the front.
            std::atomic<std::int64_t> busySince{0}; // Steady-clock ticks when the running task started; 0 when idle.
        };

        // Main loop of worker number index.
        void run(std::size_t index, const std::stop_token& stop);

        // Runs a task on a worker and adds its time to the busy total.
        void runTimed(WorkerQueue& worker, Task& task);

        // Takes a task for worker index: its own newest, else another worker's oldest. Inactive workers take none.
        std::optional<Task> take(std::size_t index);

        // Whether worker index may take tasks under the current concurrency.
        [[nodiscard]] bool active(std::size_t index) const { return index < concurrency(); }

        // Wakes sleeping workers: one when all are active, else all, since a wake-up that reaches an inactive
        // worker would otherwise be lost.
        void wakeWorkers();

        std::vector<std::unique_ptr<WorkerQueue>> queues_; // One per worker, same order as workers_.
        std::atomic<std::size_t> active_{1}; // Workers that take tasks: the first active_ ones.
        std::atomic<std::size_t> pending_{0}; // Queued tasks over all queues, for sleeping.
        std::atomic<std::size_t> nextQueue_{0}; // Round-robin position for outside submissions.
        std::atomic<std::int64_t> finishedBusy_{0}; // Steady-clock ticks spent in tasks that have ended.
        std::mutex sleepMutex_; // Pairs with wake_ so wake-ups are never lost.
        std::condition_variable_any wake_; // Signals idle workers that work arrived.
        std::vector<std::jthread> workers_; // The threads; declared last so they stop first.
    };

} // namespace icd::services
