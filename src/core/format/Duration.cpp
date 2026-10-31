#include "core/format/Duration.h"

#include <format>

namespace icd::core {

    std::string formatDuration(std::chrono::seconds duration) {
        const long long total = duration.count() < 0 ? 0 : duration.count();
        const long long hours = total / 3600;
        const long long minutes = total / 60 % 60;
        const long long seconds = total % 60;
        if (hours > 0) {
            return std::format("{}:{:02}:{:02}", hours, minutes, seconds);
        }
        return std::format("{}:{:02}", minutes, seconds);
    }

    std::string formatAge(std::chrono::seconds age) {
        using namespace std::chrono;
        if (age < minutes(1)) {
            return "just now";
        }
        if (age < hours(1)) {
            return std::format("{} min ago", duration_cast<minutes>(age).count());
        }
        if (age < hours(24)) {
            return std::format("{} h ago", duration_cast<hours>(age).count());
        }
        if (age < hours(48)) {
            return "yesterday";
        }
        return std::format("{} days ago", duration_cast<days>(age).count());
    }

} // namespace icd::core
