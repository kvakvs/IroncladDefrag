#include "../precompiled.h"
#include "DriveMapPanel.h"

#include <wx/dcbuffer.h>

#include <algorithm>
#include <cstddef>
#include <unordered_map>

namespace icd {

namespace {
    constexpr std::uint64_t MinimumClusterCount = 1;

    // Converts an integer division into a minimum-one ceiling result for map scale calculation.
    std::uint64_t CeilDivMinimumOne(std::uint64_t value, std::uint64_t divisor) {
        if (value == 0 || divisor == 0) {
            return MinimumClusterCount;
        }
        return ((value - 1) / divisor) + 1;
    }

    // Returns true when two half-open cluster ranges overlap.
    bool RangesOverlap(std::uint64_t firstStart, std::uint64_t firstEnd, std::uint64_t secondStart,
                       std::uint64_t secondEnd) {
        return firstStart < secondEnd && secondStart < firstEnd;
    }

    // Gives risk-oriented states precedence when multiple ranges occupy one visual box.
    int StatePriority(DriveMapRangeState state) {
        switch (state) {
        case DriveMapRangeState::Risky:
            return 7;
        case DriveMapRangeState::Fragmented:
            return 6;
        case DriveMapRangeState::Hot:
            return 5;
        case DriveMapRangeState::LargeFile:
            return 4;
        case DriveMapRangeState::Cold:
            return 3;
        case DriveMapRangeState::Occupied:
            return 2;
        case DriveMapRangeState::Free:
            return 1;
        case DriveMapRangeState::Unknown:
        default:
            return 0;
        }
    }

    // Chooses a stable paint color for one drive-map range state.
    wxColour ColourForState(DriveMapRangeState state) {
        switch (state) {
        case DriveMapRangeState::Risky:
            return wxColour(183, 54, 54);
        case DriveMapRangeState::Fragmented:
            return wxColour(221, 138, 36);
        case DriveMapRangeState::Hot:
            return wxColour(41, 116, 204);
        case DriveMapRangeState::LargeFile:
            return wxColour(46, 138, 132);
        case DriveMapRangeState::Cold:
            return wxColour(116, 92, 158);
        case DriveMapRangeState::Occupied:
            return wxColour(74, 142, 83);
        case DriveMapRangeState::Free:
            return wxColour(205, 211, 218);
        case DriveMapRangeState::Unknown:
        default:
            return wxColour(88, 94, 101);
        }
    }

    // Decides whether a map range survives the current class filter.
    bool FilterAllowsState(DriveMapClassFilter filter, DriveMapRangeState state) {
        switch (filter) {
        case DriveMapClassFilter::Hot:
            return state == DriveMapRangeState::Hot;
        case DriveMapClassFilter::Cold:
            return state == DriveMapRangeState::Cold;
        case DriveMapClassFilter::LargeFile:
            return state == DriveMapRangeState::LargeFile;
        case DriveMapClassFilter::Fragmented:
            return state == DriveMapRangeState::Fragmented;
        case DriveMapClassFilter::Risky:
            return state == DriveMapRangeState::Risky;
        case DriveMapClassFilter::Free:
            return state == DriveMapRangeState::Free;
        case DriveMapClassFilter::All:
        default:
            return true;
        }
    }

    // Derives the visual state for a file range using classification first and raw file safety second.
    DriveMapRangeState StateForFile(const FileMetadata& file, const FileClass* classification) {
        const auto& flags = file.GetAttributeFlags();
        const bool risky = file.IsRiskyOrUnmovable() || flags.system || flags.reparsePoint || flags.sparse ||
            flags.compressed || flags.encrypted ||
            (classification != nullptr && (classification->excluded || classification->moveOnlyWhenExplicit));
        if (risky) {
            return DriveMapRangeState::Risky;
        }

        if (file.IsFragmented()) {
            return DriveMapRangeState::Fragmented;
        }

        if (classification != nullptr) {
            if (classification->expectedPlacement == ExpectedPlacementZone::LargeFile ||
                classification->sizeClass == FileSizeClass::Large || classification->sizeClass == FileSizeClass::Huge) {
                return DriveMapRangeState::LargeFile;
            }
            if (classification->expectedPlacement == ExpectedPlacementZone::Fast ||
                classification->temperature == FileTemperature::Hot ||
                classification->temperature == FileTemperature::Warm) {
                return DriveMapRangeState::Hot;
            }
            if (classification->expectedPlacement == ExpectedPlacementZone::Slow ||
                classification->temperature == FileTemperature::Cold ||
                classification->temperature == FileTemperature::Stale) {
                return DriveMapRangeState::Cold;
            }
        }

        return DriveMapRangeState::Occupied;
    }

