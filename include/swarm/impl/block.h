/** $lic$
 * Copyright (C) 2014-2020 by Massachusetts Institute of Technology
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 * If you use this software in your research, we request that you send us a
 * citation of your work, and reference the Swarm MICRO 2015 paper ("A Scalable
 * Architecture for Ordered Parallelism", Jeffrey et al., MICRO-48, December
 * 2015) as the source of the simulator, or reference the T4 ISCA 2020 paper
 * ("T4: Compiling Sequential Code for Effective Speculative Parallelization in
 * Hardware", Ying et al., ISCA-47, June 2020) as the source of the compiler.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

#pragma once

#include <cstdint>
#include <iterator>
#include "../api.h"

namespace swarm {
namespace block {

template <class Iterator>
constexpr uint64_t elementsPerLine() {
    return std::max(1ul, SWARM_CACHE_LINE /
            sizeof(typename std::iterator_traits<Iterator>::value_type));
}


template <uint32_t LinesPerGrain>
inline uintptr_t cacheAlign(void* ptr) {
    return reinterpret_cast<uintptr_t>(ptr) / (SWARM_CACHE_LINE * LinesPerGrain);
}


template <uint32_t LinesPerGrain, class Iterator>
inline bool sameGrain(Iterator first, Iterator last) {
    return cacheAlign<LinesPerGrain>(&(*first)) ==
           cacheAlign<LinesPerGrain>(&(*(last - 1)));
}


template <class Iterator>
inline uint32_t grainSize(Iterator first, Iterator last) {
    static_assert(
            std::is_convertible<
#if __cplusplus > 201703L
                    typename std::iterator_traits<Iterator>::iterator_concept,
                    std::contiguous_iterator_tag>::value,
#else
                    typename std::iterator_traits<Iterator>::iterator_category,
                    std::random_access_iterator_tag>::value,
#endif
            "Iterator should be contiguous and random-access.");

    size_t elements = last - first;
    uint32_t numTasks = 4 * num_threads();
    uint32_t elemsPerTask = 1 + (elements - 1) / numTasks;
    constexpr uint32_t eSize = sizeof(*first);
    static_assert((eSize & (eSize - 1)) == 0, "element size must be power of 2");
    uint32_t cacheLinesPerTask = elemsPerTask * eSize / SWARM_CACHE_LINE;
    cacheLinesPerTask = std::max(1u, cacheLinesPerTask);
    uint32_t grainSize = 1 << (31u - __builtin_clz(cacheLinesPerTask));
    return grainSize;
}

}
}
