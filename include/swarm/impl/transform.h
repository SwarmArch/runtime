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
#include <cstdint>

#include "../api.h"
#include "../cps.h"
#include "block.h"


namespace swarm {

template <class InputIt, class OutputIt, class UnaryOperation>
void transform(InputIt ifirst, InputIt ilast, OutputIt ofirst, UnaryOperation o,
               Timestamp ts) {
    pls_cbegin(ts, EnqFlags::NOHINT, [ifirst, ilast, ofirst, o]);

    // TODO(mcj) verify that these are constexpr with c++14 by default.
    uint32_t blockSize = swarm::block::elementsPerLine<OutputIt>();
    // TODO(mcj) there is a tradeoff between coarsening to several cachelines
    // per task, vs locality of those cachelines, in case they were already
    // present in the L2s
    uint32_t numTasks = 1 + (std::distance(ifirst, ilast) - 1) / blockSize;
    // TODO(mcj) deepen isn't strictly necessary, but an API would use it to
    // guarantee atomicity
    swarm::deepen();

    pls_forall_ts_begin(ts, uint32_t b, 0u, numTasks,
            swarm::Hint::cacheLine(&(*(ofirst + b * blockSize))),
            [ifirst, ilast, ofirst, blockSize, o]) {
        // FIXME(mcj) align these to cache-line boundaries
        InputIt begin = ifirst + b * blockSize;
        InputIt end = std::min(ilast, ifirst + (b + 1) * blockSize);
        std::transform(begin, end, ofirst + b * blockSize, o);
    } pls_forall_ts_fallthru(EnqFlags(NOHINT | MAYSPEC), []);
    pls_forall_ts_end();

    pls_cend();
}



}
