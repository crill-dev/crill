// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <crill/reclaim_object.h>
#include "tests.h"

// TODO: move this elsewhere
std::size_t crill::test::counted_t::instances_created = 0;
std::size_t crill::test::counted_t::instances_alive = 0;

static_assert(!std::is_copy_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_constructible_v<crill::reclaim_object<int>>);
static_assert(!std::is_copy_assignable_v<crill::reclaim_object<int>>);
static_assert(!std::is_move_assignable_v<crill::reclaim_object<int>>);

TEST_CASE("rcu_object default constructor")
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

TEST_CASE("rcu_object emplace constructor")
{
    crill::reclaim_object<std::string> obj(3, 'x');
    auto reader = obj.get_reader();
    CHECK(reader.get_value() == "xxx");
}

TEST_CASE("Access rcu_object value via rcu_reader::handle")
{
    crill::reclaim_object<std::string> obj(3, 'x');
    auto reader = obj.get_reader();
    auto handle = reader.read_lock();

    SUBCASE("Dereference")
    {
        CHECK(*handle == "xxx");
    }

    SUBCASE("Member access operator")
    {
        CHECK(handle->size() == 3);
    }

    SUBCASE("Access is read-only")
    {
        static_assert(std::is_same_v<decltype(handle.operator*()), const std::string&>);
        static_assert(std::is_same_v<decltype(handle.operator->()), const std::string*>);
    }
}

TEST_CASE("Update rcu_object")
{
    crill::reclaim_object<std::string> obj("hello");
    auto reader = obj.get_reader();

    SUBCASE("read handle obtained before update reads old value after update")
    {
        auto handle = reader.read_lock();
        obj.update(3, 'x');
        CHECK(*handle == "hello");
    }

    SUBCASE("read handle obtained after update reads new value")
    {
        obj.update(3, 'x');
        auto handle = reader.read_lock();
        CHECK(*handle == "xxx");
    }
}

TEST_CASE("Modify rcu_object via rcu_writer::handle")
{
    struct test_t
    {
        int i = 0, j = 0;
    };

    crill::reclaim_object<test_t> obj;
    auto reader = obj.get_reader();

    SUBCASE("Modifications do not get published while write_ptr is still alive")
    {
        auto handle = obj.write_lock();
        handle->j = 4;
        CHECK(reader.get_value().j == 0);
    }

    SUBCASE("Modifications get published when write_ptr goes out of scope")
    {
        {
            auto handle = obj.write_lock();
            handle->j = 4;
        }
        CHECK(reader.get_value().j == 4);
    }
}

TEST_CASE("rcu_object reclamation")
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

TEST_CASE("Readers can be created and destroyed concurrently")
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

TEST_CASE("Reads, write, and reclaim can all be executed concurrently")
{
    crill::reclaim_object<std::string> obj("0");
    std::vector<std::thread> reader_threads;
    const std::size_t num_readers = 20;
    std::vector<std::string> read_results(num_readers);

    std::atomic<bool> stop = false;
    std::atomic<std::size_t> threads_running = 0;

    for (std::size_t i = 0; i < num_readers; ++i)
    {
        reader_threads.emplace_back([&]{
            auto reader = obj.get_reader();
            std::string value;

            auto thread_idx = threads_running.fetch_add(1);
            while (!stop)
                read_results[thread_idx] = *reader.read_lock();
        });
    }

    std::vector<std::thread> writer_threads;
    const std::size_t num_writers = 4;
    for (std::size_t i = 0; i < num_writers; ++i)
    {
        reader_threads.emplace_back([&]{
            threads_running.fetch_add(1);

            while (!stop)
                for (int i = 0; i < 10000; ++i)
                    obj.update(std::to_string(i));
        });
    }

    auto garbage_collector = std::thread([&]{
        threads_running.fetch_add(1);
        while (!stop) {
            obj.reclaim();
        }
    });

    while (threads_running.load() < num_readers + num_writers + 1)
        std::this_thread::yield();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop = true;

    for (auto& thread : reader_threads)
        thread.join();

    for (auto& thread : writer_threads)
        thread.join();

    garbage_collector.join();

    CHECK(obj.get_reader().get_value() == "9999");
    for (const auto& value : read_results)
        CHECK(value.size() > 0);
}
