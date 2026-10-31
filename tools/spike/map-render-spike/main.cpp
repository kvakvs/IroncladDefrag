// map-render-spike: measures how fast map tiles can be composed at 4K during continuous zoom and pan, with
// three backends (plan.md 7.3, task T026):
//   wxd2d  - wxWidgets' Direct2D renderer (wxGraphicsRenderer::GetDirect2DRenderer)
//   d2d    - Direct2D directly on the window (ID2D1HwndRenderTarget)
//   gdi    - GDI StretchDIBits, the CPU fallback
// Tiles come from a synthetic 30 TB volume and are rendered once in a warm-up pass, so the timed passes
// measure composition only. The canvas is a 3840 x 2160 child window: child windows are not limited to the
// screen size, so every backend renders a full 4K target even on smaller screens.
//
// Usage: map-render-spike [--tb N] [--seconds N] [--backend all|wxd2d|d2d|gdi]

#include "core/map/Pyramid.h"
#include "core/map/TileRasterizer.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <wx/app.h>
#include <wx/brush.h>
#include <wx/frame.h>
#include <wx/graphics.h>
#include <wx/image.h>

#include <d2d1.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#pragma comment(lib, "d2d1.lib")

namespace {

    using namespace icd::core;
    using Clock = std::chrono::steady_clock;

    constexpr int targetWidth = 3840; // 4K canvas.
    constexpr int targetHeight = 2160;

    // Command-line settings.
    struct Args {
        double terabytes = 30.0; // Synthetic volume size.
        double seconds = 6.0; // Length of one timed animation pass.
        std::string backend = "all"; // Which backends to run.
    };

