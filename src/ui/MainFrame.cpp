#include "ui/MainFrame.h"

#include "services/AppInfo.h"
#include "services/AppServices.h"
#include "services/job/FakeJob.h"
#include "ui/bridge/JobEvents.h"

#include <wx/menu.h>
#include <wx/statusbr.h>

#include <cmath>
#include <string>

namespace icd {

    namespace {

        // Status bar text for how a job ended.
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

    } // namespace

    MainFrame::MainFrame(services::AppServices& services) :
        wxFrame(nullptr, wxID_ANY,
                wxString::FromUTF8(std::string(services::appName()) + " " + std::string(services::appVersion())),
                wxDefaultPosition, wxDefaultSize),
        services_(services), bridge_(std::make_shared<ui::JobEventBridge>(this)) {
        SetMinSize(FromDIP(wxSize(640, 480)));
        SetSize(FromDIP(wxSize(1280, 800)));
        buildMenus();
        CreateStatusBar(2);
        const int widths[] = {-1, FromDIP(80)};
        SetStatusWidths(2, widths);
        SetStatusText("Ready");

        Bind(ui::jobProgressEvent(), &MainFrame::onJobProgress, this);
        Bind(ui::jobFinishedEvent(), &MainFrame::onJobFinished, this);
        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
        Centre();
    }

    MainFrame::~MainFrame() { bridge_->detach(); }

    void MainFrame::buildMenus() {
        auto* file = new wxMenu();
        file->Append(wxID_EXIT, "E&xit\tAlt+F4");
        auto* debug = new wxMenu();
        debug->Append(ID_RUN_FAKE_JOB, "&Run fake parallel job\tCtrl+Shift+J");
        debug->Append(ID_CANCEL_JOB, "&Cancel job\tCtrl+Shift+K");
        auto* bar = new wxMenuBar();
        bar->Append(file, "&File");
        bar->Append(debug, "&Debug");
        SetMenuBar(bar); // The frame owns the menu bar and its menus.

        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, &MainFrame::onRunFakeJob, this, ID_RUN_FAKE_JOB);
        Bind(wxEVT_MENU, &MainFrame::onCancelJob, this, ID_CANCEL_JOB);
        updateJobCommands();
    }

    void MainFrame::updateJobCommands() {
        wxMenuBar* bar = GetMenuBar();
        bar->Enable(ID_RUN_FAKE_JOB, !activeJob_);
        bar->Enable(ID_CANCEL_JOB, activeJob_.has_value());
    }

    void MainFrame::onRunFakeJob(wxCommandEvent& /*event*/) {
        if (activeJob_) {
            return;
        }
        activeJob_ = services_.jobs().start("fake parallel job", services::makeFakeParallelJob(), bridge_);
        updateJobCommands();
        SetStatusText("Fake job started");
        SetStatusText("0%", 1);
    }

    void MainFrame::onCancelJob(wxCommandEvent& /*event*/) {
        if (activeJob_) {
            services_.jobs().cancel(*activeJob_);
            SetStatusText("Cancelling...");
        }
    }

    void MainFrame::onJobProgress(wxThreadEvent& event) {
        const auto payload = event.GetPayload<ui::JobProgressPayload>();
        if (payload.id != activeJob_) {
            return; // A late event from an earlier job.
        }
        const core::Progress& progress = payload.progress;
        SetStatusText(wxString::Format("%s: %llu of %llu", wxString::FromUTF8(progress.stage),
                                       static_cast<unsigned long long>(progress.done),
                                       static_cast<unsigned long long>(progress.total)));
        SetStatusText(wxString::Format("%d%%", static_cast<int>(std::lround(progress.fraction() * 100.0))), 1);
    }

    void MainFrame::onJobFinished(wxThreadEvent& event) {
        const auto payload = event.GetPayload<ui::JobFinishedPayload>();
        if (payload.id != activeJob_) {
            return;
        }
        activeJob_.reset();
        updateJobCommands();
        SetStatusText(describeResult(payload.result));
        SetStatusText("", 1);
    }

    void MainFrame::onClose(wxCloseEvent& event) {
        if (activeJob_) {
            services_.jobs().cancel(*activeJob_); // AppServices waits for it when the app exits.
        }
        bridge_->detach();
        event.Skip(); // Default handling destroys the window.
    }

} // namespace icd
