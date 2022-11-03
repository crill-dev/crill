// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright(c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
//(See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <crill/spin_mutex.h>
#include <doctest/doctest.h>

TEST_CASE("crill::spin_mutex")
{
    static_assert(std::is_default_constructible_v<crill::spin_mutex>);
    static_assert(!std::is_copy_constructible_v<crill::spin_mutex>);
    static_assert(!std::is_copy_assignable_v<crill::spin_mutex>);
    static_assert(!std::is_move_constructible_v<crill::spin_mutex>);
    static_assert(!std::is_move_assignable_v<crill::spin_mutex>);

    crill::spin_mutex mtx;

    SUBCASE("If mutex is not locked, try_lock succeeds")
    {
        REQUIRE(mtx.try_lock());
        mtx.unlock();
    }

    SUBCASE("If mutex is locked, try_lock fails")
    {
        mtx.lock();
        REQUIRE_FALSE(mtx.try_lock());
        mtx.unlock();
    }

    SUBCASE("Works with std::scoped_lock")
    {
        {
            std::scoped_lock lock(mtx);
            CHECK_FALSE(mtx.try_lock());
        }

        CHECK(mtx.try_lock());
        mtx.unlock();
    }

    SUBCASE("Works with std::unique_lock")
    {
        std::unique_lock lock(mtx, std::try_to_lock);
        CHECK(lock.owns_lock());
        CHECK_FALSE(mtx.try_lock());

        lock.unlock();
        CHECK_FALSE(lock.owns_lock());
        CHECK(mtx.try_lock());
        mtx.unlock();
    }

    SUBCASE("If mutex is held by other thread, lock succeeds after mutex is released")
    {
        std::atomic<bool> stop_other_thread = false;
        std::atomic<bool> held_by_other_thread = false;

        std::thread other_thread([&] {
            std::unique_lock lock(mtx);
            held_by_other_thread = true;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lock.unlock();
            while (!stop_other_thread)
                /* wait to be stopped */;
        });

        while (!held_by_other_thread)
            /* wait for other_thread to lock mtx */;

        std::unique_lock lock(mtx);
        CHECK(lock.owns_lock());

        stop_other_thread = true;
        other_thread.join();
    }

    SUBCASE("If mutex is held by other thread, try_lock fails")
    {
        std::atomic<bool> stop_other_thread = false;
        std::atomic<bool> held_by_other_thread = false;

        std::thread other_thread([&] {
            std::scoped_lock lock(mtx);
            held_by_other_thread = true;

            while (!stop_other_thread)
                /* wait to be stopped */;
        });

        while (!held_by_other_thread)
            /* wait for other_thread to lock mtx */;

        std::unique_lock lock(mtx, std::try_to_lock);
        CHECK_FALSE(lock.owns_lock());

        stop_other_thread = true;
        other_thread.join();
    }

    // TODO: add test where many threads are poking the mutex simultaneously, and run with threadsan
}
