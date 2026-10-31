#include "ui/map/MapHover.h"

#include "core/map/HitTest.h"
#include "core/map/MapData.h"
#include "ui/map/MapCanvas.h"

#include <wx/display.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <exception>

namespace icd::ui {

    namespace {

        // How long the pointer must rest before the tooltip appears.
        constexpr int restMilliseconds = 350;

        // Gap between the pointer and the tooltip, in DIPs.
        constexpr int pointerGapDip = 16;

        // The local time zone, or null (times then show in UTC) when the system's zone database is unavailable.
        const std::chrono::time_zone* localZone() {
            try {
                return std::chrono::current_zone();
            }
            catch (const std::exception&) {
                return nullptr;
            }
        }

        // The tooltip lines as one text.
        wxString joined(const std::vector<std::string>& lines) {
            wxString text;
            for (const std::string& line : lines) {
                if (!text.empty()) {
                    text += '\n';
                }
                text += wxString::FromUTF8(line);
            }
            return text;
        }

    } // namespace

    MapHover::MapHover(MapCanvas& canvas) : canvas_(canvas), zone_(localZone()) {
        restTimer_.Bind(wxEVT_TIMER, &MapHover::onTimer, this);
        canvas_.Bind(wxEVT_MOTION, &MapHover::onMotion, this);
        for (const auto type : {wxEVT_LEAVE_WINDOW, wxEVT_LEFT_DOWN, wxEVT_RIGHT_DOWN, wxEVT_MIDDLE_DOWN,
                                wxEVT_LEFT_DCLICK, wxEVT_MOUSEWHEEL}) {
            canvas_.Bind(type, &MapHover::onDismiss, this);
        }
        canvas_.Bind(wxEVT_KEY_DOWN, &MapHover::onKey, this);
    }

    void MapHover::hide() {
        restTimer_.Stop();
        if (popup_ != nullptr && popup_->IsShown()) {
            popup_->Hide();
        }
    }

    void MapHover::onMotion(wxMouseEvent& event) {
        event.Skip(); // The navigator drags and sets the cursor.
        pointer_ = {static_cast<double>(event.GetX()), static_cast<double>(event.GetY())};
        if (event.Dragging() || canvas_.minimap().contains(pointer_)) {
            hide();
            return;
        }
        if (popup_ != nullptr && popup_->IsShown()) {
            showAt(pointer_);
            return;
        }
        restTimer_.StartOnce(restMilliseconds);
    }

    void MapHover::onTimer(wxTimerEvent& /*event*/) { showAt(pointer_); }

    void MapHover::onDismiss(wxMouseEvent& event) {
        event.Skip();
        hide();
    }

    void MapHover::onKey(wxKeyEvent& event) {
        event.Skip();
        hide();
    }

    void MapHover::showAt(core::PixelPoint pointer) {
        const core::MapData* map = canvas_.map();
        const auto hit =
            map != nullptr ? core::hitTest(map->snapshot(), map->world(), canvas_.viewport(), pointer) : std::nullopt;
        if (!hit) {
            hide();
            return;
        }
        createPopup();
        const wxString text = joined(core::describeHit(map->snapshot(), *hit, zone_));
        if (text != shownText_) {
            shownText_ = text;
            text_->SetLabelText(text);
            popup_->GetSizer()->Fit(popup_);
        }
        placePopup(pointer);
        if (!popup_->IsShown()) {
            popup_->Show(); // wxPopupWindow shows without activating, so the map keeps focus.
        }
    }

    void MapHover::createPopup() {
        if (popup_ != nullptr) {
            return;
        }
        popup_ = new wxPopupWindow(&canvas_, wxBORDER_SIMPLE);
        popup_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
        text_ = new wxStaticText(popup_, wxID_ANY, "");
        text_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(text_, wxSizerFlags().Border(wxALL, popup_->FromDIP(6)));
        popup_->SetSizer(sizer);
    }

    void MapHover::placePopup(core::PixelPoint pointer) {
        const wxPoint screen =
            canvas_.ClientToScreen(wxPoint(static_cast<int>(pointer.x), static_cast<int>(pointer.y)));
        const int gap = canvas_.FromDIP(pointerGapDip);
        const wxSize size = popup_->GetSize();
        const int displayIndex = wxDisplay::GetFromWindow(&canvas_);
        const wxRect area =
            wxDisplay(displayIndex == wxNOT_FOUND ? 0U : static_cast<unsigned>(displayIndex)).GetClientArea();
        wxPoint place(screen.x + gap, screen.y + gap);
        if (place.x + size.x > area.GetRight()) {
            place.x = screen.x - gap - size.x; // Left of the pointer near the screen's right edge.
        }
        if (place.y + size.y > area.GetBottom()) {
            place.y = screen.y - gap - size.y; // Above the pointer near the bottom.
        }
        popup_->Move(place);
    }

} // namespace icd::ui
