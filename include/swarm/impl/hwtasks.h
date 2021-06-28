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

#ifndef PLS_APP_MAX_ARGS
#define PLS_APP_MAX_ARGS SIM_MAX_ENQUEUE_REGS
#endif

#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <tuple>
#include <cstdlib>

#include "callfunc.h"
// TODO(mcj) likely hooks should go in this directory. As a first step of
// restructuring the Swarm api directory, I didn't want to change the benchmarks
// that #include hooks.h directly
#include "../hooks.h"
#include "types.h"

/* dsm: This code uses advanced C++11 templates to do a number of things
 * efficiently. Before modifying this code, make sure you understand:
 * - Variadic templates
 * - The unpacking operator (...)
 * - Template argument substitution
 * - Overload rules
 * - SFINAE and enable_if
 * - C++ casts (esp. static_cast vs reinterpret_cast)
 * - constexpr
 */

namespace swarm {

/* enqueue_task magic ops */

// Param passing order: ts, args..., taskPtr, hint (and op in rcx)
// Regs used: rdi, rsi, rdx, r8, r9, r10, r11, r12 [up to 5 args]
//
// Not all args need to be used; max is controlled by SIM_MAX_ENQUEUE_REGS macro
// We could have more HW registers, but more would require some trickery in the
// runner functions (the ABI only permits 6 registers)
//
// Reg passing order is now carefully chosen to mimic the task's call signature as much as possible.
// This reduces overheads for tasks that do a small operation and immediately enqueue a similar task.
// In addition, taskPtr, ts, and hint are *optional* to save instructions when
// (a) taskPtr is the same as ours,
// (b) there's no timestamp,
// (c) ts is the same as ours,
// (d) there's no spatial hint, or
// (e) the spatial hint should be the same as ours
static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2,
                                         uint64_t v3, uint64_t v4, uint64_t v5, uint64_t v6, uint64_t v7) {
    // no prev barrier needed, compiler can hoist up
    register uint64_t r8  asm("r8")  = v3;
    register uint64_t r9  asm("r9")  = v4;
    register uint64_t r10 asm("r10") = v5;
    register uint64_t r11 asm("r11") = v6;
    register uint64_t r12 asm("r12") = v7;
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2), "r"(r8), "r"(r9), "r"(r10), "r"(r11), "r"(r12)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2,
                                         uint64_t v3, uint64_t v4, uint64_t v5, uint64_t v6) {
    register uint64_t r8  asm("r8")  = v3;
    register uint64_t r9  asm("r9")  = v4;
    register uint64_t r10 asm("r10") = v5;
    register uint64_t r11 asm("r11") = v6;
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2), "r"(r8), "r"(r9), "r"(r10), "r"(r11)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2,
                                         uint64_t v3, uint64_t v4, uint64_t v5) {
    register uint64_t r8  asm("r8")  = v3;
    register uint64_t r9  asm("r9")  = v4;
    register uint64_t r10 asm("r10") = v5;
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2), "r"(r8), "r"(r9), "r"(r10)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2,
                                         uint64_t v3, uint64_t v4) {
    register uint64_t r8  asm("r8")  = v3;
    register uint64_t r9  asm("r9")  = v4;
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2), "r"(r8), "r"(r9)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2,
                                         uint64_t v3) {
    register uint64_t r8  asm("r8")  = v3;
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2), "r"(r8)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1, uint64_t v2) {
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1), "d"(v2)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0, uint64_t v1) {
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0), "S"(v1)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op, uint64_t v0) {
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op), "D"(v0)
            :);
    COMPILER_BARRIER();
}

static inline void __enqueue_task_helper(uint64_t op) {
    __asm__ __volatile__("xchg %%rcx, %%rcx;" :
            : "c"(op)
            :);
    COMPILER_BARRIER();
}

/* Catch-all templated helper; the compiler needs this because it expands
 * functions with >= SIM_MAX_ENQUEUE_REGS, though it never emits their code
 * (overload rules means this is matched against last; keep this after all
 *  other defs!)
 */
