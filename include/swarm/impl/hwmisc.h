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

#ifndef FROM_PLS_API
#error "This file cannot be included directly"
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>
#include <asm/prctl.h> // For ARCH_GET_FS etc
#include <sys/syscall.h>
#include "../hooks.h"
#include "spillers.h"


/* "Hardware" implementation of miscellaneous parts of the runtime. Simulator
 * runtimes (PLS, TLS) should include this file.
 */


namespace swarm {

// [victory] C++17 would allow defining inline variables in this header file:
//inline unsigned long __mainThreadFSAddr = 0;
//inline unsigned long __mainThreadGSAddr = 0;
// But since we want to support older versions of GCC, lets use the
// static-member-of-class-template trick.  See: https://wg21.link/n4424
template <typename T> struct __HWMiscState {
    static unsigned long mainThreadFSAddr;
    static unsigned long mainThreadGSAddr;
};
template <typename T> unsigned long __HWMiscState<T>::mainThreadFSAddr = 0;
template <typename T> unsigned long __HWMiscState<T>::mainThreadGSAddr = 0;
static unsigned long& __mainThreadFSAddr = __HWMiscState<int>::mainThreadFSAddr;
static unsigned long& __mainThreadGSAddr = __HWMiscState<int>::mainThreadGSAddr;

// arch_prctl has hardly any support and is tricky to use. Note that
//     objdump -D /lib/x86_64-linux-gnu/libc.so.6 | grep arch_prctl
// shows a function __arch_prctl, but here are some quotes about this function.
// https://linux.die.net/man/2/arch_prctl
//     "As of version 2.7, glibc provides no prototype for arch_prctl(). You have
//     to declare it yourself for now. This may be fixed in future glibc
//     versions."
// https://stackoverflow.com/a/31745403
//     "It took me some time to realise, that glibc probably does not support
//     arch_prctl at all. So one has to do the coresponding syscall yourself"
// [mcj] Even if I declare arch_prctl or __arch_prctl outside the swarm namespace,
// I observe linker errors without an implementation.
static inline int __arch_prctl(int code, unsigned long addr) {
    return syscall(SYS_arch_prctl, code, addr);
}

// A Swarmified app with sequential semantics (e.g. auto-parallelized or TLS)
// has no need for, nor knowledge of, thread-local storage. Therefore we make
// all thread-local storage refer to global storage. Grab the current FS and GS
// register values of the main thread, and use those addresses for all threads'
// FS/GS registers in pls_worker.
static inline void __record_main_fsgs_addresses() {
    __arch_prctl(ARCH_GET_FS,
                 reinterpret_cast<uintptr_t>(&swarm::__mainThreadFSAddr));
    __arch_prctl(ARCH_GET_GS,
                 reinterpret_cast<uintptr_t>(&swarm::__mainThreadGSAddr));
}

static void report_pthread_stack_base() {
    // From: http://man7.org/linux/man-pages/man3/pthread_attr_init.3.html
    int err;
    pthread_attr_t attr;
    void* stkaddr;
    size_t v;
    // pthread_getattr_np() is a non-standard GNU extension that
    // retrieves the attributes of the thread specified in its
    // first argument
    err = pthread_getattr_np(pthread_self(), &attr);
    if (err != 0) std::abort();
    err = pthread_attr_getstack(&attr, &stkaddr, &v);
    if (err != 0) std::abort();

    sim_stack_base(stkaddr);
}

static void* pls_worker(void* isMainThread) __attribute__((noinline));
static void* pls_worker(void* isMainThread) {
    // Note that using pthreads to find the stack of the master thread yields an
    // incorrect pointer. mcj couldn't get ucontext's getcontext, nor
    // sigaltstack to yield a correct stack pointer either.
    // Finally, note that the frame address approach below is off by a few
    // bytes, but at least it narrowly works
    if (isMainThread) sim_stack_base(__builtin_frame_address(0));
    else report_pthread_stack_base();

    assert((!!swarm::__mainThreadFSAddr) == (!!swarm::__mainThreadFSAddr));
    unsigned long localThreadFSAddr, localThreadGSAddr;
    if (swarm::__mainThreadFSAddr) {
        // Point all threads' thread-local storage to the main thread's global
        // storage. Record this thread's original FS and GS register values,
        // then set the registers to match the main thread's values.
        __arch_prctl(ARCH_GET_FS,
                     reinterpret_cast<uintptr_t>(&localThreadFSAddr));
        __arch_prctl(ARCH_GET_GS,
                     reinterpret_cast<uintptr_t>(&localThreadGSAddr));
        __arch_prctl(ARCH_SET_FS, swarm::__mainThreadFSAddr);
        __arch_prctl(ARCH_SET_GS, swarm::__mainThreadGSAddr);
    }

    sim_barrier();
    if (isMainThread) zsim_roi_begin();
    sim_barrier();
    sim_task_dequeue_runloop();
    sim_barrier();
    if (isMainThread) zsim_roi_end();
    sim_barrier();

    if (swarm::__mainThreadFSAddr) {
        // Restore the thread's FS/GS registers before it exits()
        __arch_prctl(ARCH_SET_FS, localThreadFSAddr);
        __arch_prctl(ARCH_SET_GS, localThreadGSAddr);
    }

    return nullptr;
}

static inline void launch_threads(void* (*workerFn)(void*)) {
    // Per-thread stack size
    uint32_t logStackSize = 0;
    uint32_t nthreads = 0;
    void* stacksBase = nullptr;
    sim_thread_stacks(&nthreads, &stacksBase, &logStackSize);
    assert(nthreads && logStackSize && stacksBase);
    size_t stackSize = (1 << logStackSize);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    pthread_t* pthreads = (pthread_t*) calloc(sizeof(pthread_t), nthreads);

    for (uint32_t t = 1; t < nthreads; t++) {
        void* threadStackBase = reinterpret_cast<void*>(
                reinterpret_cast<uint64_t>(stacksBase) + t*stackSize);
        pthread_attr_setstack(&attr, threadStackBase, stackSize);
        int err = pthread_create(&pthreads[t], &attr, workerFn, nullptr);
        if (err != 0) std::abort();
    }

    // Use ucontexts to switch stacks without creating a new pthread
    // [mcj] allegedly pthreads + ucontexts don't play well together
    // http://stackoverflow.com/a/8170401
    ucontext_t workerContext, returnContext;
    getcontext(&workerContext);
    workerContext.uc_stack.ss_sp = stacksBase;  // thread 0 --> offset 0
    workerContext.uc_stack.ss_size = stackSize;
    workerContext.uc_link = &returnContext;
    makecontext(&workerContext, (void (*)())workerFn, 1, 1 /*signal this is the main thread*/);
    int err = swapcontext(&returnContext, &workerContext);
    assert(!err); (void) err;

    for (uint32_t t = 1; t < nthreads; t++) {
        int err = pthread_join(pthreads[t], nullptr);
        if (err != 0) std::abort();
    }

    pthread_attr_destroy(&attr);
    free(pthreads);
}

static void task_exception_handler() __attribute__((noinline));
static void task_exception_handler() {
    swarm::serialize();
    std::abort();  // if this fires, the task became non-speculative... not good
}

static void setup_task_handlers() {
    // Run a spiller that doesn't delete any tasks
    // 1) to avoid unused function warnings
    // 2) to pre-populate the global offset table with
    //    functions so it isn't aborted.
    //    (e.g. new[], delete[], swarm::info if used)
    swarm::spiller(0, 0);

    uintptr_t requeuer_ptr = reinterpret_cast<uintptr_t>(
            bareRunner<decltype(swarm::requeuer), swarm::requeuer,
                       TaskDescriptors*>);
    uintptr_t frame_requeuer_ptr = reinterpret_cast<uintptr_t>(
            bareRunner<decltype(swarm::frame_requeuer), swarm::frame_requeuer,
                       TaskDescriptors*>);

    sim_magic_op_3(MAGIC_OP_TASK_HANDLER_ADDRS,
                   reinterpret_cast<uint64_t>(&swarm::spiller),
                   requeuer_ptr,
                   reinterpret_cast<uint64_t>(&swarm::task_exception_handler));
    sim_magic_op_2(MAGIC_OP_TASK_FRAMEHANDLER_ADDRS,
                   reinterpret_cast<uint64_t>(&swarm::frame_spiller),
                   frame_requeuer_ptr);
}

#ifdef SCC_RUNTIME
#define __SCC_serial_attr gnu::noinline, scc::noswarmify
#else
#define __SCC_serial_attr
#endif

[[__SCC_serial_attr]] static inline void info(const char* str) {
    sim_magic_op_1(MAGIC_OP_WRITE_STD_OUT, reinterpret_cast<uint64_t>(str));
}

template <typename... Args>
[[__SCC_serial_attr]] static inline void info(const char* str, Args... args) {
    char buf[1024];
    snprintf(buf, sizeof(buf)-1, str, args...);
    swarm::info(buf);
}

static inline uint32_t num_threads() {
    return sim_get_num_threads();
}
static inline uint32_t tid() {
    return sim_get_tid();
}

static inline uint32_t numTiles() {
    return sim_get_num_tiles();
}
static inline uint32_t tileId() {
    return sim_get_tile_id();
}

static inline void serialize() { sim_serialize(); }
static inline void setGvt(Timestamp ts) { sim_set_gvt(ts); }
static inline Timestamp timestamp() { return sim_get_timestamp(); }
static inline Timestamp superTimestamp() { return sim_get_timestamp_super(); }

static inline void deepen(uint64_t maxTS) { sim_deepen(maxTS); }
static inline void undeepen() { sim_undeepen(); }
static inline void clearReadSet() { sim_clear_read_set(); }
static inline void recordAsAborted() { sim_record_as_aborted(); }

static inline void mallocPartition(void* start, void* end,
                                   uint64_t partitionID) {
    sim_malloc_partition(start, end, partitionID);
}

}
