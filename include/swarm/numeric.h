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

// This file is named to imitate the C++ <numeric> header.
//
// It also must remain separate from algorithm.h because otherwise
//   algorithm.h -> impl/reduce.h -> cps.h -> algorithm.h
// TODO(mcj) Maybe the answer is to remove enqueue_all from algorithm.h

#pragma once

namespace swarm {

/**
 * The first four arguments are the same as in std::accumulate.
 * - ts is the timestamp in which the reduction should appear to be atomic.
 * - cb is the callback task that should receive the result of the reduction as
 *   an argument. It is created with timestamp ts, but does not appear atomic
 *   with the reduction. If the caller wishes for the callback to be atomic with
 *   the reduction, they should call swarm::deepen() beforehand.
 *   TODO(mcj) Maybe the API should expose a different timestamp for the
 *   reduction and the callback.
 */
template <class Iterator, class T, class BinaryOp, class CallBack>
static inline void reduce(Iterator first, Iterator last, T init, BinaryOp o,
        Timestamp ts, CallBack cb);

}

#include "impl/reduce.h"
