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

//------------------------------------------------------------------------------
// This file defines some helpful type traits that aren't available until C++17
// as well as some lambda utilities
//------------------------------------------------------------------------------
#pragma once

#include <type_traits>

namespace swarm {

// disjunction (OR), conjunction (AND), and negation
// are not available in namespace std until C++17

template <class...> struct disjunction : std::false_type {};
template <class B1> struct disjunction<B1> : B1 {};
template <class B1, class... Bn>
struct disjunction <B1, Bn...>
    : std::conditional_t<bool(B1::value), B1, disjunction<Bn...>> {};

template <class...> struct conjunction : std::true_type {};
template <class B1> struct conjunction<B1> : B1 {};
template <class B1, class... Bn>
struct conjunction <B1, Bn...>
    : std::conditional_t<bool(B1::value), conjunction<Bn...>, B1> {};

template<class B>
struct negation : std::integral_constant<bool, !bool(B::value)> {};


// Test whether a lambda type is stateless: https://stackoverflow.com/a/19962398
template <typename T, typename U>
struct is_stateless_help : is_stateless_help<T, decltype(&U::operator())> {};
template <typename T, typename C, typename R, typename... A>
struct is_stateless_help<T, R(C::*)(A...) const> {
    static const bool value = std::is_convertible<T, R(*)(A...)>::value;
};
template <typename T>
struct is_stateless {
    static const bool value = is_stateless_help<T,T>::value;
};


// Get a callable (stateless) lambda object from a stateless lambda type
// TODO(mcj) generalize beyond 0, 1, 2 arguments
template <typename T>
struct make_lambda : make_lambda<decltype(&T::operator())> {
    static_assert(is_stateless<T>::value,
                  "Only convert a stateless lambda type to a lambda");
};
template <typename C, typename R, typename... A>
struct make_lambda<R(C::*)(A...) const>;
template <typename C, typename R>
struct make_lambda<R(C::*)(void) const> {
    constexpr auto operator()() {
        return [] () -> R { return ((C*)nullptr)->operator()(); };
    }
};
template <typename C, typename R, typename A1>
struct make_lambda<R(C::*)(A1) const> {
    constexpr auto operator()() {
        return [] (A1 a1) -> R { return ((C*)nullptr)->operator()(a1); };
    }
};
template <typename C, typename R, typename A1, typename A2>
struct make_lambda<R(C::*)(A1, A2) const> {
    constexpr auto operator()() {
        return [] (A1 a1, A2 a2) -> R {
            return ((C*)nullptr)->operator()(a1, a2);
        };
    }
};


} // end namespace swarm
