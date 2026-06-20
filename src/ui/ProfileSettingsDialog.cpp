#include "../precompiled.h"
#include "ProfileSettingsDialog.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <utility>

namespace icd {

namespace {
constexpr std::uint64_t MiB = 1024ull * 1024ull;
constexpr std::uint64_t GiB = 1024ull * MiB;
constexpr int HoursPerDay = 24;

int MiBValue(byte_count64_t bytes)
{
    return static_cast<int>((bytes.getValue() + MiB - 1) / MiB);
}

int GiBValue(byte_count64_t bytes)
{
    return static_cast<int>((bytes.getValue() + GiB - 1) / GiB);
}

int DaysValue(std::chrono::hours value)
{
    return static_cast<int>(value.count() / HoursPerDay);
}

std::chrono::hours DaysToHours(int days)
{
    return std::chrono::hours(days * HoursPerDay);
}

double ParseDouble(const wxString& text, double fallback)
{
    double value = fallback;
    return text.ToDouble(&value) ? value : fallback;
}

wxSpinCtrl* AddSpin(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, int minValue, int maxValue)
{
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    auto* spin = new wxSpinCtrl(parent, wxID_ANY);
    spin->SetRange(minValue, maxValue);
    grid->Add(spin, 0, wxEXPAND | wxALL, 4);
    return spin;
}

wxCheckBox* AddCheck(wxWindow* parent, wxBoxSizer* sizer, const wxString& label)
{
    auto* check = new wxCheckBox(parent, wxID_ANY, label);
    sizer->Add(check, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    return check;
}
} // namespace

ProfileSettingsDialog::ProfileSettingsDialog(wxWindow* parent,
                                             std::vector<OptimizationProfile> dialogProfiles,
                                             const OptimizationProfile& activeProfile)
    : wxDialog(parent, wxID_ANY, "Optimization Profiles", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      profiles(std::move(dialogProfiles))
{
    if (profiles.empty()) {
        profiles.push_back(activeProfile);
    }

    for (std::size_t index = 0; index < profiles.size(); ++index) {
        if (profiles[index].mode == activeProfile.mode) {
            profiles[index] = activeProfile;
            selectedIndex = index;
            break;
        }
    }

    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* profileRow = new wxBoxSizer(wxHORIZONTAL);
    profileRow->Add(new wxStaticText(this, wxID_ANY, "Profile"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
    profileChoice = new wxChoice(this, wxID_ANY);
    for (const OptimizationProfile& profile : profiles) {
        profileChoice->Append(wxString(profile.name));
    }
    profileChoice->SetSelection(static_cast<int>(selectedIndex));
    profileRow->Add(profileChoice, 1, wxEXPAND | wxALL, 8);
    root->Add(profileRow, 0, wxEXPAND);

    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    auto* checks = new wxBoxSizer(wxVERTICAL);
    checks->Add(new wxStaticText(this, wxID_ANY, "Zones and behavior"), 0, wxALL, 8);
    fastZone = AddCheck(this, checks, "Fast zone");
    balancedZone = AddCheck(this, checks, "Balanced zone");
    slowZone = AddCheck(this, checks, "Slow zone");
    largeFileZone = AddCheck(this, checks, "Large-file zone");
    freeSpaceReserve = AddCheck(this, checks, "Free-space reserve");
    preserveDirectoryLocality = AddCheck(this, checks, "Preserve directory locality");
    allowLargeFileMoves = AddCheck(this, checks, "Allow large-file moves");
    prioritizeFreeSpace = AddCheck(this, checks, "Prioritize free-space consolidation");
    dryRunOnly = AddCheck(this, checks, "Dry-run only");
    columns->Add(checks, 0, wxEXPAND | wxALL, 4);

    auto* grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1, 1);
    smallFileMiB = AddSpin(this, grid, "Small file threshold (MiB)", 1, 1048576);
    largeFileMiB = AddSpin(this, grid, "Large file threshold (MiB)", 1, 1048576);
    hugeFileGiB = AddSpin(this, grid, "Huge file threshold (GiB)", 1, 1024);
    hotDays = AddSpin(this, grid, "Hot days", 1, 3650);
    warmDays = AddSpin(this, grid, "Warm days", 1, 3650);
    coolDays = AddSpin(this, grid, "Cool days", 1, 3650);
    coldDays = AddSpin(this, grid, "Cold days", 1, 3650);
    staleDays = AddSpin(this, grid, "Stale days", 1, 3650);
    grid->Add(new wxStaticText(this, wxID_ANY, "Minimum benefit"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    minimumBenefit = new wxTextCtrl(this, wxID_ANY);
    grid->Add(minimumBenefit, 0, wxEXPAND | wxALL, 4);
    maximumBytesGiB = AddSpin(this, grid, "Maximum bytes to move (GiB, 0 unlimited)", 0, 1024 * 1024);
    columns->Add(grid, 1, wxEXPAND | wxALL, 4);
    root->Add(columns, 1, wxEXPAND);

    auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    root->Add(buttons, 0, wxEXPAND | wxALL, 8);
    SetSizerAndFit(root);
    SetMinSize(wxSize(640, 480));

    profileChoice->Bind(wxEVT_CHOICE, &ProfileSettingsDialog::OnProfileChanged, this);
    Bind(wxEVT_BUTTON, &ProfileSettingsDialog::OnOk, this, wxID_OK);
    LoadProfileToControls(selectedIndex);
}

OptimizationProfile ProfileSettingsDialog::GetSelectedProfile() const
{
    return profiles[selectedIndex];
}

void ProfileSettingsDialog::LoadProfileToControls(std::size_t index)
{
    const OptimizationSettings& settings = profiles[index].settings;
    fastZone->SetValue(settings.enableFastZone);
    balancedZone->SetValue(settings.enableBalancedZone);
    slowZone->SetValue(settings.enableSlowZone);
    largeFileZone->SetValue(settings.enableLargeFileZone);
    freeSpaceReserve->SetValue(settings.enableFreeSpaceReserve);
    smallFileMiB->SetValue(MiBValue(settings.smallFileThreshold));
    largeFileMiB->SetValue(MiBValue(settings.largeFileThreshold));
    hugeFileGiB->SetValue(GiBValue(settings.hugeFileThreshold));
    hotDays->SetValue(DaysValue(settings.hotRecency));
    warmDays->SetValue(DaysValue(settings.warmRecency));
    coolDays->SetValue(DaysValue(settings.coolRecency));
    coldDays->SetValue(DaysValue(settings.coldRecency));
    staleDays->SetValue(DaysValue(settings.staleRecency));
    minimumBenefit->SetValue(wxString::Format("%.2f", settings.minimumBenefitScore));
    maximumBytesGiB->SetValue(GiBValue(settings.maximumBytesToMove));
    preserveDirectoryLocality->SetValue(settings.preserveDirectoryLocality);
    allowLargeFileMoves->SetValue(settings.allowLargeFileMoves);
    prioritizeFreeSpace->SetValue(settings.prioritizeFreeSpaceConsolidation);
    dryRunOnly->SetValue(settings.dryRunOnly);
}

void ProfileSettingsDialog::StoreControlsToProfile(std::size_t index)
{
    OptimizationSettings& settings = profiles[index].settings;
    settings.enableFastZone = fastZone->GetValue();
    settings.enableBalancedZone = balancedZone->GetValue();
    settings.enableSlowZone = slowZone->GetValue();
    settings.enableLargeFileZone = largeFileZone->GetValue();
    settings.enableFreeSpaceReserve = freeSpaceReserve->GetValue();
    settings.smallFileThreshold = byte_count64_t(static_cast<std::uint64_t>(smallFileMiB->GetValue()) * MiB);
    settings.largeFileThreshold = byte_count64_t(static_cast<std::uint64_t>(largeFileMiB->GetValue()) * MiB);
    settings.hugeFileThreshold = byte_count64_t(static_cast<std::uint64_t>(hugeFileGiB->GetValue()) * GiB);
    settings.hotRecency = DaysToHours(hotDays->GetValue());
    settings.warmRecency = DaysToHours(warmDays->GetValue());
    settings.coolRecency = DaysToHours(coolDays->GetValue());
    settings.coldRecency = DaysToHours(coldDays->GetValue());
    settings.staleRecency = DaysToHours(staleDays->GetValue());
    settings.minimumBenefitScore = ParseDouble(minimumBenefit->GetValue(), settings.minimumBenefitScore);
    settings.maximumBytesToMove = byte_count64_t(static_cast<std::uint64_t>(maximumBytesGiB->GetValue()) * GiB);
    settings.preserveDirectoryLocality = preserveDirectoryLocality->GetValue();
    settings.allowLargeFileMoves = allowLargeFileMoves->GetValue();
    settings.prioritizeFreeSpaceConsolidation = prioritizeFreeSpace->GetValue();
    settings.dryRunOnly = dryRunOnly->GetValue();
}

void ProfileSettingsDialog::OnProfileChanged(wxCommandEvent&)
{
    StoreControlsToProfile(selectedIndex);
    selectedIndex = static_cast<std::size_t>(profileChoice->GetSelection());
    LoadProfileToControls(selectedIndex);
}

void ProfileSettingsDialog::OnOk(wxCommandEvent& event)
{
    StoreControlsToProfile(selectedIndex);
    event.Skip();
}

} // namespace icd
