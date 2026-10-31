#include "core/map/Ruler.h"

#include "core/format/Count.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace icd::core {

    namespace {

        // Units of formatOffset(), 1024-based like formatBytes().
        constexpr std::array<std::string_view, 6> unitNames{"bytes", "KB", "MB", "GB", "TB", "PB"};

        // Most decimals a ruler label shows; finer steps switch to a smaller unit instead.
        constexpr int maxOffsetDecimals = 3;

        // Largest value a label may show after switching to a smaller unit for an exact step.
        constexpr double maxSmallerUnitValue = 100000.0;

        // Most decimals of a percentage label.
        constexpr int maxPercentDecimals = 4;

        // Labelled ticks are cut into this many minor intervals.
        constexpr std::uint64_t minorPerMajor = 4;

        // Bytes in unit index u (1024^u).
        double unitBytes(std::size_t unit) { return std::ldexp(1.0, static_cast<int>(10 * unit)); }

        // Decimals of a percentage that make values step apart read differently: 10^-decimals <= step.
        int percentDecimals(double step) {
            if (step >= 1.0 || step <= 0.0) {
                return 0;
            }
            return std::clamp(static_cast<int>(std::ceil(-std::log10(step) - 1e-9)), 0, maxPercentDecimals);
        }

        // Decimals that write an offset step exactly (a step of 1.75 reads "1.75", not "2"), at most
        // maxOffsetDecimals.
        int exactDecimals(double value) {
            double scaled = value;
            for (int decimals = 0; decimals < maxOffsetDecimals; ++decimals) {
                if (std::abs(scaled - std::round(scaled)) <= 1e-9 * std::max(1.0, scaled)) {
                    return decimals;
                }
                scaled *= 10.0;
            }
            return maxOffsetDecimals;
        }

        // Inserts thousands separators into the integer part of a plain number ("30719.97" -> "30,719.97").
        std::string groupThousands(const std::string& number) {
            const std::size_t point = number.find('.');
            const std::string whole = number.substr(0, point);
            const std::string rest = point == std::string::npos ? std::string{} : number.substr(point);
            return formatCount(std::stoull(whole)) + rest;
        }

        // A number with a fixed count of decimals and thousands separators.
        std::string formatFixed(double value, int decimals) {
            std::array<char, 64> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%.*f", decimals, value);
            return groupThousands(buffer.data());
        }

        // The visible cell range along one axis and the steps for it.
        struct Axis {
            double from = 0.0; // First visible cell (fractional).
            double to = 0.0; // Last visible cell (fractional).
            std::uint64_t limit = 0; // Largest cell that gets a tick.
            std::uint64_t step = 1; // Cells between labelled ticks.
        };

        // The visible cells of an axis that are multiples of step (k * step, k >= 0, up to the limit).
        std::vector<std::uint64_t> multiples(const Axis& axis, std::uint64_t step) {
            std::vector<std::uint64_t> cells;
            const double end = std::min(axis.to, static_cast<double>(axis.limit));
            if (step == 0 || end < 0.0 || end < axis.from) {
                return cells;
            }
            const auto stepValue = static_cast<double>(step);
            const auto firstIndex = static_cast<std::uint64_t>(std::max(0.0, std::ceil(axis.from / stepValue)));
            const auto lastIndex = static_cast<std::uint64_t>(std::floor(end / stepValue));
            for (std::uint64_t index = firstIndex; index <= lastIndex; ++index) {
                cells.push_back(index * step);
            }
            return cells;
        }

        // Minor tick step for a labelled step: a quarter of it, or every cell for small steps, or none.
        std::uint64_t minorStep(std::uint64_t step) {
            if (step >= minorPerMajor) {
                return step / minorPerMajor;
            }
            return step > 1 ? 1 : 0;
        }

        // Fills the minor ticks of an axis (those not on a labelled tick); pixelOf turns a cell into a pixel.
        template <typename PixelOf>
        std::vector<double> minorTicks(const Axis& axis, PixelOf pixelOf) {
            std::vector<double> pixels;
            for (const std::uint64_t cell : multiples(axis, minorStep(axis.step))) {
                if (cell % axis.step != 0) {
                    pixels.push_back(pixelOf(cell));
                }
            }
            return pixels;
        }

        // Whether the input can produce ticks at all.
        bool usable(const RulerInput& input) {
            return input.world != nullptr && input.world->clusters() > 0 && input.view.scale > 0.0 &&
                input.bytesPerCluster > 0;
        }

        // The "% of disk" label of a byte offset, or empty when the placement is unknown.
        std::string diskLabel(const RulerInput& input, std::uint64_t bytes, const OffsetScale& scale) {
            if (!input.disk || input.disk->diskBytes == 0) {
                return {};
            }
            const auto disk = static_cast<double>(input.disk->diskBytes);
            const double percent = static_cast<double>(input.disk->partitionOffset + bytes) / disk * 100.0;
            return "disk " + formatPercent(percent, static_cast<double>(scale.step) / disk * 100.0);
        }

    } // namespace

    std::uint64_t tickStep(double minCells) {
        constexpr double largest = 4.0e18; // Keeps the power of two inside 64 bits.
        if (!(minCells > 1.0)) {
            return 1;
        }
        return std::bit_ceil(static_cast<std::uint64_t>(std::ceil(std::min(minCells, largest))));
    }

    std::size_t offsetUnit(const OffsetScale& scale) {
        const auto stepBytes = static_cast<double>(std::max<std::uint64_t>(scale.step, 1));
        const std::uint64_t largest = scale.largest;
        std::size_t unit = 0;
        while (unit + 1 < unitNames.size() && unitBytes(unit + 1) <= static_cast<double>(largest)) {
            ++unit;
        }
        // Too fine a step for this unit: a smaller unit keeps the label short.
        while (unit > 0 && unitBytes(unit) / stepBytes > std::pow(10.0, maxOffsetDecimals)) {
            --unit;
        }
        // A step this unit cannot write in fewer than all the decimals (0.6875 TB) is often exact one unit down
        // (704 GB); take it while the numbers stay short.
        const bool inexact = exactDecimals(stepBytes / unitBytes(unit)) == maxOffsetDecimals;
        if (unit > 0 && inexact && static_cast<double>(largest) / unitBytes(unit - 1) < maxSmallerUnitValue) {
            --unit;
        }
        return unit;
    }

    std::string formatOffset(std::uint64_t bytes, const OffsetScale& scale) {
        const auto stepBytes = static_cast<double>(std::max<std::uint64_t>(scale.step, 1));
        const std::size_t unit = offsetUnit(scale);
        // offsetUnit() keeps step / unit >= 10^-maxOffsetDecimals, so these decimals also tell neighbouring
        // offsets apart.
        const int decimals = exactDecimals(stepBytes / unitBytes(unit));
        return formatFixed(static_cast<double>(bytes) / unitBytes(unit), unit == 0 ? 0 : decimals) + " " +
            std::string(unitNames[unit]);
    }

    std::string formatPercent(double percent, double step) { return formatFixed(percent, percentDecimals(step)) + "%"; }

    RulerTicks rowTicks(const RulerInput& input) {
        RulerTicks ticks;
        if (!usable(input)) {
            return ticks;
        }
        const World& world = *input.world;
        const Viewport& view = input.view;
        ticks.step = tickStep(input.minSpacing * view.scale);
        const Axis axis{.from = view.worldAt({0, 0}).y,
                        .to = view.worldAt({0, view.size.y}).y,
                        .limit = world.rows() - 1,
                        .step = ticks.step};
        const OffsetScale scale{.largest = world.clusters() * input.bytesPerCluster,
                                .step = ticks.step * world.rowWidth() * input.bytesPerCluster};
        const double stepPercent =
            static_cast<double>(ticks.step * world.rowWidth()) / static_cast<double>(world.clusters()) * 100.0;
        const auto pixelOf = [&view](std::uint64_t row) { return view.pixelAt({0, static_cast<double>(row)}).y; };
        for (const std::uint64_t row : multiples(axis, axis.step)) {
            RulerTick tick;
            tick.pixel = pixelOf(row);
            tick.cell = row;
            const std::uint64_t lcn = row * world.rowWidth();
            tick.bytes = lcn * input.bytesPerCluster;
            tick.percent = static_cast<double>(lcn) / static_cast<double>(world.clusters()) * 100.0;
            tick.bytesText = formatOffset(tick.bytes, scale);
            tick.percentText = formatPercent(tick.percent, stepPercent);
            tick.diskText = diskLabel(input, tick.bytes, scale);
            ticks.major.push_back(std::move(tick));
        }
        ticks.minor = minorTicks(axis, pixelOf);
        return ticks;
    }

    RulerTicks columnTicks(const RulerInput& input) {
        RulerTicks ticks;
        if (!usable(input)) {
            return ticks;
        }
        const World& world = *input.world;
        const Viewport& view = input.view;
        ticks.step = tickStep(input.minSpacing * view.scale);
        const Axis axis{.from = view.worldAt({0, 0}).x,
                        .to = view.worldAt({view.size.x, 0}).x,
                        .limit = world.rowWidth(),
                        .step = ticks.step};
        const OffsetScale scale{.largest = world.rowWidth() * input.bytesPerCluster,
                                .step = ticks.step * input.bytesPerCluster};
        const auto pixelOf = [&view](std::uint64_t column) { return view.pixelAt({static_cast<double>(column), 0}).x; };
        for (const std::uint64_t column : multiples(axis, axis.step)) {
            RulerTick tick;
            tick.pixel = pixelOf(column);
            tick.cell = column;
            tick.bytes = column * input.bytesPerCluster;
            tick.bytesText = "+" + formatOffset(tick.bytes, scale);
            ticks.major.push_back(std::move(tick));
        }
        ticks.minor = minorTicks(axis, pixelOf);
        return ticks;
    }

} // namespace icd::core
