#include "ui/panels/MapPanel.h"

#include "core/map/MapData.h"
#include "ui/Commands.h"
#include "ui/map/MapCanvas.h"
#include "ui/map/MapNavigator.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <array>
#include <utility>

namespace icd::ui {

    namespace {

        // Color mode names, the same list as View > Color mode.
        constexpr std::array colorModes{"File class", "Fragmentation", "Age / heat",
                                        "Size",       "Zone fit",      "Plan preview"};

    } // namespace

    MapPanel::MapPanel(wxWindow* parent, core::Executor& executor) : wxPanel(parent) {
        canvas_ = new MapCanvas(this, executor);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(createToolbar(), wxSizerFlags().Expand().Border(wxALL, FromDIP(4)));
        sizer->Add(canvas_, wxSizerFlags(1).Expand());
        SetSizer(sizer);
    }

    wxSizer* MapPanel::createToolbar() {
        auto* bar = new wxBoxSizer(wxHORIZONTAL);
        colorMode_ = new wxChoice(this, wxID_ANY);
        for (const char* mode : colorModes) {
            colorMode_->Append(mode);
        }
        colorMode_->SetSelection(0);
        auto* zones = new wxCheckBox(this, wxID_ANY, "Zones");
        auto* plan = new wxCheckBox(this, wxID_ANY, "Plan overlay");
        auto* search = new wxSearchCtrl(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(220, -1)));
        search->SetDescriptiveText("Find file");
        caption_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                    wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);

        bar->Add(new wxStaticText(this, wxID_ANY, "Color:"), wxSizerFlags().CenterVertical());
        bar->Add(colorMode_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        bar->Add(zones, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(6)));
        bar->Add(plan, wxSizerFlags().CenterVertical());
        bar->Add(caption_, wxSizerFlags(1).CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(12)));
        bar->Add(search, wxSizerFlags().CenterVertical());
        for (wxWindow* control : {static_cast<wxWindow*>(colorMode_), static_cast<wxWindow*>(zones),
                                  static_cast<wxWindow*>(plan), static_cast<wxWindow*>(search)}) {
            control->Disable(); // Color modes (T032), overlays and search (T031) bring these to life.
        }
        return bar;
    }

    void MapPanel::setCaption(const wxString& caption) { caption_->SetLabelText(caption); }

    void MapPanel::setColorMode(int mode) { colorMode_->SetSelection(mode); }

    void MapPanel::setMap(std::shared_ptr<const core::MapData> map) { canvas_->setMap(std::move(map)); }

    bool MapPanel::hasMap() const { return canvas_->hasMap(); }

    bool MapPanel::handleViewCommand(int id) {
        MapNavigator& navigator = canvas_->navigator();
        const core::Viewport& view = canvas_->viewport();
        const core::PixelPoint center{view.size.x / 2.0, view.size.y / 2.0};
        switch (id) {
        case ID_ZOOM_IN:
            navigator.zoomBy(2.0, center);
            return true;
        case ID_ZOOM_OUT:
            navigator.zoomBy(0.5, center);
            return true;
        case ID_ZOOM_FIT:
            navigator.fit();
            return true;
        case ID_ZOOM_CLUSTER:
            navigator.zoomToScale(1.0);
            return true;
        default:
            return false;
        }
    }

} // namespace icd::ui
