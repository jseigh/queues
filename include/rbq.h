/*
   Copyright 2025 Joseph W. Seigh
   
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <thread>
#include <lfrbq.h>
#include <eventcount.h>

/**
 * @brief rbq synchonization type for blocking on full/empty queues
 */
enum rbq_sync
{
    eventcount,     // use eventcount
    mutex,          // use mutex and cvars
    yield,          // use yield()
    semaphore,      // use counting semaphores
    atomic32        // use atomic wait/notify
};

class rbq : public lfrbq
{
    event_count producer_eventcount;
    event_count consumer_eventcount;

    std::mutex producer_mutex;
    std::condition_variable producer_cvar;
    std::mutex consumer_mutex;
    std::condition_variable consumer_cvar;

    std::atomic<uint32_t> producer_atomic32 = 0;    // use atomic wait/notify
    std::atomic<uint32_t> consumer_atomic32 = 0;    // use atomic wait/notify

    std::counting_semaphore<INT_MAX> empty_nodes{0};   // producer acquire, consumer release
    std::counting_semaphore<INT_MAX> full_nodes{0};    // consumer acquire, producer release

    const rbq_sync sync = rbq_sync::eventcount;

public:
    // using lfrbq::lfrbq;

    /**
     * @brief create a lock-free blocking queue
     * @param sync synchronization type {eventcount, yield, mutex}
     * 
     * @see lfrb::lfrb(uint32_t,bool,bool)
     * 
     */
    rbq(uint32_t size, bool sp_mode, bool sc_mode, rbq_sync sync) : lfrbq(size, sp_mode, sc_mode), sync(sync)
    {
        empty_nodes.release(size);
    }

    /**
     * @brief create a lock-free blocking queue
     * @param sync synchronization type {eventcount, yield, mutex}
     * 
     * @see lfrb::lfrb(uint32_t,lfrbq_qtype)
     *
     */
    rbq(uint32_t size, lfrbq_type qtype, rbq_sync sync) : lfrbq(size, qtype), sync(sync)
    {
        empty_nodes.release(size);
    }


private:


    lfrbq_status enqueue_ec(uintptr_t value)
    {
        for (;;)
        {
            lfrbq_status status = try_enqueue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    producer_eventcount.post();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::full:
                default:
                    break;
            }

            uint32_t mark = consumer_eventcount.mark();
            status = try_enqueue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    consumer_eventcount.reset(mark);
                    producer_eventcount.post();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::full:
                    break;
                default:
                    break;
            }
            tls_lfrbq_stats.producer_waits++;
            consumer_eventcount.wait(mark);   
        }
    }

    lfrbq_status dequeue_ec(uintptr_t *value)
    {
        for (;;)
        {
            lfrbq_status status = try_dequeue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    consumer_eventcount.post();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::empty:
                default:
                    break;
            }

            uint32_t mark = producer_eventcount.mark();
            status = try_dequeue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    producer_eventcount.reset(mark);
                    consumer_eventcount.post();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::empty:
                    break;
                default:
                    break;
            }
            tls_lfrbq_stats.consumer_waits++;
            producer_eventcount.wait(mark);   
        }
    }

    lfrbq_status enqueue_x(uintptr_t value)
    {
        for (;;)
        {
            lfrbq_status status = try_enqueue(value);
            switch (status)
            {
                case lfrbq_status::success:
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::full:
                default:
                    tls_lfrbq_stats.producer_waits++;
                    std::this_thread::yield();
                    break;
            }
        }
    }   

    lfrbq_status dequeue_x(uintptr_t *value)
    {
        for (;;)
        {
            lfrbq_status status = try_dequeue(value);
            switch (status)
            {
                case lfrbq_status::success:
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::empty:
                default:
                    tls_lfrbq_stats.consumer_waits++;
                    std::this_thread::yield();
                    break;
            }
        }
    }

    lfrbq_status enqueue_mx(uintptr_t value)
    {
        std::unique_lock lk(producer_mutex);
        for (;;)
        {
            lfrbq_status status = try_enqueue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    consumer_cvar.notify_one();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::full:
                default:
                    tls_lfrbq_stats.producer_waits++;
                    producer_cvar.wait(lk);
                    break;
            }
        }
    }

    lfrbq_status dequeue_mx(uintptr_t *value)
    {
        std::unique_lock lk(consumer_mutex);
        // std::unique_lock lk(producer_mutex);
        for (;;)
        {
            lfrbq_status status = try_dequeue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    producer_cvar.notify_one();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::empty:
                default:
                    tls_lfrbq_stats.consumer_waits++;
                    consumer_cvar.wait(lk);
                    break;
            }
        }
    }

    lfrbq_status enqueue_a32(uintptr_t value)
    {
        for (;;)
        {
            uint32_t mark = consumer_atomic32.load(std::memory_order_acquire);
            lfrbq_status status = try_enqueue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    producer_atomic32.fetch_add(1, std::memory_order_relaxed);
                    producer_atomic32.notify_one();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::full:
                default:
                    tls_lfrbq_stats.producer_waits++;
                    consumer_atomic32.wait(mark);
                    break;
            }
        }
    }   

    lfrbq_status dequeue_a32(uintptr_t *value)
    {
        for (;;)
        {
            uint32_t mark = producer_atomic32.load(std::memory_order_acquire);
            lfrbq_status status = try_dequeue(value);
            switch (status)
            {
                case lfrbq_status::success:
                    consumer_atomic32.fetch_add(1, std::memory_order_relaxed);
                    consumer_atomic32.notify_one();
                    return status;
                case lfrbq_status::closed:
                    return status;

                case lfrbq_status::empty:
                default:
                    tls_lfrbq_stats.consumer_waits++;
                    producer_atomic32.wait(mark);
                    break;
            }
        }
    }

    lfrbq_status enqueue_sem(uintptr_t value)
    {
        if (!empty_nodes.try_acquire())
        {
            tls_lfrbq_stats.producer_waits++;
            empty_nodes.acquire();
        }

        lfrbq_status status = try_enqueue(value);
        switch (status)
        {
            case lfrbq_status::success:
                full_nodes.release();
                return status;
            case lfrbq_status::closed:
                return status;

            case lfrbq_status::full:
            default:
                abort();
                break;
        }
    }   

    lfrbq_status dequeue_sem(uintptr_t *value)
    {
        if (!full_nodes.try_acquire())
        {
            tls_lfrbq_stats.consumer_waits++;
            full_nodes.acquire();
        }
        
        lfrbq_status status = try_dequeue(value);
        switch (status)
        {
            case lfrbq_status::success:
                empty_nodes.release();
                return status;
            case lfrbq_status::closed:
                return status;

            case lfrbq_status::empty:
            default:
                abort();
                break;
        }
    }



