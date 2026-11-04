#include "core/map/HitTest.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/map/ColorModes.h"
#include "core/map/TileRasterizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>

namespace icd::core {

    namespace {

        // FILETIME ticks: 100 ns.
        using FileTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;

        // FILETIME of the Unix epoch (1970-01-01), where std::chrono::system_clock starts.
        constexpr std::int64_t unixEpochFileTime = 116'444'736'000'000'000;

        // A file flag and how the tooltip names it.
        struct FlagName {
            FileFlag flag; // The bit.
            std::string_view name; // Its tooltip word.
        };

        // Flags worth a word in the tooltip, in display order (directory and metadata become the type).
        constexpr std::array flagNames{
            FlagName{FileCompressed, "compressed"}, FlagName{FileSparse, "sparse"},
            FlagName{FileEncrypted, "encrypted"},   FlagName{FileReparsePoint, "reparse point"},
            FlagName{FileHardLinked, "hard links"}, FlagName{FileAlternateStreams, "named streams"},
            FlagName{FileResident, "resident"},     FlagName{FileUnmovable, "unmovable"},
        };

        // A stretch of world coordinates along one axis: what one pixel spans.
        struct WorldSpan {
            double from = 0.0; // Where the pixel starts.
            double to = 0.0; // Where it ends.
        };

        // The cells [floor(from), ceil(to)) of a span, at least one cell, clipped to [0, limit). Returns
        // {first, count}; count 0 when it lies outside.
        std::pair<std::uint64_t, std::uint64_t> cellSpan(WorldSpan span, std::uint64_t limit) {
            const double first = std::floor(span.from);
            const double last = std::max(first + 1.0, std::ceil(span.to));
            const double clippedFirst = std::max(first, 0.0);
            const double clippedLast = std::min(last, static_cast<double>(limit));
            if (clippedLast <= clippedFirst) {
                return {0, 0};
            }
            return {static_cast<std::uint64_t>(clippedFirst), static_cast<std::uint64_t>(clippedLast - clippedFirst)};
        }

        // "file, compressed, unmovable": the kind from the flags, then the notable flags.
        std::string typeText(const FileTable& files, FileIndex file) {
            std::string line;
            if (files.has(file, FileMetadata)) {
                line += "NTFS metadata";
            }
            else if (files.has(file, FileDirectory)) {
                line += "directory";
            }
            else {
                line += "file";
            }
            for (const FlagName& entry : flagNames) {
                if (files.has(file, entry.flag)) {
                    line += ", ";
                    line += entry.name;
                }
            }
            return line;
        }

        // "Type: file, compressed, unmovable" for the tooltip.
        std::string typeLine(const FileTable& files, FileIndex file) { return "Type: " + typeText(files, file); }

        // The name of a catalog class value, as the class legend shows it.
        std::string_view className(std::uint8_t kind) {
            const auto legend = legendOf(ColorMode::FileClass);
            return legend[legendCategory(ColorMode::FileClass, PieceOwner{.kind = kind})].label;
        }

        // "Stream: main data" for the record's stream.
        std::string streamLine(const ExtentRecord& record) {
            if ((record.flags() & ExtentDirectoryIndex) != 0) {
                return "Stream: directory index";
            }
            return record.stream() == 0 ? std::string("Stream: main data")
                                        : std::format("Stream: #{}", record.stream());
        }

        // Bytes of a cluster count.
        std::uint64_t bytesOf(const Snapshot& snapshot, ClusterCount clusters) {
            return clusters * snapshot.geometry.bytesPerCluster;
        }

