#include "core/job/CancelToken.h"
#include "core/job/PauseGate.h"
#include "core/job/Progress.h"
#include "core/job/RateMeter.h"
#include "core/job/StageClock.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace icd::core { namespace {

    using namespace std::chrono_literals;

    // A default token is never cancelled and never throws.
    TEST(Cancel, DefaultTokenNeverCancelled) {
        const CancelToken token;
        EXPECT_FALSE(token.cancelled());
        EXPECT_NO_THROW(token.throwIfCancelled());
    }

    // Cancelling a source is seen by all of its tokens and makes throwIfCancelled throw.
    TEST(Cancel, SourceCancelsTokens) {
        CancelSource source;
        const CancelToken first = source.token();
        const CancelToken second = source.token();
        EXPECT_FALSE(first.cancelled());
        source.cancel();
        EXPECT_TRUE(first.cancelled());
        EXPECT_TRUE(second.cancelled());
        EXPECT_THROW(first.throwIfCancelled(), OperationCancelled);
    }

    // Parent cancellation reaches children and grandchildren; child cancellation stays local.
    TEST(Cancel, PropagatesFromParentOnly) {
        CancelSource parent;
        CancelSource child(parent.token());
        CancelSource grandchild(child.token());
        CancelSource sibling(parent.token());

        child.cancel();
        EXPECT_TRUE(grandchild.cancelled());
        EXPECT_FALSE(parent.cancelled());
        EXPECT_FALSE(sibling.cancelled());

        parent.cancel();
        EXPECT_TRUE(sibling.cancelled());
    }

    // A child created under an already cancelled parent starts cancelled.
    TEST(Cancel, ChildOfCancelledParentStartsCancelled) {
        CancelSource parent;
        parent.cancel();
        const CancelSource child(parent.token());
        EXPECT_TRUE(child.cancelled());
    }

    // A moved source keeps its link to the parent.
    TEST(Cancel, MovedSourceKeepsParentLink) {
        CancelSource parent;
        CancelSource child(parent.token());
        const CancelToken token = child.token();
        const CancelSource moved = std::move(child);
        parent.cancel();
        EXPECT_TRUE(moved.cancelled());
        EXPECT_TRUE(token.cancelled());
    }

    // A worker thread polling its token stops soon after cancel().
    TEST(Cancel, WorkerStopsWhenCancelled) {
        CancelSource source;
        std::atomic<bool> stopped{false};
        std::thread worker([token = source.token(), &stopped] {
            while (!token.cancelled()) {
                std::this_thread::sleep_for(1ms);
            }
            stopped = true;
        });
        source.cancel();
        worker.join();
        EXPECT_TRUE(stopped);
    }

    // Fraction is clamped and 0 for an unknown total.
    TEST(ProgressValue, Fraction) {
        EXPECT_DOUBLE_EQ((Progress{"s", 5, 0, ""}.fraction()), 0.0);
        EXPECT_DOUBLE_EQ((Progress{"s", 1, 4, ""}.fraction()), 0.25);
        EXPECT_DOUBLE_EQ((Progress{"s", 9, 4, ""}.fraction()), 1.0);
        EXPECT_TRUE((Progress{"s", 4, 4, ""}.finished()));
        EXPECT_FALSE((Progress{"s", 4, 0, ""}.finished()));
    }

    // Records forwarded progress for assertions.
    class RecordingSink : public ProgressSink {
    public:
        // Keeps every forwarded snapshot.
        void report(const Progress& progress) override {
            const std::scoped_lock lock(mutex);
            received.push_back(progress);
        }

        std::mutex mutex; // Reports may come from several threads.
        std::vector<Progress> received; // Everything forwarded, in order.
    };

    // Throttle with a manual clock, so time only moves when a test says so.
    struct ThrottleFixture {
        std::chrono::steady_clock::time_point now{}; // Current fake time.
        std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>(); // Receiver.
        ThrottledProgress throttle{sink, 100ms, [this] { return now; }}; // Unit under test.
    };

    // Reports inside the interval are held back; the newest one goes out after the interval.
    TEST(Throttle, LimitsRateAndKeepsNewest) {
        ThrottleFixture f;
        f.throttle.report({"scan", 1, 100, ""}); // First: always forwarded.
        f.now += 30ms;
        f.throttle.report({"scan", 2, 100, ""}); // Held.
        f.now += 30ms;
        f.throttle.report({"scan", 3, 100, ""}); // Held, replaces 2.
        ASSERT_EQ(f.sink->received.size(), 1u);
        f.now += 50ms; // 110 ms after the first.
        f.throttle.report({"scan", 4, 100, ""});
        ASSERT_EQ(f.sink->received.size(), 2u);
        EXPECT_EQ(f.sink->received[1].done, 4u);
    }

    // flush() delivers the held-back report once.
    TEST(Throttle, FlushDeliversPending) {
        ThrottleFixture f;
        f.throttle.report({"scan", 1, 100, ""});
        f.throttle.report({"scan", 7, 100, "file.txt"});
        f.throttle.flush();
        f.throttle.flush();
        ASSERT_EQ(f.sink->received.size(), 2u);
        EXPECT_EQ(f.sink->received[1].done, 7u);
        EXPECT_EQ(f.sink->received[1].message, "file.txt");
    }

    // Stage changes and finished stages pass immediately, whatever the time.
    TEST(Throttle, StageChangesAndCompletionPass) {
        ThrottleFixture f;
        f.throttle.report({"bitmap", 1, 10, ""});
        f.throttle.report({"bitmap", 10, 10, ""}); // Finished.
        f.throttle.report({"mft", 0, 50, ""}); // New stage.
        f.throttle.report({"mft", 5, 50, ""}); // Held.
        ASSERT_EQ(f.sink->received.size(), 3u);
        EXPECT_EQ(f.sink->received[1].done, 10u);
        EXPECT_EQ(f.sink->received[2].stage, "mft");
    }

    // A lower count arriving after a higher one (workers race) is dropped, so flush() never moves progress back.
    TEST(Throttle, DropsStaleReports) {
        ThrottleFixture f;
        f.throttle.report({"scan", 1, 64, ""});
        f.throttle.report({"scan", 64, 64, ""}); // Finished: forwarded.
        f.throttle.report({"scan", 63, 64, ""}); // Late report from another worker.
        f.throttle.flush();
        ASSERT_EQ(f.sink->received.size(), 2u);
        EXPECT_EQ(f.sink->received.back().done, 64u);
    }

    // A held-back report is not replaced by an older one.
    TEST(Throttle, PendingKeepsHighestCount) {
        ThrottleFixture f;
        f.throttle.report({"scan", 1, 100, ""});
        f.throttle.report({"scan", 9, 100, ""}); // Held.
        f.throttle.report({"scan", 7, 100, ""}); // Older: dropped.
        f.throttle.flush();
        ASSERT_EQ(f.sink->received.size(), 2u);
        EXPECT_EQ(f.sink->received.back().done, 9u);
    }

    // The same stage name with a new total is a new stage: it passes even with a lower count.
    TEST(Throttle, NewTotalStartsNewStage) {
        ThrottleFixture f;
        f.throttle.report({"pass", 50, 50, ""});
        f.throttle.report({"pass", 0, 80, ""});
        ASSERT_EQ(f.sink->received.size(), 2u);
        EXPECT_EQ(f.sink->received.back().total, 80u);
    }

    // Many threads reporting at once: forwarding is serialized and the rate stays bounded.
    TEST(Throttle, ConcurrentReportsAreSafe) {
        auto sink = std::make_shared<RecordingSink>();
        ThrottledProgress throttle(sink, 1h); // Nothing but the first report passes by time.
        std::vector<std::jthread> workers;
        for (int t = 0; t < 8; ++t) {
            workers.emplace_back([&throttle] {
                for (std::uint64_t i = 0; i < 1000; ++i) {
                    throttle.report({"work", i, 1'000'000, ""});
                }
            });
        }
        workers.clear();
        throttle.flush();
        EXPECT_EQ(sink->received.size(), 2u); // The first one and the flushed newest one.
    }

    // A paused gate holds workers until resume(); an open gate does not block.
    TEST(PauseGate, HoldsUntilResumed) {
        PauseGate gate;
        const CancelToken never;
        gate.wait(never); // Open: returns at once.
        gate.pause();
        EXPECT_TRUE(gate.paused());
        std::atomic<bool> passed{false};
        std::jthread worker([&] {
            gate.wait(never);
            passed = true;
        });
        std::this_thread::sleep_for(30ms);
        EXPECT_FALSE(passed);
        gate.resume();
        worker.join();
        EXPECT_TRUE(passed);
        EXPECT_FALSE(gate.paused());
    }

    // Cancelling releases a worker waiting at a paused gate, so Cancel works while paused.
    TEST(PauseGate, CancelReleasesWaiters) {
        PauseGate gate;
        gate.pause();
        CancelSource source;
        std::jthread worker([&gate, token = source.token()] { gate.wait(token); });
        std::this_thread::sleep_for(10ms);
        source.cancel();
        worker.join(); // Would hang if cancellation did not wake the wait.
        EXPECT_TRUE(gate.paused());
    }

    // Elapsed time runs from the job start; the estimate is linear within a stage.
    TEST(StageClock, ElapsedAndLinearEstimate) {
        const StageClock::TimePoint start{};
        StageClock clock(start);
        clock.update({"scan", 0, 100, ""}, start);
        EXPECT_EQ(clock.remaining(start), std::nullopt); // Nothing done yet.
        clock.update({"scan", 25, 100, ""}, start + 10s);
        EXPECT_EQ(clock.elapsed(start + 10s), 10s);
        EXPECT_EQ(clock.remaining(start + 10s), 30s); // 25% in 10 s: 30 s to go.
        EXPECT_EQ(clock.remaining(start + 25s), 15s); // Time passes between reports.
        EXPECT_EQ(clock.remaining(start + 99s), 0s); // Never negative.
    }

    // A new stage restarts the estimate; unknown totals and very early reports give none.
    TEST(StageClock, NewStageRestartsEstimate) {
        const StageClock::TimePoint start{};
        StageClock clock(start);
        clock.update({"bitmap", 50, 100, ""}, start + 4s);
        EXPECT_EQ(clock.remaining(start + 4s), std::nullopt); // First report starts the stage.
        clock.update({"mft", 1, 1000, ""}, start + 5s);
        clock.update({"mft", 10, 1000, ""}, start + 9s);
        EXPECT_EQ(clock.remaining(start + 9s), std::nullopt); // Only 1% done.
        clock.update({"mft", 500, 1000, ""}, start + 25s);
        EXPECT_EQ(clock.remaining(start + 25s), 20s);
        EXPECT_EQ(clock.elapsed(start + 25s), 25s);
        clock.update({"names", 10, 0, ""}, start + 30s);
        EXPECT_EQ(clock.remaining(start + 40s), std::nullopt); // Unknown total.
    }

    // The rate spans the window: older samples drop out, so a slow patch shows within a few seconds.
    TEST(RateMeter, AveragesOverTheWindow) {
        const RateMeter::TimePoint start{};
        RateMeter meter(5s);
        EXPECT_EQ(meter.perSecond(), std::nullopt);
        meter.add(start, 0.0);
        EXPECT_EQ(meter.perSecond(), std::nullopt); // One sample says nothing.
        meter.add(start + 100ms, 50.0);
        EXPECT_EQ(meter.perSecond(), std::nullopt); // Too short a span.
        meter.add(start + 2s, 200.0);
        EXPECT_DOUBLE_EQ(*meter.perSecond(), 100.0);
        meter.add(start + 4s, 400.0);
        EXPECT_DOUBLE_EQ(*meter.perSecond(), 100.0);
        meter.add(start + 10s, 430.0); // Slow for the last 6 s: the anchor is the 4 s sample.
        EXPECT_DOUBLE_EQ(*meter.perSecond(), 5.0);
        meter.add(start + 12s, 450.0); // The 10 s sample anchors only once 4 s drops out of the window.
        EXPECT_DOUBLE_EQ(*meter.perSecond(), 50.0 / 8.0);
    }

    // A total that goes down starts over; reset forgets everything.
    TEST(RateMeter, RestartsOnSmallerTotals) {
        const RateMeter::TimePoint start{};
        RateMeter meter;
        meter.add(start, 100.0);
        meter.add(start + 1s, 200.0);
        meter.add(start + 2s, 10.0);
        EXPECT_EQ(meter.perSecond(), std::nullopt);
        meter.add(start + 3s, 30.0);
        EXPECT_DOUBLE_EQ(*meter.perSecond(), 20.0);
        meter.reset();
        EXPECT_EQ(meter.perSecond(), std::nullopt);
    }

}} // namespace icd::core
