#pragma once

#include "../model/DomainTypes.h"

#include <string>

namespace icd::ui {

std::wstring FormatBytes(byte_count64_t bytes);
std::wstring DriveKindName(DriveKind kind);
std::wstring CapabilityBadge(const DriveInfo& drive);
std::wstring ProfileModeName(OptimizationMode mode);

} // namespace icd::ui
