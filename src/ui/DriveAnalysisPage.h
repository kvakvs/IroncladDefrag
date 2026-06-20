#pragma once

#include "../model/DomainTypes.h"
#include "DriveMapPanel.h"

#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <wx/tglbtn.h>
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
    void SetDefaultSashPosition(const wxSize& size = wxDefaultSize);
    void OnSize(wxSizeEvent& event);
    void OnMapModeToggle(wxCommandEvent& event);
    void UpdateMapModeToggle();

    wxSplitterWindow* splitter = nullptr;
    DriveMapPanel* mapPanel = nullptr;
    wxScrolledWindow* detailsPanel = nullptr;
    wxStaticText* title = nullptr;
    wxStaticText* volume = nullptr;
    wxStaticText* files = nullptr;
    wxStaticText* fragmentation = nullptr;
    wxStaticText* freeSpace = nullptr;
    wxStaticText* classificationSizes = nullptr;
    wxStaticText* classificationTypes = nullptr;
    wxStaticText* classificationRecency = nullptr;
    wxStaticText* classificationPlacement = nullptr;
    wxStaticText* classificationSafety = nullptr;
    wxToggleButton* mapModeToggle = nullptr;
    wxStaticText* placementIntent = nullptr;
    wxStaticText* movePlan = nullptr;
    wxStaticText* legend = nullptr;
    wxStaticText* warnings = nullptr;
    wxStaticText* todo = nullptr;
    std::wstring driveRoot;
};

} // namespace icd
