#pragma once

#include "core/map/Viewport.h"
#include "core/map/World.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icd::core {

    // Where the volume sits on its physical disk; known once the scanner reads the partition (plan.md 7.1).
    struct DiskPlacement {
        std::uint64_t partitionOffset = 0; // Bytes from the start of the disk to the volume's first byte.
        std::uint64_t diskBytes = 0; // Size of the whole disk.
    };

    // What the map rulers describe: the fixed world, the current view and the volume's byte facts.
    struct RulerInput {
        const World* world = nullptr; // Layout; ticks are empty without one.
        Viewport view; // What the map window shows.
        std::uint32_t bytesPerCluster = 4096; // Turns cells into bytes.
        std::optional<DiskPlacement> disk; // Adds "% of disk" labels when known.
        double minSpacing = 40.0; // Labelled ticks are at least this many pixels apart.
    };

    // One labelled mark on a ruler.
    struct RulerTick {
        double pixel = 0.0; // Position along the ruler, in map window pixels.
        std::uint64_t cell = 0; // Row (vertical ruler) or column (horizontal ruler) the tick marks the start of.
        std::uint64_t bytes = 0; // Byte offset: from the volume start (rows) or from the row start (columns).
        double percent = 0.0; // Share of the volume before the tick (rows only).
        std::string bytesText; // Offset label, with enough digits to tell neighbouring ticks apart.
        std::string percentText; // "% of volume" label; empty on the horizontal ruler.
        std::string diskText; // "% of disk" label; empty when the partition placement is unknown.
    };

    // The marks of one ruler for one view.
    struct RulerTicks {
        std::vector<RulerTick> major; // Labelled ticks, in increasing pixel order.
        std::vector<double> minor; // Unlabelled ticks between them (pixels).
        std::uint64_t step = 1; // Cells between labelled ticks (a power of two).
    };

    // Ticks of the vertical ruler: starts of rows, labelled with the byte offset and % of volume (and of the
    // disk when the placement is known). The world is laid out row by row, so a row start is an LCN.
    [[nodiscard]] RulerTicks rowTicks(const RulerInput& input);

    // Ticks of the horizontal ruler: offsets within a row ("+32 MB"), useful once single rows are visible.
    [[nodiscard]] RulerTicks columnTicks(const RulerInput& input);

    // The smallest power of two at least minCells (and at least 1): tick steps double, so labels stay round
    // in the binary units the volume is measured in.
    [[nodiscard]] std::uint64_t tickStep(double minCells);

    // The byte scale of one ruler's labels; all labels of a ruler share it so they read alike.
    struct OffsetScale {
        std::uint64_t largest = 0; // Largest offset the ruler can show; picks the unit.
        std::uint64_t step = 1; // Bytes between labelled ticks; picks the decimals.
    };

    // The unit (index into bytes, KB, MB, GB, TB, PB) of a scale: the unit of the largest offset, smaller when
    // the step is too fine for three decimals or is exact one unit down.
    [[nodiscard]] std::size_t offsetUnit(const OffsetScale& scale);

    // Formats a byte offset in its scale's unit with the decimals that write the step exactly (at most three,
    // still enough to tell neighbours apart): "1.75 TB", "704 GB", "30,719.500 GB".
    [[nodiscard]] std::string formatOffset(std::uint64_t bytes, const OffsetScale& scale);

    // Formats a percentage with just enough decimals that values step apart read differently ("42%", "4.2%").
    [[nodiscard]] std::string formatPercent(double percent, double step);

} // namespace icd::core
