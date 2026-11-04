#include "ui/dialogs/VssWarning.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "ui/AppIcon.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <format>

namespace icd::ui {

    namespace {

        // Width at which the explanation wraps, in DIPs.
        constexpr int wrapWidth = 520;

        // Why unaligned moves matter and what each button does.
        std::string bodyOf(const VssWarningFacts& f) {
            return std::format(
                "{} of the {} moves of this plan are not aligned to the 16 KB blocks shadow copies track. Each of them "
                "can make the shadow copies of {} grow, by up to {} in all. When their storage limit is reached, "
                "Windows deletes the oldest shadow copies (restore points, previous versions).\n\nBlocking leaves "
                "those moves (and the moves that wait on them) out of a new plan, so shadow storage grows only by the "
                "file system's own records of the moves. Tightly packed zones rarely line up with the blocks, so the "
                "new plan may move much less.",
                core::formatCount(f.unalignedMoves), core::formatCount(f.moves), f.drive,
                core::formatBytes(f.growthBytes));
        }

    } // namespace

    VssWarningDialog::VssWarningDialog(wxWindow* parent, const VssWarningFacts& facts) :
        wxDialog(parent, wxID_ANY, "Shadow copies") {
        SetIcons(appIcons());
        auto* heading = new wxStaticText(this, wxID_ANY, "This plan will make the shadow copies grow");
        heading->SetFont(heading->GetFont().Scaled(1.2F).Bold());
        auto* body = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(bodyOf(facts)));
        body->Wrap(FromDIP(wrapWidth));
        auto* block = new wxButton(this, blockAndReplan, "&Block unaligned moves and plan again");
        auto* anyway = new wxButton(this, wxID_OK, "&Execute anyway");
        auto* cancel = new wxButton(this, wxID_CANCEL, "&Cancel");
        block->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(blockAndReplan); });
        cancel->SetDefault(); // Enter alone never starts moving files.
        cancel->SetFocus();
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(block, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(anyway, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(cancel);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(16);
        sizer->Add(heading, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, border));
        sizer->Add(body, wxSizerFlags().Border(wxALL, border));
        sizer->Add(buttons, wxSizerFlags().Right().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

} // namespace icd::ui
