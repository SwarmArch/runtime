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

// Oracle Speculative runtime
// An application compiled with this runtime can be run with the oracle
// speculation simulator.
// Note that oracle_hooks.h assumes its opcodes are in sync with that simulator.
// This could be a source of error in the future.

#include <cassert>
#include <stdio.h>
#include <iostream>
#include <queue>
#include <ucontext.h>

#include "oracle_hooks.h"

#define SWTASKS_USE_UID
#include "oracle_tasks.h"

namespace swarm {

#define MAX_TS (-1ul)

extern uint64_t taskIdCounter;
extern uint64_t curTaskId;
extern uint64_t curTaskTS;
extern std::stack<uint64_t> superTsStack;

static inline void runloop() {
    std::array<int, 1024> a;
    int x = 0;
    sim_assert_is_stackaddr(&a);
    sim_assert_is_stackaddr(&x);
    swarm::info("&pqs:            %p", &pqs);
    swarm::info("&pqs->top():     %p", pqs.top());
    swarm::info("&taskIdCounter: %p", &taskIdCounter);
    swarm::info("&curTaskId:     %p", &curTaskId);
    swarm::info("&curTaskTS:     %p", &curTaskTS);
    while (!pqs.empty()) {
        while (!pqs.top()->empty()) {
#ifndef PLS_SINGLE_TASKFUNC
            TaskState* t = pqs.top()->dequeueTop();
            curTaskId = t->uid;
            curTaskTS = t->ts;
            t->call();
            delete t;
#else
            auto args = pqs.top()->dequeueTop();
            curTaskId = std::get<0>(args);
            curTaskTS = std::get<1>(args);
            // Avoid adding curTaskId to the readset of the task
            const uint64_t tid = curTaskId;
            sim_task_begin(tid);
            callFuncTupleIgnoringFirstArg(PLS_SINGLE_TASKFUNC, args);
            sim_task_end(tid);
#endif
        }
        PriorityQueue* pq = pqs.top();
        pqs.pop();
        delete pq;
        superTsStack.pop();
        sim_magic_op_0(SIM_TASK_UNDEEPEN);
    }
}

static inline void run() {
    // Use ucontexts to switch stacks without creating a new pthread
    size_t stackSize = 8*1024*1024;
    void* stackPtr = malloc(stackSize);
    sim_set_stack(stackPtr, stackSize);
    ucontext_t workerContext, returnContext;
    getcontext(&workerContext);
    workerContext.uc_stack.ss_sp = stackPtr;  // thread 0 --> offset 0
    workerContext.uc_stack.ss_size = stackSize;
    workerContext.uc_link = &returnContext;
    makecontext(&workerContext, (void (*)())runloop, 0);
    int err = swapcontext(&returnContext, &workerContext);
    assert(!err); (void) err;
}

static inline void info(const char* str) {
    // For some reason puts/printf don't work within the run-loop. Probably
    // related to the switching of ucontext?
    std::cout << str << std::endl;
}

template <typename... Args>
static inline void info(const char* str, Args... args) {
    printf(str, args...);
    std::cout << std::endl;
}

static inline uint32_t num_threads() { return (UINT32_MAX / 128); }
//TODO(victory): Should the oracle return random thread IDs to distribute dependences?
static inline uint32_t tid() { return 0; }
static inline void serialize() {}
static inline void clearReadSet() {}
static inline void recordAsAborted() {}

template<typename F, F* f, typename... Args>
void enqueueTask(Timestamp ts, Hint hint, Args... args) {
    sim_stop_recording();
    // dsm: I don't understand why this is needed; I've added start/stop
    // recording so that we can filter out every instruction in enqueueTask
    if (curTaskId == MAX_TS) {
        curTaskId = 0;
        assert(pqs.empty());
        pqs.push(new swarm::PriorityQueue());
        superTsStack.push(UINT64_MAX);
        sim_resume_recording();
        sim_task_create(0,0,0,0,0,0);
        sim_task_begin(0);
        sim_task_end(0);
        sim_stop_recording();
    }

    uint64_t newTaskId = ++taskIdCounter;
    size_t nargs = sizeof...(args);
    sim_task_create(curTaskId, newTaskId, ts, (uint64_t) hint.flags, hint.hint, nargs);
    __enqueueSwTask<F, f, Args...>(newTaskId, ts, hint, args...);
    sim_resume_recording();
}


static inline void deepen(uint64_t maxTS) {
    sim_stop_recording();
    // FIXME: Ideally assert this. But maxflow uses
    // maxTS != -1 --> It does not affect correctness
    // but we currently ignore maxTS settings in ordspecsim.
    if (maxTS != -1ul) swarm::info("WARN: maxTS: %lu != -1 used, ignoring...", maxTS);
    pqs.push(new swarm::PriorityQueue());
    superTsStack.push(curTaskTS);
    sim_magic_op_1(SIM_TASK_DEEPEN, curTaskId);
    sim_resume_recording();
}

static inline void undeepen() {
    swarm::info("swarm::undeepen() unimplemented");
    std::abort();
}

static inline void setGvt(Timestamp ts) {
    sim_stop_recording();
    sim_magic_op_2(SIM_TASK_SET_GVT, curTaskId, ts);
    sim_resume_recording();
}

static inline Timestamp timestamp() {
    sim_stop_recording();
    uint64_t ret = curTaskTS;
    sim_resume_recording();
    return ret;
}

static inline Timestamp superTimestamp() {
    sim_stop_recording();
    uint64_t ret = superTsStack.top();
    sim_resume_recording();
    return ret;
}

static inline void mallocPartition(void*, void*, uint64_t) {}

}  // namespace swarm
