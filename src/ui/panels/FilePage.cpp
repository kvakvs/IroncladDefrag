#include "ui/panels/FilePage.h"

#include "ui/Accelerators.h"
#include "ui/Commands.h"

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

#include <format>

namespace icd::ui {

    namespace {

        // What the page says without a selected file.
        constexpr const char* emptyHint = "Click a file on the map to see it here, or find one by name.";

        // Facts per row of the grid (label and value each).
        constexpr int factsPerRow = 3;

        // Sends a View command to the frame, as the menu would.
        void postCommand(wxWindow* from, int id) {
            if (wxWindow* frame = wxGetTopLevelParent(from); frame != nullptr) {
                wxCommandEvent command(wxEVT_MENU, id);
                wxPostEvent(frame, command);
            }
        }

        // "Fragment 2 of 5" for the fragment the map shows.
        std::string fragmentText(const FileView& view) {
            if (view.fragments < 2) {
                return {};
            }
            return std::format("  (map shows fragment {} of {})", view.fragment + 1, view.fragments);
        }

    } // namespace

    FilePage::FilePage(wxWindow* parent) : wxPanel(parent) {
        path_ = new wxStaticText(this, wxID_ANY, emptyHint, wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_MIDDLE);
        facts_ = new wxFlexGridSizer(factsPerRow * 2, wxSize(FromDIP(8), FromDIP(4))); // Compact, left-aligned.
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(8);
        sizer->Add(createToolbar(), wxSizerFlags().Expand().Border(wxALL, border));
        sizer->Add(path_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(facts_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        SetSizer(sizer);
        clear();
    }

    wxSizer* FilePage::createToolbar() {
        search_ = new wxSearchCtrl(this, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(240, -1)), wxTE_PROCESS_ENTER);
        search_->SetDescriptiveText("Find file");
        search_->SetToolTip(commandTip(ID_GOTO_FILE));
        search_->Bind(wxEVT_SEARCH, [this](wxCommandEvent&) {
            if (actions_.search) {
                actions_.search(search_->GetValue());
            }
        });
        search_->Disable(); // Until a map is shown.
        explorer_ = new wxButton(this, wxID_ANY, "Show in E&xplorer");
        explorer_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (actions_.showInExplorer) {
                actions_.showInExplorer();
            }
        });
        copy_ = new wxButton(this, wxID_ANY, "Cop&y path");
        copy_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (actions_.copyPath) {
                actions_.copyPath();
            }
        });
        previous_ = new wxButton(this, wxID_ANY, "Previous fragment");
        previous_->SetToolTip(commandTip(ID_PREVIOUS_FRAGMENT));
        previous_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { postCommand(this, ID_PREVIOUS_FRAGMENT); });
        next_ = new wxButton(this, wxID_ANY, "&Next fragment");
        next_->SetToolTip(commandTip(ID_NEXT_FRAGMENT));
        next_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { postCommand(this, ID_NEXT_FRAGMENT); });

        // Its command id lets the frame's command handler and enable rule cover it like the menu item.
        defrag_ = new wxButton(this, ID_DEFRAG_FILE, "Defra&gment this file...");
        defrag_->SetToolTip(
            "Move this file into one piece (or a few large ones) now, checked against the volume first");
        defrag_->Disable();

        auto* bar = new wxBoxSizer(wxHORIZONTAL);
        bar->Add(search_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(16)));
        for (wxButton* button : {explorer_, copy_, previous_, next_}) {
            bar->Add(button, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(4)));
        }
        bar->Add(defrag_, wxSizerFlags().CenterVertical().Border(wxLEFT, FromDIP(12)));
        return bar;
    }

    void FilePage::showFile(const FileView& view) {
        path_->SetLabelText(wxString::FromUTF8(view.path + fragmentText(view)));
        path_->SetToolTip(wxString::FromUTF8(view.path)); // The label may be cut in the middle.
        path_->SetFont(GetFont().Bold());
        path_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
        showFacts(view.facts);
        explorer_->Enable(view.openable);
        copy_->Enable(!view.path.empty());
        previous_->Enable(view.fragments > 1);
        next_->Enable(view.fragments > 1);
        Layout();
    }

    void FilePage::clear() {
        path_->SetLabelText(emptyHint);
        path_->SetToolTip(wxString());
        path_->SetFont(GetFont());
        path_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        showFacts({});
        for (wxButton* button : {explorer_, copy_, previous_, next_}) {
            button->Disable();
        }
        Layout();
    }

    void FilePage::showFacts(const std::vector<core::FileFact>& facts) {
        facts_->Clear(true);
        for (const core::FileFact& fact : facts) {
            auto* label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(fact.label + ":"));
            label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            facts_->Add(label);
            facts_->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(fact.value)),
                        wxSizerFlags().Border(wxRIGHT, FromDIP(32)));
        }
    }

    void FilePage::setSearchEnabled(bool enabled) { search_->Enable(enabled); }

    wxString FilePage::query() const { return search_->GetValue(); }

} // namespace icd::ui
