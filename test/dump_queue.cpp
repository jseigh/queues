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
#include <iostream>

#include <stdio.h>

#include <lfrbq.h>
#include <rbq.h>

#include <eventcount.h>

static const char* qtype_names[] = {"mpmc", "mpsc", "spmc", "spsc", NULL};
static const lfrbq_type qtype[] = {mpmc, mpsc, spmc, spsc};

static lfrbq_type find_qtype(char* opt)
{
    for (int ndx = 0; qtype_names[ndx] != NULL; ndx++) {
        if (strcasecmp(opt, qtype_names[ndx]) == 0)
            return qtype[ndx];
    }

    return lfrbq_type::mpmc;
}


const char* status_str(lfrbq_status status)
{
    switch (status)
    {
    case lfrbq_status::success: return "success";
    case lfrbq_status::full: return "full";
    case lfrbq_status::empty: return "empty";
    case lfrbq_status::closed: return "closed";
    case lfrbq_status::fail: return "fail";
    default: return "?";
    }
}

class lfrbtest : public lfrbq
{
public:
    using lfrbq::lfrbq;

    // unsigned int get_size()
    // {
    //     return head.load(std::memory_order_relaxed) - tail.load(std::memory_order_relaxed) - this->size;
    // }


    void dump(FILE* out, std::string label)
    {
        fprintf(out, "%s:\n", label.c_str());
        seq_t head_copy = head.load(std::memory_order_relaxed);
        seq_t tail_copy = tail.load(std::memory_order_relaxed);
        uint32_t q_size = (tail_copy + size) - head_copy;

        fprintf(out, "  head = %llu head.seq=%llu head.ndx=%u\n", head_copy, seq2node(head_copy), seq2ndx(head_copy));
        fprintf(out, "  tail = %llu tail.seq=%llu tail.ndx=%u\n", tail_copy, seq2node(tail_copy), seq2ndx(tail_copy));
        fprintf(out, "  capacity=%u size=%u status=%s\n",
            size, q_size, qclosed ? "closed" : "open");

        for (unsigned int ndx = 0; ndx < size; ndx++)
        {
            seq_t node_seq = rbuffer[ndx].seq.load(std::memory_order_relaxed);
            seq_t node_vseq = seq2node(node_seq) + ndx;
            fprintf(out, "  node[%02d]: seq=%llu (%llu) value=%llu\n",
                ndx,
                node_seq, node_vseq,
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

        lfrbq_status status = try_enqueue(value);
        fprintf(stdout, "[%02d] value=%llu cc=%d (%s)\n", 
            ndx, value, status, status_str(status));
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
        lfrbq_status status = try_dequeue(&value2);
        fprintf(stdout, "[%02d] ==> %llu cc=%d (%s)\n", 
            ndx, value2, status, status_str(status));
    }


};


static unsigned int get_count()
{
    unsigned int count;
    std::cin >> count;
    if (std::cin.fail())
    {
        std::cin.clear();
        return 0;
    }
    else
    {
        return count;
    }
}


int main(int argc, char** argv)
{
    lfrbq_type qtype = lfrbq_type::mpmc;

    if (argc > 1)
        qtype = find_qtype(argv[1]);

    fprintf(stdout, "queue type = %s\n", qtype_names[qtype]);

    using string = std::string;

    string cmd_enq("enqueue");
    string cmd_deq("dequeue");
    string cmd_xchg("xchg");
    string cmd_close("close");
    string cmd_show("show");
    string cmd_quit("quit");
    string cmd_help("help");

    lfrbtest queue(8, mpmc);
    // lfrbtest queue(8, spsc);

    queue.info();

    queue.dump("init");

    int value = 1000;
    std::string cmd;
    unsigned int count;

    for (;;)
    {
        std::cin >> cmd;
        if (std::cin.eof())
            break;
        if (std::cin.bad())
            break;
        if (std::cin.fail())
            break;

        fprintf(stdout, "command=%s\n", cmd.c_str());

        if (cmd_enq.starts_with(cmd))
        {
            count = get_count();
            for (unsigned int ndx = 0; ndx < count; ndx++)
            {
                queue.enqueue(value++);
            }
        }

        else if (cmd_deq.starts_with(cmd))
        {
            count = get_count();
            for (unsigned int ndx = 0; ndx < count; ndx++)
            {
                queue.dequeue();
            }
        }

        else if (cmd_xchg.starts_with(cmd))
        {
            count = get_count();
            for (unsigned int ndx = 0; ndx < count; ndx++)
            {
                queue.enqueue(value++);
                queue.dequeue();
            }
        }

        else if (cmd_close.starts_with(cmd))
        {
            queue.close();
        }
        
        else if (cmd_show.starts_with(cmd))
        {
            queue.dump("queue");
        }
        
        else if (cmd_quit.starts_with(cmd))
        {
            break;
        }
        
        else if (cmd_help.starts_with(cmd))
        {
            std::cout << "enqueue <count>\n";
            std::cout << "dequeue <count>\n";
            std::cout << "xchg <count>  -- enqueue followed by dequeue\n";
            std::cout << "close\n";
            std::cout << "show\n";
            std::cout << "quit\n";
            std::cout << "help\n";
        }
        
        else
        {
            std::cout <<  "unknown command\n";
        }

        std::cin.ignore(1024, '\n');
        std::cout << "\n";
    }

    return 0;
}


/**/