    // Derives the intended-placement state without changing current file extents.
    DriveMapRangeState StateForIntendedFile(const FileMetadata& file,
                                            const FileClass* classification,
                                            const PlacementPlan::FilePlacementIntent* intent) {
        const auto& flags = file.GetAttributeFlags();
        const bool risky = file.IsRiskyOrUnmovable() || flags.system || flags.reparsePoint || flags.sparse ||
            flags.compressed || flags.encrypted ||
            (classification != nullptr && (classification->excluded || classification->moveOnlyWhenExplicit));
        if (risky) {
            return DriveMapRangeState::Risky;
        }

        if (intent == nullptr || intent->targetZone == ExpectedPlacementZone::None) {
            return DriveMapRangeState::Unknown;
        }

        switch (intent->targetZone) {
        case ExpectedPlacementZone::Fast:
            return DriveMapRangeState::Hot;
        case ExpectedPlacementZone::Slow:
            return DriveMapRangeState::Cold;
        case ExpectedPlacementZone::LargeFile:
            return DriveMapRangeState::LargeFile;
        case ExpectedPlacementZone::Balanced:
            return DriveMapRangeState::Occupied;
        case ExpectedPlacementZone::None:
        default:
            return DriveMapRangeState::Unknown;
        }
    }

    // Finds the classification record for a file index without duplicating per-file path data.
    std::vector<const FileClass*> BuildClassificationIndex(const AnalysisResult& analysis) {
        std::vector<const FileClass*> byFileIndex(analysis.files.size(), nullptr);
        for (const auto& item : analysis.classifications) {
            if (item.fileIndex < byFileIndex.size()) {
                byFileIndex[item.fileIndex] = &item.classification;
            }
        }
        return byFileIndex;
    }

    // Finds placement-intent records by analysed file index for intended map rendering.
    std::unordered_map<std::size_t, const PlacementPlan::FilePlacementIntent*> BuildIntentIndex(
        const PlacementPlan& plan) {
        std::unordered_map<std::size_t, const PlacementPlan::FilePlacementIntent*> byFileIndex;
        for (const auto& intent : plan.fileIntents) {
            byFileIndex[intent.fileIndex] = &intent;
        }
        return byFileIndex;
    }

    // Prevents unsigned wrap when a queried range extends to the end of the addressable cluster space.
    std::uint64_t SaturatingRangeEnd(std::uint64_t start, std::uint64_t count) {
        const std::uint64_t end = start + count;
        return end < start ? UINT64_MAX : end;
    }
} // namespace

DriveMapPanel::DriveMapPanel(wxWindow* parent, const AnalysisResult& result) :
    wxPanel(parent, wxID_ANY), analysis(result) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &DriveMapPanel::OnPaint, this);
    Bind(wxEVT_SIZE, &DriveMapPanel::OnSize, this);

    BuildRanges();
    RecalculateScale(GetClientSize());
}

void DriveMapPanel::UpdateResult(const AnalysisResult& result) {
    analysis = result;
    placementPlan.reset();
    movePlan.reset();
    renderMode = DriveMapRenderMode::ActualLayout;
    showPlannedMoves = false;
    BuildRanges();
    RecalculateScale(GetClientSize());
    Refresh();
}

void DriveMapPanel::UpdatePlacementPlan(const PlacementPlan& plan) {
    placementPlan = plan;
    BuildRanges();
    Refresh();
}

void DriveMapPanel::UpdateMovePlan(const MovePlan& plan) {
    movePlan = plan;
    showPlannedMoves = true;
    BuildRanges();
    Refresh();
}

void DriveMapPanel::SetRenderMode(DriveMapRenderMode mode) {
    DriveMapRenderMode nextMode = mode;
    if (nextMode == DriveMapRenderMode::IntendedPlacement && !placementPlan.has_value()) {
        nextMode = DriveMapRenderMode::ActualLayout;
    }
    if (nextMode == DriveMapRenderMode::PlannedMoves && !movePlan.has_value()) {
        nextMode = placementPlan.has_value() ? DriveMapRenderMode::IntendedPlacement : DriveMapRenderMode::ActualLayout;
    }
    if (renderMode == nextMode) {
        return;
    }

    renderMode = nextMode;
    BuildRanges();
    Refresh();
}

void DriveMapPanel::SetClassFilter(DriveMapClassFilter filter) {
    if (classFilter == filter) {
        return;
    }

    classFilter = filter;
    BuildRanges();
    Refresh();
}

