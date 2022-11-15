// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_RECLAIM_OBJECT_H
#define CRILL_RECLAIM_OBJECT_H

#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <crill/atomic_unique_ptr.h>

namespace crill
{

// crill::reclaim_object stores a value of type T and provides concurrent
// read and write access to it. Multiple readers and writers are supported.
//
// Readers are guaranteed to always be wait-free. Readers will never block
// writers, but writers may block other writers.
//
// Overwritten values are put on a "zombie list". Values on the zombie list
// that are no longer referred to by any reader can be reclaimed by calling
// reclaim(). A call to reclaim() will block writers.
//
// The principle is very similar to RCU, with two key differences:
// 1) reclamation is managed per object, not in a single global domain
// 2) reclamation does not happen automatically: the user needs to explicitly
//    call reclaim() periodically (e.g., on a timer).
template <typename T>
class reclaim_object
{
    // TODO:
    // allow the user to specify whether they need single or multiple readers
    // and single or multiple writers. For the single-reader and single-writer
    // cases, enable more efficient implementations.

public:
    // Effects: constructs a reclaim_object containing a default-constructed value.
    reclaim_object()
      : value(std::make_unique<T>())
    {}

    // Effects: constructs a reclaim_object containing a value constructed with
    // the constructor arguments provided.
    template <typename... Args>
    reclaim_object(Args... args)
      : value(std::make_unique<T>(std::forward<Args>(args)...))
    {}

    // reclaim_object is non-copyable and non-movable.
    reclaim_object(reclaim_object&&) = delete;
    reclaim_object& operator=(reclaim_object&&) = delete;
    reclaim_object(const reclaim_object&) = delete;
    reclaim_object& operator=(const reclaim_object&) = delete;

    // Reading the value must happen through a reader class.
    class reader;

    // read_ptr provides scoped read access to the value.
    class read_ptr
    {
    public:
        read_ptr(reader& rdr) noexcept
          : rdr(rdr)
        {
            assert(rdr.min_epoch == 0);

            rdr.min_epoch.store(rdr.obj.current_epoch.load());
            assert(rdr.min_epoch =! 0);

            value_read = rdr.obj.value.load();
            assert(value_read);
        }

        ~read_ptr()
        {
            assert(rdr.min_epoch != 0);
            rdr.min_epoch.store(0);
        }

        const T& operator*() const
        {
            assert(value_read);
            return *value_read;
        }

        const T* operator->() const
        {
            assert(value_read);
            return value_read;
        }

        read_ptr(read_ptr&&) = delete;
        read_ptr& operator=(read_ptr&&) = delete;
        read_ptr(const read_ptr&) = delete;
        read_ptr& operator=(const read_ptr&) = delete;

    private:
        reader& rdr;
        T* value_read = nullptr;
    };

    class reader
    {
    public:
        reader(reclaim_object& obj) : obj(obj)
        {
            obj.register_reader(this);
        }

        ~reader()
        {
            obj.unregister_reader(this);
        }

        // Returns: a copy of the current value.
        // Non-blocking guarantees: wait-free if the copy constructor of
        // T is wait-free.
        T get_value() noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            return *read_lock();
        }

        // Returns: a read_ptr giving read access to the current value.
        // Non-blocking guarantees: wait-free.
        read_ptr read_lock() noexcept
        {
            return read_ptr(*this);
        }

    private:
        friend class reclaim_object;
        friend class read_ptr;
        reclaim_object& obj;
        std::atomic<std::uint64_t> min_epoch = 0;
    };

    reader get_reader()
    {
        return reader(*this);
    }

    // Effects: Updates the current value to a new value constructed from the
    // provided constructor arguments.
    // Note: allocates memory.
    template <typename... Args>
    void update(Args... args)
    {
        exchange_and_retire(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // write_ptr provides scoped write access to the value. This is useful if
    // you want to modify e.g. only a single data member of a larger class.
    // The new value will be atomically published when write_ptr goes out of scope.
    class write_ptr
    {
    public:
        write_ptr(reclaim_object& obj)
          : obj(obj),
            new_value(std::make_unique<T>(*obj.value.load()))
        {
            assert(new_value);
        }

        ~write_ptr()
        {
            assert(new_value);
            obj.exchange_and_retire(std::move(new_value));
        }

        T& operator*()
        {
            assert(new_value);
            return *new_value;
        }

        T* operator->() noexcept
        {
            assert(new_value);
            return new_value.get();
        }

        write_ptr(write_ptr&&) = delete;
        write_ptr& operator=(write_ptr&&) = delete;
        write_ptr(const write_ptr&) = delete;
        write_ptr& operator=(const write_ptr&) = delete;

    private:
        reclaim_object& obj;
        std::unique_ptr<T> new_value;
    };

    // Returns: a write_ptr giving scoped write access to the current value.
    write_ptr write_lock()
    {
        return write_ptr(*this);
    }

    // Effects: Deletes all previously overwritten values that are no longer
    // referred to by a read_ptr.
    void reclaim()
    {
        std::scoped_lock lock(zombies_mtx);

        for (auto& zombie : zombies)
        {
            assert(zombie.value != nullptr);
            if (!has_readers_using_epoch(zombie.epoch_when_retired))
                zombie.value.reset();
        }

        zombies.erase(
            std::remove_if(zombies.begin(), zombies.end(), [](auto& zombie){ return zombie.value == nullptr; }),
            zombies.end());
    }

private:
    void exchange_and_retire(std::unique_ptr<T> new_value)
    {
        assert(new_value != nullptr);
        auto old_value = value.exchange(std::move(new_value));

        std::scoped_lock lock(zombies_mtx);
        zombies.push_back({
            current_epoch.fetch_add(1),
            std::move(old_value)});
    }

    void register_reader(reader* rdr)
    {
        assert(rdr != nullptr);
        std::scoped_lock lock(readers_mtx);
        readers.push_back(rdr);
    }

    void unregister_reader(reader* rdr)
    {
        assert(rdr != nullptr);
        std::scoped_lock lock(readers_mtx);

        auto iter = std::find(readers.begin(), readers.end(), rdr);
        assert(iter != readers.end());
        readers.erase(iter);
    }

    bool has_readers_using_epoch(std::uint64_t epoch) noexcept
    {
        std::scoped_lock lock(readers_mtx);
        return std::any_of(readers.begin(), readers.end(), [epoch](auto* reader){
            assert(reader);
            std::uint64_t reader_epoch = reader->min_epoch.load();
            return reader_epoch != 0 && reader_epoch <= epoch;
        });
    }

    struct zombie
    {
        std::uint64_t epoch_when_retired;
        std::unique_ptr<T> value;
    };

    crill::atomic_unique_ptr<T> value;
    std::vector<reader*> readers;
    std::mutex readers_mtx;
    std::vector<zombie> zombies;
    std::mutex zombies_mtx;
    std::atomic<std::uint64_t> current_epoch = 1;

    // This algorithm requires a 64-bit lock-free atomic counter to avoid overflow.
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
};

} // namespace crill

#endif //CRILL_RECLAIM_OBJECT_H
