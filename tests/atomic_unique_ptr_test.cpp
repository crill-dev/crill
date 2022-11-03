// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <vector>
#include <thread>
#include <crill/atomic_unique_ptr.h>
#include "tests.h"

TEST_CASE("Default constructor")
{
    crill::atomic_unique_ptr<int> auptr;
    CHECK(auptr.get() == nullptr);
}

TEST_CASE("Pointer constructor")
{
    auto uptr = std::make_unique<int>();
    int* ptr = uptr.get();

    crill::atomic_unique_ptr<int> auptr(std::move(uptr));
    CHECK(auptr.get() == ptr);
    CHECK(uptr == nullptr);
}

TEST_CASE("Atomic exchange")
{
    auto uptr1 = std::make_unique<int>();
    int* ptr1 = uptr1.get();

    auto uptr2 = std::make_unique<int>();
    int* ptr2 = uptr2.get();

    crill::atomic_unique_ptr<int> auptr(std::move(uptr1));
    auto uptr3 = auptr.exchange(std::move(uptr2));
    CHECK(auptr.get() == ptr2);
    CHECK(uptr3.get() == ptr1);
}

TEST_CASE("Atomic exchange from multiple threads")
{
    crill::atomic_unique_ptr<int> auptr(std::make_unique<int>());

    std::vector<std::thread> threads;
    const std::size_t num_threads = 20;
    std::atomic<bool> stop = false;
    std::atomic<std::size_t> counter = 0;
    std::atomic<std::size_t> threads_running = 0;

    for (std::size_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&] {
            threads_running.fetch_add(1);
            while (!stop)
                for (int i = 0; i < 10000; ++i)
                    counter.fetch_add(*auptr.exchange(std::make_unique<int>(i)).get());
        });
    }

    while (threads_running < num_threads)
        std::this_thread::yield();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    stop.store(true);
    for (auto& thread : threads)
        thread.join();
}

TEST_CASE("Destructor deletes managed object")
{
    std::size_t dtor_counter = 0;
    struct test_t
    {
        test_t(std::size_t& dtor_counter) : dtor_counter(dtor_counter) {}
        ~test_t() { ++dtor_counter; }
        std::size_t& dtor_counter;
    };

    auto uptr = std::make_unique<test_t>(dtor_counter);

    {
        crill::atomic_unique_ptr<test_t> auptr(std::move(uptr));
        CHECK(dtor_counter == 0);
    }
    CHECK(dtor_counter == 1);
}
