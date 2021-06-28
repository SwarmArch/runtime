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

// This file provides two things:
//  - utilities similar to C++17's std::apply, implemented using only C++11.
//  - utilities functions for calling lambdas.

#pragma once

#include <array>
#include <tuple>

namespace swarm {

// This uses a fairly obscure templating trick to unpack a tuple into an argument sequence
// See http://stackoverflow.com/questions/7858817/unpacking-a-tuple-to-call-a-matching-function-pointer
template<int...> struct seq {};
template<int N, int... S> struct gens : gens<N-1, N-1, S...> {};
template<int...S> struct gens<0, S...>{ typedef seq<S...> type; };

template<typename F, typename FirstArg, typename Tuple, int... S>
inline void callFunc(F f, FirstArg ts, Tuple tup, seq<S...>) {
    f(ts, std::get<S>(tup)...);
}

/* General version with all args in tuple-like container */

template<typename F, typename Tuple, int... S>
inline void callFuncTupleImpl(F f, Tuple tup, seq<S...>) {
    f(std::get<S>(tup)...);
}

template<typename F, typename Tuple>
inline void callFuncTuple(F f, Tuple tup) {
    callFuncTupleImpl(f, tup, typename gens<std::tuple_size<Tuple>::value>::type());
}

/* Version without first argument */
template<int N, int... S> struct gent : gent<N-1, N-1, S...> {};
template<int...S> struct gent<1, S...>{ typedef seq<S...> type; };

template<typename F, typename Tuple>
inline void callFuncTupleIgnoringFirstArg(F f, Tuple tup) {
    callFuncTupleImpl(f, tup, typename gent<std::tuple_size<Tuple>::value>::type());
}

/* Version for lambda enqueues */
template<typename L, typename FirstArg, typename... Args>
inline void callLambdaFunc(FirstArg ts, L lfunc, Args... args) {
    lfunc(ts, args...);
}

/* Version for lambda pointer enqueues
 *
 * Suppose the programmer creates a large FunctionObject, whose arguments do not
 * fit in registers. Rather than making frequent copies of the object, pass a
 * pointer around.
 */
template<typename L, typename FirstArg, typename... Args>
inline void callLambdaPointer(FirstArg ts, L* plfunc, Args... args) {
    (*plfunc)(ts, args...);
}


}

