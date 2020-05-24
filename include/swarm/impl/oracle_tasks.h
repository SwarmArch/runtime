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

#ifndef FROM_PLS_API
#error "This file cannot be included directly"
#endif

#include <stack>
#include "swtasks.h"

namespace swarm {

// [mcj] To handle swarm::deepen(), we need a stack of priority queues to
// represent virtual time intervals. This is implemented in the Oracle runtime,
// but not the Sequential runtime, due to the extra indirection incurred by the
// stack-to-priority-queue access. Perhaps we can have a global PriorityQueue*
// that represents the latest top of the stack. Or maybe the indirection of
// pqs.top()->top() isn't too bad for the sequential runtime.
_PLS_GLOBAL_PQ_QUALIFIER std::stack<swarm::PriorityQueue*> pqs;

#ifndef PLS_SINGLE_TASKFUNC

template<typename F, F* f, typename... Args>
#ifdef SWTASKS_USE_UID
void __enqueueSwTask(uint64_t uid, Timestamp ts, Hint hint, Args... args) {
#else
void __enqueueSwTask(Timestamp ts, Hint hint, Args... args) {
#endif
    if (false) {
        // Check it's a well-typed call
        // If you see a compiler error here, you're using the wrong args...
        f(ts, args...);
    }

    const bool useParentDomain = hint.flags & EnqFlags::PARENTDOMAIN;
    PriorityQueue* pqToEnqueueTask = nullptr;
    PriorityQueue* childPq = nullptr;

    if (useParentDomain) {
        assert(pqs.size() > 1);
        childPq = pqs.top(); pqs.pop();
        pqToEnqueueTask = pqs.top();
    } else pqToEnqueueTask = pqs.top();

#ifdef SWTASKS_USE_UID
    pqToEnqueueTask->push(new Task<F, f, Args...>(uid, ts, args...));
#else
    pqToEnqueueTask->push(new Task<F, f, Args...>(ts, args...));
#endif
    if (childPq) pqs.push(childPq);
}

#else

#ifndef SWTASKS_USE_UID
template<typename F, F* f, typename... Args>
void __enqueueSwTask(Timestamp ts, Hint hint, Args... args) {
    static_assert(f == PLS_SINGLE_TASKFUNC, "PLS_SINGLE_TASKFUNC defined, but does not match enqueued function.");

    pqs.top()->push(TaskArgs(ts, args...));
}
#else
template<typename F, F* f, typename... Args>
void __enqueueSwTask(uint64_t uid, Timestamp ts, Hint hint, Args... args) {
    static_assert(f == PLS_SINGLE_TASKFUNC, "PLS_SINGLE_TASKFUNC defined, but does not match enqueued function.");

    pqs.top()->push(TaskArgs(uid, ts, args...));
}
#endif

#endif

}
