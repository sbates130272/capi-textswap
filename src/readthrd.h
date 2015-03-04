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
//     Read thread code which loads a file (using multiple threads) and
//     sends the chunks, in order, to wqueue for processing by the
//     hardware.
//
////////////////////////////////////////////////////////////////////////

#ifndef READTHRD_H
#define READTHRD_H

#include <capi/fifo.h>
#include <stdlib.h>

enum {
    READTHREAD_DISCARD = 1,
    READTHREAD_VERBOSE = 2,
    READTHREAD_COPY = 4,
};

struct readthrd_item {
    unsigned index;
    int last;
    size_t offset;
    size_t bytes;
    size_t real_bytes;
    size_t result_bytes;
    void *buf;
};

struct readthrd *readthrd_start(const char *fpath, int num_threads, int flags);
size_t readthrd_run(struct readthrd *rt, size_t chunk_size, size_t read_size);
void readthrd_print_cputime(struct readthrd *rt);
void readthrd_join(struct readthrd *rt);
void readthrd_free(struct readthrd *rt);
size_t readthrd_file_size(struct readthrd *rt);





#endif
