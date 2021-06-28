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

/* Macros to transform sequential code into continuation-passing style
 */

#pragma once

#include <boost/iterator/counting_iterator.hpp>
#include <algorithm>
#include "api.h"
#include "aligned.h"
#include "algorithm.h"

// Normal continuations are simple wrappers for enqueueLambda. Allows breaking
// up sequential code in tasks, with the appropriate context passed through
// captures
#define pls_cbegin(__ts, hint, captureList...) \
{ \
    swarm::Timestamp __t = __ts; \
    swarm::Hint __hint = hint; \
    swarm::enqueueLambda(captureList (swarm::Timestamp ts) -> void {

#define pls_cend() \
    }, __t, __hint); \
}

// forall
namespace swarm {

template <typename IterType, typename HintLambda,
          typename BodyLambda, typename TermLambda>
struct alignas(SWARM_CACHE_LINE) __ForallLoopData {
    const HintLambda hl;
    const BodyLambda bl;
    const IterType sup;
    const IterType stride;
    const TermLambda tl;
    volatile IterType strandsFinished __attribute__((aligned(SWARM_CACHE_LINE)));

    inline void operator()(swarm::Timestamp ts, IterType i) {
        bl(ts, i);
        IterType next = i + stride;
        if (next < sup) {
            swarm::Hint h(hl(next));
            swarm::enqueueLambda(this, ts,
                               {h.hint, EnqFlags(h.flags | SAMETASK)},
                               next);
        } else {
            pls_cbegin(ts,
                       Hint(Hint::cacheLine(&strandsFinished),
                            EnqFlags::MAYSPEC),
                       [this]);
            IterType last = __sync_add_and_fetch(&strandsFinished, 1);
            if (last == stride) {
                // We don't know what data the termination lambda accesses,
                // so we can't wrap it in a MAYSPEC task
                pls_cbegin(ts, EnqFlags::NOHINT, [this]);
                tl(ts);
                delete this;
                pls_cend();
            }
            pls_cend();
        }
    }
};


template <typename IterType, typename HintLambda,
          typename BodyLambda, typename TermLambda>
inline void forall(swarm::Timestamp ts, IterType first, IterType sup,
                   HintLambda hl, BodyLambda bl, TermLambda tl) {
    assert(sup >= first);
    if (sup == first) {
        tl(ts);
        return;
    }

    // TODO(dsm): Allow setting number of strands? (this prioritizes
    // parallelism, but may incur excessive control overheads with short loops)
    uint32_t stride = std::min(sup - first, (IterType) num_threads()*4);

    using LD = __ForallLoopData<IterType, HintLambda, BodyLambda, TermLambda>;
    auto* l = new LD{hl, bl, sup, (IterType)stride, tl, 0};

    swarm::enqueue_all<EnqFlags(NOHINT | MAYSPEC)>(
                     boost::counting_iterator<uint32_t>(0),
                     boost::counting_iterator<uint32_t>(stride),
                     [ts, l, first] (uint32_t s) {
        swarm::enqueueLambda(l, ts, l->hl(first + s), first + s);
    }, ts);
}

}  // namespace swarm

// Convenience macro to make the termination function a continuation
#define pls_forall_begin(__ts, argExpr, start, sup, hlExpr, captureList...) \
{ \
    swarm::forall(__ts, start, sup, \
            captureList (argExpr) -> swarm::Hint { return (hlExpr); }, \
            captureList (swarm::Timestamp ts, argExpr) -> void { \
            /* user specifies body lambda in full */

#define pls_forall_fallthru(captureList...) \
            }, captureList (swarm::Timestamp ts) -> void {

#define pls_forall_end() \
            }); \
}

/* Forall variant with exposed per-iteration continuations (more expensive, but
 * needed if the loop body has continuations)
 */
