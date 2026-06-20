#include "../precompiled.h"
#include "WorkflowPanel.h"

#include "UIFormatting.h"

#include <sstream>
#include <wx/notebook.h>
#include <wx/sizer.h>

namespace icd {

namespace {
wxButton* AddButton(wxWindow* parent, wxBoxSizer* sizer, const wxString& label)
{
    auto* button = new wxButton(parent, wxID_ANY, label);
    sizer->Add(button, 0, wxALL, 3);
    return button;
}

void SetActionVisible(wxWindow* window, bool visible)
{
    window->Show(visible);
    window->Enable(visible);
}

std::wstring BuildAnalysisSummary(const AnalysisResult& result)
{
    std::wstringstream text;
    text << L"Analysis: " << result.stats.scannedFiles.getValue() << L" files, "
         << result.stats.fragmentedFiles.getValue() << L" fragmented, "
         << result.stats.freeSpaceBlocks.getValue() << L" free blocks, "
         << result.classificationSummary.highBenefitFragmentationFiles.getValue() << L" high-benefit candidates.";
    return text.str();
}

std::wstring BuildRecommendation(const AnalysisResult& result)
{
    std::wstringstream text;
    if (result.stats.freeSpaceMapAvailable && result.stats.freeSpaceBlocks.getValue() > 128) {
        text << L"Recommendation: review free-space optimization; free space is split into many blocks.";
    } else if (result.classificationSummary.highBenefitFragmentationFiles.getValue() > 0) {
        text << L"Recommendation: build a dry-run plan; fragmentation candidates are present.";
    } else if (result.classificationSummary.placementCounts.size() > 1) {
        text << L"Recommendation: review placement intent for hot, cold, and large-file distribution.";
    } else {
        text << L"Recommendation: analysis is available; build a plan only if the profile has a useful target.";
    }
    return text.str();
}

std::wstring ExecutionStateName(MoveExecutionState state)
{
    switch (state) {
    case MoveExecutionState::Moved:
        return L"moved";
    case MoveExecutionState::Skipped:
        return L"skipped";
    case MoveExecutionState::Failed:
        return L"failed";
    case MoveExecutionState::VerificationFailed:
        return L"verification failed";
    case MoveExecutionState::Pending:
    default:
        return L"pending";
    }
}
} // namespace

WorkflowPanel::WorkflowPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* topRow = new wxBoxSizer(wxHORIZONTAL);
    stageText = new wxStaticText(this, wxID_ANY, "Stage: select a drive");
    wxFont stageFont = stageText->GetFont();
    stageFont.SetWeight(wxFONTWEIGHT_BOLD);
    stageText->SetFont(stageFont);
    topRow->Add(stageText, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    topRow->AddStretchSpacer(1);
    profileChoice = new wxChoice(this, wxID_ANY);
    profileLabel = new wxStaticText(this, wxID_ANY, "Profile");
    topRow->Add(profileLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    topRow->Add(profileChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    root->Add(topRow, 0, wxEXPAND);

    driveText = new wxStaticText(this, wxID_ANY, "Drive: none");
    root->Add(driveText, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    auto* buttonRows = new wxBoxSizer(wxVERTICAL);
    auto* stepRow = new wxBoxSizer(wxHORIZONTAL);
    refreshButton = AddButton(this, stepRow, "Refresh");
    analyzeButton = AddButton(this, stepRow, "Analyze");
    settingsButton = AddButton(this, stepRow, "Settings");
    buildPlacementButton = AddButton(this, stepRow, "Build Intent");
    buildPlanButton = AddButton(this, stepRow, "Build Plan");
    reviewPlanButton = AddButton(this, stepRow, "Review Plan");
    executeButton = AddButton(this, stepRow, "Execute");
    cancelButton = AddButton(this, stepRow, "Cancel");
    buttonRows->Add(stepRow, 0, wxEXPAND);

    auto* fastRow = new wxBoxSizer(wxHORIZONTAL);
    quickDefragButton = AddButton(this, fastRow, "Quick Defrag");
    fullOptimizeButton = AddButton(this, fastRow, "Full Optimize");
    buttonRows->Add(fastRow, 0, wxEXPAND);
    root->Add(buttonRows, 0, wxEXPAND | wxLEFT | wxRIGHT, 3);

    progressGauge = new wxGauge(this, wxID_ANY, 100);
    progressText = new wxStaticText(this, wxID_ANY, "Progress: idle");
    root->Add(progressGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    root->Add(progressText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    auto* details = new wxNotebook(this, wxID_ANY);
    auto* summaryPanel = new wxPanel(details, wxID_ANY);
    auto* summarySizer = new wxBoxSizer(wxVERTICAL);
    analysisText = new wxStaticText(summaryPanel, wxID_ANY, "Analysis: not available");
    placementText = new wxStaticText(summaryPanel, wxID_ANY, "Placement intent: not built");
    planText = new wxStaticText(summaryPanel, wxID_ANY, "Move plan: not built");
    summarySizer->Add(analysisText, 0, wxEXPAND | wxALL, 6);
    summarySizer->Add(placementText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    summarySizer->Add(planText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    summaryPanel->SetSizer(summarySizer);
    details->AddPage(summaryPanel, "Summary", true);

    warningsText = new wxTextCtrl(details, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    executionText = new wxTextCtrl(details, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    details->AddPage(warningsText, "Warnings");
    details->AddPage(executionText, "Execution");
    root->Add(details, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(root);

    refreshButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (refreshCallback) {
            refreshCallback();
        }
    });
    analyzeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (analyzeCallback) {
            analyzeCallback();
        }
    });
    cancelButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (cancelCallback) {
            cancelCallback();
        }
    });
    settingsButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (settingsCallback) {
            settingsCallback();
        }
    });
    buildPlacementButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (buildPlacementCallback) {
            buildPlacementCallback();
        }
    });
    buildPlanButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (buildPlanCallback) {
            buildPlanCallback();
        }
    });
    quickDefragButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (quickDefragCallback) {
            quickDefragCallback();
        }
    });
    fullOptimizeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (fullOptimizeCallback) {
            fullOptimizeCallback();
        }
    });
    reviewPlanButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (reviewPlanCallback) {
            reviewPlanCallback();
        }
    });
    executeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (executeCallback) {
            executeCallback();
        }
    });
    profileChoice->Bind(wxEVT_CHOICE, &WorkflowPanel::OnProfileChanged, this);
    UpdateControls();
}

