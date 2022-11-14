// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <vector>
#include <thread>
#include <crill/utility.h>
#include "tests.h"

TEST_CASE("call_once")
{
    struct test_t
    {
        test_t(std::atomic<std::size_t>& counter)
        {
            crill::call_once([&] { counter.fetch_add(1); });
        }
    };

    std::atomic<std::size_t> counter = 0;

    std::vector<std::thread> threads;
    std::size_t num_threads = 8;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&]{
            test_t t1(counter);
            test_t t2(counter);
            test_t t3(counter);
        });
    }

    for (auto& thread : threads)
        thread.join();

    CHECK(counter.load() == 1);
}

TEST_CASE("call_once_per_thread")
{
    struct test_t
    {
        test_t(std::atomic<std::size_t>& counter)
        {
            crill::call_once_per_thread([&] { ++counter; });
        }
    };

    std::atomic<std::size_t> counter = 0;

    std::vector<std::thread> threads;
    std::size_t num_threads = 8;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&]{
            test_t t1(counter);
            test_t t2(counter);
            test_t t3(counter);
        });
    }

    for (auto& thread : threads)
        thread.join();

    CHECK(counter.load() == num_threads);
}
