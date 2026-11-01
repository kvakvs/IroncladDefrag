#include "platform/win/StorageProperties.h"

#include "platform/win/Handle.h"
#include "platform/win/Volumes.h"

#include <windows.h>

#include <winioctl.h>

#include <array>
#include <format>
#include <vector>

namespace icd::platform {

    namespace {

        // Runs one standard storage property query into `out`; false when the device does not support it.
        template <typename T>
        bool queryProperty(HANDLE device, STORAGE_PROPERTY_ID id, T& out) {
            STORAGE_PROPERTY_QUERY query{};
            query.PropertyId = id;
            query.QueryType = PropertyStandardQuery;
            DWORD bytes = 0;
            return DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &out, sizeof(out),
                                   &bytes, nullptr) != FALSE &&
                bytes >= sizeof(out);
        }

        // Text at an offset of the device descriptor (vendor, product), trimmed; empty for offset 0.
        std::string descriptorText(const std::vector<std::byte>& buffer, DWORD offset) {
            if (offset == 0 || offset >= buffer.size()) {
                return {};
            }
            std::string text(reinterpret_cast<const char*>(buffer.data() + offset));
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
            return text;
        }

        // Bus type, removable flag and model from STORAGE_DEVICE_DESCRIPTOR.
        void readDescriptor(HANDLE device, core::StorageFacts& facts) {
            STORAGE_PROPERTY_QUERY query{};
            query.PropertyId = StorageDeviceProperty;
            query.QueryType = PropertyStandardQuery;
            std::vector<std::byte> buffer(1024);
            DWORD bytes = 0;
            if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE ||
                bytes < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
                return;
            }
            buffer.back() = std::byte{0}; // The strings inside are NUL-terminated; make sure the buffer is too.
            const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
            facts.bus = busTypeName(descriptor->BusType);
            facts.virtualDisk = descriptor->BusType == BusTypeFileBackedVirtual;
            facts.removable = descriptor->RemovableMedia != FALSE;
            facts.model = descriptorText(buffer, descriptor->ProductIdOffset);
        }

        // All facts from an open device handle.
        core::StorageFacts queryDevice(HANDLE device) {
            core::StorageFacts facts;
            DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
            if (queryProperty(device, StorageDeviceSeekPenaltyProperty, seek)) {
                facts.seekPenalty = seek.IncursSeekPenalty != FALSE;
            }
            DEVICE_TRIM_DESCRIPTOR trim{};
            if (queryProperty(device, StorageDeviceTrimProperty, trim)) {
                facts.trimEnabled = trim.TrimEnabled != FALSE;
            }
            readDescriptor(device, facts);
            return facts;
        }

    } // namespace

    core::StorageFacts queryDiskStorage(std::uint32_t diskNumber) {
        const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", diskNumber);
        const UniqueHandle disk(
            CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        return disk ? queryDevice(disk.get()) : core::StorageFacts{};
    }

    core::StorageFacts queryVolumeStorage(const std::wstring& volumeName) {
        const UniqueHandle volume = openVolume(volumeName, 0);
        return volume ? queryDevice(volume.get()) : core::StorageFacts{};
    }

    std::string busTypeName(unsigned busType) {
        // Indexed by STORAGE_BUS_TYPE; names follow Get-PhysicalDisk, an unknown bus has no name.
        static constexpr std::array<const char*, 19> kNames{
            "",    "SCSI", "ATAPI", "ATA", "1394",    "SSA",     "Fibre Channel",  "USB",  "RAID", "iSCSI",
            "SAS", "SATA", "SD",    "MMC", "Virtual", "Virtual", "Storage Spaces", "NVMe", "SCM"};
        return busType < kNames.size() ? kNames[busType] : std::format("bus {}", busType);
    }

} // namespace icd::platform
