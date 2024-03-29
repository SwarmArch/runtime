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

#pragma once

#define SIM_MAX_ENQUEUE_REGS (5)

// These flags must be OR-able and must leave the lowest 4 bits untouched, as
// they are used to pass the number of args
// With NOHINT or SAMEHINT, the hint arg is not passed to the sim
// With SAMETASK, taskPtr is not passed to the sim
// With NOTIMESTMAP or SAMETIME, ts arg is not passed to the sim
// NOTE (dsm):
enum EnqFlags {
    NOFLAGS      = 0,

    // Flags up to (1 << 15) can be task properties preserved during spilling.
    NOHASH        = 1 << 4,  // use modulo indexing on hint when mapping to a tile, not a hash
    PRODUCER      = 1 << 5,  // deprioritize w/r/t same-TS tasks, to avoid flooding task queues
    MAYSPEC       = 1 << 6,  // may run non-speculatively if GVT matches TS
    CANTSPEC      = 1 << 7,  // only run non-speculatively, and only when GVT matches TS
    ISSOFTPRIO    = 1 << 8,  // DEPRECATED
    NOTIMESTAMP   = 1 << 9,  // lacks a TS and does not participate in the GVT protocol.
    REQUEUER      = 1 << 10, // only run non-speculatively, and can run regardless of GVT value
    NONSERIALHINT = 1 << 11, // may run in parallel with same-hint tasks

    // Flags (1 << 16) and beyond must be discarded when the task is spilled.
    // See MAGIC_OP_TASK_REMOVE_UNTIED implementation in sim for explanations.
    NOHINT        = 1 << 16, // ignore the spatial hint
    SAMEHINT      = 1 << 17, // queue with same hint as my task's (--> same tile)
    SAMETASK      = 1 << 18, // queue with same taskPtr as my task's (avoids clobbering taskPtr reg & saves one move)
    SAMETIME      = 1 << 19, // THIS FLAG IS DEPRECATED. queue with same timestamp as my task's
    YIELDIFFULL   = 1 << 20, // if this TSB is full, requeue the parent task and yield the core
    PARENTDOMAIN  = 1 << 21, // queue to parent domain
    SUBDOMAIN     = 1 << 22, // queue to the domain created by the current task.
    SUPERDOMAIN   = 1 << 23, // queue to the immediate enclosing domain
    RUNONABORT    = 1 << 24, // this task runs when the parent is aborted, and is discarded if the parent commits. Can only be enqueued from priv mode, and is a NOP when irrevocable
};

#ifdef __cplusplus
constexpr EnqFlags operator|(EnqFlags a, EnqFlags b) {
    return static_cast<EnqFlags>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
}
#endif