void WorkflowPanel::SetDrive(const std::optional<DriveInfo>& drive)
{
    const std::wstring previousRoot = selectedDrive.has_value() ? selectedDrive->rootPath : std::wstring();
    selectedDrive = drive;
    const std::wstring nextRoot = selectedDrive.has_value() ? selectedDrive->rootPath : std::wstring();
    if (previousRoot != nextRoot) {
        hasAnalysis = false;
        hasPlacement = false;
        hasPlan = false;
        hasExecution = false;
        analysisText->SetLabel("Analysis: not available");
        placementText->SetLabel("Placement intent: not built");
        planText->SetLabel("Move plan: not built");
        warningsText->SetValue("");
        executionText->SetValue("");
        progressGauge->SetValue(0);
        progressText->SetLabel("Progress: idle");
    }

    if (selectedDrive.has_value()) {
        driveText->SetLabel(wxString::Format("Drive: %s | %s | %s free of %s",
                                             wxString(selectedDrive->displayName),
                                             wxString(ui::CapabilityBadge(*selectedDrive)),
                                             wxString(ui::FormatBytes(selectedDrive->volume.freeBytes)),
                                             wxString(ui::FormatBytes(selectedDrive->volume.totalBytes))));
    } else {
        driveText->SetLabel("Drive: none");
    }
    UpdateControls();
}

