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

#ifndef __PLS_RAND_H__
#define __PLS_RAND_H__

#include "hooks.h"
#include <stdlib.h>

static inline uint64_t swarm_rand64(void) {
    uint64_t randVal = 42;
    sim_rdrand(&randVal);
    if (__builtin_expect(randVal == 42, 0)) {
        // not in sim, and not PLS, so no problem calling glibc's random
        randVal = (((uint64_t)random()) << 32) ^ random();
    }
    return randVal;
}

#ifdef __cplusplus

// Expose the (preferred) C++ API
namespace swarm {
static inline uint64_t rand64() { return swarm_rand64(); }
}

#endif

#endif // __PLS_RAND_H__
