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

// PLS runtime

#include <cstdlib>
#include <cassert>

#ifdef SCC_RUNTIME
#undef PLS_APP_MAX_ARGS
#endif
#include "hwtasks.h"
#include "hwmisc.h"

namespace swarm {

static inline void run() {
    setup_task_handlers();
    launch_threads(pls_worker);
}

template<typename F, F* f, typename... Args>
static inline void enqueueTask(Timestamp ts, Hint hint, Args... args) {
    __enqueueHwTask<F, f, Args...>(ts, hint, args...);
}

}
