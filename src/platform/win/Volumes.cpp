#include "platform/win/Volumes.h"

#include "platform/win/WinError.h"

#include <windows.h>

#include <winioctl.h>

#include <algorithm>
#include <format>

namespace icd::platform {

    namespace {

        // Keeps the first failure only: later ones are usually consequences of it.
        void note(VolumeRecord& record, const std::wstring& what) {
            if (record.problem.empty()) {
                record.problem = what + L": " + lastWinError();
            }
        }

        // Label, file system, drive type and sizes from the path-based APIs.
        void readBasics(VolumeRecord& record) {
            wchar_t label[MAX_PATH + 1]{};
            wchar_t fs[MAX_PATH + 1]{};
            const wchar_t* name = record.volumeName.c_str();
            record.driveType = GetDriveTypeW(name);
            if (GetVolumeInformationW(name, label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH) == FALSE) {
                note(record, L"GetVolumeInformationW");
                return;
            }
            record.label = label;
            record.fileSystem = fs;
            ULARGE_INTEGER total{};
            ULARGE_INTEGER freeBytes{};
            if (GetDiskFreeSpaceExW(name, nullptr, &total, &freeBytes) != FALSE) {
                record.totalBytes = total.QuadPart;
                record.freeBytes = freeBytes.QuadPart;
            }
            DWORD sectorsPerCluster = 0;
            DWORD bytesPerSector = 0;
            DWORD freeClusters = 0;
            DWORD totalClusters = 0;
            // Only the sizes are used: the cluster counts are 32-bit and wrap on big volumes.
            if (GetDiskFreeSpaceW(name, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters) != FALSE) {
                record.layout.bytesPerSector = bytesPerSector;
                record.layout.bytesPerCluster = sectorsPerCluster * bytesPerSector;
            }
            if (record.layout.bytesPerCluster != 0) {
                record.layout.totalClusters = record.totalBytes / record.layout.bytesPerCluster;
                record.layout.freeClusters = record.freeBytes / record.layout.bytesPerCluster;
            }
        }

        // Size of a physical disk, for "% of disk" positions.
        std::uint64_t diskSize(std::uint32_t diskNumber) {
            const std::wstring path = std::format(L"\\\\.\\PhysicalDrive{}", diskNumber);
            const UniqueHandle disk(
                CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
            DISK_GEOMETRY_EX geometry{};
            DWORD bytes = 0;
            if (!disk ||
                DeviceIoControl(disk.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geometry, sizeof(geometry),
                                &bytes, nullptr) == FALSE) {
                return 0;
            }
            return static_cast<std::uint64_t>(geometry.DiskSize.QuadPart);
        }

        // Disk number and partition offset when the volume lives on one disk extent.
        void readDiskExtents(HANDLE volume, VolumeRecord& record) {
            // Room for 32 extents covers spanned and striped volumes; only the count matters for those.
            std::vector<std::byte> buffer(sizeof(VOLUME_DISK_EXTENTS) + 31 * sizeof(DISK_EXTENT));
            DWORD bytes = 0;
            if (DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE) {
                note(record, L"IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS");
                return;
            }
            const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
            record.layout.diskExtentCount = extents->NumberOfDiskExtents;
            if (extents->NumberOfDiskExtents == 1) {
                const DISK_EXTENT& extent = extents->Extents[0];
                record.layout.diskNumber = extent.DiskNumber;
                record.layout.partitionOffset = static_cast<std::uint64_t>(extent.StartingOffset.QuadPart);
                record.layout.diskBytes = diskSize(extent.DiskNumber);
            }
        }

        // NTFS volume data plus the extended block with the NTFS version.
        struct NtfsData {
            NTFS_VOLUME_DATA_BUFFER base; // Clusters, MFT position and zone.
            NTFS_EXTENDED_VOLUME_DATA extended; // Version; present when ByteCount says so.
        };

        // Copies FSCTL_GET_NTFS_VOLUME_DATA into the core layout; exact cluster counts come from here.
        core::NtfsLayout toLayout(const NtfsData& data, VolumeRecord& record) {
            const NTFS_VOLUME_DATA_BUFFER& v = data.base;
            record.layout.totalClusters = static_cast<std::uint64_t>(v.TotalClusters.QuadPart);
            record.layout.freeClusters = static_cast<std::uint64_t>(v.FreeClusters.QuadPart);
            record.layout.bytesPerCluster = v.BytesPerCluster;
            record.layout.bytesPerSector = v.BytesPerSector;
            core::NtfsLayout ntfs;
            ntfs.serialNumber = static_cast<std::uint64_t>(v.VolumeSerialNumber.QuadPart);
            ntfs.bytesPerFileRecord = v.BytesPerFileRecordSegment;
            ntfs.mftValidDataLength = static_cast<std::uint64_t>(v.MftValidDataLength.QuadPart);
            ntfs.mftStartLcn = static_cast<std::uint64_t>(v.MftStartLcn.QuadPart);
            ntfs.mft2StartLcn = static_cast<std::uint64_t>(v.Mft2StartLcn.QuadPart);
            ntfs.mftZoneStart = static_cast<std::uint64_t>(v.MftZoneStart.QuadPart);
            ntfs.mftZoneEnd = static_cast<std::uint64_t>(v.MftZoneEnd.QuadPart);
            ntfs.totalReserved = static_cast<std::uint64_t>(v.TotalReserved.QuadPart);
            if (data.extended.ByteCount >= sizeof(DWORD) + 2 * sizeof(WORD)) {
                ntfs.majorVersion = data.extended.MajorVersion;
                ntfs.minorVersion = data.extended.MinorVersion;
            }
            return ntfs;
        }

        // Reads the NTFS data; needs a handle with read access.
        void readNtfs(HANDLE volume, VolumeRecord& record) {
            NtfsData data{};
            DWORD bytes = 0;
            if (DeviceIoControl(volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &data, sizeof(data), &bytes, nullptr) ==
                FALSE) {
                note(record, L"FSCTL_GET_NTFS_VOLUME_DATA");
                return;
            }
            if (bytes < sizeof(data.base) + sizeof(DWORD) * 2) {
                data.extended = {};
            }
            record.layout.ntfs = toLayout(data, record);
        }

        // Disk extents (any access) and NTFS data (read access, so administrators only).
        void readDevice(VolumeRecord& record) {
            UniqueHandle volume = openVolume(record.volumeName, GENERIC_READ);
            const bool canRead = volume.valid();
            if (!canRead) {
                note(record, L"open volume for reading");
                volume = openVolume(record.volumeName, 0);
            }
            if (!volume) {
                note(record, L"open volume");
                return;
            }
            readDiskExtents(volume.get(), record);
            if (canRead && record.fileSystem == L"NTFS") {
                readNtfs(volume.get(), record);
            }
        }

    } // namespace

