#include "ui/panels/BottomPanel.h"

#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/panels/PlanPage.h"
#include "ui/panels/StrategyPage.h"
#include "ui/panels/SummaryPage.h"

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <array>
#include <cmath>

namespace icd::ui {

    namespace {

        // Gauge resolution: tenths of a percent.
        constexpr int gaugeRange = 1000;

        // A workflow button in the status strip; its id is the command id, so the frame's command handler
        // and enable rule cover it like the menu item.
        struct ButtonSpec {
            int id = 0; // Command id.
            const char* label = ""; // Button text with & mnemonic.
        };

        constexpr std::array workflowButtons{
            ButtonSpec{ID_ANALYZE, "&Analyze"},       ButtonSpec{ID_QUICK, "&Quick"},
            ButtonSpec{ID_BUILD_PLAN, "&Build plan"}, ButtonSpec{ID_EXECUTE, "&Execute..."},
            ButtonSpec{ID_PAUSE, "&Pause"},           ButtonSpec{ID_CANCEL, "&Cancel"},
        };

        constexpr std::array<const char*, 6> detailLabels{
            "Stage:", "Done:", "Elapsed:", "Throughput:", "Time left:", "Current file:",
        };

        // ProgressDetails fields in the order of detailLabels.
        std::array<const wxString*, 6> detailValues(const ProgressDetails& details) {
            return {&details.stage,      &details.done, &details.elapsed,
                    &details.throughput, &details.eta,  &details.currentFile};
        }

    } // namespace

    BottomPanel::BottomPanel(wxWindow* parent, const wxString& resourcesText) : wxPanel(parent) {
        notebook_ = new wxNotebook(this, wxID_ANY);
        summary_ = new SummaryPage(notebook_);
        strategy_ = new StrategyPage(notebook_);
        plan_ = new PlanPage(notebook_);
        notebook_->AddPage(summary_, "Summary");
        notebook_->AddPage(strategy_, "Strategy");
        notebook_->AddPage(plan_, "Plan");
        notebook_->AddPage(createProgressPage(resourcesText), "Progress");
        notebook_->AddPage(createLogPage(), "Log");

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(notebook_, wxSizerFlags(1).Expand());
        sizer->Add(createStatusStrip(), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
        SetSizer(sizer);
    }

    wxWindow* BottomPanel::createProgressPage(const wxString& resourcesText) {
        auto* page = new wxPanel(notebook_);
        auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(12), FromDIP(4)));
        grid->AddGrowableCol(1);
        for (std::size_t row = 0; row < detailRows; ++row) {
            grid->Add(new wxStaticText(page, wxID_ANY, detailLabels.at(row)));
            details_.at(row) = new wxStaticText(page, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize,
                                                wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
            grid->Add(details_.at(row), wxSizerFlags().Expand());
        }
        grid->Add(new wxStaticText(page, wxID_ANY, "Resources:"));
        grid->Add(new wxStaticText(page, wxID_ANY, resourcesText), wxSizerFlags().Expand());

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, FromDIP(12)));
        page->SetSizer(sizer);
        return page;
    }

    wxWindow* BottomPanel::createLogPage() {
        log_ = new wxListView(notebook_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        log_->AppendColumn("Time", wxLIST_FORMAT_LEFT, FromDIP(140));
        log_->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(480));
        log_->AppendColumn("Reason", wxLIST_FORMAT_LEFT, FromDIP(320));
        return log_;
    }

    wxSizer* BottomPanel::createStatusStrip() {
        auto* strip = new wxBoxSizer(wxHORIZONTAL);
        for (const ButtonSpec& spec : workflowButtons) {
            auto* button = new wxButton(this, spec.id, wxString::FromUTF8(spec.label));
            button->SetToolTip(commandTip(spec.id)); // What it does and its key, from the shortcut table.
            button->Disable(); // Enabled by the frame from the app state.
            strip->Add(button, wxSizerFlags().Border(wxRIGHT, FromDIP(4)));
        }
        status_ = new wxStaticText(this, wxID_ANY, "Ready", wxDefaultPosition, wxDefaultSize,
                                   wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        gauge_ = new wxGauge(this, wxID_ANY, gaugeRange, wxDefaultPosition, FromDIP(wxSize(160, 16)));
        percent_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(40, -1)),
                                    wxST_NO_AUTORESIZE | wxALIGN_RIGHT);
        strip->Add(status_, wxSizerFlags(1).CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(8)));
        strip->Add(gauge_, wxSizerFlags().CenterVertical());
        strip->Add(percent_, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(4)));
        return strip;
    }

    void BottomPanel::showTab(BottomTab tab) { notebook_->SetSelection(static_cast<std::size_t>(tab)); }

    void BottomPanel::setStatus(const wxString& text) { status_->SetLabelText(text); }

    void BottomPanel::setProgress(std::optional<double> fraction) {
        if (!fraction) {
            gauge_->SetValue(0);
            percent_->SetLabelText("");
            return;
        }
        gauge_->SetValue(static_cast<int>(std::lround(*fraction * gaugeRange)));
        percent_->SetLabelText(wxString::Format("%d%%", static_cast<int>(std::lround(*fraction * 100.0))));
    }

    void BottomPanel::setProgressDetails(const ProgressDetails& details) {
        const auto values = detailValues(details);
        for (std::size_t row = 0; row < detailRows; ++row) {
            const wxString& value = *values.at(row);
            details_.at(row)->SetLabelText(value.empty() ? wxString("-") : value);
        }
    }

    void BottomPanel::addLogEntry(const wxString& file, const wxString& reason) {
        const long row = log_->InsertItem(log_->GetItemCount(), wxDateTime::Now().FormatISOCombined(' '));
        log_->SetItem(row, 1, file);
        log_->SetItem(row, 2, reason);
    }

    void BottomPanel::clearLog() { log_->DeleteAllItems(); }

    void BottomPanel::setPauseLabel(bool paused) {
        if (wxWindow* button = FindWindow(ID_PAUSE); button != nullptr) {
            button->SetLabel(paused ? "&Resume" : "&Pause");
        }
    }

} // namespace icd::ui