        // Lines about the file owning a record.
        void describeOwner(const Snapshot& snapshot, const MapHit& hit, const std::chrono::time_zone* zone,
                           std::vector<std::string>& lines) {
            const ExtentRecord& record = snapshot.extents.records()[*hit.record];
            const FileIndex file = record.file();
            const FileTable& files = snapshot.files;
            const std::string path = snapshot.path(file);
            lines.push_back(path.empty() ? std::string("\\") : path);
            lines.push_back(streamLine(record));
            lines.push_back(std::format("Fragment {} of {}; this extent {} clusters ({})", hit.fragment.index,
                                        hit.fragment.count, formatCount(record.length()),
                                        formatBytes(bytesOf(snapshot, record.length()))));
            lines.push_back(std::format("Size {}; allocated {}", formatBytes(files.sizeBytes(file)),
                                        formatBytes(bytesOf(snapshot, files.allocatedClusters(file)))));
            lines.push_back(typeLine(files, file));
            const FileTimes& times = files.times(file);
            lines.push_back(std::format("Modified {}; created {}; accessed {}", formatFileTime(times.modified, zone),
                                        formatFileTime(times.created, zone), formatFileTime(times.accessed, zone)));
        }

        // "Class: Photos" after the type line, when classes are known.
        void addClassLine(const MapHit& hit, std::span<const std::uint8_t> extentKinds,
                          std::vector<std::string>& lines) {
            if (!hit.record || *hit.record >= extentKinds.size()) {
                return;
            }
            lines.insert(lines.end() - 1,
                         std::format("Class: {}", className(extentKinds[*hit.record]))); // Before times.
        }

        // The line about the free run holding the cluster.
        std::string freeLine(const Snapshot& snapshot, const MapHit& hit) {
            if (!hit.freeRun) {
                return "Free";
            }
            return std::format("Free: run of {} clusters ({}) from LCN {}", formatCount(hit.freeRun->length),
                               formatBytes(bytesOf(snapshot, hit.freeRun->length)), formatCount(hit.freeRun->start));
        }

    } // namespace

    FragmentPosition fragmentOf(const ExtentIndex& extents, std::size_t record) {
        FragmentPosition position;
        const auto records = extents.records();
        if (record >= records.size()) {
            return position;
        }
        Lcn previousEnd = 0;
        bool first = true;
        for (const std::uint32_t at : extents.extentsOf(records[record].file())) {
            const ExtentRecord& piece = records[at];
            if (first || piece.lcn() != previousEnd) {
                ++position.count;
            }
            if (at == record) {
                position.index = position.count;
            }
            previousEnd = piece.end();
            first = false;
        }
        return position;
    }

    ClusterCount usedClustersInRect(const Snapshot& snapshot, const World& world, CellRect rect) {
        ClusterCount used = 0;
        const std::uint64_t right = std::min(rect.x + rect.width, world.rowWidth());
        const std::uint64_t bottom = std::min(rect.y + rect.height, world.rows());
        for (std::uint64_t row = rect.y; row < bottom && rect.x < right; ++row) {
            const Lcn start = row * world.rowWidth() + rect.x;
            const Lcn end = std::min(row * world.rowWidth() + right, world.clusters());
            if (start >= end) {
                continue;
            }
            for (const ExtentRecord& record : snapshot.extents.overlapping({start, end - start})) {
                used += std::min(record.end(), end) - std::max(record.lcn(), start);
            }
        }
        return used;
    }

    CellRect pixelCells(const World& world, const Viewport& view, PixelPoint point) {
        const PixelPoint corner{std::floor(point.x), std::floor(point.y)};
        const WorldPoint from = view.worldAt(corner);
        const WorldPoint to = view.worldAt({corner.x + 1.0, corner.y + 1.0});
        const auto [x, width] = cellSpan({.from = from.x, .to = to.x}, world.rowWidth());
        const auto [y, height] = cellSpan({.from = from.y, .to = to.y}, world.rows());
        if (width == 0 || height == 0) {
            return {};
        }
        return {.x = x, .y = y, .width = width, .height = height};
    }

