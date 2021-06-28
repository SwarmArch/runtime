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

#ifndef FROM_PLS_API
#error "This file cannot be included directly"
#endif

// TLS runtime: An amalgam of sequential and PLS runtimes; TLS parallelizes
// sequential programs, so this is at heart a sequential program with a software
// priority queue. This pq is accessed in a distributed, shared-memory fashion.
// Each worker thread runs the following sequence:
//   tls_deq_task [timestamp 0] -> task (which may enqueue other tasks) in the pq ->
//    tls_deq_task [same timestamp as last task] -> task -> etc.

#include <cassert>
#include <cstdio>
#include <queue>
#include <tuple>
#include "hwtasks.h"
#include "hwmisc.h"
#include "seq_tasks.h" // Don't support swarm::deepen yet

namespace swarm {

// Use thread-local storage to track the minimum timestamp for the next task
// The TLS runtime uses TLS... too many TLAs
extern volatile __thread Timestamp minTs __attribute__((aligned(SWARM_CACHE_LINE)));

static inline void tlsTask(uint64_t ts) {
    // Try to get a task, as in the SW runtime
    if (pq.empty()) return;
#ifndef PLS_SINGLE_TASKFUNC
    TaskState* t = pq.dequeueTop();
    minTs = pq.empty()? GetTimestamp(t) : pq.minTs();
    t->call();
#else
    auto args = pq.dequeueTop();
    minTs = pq.empty()? GetTimestamp(args) : pq.minTs();
    callFuncTuple(PLS_SINGLE_TASKFUNC, args);
#endif

    __enqueueHwTask<decltype(tlsTask), tlsTask>(minTs, EnqFlags(SAMEHINT | SAMETASK));
}

static void* tls_worker(void* arg /* unused */) {
    // Looks like this is on the stack
    //swarm::info("&minTs = %p", &minTs);
    minTs = 0;
    return pls_worker(arg);
}

static inline void run() {
    setup_task_handlers();
    uint32_t nthreads = num_threads();
    for (uint32_t t = 0; t < nthreads; t++) {
        // One per core; should have per-core ROBs for this to work well...
        __enqueueHwTask<decltype(tlsTask), tlsTask>(0, {t, EnqFlags::NOHASH});
    }
    launch_threads(tls_worker);
}

template<typename F, F* f, typename... Args>
void enqueueTask(Timestamp ts, Hint hint, Args... args) {
   if (ts < minTs) minTs = ts;
    __enqueueSwTask<F, f, Args...>(ts, hint, args...);
}

} // namespace swarm
