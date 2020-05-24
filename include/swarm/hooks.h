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

#include "impl/enqflags.h"

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// Avoid optimizing compilers moving code around this barrier
#define COMPILER_BARRIER() { __asm__ __volatile__("" ::: "memory");}

/* These need to be in sync with the simulator
 *
 * NOTE(dsm): It may be tempting to pack magic ops to use small numbers, since
 * the magic op is typically a load-immediate followed by xchg %rcx,%rcx.
 * However, in x86-64, this is irrelevant, because mov-immediate always uses 32
 * or 64-bit immediates. Therefore, even very small constants take a 5-byte
 * instruction, e.g.:
 *
 *    401856:       bf 01 00 00 00          mov    $0x1,%edi
 *
 * Using small magic op ids may be relevant with a RISC arch, where immediates
 * are ~10-16 bits, but then we'd add new opcodes anyway.
 */
#define MAGIC_OP_ROI_BEGIN          (1025)  // no args
#define MAGIC_OP_ROI_END            (1026)  // no args
#define MAGIC_OP_HEARTBEAT          (1028)  // no args
#define MAGIC_OP_WRITE_STD_OUT      (1029)  // args: char *
#define MAGIC_OP_UPDATE_STACK       (1030)  // args: base pointer, integer size
#define MAGIC_OP_THREADS_AND_STACKS (1031)  // args: integer threads, base pointer, integer size
#define MAGIC_OP_YIELD              (1032)
#define MAGIC_OP_BARRIER            (1033)
#define MAGIC_OP_SERIALIZE          (1034)
#define MAGIC_OP_RDRAND             (1035)
#define MAGIC_OP_SET_GVT            (1036)
#define MAGIC_OP_DEEPEN             (1037)  // args: maxTS of new domain (all higher are enqueued to parent domain)
#define MAGIC_OP_CLEAR_READ_SET     (1038)
#define MAGIC_OP_RECORD_AS_ABORTED  (1039)
#define MAGIC_OP_GET_TIMESTAMP      (1041)
#define MAGIC_OP_GET_TIMESTAMP_SUPER (1042)
#define MAGIC_OP_PRIV_CALL          (1043)
#define MAGIC_OP_PRIV_RET           (1044)
#define MAGIC_OP_PRIV_ISDOOMED      (1045)
#define MAGIC_OP_GET_TID            (1046) // Deprecated in favor of GET_THREAD_ID below
#define MAGIC_OP_ISIRREVOCABLE      (1047)
#define MAGIC_OP_READ_PSEUDOSYSCALL (1048)
#define MAGIC_OP_WRITE_PSEUDOSYSCALL (1049)
#define MAGIC_OP_MALLOC_PARTITION   (1050)
#define MAGIC_OP_UNDEEPEN           (1051)  // no args
#define MAGIC_OP_GET_PARFUNC        (1052)  // Deprecated
#define MAGIC_OP_IN_FF              (1053)  // no args
#define MAGIC_OP_REGISTER_END_HANDLER (1054)  // arg: function pointer
#define MAGIC_OP_GET_THREAD_ID      (1055)  // no args

// args: pointers to finish, abort, and termination handlers
#define MAGIC_OP_TASK_DEQUEUE_SETUP (2048)
// NOTE: Dequeue and finish are custom no-ops; this saves us an instruction

#define MAGIC_OP_TASK_REMOVE_UNTIED         (2049)
#define MAGIC_OP_TASK_REMOVE_OUT_OF_FRAME   (2050)
// args: pointer to coalescer, splitter, and exception addresses
#define MAGIC_OP_TASK_HANDLER_ADDRS         (2051)
#define MAGIC_OP_TASK_FRAMEHANDLER_ADDRS    (2052)

#define MAGIC_OP_ALLOC_BASE         (8192)
#define MAGIC_OP_ALLOC              (MAGIC_OP_ALLOC_BASE + 0)
#define MAGIC_OP_POSIX_MEMALIGN     (MAGIC_OP_ALLOC_BASE + 1)
#define MAGIC_OP_REALLOC            (MAGIC_OP_ALLOC_BASE + 2)
#define MAGIC_OP_FREE               (MAGIC_OP_ALLOC_BASE + 3)
#define MAGIC_OP_MALLOC_USABLE_SIZE (MAGIC_OP_ALLOC_BASE + 4)
#define MAGIC_OP_ZERO_CYCLE_ALLOC   (MAGIC_OP_ALLOC_BASE + 16)
#define MAGIC_OP_ZERO_CYCLE_FREE    (MAGIC_OP_ALLOC_BASE + 17)
#define MAGIC_OP_ZERO_CYCLE_UNTRACKED_ALLOC (MAGIC_OP_ALLOC_BASE + 18)

// enqueue_task calls have the number of register arguments and flags embedded in call (saves an arg)
// args: function pointer, ts, args...
// both _BEGIN and _END need to allow enough bits for flags, defined in enqflags.h
// (currently flags #4 to #29 are allowed)
#define MAGIC_OP_TASK_ENQUEUE_BEGIN (1u << 30)
#define MAGIC_OP_TASK_ENQUEUE_END   (MAGIC_OP_TASK_ENQUEUE_BEGIN << 1)

