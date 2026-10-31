#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace icd::core {

    // Seeded random numbers for the synthetic volume generator. Self-contained (xoshiro256** seeded by
    // splitmix64, own distributions) because std distributions differ between standard libraries, and the
    // same seed must give the same volume everywhere.
    class SynthRandom {
    public:
        // Seeds the four state words from one number.
        explicit SynthRandom(std::uint64_t seed) {
            for (std::uint64_t& word : state_) {
                seed += 0x9E3779B97F4A7C15ULL;
                std::uint64_t z = seed;
                z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
                word = z ^ (z >> 31U);
            }
        }

        // 64 random bits.
        std::uint64_t next() {
            const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
            const std::uint64_t t = state_[1] << 17U;
            state_[2] ^= state_[0];
            state_[3] ^= state_[1];
            state_[1] ^= state_[2];
            state_[0] ^= state_[3];
            state_[2] ^= t;
            state_[3] = rotl(state_[3], 45);
            return result;
        }

        // Uniform in [0, bound); 0 for bound 0. The tiny modulo bias does not matter for test data.
        std::uint64_t below(std::uint64_t bound) { return bound == 0 ? 0 : next() % bound; }

        // Uniform in [0, 1).
        double unit() { return static_cast<double>(next() >> 11U) * 0x1.0p-53; }

        // Standard normal (Box-Muller).
        double normal() {
            const double u = 1.0 - unit(); // (0, 1], so the log is finite.
            const double v = unit();
            return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * std::numbers::pi * v);
        }

        // Exponential with the given mean.
        double exponential(double mean) { return -mean * std::log(1.0 - unit()); }

    private:
        // Left rotation.
        static constexpr std::uint64_t rotl(std::uint64_t x, int k) {
            return (x << static_cast<unsigned>(k)) | (x >> static_cast<unsigned>(64 - k));
        }

        std::uint64_t state_[4]{}; // Generator state.
    };

} // namespace icd::core
