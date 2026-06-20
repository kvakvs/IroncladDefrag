#pragma once

#include <atomic>
#include <functional>

#include "../model/DomainTypes.h"

namespace icd {

class FakeAnalysisService {
public:
    using ProgressCallback = std::function<void(const JobProgress&)>;

    AnalysisResult Run(const std::atomic_bool& cancellationRequested, const ProgressCallback& progressCallback) const;
};

} // namespace icd