    // Parses "--name value" pairs.
    Args parse(int argc, wchar_t** argv) {
        Args args;
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring name = argv[i];
            const std::wstring value = argv[i + 1];
            if (name == L"--tb") {
                args.terabytes = std::stod(value);
            }
            else if (name == L"--seconds") {
                args.seconds = std::stod(value);
            }
            else if (name == L"--backend") {
                args.backend = std::string(value.begin(), value.end());
            }
        }
        return args;
    }

    // Tile cache key.
    using TileKey = std::tuple<int, std::uint64_t, std::uint64_t>;

    // One tile to draw: which tile and where on the target (pixels, may be fractional and partly off-target).
    struct Placement {
        TileKey key; // Tile.
        double x = 0, y = 0, size = 0; // Destination square.
    };

    // The view at time t of the scripted animation: zoom from the whole volume to 8 px per cluster at the
    // middle, with a slow sideways pan, there and back.
    struct View {
        double scale = 1.0; // Cells per pixel.
        double left = 0.0, top = 0.0; // World cell at the target's top-left corner.
    };

    View viewAt(const World& world, double t) {
        const double fit = std::max(static_cast<double>(world.rowWidth()) / targetWidth,
                                    static_cast<double>(world.rows()) / targetHeight);
        const double phase = 0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * t); // 0 -> 1 -> 0.
        const double scale = std::exp(std::log(fit) + (std::log(1.0 / 8.0) - std::log(fit)) * phase);
        const double centerX = static_cast<double>(world.rowWidth()) * (0.45 + 0.1 * t);
        const double centerY = static_cast<double>(world.rows()) * 0.4;
        return {scale, centerX - targetWidth / 2.0 * scale, centerY - targetHeight / 2.0 * scale};
    }

    // Tiles covering the target for a view, at the nearest finer level.
    std::vector<Placement> placements(const World& world, const View& view) {
        const int level = world.levelFor(view.scale);
        const double span = World::tileSize * World::cellsPerPixel(level); // Cells per tile edge.
        const double size = span / view.scale; // Pixels per tile edge on the target.
        std::vector<Placement> out;
        const auto firstX = static_cast<std::int64_t>(std::floor(view.left / span));
        const auto firstY = static_cast<std::int64_t>(std::floor(view.top / span));
        for (std::int64_t ty = std::max<std::int64_t>(0, firstY);
             ty < static_cast<std::int64_t>(world.tilesDown(level)) &&
             (ty * span - view.top) / view.scale < targetHeight;
             ++ty) {
            for (std::int64_t tx = std::max<std::int64_t>(0, firstX);
                 tx < static_cast<std::int64_t>(world.tilesAcross(level)) &&
                 (tx * span - view.left) / view.scale < targetWidth;
                 ++tx) {
                out.push_back({{level, static_cast<std::uint64_t>(tx), static_cast<std::uint64_t>(ty)},
                               (tx * span - view.left) / view.scale,
                               (ty * span - view.top) / view.scale,
                               size});
            }
        }
        return out;
    }

    // Frame time statistics of one pass.
    struct Stats {
        std::vector<double> frames; // Milliseconds per frame.
        double wall = 0; // Seconds for the whole pass.
        std::size_t tilesDrawn = 0; // Tile draws over the pass.

        // Prints a summary line.
        void print(const char* name) {
            std::sort(frames.begin(), frames.end());
            double sum = 0;
            for (const double f : frames) {
                sum += f;
            }
            const auto at = [&](double q) {
                return frames.empty() ? 0.0 : frames[static_cast<std::size_t>(q * (frames.size() - 1))];
            };
            std::printf("%-6s %5zu frames  avg %6.2f ms  p50 %6.2f  p95 %6.2f  max %6.2f  sustained %6.1f fps  "
                        "(%.0f tiles/frame)\n",
                        name, frames.size(), frames.empty() ? 0.0 : sum / frames.size(), at(0.5), at(0.95),
                        frames.empty() ? 0.0 : frames.back(), frames.size() / wall,
                        frames.empty() ? 0.0 : static_cast<double>(tilesDrawn) / frames.size());
            std::fflush(stdout);
        }
    };

    // A drawing backend.
    class Backend {
    public:
        virtual ~Backend() = default;
        // Draws one frame of placements and presents it.
        virtual void frame(const std::vector<Placement>& tiles) = 0;
    };

    // Shared CPU tile pixels.
    using TilePixels = std::map<TileKey, std::vector<Rgba>>;

    // GDI: StretchDIBits per tile (CPU scaling).
    class GdiBackend : public Backend {
    public:
        GdiBackend(HWND window, const TilePixels& pixels) : window_(window), pixels_(pixels) {}

        void frame(const std::vector<Placement>& tiles) override {
            HDC dc = GetDC(window_);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = World::tileSize;
            info.bmiHeader.biHeight = -World::tileSize;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            for (const Placement& p : tiles) {
                const auto found = pixels_.find(p.key);
                if (found == pixels_.end()) {
                    continue;
                }
                const auto& data = found->second;
                StretchDIBits(dc, static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
                              static_cast<int>(std::ceil(p.size)), static_cast<int>(std::ceil(p.size)), 0, 0,
                              World::tileSize, World::tileSize, data.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            }
            GdiFlush();
            ReleaseDC(window_, dc);
        }

    private:
        HWND window_;
        const TilePixels& pixels_;
    };

    // Direct2D on the window: one HWND render target, one GPU bitmap per tile.
    class D2dBackend : public Backend {
    public:
        D2dBackend(HWND window, const TilePixels& pixels) {
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
            const auto props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_HARDWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96.0F, 96.0F);
            factory_->CreateHwndRenderTarget(props,
                                             D2D1::HwndRenderTargetProperties(window,
                                                                              D2D1::SizeU(targetWidth, targetHeight),
                                                                              D2D1_PRESENT_OPTIONS_IMMEDIATELY),
                                             &target_);
            const auto bitmapProps =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            for (const auto& [key, data] : pixels) {
                ID2D1Bitmap* bitmap = nullptr;
                target_->CreateBitmap(D2D1::SizeU(World::tileSize, World::tileSize), data.data(), World::tileSize * 4,
                                      bitmapProps, &bitmap);
                bitmaps_[key] = bitmap;
            }
        }

        ~D2dBackend() override {
            for (auto& [key, bitmap] : bitmaps_) {
                bitmap->Release();
            }
            target_->Release();
            factory_->Release();
        }

        void frame(const std::vector<Placement>& tiles) override {
            target_->BeginDraw();
            target_->Clear(D2D1::ColorF(0.78F, 0.78F, 0.78F));
            for (const Placement& p : tiles) {
                const auto rect = D2D1::RectF(static_cast<float>(p.x), static_cast<float>(p.y),
                                              static_cast<float>(p.x + p.size), static_cast<float>(p.y + p.size));
                if (const auto found = bitmaps_.find(p.key); found != bitmaps_.end()) {
                    target_->DrawBitmap(found->second, rect, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                }
            }
            target_->EndDraw();
        }

    private:
        ID2D1Factory* factory_ = nullptr;
        ID2D1HwndRenderTarget* target_ = nullptr;
        std::map<TileKey, ID2D1Bitmap*> bitmaps_;
    };

    // wxWidgets' Direct2D renderer: a graphics context per frame, wxGraphicsBitmap per tile.
    class WxD2dBackend : public Backend {
    public:
        WxD2dBackend(wxWindow* window, const TilePixels& pixels) :
            window_(window), renderer_(wxGraphicsRenderer::GetDirect2DRenderer()) {
            for (const auto& [key, data] : pixels) {
                wxImage image(World::tileSize, World::tileSize, false);
                unsigned char* rgb = image.GetData();
                for (std::size_t i = 0; i < data.size(); ++i) {
                    rgb[3 * i] = static_cast<unsigned char>(data[i] >> 16U);
                    rgb[3 * i + 1] = static_cast<unsigned char>(data[i] >> 8U);
                    rgb[3 * i + 2] = static_cast<unsigned char>(data[i]);
                }
                bitmaps_.emplace(key, renderer_->CreateBitmapFromImage(image));
            }
        }

        void frame(const std::vector<Placement>& tiles) override {
            std::unique_ptr<wxGraphicsContext> gc(renderer_->CreateContext(window_));
            gc->SetInterpolationQuality(wxINTERPOLATION_NONE);
            gc->SetBrush(wxBrush(wxColour(200, 200, 200)));
            gc->DrawRectangle(0, 0, targetWidth, targetHeight);
            for (const Placement& p : tiles) {
                if (const auto found = bitmaps_.find(p.key); found != bitmaps_.end()) {
                    gc->DrawBitmap(found->second, p.x, p.y, p.size, p.size);
                }
            }
            gc->Flush();
        }

    private:
        wxWindow* window_;
        wxGraphicsRenderer* renderer_;
        std::map<TileKey, wxGraphicsBitmap> bitmaps_;
    };

    // Runs the animation for seconds and times every frame.
    Stats run(Backend& backend, const World& world, double seconds) {
        Stats stats;
        const auto start = Clock::now();
        while (true) {
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            if (elapsed >= seconds) {
                stats.wall = elapsed;
                break;
            }
            const auto tiles = placements(world, viewAt(world, elapsed / seconds));
            const auto frameStart = Clock::now();
            backend.frame(tiles);
            stats.frames.push_back(std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
            stats.tilesDrawn += tiles.size();
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                DispatchMessageW(&message);
            }
        }
        return stats;
    }

    // Renders every tile the animation needs, sampling the path finely.
    TilePixels warmUp(const MapSource& source, icd::services::ThreadPool& pool) {
        std::map<TileKey, int> wanted;
        for (int step = 0; step <= 20000; ++step) {
            for (const Placement& p : placements(source.world, viewAt(source.world, step / 20000.0))) {
                wanted[p.key] = 0;
            }
        }
        std::vector<TileKey> keys;
        for (const auto& [key, unused] : wanted) {
            keys.push_back(key);
        }
        std::vector<std::vector<Rgba>> rendered(keys.size());
        const OccupancyPalette palette;
        const auto start = Clock::now();
        pool.parallelFor({.count = keys.size(), .grain = 1}, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const auto [level, x, y] = keys[i];
                rendered[i] = renderTile(source, {level, x, y}, palette);
            }
        });
        std::printf("warm-up: %zu tiles in %.2f s on %zu threads\n", keys.size(),
                    std::chrono::duration<double>(Clock::now() - start).count(), pool.concurrency());
        TilePixels pixels;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            pixels.emplace(keys[i], std::move(rendered[i]));
        }
        return pixels;
    }

} // namespace

