#pragma once

#include "../model/DomainTypes.h"

#include <vector>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>

namespace icd {

// Edits the active optimization profile in memory without touching disk persistence.
class ProfileSettingsDialog : public wxDialog {
public:
    ProfileSettingsDialog(wxWindow* parent,
                          std::vector<OptimizationProfile> profiles,
                          const OptimizationProfile& activeProfile);

    OptimizationProfile GetSelectedProfile() const;

private:
    void LoadProfileToControls(std::size_t index);
    void StoreControlsToProfile(std::size_t index);
    void OnProfileChanged(wxCommandEvent& event);
    void OnOk(wxCommandEvent& event);

    std::vector<OptimizationProfile> profiles;
    std::size_t selectedIndex = 0;
    wxChoice* profileChoice = nullptr;
    wxCheckBox* fastZone = nullptr;
    wxCheckBox* balancedZone = nullptr;
    wxCheckBox* slowZone = nullptr;
    wxCheckBox* largeFileZone = nullptr;
    wxCheckBox* freeSpaceReserve = nullptr;
    wxSpinCtrl* smallFileMiB = nullptr;
    wxSpinCtrl* largeFileMiB = nullptr;
    wxSpinCtrl* hugeFileGiB = nullptr;
    wxSpinCtrl* hotDays = nullptr;
    wxSpinCtrl* warmDays = nullptr;
    wxSpinCtrl* coolDays = nullptr;
    wxSpinCtrl* coldDays = nullptr;
    wxSpinCtrl* staleDays = nullptr;
    wxTextCtrl* minimumBenefit = nullptr;
    wxSpinCtrl* maximumBytesGiB = nullptr;
    wxCheckBox* preserveDirectoryLocality = nullptr;
    wxCheckBox* allowLargeFileMoves = nullptr;
    wxCheckBox* prioritizeFreeSpace = nullptr;
    wxCheckBox* dryRunOnly = nullptr;
};

} // namespace icd
