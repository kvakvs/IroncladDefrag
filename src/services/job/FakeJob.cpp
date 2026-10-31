#include "services/job/FakeJob.h"

#include "core/log/Log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>

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
        void prepare(JobContext& context, const FakeJobOptions& options) {
            constexpr std::uint64_t steps = 10;
            for (std::uint64_t step = 0; step < steps; ++step) {
                context.token.throwIfCancelled();
                context.progress.report({options.serialStage, step, steps, ""});
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            context.progress.report({options.serialStage, steps, steps, ""});
        }

        // One chunk of the parallel stage: wait while paused, work, report.
        double runChunk(JobContext& context, const FakeJobOptions& options, std::size_t index,
                        std::atomic<std::uint64_t>& finished) {
            if (options.pause) {
                options.pause->wait(context.token);
            }
            const double value = spin(options, index);
            std::this_thread::sleep_for(options.sleepPerChunk);
            const std::string message = options.itemLabel ? options.itemLabel(index) : std::string();
            context.progress.report({options.parallelStage, finished.fetch_add(1) + 1, options.chunks, message});
            return value;
        }

        // Parallel stage: one chunk per work item, progress from whichever worker finishes a chunk.
        double crunch(JobContext& context, const FakeJobOptions& options) {
            std::atomic<std::uint64_t> finished{0};
            return core::parallelReduce(
                context.executor, {.count = options.chunks, .grain = 1}, 0.0,
                [&](std::size_t begin, std::size_t /*end*/) { return runChunk(context, options, begin, finished); },
                [](double a, double b) { return a + b; }, context.token);
        }

    } // namespace

    JobFunction makeFakeParallelJob(FakeJobOptions options) {
        return [options = std::move(options)](JobContext& context) {
            prepare(context, options);
            const double checksum = crunch(context, options);
            core::logging::info("fake job checksum", {{"value", checksum}});
        };
    }

} // namespace icd::services
