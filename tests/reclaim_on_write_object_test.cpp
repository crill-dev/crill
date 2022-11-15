// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <crill/reclaim_on_write_object.h>
#include "tests.h"

static_assert(!std::is_copy_constructible_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_move_constructible_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_copy_assignable_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_move_assignable_v<crill::reclaim_on_write_object<int>>);

static_assert(!std::is_copy_constructible_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_move_constructible_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_copy_assignable_v<crill::reclaim_on_write_object<int>>);
static_assert(!std::is_move_assignable_v<crill::reclaim_on_write_object<int>>);

TEST_CASE("reclaim_on_write_object::reclaim_on_write_object()")
{
    struct test_t
    {
        test_t() : i(42) {}
        int i;
    };

    crill::reclaim_on_write_object<test_t> obj;
    auto reader = obj.get_reader();
    CHECK(reader.get_value().i == 42);
}

TEST_CASE("reclaim_on_write_object::reclaim_on_write_object(Args...)")
{
    crill::reclaim_on_write_object<std::string> obj(3, 'x');
    auto reader = obj.get_reader();
    CHECK(reader.get_value() == "xxx");
}

TEST_CASE("reclaim_on_write_object::read_ptr")
{
    crill::reclaim_on_write_object<std::string> obj(3, 'x');
    auto reader = obj.get_reader();

    SUBCASE("read_ptr is not copyable or movable")
    {
        auto read_ptr = reader.read_lock();
        static_assert(!std::is_copy_constructible_v<decltype(read_ptr)>);
        static_assert(!std::is_copy_assignable_v<decltype(read_ptr)>);
        static_assert(!std::is_move_constructible_v<decltype(read_ptr)>);
        static_assert(!std::is_move_assignable_v<decltype(read_ptr)>);
    }

    SUBCASE("Dereference")
    {
        auto read_ptr = reader.read_lock();
        CHECK(*read_ptr == "xxx");
    }

    SUBCASE("Member access operator")
    {
        auto read_ptr = reader.read_lock();
        CHECK(read_ptr->size() == 3);
    }

    SUBCASE("Access is read-only")
    {
        auto read_ptr = reader.read_lock();
        static_assert(std::is_same_v<decltype(read_ptr.operator*()), const std::string&>);
        static_assert(std::is_same_v<decltype(read_ptr.operator->()), const std::string*>);
    }

    SUBCASE("Multiple read_ptrs from same reader are OK as long as lifetimes do not overlap")
    {
        {
            auto read_ptr = reader.read_lock();
        }
        {
            auto read_ptr = reader.read_lock();
            CHECK(*read_ptr == "xxx");
        }
    }
}

TEST_CASE("reclaim_on_write_object::update")
{
    crill::reclaim_on_write_object<std::string> obj("hello");
    auto reader = obj.get_reader();

    SUBCASE("read_ptr obtained after update reads new value")
    {
        obj.update(3, 'x');
        auto read_ptr = reader.read_lock();
        CHECK(*read_ptr == "xxx");
    }

    SUBCASE("read_ptr obtained before update reads old value after update")
    {
        std::atomic<bool> has_read_lock = false;
        std::string read_result;

        std::thread reader_thread([&]{
            auto read_ptr = reader.read_lock();
            has_read_lock = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            read_result = *read_ptr;
        });

        while (!has_read_lock)
            std::this_thread::yield();

        obj.update(3, 'x');
        reader_thread.join();

        CHECK(read_result == "hello");
        CHECK(*obj.get_reader().read_lock() == "xxx");
    }
}

TEST_CASE("reclaim_on_write_object::write_ptr")
{
    struct test_t
    {
        int i = 0, j = 0;
    };

    crill::reclaim_on_write_object<test_t> obj;
    auto reader = obj.get_reader();

    SUBCASE("read_ptr is not copyable or movable")
    {
        auto write_ptr = obj.write_lock();
        static_assert(!std::is_copy_constructible_v<decltype(write_ptr)>);
        static_assert(!std::is_copy_assignable_v<decltype(write_ptr)>);
        static_assert(!std::is_move_constructible_v<decltype(write_ptr)>);
        static_assert(!std::is_move_assignable_v<decltype(write_ptr)>);
    }

    SUBCASE("Modifications do not get published while write_ptr is still alive")
    {
        auto write_ptr = obj.write_lock();
        write_ptr->j = 4;
        CHECK(reader.get_value().j == 0);
    }

    SUBCASE("Modifications get published when write_ptr goes out of scope")
    {
        {
            auto write_ptr = obj.write_lock();
            write_ptr->j = 4;
        }
        CHECK(reader.get_value().j == 4);
    }
}

TEST_CASE("reclaim_on_write_object readers can be created and destroyed concurrently")
{
    crill::reclaim_on_write_object<int> obj(42);
    std::vector<std::thread> reader_threads;
    const std::size_t num_readers = 20;
    std::vector<std::size_t> read_results(num_readers);

    std::atomic<bool> stop = false;
    std::atomic<std::size_t> threads_running = 0;

    for (std::size_t i = 0; i < num_readers; ++i)
    {
        reader_threads.emplace_back([&]{
            auto thread_idx = threads_running.fetch_add(1);
            while (!stop)
                read_results[thread_idx] = obj.get_reader().get_value();
        });
    }

    while (threads_running.load() < num_readers)
        std::this_thread::yield();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;
    for (auto& thread : reader_threads)
        thread.join();

    for (auto value : read_results)
        CHECK(value == 42);
}

TEST_CASE("reclaim_on_write_object writers can make progress even if there is always a reader holding a lock")
{
    crill::reclaim_on_write_object<int> obj(42);

    std::atomic<bool> reader_1_started = false;
    std::atomic<bool> reader_2_started = false;
    std::atomic<bool> reader_1_should_release = false;
    std::atomic<bool> reader_2_should_release = false;
    std::atomic<bool> stop = false;

    std::thread reader_1([&]{
        auto reader = obj.get_reader();
        while (true)
        {
            auto read_ptr = reader.read_lock();
            reader_1_should_release = false;
            reader_2_should_release = true;
            while (!reader_1_should_release)
            {
                if (stop) return;
                std::this_thread::yield();
            }
        }
    });

    std::thread reader_2([&]{
        auto reader = obj.get_reader();
        while (true)
        {
            auto read_ptr = reader.read_lock();
            reader_2_should_release = false;
            reader_1_should_release = true;
            while (!reader_2_should_release)
            {
                if (stop) return;
                std::this_thread::yield();
            }
        }
    });

    obj.update(43); // this will not deadlock if implemented correctly

    stop = true;
    reader_1.join();
    reader_2.join();

    CHECK(obj.get_reader().get_value() == 43);
}