#include "../precompiled.h"
#include "DriveMapPanel.h"

#include <wx/dcbuffer.h>

#include <algorithm>
#include <cstddef>

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
            return 6;
        case DriveMapRangeState::Fragmented:
            return 5;
        case DriveMapRangeState::Hot:
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
    BuildRanges();
    RecalculateScale(GetClientSize());
    Refresh();
}

// Builds sorted cluster ranges from read-only file extents, free-space bitmap, and classifications.
void DriveMapPanel::BuildRanges() {
    ranges.clear();
    totalClusters = GetTotalClusters();

    for (const auto& block : analysis.freeSpace.GetFreeBlocks()) {
        const auto start = block.startSector.getValue();
        const auto count = block.sectorCount.getValue();
        if (count > 0) {
            ranges.push_back({start, count, DriveMapRangeState::Free});
        }
    }

    const auto classificationsByFile = BuildClassificationIndex(analysis);
    for (std::size_t fileIndex = 0; fileIndex < analysis.files.size(); ++fileIndex) {
        const auto& file = analysis.files[fileIndex];
        const FileClass* classification = classificationsByFile[fileIndex];
        const DriveMapRangeState state = StateForFile(file, classification);

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
        for (int column = 0; column < columns; ++column) {
            const auto boxIndex = static_cast<std::uint64_t>(row) * static_cast<std::uint64_t>(columns) +
                static_cast<std::uint64_t>(column);
            const auto startCluster = boxIndex * clustersPerBox;
            if (startCluster >= totalClusters) {
                return;
            }

            const auto endCluster = (std::min)(totalClusters, SaturatingRangeEnd(startCluster, clustersPerBox));
            const DriveMapRangeState state = StateForBox(startCluster, endCluster, rangeCursor);
            dc.SetBrush(wxBrush(ColourForState(state)));
            dc.DrawRectangle(column * pitchX, row * pitchY, settings.cellWidth, settings.cellHeight);
        }
    }
}

void DriveMapPanel::OnSize(wxSizeEvent& event) {
    RecalculateScale(event.GetSize());
    Refresh();
    event.Skip();
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
