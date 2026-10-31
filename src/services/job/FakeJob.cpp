#include "services/job/FakeJob.h"

#include "core/log/Log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>

namespace icd::services {

    namespace {

        // Burns CPU for a fixed number of steps and returns a value that depends on every step, so the job's
        // checksum is the same on every run and for any thread count.
        double spin(const FakeJobOptions& options, std::size_t seed) {
            double value = static_cast<double>(seed);
            for (std::uint32_t i = 0; i < options.spinIterations; ++i) {
                value = std::sqrt(value * value + 1.0);
            }
            return value;
        }

        // Serial warm-up stage: ten short steps, each checking for cancellation.
        void prepare(JobContext& context) {
            constexpr std::uint64_t steps = 10;
            for (std::uint64_t step = 0; step < steps; ++step) {
                context.token.throwIfCancelled();
                context.progress.report({"Preparing", step, steps, ""});
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            context.progress.report({"Preparing", steps, steps, ""});
        }

        // Parallel stage: one chunk per work item, progress from whichever worker finishes a chunk.
        double crunch(JobContext& context, const FakeJobOptions& options) {
            std::atomic<std::uint64_t> finished{0};
            const std::uint64_t total = options.chunks;
            return core::parallelReduce(
                context.executor, {.count = options.chunks, .grain = 1}, 0.0,
                [&](std::size_t begin, std::size_t /*end*/) {
                    const double value = spin(options, begin);
                    std::this_thread::sleep_for(options.sleepPerChunk);
                    context.progress.report({"Crunching", finished.fetch_add(1) + 1, total, ""});
                    return value;
                },
                [](double a, double b) { return a + b; }, context.token);
        }

    } // namespace

    JobFunction makeFakeParallelJob(FakeJobOptions options) {
        return [options](JobContext& context) {
            prepare(context);
            const double checksum = crunch(context, options);
            core::logging::info("fake job checksum", {{"value", checksum}});
        };
    }

} // namespace icd::services
