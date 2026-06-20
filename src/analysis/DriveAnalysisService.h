#pragma once

#include <atomic>
#include <functional>

#include "../model/DomainTypes.h"

namespace icd {

// Builds an AnalysisResult by scanning a selected drive with read-only operations.
class DriveAnalysisService {
public:
    using ProgressCallback = std::function<void(const JobProgress&)>;

    AnalysisResult Run(const DriveInfo& drive,
                       const std::atomic_bool& cancellationRequested,
                       const ProgressCallback& progressCallback) const;
};

} // namespace icd
