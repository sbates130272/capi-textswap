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

#include "readthrd.h"
#include "textswap.h"

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


struct readthrd {
    struct worker worker;
    const char *fpath;
    struct fifo *input;
    int flags;
    ssize_t file_size;
    struct readthrd_item **reorder_buf;
    unsigned *reorder_idx;
    int reorder_len;

    pthread_t wqueue_thrd;
    struct rusage wqueue_rusage;

    pthread_mutex_t mutex;
    pthread_cond_t ready_cond;
    pthread_cond_t free_cond;
};

static void *read_thread(void *arg)
{
    struct readthrd *rt = container_of(arg, struct readthrd, worker);

    int fd = open(rt->fpath, O_RDONLY);
    if (fd < 0) {
        perror("read thread open");
        return NULL;
    }

    struct readthrd_item *item;

    while ((item = fifo_pop(rt->input)) != NULL) {
        lseek(fd, item->offset, SEEK_SET);

        size_t memsize = item->bytes;
        if (!(rt->flags & READTHREAD_COPY))
            memsize *= sizeof(uint32_t);

        unsigned char *buf;
        if ((buf = capi_alloc(memsize)) == NULL) {
            perror("read thread alloc");
            break;
        }

        ssize_t rd = read(fd, buf, item->bytes);
        memset(&buf[rd], 0, item->bytes - rd);

        item->buf = buf;

        pthread_mutex_lock(&rt->mutex);
        int idx = item->index % rt->reorder_len;
        while (rt->reorder_buf[idx] != NULL ||
               rt->reorder_idx[idx] != item->index)
            pthread_cond_wait(&rt->free_cond, &rt->mutex);
        rt->reorder_buf[idx] = item;
        pthread_cond_signal(&rt->ready_cond);
        pthread_mutex_unlock(&rt->mutex);
    }

    close(fd);
    worker_finish_thread(&rt->worker);

    return NULL;
}

static void *wqueue_thread(void *arg)
{
    int idx = 0;
    struct readthrd *rt = container_of(arg, struct readthrd, worker);

    int last = 0;
    while(!last) {
        pthread_mutex_lock(&rt->mutex);
        while(rt->reorder_buf[idx] == NULL)
            pthread_cond_wait(&rt->ready_cond, &rt->mutex);

        struct readthrd_item *item = rt->reorder_buf[idx];
        rt->reorder_idx[idx] += rt->reorder_len;
        rt->reorder_buf[idx] = NULL;
        pthread_cond_broadcast(&rt->free_cond);
        idx++;
        idx %= rt->reorder_len;
        pthread_mutex_unlock(&rt->mutex);

        last = item->last;

        if (rt->flags & READTHREAD_VERBOSE)
            printf("Put buffer %d: %p for %zd\n", item->index, item->buf,
                   item->offset);

        if (rt->flags & READTHREAD_DISCARD) {
            free(item->buf);
            free(item);
            continue;
        }

        struct wqueue_item witem;
        witem.flags = 0;
        if (rt->flags & READTHREAD_COPY)
            witem.flags |= WQ_PROC_MEMCPY_FLAG | WQ_ALWAYS_WRITE_FLAG;;
        if (last)
            witem.flags |= WQ_LAST_ITEM_FLAG;

        witem.src = item->buf;
        witem.dst = item->buf;
        witem.src_len = item->bytes;
        witem.opaque = item;

        wqueue_push(&witem);
    }

    getrusage(RUSAGE_THREAD, &rt->wqueue_rusage);

    return NULL;
}

static ssize_t check_file(const char *f)
{
    int fd = open(f, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Unable to open '%s': %s\n", f, strerror(errno));
        return -1;
    }

    struct stat s;
    ssize_t ret = -1;
    if (!fstat(fd, &s))
        ret = s.st_size;
    else
        fprintf(stderr, "Unable to get file size of '%s': %s\n", f, strerror(errno));

    close(fd);
    return ret;
}


static int next_power_of_2(int x)
{
    return (x & -x) + x;
}

