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

#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <tuple>
#include <assert.h>

#include <queue>
#include <ext/pb_ds/priority_queue.hpp>

#include "callfunc.h"
#include "types.h"

namespace swarm {

// Implement dequeueTop and minTs over the standard prio queues
template <typename T, typename PQ, uint64_t (*GetTimestamp)(const T&)>
class dtpq : public PQ {
    public:
        inline T dequeueTop() {
            T v = PQ::top();
            PQ::pop();
            return v;
        }
        inline uint64_t minTs() const {
            return GetTimestamp(PQ::top());
        }
};

#ifndef PLS_SINGLE_TASKFUNC

struct TaskState {
#ifdef SWTASKS_USE_UID
    const uint64_t uid;
#endif
    const Timestamp ts;
#ifdef SWTASKS_USE_UID
    TaskState(uint64_t _uid, Timestamp _ts) : uid(_uid), ts(_ts) {}
#else
    TaskState(Timestamp _ts) : ts(_ts) {}
#endif
    virtual ~TaskState() = default;
    virtual void call() = 0;
};

template <typename F, F* f, typename... Args>
struct Task : TaskState {
    const std::tuple<Args...> args;
#ifdef SWTASKS_USE_UID
    Task(uint64_t _uid, Timestamp _ts, Args... _args) : TaskState(_uid, _ts), args(_args...) {}
#else
    Task(Timestamp _ts, Args... _args) : TaskState(_ts), args(_args...) {}
#endif
    void call() {
#ifdef SWTASKS_USE_UID
        // Avoid adding reads of uid and ts to the read-set of the task. Load
        // the values into registers before the task begins. It would be nice to
        // do the same with "args", but I'm not sure of how to achieve that with
        // parameter packs.
        const uint64_t reg_uid = uid;
        const uint64_t reg_ts = ts;
        sim_task_begin(reg_uid);
        callFunc(f, reg_ts, args, typename gens<sizeof...(Args)>::type());
        sim_task_end(reg_uid);
#else
        callFunc(f, ts, args, typename gens<sizeof...(Args)>::type());
#endif
    }
};

struct CompareTasks {
    bool operator()(const TaskState* a, const TaskState* b) {
        return a->ts > b->ts;
    }
};

inline uint64_t GetTimestamp(TaskState* const& a) {
    return a->ts;
}

// This priority queue is instantiated in the accompanying library
#define _PLS_GLOBAL_PQ_QUALIFIER extern

using PriorityQueue = dtpq<TaskState*, std::priority_queue<TaskState*, std::vector<TaskState*>, CompareTasks>, GetTimestamp>;
// dsm: PBDS is slower than the native priority_queue here...
// pairing heap performs better than thin and binary heaps; binomial and rc_binomial do not compile
//using PriorityQueue = __gnu_pbds::priority_queue<TaskState*, CompareTasks, __gnu_pbds::pairing_heap_tag>;

#else

#ifndef PLS_SINGLE_TASKFUNC_ARGS
#error "Must define PLS_SINGLE_TASKFUNC_ARGS with PLS_SINGLE_TASKFUNC"
#endif

// If we need task UIDs, allow
#ifndef SWTASKS_USE_UID
#define TASKARGS_TS_POS 0
typedef std::tuple<Timestamp, PLS_SINGLE_TASKFUNC_ARGS> TaskArgs;
#else
#define TASKARGS_TS_POS 1
typedef std::tuple<uint64_t, Timestamp, PLS_SINGLE_TASKFUNC_ARGS> TaskArgs;
#endif

struct CompareTasks {
    bool operator()(const TaskArgs& a, const TaskArgs& b) {
        return std::get<TASKARGS_TS_POS>(a) > std::get<TASKARGS_TS_POS>(b);
    }
};

inline uint64_t GetTimestamp(const TaskArgs& a) {
    return std::get<TASKARGS_TS_POS>(a);
}

//using PriorityQueue = std::priority_queue<TaskArgs, std::vector<TaskArgs>, CompareTasks>;
// pbds pairing_heap works better than std heap on non-native types
using PriorityQueue = dtpq<TaskArgs, __gnu_pbds::priority_queue<TaskArgs, CompareTasks>, GetTimestamp>;

#endif

#ifndef _PLS_GLOBAL_PQ_QUALIFIER
// There is one PQ configuration above (the "default") that the shared runtime
// library instantiates. Other than that one case, the library can't possibly
// know which to use. This is because of our pattern to make custom definitions
// just before #including api.h.
// So users of custom libraries, be warned.
// FIXME(mcj) there must be a more elegant way to customize the runtime
// priority queue? I think that putting global variables into a shared library
// is at least less hacky than the previous SECONDARY_INCLUDE pattern.
#define _PLS_GLOBAL_PQ_QUALIFIER
#endif


}
