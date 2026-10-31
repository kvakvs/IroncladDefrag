#pragma once

#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icd::core {

    // One kind of file in a synthetic volume.
    struct SynthClass {
        std::string extension; // Name suffix, e.g. ".jpg"; drives classification later.
        double fileShare = 0.0; // Relative share of the file count.
        double meanBytes = 0.0; // Typical size before scaling to the fill level.
        double sizeSpread = 1.0; // Log-normal sigma: how widely sizes vary.
    };

    // Where the free space goes.
    enum class FreePattern : std::uint8_t {
        Scattered, // Small gaps all over the volume, as after years of use.
        Tail, // Almost everything free at the end, as on a freshly filled drive.
        Mixed, // Half scattered, half at the end.
    };

    // Shape of a synthetic volume. Everything is derived from the seed, so the same options always give the
    // same snapshot.
    struct SynthOptions {
        std::uint64_t seed = 1; // Random seed.
        VolumeGeometry geometry{.bytesPerCluster = 4096, .totalClusters = 1ULL << 20U}; // Volume size.
        std::uint32_t files = 10'000; // Regular files (directories come on top).
        std::uint32_t filesPerDirectory = 40; // Average directory size.
        double fillPercent = 60.0; // Used share of the volume.
        double fragmentedShare = 0.25; // Share of files split into several fragments.
        std::uint32_t maxFragments = 16; // Most fragments per fragmented file.
        std::uint32_t interleave = 2000; // How far apart (in pieces) a file's fragments land, at most.
        FreePattern freePattern = FreePattern::Mixed; // Where free space goes.
        double gapChance = 0.3; // Chance that a piece has a free gap before it (scattered free space).
        std::vector<SynthClass> classes = defaultClasses(); // File kinds; empty uses the defaults.

        // A mixed drive: sources, documents, photos, binaries, archives and some video.
        static std::vector<SynthClass> defaultClasses();
    };

    // Builds a seeded synthetic snapshot: a directory tree, files of the configured classes scaled to the fill
    // level, fragmented files whose pieces interleave with others, free gaps by the chosen pattern, and the
    // $MFT as an unmovable metadata file. Used to test the map, the planner and scaling up to 30 TB / 20 M
    // files without a real drive (plan.md 6.5). Throws SnapshotError when the files cannot fit.
    [[nodiscard]] Snapshot generateSyntheticVolume(const SynthOptions& options);

} // namespace icd::core
