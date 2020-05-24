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

#include <algorithm>
#include <functional>
#include <type_traits>

#include "../api.h"
#include "type_traits.h"

namespace swarm {

//-----------------------------------------------------------------------------
// By default you can use a unary enqueue lamba: e.g.
//    [] (int i) {
//        swarm::enqueue(0ul, ..., functionOf(i));
//    }
//
// Sometimes you want to use the timestamp that was passed to enqueue_all as an
// argument to the lambda, perhaps by capture list.
//
//    [enqueuerTS] (int i) {
//        swarm::enqueue(enqueuerTS, ..., functionOf(i));
//    }
//
// But this wastes a precious task argument. Now you can request the timestamp
// passed to enqueue_all by (optionally) making your enqueue lambda binary:
//
//    [] (swarm::Timestamp enqueuerTS, int i) {
//        swarm::enqueue(enqueuerTS, ..., functionOf(i));
//    }
//
// and you can continue to use a reference to the iterable:
//
//    [] (swarm::Timestamp enqueuerTS, int& i) {
//        swarm::enqueue(enqueuerTS, ..., functionOfPtr(&i));
//    }
//-----------------------------------------------------------------------------


template <typename EnqOneFn, typename Iterator>
struct __is_timestamp_enqueue_lambda {
    static const bool value = swarm::disjunction<
            std::is_convertible<
                EnqOneFn,
                std::function<
                    void(Timestamp,
                         typename std::iterator_traits<Iterator>::value_type)
                >
            >
            ,
            std::is_convertible<
                EnqOneFn,
                std::function<
                    void(Timestamp,
                         typename std::iterator_traits<Iterator>::reference)
                >
            >
            >::value;
};


template <typename Iterator,
          typename EnqOneFn,
          typename Enable = void>
struct __EnqueueForEach {
    // Use a unary enqueue lambda
    void operator()(Timestamp, Iterator first, Iterator last, EnqOneFn enq) {
        for (; first != last; ++first) enq(*first);
    }
};

template <typename Iterator, typename EnqOneFn>
struct __EnqueueForEach<Iterator, EnqOneFn,
        typename std::enable_if<
                __is_timestamp_enqueue_lambda<EnqOneFn, Iterator>::value
                >::type
        > {
    // Binary enqueue lambda
    void operator()(Timestamp ts, Iterator first, Iterator last, EnqOneFn enq) {
        for (; first != last; ++first) enq(ts, *first);
    }
};


template <typename Iterator, typename EnqOneFn>
static inline void __enqueue_for_each(Timestamp ts, Iterator first,
                               Iterator last, EnqOneFn enq) {
    __EnqueueForEach<Iterator, EnqOneFn> efe;
    efe(ts, first, last, enq);
}


// FIXME(mcj) make this port modular than using #ifdefs
template <typename Iterator>
static constexpr uint32_t __enqueue_lg_fanout(Iterator first, Iterator last) {
#if !defined(PLS_ENQUEUE_ALL_2_WAY_FANOUT)
    // Wide expansion.
    // Notice that with only 10 elements to iterate over, we want a
    // fanout of 2, otherwise each leaf enqueuer would wrap only 1 to 2
    // real tasks. With, say, 1024 elements, we want that large fanout
    // of 8. The following formula provides a fanout of 4 for 17
    // elements, yielding leaf enqueuers that wrap 4 to 5 real tasks.
    constexpr int64_t T = swarm::max_children * swarm::max_children / 2;
    if ((last - first) > T) return swarm::ilog2(swarm::max_children);
    if ((last - first) > T / 2) return swarm::ilog2(swarm::max_children / 2);
#endif
    return swarm::ilog2(2);
}


// Do not call this directly
template <typename Iterator, typename EnqOneFn, EnqFlags Flags>
static inline void __enqueuer(Timestamp ts, Iterator first, Iterator last,
        EnqOneFn enq) {
    // TODO(mcj) the following only works for
    // http://en.cppreference.com/w/cpp/concept/RandomAccessIterator
    // To generalize, we could use a local integer counter, but that adds more
    // operations to the loop.
    if ((last - first) <= swarm::max_children) {
        __enqueue_for_each(ts, first, last, enq);
    } else {
        const uint32_t lgfanout = __enqueue_lg_fanout(first, last);
        // If we're supposed to use NOHINT, for the left-most child there's
        // little harm in using SAMEHINT, and avoiding the NoC latency to start
        // the left-most __enqueuer.
        constexpr EnqFlags LeftFlags = swarm::Hint::__replaceNoWithSame(Flags);

        Iterator start = first;
        Iterator end = start + ((last - first) >> lgfanout);
        swarm::enqueue((__enqueuer<Iterator, EnqOneFn, Flags>),
                     ts, LeftFlags, start, end, enq);
        for (uint32_t i = 1u; i < (1u << lgfanout) - 1u; i++) {
            start = end;
            end = start + ((last - first) >> lgfanout);
            swarm::enqueue((__enqueuer<Iterator, EnqOneFn, Flags>),
                         ts, Flags, start, end, enq);
        }
        start = end;
        swarm::enqueue((__enqueuer<Iterator, EnqOneFn, Flags>),
                     ts, Flags, start, last, enq);
    }
}


// Do not call this directly.
// A variant of __enqueuer when EnqOneFn is a stateless lambda
template <typename Iterator, typename EnqOneFn, EnqFlags Flags>
static inline void __enqueuerNoLmb(Timestamp ts, Iterator first, Iterator last) {
    if ((last - first) <= swarm::max_children) {
        // Get a callable function object from the enqueue lambda type.  NOTE:
        // lambdas aren't constexpr in C++14, but can be constexpr in C++17.
        const auto enq = swarm::make_lambda<EnqOneFn>{}();
        __enqueue_for_each(ts, first, last, enq);
    } else {
        const uint32_t lgfanout = __enqueue_lg_fanout(first, last);
        constexpr EnqFlags LeftFlags = swarm::Hint::__replaceNoWithSame(Flags);

        Iterator start = first;
        Iterator end = start + ((last - first) >> lgfanout);
        swarm::enqueue((__enqueuerNoLmb<Iterator, EnqOneFn, Flags>),
                     ts, LeftFlags, start, end);
        for (uint32_t i = 1u; i < (1u << lgfanout) - 1u; i++) {
            start = end;
            end = start + ((last - first) >> lgfanout);
            swarm::enqueue((__enqueuerNoLmb<Iterator, EnqOneFn, Flags>),
                         ts, Flags, start, end);
        }
        start = end;
        swarm::enqueue((__enqueuerNoLmb<Iterator, EnqOneFn, Flags>),
                     ts, Flags, start, last);
    }
}


// Do not call this directly
template <typename Iterator, typename EnqOneFn, typename TSFn, EnqFlags Flags>
static inline void __enqueuerTSFn(Timestamp ts, Iterator first, Iterator last,
        TSFn tsfn, EnqOneFn enq) {
    if ((last - first) <= swarm::max_children) {
        __enqueue_for_each(tsfn(*first), first, last, enq);
    } else {
        const uint32_t lgfanout = __enqueue_lg_fanout(first, last);
        constexpr EnqFlags LeftFlags = swarm::Hint::__replaceNoWithSame(Flags);
        Iterator start = first;
        Iterator end = start + ((last - first) >> lgfanout);
        // Note: ts == tsfn(*first)
        swarm::enqueue((__enqueuerTSFn<Iterator, EnqOneFn, TSFn, Flags>),
                     ts, LeftFlags, start, end, tsfn, enq);
        for (uint32_t i = 1u; i < (1u << lgfanout) - 1u; i++) {
            start = end;
            end = start + ((last - first) >> lgfanout);
            swarm::enqueue((__enqueuerTSFn<Iterator, EnqOneFn, TSFn, Flags>),
                         tsfn(*start), Flags, start, end, tsfn, enq);
        }
        start = end;
        swarm::enqueue((__enqueuerTSFn<Iterator, EnqOneFn, TSFn, Flags>),
                     tsfn(*start), Flags, start, last, tsfn, enq);
    }
}


// Use a struct for partial template specialization
// https://artofsoftware.org/2012/12/20/c-template-function-partial-specialization/
// http://en.cppreference.com/w/cpp/language/partial_specialization
// This variant is used when a timestamp lambda is provided.
//
// FirstFlags: Flags passed to the first pair of __enqueuers
// ChildFlags: Flags passed to the rest of the descendents of those __enqueuers.
// We cannot pass SAMETASK into the hint flags of the __enqueuer, so we pass
// FirstFlags. But with SAMETASK in the template arguments of __enqueuers, then
// actual running __enqueuers will pass SAMETASK as an enqueue flag.
template <EnqFlags FirstFlags,
          EnqFlags ChildFlags,
          uint32_t MaxBaseEnqs,
          typename Iterator,
          typename EnqOneFn,
          typename TSFn,
          typename Enable = void>
struct __EnqueueAll {

