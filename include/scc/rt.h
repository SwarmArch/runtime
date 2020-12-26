/*===-- scc/rt.h - SCCRT Interface --------------------------------*- C -*-===*\
|*                                                                            *|
|*                       The SCC Parallelizing Compiler                       *|
|*                                                                            *|
|*          Copyright (c) 2020 Massachusetts Institute of Technology          *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* A runtime library interface used by the compiler.                          *|
|*                                                                            *|
|* This is a runtime library in the strict sence that programmers should      *|
|* never call anything declared in this file.  It is only the compiler        *|
|* that inserts calls to these functions into the application code.           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef __PLS_SCC_RT_H__
#define __PLS_SCC_RT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Launch progressive enqueuers for parallelized loops with unknown trip counts.
void __sccrt_enqueue_progressive_64(void (*iterTask)(uint64_t, uint32_t*, void*),
                                    uint32_t* done,
                                    void* closure);
void __sccrt_enqueue_progressive_32(void (*iterTask)(uint32_t, uint32_t*, void*),
                                    uint32_t* done,
                                    void* closure);

void __sccrt_memset(void* dest, uint8_t ch, uint64_t count);
void __sccrt_memcpy(void* dest, const void* src, uint64_t count);
void __sccrt_calloc(uint64_t num, uint64_t size, void* cont);

void __sccrt_log(const char *string);

void __sccrt_log_loop_begin(const char *loop_descriptor);
void __sccrt_log_loop_iter(const char *loop_descriptor);
void __sccrt_log_loop_end(const char *loop_descriptor);

void __sccrt_log_read(const char *access_descriptor, void *address, uint64_t size);
void __sccrt_log_write(const char *access_descriptor, void *address, uint64_t size);
uint64_t __sccrt_log_task_spawn(void);
void __sccrt_log_task_start(const char *task_descriptor, uint64_t dynamic_task_id, uint64_t static_task_id, uint64_t inst_count);

// Parallel reductions via thread-private variables

#define SCCRT_REDUCTION_INIT_DECLARATION(type)                                 \
    void* __sccrt_reduction_##type##_init(type initial, type identity);

#define SCCRT_REDUCTION_DECLARATIONS(type, op)                                 \
    void __sccrt_reduction_##type##_##op(uint64_t ts, void* r, type update);   \
    type __sccrt_reduction_##type##_##op##_collapse(void* r);

SCCRT_REDUCTION_INIT_DECLARATION(uint64_t)
SCCRT_REDUCTION_INIT_DECLARATION(float)
SCCRT_REDUCTION_INIT_DECLARATION(double)

SCCRT_REDUCTION_DECLARATIONS(uint64_t, plus)
SCCRT_REDUCTION_DECLARATIONS(uint64_t, multiplies)
SCCRT_REDUCTION_DECLARATIONS(uint64_t, bit_or)
SCCRT_REDUCTION_DECLARATIONS(uint64_t, bit_and)
SCCRT_REDUCTION_DECLARATIONS(uint64_t, bit_xor)
SCCRT_REDUCTION_DECLARATIONS(float, plus)
SCCRT_REDUCTION_DECLARATIONS(float, multiplies)
SCCRT_REDUCTION_DECLARATIONS(double, plus)
SCCRT_REDUCTION_DECLARATIONS(double, multiplies)

SCCRT_REDUCTION_DECLARATIONS(uint64_t, min)
SCCRT_REDUCTION_DECLARATIONS(uint64_t, max)
SCCRT_REDUCTION_DECLARATIONS(int64_t, min)
SCCRT_REDUCTION_DECLARATIONS(int64_t, max)
SCCRT_REDUCTION_DECLARATIONS(float, min)
SCCRT_REDUCTION_DECLARATIONS(float, max)
SCCRT_REDUCTION_DECLARATIONS(double, min)
SCCRT_REDUCTION_DECLARATIONS(double, max)


// Serial software queuing of tasks
static const unsigned __SCCRT_SERIAL_MAX_ARGS = 5;
void __sccrt_serial_enqueue(void *taskfn, uint64_t ts, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t);
void __sccrt_serial_enqueue_super(void *taskfn, uint64_t ts, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t);
void __sccrt_serial_deepen(void);
void __sccrt_serial_undeepen(void);
void __sccrt_serial_heartbeat(void);
uint64_t __sccrt_serial_get_timestamp(void);
uint64_t __sccrt_serial_get_timestamp_super(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // __PLS_SCC_RT_H__
