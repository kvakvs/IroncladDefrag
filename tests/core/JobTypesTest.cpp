#include "core/job/CancelToken.h"
#include "core/job/Progress.h"

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

}} // namespace icd::core
