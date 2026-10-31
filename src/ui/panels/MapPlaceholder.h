#pragma once

#include <wx/panel.h>

#include <cstdint>

class wxChoice;
class wxDC;
class wxKeyEvent;

namespace icd::ui {

    // Stand-in for the drive map (plan.md section 7) until the tile engine exists in Phase 2: the map toolbar
    // with disabled controls, and a canvas painting a made-up cluster pattern so the layout can be judged
    // with something map-like in it.
    class MapPlaceholder : public wxPanel {
    public:
        // Creates the toolbar and the canvas.
        explicit MapPlaceholder(wxWindow* parent);

        // Text drawn over the canvas, e.g. which drive is selected and what state it is in.
        void setCaption(const wxString& caption);

        // Changes the made-up pattern, so different drives look different; 0 paints an empty map.
        void setPattern(std::uint32_t seed);

        // Shows a color mode (index of View > Color mode) in the toolbar.
        void setColorMode(int mode);

    private:
        // Creates the color mode, overlay and search controls (disabled until the map exists).
        wxSizer* createToolbar();

        // Turns map keys (plan.md 7.4) into the View commands they share; other keys go on.
        void onChar(wxKeyEvent& event);

        // Paints the pattern and the caption.
        void paintCanvas(wxDC& dc, const wxSize& size) const;

        wxWindow* canvas_ = nullptr; // Painted area; child window owned by wx.
        wxChoice* colorMode_ = nullptr; // Toolbar color mode; child window owned by wx.
        wxString caption_; // Text over the canvas.
        std::uint32_t seed_ = 0; // Pattern seed; 0 = no drive.
    };

} // namespace icd::ui
