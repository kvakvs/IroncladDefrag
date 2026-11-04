#include "ui/dialogs/SettingsDialog.h"

#include "core/format/ByteSize.h"
#include "services/AppServices.h"
#include "services/exec/ThreadPool.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace icd::ui {

    namespace {

        // Widest a text line of the dialog wraps at, in DIP.
        constexpr int wrapWidth = 460;

        // Bytes in the GB unit of the memory field.
        constexpr std::uint64_t gigabyte = 1ULL << 30U;

        // What the Fast budget is, in the words of plan.md 9.2.1.
        constexpr const char* fastNote =
            "Fast plans fix the worst spots within a budget of 2% of the used space or 15 minutes of moving, "
            "whichever comes first. A larger multiplier lets one Fast run do more; running Fast again also "
            "continues where the last run stopped.";

        // What Auto does, in the words of plan.md 6.6.
        constexpr const char* resourcesNote =
            "Auto leaves one processor for the window and the disk reader and uses up to 60% of the free memory "
            "(at most 8 GB). A manual value is never allowed above what the machine offers. Changes apply to the "
            "next job.";

        // A grey explanatory line, wrapped.
        wxStaticText* note(wxWindow* parent, const wxString& text) {
            auto* label = new wxStaticText(parent, wxID_ANY, text);
            label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            label->Wrap(parent->FromDIP(wrapWidth));
            return label;
        }

        // What the appearance choice does.
        constexpr const char* appearanceNote =
            "System follows Windows (Settings > Personalization > Colors > Choose your app mode). The change "
            "applies the next time Ironclad Defrag starts. The map's own colors are chosen in the Map colors tab.";

        // Whole GB of a byte count for the memory field, at least 1.
        int gigabytes(std::uint64_t bytes) {
            return static_cast<int>(std::clamp<std::uint64_t>(bytes / gigabyte, 1, 1U << 20U));
        }

        // "×2 (4% or 30 minutes)": a multiplier with the budget it gives (plan.md 9.2.1: 2% or 15 minutes at ×1).
        std::string budgetLabel(std::uint32_t multiplier) {
            return std::format("\xC3\x97{} ({}% or {} minutes{})", multiplier, 2 * multiplier, 15 * multiplier,
                               multiplier == 1 ? ", default" : "");
        }

        // A label, an "Auto" check box and a spin field in one row of a two-column grid.
        void addCapRow(wxWindow* parent, wxFlexGridSizer& grid, const char* label, wxCheckBox* automatic,
                       wxSpinCtrl* field, const char* unit) {
            grid.Add(new wxStaticText(parent, wxID_ANY, label), wxSizerFlags().CenterVertical());
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(automatic, wxSizerFlags().CenterVertical().Border(wxRIGHT, parent->FromDIP(12)));
            row->Add(field, wxSizerFlags().CenterVertical().Border(wxRIGHT, parent->FromDIP(6)));
            row->Add(new wxStaticText(parent, wxID_ANY, unit), wxSizerFlags().CenterVertical());
            grid.Add(row);
        }

    } // namespace

    SettingsDialog::SettingsDialog(wxWindow* parent, const services::Settings& settings, const ResourceFacts& facts,
                                   SettingsPage page) :
        wxDialog(parent, wxID_ANY, "Settings"), settings_(settings), facts_(facts) {
        auto* notebook = new wxNotebook(this, wxID_ANY);
        notebook->AddPage(createPlanningPage(notebook), "Planning");
        notebook->AddPage(createResourcesPage(notebook), "Resources");
        notebook->AddPage(createAppearancePage(notebook), "Appearance");
        notebook->SetSelection(static_cast<std::size_t>(page));

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(notebook, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(8)));
        sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL, FromDIP(8)));
        updateResources(); // Before fitting, so the preview line has its size.
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

    wxWindow* SettingsDialog::createPlanningPage(wxWindow* parent) {
        auto* page = new wxPanel(parent);
        multiplier_ = new wxChoice(page, wxID_ANY);
        int selection = 0;
        for (const std::uint32_t value : services::fastBudgetMultipliers()) {
            if (value == settings_.fastBudgetMultiplier) {
                selection = static_cast<int>(multiplier_->GetCount());
            }
            multiplier_->Append(wxString::FromUTF8(budgetLabel(value)));
        }
        multiplier_->SetSelection(selection);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(page, wxID_ANY, "Fast &budget:"), wxSizerFlags().CenterVertical());
        row->Add(multiplier_, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(8)));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(12);
        sizer->Add(row, wxSizerFlags().Border(wxALL, border));
        sizer->Add(note(page, fastNote), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        page->SetSizer(sizer);
        return page;
    }

    wxWindow* SettingsDialog::createResourcesPage(wxWindow* parent) {
        auto* page = new wxPanel(parent);
        const core::ResourceSettings& chosen = settings_.resources;
        const int maxThreads = static_cast<int>(std::max<std::uint32_t>(facts_.maxThreads, 1));
        autoThreads_ = new wxCheckBox(page, wxID_ANY, "&Auto");
        autoThreads_->SetValue(chosen.workerThreads == 0);
        threads_ =
            new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(80, -1)), wxSP_ARROW_KEYS,
                           1, maxThreads, std::clamp(static_cast<int>(chosen.workerThreads), 1, maxThreads));
        const int maxGb = gigabytes(facts_.detected.totalMemoryBytes);
        autoMemory_ = new wxCheckBox(page, wxID_ANY, "A&uto");
        autoMemory_->SetValue(chosen.memoryBudgetBytes == 0);
        memoryGb_ = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(80, -1)),
                                   wxSP_ARROW_KEYS, 1, maxGb, std::min(gigabytes(chosen.memoryBudgetBytes), maxGb));

        auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(12), FromDIP(6)));
        addCapRow(page, *grid, "Worker &threads:", autoThreads_, threads_, "threads");
        addCapRow(page, *grid, "&Memory budget:", autoMemory_, memoryGb_, "GB");
        preview_ = new wxStaticText(page, wxID_ANY, wxEmptyString);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(12);
        sizer->Add(grid, wxSizerFlags().Border(wxALL, border));
        sizer->Add(preview_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(note(page, resourcesNote), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        page->SetSizer(sizer);

        const auto changed = [this](wxCommandEvent&) { updateResources(); };
        autoThreads_->Bind(wxEVT_CHECKBOX, changed);
        autoMemory_->Bind(wxEVT_CHECKBOX, changed);
        threads_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { updateResources(); });
        memoryGb_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { updateResources(); });
        return page;
    }

    wxWindow* SettingsDialog::createAppearancePage(wxWindow* parent) {
        auto* page = new wxPanel(parent);
        const std::array<wxString, 3> choices{"&System", "&Light", "&Dark"};
        appearance_ = new wxRadioBox(page, wxID_ANY, "Windows", wxDefaultPosition, wxDefaultSize,
                                     static_cast<int>(choices.size()), choices.data(), 1, wxRA_SPECIFY_COLS);
        appearance_->SetSelection(static_cast<int>(settings_.appearance));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(12);
        sizer->Add(appearance_, wxSizerFlags().Border(wxALL, border));
        sizer->Add(note(page, appearanceNote), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        page->SetSizer(sizer);
        return page;
    }

    core::ResourceSettings SettingsDialog::resourceChoice() const {
        core::ResourceSettings choice;
        if (!autoThreads_->GetValue()) {
            choice.workerThreads = static_cast<std::uint32_t>(std::max(threads_->GetValue(), 1));
        }
        if (!autoMemory_->GetValue()) {
            choice.memoryBudgetBytes = static_cast<std::uint64_t>(std::max(memoryGb_->GetValue(), 1)) * gigabyte;
        }
        return choice;
    }

    void SettingsDialog::updateResources() {
        const bool autoThreads = autoThreads_->GetValue();
        const bool autoMemory = autoMemory_->GetValue();
        threads_->Enable(!autoThreads);
        memoryGb_->Enable(!autoMemory);
        const core::ResourceCaps caps = core::computeCaps(facts_.detected, resourceChoice());
        // Auto fields show what Auto picks, which is also where a switch to manual starts from.
        if (autoThreads) {
            threads_->SetValue(static_cast<int>(caps.workerThreads));
        }
        if (autoMemory) {
            memoryGb_->SetValue(gigabytes(caps.memoryBudgetBytes));
        }
        preview_->SetLabel(wxString::FromUTF8("In effect: " + core::describeCaps(facts_.detected, caps)));
        preview_->Wrap(FromDIP(wrapWidth));
        if (wxSizer* sizer = GetSizer(); sizer != nullptr) {
            sizer->Fit(this); // A wrapped preview may need another line.
        }
    }

    services::Settings SettingsDialog::settings() const {
        services::Settings result = settings_;
        const auto multipliers = services::fastBudgetMultipliers();
        const auto index = static_cast<std::size_t>(std::max(multiplier_->GetSelection(), 0));
        result.fastBudgetMultiplier = multipliers[std::min(index, multipliers.size() - 1)];
        result.resources = resourceChoice();
        result.appearance = static_cast<services::Appearance>(std::clamp(appearance_->GetSelection(), 0, 2));
        return result;
    }

    bool editSettings(wxWindow* parent, services::AppServices& services, SettingsPage page, wxString& status) {
        const ResourceFacts facts{.detected = services.detectResources(),
                                  .maxThreads = static_cast<std::uint32_t>(services.pool().capacity())};
        SettingsDialog dialog(parent, services.settings().current(), facts, page);
        if (dialog.ShowModal() != wxID_OK) {
            return false;
        }
        services::Settings next = dialog.settings();
        if (next == services.settings().current()) {
            return false;
        }
        const bool restart = next.appearance != services.settings().current().appearance;
        const bool saved = services.applySettings(std::move(next));
        status = saved ? wxString("Settings saved; they apply to the next plan and job")
                       : wxString("Settings apply to this run but could not be saved (see the log)");
        if (saved && restart) {
            status += "; the new appearance applies after a restart";
        }
        return true;
    }

} // namespace icd::ui
