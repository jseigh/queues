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

#include <type_traits>
#include <atomic>
#include <stdexcept>
#include <cstddef>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <atomix.h>


/**
 * per thread statistics
 * 
 * waits are counts of yield(), eventcount.wait(), or cvar.wait()
 */
struct lfrbq_stats_t {
    uint32_t queue_full_count = 0;      // queue full count
    uint32_t queue_empty_count = 0;     // queue empty count

    uint32_t producer_waits = 0;        // rbq enqueue waits for non-full queue
    uint32_t consumer_waits = 0;        // rbq dequeue  waits for non-empty queue

    uint32_t producer_retries = 0;      // producer atomic op retries
    uint32_t consumer_retries = 0;      // consumer atomic op retries

    uint32_t producer_wraps = 0;        // producer detected wraps
    uint32_t consumer_wraps = 0;        // consumer detected wraps
};

inline thread_local lfrbq_stats_t tls_lfrbq_stats;


using seq_t = uint64_t;
using stat_t = uint32_t;

/**
 * @brief lfrb queue type
 *
 * enum value = (sp_mode * 2) + sc_mode
 */
enum lfrbq_type
{
    /** multi-producer, multi-consumer */
    mpmc = 0,   // multi-producer, multi-consumer
    /** multi-producer, single consumer */
    mpsc = 1,   // multi-producer, single consumer
    /** single producer, multi-consumer */
    spmc = 2,   // single producer, multi-consumer
    /** single producer, single consumer */
    spsc = 3,   // single producer, single consumer
};


enum lfrbq_status
{
    success,    // queue operation was a success
    fail,       // queue operation failed, unknown error
    empty,      // dequeue failed, queue empty
    full,       // enqueue failed, queue full
    closed      // queue operation failed, queue is closed
};


struct alignas(16) lfrbq_node
{
    std::atomic<seq_t> seq;
    std::atomic<uintptr_t> value;

    lfrbq_node(seq_t seq, uintptr_t value) : seq(seq), value(value) {}
    lfrbq_node() : seq(0), value(0) {}

