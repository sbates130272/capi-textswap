////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 PMC-Sierra, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0 Unless required by
// applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for
// the specific language governing permissions and limitations under the
// License.
//
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
//   Author: Stephen Bates
//
//   Description:
//     A simple IO tester to treat the CAPI AFU like a simple block
//     interface and do some simple IO testing.
//
////////////////////////////////////////////////////////////////////////

#include "textswap.h"
#include "readthrd.h"
#include "writethrd.h"
#include "version.h"

#include <libcxl.h>
#include <capi/capi.h>
#include <capi/wqueue.h>
#include <capi/wqueue_emul.h>
#include <capi/snooper.h>
#include <capi/worker.h>
#include <capi/macro.h>

#include <argconfig/argconfig.h>
#include <argconfig/report.h>
#include <argconfig/suffix.h>

#include <sys/time.h>
#include <unistd.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

const char program_desc[]  =
    "Perform simple IO testing on the AFU";

struct config {
    char *device;
    int verbose;
    int software;
    unsigned long buffer;
    unsigned long io;
    unsigned long seed;
    unsigned rwmix;
    int croom;
    unsigned queue_len;
    unsigned long numio;
};

static const struct config defaults = {
    .device        = "/dev/cxl/afu0.0d",
    .buffer        = CAPI_CACHELINE_BYTES * 16,
    .io            = 512,
    .seed          = 1,
    .croom         = -1,
    .rwmix         = 100,
    .numio         = 16,
    .queue_len     = 16,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"b",          "NUM",  CFG_LONG_SUFFIX, &defaults.buffer, required_argument, NULL},
    {"buffer",     "NUM",  CFG_LONG_SUFFIX, &defaults.buffer, required_argument,
            "buffer size to work within (like size in fio, in bytes)"},
    {"c",          "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument, NULL},
    {"croom",      "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument,
            "croom tag credits to permit (per direction). Set to < 0 to use default"},
    {"d",             "STRING", CFG_STRING, &defaults.device, required_argument, NULL},
    {"device",        "STRING", CFG_STRING, &defaults.device, required_argument,
            "the /dev/ path to the CAPI device"},
    {"i",          "NUM",  CFG_LONG_SUFFIX, &defaults.io, required_argument, NULL},
    {"io",         "NUM",  CFG_LONG_SUFFIX, &defaults.io, required_argument,
            "IO size of each transfer (bytes)"},
    {"n",          "NUM",  CFG_LONG_SUFFIX, &defaults.numio, required_argument, NULL},
    {"numio",      "NUM",  CFG_LONG_SUFFIX, &defaults.numio, required_argument,
            "Number of IO in this run"},
    {"q",          "NUM",  CFG_POSITIVE, &defaults.queue_len, required_argument, NULL},
    {"queue",      "NUM",  CFG_POSITIVE, &defaults.queue_len, required_argument,
            "Queue length"},
    {"r",          "NUM",  CFG_POSITIVE, &defaults.rwmix, required_argument, NULL},
    {"rwmix",      "NUM",  CFG_POSITIVE, &defaults.rwmix, required_argument,
            "Read/write mix (percentage read)"},
    {"s",          "NUM",  CFG_LONG, &defaults.seed, required_argument, NULL},
    {"seed",       "NUM",  CFG_LONG, &defaults.seed, required_argument,
            "lfsr seed (set to 0 for random)"},
    {"S",           "", CFG_NONE, &defaults.software, no_argument, NULL},
    {"software",    "", CFG_NONE, &defaults.software, no_argument,
            "use sotfware emulation"},
    {"v",           "", CFG_INCREMENT, NULL, no_argument, NULL},
    {"verbose",     "", CFG_INCREMENT, &defaults.verbose, no_argument,
            "be verbose"},
    {0}
};

struct queue_thread {
    struct worker worker;
    void *buffer;
    size_t bufsize;
    size_t iosize;
    unsigned rwmix;
    unsigned numio;
    unsigned reads, writes;
};