// Basic defs

static inline void sim_magic_op_0(uint64_t op) {
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%rcx, %%rcx;" : : "c"(op));
    COMPILER_BARRIER();
}

static inline void sim_magic_op_1(uint64_t op, uint64_t arg0) {
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%rcx, %%rcx;" : : "c"(op), "D"(arg0));
    COMPILER_BARRIER();
}

static inline void sim_magic_op_2(uint64_t op, uint64_t arg0, uint64_t arg1) {
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%rcx, %%rcx;" : : "c"(op), "D"(arg0), "S"(arg1));
    COMPILER_BARRIER();
}

static inline void sim_magic_op_3(uint64_t op, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%rcx, %%rcx;" : : "c"(op), "D"(arg0), "S"(arg1), "d"(arg2));
    COMPILER_BARRIER();
}

static inline void sim_magic_op_6(uint64_t op, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    register uint64_t r8  __asm__("r8")  = arg3;
    register uint64_t r9  __asm__("r9")  = arg4;
    register uint64_t r10 __asm__("r10") = arg5;
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%rcx, %%rcx;" : : "c"(op), "D"(arg0), "S"(arg1), "d"(arg2), "r"(r8), "r"(r9), "r"(r10));
    COMPILER_BARRIER();
}

static inline uint64_t sim_magic_op_r0(uint64_t op) {
    uint64_t res;
    COMPILER_BARRIER();
    __asm__ __volatile__(
        "mov %[opcode], %%rcx;" \
        "xchg %%rcx, %%rcx;"
        : "=c"(res)
        : [opcode]"g"(op));
    COMPILER_BARRIER();
    return res;
}

static inline uint64_t sim_magic_op_r1(uint64_t op, uint64_t arg0) {
    uint64_t res;
    COMPILER_BARRIER();
    __asm__ __volatile__(
        "mov %[opcode], %%rcx;" \
        "xchg %%rcx, %%rcx;"
        : "=c"(res)
        : [opcode]"g"(op), "D"(arg0));
    COMPILER_BARRIER();
    return res;
}

static inline uint64_t bitcast_ptr_to_uint64(void* ptr) {
#ifdef __cplusplus
    return reinterpret_cast<uint64_t>(ptr);
#else
    return (uint64_t)ptr;
#endif
}

static inline void* bitcast_uint64_to_ptr(uint64_t ptr) {
#ifdef __cplusplus
    return reinterpret_cast<void*>(ptr);
#else
    return (void*)ptr;
#endif
}

// Op defs (some kept "zsim_XXX" for backwards compatibility)

#define HOOKS_STR  "HOOKS"

static inline void zsim_roi_begin(void) {
    fflush(NULL);
    printf("[" HOOKS_STR "] ROI begin\n");
    fflush(stdout);
    sim_magic_op_0(MAGIC_OP_ROI_BEGIN);
}

static inline void zsim_roi_end(void) {
    sim_magic_op_0(MAGIC_OP_ROI_END);
    fflush(NULL);
    printf("[" HOOKS_STR  "] ROI end\n");
    fflush(stdout);
}

static inline bool sim_in_ff(void) {
    return sim_magic_op_r0(MAGIC_OP_IN_FF);
}

static inline void sim_register_end_handler(void (*handler)(void)) {
    sim_magic_op_1(MAGIC_OP_REGISTER_END_HANDLER,
#ifdef __cplusplus
                   // We cannot use bitcast_ptr_to_uint64 because C++ does not
                   // allow implicitly converting function pointers to void*.
                   reinterpret_cast<uint64_t>(handler)
#else
                   (uint64_t)handler
#endif
                   );

}

static inline void zsim_heartbeat(void) {
    sim_magic_op_0(MAGIC_OP_HEARTBEAT);
}

static inline void sim_stack_base(void* base) {
    sim_magic_op_1(MAGIC_OP_UPDATE_STACK, bitcast_ptr_to_uint64(base));
}

static inline void sim_thread_stacks(uint32_t* pnthreads, void** pbase, uint32_t* plogStackBytes) {
    sim_magic_op_3(MAGIC_OP_THREADS_AND_STACKS,
                   bitcast_ptr_to_uint64(pnthreads),
                   bitcast_ptr_to_uint64(pbase),
                   bitcast_ptr_to_uint64(plogStackBytes));
}

static inline void sim_yield(void) { sim_magic_op_0(MAGIC_OP_YIELD); }
static inline void sim_barrier(void) { sim_magic_op_0(MAGIC_OP_BARRIER); }
static inline void sim_serialize(void) { sim_magic_op_0(MAGIC_OP_SERIALIZE); }

static inline void sim_deepen(uint64_t maxTS) {
    sim_magic_op_1(MAGIC_OP_DEEPEN, maxTS);
}

static inline void sim_undeepen(void) {
    sim_magic_op_0(MAGIC_OP_UNDEEPEN);
}

