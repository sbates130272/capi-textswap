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
//     Generate random haystack files with a specific phrase inserted
//     multiple times.
//
////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <argconfig/argconfig.h>
#include <capi/capi.h>

#define CAPI_CACHELINE_MASK (CAPI_CACHELINE_BYTES-1)

const char program_desc[]  =
    "Generate a haystack of random data with needle strings in it";

struct config {
    unsigned insert;
    int seed;
    size_t size;
    char *phrase;
    int disallow_cacheline_spanning;
    int printable;
};

static const struct config defaults = {
    .insert = 50,
    .seed = -1,
    .phrase = "GoPower8",
    .size = 1024*1024*16,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"C",           "", CFG_NONE, &defaults.disallow_cacheline_spanning, no_argument, NULL},
    {"cacheline",   "", CFG_NONE, &defaults.disallow_cacheline_spanning, no_argument,
            "do not insert phrase across cachelines"},
    {"i",           "NUM",  CFG_POSITIVE, &defaults.insert, required_argument, NULL},
    {"insert",      "NUM",  CFG_POSITIVE, &defaults.insert, required_argument,
            "the number of times to insert 'phrase'"},
    {"p",             "STRING", CFG_STRING, &defaults.phrase, required_argument, NULL},
    {"phrase",        "STRING", CFG_STRING, &defaults.phrase, required_argument,
            "the ASCII phrase to use as a needle"},
    {"P",           "", CFG_NONE, &defaults.printable, no_argument, NULL},
    {"printable",   "", CFG_NONE, &defaults.printable, no_argument,
            "only insert printable characters into the random data"},
    {"seed",        "NUM", CFG_INT, &defaults.seed, required_argument,
            "random number seed, set <0 for random seed"},
    {"s",          "NUM",  CFG_LONG_SUFFIX, &defaults.size, required_argument, NULL},
    {"size",       "NUM",  CFG_LONG_SUFFIX, &defaults.size, required_argument,
            "file size to generate"},
    {0}
};

static void make_printable(int *x)
{
    *x &= ~0x8080808080808080;

    char *c = (char *) x;
    for (int i = 0; i < sizeof(*x); i++) {
        if (c[i] < '\n')
            c[i] = '\n';
        else if (c[i] < ' ')
            c[i] = ' ';
        else if (c[i] > 0x7e)
            c[i] = ' ';
    }
}

static void generate_random_file(FILE *out, size_t size, int printable)
{
    int x;
    time_t start = time(NULL);
    time_t last_print = start;
    size_t wrote=0;

    while(size) {
        x = rand();
        if (printable)
            make_printable(&x);

        int l = sizeof(x);
        if (l > size) l = size;
        if (!fwrite(&x, l, 1, out)) {
            perror("Writing File");
            exit(3);
        }
        size -= l;
        wrote += l;

        if ((time(NULL)- last_print) > 2) {
            fprintf(stderr, "\rWrote %zdMiB", wrote >> 20);
            last_print = time(NULL);
        }
    }

    if (last_print != start)
        fprintf(stderr, "\rWrote %zdMiB\n", wrote >> 20);
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

static void insert_needles(FILE *out, const struct config *cfg)
{
    size_t plen = strlen(cfg->phrase);
    size_t locs[cfg->insert];
    size_t pos;
    int j;

    for (int i = 0; i < cfg->insert; i++) {
        for (j = 0; j < 500; j++) {
            pos = rand() % (cfg->size - plen);

            if (cfg->disallow_cacheline_spanning &&
                (pos & CAPI_CACHELINE_BYTES) != ((pos + plen) & CAPI_CACHELINE_BYTES))
                continue;

            if (!contains(pos, i, locs, plen))
                break;
        }

        if (j == 100) {
            fprintf(stderr, "Error: Unable to insert phrase at enough unique locations!\n");
            exit(2);
        }

        locs[i] = pos;
        fseek(out, pos, SEEK_SET);
        fwrite(cfg->phrase, plen, 1, out);
    }
}

int main (int argc, char *argv[])
{
    struct config cfg;

    argconfig_append_usage("[OUTPUT_FILE]");
    int args = argconfig_parse(argc, argv, program_desc, command_line_options,
                               &defaults, &cfg, sizeof(cfg));

    if (args > 1) {
        argconfig_print_help(argv[0], program_desc, command_line_options);
        return 1;
    }

    if (cfg.size <= 0) {
        fprintf(stderr, "Size argument must be greater than zero!");
        return 1;
    }

    FILE *output = stdout;
    if (args == 1) {
        output = fopen(argv[1], "w");
        if (output == NULL) {
            fprintf(stderr, "Error openning file '%s': %s\n", argv[1], strerror(errno));
            return 1;
        }
    }

    if (cfg.seed > 0)
        srand(cfg.seed);
    else
        srand(time(NULL));

    generate_random_file(output, cfg.size, cfg.printable);
    insert_needles(output, &cfg);

    return 0;
}
