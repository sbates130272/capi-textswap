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
//   Author: Logan Gunthorpe
//
//   Description:
//     Unit test which checks simple memory copy transfers using the
//     memcpy proc.
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
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>

const char program_desc[]  =
    "Unit tests for the textswap code";

struct config {
    char *device;
    int verbose;
    int read_only;
    int software;
    int test_error;
    int random;
    unsigned long seed;
    unsigned long length;
    int croom;
};

static const struct config defaults = {
    .device        = "/dev/cxl/afu0.0d",
    .length        = CAPI_CACHELINE_BYTES * 16,
    .croom         = -1,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"d",             "STRING", CFG_STRING, &defaults.device, required_argument, NULL},
    {"device",        "STRING", CFG_STRING, &defaults.device, required_argument,
            "the /dev/ path to the CAPI device"},
    {"E",           "", CFG_NONE, &defaults.test_error, no_argument, NULL},
    {"error",       "", CFG_NONE, &defaults.test_error, no_argument,
            "test error handling"},
    {"n",          "NUM",  CFG_LONG_SUFFIX, &defaults.length, required_argument, NULL},
    {"length",     "NUM",  CFG_LONG_SUFFIX, &defaults.length, required_argument,
            "length of data to transfer (bytes)"},
    {"c",          "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument, NULL},
    {"croom",      "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument,
            "croom tag credits to permit (per direction). Set to < 0 to use default"},
    {"r",           "", CFG_NONE, &defaults.read_only, no_argument, NULL},
    {"read-only",   "", CFG_NONE, &defaults.read_only, no_argument,
            "read only"},
    {"R",           "", CFG_NONE, &defaults.random, no_argument, NULL},
    {"random",      "", CFG_NONE, &defaults.random, no_argument,
            "use random data"},
    {"seed",      "NUM",  CFG_LONG, &defaults.seed, required_argument,
            "seed to use for randomization in the PSL simulator"},
    {"S",           "", CFG_NONE, &defaults.software, no_argument, NULL},
    {"software",    "", CFG_NONE, &defaults.software, no_argument,
            "use sotfware emulation"},
    {"v",           "", CFG_INCREMENT, NULL, no_argument, NULL},
    {"verbose",     "", CFG_INCREMENT, &defaults.verbose, no_argument,
            "be verbose"},
    {0}
};

static int copy(uint64_t *src, uint64_t *dst, struct config *cfg,
                double *duration)
{
    struct wqueue_item it = {
        .src = src,
        .dst = dst,
        .src_len = cfg->length,
        .flags = WQ_PROC_MEMCPY_FLAG,
    };

    if (cfg->test_error)
        it.src = NULL;
    if (cfg->read_only)
        it.dst = src;

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
        fprintf(stderr, "Error 0x%04x processing buffer (src 0x%p, dst 0x%p)\n",
                error_code, src, dst);
        return !cfg->test_error;
    }


    return cfg->test_error;
}

static int check_counts(size_t len, int read_only, int dump)
{
    uint32_t rcount, wcount;
    cxl->mmio_read32(wqueue_afu(), &MMIO->wq.read_count, &rcount);
    cxl->mmio_read32(wqueue_afu(), &MMIO->wq.write_count, &wcount);

    int rbad = 0, wbad = 0;
    if (rcount != len / CAPI_CACHELINE_BYTES)
        rbad = 1;

    if (read_only) {
        if (wcount != 0)
            wbad = 1;
    } else {
        if (wcount != len / CAPI_CACHELINE_BYTES)
            wbad = 1;
    }

    printf("Read Count:  %d (%s)\n", rcount, rbad ? "Fail" : "Good");
    printf("Write Count: %d (%s)\n", wcount, wbad ? "Fail" : "Good");

    snooper_tag_usage(wqueue_afu());
    snooper_tag_stats(wqueue_afu(), dump);

    return wbad || rbad;
}

static int check_xor(uint64_t *src, size_t len)
{
    uint64_t expected = wqueue_xor_sum();
    for (int i = 0; i < len / sizeof(*src); i++)
        expected ^= src[i];

    uint64_t hw = snooper_xor_sum(wqueue_afu());
    printf("SNP XOR: %016"PRIx64" (Exp: %016"PRIx64") %s\n",
           hw, expected, hw == expected ? "Matches" : "FAILURE");

    return hw != expected;
}

static int check_tag_alert()
{
    uint64_t expected = 0;
    uint64_t hw = snooper_tag_alert(wqueue_afu());
    printf("SNP TAGS: %016"PRIx64" (Exp: %016"PRIx64") %s\n",
           hw, expected, hw == expected ? "Matches" : "FAILURE");

    return hw != expected;
}

static void dump_mismatches(uint64_t *src, uint64_t *dst, size_t len)
{
    int count = 0;
    for (int i = 0; i < len / sizeof(*src); i++) {
        if (src[i] != dst[i]) {
            printf("%8d - %016"PRIx64"  %016"PRIx64"\n", i, src[i], dst[i]);
            count++;;
        }

        if (count > 20)
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

    if (cfg.length & (CAPI_CACHELINE_BYTES-1)) {
        fprintf(stderr, "Length must be a multiple of the cache line size (%d)\n",
                CAPI_CACHELINE_BYTES);
        return 1;
    }

    uint64_t *src, *dst;

    if ((src = capi_alloc(cfg.length)) == NULL ||
        (dst = capi_alloc(cfg.length)) == NULL)
    {
        perror("capi_alloc");
        return 1;
    }

    memset(dst, 0, cfg.length);

    if (!cfg.random) {
        for (int i = 0; i < cfg.length / sizeof(*src); i++)
            src[i] = i;
    } else {
        for (int i = 0; i < cfg.length / sizeof(*src); i++)
            src[i] = rand() | ((uint64_t) rand() << 32);
    }

    if (cfg.software)
        wqueue_emul_init();

    printf("Src %p - Dst %p - Len %ld\n", src, dst, cfg.length);

    snooper_init(&MMIO->snooper);
    if (wqueue_init(cfg.device, &MMIO->wq, 4)) {
        perror("Initializing wqueue");
        ret = 1;
        goto free_buffers;
    }

    if (!cfg.software && cfg.croom >= 0)
        wqueue_set_croom(cfg.croom);

    double duration;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    ret |= copy(src, dst, &cfg, &duration);

    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    if (cfg.test_error) {
        cfg.test_error = 0;
        gettimeofday(&start_time, NULL);
        ret |= copy(src, dst, &cfg, &duration);
        gettimeofday(&end_time, NULL);
        cfg.test_error = 1;
    }

    if (!cfg.software && !cfg.test_error) {
      ret |= check_counts(cfg.length, cfg.read_only, cfg.verbose);
        if (cfg.verbose)
            snooper_dump(wqueue_afu());
        ret |= check_xor(src, cfg.length);
        ret |= check_tag_alert();
    }

    printf("Hardware rate:  ");
    report_transfer_bin_rate_elapsed(stdout, duration, cfg.length);
    printf("\n");
    printf("Software rate:  ");
    report_transfer_bin_rate(stdout, &start_time, &end_time, cfg.length);
    printf("\n");

    if (!cfg.read_only) {
        if (memcmp(src, dst, cfg.length) == 0) {
            printf("Buffers matched!\n");
        } else {
            if (cfg.verbose >= 2)
                dump_mismatches(src, dst, cfg.length);

            printf("FAILED: Buffers did not match!\n");
            ret = 1;
        }
    }

    wqueue_cleanup();

free_buffers:
    free(src);
    free(dst);

    return ret;
}