static void *queue_thread(void *arg)
{
    struct queue_thread *t = container_of(arg, struct queue_thread, worker);

    size_t maxios = t->bufsize / t->iosize;
    struct {
        uint8_t buf[t->iosize];
    } *ios = t->buffer;

    for (unsigned i = 0; i < t->numio; i++) {
        int off = rand() % maxios;
        int read = rand() % 100 < t->rwmix;

        struct wqueue_item it = {
            .dst = &ios[off],
            .src = &ios[off],
            .src_len = t->iosize,
        };

        // Let's define a 'read' as from the CAPI card to local memory.
        // and a 'write' as from local memory to the CAPI card.
        if (read) {
            __sync_add_and_fetch(&t->reads, 1);
            it.flags = WQ_WRITE_ONLY_FLAG | WQ_PROC_LFSR_FLAG;
        } else {
            __sync_add_and_fetch(&t->writes, 1);
            it.flags = WQ_PROC_MEMCPY_FLAG;
        }

        if (i == t->numio-1)
            it.flags |= WQ_LAST_ITEM_FLAG;

        wqueue_push(&it);
    }

    worker_finish_thread(&t->worker);
    return NULL;
}

static int pop_loop(double *hw_time)
{
    struct wqueue_item it;
    unsigned count = 0;
    double total_duration = 0.0;

    do {
        int error_code = wqueue_pop(&it);

        if (error_code) {
            fprintf(stderr, "Error 0x%04x processing buffer (dst 0x%p)\n",
                    error_code, it.dst);
            return -1;
        }
        total_duration += wqueue_calc_duration(&it);
        count++;

    } while (!(it.flags & WQ_LAST_ITEM_FLAG));

    *hw_time = total_duration;

    return count;
}

int main (int argc, char *argv[])
{
    int ret = 0;
    struct config cfg;
    struct queue_thread qt = {};

    argconfig_append_usage("INPUT [OUTPUT]");
    argconfig_parse(argc, argv, program_desc, command_line_options,
                    &defaults, &cfg, sizeof(cfg));

    if (cfg.seed == 0) {
        srand(time(NULL));
    } else {
        printf("Using Seed: %ld\n", cfg.seed);
        srand(cfg.seed);
    }

    if (cfg.buffer & (CAPI_CACHELINE_BYTES-1)) {
        fprintf(stderr, "Buffer must be a multiple of the cache line size (%d)\n",
                CAPI_CACHELINE_BYTES);
        return 1;
    }
    if (cfg.io & (CAPI_CACHELINE_BYTES-1)) {
        fprintf(stderr, "IO size must be a multiple of the cache line size (%d)\n",
                CAPI_CACHELINE_BYTES);
        return 1;
    }

    if ((qt.buffer = capi_alloc(cfg.buffer)) == NULL) {
        perror("capi_alloc");
        return 1;
    }

    memset(qt.buffer, 0, cfg.buffer);

    if (cfg.software)
        wqueue_emul_init();

    printf("Buffer %p - Len %ld\n", qt.buffer, cfg.buffer);

    snooper_init(&MMIO->snooper);
    if (wqueue_init(cfg.device, &MMIO->wq, cfg.queue_len)) {
        perror("Initializing wqueue");
        return 1;
    }

    if (cfg.seed) {
        cxl->mmio_write64(wqueue_afu(), &MMIO->lfsr_seed, cfg.seed);
        srand(cfg.seed);
    }

    if (!cfg.software && cfg.croom >= 0)
        wqueue_set_croom(cfg.croom);

    qt.bufsize = cfg.buffer;
    qt.iosize = cfg.io;
    qt.rwmix = cfg.rwmix;
    qt.numio = cfg.numio;

    double hw_duration = 0;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    worker_start(&qt.worker, 1, queue_thread);
    int io_count = pop_loop(&hw_duration);

    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    if (io_count < 0)
        return 2;

    if (!cfg.software && cfg.verbose) {
        snooper_dump(wqueue_afu());
        snooper_tag_usage(wqueue_afu());
        snooper_tag_stats(wqueue_afu(), cfg.verbose);
    }

    double read_bytes = qt.reads * cfg.io;
    double wrote_bytes = qt.writes * cfg.io;
    const char *read_suffix = suffix_dbinary_get(&read_bytes);
    const char *wrote_suffix = suffix_dbinary_get(&wrote_bytes);

    printf("\nRead:  %6.2f%sB\n", read_bytes, read_suffix);
    printf("Wrote: %6.2f%sB\n\n", wrote_bytes, wrote_suffix);


    printf("Hardware rate:  ");
    report_transfer_bin_rate_elapsed(stdout, hw_duration, cfg.io*io_count);
    printf("\n");
    printf("Software rate:  ");
    report_transfer_bin_rate(stdout, &start_time, &end_time, cfg.io*io_count);
    printf("\n");

    wqueue_cleanup();

    return ret;
}
