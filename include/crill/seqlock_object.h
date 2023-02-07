// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_SEQLOCK_OBJECT_H
#define CRILL_SEQLOCK_OBJECT_H

#include <atomic>

namespace crill {

template <typename T>
class seqlock_object
{
public:
    static_assert(std::is_trivially_copyable_v<T>);

    seqlock_object()
    {
        store_all_zeroes();
    }

    seqlock_object(T t)
    {
        store(t);
    }

    T load() const noexcept
    {
        T t;
        while (!try_load(t))
            /* keep trying */;

        return t;
    }

    bool try_load(T& t) const noexcept
    {
        std::size_t buffer[buffer_size];

        std::size_t seq1 = seq.load(std::memory_order_acquire);
        if (seq1 % 2 != 0)
            return false;

        for (std::size_t i = 0; i < buffer_size; ++i)
            buffer[i] = data[i].load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);

        std::size_t seq2 = seq.load(std::memory_order_relaxed);
        if (seq1 != seq2)
            return false;

        std::memcpy(&t, buffer, sizeof(T));
        return true;
    }

    void store(T t) noexcept
    {
        std::size_t buffer[buffer_size];
        if constexpr (sizeof(T) % sizeof(std::size_t) != 0)
            buffer[buffer_size - 1] = 0;

        std::memcpy(&buffer, &t, sizeof(T));

        std::size_t old_seq = seq.load(std::memory_order_relaxed);
        seq.store(old_seq + 1, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_release);

        for (std::size_t i = 0; i < buffer_size; ++i)
            data[i].store(buffer[i], std::memory_order_relaxed);

        seq.store(old_seq + 2, std::memory_order_release);
    }

private:
    void store_all_zeroes()
    {
        for (std::size_t i = 0; i < buffer_size; ++i)
            data[i].store(0, std::memory_order_relaxed);
    }

    static constexpr std::size_t buffer_size = (sizeof(T) + sizeof(std::size_t) - 1) / sizeof(std::size_t);
    static constexpr std::size_t buffer_size_bytes = buffer_size * sizeof(std::size_t);

    std::atomic<std::size_t> data[buffer_size];
    std::atomic<std::size_t> seq = 0;

    static_assert(decltype(seq)::is_always_lock_free);
};

} // namespace crill

#endif //CRILL_SEQLOCK_OBJECT_H
