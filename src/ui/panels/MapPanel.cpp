#include "ui/panels/MapPanel.h"

#include "core/map/MapData.h"
#include "ui/Commands.h"
#include "ui/dialogs/GoToFileDialog.h"
#include "ui/map/MapCanvas.h"
#include "ui/map/MapNavigator.h"
#include "ui/map/MapSelection.h"
#include "ui/map/Ruler.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>

#include <array>
#include <format>
#include <string>
#include <utility>

namespace icd::ui {

    namespace {

        // Color mode names, the same list as View > Color mode.
        constexpr std::array colorModes{"File class", "Fragmentation", "Age / heat",
                                        "Size",       "Zone fit",      "Plan preview"};

        // The toolbar text for a selected file: its path and where in its fragments the view is.
        std::string selectionText(const std::string& path, std::size_t fragment, std::size_t count) {
            if (count == 0) {
                return path + ": no clusters on disk";
            }
            if (count == 1) {
                return path + ": 1 fragment";
            }
            return std::format("{}: fragment {} of {}", path, fragment + 1, count);
        }

    } // namespace

    MapPanel::MapPanel(wxWindow* parent, core::Executor& executor) : wxPanel(parent), executor_(executor) {
        canvas_ = new MapCanvas(this, executor);
        auto* rows = new Ruler(this, *canvas_, Ruler::Orientation::Vertical);
        auto* columns = new Ruler(this, *canvas_, Ruler::Orientation::Horizontal);
        canvas_->setViewObserver([rows, columns] {
            rows->Refresh(false);
            columns->Refresh(false);
        });

        // Rulers above and left of the map; the corner stays empty.
        auto* map = new wxFlexGridSizer(2, 2, 0, 0);
        map->AddGrowableCol(1);
        map->AddGrowableRow(1);
        map->AddSpacer(0);
        map->Add(columns, wxSizerFlags().Expand());
        map->Add(rows, wxSizerFlags().Expand());
        map->Add(canvas_, wxSizerFlags().Expand());

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(createToolbar(), wxSizerFlags().Expand().Border(wxALL, FromDIP(4)));
        sizer->Add(map, wxSizerFlags(1).Expand());
        SetSizer(sizer);
    }

    MapPanel::~MapPanel() {
        canvas_->setViewObserver({}); // The rulers may be destroyed before the canvas.
        canvas_->selection().setObserver({}); // So may the toolbar label.
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
        search_ = new wxSearchCtrl(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(220, -1)), wxTE_PROCESS_ENTER);
        search_->SetDescriptiveText("Find file");
        search_->Bind(wxEVT_SEARCH, [this](wxCommandEvent&) { goToFile(search_->GetValue()); });
        search_->Disable(); // Until a map is shown.
        caption_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                    wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        selectionLabel_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                           wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
        canvas_->selection().setObserver([this] { updateSelectionLabel(); });

        bar->Add(new wxStaticText(this, wxID_ANY, "Color:"), wxSizerFlags().CenterVertical());
        bar->Add(colorMode_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        bar->Add(zones, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(6)));
        bar->Add(plan, wxSizerFlags().CenterVertical());
        bar->Add(caption_, wxSizerFlags(1).CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(12)));
        bar->Add(selectionLabel_, wxSizerFlags(1).CenterVertical().Border(wxRIGHT, FromDIP(12)));
        bar->Add(search_, wxSizerFlags().CenterVertical());
        for (wxWindow* control :
             {static_cast<wxWindow*>(colorMode_), static_cast<wxWindow*>(zones), static_cast<wxWindow*>(plan)}) {
            control->Disable(); // Color modes (T032) and overlays bring these to life.
        }
        return bar;
    }

    void MapPanel::setCaption(const wxString& caption) { caption_->SetLabelText(caption); }

    void MapPanel::setColorMode(int mode) { colorMode_->SetSelection(mode); }

    void MapPanel::setMap(std::shared_ptr<const core::MapData> map) {
        map_ = map;
        search_->Enable(map_ != nullptr);
        canvas_->setMap(std::move(map));
    }

    void MapPanel::goToFile(const wxString& query) {
        if (map_ == nullptr) {
            return;
        }
        GoToFileDialog dialog(this, map_, executor_, query);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        if (const auto file = dialog.chosen(); file) {
            canvas_->selection().goTo(*file);
            canvas_->SetFocus(); // Map keys and F3 work on from here.
        }
    }

    void MapPanel::updateSelectionLabel() {
        const MapSelection& selection = canvas_->selection();
        const auto file = selection.file();
        if (!file || map_ == nullptr) {
            selectionLabel_->SetLabelText("");
            return;
        }
        const std::string text =
            selectionText(map_->snapshot().path(*file), selection.fragment(), selection.fragmentCount());
        selectionLabel_->SetLabelText(wxString::FromUTF8(text));
        selectionLabel_->SetToolTip(wxString::FromUTF8(text)); // The label may be cut in the middle.
    }

    bool MapPanel::hasMap() const { return canvas_->hasMap(); }

    bool MapPanel::minimapShown() const { return canvas_->minimap().shown(); }

    bool MapPanel::handleViewCommand(int id) {
        MapNavigator& navigator = canvas_->navigator();
        const core::Viewport& view = canvas_->viewport();
        const core::PixelPoint center{view.size.x / 2.0, view.size.y / 2.0};
        switch (id) {
        case ID_SHOW_MINIMAP:
            canvas_->showMinimap(!canvas_->minimap().shown());
            return true;
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
        case ID_GOTO_FILE:
            goToFile(search_->GetValue());
            return true;
        case ID_NEXT_FRAGMENT:
            canvas_->selection().nextFragment();
            return true;
        case ID_PREVIOUS_FRAGMENT:
            canvas_->selection().previousFragment();
            return true;
        default:
            return false;
        }
    }

} // namespace icd::ui
