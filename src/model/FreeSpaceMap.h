#pragma once
#include <vector>

#include "Units.h"

namespace icd {

class FreeSpaceMap {
public:
    struct FreeBlock {
        index64_t startSector;
        count64_t sectorCount;
    };

    FreeSpaceMap() = default;
    explicit FreeSpaceMap(std::vector<FreeBlock> blocks);

    const std::vector<FreeBlock>& GetFreeBlocks() const { return freeBlocks; }
    count64_t GetTotalFreeSectors() const { return totalFreeSectors; }
    count32_t GetFragmentationCount() const { return fragmentationCount; }
    double GetFragmentationRatio() const { return fragmentationRatio; }
    bool IsEmpty() const { return freeBlocks.empty(); }

private:
    std::vector<FreeBlock> freeBlocks;
    count64_t totalFreeSectors;
    count32_t fragmentationCount;
    double fragmentationRatio = 0.0;
};

} // namespace icd
