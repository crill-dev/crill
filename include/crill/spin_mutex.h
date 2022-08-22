// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_SPIN_MUTEX_H
#define CRILL_SPIN_MUTEX_H

#include <crill/platform.h>
#include <atomic>
#include <thread>
#include <mutex>

#if CRILL_INTEL
  #include <emmintrin.h>
#elif CRILL_ARM_64BIT
  #include <arm_acle.h>
#endif

namespace crill
{

// crill::spin_mutex is a spinlock with progressive backoff for safely and
// efficiently synchronising a real-time thread with other threads.
//
// crill::spin_mutex meets the standard C++ requirements for mutex
// [thread.req.lockable.req] and can therefore be used with std::scoped_lock
// and std::unique_lock as a drop-in replacement for std::mutex.
//
// try_lock() and unlock() are implemented by setting a std::atomic_flag and
// are therefore always wait-free. This is the main difference to a std::mutex
// which doesn't have a wait-free unlock() as std::mutex::unlock() may perform
// a system call to wake up a waiting thread.
//
// On Intel as well as 64-bit ARM, lock() is implemented using a progressive
// back-off strategy, rather than just spin (busy-wait) like in a naive spinlock,
// to prevent wasting energy and allow other threads to progress.
// The parameters of the progressive back-off are tuned for the scenario in a
// typical audio app (real-time thread is being called on a callback every 1-10 ms)
// but are useful for other scenarios as well.
//
// On platforms other than Intel and 64-bit ARM, progressive backoff is currently
// not implemented and lock() uses a naive fallback implementation that just spins
// (busy-waits).
//
// crill::spin_mutex is not recursive; repeatedly locking it on the same thread
// is undefined behaviour (in practice, it will probably deadlock your app).
class spin_mutex
{
public:
    // Effects: Acquires the lock. If necessary, blocks until the lock can be acquired.
    // Preconditions: The current thread does not already hold the lock.
    void lock() noexcept
    {
      #if CRILL_INTEL
        lock_impl_intel<5, 10, 3000>();
        // approx. 5x5 ns (= 25 ns), 10x40 ns (= 400 ns), and 3000x350 ns (~ 1 ms),
        // respectively, when measured on a 2.9 GHz Intel i9
      #elif CRILL_ARM_64BIT
        lock_impl_armv8<2, 750>();
        // approx. 2x10 ns (= 20 ns) and 750x1333 ns (~ 1 ms), respectively, on an
        // Apple Silicon Mac or an armv8 based phone.
      #else
        lock_impl_no_progressive_backoff();
      #endif
    }

    // Effects: Attempts to acquire the lock without blocking.
    // Returns: true if the lock was acquired, false otherwise.
    // Non-blocking guarantees: wait-free.
    bool try_lock() noexcept
    {
        return !flag.test_and_set(std::memory_order_acquire);
    }

    // Effects: Releases the lock.
    // Preconditions: The lock is being held by the current thread.
    // Non-blocking guarantees: wait-free.
    void unlock() noexcept
    {
        flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

  #if CRILL_INTEL
    template <std::size_t N0, std::size_t N1, std::size_t N2>
    void lock_impl_intel()
    {
        for (int i = 0; i < N0; ++i)
        {
            if (try_lock())
                return;
        }

        for (int i = 0; i < N1; ++i)
        {
            if (try_lock())
                return;

            _mm_pause();
        }

        while (true)
        {
            for (int i = 0; i < N2; ++i)
            {
                if (try_lock())
                    return;

                // Do not roll these into a loop: not every compiler unrolls it
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
                _mm_pause();
            }

            // waiting longer than we should, let's give other threads a chance to recover
            std::this_thread::yield();
        }
    }
  #elif CRILL_ARM_64BIT
    template <std::size_t N0, std::size_t N1>
    void lock_impl_armv8() noexcept
    {
        for (int i = 0; i < N0; ++i)
        {
            if (try_lock())
                return;
        }

        while (true)
        {
            for (int i = 0; i < N1; ++i)
            {
                if (try_lock())
                    return;

                __wfe();
            }

            // waiting longer than we should, let's give other threads a chance to recover
            std::this_thread::yield();
        }
    }
  #else
    // fallback for unsupported platforms
    void lock_impl_no_progressive_backoff() noexcept
    {
        while (!try_lock())
            /* spin */;
    }
  #endif
};

} // namespace crill

#endif //CRILL_SPIN_MUTEX_H
