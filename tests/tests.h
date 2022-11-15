// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_TESTS_H
#define CRILL_TESTS_H

#include <thread>
#include <doctest/doctest.h>

namespace crill::test
{
    // Helper to track constructor and destructor calls
    struct counted_t
    {
        static void reset() { instances_created = 0; instances_alive = 0; }
        inline static std::size_t instances_created;
        inline static std::size_t instances_alive;

        std::size_t index;
        counted_t() : index(instances_created++) { ++instances_alive; }
        ~counted_t() { --instances_alive; }

        counted_t(const counted_t&) = delete;
        counted_t(counted_t&&) = delete;
        counted_t& operator=(const counted_t&) = delete;
        counted_t& operator=(counted_t&&) = delete;
    };
}

#endif //CRILL_TESTS_H