    std::vector<std::wstring> listVolumeNames() {
        std::vector<std::wstring> names;
        wchar_t name[MAX_PATH + 1]{};
        HANDLE find = FindFirstVolumeW(name, MAX_PATH);
        if (find == INVALID_HANDLE_VALUE) {
            return names;
        }
        do {
            names.emplace_back(name);
        }
        while (FindNextVolumeW(find, name, MAX_PATH) != FALSE);
        FindVolumeClose(find);
        return names;
    }

    std::vector<std::wstring> mountPathsOf(const std::wstring& volumeName) {
        DWORD length = 0;
        GetVolumePathNamesForVolumeNameW(volumeName.c_str(), nullptr, 0, &length);
        if (GetLastError() != ERROR_MORE_DATA || length == 0) {
            return {};
        }
        std::wstring buffer(length, L'\0');
        if (GetVolumePathNamesForVolumeNameW(volumeName.c_str(), buffer.data(), length, &length) == FALSE) {
            return {};
        }
        std::vector<std::wstring> paths;
        for (const wchar_t* p = buffer.c_str(); *p != L'\0'; p += wcslen(p) + 1) {
            paths.emplace_back(p);
        }
        return paths;
    }

    UniqueHandle openVolume(const std::wstring& volume, unsigned long access) {
        std::wstring device = volume;
        if (device.size() == 3 && device[1] == L':') {
            device = L"\\\\.\\" + device.substr(0, 2); // "C:\" -> "\\.\C:"
        }
        while (!device.empty() && device.back() == L'\\') {
            device.pop_back(); // A trailing backslash would open the root directory, not the volume.
        }
        return UniqueHandle(CreateFileW(device.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, 0, nullptr));
    }

    VolumeRecord queryVolume(const std::wstring& volumeName) {
        VolumeRecord record;
        record.volumeName = volumeName;
        record.mountPaths = mountPathsOf(volumeName);
        readBasics(record);
        if (!record.fileSystem.empty()) {
            readDevice(record);
        }
        return record;
    }

    std::vector<VolumeRecord> enumerateVolumes() {
        std::vector<VolumeRecord> volumes;
        for (const std::wstring& name : listVolumeNames()) {
            if (mountPathsOf(name).empty()) {
                continue; // Recovery and EFI partitions: nothing a user can pick.
            }
            VolumeRecord record = queryVolume(name);
            if (!record.fileSystem.empty()) {
                volumes.push_back(std::move(record));
            }
        }
        std::ranges::sort(volumes, {}, [](const VolumeRecord& v) { return v.mountPaths.front(); });
        return volumes;
    }

    bool isElevated() {
        HANDLE raw = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) == FALSE) {
            return false;
        }
        const UniqueHandle token(raw);
        TOKEN_ELEVATION elevation{};
        DWORD bytes = 0;
        return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytes) != FALSE &&
            elevation.TokenIsElevated != 0;
    }

} // namespace icd::platform
