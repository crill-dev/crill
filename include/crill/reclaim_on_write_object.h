// crill - the Cross-platform Real-time, I/O, and Low-Latency Library
// Copyright (c) 2022 - Timur Doumler and Fabian Renn-Giles
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)

#ifndef CRILL_RECLAIM_ON_WRITE_OBJECT_H
#define CRILL_RECLAIM_ON_WRITE_OBJECT_H

#include <utility>
#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <vector>

namespace crill
{

// crill::reclaim_on_write_object has the same interface as
// crill::reclaim_object, except that it is not necessary to
// call reclaim(), instead the reclamation happens automatically
// on update. This enables an algorithm that does not require
// a zombie list or allocating any objects on the heap.
// The tradeoff is that the writer needs to block on update
// until all readers accessing the old value have finished.
template <typename T>
class reclaim_on_write_object
{
public:
    // Effects: constructs a reclaim_on_write_object containing a default-constructed value.
    reclaim_on_write_object()
      : slots{{T(), T()}}
    {}

    // Effects: constructs a reclaim_on_write_object containing a value constructed with
    // the constructor arguments provided.
    template <typename... Args>
    reclaim_on_write_object(Args... args)
      : slots{{T(std::forward<Args>(args)...), T(std::forward<Args>(args)...)}}
    {}

    // reclaim_on_write_object is non-copyable and non-movable.
    reclaim_on_write_object(reclaim_on_write_object&&) = delete;
    reclaim_on_write_object& operator=(reclaim_on_write_object&&) = delete;
    reclaim_on_write_object(const reclaim_on_write_object&) = delete;
    reclaim_on_write_object& operator=(const reclaim_on_write_object&) = delete;

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

            read_slot = rdr.obj.current_read_slot.load();
        }

        ~read_ptr()
        {
            assert(rdr.min_epoch != 0);
            rdr.min_epoch.store(0);
        }

        const T& operator*() const
        {
            return rdr.obj.slots[read_slot];
        }

        const T* operator->() const
        {
            return &rdr.obj.slots[read_slot];
        }

        read_ptr(read_ptr&&) = delete;
        read_ptr& operator=(read_ptr&&) = delete;
        read_ptr(const read_ptr&) = delete;
        read_ptr& operator=(const read_ptr&) = delete;

    private:
        reader& rdr;
        std::uint8_t read_slot;
    };

    class reader
    {
    public:
        reader(reclaim_on_write_object& obj) : obj(obj)
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
        friend class reclaim_on_write_object;
        friend class read_ptr;
        reclaim_on_write_object& obj;
        std::atomic<std::uint64_t> min_epoch = 0;
    };

    reader get_reader()
    {
        return reader(*this);
    }

    // Effects: Updates the current value to a new value constructed from the
    // provided constructor arguments.
    // Note: Blocks until all readers accessing the old value have finished.
    template <typename... Args>
    void update(Args... args)
    {
        std::uint8_t write_slot = current_read_slot.load() ^ 1;
        slots[write_slot] = T(std::forward<Args>(args)...);
        swap_slot_and_wait_for_readers(write_slot);
    }

    // write_ptr provides scoped write access to the value. This is useful if
    // you want to modify e.g. only a single data member of a larger class.
    // The new value will be atomically published when write_ptr goes out of scope.
    // Note: The destructor of write_ptr will blocks until all readers accessing
    // the old value have finished.
    class write_ptr
    {
    public:
        write_ptr(reclaim_on_write_object& obj)
          : obj(obj), write_slot(obj.current_read_slot.load() ^ 1)
        {
            obj.slots[write_slot] = obj.slots[write_slot ^ 1];
        }

        ~write_ptr()
        {
            obj.swap_slot_and_wait_for_readers(write_slot);
        }

        T& operator*()
        {
            return obj.slots[write_slot];
        }

        T* operator->() noexcept
        {
            return &obj.slots[write_slot];
        }

        write_ptr(write_ptr&&) = delete;
        write_ptr& operator=(write_ptr&&) = delete;
        write_ptr(const write_ptr&) = delete;
        write_ptr& operator=(const write_ptr&) = delete;

    private:
        reclaim_on_write_object& obj;
        std::uint8_t write_slot;
    };

    // Returns: a write_ptr giving scoped write access to the current value.
    write_ptr write_lock()
    {
        return write_ptr(*this);
    }

private:
    void swap_slot_and_wait_for_readers(std::uint8_t write_slot)
    {
        current_read_slot.store(write_slot);
        auto retired_epoch = current_epoch.fetch_add(1);

        while (has_readers_using_epoch(retired_epoch))
            std::this_thread::yield(); // TODO: progressive backoff

        // TODO:
        //  Paul McKenney mentioned that in similar algorithms, it is necessary
        //  to swap the slot and wait for readers *twice* before writing again
        //  to avoid a race condition. Check if our algorithm is affected by this!
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

    std::array<T, 2> slots;
    std::atomic<std::uint8_t> current_read_slot = 0;
    std::vector<reader*> readers;
    std::mutex readers_mtx;
    std::atomic<std::uint64_t> current_epoch = 1;

    // This algorithm requires a 64-bit lock-free atomic counter to avoid overflow.
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    // This algorithm does not work for non-copyable types!
    static_assert(std::is_copy_constructible_v<T>);
    static_assert(std::is_copy_assignable_v<T>);
};

} // namespace crill

#endif // CRILL_RECLAIM_ON_WRITE_OBJECT_H
