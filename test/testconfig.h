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

#ifndef TESTCONFIG_H
#define TESTCONFIG_H

#include <rbq.h>

#ifdef __cplusplus
extern "C" {
#endif


#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <stdbool.h>
#include <getopt.h>
#include <string.h>

#include <stdio.h>

/**
 * 
 */
static int find_enum(const char** names, char* opt)
{
    for (int ndx = 0; names[ndx] != NULL; ndx++) {
        if (strcasecmp(opt, names[ndx]) == 0)
            return ndx;
    }

    return -1;
}

static const char* qtype_names[] = {"mpmc", "mpsc", "spmc", "spsc", NULL};
static const lfrbq_type qtype[] = {mpmc, mpsc, spmc, spsc};
static const char* qtype_choices = "{mpmc, mpsc, spmc, spsc}";

static const char* sync_names[] = {"eventcount", "mutex", "yield", NULL};
static const rbq_sync sync_values[] = {rbq_sync::eventcount, rbq_sync::mutex, rbq_sync::yield };
static const char* sync_choices = "{eventcount, mutex, yield}";



typedef struct testconfig_t {
    unsigned int capacity;    // lfrb queue capacity -- must be a power of 2
    lfrbq_type qtype;

    const char* qtype_name;


    unsigned int nproducers;    // number of producer threads
    unsigned int nconsumers;    // number of consumer threads

    unsigned int count;         // enqueue count -- per producer

    // unsigned int pause_count;   // number of pause instructions

    rbq_sync sync;
    const char* sync_name;

    bool quiet;

    bool verbose;

    bool debug;


} testconfig_t;

static const testconfig_t testconfig_init = {
    capacity : 8192,
    qtype : mpmc,
    qtype_name : "mpmc",
    nproducers : 1,
    nconsumers : 1,
    count : 0,
    // pause_count : 0,
    sync : rbq_sync::eventcount,
    sync_name : "eventcount",
    quiet : false,
    verbose : false,
    debug : false,
};

enum optvals
{
    pause_val = 257,
};

static struct option long_options[] = {
    {"count", required_argument, 0, 'n'},
    {"type", required_argument, 0, 't'},
    {"producers", required_argument, 0, 'p'},
    {"consumers", required_argument, 0, 'c'},
    {"size", required_argument, 0, 's'},
    {"sync", required_argument, 0, 'x'},
    // {"pause", required_argument, 0, pause_val},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {"debug", no_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};

static bool check(const bool predicate, const char* msg) {
    if (predicate)
    {
        fprintf(stderr, "%s\n", msg);
    }
    return !predicate;
}

static bool parse_options(testconfig_t* config, int argc, char** argv)
{
    *config = testconfig_init;

    bool retval = true;

    bool help = false;
    bool verbose = false;

    int sz = 0;
    while (long_options[sz].name != 0) { sz++;}

    char* short_options = (char*) malloc((2*sz) + 1);
    memset(short_options, 0, (2*sz) + 1);
    int short_ndx = 0;
    for (int ndx = 0; ndx < sz ; ndx++)
    {
        char val = long_options[ndx].val;

        switch (long_options[ndx].has_arg)
        {
            case no_argument:
                short_options[short_ndx++] = val;
                break;
            case required_argument:
                short_options[short_ndx++] = val;
                short_options[short_ndx++] = ':';
                break;
        }
    }


    for (;;)
    {
        //int this_option_optind = optind ? optind : 1;
        int option_index = 0;

        int c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        int ndx;

        switch (c)
        {
            case 'n':
                config->count = strtoul(optarg, NULL, 10);
                break;
            case 't':
                ndx = find_enum(qtype_names, optarg);
                if (ndx >= 0) {
                    config->qtype = qtype[ndx];
                    config->qtype_name = optarg;
                }
                else {
                    fprintf(stderr, "unknown type=%s\n", optarg);
                    retval = false;
                }
                break;
            case 'p':
                config->nproducers = strtoul(optarg, NULL, 10);
                break;
            case 'c':      
                config->nconsumers = strtoul(optarg, NULL, 10);
                break;
            case 's':
                config->capacity = strtoul(optarg, NULL, 10);
                break;
            case 'x':
                ndx = find_enum(sync_names, optarg);
                if (ndx >= 0) {
                    config->sync = sync_values[ndx];
                    config->sync_name = optarg;
                    // fprintf(stderr, "sync=%s\n", optarg);
                }
                else {
                    fprintf(stderr, "unknown sync=%s\n", optarg);
                    retval = false;
                }
                break;
            // case pause_val:
            //     config->pause_count = strtoul(optarg, NULL, 10);
            //     break;
            case 'q':
                config->quiet = true;
                break;
            case 'v':
                config->verbose = true;
                verbose = true;
                break;
            case 'd':
                config->debug = true;
                break;
            case 'h':
                help = true;
                break;
            case '?':
                help = true;
                break;
            default:
                break;
        }

    }

    free(short_options);

    if (config->sync != mutex)
    {
        switch (config->qtype)
        {
            case mpmc:
                break;
            case mpsc:
                retval &= check(config->nconsumers > 1, "nconsumers can only be 1");
                break;
            case spmc:
                retval &= check(config->nproducers > 1, "nproducers can only be 1");
                break;
            case spsc:
                retval &= check(config->nconsumers > 1, "nconsumers can only be 1");
                retval &= check(config->nproducers > 1, "nproducers can only be 1");
                break;
            default:
                break;
        }
    }


    if (help) {
        fprintf(stderr, "  -n --count <arg>  enqueue count per producer thread (default %u)\n", testconfig_init.count);
        fprintf(stderr, "  -t --type <arg>  queue type %s (default %s)\n", qtype_choices, testconfig_init.qtype_name);
        fprintf(stderr, "  -p --producers <arg>  number of producer threads (default %u)\n", testconfig_init.nproducers);
        fprintf(stderr, "  -c --consumers <arg>  number of producer threads (default %u)\n", testconfig_init.nconsumers);
        fprintf(stderr, "  -x --sync <name> queue enqueue/dequeue synchronization %s (default %s)\n", sync_choices, testconfig_init.sync_name);
        fprintf(stderr, "  -s --size <arg>  queue capacity (power of 2) (default %u)\n", testconfig_init.capacity);
        fprintf(stderr, "  -q --quiet less output (default false)\n");
        fprintf(stderr, "  -v --verbose show config values (default false)\n");
        fprintf(stderr, "  -h --help show config values (default false)\n");

        retval = false;
    }

    else if (verbose)
    {
        fprintf(stderr, "Test configuration:\n");
        fprintf(stderr, "  count=%u\n", config->count);
        fprintf(stderr, "  type=%s\n", config->qtype_name);
        fprintf(stderr, "  producers=%u\n", config->nproducers);
        fprintf(stderr, "  consumers=%u\n", config->nconsumers);
        fprintf(stderr, "  sync=%s\n", config->sync_name);
        fprintf(stderr, "  capacity=%u\n", config->capacity);
        fprintf(stderr, "  quiet=%s\n", config->quiet ? "true" : "false");
        fprintf(stderr, "  verbose=%s\n", config->verbose ? "true" : "false");
        fprintf(stderr, "  debug=%s\n", config->debug ? "true" : "false");
    }



    return retval;

    // return optind;

}

#ifdef __cplusplus
}
#endif


#endif // TESTCONFIG_H