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

// SCC runtime

#include <cassert>

#include <swarm/swarm.h>

#ifndef SCC_SERIAL_RUNTIME

#undef PLS_APP_MAX_ARGS
#include "hwmisc.h"
#include "../hooks.h"

#else

#include "scc/serial.h"
#include <scc/rt.h>

#endif

namespace swarm {

static inline void run() {
#ifndef SCC_SERIAL_RUNTIME
    // This part is the same as the swarm_runtime.h implementation of swarm::run()
    setup_task_handlers();
    launch_threads(pls_worker);
#else
    sccrt::runSerial();
#endif
}

template<typename F, F* f, typename... Args>
__attribute__((always_inline))
inline void enqueueTask(Timestamp ts, Hint hint, Args... args);

template<typename F, F* f, typename... Args>
inline void enqueueTask(Timestamp ts, Hint hint, Args... args) {
#ifndef SCC_SERIAL_RUNTIME
    // Coalescers must perform a bare enqueue of splitters with no hint hashing.
    // PRODUCER is also crucial for performance of unordered PLS apps.
    if (hint.flags & EnqFlags(NOHASH | PRODUCER)) {
        __enqueueHwTask<F, f, Args...>(ts, hint, args...);
        return;
    }
#endif

    swarm_spawn (ts) (*f)(ts, args...);
}

// For the serial runtime, we must provide serial implementations of features
// normally provided by the simulator via hwmisc.h
#ifdef SCC_SERIAL_RUNTIME

static inline uint32_t num_threads() { return 1; }
static inline uint32_t tid() { return 0; }

static inline Timestamp timestamp() {
    return __sccrt_serial_get_timestamp();
}
static inline Timestamp superTimestamp() {
    return __sccrt_serial_get_timestamp_super();
}

static inline void deepen(uint64_t maxTS) {
    assert(maxTS == -1ULL);
    __sccrt_serial_deepen();
}
static inline void undeepen() {
    __sccrt_serial_undeepen();
}

template <typename... Args>
static inline void info(const char* str, Args... args) {
    printf(str, args...);
    printf("\n");
}

// This is called by autoparallel.h.  When running with a serial runtime that
// does not create any threads, there's no need for the FS & GS thread state
// hack, so we can make this a no-op by providing this empty definition here.
static inline void __record_main_fsgs_addresses() {}

#endif

}
