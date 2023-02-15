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

/*
 * This header file defines the software handlers ("spillers")
 * that spill generic entries from overflowing hardware task queues into
 * software buffers, and later fill the tasks back into hardware queues.
 *
 * A spiller allocates a heap chunk into which it stores tasks removed from a
 * hardware task queue, and enqueues a single "requeuer" task whose job is to
 * later read and deallocate the heap chunk and put the spilled tasks back into
 * the hardware queue.  A spiller can be thought of as "coalescing" several
 * hardware task queue entries by replacing them with a requeuer task occupying
 * a single queue entry.  When the requeuer runs, it's as if the single task
 * queue entry splits up into several task queue entries.  Previously,
 * "spillers" were called "coalescers" and "requeuers" were called "splitters".
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include <array>
// No application should ever #include spillers.h directly.
#include "../aligned.h"
#include "../hooks.h"
#include "../api.h"
#include "limits.h"
#include "hwtasks.h" // for __enqueue_task_skipargs and PLS_APP_MAX_ARGS


#if PLS_APP_MAX_ARGS==0
#   error "requeuer tasks take one argument, even if the app supports zero args"
#endif

namespace swarm {

struct TaskDescriptor {
    uint64_t ts;
    uint64_t taskPtrAndFlags; // [ 48 bits of task ptr | 16 bits of flags ]
    uint64_t hint;
    uint64_t args[PLS_APP_MAX_ARGS];
};

struct TaskDescriptors {
    uint64_t size;
    TaskDescriptor tds[0];  // that's OK, we'll dynamically size it
};

template<bool isFrame>
static inline void __enqueueOrYield(const TaskDescriptor& task) {
    // N.B. any task originally enqueued with NOHINT was assigned a hint
    // from a uniform distribution over ints from 0 to UINT64_MAX. That hint
    // sent the task to the ROB that houses this requeuer, so we reuse the
    // hint to enqueue the task to the same ROB.

    // The lower 16 bits of taskPtrAndFlags are the task's flags
    EnqFlags ef = EnqFlags(YIELDIFFULL | (task.taskPtrAndFlags & 0x0fffful));
    // This should be a constexpr if once we use C++17.
    if (isFrame) ef = EnqFlags(ef | PARENTDOMAIN);
    // The upper 48 bits are the task's pointer; recover with a 16-bit shift
    // arithmetic right (which preserves the sign, in case taskPtr is in the
    // upper half of the address space)
    uint64_t taskPtr;
    asm("sar $16,%%rcx;" : "=c"(taskPtr) : "c"(task.taskPtrAndFlags));

    static_assert(PLS_APP_MAX_ARGS <= 5, "Invalid PLS_APP_MAX_ARGS");
    uint64_t magicOp = enqueueMagicOp(PLS_APP_MAX_ARGS, ef);

    swarm::__enqueue_task_skipargs(magicOp, taskPtr, task.ts, task.hint
#if PLS_APP_MAX_ARGS > 0
                                 , task.args[0]
#if PLS_APP_MAX_ARGS > 1
                                 , task.args[1]
#if PLS_APP_MAX_ARGS > 2
                                 , task.args[2]
#if PLS_APP_MAX_ARGS > 3
                                 , task.args[3]
#if PLS_APP_MAX_ARGS > 4
                                 , task.args[4]
#endif
#endif
#endif
#endif
#endif
                                 );
}

// Requeuers were called "splitters" in the early Swarm papers
template <bool isFrame>
static inline void requeuer_impl(swarm::Timestamp, TaskDescriptors* descs) {
    // This requeuer can yield before an enqueue, so we always update the size
    // field directly
    __builtin_prefetch(&descs->tds[descs->size - 1].ts);
    while (descs->size) {
        if (descs->size >= 3) {
            __builtin_prefetch(&descs->tds[descs->size - 3].ts);
        }
        __enqueueOrYield<isFrame>(descs->tds[descs->size - 1]);
        descs->size--;
    }
    sim_zero_cycle_free(descs);
}

static inline void requeuer(swarm::Timestamp ts, TaskDescriptors* descs) {
    requeuer_impl<false>(ts, descs);
}
static inline void frame_requeuer(swarm::Timestamp ts, TaskDescriptors* descs) {
    requeuer_impl<true>(ts, descs);
}

// It'd be nice to use a bool template parameter rather than passing magicOp as
// an argument.  Unfortunately, due to a long standing bug in GCC, explicit
// register variables a[1-4] are not compiled correctly when put inside a
// template function.
// https://github.mit.edu/swarm/sim/pull/69
static inline uint64_t __removeOne(TaskDescriptor* task, TaskDescriptor* end,
                                   uint64_t magicOp, uint64_t minTs,
                                   uint64_t* requeuerFlags, bool* nonTimestamped) {
    // Prefetch two cachelines ahead, since the instruction loads two lines
    constexpr uint64_t mask = ~(SWARM_CACHE_LINE - 1ul);
    void* prefetch = (void*)(mask &
            (reinterpret_cast<uintptr_t>(&task->ts) + 2ul * SWARM_CACHE_LINE));
    if (pls_likely(prefetch < end)) __builtin_prefetch(prefetch, 1);

    uint64_t ts;
    uint64_t taskPtrAndFlags;
    uint64_t hint;

    // TODO: Change MAGIC_OP_TASK_REMOVE_UNTIED to clobber only regs with
    // args and use ifdefs to reduce register pressure
    uint64_t a0;
    register uint64_t a1      asm("r8");
    register uint64_t a2      asm("r9");
    register uint64_t a3      asm("r10");
    register uint64_t a4      asm("r11");

    COMPILER_BARRIER();
    // For some reason passing the MAGIC_OP as an input to the xchg rcx rcx
    // instruction requires one extra register to hold the MAGIC_OP before
    // moving it into rcx. The following forces the compiler to move the
    // MAGIC_OP directly into rcx, releliving register pressure by one.
    asm volatile("mov %0, %%rcx;"
        :
        : "g" (magicOp)
        :);
    asm volatile("xchg %%rcx, %%rcx;"
        : "=D"(ts), "=S"(taskPtrAndFlags), "=d"(hint),
          "=c"(a0), "=r"(a1), "=r"(a2), "=r"(a3), "=r"(a4)
        : "D"(minTs)
        :);
    COMPILER_BARRIER();

    if (pls_unlikely(taskPtrAndFlags == 0ul)) return UINT64_MAX;
    *requeuerFlags &= taskPtrAndFlags;
    *nonTimestamped = taskPtrAndFlags & EnqFlags::NOTIMESTAMP;

    task->ts = ts;
    task->taskPtrAndFlags = taskPtrAndFlags;
    task->hint = hint;
#if PLS_APP_MAX_ARGS > 0
    task->args[0] = a0;
#if PLS_APP_MAX_ARGS > 1
    task->args[1] = a1;
#if PLS_APP_MAX_ARGS > 2
    task->args[2] = a2;
#if PLS_APP_MAX_ARGS > 3
    task->args[3] = a3;
#if PLS_APP_MAX_ARGS > 4
    task->args[4] = a4;
#endif
#endif
#endif
#endif
#endif
    return ts;
}

template<bool isFrame>
static inline void spiller_impl(swarm::Timestamp, const uint32_t n) {
    // Remove n oldest untied tasks from the tile and dump them into memory
    TaskDescriptors* tdstruct = (TaskDescriptors*) sim_zero_cycle_untracked_malloc(
                        sizeof(TaskDescriptors) + n*sizeof(TaskDescriptor));
    TaskDescriptor* tasks = tdstruct->tds;

    // Prefetch for a later write (1)
    __builtin_prefetch(&tasks[0].ts, 1);

    constexpr uint64_t magicOp = isFrame ? MAGIC_OP_TASK_REMOVE_OUT_OF_FRAME
                                         : MAGIC_OP_TASK_REMOVE_UNTIED;
    uint64_t minTs = UINT64_MAX;
    TaskDescriptor* const begin = tasks;
    TaskDescriptor* const end = tasks + n;
    TaskDescriptor* task;
    // For ordinary (non-frame) spilling, tag the requeuer as
    // * NOTIMESTAMP iff all spilled tasks are NOTIMESTAMP
    // * CANTSPEC iff all spilled tasks are CANTSPEC
    // The latter ensures that the requeuer won't dump tasks that can't run now
    // anyway.
    uint64_t requeuerFlags = isFrame ? 0 : EnqFlags(NOTIMESTAMP | CANTSPEC);
    bool nonTimestamped = false;
    for (task = begin; task < end; task++) {
        uint64_t newMin = __removeOne(task, end, magicOp, minTs,
                                      &requeuerFlags, &nonTimestamped);
        // Frame spillers only get tasks from non-root domains.
        assert(!isFrame || !nonTimestamped);
        if (nonTimestamped || newMin == UINT64_MAX) break;
        // The timestamp of the removed task precedes (or equals) minTs,
        // so overwrite it.
        minTs = newMin;
    }

    if (nonTimestamped) {
        // Fall into this loop if any non-timestamped task was found. We must
        // bound the maxTS provided to the remove instruction to 0, i.e. we can
        // only extract non-timestamped or zero-timestamped tasks now.
        // Note: an increment was missing from the previous loop
        for (task = task + 1; task < end; task++) {
            uint64_t newMin = __removeOne(task, end, magicOp, 0,
                                          &requeuerFlags, &nonTimestamped);
            if (newMin == UINT64_MAX) break;
            if (!nonTimestamped) minTs = 0ul;
        }
    }

    tdstruct->size = std::distance(begin, task);

    if (tdstruct->size > 0) {
        if (!isFrame) {
            EnqFlags ef = SAMEHINT | NONSERIALHINT | NOHASH |
                          PRODUCER | REQUEUER | (EnqFlags)requeuerFlags;
            swarm::enqueue(swarm::requeuer, minTs, ef, tdstruct);
        } else {
            constexpr EnqFlags ef = SAMEHINT | NONSERIALHINT | NOHASH |
                                    PRODUCER | REQUEUER | CANTSPEC;
            swarm::enqueue(swarm::frame_requeuer, 42, ef, tdstruct);
        }
    } else {
        // Don't create a requeuer task if the ROB offered zero tasks.
        // Unfortunately, even if this spiller removed only one task, we must
        // wrap it in a requeuer. A requeuer is guaranteed to be enqueued to
        // the same tile as its spiller, whereas a normal task might be
        // hint-mapped to a different tile (e.g. due to hint-stealing). A
        // spiller must never stall, and there is no guarantee that different
        // tile has a free slot to which to enqueue.
        // Frankly, if spillers are frequently removing single tasks, then
        // there is a more important problem to solve in terms of when a
        // spiller is launched.
        sim_zero_cycle_free(tdstruct);
    }
}

// spiller (a.k.a. "coalescer") is enqueued naked (i.e., without a bareRunner)
__attribute__((noinline))
inline void spiller(swarm::Timestamp ts, const uint32_t n) {
    spiller_impl<false>(ts, n);
}
__attribute__((noinline))
inline void frame_spiller(swarm::Timestamp ts, const uint32_t n) {
    spiller_impl<true>(ts, n);
}


} // end namespace swarm