void WorkflowPanel::SetAnalysis(const AnalysisResult& result)
{
    selectedDrive = result.drive;
    hasAnalysis = true;
    hasPlacement = false;
    hasPlan = false;
    hasExecution = false;
    analysisText->SetLabel(wxString(BuildAnalysisSummary(result) + L"\n" + BuildRecommendation(result)));
    placementText->SetLabel("Placement intent: not built");
    planText->SetLabel("Move plan: not built");
    warningsText->SetValue("");
    executionText->SetValue("");
    progressGauge->SetValue(100);
    progressText->SetLabel("Progress: analysis complete");
    driveText->SetLabel(wxString::Format("Drive: %s | %s", wxString(result.drive.displayName), wxString(ui::CapabilityBadge(result.drive))));
    UpdateControls();
}

void WorkflowPanel::SetPlacementPlan(const PlacementPlan& plan)
{
    hasPlacement = true;
    hasPlan = false;
    hasExecution = false;
    placementText->SetLabel(wxString::Format("Placement intent: %s, targeted %llu files, %s considered.",
                                             wxString(plan.profile.name),
                                             static_cast<unsigned long long>(plan.targetedFiles.getValue()),
                                             wxString(ui::FormatBytes(plan.bytesConsidered))));
    planText->SetLabel("Move plan: not built");
    executionText->SetValue("");
    UpdateControls();
}

void WorkflowPanel::SetMovePlan(const MovePlan& plan)
{
    hasPlacement = true;
    hasPlan = true;
    hasExecution = false;
    planText->SetLabel(wxString::Format("Move plan: %llu files, %s estimated, %llu skipped, %s.",
                                        static_cast<unsigned long long>(plan.metrics.affectedFiles.getValue()),
                                        wxString(ui::FormatBytes(plan.estimatedBytesToMove)),
                                        static_cast<unsigned long long>(plan.metrics.skippedFiles.getValue()),
                                        plan.impossible ? "impossible" : (plan.partial ? "partial" : "ready")));
    AppendIssueText(plan);
    UpdateControls();
}

void WorkflowPanel::SetExecutionResult(const MoveExecutionResult& result)
{
    hasExecution = true;
    std::wstringstream text;
    text << result.summary << L"\n";
    text << L"Moved: " << result.metrics.movedOperations.getValue() << L", skipped "
         << result.metrics.skippedOperations.getValue() << L", failed " << result.metrics.failedOperations.getValue()
         << L", verification failures " << result.metrics.verificationFailures.getValue() << L".\n\n";

    const std::size_t resultsToShow = (std::min)(std::size_t(25), result.operationResults.size());
    for (std::size_t index = 0; index < resultsToShow; ++index) {
        const auto& item = result.operationResults[index];
        text << L"- " << ExecutionStateName(item.state) << L": " << item.filePath.wstring() << L" " << item.detail << L"\n";
    }
    if (result.operationResults.size() > resultsToShow) {
        text << L"- ... " << (result.operationResults.size() - resultsToShow) << L" more operation results\n";
    }
    executionText->SetValue(text.str());
    UpdateControls();
}

void WorkflowPanel::SetProgress(const JobProgress& progress)
{
    const int percent = (std::max)(0, (std::min)(100, static_cast<int>(progress.percentComplete)));
    progressGauge->SetValue(percent);
    progressText->SetLabel(wxString::Format("Progress: %.0f%% - %s %s",
                                            progress.percentComplete,
                                            wxString(progress.statusMessage),
                                            wxString(progress.currentItem)));
    UpdateControls();
}

void WorkflowPanel::SetProfiles(const std::vector<OptimizationProfile>& nextProfiles, const OptimizationProfile& activeProfile)
{
    profiles = nextProfiles;
    UpdateProfileChoice(activeProfile);
    UpdateControls();
}

void WorkflowPanel::SetBusy(bool running)
{
    busy = running;
    UpdateControls();
}

