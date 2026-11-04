#include "ui/tabs/ProgressTab.h"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace icd::ui {

    namespace {

        // Field labels in ProgressDetails order; laid out in pairs per row (left, right), so the tab fits the
        // bottom panel's height.
        constexpr std::array<const char*, 8> labels{
            "Stage:", "Done:", "Elapsed:", "Time left:", "Throughput:", "Workers:", "Current file:", "Moves:",
        };

        // ProgressDetails fields in the order of labels.
        std::array<const wxString*, 8> fieldsOf(const ProgressDetails& d) {
            return {&d.stage, &d.done, &d.elapsed, &d.eta, &d.throughput, &d.workers, &d.currentFile, &d.moves};
        }

    } // namespace

    ProgressTab::ProgressTab(wxWindow* parent, const wxString& resourcesText) : wxPanel(parent) {
        auto* grid = new wxFlexGridSizer(4, wxSize(FromDIP(12), FromDIP(4)));
        grid->AddGrowableCol(1, 1);
        grid->AddGrowableCol(3, 1);
        for (std::size_t field = 0; field < rows; ++field) {
            grid->Add(new wxStaticText(this, wxID_ANY, labels.at(field)));
            values_.at(field) = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize,
                                                 wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
            grid->Add(values_.at(field), wxSizerFlags().Expand().Border(wxRIGHT, FromDIP(12)));
        }
        auto* resources = new wxBoxSizer(wxHORIZONTAL);
        resources->Add(new wxStaticText(this, wxID_ANY, "Resources:"), wxSizerFlags().Border(wxRIGHT, FromDIP(12)));
        resources_ = new wxStaticText(this, wxID_ANY, resourcesText);
        resources->Add(resources_, wxSizerFlags(1));

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, FromDIP(12)));
        sizer->Add(resources, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12)));
        SetSizer(sizer);
    }

    void ProgressTab::setResources(const wxString& resourcesText) {
        if (resources_->GetLabel() != resourcesText) {
            resources_->SetLabel(resourcesText);
            Layout();
        }
    }

    void ProgressTab::show(const ProgressDetails& details) {
        const auto fields = fieldsOf(details);
        for (std::size_t field = 0; field < rows; ++field) {
            const wxString& value = *fields.at(field);
            values_.at(field)->SetLabelText(value.empty() ? wxString("-") : value);
        }
    }

} // namespace icd::ui
