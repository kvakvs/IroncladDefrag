#include "core/map/ColorModes.h"

#include "core/classify/Catalog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace icd::core {

    namespace {

        // FILETIME ticks in a day (100 ns each).
        constexpr std::int64_t ticksPerDay = 864'000'000'000LL;

        // Upper ends of the age buckets but the last: a day, a week, a month, a year, three years.
        constexpr std::array<std::int64_t, valueBuckets - 1> ageLimits{ticksPerDay, ticksPerDay * 7, ticksPerDay * 30,
                                                                       ticksPerDay * 365, ticksPerDay * 365 * 3};

        // The smallest size bucket ends at 64 KB; each further one is 16 times larger.
        constexpr std::uint64_t firstSizeLimit = std::uint64_t{64} << 10U;

        // Pieces the legend count is split into, so a full-width view of a large volume still runs in parallel.
        constexpr ClusterCount legendPieceClusters = ClusterCount{1} << 22U;

        // Pieces per parallel chunk of the legend count.
        constexpr std::size_t legendPiecesPerChunk = 8;

        // Legend of the fragmentation mode: the occupancy palette's colors.
        constexpr std::array fragmentationLegend{
            LegendEntry{"Free", OccupancyPalette::freeColor, "free"},
            LegendEntry{"Contiguous", OccupancyPalette::usedColor, "fragmentation.contiguous"},
            LegendEntry{"Fragmented", OccupancyPalette::fragmentedColor, "fragmentation.fragmented"},
            LegendEntry{"Unmovable", OccupancyPalette::unmovableColor, "fragmentation.unmovable"},
        };

        // Legend of the age mode, newest (warm) to oldest (cool).
        constexpr std::array ageLegend{
            LegendEntry{"Free", OccupancyPalette::freeColor, "free"},
            LegendEntry{"Under a day", rgb(214, 48, 49), "age.0"},
            LegendEntry{"Under a week", rgb(238, 130, 38), "age.1"},
            LegendEntry{"Under a month", rgb(222, 186, 40), "age.2"},
            LegendEntry{"Under a year", rgb(96, 172, 110), "age.3"},
            LegendEntry{"Under 3 years", rgb(60, 124, 196), "age.4"},
            LegendEntry{"3 years or more", rgb(88, 64, 150), "age.5"},
        };

        // Legend of the size mode, small (light) to large (dark).
        constexpr std::array sizeLegend{
            LegendEntry{"Free", OccupancyPalette::freeColor, "free"},
            LegendEntry{"Under 64 KB", rgb(140, 200, 236), "size.0"},
            LegendEntry{"64 KB - 1 MB", rgb(84, 160, 220), "size.1"},
            LegendEntry{"1 - 16 MB", rgb(48, 116, 196), "size.2"},
            LegendEntry{"16 - 256 MB", rgb(40, 78, 164), "size.3"},
            LegendEntry{"256 MB - 4 GB", rgb(44, 44, 122), "size.4"},
            LegendEntry{"4 GB or more", rgb(34, 18, 70), "size.5"},
        };

        static_assert(ageLegend.size() == valueBuckets + 1 && sizeLegend.size() == valueBuckets + 1,
                      "one legend entry per bucket plus free space");

        // The legend entry of a per-bin value: age values are hot (new) high, listed newest first.
        std::size_t entryOfValue(ColorMode mode, std::uint8_t value) {
            const std::size_t bucket = std::min(bucketOfValue(value), valueBuckets - 1);
            return mode == ColorMode::Age ? valueBuckets - bucket : bucket + 1;
        }

        // Colors a bin by its average age or size value, blended with free space by how full it is.
        class BucketPalette : public Palette {
        public:
            // A palette for the age or the size mode.
            BucketPalette(ColorMode mode, const ColorScheme& scheme) :
                mode_(mode), colors_(legendColors(mode, scheme)), outside_(scheme.outside()) {}

            // The bucket color of the bin's value.
            [[nodiscard]] Rgba color(const PixelSummary& summary) const override {
                const MapBin& bin = summary.bin;
                if (bin.used == 0) {
                    return colors_.front();
                }
                const auto value = static_cast<std::uint8_t>(mode_ == ColorMode::Age ? bin.heat : bin.size);
                return blendColors(
                    {.from = colors_.front(), .to = colors_[entryOfValue(mode_, value)], .share = usedShare(summary)});
            }

            // The scheme's background past the volume.
            [[nodiscard]] Rgba outside() const override { return outside_; }

        private:
            ColorMode mode_; // Age or size.
            std::vector<Rgba> colors_; // Legend colors in the scheme.
            Rgba outside_; // Past the volume.
        };

        // Legend of the class mode: free space, then every class of the built-in catalog in its color, keyed
        // "class.<id>" (the keys are kept here for the program's life, like the catalog the labels point into).
        std::span<const LegendEntry> classLegend() {
            static const std::vector<std::string> keys = [] {
                std::vector<std::string> out;
                for (const classify::ClassInfo& info : classify::builtinCatalog().classes) {
                    out.push_back("class." + info.id);
                }
                return out;
            }();
            static const std::vector<LegendEntry> legend = [] {
                std::vector<LegendEntry> out{LegendEntry{"Free", OccupancyPalette::freeColor, "free"}};
                const auto& classes = classify::builtinCatalog().classes;
                for (std::size_t index = 0; index < classes.size(); ++index) {
                    const classify::ClassInfo& info = classes[index];
                    const auto channel = [&](unsigned shift) { return static_cast<std::uint8_t>(info.color >> shift); };
                    out.push_back({info.name, rgb(channel(16), channel(8), channel(0)), keys[index]});
                }
                return out;
            }();
            return legend;
        }

        // The legend entry of a class (classes past the legend fall back to the first class, "Unknown").
        std::size_t entryOfClass(std::uint8_t kind) {
            const std::size_t entry = std::size_t{kind} + 1;
            return entry < classLegend().size() ? entry : 1;
        }

        // Colors a bin by the class of its largest contributor, blended with free space by how full it is.
        class ClassPalette : public Palette {
        public:
            // The class colors of a scheme.
            explicit ClassPalette(const ColorScheme& scheme) :
                colors_(legendColors(ColorMode::FileClass, scheme)), outside_(scheme.outside()) {}

            // The class color of the bin's dominant class.
            [[nodiscard]] Rgba color(const PixelSummary& summary) const override {
                if (summary.bin.used == 0) {
                    return colors_.front();
                }
                return blendColors({.from = colors_.front(),
                                    .to = colors_[entryOfClass(summary.bin.kind)],
                                    .share = usedShare(summary)});
            }

            // The scheme's background past the volume.
            [[nodiscard]] Rgba outside() const override { return outside_; }

        private:
            std::vector<Rgba> colors_; // Legend colors in the scheme.
            Rgba outside_; // Past the volume.
        };

        // The LCN ranges of the clusters inside a cell rectangle, cut into pieces of at most legendPieceClusters.
        std::vector<ClusterRun> piecesOf(const World& world, CellRect cells) {
            std::vector<ClusterRun> rows;
            const std::uint64_t width = world.rowWidth();
            if (cells.x == 0 && cells.width == width) {
                rows.push_back({cells.y * width, cells.height * width}); // Whole rows: one range.
            }
            else {
                for (std::uint64_t y = cells.y; y < cells.y + cells.height; ++y) {
                    rows.push_back({y * width + cells.x, cells.width});
                }
            }
            std::vector<ClusterRun> pieces;
            for (const ClusterRun row : rows) {
                const Lcn end = std::min(row.end(), world.clusters());
                for (Lcn start = row.start; start < end; start += legendPieceClusters) {
                    pieces.push_back({start, std::min(legendPieceClusters, end - start)});
                }
            }
            return pieces;
        }

        // What one legend count reads.
        struct LegendInput {
            const MapFileFacts& facts; // Extents and owner facts.
            ColorMode mode; // Which legend.
        };

        // Adds the clusters of one LCN piece to counts.
        void countPiece(const LegendInput& input, ClusterRun piece, LegendCounts& counts) {
            const ExtentRecord* const records = input.facts.snapshot.extents.records().data();
            ClusterCount used = 0;
            for (const ExtentRecord& record : input.facts.snapshot.extents.overlapping(piece)) {
                const ClusterCount inside = std::min(record.end(), piece.end()) - std::max(record.lcn(), piece.start);
                const auto index = static_cast<std::size_t>(&record - records);
                counts.clusters[legendCategory(input.mode, ownerOf(index, input.facts))] += inside;
                used += inside;
            }
            counts.clusters.front() += piece.length - used;
            counts.total += piece.length;
        }

        // Sums two partial counts.
        LegendCounts combined(LegendCounts into, const LegendCounts& part) {
            for (std::size_t entry = 0; entry < into.clusters.size(); ++entry) {
                into.clusters[entry] += part.clusters[entry];
            }
            into.total += part.total;
            return into;
        }

        // The world coordinates one window axis shows.
        struct AxisWindow {
            double from = 0.0; // At the window's first pixel.
            double to = 0.0; // At its far edge.
        };

        // The cells [first, last) of an axis of size cells that a window shows.
        std::pair<std::uint64_t, std::uint64_t> visibleSpan(AxisWindow window, std::uint64_t size) {
            const auto limit = static_cast<double>(size);
            const double first = std::clamp(std::floor(window.from), 0.0, limit);
            const double last = std::clamp(std::ceil(window.to), first, limit);
            return {static_cast<std::uint64_t>(first), static_cast<std::uint64_t>(last)};
        }

    } // namespace

    std::size_t ageBucket(std::int64_t modified, std::int64_t reference) {
        const std::int64_t age = reference - modified;
        const auto above = std::ranges::count_if(ageLimits, [age](std::int64_t limit) { return age >= limit; });
        return static_cast<std::size_t>(above);
    }

    std::size_t sizeBucket(std::uint64_t bytes) {
        std::size_t bucket = 0;
        std::uint64_t limit = firstSizeLimit;
        while (bucket < valueBuckets - 1 && bytes >= limit) {
            ++bucket;
            limit <<= 4U;
        }
        return bucket;
    }

    std::int64_t newestModification(const Snapshot& snapshot, Executor& executor, const CancelToken& token) {
        const FileTable& files = snapshot.files;
        constexpr std::int64_t none = std::numeric_limits<std::int64_t>::min();
        const std::int64_t newest = parallelReduce(
            executor, {.count = files.size(), .grain = 65536}, none,
            [&files](std::size_t begin, std::size_t end) {
                std::int64_t local = none;
                for (std::size_t file = begin; file < end; ++file) {
                    local = std::max(local, files.times(static_cast<FileIndex>(file)).modified);
                }
                return local;
            },
            [](std::int64_t a, std::int64_t b) { return std::max(a, b); }, token);
        return newest == none ? 0 : newest;
    }

    FileColorValues fileColorValues(const Snapshot& snapshot, std::int64_t reference, Executor& executor,
                                    const CancelToken& token) {
        const FileTable& files = snapshot.files;
        FileColorValues values{.heat = std::vector<std::uint8_t>(files.size()),
                               .sizes = std::vector<std::uint8_t>(files.size())};
        executor.parallelFor(
            {.count = files.size(), .grain = 65536},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t file = begin; file < end; ++file) {
                    const auto index = static_cast<FileIndex>(file);
                    const std::size_t age = ageBucket(files.times(index).modified, reference);
                    values.heat[file] = bucketValue(valueBuckets - 1 - age);
                    values.sizes[file] = bucketValue(sizeBucket(files.sizeBytes(index)));
                }
            },
            token);
        return values;
    }

    std::vector<std::uint8_t> extentColorValues(const Snapshot& snapshot, const FileColorValues& values,
                                                Executor& executor, const CancelToken& token) {
        const auto records = snapshot.extents.records();
        std::vector<std::uint8_t> out(records.size(), 0);
        executor.parallelFor(
            {.count = out.size(), .grain = 65536},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t index = begin; index < end; ++index) {
                    const FileIndex file = records[index].file();
                    const std::uint32_t heat = file < values.heat.size() ? values.heat[file] / bucketStep : 0;
                    const std::uint32_t size = file < values.sizes.size() ? values.sizes[file] / bucketStep : 0;
                    out[index] = packExtentValues(heat, size);
                }
            },
            token);
        return out;
    }

    std::span<const LegendEntry> legendOf(ColorMode mode) {
        switch (mode) {
        case ColorMode::Age:
            return ageLegend;
        case ColorMode::Size:
            return sizeLegend;
        case ColorMode::FileClass:
            return classLegend();
        case ColorMode::Fragmentation:
        case ColorMode::ZoneFit:
        case ColorMode::PlanPreview:
            break; // The overlay modes draw over the fragmentation colors and count them.
        }
        return fragmentationLegend;
    }

    std::vector<Rgba> legendColors(ColorMode mode, const ColorScheme& scheme) {
        std::vector<Rgba> colors;
        for (const LegendEntry& entry : legendOf(mode)) {
            colors.push_back(scheme.color(entry.key));
        }
        return colors;
    }

    std::shared_ptr<const Palette> paletteFor(ColorMode mode, const ColorScheme& scheme) {
        if (mode == ColorMode::Fragmentation || mode == ColorMode::ZoneFit || mode == ColorMode::PlanPreview) {
            // Overlay modes paint their runs over this base.
            return std::make_shared<OccupancyPalette>(
                OccupancyColors{.free = scheme.color("free"),
                                .used = scheme.color("fragmentation.contiguous"),
                                .fragmented = scheme.color("fragmentation.fragmented"),
                                .unmovable = scheme.color("fragmentation.unmovable"),
                                .outside = scheme.outside()});
        }
        if (mode == ColorMode::FileClass) {
            return std::make_shared<ClassPalette>(scheme);
        }
        return std::make_shared<BucketPalette>(mode, scheme);
    }

    std::size_t legendCategory(ColorMode mode, const PieceOwner& owner) {
        switch (mode) {
        case ColorMode::Age:
            return entryOfValue(mode, owner.heat);
        case ColorMode::Size:
            return entryOfValue(mode, owner.size);
        case ColorMode::FileClass:
            return entryOfClass(owner.kind);
        case ColorMode::Fragmentation:
        case ColorMode::ZoneFit:
        case ColorMode::PlanPreview:
            break;
        }
        // The same precedence as the occupancy palette: unmovable over fragmented over contiguous.
        if ((owner.flags & BinUnmovable) != 0) {
            return 3;
        }
        return owner.fragmented ? 2 : 1;
    }

    CellRect visibleCells(const World& world, const Viewport& view) {
        const WorldPoint topLeft = view.worldAt({0.0, 0.0});
        const WorldPoint bottomRight = view.worldAt(view.size);
        const auto [x0, x1] = visibleSpan({.from = topLeft.x, .to = bottomRight.x}, world.rowWidth());
        const auto [y0, y1] = visibleSpan({.from = topLeft.y, .to = bottomRight.y}, world.rows());
        return {.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
    }

    LegendCounts countLegend(const World& world, const MapFileFacts& facts, ColorMode mode, CellRect cells,
                             Executor& executor, const CancelToken& token) {
        const std::vector<ClusterRun> pieces = piecesOf(world, cells);
        const LegendInput input{.facts = facts, .mode = mode};
        const LegendCounts empty{.clusters = std::vector<ClusterCount>(legendOf(mode).size(), 0), .total = 0};
        return parallelReduce(
            executor, {.count = pieces.size(), .grain = legendPiecesPerChunk}, empty,
            [&](std::size_t begin, std::size_t end) {
                LegendCounts part = empty;
                for (std::size_t piece = begin; piece < end; ++piece) {
                    countPiece(input, pieces[piece], part);
                }
                return part;
            },
            [](LegendCounts into, const LegendCounts& part) { return combined(std::move(into), part); }, token);
    }

} // namespace icd::core