void DriveMapPanel::SetShowPlannedMoves(bool show) {
    if (showPlannedMoves == show) {
        return;
    }

    showPlannedMoves = show;
    Refresh();
}

// Builds sorted cluster ranges from read-only file extents, free-space bitmap, and classifications.
void DriveMapPanel::BuildRanges() {
    ranges.clear();
    totalClusters = GetTotalClusters();

    for (const auto& block : analysis.freeSpace.GetFreeBlocks()) {
        const auto start = block.startSector.getValue();
        const auto count = block.sectorCount.getValue();
        if (count > 0 && FilterAllowsState(classFilter, DriveMapRangeState::Free)) {
            ranges.push_back({start, count, DriveMapRangeState::Free});
        }
    }

    const auto classificationsByFile = BuildClassificationIndex(analysis);
    const auto intentsByFile = placementPlan.has_value() ? BuildIntentIndex(*placementPlan)
                                                         : std::unordered_map<std::size_t, const PlacementPlan::FilePlacementIntent*>();
    for (std::size_t fileIndex = 0; fileIndex < analysis.files.size(); ++fileIndex) {
        const auto& file = analysis.files[fileIndex];
        const FileClass* classification = classificationsByFile[fileIndex];
        const auto intent = intentsByFile.find(fileIndex);
        const DriveMapRangeState state =
            renderMode == DriveMapRenderMode::IntendedPlacement
                ? StateForIntendedFile(file,
                                       classification,
                                       intent == intentsByFile.end() ? nullptr : intent->second)
                : StateForFile(file, classification);
        if (!FilterAllowsState(classFilter, state)) {
            continue;
        }

        for (const auto& fragment : file.GetFragments()) {
            const auto start = fragment.startCluster.getValue();
            const auto count = fragment.clusterCount.getValue();
            if (count > 0) {
                ranges.push_back({start, count, state});
            }
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const DriveMapRange& lhs, const DriveMapRange& rhs) {
        if (lhs.startCluster == rhs.startCluster) {
            return StatePriority(lhs.state) > StatePriority(rhs.state);
        }
        return lhs.startCluster < rhs.startCluster;
    });
}

// Recalculates visual grid dimensions and clusters represented by each visible box.
void DriveMapPanel::RecalculateScale(const wxSize& size) {
    const int pitchX = (std::max)(1, settings.cellWidth + settings.cellGap);
    const int pitchY = (std::max)(1, settings.cellHeight + settings.cellGap);
    columns = (std::max)(1, size.GetWidth() / pitchX);
    rows = (std::max)(1, size.GetHeight() / pitchY);

    const auto availableBoxes = static_cast<std::uint64_t>(columns) * static_cast<std::uint64_t>(rows);
    clustersPerBox = CeilDivMinimumOne(totalClusters, availableBoxes);
}

void DriveMapPanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(28, 31, 35)));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);

    std::size_t rangeCursor = 0;
    const int pitchX = (std::max)(1, settings.cellWidth + settings.cellGap);
    const int pitchY = (std::max)(1, settings.cellHeight + settings.cellGap);

    for (int row = 0; row < rows; ++row) {
        int runStartColumn = -1;
        int runEndColumn = -1;
        DriveMapRangeState runState = DriveMapRangeState::Unknown;

        const auto flushRun = [&]() {
            if (runStartColumn < 0) {
                return;
            }

            const int runWidth = ((runEndColumn - runStartColumn) * pitchX) + settings.cellWidth;
            dc.SetBrush(wxBrush(ColourForState(runState)));
            dc.DrawRectangle(runStartColumn * pitchX, row * pitchY, runWidth, settings.cellHeight);
            runStartColumn = -1;
        };

        for (int column = 0; column < columns; ++column) {
            const auto boxIndex = static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(columns) +
                static_cast<std::uint64_t>(column);
            const auto startCluster = boxIndex * clustersPerBox;
            if (startCluster >= totalClusters) {
                flushRun();
                if ((showPlannedMoves || renderMode == DriveMapRenderMode::PlannedMoves) && movePlan.has_value()) {
                    DrawPlannedMoveOverlays(dc);
                }
                return;
            }

            const auto endCluster = (std::min)(totalClusters, SaturatingRangeEnd(startCluster, clustersPerBox));
            const DriveMapRangeState state = StateForBox(startCluster, endCluster, rangeCursor);

            if (runStartColumn < 0) {
                runStartColumn = column;
                runEndColumn = column;
                runState = state;
                continue;
            }

            if (state == runState) {
                runEndColumn = column;
                continue;
            }

            flushRun();
            runStartColumn = column;
            runEndColumn = column;
            runState = state;
        }

        flushRun();
    }

    if ((showPlannedMoves || renderMode == DriveMapRenderMode::PlannedMoves) && movePlan.has_value()) {
        DrawPlannedMoveOverlays(dc);
    }
}

