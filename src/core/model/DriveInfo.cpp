#include "core/model/DriveInfo.h"

#include <algorithm>

namespace icd::core {

    double DriveInfo::usedFraction() const {
        if (totalBytes == 0) {
            return 0.0;
        }
        return std::clamp(static_cast<double>(usedBytes()) / static_cast<double>(totalBytes), 0.0, 1.0);
    }

    bool canOptimize(const DriveInfo& drive) { return !isSsd(drive) && drive.fileSystem == "NTFS"; }

    std::vector<MoveBlock> moveBlocks(const DriveInfo& drive) {
        const StorageFacts& s = drive.storage;
        std::vector<MoveBlock> blocks;
        if (s.bus == "RAID") {
            blocks.push_back(MoveBlock::Raid);
        }
        if (s.bus == "Storage Spaces") {
            blocks.push_back(MoveBlock::StorageSpaces);
        }
        if (s.dynamicDisk || drive.layout.diskExtentCount > 1) {
            blocks.push_back(MoveBlock::DynamicDisk);
        }
        if (s.removable) {
            blocks.push_back(MoveBlock::Removable);
        }
        if (s.encrypted.value_or(false)) {
            blocks.push_back(MoveBlock::Encrypted);
        }
        return blocks;
    }

    bool canMove(const DriveInfo& drive) { return canOptimize(drive) && moveBlocks(drive).empty(); }

    MediaType classifyMedia(const StorageFacts& facts) {
        if (facts.virtualDisk) {
            return MediaType::Virtual;
        }
        if (facts.seekPenalty) {
            return *facts.seekPenalty ? MediaType::Hdd : MediaType::Ssd;
        }
        return facts.trimEnabled.value_or(false) ? MediaType::Ssd : MediaType::Unknown;
    }

    bool isSsd(const DriveInfo& drive) { return drive.media == MediaType::Ssd; }

} // namespace icd::core
