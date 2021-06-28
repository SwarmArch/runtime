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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>

#include "swarm/hooks.h"

void* malloc(size_t size) {
    if (size > 0) {
        void* ptr;
        sim_magic_op_2(MAGIC_OP_ALLOC, (uint64_t)(&ptr), size);
        return ptr;
    } else {
        return NULL;
    }
}

void* calloc(size_t nmemb, size_t size) {
    const size_t total = nmemb * size;
    void* ptr;
    sim_magic_op_2(MAGIC_OP_ALLOC, (uint64_t)(&ptr), total);
    // NOTE: sim returns pre-zeroed mem, so you can comment out this memset,
    // but apps should avoid calloc-ing lots of memory in parallel tasks.
    memset(ptr, 0, total);
    return ptr;
}

/**
 * N.B. we're still missing some features of realloc, as defined by
 * http://linux.die.net/man/3/malloc
 */
void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    } else if (size == 0) {
        free(ptr);
        return NULL;
    } else {
        size_t oldSize = malloc_usable_size(ptr);
        if (oldSize >= size) {
            return ptr;
        } else {  // oldSize < size
            void* newPtr = malloc(size);
            memcpy(newPtr, ptr, oldSize);
            free(ptr);
            return newPtr;
        }
    }
}

void free(void* ptr) {
    if (ptr) sim_magic_op_1(MAGIC_OP_FREE, (uint64_t)(ptr));
}

void cfree(void* ptr) { free(ptr); }

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    // NOTE: On failure, posix_memalign doesn't modify memptr
    if (size == 0) {
        *memptr = NULL;
    } else if (!alignment || (alignment & (alignment - 1))
               || (alignment % sizeof(void*))) {
        return EINVAL;
    } else {
        void* ptr;
        sim_magic_op_3(MAGIC_OP_POSIX_MEMALIGN,
                       (uint64_t)(&ptr),
                       alignment, size);
        if (ptr == NULL) return ENOMEM;
        *memptr = ptr;
    }
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
    void* ptr;
    int dc = posix_memalign(&ptr, alignment, size);
    if (dc != 0) ptr = NULL;
    return ptr;
}

void* memalign(size_t alignment, size_t size) {
    return aligned_alloc(alignment, size);
}

// The version of <string.h> header we have installed declares
// the argument of strdup to be non-null. Perhaps we should
// follow the standard and assert/assume src is non-null?
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wtautological-pointer-compare"
#elif defined(__GNUC__) && (__GNUC__ >= 6)
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
char* strdup(const char* src) {
    if (src == NULL) return NULL;
    size_t len = strlen(src);
    char* dst = (char*)malloc(len);
    memcpy(dst, src, len);
    return dst;
}
#pragma GCC diagnostic pop

size_t malloc_usable_size(void* ptr) {
    size_t usableSize;
    sim_magic_op_2(MAGIC_OP_MALLOC_USABLE_SIZE, (uint64_t)(&usableSize), (uint64_t)(ptr));
    return usableSize;
}

/* Unimplemented functions below. Programs rarely use these, so rather than
 * implementing the library in full, we do these on demand */

static void abort_unimplemented(const char* fn) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "Aborting: sim-alloc function unimplemented: %s", fn);
    sim_magic_op_1(MAGIC_OP_WRITE_STD_OUT, (uint64_t)(&buf[0]));
    abort();
}

void* valloc(size_t size) {
    abort_unimplemented(__FUNCTION__);
    return NULL;
}

void* pvalloc(size_t size) {
    abort_unimplemented(__FUNCTION__);
    return NULL;
}

void* malloc_get_state(void) {
    abort_unimplemented(__FUNCTION__);
    return NULL;
}

int malloc_set_state(void* state) {
    abort_unimplemented(__FUNCTION__);
    return -1;
}

int malloc_info(int options, FILE* stream) {
    abort_unimplemented(__FUNCTION__);
    return -1;
}

void malloc_stats(void) {
    abort_unimplemented(__FUNCTION__);
}

int malloc_trim(size_t pad) {
    abort_unimplemented(__FUNCTION__);
    return -1;
}

// http://www.gnu.org/software/libc/manual/html_node/Hooks-for-Malloc.html
// __malloc_hook
// __malloc_initialize_hook
// __free_hook
