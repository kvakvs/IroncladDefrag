#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/map/BinSummary.h"
#include "core/map/ColorScheme.h"
#include "core/map/Palette.h"
#include "core/map/Viewport.h"
#include "core/map/World.h"
#include "core/snapshot/Snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace icd::core {

    // What the map colors by (plan.md 7.4). Each mode is a palette over the same pyramid, so switching modes
    // renders tiles again but never rebuilds map data. Zone fit and plan preview add an overlay from the plan.
    enum class ColorMode : std::uint8_t {
        Fragmentation, // Free, contiguous, fragmented and unmovable data.
        Age, // Time since the last change of the owning file, newest to oldest.
        Size, // Size of the owning file, smallest to largest.
        FileClass, // Class of the owning file (plan.md 8.2), in the built-in catalog's colors.
        ZoneFit, // Zone targets and misplaced data over the fragmentation colors (an overlay, PlanColorMode.h).
        PlanPreview, // A plan's sources (hollow) and destinations (solid) over the fragmentation colors (an overlay).
    };

    // Buckets of the age and size modes. Per-file values are bucket * bucketStep, so a bin holding one file
    // keeps its exact bucket and mixed bins average to the bucket in between.
    inline constexpr std::size_t valueBuckets = 6;

    // Value distance between neighbouring buckets (0, 51, ..., 255).
    inline constexpr std::uint32_t bucketStep = 255 / (valueBuckets - 1);

    static_assert(bucketStep == extentValueStep, "per-record values use the same steps as the buckets");

    // The per-file value of a bucket.
    [[nodiscard]] constexpr std::uint8_t bucketValue(std::size_t bucket) {
        return static_cast<std::uint8_t>(bucket * bucketStep);
    }

    // The bucket nearest to a (possibly averaged) value.
    [[nodiscard]] constexpr std::size_t bucketOfValue(std::uint8_t value) {
        return (value + bucketStep / 2) / bucketStep;
    }

    // Age bucket of a file modified at a FILETIME, seen from reference: 0 for under a day up to 5 for three
    // years or more. Times after the reference (clock skew) count as new.
    [[nodiscard]] std::size_t ageBucket(std::int64_t modified, std::int64_t reference);

    // Size bucket of a file: 0 for under 64 KB, then steps of 16x up to 5 for 4 GB or more.
    [[nodiscard]] std::size_t sizeBucket(std::uint64_t bytes);

    // The time ages are measured from: the newest modification in the snapshot, so a snapshot shows the same
    // ages whenever it is drawn (synthetic volumes, saved snapshots); 0 for a snapshot without files.
    [[nodiscard]] std::int64_t newestModification(const Snapshot& snapshot, Executor& executor,
                                                  const CancelToken& token = {});

    // Per-file inputs of the age and size modes, indexed by FileIndex: what the pyramid averages per bin.
    struct FileColorValues {
        std::vector<std::uint8_t> heat; // bucketValue() of the age bucket, newest = 255 (hot).
        std::vector<std::uint8_t> sizes; // bucketValue() of the size bucket.
    };

    // Computes the per-file values in parallel.
    [[nodiscard]] FileColorValues fileColorValues(const Snapshot& snapshot, std::int64_t reference, Executor& executor,
                                                  const CancelToken& token = {});

    // The same values per extent record, packed into one byte (packExtentValues()), so tiles read them in record
    // order instead of jumping into the 20 M-entry file columns.
    [[nodiscard]] std::vector<std::uint8_t> extentColorValues(const Snapshot& snapshot, const FileColorValues& values,
                                                              Executor& executor, const CancelToken& token = {});

    // One row of the map legend.
    struct LegendEntry {
        std::string_view label; // What the color means.
        Rgba color = 0; // The standard preset's color, as tiles draw it for a block fully used by that category.
        std::string_view key; // Stable name of the color in color schemes and the settings file (ColorScheme).
    };

    // The legend of a mode; entry 0 is always free space. The class mode lists the built-in catalog's classes in
    // ClassId order after it (its labels point into the catalog, which lives for the whole program).
    [[nodiscard]] std::span<const LegendEntry> legendOf(ColorMode mode);

    // The colors of a mode's legend entries in a scheme, in legendOf() order.
    [[nodiscard]] std::vector<Rgba> legendColors(ColorMode mode, const ColorScheme& scheme);

    // The palette that draws a mode in a scheme (default: the standard preset); its colors are the legend's.
    [[nodiscard]] std::shared_ptr<const Palette> paletteFor(ColorMode mode, const ColorScheme& scheme = ColorScheme());

    // The legend entry (1 and up) a used cluster falls in, from its owner.
    [[nodiscard]] std::size_t legendCategory(ColorMode mode, const PieceOwner& owner);

    // Clusters per legend entry in an area, counted exactly from the extent index.
    struct LegendCounts {
        std::vector<ClusterCount> clusters; // Per legend entry, free first.
        ClusterCount total = 0; // Clusters of the volume in the area.
    };

    // The cells a view shows (any part of a cell counts), clipped to the world.
    [[nodiscard]] CellRect visibleCells(const World& world, const Viewport& view);

    // Counts the clusters of each legend entry inside a cell rectangle (plan.md 7.4: live counts for the visible
    // area), in parallel over LCN pieces; equal to a per-cluster count for any thread count.
    [[nodiscard]] LegendCounts countLegend(const World& world, const MapFileFacts& facts, ColorMode mode,
                                           CellRect cells, Executor& executor, const CancelToken& token = {});

} // namespace icd::core
