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
//     Main text swap executable code
//
////////////////////////////////////////////////////////////////////////

#include "textswap.h"
#include "readthrd.h"
#include "writethrd.h"
#include "version.h"

#include <libcxl.h>
#include <capi/capi.h>
#include <capi/utils.h>
#include <capi/wqueue.h>
#include <capi/wqueue_emul.h>

#include <argconfig/argconfig.h>
#include <argconfig/report.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>

const char program_desc[]  =
    "A CAPI demo that performs text replaces on data files";

struct config {
    char     *device;
    char     *phrase;
    char     *swap_phrase;
    unsigned read_threads;
    unsigned write_threads;
    unsigned long chunk;
    unsigned queue_len;
    int verbose;
    int version;
    int software;

    int read_discard;
    int write_discard;

    int copy;
    int read_only;

    int expected_matches;

    const char *finput;
    const char *foutput;
};

static const struct config defaults = {
    .device        = "/dev/cxl/afu0.0d",
    .phrase        = "GoPower8",
    .swap_phrase   = "Power8Go",
    .read_threads  = 4,
    .write_threads = 4,
    .chunk         = 8192,
    .queue_len     = 8,
    .expected_matches = -1,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"c",          "NUM",  CFG_LONG_SUFFIX, &defaults.chunk, required_argument, NULL},
    {"chunk",      "NUM",  CFG_LONG_SUFFIX, &defaults.chunk, required_argument,
            "chunk size for reading files and pushing to AFU (bytes)"},
    {"C",           "", CFG_NONE, &defaults.copy, no_argument, NULL},
    {"copy",        "", CFG_NONE, &defaults.copy, no_argument,
            "use the copy processor to copy the data to a new file"},
    {"d",             "STRING", CFG_STRING, &defaults.device, required_argument, NULL},
    {"device",        "STRING", CFG_STRING, &defaults.device, required_argument,
            "the /dev/ path to the CAPI device"},
    {"E",              "NUM", CFG_POSITIVE, &defaults.expected_matches, required_argument, NULL},
    {"expected",       "NUM", CFG_POSITIVE, &defaults.expected_matches, required_argument,
            "test if the number of matches equals an expected value"},
    {"p",             "STRING", CFG_STRING, &defaults.phrase, required_argument, NULL},
    {"phrase",        "STRING", CFG_STRING, &defaults.phrase, required_argument,
            "the ASCII phrase to search for (set command to CMD_D_TX_SRCH)"},
    {"q",              "NUM", CFG_POSITIVE, &defaults.queue_len, required_argument, NULL},
    {"queue",          "NUM", CFG_POSITIVE, &defaults.queue_len, required_argument,
            "number of wed queue entries"},
    {"r",              "NUM", CFG_POSITIVE, &defaults.read_threads, required_argument, NULL},
    {"read-threads",   "NUM", CFG_POSITIVE, &defaults.read_threads, required_argument,
            "number of read threads"},
    {"read-discard",   "", CFG_NONE, &defaults.read_discard, no_argument,
            "discard data after reading it (before going through the wqueue)"},
    {"R",            "", CFG_NONE, &defaults.read_only, no_argument, NULL},
    {"read-only",    "", CFG_NONE, &defaults.read_only, no_argument,
            "only search for matches (don't swap)"},
    {"s",             "STRING", CFG_STRING, &defaults.swap_phrase, required_argument, NULL},
    {"swap"  ,        "STRING", CFG_STRING, &defaults.swap_phrase, required_argument,
            "the ASCII phrae to replace the search phrase with"},
    {"S",           "", CFG_NONE, &defaults.software, no_argument, NULL},
    {"software",    "", CFG_NONE, &defaults.software, no_argument,
            "use sotfware emulation"},
    {"w",              "NUM", CFG_POSITIVE, &defaults.write_threads, required_argument, NULL},
    {"write-threads",  "NUM", CFG_POSITIVE, &defaults.write_threads, required_argument,
            "number of write threads"},
    {"write-discard",   "", CFG_NONE, &defaults.write_discard, no_argument,
            "discard data before writing it (after going through the wqueue)"},
    {"v",           "", CFG_INCREMENT, NULL, no_argument, NULL},
    {"verbose",     "", CFG_INCREMENT, &defaults.verbose, no_argument,
            "be verbose"},
    {"V",           "", CFG_NONE, &defaults.version, no_argument, NULL},
    {"version",     "", CFG_NONE, &defaults.version, no_argument,
            "print version information and exit"},
    {0}
};

