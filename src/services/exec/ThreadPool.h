#pragma once

#include "core/exec/Executor.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace icd::services {

    // Shared work-stealing pool for all jobs, sized from the resource caps (plan.md 6.6). Each worker has its
    // own queue (newest first, for cache locality); idle workers steal the oldest task of another worker.
    // Pending tasks are dropped on destruction, so owners cancel and wait for their jobs first.
    class ThreadPool : public core::Executor {
    public:
        // Starts workers threads (at least 1).
        explicit ThreadPool(std::size_t workers);

        // Stops the workers after their current task and joins them.
        ~ThreadPool() override;

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        // Number of worker threads.
        [[nodiscard]] std::size_t concurrency() const override { return workers_.size(); }

        // Queues a task: on the calling worker's own queue, or round-robin when called from outside the pool.
        void submit(Task task) override;

        // Splits the range into grain-sized chunks that workers and the calling thread claim one by one. The
        // caller takes part, so nested calls from pool tasks never deadlock.
        void parallelFor(core::ChunkedRange range, const RangeBody& body, const core::CancelToken& token = {}) override;

    private:
        // One worker's task queue.
        struct WorkerQueue {
            std::mutex mutex; // Guards tasks; owners and thieves both lock it.
            std::deque<Task> tasks; // Owner pops from the back, thieves from the front.
        };

        // Main loop of worker number index.
        void run(std::size_t index, const std::stop_token& stop);

        // Takes a task for worker index: its own newest, else another worker's oldest.
        std::optional<Task> take(std::size_t index);

        std::vector<std::unique_ptr<WorkerQueue>> queues_; // One per worker, same order as workers_.
        std::atomic<std::size_t> pending_{0}; // Queued tasks over all queues, for sleeping.
        std::atomic<std::size_t> nextQueue_{0}; // Round-robin position for outside submissions.
        std::mutex sleepMutex_; // Pairs with wake_ so wake-ups are never lost.
        std::condition_variable_any wake_; // Signals idle workers that work arrived.
        std::vector<std::jthread> workers_; // The threads; declared last so they stop first.
    };

} // namespace icd::services
