#include "ui/map/Ruler.h"

#include "core/map/MapData.h"
#include "ui/map/MapCanvas.h"

#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include <cmath>

namespace icd::ui {

    namespace {

        // Widest label each ruler expects (formatOffset() shows at most three decimals and switches units early).
        constexpr const char* widestRowLabel = "888,888.888 GB";
        constexpr const char* widestColumnLabel = "+8,888.888 MB";

        // A window coordinate from a fractional ruler position.
        int pixelOf(double position) { return static_cast<int>(std::lround(position)); }

    } // namespace

    Ruler::Ruler(wxWindow* parent, const MapCanvas& canvas, Orientation orientation) :
        wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE), canvas_(canvas),
        orientation_(orientation) {
        SetBackgroundStyle(wxBG_STYLE_PAINT); // Painted whole into a buffer: no erase, no flicker.
        Bind(wxEVT_PAINT, &Ruler::onPaint, this);
        Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
            updateMetrics();
            event.Skip();
        });
        updateMetrics();
    }

    void Ruler::updateMetrics() {
        font_ = GetFont().Scaled(0.9F);
        wxClientDC dc(this);
        dc.SetFont(font_);
        const bool vertical = orientation_ == Orientation::Vertical;
        const wxSize label = dc.GetTextExtent(vertical ? widestRowLabel : widestColumnLabel);
        lineHeight_ = label.y;
        labelWidth_ = label.x;
        padding_ = FromDIP(4);
        if (vertical) {
            SetMinSize(wxSize(labelWidth_ + 3 * padding_, -1));
        }
        else {
            SetMinSize(wxSize(-1, lineHeight_ + 2 * padding_));
        }
        InvalidateBestSize();
    }

    void Ruler::setDisk(std::optional<core::DiskPlacement> disk) {
        disk_ = disk;
        updateMetrics();
        Refresh(false);
    }

    core::RulerInput Ruler::input() const {
        core::RulerInput in;
        const core::MapData* map = canvas_.map();
        if (map == nullptr) {
            return in;
        }
        in.world = &map->world();
        in.view = canvas_.viewport();
        in.bytesPerCluster = map->snapshot().geometry.bytesPerCluster;
        in.disk = disk_;
        const int lines = in.disk ? 3 : 2;
        in.minSpacing =
            orientation_ == Orientation::Vertical ? lines * lineHeight_ + 2 * padding_ : labelWidth_ + 4 * padding_;
        return in;
    }

    void Ruler::onPaint(wxPaintEvent& /*event*/) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
        dc.Clear();
        const core::RulerInput in = input();
        if (in.world == nullptr) {
            return;
        }
        dc.SetFont(font_);
        dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
        if (orientation_ == Orientation::Vertical) {
            drawVertical(dc, core::rowTicks(in));
        }
        else {
            drawHorizontal(dc, core::columnTicks(in));
        }
    }

    void Ruler::drawVertical(wxDC& dc, const core::RulerTicks& ticks) const {
        const wxSize size = GetClientSize();
        const int minorLength = padding_;
        dc.DrawLine(size.x - 1, 0, size.x - 1, size.y); // Edge against the map.
        for (const double minor : ticks.minor) {
            dc.DrawLine(size.x - minorLength, pixelOf(minor), size.x, pixelOf(minor));
        }
        const wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
        const wxColour secondary = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
        for (const core::RulerTick& tick : ticks.major) {
            const int y = pixelOf(tick.pixel);
            dc.DrawLine(0, y, size.x, y);
            dc.SetTextForeground(text);
            dc.DrawText(tick.bytesText, padding_, y + 1);
            dc.SetTextForeground(secondary);
            dc.DrawText(tick.percentText, padding_, y + 1 + lineHeight_);
            if (!tick.diskText.empty()) {
                dc.DrawText(tick.diskText, padding_, y + 1 + 2 * lineHeight_);
            }
        }
    }

    void Ruler::drawHorizontal(wxDC& dc, const core::RulerTicks& ticks) const {
        const wxSize size = GetClientSize();
        const int minorLength = padding_;
        dc.DrawLine(0, size.y - 1, size.x, size.y - 1); // Edge against the map.
        for (const double minor : ticks.minor) {
            dc.DrawLine(pixelOf(minor), size.y - minorLength, pixelOf(minor), size.y);
        }
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
        for (const core::RulerTick& tick : ticks.major) {
            const int x = pixelOf(tick.pixel);
            dc.DrawLine(x, 0, x, size.y);
            dc.DrawText(tick.bytesText, x + padding_ / 2 + 1, padding_ / 2);
        }
    }

} // namespace icd::ui
