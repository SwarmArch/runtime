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

#pragma once

#ifndef FROM_PLS_API
#error "This file cannot be included directly"
#endif

#include "enqflags.h"

#include <stdint.h>

#ifndef SWARM_CACHE_LINE
// FIXME(mcj) de-duplicate from aligned.h
#define SWARM_CACHE_LINE 64
#endif


/* Interface types, shared by all runtimes */

namespace swarm {

typedef uint64_t Timestamp;

/* Convenience struct to pass hint and flags together
 * Example: enqueue(..., 67, ...) -> flags = NONE and hint = 67
 *          enqueue(..., EnqFlags::NOHINT, ...) --> NOHINT flag
 */
struct Hint {
    const uint64_t hint;
    const EnqFlags flags;

    constexpr Hint(uint64_t h) : hint(h), flags(EnqFlags::NOFLAGS) {}
    constexpr Hint(EnqFlags f) : hint(0), flags(f) {}
    constexpr Hint(uint64_t h, EnqFlags f) : hint(h), flags(f) {}

    static inline uint64_t cacheLine(const volatile void* ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) / SWARM_CACHE_LINE);
    }

    static constexpr EnqFlags __replaceNoWithSame(EnqFlags flags) {
        return (flags & EnqFlags::NOHINT) ?
                EnqFlags((flags & ~NOHINT) | SAMEHINT) :
                flags;
    }
};

}
