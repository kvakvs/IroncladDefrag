#include "services/exec/ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace icd::services {

    namespace {

        // Which pool and worker the current thread belongs to, so submit() from a task uses its own queue.
        thread_local const ThreadPool* currentPool = nullptr;
        thread_local std::size_t currentWorker = 0;

        // Shared state of one parallelFor call. Helper tasks hold it by shared_ptr, so a helper that starts
        // after the call returned only sees "no chunks left" and never touches the (gone) body.
        struct ForState {
            core::ChunkedRange range; // The loop and its chunking.
            std::size_t chunks = 0; // range.chunks(), computed once.
            const core::Executor::RangeBody* body = nullptr; // Valid while chunks remain unclaimed.
            core::CancelToken token; // Stops claiming when cancelled.
            std::atomic<std::size_t> next{0}; // Next chunk to claim.
            std::atomic<std::size_t> done{0}; // Claimed chunks that ran or were skipped.
            std::atomic<bool> stop{false}; // Set on the first failure or cancellation.
            std::atomic<bool> cancelled{false}; // A chunk was skipped because of the token.
            std::mutex errorMutex; // Guards error.
            std::exception_ptr error; // First exception thrown by a chunk.
        };

        // Runs one chunk unless the loop is already stopping; records the first exception.
        void runChunk(ForState& state, std::size_t chunk) {
            if (state.stop.load(std::memory_order_relaxed)) {
                return;
            }
            if (state.token.cancelled()) {
                state.cancelled = true;
                state.stop = true;
                return;
            }
            try {
                (*state.body)(state.range.chunkBegin(chunk), state.range.chunkEnd(chunk));
            }
            catch (...) {
                const std::scoped_lock lock(state.errorMutex);
                if (!state.error) {
                    state.error = std::current_exception();
                }
                state.stop = true;
            }
        }

        // Claims and runs chunks until none are left; the last one to finish wakes the waiting caller.
        void claimChunks(ForState& state) {
            for (;;) {
                const std::size_t chunk = state.next.fetch_add(1, std::memory_order_relaxed);
                if (chunk >= state.chunks) {
                    return;
                }
                runChunk(state, chunk);
                if (state.done.fetch_add(1) + 1 == state.chunks) {
                    state.done.notify_all();
                }
            }
        }

        // Steady-clock ticks now, never 0 (0 marks an idle worker).
        std::int64_t ticksNow() {
            return std::max<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count(), 1);
        }

        // Blocks until every chunk has been run or skipped.
        void waitForChunks(ForState& state) {
            for (std::size_t seen = state.done.load(); seen != state.chunks; seen = state.done.load()) {
                state.done.wait(seen);
            }
        }

    } // namespace

    ThreadPool::ThreadPool(std::size_t workers) {
        const std::size_t count = std::max<std::size_t>(workers, 1);
        for (std::size_t i = 0; i < count; ++i) {
            queues_.push_back(std::make_unique<WorkerQueue>());
        }
        active_ = count;
        workers_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this, i](const std::stop_token& stop) { run(i, stop); });
        }
    }

    ThreadPool::~ThreadPool() {
        workers_.clear(); // Requests stop (which also wakes sleeping workers) and joins.
    }

    void ThreadPool::setConcurrency(std::size_t workers) {
        active_ = std::clamp<std::size_t>(workers, 1, queues_.size());
        {
            const std::scoped_lock lock(sleepMutex_); // Orders the change before the sleepers' predicate checks.
        }
        wake_.notify_all(); // Newly active workers pick up queued tasks.
    }

    void ThreadPool::wakeWorkers() {
        {
            const std::scoped_lock lock(sleepMutex_); // Orders this wake-up after a worker's predicate check.
        }
        if (concurrency() < queues_.size()) {
            wake_.notify_all();
        }
        else {
            wake_.notify_one();
        }
    }

    void ThreadPool::submit(Task task) {
        // Outside submissions go to active queues only; an inactive worker's own queue is drained by stealing.
        const std::size_t index =
            currentPool == this ? currentWorker : nextQueue_.fetch_add(1, std::memory_order_relaxed) % concurrency();
        {
            const std::scoped_lock lock(queues_[index]->mutex);
            queues_[index]->tasks.push_back(std::move(task));
        }
        pending_.fetch_add(1);
        wakeWorkers();
    }

    std::optional<ThreadPool::Task> ThreadPool::take(std::size_t index) {
        if (!active(index)) {
            return std::nullopt;
        }
        const std::size_t n = queues_.size();
        for (std::size_t k = 0; k < n; ++k) {
            WorkerQueue& queue = *queues_[(index + k) % n];
            const std::scoped_lock lock(queue.mutex);
            if (queue.tasks.empty()) {
                continue;
            }
            Task task;
            if (k == 0) { // Own queue: newest task, its data is likely still in cache.
                task = std::move(queue.tasks.back());
                queue.tasks.pop_back();
            }
            else { // Stealing: oldest task, the one its owner would reach last.
                task = std::move(queue.tasks.front());
                queue.tasks.pop_front();
            }
            pending_.fetch_sub(1);
            return task;
        }
        return std::nullopt;
    }

    void ThreadPool::run(std::size_t index, const std::stop_token& stop) {
        currentPool = this;
        currentWorker = index;
        while (!stop.stop_requested()) {
            if (std::optional<Task> task = take(index)) {
                runTimed(*queues_[index], *task);
                continue;
            }
            std::unique_lock lock(sleepMutex_);
            wake_.wait(lock, stop, [this, index] { return pending_.load() > 0 && active(index); });
        }
    }

    void ThreadPool::runTimed(WorkerQueue& worker, Task& task) {
        const std::int64_t start = ticksNow();
        worker.busySince.store(start, std::memory_order_relaxed);
        task();
        // Added before the start is cleared, so a sample may count a moment twice but never loses one.
        finishedBusy_.fetch_add(ticksNow() - start, std::memory_order_relaxed);
        worker.busySince.store(0, std::memory_order_relaxed);
    }

    std::chrono::nanoseconds ThreadPool::busyTime() const {
        const std::int64_t now = ticksNow();
        std::int64_t total = finishedBusy_.load(std::memory_order_relaxed);
        for (const auto& worker : queues_) {
            if (const std::int64_t since = worker->busySince.load(std::memory_order_relaxed); since != 0) {
                total += std::max<std::int64_t>(now - since, 0);
            }
        }
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::duration(total));
    }

    void ThreadPool::parallelFor(core::ChunkedRange range, const RangeBody& body, const core::CancelToken& token) {
        auto state = std::make_shared<ForState>();
        state->range = range;
        state->chunks = range.chunks();
        state->body = &body;
        state->token = token;
        if (state->chunks == 0) {
            return;
        }
        const std::size_t helpers = std::min(concurrency(), state->chunks - 1);
        for (std::size_t i = 0; i < helpers; ++i) {
            submit([state] { claimChunks(*state); });
        }
        claimChunks(*state);
        waitForChunks(*state);
        if (state->error) {
            std::rethrow_exception(state->error);
        }
        if (state->cancelled) {
            throw core::OperationCancelled();
        }
    }

} // namespace icd::services
