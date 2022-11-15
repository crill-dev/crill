// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_UTILITY_H
#define CRILL_UTILITY_H

namespace crill
{

template <typename F>
void call_once(F&& f) noexcept(noexcept(f()))
{
    static bool _ = [&]{ f(); return true; }();
}

template <typename F>
void call_once_per_thread(F&& f) noexcept(noexcept(f()))
{
    static thread_local bool _ = [&]{ f(); return true; }();
}

} // namespace crill

#endif //CRILL_UTILITY_H
