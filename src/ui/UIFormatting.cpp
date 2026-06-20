#include "../precompiled.h"
#include "UIFormatting.h"

#include <sstream>

namespace icd::ui {

std::wstring FormatBytes(byte_count64_t bytes)
{
    double value = static_cast<double>(bytes.getValue());
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        ++unitIndex;
    }

    std::wstringstream stream;
    stream.precision(unitIndex == 0 ? 0 : 1);
    stream << std::fixed << value << L" " << units[unitIndex];
    return stream.str();
}

std::wstring DriveKindName(DriveKind kind)
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

std::wstring CapabilityBadge(const DriveInfo& drive)
{
    std::wstringstream text;
    auto needSeparator = false;

    if (drive.capabilities.canAnalyze) {
        text << L"Can analyze";
        needSeparator = true;
    }
    if (drive.supportsFileMove) {
        if (needSeparator) {
            text << L" | ";
        }
        text << L"Can move files";
        needSeparator = true;
    }
    if (drive.supportsTrim) {
        if (needSeparator) {
            text << L" | ";
        }
        text << L"TRIM";
    }
    return text.str();
}

std::wstring ProfileModeName(OptimizationMode mode)
{
    switch (mode) {
    case OptimizationMode::BalancedDataDrive:
        return L"Balanced";
    case OptimizationMode::LargeFileContiguous:
        return L"Large-file";
    case OptimizationMode::SmallFileFastZone:
        return L"Small-file fast";
    case OptimizationMode::ColdArchive:
        return L"Cold archive";
    case OptimizationMode::DirectoryClustering:
        return L"Directory clustering";
    case OptimizationMode::FileTypeSegregation:
        return L"File types";
    case OptimizationMode::SizeBasedPlacement:
        return L"Size placement";
    case OptimizationMode::FreeSpaceOptimization:
        return L"Free space";
    case OptimizationMode::SingleFileDefragmentation:
        return L"Single-file defrag";
    default:
        return L"Optimization";
    }
}

} // namespace icd::ui
