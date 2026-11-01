#pragma once

#include "core/model/DriveInfo.h"

#include <cstdint>
#include <string>

namespace icd::platform {

    // Media facts of a physical disk (\\.\PhysicalDriveN) through IOCTL_STORAGE_QUERY_PROPERTY: seek penalty,
    // TRIM, bus type, removable, model. Opens the disk without read access, so no elevation is needed.
    core::StorageFacts queryDiskStorage(std::uint32_t diskNumber);

    // The same for a volume device, for volumes without a single disk (spanned, striped); the storage stack
    // forwards the queries when it can.
    core::StorageFacts queryVolumeStorage(const std::wstring& volumeName);

    // Display name of a STORAGE_BUS_TYPE value ("SATA", "NVMe", ...), as Get-PhysicalDisk names them;
    // empty for an unknown bus.
    std::string busTypeName(unsigned busType);

} // namespace icd::platform
