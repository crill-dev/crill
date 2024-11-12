// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#include <crill/bytewise_atomic_memcpy.h>
#include <doctest/doctest.h>

TEST_CASE("atomic_load_per_byte_memcpy with nullptrs")
{
    REQUIRE(crill::atomic_load_per_byte_memcpy(nullptr, nullptr, 0, std::memory_order_relaxed) == nullptr);
}

TEST_CASE("atomic_store_per_byte_memcpy with nullptrs")
{
    REQUIRE(crill::atomic_store_per_byte_memcpy(nullptr, nullptr, 0, std::memory_order_relaxed) == nullptr);
}

struct TestData {
    double x, y, z;
    bool operator==(const TestData& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

TEST_CASE("atomic_load_per_byte_memcpy with struct")
{
    TestData td1 = { 1, 2, 3 };
    TestData td2 = { 3, 4, 5 };
    REQUIRE(crill::atomic_load_per_byte_memcpy(&td2, &td1, sizeof(TestData), std::memory_order_relaxed) == &td2);
    REQUIRE(td1 == td2);
}

TEST_CASE("atomic_store_per_byte_memcpy with struct")
{
    TestData td1 = { 1, 2, 3 };
    TestData td2 = { 3, 4, 5 };
    REQUIRE(crill::atomic_store_per_byte_memcpy(&td2, &td1, sizeof(TestData), std::memory_order_relaxed) == &td2);
    REQUIRE(td1 == td2);
}

// TODO: More thorough tests