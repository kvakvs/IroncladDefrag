// Bitmap to free runs (T045): hand-made edge cases, and random bitmaps checked bit by bit; chunked, stitched and
// parallel conversions must all give the same runs as one serial pass.

#include "core/snapshot/BitmapToRuns.h"
#include "core/snapshot/FreeRunList.h"
#include "core/synth/SynthRandom.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

    using icd::core::BitmapChunk;
    using icd::core::ClusterRun;
    using icd::core::freeRunsOf;
    using icd::core::freeRunsParallel;
    using icd::core::stitchRuns;

    // A bitmap with the given clusters in use (true = used).
    std::vector<std::uint8_t> pack(const std::vector<bool>& used) {
        std::vector<std::uint8_t> bits((used.size() + 7) / 8, 0);
        for (std::size_t i = 0; i < used.size(); ++i) {
            if (used[i]) {
                bits[i / 8] |= static_cast<std::uint8_t>(1U << (i % 8));
            }
        }
        return bits;
    }

    // Free runs by looking at every cluster.
    std::vector<ClusterRun> bruteForce(const std::vector<bool>& used, std::uint64_t base) {
        std::vector<ClusterRun> runs;
        for (std::size_t i = 0; i < used.size(); ++i) {
            if (used[i]) {
                continue;
            }
            if (!runs.empty() && runs.back().end() == base + i) {
                ++runs.back().length;
            }
            else {
                runs.push_back({base + i, 1});
            }
        }
        return runs;
    }

    // A random bitmap with runs of random lengths, so both long runs and single clusters occur.
    std::vector<bool> randomBitmap(icd::core::SynthRandom& rng, std::size_t clusters) {
        std::vector<bool> used;
        bool state = rng.below(2) == 0;
        while (used.size() < clusters) {
            const std::size_t length = rng.below(4) == 0 ? 1 + rng.below(300) : 1 + rng.below(6);
            for (std::size_t i = 0; i < length && used.size() < clusters; ++i) {
                used.push_back(state);
            }
            state = !state;
        }
        return used;
    }

    TEST(BitmapToRuns, HandMadeEdges) {
        const std::vector<std::uint8_t> free(16, 0x00);
        EXPECT_EQ(freeRunsOf({64, 128, free}), (std::vector<ClusterRun>{{64, 128}}));
        const std::vector<std::uint8_t> used(16, 0xFF);
        EXPECT_TRUE(freeRunsOf({0, 128, used}).empty());
        // Alternating clusters: every second one free.
        const std::vector<std::uint8_t> alternating(2, 0x55);
        EXPECT_EQ(freeRunsOf({0, 16, alternating}).size(), 8U);
        EXPECT_EQ(freeRunsOf({0, 16, alternating})[0], (ClusterRun{1, 1}));
        // Bits past the valid cluster count never become free space.
        EXPECT_EQ(freeRunsOf({0, 5, free}), (std::vector<ClusterRun>{{0, 5}}));
        EXPECT_EQ(freeRunsOf({0, 70, free}), (std::vector<ClusterRun>{{0, 70}}));
        // A run crossing a 64-bit word boundary stays one run.
        std::vector<bool> crossing(130, true);
        for (std::size_t i = 60; i < 70; ++i) {
            crossing[i] = false;
        }
        EXPECT_EQ(freeRunsOf({8, 130, pack(crossing)}), (std::vector<ClusterRun>{{68, 10}}));
        EXPECT_TRUE(freeRunsOf({0, 0, {}}).empty());
    }

    TEST(BitmapToRuns, RandomBitmapsMatchBruteForce) {
        icd::core::SynthRandom rng(7);
        for (int round = 0; round < 200; ++round) {
            const std::size_t clusters = 1 + rng.below(5000);
            const auto used = randomBitmap(rng, clusters);
            const auto bits = pack(used);
            const std::uint64_t base = rng.below(1000) * 8;
            ASSERT_EQ(freeRunsOf({base, clusters, bits}), bruteForce(used, base)) << "round " << round;
        }
    }

    TEST(BitmapToRuns, ChunksStitchToTheWhole) {
        icd::core::SynthRandom rng(11);
        for (int round = 0; round < 50; ++round) {
            const std::size_t clusters = 64 + rng.below(20000);
            const auto used = randomBitmap(rng, clusters);
            const auto bits = pack(used);
            const auto whole = freeRunsOf({0, clusters, bits});
            // Cut at random byte boundaries, as the volume reader's pieces are.
            std::vector<ClusterRun> stitched;
            std::size_t byte = 0;
            while (byte * 8 < clusters) {
                const std::size_t size = 1 + rng.below(300);
                const std::size_t end = std::min(bits.size(), byte + size);
                const std::uint64_t first = std::uint64_t{byte} * 8;
                const BitmapChunk piece{first, std::min<std::uint64_t>(clusters - first, (end - byte) * 8),
                                        std::span(bits).subspan(byte, end - byte)};
                stitchRuns(stitched, freeRunsOf(piece));
                byte = end;
            }
            ASSERT_EQ(stitched, whole) << "round " << round;
        }
    }

    TEST(BitmapToRuns, ParallelEqualsSerial) {
        icd::core::SynthRandom rng(3);
        const std::size_t clusters = 1'000'003;
        const auto used = randomBitmap(rng, clusters);
        const auto bits = pack(used);
        const BitmapChunk chunk{4096, clusters, bits};
        const auto serial = freeRunsOf(chunk);
        EXPECT_EQ(serial, bruteForce(used, 4096));
        icd::core::InlineExecutor inline_;
        icd::services::ThreadPool pool(6);
        for (const std::size_t grain : {1U, 8U, 13U, 1024U, 1U << 20U}) {
            EXPECT_EQ(freeRunsParallel(inline_, chunk, grain), serial) << "grain " << grain;
            EXPECT_EQ(freeRunsParallel(pool, chunk, grain), serial) << "grain " << grain;
        }
        const icd::core::FreeRunList list(serial, 4096 + clusters);
        EXPECT_EQ(list.size(), serial.size()); // Already merged: the list keeps every run.
    }

} // namespace
