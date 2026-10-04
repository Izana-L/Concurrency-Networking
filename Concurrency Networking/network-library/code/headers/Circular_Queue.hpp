/*
* Copyright © 2025+ Ángel Rodríguez Ballesteros
*
* Distributed under the Boost Software License, version 1.0
* See www.boost.org/LICENSE_1_0.txt
*
* angel.rodriguez@udit.es
*/

#ifndef CIRCULAR_QUEUE_HEADER
#define CIRCULAR_QUEUE_HEADER

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace argb
{

    /** It represents a FIFO queue of fixed size which can be used as a consumer/producer buffer.
      * It handles thread synchronization internally blocking writes (push/emplace) when the queue
      * is full and reads (pop) when the queue is empty.
      * It allocates a single memory buffer on construction and that buffer is used without further
      * memory allocations at runtime.
      * The public interface is similar to that of the containers of the standard library.
      */
    template< typename TYPE >
    class Circular_Queue final
    {
    public:

        using Value_Type = TYPE;

    private:

        std::vector< std::byte > elements;
        const size_t             real_capacity;

        std::atomic< size_t >    first;
        std::atomic< size_t >    last;
        std::atomic< size_t >    allocated;

        std::atomic< bool   >    cancelled;
        std::condition_variable  condition;
        std::mutex               mutex;

    public:

        Circular_Queue(size_t desired_capacity)
            :
            elements((desired_capacity + 1) * sizeof(Value_Type)),
            real_capacity(desired_capacity + 1)
        {
            first = last = 0;
            allocated = 0;
            cancelled = false;
        }

        size_t capacity() const
        {
            return real_capacity - 1;
        }

        size_t size() const
        {
            return allocated;
        }

        size_t available() const
        {
            return capacity() - size();
        }

        bool empty() const
        {
            return size() == 0;
        }

        bool full() const
        {
            return available() == 0;
        }

        Value_Type& front()
        {
            assert(not empty());
            return element_at(first);
        }

        const Value_Type& front() const
        {
            assert(not empty());
            return element_at(first);
        }

    public:

        void cancel()
        {
            cancelled = true;

            condition.notify_all();
        }

        template< typename ...ARGUMENTS >
        void emplace(ARGUMENTS && ...arguments);

        void push(const Value_Type& value);

        std::optional< Value_Type > pop();
        std::optional<TYPE> try_pop();

    private:

        Value_Type& element_at(size_t index)
        {
            return reinterpret_cast<Value_Type&>(elements[index * sizeof(Value_Type)]);
        }

        Value_Type& allocate_one();
        Value_Type& allocate_one_before(size_t new_last);

        void free_one();

    };

    template< typename TYPE >
    template< typename ...ARGUMENTS >
    void Circular_Queue< TYPE >::emplace(ARGUMENTS && ...arguments)
    {
        assert(capacity() > 0);

        std::unique_lock lock(mutex);

        if (full() && not cancelled)
        {
            condition.wait(lock, [this] { return not full() || cancelled; });
        }

        if (cancelled) return;

        new (&allocate_one()) Value_Type(std::forward<ARGUMENTS>(arguments)...);

        lock.unlock();

        condition.notify_one();
    }

    template< typename TYPE >
    void Circular_Queue< TYPE >::push(const Value_Type& value)
    {
        assert(capacity() > 0);

        std::unique_lock lock(mutex);

        if (full() && not cancelled)
        {
            condition.wait(lock, [this] { return not full() || cancelled; });
        }

        if (cancelled) return;

        allocate_one() = value;

        lock.unlock();

        condition.notify_one();
    }

    template< typename TYPE >
    std::optional< TYPE > Circular_Queue< TYPE >::pop()
    {
        std::unique_lock lock(mutex);

        if (empty() && not cancelled)
        {
            condition.wait(lock, [this] { return not empty() || cancelled; });
        }

        if (cancelled) return std::nullopt;

        Value_Type value = front();

        free_one();

        lock.unlock();

        condition.notify_one();

        return value;
    }
    template< typename TYPE >
    std::optional<TYPE> Circular_Queue< TYPE >::try_pop()
    {
        std::unique_lock lock(mutex);          // lock normal, sin espera
        if (empty() || cancelled)
            return std::nullopt;

        Value_Type value = std::move(front());
        free_one();
        lock.unlock();
        condition.notify_one();                // para despertar posibles productores
        return value;
    }
    template< typename TYPE >
    TYPE& Circular_Queue< TYPE >::allocate_one()
    {
        if (last >= first)
        {
            if (last == real_capacity - 1)
            {
                if (first > 0)
                {
                    return allocate_one_before(0);
                }
            }
            else
                return allocate_one_before(last + 1);
        }
        else
        {
            if (last < first - 1) return allocate_one_before(last + 1);
        }

        throw std::bad_alloc();
    }

    template< typename TYPE >
    inline TYPE& Circular_Queue< TYPE >::allocate_one_before(size_t new_last)
    {
        size_t previous_last = last;

        last = new_last;

        ++allocated;

        return element_at(previous_last);
    }

    template< typename TYPE >
    void Circular_Queue< TYPE >::free_one()
    {
        front().~Value_Type();

        if (first < last)
        {
            ++first;
            --allocated;
        }
        else if (first > last)
        {
            if (++first == real_capacity) first = 0;
            --allocated;
        }
    }

}

#endif