namespace swarm {

struct Continuation {
    virtual void run(swarm::Timestamp ts) = 0;
    virtual ~Continuation() {}
};

template <typename IterType, typename HintLambda,
          typename BodyLambda, typename TermLambda>
struct alignas(SWARM_CACHE_LINE) __ForallLoopCont : public Continuation {

    struct alignas(SWARM_CACHE_LINE) LoopData {
        const HintLambda hl;
        const BodyLambda bl;
        const IterType sup;
        const IterType stride;
        const TermLambda tl;
        volatile IterType strandsFinished __attribute__((aligned(SWARM_CACHE_LINE)));
    };

    LoopData* l;
    IterType i;

    __ForallLoopCont(LoopData* _l, IterType _i) : l(_l), i(_i) {}
    ~__ForallLoopCont() {}

    void operator()(swarm::Timestamp ts) { l->bl(ts, this, i); }

    void run(swarm::Timestamp ts) {
        i += l->stride;
        if (i < l->sup) {
            swarm::enqueueLambda(this, ts, l->hl(i));
        } else {
            pls_cbegin(ts,
                       Hint(Hint::cacheLine(&l->strandsFinished),
                            EnqFlags::MAYSPEC),
                       [this]);
            IterType last = __sync_add_and_fetch(&l->strandsFinished, 1);
            if (last == l->stride) {
                // We don't know what data the termination lambda accesses,
                // so we can't wrap it in a MAYSPEC task
                pls_cbegin(ts, EnqFlags::NOHINT, [l=this->l]);
                l->tl(ts);
                delete l;
                pls_cend();
            }
            delete this;
            pls_cend();
        }
    }
};


template <typename IterType, typename HintLambda,
          typename BodyLambda, typename TermLambda>
inline void forallcc(swarm::Timestamp ts, IterType first, IterType sup,
                     HintLambda hl, BodyLambda bl, TermLambda tl) {
    assert(sup >= first);
    if (sup == first) {
        tl(ts);
        return;
    }

    // TODO(dsm): Allow setting number of strands? (this prioritizes
    // parallelism, but may incur excessive control overheads with short loops)
    uint32_t stride = std::min(sup - first, (IterType) num_threads()*4);

    using LC = __ForallLoopCont<IterType, HintLambda, BodyLambda, TermLambda>;
    auto* l = new typename LC::LoopData{hl, bl, sup, (IterType)stride, tl, 0};

    swarm::enqueue_all<EnqFlags(NOHINT | MAYSPEC)>(
                     boost::counting_iterator<uint32_t>(0),
                     boost::counting_iterator<uint32_t>(stride),
                     [ts, l, first] (uint32_t s) {
        LC* lc = new LC(l, first + s);
        swarm::enqueueLambda(lc, ts, l->hl(first + s));
    }, ts);
}

}  // namespace swarm

// Convenience macros for forallcc
#define pls_forallcc_begin(__ts, argExpr, start, sup, hlExpr, captureList...) \
{ \
    swarm::forallcc(__ts, start, sup, \
            captureList (argExpr) -> swarm::Hint { return (hlExpr); }, \
            captureList (swarm::Timestamp ts, swarm::Continuation* cc, argExpr) -> void { \
            /* user specifies body lambda in full */

#define pls_forallcc_fallthru(captureList...) \
            }, captureList (swarm::Timestamp ts) -> void {

#define pls_forallcc_end() \
            }); \
}

namespace swarm {

template <typename IterType, typename HintLambda, typename BodyLambda,
          typename TermHintLambda, typename TermLambda>
struct alignas(SWARM_CACHE_LINE) __ForallTSLoopData {
    const HintLambda hl;
    const BodyLambda bl;
    const IterType sup;
    const IterType stride;
    const TermHintLambda tlhl;
    const TermLambda tl;

