#include "core/snapshot/NamePool.h"

#include "core/snapshot/Types.h"

#include <functional>
#include <limits>

namespace icd::core {

    NameId NamePool::intern(std::string_view name) {
        const std::size_t hash = std::hash<std::string_view>{}(name);
        const auto [first, last] = lookup_.equal_range(hash);
        for (auto it = first; it != last; ++it) {
            if (this->name(it->second) == name) {
                return it->second;
            }
        }
        if (text_.size() + name.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw SnapshotError("name pool exceeds 4 GB");
        }
        const auto id = static_cast<NameId>(size());
        text_.append(name);
        offsets_.push_back(static_cast<std::uint32_t>(text_.size()));
        lookup_.emplace(hash, id);
        return id;
    }

    std::string_view NamePool::name(NameId id) const {
        const std::uint32_t begin = offsets_.at(id);
        return std::string_view(text_).substr(begin, offsets_.at(id + 1) - begin);
    }

    void NamePool::freezeLookup() { lookup_ = {}; }

} // namespace icd::core
