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

#include <atomic>
#include <thread>
#include <latch>

#include <time.h>
#include <sys/resource.h>
#include <stdio.h>
#include <locale.h>

#include <rbq.h>
#include <testconfig.h>

static inline uint64_t timeval_nsecs(struct timeval *t)
{
    return (t->tv_sec * 1'000'000'000) + (t->tv_usec * 1000);
}

static uint64_t gettimex(clock_t id)
{
    struct timespec t;
    clock_gettime(id, &t);
    uint64_t value = (t.tv_sec * 1'000'000'000) + t.tv_nsec;
    return value;
}

/** get thread cpu time in nanoseconds */
static inline uint64_t getcputime() { return gettimex(CLOCK_THREAD_CPUTIME_ID); }
/** get clock time in nanoseconds */
static inline uint64_t gettime() { return gettimex(CLOCK_MONOTONIC); }


struct stats_t {

        uint64_t producer_time;     // sum of all producer cpu times
        uint64_t consumer_time;     // sum of all producer cpu times

        uint32_t enqueue_count;     // count of enqueues
        uint32_t dequeue_count;     // count of dequeues

        uint64_t producer_sums;     // sum of all producer messages
        uint64_t consumer_sums;     // sum of all consumer messages

        // rusage stats
        uint32_t ru_nvcsw;          // voluntary context switches
        uint32_t ru_nivcsw;         // involuntary context switches
        uint64_t ru_utime;          // user cpu time
        uint64_t ru_stime;          // system cpu time

        uint64_t elapsed;          // overall elapsed time

        lfrbq_stats_t lfrbq_stats;

};

template<typename T, typename V>
void atomic_fetch_add(T& dest, V value)
{
    std::atomic_ref<T>(dest).fetch_add(value, std::memory_order_relaxed);
}

static void update_stats(stats_t& stats, stats_t& local_stats)
{
    struct rusage usage;

    getrusage(RUSAGE_THREAD, &usage);

    atomic_fetch_add(stats.ru_utime, timeval_nsecs(&(usage.ru_utime)));
    atomic_fetch_add(stats.ru_stime, timeval_nsecs(&(usage.ru_stime)));
    atomic_fetch_add(stats.ru_nvcsw, usage.ru_nvcsw);
    atomic_fetch_add(stats.ru_nivcsw, usage.ru_nivcsw);

    atomic_fetch_add(stats.producer_time, local_stats.producer_time);
    atomic_fetch_add(stats.consumer_time, local_stats.consumer_time);

    atomic_fetch_add(stats.enqueue_count, local_stats.enqueue_count);
    atomic_fetch_add(stats.dequeue_count, local_stats.dequeue_count);

    atomic_fetch_add(stats.producer_sums, local_stats.producer_sums);
    atomic_fetch_add(stats.consumer_sums, local_stats.consumer_sums);

    atomic_fetch_add(stats.lfrbq_stats.queue_full_count, tls_lfrbq_stats.queue_full_count);
    atomic_fetch_add(stats.lfrbq_stats.queue_empty_count, tls_lfrbq_stats.queue_empty_count);
    atomic_fetch_add(stats.lfrbq_stats.producer_waits, tls_lfrbq_stats.producer_waits);
    atomic_fetch_add(stats.lfrbq_stats.consumer_waits, tls_lfrbq_stats.consumer_waits);
    atomic_fetch_add(stats.lfrbq_stats.producer_retries, tls_lfrbq_stats.producer_retries);
    atomic_fetch_add(stats.lfrbq_stats.consumer_retries, tls_lfrbq_stats.consumer_retries);
    atomic_fetch_add(stats.lfrbq_stats.producer_wraps, tls_lfrbq_stats.producer_wraps);
    atomic_fetch_add(stats.lfrbq_stats.consumer_wraps, tls_lfrbq_stats.consumer_wraps);
}

/**
 * pause
 * 
 * @param count number of pause instructions
 */
static void pause(const uint32_t count)
{
    for (unsigned int ndx = 0; ndx < count; ndx++)
    {
        __builtin_ia32_pause();
    }

}



void producer(rbq* queue, std::latch* latch, stats_t* stats, testconfig_t* config)
{
    stats_t local_stats = {};

    const uint32_t count = config->count;
    // const unsigned int pause_count = config->pause_count;

    latch->arrive_and_wait();

    uint64_t t0 = getcputime();
    
    for (uint32_t ndx = 0; ndx < count; ndx++)
    {
        lfrbq_status status = queue->enqueue(ndx);
        if (status != lfrbq_status::success)
            break;

        // pause(pause_count);
        
        local_stats.producer_sums += ndx;
        local_stats.enqueue_count++;
    }

    uint64_t t1 = getcputime();
    local_stats.producer_time = (t1 - t0);


    update_stats(*stats, local_stats);
}

void consumer(rbq* queue, std::latch* latch, stats_t* stats, testconfig_t* config)
{
    stats_t local_stats = {};

    // const unsigned int pause_count = config->pause_count;

    latch->arrive_and_wait();

    uint64_t t0 = getcputime();
    
    for (;;)
    {
        uintptr_t value;
        lfrbq_status status = queue->dequeue(&value);
        if (status != lfrbq_status::success)
            break;

        // pause(pause_count);
        
        local_stats.consumer_sums += value;
        local_stats.dequeue_count++;
    }

    uint64_t t1 = getcputime();
    local_stats.consumer_time = (t1 - t0);


    update_stats(*stats, local_stats);
}

static void print_stats(FILE *out, testconfig_t& config, stats_t& stats);


int main(int argc, char** argv)
{
    stats_t stats = {};
    testconfig_t config;

    uint64_t elapsed;

    bool rc = parse_options(&config, argc, argv);
    if (!rc) {
        fprintf(stderr, "config parse options returned false\n");

        return 1;
    }

    rbq queue(config.queue_size, config.qtype, config.sync);

    std::thread producers[config.nproducers];
    std::thread consumers[config.nconsumers];

    std::latch latch(config.nproducers + config.nconsumers + 1);


    for (int ndx = 0; ndx < config.nproducers; ndx++)
    {
        producers[ndx] = std::thread(producer, &queue, &latch, &stats, &config);
    }

    for (int ndx = 0; ndx < config.nconsumers; ndx++)
    {
        consumers[ndx] = std::thread(consumer, &queue, &latch, &stats, &config);
    }

    latch.arrive_and_wait();

    uint64_t x0 = gettime();

    for (int ndx = 0; ndx < config.nproducers; ndx++)
    {
        producers[ndx].join();
    }

    queue.close();

    for (int ndx = 0; ndx < config.nconsumers; ndx++)
    {
        consumers[ndx].join();
    }

    uint64_t x1 = gettime();
    stats.elapsed = (x1 - x0);

    print_stats(stdout, config, stats);


    return 0;
}


static double avg(double nanoseconds, double count, double scale)
{
    return count == 0.0 ? 0.0 : (nanoseconds/count)/scale;
}

static const char* zz(uint64_t a, uint64_t b)
{
    return a == b ? "==" : "<>";
}


static void print_stats(FILE *out, testconfig_t& config, stats_t& stats)
{

    char *current = setlocale(LC_NUMERIC, "");
    char current_locale[64];
    strncpy(current_locale, current, 64);
    // fprintf(out,"current locale = %s\n", current_locale);


    // locale_t templocale = newlocale(LC_NUMERIC_MASK, "en_US.UTF-8", (locale_t) 0);
    locale_t templocale = newlocale(LC_NUMERIC_MASK, current_locale, (locale_t) 0);
    locale_t prevlocale = uselocale(templocale);  // or (locale_t) 0 or LC_GLOBAL_LOCALE

    if (config.quiet)
    {
        double avg_overall = avg(stats.elapsed, stats.enqueue_count, 1);
        double aggregate_rate = avg_overall == 0.0 ? 0.0 : 1e9 / avg_overall;
        fprintf(out, "  overall rate = %'10.4f /sec\n", aggregate_rate);
    }

    else
    {
        fprintf(out, "Statistics:\n");
        fprintf(out, "  producer count = %u\n", config.nproducers);
        fprintf(out, "  consumer count = %u\n", config.nconsumers);

        fprintf(out, "  -- aggregate producer/consumer stats --\n");
        unsigned int enqueue_count_expected = (config.nproducers * config.count);
        unsigned int enqueue_count = stats.enqueue_count;
        unsigned int dequeue_count = stats.dequeue_count;

        fprintf(out, "  producer enqueue count = %'u %s %'u (expected)\n", enqueue_count, zz(enqueue_count, enqueue_count_expected), enqueue_count_expected);
        fprintf(out, "  consumer dequeue count = %'u %s %'u (expected)\n", dequeue_count, zz(dequeue_count, enqueue_count_expected), enqueue_count_expected);

        uint64_t count = config.count;
        uint64_t nprod = config.nproducers;
        uint64_t expected_sums = ((count * (count - 1))/2) * nprod;
        fprintf(out, "  producer message sums = %'llu %s %'llu (expected)\n", stats.producer_sums, zz(stats.producer_sums, expected_sums), expected_sums);
        fprintf(out, "  consumer message sums = %'llu %s %'llu (expected)\n", stats.consumer_sums, zz(stats.consumer_sums, expected_sums), expected_sums);

        fprintf(out, "  producer cpu time = %'llu nsecs\n", stats.producer_time);
        fprintf(out, "  consumer cpu time = %'llu nsecs\n", stats.consumer_time);

        double avg_enqueue_time = avg(stats.producer_time, enqueue_count, 1);
        double enq_rate = avg_enqueue_time == 0.0 ? 0.0 : 1e9 / avg_enqueue_time;

        double avg_dequeue_time = avg(stats.consumer_time, dequeue_count, 1);
        double deq_rate = avg_dequeue_time == 0.0 ? 0.0 : 1e9 / avg_dequeue_time;


        fprintf(out, "  avg enqueue time = %'10.4f nsecs\n", avg_enqueue_time);
        fprintf(out, "  avg dequeue time = %'10.4f nsecs\n", avg_dequeue_time);

        fprintf(out, "  enqueue rate = %'14.4f /sec\n", enq_rate);
        fprintf(out, "  dequeue rate = %'14.4f /sec\n", deq_rate);

        // fprintf(out, "  producer waits = %'llu\n", stats.producer_waits);
        // fprintf(out, "  consumer waits = %'llu\n", stats.consumer_waits);

        // fprintf(out, "  enqueue try count = %'u\n", stats.lfrb_stats.enqueue_try_count);
        // fprintf(out, "  dequeue try count = %'u\n", stats.lfrb_stats.dequeue_try_count);
        // fprintf(out, "  queue full count = %'u\n", stats.lfrb_stats.queue_full_count);
        // fprintf(out, "  queue empty count = %'u\n", stats.lfrb_stats.queue_empty_count);

        fprintf(out, "\n");
        fprintf(out, "  -- process stats --\n");

        fprintf(out, "  voluntary context switches = %'ld\n", stats.ru_nvcsw);
        fprintf(out, "  involuntary context switches = %'ld\n", stats.ru_nivcsw);
        fprintf(out, "  user cpu time = %'lld nsecs\n", stats.ru_utime);
        fprintf(out, "  system cpu time = %'lld nsecs\n", stats.ru_stime);

        double elapsed_cpu = (stats.ru_utime + stats.ru_stime)/1e9;
        fprintf(out, "  total cpu time = %'8.4f secs\n", elapsed_cpu);

        fprintf(out, "\n");

        double elapsed = stats.elapsed/1e9;
        fprintf(out, "  elapsed time = %'8.4f secs\n", elapsed);

        double avg_overall = avg(stats.elapsed, enqueue_count, 1);
        double aggregate_rate = avg_overall == 0.0 ? 0.0 : 1e9 / avg_overall;
        fprintf(out, "  average enq/deq time %'10.4f nsecs\n", avg_overall);
        fprintf(out, "  overall rate = %'10.4f /sec\n", aggregate_rate);

        // avg_overall = avg((stats.ru_utime + stats.ru_stime), enqueue_count, 1);
        // aggregate_rate = avg_overall == 0.0 ? 0.0 : 1e9 / avg_overall;
        // fprintf(out, "  *overall enq/deq time %10.4f nsecs\n", avg_overall);
        // fprintf(out, "  *aggregate rate = %10.4
        
        fprintf(out, "\n  -- client stats --\n");

        fprintf(out, "  queue full count  = %lu\n", stats.lfrbq_stats.queue_full_count);
        fprintf(out, "  queue empty count = %lu\n", stats.lfrbq_stats.queue_empty_count);
        fprintf(out, "  producer waits    = %lu\n", stats.lfrbq_stats.producer_waits);
        fprintf(out, "  consumer waits    = %lu\n", stats.lfrbq_stats.consumer_waits);

        fprintf(out, "  producer retries  = %lu\n", stats.lfrbq_stats.producer_retries);
        fprintf(out, "  consumer retries  = %lu\n", stats.lfrbq_stats.consumer_retries);

        fprintf(out, "  producer wraps    = %lu\n", stats.lfrbq_stats.producer_wraps);
        fprintf(out, "  consumer wraps    = %lu\n", stats.lfrbq_stats.consumer_wraps);
    }

    uselocale(prevlocale);
    freelocale(templocale);

}

