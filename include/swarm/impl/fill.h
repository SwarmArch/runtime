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

#include <cstdint>
#include <algorithm>
#include "../algorithm.h"
#include "../api.h"
#include "block.h"

namespace swarm {


template <class Iterator, class T>
static inline void __writer(Timestamp, Iterator first, Iterator last, const T& value) {
    std::fill(first, last, value);
}


// TODO(mcj) add support for hints
// FIXME(mcj) this is dangerously assuming use of ContiguousIterators
// (http://en.cppreference.com/w/cpp/iterator), but doesn't actually check which
// type of iterator is used.
template <uint8_t BlockSize, EnqFlags Flags, class Iterator, typename T>
static inline void fill_impl(Timestamp ts, Iterator first, Iterator last, const T& value) {
  constexpr unsigned grainSize = swarm::block::elementsPerGrain<BlockSize, Iterator>();
  constexpr size_t elemSize = sizeof(*first);
  constexpr size_t grainBytes = grainSize * elemSize;

  uintptr_t firstAddr = reinterpret_cast<uintptr_t>(&(*first));
  uintptr_t lastAddr = reinterpret_cast<uintptr_t>(&(*last));

  static_assert((grainBytes % SWARM_CACHE_LINE) == 0,
                "We assume grains are a whole number of cache lines.");
  static_assert((grainBytes & (grainBytes - 1)) == 0,
                "We assume cache-line-aligned grains have power-of-2 size.");
  uintptr_t firstAddrRoundedUp =
      (firstAddr + grainBytes - 1) & (~(grainBytes - 1ul));

  // If the range falls within a single grain, no point spawning more tasks
  if (firstAddrRoundedUp >= lastAddr) {
      std::fill(first, last, value);
      return;
  }

  // Start by filling the start of the range up to the first grain boundary
  size_t firstBytes = firstAddrRoundedUp - firstAddr;
  assert(firstBytes % elemSize == 0);
  Iterator firstRoundedUp = first + (firstBytes / elemSize);
  swarm::enqueue((__writer<Iterator, T>), ts, Flags, first, firstRoundedUp, value);

  // Now, spawn tasks to fill each grain in the rest of the range
  // [firstRoundedUp, last)

  uint64_t grains = (last - firstRoundedUp) / grainSize;
  // TODO(victory): A strided version of boost::counting_iterator would let us
  //                avoid the need to capture firstRoundedUp and thereby save a
  //                register arg for the enqueuer tasks.
  // HACK: For now, use 32-bit integer for the grain counter to enable the
  //       enqueuer task regTupleRunner to pack things into 3 64-bit registers.
  //       This is safe as long as nobody tries to fill a terabyte-sized array.
  assert(grains < UINT32_MAX);
  swarm::enqueue_all<Flags, swarm::max_children - 2>(
      u32it(0), u32it(grains),
      [firstRoundedUp, value](Timestamp ts, uint64_t i) {
          swarm::enqueue((__writer<Iterator, T>), ts, Flags,
                         firstRoundedUp + (i * grainSize),
                         firstRoundedUp + ((i + 1) * grainSize),
                         value);
      },
      ts);

  Iterator lastRoundedDown = firstRoundedUp + (grains * grainSize);
  assert(lastRoundedDown <= last);
  if (lastRoundedDown < last)
      swarm::enqueue((__writer<Iterator, T>), ts, Flags, lastRoundedDown, last, value);
}


// TODO(mcj) add support for hints
template <EnqFlags Flags, class Iterator, typename T>
static inline void fill(Iterator first, Iterator last, const T& value,
        Timestamp ts) {
    static_assert(!(Flags & EnqFlags::SAMETASK), "SAMETASK is invalid here");
    static_assert(!(Flags & EnqFlags::NOHASH), "NOHASH is a bad idea here");

    uint32_t blockSize = swarm::block::grainSize(first, last);
    switch(blockSize) {
        case 1:
        {
            swarm::enqueue((fill_impl<1u, Flags, Iterator, T>),
                    ts, Flags, first, last, value);
            break;
        }
        case 2:
        {
            swarm::enqueue((fill_impl<2u, Flags, Iterator, T>),
                    ts, Flags, first, last, value);
            break;
        }
        case 4:
        {
            swarm::enqueue((fill_impl<4u, Flags, Iterator, T>),
                    ts, Flags, first, last, value);
            break;
        }
        case 8:
        {
            swarm::enqueue((fill_impl<8u, Flags, Iterator, T>),
                    ts, Flags, first, last, value);
            break;
        }
        default:
        {
            swarm::enqueue((fill_impl<16u, Flags, Iterator, T>),
                    ts, Flags, first, last, value);
            break;
        }
    }
}

}
