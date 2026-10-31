#pragma once

#include <cstdint>
#include <string>

namespace icd::core {

    // Formats a count with thousands separators ("10,000"), the same on every machine, so UI text and logs do
    // not depend on the C locale (which also drives printf in formatBytes).
    std::string formatCount(std::uint64_t value);

} // namespace icd::core
