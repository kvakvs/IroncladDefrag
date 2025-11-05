#pragma once
#include <vector>

#include "Units.h"

namespace icd {

    class FreeSpaceMap {
    private:
        struct FreeBlock {
            index64_t startSector;
            count64_t sectorCount;
        };

        std::vector<FreeBlock> freeBlocks;
        count64_t totalFreeSpace;
        count32_t fragmentationCount;
        double fragmentationRatio = 0.0;
    };

} // namespace icd
