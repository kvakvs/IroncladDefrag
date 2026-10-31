#pragma once

#include "core/job/CancelToken.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace icd::core {

    // A loop over [0, count) cut into fixed-size chunks. The chunk boundaries depend only on these two
    // numbers, never on the thread count, which is what makes parallel results equal serial ones.
    struct ChunkedRange {
        std::size_t count = 0; // Items in the loop.
        std::size_t grain = 1; // Items per chunk; 0 counts as 1.

        // Items per chunk, at least 1.
        [[nodiscard]] constexpr std::size_t chunkSize() const { return grain == 0 ? 1 : grain; }

        // Number of chunks.
        [[nodiscard]] constexpr std::size_t chunks() const { return (count + chunkSize() - 1) / chunkSize(); }

        // First item of a chunk.
        [[nodiscard]] constexpr std::size_t chunkBegin(std::size_t chunk) const { return chunk * chunkSize(); }

        // One past the last item of a chunk.
        [[nodiscard]] constexpr std::size_t chunkEnd(std::size_t chunk) const {
            return std::min(chunkBegin(chunk) + chunkSize(), count);
        }
    };

    // Runs work for core algorithms without core knowing about threads: services passes a thread pool, tests
    // pass an InlineExecutor, and both must give the same results (plan.md 6.2.8).
    class Executor {
    public:
        // A unit of fire-and-forget work.
        using Task = std::move_only_function<void()>;

        // Body of a parallel loop: processes the index range [begin, end).
        using RangeBody = std::function<void(std::size_t begin, std::size_t end)>;

        virtual ~Executor() = default;

        // How many tasks can run at the same time (1 for the inline executor).
        [[nodiscard]] virtual std::size_t concurrency() const = 0;

        // Queues a task. Exceptions escaping the task are the task's own problem (they end the process).
        virtual void submit(Task task) = 0;

        // Calls body on every chunk of range and returns when all are done. Stops starting chunks when token
        // is cancelled (then throws OperationCancelled) or a chunk throws (then rethrows the first exception),
        // in both cases after the running chunks have finished.
        virtual void parallelFor(ChunkedRange range, const RangeBody& body, const CancelToken& token = {}) = 0;
    };

    // Runs everything on the calling thread, in order. The reference for "parallel equals serial" tests.
    class InlineExecutor : public Executor {
    public:
        // Always 1.
        [[nodiscard]] std::size_t concurrency() const override { return 1; }

        // Runs the task immediately.
        void submit(Task task) override { task(); }

        // Runs the chunks in index order.
        void parallelFor(ChunkedRange range, const RangeBody& body, const CancelToken& token = {}) override;
    };

    // Deterministic parallel reduction: map(begin, end) gives one partial result per chunk, and the partials
    // are combined in chunk order, so even floating-point results are identical for any thread count.
    template <typename T, typename Map, typename Combine>
    T parallelReduce(Executor& executor, ChunkedRange range, T init, Map map, Combine combine,
                     const CancelToken& token = {}) {
        std::vector<T> partial(range.chunks(), init);
        const std::size_t size = range.chunkSize();
        executor.parallelFor(
            range, [&](std::size_t begin, std::size_t end) { partial[begin / size] = map(begin, end); }, token);
        T result = std::move(init);
        for (T& value : partial) {
            result = combine(std::move(result), std::move(value));
        }
        return result;
    }

} // namespace icd::core
