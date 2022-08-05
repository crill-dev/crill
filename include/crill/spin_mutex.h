// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_SPIN_MUTEX_H
#define CRILL_SPIN_MUTEX_H

#include <crill/platform.h>
#include <atomic>
#include <thread>

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
// lock() is implemented using a progressive back-off strategy, rather than just
// busy waiting like in a simple spinlock, to prevent wasting energy and
// allow other threads to progress. The parameters of the progressive back-off
// are tuned for the scenario in a typical audio app (real-time thread is
// being called on a callback every 1-10 ms) but are useful for other scenarios
// as well.
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
        // Stage 1
        for (int i = 0; i < stage_reps[0]; ++i)
        {
            if (try_lock())
                return;
        }

        // Stage 2
        for (int i = 0; i < stage_reps[1]; ++i)
        {
            if (try_lock())
                return;

          #if CRILL_INTEL
            _mm_pause();
          #elif CRILL_ARM_64BIT
            // The middle stage is not used on ARM, because there is no "short CPU pause"
            // instruction like _mm_pause(). __wfe() typically pauses for ~ 1.3 us if the
            // event register is not set.
            assert(false);
          #else
            #error "crill::spin_mutex not implemented for the platform being compiled"
          #endif
        }

        // Stage 3
        while (true)
        {
            for (int i = 0; i < stage_reps[2]; ++i)
            {
                if (try_lock())
                    return;

              #if CRILL_INTEL
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
              #elif CRILL_ARM_64BIT
                __wfe();
              #else
                #error "Unsupported platform!"
              #endif
            }

            // waiting longer than we should, let's give other threads a chance to recover
            std::this_thread::yield();
        }
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

    // number of repetitions for each progressive backoff stage
    static constexpr std::size_t stage_reps[] =
      #if CRILL_INTEL
        // approx. 5x5 ns (= 25 ns), 10x40 ns (= 400 ns), and 3000x350 ns (~ 1 ms),
        // respectively, when measured on a 2.9 GHz Intel i9
        {5, 10, 3000};
      #elif CRILL_ARM_64BIT
        // approx. 2x10 ns (= 20 ns) and 750x1333 ns (~ 1 ms), respectively, on an
        // Apple Silicon Mac or an armv8 based phone. No middle stage.
        {2, 0, 750};
      #else
        #error "crill::spin_mutex not implemented for the platform being compiled"
      #endif
};

} // namespace crill

#endif //CRILL_SPIN_MUTEX_H
