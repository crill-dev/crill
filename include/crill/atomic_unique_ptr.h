// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_ATOMIC_UNIQUE_PTR_H
#define CRILL_ATOMIC_UNIQUE_PTR_H

#include <memory>
#include <atomic>
#include <optional>

namespace crill
{

// crill::atomic_unique_ptr wraps a std::unique_ptr and provides some
// atomic wait-free operations on the underlying pointer.
// This can be useful for atomic pointer swaps when building lock-free
// algorithms without sacrificing the lifetime management semantics of
// unique_ptr. Custom deleters are not supported.
template <typename T>
class atomic_unique_ptr
{
    // TODO: support std::memory_order parameters

public:
    // Effects: Constructs an atomic_unique_ptr containing an empty
    // unique_ptr.
    atomic_unique_ptr() = default;

    // Effects: Constructs an atomic_unique_ptr containing the passed-in
    // unique_ptr.
    atomic_unique_ptr(std::unique_ptr<T> uptr)
      : ptr(uptr.release())
    {}

    // Effects: Constructs an atomic_unique_ptr containing a new unique_ptr
    // managing an object constructed from the given constructor arguments.
    template <typename... Args>
    atomic_unique_ptr(Args... args)
      : atomic_unique_ptr(std::make_unique<T>(std::forward<Args>(args)...))
    {
    }

    // Effects: Deletes the object managed by the unique_ptr.
    ~atomic_unique_ptr()
    {
        delete load();
    }

    // Returns: a pointer to the managed object.
    // Non-blocking guarantees: wait-free.
    // Note: get() itself is race-free, but the returned pointer will
    // dangle if the underlying unique_ptr has deleted the managed object
    // in the meantime!
    T* load() const
    {
        return ptr.load();
    }

    // Effects: Atomically swaps the currently stored unique_ptr with a
    // new unique_ptr.
    // Returns: the previously stored unique_ptr.
    // Non-blocking guarantees: wait-free.
    std::unique_ptr<T> exchange(std::unique_ptr<T> desired)
    {
        return std::unique_ptr<T>(ptr.exchange(desired.release()));
    }

    // Effects: If the address of the managed object is equal to expected,
    // replaces the currently stored unique_ptr with desired by moving from
    // desired. Otherwise, changes the value of expected to the address of
    // the managed object.
    // Returns: If the compare succeeded, the previously stored unique_ptr;
    // otherwise, an empty optional.
    // Non-blocking guarantees: wait-free.
    std::optional<std::unique_ptr<T>>
    compare_exchange_strong(T*& expected, std::unique_ptr<T>& desired)
    {
        return compare_exchange_impl([this](T*& expected, T* desired){
            return ptr.compare_exchange_strong(expected, desired);
        }, expected, desired);
    }

    // Effects: Equivalent to compare_exchange_strong, but the comparison
    // may spuriously fail. On some platforms, this gives better performance.
    // Use this version when calling compare_exchange in a loop.
    // Non-blocking guarantees: wait-free.
    std::optional<std::unique_ptr<T>>
    compare_exchange_weak(T*& expected, std::unique_ptr<T>& desired)
    {
        return compare_exchange_impl([this](T*& expected, T* desired){
            return ptr.compare_exchange_weak(expected, desired);
        }, expected, desired);
    }

private:
    template <typename FuncT>
    std::optional<std::unique_ptr<T>>
    compare_exchange_impl(FuncT&& cx_func, T*& expected, std::unique_ptr<T>& desired)
    {
        auto* desired_raw = desired.get();
        if (cx_func(expected, desired_raw))
        {
            desired.release();
            return std::unique_ptr<T>(expected);
        }
        else
        {
            return {};
        }
    }

    std::atomic<T*> ptr = nullptr;
    static_assert(std::atomic<T*>::is_always_lock_free);
};

} // namespace crill

#endif //CRILL_ATOMIC_UNIQUE_PTR_H
