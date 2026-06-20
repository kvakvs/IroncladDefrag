#pragma once

#include "../model/DomainTypes.h"

#include <cstdint>
#include <vector>
#include <wx/wx.h>

namespace icd {

// Stores the currently fixed visual geometry for the read-only drive map.
struct DriveMapRenderSettings {
    int cellWidth = 2;
    int cellHeight = 2;
    int cellGap = 1;
};

// Names the visible state chosen for a drive-map cluster range.
enum class DriveMapRangeState { Unknown, Free, Occupied, Hot, Cold, Fragmented, Risky };

// Stores one cluster range and the highest-level map state known for it.
struct DriveMapRange {
    std::uint64_t startCluster = 0;
    std::uint64_t clusterCount = 0;
    DriveMapRangeState state = DriveMapRangeState::Unknown;
};

// Renders a read-only, scaled cluster map for one analysed drive snapshot.
class DriveMapPanel : public wxPanel {
public:
    DriveMapPanel(wxWindow* parent, const AnalysisResult& result);

    void UpdateResult(const AnalysisResult& result);

private:
    void BuildRanges();
    void RecalculateScale(const wxSize& size);
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);

    std::uint64_t GetTotalClusters() const;
    DriveMapRangeState StateForBox(std::uint64_t startCluster,
                                   std::uint64_t endCluster,
                                   std::size_t& rangeCursor) const;

    AnalysisResult analysis;
    DriveMapRenderSettings settings;
    std::vector<DriveMapRange> ranges;
    std::uint64_t totalClusters = 1;
    std::uint64_t clustersPerBox = 1;
    int columns = 1;
    int rows = 1;
};

} // namespace icd
