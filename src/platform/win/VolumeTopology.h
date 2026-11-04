#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace icd::platform {

    // Storage setups that block moves (plan.md 4 and Phase 9) beyond what the storage property queries tell: dynamic
    // disks from the partition table and BitLocker from WMI. Bus types (RAID, Storage Spaces) and removable media come
    // from StorageProperties.

    // A partition as the layout query reports it, reduced to what identifies an LDM (dynamic disk) partition.
    struct PartitionType {
        bool gpt = false; // GPT partition (else MBR).
        std::uint8_t mbrType = 0; // MBR partition type byte.
        std::array<std::uint8_t, 16> gptType{}; // GPT partition type GUID, its bytes as a GUID lies in memory.
    };

    // Whether any partition is an LDM metadata or data partition, which only dynamic disks have. Kept apart for tests.
    [[nodiscard]] bool hasLdmPartition(std::span<const PartitionType> partitions);

    // Whether a physical disk is dynamic, from IOCTL_DISK_GET_DRIVE_LAYOUT_EX (no elevation needed); nullopt when the
    // disk did not answer.
    [[nodiscard]] std::optional<bool> isDynamicDisk(std::uint32_t diskNumber);

    // BitLocker state of every volume that WMI knows (Win32_EncryptableVolume, administrators only), keyed by the
    // volume GUID path in lower case ("\\?\volume{...}\"): true when encrypted, encrypting, decrypting or paused.
    // Empty when WMI cannot be asked (not elevated, BitLocker not installed).
    [[nodiscard]] std::map<std::wstring, bool> encryptedVolumes();

    // A volume GUID path in the form encryptedVolumes() keys use.
    [[nodiscard]] std::wstring volumeKey(std::wstring volumeName);

} // namespace icd::platform
