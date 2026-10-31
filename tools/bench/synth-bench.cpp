// synth-bench: builds a synthetic snapshot and its map pyramid and prints how long they took and how much
// memory they use, to check the plan.md 6.5 and 7.5 targets (30 TB / 20 M files within the 8 GB snapshot
// budget, pyramid in 5 s or less on 8 cores) on a real machine.
//
// Usage: synth-bench [--tb N] [--files N] [--seed N] [--fill PERCENT] [--fragmented SHARE] [--max-fragments N]
//                    [--dump DIR]   (DIR gets a few rendered tiles as .bmp files)

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/map/Pyramid.h"
#include "core/map/TileRasterizer.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace {

    // Command-line settings.
    struct Args {
        double terabytes = 30.0; // Volume size.
        std::uint32_t files = 20'000'000; // Regular files.
        std::uint64_t seed = 1; // Random seed.
        double fill = 60.0; // Used share in percent.
        double fragmented = 0.3; // Share of files split into fragments.
        std::uint32_t maxFragments = 40; // Most fragments per file; with 0.3 this gives about 60 M extents.
        std::string dumpDirectory; // Where to write sample tiles; empty for none.
    };

    // Parses "--name value" pairs; unknown names are ignored.
    Args parse(int argc, char** argv) {
        Args args;
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::string name = argv[i];
            const char* value = argv[i + 1];
            if (name == "--tb") {
                args.terabytes = std::strtod(value, nullptr);
            }
            else if (name == "--files") {
                args.files = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
            }
            else if (name == "--seed") {
                args.seed = std::strtoull(value, nullptr, 10);
            }
            else if (name == "--fill") {
                args.fill = std::strtod(value, nullptr);
            }
            else if (name == "--fragmented") {
                args.fragmented = std::strtod(value, nullptr);
            }
            else if (name == "--max-fragments") {
                args.maxFragments = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
            }
            else if (name == "--dump") {
                args.dumpDirectory = value;
            }
        }
        return args;
    }

    // Peak working set of this process so far.
    std::uint64_t peakWorkingSet() {
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0 ? counters.PeakWorkingSetSize
                                                                                           : 0;
    }

    // Seconds since start.
    double since(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // Writes a tile as a 32-bit top-down BMP.
    void writeBmp(const std::string& path, const std::vector<icd::core::Rgba>& pixels) {
        const auto edge = static_cast<std::int32_t>(icd::core::World::tileSize);
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(pixels.size() * sizeof(icd::core::Rgba));
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + dataBytes;
        info.biSize = sizeof(info);
        info.biWidth = edge;
        info.biHeight = -edge; // Top-down rows.
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file), sizeof(file));
        out.write(reinterpret_cast<const char*>(&info), sizeof(info));
        out.write(reinterpret_cast<const char*>(pixels.data()), dataBytes);
    }

    // Renders count tiles spread over a level and prints the average time per tile.
    void timeTiles(const icd::core::MapSource& source, int level, std::size_t count) {
        using namespace icd::core;
        const OccupancyPalette palette;
        const std::uint64_t across = source.world.tilesAcross(level);
        const std::uint64_t down = source.world.tilesDown(level);
        const std::uint64_t total = across * down;
        const std::uint64_t step = std::max<std::uint64_t>(1, total / count);
        std::size_t rendered = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t index = 0; index < total && rendered < count; index += step, ++rendered) {
            const auto pixels = renderTile(source, {level, index % across, index / across}, palette);
            (void)pixels;
        }
        const char* kind = level >= source.pyramid.baseLevel() ? "coarse" : "fine";
        std::printf("tiles level %3d %.2f ms per tile (%s, %zu tiles; target %s)\n", level,
                    1000.0 * since(start) / static_cast<double>(std::max<std::size_t>(rendered, 1)), kind, rendered,
                    level >= source.pyramid.baseLevel() ? "5 ms" : "2 ms");
    }

    // Writes sample tiles: the whole volume and a few zoomed-in ones near the middle.
    void dumpTiles(const icd::core::MapSource& source, const std::string& directory) {
        using namespace icd::core;
        const OccupancyPalette palette;
        for (const int level : {source.world.maxLevel(), source.pyramid.baseLevel() - 1, 0, World::minLevel}) {
            const TileId tile{level, source.world.tilesAcross(level) / 2, source.world.tilesDown(level) / 2};
            const std::string path = directory + "\\tile-level" + std::to_string(level) + ".bmp";
            writeBmp(path, renderTile(source, tile, palette));
            std::printf("wrote %s\n", path.c_str());
        }
    }

} // namespace

