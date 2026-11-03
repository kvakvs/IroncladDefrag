#pragma once

#include "core/job/CancelToken.h"
#include "core/map/ColorModes.h"
#include "core/map/Viewport.h"

#include <wx/panel.h>
#include <wx/timer.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class wxStaticText;
class wxWrapSizer;

namespace icd::core {
    class Executor;
    class MapData;
} // namespace icd::core

namespace icd::ui {

    struct LegendMailbox;

    // The map legend (plan.md 7.4): one swatch per color of the current mode with the size and share of the
    // visible area it covers. Counts are exact (core::countLegend()) and run on the pool once the view rests, so
    // panning never waits for them; a newer view cancels a running count. Entries wrap onto more lines in a
    // narrow window.
    class Legend : public wxPanel {
    public:
        // An empty legend; counts run on executor, which must outlive it.
        Legend(wxWindow* parent, core::Executor& executor);

        // Stops delivery of counts still running.
        ~Legend() override;

        Legend(const Legend&) = delete;
        Legend& operator=(const Legend&) = delete;

        // Shows the legend of a map (null: none) and counts its whole view once it gets one.
        void setMap(std::shared_ptr<const core::MapData> map);

        // Takes newer data of the same volume (an analysis filling in, moves during execution): the numbers shown stay
        // until the new count of the same view arrives, so they do not blink with every update. Another volume size is
        // like setMap().
        void updateMap(std::shared_ptr<const core::MapData> map);

        // Switches to the entries of a mode and counts them again.
        void setMode(core::ColorMode mode);

        // The view changed: counts again once it has rested.
        void setView(const core::Viewport& view);

        // Takes finished counts on the UI thread (called through the mailbox).
        void deliver(std::uint64_t request, core::LegendCounts counts);

    private:
        // One entry's controls.
        struct EntryControls {
            wxWindow* swatch = nullptr; // Color square; child owned by wx.
            wxStaticText* text = nullptr; // Label and count; child owned by wx.
        };

        // Creates the entries of the current mode.
        void rebuild();

        // Starts a count of the visible cells after the view rested.
        void startCount();

        // Writes the entry texts: label alone while counting, then size and share.
        void showCounts();

        core::Executor& executor_; // Runs counts.
        std::shared_ptr<LegendMailbox> mailbox_; // Route from workers back to this window.
        std::shared_ptr<const core::MapData> map_; // What is counted; null for none.
        core::ColorMode mode_ = core::ColorMode::Fragmentation; // Entries shown.
        std::optional<core::Viewport> view_; // Last view seen.
        std::vector<EntryControls> entries_; // Controls per legend entry.
        std::optional<core::LegendCounts> counts_; // Counts of the current view, or nullopt while counting.
        wxWrapSizer* sizer_ = nullptr; // Entry layout; owned by the window.
        wxTimer restTimer_; // Fires when the view has not changed for a moment.
        core::CancelSource cancel_; // Cancels the running count.
        std::uint64_t request_ = 0; // Id of the newest count; older results are dropped.
    };

} // namespace icd::ui