    inline void operator()(swarm::Timestamp ts, IterType i) const {
        bl(ts, i);
        IterType next = i + stride;
        if (next < sup) {
            swarm::Hint h(hl(next));
            swarm::enqueueLambda(this, ts,
                               {h.hint, EnqFlags(h.flags | SAMETASK)},
                               next);
        } else if (next == sup) {
            pls_cbegin(ts + 1, tlhl(), [this]);
            tl(ts);
            delete this;
            pls_cend();
        }
    }
};

// forall-like loop where all body lambdas run with the given Timestamp, but the
// termination lambda runs at ts + 1.
// This accrues the benefits of a strided enqueue-all pattern, without the
// global stride counter increments of forall and forallcc.
template <typename IterType, typename HintLambda, typename BodyLambda,
          typename TermHintLambda, typename TermLambda>
inline void forall_ts(swarm::Timestamp ts, IterType first, IterType sup,
                      HintLambda hl, BodyLambda bl, TermHintLambda tlhl,
                      TermLambda tl) {
    assert(sup >= first);
    if (sup == first) {
        tl(ts + 1);
        return;
    }

    // Control parallelism: short loops are likely to be inner loops
    // where we're better off with a single of few strands to limit
    // the cost of termination.
    uint32_t stride = std::min((sup - first)/4 + 1, (IterType) num_threads()*4);

    using LD = __ForallTSLoopData<IterType, HintLambda, BodyLambda,
                                  TermHintLambda, TermLambda>;
    const auto* l = new LD{hl, bl, sup, (IterType)stride, tlhl, tl};

    swarm::enqueue_all<EnqFlags(NOHINT | MAYSPEC)>(
                     boost::counting_iterator<uint32_t>(0),
                     boost::counting_iterator<uint32_t>(stride),
                     [ts, l, first] (uint32_t s) {
        swarm::enqueueLambda(l, ts, l->hl(first + s), first + s);
    }, ts);
}

}  // namespace swarm

// Convenience macro to make the termination function a continuation
#define pls_forall_ts_begin(__ts, argExpr, start, sup, hlExpr, captureList...) \
{ \
    swarm::forall_ts(__ts, start, sup, \
            captureList (argExpr) -> swarm::Hint { return (hlExpr); }, \
            captureList (swarm::Timestamp ts, argExpr) -> void { \
            /* user specifies body lambda in full */

#define pls_forall_ts_fallthru(tlhlExpr, captureList...) \
            }, \
            [] (void) -> swarm::Hint { return (tlhlExpr); }, \
            captureList (swarm::Timestamp ts) -> void {

#define pls_forall_ts_end() \
            }); \
}



// sequential loops
namespace swarm {

struct SeqLoopContinuation {
    virtual void next(swarm::Timestamp ts, swarm::Hint hint) = 0;
    virtual void done(swarm::Timestamp ts, swarm::Hint hint) = 0;
    virtual ~SeqLoopContinuation() {}
};

template <typename BodyLambda, typename TermLambda>
struct __SeqLoopCont : public SeqLoopContinuation {
    const BodyLambda bl;
    const TermLambda tl;

    void operator()(swarm::Timestamp ts) { bl(ts, this); }

    void next(swarm::Timestamp ts, swarm::Hint hint) {
        swarm::enqueueLambda(this, ts, hint);
    }

    void done(swarm::Timestamp ts, swarm::Hint hint) {
        swarm::enqueueLambda(tl, ts, hint);
        delete this;
    }

    __SeqLoopCont(BodyLambda _bl, TermLambda _tl) : bl(_bl), tl(_tl) {}
};

template <typename BodyLambda, typename TermLambda>
void loopcc(swarm::Timestamp ts, swarm::Hint initialHint, BodyLambda bl, TermLambda tl) {
    auto* l = new __SeqLoopCont<BodyLambda, TermLambda>(bl, tl);
    swarm::enqueueLambda(l, ts, initialHint);
}

}  // namespace swarm