    /** copy ctor */
    lfrbq_node(lfrbq_node& node)
    {
        seq.store(node.seq.load(std::memory_order_acquire), std::memory_order_relaxed);
        value.store(node.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
};


class alignas(64) lfrbq
{
protected:

    const uint32_t size;                    // capacity -- power of 2    xxxxx10...0
    const seq_t mask;                       // capacity - 1              xxxxx01...1
    const seq_t seq_mask;                   // sequence w/o index bits   1111110...0
    const bool sp_mode;                     // single producer mode -- enqueue not thread-safe
    const bool sc_mode;                     // single consumer mode -- dequeue not thread-safe

    std::atomic<bool> qclosed = false;


    lfrbq_node* rbuffer;                     // the ring buffer


    alignas(64) std::atomic<seq_t> head;    // next available full buffer if head == rbuffer[seq2ndx(head)]
    alignas(64) std::atomic<seq_t> tail;    // next available empty buffer if tail == rbuffer[seq2ndx(tail)]

    /**
     * Convert sequence to index into rbuffer array
     * @param seq
     * @return index into array
     */
    unsigned int seq2ndx(seq_t seq) { return seq & mask; }

    /**
     * @brief Convert head or tail sequence to node sequence
     */
    inline seq_t seq2node(seq_t seq) { return seq & seq_mask; }

    /**
     * @brief  3-way comparator for seq_t values
     * @param a 
     * @param b 
     * @return 
     */
    int64_t xcmp(seq_t a, seq_t b) { return (a - b); }


public:

    /**
     * @brief create lock-free ring buffer or bounded queue
     * @param size or capacity of queue, must be power of 2
     * @param sp_mode single producer if true
     * @param sc_mode single consumer if true
     * @throws invalid_argument if size not power of 2
     */
    lfrbq(uint32_t size, bool sp_mode, bool sc_mode) :
        size(size),
        mask(size - 1),
        seq_mask(~mask),
        sp_mode(sp_mode),
        sc_mode(sc_mode)
    {
        if ((size & (size - 1)) != 0)
        {
            throw std::invalid_argument("size not power of 2");
        }


        /*--*/

        this->head.store(size, std::memory_order_relaxed);
        this->tail.store(0, std::memory_order_relaxed);

        /*
         * allocate and initialize ring buffer
        */

        size_t sz = (size * sizeof(lfrbq_node));
        this->rbuffer = (lfrbq_node*) aligned_alloc(16, sz);
        // memset(this->rbuffer, 0, sz);

        for (unsigned int ndx = 0; ndx < size; ndx++)
        {
            rbuffer[ndx].value.store(0, std::memory_order_relaxed);
            // this->rbuffer[ndx].seq.store(ndx, std::memory_order_relaxed);
            this->rbuffer[ndx].seq.store(0, std::memory_order_relaxed);
        }

    }   // CTOR

    /**
     * @brief create lock-free ring buffer or bounded queue
     * @param size or capacity of queue, must be power of 2
     * @param qtype queue type, one of mpmc, mpsc, spmc, or spsc
     * @throws invalid_argument if size not power of 2
     */
    lfrbq(uint32_t size, lfrbq_type qtype) : lfrbq(size, qtype & 2, qtype & 1) {}

    ~lfrbq()
    {
        for (unsigned int ndx = 0; ndx < size; ndx++)
        {
            // invoke dtors if required
        }
        free(rbuffer);
    }

    /**
     * @brief close the queue
     */
    void close() { qclosed.store(true, std::memory_order_release); }
    /**
     * @brief get queue closed status
     * @retval true queue is closed
     * @retval false queue is not closed
     */
    bool closed() { return qclosed.load(std::memory_order_acquire); }

private:

    bool enqueue_sp(uintptr_t value)
    {
        seq_t tail_copy = tail.load(std::memory_order_relaxed);   // always current

        unsigned int ndx = seq2ndx(tail_copy);
        lfrbq_node *node = &rbuffer[ndx];

        seq_t node_seq = node->seq.load(std::memory_order_acquire);

        if (node_seq != seq2node(tail_copy)) {
            return false;   // full
        }

        seq_t head_copy = head.load(std::memory_order_relaxed);
        if ((node_seq + ndx) == head_copy) {
            return false;   // full
        }

        node->value.store(value, std::memory_order_relaxed);
        node->seq.store(node_seq + (seq_t) size, std::memory_order_release);
        tail.store(tail_copy + 1, std::memory_order_release);

        return true;
    }

    /**
     * Try updating lfrb tail
     * @param new_tail new tail value
     * @return current tail value which will be >= new_tail
     */
    seq_t try_update_tail(const seq_t new_tail)
    {
        seq_t current_tail = tail.load(std::memory_order_relaxed);
        do {
            if (xcmp(current_tail, new_tail) >= 0)
                return current_tail;
        }
        while (!tail.compare_exchange_strong(current_tail, new_tail, std::memory_order_release));
        return new_tail;
    }

    bool enqueue_mp(uintptr_t value)
    {
        // outer_loop:
        for (;;)
        {
            seq_t tail_copy = tail.load(std::memory_order_relaxed);

            unsigned int ndx = seq2ndx(tail_copy);
            seq_t node_seq = rbuffer[ndx].seq.load(std::memory_order_relaxed);

            while (xcmp(node_seq, tail_copy) > 0) {   // seq > tail

                uint64_t tail_latency = node_seq - seq2node(tail_copy);
                if (tail_latency > size)
                {
                    tls_lfrbq_stats.producer_wraps++;
                    // fprintf(stderr, "wrapped tail seq=%llu tail_copy=%llu\n", seq, tail_copy);   // ???
                    tail_copy = (node_seq - size) + ndx;
                }
                else
                {
                    tail_copy++;
                }

                ndx = seq2ndx(tail_copy);
                node_seq = rbuffer[ndx].seq.load(std::memory_order_relaxed);
            }

            if (xcmp(node_seq, seq2node(tail_copy)) < 0)
            {
                fprintf(stderr, "invalid tail seq=%llu tail_copy=%llu\n", node_seq, tail_copy);   // ???
                continue;
            }

            /*
            * tail_copy > seq  -- should never happen
            * tail_copy == seq
            */

            seq_t head_copy = head.load(std::memory_order_relaxed);
            if ((node_seq + ndx) == head_copy) {
                return false; // full
            }

            // seq == tail
            uintptr_t old_value = rbuffer[ndx].value.load(std::memory_order_relaxed);


            lfrbq_node update((seq2node(tail_copy) + size), value);
            lfrbq_node expected(seq2node(tail_copy), old_value);

            if (atomic_compare_exchange_16xx(rbuffer[ndx], expected, update, std::memory_order_release))
            {
                try_update_tail(tail_copy + 1);
                return true;
            }
            tls_lfrbq_stats.producer_retries++;
        }
    }

    bool dequeue_sc(uintptr_t *value)
    {
        seq_t head_copy = head.load(std::memory_order_acquire);

        unsigned int ndx = seq2ndx(head_copy);
        lfrbq_node *node = &rbuffer[ndx];

        seq_t node_seq = node->seq.load(std::memory_order_acquire);

        if (node_seq != seq2node(head_copy)) {
            return false;   // empty
        }

        *value = node->value.load(std::memory_order_relaxed);
        head.store(head_copy + 1, std::memory_order_release);

        return true;
    }

    bool dequeue_mc(uintptr_t *value)
    {
        uintptr_t _value;
        seq_t head_copy = head.load(std::memory_order_acquire);
        outer:
        do {
            unsigned int ndx = seq2ndx(head_copy);

            seq_t node_seq = rbuffer[ndx].seq.load(std::memory_order_acquire);
            int64_t cc = xcmp(node_seq, seq2node(head_copy));
            if (cc < 0) {
                return false;   // seq < head  --  empty
            }
            else if (cc > 0) {  // seq > head  --  wrapped, reload head and retry
                tls_lfrbq_stats.consumer_wraps++;
                head_copy = head.load(std::memory_order_acquire);   // reload head
                // continue;
                goto outer;
            }
            else // seq == head
                ;

            _value = rbuffer[ndx].value.load(std::memory_order_acquire);
            tls_lfrbq_stats.consumer_retries++;
        }
        while (!head.compare_exchange_weak(head_copy, head_copy + 1, std::memory_order_acquire));
        tls_lfrbq_stats.consumer_retries--;

        *value = _value;
        return true;
    }

public:

    /**
     * @brief enqueue a value
     * @param value to be queued
     * @retval lfrbq_status::success enqueue succeeded
     * @retval lfrbq_status::full    enqueue failed - queue full
     * @retval lfrbq_status::closed  enqueue failed - queue closed
     */
    lfrbq_status try_enqueue(uintptr_t value)
    {
        if (closed())
            return lfrbq_status::closed;
        else if (sp_mode ? enqueue_sp(value) : enqueue_mp(value))
            return lfrbq_status::success;
        else
        {
            tls_lfrbq_stats.queue_full_count++;
            return lfrbq_status::full;
        }
    }

    /**
     * @brief dequeue a value
     * @param value address for returned value
     * @retval lfrbq_status::success dequeue succeeded
     * @retval lfrbq_status::empty    dequeue failed - queue empty
     * @retval lfrbq_status::closed  dequeue failed - queue is empty and closed
     */
    lfrbq_status try_dequeue(uintptr_t *value)
    {
        bool _closed = closed();

        if (sc_mode ? dequeue_sc(value) : dequeue_mc(value))
            return lfrbq_status::success;
        else if (_closed)
            return lfrbq_status::closed;
        else
        {
            tls_lfrbq_stats.queue_empty_count++;
            return lfrbq_status::empty;
        }
    }


};



/*==*/
