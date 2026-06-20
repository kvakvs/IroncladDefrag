#pragma once

#include "../model/DomainTypes.h"

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>

namespace icd {

// Edits global safety guardrails that apply across profiles.
class SafetySettingsDialog : public wxDialog {
public:
    SafetySettingsDialog(wxWindow* parent, const SafetySettings& settings);

    SafetySettings GetSafetySettings() const;

private:
    void OnOk(wxCommandEvent& event);
    void LoadSettings(const SafetySettings& settings);
    bool StoreSettings();

    wxCheckBox* defaultDryRunOnly = nullptr;
    wxCheckBox* excludeProtectedSystemPaths = nullptr;
    wxSpinCtrl* maximumBytesGiB = nullptr;
    wxTextCtrl* excludedDirectories = nullptr;
    wxTextCtrl* excludedExtensions = nullptr;
    wxTextCtrl* excludedSizeRanges = nullptr;
    SafetySettings safetySettings;
};

} // namespace icd
