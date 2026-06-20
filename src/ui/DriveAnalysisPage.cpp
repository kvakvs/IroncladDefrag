#include "../precompiled.h"
#include "DriveAnalysisPage.h"

#include <map>
#include <sstream>

namespace icd {

namespace {
    std::wstring FormatBytes(byte_count64_t bytes) {
        double value = static_cast<double>(bytes.getValue());
        const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
        int unitIndex = 0;
        while (value >= 1024.0 && unitIndex < 4) {
            value /= 1024.0;
            ++unitIndex;
        }

        std::wstringstream stream;
        stream.precision(unitIndex == 0 ? 0 : 1);
        stream << std::fixed << value << L" " << units[unitIndex];
        return stream.str();
    }

    std::wstring KindToString(DriveKind kind) {
        switch (kind) {
        case DriveKind::Mechanical:
            return L"Mechanical";
        case DriveKind::SolidState:
            return L"SSD";
        case DriveKind::Removable:
            return L"Removable";
        case DriveKind::Network:
            return L"Network";
        case DriveKind::Unknown:
        default:
            return L"Unknown";
        }
    }

    std::uint64_t CountOrZero(const std::map<FileSizeClass, count64_t>& counts, FileSizeClass key) {
        const auto found = counts.find(key);
        return found == counts.end() ? 0 : found->second.getValue();
    }

    std::uint64_t CountOrZero(const std::map<BroadFileType, count64_t>& counts, BroadFileType key) {
        const auto found = counts.find(key);
        return found == counts.end() ? 0 : found->second.getValue();
    }

    std::uint64_t CountOrZero(const std::map<FileTemperature, count64_t>& counts, FileTemperature key) {
        const auto found = counts.find(key);
        return found == counts.end() ? 0 : found->second.getValue();
    }

    std::uint64_t CountOrZero(const std::map<ExpectedPlacementZone, count64_t>& counts, ExpectedPlacementZone key) {
        const auto found = counts.find(key);
        return found == counts.end() ? 0 : found->second.getValue();
    }

    // Formats classification counters into compact labels for the temporary analysis page.
    std::wstring BuildSizeSummary(const ClassificationSummary& summary) {
        std::wstringstream text;
        text << L"Size classes: tiny " << CountOrZero(summary.sizeCounts, FileSizeClass::Tiny) << L", small "
             << CountOrZero(summary.sizeCounts, FileSizeClass::Small) << L", medium "
             << CountOrZero(summary.sizeCounts, FileSizeClass::Medium) << L", large "
             << CountOrZero(summary.sizeCounts, FileSizeClass::Large) << L", huge "
             << CountOrZero(summary.sizeCounts, FileSizeClass::Huge) << L".";
        return text.str();
    }

    // Formats broad file-type counters for quick data-drive mix inspection.
    std::wstring BuildTypeSummary(const ClassificationSummary& summary) {
        std::wstringstream text;
        text << L"Types: media " << CountOrZero(summary.typeCounts, BroadFileType::Media) << L", archives "
             << CountOrZero(summary.typeCounts, BroadFileType::Archive) << L", documents "
             << CountOrZero(summary.typeCounts, BroadFileType::Document) << L", source/project "
             << CountOrZero(summary.typeCounts, BroadFileType::SourceProject) << L", backups "
             << CountOrZero(summary.typeCounts, BroadFileType::Backup) << L", virtual disks "
             << CountOrZero(summary.typeCounts, BroadFileType::VirtualDisk) << L".";
        return text.str();
    }

    // Formats recency counters so hot/cold data-drive shape is visible before strategy UI exists.
    std::wstring BuildRecencySummary(const ClassificationSummary& summary) {
        std::wstringstream text;
        text << L"Recency: hot " << CountOrZero(summary.temperatureCounts, FileTemperature::Hot) << L", warm "
             << CountOrZero(summary.temperatureCounts, FileTemperature::Warm) << L", cool "
             << CountOrZero(summary.temperatureCounts, FileTemperature::Cool) << L", cold "
             << CountOrZero(summary.temperatureCounts, FileTemperature::Cold) << L", stale "
             << CountOrZero(summary.temperatureCounts, FileTemperature::Stale) << L".";
        return text.str();
    }

    // Formats expected placement counters without exposing future move-planning controls.
    std::wstring BuildPlacementSummary(const ClassificationSummary& summary) {
        std::wstringstream text;
        text << L"Expected placement: fast " << CountOrZero(summary.placementCounts, ExpectedPlacementZone::Fast)
             << L", balanced " << CountOrZero(summary.placementCounts, ExpectedPlacementZone::Balanced)
             << L", slow " << CountOrZero(summary.placementCounts, ExpectedPlacementZone::Slow) << L", large-file "
             << CountOrZero(summary.placementCounts, ExpectedPlacementZone::LargeFile) << L", no target "
             << CountOrZero(summary.placementCounts, ExpectedPlacementZone::None) << L".";
        return text.str();
    }

