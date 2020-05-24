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

// This file is named to imitate the C++ <algorithm> header. Time will tell
// whether that name is appropriate.

#pragma once

#include <algorithm>
#include <boost/iterator/counting_iterator.hpp>
#include <stdint.h>
#include "api.h"
#include "impl/limits.h"

namespace swarm {

typedef boost::counting_iterator<uint64_t> u64it;
typedef boost::counting_iterator<uint32_t> u32it;
typedef boost::counting_iterator<int64_t> i64it;
typedef boost::counting_iterator<int32_t> i32it;

// The enqueue_all* variants below are Swarm-parallel versions of std::for_each.
// They apply their third argument, a function object, to each element in the
// range [first,last) given by their first two arguments.  The third argument
// is typically a lambda that enqueues (at most) one child task.  If the range
// is small (<= MaxBaseEnqs), enqueue_all* directly runs all the calls to the
// enqueue lambda serially.  If the range is large (> MaxBaseEnqs),
// enqueue_all* spawns enqueuer tasks that may run the calls to the enqueue
// lambda in parallel.
//
// The fourth argument to enqueue_all* specifies the timestamp of enqueuers.
// It may be either an integer timestamp value, or a unary function object that
// returns an integer timestamp when applied to any element in the range.

// This uses a *complete* n-arry tree of enqueuer tasks.  That is, each
// enqueuer recursively enqueues n children enqueuers except for in the last
// level of recursive enqueues, which may not be full.
template <EnqFlags Flags = EnqFlags::NOHINT,
          uint32_t MaxBaseEnqs = swarm::max_children,
          typename Iterator,
          typename EnqOneFn,
          typename TimestampType>
static inline void enqueue_all(
        Iterator first, Iterator last,
        EnqOneFn enq, // The lambda that enqueues a task using the iterator
        // This takes one of two types: a swarm::Timestamp when all
        // internal enqueuers have equal timestamp, or a lambda to
        // derive a timestamp from an iterator, for an enqueuer. The
        // latter assumes tasks are enqueued in increasing TS order
        // using the given iterator. For random TS order, use
        // enqueue_all with a fixed __enqueuer's timestamp.
        TimestampType tsfn);

// An enqueue_all that uses bounded parallel strands, not a complete tree.
// It expects a timestamp lambda and hint lambda for __enqueuers
template <uint32_t MaxBaseEnqs = swarm::max_children,
          uint32_t EnqueuesPerTask = 4,
          uint32_t MaxStrands = UINT32_MAX,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
static inline void enqueue_all(Iterator first, Iterator last,
        EnqueueLambda, TimestampLambda, HintLambda);

// An enqueue_all variant that starts enqueuing real tasks immediately,
// and gradually expands into a tree of parallel strands.
template <uint32_t MaxBaseEnqs = swarm::max_children,
          uint32_t EnqueuesPerTask = 4,
          uint32_t MaxStrands = UINT32_MAX,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
static inline void enqueue_all_progressive(Iterator first, Iterator last,
        EnqueueLambda, TimestampLambda, HintLambda);



template <EnqFlags Flags, class InputIt, class OutputIt>
static inline void copy(InputIt first, InputIt last,
                        OutputIt d_first, Timestamp);

template <EnqFlags Flags = EnqFlags::NOHINT, class Iterator, typename T>
static inline void fill(Iterator first, Iterator last, const T& v, Timestamp);



static constexpr uint32_t ilog2(uint64_t i) { return 63 - __builtin_clzl(i); }

}

#include "impl/enqueue_all.h"
#include "impl/copy.h"
#include "impl/fill.h"