    std::optional<MapHit> hitTest(const Snapshot& snapshot, const World& world, const Viewport& view,
                                  PixelPoint pointer) {
        const WorldPoint point = view.worldAt(pointer);
        const bool inside = point.x >= 0.0 && point.y >= 0.0 && point.x < static_cast<double>(world.rowWidth()) &&
            point.y < static_cast<double>(world.rows());
        if (!inside) {
            return std::nullopt; // Also keeps the casts below in range.
        }
        const CellPoint cell{static_cast<std::uint64_t>(point.x), static_cast<std::uint64_t>(point.y)};
        const std::optional<Lcn> lcn = world.lcnAt(cell);
        if (!lcn) {
            return std::nullopt;
        }
        MapHit hit;
        hit.cell = cell;
        hit.lcn = *lcn;
        if (const ExtentRecord* record = snapshot.extents.find(*lcn); record != nullptr) {
            hit.record = static_cast<std::size_t>(record - snapshot.extents.records().data());
            hit.fragment = fragmentOf(snapshot.extents, *hit.record);
        }
        else {
            hit.freeRun = snapshot.freeSpace.find(*lcn);
        }
        hit.pixel.cells = pixelCells(world, view, pointer);
        hit.pixel.clusters = clustersInRect(world, hit.pixel.cells);
        hit.pixel.used = usedClustersInRect(snapshot, world, hit.pixel.cells);
        return hit;
    }

    std::string formatFileTime(std::int64_t fileTime, const std::chrono::time_zone* zone) {
        if (fileTime <= 0) {
            return "-";
        }
        const std::chrono::sys_time<FileTicks> utc{FileTicks{fileTime - unixEpochFileTime}};
        const auto minutes = std::chrono::floor<std::chrono::minutes>(utc);
        if (zone == nullptr) {
            return std::format("{:%Y-%m-%d %H:%M} UTC", minutes);
        }
        return std::format("{:%Y-%m-%d %H:%M}", zone->to_local(minutes));
    }

    std::vector<FileFact> describeFile(const Snapshot& snapshot, FileIndex file, const std::chrono::time_zone* zone,
                                       std::span<const std::uint8_t> fileClasses) {
        const FileTable& files = snapshot.files;
        const std::size_t fragments = snapshot.extents.fragmentCount(file);
        std::vector<FileFact> facts{
            {"Size", formatBytes(files.sizeBytes(file))},
            {"Allocated", formatBytes(bytesOf(snapshot, files.allocatedClusters(file)))},
            {"Fragments", fragments == 0 ? std::string("none (data in the MFT record)") : formatCount(fragments)},
            {"Type", typeText(files, file)},
        };
        if (file < fileClasses.size()) {
            facts.push_back({"Class", std::string(className(fileClasses[file]))});
        }
        const FileTimes& times = files.times(file);
        facts.push_back({"Modified", formatFileTime(times.modified, zone)});
        facts.push_back({"Created", formatFileTime(times.created, zone)});
        facts.push_back({"Accessed", formatFileTime(times.accessed, zone)});
        return facts;
    }

    std::vector<std::string> describeHit(const Snapshot& snapshot, const MapHit& hit,
                                         const std::chrono::time_zone* zone,
                                         std::span<const std::uint8_t> extentKinds) {
        std::vector<std::string> lines;
        lines.push_back(
            std::format("LCN {}; byte offset {}", formatCount(hit.lcn), formatCount(bytesOf(snapshot, hit.lcn))));
        if (hit.record) {
            describeOwner(snapshot, hit, zone, lines);
            addClassLine(hit, extentKinds, lines);
        }
        else {
            lines.push_back(freeLine(snapshot, hit));
        }
        if (hit.pixel.clusters > 1) {
            const double share = static_cast<double>(hit.pixel.used) / static_cast<double>(hit.pixel.clusters);
            lines.push_back(
                std::format("This pixel: {} clusters, {:.0f}% used", formatCount(hit.pixel.clusters), share * 100.0));
        }
        return lines;
    }

} // namespace icd::core