    std::uint64_t CountEnabledZones(const PlacementPlan& plan) {
        std::uint64_t count = 0;
        for (const DiskZone& zone : plan.zones) {
            if (zone.enabled) {
                ++count;
            }
        }
        return count;
    }

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

    std::wstring BuildRecommendationText(const AnalysisResult& result) {
        if (result.stats.freeSpaceMapAvailable && result.stats.freeSpaceBlocks.getValue() > 128) {
            return L"Recommendation: free space is heavily split; review free-space optimization.";
        }
        if (result.classificationSummary.highBenefitFragmentationFiles.getValue() > 0) {
            return L"Recommendation: build a dry-run plan to inspect high-benefit fragmentation candidates.";
        }
        if (result.classificationSummary.placementCounts.size() > 1) {
            return L"Recommendation: inspect placement intent for hot, cold, and large-file distribution.";
        }
        return L"Recommendation: analysis is available; plan only when the selected profile has a useful target.";
    }
} // namespace

DriveAnalysisPage::DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result) : wxPanel(parent, wxID_ANY) {
    splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    mapPanel = new DriveMapPanel(splitter, result);
    detailsPanel = new wxScrolledWindow(splitter, wxID_ANY);
    detailsPanel->SetScrollRate(0, 8);

    auto* detailsSizer = new wxBoxSizer(wxVERTICAL);
    auto* mapControls = new wxBoxSizer(wxHORIZONTAL);
    mapModeChoice = new wxChoice(detailsPanel, wxID_ANY);
    mapModeChoice->Append("Actual");
    mapModeChoice->Append("Intended");
    mapModeChoice->Append("Planned");
    mapModeChoice->SetSelection(0);
    classFilterChoice = new wxChoice(detailsPanel, wxID_ANY);
    classFilterChoice->Append("All");
    classFilterChoice->Append("Hot");
    classFilterChoice->Append("Cold");
    classFilterChoice->Append("Large");
    classFilterChoice->Append("Fragmented");
    classFilterChoice->Append("Risky");
    classFilterChoice->Append("Free");
    classFilterChoice->SetSelection(0);
    plannedMovesCheck = new wxCheckBox(detailsPanel, wxID_ANY, "Move outlines");
    plannedMovesCheck->Enable(false);
    mapControls->Add(new wxStaticText(detailsPanel, wxID_ANY, "Map"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    mapControls->Add(mapModeChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    mapControls->Add(new wxStaticText(detailsPanel, wxID_ANY, "Filter"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
    mapControls->Add(classFilterChoice, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    mapControls->Add(plannedMovesCheck, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
    detailsSizer->Add(mapControls, 0, wxEXPAND);

    detailsTabs = new wxNotebook(detailsPanel, wxID_ANY);
    auto* summaryTab = new wxPanel(detailsTabs, wxID_ANY);
    auto* planTab = new wxPanel(detailsTabs, wxID_ANY);
    auto* warningsTab = new wxPanel(detailsTabs, wxID_ANY);
    auto* executionTab = new wxPanel(detailsTabs, wxID_ANY);

    title = new wxStaticText(summaryTab, wxID_ANY, "");
    volume = new wxStaticText(summaryTab, wxID_ANY, "");
    files = new wxStaticText(summaryTab, wxID_ANY, "");
    fragmentation = new wxStaticText(summaryTab, wxID_ANY, "");
    freeSpace = new wxStaticText(summaryTab, wxID_ANY, "");
    classificationSizes = new wxStaticText(summaryTab, wxID_ANY, "");
    classificationTypes = new wxStaticText(summaryTab, wxID_ANY, "");
    classificationRecency = new wxStaticText(summaryTab, wxID_ANY, "");
    classificationPlacement = new wxStaticText(summaryTab, wxID_ANY, "");
    classificationSafety = new wxStaticText(summaryTab, wxID_ANY, "");
    recommendations = new wxStaticText(summaryTab, wxID_ANY, "");
    placementIntent = new wxStaticText(planTab, wxID_ANY, "");
    movePlan = new wxStaticText(planTab, wxID_ANY, "");
    legend = new wxStaticText(planTab, wxID_ANY, ActualMapLegend());
    warnings = new wxStaticText(warningsTab, wxID_ANY, "");
    execution = new wxStaticText(executionTab, wxID_ANY, "Execution: not started.");

    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);

    auto* summarySizer = new wxBoxSizer(wxVERTICAL);
    summarySizer->Add(title, 0, wxALL | wxEXPAND, 8);
    summarySizer->Add(volume, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(files, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(fragmentation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(freeSpace, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(classificationSizes, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(classificationTypes, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(classificationRecency, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(classificationPlacement, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(classificationSafety, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summarySizer->Add(recommendations, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    summaryTab->SetSizer(summarySizer);

    auto* planSizer = new wxBoxSizer(wxVERTICAL);
    planSizer->Add(placementIntent, 0, wxALL | wxEXPAND, 8);
    planSizer->Add(movePlan, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    planSizer->Add(legend, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    planTab->SetSizer(planSizer);

    auto* warningSizer = new wxBoxSizer(wxVERTICAL);
    warningSizer->Add(warnings, 0, wxALL | wxEXPAND, 8);
    warningsTab->SetSizer(warningSizer);

    auto* executionSizer = new wxBoxSizer(wxVERTICAL);
    executionSizer->Add(execution, 0, wxALL | wxEXPAND, 8);
    executionTab->SetSizer(executionSizer);

    detailsTabs->AddPage(summaryTab, "Summary", true);
    detailsTabs->AddPage(planTab, "Plan");
    detailsTabs->AddPage(warningsTab, "Warnings");
    detailsTabs->AddPage(executionTab, "Execution");
    detailsSizer->Add(detailsTabs, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    detailsPanel->SetSizer(detailsSizer);

    splitter->SetMinimumPaneSize(96);
    splitter->SetSashGravity(0.75);
    splitter->SplitHorizontally(mapPanel, detailsPanel);

    auto* pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(splitter, 1, wxEXPAND);
    SetSizer(pageSizer);
    splitter->Bind(wxEVT_SIZE, &DriveAnalysisPage::OnSize, this);
    mapModeChoice->Bind(wxEVT_CHOICE, &DriveAnalysisPage::OnMapControlsChanged, this);
    classFilterChoice->Bind(wxEVT_CHOICE, &DriveAnalysisPage::OnMapControlsChanged, this);
    plannedMovesCheck->Bind(wxEVT_CHECKBOX, &DriveAnalysisPage::OnMapControlsChanged, this);

    UpdateResult(result);
    SetDefaultSashPosition();
}

void DriveAnalysisPage::UpdateResult(const AnalysisResult& result) {
    driveRoot = result.drive.rootPath;
    mapPanel->UpdateResult(result);
    mapModeChoice->SetSelection(0);
    plannedMovesCheck->SetValue(false);
    plannedMovesCheck->Enable(false);
    UpdateMapControls();

    std::wstringstream volumeText;
    volumeText << L"Volume: " << result.volume.label << L" (" << result.volume.fileSystem << L"), "
               << FormatBytes(result.volume.freeBytes) << L" free of " << FormatBytes(result.volume.totalBytes)
               << L", cluster size " << FormatBytes(result.volume.bytesPerCluster);

    std::wstringstream filesText;
    filesText << L"Files: " << result.stats.scannedFiles.getValue() << L" scanned, "
              << result.stats.skippedFiles.getValue() << L" skipped, " << result.stats.inaccessibleFiles.getValue()
              << L" inaccessible, " << result.stats.filesWithExtents.getValue() << L" with extent data.";

    std::wstringstream fragmentationText;
    fragmentationText << L"Fragmentation: " << result.stats.fragmentedFiles.getValue() << L" fragmented files, "
                      << result.stats.totalFragments.getValue() << L" total fragments.";

    std::wstringstream freeSpaceText;
    freeSpaceText << L"Free space: ";
    if (result.stats.freeSpaceMapAvailable) {
        freeSpaceText << result.stats.freeSpaceBlocks.getValue() << L" blocks, largest block "
                      << result.stats.largestFreeBlockSectors.getValue() << L" clusters.";
    }
    else {
        freeSpaceText << L"bitmap unavailable.";
    }

    std::wstringstream classificationSafetyText;
    classificationSafetyText << L"Classification safety: excluded "
                             << result.classificationSummary.excludedFiles.getValue() << L", explicit-only "
                             << result.classificationSummary.explicitOnlyFiles.getValue()
                             << L", fragmentation candidates "
                             << result.classificationSummary.beneficialFragmentationFiles.getValue()
                             << L", high-benefit candidates "
                             << result.classificationSummary.highBenefitFragmentationFiles.getValue() << L".";

    std::wstringstream warningsText;
    warningsText << L"Capabilities: " << KindToString(result.drive.kind);
    if (!result.drive.capabilities.disabledReason.empty()) {
        warningsText << L"; " << result.drive.capabilities.disabledReason;
    }
    if (!result.drive.capabilities.statusReason.empty()) {
        warningsText << L"; " << result.drive.capabilities.statusReason;
    }
    if (result.stats.cancelled) {
        warningsText << L"; analysis was cancelled.";
    }

    title->SetLabel(wxString::Format("Analysis: %s", wxString(result.drive.displayName)));
    volume->SetLabel(volumeText.str());
    files->SetLabel(filesText.str());
    fragmentation->SetLabel(fragmentationText.str());
    freeSpace->SetLabel(freeSpaceText.str());
    classificationSizes->SetLabel(BuildSizeSummary(result.classificationSummary));
    classificationTypes->SetLabel(BuildTypeSummary(result.classificationSummary));
    classificationRecency->SetLabel(BuildRecencySummary(result.classificationSummary));
    classificationPlacement->SetLabel(BuildPlacementSummary(result.classificationSummary));
    classificationSafety->SetLabel(classificationSafetyText.str());
    placementIntent->SetLabel("Placement intent: not built for this analysis snapshot.");
    movePlan->SetLabel("Move plan: not built for this analysis snapshot.");
    recommendations->SetLabel(BuildRecommendationText(result));
    warnings->SetLabel(warningsText.str());
    execution->SetLabel("Execution: not started.");
    detailsPanel->FitInside();
    Layout();
}

void DriveAnalysisPage::UpdatePlacementPlan(const PlacementPlan& plan) {
    mapPanel->UpdatePlacementPlan(plan);
    UpdateMapControls();

    std::wstringstream text;
    text << L"Placement intent: " << plan.profile.name << L", zones enabled " << CountEnabledZones(plan) << L"/"
         << plan.zones.size() << L", targeted " << plan.targetedFiles.getValue() << L", no target "
         << plan.noTargetFiles.getValue() << L", considered " << FormatBytes(plan.bytesConsidered) << L", "
         << (plan.profile.settings.dryRunOnly ? L"dry-run only" : L"dry-run disabled in settings") << L".";
    placementIntent->SetLabel(text.str());
    detailsPanel->FitInside();
    Layout();
}

void DriveAnalysisPage::UpdateMovePlan(const MovePlan& plan) {
    mapPanel->UpdateMovePlan(plan);
    plannedMovesCheck->Enable(true);
    plannedMovesCheck->SetValue(true);
    UpdateMapControls();

    std::wstringstream text;
    text << L"Move plan: " << plan.metrics.affectedFiles.getValue() << L" files, "
         << FormatBytes(plan.estimatedBytesToMove) << L" estimated moved, skipped "
         << plan.metrics.skippedFiles.getValue() << L", zone changes "
         << plan.metrics.expectedZoneChanges.getValue() << L", fragmentation improvements "
         << plan.metrics.fragmentationImprovementFiles.getValue();
    if (plan.partial) {
        text << L", partial";
    }
    if (plan.impossible) {
        text << L", impossible";
    }
    text << L".";
    movePlan->SetLabel(text.str());

    std::wstringstream warningText;
    if (!plan.issues.empty()) {
        warningText << L"Issues:\n";
        for (const MovePlanIssue& issue : plan.issues) {
            warningText << L"- " << (issue.blocking ? L"blocking: " : L"note: ") << issue.message << L"\n";
        }
        warningText << L"\n";
    }

    warningText << L"Skipped candidates:\n";
    const std::size_t skippedToShow = (std::min)(std::size_t(20), plan.skippedCandidates.size());
    for (std::size_t index = 0; index < skippedToShow; ++index) {
        const SkippedMoveCandidate& skipped = plan.skippedCandidates[index];
        warningText << L"- file #" << skipped.fileIndex << L": " << skipped.detail << L"\n";
    }
    if (plan.skippedCandidates.size() > skippedToShow) {
        warningText << L"- ... " << (plan.skippedCandidates.size() - skippedToShow) << L" more skipped candidates\n";
    }
    warnings->SetLabel(warningText.str());
    detailsPanel->FitInside();
    Layout();
}

void DriveAnalysisPage::UpdateExecutionResult(const MoveExecutionResult& result) {
    std::wstringstream text;
    text << result.summary << L"\n";
    text << L"Moved: " << result.metrics.movedOperations.getValue() << L", skipped "
         << result.metrics.skippedOperations.getValue() << L", failed " << result.metrics.failedOperations.getValue()
         << L", verification failures " << result.metrics.verificationFailures.getValue() << L".";
    execution->SetLabel(text.str());
    detailsPanel->FitInside();
    Layout();
}

// Keeps the map/details split near the planned 75/25 document proportions.
void DriveAnalysisPage::SetDefaultSashPosition(const wxSize& size) {
    if (splitter == nullptr || !splitter->IsSplit()) {
        return;
    }

    const int height = size.GetHeight() > 0 ? size.GetHeight() : splitter->GetClientSize().GetHeight();
    if (height > 0) {
        splitter->SetSashPosition((height * 3) / 4);
    }
}

void DriveAnalysisPage::OnSize(wxSizeEvent& event) {
    SetDefaultSashPosition(event.GetSize());
    event.Skip();
}

void DriveAnalysisPage::OnMapControlsChanged(wxCommandEvent&) {
    UpdateMapControls();
    detailsPanel->FitInside();
    Layout();
}

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
