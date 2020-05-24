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

#ifndef FROM_PLS_API
#error "This file cannot be included directly"
#endif

// Sequential runtime

#include <cassert>
#include <stdio.h>
#include <queue>
#include "../hooks.h"
#include "seq_tasks.h"

namespace swarm {

static inline void run() {
    zsim_roi_begin();
    while (!pq.empty()) {
#ifndef PLS_SINGLE_TASKFUNC
        TaskState* t = pq.dequeueTop();
        t->call();
#else
        auto args = pq.dequeueTop();
        callFuncTuple(PLS_SINGLE_TASKFUNC, args);
#endif
    }
    zsim_roi_end();
}

static inline void info(const char* str) {
    puts(str);
    puts("\n");
}

template <typename... Args>
static inline void info(const char* str, Args... args) {
    printf(str, args...);
    printf("\n");
}

static inline uint32_t num_threads() { return 1; }
static inline uint32_t tid() { return 0; }
static inline void serialize() {}
static inline void deepen(uint64_t) {
    swarm::info("swarm::deepen() unimplemented");
    std::abort();
}
static inline void undeepen() {
    swarm::info("swarm::undeepen() unimplemented");
    std::abort();
}
static inline void setGvt(Timestamp) {}
static inline void clearReadSet() {}
static inline void recordAsAborted() {}
static inline Timestamp timestamp() {
    return 0;  // FIXME(victory): implement this for seq runtime
}
static inline Timestamp superTimestamp() {
    return 0;  // FIXME(victory): implement this for seq runtime
}

template<typename F, F* f, typename... Args>
void enqueueTask(Timestamp ts, Hint hint, Args... args) {
   __enqueueSwTask<F, f, Args...>(ts, hint, args...);
}

}
