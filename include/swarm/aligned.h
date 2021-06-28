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

#ifndef SWARM_CACHE_LINE
// Indicates the number of bytes in a cache line,
// which is the size at which shared writeable data should be aligned.
#define SWARM_CACHE_LINE 64
#endif

namespace swarm {

// Regarding naming, this class is used as
//   swarm::aligned<int> or swarm::aligned<bool>
// in effect imitating the signature of C++11's
//   std::atomic<int> or std::atomic<bool>
// FIXME [mcj] Suvinay had some trouble using vector<aligned<T> >::resize, so
// I want to document some sources of insight on using aligned types with
// std::vector. I'm not sure exactly what's happening, but the allocator may
// fail at re-aligning the data? I need to read these more thoroughly
// * MS Visual C++ std::vector doesn't play well with alignment, but gcc may be
//   fine: http://ofekshilon.com/2010/05/05/stdvector-of-aligned-elements/
// * C++11 introduced std::aligned_storage which seems to be a platform-agnostic
//   allocation of aligned data types. It doesn't wrap the operators of an int,
//   as we have below http://en.cppreference.com/w/cpp/types/aligned_storage
//
// dsm: This seems like a very error-prone of achieving the same effect as
// __attribute__ aligned. See the simulator's pad.h.
template<typename T>
class alignas(SWARM_CACHE_LINE) aligned {
    T val_;
public:
    aligned(const T & val) : val_(val) { }
    aligned() : aligned(0) { }

    operator T() const { return val_; }

    aligned & operator=(const T & rhs) {
        val_ = rhs;
        return *this;
    }

    aligned & operator=(const aligned<T> & rhs) {
        return operator=(rhs.val_);
    }

    aligned & operator+=(const T & rhval) {
        val_ += rhval;
        return *this;
    }

    aligned & operator+=(const aligned<T> & rhs) {
        return operator+=(rhs.val_);
    }

    aligned & operator-=(const T & rhval) {
        val_ -= rhval;
        return *this;
    }

    aligned & operator-=(const aligned<T> & rhs) {
        return operator-=(rhs.val_);
    }

    // Prefix operators
    aligned & operator++() {
        ++val_;
        return *this;
    }

     aligned & operator--() {
        --val_;
        return *this;
    }

    // Postfix operators
    aligned  operator++(int/*dummy*/) {
        aligned<T> result (val_);
        ++(*this);
        return result;
    }

    aligned  operator--(int/*dummy*/) {
        aligned<T> result (val_);
        --(*this);
        return result;
    }

    bool operator<(const T & rhval) const { return val_ < rhval; }
    bool operator<=(const T & rhval) const { return val_ <= rhval; }
    bool operator>(const T & rhval) const { return val_ > rhval; }
    bool operator>=(const T & rhval) const { return val_ >= rhval; }
    bool operator==(const T & rhval) const { return val_ == rhval; }
    bool operator!=(const T & rhval) const { return !operator==(rhval); }

    bool operator<(const aligned<T> & rhs) const { return operator<(rhs.val_); }
    bool operator<=(const aligned<T> & rhs) const { return operator<=(rhs.val_); }
    bool operator>(const aligned<T> & rhs) const { return operator>(rhs.val_); }
    bool operator>=(const aligned<T> & rhs) const { return operator>=(rhs.val_); }
    bool operator==(const aligned<T> & rhs) const { return operator==(rhs.val_); }
    bool operator!=(const aligned<T> & rhs) const { return operator!=(rhs.val_); }
};


} // end namespace swarm
