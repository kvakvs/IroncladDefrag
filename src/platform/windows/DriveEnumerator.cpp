#include "../../precompiled.h"
#include "DriveEnumerator.h"

#include "UniqueHandle.h"

#include <winioctl.h>

#include <array>
#include <cwctype>
#include <sstream>

namespace icd::win {

namespace {
std::wstring KindLabel(DriveKind kind)
{
    switch (kind) {
    case DriveKind::Mechanical:
        return L"HDD";
    case DriveKind::SolidState:
        return L"SSD";
    case DriveKind::Removable:
        return L"Removable";
    case DriveKind::Network:
        return L"Network";
    case DriveKind::Unknown:
    default:
        return L"Unknown";
    }
}

bool IsNtfs(const std::wstring& fileSystem)
{
    return _wcsicmp(fileSystem.c_str(), L"NTFS") == 0;
}

DriveKind DriveKindFromDriveType(UINT driveType)
{
    switch (driveType) {
    case DRIVE_FIXED:
        return DriveKind::Unknown;
    case DRIVE_REMOVABLE:
    case DRIVE_CDROM:
        return DriveKind::Removable;
    case DRIVE_REMOTE:
        return DriveKind::Network;
    default:
        return DriveKind::Unknown;
    }
}

bool QuerySeekPenalty(HANDLE handle, bool& incursSeekPenalty)
{
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &query,
                         sizeof(query),
                         &descriptor,
                         sizeof(descriptor),
                         &bytesReturned,
                         nullptr)) {
        return false;
    }

    incursSeekPenalty = descriptor.IncursSeekPenalty != FALSE;
    return true;
}

bool QueryTrim(HANDLE handle, bool& trimEnabled)
{
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceTrimProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_TRIM_DESCRIPTOR descriptor{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(handle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &query,
                         sizeof(query),
                         &descriptor,
                         sizeof(descriptor),
                         &bytesReturned,
                         nullptr)) {
        return false;
    }

    trimEnabled = descriptor.TrimEnabled != FALSE;
    return true;
}

bool CanQueryVolumeBitmap(HANDLE handle)
{
    STARTING_LCN_INPUT_BUFFER input{};
    input.StartingLcn.QuadPart = 0;

    std::array<std::byte, 4096> output{};
    DWORD bytesReturned = 0;
    if (DeviceIoControl(handle,
                        FSCTL_GET_VOLUME_BITMAP,
                        &input,
                        sizeof(input),
                        output.data(),
                        static_cast<DWORD>(output.size()),
                        &bytesReturned,
                        nullptr)) {
        return true;
    }

    return GetLastError() == ERROR_MORE_DATA;
}

// Opens a volume handle with the requested access while preserving broad sharing for read-only probes.
UniqueHandle OpenVolume(const std::wstring& rootPath, DWORD desiredAccess)
{
    const std::wstring volumePath = ToVolumePath(rootPath);
    if (volumePath.empty()) {
        return UniqueHandle();
    }

    return UniqueHandle(CreateFileW(volumePath.c_str(),
                                    desiredAccess,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr));
}

std::wstring BuildDisplayName(const std::wstring& rootPath,
                              const std::wstring& label,
                              const std::wstring& fileSystem,
                              DriveKind kind)
{
    std::wstringstream stream;
    stream << rootPath;
    if (!label.empty()) {
        stream << L" " << label;
    }
    if (!fileSystem.empty()) {
        stream << L" [" << fileSystem << L"]";
    }
    stream << L" " << KindLabel(kind);
    return stream.str();
}
} // namespace

std::wstring ToVolumePath(const std::wstring& rootPath)
{
    if (rootPath.size() < 2 || rootPath[1] != L':') {
        return {};
    }

    std::wstring volumePath = L"\\\\.\\";
    volumePath.push_back(static_cast<wchar_t>(std::towupper(rootPath[0])));
    volumePath.push_back(L':');
    return volumePath;
}

UniqueHandle OpenVolumeForMetadata(const std::wstring& rootPath)
{
    return OpenVolume(rootPath, 0);
}

UniqueHandle OpenVolumeReadOnly(const std::wstring& rootPath)
{
    return OpenVolume(rootPath, GENERIC_READ);
}

