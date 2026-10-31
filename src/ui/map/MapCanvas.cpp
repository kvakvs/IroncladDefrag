#include "ui/map/MapCanvas.h"

#include "core/exec/Executor.h"
#include "core/log/Log.h"
#include "core/map/TileRasterizer.h"
#include "ui/Accelerators.h"

#include <wx/dcclient.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <mutex>
#include <utility>

namespace icd::ui {

    // Where workers send finished tiles: the canvas while it lives, nobody after it is destroyed. CallAfter only
    // queues an event on the canvas, and a destroyed window drops its queued events, so a tile finishing late
    // never reaches a deleted window.
    struct TileMailbox {
        std::mutex mutex; // Guards target.
        MapCanvas* target = nullptr; // Receiver; null after the canvas is gone.
    };

    namespace {

        // Background where there is no map or no tile yet.
        constexpr core::Rgba background = core::rgb(200, 200, 200);

        // Margin around the volume when fitted, as a share of the window.
        constexpr double fitMargin = 1.04;

        // Bytes one cached tile takes on the GPU.
        constexpr std::size_t tileBytes = core::tilePixels * sizeof(core::Rgba);

        // Hands a rendered tile to the canvas, if it still exists.
        void post(const std::shared_ptr<TileMailbox>& mailbox, std::uint64_t generation, core::TileId tile,
                  std::vector<core::Rgba> pixels) {
            const std::scoped_lock lock(mailbox->mutex);
            if (mailbox->target == nullptr) {
                return;
            }
            MapCanvas* canvas = mailbox->target;
            canvas->CallAfter(
                [canvas, generation, tile, pixels = std::move(pixels)] { canvas->deliver(generation, tile, pixels); });
        }

    } // namespace

    MapCanvas::MapCanvas(wxWindow* parent, core::Executor& executor) :
        wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS),
        executor_(executor), mailbox_(std::make_shared<TileMailbox>()),
        palette_(std::make_shared<core::OccupancyPalette>()) {
        mailbox_->target = this;
        SetBackgroundStyle(wxBG_STYLE_PAINT); // Direct2D paints every pixel; no erase, no flicker.
        Bind(wxEVT_PAINT, &MapCanvas::onPaint, this);
        Bind(wxEVT_SIZE, &MapCanvas::onSize, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            SetFocus(); // Map keys work once the map was clicked.
            event.Skip();
        });
        Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
            const auto key = mapKeyFor(event);
            const auto command = key ? commandForMapKey(*key) : std::nullopt;
            if (!command) {
                event.Skip();
                return;
            }
            wxCommandEvent menuEvent(wxEVT_MENU, *command);
            menuEvent.SetEventObject(this);
            wxPostEvent(this, menuEvent); // Queued copy; travels up to the frame like the menu command.
        });
    }

    MapCanvas::~MapCanvas() {
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void MapCanvas::setMap(std::shared_ptr<const core::MapData> map) {
        map_ = std::move(map);
        resetTiles();
        fitted_ = false;
        fitToView();
        Refresh();
    }

    void MapCanvas::setPalette(std::shared_ptr<const core::Palette> palette) {
        palette_ = std::move(palette);
        resetTiles();
        Refresh();
    }

    void MapCanvas::fitToView() {
        const wxSize size = GetClientSize();
        if (map_ == nullptr || size.x <= 0 || size.y <= 0) {
            return;
        }
        const core::World& world = map_->world();
        view_.scale = fitMargin *
            std::max(static_cast<double>(world.rowWidth()) / size.x, static_cast<double>(world.rows()) / size.y);
        view_.centerX = static_cast<double>(world.rowWidth()) / 2.0;
        view_.centerY = static_cast<double>(world.rows()) / 2.0;
        fitted_ = true;
        Refresh();
    }

    void MapCanvas::onSize(wxSizeEvent& event) {
        if (!fitted_) {
            fitToView();
        }
        Refresh();
        event.Skip();
    }

    void MapCanvas::resetTiles() {
        ++generation_;
        cache_.clear();
        pending_.clear();
    }

    void MapCanvas::onPaint(wxPaintEvent& /*event*/) {
        const wxPaintDC dc(this); // Validates the update region; Direct2D draws the pixels.
        if (renderer_ == nullptr) {
            renderer_ = std::make_unique<D2DRenderer>(GetHWND());
        }
        const wxSize size = GetClientSize();
        if (!renderer_->begin(size)) {
            return;
        }
        renderer_->clear(background);
        if (map_ != nullptr) {
            drawTiles(size);
        }
        if (!renderer_->end()) {
            resetTiles(); // Device lost: tiles must be uploaded again.
            Refresh();
        }
    }

    void MapCanvas::drawTiles(wxSize size) {
        const core::World& world = map_->world();
        const int level = world.levelFor(view_.scale);
        const double span = core::World::tileSize * core::World::cellsPerPixel(level); // Cells per tile edge.
        const double tilePixels = span / view_.scale; // Window pixels per tile edge.
        const double left = view_.centerX - size.x / 2.0 * view_.scale;
        const double top = view_.centerY - size.y / 2.0 * view_.scale;
        const auto firstX = static_cast<std::int64_t>(std::max(0.0, std::floor(left / span)));
        const auto firstY = static_cast<std::int64_t>(std::max(0.0, std::floor(top / span)));
        const auto lastX = std::min(static_cast<std::int64_t>(world.tilesAcross(level)),
                                    static_cast<std::int64_t>(std::ceil((left + size.x * view_.scale) / span)));
        const auto lastY = std::min(static_cast<std::int64_t>(world.tilesDown(level)),
                                    static_cast<std::int64_t>(std::ceil((top + size.y * view_.scale) / span)));
        const PixelRect whole{0, 0, core::World::tileSize, core::World::tileSize};
        for (std::int64_t y = firstY; y < lastY; ++y) {
            for (std::int64_t x = firstX; x < lastX; ++x) {
                const core::TileId tile{level, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y)};
                if (GpuTileHandle* cached = cache_.find(tile); cached != nullptr) {
                    const PixelRect destination{(static_cast<double>(x) * span - left) / view_.scale,
                                                (static_cast<double>(y) * span - top) / view_.scale, tilePixels,
                                                tilePixels};
                    renderer_->draw(*cached, destination, whole);
                }
                else {
                    request(tile);
                }
            }
        }
    }

    void MapCanvas::request(core::TileId tile) {
        if (!pending_.insert(tile).second) {
            return; // Already rendering.
        }
        executor_.submit([map = map_, palette = palette_, mailbox = mailbox_, generation = generation_, tile] {
            try {
                post(mailbox, generation, tile, core::renderTile(map->source(), tile, *palette));
            }
            catch (const std::exception& error) {
                core::logging::error("tile render failed", {{"level", tile.level}, {"what", error.what()}});
            }
        });
    }

    void MapCanvas::deliver(std::uint64_t generation, core::TileId tile, const std::vector<core::Rgba>& pixels) {
        if (generation != generation_) {
            return; // Rendered for an earlier map, palette or device.
        }
        pending_.erase(tile);
        if (renderer_ == nullptr) {
            return;
        }
        if (GpuTileHandle uploaded = renderer_->upload(pixels); uploaded != nullptr) {
            cache_.insert(tile, std::move(uploaded), tileBytes);
            Refresh(false);
        }
    }

} // namespace icd::ui
