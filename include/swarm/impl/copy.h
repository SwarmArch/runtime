/** $lic$
 * Copyright (C) 2014-2021 by Massachusetts Institute of Technology
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

#include <cstdint>
#include <cstring>
#include "../api.h"
#include "block.h"

namespace swarm {


template <uint8_t LinesPerGrain, EnqFlags Flags>
static inline void __copier(Timestamp ts,
                            char* __restrict dest,
                            const char* __restrict source,
                            size_t bytes) {
    static_assert(LinesPerGrain != 0,
                  "ERROR: __copier<0> should never be created.");
    if (!bytes) {
        return;
    } else if (swarm::block::sameGrain<LinesPerGrain>(dest, dest + bytes)) {
        // Base-case: the destination is a single grain.  Copy serially.
        std::memcpy(dest, source, bytes);
    } else {
        char* d_midpoint = dest + bytes / 2;
        // Split destination on a cache-line boundary to avoid false sharing
        // between child tasks on stores to destination.
        uintptr_t d_cutAddr = reinterpret_cast<uintptr_t>(d_midpoint) &
                              ~(SWARM_CACHE_LINE - 1ul);

        const uintptr_t destAddr = reinterpret_cast<uintptr_t>(dest);
        if (d_cutAddr <= destAddr) {
            // The midpoint is in the same cache line as dest.
            // dest and dest+bytes must straddle a cache-line boundary
            // (otherwise they would be in the same grain).
            // Cut at the straddled cache-line boundary.
            d_cutAddr += SWARM_CACHE_LINE;
            assert(d_cutAddr > destAddr);
        }
        char* d_cut = reinterpret_cast<char*>(d_cutAddr);
        size_t leftHalfBytes = d_cut - dest;
        assert(leftHalfBytes < bytes);
        const char* s_cut = source + leftHalfBytes;
        constexpr EnqFlags RightFlags = EnqFlags(Flags | SAMETASK);
        constexpr EnqFlags LeftFlags =
                swarm::Hint::__replaceNoWithSame(RightFlags);
        swarm::enqueue((__copier<LinesPerGrain, Flags>), ts, LeftFlags,
                     dest, source, leftHalfBytes);
        swarm::enqueue((__copier<LinesPerGrain, Flags>), ts, RightFlags,
                     d_cut, s_cut, bytes - leftHalfBytes);
    }
}


template <EnqFlags Flags, class InputIt, class OutputIt>
static inline void copy(
        InputIt first, InputIt last,
        OutputIt d_first,
        Timestamp ts) {
    static_assert(!(Flags & EnqFlags::SAMETASK), "SAMETASK is invalid here");
    static_assert(!(Flags & EnqFlags::NOHASH), "NOHASH is a bad idea here");

    const char* source = reinterpret_cast<const char*>(&*first);
    char* dest = reinterpret_cast<char*>(&*d_first);
    size_t bytes = (last - first) * sizeof(*first);

    // this utility handles only non-overlapping ranges, like memcpy.
    // todo(mcj) handle overlapping cases as needed, like memmove.
    if (source < dest + bytes && dest < source + bytes)
        std::abort();

    uint32_t grainSize = swarm::block::grainSize(dest, dest + bytes);
    switch (grainSize) {
        case 1:
            swarm::enqueue((__copier<1u, Flags>), ts, Flags, dest, source, bytes);
            break;
        case 2:
            swarm::enqueue((__copier<2u, Flags>), ts, Flags, dest, source, bytes);
            break;
        case 4:
            swarm::enqueue((__copier<4u, Flags>), ts, Flags, dest, source, bytes);
            break;
        case 8:
            swarm::enqueue((__copier<8u, Flags>), ts, Flags, dest, source, bytes);
            break;
        default:
            swarm::enqueue((__copier<16u, Flags>), ts, Flags, dest, source, bytes);
            break;
    }
}

}
