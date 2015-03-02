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
//     Unit test for ensuring the correct functionality of the text
//     processor which searches for strings in a stream of data.
//
////////////////////////////////////////////////////////////////////////

#include "textswap.h"
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
#include <ctype.h>
#include <limits.h>

const char program_desc[]  =
    "Unit tests for the textswap code";

struct config {
    char *device;
    int verbose;
    int software;
    unsigned long seed;
    unsigned long length;
    int croom;
    int nonoise;
    unsigned insert;
    char *phrase;
    int test_flow;
};

static const struct config defaults = {
    .device        = "/dev/cxl/afu0.0d",
    .length        = CAPI_CACHELINE_BYTES * 16,
    .croom         = -1,
    .insert        = 6,
    .phrase        = "GoPower8",
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"c",          "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument, NULL},
    {"croom",      "NUM",  CFG_LONG_SUFFIX, &defaults.croom, required_argument,
            "croom tag credits to permit (per direction). Set to < 0 to use default"},
    {"d",             "STRING", CFG_STRING, &defaults.device, required_argument, NULL},
    {"device",        "STRING", CFG_STRING, &defaults.device, required_argument,
            "the /dev/ path to the CAPI device"},
    {"n",          "NUM",  CFG_LONG_SUFFIX, &defaults.length, required_argument, NULL},
    {"length",     "NUM",  CFG_LONG_SUFFIX, &defaults.length, required_argument,
            "length of data to transfer (bytes)"},
    {"i",           "NUM",  CFG_POSITIVE, &defaults.insert, required_argument, NULL},
    {"insert",      "NUM",  CFG_POSITIVE, &defaults.insert, required_argument,
            "the number of times to insert 'phrase'"},
    {"N",           "", CFG_NONE, &defaults.nonoise, no_argument, NULL},
    {"nonoise",     "", CFG_NONE, &defaults.nonoise, no_argument,
            "don't generate random noise"},
    {"p",             "STRING", CFG_STRING, &defaults.phrase, required_argument, NULL},
    {"phrase",        "STRING", CFG_STRING, &defaults.phrase, required_argument,
            "the ASCII phrase to use as a needle"},
    {"seed",      "NUM",  CFG_LONG, &defaults.seed, required_argument,
            "seed to use for randomization in the PSL simulator"},
    {"test-flow",   "", CFG_NONE, &defaults.test_flow, no_argument,
            "test flow control by flooding the result with matches"},
    {"S",           "", CFG_NONE, &defaults.software, no_argument, NULL},
    {"software",    "", CFG_NONE, &defaults.software, no_argument,
            "use sotfware emulation"},
    {"v",           "", CFG_INCREMENT, NULL, no_argument, NULL},
    {"verbose",     "", CFG_INCREMENT, &defaults.verbose, no_argument,
            "be verbose"},
    {0}
};

static void gen_haystack(char *haystack, size_t len)
{
    while(len) {
        char x = rand();
        if (!isalnum(x)) continue;
        *haystack++ = x;
        len--;
    }
}


static int contains(int pos, int i, size_t *locs, size_t len) {
    for (int j = 0; j < i; j++) {
        if (pos >= locs[j] && pos <= locs[j] + len)
            return 1;
        if (pos + len >= locs[j] && pos <= locs[j])
            return 1;
    }

    return 0;
}

static void insert_needles(char *haystack, size_t *locs, struct config *cfg)
{
    size_t plen = strlen(cfg->phrase);
    size_t pos;
    int j;
    int i = 0;

    if (i < cfg->insert) {
        locs[i] = cfg->length / 2 - 1;
        strcpy(&haystack[locs[i++]], cfg->phrase);
    }

    if (i < cfg->insert && cfg->length > CAPI_CACHELINE_BYTES) {
        locs[i] = CAPI_CACHELINE_BYTES - 14;
        strcpy(&haystack[locs[i++]], cfg->phrase);
    }

    if (i < cfg->insert && cfg->length > CAPI_CACHELINE_BYTES*3) {
        locs[i] = 2*CAPI_CACHELINE_BYTES - 2;
        strcpy(&haystack[locs[i++]], cfg->phrase);
    }

    for (; i < cfg->insert; i++) {
        for (j = 0; j < 500; j++) {
            pos = rand() % (cfg->length - plen);

            if (!contains(pos, i, locs, plen))
                break;
        }

        if (j == 500) {
            fprintf(stderr, "Error: Unable to insert phrase at enough unique locations!\n");
            exit(2);
        }

        locs[i] = pos;
        strcpy(&haystack[pos], cfg->phrase);
    }
}

