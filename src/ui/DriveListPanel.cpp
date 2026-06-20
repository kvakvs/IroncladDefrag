#include "../precompiled.h"
#include "DriveListPanel.h"

#include "UIFormatting.h"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace icd {

DriveListPanel::DriveListPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* header = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Disks");
    wxFont titleFont = title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    header->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    header->AddStretchSpacer(1);

    refreshButton = new wxButton(this, wxID_ANY, "Refresh");
    analyzeButton = new wxButton(this, wxID_ANY, "Analyze");
    header->Add(refreshButton, 0, wxALL, 4);
    header->Add(analyzeButton, 0, wxALL, 4);
    root->Add(header, 0, wxEXPAND);

    drivesList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 126), wxLC_REPORT | wxLC_SINGLE_SEL);
    drivesList->AppendColumn("Drive", wxLIST_FORMAT_LEFT, 220);
    drivesList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 90);
    drivesList->AppendColumn("File system", wxLIST_FORMAT_LEFT, 90);
    drivesList->AppendColumn("Capacity", wxLIST_FORMAT_RIGHT, 110);
    drivesList->AppendColumn("Free", wxLIST_FORMAT_RIGHT, 110);
    drivesList->AppendColumn("Badges", wxLIST_FORMAT_LEFT, 230);
    drivesList->AppendColumn("Status", wxLIST_FORMAT_LEFT, 380);
    root->Add(drivesList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(root);

    refreshButton->Bind(wxEVT_BUTTON, &DriveListPanel::OnRefresh, this);
    analyzeButton->Bind(wxEVT_BUTTON, &DriveListPanel::OnAnalyze, this);
    drivesList->Bind(wxEVT_LIST_ITEM_SELECTED, &DriveListPanel::OnSelectionChanged, this);
    drivesList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &DriveListPanel::OnItemActivated, this);
    UpdateButtons();
}

void DriveListPanel::SetDrives(const std::vector<DriveInfo>& nextDrives)
{
    drives = nextDrives;
    if (!selectedDriveRoot.empty()) {
        const auto found = std::find_if(drives.begin(), drives.end(), [this](const DriveInfo& drive) {
            return drive.rootPath == selectedDriveRoot;
        });
        if (found == drives.end()) {
            selectedDriveRoot.clear();
        }
    }
    PopulateRows();
    UpdateButtons();
}

void DriveListPanel::SetSelectedDrive(const std::wstring& driveRoot)
{
    selectedDriveRoot = driveRoot;
    for (long row = 0; row < drivesList->GetItemCount(); ++row) {
        const auto index = static_cast<std::size_t>(drivesList->GetItemData(row));
        const bool selected = index < drives.size() && drives[index].rootPath == selectedDriveRoot;
        drivesList->SetItemState(row, selected ? wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED : 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        if (selected) {
            drivesList->EnsureVisible(row);
        }
    }
    UpdateButtons();
}

std::optional<std::wstring> DriveListPanel::GetSelectedDriveRoot() const
{
    if (selectedDriveRoot.empty()) {
        return std::nullopt;
    }
    return selectedDriveRoot;
}

void DriveListPanel::SetBusy(bool running)
{
    busy = running;
    UpdateButtons();
}

void DriveListPanel::PopulateRows()
{
    drivesList->DeleteAllItems();
    long row = 0;
    for (std::size_t index = 0; index < drives.size(); ++index) {
        const DriveInfo& drive = drives[index];
        drivesList->InsertItem(row, wxString(drive.displayName));
        drivesList->SetItem(row, 1, wxString(ui::DriveKindName(drive.kind)));
        drivesList->SetItem(row, 2, wxString(drive.volume.fileSystem));
        drivesList->SetItem(row, 3, wxString(ui::FormatBytes(drive.volume.totalBytes)));
        drivesList->SetItem(row, 4, wxString(ui::FormatBytes(drive.volume.freeBytes)));
        drivesList->SetItem(row, 5, wxString(ui::CapabilityBadge(drive)));

        wxString status;
        if (!drive.capabilities.disabledReason.empty()) {
            status = wxString(drive.capabilities.disabledReason);
        } else if (!drive.capabilities.statusReason.empty()) {
            status = wxString(drive.capabilities.statusReason);
        } else {
            status = drive.capabilities.canAnalyze ? "Ready" : "Unavailable";
        }
        drivesList->SetItem(row, 6, status);
        drivesList->SetItemData(row, static_cast<long>(index));
        ++row;
    }

    if (!selectedDriveRoot.empty()) {
        SetSelectedDrive(selectedDriveRoot);
    }
}

void DriveListPanel::UpdateButtons()
{
    refreshButton->Enable(!busy);
    const std::optional<DriveInfo> selected = GetSelectedDrive();
    analyzeButton->Enable(!busy && selected.has_value() && selected->capabilities.canAnalyze);
}

std::optional<DriveInfo> DriveListPanel::GetSelectedDrive() const
{
    if (selectedDriveRoot.empty()) {
        return std::nullopt;
    }

    const auto found = std::find_if(drives.begin(), drives.end(), [this](const DriveInfo& drive) {
        return drive.rootPath == selectedDriveRoot;
    });
    if (found == drives.end()) {
        return std::nullopt;
    }
    return *found;
}

void DriveListPanel::OnRefresh(wxCommandEvent&)
{
    if (refreshCallback) {
        refreshCallback();
    }
}

void DriveListPanel::OnAnalyze(wxCommandEvent&)
{
    const std::optional<DriveInfo> selected = GetSelectedDrive();
    if (selected.has_value() && analyzeCallback) {
        analyzeCallback(*selected);
    }
}

void DriveListPanel::OnSelectionChanged(wxListEvent& event)
{
    const auto index = static_cast<std::size_t>(event.GetData());
    if (index >= drives.size()) {
        return;
    }

    selectedDriveRoot = drives[index].rootPath;
    UpdateButtons();
    if (selectionChangedCallback) {
        selectionChangedCallback(drives[index]);
    }
}

void DriveListPanel::OnItemActivated(wxListEvent&)
{
    const std::optional<DriveInfo> selected = GetSelectedDrive();
    if (selected.has_value() && selected->capabilities.canAnalyze && analyzeCallback && !busy) {
        analyzeCallback(*selected);
    }
}

} // namespace icd
