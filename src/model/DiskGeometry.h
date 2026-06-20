#pragma once
#include <chrono>
#include <utility>
#include <vector>

#include "Units.h"

namespace icd {

// Stores drive geometry and coarse performance zones used by analysis and future placement strategies.
class DiskGeometry {
public:
    // Describes a hardware-style cylinder zone when low-level geometry is available.
    struct ZoneInfo {
        index32_t startCylinder;
        sector_count32_t sectorsPerTrack;
        megabyte_sec_t transferRate; // MB/s
    };

    // Describes an analysis-friendly sector range with approximate performance characteristics.
    struct PerformanceZone {
        index64_t startSector;
        index64_t endSector;
        megabyte_sec_t averageReadSpeed; // MB/s
        megabyte_sec_t averageWriteSpeed; // MB/s
        std::chrono::milliseconds averageSeekTime{}; // ms
        std::chrono::milliseconds rotationalLatency{}; // ms
    };

    DiskGeometry() = default;
    DiskGeometry(sector_count64_t total,
                 byte_count64_t sectorBytes,
                 sector_count32_t trackSectors,
                 count32_t cylinderTracks,
                 std::vector<ZoneInfo> zoneLayout = {},
                 std::vector<PerformanceZone> performanceLayout = {})
        : totalSectors(total),
          bytesPerSector(sectorBytes),
          sectorsPerTrack(trackSectors),
          tracksPerCylinder(cylinderTracks),
          zones(std::move(zoneLayout)),
          performanceZones(std::move(performanceLayout))
    {
    }

    sector_count64_t GetTotalSectors() const { return totalSectors; }
    byte_count64_t GetBytesPerSector() const { return bytesPerSector; }
    sector_count32_t GetSectorsPerTrack() const { return sectorsPerTrack; }
    count32_t GetTracksPerCylinder() const { return tracksPerCylinder; }
    const std::vector<ZoneInfo>& GetZones() const { return zones; }
    const std::vector<PerformanceZone>& GetPerformanceZones() const { return performanceZones; }
    bool HasGeometry() const { return totalSectors.getValue() > 0 && bytesPerSector.getValue() > 0; }

private:
    sector_count64_t totalSectors;
    byte_count64_t bytesPerSector;
    sector_count32_t sectorsPerTrack;
    count32_t tracksPerCylinder;
    std::vector<ZoneInfo> zones;
    std::vector<PerformanceZone> performanceZones;
};

} // namespace icd
