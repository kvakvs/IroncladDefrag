#include "core/job/CancelToken.h"

namespace icd::core {

    CancelSource::CancelSource(const CancelToken& parent) :
        link_(std::make_unique<std::stop_callback<Forward>>(parent.stdToken(), Forward{source_})) {}

} // namespace icd::core
