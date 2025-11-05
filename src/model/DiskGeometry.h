#pragma once
#include <chrono>
#include <vector>

#include "Units.h"

namespace icd {

    class DiskGeometry {
    private:
        // Basic geometry
        sector_count64_t totalSectors;
        byte_count64_t bytesPerSector;
        sector_count32_t sectorsPerTrack;
        count32_t tracksPerCylinder;

        // Zone-based layout
        struct ZoneInfo {
            index32_t startCylinder;
            sector_count32_t sectorsPerTrack;
            megabyte_sec_t transferRate; // MB/s
        };
        std::vector<ZoneInfo> zones;

        // Performance characteristics
        struct PerformanceZone {
            index64_t startSector;
            index64_t endSector;
            megabyte_sec_t averageReadSpeed; // MB/s
            megabyte_sec_t averageWriteSpeed; // MB/s
            std::chrono::milliseconds averageSeekTime; // ms
            std::chrono::milliseconds rotationalLatency; // ms
        };
        std::vector<PerformanceZone> performanceZones;
    };

} // namespace icd
