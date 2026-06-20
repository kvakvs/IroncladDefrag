#include "FreeSpaceMap.h"

#include <cstdint>
#include <utility>

namespace icd
{
FreeSpaceMap::FreeSpaceMap(std::vector<FreeBlock> blocks)
    : freeBlocks(std::move(blocks)),
      fragmentationCount(static_cast<std::uint32_t>(freeBlocks.size()))
{
    std::uint64_t sectorTotal = 0;
    std::uint64_t largestBlock = 0;

    for (const FreeBlock& block : freeBlocks) {
        const std::uint64_t sectors = block.sectorCount.getValue();
        sectorTotal += sectors;
        if (sectors > largestBlock) {
            largestBlock = sectors;
        }
    }

    totalFreeSectors = count64_t(sectorTotal);
    fragmentationRatio = sectorTotal == 0 ? 0.0 : 1.0 - (static_cast<double>(largestBlock) / sectorTotal);
}
} // namespace icd