#define pls_loopcc_begin(__ts, initialHint, captureList...) \
{ \
    swarm::loopcc(__ts, initialHint, \
            captureList (swarm::Timestamp ts, \
                         swarm::SeqLoopContinuation* cc) -> void { \
            /* user specifies body lambda, calling cc->next/done to continue/terminate */

#define pls_loopcc_fallthru(captureList...) \
            }, captureList (swarm::Timestamp ts) -> void {

#define pls_loopcc_end() \
            }); \
}

// call-with-continuation
// NOTE(dsm): Can't figure out how to have calls to functions with multiple
// args (args must come before continuation, and gcc chokes on a parameter
// template pack in the middle, ie Arg arg --> Args... args gets deduced as
// zero args). If you need multiple arguments, use a tuple!
namespace swarm {

template <typename ResultType>
struct CC {
    virtual void run(swarm::Timestamp ts, ResultType res) = 0;
    virtual ~CC() {}
};

template <typename F, F* f, typename RetType, typename Arg, typename ContLambda>
void callcc(swarm::Timestamp ts, swarm::Hint hint, Arg arg, swarm::Hint contHint, ContLambda cl) {
    struct Cont : public CC<RetType> {
        ContLambda l;
        swarm::Hint h;

        Cont(ContLambda _l, Hint _h) : l(_l), h(_h) {}
        ~Cont() {}

        void run(swarm::Timestamp ts, RetType res) override {
            swarm::enqueueLambda(l, ts, h, res);
            delete this;
        }
    };

    Cont* cc = new Cont(cl, contHint);
    enqueueTask<F, f>(ts, hint, cc, arg);
}

}  // namespace swarm

#define pls_callcc_begin(__ts, hint, f, arg, retType, retVal, contHint, captureList...) \
{ \
    swarm::callcc<decltype(f), f, retType>(ts, hint, arg, contHint, \
           captureList (swarm::Timestamp ts, retType retVal) -> void {

#define pls_callcc_end() \
            }); \
}

/* Forall variant with tree reductions
 * NOTE: Has a different parallelization strategy than other foralls (expands
 * and reduces recursively, no strands). May be more expensive fro short loops
 * but exposes the most parallelism.
 */
namespace swarm {

template <typename RedType, typename IterType, typename HintLambda,
          typename BodyLambda, typename RedLambda, typename TermLambda>
struct alignas(SWARM_CACHE_LINE) __ForallRedLoopCont : public CC<RedType> {

    struct alignas(SWARM_CACHE_LINE) LoopData {
        const HintLambda hintLambda;
        const BodyLambda bodyLambda;
        const RedLambda redLambda;
        const TermLambda termLambda;
        const RedType initialValue;
    };
    using LoopCont = __ForallRedLoopCont;

    const LoopData* loopData;
    LoopCont* parent;

    // New cache line
    RedType redVal __attribute__((aligned(SWARM_CACHE_LINE)));
    uint32_t syncsLeft;

    __ForallRedLoopCont(const LoopData* l, LoopCont* p)
        : loopData(l), parent(p), redVal(l->initialValue), syncsLeft(-1) {}
    ~__ForallRedLoopCont() {}

    void operator()(swarm::Timestamp ts, IterType i) {
        loopData->bodyLambda(ts, i, this);
    }

    // Expansion; must call right after creation
    void expand(swarm::Timestamp ts, IterType first, IterType sup) {
        assert(first < sup);
        IterType iters = sup - first;
        if (iters == 1) {
            syncsLeft = 1;
            (*this)(ts, first);
        } else if (iters <= swarm::max_children) {
            syncsLeft = iters;
            for (IterType i = first; i < sup; i++) {
                swarm::enqueueLambda(this, ts, loopData->hintLambda(i), i);
            }
        } else {
            // Variable-radix enqueue seeks to make the leaves as
            // high-radix as possible, i.e. 8, as leaves are the most
            // common, so that minimizes overheads (e.g., with 32
            // iterations, an 8x4 tree has 9 allocations total, but a 4x8
            // has 5)
            uint32_t radix = std::min((IterType)8, (iters + 7)/8);
            syncsLeft = radix;
            auto expandLambda = [](swarm::Timestamp ts, LoopCont* lc,
                                   IterType f,
                                   IterType s) { lc->expand(ts, f, s); };

            for (uint32_t i = 0; i < radix; i++) {
                IterType f = first + i*iters/radix;
                IterType s = first + (i+1)*iters/radix;
                LoopCont* child = new LoopCont(loopData, this);
                swarm::enqueueLambda(expandLambda, ts, Hint::cacheLine(child),
                        child, f, s);
            }
        }
    }

