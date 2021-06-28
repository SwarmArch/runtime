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

// A pared-down imitation of boost::dynamic_bitset that is friendlier to
// Swarm algorithms (e.g. support for spatial hints and parallel fill)
#pragma once

#include "api.h"
#include "algorithm.h"

namespace swarm {

class bitset {
  public:
    bitset() : length(0ul), blocks(nullptr) {}
    ~bitset() { if (blocks) delete [] blocks; }

    inline bool test(uint64_t pos) const;
    inline void set(uint64_t pos, bool value);
    inline void reset(uint64_t pos) { set(pos, false); }

    inline size_t size() const { return length; }
    void resize(size_t l, bool value);

    // Swarm-style interface

    // Asynchronously resize the bitset at the given timestamp
    template <EnqFlags Flags = EnqFlags::NOHINT>
    void resize(size_t l, bool value, swarm::Timestamp ts);

    // The cache-line hint if you intend to read/write bit pos
    inline uint64_t hint(uint64_t pos) const;

  private:
    inline uint64_t numBlocks() const;

    uint64_t length;
    uint64_t* blocks;
    static constexpr uint64_t BLOCK_SIZE = 64ul;
};

} // end namespace swarm


bool swarm::bitset::test(uint64_t pos) const {
    uint64_t block = pos / BLOCK_SIZE;
    uint64_t bit = pos % BLOCK_SIZE;
    return (blocks[block] >> bit) & 1ul;
}


void swarm::bitset::set(uint64_t pos, bool value) {
    uint64_t block = pos / BLOCK_SIZE;
    uint64_t bit = pos % BLOCK_SIZE;
    if (value) blocks[block] |= (1ul << bit);
    else blocks[block] &= ~(1ul << bit);
}


void swarm::bitset::resize(size_t l, bool value) {
    if (length > 0ul) {
        swarm::info("Resize of existing data not yet supported");
        std::abort();
    }
    length = l;
    if (blocks) delete [] blocks;
    blocks = new uint64_t[numBlocks()];
    uint64_t mask = value ? ~(0ul) : 0ul;
    std::fill(blocks, blocks + numBlocks(), mask);
}


template <EnqFlags Flags>
void swarm::bitset::resize(size_t l, bool value, swarm::Timestamp ts) {
    uint64_t mask = value ? ~(0ul) : 0ul;
    swarm::enqueueLambda([this, l, mask] (swarm::Timestamp ts) {
        if (length > 0ul) {
            swarm::info("Resize of existing data not yet supported");
            std::abort();
        }
        length = l;
        if (blocks) delete [] blocks;
        blocks = new uint64_t[numBlocks()];
        swarm::fill<Flags>(blocks, blocks + numBlocks(), mask, ts);
    }, ts, Flags);
}


uint64_t swarm::bitset::hint(uint64_t pos) const {
    uint64_t block = pos / BLOCK_SIZE;
    return swarm::Hint::cacheLine(&blocks[block]);
}


uint64_t swarm::bitset::numBlocks() const {
    return (length + BLOCK_SIZE - 1) / BLOCK_SIZE;
}