// Builds the drive menu model with read-only capability checks for each visible drive.
std::vector<DriveInfo> DriveEnumerator::Enumerate() const
{
    std::vector<DriveInfo> drives;
    const DWORD driveMask = GetLogicalDrives();

    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const DWORD bit = 1u << (letter - L'A');
        if ((driveMask & bit) == 0) {
            continue;
        }

        std::wstring rootPath;
        rootPath.push_back(letter);
        rootPath.append(L":\\");

        const UINT driveType = GetDriveTypeW(rootPath.c_str());
        if (driveType == DRIVE_NO_ROOT_DIR) {
            continue;
        }

        DriveInfo drive;
        drive.rootPath = rootPath;
        drive.kind = DriveKindFromDriveType(driveType);
        drive.isFixed = driveType == DRIVE_FIXED;

        std::array<wchar_t, MAX_PATH + 1> label{};
        std::array<wchar_t, MAX_PATH + 1> fileSystem{};
        DWORD serialNumber = 0;
        DWORD maxComponentLength = 0;
        DWORD fileSystemFlags = 0;
        if (GetVolumeInformationW(rootPath.c_str(),
                                  label.data(),
                                  static_cast<DWORD>(label.size()),
                                  &serialNumber,
                                  &maxComponentLength,
                                  &fileSystemFlags,
                                  fileSystem.data(),
                                  static_cast<DWORD>(fileSystem.size()))) {
            drive.volume.label = label.data();
            drive.volume.fileSystem = fileSystem.data();
        }

        DWORD sectorsPerCluster = 0;
        DWORD bytesPerSector = 0;
        DWORD freeClusters = 0;
        DWORD totalClusters = 0;
        if (GetDiskFreeSpaceW(rootPath.c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) {
            drive.volume.bytesPerCluster = byte_count64_t(static_cast<std::uint64_t>(sectorsPerCluster) * bytesPerSector);
            drive.bytesPerSector = byte_count64_t(bytesPerSector);
            drive.sectorsPerCluster = count64_t(sectorsPerCluster);
        }

        ULARGE_INTEGER freeBytesAvailable{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER freeBytes{};
        if (GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytesAvailable, &totalBytes, &freeBytes)) {
            drive.volume.totalBytes = byte_count64_t(totalBytes.QuadPart);
            drive.volume.freeBytes = byte_count64_t(freeBytes.QuadPart);
        }

        UniqueHandle metadataVolume = OpenVolumeForMetadata(rootPath);
        drive.capabilities.canOpenVolumeMetadata = metadataVolume.IsValid();

        UniqueHandle bitmapVolume = OpenVolumeReadOnly(rootPath);
        drive.capabilities.canOpenVolume = bitmapVolume.IsValid();
        drive.capabilities.canQueryExtents = drive.isFixed && IsNtfs(drive.volume.fileSystem);

        HANDLE mediaProbe = metadataVolume.IsValid() ? metadataVolume.Get() : bitmapVolume.Get();
        if (mediaProbe != INVALID_HANDLE_VALUE && mediaProbe != nullptr) {
            bool incursSeekPenalty = false;
            if (QuerySeekPenalty(mediaProbe, incursSeekPenalty)) {
                drive.kind = incursSeekPenalty ? DriveKind::Mechanical : DriveKind::SolidState;
                drive.capabilities.mediaKnown = true;
            }

            bool trimEnabled = false;
            if (QueryTrim(mediaProbe, trimEnabled)) {
                drive.supportsTrim = trimEnabled;
            }
        }

        if (bitmapVolume.IsValid()) {
            drive.capabilities.canQueryBitmap = CanQueryVolumeBitmap(bitmapVolume.Get());
        }

        drive.supportsFileMove = drive.isFixed && drive.kind != DriveKind::SolidState && drive.capabilities.canOpenVolume;
        drive.capabilities.canAnalyze = drive.isFixed &&
                                        (drive.kind == DriveKind::Mechanical || drive.kind == DriveKind::Unknown);

        if (!drive.isFixed) {
            drive.capabilities.disabledReason = L"not a fixed local drive";
        } else if (drive.kind == DriveKind::SolidState) {
            drive.capabilities.disabledReason = L"SSD analysis is disabled; TRIM belongs to a later phase";
        } else if (!drive.capabilities.canQueryBitmap && !drive.capabilities.canQueryExtents) {
            drive.capabilities.statusReason = L"metadata analysis only; free-space bitmap and extents unavailable";
        } else if (!drive.capabilities.canQueryBitmap) {
            drive.capabilities.statusReason = L"free-space bitmap unavailable; metadata analysis only";
        } else if (!drive.capabilities.canQueryExtents) {
            drive.capabilities.statusReason = L"partial metadata analysis only; extent support may be unavailable";
        }

        drive.displayName = BuildDisplayName(drive.rootPath, drive.volume.label, drive.volume.fileSystem, drive.kind);
        drives.push_back(std::move(drive));
    }

    return drives;
}

} // namespace icd::win
