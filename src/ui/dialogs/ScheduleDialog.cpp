#include "ui/dialogs/ScheduleDialog.h"

#include <wx/button.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include "ui/AppIcon.h"

#include <format>

namespace icd::ui {

    namespace {

        // Width at which the explanations wrap, in DIPs.
        constexpr int wrapWidth = 500;

        // The texts of the dialog for one direction of the change.
        struct ScheduleTexts {
            std::string heading; // The question.
            std::string body; // Why, and what changes.
            const char* button = ""; // The confirming button.
        };

        // The texts for taking the drive out of the schedule, or putting it back.
        ScheduleTexts textsFor(const ScheduleQuestion& what) {
            if (what.scheduled) {
                return {
                    .heading = std::format("Remove {} from Windows' scheduled optimization?", what.drive),
                    .body = std::format(
                        "Windows optimizes {} on a schedule (Optimize Drives, weekly by default). Its defragmentation "
                        "knows nothing of this app's zones and may undo a zone layout, moving files out of their "
                        "zones again.\n\nOnly this drive's check box in Optimize Drives > Change settings > Choose "
                        "changes; the other drives and the schedule stay as they are. The drive can be put back here "
                        "or in Optimize Drives at any time.",
                        what.drive),
                    .button = "&Remove from schedule",
                };
            }
            return {
                .heading = std::format("Put {} back on Windows' scheduled optimization?", what.drive),
                .body = std::format("Windows' scheduled optimization (Optimize Drives) leaves {} out now. Putting it "
                                    "back lets Windows defragment it on its schedule again, which may undo zone "
                                    "layouts built here.",
                                    what.drive),
                .button = "&Put back on schedule",
            };
        }

    } // namespace

    ScheduleDialog::ScheduleDialog(wxWindow* parent, const ScheduleQuestion& what) :
        wxDialog(parent, wxID_ANY, "Windows scheduled optimization") {
        SetIcons(appIcons());
        const ScheduleTexts texts = textsFor(what);
        auto* heading = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(texts.heading));
        heading->SetFont(heading->GetFont().Scaled(1.2F).Bold());
        auto* body = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(texts.body));
        body->Wrap(FromDIP(wrapWidth));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(16);
        sizer->Add(heading, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, border));
        sizer->Add(body, wxSizerFlags().Border(wxALL, border));
        if (what.simulated) {
            auto* note = new wxStaticText(this, wxID_ANY,
                                          "This is the fake drive list: only a simulated schedule "
                                          "changes, not the one in Windows.");
            note->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            note->Wrap(FromDIP(wrapWidth));
            sizer->Add(note, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        }
        auto* change = new wxButton(this, wxID_OK, texts.button);
        auto* cancel = new wxButton(this, wxID_CANCEL, "&Cancel");
        cancel->SetDefault(); // Enter alone never changes the system's schedule.
        cancel->SetFocus();
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(change, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(cancel);
        sizer->Add(buttons, wxSizerFlags().Right().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

} // namespace icd::ui