template<typename... Args>
static inline void __enqueue_task_helper(uint64_t, uint64_t, Args...) {
    // if this fails, most likely you've increased SIM_MAX_ENQUEUE_REGS but have not defined additional __enqueue_task_helpers
    std::abort();
}

/* Skip unnecessary arguments (ts, taskPtr, hint) if possible.
 * Force inline this function that looks complicated to the compiler,
 * but simplifies considerably once inlined
 */
template<typename... Args>
__attribute__((always_inline))
static inline void __enqueue_task_skipargs(uint64_t, uint64_t, uint64_t,
                                           uint64_t, Args...);

template<typename... Args>
static inline void __enqueue_task_skipargs(uint64_t op, uint64_t taskPtr,
        uint64_t ts, uint64_t hint, Args... args) {
    bool skipTask = (op & EnqFlags::SAMETASK);
    bool skipHint = (op & EnqFlags::SAMEHINT) || (op & EnqFlags::NOHINT);
    bool skipTs = (op & EnqFlags::NOTIMESTAMP) || (op & EnqFlags::SAMETIME) ||
                  (op & EnqFlags::RUNONABORT);

    if (!skipTs && !skipTask && !skipHint) {
        __enqueue_task_helper(op, ts, args..., taskPtr, hint);
    } else if (!skipTs && !skipTask && skipHint) {
        __enqueue_task_helper(op, ts, args..., taskPtr);
    } else if (!skipTs && skipTask && !skipHint) {
        __enqueue_task_helper(op, ts, args..., hint);
    } else if (!skipTs && skipTask && skipHint) {
        __enqueue_task_helper(op, ts, args...);
    } else if (skipTs && !skipTask && !skipHint) {
        __enqueue_task_helper(op, args..., taskPtr, hint);
    } else if (skipTs && !skipTask && skipHint) {
        __enqueue_task_helper(op, args..., taskPtr);
    } else if (skipTs && skipTask && !skipHint) {
        __enqueue_task_helper(op, args..., hint);
    } else if (skipTs && skipTask && skipHint) {
        __enqueue_task_helper(op, args...);
    } else {
      std::abort();
    }
}


/* Casting and uncasting of arbitrary arguments into uint64_t
 *
 * These use SFINAE rules and enable_if to provide non-overlapping overloads
 * that are as efficient as possible, and compile.
 *
 * Specifically, the most general way to do a bit-compatible copy for any data
 * type with <= 64 bits is:
 *
 * uint64_t a = *reinterpret_cast<uint64_t*>(&t); -> T t = *reinterpret_cast<T*>(&a);
 * (or using unions, which we now do, as they do not break strict aliasing rules)
 *
 * However, for integers < 64 bits this leads to reads and writes to the stack.
 * Instead, static_cast performs safe conversions and emits no instructions. So
 * we do more efficient specializations for integral types through static_cast
 * and for refs/pointers through direct reinterpret_casts.
 */

// Must have full coverage across all conditions
#define __PLS_STATIC_CAST_COND(T) (std::is_integral<T>::value || std::is_enum<T>::value)
#define __PLS_REINTERPRET_CAST_COND(T) (std::is_pointer<T>::value || std::is_reference<T>::value)
#define __PLS_GENERAL_CAST_COND(T) (!__PLS_STATIC_CAST_COND(T) && !__PLS_REINTERPRET_CAST_COND(T))

// T --> uint64_t
template <typename T, typename std::enable_if<__PLS_STATIC_CAST_COND(T)>::type* = nullptr>
inline uint64_t castArg(T t) {
    return static_cast<uint64_t>(t);
}

template <typename T, typename std::enable_if<__PLS_REINTERPRET_CAST_COND(T)>::type* = nullptr>
inline uint64_t castArg(T t) {
    return reinterpret_cast<uint64_t>(t);
}

