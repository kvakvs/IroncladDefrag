#pragma once
#include <vector>

#include "Units.h"

namespace icd {
class FragmentMap {
public:
    struct Fragment {
        index64_t startSector;
        index64_t endSector;
        byte_count64_t sizeBytes;
        bool isContiguous{};
    };

    FragmentMap() = default;
    explicit FragmentMap(std::vector<Fragment> fileFragments);

    const std::vector<Fragment>& GetFragments() const { return fragments; }
    count64_t GetTotalFragments() const { return totalFragments; }
    bool IsFileContiguous() const { return isFileContiguous; }
    bool IsEmpty() const { return fragments.empty(); }

private:
    std::vector<Fragment> fragments;
    count64_t totalFragments;
    bool isFileContiguous = true;
};
} // namespace icd
