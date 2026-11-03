#include "ui/map/MapSelection.h"

#include "core/map/HitTest.h"
#include "core/map/MapData.h"
#include "core/map/Selection.h"
#include "ui/map/MapCanvas.h"
#include "ui/map/MapNavigator.h"

#include <wx/event.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace icd::ui {

    namespace {

        // How far (DIP) the pointer may move between press and release for a click rather than a drag.
        constexpr double clickSlop = 4.0;

        // A window pixel from a mouse event.
        core::PixelPoint pointerOf(const wxMouseEvent& event) {
            return {static_cast<double>(event.GetX()), static_cast<double>(event.GetY())};
        }

    } // namespace

    MapSelection::MapSelection(MapCanvas& canvas) : canvas_(canvas) {
        canvas_.Bind(wxEVT_LEFT_DOWN, &MapSelection::onLeftDown, this);
        canvas_.Bind(wxEVT_LEFT_UP, &MapSelection::onLeftUp, this);
    }

    std::optional<core::FileIndex> MapSelection::file() const {
        return file_ == core::noFile ? std::nullopt : std::optional(file_);
    }

    void MapSelection::onLeftDown(wxMouseEvent& event) {
        event.Skip(); // The navigator starts a drag from the same press.
        const core::PixelPoint pointer = pointerOf(event);
        press_.reset();
        if (canvas_.hasMap() && !canvas_.minimap().contains(pointer)) {
            press_ = pointer;
        }
    }

    void MapSelection::onLeftUp(wxMouseEvent& event) {
        event.Skip();
        if (!press_) {
            return;
        }
        const core::PixelPoint start = *press_;
        const core::PixelPoint end = pointerOf(event);
        press_.reset();
        const double slop = clickSlop * canvas_.GetDPIScaleFactor();
        if (std::abs(end.x - start.x) <= slop && std::abs(end.y - start.y) <= slop) {
            clickAt(end);
        }
    }

    void MapSelection::clickAt(core::PixelPoint pointer) {
        const core::MapData* map = canvas_.map();
        if (map == nullptr) {
            return;
        }
        const auto hit = core::hitTest(map->snapshot(), map->world(), canvas_.viewport(), pointer);
        if (!hit || !hit->record) {
            clear(); // Free space or off the volume.
            return;
        }
        const core::ExtentRecord& record = map->snapshot().extents.records()[*hit->record];
        select(record.file(), hit->fragment);
    }

    void MapSelection::select(core::FileIndex file, const core::FragmentPosition& fragment) {
        const core::MapData* map = canvas_.map();
        if (map == nullptr) {
            return;
        }
        if (file != file_) {
            const core::ExtentIndex& extents = map->snapshot().extents;
            file_ = file;
            fragments_ = core::fileFragments(extents, file);
            canvas_.setSelection(std::make_shared<const core::SelectionOverlay>(core::selectionOf(extents, file)));
        }
        const std::size_t index = fragment.index > 0 ? fragment.index - 1 : 0;
        current_ = std::min(index, std::max<std::size_t>(fragments_.size(), 1) - 1);
        notify();
    }

    std::optional<core::FileIndex> MapSelection::selectAt(core::PixelPoint pointer) {
        if (!canvas_.minimap().contains(pointer)) {
            clickAt(pointer);
        }
        return file();
    }

    void MapSelection::goTo(core::FileIndex file) {
        select(file, {.index = 1, .count = 0});
        showCurrent();
    }

    void MapSelection::clear() {
        if (file_ == core::noFile) {
            return;
        }
        reset();
        canvas_.setSelection(nullptr);
    }

    void MapSelection::reset() {
        file_ = core::noFile;
        fragments_.clear();
        current_ = 0;
        press_.reset();
        notify();
    }

    void MapSelection::refresh() {
        const core::MapData* map = canvas_.map();
        if (file_ == core::noFile) {
            return;
        }
        if (map == nullptr || file_ >= map->snapshot().files.size()) {
            clear();
            return;
        }
        const core::ExtentIndex& extents = map->snapshot().extents;
        fragments_ = core::fileFragments(extents, file_);
        current_ = std::min(current_, std::max<std::size_t>(fragments_.size(), 1) - 1);
        canvas_.setSelection(std::make_shared<const core::SelectionOverlay>(core::selectionOf(extents, file_)));
        notify();
    }

    bool MapSelection::nextFragment() { return step(1); }

    bool MapSelection::previousFragment() { return step(-1); }

    bool MapSelection::step(std::ptrdiff_t delta) {
        if (fragments_.empty()) {
            return false;
        }
        const auto count = static_cast<std::ptrdiff_t>(fragments_.size());
        current_ = static_cast<std::size_t>(((static_cast<std::ptrdiff_t>(current_) + delta) % count + count) % count);
        showCurrent();
        notify();
        return true;
    }

    void MapSelection::showCurrent() {
        const core::MapData* map = canvas_.map();
        if (map == nullptr || fragments_.empty()) {
            return;
        }
        canvas_.navigator().show(
            core::focusView(map->world(), canvas_.viewport(), fragments_.at(current_), canvas_.limits()));
    }

    void MapSelection::notify() const {
        if (observer_) {
            observer_();
        }
    }

} // namespace icd::ui