struct readthrd *readthrd_start(const char *fpath,
                                int num_threads, int flags)
{
    struct readthrd *rt = malloc(sizeof(*rt));
    if (rt == NULL)
        return NULL;

    rt->input = fifo_new(next_power_of_2(num_threads*2));
    if (rt->input == NULL)
        goto error_out;
    fifo_open(rt->input);

    rt->file_size = check_file(fpath);
    if (rt->file_size < 0)
        goto error_fifo_out;

    rt->fpath = fpath;
    rt->flags = flags;

    rt->reorder_len = num_threads * 2;
    rt->reorder_buf = calloc(rt->reorder_len, sizeof(*rt->reorder_buf));
    if (rt->reorder_buf == NULL)
        goto error_fifo_out;

    rt->reorder_idx = malloc(rt->reorder_len * sizeof(*rt->reorder_idx));
    if (rt->reorder_idx == NULL)
        goto error_reorder_buf_out;

    for (unsigned i = 0; i < rt->reorder_len; i++)
        rt->reorder_idx[i] = i;

    if (pthread_mutex_init(&rt->mutex, NULL))
        goto error_reorder_out;

    if (pthread_cond_init(&rt->ready_cond, NULL))
        goto error_mutex_out;

    if (pthread_cond_init(&rt->free_cond, NULL))
        goto error_ready_cond_out;

    if (pthread_create(&rt->wqueue_thrd, NULL, wqueue_thread, rt))
        goto error_free_cond_out;

    if (worker_start(&rt->worker, num_threads, read_thread))
        goto error_wqueue_stop;

    return rt;

error_wqueue_stop:
    pthread_cancel(rt->wqueue_thrd);
error_free_cond_out:
    pthread_cond_destroy(&rt->free_cond);
error_ready_cond_out:
    pthread_cond_destroy(&rt->ready_cond);
error_mutex_out:
    pthread_mutex_destroy(&rt->mutex);
error_reorder_out:
    free(rt->reorder_idx);
error_reorder_buf_out:
    free(rt->reorder_buf);
error_fifo_out:
    fifo_free(rt->input);
error_out:
    free(rt);
    return NULL;
}

void readthrd_print_cputime(struct readthrd *rt)
{
    fprintf(stderr, "Read Thread CPU Time:\n");
    worker_print_cputime(&rt->worker, &rt->wqueue_rusage, "W");
}

void readthrd_join(struct readthrd *rt)
{
    worker_join(&rt->worker);
    pthread_join(rt->wqueue_thrd, NULL);
}

void readthrd_free(struct readthrd *rt)
{
    worker_free(&rt->worker);
    pthread_cond_destroy(&rt->free_cond);
    pthread_cond_destroy(&rt->ready_cond);
    pthread_mutex_destroy(&rt->mutex);
    fifo_free(rt->input);
    free(rt->reorder_buf);
    free(rt);
}


static size_t round_to_cache_line(size_t x)
{
    x += CAPI_CACHELINE_BYTES - 1;
    x &= ~(CAPI_CACHELINE_BYTES - 1);
    return x;
}

size_t readthrd_run(struct readthrd *rt, size_t chunk_size, size_t read_size)
{
    size_t offset = 0;
    ssize_t remain = rt->file_size;
    unsigned idx = 0;

    if (read_size && remain > read_size)
        remain = read_size;

    while (1) {
        struct readthrd_item *it = malloc(sizeof(*it));
        if (it == NULL) {
            perror("readthrd_item malloc");
            exit(1);
        }

        it->index = idx++;
        it->offset = offset;
        it->bytes = chunk_size;
        it->real_bytes = chunk_size;
        if (it->bytes > remain) {
            it->bytes = round_to_cache_line(remain);
            it->real_bytes = remain;
        }

        it->last = 0;

        offset += it->bytes;
        remain -= it->bytes;
        if (remain <= 0)
            it->last = 1;

        fifo_push(rt->input, it);

        if (it->last)
            break;
    }

    fifo_close(rt->input);
    return offset;
}

size_t readthrd_file_size(struct readthrd *rt)
{
    return rt->file_size;
}