void DriveMapPanel::OnSize(wxSizeEvent& event) {
    RecalculateScale(event.GetSize());
    Refresh();
    event.Skip();
}

void DriveMapPanel::DrawPlannedMoveOverlays(wxDC& dc) const {
    const wxPen sourcePen(wxColour(241, 91, 91), 1, wxPENSTYLE_SOLID);
    const wxPen targetPen(wxColour(245, 214, 92), 1, wxPENSTYLE_SOLID);
    for (const MoveOperation& operation : movePlan->operations) {
        DrawClusterRangeOutline(dc, operation.sourceStartCluster.getValue(), operation.clusterCount.getValue(), sourcePen);
        DrawClusterRangeOutline(dc, operation.targetStartCluster.getValue(), operation.clusterCount.getValue(), targetPen);
    }
}

void DriveMapPanel::DrawClusterRangeOutline(wxDC& dc,
                                            std::uint64_t startCluster,
                                            std::uint64_t clusterCount,
                                            const wxPen& pen) const {
    if (clusterCount == 0 || clustersPerBox == 0) {
        return;
    }

    const std::uint64_t endCluster = (std::min)(totalClusters, SaturatingRangeEnd(startCluster, clusterCount));
    if (startCluster >= totalClusters || endCluster <= startCluster) {
        return;
    }

    const std::uint64_t firstBox = startCluster / clustersPerBox;
    const std::uint64_t lastBox = (endCluster - 1) / clustersPerBox;
    const int pitchX = (std::max)(1, settings.cellWidth + settings.cellGap);
    const int pitchY = (std::max)(1, settings.cellHeight + settings.cellGap);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(pen);

    for (std::uint64_t box = firstBox; box <= lastBox; ++box) {
        const int row = static_cast<int>(box / static_cast<std::uint64_t>(columns));
        const int column = static_cast<int>(box % static_cast<std::uint64_t>(columns));
        if (row >= rows) {
            break;
        }

        const int x = column * pitchX;
        const int y = row * pitchY;
        dc.DrawRectangle(x - 1, y - 1, settings.cellWidth + 2, settings.cellHeight + 2);
    }
    dc.SetPen(*wxTRANSPARENT_PEN);
}

std::uint64_t DriveMapPanel::GetTotalClusters() const {
    const auto totalBytes = analysis.volume.totalBytes.getValue() > 0 ? analysis.volume.totalBytes.getValue()
                                                                      : analysis.drive.volume.totalBytes.getValue();
    const auto bytesPerCluster = analysis.volume.bytesPerCluster.getValue() > 0
        ? analysis.volume.bytesPerCluster.getValue()
        : analysis.drive.volume.bytesPerCluster.getValue();
    if (totalBytes > 0 && bytesPerCluster > 0) {
        return (std::max)(MinimumClusterCount, CeilDivMinimumOne(totalBytes, bytesPerCluster));
    }

    const auto totalSectors = analysis.geometry.GetTotalSectors().getValue();
    const auto sectorsPerCluster = analysis.drive.sectorsPerCluster.getValue();
    if (totalSectors > 0 && sectorsPerCluster > 0) {
        return (std::max)(MinimumClusterCount, CeilDivMinimumOne(totalSectors, sectorsPerCluster));
    }

    return (std::max)(MinimumClusterCount, totalSectors);
}

DriveMapRangeState DriveMapPanel::StateForBox(std::uint64_t startCluster, std::uint64_t endCluster,
                                              std::size_t& rangeCursor) const {
    while (rangeCursor < ranges.size()) {
        const auto rangeEnd =
            SaturatingRangeEnd(ranges[rangeCursor].startCluster, ranges[rangeCursor].clusterCount);
        if (rangeEnd > startCluster) {
            break;
        }
        ++rangeCursor;
    }

    DriveMapRangeState selected = DriveMapRangeState::Unknown;
    for (std::size_t index = rangeCursor; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        if (range.startCluster >= endCluster) {
            break;
        }

        const auto rangeEnd = SaturatingRangeEnd(range.startCluster, range.clusterCount);
        if (RangesOverlap(startCluster, endCluster, range.startCluster, rangeEnd) &&
            StatePriority(range.state) > StatePriority(selected)) {
            selected = range.state;
        }
    }

    return selected;
}

} // namespace icd
