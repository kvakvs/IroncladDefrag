#pragma once

#include "../model/DomainTypes.h"

#include <vector>

namespace icd {

// Stores deterministic data-drive classification thresholds and directory hints.
struct ClassificationRules {
    byte_count64_t tinyFileThreshold = byte_count64_t(64ull * 1024ull);
    byte_count64_t smallFileThreshold = byte_count64_t(1ull * 1024ull * 1024ull);
    byte_count64_t mediumFileThreshold = byte_count64_t(128ull * 1024ull * 1024ull);
    byte_count64_t largeFileThreshold = byte_count64_t(4ull * 1024ull * 1024ull * 1024ull);
    std::chrono::hours hotRecency = std::chrono::hours(24 * 14);
    std::chrono::hours warmRecency = std::chrono::hours(24 * 60);
    std::chrono::hours coolRecency = std::chrono::hours(24 * 180);
    std::chrono::hours coldRecency = std::chrono::hours(24 * 365);
    std::vector<std::wstring> hotDirectoryHints = {L"projects", L"source", L"src", L"work", L"documents"};
    std::vector<std::wstring> coldDirectoryHints = {L"archive", L"archives", L"backup", L"backups", L"old", L"installers"};
    std::vector<std::wstring> userHotDirectoryOverrides;
    std::vector<std::wstring> userColdDirectoryOverrides;
};

// Adds deterministic data-drive file classifications to completed analysis snapshots.
class FileClassifier {
public:
    AnalysisResult Classify(AnalysisResult result,
                            const OptimizationSettings& settings = OptimizationSettings(),
                            const SafetySettings& safety = SafetySettings()) const;

private:
    ClassificationRules BuildRules(const OptimizationSettings& settings) const;
};

} // namespace icd
