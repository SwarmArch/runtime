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

#ifndef PLS_API_H_
#define PLS_API_H_
#define FROM_PLS_API

#define pls_unlikely(x) __builtin_expect((x), 0)
#define pls_likely(x)   __builtin_expect((x), 1)

#include <stdint.h>
#include <type_traits>
#include "impl/types.h"
#include "impl/callfunc.h"

namespace swarm {

static inline void run();

static inline uint32_t num_threads();
static inline uint32_t tid();

/* Every runtime must implement this one.
 * Force inline this function that looks complicated to the compiler,
 * but simplifies considerably once inlined
 */
template <typename F, F* f, typename... Args>
__attribute__((always_inline))
static inline void enqueueTask(Timestamp ts, Hint hint, Args... args);

/* Syntactic sugar (pollutes global namespace, but there's no way to do this
 * without a macro prior to C++17, and writing decltype all the time is tedious)
 * http://stackoverflow.com/q/5628121
 *
 * NOTE: If f is a template function with multiple specified template
 * arguments, the caller must enclose it in parentheses.  Generally, the
 * preprocessor will consider the first macro argument to end at the first
 * comma not enclosed in parentheses.
 * TODO(victory): Remove/deprecate any direct use/exposure of enqueueTask.
 * TODO(victory): After moving to C++17, replace this macro with template<auto>.
 */
#define enqueue(f, ...) \
    enqueueTask<std::remove_reference_t<decltype(f)>, f>(__VA_ARGS__)

/* Lambda function interface. Wastes at least an arg, but gcc is typically
 * smart enough to optimize these fairly well (lambda inlining, closure does
 * not take too much space, etc).
 *
 * If you need to call into a member function, just use a lambda with a closure
 * that takes a reference to the pointer.
 */
template<typename L, typename... Args>
__attribute__((always_inline))
static inline void enqueueLambda(L lfunc, Timestamp ts, Hint hint, Args... args) {
    enqueue((callLambdaFunc<L, Timestamp, Args...>), ts, hint, lfunc, args...);
}

template<typename L, typename... Args>
__attribute__((always_inline))
static inline void enqueueLambda(L* plfunc, Timestamp ts, Hint hint, Args... args) {
    enqueue((callLambdaPointer<L, Timestamp, Args...>), ts, hint, plfunc, args...);
}


template <typename... Args>
static inline void info(const char* str, Args... args);

/* If it's not the GVT task, waits until it is (by aborting) */
static inline void serialize();

/* Fractal time: Enter a new time subdomain */
static inline void deepen(uint64_t maxTS = -1ul);
/* Fractal time: Return to the task's original domain */
static inline void undeepen();

/* Lower our own timestamp if we are the GVT task */
static inline void setGvt(Timestamp ts);

/* Clear the read Set of the current task */
static inline void clearReadSet();

/* current Task will be recorded as aborted even if it was committed */
static inline void recordAsAborted();

/* Returns current task's timestamp,
 * or UINT64_MAX if not in a task. */
static inline Timestamp timestamp();
/* Returns current task's domain's creator's timestamp,
 * or UINT64_MAX if in a root-domain task.
 * It is an error to call this if not in a task. */
static inline Timestamp superTimestamp();

}

#if defined(SEQ_RUNTIME)
#include "impl/seq_runtime.h"
#elif defined(SWARM_RUNTIME)
#include "impl/swarm_runtime.h"
#elif defined(TLS_RUNTIME)
#include "impl/tls_runtime.h"
#elif defined(ORACLE_RUNTIME)
#include "impl/oracle_runtime.h"
#elif defined(SCC_RUNTIME) || defined(SCC_SERIAL_RUNTIME)
#include "impl/scc_runtime.h"
#else
#error "Need appropriate runtime"
#endif

#undef FROM_PLS_API
#endif  // PLS_API_H_
