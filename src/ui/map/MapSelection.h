#pragma once

#include "core/map/Viewport.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class wxMouseEvent;

namespace icd::core {
    struct FragmentPosition;
}

namespace icd::ui {

    class MapCanvas;

    // The selected file of the map (plan.md 7.4): a click on used clusters selects their file and the canvas
    // highlights all its fragments; a click on free space clears it. Next/previous fragment and "go to file" move
    // the view to a fragment. Holds the fragments in file order and which one is current; the highlight itself is
    // a core::SelectionOverlay the canvas paints into tiles. UI thread only.
    class MapSelection {
    public:
        // Watches clicks on canvas, which owns the selection; always passes the events on.
        explicit MapSelection(MapCanvas& canvas);

        MapSelection(const MapSelection&) = delete;
        MapSelection& operator=(const MapSelection&) = delete;

        // Selects a file and moves the view to its first fragment ("go to file").
        void goTo(core::FileIndex file);

        // Clears the selection and its highlight.
        void clear();

        // Forgets the selection without touching the canvas (the canvas shows a new map).
        void reset();

        // Takes the selected file's fragments again from the canvas's map (newer data of the same volume, where file
        // indexes stay): the highlight follows a file that moved, the view stays.
        void refresh();

        // Moves the view to the next fragment, after the last one back to the first; false without fragments.
        bool nextFragment();

        // Moves the view to the previous fragment, before the first one to the last; false without fragments.
        bool previousFragment();

        // The selected file, if any.
        [[nodiscard]] std::optional<core::FileIndex> file() const;

        // The current fragment, counted from 0 (meaningless without fragments).
        [[nodiscard]] std::size_t fragment() const { return current_; }

        // Fragments of the selected file (0: none selected, or its data lives in the MFT record).
        [[nodiscard]] std::size_t fragmentCount() const { return fragments_.size(); }

        // Calls observer after every change of the file or the current fragment (the toolbar label); empty to stop.
        void setObserver(std::function<void()> observer) { observer_ = std::move(observer); }

    private:
        // Remembers where a press on the map (not the minimap) happened.
        void onLeftDown(wxMouseEvent& event);

        // A release close to the press is a click: select what lies there.
        void onLeftUp(wxMouseEvent& event);

        // Selects what lies under a window pixel, or clears on free space.
        void clickAt(core::PixelPoint pointer);

        // Makes file the selection (fragments, highlight), with fragment.index (from 1) current.
        void select(core::FileIndex file, const core::FragmentPosition& fragment);

        // Steps the current fragment by delta (wrapping) and shows it.
        bool step(std::ptrdiff_t delta);

        // Moves the view to the current fragment.
        void showCurrent();

        // Tells the observer.
        void notify() const;

        MapCanvas& canvas_; // The map.
        core::FileIndex file_ = core::noFile; // Selected file, noFile for none.
        std::vector<core::ClusterRun> fragments_; // Its fragments in file order.
        std::size_t current_ = 0; // Current fragment.
        std::optional<core::PixelPoint> press_; // Where the left button went down on the map, while held.
        std::function<void()> observer_; // Called on changes.
    };

} // namespace icd::ui
