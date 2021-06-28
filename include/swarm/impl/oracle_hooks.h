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

#include <stdint.h>
#include <stdio.h>

#include "../hooks.h"

//These need to be in sync with the simulator
// [mcj] Why does this file not perfectly match the Oracle's hooks file? If
// we're going to make file synchronization difficult, we don't need to make it
// so difficult where the following doesn't even work.
//  cp /path/to/oracle/include/hooks.h
//     /path/to/benchmarks/include/pls/impl/oracle_hooks.h
// FIXME(mcj) extract all the sim_blah() functions to a separate file
#define SIM_TASK_BEGIN         (0x3000)
#define SIM_TASK_END           (0x3001)
#define SIM_TASK_CREATE        (0x3002)
#define SIM_CHANGE_RECORDING   (0x3003)
#define SIM_DEBUG_IS_STACKADDR (0x3010)
#define SIM_TASK_SET_LOCALE    (0x3011)
#define SIM_SET_STACK          (0x3012)
#define SIM_SET_SKIP_RANGE     (0x3013)
#define SIM_TASK_NUM_ARGS      (0x3014)
#define SIM_TASK_DEEPEN        (0x3015)
#define SIM_TASK_UNDEEPEN      (0x3016)
#define SIM_TASK_SET_GVT       (0x3017)


#define START_BBL() __asm__ __volatile__("jmp 1f\n\t1:\n\t" :::)

static inline void sim_task_begin(uint64_t taskId) {
    COMPILER_BARRIER();
    sim_magic_op_1(SIM_TASK_BEGIN, taskId);
    COMPILER_BARRIER();
    START_BBL();
    COMPILER_BARRIER();
}

static inline void sim_task_end(uint64_t taskId) {
    COMPILER_BARRIER();
    sim_magic_op_1(SIM_TASK_END, taskId);
    COMPILER_BARRIER();
    START_BBL();
    COMPILER_BARRIER();
}

static inline void sim_task_create(uint64_t parentId, uint64_t childId, uint64_t prio, uint64_t hint, uint64_t hint_flags, uint64_t numArgs) {
    COMPILER_BARRIER();
    sim_magic_op_3(SIM_TASK_CREATE, parentId, childId, prio);
    sim_magic_op_3(SIM_TASK_SET_LOCALE, childId, hint, hint_flags);
    sim_magic_op_1(SIM_TASK_NUM_ARGS, numArgs);
    COMPILER_BARRIER();
}

static inline void sim_stop_recording() {
    COMPILER_BARRIER();
    sim_magic_op_1(SIM_CHANGE_RECORDING, 0);
    // Stop recording addresses *and* instructions after this invocation. Thus
    // the next instruction should be part of a new basic block, so add an
    // artificial jump
    COMPILER_BARRIER();
    START_BBL();
    //__asm__ __volatile__ goto("jmp %l0;" :::: NewBasicBlock1);
    COMPILER_BARRIER();
//NewBasicBlock1:
    //return;
}

static inline void sim_resume_recording() {
    COMPILER_BARRIER();
    sim_magic_op_1(SIM_CHANGE_RECORDING, 1);
    // Resume recording addresses and instruction counts, so the subsequent
    // instruction should be in a new basic block
    COMPILER_BARRIER();
    START_BBL();
    //__asm__ __volatile__ goto("jmp %l0;" :::: NewBasicBlock2);
    COMPILER_BARRIER();
//NewBasicBlock2:
//    return;
}

static inline void sim_assert_is_stackaddr(void* addr) {
    COMPILER_BARRIER();
    sim_magic_op_3(SIM_DEBUG_IS_STACKADDR,
                   reinterpret_cast<uint64_t>(addr), 0, 0);
    COMPILER_BARRIER();
}

static inline void sim_set_stack(void* stack, uint64_t size) {
    COMPILER_BARRIER();
    uint64_t startPtr = (uintptr_t)stack;
    sim_magic_op_3(SIM_SET_STACK, startPtr, startPtr+size, 0);
    COMPILER_BARRIER();
}
