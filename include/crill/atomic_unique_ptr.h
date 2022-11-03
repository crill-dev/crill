// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_ATOMIC_UNIQUE_PTR_H
#define CRILL_ATOMIC_UNIQUE_PTR_H

#include <memory>
#include <atomic>

namespace crill
{

// crill::atomic_unique_ptr wraps a std::unique_ptr and allows to
// replace this std::unique_ptr with a different std::unique_ptr
// as well as obtain the current pointer value as wait-free atomic
// operations. Custom deleters are not supported.
template <typename T>
class atomic_unique_ptr
{
public:
    // Effects: Constructs an empty atomic_unique_ptr.
    atomic_unique_ptr() = default;

    // Effects: Constructs an atomic_unique_ptr and stores the passed-in
    // unique_ptr into it.
    atomic_unique_ptr(std::unique_ptr<T> uptr)
      : ptr(uptr.release())
    {}

    // Effects: Deletes the object managed by the unique_ptr.
    ~atomic_unique_ptr()
    {
        delete get();
    }

    // Effects: Atomically swaps the currently stored unique_ptr with a
    // new unique_ptr.
    // Returns: the previously stored unique_ptr.
    // Non-blocking guarantees: wait-free.
    std::unique_ptr<T> exchange(std::unique_ptr<T> desired)
    {
        return std::unique_ptr<T>(ptr.exchange(desired.release()));
    }

    // Returns: a pointer to the managed object.
    // Non-blocking guarantees: wait-free.
    // Note: get() itself is race-free, but the returned pointer will
    // dangle if the underlying unique_ptr has deleted the managed object
    // in the meantime!
    T* get() const
    {
        return ptr.load();
    }

private:
    std::atomic<T*> ptr = nullptr;
    static_assert(std::atomic<T*>::is_always_lock_free);
};

} // namespace crill

#endif //CRILL_ATOMIC_UNIQUE_PTR_H