void WorkflowPanel::SetExecuteAllowed(bool allowed)
{
    executeAllowed = allowed;
    UpdateControls();
}

void WorkflowPanel::UpdateControls()
{
    const bool hasDrive = selectedDrive.has_value();
    const bool canAnalyze = hasDrive && selectedDrive->capabilities.canAnalyze;
    const bool showPlanning = !busy && hasAnalysis;
    const bool showProfile = showPlanning && !profiles.empty();

    SetActionVisible(refreshButton, !busy);
    SetActionVisible(analyzeButton, !busy && canAnalyze);
    SetActionVisible(cancelButton, busy);
    SetActionVisible(profileLabel, showProfile);
    SetActionVisible(profileChoice, showProfile);
    SetActionVisible(settingsButton, showPlanning);
    SetActionVisible(buildPlacementButton, showPlanning);
    SetActionVisible(buildPlanButton, showPlanning);
    SetActionVisible(quickDefragButton, showPlanning);
    SetActionVisible(fullOptimizeButton, showPlanning);
    SetActionVisible(reviewPlanButton, !busy && hasPlan);
    SetActionVisible(executeButton, !busy && hasPlan && executeAllowed);

    if (busy) {
        stageText->SetLabel("Stage: running");
    } else if (!hasDrive) {
        stageText->SetLabel("Stage: select a drive");
    } else if (!hasAnalysis) {
        stageText->SetLabel("Stage: analyze drive");
    } else if (!hasPlan) {
        stageText->SetLabel(hasPlacement ? "Stage: build or review move plan" : "Stage: choose profile and plan");
    } else if (hasExecution) {
        stageText->SetLabel("Stage: execution results available");
    } else {
        stageText->SetLabel("Stage: review and execute");
    }

    Layout();
    if (GetParent() != nullptr) {
        GetParent()->Layout();
    }
}

void WorkflowPanel::UpdateProfileChoice(const OptimizationProfile& activeProfile)
{
    updatingProfiles = true;
    profileChoice->Clear();
    int selection = wxNOT_FOUND;
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        profileChoice->Append(wxString(profiles[index].name));
        if (profiles[index].mode == activeProfile.mode) {
            selection = static_cast<int>(index);
        }
    }
    if (selection == wxNOT_FOUND && !profiles.empty()) {
        selection = 0;
    }
    if (selection != wxNOT_FOUND) {
        profileChoice->SetSelection(selection);
    }
    updatingProfiles = false;
}

void WorkflowPanel::AppendIssueText(const MovePlan& plan)
{
    std::wstringstream text;
    if (!plan.issues.empty()) {
        text << L"Issues:\n";
        for (const MovePlanIssue& issue : plan.issues) {
            text << L"- " << (issue.blocking ? L"blocking: " : L"note: ") << issue.message << L"\n";
        }
        text << L"\n";
    }

    text << L"Skipped candidates:\n";
    const std::size_t skippedToShow = (std::min)(std::size_t(20), plan.skippedCandidates.size());
    for (std::size_t index = 0; index < skippedToShow; ++index) {
        const SkippedMoveCandidate& skipped = plan.skippedCandidates[index];
        text << L"- file #" << skipped.fileIndex << L": " << skipped.detail << L"\n";
    }
    if (plan.skippedCandidates.size() > skippedToShow) {
        text << L"- ... " << (plan.skippedCandidates.size() - skippedToShow) << L" more skipped candidates\n";
    }
    warningsText->SetValue(text.str());
}

void WorkflowPanel::OnProfileChanged(wxCommandEvent&)
{
    if (updatingProfiles || profileChangedCallback == nullptr) {
        return;
    }

    const int selection = profileChoice->GetSelection();
    if (selection == wxNOT_FOUND || static_cast<std::size_t>(selection) >= profiles.size()) {
        return;
    }

    profileChangedCallback(profiles[static_cast<std::size_t>(selection)]);
}

} // namespace icd
