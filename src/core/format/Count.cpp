#include "core/format/Count.h"

namespace icd::core {

    std::string formatCount(std::uint64_t value) {
        const std::string digits = std::to_string(value);
        std::string out;
        out.reserve(digits.size() + digits.size() / 3);
        for (std::size_t i = 0; i < digits.size(); ++i) {
            if (i != 0 && (digits.size() - i) % 3 == 0) {
                out += ',';
            }
            out += digits[i];
        }
        return out;
    }

} // namespace icd::core
