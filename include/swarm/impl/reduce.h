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

#include <algorithm>
#include <cstdint>
#include <numeric>

#include "../api.h"
#include "../algorithm.h"
#include "../aligned.h"
#include "../cps.h"
#include "block.h"


namespace swarm {
namespace _reduce {

template <class Iterator, class T, class BinaryOp,
          class CallBackLambda, class CallBackHintLambda>
struct Reducer {
    static const uint32_t size;

    swarm::Timestamp cbts;
    CallBackLambda cb;
    CallBackHintLambda cbhint;

    BinaryOp o;
    T identity;
    Iterator first;
    Iterator last;
    uint64_t numTasks;
    swarm::aligned<T> intermediates[0];

    Reducer(swarm::Timestamp ts, CallBackLambda _cb, CallBackHintLambda _cbh,
            BinaryOp _o, T i,
            Iterator _first, Iterator _last)
        : cbts(ts)
        , cb(_cb)
        , cbhint(_cbh)
        , o(_o)
        , identity(i)
        , first(_first)
        , last(_last)
        , numTasks(1 + (std::distance(first, last) - 1) / blockSize())
    {}

    inline void operator() (swarm::Timestamp) {
        if (numTasks == 1) {
            T r = std::accumulate(first, last, identity, o);
            swarm::enqueueLambda(cb, cbts, cbhint(*first), r);
            free(this);
        } else {
            swarm::deepen();

            swarm::fill<EnqFlags(NOHINT | MAYSPEC)>(
                    intermediates,
                    intermediates + Reducer::size,
                    identity, 0ul);

            pls_cbegin(1, EnqFlags(NOHINT | MAYSPEC), [this]);

            swarm::enqueue_all<swarm::max_children - 4, swarm::max_children - 1>(
                    swarm::u64it(0ul), swarm::u64it(numTasks - 1),
                    [this] (swarm::Timestamp ts, uint64_t b) {
                // FIXME(mcj) align these to cache-line boundaries
                Iterator begin = this->first + b * blockSize();
                // Iterator end is derived within Reducer::accumulateBlock
                swarm::enqueue(Reducer::accumulateBlock, ts,
                        // FIXME(mcj) MAYSPEC isn't necessarily safe here
                        // We should let the caller provide the flag.
                        {swarm::Hint::cacheLine(&(*begin)), EnqFlags::MAYSPEC},
                        this, begin);
            },
            [] (uint64_t) { return 1; },
            [] (uint64_t) { return EnqFlags(NOHINT | MAYSPEC); });

            Iterator begin = this->first + (numTasks - 1) * blockSize();
            swarm::enqueue(Reducer::accumulate, 1,
                         {swarm::Hint::cacheLine(&(*begin)), EnqFlags::MAYSPEC},
                         this, begin, this->last);

            swarm::enqueue(Reducer::collapse, 2,
                         EnqFlags(NOHINT | CANTSPEC), this);
            pls_cend();
        }
    }


    static inline void accumulate(
            swarm::Timestamp ts,
            Reducer* r,
            Iterator begin,
            Iterator end) {
        T value = std::accumulate(begin, end, r->identity, r->o);
        swarm::enqueue(updateIntermediate,
                     ts, EnqFlags(SAMEHINT | MAYSPEC),
                     r, value);
    }


    static void accumulateBlock(swarm::Timestamp ts, Reducer* r, Iterator begin) {
        accumulate(ts, r, begin, begin + blockSize());
    }


    static void updateIntermediate(swarm::Timestamp, Reducer* r, T value) {
        //swarm::info("tid %ld", swarm::tid());
        auto* intermediate = &r->intermediates[swarm::tid()];
        *intermediate = r->o(*intermediate, value);
    }


    static void collapse(swarm::Timestamp, Reducer* r) {
        auto* begin = &r->intermediates[0];
        auto* end = &r->intermediates[Reducer::size];
        BinaryOp o = r->o;

        // Force 12 in-flight loads to exploits MLP since most loads will miss.
        // The alternative (see the epilog below) compiles the loop such that
        // all loads serialize on an in-order processor, due to dependences on
        // the sole accumulator register.
        constexpr unsigned INFLIGHT = 12;
        T accumulator = *begin;
        auto it = begin + 1;
        while (it < end - INFLIGHT) {
            T v[INFLIGHT];
            v[0] = *(it++);
            v[1] = *(it++);
            v[2] = *(it++);
            v[3] = *(it++);
            v[4] = *(it++);
            v[5] = *(it++);
            v[6] = *(it++);
            v[7] = *(it++);
            v[8] = *(it++);
            v[9] = *(it++);
            v[10] = *(it++);
            v[11] = *(it++);
            COMPILER_BARRIER();
            // Don't bother with a reduction tree on the v's, since
            // each add takes only one cycle, plus gcc ignores it.
            accumulator = std::accumulate(v, v + INFLIGHT, accumulator, o);
        }
        accumulator = std::accumulate(it, end, accumulator, o);
        r->finish(accumulator);
    }


    void finish(T r) {
        // Enqueue up one timestamp interval.
        swarm::Hint h(cbhint(*first));
        swarm::enqueueLambda(cb, cbts,
                {h.hint, EnqFlags(h.flags | PARENTDOMAIN)},
                r);
        free(this);
    }


    static constexpr uint32_t blockSize() {
        // TODO(mcj) process more elements (cache lines) per task by factoring
        // in (last-first)
        constexpr uint32_t epl = swarm::block::elementsPerLine<Iterator>();
        return std::max(epl, 2u);
    }
};


template <class It, class T, class BinaryOp, class CB, class CBH>
const uint32_t Reducer<It, T, BinaryOp, CB, CBH>::size = swarm::num_threads();


} // end namespace _reduce


/**
 * Create a task that performs a parallel reduction into per-thread intermediate
 * variables, then collapses those intermediates and calls your callback.
 *
 * TODO(mcj)
 * - Should the callback appear atomic with the caller of reduce, or are they
 * willing to accept that the callback is created as a new task, using the
 * reduction result?
 */
template <class Iterator, class T, class BinaryOp, class CallBackLambda>
void reduce(Iterator first, Iterator last, T identity, BinaryOp o, Timestamp ts,
            CallBackLambda cb) {
    // TODO(mcj) offer a callback timestamp and callback hint to the API
    auto cbh = [] (typename std::iterator_traits<Iterator>::reference) {
        return swarm::Hint(EnqFlags::NOHINT);
    };

    using Reducer = swarm::_reduce::Reducer<Iterator, T, BinaryOp,
                                          CallBackLambda, decltype(cbh)>;
    void* b = malloc(sizeof(Reducer) + SWARM_CACHE_LINE * Reducer::size);
    auto r = new(b) Reducer(ts, cb, cbh, o, identity, first, last);
    // FIXME(mcj) MAYSPEC isn't necessarily safe. The caller should be able to
    // express it.
    swarm::enqueueLambda(r, ts,
                       {swarm::Hint::cacheLine(&(*first)), EnqFlags::MAYSPEC});
}

} // end namespace swarm
