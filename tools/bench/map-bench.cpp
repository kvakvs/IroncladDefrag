// map-bench: checks the drive map's performance targets (plan.md 7.5) on a synthetic volume, the Phase 2 exit check.
// Builds the map data the way the app does (MapData::build), then measures:
//   - map data build and the pyramid on 8 threads (target 5 s), map memory (target 512 MB);
//   - tile render times at every zoom level in every color mode (coarse 5 ms, fine 2 ms);
//   - unless --no-window: the real MapCanvas in a window, driven through a scripted zoom from the whole volume to
//     8 px per cluster and back, and pans at several zooms (target 60 fps).
//
// Usage: map-bench [--tb N] [--files N] [--seconds S] [--width W] [--height H] [--no-window 1]

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/map/ColorModes.h"
#include "core/map/MapData.h"
#include "core/map/Pyramid.h"
#include "core/map/TileRasterizer.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"
#include "ui/map/MapCanvas.h"

#include <wx/app.h>
#include <wx/frame.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

    using namespace icd::core;

    // Command line options.
    struct Args {
        double terabytes = 30.0; // Volume size.
        std::uint32_t files = 20'000'000; // Regular files.
        double seconds = 6.0; // Length of each scripted window pass.
        int width = 1920; // Canvas width in pixels.
        int height = 1080; // Canvas height in pixels.
        bool window = true; // Whether to run the window passes.
    };

    // Parses "--name value" pairs; unknown names are ignored.
    Args parse(int argc, wchar_t** argv) {
        Args args;
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring name = argv[i];
            const wchar_t* value = argv[i + 1];
            if (name == L"--tb") {
                args.terabytes = std::wcstod(value, nullptr);
            }
            else if (name == L"--files") {
                args.files = static_cast<std::uint32_t>(std::wcstoul(value, nullptr, 10));
            }
            else if (name == L"--seconds") {
                args.seconds = std::wcstod(value, nullptr);
            }
            else if (name == L"--width") {
                args.width = static_cast<int>(std::wcstol(value, nullptr, 10));
            }
            else if (name == L"--height") {
                args.height = static_cast<int>(std::wcstol(value, nullptr, 10));
            }
            else if (name == L"--no-window") {
                args.window = std::wcstol(value, nullptr, 10) == 0;
            }
        }
        return args;
    }

    // Seconds since start.
    double since(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // Prints a check line and whether it met its target.
    void report(const char* what, double value, double target, const char* unit) {
        std::printf("%-50s %8.2f %-3s target %7.1f %-3s %s\n", what, value, unit, target, unit,
                    value <= target ? "ok" : "MISSED");
    }

    // The value at a quantile of sorted values.
    double quantile(const std::vector<double>& sorted, double q) {
        return sorted.empty() ? 0.0 : sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
    }

    // Mean of values.
    double mean(const std::vector<double>& values) {
        double sum = 0.0;
        for (const double value : values) {
            sum += value;
        }
        return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
    }

    // The 30 TB synthetic volume of plan.md 6.5, scaled by size.
    Snapshot buildSnapshot(const Args& args) {
        SynthOptions options;
        options.files = args.files;
        options.fragmentedShare = 0.3;
        options.maxFragments = 40;
        options.geometry.totalClusters =
            static_cast<ClusterCount>(args.terabytes * static_cast<double>(1ULL << 40U) / 4096.0);
        return generateSyntheticVolume(options);
    }

    // Times the pyramid build alone on 8 threads (the caller joins parallelFor, so 7 workers).
    void timePyramid(const MapData& map) {
        icd::services::ThreadPool eight(7);
        const FileColorValues values = fileColorValues(map.snapshot(), map.ageReference(), eight);
        const auto start = std::chrono::steady_clock::now();
        const Pyramid pyramid = Pyramid::build(
            {.snapshot = map.snapshot(), .world = map.world(), .fileHeat = values.heat, .fileSizes = values.sizes},
            eight);
        report("pyramid build on 8 threads", since(start), 5.0, "s");
    }

    // Tiles of a level that hold part of the volume, spread evenly, at most count of them.
    std::vector<TileId> sampleTiles(const World& world, int level, std::size_t count) {
        const std::uint64_t across = world.tilesAcross(level);
        const auto span = static_cast<std::uint64_t>(std::ceil(World::tileSize * World::cellsPerPixel(level)));
        const std::uint64_t down = std::min(world.tilesDown(level), (world.rows() + span - 1) / span);
        const std::uint64_t total = across * down;
        const std::uint64_t step = std::max<std::uint64_t>(1, total / count);
        std::vector<TileId> tiles;
        for (std::uint64_t index = step / 2; index < total && tiles.size() < count; index += step) {
            tiles.push_back({level, index % across, index / across});
        }
        return tiles;
    }

    // Times tiles of every level in every color mode, one thread, and prints the slowest mode per level.
    void timeTiles(const MapData& map) {
        const MapSource source = map.source();
        const World& world = map.world();
        for (int level = world.maxLevel(); level >= World::minLevel; --level) {
            const bool coarse = level >= map.pyramid().baseLevel();
            double worstMean = 0.0;
            double worstMedian = 0.0;
            double worstMax = 0.0;
            for (const ColorMode mode : {ColorMode::Fragmentation, ColorMode::Age, ColorMode::Size}) {
                const auto palette = paletteFor(mode);
                std::vector<double> times;
                for (const TileId tile : sampleTiles(world, level, 64)) {
                    const auto start = std::chrono::steady_clock::now();
                    const auto pixels = renderTile(source, tile, *palette);
                    times.push_back(1000.0 * since(start));
                    (void)pixels;
                }
                std::ranges::sort(times);
                worstMean = std::max(worstMean, mean(times));
                worstMedian = std::max(worstMedian, quantile(times, 0.5));
                worstMax = std::max(worstMax, times.empty() ? 0.0 : times.back());
            }
            const std::string what = std::format("{} tile, level {} (median {:.2f}, max {:.1f} ms)",
                                                 coarse ? "coarse" : "fine", level, worstMedian, worstMax);
            report(what.c_str(), worstMean, coarse ? 5.0 : 2.0, "ms");
        }
    }

    // Frame statistics of one scripted pass.
    struct PassStats {
        std::vector<double> intervals; // Milliseconds between frames.
        std::vector<double> paints; // Milliseconds spent painting each frame.
        std::size_t missingFrames = 0; // Frames that showed a coarser stand-in for some exact tile.
    };

    // A view along a scripted path at a share 0-1 of the pass.
    using Path = std::function<Viewport(double share)>;

    // Drives the canvas along path for seconds, painting as fast as the display allows.
    PassStats runPass(icd::ui::MapCanvas& canvas, const Path& path, double seconds) {
        PassStats stats;
        const auto start = std::chrono::steady_clock::now();
        auto last = start;
        for (double elapsed = 0.0; elapsed < seconds; elapsed = since(start)) {
            canvas.setViewport(path(elapsed / seconds));
            const auto paintStart = std::chrono::steady_clock::now();
            canvas.Update(); // Paints now; Direct2D presents in step with the display.
            const auto now = std::chrono::steady_clock::now();
            stats.paints.push_back(std::chrono::duration<double, std::milli>(now - paintStart).count());
            if (now != start) {
                stats.intervals.push_back(std::chrono::duration<double, std::milli>(now - last).count());
            }
            last = now;
            stats.missingFrames += canvas.missingTiles() > 0 ? 1 : 0;
            wxTheApp->Yield(true); // Tiles arrive through queued events.
        }
        return stats;
    }

    // Prints a pass and checks its frame rate.
    void printPass(const char* name, PassStats stats) {
        std::ranges::sort(stats.intervals);
        const double fps = 1000.0 / std::max(mean(stats.intervals), 1e-9);
        std::printf("%-12s %5zu frames  interval avg %5.2f ms  p95 %5.2f  max %6.2f  paint avg %5.2f ms  "
                    "frames with stand-in tiles %zu\n",
                    name, stats.intervals.size(), mean(stats.intervals), quantile(stats.intervals, 0.95),
                    stats.intervals.empty() ? 0.0 : stats.intervals.back(), mean(stats.paints), stats.missingFrames);
        const std::string what = std::string(name) + " frame rate";
        std::printf("%-50s %8.2f fps target %7.1f fps %s\n", what.c_str(), fps, 60.0, fps >= 59.0 ? "ok" : "MISSED");
    }

    // Runs the scripted window passes: zoom in and out, and pans at three zooms.
    void runWindow(const Args& args, std::shared_ptr<const MapData> map, Executor& executor) {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "map-bench", wxPoint(0, 0), wxDefaultSize);
        auto* canvas = new icd::ui::MapCanvas(frame, executor);
        frame->SetClientSize(args.width, args.height);
        frame->Show();
        canvas->SetSize(frame->GetClientSize());
        wxTheApp->Yield(true);
        canvas->setMap(map);
        const Viewport fit = *canvas->fittedView();
        const World& world = map->world();
        const WorldPoint middle{static_cast<double>(world.rowWidth()) / 2.0, static_cast<double>(world.rows()) / 2.0};
        std::printf("canvas %d x %d, %.0f s per pass\n", canvas->GetClientSize().x, canvas->GetClientSize().y,
                    args.seconds);
        runPass(*canvas, [&](double) { return fit; }, 1.0); // Settle: first tiles, driver warm-up.

        // Whole volume to 8 px per cluster and back, at a steady rate in log scale.
        const double deep = 1.0 / 8.0;
        printPass("zoom",
                  runPass(
                      *canvas,
                      [&](double share) {
                          const double t = share < 0.5 ? share * 2.0 : (1.0 - share) * 2.0;
                          Viewport view = fit;
                          view.scale = fit.scale * std::pow(deep / fit.scale, t);
                          view.center = {fit.center.x + (middle.x - fit.center.x) * t,
                                         fit.center.y + (middle.y - fit.center.y) * t};
                          return view;
                      },
                      args.seconds));

        // Pans across at 2,000 px/s at the base-level zoom, one cluster per pixel and 8 px per cluster.
        for (const double scale : {16.0, 1.0, deep}) {
            const std::string name =
                scale >= 1.0 ? "pan " + std::to_string(static_cast<int>(scale)) + " cl/px" : "pan 8 px/cl";
            printPass(name.c_str(),
                      runPass(
                          *canvas,
                          [&](double share) {
                              Viewport view = fit;
                              view.scale = scale;
                              view.center = {middle.x + (share - 0.5) * args.seconds * 2000.0 * scale, middle.y};
                              return view;
                          },
                          args.seconds));
        }
        frame->Destroy();
    }

    // The checks, in order: build, memory, tiles, window.
    void runChecks(const Args& args) {
        std::printf("building %.1f TB synthetic volume, %s files...\n", args.terabytes,
                    formatCount(args.files).c_str());
        std::fflush(stdout);
        Snapshot snapshot = buildSnapshot(args);
        const unsigned threads = std::max(2U, std::thread::hardware_concurrency());
        icd::services::ThreadPool pool(threads - 1);
        const auto start = std::chrono::steady_clock::now();
        const std::shared_ptr<const MapData> map =
            MapData::build(std::move(snapshot), {.width = 16, .height = 9}, pool);
        std::printf("map data built in %.2f s on %u threads; %s extents\n", since(start), threads,
                    formatCount(map->snapshot().extents.size()).c_str());
        timePyramid(*map);
        report("map memory (MB)", static_cast<double>(map->memoryBytes()) / (1 << 20U), 512.0, "MB");
        timeTiles(*map);
        if (args.window) {
            runWindow(args, map, pool);
        }
    }

} // namespace

