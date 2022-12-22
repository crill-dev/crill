// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <atomic>
#include <thread>
#include <crill/progressive_backoff_wait.h>
#include <doctest/doctest.h>

TEST_CASE("Waiting on a true predicate immediately returns")
{
    crill::progressive_backoff_wait([]{ return true; });
}

TEST_CASE("Waiting on a false predicate blocks until predicate becomes true")
{
    std::atomic<bool> flag = false;
    std::atomic<bool> waiter_thread_running = false;
    std::atomic<bool> waiter_thread_done = false;
    std::thread waiter_thread([&]{
        waiter_thread_running = true;
        crill::progressive_backoff_wait([&]{ return flag == true; });
        waiter_thread_done = true;
    });

    while (!waiter_thread_running)
        /* wait for thread to start*/;

    REQUIRE_FALSE(waiter_thread_done);

    flag = true;
    waiter_thread.join();
    REQUIRE(waiter_thread_done);
}
