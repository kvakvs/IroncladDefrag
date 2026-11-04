#include "platform/win/VolumeTopology.h"

#include "platform/win/Handle.h"

#include <windows.h>

#include <Wbemidl.h>
#include <winioctl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <format>
#include <vector>

namespace icd::platform {

    namespace {

        using Microsoft::WRL::ComPtr;

        // LDM metadata partition type ({5808C8AA-7E8F-42E0-85D2-E1E90434CFB3}, PARTITION_LDM_METADATA_GUID).
        constexpr GUID ldmMetadata{0x5808C8AA, 0x7E8F, 0x42E0, {0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}};
        // LDM data partition type ({AF9B60A0-1431-4F62-BC68-3311714A69AD}, PARTITION_LDM_DATA_GUID).
        constexpr GUID ldmData{0xAF9B60A0, 0x1431, 0x4F62, {0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD}};

        // Whether a partition's GPT type bytes are the given GUID.
        bool sameGuid(const std::array<std::uint8_t, 16>& bytes, const GUID& guid) {
            static_assert(sizeof(GUID) == 16);
            return std::memcmp(bytes.data(), &guid, sizeof(GUID)) == 0;
        }

        // The type of one partition of a drive layout.
        PartitionType typeOf(const PARTITION_INFORMATION_EX& partition) {
            PartitionType type;
            type.gpt = partition.PartitionStyle == PARTITION_STYLE_GPT;
            if (type.gpt) {
                std::memcpy(type.gptType.data(), &partition.Gpt.PartitionType, sizeof(GUID));
            }
            else if (partition.PartitionStyle == PARTITION_STYLE_MBR) {
                type.mbrType = partition.Mbr.PartitionType;
            }
            return type;
        }

        // The partition table of an open disk; nullopt when the disk does not answer. The buffer grows until the
        // whole layout fits.
        std::optional<std::vector<PartitionType>> layoutOf(HANDLE disk) {
            std::vector<std::byte> buffer(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 16 * sizeof(PARTITION_INFORMATION_EX));
            DWORD bytes = 0;
            while (DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE) {
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() > (1U << 20)) {
                    return std::nullopt;
                }
                buffer.resize(buffer.size() * 2);
            }
            const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(buffer.data());
            std::vector<PartitionType> types;
            types.reserve(layout->PartitionCount);
            for (DWORD i = 0; i < layout->PartitionCount; ++i) {
                types.push_back(typeOf(layout->PartitionEntry[i])); // PartitionCount entries follow the header.
            }
            return types;
        }

        // Initializes COM on this thread for the scope; a thread already in another apartment keeps it (WMI works
        // from either).
        class ComScope {
        public:
            ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
            ComScope(const ComScope&) = delete;
            ComScope& operator=(const ComScope&) = delete;
            ~ComScope() {
                if (SUCCEEDED(result_)) {
                    CoUninitialize();
                }
            }

        private:
            HRESULT result_; // S_OK or S_FALSE: balanced by CoUninitialize; another mode: nothing to undo.
        };

        // Owns a BSTR for one WMI call.
        class Bstr {
        public:
            explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {}
            Bstr(const Bstr&) = delete;
            Bstr& operator=(const Bstr&) = delete;
            ~Bstr() { SysFreeString(value_); }

            // The string for the call (still owned).
            [[nodiscard]] BSTR get() const { return value_; }

        private:
            BSTR value_; // Allocated by SysAllocString.
        };

        // Owns a VARIANT read from a WMI object.
        struct Variant {
            VARIANT value{}; // Filled by IWbemClassObject::Get.
            Variant() { VariantInit(&value); }
            Variant(const Variant&) = delete;
            Variant& operator=(const Variant&) = delete;
            ~Variant() { VariantClear(&value); }
        };

        // The BitLocker namespace, with the packet privacy it requires; null when WMI or BitLocker is not there.
        ComPtr<IWbemServices> connectBitLocker() {
            ComPtr<IWbemLocator> locator;
            if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) {
                return nullptr;
            }
            ComPtr<IWbemServices> services;
            const Bstr space(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
            if (FAILED(locator->ConnectServer(space.get(), nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                              nullptr, nullptr, &services))) {
                return nullptr;
            }
            if (FAILED(CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                         RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                                         EOAC_NONE))) {
                return nullptr;
            }
            return services;
        }

        // Reads one Win32_EncryptableVolume into the map (objects without a device id are skipped).
        void readVolume(IWbemClassObject& object, std::map<std::wstring, bool>& out) {
            Variant id;
            Variant status;
            if (FAILED(object.Get(L"DeviceID", 0, &id.value, nullptr, nullptr)) || id.value.vt != VT_BSTR ||
                FAILED(object.Get(L"ConversionStatus", 0, &status.value, nullptr, nullptr))) {
                return;
            }
            // ConversionStatus: 0 fully decrypted; 1 fully encrypted, 2-5 converting or paused.
            const bool encrypted = status.value.vt == VT_I4 && status.value.lVal != 0;
            out[volumeKey(id.value.bstrVal)] = encrypted;
        }

    } // namespace

    bool hasLdmPartition(std::span<const PartitionType> partitions) {
        return std::ranges::any_of(partitions, [](const PartitionType& p) {
            return p.gpt ? sameGuid(p.gptType, ldmMetadata) || sameGuid(p.gptType, ldmData)
                         : p.mbrType == PARTITION_LDM;
        });
    }

    std::optional<bool> isDynamicDisk(std::uint32_t diskNumber) {
        const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", diskNumber);
        const UniqueHandle disk(
            CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!disk) {
            return std::nullopt;
        }
        const auto layout = layoutOf(disk.get());
        return layout ? std::optional(hasLdmPartition(*layout)) : std::nullopt;
    }

    std::map<std::wstring, bool> encryptedVolumes() {
        const ComScope com;
        std::map<std::wstring, bool> volumes;
        const ComPtr<IWbemServices> services = connectBitLocker();
        if (!services) {
            return volumes;
        }
        const Bstr language(L"WQL");
        const Bstr query(L"SELECT DeviceID, ConversionStatus FROM Win32_EncryptableVolume");
        ComPtr<IEnumWbemClassObject> rows;
        if (FAILED(services->ExecQuery(language.get(), query.get(),
                                       WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows))) {
            return volumes;
        }
        constexpr long timeoutMs = 10'000; // A hung provider must not hang the drive list.
        ComPtr<IWbemClassObject> object;
        ULONG returned = 0;
        while (rows->Next(timeoutMs, 1, &object, &returned) == WBEM_S_NO_ERROR && returned == 1) {
            readVolume(*object.Get(), volumes);
            object.Reset();
        }
        return volumes;
    }

    std::wstring volumeKey(std::wstring volumeName) {
        std::ranges::transform(volumeName, volumeName.begin(),
                               [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return volumeName;
    }

} // namespace icd::platform
