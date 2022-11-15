// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <crill/reclaim_object.h>
#include <crill/utility.h>
#include "tests.h"

static_assert(!std::is_copy_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_copy_assignable_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_assignable_v<crill::reclaim_object<int>>);

static_assert(!std::is_copy_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_copy_assignable_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_assignable_v<crill::reclaim_object<int>>);

TEST_CASE("reclaim_object::reclaim_object()")
{
    struct test_t
    {
        test_t() : i(42) {}
        int i;
    };

    crill::reclaim_object<test_t> obj;
    auto reader = obj.get_reader();
    CHECK(reader.get_value().i == 42);
}

TEST_CASE("reclaim_object::reclaim_object(Args...)")
{
    crill::reclaim_object<std::string> obj(3, 'x');
    auto reader = obj.get_reader();
    CHECK(reader.get_value() == "xxx");
}

TEST_CASE("reclaim_object::read_ptr")
{
    crill::reclaim_object<std::string> obj(3, 'x');
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

TEST_CASE("reclaim_object::update")
{
    crill::reclaim_object<std::string> obj("hello");
    auto reader = obj.get_reader();

    SUBCASE("read read_ptr obtained before update reads old value after update")
    {
        auto read_ptr = reader.read_lock();
        obj.update(3, 'x');
        CHECK(*read_ptr == "hello");
    }

    SUBCASE("read read_ptr obtained after update reads new value")
    {
        obj.update(3, 'x');
        auto read_ptr = reader.read_lock();
        CHECK(*read_ptr == "xxx");
    }
}

TEST_CASE("reclaim_object::write_ptr")
{
    struct test_t
    {
        int i = 0, j = 0;
    };

    crill::reclaim_object<test_t> obj;
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

TEST_CASE("reclaim_object::reclaim")
{
    using namespace crill::test;

    counted_t::reset();
    crill::reclaim_object<counted_t> obj;

    CHECK(counted_t::instances_created == 1);
    CHECK(counted_t::instances_alive == 1);
    CHECK(obj.get_reader().read_lock()->index == 0);

    SUBCASE("No reclamation without call to reclaim()")
    {
        obj.update();
        obj.update();
        CHECK(counted_t::instances_created == 3);
        CHECK(counted_t::instances_alive == 3);
        CHECK(obj.get_reader().read_lock()->index == 2);
    }

    SUBCASE("reclaim() reclaims retired objects")
    {
        obj.update();
        obj.update();

        obj.reclaim();
        CHECK(counted_t::instances_created == 3);
        CHECK(counted_t::instances_alive == 1);
        CHECK(obj.get_reader().read_lock()->index == 2);
    }

    SUBCASE("reclaim() reclaims retired objects if there is an old reader, as long as there is no active read_ptr")
    {
        auto reader = obj.get_reader();
        obj.update();
        obj.update();

        obj.reclaim();
        CHECK(counted_t::instances_created == 3);
        CHECK(counted_t::instances_alive == 1);
        CHECK(obj.get_reader().read_lock()->index == 2);
    }

    SUBCASE("reclaim() does not reclaim retired objects if there is an old read_ptr")
    {
        auto reader = obj.get_reader();
        auto read_ptr = reader.read_lock();
        obj.update();
        obj.update();

        obj.reclaim();
        CHECK(counted_t::instances_created == 3);
        CHECK(counted_t::instances_alive == 3);
        CHECK(obj.get_reader().read_lock()->index == 2);
    }
}

TEST_CASE("reclaim_object reader does not block writer")
{
    crill::reclaim_object<int> obj(42);

    std::atomic<bool> has_read_lock = false;
    std::atomic<bool> start_writer = false;
    std::atomic<bool> give_up_read_lock = false;
    std::atomic<bool> obj_updated = false;

    std::thread reader_thread([&]{
        auto reader = obj.get_reader();
        auto read_ptr = reader.read_lock();

        has_read_lock = true;
        start_writer = true;

        while (!give_up_read_lock)
            std::this_thread::yield();

        CHECK(obj_updated);
        CHECK(*read_ptr == 42); // must still read old value here!
    });

    std::thread writer_thread([&]{
        while (!start_writer)
            std::this_thread::yield();

        obj.update(43); // will reach this line while read_lock is held
        obj_updated = true;
    });

    while (!has_read_lock)
        std::this_thread::yield();

    while (!obj_updated)
        std::this_thread::yield();

    give_up_read_lock = true;
    reader_thread.join();
    writer_thread.join();
}

TEST_CASE("reclaim_object readers can be created and destroyed concurrently")
{
    crill::reclaim_object<int> obj(42);
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

TEST_CASE("reclaim_object reads, writes, and reclaim can all run concurrently")
{
    crill::reclaim_object<std::string> obj("0");
    std::vector<std::thread> reader_threads;
    const std::size_t num_readers = 5;
    std::vector<std::string> read_results(num_readers);

    std::atomic<bool> stop = false;
    std::atomic<std::size_t> readers_started = 0;
    std::atomic<std::size_t> writers_started = 0;
    std::atomic<bool> garbage_collector_started = false;

    for (std::size_t i = 0; i < num_readers; ++i)
    {
        reader_threads.emplace_back([&]{
            auto reader = obj.get_reader();
            std::string value;
            std::size_t thread_idx = readers_started;

            while (!stop)
            {
                read_results[thread_idx] = *reader.read_lock();
                crill::call_once_per_thread([&] { ++readers_started; });
            }
        });
    }

    std::vector<std::thread> writer_threads;
    const std::size_t num_writers = 2;
    for (std::size_t i = 0; i < num_writers; ++i)
    {
        reader_threads.emplace_back([&]{
            while (!stop)
            {
                for (int i = 0; i < 1000; ++i)
                    obj.update(std::to_string(i));

                crill::call_once_per_thread([&] { ++writers_started; });
            }
        });
    }

    auto garbage_collector = std::thread([&]{
        garbage_collector_started = true;
        while (!stop) {
            obj.reclaim();
        }
    });

    while (readers_started < num_readers)
        std::this_thread::yield();

    while (writers_started < num_writers)
        std::this_thread::yield();

    while (!garbage_collector_started)
        std::this_thread::yield();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    for (auto& thread : reader_threads)
        thread.join();

    for (auto& thread : writer_threads)
        thread.join();

    garbage_collector.join();

    // every reader read some values that were written by writers:
    for (const auto& value : read_results)
        CHECK(value.size() > 0);

    // value is the last value written:
    CHECK(obj.get_reader().get_value() == "999");
}
