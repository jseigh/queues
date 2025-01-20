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

#include <atomic>
#include <chrono>
#include <ratio>
#include <climits>

#include <stdint.h>

#include <unistd.h>
// #include <stdint.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>



static inline long futex_call(uint32_t *futex, int futex_op, uint32_t val, uint32_t *val2, uint32_t * uaddr2, uint32_t val3)
{
    return syscall(SYS_futex, futex, futex_op, val, val2, uaddr2, val3);
}

static inline long futex_wake(uint32_t *futex, uint32_t wakeup_count)
{
    return futex_call(futex, FUTEX_WAKE_PRIVATE, wakeup_count, NULL, NULL, 0);
}

static inline long futex_wait(uint32_t *futex, uint32_t val, const struct timespec *timeout)
{
    return futex_call(futex, FUTEX_WAIT_PRIVATE, val, (uint32_t *) timeout, NULL, 0);
}

/*
 * eventcount
 * bits
 *   63..32 waiter count
 *   31..1 sequence
 *   0 1 = open, 0 = closed
 */

uint64_t ecval(uint32_t futex, uint32_t waiters)
{
    uint64_t rval = waiters;
    rval <<= 32;
    rval += futex;
    return rval;
}


const uint64_t wait_incr = 1llu << 32;
const uint64_t futex_incr = 2;

class alignas(uint64_t) event_count
{

    union
    {
        std::atomic<uint64_t> xval;
        uint64_t _val;
        struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
            uint32_t futex;
            uint32_t waiters;
#elif __BYTE_ORDER == __BIG_ENIAN
            uint32_t waiters;
            uint32_t futex;
#else
#error "Byte order not little endian or big endian"
#endif
        };
    };


    event_count(uint32_t futex, uint32_t waiters) : futex(futex), waiters(waiters) {}
    event_count(uint64_t val) : _val(val) {}

    event_count(event_count& other)
    {
        _val = other.xval.load(std::memory_order_acquire);
    }
    
    // event_countxx& operator =(event_countxx& other)
    // {
    //     _val = other.xval.load(std::memory_order_acquire);
    //     return *this;
    // }

    template<typename Rep, typename Period>
    // template<typename Duration = std::chrono::milliseconds>
    struct timespec* _timespec(struct timespec& timeout, std::chrono::duration<Rep, Period> duration)
    {
        if (duration.count() <= 0)
        {
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;
            return nullptr;
        }

        auto duration_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
        auto wait_nanos = duration_nanos.count();

        auto nanos = std::nano::den;
        timeout.tv_sec = wait_nanos / nanos;    // / 1'000'000'000;
        timeout.tv_nsec = wait_nanos % nanos;   // % 1'000'000'000;
        return &timeout;
    }



public:
    event_count() : futex(1), waiters(0) {};

    ~event_count() {}

    /**
     * @brief close eventcount.
     * All current and future waits will no longer block.
     * 
     */
    void close()
    {

        xval.store(0, std::memory_order_release);
        futex_wake(&futex, INT_MAX);
    }

    /**
     * @brief get current eventcount value
     * @return eventcount value
     */
    uint32_t mark()
    {
        event_count current = xval.fetch_add(wait_incr, std::memory_order_acquire);
        return current.futex;
    }

    /**
     * @brief eventcount timed wait
     * @tparam Rep
     * @tparam Period
     * @param mark from mark()
     * @param duration of wait, if <= 0, then wait forever
     */
    template<typename Rep, typename Period>
    void timedwait(uint32_t mark, std::chrono::duration<Rep, Period> duration)
    {
        if ((mark) == 0)
            return;

        struct timespec timeout;
        struct timespec* ptimeout = _timespec(timeout, duration);

        for (;;) {
            uint32_t current =  std::atomic_ref(futex).load(std::memory_order_acquire);
            // if ((current) == 0)
            //     return;

            if (current != mark)    // EAGAIN
                return;

            long rc = futex_wait(&futex, current, ptimeout);

            if (rc == 0)
                return;

            if (errno == ETIMEDOUT)
                return;
        }

    }

    /**
     * @brief eventcount wait
     * @param mark from mark()
     */
    void wait(uint32_t mark)
    {
        constexpr std::chrono::milliseconds nowait(0);
        timedwait(mark, nowait);
    }

    /**
     * @brief reset wait count contribution from prior mark()
     */
    void reset(uint32_t mark)
    {
        if (mark == 0)
            return;
            
        uint64_t update;
        event_count expected = xval.load(std::memory_order_acquire);
        do {
            if ((expected.futex) != mark)
                break;

            if (expected.waiters == 0)
                break;

            update = ecval(expected.futex, expected.waiters - 1);
        }
        while (!xval.compare_exchange_weak(expected._val, update, std::memory_order_relaxed));
    }

    /**
     * @brief Increment eventcount
     * if there are any waiters and wake
     * any waiting threads.
     * 
     */
    void post()
    {
        uint64_t update;
        event_count expected = xval.load(std::memory_order_acquire);
        uint32_t _futex = expected.futex;
        do {
            if (_futex != expected.futex)   // ?
                return;

            if ((expected.futex) == 0)
                return;

            if (expected.waiters == 0)
                return;
            
            update = ecval(expected.futex + 2, 0);
        }
        while (!xval.compare_exchange_weak(expected._val, update, std::memory_order_release));

        futex_wake(&futex, INT_MAX);
        return;
    }





};

