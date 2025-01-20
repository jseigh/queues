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
        fprintf(out, "  head = %llu\n", head.load(std::memory_order_relaxed));
        fprintf(out, "  tail = %llu\n", tail.load(std::memory_order_relaxed));

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

    seq_t get_head() { return head.load(); }


};


int main()
{
    fprintf(stdout, "lfrb node size=%d align= %d\n", sizeof(lfrbq_node), alignof(lfrbq_node));

    lfrbtest queue(8, true, true);

    queue.dump("init");

    int value = 1000;
    uintptr_t value2;

    for (int ndx = 0; ndx < 6; ndx++)
        queue.try_enqueue(value++);

    queue.dump("enqueue 6");

    for (int ndx = 0; ndx < 3; ndx++) {
        seq_t head = queue.get_head();
        value2 = 0;
        queue.try_dequeue(&value2);
        fprintf(stdout, "[%02d] ==> %llu\n", head, value2);
    }

    queue.dump("dequeue 3");

    for (int ndx = 0; ndx < 4; ndx++)
        queue.try_enqueue(value++);

    queue.dump("enqueue 4");


    return 0;
}


/**/