// The bench's wx application: everything happens in OnInit, then the app quits.
class MapBenchApp : public wxApp {
public:
    // Lets exceptions reach wmain instead of wx's message box.
    bool OnExceptionInMainLoop() override { throw; }

    // Same for exceptions thrown before the main loop.
    void OnUnhandledException() override { throw; }

    // Runs the checks.
    bool OnInit() override {
        std::vector<std::wstring> storage;
        for (int i = 0; i < wxApp::argc; ++i) {
            storage.emplace_back(wxApp::argv[i].wc_str());
        }
        std::vector<wchar_t*> argv;
        for (std::wstring& arg : storage) {
            argv.push_back(arg.data());
        }
        runChecks(parse(static_cast<int>(argv.size()), argv.data()));
        return true;
    }

    // All work happened in OnInit; skip the main loop and exit with 0.
    int OnRun() override { return 0; }
};

wxIMPLEMENT_APP_NO_MAIN(MapBenchApp);

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Measure like the app in the foreground: a background console process gets power throttling (EcoQoS) and on
    // hybrid CPUs runs on efficiency cores, which roughly halves single-thread speed and makes timings swing.
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0; // Throttling off.
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    try {
        return wxEntry(argc, argv);
    }
    catch (const std::exception& error) {
        std::printf("failed: %s\n", error.what()); // Print instead of wx's modal dialog.
        return 1;
    }
}
