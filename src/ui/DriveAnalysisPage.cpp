#include "../precompiled.h"
#include "DriveAnalysisPage.h"

namespace icd {

namespace {
    const char* ActualMapLegend() {
        return "Legend: red risky, orange fragmented, blue hot, purple cold, green occupied, "
               "light gray free, dark gray unknown.";
    }

    const char* IntendedMapLegend() {
        return "Legend: red risky/excluded, blue fast target, purple slow target, teal large-file target, "
               "green balanced target, light gray free, dark gray no target.";
    }

    const char* PlannedMapLegend() {
        return "Legend: actual colors plus red source outlines and yellow target outlines for planned moves.";
    }
} // namespace

// Builds the map-only document page while leaving textual workflow details to WorkflowPanel.
DriveAnalysisPage::DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result) : wxPanel(parent, wxID_ANY) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* mapControls = new wxBoxSizer(wxHORIZONTAL);
    mapModeChoice = new wxChoice(this, wxID_ANY);
    mapModeChoice->Append("Actual");
    mapModeChoice->Append("Intended");
    mapModeChoice->Append("Planned");
    mapModeChoice->SetSelection(0);
    classFilterChoice = new wxChoice(this, wxID_ANY);
    classFilterChoice->Append("All");
    classFilterChoice->Append("Hot");
    classFilterChoice->Append("Cold");
    classFilterChoice->Append("Large");
    classFilterChoice->Append("Fragmented");
    classFilterChoice->Append("Risky");
    classFilterChoice->Append("Free");
    classFilterChoice->SetSelection(0);
    plannedMovesCheck = new wxCheckBox(this, wxID_ANY, "Move outlines");
    plannedMovesCheck->Enable(false);
    mapControls->Add(new wxStaticText(this, wxID_ANY, "Map"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    mapControls->Add(mapModeChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    mapControls->Add(new wxStaticText(this, wxID_ANY, "Filter"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    mapControls->Add(classFilterChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    mapControls->Add(plannedMovesCheck, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    root->Add(mapControls, 0, wxEXPAND);

    legend = new wxStaticText(this, wxID_ANY, ActualMapLegend());
    root->Add(legend, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    mapPanel = new DriveMapPanel(this, result);
    root->Add(mapPanel, 1, wxEXPAND);
    SetSizer(root);

    mapModeChoice->Bind(wxEVT_CHOICE, &DriveAnalysisPage::OnMapControlsChanged, this);
    classFilterChoice->Bind(wxEVT_CHOICE, &DriveAnalysisPage::OnMapControlsChanged, this);
    plannedMovesCheck->Bind(wxEVT_CHECKBOX, &DriveAnalysisPage::OnMapControlsChanged, this);

    UpdateResult(result);
}

// Replaces the map data for a fresh analysis and resets overlay controls to the actual layout.
void DriveAnalysisPage::UpdateResult(const AnalysisResult& result) {
    driveRoot = result.drive.rootPath;
    mapPanel->UpdateResult(result);
    mapModeChoice->SetSelection(0);
    plannedMovesCheck->SetValue(false);
    plannedMovesCheck->Enable(false);
    UpdateMapControls();
    Layout();
}

// Adds intended-placement data to the map without duplicating plan text in the document page.
void DriveAnalysisPage::UpdatePlacementPlan(const PlacementPlan& plan) {
    mapPanel->UpdatePlacementPlan(plan);
    UpdateMapControls();
    Layout();
}

// Adds planned-move overlay data to the map and enables source/target outlines.
void DriveAnalysisPage::UpdateMovePlan(const MovePlan& plan) {
    mapPanel->UpdateMovePlan(plan);
    plannedMovesCheck->Enable(true);
    plannedMovesCheck->SetValue(true);
    UpdateMapControls();
    Layout();
}

// Applies user map-control changes without starting analysis or planning work.
void DriveAnalysisPage::OnMapControlsChanged(wxCommandEvent&) {
    UpdateMapControls();
    Layout();
}

// Synchronizes the map renderer and legend with the selected display mode and class filter.
void DriveAnalysisPage::UpdateMapControls() {
    DriveMapRenderMode renderMode = DriveMapRenderMode::ActualLayout;
    if (mapModeChoice->GetSelection() == 1) {
        renderMode = DriveMapRenderMode::IntendedPlacement;
    } else if (mapModeChoice->GetSelection() == 2) {
        renderMode = DriveMapRenderMode::PlannedMoves;
    }
    mapPanel->SetRenderMode(renderMode);

    DriveMapClassFilter filter = DriveMapClassFilter::All;
    switch (classFilterChoice->GetSelection()) {
    case 1:
        filter = DriveMapClassFilter::Hot;
        break;
    case 2:
        filter = DriveMapClassFilter::Cold;
        break;
    case 3:
        filter = DriveMapClassFilter::LargeFile;
        break;
    case 4:
        filter = DriveMapClassFilter::Fragmented;
        break;
    case 5:
        filter = DriveMapClassFilter::Risky;
        break;
    case 6:
        filter = DriveMapClassFilter::Free;
        break;
    default:
        break;
    }
    mapPanel->SetClassFilter(filter);
    mapPanel->SetShowPlannedMoves(plannedMovesCheck->GetValue());

    if (renderMode == DriveMapRenderMode::IntendedPlacement) {
        legend->SetLabel(IntendedMapLegend());
    } else if (renderMode == DriveMapRenderMode::PlannedMoves) {
        legend->SetLabel(PlannedMapLegend());
    }
    else {
        legend->SetLabel(ActualMapLegend());
    }
}

} // namespace icd
