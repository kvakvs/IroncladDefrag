#pragma once

#include "../../model/DomainTypes.h"
#include "UniqueHandle.h"

#include <vector>

namespace icd::win {

// Enumerates visible Windows drives and their read-only analysis capabilities.
class DriveEnumerator {
public:
    std::vector<DriveInfo> Enumerate() const;
};

std::wstring ToVolumePath(const std::wstring& rootPath);
UniqueHandle OpenVolumeForMetadata(const std::wstring& rootPath);
UniqueHandle OpenVolumeReadOnly(const std::wstring& rootPath);

} // namespace icd::win
