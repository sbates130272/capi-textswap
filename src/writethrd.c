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

#include "writethrd.h"
#include "readthrd.h"

#include <capi/worker.h>
#include <capi/macro.h>
#include <capi/capi.h>
#include <capi/wqueue.h>

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
#include <inttypes.h>

struct writethrd {
    struct worker worker;
    const char *fpath;
    struct fifo *fifo;
    int flags;
    unsigned long matches;
    char swap_phrase[17];

    pthread_t wqueue_thrd;
    struct rusage wqueue_rusage;
};

static void *copy_thread(void *arg)
{
    struct writethrd *wt = container_of(arg, struct writethrd, worker);

    int fd = open(wt->fpath, O_WRONLY);
    if (fd < 0) {
        perror("copy thread open");
        return NULL;
    }

    struct readthrd_item *item;
    while ((item = fifo_pop(wt->fifo)) != NULL) {
        lseek(fd, item->offset, SEEK_SET);
        if (write(fd, item->buf, item->real_bytes) < 0) {
            perror("Copy Thread Write");
            exit(EIO);
        }

        free(item->buf);
        free(item);
    }

    close(fd);
    worker_finish_thread(&wt->worker);

    return NULL;
}

static void *swap_thread(void *arg)
{
    struct writethrd *wt = container_of(arg, struct writethrd, worker);
    int fd = -1;

    if (!(wt->flags & WRITETHREAD_SEARCH_ONLY)) {
        fd = open(wt->fpath, O_WRONLY);
        if (fd < 0) {
            perror("swap thread open");
            return NULL;
        }
    }

    size_t plen = strlen(wt->swap_phrase);
    struct readthrd_item *item;
    unsigned long matches = 0;
    while ((item = fifo_pop(wt->fifo)) != NULL) {

        uint32_t *indexes = item->buf;
        for (int i = 0; i < item->result_bytes / sizeof(*indexes); i++) {
            if (indexes[i] == INT32_MAX)
                break;

            int idx = indexes[i] + item->offset;

            matches++;

            if (wt->flags & WRITETHREAD_PRINT_OFFSETS)
                printf("%10"PRId32"\n", idx);

            if (wt->flags & WRITETHREAD_SEARCH_ONLY)
                continue;

            lseek(fd, idx, SEEK_SET);
            if (write(fd, wt->swap_phrase, plen) < 0) {
                perror("Swap Thread Write");
                exit(EIO);
            }
        }

        free(item->buf);
        free(item);
    }

    __sync_add_and_fetch(&wt->matches, matches);

    if (fd >= 0)
        close(fd);
    worker_finish_thread(&wt->worker);

    return NULL;
}


static void *wqueue_thread(void *arg)
{
    struct writethrd *wt = arg;

    int last = 0;
    unsigned next_index = 0;
    while(!last) {
        struct wqueue_item it;
        int error_code = wqueue_pop(&it);
        int dirty = it.flags & WQ_DIRTY_FLAG || wt->flags & WRITETHREAD_ALWAYS_WRITE;

        struct readthrd_item *item = it.opaque;

        if (error_code) {
            fprintf(stderr, "Error 0x%04x processing buffer %d (at 0x%p)\n",
                    error_code, item->index, item->buf);

            exit(EIO);
        }

        if (wt->flags & WRITETHREAD_VERBOSE)
            printf("Got Buffer %d: %p for %zd (%d)\n", item->index,
                   item->buf, item->offset, dirty);

        if (item->index != next_index++) {
            fprintf(stderr, "Error buffers came back out of order!\n");
            exit(EPIPE);
        }

        last = item->last;
        item->result_bytes = it.dst_len;

        if (wt->flags & WRITETHREAD_DISCARD || !dirty) {
            free(item->buf);
            free(item);
        } else {
            fifo_push(wt->fifo, item);
        }
    }

    fifo_close(wt->fifo);
    getrusage(RUSAGE_THREAD, &wt->wqueue_rusage);

    return NULL;
}

static int check_file(const char *f, int truncate)
{
    int fd = open(f, O_WRONLY | O_CREAT | (truncate ? O_TRUNC : 0), 0664);
    if (fd < 0) {
        fprintf(stderr, "Unable to open '%s': %s\n", f, strerror(errno));
        return -1;
    }

    close(fd);
    return 0;
}

static int next_power_of_2(int x)
{
    return (x & -x) + x;
}

struct writethrd *writethrd_start(const char *fpath, const char *swap_phrase,
                                  int num_threads, int flags)
{
    if (!(flags & WRITETHREAD_SEARCH_ONLY) &&
        check_file(fpath, flags & WRITETHREAD_TRUNCATE))
        return NULL;

    struct writethrd *wt = malloc(sizeof(*wt));
    if (wt == NULL)
        return NULL;

    wt->fifo = fifo_new(next_power_of_2(num_threads*2));
    if (wt->fifo == NULL)
        goto error_out;
    fifo_open(wt->fifo);

    wt->fpath = fpath;
    wt->flags = flags;
    wt->matches = 0;

    strncpy(wt->swap_phrase, swap_phrase, sizeof(wt->swap_phrase) - 1);
    wt->swap_phrase[sizeof(wt->swap_phrase) - 1] = 0;

    if (pthread_create(&wt->wqueue_thrd, NULL, wqueue_thread, wt))
        goto error_fifo_out;

    void *(*start_routine) (void *);
    start_routine = swap_thread;
    if (wt->flags & WRITETHREAD_COPY)
        start_routine = copy_thread;

    if (worker_start(&wt->worker, num_threads, start_routine))
        goto error_wqueue_stop;

    return wt;

error_wqueue_stop:
    pthread_cancel(wt->wqueue_thrd);
error_fifo_out:
    fifo_free(wt->fifo);
error_out:
    free(wt);
    return NULL;
}

void writethrd_join(struct writethrd *wt)
{
    if (wt == NULL) return;

    pthread_join(wt->wqueue_thrd, NULL);
    worker_join(&wt->worker);
}

void writethrd_print_cputime(struct writethrd *wt)
{
    if (wt == NULL) return;

    fprintf(stderr, "Write Thread CPU Time:\n");
    worker_print_cputime(&wt->worker, &wt->wqueue_rusage, "W");
}

void writethrd_free(struct writethrd *wt)
{
    if (wt == NULL) return;

    worker_free(&wt->worker);
    fifo_free(wt->fifo);
    free(wt);
}

unsigned long writethrd_matches(struct writethrd *wt)
{
    return wt->matches;
}
