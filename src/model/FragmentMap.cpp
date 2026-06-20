#include "FragmentMap.h"

#include <cstdint>
#include <utility>

namespace icd
{
FragmentMap::FragmentMap(std::vector<Fragment> fileFragments)
    : fragments(std::move(fileFragments)),
      totalFragments(static_cast<std::uint64_t>(fragments.size())),
      isFileContiguous(fragments.size() <= 1)
{
    for (const Fragment& fragment : fragments) {
        if (!fragment.isContiguous) {
            isFileContiguous = false;
            break;
        }
    }
}
} // namespace icd