static void test_flow(char *haystack, size_t *locs, struct config *cfg)
{
    memset(haystack, '.', cfg->length);
    textswap_set_phrase(wqueue_afu(), ".");
    for (int i = 0; i < cfg->insert; i++)
        locs[i] = i;
}

int cmpfunc(const void *a, const void *b)
{
    return *(size_t *) a - *(size_t *) b;
}


static int search(char *needle, int32_t *indexes, size_t length,
                  double *duration, unsigned offset)
{
    struct wqueue_item it = {
        .src = needle,
        .dst = indexes,
        .src_len = length,
    };

    wqueue_push(&it);

    int error_code = wqueue_pop(&it);

    if (duration != NULL)
        *duration += wqueue_calc_duration(&it);

    if (error_code) {
        fprintf(stderr, "Error 0x%04x processing buffer (src 0x%p, dst 0x%p)\n",
                error_code, it.src, it.dst);
        return -1;
    }

    unsigned found =  it.dst_len / sizeof(*indexes);
    for (int i = 0; i < found; i++) {
        if (indexes[i] == INT32_MAX)
            break;
        indexes[i] += offset;
    }

    return found;
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

    if (cfg.test_flow)
        cfg.insert = cfg.length;

    char *haystack;
    int32_t *indexes;
    size_t locs[cfg.insert];

    if ((haystack = capi_alloc(cfg.length)) == NULL ||
        (indexes = capi_alloc(cfg.length*sizeof(*indexes))) == NULL)
    {
        perror("capi_alloc");
        return 1;
    }

    if (cfg.software)
        wqueue_emul_init();

    snooper_init(&MMIO->snooper);
    if (wqueue_init(cfg.device, &MMIO->wq, 4)) {
        perror("Initializing wqueue");
        ret = 1;
        goto free_buffers;
    }

    textswap_set_phrase(wqueue_afu(), cfg.phrase);
    if (!cfg.test_flow) {
        if (cfg.nonoise)
            memset(haystack, '.', cfg.length);
        else
            gen_haystack(haystack, cfg.length);

        insert_needles(haystack, locs, &cfg);
    } else {
        test_flow(haystack, locs, &cfg);
    }

    qsort(locs, cfg.insert, sizeof(*locs), cmpfunc);

    if (cfg.verbose > 1) {
        printf("Inserted Locations:\n");
        for (int i = 0; i < cfg.insert; i++)
            printf("  - %zd\n", locs[i]);
    }

    if (cfg.verbose > 2) {
        printf("\nData Words:\n");
        for (int i = 0; i < cfg.length / 64; i++) {
            fwrite(&haystack[i*64], 1, 64, stdout);
            printf("\n");
        }
        printf("\n");
    }

    double duration = 0;
    struct timeval start_time;
    gettimeofday(&start_time, NULL);
    int found = search(haystack, indexes, cfg.length / 2, &duration, 0);
    int found2 = search(&haystack[cfg.length / 2], &indexes[found],
                        cfg.length / 2, &duration, cfg.length / 2);
    struct timeval end_time;
    gettimeofday(&end_time, NULL);

    if (found < 0 || found2 < 0) {
        ret = 1;
        goto print_rates;
    }

    found += found2;

    int j = 0;
    for (int i = 0; i < found; i++) {
        if (indexes[i] == INT32_MAX)
            continue;

        if (j >= cfg.insert) {
            ret = 3;
            break;
        }

        if (locs[j] != indexes[i])  {
            if (cfg.verbose)
                printf(" %"PRId32" - %zd\n", indexes[i], locs[j]);
            ret = 2;
        }
        j++;
    }

    if (!ret && j == cfg.insert)
        printf("All matches found!\n");
    else
        printf("Failed: matches not found!\n");

print_rates:
    printf("Hardware rate:  ");
    report_transfer_bin_rate_elapsed(stdout, duration, cfg.length);
    printf("\n");
    printf("Software rate:  ");
    report_transfer_bin_rate(stdout, &start_time, &end_time, cfg.length);
    printf("\n");

    wqueue_cleanup();

free_buffers:
    free(haystack);
    free(indexes);

    return ret;
}
