#pragma once

#include "../model/DomainTypes.h"

#include <functional>
#include <optional>
#include <vector>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace icd {

// Presents the staged analysis, planning, and execution controls for the selected drive.
class WorkflowPanel : public wxPanel {
public:
    using SimpleCallback = std::function<void()>;
    using ProfileCallback = std::function<void(const OptimizationProfile&)>;

    explicit WorkflowPanel(wxWindow* parent);

    void SetDrive(const std::optional<DriveInfo>& drive);
    void SetAnalysis(const AnalysisResult& result);
    void SetPlacementPlan(const PlacementPlan& plan);
    void SetMovePlan(const MovePlan& plan);
    void SetExecutionResult(const MoveExecutionResult& result);
    void SetProgress(const JobProgress& progress);
    void SetProfiles(const std::vector<OptimizationProfile>& nextProfiles, const OptimizationProfile& activeProfile);
    void SetBusy(bool running);
    void SetExecuteAllowed(bool allowed);

    void SetRefreshCallback(SimpleCallback callback) { refreshCallback = std::move(callback); }
    void SetAnalyzeCallback(SimpleCallback callback) { analyzeCallback = std::move(callback); }
    void SetCancelCallback(SimpleCallback callback) { cancelCallback = std::move(callback); }
    void SetSettingsCallback(SimpleCallback callback) { settingsCallback = std::move(callback); }
    void SetBuildPlacementCallback(SimpleCallback callback) { buildPlacementCallback = std::move(callback); }
    void SetBuildPlanCallback(SimpleCallback callback) { buildPlanCallback = std::move(callback); }
    void SetQuickDefragCallback(SimpleCallback callback) { quickDefragCallback = std::move(callback); }
    void SetFullOptimizeCallback(SimpleCallback callback) { fullOptimizeCallback = std::move(callback); }
    void SetReviewPlanCallback(SimpleCallback callback) { reviewPlanCallback = std::move(callback); }
    void SetExecuteCallback(SimpleCallback callback) { executeCallback = std::move(callback); }
    void SetProfileChangedCallback(ProfileCallback callback) { profileChangedCallback = std::move(callback); }

private:
    void UpdateControls();
    void UpdateProfileChoice(const OptimizationProfile& activeProfile);
    void AppendIssueText(const MovePlan& plan);
    void OnProfileChanged(wxCommandEvent& event);

    wxStaticText* stageText = nullptr;
    wxStaticText* driveText = nullptr;
    wxStaticText* analysisText = nullptr;
    wxStaticText* placementText = nullptr;
    wxStaticText* planText = nullptr;
    wxStaticText* progressText = nullptr;
    wxStaticText* profileLabel = nullptr;
    wxChoice* profileChoice = nullptr;
    wxGauge* progressGauge = nullptr;
    wxTextCtrl* warningsText = nullptr;
    wxTextCtrl* executionText = nullptr;
    wxButton* refreshButton = nullptr;
    wxButton* analyzeButton = nullptr;
    wxButton* cancelButton = nullptr;
    wxButton* settingsButton = nullptr;
    wxButton* buildPlacementButton = nullptr;
    wxButton* buildPlanButton = nullptr;
    wxButton* quickDefragButton = nullptr;
    wxButton* fullOptimizeButton = nullptr;
    wxButton* reviewPlanButton = nullptr;
    wxButton* executeButton = nullptr;

    std::optional<DriveInfo> selectedDrive;
    std::vector<OptimizationProfile> profiles;
    bool hasAnalysis = false;
    bool hasPlacement = false;
    bool hasPlan = false;
    bool hasExecution = false;
    bool busy = false;
    bool executeAllowed = false;
    bool updatingProfiles = false;

    SimpleCallback refreshCallback;
    SimpleCallback analyzeCallback;
    SimpleCallback cancelCallback;
    SimpleCallback settingsCallback;
    SimpleCallback buildPlacementCallback;
    SimpleCallback buildPlanCallback;
    SimpleCallback quickDefragCallback;
    SimpleCallback fullOptimizeCallback;
    SimpleCallback reviewPlanCallback;
    SimpleCallback executeCallback;
    ProfileCallback profileChangedCallback;
};

} // namespace icd
