#include "ui/MainFrame.h"

#include "core/format/Count.h"
#include "core/resources/ResourceCaps.h"
#include "services/AppInfo.h"
#include "services/AppServices.h"
#include "services/job/FakeJob.h"
#include "ui/Commands.h"
#include "ui/MainMenu.h"
#include "ui/bridge/JobEvents.h"
#include "ui/panels/BottomPanel.h"
#include "ui/panels/MapPlaceholder.h"

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <string>

namespace icd {

    namespace {

        // Height the bottom panel starts with; the map gets the rest and all later growth.
        constexpr int bottomPanelHeight = 260;

        // Status line text for how a job ended.
        wxString describeResult(const services::JobResult& result) {
            const double seconds = static_cast<double>(result.elapsed.count()) / 1000.0;
            switch (result.outcome) {
            case services::JobOutcome::Succeeded:
                return wxString::Format("Fake job finished in %.1f s", seconds);
            case services::JobOutcome::Cancelled:
                return wxString::Format("Fake job cancelled after %.1f s", seconds);
            case services::JobOutcome::Failed:
                return "Fake job failed: " + wxString::FromUTF8(result.error);
            }
            return {};
        }

        // "1,234 of 10,000", or just the count when the total is unknown.
        wxString describeCount(const core::Progress& progress) {
            std::string text = core::formatCount(progress.done);
            if (progress.total != 0) {
                text += " of " + core::formatCount(progress.total);
            }
            return wxString::FromUTF8(text);
        }

        // Stand-in for the all-disks panel until the drive list exists (T011).
        wxWindow* createDrivesPlaceholder(wxWindow* parent) {
            auto* panel = new wxPanel(parent);
            auto* box = new wxStaticBoxSizer(wxVERTICAL, panel, "Drives");
            box->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, "The list of all drives goes here."),
                     wxSizerFlags().Border(wxALL, panel->FromDIP(8)));
            box->SetMinSize(panel->FromDIP(wxSize(-1, 120)));
            panel->SetSizer(box);
            return panel;
        }

    } // namespace

    MainFrame::MainFrame(services::AppServices& services) :
        wxFrame(nullptr, wxID_ANY,
                wxString::FromUTF8(std::string(services::appName()) + " " + std::string(services::appVersion())),
                wxDefaultPosition, wxDefaultSize),
        services_(services), bridge_(std::make_shared<ui::JobEventBridge>(this)) {
        SetMenuBar(ui::createMainMenuBar()); // The frame owns the menu bar and its menus.
        buildLayout();
        bindCommands();
        updateCommands();
        SetMinSize(FromDIP(wxSize(800, 600)));
        SetSize(FromDIP(wxSize(1280, 860)));
        Centre();
        CallAfter([this] { placeSash(); });
    }

    MainFrame::~MainFrame() { bridge_->detach(); }

    void MainFrame::buildLayout() {
        auto* root = new wxPanel(this);
        splitter_ =
            new wxSplitterWindow(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
        map_ = new ui::MapPlaceholder(splitter_);
        const wxString resources = wxString::FromUTF8(core::describeCaps(services_.resources(), services_.caps()));
        bottom_ = new ui::BottomPanel(splitter_, resources);
        splitter_->SetSashGravity(1.0); // Window growth goes to the map.
        splitter_->SetMinimumPaneSize(FromDIP(140));
        splitter_->SplitHorizontally(map_, bottom_);
        map_->setCaption("No drive selected");

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(createDrivesPlaceholder(root), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(4)));
        sizer->Add(splitter_, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(4)));
        root->SetSizer(sizer);
    }

    void MainFrame::placeSash() {
        splitter_->SetSashPosition(splitter_->GetClientSize().y - FromDIP(bottomPanelHeight));
    }

    void MainFrame::bindCommands() {
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        for (const auto& eventType : {wxEVT_MENU, wxEVT_BUTTON}) {
            Bind(eventType, &MainFrame::onRunFakeJob, this, ui::ID_RUN_FAKE_JOB);
            Bind(eventType, &MainFrame::onCancel, this, ui::ID_CANCEL);
        }
        Bind(ui::jobProgressEvent(), &MainFrame::onJobProgress, this);
        Bind(ui::jobFinishedEvent(), &MainFrame::onJobFinished, this);
        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    }

    bool MainFrame::commandEnabled(int id) const {
        switch (id) {
        case ui::ID_RUN_FAKE_JOB:
            return !activeJob_;
        case ui::ID_CANCEL:
            return activeJob_.has_value();
        default:
            return false; // Stub: the workflow (T012) and later phases enable the rest.
        }
    }

    void MainFrame::updateCommands() {
        for (const int id : ui::stateDependentCommands()) {
            ui::enableCommand(*this, id, commandEnabled(id));
        }
    }

    void MainFrame::onRunFakeJob(wxCommandEvent& /*event*/) {
        if (activeJob_) {
            return;
        }
        activeJob_ = services_.jobs().start("fake parallel job", services::makeFakeParallelJob(), bridge_);
        updateCommands();
        bottom_->setStatus("Fake job started");
        bottom_->setProgress(0.0);
        bottom_->showTab(ui::BottomTab::Progress);
    }

    void MainFrame::onCancel(wxCommandEvent& /*event*/) {
        if (activeJob_) {
            services_.jobs().cancel(*activeJob_);
            bottom_->setStatus("Cancelling...");
        }
    }

    void MainFrame::onJobProgress(wxThreadEvent& event) {
        const auto payload = event.GetPayload<ui::JobProgressPayload>();
        if (payload.id != activeJob_) {
            return; // A late event from an earlier job.
        }
        const core::Progress& progress = payload.progress;
        const wxString stage = wxString::FromUTF8(progress.stage);
        bottom_->setStatus("Fake job: " + stage + ", " + describeCount(progress));
        bottom_->setProgress(progress.fraction());
        bottom_->setProgressDetails({.stage = stage,
                                     .done = describeCount(progress),
                                     .elapsed = {},
                                     .throughput = {},
                                     .eta = {},
                                     .currentFile = wxString::FromUTF8(progress.message)});
    }

    void MainFrame::onJobFinished(wxThreadEvent& event) {
        const auto payload = event.GetPayload<ui::JobFinishedPayload>();
        if (payload.id != activeJob_) {
            return;
        }
        activeJob_.reset();
        updateCommands();
        bottom_->setStatus(describeResult(payload.result));
        bottom_->setProgress(std::nullopt);
        bottom_->setProgressDetails({});
    }

    void MainFrame::onClose(wxCloseEvent& event) {
        if (activeJob_) {
            services_.jobs().cancel(*activeJob_); // AppServices waits for it when the app exits.
        }
        bridge_->detach();
        event.Skip(); // Default handling destroys the window.
    }

} // namespace icd
