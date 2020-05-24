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

#include <cstdint>
#include <algorithm>
#include "../api.h"
#include "block.h"

namespace swarm {


// TODO(mcj) add support for hints
// FIXME(mcj) this is dangerously assuming use of ContiguousIterators
// (http://en.cppreference.com/w/cpp/iterator), but doesn't actually check which
// type of iterator is used.
template <uint8_t BlockSize, EnqFlags Flags, class Iterator, class T>
static inline void __filler(Timestamp ts, Iterator first, Iterator last,
        const T& value) {
    static_assert(BlockSize != 0, "ERROR: __filler<0> should never be created.");
    if (first == last) {
        return;
    } else if (swarm::block::sameGrain<BlockSize>(first, last)) {
        // Base-case: first and last are within the same cache-line block
        std::fill(first, last, value);
    } else {
        Iterator midpoint = first + ((last - first) / 2);
        uintptr_t midpointAddr = reinterpret_cast<uintptr_t>(&(*midpoint));
        uintptr_t alignedAddr = midpointAddr & (~(SWARM_CACHE_LINE - 1ul));

        constexpr size_t elemSize = sizeof(*first);
        static_assert(!(elemSize & (elemSize - 1ul)),
                "element size should be a power of two");

        // FIXME(victory): What is with this insanely fragile logic?
        // d_first may be unaligned, so what if d_first > alignedAddr?
        if (alignedAddr == reinterpret_cast<uintptr_t>(&(*first))) {
            // The nearest aligned address is first. First and last must
            // straddle a cache line boundary (otherwise they would be in the
            // same block). Use the next cacheline for the midpoint.
            midpoint = first + SWARM_CACHE_LINE / elemSize;
            assert(midpoint < last);
        } else if (midpointAddr != alignedAddr) {
            // Align tasks to a cache line.
            uint64_t backwardBytes = midpointAddr & (SWARM_CACHE_LINE - 1ul);
            uint64_t backwardElems = backwardBytes / elemSize;
            midpoint = midpoint - backwardElems;
            // midpoint should be strictly ahead of first, otherwise the
            // aligned address of midpoint would equal first, and we'd be in
            // the block above
            assert(midpoint > first);
        }
        using fillFnTy = decltype(__filler<BlockSize, Flags, Iterator, T>);
        constexpr EnqFlags RightFlags = EnqFlags(Flags | SAMETASK);
        constexpr EnqFlags LeftFlags = swarm::Hint::__replaceNoWithSame(RightFlags);
        swarm::enqueueTask<fillFnTy, __filler<BlockSize, Flags, Iterator, T> >
                (ts, LeftFlags, first, midpoint, value);
        swarm::enqueueTask<fillFnTy, __filler<BlockSize, Flags, Iterator, T> >
                (ts, RightFlags, midpoint, last, value);
    }
}


// TODO(mcj) add support for hints
template <EnqFlags Flags, class Iterator, typename T>
static inline void fill(Iterator first, Iterator last, const T& value,
        Timestamp ts) {
    static_assert(!(Flags & EnqFlags::SAMETASK), "SAMETASK is invalid here");
    static_assert(!(Flags & EnqFlags::NOHASH), "NOHASH is a bad idea here");

    uint32_t blockSize = swarm::block::grainSize(first, last);
    switch(blockSize) {
        case 1:
        {
            using fillFnTy = decltype(__filler<1u, Flags, Iterator, T>);
            swarm::enqueueTask<fillFnTy, __filler<1u, Flags, Iterator, T> >(
                    ts, Flags, first, last, value);
            break;
        }
        case 2:
        {
            using fillFnTy = decltype(__filler<2u, Flags, Iterator, T>);
            swarm::enqueueTask<fillFnTy, __filler<2u, Flags, Iterator, T> >(
                    ts, Flags, first, last, value);
            break;
        }
        case 4:
        {
            using fillFnTy = decltype(__filler<4u, Flags, Iterator, T>);
            swarm::enqueueTask<fillFnTy, __filler<4u, Flags, Iterator, T> >(
                    ts, Flags, first, last, value);
            break;
        }
        case 8:
        {
            using fillFnTy = decltype(__filler<8u, Flags, Iterator, T>);
            swarm::enqueueTask<fillFnTy, __filler<8u, Flags, Iterator, T> >(
                    ts, Flags, first, last, value);
            break;
        }
        default:
        {
            using fillFnTy = decltype(__filler<16u, Flags, Iterator, T>);
            swarm::enqueueTask<fillFnTy, __filler<16u, Flags, Iterator, T> >(
                    ts, Flags, first, last, value);
            break;
        }
    }
}

}
