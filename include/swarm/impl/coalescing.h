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

#include <cstdint>
#include <algorithm>
#include <array>
// No application should ever #include coalescing.h directly.
#include "../aligned.h"
#include "../hooks.h"
#include "../api.h"
#include "limits.h"
#include "hwtasks.h" // for __enqueue_task_skipargs and PLS_APP_MAX_ARGS


#if PLS_APP_MAX_ARGS==0
#   error "Splitter tasks take one argument, even if the app supports zero args"
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
    // sent the task to the ROB that houses this splitter, so we reuse the
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

template <bool isFrame>
static inline void splitter_impl(swarm::Timestamp, TaskDescriptors* descs) {
    // This splitter can yield before an enqueue, so we always update the size
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

static inline void splitter(swarm::Timestamp ts, TaskDescriptors* descs) {
    splitter_impl<false>(ts, descs);
}
static inline void frame_splitter(swarm::Timestamp ts, TaskDescriptors* descs) {
    splitter_impl<true>(ts, descs);
}

// It'd be nice to use a bool template parameter rather than passing magicOp as
// an argument.  Unfortunately, due to a long standing bug in GCC, explicit
// register variables a[1-4] are not compiled correctly when put inside a
// template function.
// https://github.mit.edu/swarm/sim/pull/69
static inline uint64_t __removeOne(TaskDescriptor* task, TaskDescriptor* end,
                                   uint64_t magicOp, uint64_t minTs,
                                   uint64_t* splitterFlags, bool* nonTimestamped) {
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
    *splitterFlags &= taskPtrAndFlags;
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
static inline void coalescer_impl(swarm::Timestamp, const uint32_t n) {
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
    // For ordinary (non-frame) coalescing/splitting, tag the splitter as
    // * NOTIMESTAMP iff all spilled tasks are NOTIMESTAMP
    // * CANTSPEC iff all spilled tasks are CANTSPEC
    // The latter ensures that the splitter won't dump tasks that can't run now
    // anyway.
    uint64_t splitterFlags = isFrame ? 0 : EnqFlags(NOTIMESTAMP | CANTSPEC);
    bool nonTimestamped = false;
    for (task = begin; task < end; task++) {
        uint64_t newMin = __removeOne(task, end, magicOp, minTs,
                                      &splitterFlags, &nonTimestamped);
        // Frame coalescers only get tasks from non-root domains.
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
                                          &splitterFlags, &nonTimestamped);
            if (newMin == UINT64_MAX) break;
            if (!nonTimestamped) minTs = 0ul;
        }
    }

    tdstruct->size = std::distance(begin, task);

    if (tdstruct->size > 0) {
        if (!isFrame) {
            EnqFlags ef = EnqFlags(SAMEHINT | NOHASH | PRODUCER | splitterFlags);
            swarm::enqueue(swarm::splitter, minTs, ef, tdstruct);
        } else {
            constexpr EnqFlags ef =
                    EnqFlags(SAMEHINT | NOHASH | PRODUCER | CANTSPEC);
            swarm::enqueue(swarm::frame_splitter, 42, ef, tdstruct);
        }
    } else {
        // Don't create a splitter task if the ROB offered zero tasks.
        // Unfortunately, even if this coalescer removed only one task, we must
        // wrap it in a splitter. A splitters is guaranteed to be enqueued to
        // the same tile as its coalescer, whereas a normal task might be
        // hint-mapped to a different tile (e.g. due to hint-stealing). A
        // coalescer must never stall, and there is no guarantee that different
        // tile has a free slot to which to enqueue.
        // Frankly, if coalescers are frequently removing single tasks, then
        // there is a more important problem to solve in terms of when a
        // coalescer is launched.
        //
        // N.B. splitter_impl takes care of deleting the empty tasks array.
        // TODO Don't even bother removing an untied task if it is the last one
        // remaining in the ROB.
        splitter_impl<isFrame>(minTs, tdstruct);
    }
}

// coalescer is enqueued naked (i.e., without a bareRunner)
__attribute__((noinline))
inline void coalescer(swarm::Timestamp ts, const uint32_t n) {
    coalescer_impl<false>(ts, n);
}
__attribute__((noinline))
inline void frame_coalescer(swarm::Timestamp ts, const uint32_t n) {
    coalescer_impl<true>(ts, n);
}


} // end namespace swarm
