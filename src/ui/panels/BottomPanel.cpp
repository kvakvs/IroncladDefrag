#include "ui/panels/BottomPanel.h"

#include "ui/Commands.h"

#include <wx/button.h>
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

        // A page holding a single explanatory line until its real content exists.
        wxWindow* createPlaceholderPage(wxWindow* parent, const wxString& hint) {
            auto* page = new wxPanel(parent);
            auto* text = new wxStaticText(page, wxID_ANY, hint);
            text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            auto* sizer = new wxBoxSizer(wxVERTICAL);
            sizer->Add(text, wxSizerFlags().Border(wxALL, page->FromDIP(12)));
            page->SetSizer(sizer);
            return page;
        }

        // ProgressDetails fields in the order of detailLabels.
        std::array<const wxString*, 6> detailValues(const ProgressDetails& details) {
            return {&details.stage,      &details.done, &details.elapsed,
                    &details.throughput, &details.eta,  &details.currentFile};
        }

    } // namespace

    BottomPanel::BottomPanel(wxWindow* parent, const wxString& resourcesText) : wxPanel(parent) {
        notebook_ = new wxNotebook(this, wxID_ANY);
        notebook_->AddPage(createPlaceholderPage(notebook_,
                                                 "Analyze a drive to see fragmentation, free-space runs "
                                                 "and per-class counts and bytes."),
                           "Summary");
        notebook_->AddPage(createPlaceholderPage(notebook_,
                                                 "Strategy presets, effort level and the zone list "
                                                 "appear here after an analysis."),
                           "Strategy");
        notebook_->AddPage(createPlaceholderPage(notebook_,
                                                 "Build a plan to compare the Fast, Moderate and "
                                                 "Maximum levels and review what will move."),
                           "Plan");
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
        auto* list =
            new wxListView(notebook_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        list->AppendColumn("Time", wxLIST_FORMAT_LEFT, FromDIP(140));
        list->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(480));
        list->AppendColumn("Reason", wxLIST_FORMAT_LEFT, FromDIP(320));
        return list;
    }

    wxSizer* BottomPanel::createStatusStrip() {
        auto* strip = new wxBoxSizer(wxHORIZONTAL);
        for (const ButtonSpec& spec : workflowButtons) {
            auto* button = new wxButton(this, spec.id, wxString::FromUTF8(spec.label));
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

    wxWindow* BottomPanel::page(BottomTab tab) const { return notebook_->GetPage(static_cast<std::size_t>(tab)); }

} // namespace icd::ui