static void print_cputime(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double user = utils_timeval_to_secs(&ru.ru_utime);
    double sys = utils_timeval_to_secs(&ru.ru_stime);

    fprintf(stderr, "Overall CPU Time:\n");
    fprintf(stderr, "   Tot    %.1fs user, %.1fs system\n", user, sys);
}

int main (int argc, char *argv[])
{
    int ret = 0;
    struct config cfg;
    struct writethrd *wt = NULL;

    argconfig_append_usage("INPUT [COPY_OUTPUT]");
    int args = argconfig_parse(argc, argv, program_desc, command_line_options,
                               &defaults, &cfg, sizeof(cfg));

    if (cfg.software) {
        wqueue_emul_init();
    }

    if (cfg.version) {
        wqueue_init(cfg.device, &MMIO->wq, cfg.queue_len);
        printf("Software Version:  \t%s\n", VERSION);
        build_version_print(stdout, wqueue_afu(), &MMIO->version);
        wqueue_cleanup();
        return 0;
    }

    if (args < 1 || args > 2) {
        argconfig_print_help(argv[0], program_desc, command_line_options);
        return 1;
    }

    if (args == 2 && !cfg.copy) {
        argconfig_print_help(argv[0], program_desc, command_line_options);
        return 1;
    }

    cfg.foutput = cfg.finput = argv[1];
    if (args == 2)
        cfg.foutput = argv[2];

    int read_flags = 0;
    int write_flags = 0;

    if (cfg.verbose >= 1) {
        write_flags |= WRITETHREAD_PRINT_OFFSETS;
        printf("Matches: \n");
    }

    if (cfg.verbose >= 3) {
        write_flags |= WRITETHREAD_VERBOSE;
        read_flags |= READTHREAD_VERBOSE;
    }

    if (strcmp(cfg.foutput, cfg.finput) != 0)
        write_flags |= WRITETHREAD_TRUNCATE | WRITETHREAD_ALWAYS_WRITE;

    if (cfg.read_discard)
        read_flags |= READTHREAD_DISCARD;

    if (cfg.write_discard)
        write_flags |= WRITETHREAD_DISCARD;

    if (cfg.copy) {
        read_flags |= READTHREAD_COPY;
        write_flags |= WRITETHREAD_COPY;
    }

    if (cfg.read_only)
        write_flags |= WRITETHREAD_SEARCH_ONLY;

    if (!cfg.read_discard) {
        if (wqueue_init(cfg.device, &MMIO->wq, cfg.queue_len)) {
            perror("Initializing wqueue");
            return 1;
        }

        textswap_set_phrase(wqueue_afu(), cfg.phrase);

        wt = writethrd_start(cfg.foutput, cfg.swap_phrase, cfg.write_threads,
                             write_flags);
        if (wt == NULL) {
            perror("Starting Write Threads");
            ret = 1;
            goto wqueue_cleanup;
        }
    }

    struct readthrd *rt = readthrd_start(cfg.finput, cfg.read_threads,
                                         read_flags);
    if (rt == NULL) {
        perror("Starting Read Threads");
        ret = 1;
        goto wqueue_cleanup;
    }

    size_t file_size = readthrd_file_size(rt);

    struct timeval start_time;
    gettimeofday(&start_time, NULL);

    readthrd_run(rt, cfg.chunk);
    readthrd_join(rt);

    writethrd_join(wt);

    if (cfg.verbose >= 2) {
        readthrd_print_cputime(rt);
        writethrd_print_cputime(wt);
        print_cputime();
    }

    printf("Transfer rate:\n  ");
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    report_transfer_bin_rate(stdout, &start_time, &end_time, file_size);
    printf("\n");

    if (!cfg.copy && !cfg.read_discard && !cfg.write_discard) {
        if (cfg.read_only)
            printf("Matches Found: %ld", writethrd_matches(wt));
        else
            printf("Matches Replaced: %ld", writethrd_matches(wt));

        if (cfg.expected_matches >= 0) {
            if (writethrd_matches(wt) == cfg.expected_matches) {
                printf(" (Good)");
            } else {
                printf(" (Bad!)");
                ret = 7;
            }
        }

        printf("\n");
    }

    readthrd_free(rt);
    writethrd_free(wt);

wqueue_cleanup:
    if (!cfg.read_discard)
        wqueue_cleanup();

    return ret;
}
