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

/* Sequential implementations of CPS macros. Useful to compare fine- and
 * coarse-grained versions of Swram programs.
 */

#pragma once

#include "cps.h"


/* Forall variant with tree reductions */
namespace swarm {

// NOTE(dsm): There are two levels of optimization allowed here:
// - If you maintain compatibility with the parallel version, i.e., you allow
//   having parallellism inside of each iteration, then the continuation must be
//   heap-allocated. This is what this implementation does.
//
// - If you only allow sequential code inside each operation, then the
//   continuation can be stack-allocated and only needs to do the reduction.
//   The loop can be external to the continuation.
//
// In bayes, there is negligible (<0.1%) difference between both forall_seq
// implementations at 1 core (when applied to the inner loops). Looks like gcc
// optimizes the code below heavily to make it as cheap as a loop that calls
// the continuation; the only diff is the heap allocation.
template <typename RedType, typename IterType, typename HintLambda, typename
    BodyLambda, typename RedLambda, typename TermLambda>
inline void forallred_seq(swarm::Timestamp ts, IterType first, IterType sup, HintLambda hl, BodyLambda bl, RedType initialValue, RedLambda rl, TermLambda tl) {
    assert(sup >= first);
    if (sup == first) {
        tl(ts, initialValue);
        return;
    }

    struct LoopCont : public CC<RedType> {
        const BodyLambda bodyLambda;
        const RedLambda redLambda;
        const TermLambda termLambda;
        RedType redVal;
        IterType cur;
        const IterType sup;

        LoopCont(const BodyLambda& bl, const RedLambda& rl, const TermLambda& tl, const RedType& initialValue, IterType first, IterType sup)
            : bodyLambda(bl), redLambda(rl), termLambda(tl), redVal(initialValue), cur(first), sup(sup) {}
        ~LoopCont() {}

        // Reduction
        void run(swarm::Timestamp ts, RedType r) override {
            redLambda(redVal, r);
            if (++cur == sup) {
                // Make termination a tail call. This way we don't eat a stack
                // frame every loop, and delete CC before termination so use
                // less heap. Faster than non-tail-call:
                //    termLambda(ts, redVal); delete this;
                // (bayes @ 1core -r128 + "+" params: 12.13 -> 12.25 MCycles)
                TermLambda tl = termLambda;
                RedType rv = redVal;
                delete this;
                tl(ts, rv);
            } else {
                // Tail-call-optimized so we don't blow up the stack
                bodyLambda(ts, cur, this);
            }
        }
    };

    LoopCont* lc = new LoopCont(bl, rl, tl, initialValue, first, sup);
    bl(ts, first, lc);  // tail call
}

}  // namespace swarm

// Convenience macro for forallred (all others use thenormal reduce)
#define pls_forallred_seq_begin(__ts, argExpr, start, sup, redType, hlExpr, captureList...) \
{ \
    swarm::forallred_seq<redType>(__ts, start, sup, \
            captureList (argExpr) { return (hlExpr); }, \
            captureList (swarm::Timestamp ts, argExpr, swarm::CC<redType>* cc) { \
            /* user specifies body lambda in full */