public:

    /**
     * @brief enqueue a value, blocks if queue is full
     * @param value to be queued
     * @retval lfrbq_status::success enqueue succeeded
     * @retval lfrbq_status::closed  enqueue failed - queue closed
     */
    lfrbq_status enqueue(uintptr_t value)
    {
        switch (sync)
        {
            case rbq_sync::mutex: return enqueue_mx(value);
            case rbq_sync::eventcount: return enqueue_ec(value);
            case rbq_sync::yield: return enqueue_x(value);
            case rbq_sync::semaphore: return enqueue_sem(value);
            case rbq_sync::atomic32: return enqueue_a32(value);

            default: return lfrbq_status::fail;
        }
    }

    /**
     * @brief dequeue a value, blocks if queue is empty and not closed
     * @param value address for returned value
     * @retval lfrbq_status::success dequeue succeeded
     * @retval lfrbq_status::closed  dequeue failed - queue is empty and closed
     */
    lfrbq_status dequeue(uintptr_t *value)
    {
        switch (sync)
        {
            case rbq_sync::mutex: return dequeue_mx(value);
            case rbq_sync::eventcount: return dequeue_ec(value);
            case rbq_sync::yield: return dequeue_x(value);
            case rbq_sync::semaphore: return dequeue_sem(value);
            case rbq_sync::atomic32: return dequeue_a32(value);

            default: return lfrbq_status::fail;
        }
    }

    /**
     * @brief close the queue
     */
    void close()
    {
        lfrbq::close();

        /*
        * close queue before closing or posting the eventcounts
        * and notifying the cvars.
        */

        producer_eventcount.close();
        consumer_eventcount.close();

        producer_cvar.notify_all();
        consumer_cvar.notify_all();

        producer_atomic32.fetch_add(1, std::memory_order_relaxed);
        producer_atomic32.notify_all();
        consumer_atomic32.fetch_add(1, std::memory_order_relaxed);
        consumer_atomic32.notify_all();

        int xval = INT_MAX - capacity;  // > # producers and consumers
        empty_nodes.release(xval);
        full_nodes.release(xval);
    }

};
