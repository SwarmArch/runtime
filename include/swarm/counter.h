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

#include <stdint.h>
#include <atomic>
#include <array>

#include "aligned.h"
#include "api.h"

namespace swarm {

template<typename T> class ParallelCounter;

template<typename T>
void __increment(Timestamp ts, ParallelCounter<T>* pc, uint64_t val) {
    (*pc) += val;
}

template<typename T>
class alignas(SWARM_CACHE_LINE) ParallelCounter {
public:
    ParallelCounter & operator+=(const T & rhs) {
        lcs_[swarm::tid()] += rhs;
        return *this;
    }

    ParallelCounter & operator++() { return operator+=(1); }

    void incrementLater(Timestamp ts, const T & val) {
        // dsm: This was not spatial. Should it be? I've changed it to same
        // since it looks like a deferred action
        // mcj: __increment will increment a thread-local counter, no matter
        // to which tile the task is sent. In that sense, we don't need a hint,
        // but can use system-level load balance (i.e. randomization for now)
        swarm::enqueue(swarm::__increment<T>, ts, EnqFlags::NOHINT, this, val);
    }

    T reduce() const {
        // Not thread-safe
        T accumulator = 0;
        for (auto & lc : lcs_) accumulator += lc;
        return accumulator;
    }

private:
    using LocalCounter = swarm::aligned<T>;
    std::array<LocalCounter, 2048> lcs_;
};

} // end namespace swarm
