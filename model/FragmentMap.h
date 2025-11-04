#pragma once
#include <vector>

#include "Units.h"

namespace icd {
    class FragmentMap {
    private:
        struct Fragment {
            index64_t startSector;
            index64_t endSector;
            byte_count64_t sizeBytes;
            bool isContiguous{};
        };

        std::vector<Fragment> fragments;
        count64_t totalFragments;
        bool isFileContiguous = true;
    };
} // namespace icd
