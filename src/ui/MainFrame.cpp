#include "ui/MainFrame.h"

#include "core/resources/ResourceCaps.h"
#include "services/AppInfo.h"
#include "services/AppServices.h"
#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/MainMenu.h"
#include "ui/WorkflowBinding.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/ShortcutsDialog.h"
#include "ui/panels/BottomPanel.h"
#include "ui/panels/DrivesPanel.h"
#include "ui/panels/MapPanel.h"

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>

#include <string>

namespace icd {

    namespace {

        // Height the bottom panel starts with; the map gets the rest and all later growth.
        constexpr int bottomPanelHeight = 280;

    } // namespace

    MainFrame::MainFrame(services::AppServices& services, const std::string& initialDrive) :
        wxFrame(nullptr, wxID_ANY,
                wxString::FromUTF8(std::string(services::appName()) + " " + std::string(services::appVersion())),
                wxDefaultPosition, wxDefaultSize),
        services_(services) {
        SetMenuBar(ui::createMainMenuBar()); // The frame owns the menu bar and its menus.
        ui::applyMenuShortcuts(*GetMenuBar());
        buildLayout();
        binding_ = std::make_unique<ui::WorkflowBinding>(*this, services_,
                                                         ui::WorkflowBinding::Widgets{*drives_, *map_, *bottom_});
        if (!initialDrive.empty()) {
            binding_->selectDrive(initialDrive);
        }
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ui::AboutDialog(this, services_).ShowModal(); }, wxID_ABOUT);
        Bind(
            wxEVT_MENU, [this](wxCommandEvent&) { ui::ShortcutsDialog(this, *GetMenuBar()).ShowModal(); },
            ui::ID_SHORTCUTS);
        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
        SetMinSize(FromDIP(wxSize(800, 600)));
        SetSize(FromDIP(wxSize(1280, 860)));
        Centre();
        CallAfter([this] { placeSash(); });
    }

    MainFrame::~MainFrame() = default;

    void MainFrame::buildLayout() {
        auto* root = new wxPanel(this);
        splitter_ =
            new wxSplitterWindow(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
        map_ = new ui::MapPanel(splitter_, services_.pool());
        const wxString resources = wxString::FromUTF8(core::describeCaps(services_.resources(), services_.caps()));
        bottom_ = new ui::BottomPanel(splitter_, resources);
        splitter_->SetSashGravity(1.0); // Window growth goes to the map.
        splitter_->SetMinimumPaneSize(FromDIP(140));
        splitter_->SplitHorizontally(map_, bottom_);
        drives_ = new ui::DrivesPanel(root);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(drives_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(4)));
        sizer->Add(splitter_, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(4)));
        root->SetSizer(sizer);
    }

    void MainFrame::placeSash() {
        splitter_->SetSashPosition(splitter_->GetClientSize().y - FromDIP(bottomPanelHeight));
    }

    void MainFrame::onClose(wxCloseEvent& event) {
        binding_->shutdown();
        event.Skip(); // Default handling destroys the window.
    }

} // namespace icd