    inline void operator()(Iterator first, Iterator last,
                           EnqOneFn enq, TSFn tsfn) {
#ifndef SEQ_RUNTIME
        if ((last - first) <= MaxBaseEnqs) {
#else
        if (true) {
#endif
            __enqueue_for_each(tsfn(*first), first, last, enq);
            return;
        }

        Iterator midpoint = (MaxBaseEnqs > 1) ?
                (first + (last - first) / 2) :
                last;
        swarm::enqueue((__enqueuerTSFn<Iterator, EnqOneFn, TSFn, ChildFlags>),
                     tsfn(*first), FirstFlags, first, midpoint, tsfn, enq);
        if (MaxBaseEnqs > 1) {
            swarm::enqueue((__enqueuerTSFn<Iterator, EnqOneFn, TSFn, ChildFlags>),
                         tsfn(*midpoint), FirstFlags, midpoint, last, tsfn, enq);
        }
    }

};

// Single timestamp and stateless lambda variant
template <EnqFlags FirstFlags,
          EnqFlags ChildFlags,
          uint32_t MaxBaseEnqs,
          typename Iterator,
          typename EnqOneFn,
          typename TimestampType>
struct __EnqueueAll<FirstFlags, ChildFlags, MaxBaseEnqs,
        Iterator, EnqOneFn, TimestampType,
        typename std::enable_if< swarm::conjunction<
              std::is_integral<TimestampType>
              ,
              swarm::is_stateless<EnqOneFn>
            >::value >::type
        > {

    inline void operator()(Iterator first, Iterator last,
                           EnqOneFn enq, TimestampType ts) {
        swarm::Timestamp enqTS = static_cast<swarm::Timestamp>(ts);
#ifndef SEQ_RUNTIME
        if ((last - first) <= MaxBaseEnqs) {
#else
        if (true) {
#endif
            __enqueue_for_each(enqTS, first, last, enq);
            return;
        }

        Iterator midpoint = (MaxBaseEnqs > 1) ?
                (first + (last - first) / 2) :
                last;
        swarm::enqueue((__enqueuerNoLmb<Iterator, EnqOneFn, ChildFlags>),
                     enqTS, FirstFlags, first, midpoint);
        if (MaxBaseEnqs > 1) {
            swarm::enqueue((__enqueuerNoLmb<Iterator, EnqOneFn, ChildFlags>),
                         enqTS, FirstFlags, midpoint, last);
        }
    }
};


// Single timestamp and stateful lambda variant
template <EnqFlags FirstFlags,
          EnqFlags ChildFlags,
          uint32_t MaxBaseEnqs,
          typename Iterator,
          typename EnqOneFn,
          typename TimestampType>
struct __EnqueueAll<FirstFlags, ChildFlags, MaxBaseEnqs,
        Iterator, EnqOneFn, TimestampType,
        typename std::enable_if< swarm::conjunction<
              std::is_integral<TimestampType>
              ,
              swarm::negation<swarm::is_stateless<EnqOneFn> >
            >::value >::type
        > {

    inline void operator()(Iterator first, Iterator last,
                           EnqOneFn enq, TimestampType ts) {
        swarm::Timestamp enqTS = static_cast<swarm::Timestamp>(ts);
        if ((last - first) <= MaxBaseEnqs) {
            __enqueue_for_each(enqTS, first, last, enq);
            return;
        }

        Iterator midpoint = (MaxBaseEnqs > 1) ?
                (first + (last - first) / 2) :
                last;
        swarm::enqueue((__enqueuer<Iterator, EnqOneFn, ChildFlags>),
                     enqTS, FirstFlags, first, midpoint, enq);
        if (MaxBaseEnqs > 1) {
            swarm::enqueue((__enqueuer<Iterator, EnqOneFn, ChildFlags>),
                         enqTS, FirstFlags, midpoint, last, enq);
        }
    }
};


template <EnqFlags Flags,
          uint32_t MaxBaseEnqs,
          typename Iterator,
          typename EnqOneFn,
          typename TimestampType>
static inline void enqueue_all(Iterator first, Iterator last,
                               EnqOneFn enq, TimestampType tstype) {
    static_assert(!(Flags & EnqFlags::NOHASH), "No support for NOHASH");
    static_assert(!(Flags & EnqFlags::SAMETASK), "Cannot specify SAMETASK here");
    static_assert(
            std::is_convertible<
                    typename std::iterator_traits<Iterator>::iterator_category,
                    std::random_access_iterator_tag>::value,
            "We require iterators with constant-time random access.");

    constexpr EnqFlags FirstFlags = EnqFlags(Flags | PRODUCER);
    constexpr EnqFlags ChildFlags = EnqFlags(FirstFlags | SAMETASK);
    __EnqueueAll<FirstFlags, ChildFlags,
                 MaxBaseEnqs,
                 Iterator, EnqOneFn, TimestampType> ea;
    ea(first, last, enq, tstype);
}


template <uint32_t EnqueuesPerTask,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
struct alignas(SWARM_CACHE_LINE) __EnqueueStrandsData {
    EnqueueLambda el;
    TimestampLambda tsl;
    HintLambda hl;
    uint32_t stride;
    Iterator last;

    // Extract from the lambda to easily see the overheads
    static inline void del(swarm::Timestamp, const __EnqueueStrandsData* ed) {
        delete ed;
    }

    inline void operator()(swarm::Timestamp ts, Iterator begin) const {
        Iterator end = std::min(begin + EnqueuesPerTask, last);
        __enqueue_for_each(ts, begin, end, el);
        Iterator next = begin + stride;
        if (next < last) {
            swarm::Hint h(hl(*next));
            swarm::enqueueLambda(this,
                    tsl(*next),
                    // FIXME(mcj): This is awful.
                    {h.hint, EnqFlags(h.flags | PRODUCER | SAMETASK)},
                    next);
        } else if (end == last) {
            // ts >= timestamp(all other instances of this task) since this is
            // the last instance in an unordered or ordered iterable.
            // Add 1 in case the instances are all unordered.
            swarm::enqueue(del, ts + 1, EnqFlags(SAMEHINT | MAYSPEC), this);
        }
    }
};

// An enqueue_all that uses parallel strands, not an unbounded tree.
// It expects a timestamp lambda and hint lambda for __enqueuers
// TODO(mcj) support an integer timestamp
// For now it's easier to just assume the timestamp is a lambda
template <uint32_t MaxBaseEnqs,
          uint32_t EnqueuesPerTask,
          uint32_t MaxStrands, // Maximum number of strands you can tolerate
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
static inline void enqueue_all(Iterator first, Iterator last,
                               EnqueueLambda el, TimestampLambda tsl,
                               HintLambda hl) {
    static_assert(EnqueuesPerTask < swarm::max_children,
            "Number of enqueues per enqueuer task must be < max_children");
    static_assert(
            std::is_convertible<
                    typename std::iterator_traits<Iterator>::iterator_category,
                    std::random_access_iterator_tag>::value,
            "We require iterators with constant-time random access.");

#ifndef SEQ_RUNTIME
    if ((last - first) <= MaxBaseEnqs) {
#else
    if (true) {
#endif
        __enqueue_for_each(tsl(*first), first, last, el);
        return;
    }

    // FIXME(mcj) we should probably listen to the caller if they provided any
    // MaxStrands other than UINT32_MAX. For now, assume 4*threads is a
    // reasonable max, unless the caller specified lower.
    constexpr uint32_t numstrands = 4;
    uint32_t maxstrands = std::min(swarm::num_threads() * numstrands, MaxStrands);
    uint32_t strands = (last - first) / EnqueuesPerTask;
    if (strands < maxstrands) {
        // FIXME(mcj) HACK: if the first element's hint has MAYSPEC, assume the
        // whole range of __enqueuers can use MAYSPEC.
        swarm::Hint h(hl(*first));
        if (h.flags & EnqFlags::MAYSPEC) {
            swarm::enqueue_all<EnqFlags(NOHINT | MAYSPEC), MaxBaseEnqs, Iterator,
                             EnqueueLambda, TimestampLambda>
                            (first, last, el, tsl);
        } else {
            swarm::enqueue_all<NOHINT, MaxBaseEnqs, Iterator,
                             EnqueueLambda, TimestampLambda>
                            (first, last, el, tsl);
        }
    } else {
        using ED = __EnqueueStrandsData<EnqueuesPerTask, Iterator,
                                        EnqueueLambda, TimestampLambda,
                                        HintLambda>;
        const auto* ed = new ED{el, tsl, hl, maxstrands*EnqueuesPerTask, last};

        // N.B. this assumes that the hint lambda and timestamp lambdas do not
        // mutate global shared state. That's probably a safe assumption?
        swarm::enqueue_all<EnqFlags(NOHINT | MAYSPEC), MaxBaseEnqs>(
                swarm::u32it(0), swarm::u32it(maxstrands),
                [ed, first] (uint32_t s) {
                    Iterator sbegin = first + s * EnqueuesPerTask;
                    swarm::Hint h(ed->hl(*sbegin));

                    swarm::enqueueLambda(ed,
                        ed->tsl(*sbegin),
                        {h.hint, EnqFlags(h.flags | PRODUCER)},
                        sbegin);
                },
                // [mcj] We can't use the timestamp lambda for a different
                // iterator. All __enqueuers leading to the strands will have
                // the same timestamp as the first element's enqueue.
                tsl(*first));
    }
}


template <uint32_t EnqueuesPerTask,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
struct __EnqueueProgressiveData {
    EnqueueLambda el;
    TimestampLambda tsl;
    HintLambda hl;
    uint32_t maxstride;
    Iterator last;
};


template <uint32_t EnqueuesPerTask,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
struct alignas(SWARM_CACHE_LINE) __EnqueueProgressiveByPointer {

    using EPD = __EnqueueProgressiveData<EnqueuesPerTask, Iterator,
                                         EnqueueLambda, TimestampLambda,
                                         HintLambda>;
    EPD data;

    void operator()(swarm::Timestamp ts, Iterator begin, uint32_t stride) const {
        Iterator end = std::min(begin + EnqueuesPerTask, data.last);
        __enqueue_for_each(ts, begin, end, data.el);
        Iterator left = begin + stride;
        Iterator right = begin + 2 * stride;
        if (left < data.last) {
            if (right < data.last && stride < data.maxstride) {
                reenqueue(left, 2 * stride);
                reenqueue(right, 2 * stride);
            } else {
                reenqueue(left, stride);
            }
        } else if (end == data.last) {
            finish();
        }
    }

    inline void reenqueue(Iterator begin, uint32_t strands) const {
        swarm::Hint h(data.hl(*begin));
        swarm::enqueueLambda(this,
                data.tsl(*begin),
                {h.hint, EnqFlags(h.flags | PRODUCER | SAMETASK)},
                begin,
                strands);
    }

    inline void finish() const {
        // ts >= timestamp(all other instances of this task) since this is the
        // last instance in an unordered or ordered iterable.
        // Add 1 in case the instances are all unordered.
        swarm::enqueue(del, swarm::timestamp() + 1,
                     EnqFlags(SAMEHINT | MAYSPEC), this);
    }

    static void del(swarm::Timestamp, const __EnqueueProgressiveByPointer* ep) {
        delete ep;
    }
};


template <uint32_t EnqueuesPerTask,
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
struct __EnqueueProgressiveByValue {
    using EPD = __EnqueueProgressiveData<EnqueuesPerTask, Iterator,
                                         EnqueueLambda, TimestampLambda,
                                         HintLambda>;
    EPD data;

    void operator()(swarm::Timestamp ts, Iterator begin, uint32_t stride) const {
        Iterator end = std::min(begin + EnqueuesPerTask, data.last);
        __enqueue_for_each(ts, begin, end, data.el);
        Iterator left = begin + stride;
        Iterator right = begin + 2 * stride;
        if (left < data.last) {
            if (right < data.last && stride < data.maxstride) {
                reenqueue(left, 2 * stride);
                reenqueue(right, 2 * stride);
            } else {
                reenqueue(left, stride);
            }
        }
    }

    inline void reenqueue(Iterator begin, uint32_t strands) const {
        swarm::Hint h(data.hl(*begin));
        swarm::enqueueLambda(*this,
                data.tsl(*begin),
                {h.hint, EnqFlags(h.flags | PRODUCER | SAMETASK)},
                begin,
                strands);
    }
};


// An enqueue_all that starts with a gradually widening tree that enqueues
// the earliest real tasks first, and eventually expands into parallel strands
// It expects a timestamp lambda and hint lambda for __enqueuers
// TODO(mcj) support an integer timestamp
// For now it's easier to just assume the timestamp is a lambda
template <uint32_t MaxBaseEnqs,
          uint32_t EnqueuesPerTask,
          uint32_t MaxStrands, // Maximum number of strands you can tolerate
          typename Iterator,
          typename EnqueueLambda,
          typename TimestampLambda,
          typename HintLambda>
static inline void enqueue_all_progressive(Iterator first, Iterator last,
        EnqueueLambda el, TimestampLambda tsl, HintLambda hl) {
    static_assert(EnqueuesPerTask < swarm::max_children,
            "Number of enqueues per enqueuer task must be < max_children");
    static_assert(
            std::is_convertible<
                    typename std::iterator_traits<Iterator>::iterator_category,
                    std::random_access_iterator_tag>::value,
            "We require iterators with constant-time random access.");

#ifndef SEQ_RUNTIME
    if ((last - first) <= MaxBaseEnqs) {
#else
    if (true) {
#endif
        __enqueue_for_each(tsl(*first), first, last, el);
        return;
    }

    // FIXME(mcj) we should probably listen to the caller if they provided any
    // MaxStrands other than UINT32_MAX. For now, assume 4*threads is a
    // reasonable max, unless the caller specified lower.
    constexpr uint32_t numstrands = 4;
    uint32_t maxstrands = std::min(swarm::num_threads() * numstrands, MaxStrands);
    using EPV = __EnqueueProgressiveByValue<EnqueuesPerTask, Iterator,
            EnqueueLambda, TimestampLambda, HintLambda>;
    using EPP = __EnqueueProgressiveByPointer<EnqueuesPerTask, Iterator,
            EnqueueLambda, TimestampLambda, HintLambda>;

    if (sizeof(EPV) <= (SIM_MAX_ENQUEUE_REGS - 2) * sizeof(uint64_t)) {
        EPV epv{{el, tsl, hl, maxstrands * EnqueuesPerTask, last}};
        swarm::Hint h(epv.data.hl(*first));
        swarm::enqueueLambda(epv,
                epv.data.tsl(*first),
                {h.hint, EnqFlags(h.flags | PRODUCER)},
                first,
                EnqueuesPerTask);
    } else {
        EPP* epp = new EPP{{el, tsl, hl, maxstrands * EnqueuesPerTask, last}};
        swarm::Hint h(epp->data.hl(*first));
        swarm::enqueueLambda(epp,
                epp->data.tsl(*first),
                {h.hint, EnqFlags(h.flags | PRODUCER)},
                first,
                EnqueuesPerTask);
    }
}

}
