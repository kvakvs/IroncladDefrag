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
} // namespace

DriveAnalysisPage::DriveAnalysisPage(wxWindow* parent, const AnalysisResult& result) : wxPanel(parent, wxID_ANY) {
    splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    mapPanel = new DriveMapPanel(splitter, result);
    detailsPanel = new wxScrolledWindow(splitter, wxID_ANY);
    detailsPanel->SetScrollRate(0, 8);

    auto* detailsSizer = new wxBoxSizer(wxVERTICAL);
    title = new wxStaticText(detailsPanel, wxID_ANY, "");
    volume = new wxStaticText(detailsPanel, wxID_ANY, "");
    files = new wxStaticText(detailsPanel, wxID_ANY, "");
    fragmentation = new wxStaticText(detailsPanel, wxID_ANY, "");
    freeSpace = new wxStaticText(detailsPanel, wxID_ANY, "");
    classificationSizes = new wxStaticText(detailsPanel, wxID_ANY, "");
    classificationTypes = new wxStaticText(detailsPanel, wxID_ANY, "");
    classificationRecency = new wxStaticText(detailsPanel, wxID_ANY, "");
    classificationPlacement = new wxStaticText(detailsPanel, wxID_ANY, "");
    classificationSafety = new wxStaticText(detailsPanel, wxID_ANY, "");
    placementIntent = new wxStaticText(detailsPanel, wxID_ANY, "");
    movePlan = new wxStaticText(detailsPanel, wxID_ANY, "");
    legend = new wxStaticText(detailsPanel, wxID_ANY,
                              "Legend: red risky, orange fragmented, blue hot, purple cold, green occupied, "
                              "light gray free, dark gray unknown.");
    warnings = new wxStaticText(detailsPanel, wxID_ANY, "");
    todo = new wxStaticText(detailsPanel, wxID_ANY,
                            "Drive map is read-only. Planning and movement controls are not active.");

    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);

    detailsSizer->Add(title, 0, wxALL | wxEXPAND, 12);
    detailsSizer->Add(volume, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(files, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(fragmentation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(freeSpace, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(classificationSizes, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(classificationTypes, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(classificationRecency, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(classificationPlacement, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(classificationSafety, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(placementIntent, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(movePlan, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(legend, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(warnings, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
    detailsSizer->Add(todo, 0, wxALL | wxEXPAND, 12);
    detailsPanel->SetSizer(detailsSizer);

    splitter->SetMinimumPaneSize(96);
    splitter->SetSashGravity(0.75);
    splitter->SplitHorizontally(mapPanel, detailsPanel);

    auto* pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(splitter, 1, wxEXPAND);
    SetSizer(pageSizer);
    splitter->Bind(wxEVT_SIZE, &DriveAnalysisPage::OnSize, this);

    UpdateResult(result);
    SetDefaultSashPosition();
}

void DriveAnalysisPage::UpdateResult(const AnalysisResult& result) {
    driveRoot = result.drive.rootPath;
    mapPanel->UpdateResult(result);

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
    warnings->SetLabel(warningsText.str());
    detailsPanel->FitInside();
    Layout();
}

void DriveAnalysisPage::UpdatePlacementPlan(const PlacementPlan& plan) {
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

} // namespace icd