    // Reduction
    void run(swarm::Timestamp ts, RedType r) override {
        pls_cbegin(ts, Hint::cacheLine(this), [this, r]);
        loopData->redLambda(redVal, r);
        if (--syncsLeft == 0) {
            if (parent) {
                parent->run(ts, redVal);
            } else {
                loopData->termLambda(ts, redVal);
                delete loopData;
            }
            delete this;
        }
        pls_cend();
    }
};


template <typename RedType, typename IterType, typename HintLambda,
          typename BodyLambda, typename RedLambda, typename TermLambda>
inline void forallred(swarm::Timestamp ts, IterType first, IterType sup,
                      HintLambda hl, BodyLambda bl, RedType initialValue,
                      RedLambda rl, TermLambda tl) {
    assert(sup >= first);
    if (sup == first) {
        tl(ts, initialValue);
        return;
    }

    using LC = __ForallRedLoopCont<RedType, IterType, HintLambda,
                                   BodyLambda, RedLambda, TermLambda>;
    const auto* l = new typename LC::LoopData{hl, bl, rl, tl, initialValue};
    auto* rootCont = new LC(l, nullptr);
    rootCont->expand(ts, first, sup);
}

}  // namespace swarm

// Convenience macros for forallred
#define pls_forallred_begin(__ts, argExpr, start, sup, redType, hlExpr, captureList...) \
{ \
    swarm::forallred<redType>(__ts, start, sup, \
            captureList (argExpr) -> swarm::Hint { return (hlExpr); }, \
            captureList (swarm::Timestamp ts, argExpr, swarm::CC<redType>* cc) -> void { \
            /* user specifies body lambda in full */

#define pls_forallred_reducer(accumExpr, valExpr, initialValue, captureList...) \
            }, initialValue, captureList (accumExpr, valExpr) -> void {

#define pls_forallred_fallthru(resExpr, captureList...) \
            }, captureList (swarm::Timestamp ts, resExpr) -> void {

#define pls_forallred_end() \
            }); \
}

// getcc - Materialize a continuation
// Sometimes, you need to use cps across control-flow structures, for example,
// a branch of an if statement should enqueue some tasks. cc simply gives you
// a continuation that you can use to invoke the fallthru code.
// **cc does not create any tasks**
namespace swarm {

template <typename ResultType>
struct RawCC {
    virtual void run(ResultType res) = 0;
    virtual ~RawCC() {}
};

template <typename RetType, typename BodyLambda, typename TermLambda>
inline void getcc(BodyLambda bl, TermLambda tl) {
    struct Continuation : RawCC<RetType> {
        const TermLambda termLambda;
        void run(RetType v) override {
            termLambda(v);
            delete this;
        }
        Continuation(TermLambda tl) : termLambda(tl) {}
    };

    Continuation* cc = new Continuation(tl);
    bl(cc);
};

}  // namespace swarm

// Convenience macros for getcc
#define pls_getcc_begin(retType, captureList...) \
{ \
    swarm::getcc<retType>(captureList (swarm::RawCC<retType>* cc) -> void {

#define pls_getcc_fallthru(retExpr, captureList...) \
            }, captureList (retExpr) -> void {

#define pls_getcc_end() \
            }); \
}

