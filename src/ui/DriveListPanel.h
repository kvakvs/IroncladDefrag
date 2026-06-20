#pragma once

#include "../model/DomainTypes.h"

#include <functional>
#include <optional>
#include <vector>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>

namespace icd {

// Presents discovered drives and forwards user commands to the owning frame.
class DriveListPanel : public wxPanel {
public:
    using DriveCallback = std::function<void(const DriveInfo&)>;
    using SimpleCallback = std::function<void()>;

    explicit DriveListPanel(wxWindow* parent);

    void SetDrives(const std::vector<DriveInfo>& nextDrives);
    void SetSelectedDrive(const std::wstring& driveRoot);
    std::optional<std::wstring> GetSelectedDriveRoot() const;

    void SetRefreshCallback(SimpleCallback callback) { refreshCallback = std::move(callback); }
    void SetAnalyzeCallback(DriveCallback callback) { analyzeCallback = std::move(callback); }
    void SetSelectionChangedCallback(DriveCallback callback) { selectionChangedCallback = std::move(callback); }
    void SetBusy(bool running);

private:
    void PopulateRows();
    void UpdateButtons();
    std::optional<DriveInfo> GetSelectedDrive() const;
    void OnRefresh(wxCommandEvent& event);
    void OnAnalyze(wxCommandEvent& event);
    void OnSelectionChanged(wxListEvent& event);
    void OnItemActivated(wxListEvent& event);

    wxListCtrl* drivesList = nullptr;
    wxButton* refreshButton = nullptr;
    wxButton* analyzeButton = nullptr;
    std::vector<DriveInfo> drives;
    std::wstring selectedDriveRoot;
    bool busy = false;
    SimpleCallback refreshCallback;
    DriveCallback analyzeCallback;
    DriveCallback selectionChangedCallback;
};

} // namespace icd
