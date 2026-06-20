#pragma once

#include "../model/DomainTypes.h"

#include <atomic>
#include <functional>

namespace icd {

// Executes a bounded, revalidated move plan without depending on wxWidgets.
class MoveExecutor {
public:
    using ProgressCallback = std::function<void(const JobProgress&)>;

    MoveExecutionResult Execute(const AnalysisResult& analysis,
                                const MovePlan& plan,
                                const std::atomic_bool& cancellationRequested,
                                const ProgressCallback& progressCallback) const;
};

} // namespace icd
