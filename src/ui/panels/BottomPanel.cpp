#include "ui/panels/BottomPanel.h"

#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/panels/FilePage.h"
#include "ui/panels/MapColorsPage.h"
#include "ui/panels/PlanPage.h"
#include "ui/panels/StrategyPage.h"
#include "ui/panels/SummaryPage.h"
#include "ui/tabs/LogTab.h"
#include "ui/tabs/ProgressTab.h"

#include <wx/button.h>
#include <wx/gauge.h>
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


    } // namespace

    BottomPanel::BottomPanel(wxWindow* parent, const wxString& resourcesText, core::Executor& executor) :
        wxPanel(parent) {
        notebook_ = new wxNotebook(this, wxID_ANY);
        summary_ = new SummaryPage(notebook_);
        strategy_ = new StrategyPage(notebook_);
        plan_ = new PlanPage(notebook_);
        notebook_->AddPage(summary_, "Summary");
        notebook_->AddPage(strategy_, "Strategy");
        notebook_->AddPage(plan_, "Plan");
        progress_ = new ProgressTab(notebook_, resourcesText);
        notebook_->AddPage(progress_, "Progress");
        log_ = new LogTab(notebook_);
        notebook_->AddPage(log_, "Log");
        mapColors_ = new MapColorsPage(notebook_, executor);
        notebook_->AddPage(mapColors_, "Map colors");
        file_ = new FilePage(notebook_);
        notebook_->AddPage(file_, "File");

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(notebook_, wxSizerFlags(1).Expand());
        sizer->Add(createStatusStrip(), wxSizerFlags().Expand().Border(wxALL, FromDIP(6)));
        SetSizer(sizer);
    }

    wxSizer* BottomPanel::createStatusStrip() {
        auto* strip = new wxBoxSizer(wxHORIZONTAL);
        // Each button's id is its command id, so the frame's command handler and enable rule cover it like the menu
        // item.
        for (const WorkflowCommand& spec : workflowCommands()) {
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

    bool BottomPanel::tabShown(BottomTab tab) const { return notebook_->GetSelection() == static_cast<int>(tab); }

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

    void BottomPanel::setPauseLabel(bool paused) {
        if (wxWindow* button = FindWindow(ID_PAUSE); button != nullptr) {
            button->SetLabel(paused ? "&Resume" : "&Pause");
        }
    }

} // namespace icd::ui
