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

#include <thread>
#include <string>
#include <atomic>

#include <stdio.h>

#include <lfrbq.h>
#include <rbq.h>

#include <eventcount.h>


class lfrbtest : public lfrbq
{
public:
    using lfrbq::lfrbq;


    void dump(FILE* out, std::string label)
    {
        fprintf(out, "%s:\n", label.c_str());
        seq_t head_copy = head.load(std::memory_order_relaxed);
        seq_t tail_copy = tail.load(std::memory_order_relaxed);

        fprintf(out, "  head = %llu head.seq=%llu head.ndx=%u\n", head_copy, seq2node(head_copy), seq2ndx(head_copy));
        fprintf(out, "  tail = %llu tail.seq=%llu tail.ndx=%u\n", tail_copy, seq2node(tail_copy), seq2ndx(tail_copy));

        for (int ndx = 0; ndx < size; ndx++)
        {
            fprintf(out, "  node[%02d]: %4llu %4llu\n",
                ndx,
                rbuffer[ndx].seq.load(std::memory_order_relaxed),
                rbuffer[ndx].value.load(std::memory_order_relaxed),
                1);
        }
        fprintf(out, "\n\n");
    }

    void dump(std::string label)
    {
        dump(stdout, label);
    }

    void info()
    {
        fprintf(stdout, "mask = %llx seq_mask = %llx\n", mask, seq_mask);
    }

    void enqueue(uintptr_t value)
    {
        seq_t head_copy = head.load(std::memory_order_relaxed);
        seq_t head_seq = seq2node(head_copy);
        int head_ndx = seq2ndx(head_copy);


        seq_t tail_copy = tail.load(std::memory_order_relaxed);
        int ndx = seq2ndx(tail_copy);
        seq_t tail_seq = seq2node(tail_copy);
        seq_t node_seq = rbuffer[ndx].seq.load(std::memory_order_relaxed);
        fprintf(stdout, "enqueue: tail=%llu tail.seq=%llu, ndx=%u node.seq=%llu -- head=%llu head.seq=%llu head_ndx=%u\n",
            tail_copy,
            tail_seq,
            ndx,
            node_seq,
            head_copy,
            head_seq,
            head_ndx,
            1);

        bool cc = try_enqueue(value);
        fprintf(stdout, "[%02d] value=%llu cc=%d\n", 
            ndx, value, cc);
    }

    void dequeue()
    {
        seq_t head_copy = head.load(std::memory_order_relaxed);
        int ndx = seq2ndx(head_copy);
        seq_t head_seq = seq2node(head_copy);
        seq_t node_seq = rbuffer[ndx].seq.load(std::memory_order_relaxed);
        fprintf(stdout, "dequeue: head=%llu head.seq=%llu, ndx=%u node.seq=%llu\n",
            head_copy,
            head_seq,
            ndx,
            node_seq,
            1);

        uintptr_t value2 = 0;
        bool cc = try_dequeue(&value2);
        fprintf(stdout, "[%02d] ==> %llu cc=%d\n", 
            ndx, value2, cc);
    }


};



int main()
{
    fprintf(stdout, "lfrb node size=%d align= %d\n", sizeof(lfrbq_node), alignof(lfrbq_node));

    lfrbtest queue(8, mpmc);

    queue.info();

    queue.dump("init");

    int value = 1000;

    for (int ndx = 0; ndx < 6; ndx++)
        queue.enqueue(value++);

    queue.dump("enqueue 6");

    for (int ndx = 0; ndx < 3; ndx++) {
        queue.dequeue();
    }

    queue.dump("dequeue 3");

    for (int ndx = 0; ndx < 4; ndx++)
        queue.enqueue(value++);

    queue.dump("enqueue 4");

    for (int ndx = 0; ndx < 26; ndx++) {
        queue.dequeue();
        queue.enqueue(value++);
    }

    queue.dump("dequeue/enqueue 10");


    return 0;
}


/**/