// The spike's wx application: everything happens in OnInit, then the app quits.
class SpikeApp : public wxApp {
public:
    // Lets exceptions reach wmain instead of wx's message box.
    bool OnExceptionInMainLoop() override { throw; }

    // Same for exceptions thrown before the main loop.
    void OnUnhandledException() override { throw; }

    bool OnInit() override {
        int argc = wxApp::argc;
        std::vector<std::wstring> storage;
        std::vector<wchar_t*> argv;
        for (int i = 0; i < argc; ++i) {
            storage.emplace_back(wxApp::argv[i].wc_str());
        }
        for (auto& s : storage) {
            argv.push_back(s.data());
        }
        const Args args = parse(argc, argv.data());

        SynthOptions options;
        options.files = static_cast<std::uint32_t>(20'000'000 * args.terabytes / 30.0);
        options.fragmentedShare = 0.3;
        options.maxFragments = 40;
        options.geometry.totalClusters = static_cast<ClusterCount>(args.terabytes * (1ULL << 40U) / 4096);
        std::printf("building %.0f TB synthetic volume...\n", args.terabytes);
        std::fflush(stdout);
        const Snapshot snapshot = generateSyntheticVolume(options);
        const World world(snapshot.geometry.totalClusters, {16, 9});
        icd::services::ThreadPool pool(std::max(2U, std::thread::hardware_concurrency()) - 1);
        const Pyramid pyramid = Pyramid::build({.snapshot = snapshot, .world = world}, pool);
        const auto flags = extentMapFlags(snapshot, pool);
        const MapSource source{
            .world = world, .pyramid = pyramid, .facts = {.snapshot = snapshot, .extentFlags = flags}};
        const TilePixels pixels = warmUp(source, pool);

        auto* frame = new wxFrame(nullptr, wxID_ANY, "map-render-spike", wxPoint(0, 0), wxSize(1600, 900));
        auto* canvas = new wxWindow(frame, wxID_ANY, wxPoint(0, 0), wxSize(targetWidth, targetHeight));
        canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
        frame->Show();
        canvas->SetSize(targetWidth, targetHeight);
        wxYield();
        std::printf("canvas %d x %d, %.0f s per pass\n", canvas->GetClientSize().x, canvas->GetClientSize().y,
                    args.seconds);

        const auto want = [&](const char* name) { return args.backend == "all" || args.backend == name; };
        if (want("d2d")) {
            D2dBackend backend(canvas->GetHWND(), pixels);
            run(backend, world, 1.0); // Settle drivers.
            run(backend, world, args.seconds).print("d2d");
        }
        if (want("wxd2d")) {
            WxD2dBackend backend(canvas, pixels);
            run(backend, world, 1.0);
            run(backend, world, args.seconds).print("wxd2d");
        }
        if (want("gdi")) {
            GdiBackend backend(canvas->GetHWND(), pixels);
            run(backend, world, 1.0);
            run(backend, world, args.seconds).print("gdi");
        }
        frame->Destroy();
        return true;
    }

    // All work happened in OnInit; skip the main loop and exit with 0.
    int OnRun() override { return 0; }
};

wxIMPLEMENT_APP_NO_MAIN(SpikeApp);

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        return wxEntry(argc, argv);
    }
    catch (const std::exception& error) {
        std::printf("failed: %s\n", error.what()); // Print instead of wx's modal dialog.
        return 1;
    }
}