int main(int argc, char** argv) {
    using namespace icd::core;
    const Args args = parse(argc, argv);
    SynthOptions options;
    options.seed = args.seed;
    options.files = args.files;
    options.fillPercent = args.fill;
    options.fragmentedShare = args.fragmented;
    options.maxFragments = args.maxFragments;
    options.geometry.totalClusters = static_cast<ClusterCount>(args.terabytes * 1024.0 * 1024.0 * 1024.0 * 1024.0 /
                                                               options.geometry.bytesPerCluster);

    std::printf("Building %.1f TB, %s files, seed %llu...\n", args.terabytes, formatCount(args.files).c_str(),
                static_cast<unsigned long long>(args.seed));
    const auto start = std::chrono::steady_clock::now();
    const Snapshot snapshot = generateSyntheticVolume(options);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::size_t fragmented = 0;
    for (FileIndex file = 0; file < snapshot.files.size(); ++file) {
        fragmented += snapshot.extents.fragmentCount(file) > 1 ? 1 : 0;
    }
    std::printf("time            %.1f s\n", seconds);
    std::printf("files           %s (%s fragmented)\n", formatCount(snapshot.files.size()).c_str(),
                formatCount(fragmented).c_str());
    std::printf("extents         %s\n", formatCount(snapshot.extents.size()).c_str());
    std::printf("free runs       %s, %s free\n", formatCount(snapshot.freeSpace.size()).c_str(),
                formatBytes(snapshot.freeSpace.freeClusters() * snapshot.geometry.bytesPerCluster).c_str());
    std::printf("distinct names  %s\n", formatCount(snapshot.names.size()).c_str());
    std::printf("snapshot data   %s\n", formatBytes(snapshotMemoryBytes(snapshot)).c_str());
    std::printf("peak memory     %s (budget 8 GB)\n", formatBytes(peakWorkingSet()).c_str());

    const World world(snapshot.geometry.totalClusters, {.width = 16, .height = 9});
    // The caller joins parallelFor, so a pool of N - 1 workers uses N threads.
    const unsigned all = std::max(2U, std::thread::hardware_concurrency());
    std::optional<Pyramid> pyramid;
    for (const unsigned threads : {8U, all}) {
        icd::services::ThreadPool pool(threads - 1);
        const auto pyramidStart = std::chrono::steady_clock::now();
        pyramid = Pyramid::build({.snapshot = snapshot, .world = world}, pool);
        std::printf("pyramid         %.2f s on %u threads (target 5 s on 8), base level %d, %s\n", since(pyramidStart),
                    threads, pyramid->baseLevel(), formatBytes(pyramid->memoryBytes()).c_str());
    }

    icd::services::ThreadPool pool(all - 1);
    const auto extentFlags = extentMapFlags(snapshot, pool);
    const MapSource source{
        .world = world, .pyramid = *pyramid, .facts = {.snapshot = snapshot, .extentFlags = extentFlags}};
    std::printf("world           %s x %s cells, levels %d..%d\n", formatCount(world.rowWidth()).c_str(),
                formatCount(world.rows()).c_str(), World::minLevel, world.maxLevel());
    for (const int level : {world.maxLevel(), pyramid->baseLevel() + 1, pyramid->baseLevel(), pyramid->baseLevel() - 1,
                            0, -3, World::minLevel}) {
        timeTiles(source, level, 64);
    }
    if (!args.dumpDirectory.empty()) {
        dumpTiles(source, args.dumpDirectory);
    }
    return 0;
}
