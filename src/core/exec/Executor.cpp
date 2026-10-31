#include "core/exec/Executor.h"

namespace icd::core {

    void InlineExecutor::parallelFor(ChunkedRange range, const RangeBody& body, const CancelToken& token) {
        for (std::size_t chunk = 0; chunk < range.chunks(); ++chunk) {
            token.throwIfCancelled();
            body(range.chunkBegin(chunk), range.chunkEnd(chunk));
        }
    }

} // namespace icd::core
