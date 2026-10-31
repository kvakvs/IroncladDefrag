#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <latch>
#include <stdexcept>
#include <thread>
#include <vector>

namespace icd::services { namespace {

    using namespace std::chrono_literals;

    // Sum of values with a magnitude spread that makes floating-point order matter.
    double reduceSum(core::Executor& executor, const std::vector<double>& values) {
        return core::parallelReduce(
            executor, {.count = values.size(), .grain = 1000}, 0.0,
            [&](std::size_t begin, std::size_t end) {
                double sum = 0.0;
                for (std::size_t i = begin; i < end; ++i) {
                    sum += values[i];
                }
                return sum;
            },
            [](double a, double b) { return a + b; });
    }

    // Values whose sum depends on the order of additions.
    std::vector<double> orderSensitiveValues() {
        std::vector<double> values(1'000'003);
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = std::sin(static_cast<double>(i)) * std::pow(10.0, static_cast<double>(i % 17) - 8.0);
        }
        return values;
    }

    // The same reduction gives bit-identical results inline, with 1 worker and with many workers.
    TEST(ThreadPool, ReductionIdenticalForAnyThreadCount) {
        const std::vector<double> values = orderSensitiveValues();
        core::InlineExecutor serial;
        const double expected = reduceSum(serial, values);
        for (const std::size_t workers : {1u, 2u, 8u, 31u}) {
            ThreadPool pool(workers);
            for (int repeat = 0; repeat < 3; ++repeat) {
                EXPECT_EQ(reduceSum(pool, values), expected) << workers << " workers";
            }
        }
    }

    // Every index is visited exactly once, including a short last chunk.
    TEST(ThreadPool, ParallelForCoversEachIndexOnce) {
        ThreadPool pool(8);
        std::vector<std::atomic<int>> hits(10'007);
        pool.parallelFor({.count = hits.size(), .grain = 64}, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                hits[i].fetch_add(1);
            }
        });
        for (std::size_t i = 0; i < hits.size(); ++i) {
            ASSERT_EQ(hits[i].load(), 1) << i;
        }
    }

    // An empty range and a zero grain are handled.
    TEST(ThreadPool, EmptyRangeAndZeroGrain) {
        ThreadPool pool(2);
        int calls = 0;
        pool.parallelFor({.count = 0, .grain = 10}, [&](std::size_t, std::size_t) { ++calls; });
        EXPECT_EQ(calls, 0);
        std::atomic<int> items{0};
        pool.parallelFor({.count = 5, .grain = 0},
                         [&](std::size_t begin, std::size_t end) { items += static_cast<int>(end - begin); });
        EXPECT_EQ(items.load(), 5);
    }

    // The first exception from a chunk reaches the caller after the other chunks stopped.
    TEST(ThreadPool, ExceptionPropagates) {
        ThreadPool pool(4);
        EXPECT_THROW(pool.parallelFor({.count = 1000, .grain = 1},
                                      [](std::size_t begin, std::size_t) {
                                          if (begin == 500) {
                                              throw std::runtime_error("chunk failed");
                                          }
                                      }),
                     std::runtime_error);
    }

    // Cancelling stops a long loop quickly: it throws OperationCancelled and skips most chunks.
    TEST(ThreadPool, CancelStopsWorkFast) {
        ThreadPool pool(4);
        core::CancelSource source;
        std::atomic<int> ran{0};
        constexpr int chunks = 10'000; // 10 000 chunks x 1 ms / 5 threads = about 2 s without cancel.
        std::jthread canceller([&source] {
            std::this_thread::sleep_for(50ms);
            source.cancel();
        });
        const auto start = std::chrono::steady_clock::now();
        EXPECT_THROW(pool.parallelFor(
                         {.count = chunks, .grain = 1},
                         [&](std::size_t, std::size_t) {
                             ran.fetch_add(1);
                             std::this_thread::sleep_for(1ms);
                         },
                         source.token()),
                     core::OperationCancelled);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_LT(elapsed, 1s);
        EXPECT_LT(ran.load(), chunks / 2);
    }

    // A loop that finished before cancellation does not throw.
    TEST(ThreadPool, CancelAfterCompletionDoesNotThrow) {
        ThreadPool pool(2);
        core::CancelSource source;
        pool.parallelFor({.count = 100, .grain = 10}, [](std::size_t, std::size_t) {}, source.token());
        source.cancel();
        SUCCEED();
    }

    // Submitted tasks all run, also when they come from many threads.
    TEST(ThreadPool, SubmitRunsAllTasks) {
        ThreadPool pool(4);
        constexpr int tasks = 2000;
        std::latch finished(tasks);
        std::atomic<int> ran{0};
        std::vector<std::jthread> producers;
        for (int p = 0; p < 4; ++p) {
            producers.emplace_back([&] {
                for (int i = 0; i < tasks / 4; ++i) {
                    pool.submit([&] {
                        ran.fetch_add(1);
                        finished.count_down();
                    });
                }
            });
        }
        finished.wait();
        EXPECT_EQ(ran.load(), tasks);
    }

    // A parallel loop started from inside a pool task completes (the caller works on its own chunks).
    TEST(ThreadPool, NestedParallelForDoesNotDeadlock) {
        ThreadPool pool(2);
        std::latch finished(4);
        std::atomic<int> items{0};
        for (int outer = 0; outer < 4; ++outer) {
            pool.submit([&] {
                pool.parallelFor({.count = 1000, .grain = 10},
                                 [&](std::size_t begin, std::size_t end) { items += static_cast<int>(end - begin); });
                finished.count_down();
            });
        }
        finished.wait();
        EXPECT_EQ(items.load(), 4000);
    }

    // Worker count follows the request, with a minimum of one.
    TEST(ThreadPool, Concurrency) {
        EXPECT_EQ(ThreadPool(0).concurrency(), 1u);
        EXPECT_EQ(ThreadPool(3).concurrency(), 3u);
    }

    // The inline executor runs chunks in order and honours cancellation.
    TEST(InlineExecutor, RunsInOrderAndCancels) {
        core::InlineExecutor executor;
        std::vector<std::size_t> begins;
        executor.parallelFor({.count = 10, .grain = 3},
                             [&](std::size_t begin, std::size_t) { begins.push_back(begin); });
        EXPECT_EQ(begins, (std::vector<std::size_t>{0, 3, 6, 9}));
        core::CancelSource source;
        source.cancel();
        EXPECT_THROW(executor.parallelFor(
                         {.count = 10, .grain = 3}, [](std::size_t, std::size_t) {}, source.token()),
                     core::OperationCancelled);
    }

}} // namespace icd::services