//dsm: For whatever reason, gcc warns of unitialized values with lambdas... probably they don't get initialized?
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
template <typename T, typename std::enable_if<__PLS_GENERAL_CAST_COND(T)>::type* = nullptr>
inline uint64_t castArg(T t) {
    union U {
        uint64_t res;
        T t;
        U(T _t) : t(_t) {}
    };
    U u(t);
    return u.res;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// uint64_t --> T
template <typename T, typename std::enable_if<__PLS_STATIC_CAST_COND(T)>::type* = nullptr>
inline /*T*/ uint64_t uncastArg(uint64_t a) {
    //return static_cast<T>(a);
    /* dsm: For integral types, uncasts to < 64-bit types cause unnecessary
     * movs (e.g., uint32 -> mov %esi,%esi). However, casts are free. The
     * reason is that, when it uncasts, the compiler does not know that the 32
     * highest bits are zero. But because these come from casts, we know they
     * actually do. So, just return the casted uint64_t. If the actual arg is
     * 64 bits, uncasts are free, and this should be safe. If the actual arg is
     * an unsigned < 64-bit number, this might still incur some movs later on.
     */
    return a;
}

template <typename T, typename std::enable_if<__PLS_REINTERPRET_CAST_COND(T)>::type* = nullptr>
inline T uncastArg(uint64_t a) {
    return reinterpret_cast<T>(a);
}

template <typename T, typename std::enable_if<__PLS_GENERAL_CAST_COND(T)>::type* = nullptr>
inline T uncastArg(uint64_t a) {
    union U {
        T res;
        uint64_t aa;
        U(uint64_t _a) : aa(_a) {}
    };
    U u(a);
    return u.res;
}

// Avoid erroneous behavior with values > 64 bits
// int i is a dummy arg, because I need something to avoid mixing non-template
// and template overloads...
template <int i>
constexpr bool canCastArgs() {
    return true;
}

template<int i, typename T, typename... Args>
constexpr bool canCastArgs() {
    return (sizeof(T) > sizeof(uint64_t))? false : canCastArgs<i, Args...>();
}

/* Tuple-based calls */

/* Bare runners: Simply undo casts. These often just compile to the function
 * itself, however, we need them in case we pass an argument that is
 * bit-compatible with an uint64_t but for which the ABI specifies a different
 * passing convention (e.g., floating-point args or small structs)
 */
template<typename F, F* f>
inline void bareRunner(Timestamp ts) {
    (*f)(ts);
}

template<typename F, F* f, typename T>
inline void bareRunner(Timestamp ts, uint64_t arg0) {
    (*f)(ts, uncastArg<T>(arg0));
}

template<typename F, F* f, typename T, typename U>
inline void bareRunner(Timestamp ts, uint64_t arg0, uint64_t arg1) {
    (*f)(ts, uncastArg<T>(arg0), uncastArg<U>(arg1));
}

template<typename F, F* f, typename T, typename U, typename V>
inline void bareRunner(Timestamp ts, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    (*f)(ts, uncastArg<T>(arg0), uncastArg<U>(arg1), uncastArg<V>(arg2));
}

template<typename F, F* f, typename T, typename U, typename V, typename X>
inline void bareRunner(Timestamp ts, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (*f)(ts, uncastArg<T>(arg0), uncastArg<U>(arg1), uncastArg<V>(arg2), uncastArg<X>(arg3));
}

template<typename F, F* f, typename T, typename U, typename V, typename X, typename Y>
inline void bareRunner(Timestamp ts, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (*f)(ts, uncastArg<T>(arg0), uncastArg<U>(arg1), uncastArg<V>(arg2), uncastArg<X>(arg3), uncastArg<Y>(arg4));
}

// Catch-all template; matches everything with args > SIM_MAX_ENQUEUE_REGS, which bareRunner cannot handle
template<typename F, F* f, typename T, typename U, typename V, typename X, typename Y, typename Z, typename... Args>
inline void bareRunner() {
    std::abort();
}

/* Tuple-based runners */

template<typename F, F* f, typename... Args>
inline void regTupleRunner(Timestamp ts, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
    // gcc's pointer analysis has enough info to avoid memory reads and writes
    // completely on aligned fields, and just use the t vars in callFunc below.
    // Very impressive!

    constexpr size_t targs = (sizeof(std::tuple<Args...>)-1)/8 + 1;

    // Use aliasing through union
    // Avoid default constructors b/c some objects, like lambdas, have deleted default constructors
    // This does *not* break strict-aliasing rules, and is *more* efficient than
    // the old PaddedTuple and type-punned implementations :)
    union U {
        uint64_t tp[targs];
        std::tuple<Args...> tup;
        U() { /* avoid default constructors */ }
    };

    U u;

    // TODO: There's probably a more elegant way of doing this with unpacking,
    // but this is efficiently compiled
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    u.tp[0] = t0;
    if (targs > 1) u.tp[1] = t1;
    if (targs > 2) u.tp[2] = t2;
    if (targs > 3) u.tp[3] = t3;
    if (targs > 4) u.tp[4] = t4;
#ifdef __clang__
#pragma GCC diagnostic pop
#endif

    callFunc(f, ts, u.tup, typename gens<sizeof...(Args)>::type());
}


template<typename F, F* f, typename... Args>
inline void memTupleRunner(Timestamp ts, uint64_t t0) {
    auto tup = reinterpret_cast<std::tuple<Args...>*>(t0);
    callFunc(f, ts, *tup, typename gens<sizeof...(Args)>::type());
    delete tup;
}

/* Enqueue functions */

constexpr uint64_t enqueueMagicOp(uint64_t numArgs, EnqFlags hintFlags) {
    return (MAGIC_OP_TASK_ENQUEUE_BEGIN + numArgs) | static_cast<uint64_t>(hintFlags);
}

/* Force inline this function that looks complicated to the compiler,
 * but simplifies considerably once inlined
 */
template <typename F, F* f, typename... Args>
__attribute__((always_inline))
static inline void __enqueueHwTask(Timestamp ts, Hint hint, Args... args);

template <typename F, F* f, typename... Args>
static inline void __enqueueHwTask(Timestamp ts, Hint hint, Args... args) {
    if (false) {
        // Check it's a well-typed call
        // If you see a compiler error here, you're using the wrong args...
        f(ts, args...);
    }

    size_t nargs = sizeof...(args);
    if (canCastArgs<0, Args...>()
            && nargs <= SIM_MAX_ENQUEUE_REGS
            && nargs <= PLS_APP_MAX_ARGS) {
        // Safe to just use regs!
        uint64_t magicOp = enqueueMagicOp(nargs, hint.flags);
        uintptr_t fp = reinterpret_cast<uintptr_t>(bareRunner<F, f, Args...>);
        __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, castArg(args)...);
    } else {
        constexpr size_t targs = (sizeof(std::tuple<Args...>)-1)/8 + 1;
        static_assert(targs != 0, "");
        static_assert(targs <= PLS_APP_MAX_ARGS || targs > SIM_MAX_ENQUEUE_REGS,
            "The app's task argument list exceeds PLS_APP_MAX_ARGS");

        if (targs <= SIM_MAX_ENQUEUE_REGS) {
            union U {
                uint64_t tp[targs];
                std::tuple<Args...> tup;
                U(std::tuple<Args...> t) : tup(t) {}
            };

            U u(std::make_tuple(args...));

            uint64_t magicOp = enqueueMagicOp(targs, hint.flags);
            uintptr_t fp = reinterpret_cast<uintptr_t>(regTupleRunner<F, f, Args...>);

            // TODO: Unpacking / gens trick could do this in one call
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
            if (targs == 1) {
                __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, u.tp[0]);
            } else if (targs == 2) {
                __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, u.tp[0], u.tp[1]);
            } else if (targs == 3) {
                __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, u.tp[0], u.tp[1], u.tp[2]);
            } else if (targs == 4) {
                __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, u.tp[0], u.tp[1], u.tp[2], u.tp[3]);
            } else if (targs == 5) {
                __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, u.tp[0], u.tp[1], u.tp[2], u.tp[3], u.tp[4]);
            } else {
                std::abort(); // if this fails, check SIM_MAX_ENQUEUE_REGS...
            }
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
        } else {
            auto tup = new std::tuple<Args...>(args...);
            uint64_t magicOp = enqueueMagicOp(1, hint.flags);
            uintptr_t fp = reinterpret_cast<uintptr_t>(memTupleRunner<F, f, Args...>);
            __enqueue_task_skipargs(magicOp, fp, ts, hint.hint, reinterpret_cast<uintptr_t>(tup));
        }
    }
}

}
