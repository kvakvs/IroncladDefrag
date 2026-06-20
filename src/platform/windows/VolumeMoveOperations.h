#pragma once

#include "../../model/DomainTypes.h"

#include <filesystem>

namespace icd::win {

// Describes one write-capable file-cluster move request for the Win32 boundary.
struct FileMoveRequest {
    std::wstring rootPath;
    std::filesystem::path filePath;
    index64_t startingVcn = index64_t();
    index64_t targetLcn = index64_t();
    count64_t clusterCount = count64_t();
};

// Carries the Win32 result of one file-cluster move attempt.
struct FileMoveResult {
    bool succeeded = false;
    std::uint32_t errorCode = 0;
    std::wstring message;
};

MoveExecutionPrivilegeStatus ProbeMovePrivileges(const std::wstring& rootPath);
bool RelaunchElevated();
FileMoveResult MoveFileClusters(const FileMoveRequest& request);

} // namespace icd::win
