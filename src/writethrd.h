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
//     Write thread which pops buffers from the wqueue and processes
//     their results. Multiple threads either copy the buffer to another
//     file or swaps the target phrase at the indexes found.
//
////////////////////////////////////////////////////////////////////////

#ifndef WRITETHRD_H
#define WRITETHRD_H

#include <stdlib.h>
#include <capi/fifo.h>

enum {
    WRITETHREAD_DISCARD = 1,
    WRITETHREAD_TRUNCATE = 2,
    WRITETHREAD_ALWAYS_WRITE = 4,
    WRITETHREAD_VERBOSE = 8,
    WRITETHREAD_COPY = 16,
    WRITETHREAD_SEARCH_ONLY = 32,
    WRITETHREAD_PRINT_OFFSETS = 64,
};

struct writethrd *writethrd_start(const char *fpath, const char *swap_phrase,
                                  int num_threads, int flags);
void writethrd_join(struct writethrd *wt);
void writethrd_print_cputime(struct writethrd *wt);
void writethrd_free(struct writethrd *wt);
unsigned long writethrd_matches(struct writethrd *wt);



#endif