static inline void sim_rdrand(uint64_t* res) {
    sim_magic_op_1(MAGIC_OP_RDRAND, bitcast_ptr_to_uint64(res));
}

static inline void sim_set_gvt(uint64_t ts) {
    sim_magic_op_1(MAGIC_OP_SET_GVT, ts);
}

static inline void sim_clear_read_set(void) {
    sim_magic_op_0(MAGIC_OP_CLEAR_READ_SET);
}

static inline void sim_record_as_aborted(void) {
    sim_magic_op_0(MAGIC_OP_RECORD_AS_ABORTED);
}

static inline uint64_t sim_get_timestamp(void) {
    return sim_magic_op_r0(MAGIC_OP_GET_TIMESTAMP);
}

static inline uint64_t sim_get_timestamp_super(void) {
    return sim_magic_op_r0(MAGIC_OP_GET_TIMESTAMP_SUPER);
}

static inline void* sim_zero_cycle_malloc(size_t size) {
    void* ptr;
    sim_magic_op_3(MAGIC_OP_ZERO_CYCLE_ALLOC, bitcast_ptr_to_uint64(&ptr),
                   size, 0ul);
    return ptr;
}

static inline void sim_zero_cycle_free(void* ptr) {
    if (ptr) sim_magic_op_1(MAGIC_OP_ZERO_CYCLE_FREE, bitcast_ptr_to_uint64(ptr));
}

static inline void* sim_zero_cycle_untracked_malloc(size_t size) {
    void* ptr;
    sim_magic_op_3(MAGIC_OP_ZERO_CYCLE_UNTRACKED_ALLOC, bitcast_ptr_to_uint64(&ptr),
                   size, 0ul);
    return ptr;
}

static inline void sim_priv_call(void) {
    sim_magic_op_0(MAGIC_OP_PRIV_CALL);
}

static inline void sim_priv_ret(void) {
    sim_magic_op_0(MAGIC_OP_PRIV_RET);
}

static inline bool sim_priv_isdoomed(void) {
    return sim_magic_op_r0(MAGIC_OP_PRIV_ISDOOMED);
}

static inline bool sim_isirrevocable(void) {
    return sim_magic_op_r0(MAGIC_OP_ISIRREVOCABLE);
}

static inline uint64_t sim_get_tid(void) {
    return sim_magic_op_r0(MAGIC_OP_GET_THREAD_ID);
}

static inline void sim_read_pseudosyscall(size_t bytes) {
    sim_magic_op_1(MAGIC_OP_READ_PSEUDOSYSCALL, bytes);
}

static inline void sim_write_pseudosyscall(size_t bytes) {
    sim_magic_op_1(MAGIC_OP_WRITE_PSEUDOSYSCALL, bytes);
}

static inline void sim_task_dequeue_setup(void* taskFinishPc, void* taskAbortPc, void* donePc) {
    sim_magic_op_3(MAGIC_OP_TASK_DEQUEUE_SETUP,
                   bitcast_ptr_to_uint64(taskFinishPc),
                   bitcast_ptr_to_uint64(taskAbortPc),
                   bitcast_ptr_to_uint64(donePc));
}

static inline void sim_malloc_partition(void* startAddress, void* endAddress,
                                        uint64_t partID) {
    sim_magic_op_3(MAGIC_OP_MALLOC_PARTITION,
                   bitcast_ptr_to_uint64(startAddress),
                   bitcast_ptr_to_uint64(endAddress), partID);
}

static inline void* sim_get_parfunc(void* fPtr) {
    uint64_t res = sim_magic_op_r1(MAGIC_OP_GET_PARFUNC, bitcast_ptr_to_uint64(fPtr));
    return bitcast_uint64_to_ptr(res);
}

static inline void sim_task_dequeue_runloop(void) {
    // Combines sim_task_dequeue_setup and task_dequeue; doing this with C++
    // labels produces incorrect behavior...
    // NOTE: the local labels inside this assembly are left to the linker to
    // resolve as relocations of type R_X86_64_32S, which is incompatible with
    // -pie, so you must link your benchmarks with -no-pie.
   __asm__ __volatile__(
            "   push %%rbp\n\t" \
            "   mov $1f, %%rsi\n\t" \
            "   mov $1f, %%rdi\n\t" \
            "   mov $2f,    %%rdx\n\t" \
            "   xchg %%rcx, %%rcx\n\t" \
            " 1:\n\t" \
            "   xchg %%rdx, %%rdx\n\t" \
            " 2:\n\t" \
            "   pop %%rbp\n\t"
            :
            : "c"(MAGIC_OP_TASK_DEQUEUE_SETUP)
            /* Clobber everything except rsp (stack pointer) and rbp (base pointer)
             * sim restores rsp; asm above saves & restores rbp; rbp is caller-saved
             * anyway, and methods that use rbp start with push rbp + mov rsp, rbp
             * so it should be OK if some task rewrites rbp. Clobbering rbp does not
             * work without -fomit-frame-pointer...
             */
            : "rax", "rbx", "rdx", "rsi", "rdi",
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
            "flags", "memory"
            );
    return;
}
