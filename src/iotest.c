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

#include <argconfig/argconfig.h>
#include <argconfig/report.h>

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
  unsigned long numio;
};

static const struct config defaults = {
    .device        = "/dev/cxl/afu0.0d",
    .buffer        = CAPI_CACHELINE_BYTES * 16,
    .io            = 512,
    .seed          = 1,
    .croom         = -1,
    .rwmix         = 100,
    .numio         = 1024*1024,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"d",             "STRING", CFG_STRING, &defaults.device, required_argument, NULL},
    {"device",        "STRING", CFG_STRING, &defaults.device, required_argument,
            "the /dev/ path to the CAPI device"},
    {"b",          "NUM",  CFG_LONG_SUFFIX, &defaults.buffer, required_argument, NULL},
    {"buffer",     "NUM",  CFG_LONG_SUFFIX, &defaults.buffer, required_argument,
            "buffer size to work within (like size in fio, in bytes)"},
    {"i",          "NUM",  CFG_LONG_SUFFIX, &defaults.io, required_argument, NULL},
    {"io",         "NUM",  CFG_LONG_SUFFIX, &defaults.io, required_argument,
            "IO size of each transfer (bytes)"},
    {"n",          "NUM",  CFG_LONG_SUFFIX, &defaults.numio, required_argument, NULL},
    {"numio",      "NUM",  CFG_LONG_SUFFIX, &defaults.numio, required_argument,
            "Number of IO in this run"},
    {"s",          "NUM",  CFG_LONG, &defaults.seed, required_argument, NULL},
    {"seed",       "NUM",  CFG_LONG, &defaults.seed, required_argument,
            "lfsr seed (set to 0 for random)"},
    {"r",          "NUM",  CFG_POSITIVE, &defaults.rwmix, required_argument, NULL},
    {"rwmix",      "NUM",  CFG_POSITIVE, &defaults.rwmix, required_argument,
            "Read/write mix (percentage read)"},
    {"c",          "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument, NULL},
    {"croom",      "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument,
            "croom tag credits to permit (per direction). Set to < 0 to use default"},
    {"S",           "", CFG_NONE, &defaults.software, no_argument, NULL},
    {"software",    "", CFG_NONE, &defaults.software, no_argument,
            "use sotfware emulation"},
    {"v",           "", CFG_INCREMENT, NULL, no_argument, NULL},
    {"verbose",     "", CFG_INCREMENT, &defaults.verbose, no_argument,
            "be verbose"},
    {0}
};

static int run_io(uint64_t *dst, struct config *cfg, double *duration)
{
    unsigned long countio = 0;
    duration = NULL;
    
    struct wqueue_item it = {
        .dst     = dst,
        .src_len = cfg->io,
        .flags   = WQ_WRITE_ONLY_FLAG | WQ_PROC_LFSR_FLAG,
    };

    while(1)
    {
      if (countio >= cfg->numio)
	break;
      
      countio++;
      //printf("%lu\t%lu\n", countio, cfg->numio);
      wqueue_push(&it);

      if (cfg->verbose) {
        for (int i = 0; i < 20; i++) {
	  uint64_t debug;
	  cxl->mmio_read64(wqueue_afu(), &MMIO->wq.debug, &debug);
	  printf("DBG: %"PRIx64"\n", debug);
        }
      }
      
      int error_code = wqueue_pop(&it);

      if (duration != NULL)
        *duration = wqueue_calc_duration(&it);

      if (cfg->verbose) {
        uint64_t debug;
        cxl->mmio_read64(wqueue_afu(), &MMIO->wq.debug, &debug);
        printf("DBG: %"PRIx64"\n", debug);
      }

      if (error_code) {
        fprintf(stderr, "Error 0x%04x processing buffer (dst 0x%p)\n",
                error_code, dst);
        return 1;
      }
    }

    return 0;
}

static int check_counts(size_t len)
{
    uint32_t rcount, wcount;
    cxl->mmio_read32(wqueue_afu(), &MMIO->wq.read_count, &rcount);
    cxl->mmio_read32(wqueue_afu(), &MMIO->wq.write_count, &wcount);

    int rbad = 0, wbad = 0;
    if (rcount != 0)
        rbad = 1;

    if (wcount != len / CAPI_CACHELINE_BYTES)
        wbad = 1;

    printf("Read Count:  %d (%s)\n", rcount, rbad ? "Fail" : "Good");
    printf("Write Count: %d (%s)\n", wcount, wbad ? "Fail" : "Good");

    return wbad || rbad;
}

static void dump(uint64_t *dst, size_t len)
{
    for (int i = 0; i < len / sizeof(*dst); i++) {
        printf(" %4d - %016"PRIx64"\n", i, dst[i]);

        if (i > 20)
            break;
    }
}

int main (int argc, char *argv[])
{
    int ret = 0;
    struct config cfg;

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

    uint64_t *dst;

    if ((dst = capi_alloc(cfg.buffer)) == NULL) {
        perror("capi_alloc");
        return 1;
    }

    memset(dst, 0, cfg.buffer);

    if (cfg.software)
        wqueue_emul_init();

    printf("Dst %p - Len %ld\n", dst, cfg.buffer);

    snooper_init(&MMIO->snooper);
    if (wqueue_init(cfg.device, &MMIO->wq, 4)) {
        perror("Initializing wqueue");
        return 1;
    }

    if (cfg.seed) {
        cxl->mmio_write64(wqueue_afu(), &MMIO->lfsr_seed, cfg.seed);
        srand(cfg.seed);
    }

    if (!cfg.software && cfg.croom >= 0)
        wqueue_set_croom(cfg.croom);

    double duration = 0;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    ret |= run_io(dst, &cfg, &duration);

    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    if (!cfg.software) {
        ret |= check_counts(cfg.io*cfg.numio);
        if (cfg.verbose)
            snooper_dump(wqueue_afu());
    }
    snooper_tag_usage(wqueue_afu());
    snooper_tag_stats(wqueue_afu(), cfg.verbose);

    printf("Hardware rate:  ");
    report_transfer_bin_rate_elapsed(stdout, duration, cfg.io*cfg.numio);
    printf("\n");
    printf("Software rate:  ");
    report_transfer_bin_rate(stdout, &start_time, &end_time, cfg.io*cfg.numio);
    printf("\n");

    if (cfg.verbose)
      dump(dst, cfg.buffer);

    wqueue_cleanup();

    return ret;
}
