#pragma once

#include <chrono>
#include <string>

namespace icd::core {

    // Formats elapsed or remaining time like a stopwatch: "0:42", "12:05", "1:02:03". Negative values
    // count as zero.
    std::string formatDuration(std::chrono::seconds duration);

    // Formats how long ago something happened, coarsely, for lists: "just now", "5 min ago", "3 h ago",
    // "yesterday", "12 days ago". Times in the future count as "just now".
    std::string formatAge(std::chrono::seconds age);

} // namespace icd::core
