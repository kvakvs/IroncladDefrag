#include "ui/dialogs/AboutDialog.h"

#include "core/format/ByteSize.h"
#include "core/resources/ResourceCaps.h"
#include "services/AppInfo.h"
#include "services/AppServices.h"
#include "ui/AppIcon.h"

#include <wx/button.h>
#include <wx/font.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/version.h>

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace icd::ui {

    namespace {

        // Label and value rows of the dialog.
        std::vector<std::pair<std::string, std::string>> facts(const services::AppServices& services) {
            const core::DetectedResources now = services.detectResources();
            const core::DetectedResources& capped = services.resources(); // What the caps were computed from.
            return {
                {"Version", std::string(services::appVersion())},
                {"Build", services::buildDescription() + ", wxWidgets " + wxVERSION_NUM_DOT_STRING},
                {"Processors", std::format("{} usable, {} physical cores", now.logicalProcessors, now.physicalCores)},
                {"Memory",
                 std::format("{} installed, {} available now", core::formatBytes(now.totalMemoryBytes),
                             core::formatBytes(now.availableMemoryBytes))},
                {"Active caps", core::describeCaps(capped, services.caps())},
            };
        }

    } // namespace

    AboutDialog::AboutDialog(wxWindow* parent, const services::AppServices& services) :
        wxDialog(parent, wxID_ANY, wxString::FromUTF8("About " + std::string(services::appName()))) {
        SetIcons(appIcons());
        auto* title = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(std::string(services::appName())));
        title->SetFont(title->GetFont().Scaled(1.6F).Bold());
        auto* tagline = new wxStaticText(this, wxID_ANY, "Disk analyser and defragmenter for NTFS hard drives");
        tagline->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(16), FromDIP(6)));
        for (const auto& [label, value] : facts(services)) {
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label + ":")));
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(value)));
        }

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(title, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
        sizer->Add(tagline, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(16)));
        sizer->Add(grid, wxSizerFlags().Border(wxALL, FromDIP(16)));
        sizer->Add(CreateStdDialogButtonSizer(wxOK), wxSizerFlags().Expand().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

} // namespace icd::ui
