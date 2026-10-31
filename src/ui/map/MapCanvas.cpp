#include "ui/map/MapCanvas.h"

#include "core/exec/Executor.h"
#include "core/log/Log.h"
#include "core/map/Selection.h"
#include "core/map/TileRasterizer.h"
#include "ui/Accelerators.h"
#include "ui/map/MapHover.h"
#include "ui/map/MapNavigator.h"
#include "ui/map/MapSelection.h"

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

        // Bytes one cached tile takes on the GPU.
        constexpr std::size_t tileBytes = core::tilePixels * sizeof(core::Rgba);

        // Tiles rendering at once; more wait for the next frame, so a fast zoom does not flood the pool with
        // tiles of levels it has already left.
        constexpr std::size_t maxPending = 96;

        // Tiles uploaded per frame; a burst of finished tiles would otherwise stall one frame.
        constexpr std::size_t uploadsPerFrame = 16;

        // How many coarser levels to search for a fallback tile.
        constexpr int fallbackLevels = 4;

        // Hands a rendered tile to the canvas, if it still exists.
        void post(const std::shared_ptr<TileMailbox>& mailbox, RenderStamp stamp, core::TileId tile,
                  std::vector<core::Rgba> pixels) {
            const std::scoped_lock lock(mailbox->mutex);
            if (mailbox->target == nullptr) {
                return;
            }
            MapCanvas* canvas = mailbox->target;
            canvas->CallAfter([canvas, stamp, tile, pixels = std::move(pixels)]() mutable {
                canvas->deliver(stamp, tile, std::move(pixels));
            });
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

        // Milliseconds since a time.
        double millisecondsSince(core::AnimationClock::time_point start) {
            return std::chrono::duration<double, std::milli>(core::AnimationClock::now() - start).count();
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
        Bind(wxEVT_CHAR, &MapCanvas::onChar, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            SetFocus(); // Map keys work once the map was clicked.
            event.Skip();
        });
        navigator_ = std::make_unique<MapNavigator>(*this);
        selection_ = std::make_unique<MapSelection>(*this);
        hover_ = std::make_unique<MapHover>(*this);
    }

    MapCanvas::~MapCanvas() {
        navigator_->stop();
        const std::scoped_lock lock(mailbox_->mutex);
        mailbox_->target = nullptr;
    }

    void MapCanvas::setMap(std::shared_ptr<const core::MapData> map) {
        navigator_->stop();
        hover_->hide();
        selection_->reset();
        overlay_.reset();
        map_ = std::move(map);
        resetTiles();
        fitted_ = false;
        if (const auto fitted = fittedView(); fitted) {
            view_ = *fitted;
            fitted_ = true;
        }
        layoutMinimap();
        Refresh(false);
        notifyView();
    }

    void MapCanvas::setPalette(std::shared_ptr<const core::Palette> palette) {
        palette_ = std::move(palette);
        resetTiles();
        Refresh(false);
    }

    void MapCanvas::setSelection(std::shared_ptr<const core::SelectionOverlay> overlay) {
        const std::shared_ptr<const core::SelectionOverlay> previous = std::exchange(overlay_, std::move(overlay));
        ++selectionVersion_;
        if (map_ == nullptr) {
            return;
        }
        const core::World& world = map_->world();
        const auto touched = [&world](const core::SelectionOverlay* selection, core::TileId tile) {
            return selection != nullptr && core::runsTouch(world, selection->runs, world.tileCells(tile));
        };
        // Tiles rendering now were drawn with the previous selection too, so they are checked like cached ones.
        std::vector<core::TileId> candidates = cache_.keys();
        candidates.insert(candidates.end(), pending_.begin(), pending_.end());
        for (const core::TileId tile : candidates) {
            if (touched(previous.get(), tile) || touched(overlay_.get(), tile)) {
                dirty_[tile] = selectionVersion_;
            }
        }
        Refresh(false);
    }

    void MapCanvas::setViewport(const core::Viewport& view) {
        view_ = map_ != nullptr ? core::clampView(view, limits()) : view;
        Refresh(false);
        notifyView();
    }

    void MapCanvas::notifyView() const {
        if (viewObserver_) {
            viewObserver_();
        }
    }

    void MapCanvas::showMinimap(bool shown) {
        minimap_.setShown(shown);
        layoutMinimap();
        Refresh(false);
    }

    void MapCanvas::layoutMinimap() {
        minimap_.layout(map_ != nullptr ? &map_->world() : nullptr, view_.size, GetDPIScaleFactor());
    }

    core::ViewLimits MapCanvas::limits() const {
        const wxSize size = GetClientSize();
        if (map_ == nullptr) {
            return {};
        }
        return core::limitsFor(map_->world(), {static_cast<double>(size.x), static_cast<double>(size.y)});
    }

    std::optional<core::Viewport> MapCanvas::fittedView() const {
        const wxSize size = GetClientSize();
        if (map_ == nullptr || size.x <= 0 || size.y <= 0) {
            return std::nullopt;
        }
        return core::fitView(map_->world(), {static_cast<double>(size.x), static_cast<double>(size.y)});
    }

    void MapCanvas::onSize(wxSizeEvent& event) {
        const wxSize size = GetClientSize();
        view_.size = {static_cast<double>(size.x), static_cast<double>(size.y)};
        if (!fitted_) {
            if (const auto fitted = fittedView(); fitted) {
                view_ = *fitted;
                fitted_ = true;
            }
        }
        layoutMinimap();
        Refresh(false);
        notifyView();
        event.Skip();
    }

    void MapCanvas::onChar(wxKeyEvent& event) {
        const auto key = mapKeyFor(event);
        if (!key) {
            event.Skip();
            return;
        }
        if (const auto command = commandForMapKey(*key); command) {
            wxCommandEvent menuEvent(wxEVT_MENU, *command);
            menuEvent.SetEventObject(this);
            wxPostEvent(this, menuEvent); // Queued copy; travels up to the frame like the menu command.
            return;
        }
        navigator_->panKey(*key);
    }

    void MapCanvas::resetTiles() {
        ++generation_;
        cache_.clear();
        pending_.clear();
        arrived_.clear();
        dirty_.clear();
        minimap_.resetImage();
    }

    void MapCanvas::uploadArrived() {
        for (std::size_t count = 0; count < uploadsPerFrame && !arrived_.empty(); ++count) {
            ArrivedTile& next = arrived_.front();
            if (GpuTileHandle uploaded = renderer_->upload(next.pixels); uploaded != nullptr) {
                storeTile(next.tile, next.selection, std::move(uploaded));
            }
            pending_.erase(next.tile);
            arrived_.pop_front();
        }
        if (!arrived_.empty()) {
            Refresh(false); // The rest go up in the next frames.
        }
    }

    void MapCanvas::storeTile(core::TileId tile, std::uint64_t selection, GpuTileHandle uploaded) {
        if (map_ != nullptr && tile == core::TileId{map_->world().maxLevel(), 0, 0}) {
            minimap_.setImage(uploaded); // The minimap shows the top tile, also after a selection change.
        }
        cache_.insert(tile, std::move(uploaded), tileBytes);
        if (const auto mark = dirty_.find(tile); mark != dirty_.end() && mark->second <= selection) {
            dirty_.erase(mark);
        }
    }

    void MapCanvas::beginFrameStats() {
        frameIntervals_.clear();
        paintTimes_.clear();
        lastFrame_.reset();
        recording_ = true;
    }

    void MapCanvas::endFrameStats() {
        recording_ = false;
        if (frameIntervals_.size() < 5) {
            return; // Too short to say anything.
        }
        std::vector<double> sorted = frameIntervals_;
        std::ranges::sort(sorted);
        core::logging::info("map frames",
                            {{"frames", sorted.size()},
                             {"interval_ms", mean(sorted)},
                             {"p95_ms", quantile(sorted, 0.95)},
                             {"max_ms", sorted.back()},
                             {"paint_ms", mean(paintTimes_)},
                             {"fps", 1000.0 / mean(sorted)}});
    }

    void MapCanvas::onPaint(wxPaintEvent& /*event*/) {
        const wxPaintDC dc(this); // Validates the update region; Direct2D draws the pixels.
        const auto start = core::AnimationClock::now();
        if (recording_ && lastFrame_) {
            frameIntervals_.push_back(std::chrono::duration<double, std::milli>(start - *lastFrame_).count());
        }
        if (renderer_ == nullptr) {
            renderer_ = std::make_unique<D2DRenderer>(GetHWND());
        }
        if (!renderer_->begin(GetClientSize())) {
            return;
        }
        uploadArrived();
        renderer_->clear(background);
        if (map_ != nullptr) {
            drawTiles();
            drawMinimap();
        }
        if (!renderer_->end()) {
            resetTiles(); // Device lost: tiles must be uploaded again.
            Refresh(false);
        }
        if (recording_) {
            paintTimes_.push_back(millisecondsSince(start));
            lastFrame_ = start;
        }
    }

    void MapCanvas::drawTiles() {
        const core::World& world = map_->world();
        const int level = world.levelFor(view_.scale);
        const double span = core::World::tileSize * core::World::cellsPerPixel(level); // Cells per tile edge.
        const double tilePixels = span / view_.scale; // Window pixels per tile edge.
        const core::WorldPoint topLeft = view_.worldAt({0, 0});
        const core::WorldPoint bottomRight = view_.worldAt(view_.size);
        const auto firstX = static_cast<std::int64_t>(std::max(0.0, std::floor(topLeft.x / span)));
        const auto firstY = static_cast<std::int64_t>(std::max(0.0, std::floor(topLeft.y / span)));
        const auto lastX = std::min(static_cast<std::int64_t>(world.tilesAcross(level)),
                                    static_cast<std::int64_t>(std::ceil(bottomRight.x / span)));
        const auto lastY = std::min(static_cast<std::int64_t>(world.tilesDown(level)),
                                    static_cast<std::int64_t>(std::ceil(bottomRight.y / span)));
        for (std::int64_t y = firstY; y < lastY; ++y) {
            for (std::int64_t x = firstX; x < lastX; ++x) {
                const core::TileId tile{level, static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(y)};
                const core::PixelPoint corner =
                    view_.pixelAt({static_cast<double>(x) * span, static_cast<double>(y) * span});
                const PixelRect destination{corner.x, corner.y, tilePixels, tilePixels};
                if (GpuTileHandle* cached = cache_.find(tile); cached != nullptr) {
                    drawCached(tile, *cached, destination);
                    continue;
                }
                request(tile);
                drawFallback(tile, destination);
            }
        }
    }

    void MapCanvas::drawCached(core::TileId tile, const GpuTileHandle& cached, const PixelRect& destination) {
        const PixelRect whole{0, 0, core::World::tileSize, core::World::tileSize};
        renderer_->draw(cached, destination, whole);
        if (dirty_.contains(tile)) {
            request(tile); // Shown as it is until the tile with the current selection arrives.
        }
    }

    void MapCanvas::drawMinimap() {
        const core::World& world = map_->world();
        const core::TileId top{world.maxLevel(), 0, 0}; // The whole world in one tile.
        if (minimap_.needsImage()) {
            if (GpuTileHandle* cached = cache_.find(top); cached != nullptr) {
                minimap_.setImage(*cached);
            }
            else {
                request(top);
            }
        }
        else if (dirty_.contains(top)) {
            request(top);
        }
        minimap_.draw(*renderer_, view_, world);
    }

    bool MapCanvas::drawFallback(core::TileId tile, const PixelRect& destination) {
        const int top = map_->world().maxLevel();
        for (int up = 1; up <= fallbackLevels && tile.level + up <= top; ++up) {
            const auto shift = static_cast<unsigned>(up);
            const core::TileId parent{tile.level + up, tile.x >> shift, tile.y >> shift};
            if (GpuTileHandle* cached = cache_.find(parent); cached != nullptr) {
                // The part of the parent tile that covers this tile.
                const double part = static_cast<double>(core::World::tileSize >> shift);
                const std::uint64_t mask = (std::uint64_t{1} << shift) - 1;
                const PixelRect source{static_cast<double>(tile.x & mask) * part,
                                       static_cast<double>(tile.y & mask) * part, part, part};
                renderer_->draw(*cached, destination, source);
                return true;
            }
        }
        return false;
    }

    void MapCanvas::request(core::TileId tile) {
        if (pending_.size() >= maxPending || pending_.contains(tile)) {
            return;
        }
        pending_.insert(tile);
        const RenderStamp stamp{.generation = generation_, .selection = selectionVersion_};
        executor_.submit([map = map_, palette = palette_, overlay = overlay_, mailbox = mailbox_, stamp, tile] {
            try {
                std::vector<core::Rgba> pixels = core::renderTile(map->source(), tile, *palette);
                if (overlay != nullptr) {
                    core::highlightRuns(map->world(), tile, overlay->runs, palette->selection(), pixels);
                }
                post(mailbox, stamp, tile, std::move(pixels));
            }
            catch (const std::exception& error) {
                core::logging::error("tile render failed", {{"level", tile.level}, {"what", error.what()}});
            }
        });
    }

    void MapCanvas::deliver(RenderStamp stamp, core::TileId tile, std::vector<core::Rgba>&& pixels) {
        if (stamp.generation != generation_) {
            return; // Rendered for an earlier map, palette or device.
        }
        arrived_.push_back(
            {tile, stamp.selection, std::move(pixels)}); // Still pending until uploaded, so it is not requested again.
        Refresh(false);
    }

} // namespace icd::ui
