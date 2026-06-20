#pragma once

#include "../model/DomainTypes.h"
#include "DriveMapPanel.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/wx.h>

namespace icd {

// Shows the current read-only analysis snapshot for one drive document tab.
class DriveAnalysisPage : public wxPanel {
public:
    DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result);

    void UpdateResult(const AnalysisResult& result);
    void UpdatePlacementPlan(const PlacementPlan& plan);
    void UpdateMovePlan(const MovePlan& plan);
    const std::wstring& GetDriveRoot() const { return driveRoot; }

private:
    void OnMapControlsChanged(wxCommandEvent& event);
    void UpdateMapControls();

    DriveMapPanel* mapPanel = nullptr;
    wxChoice* mapModeChoice = nullptr;
    wxChoice* classFilterChoice = nullptr;
    wxCheckBox* plannedMovesCheck = nullptr;
    wxStaticText* legend = nullptr;
    std::wstring driveRoot;
};

} // namespace icd
