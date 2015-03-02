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
//     Software emulation of the textswap RTL processors.
//
////////////////////////////////////////////////////////////////////////

#include "textswap.h"

#include <capi/capi.h>
#include <capi/proc.h>
#include <capi/build_version.h>

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>


struct proc {
    char needle[16];
    int match_so_far;
    int offset;
};


struct proc *proc_init(void)
{
    struct proc *ret = calloc(1, sizeof(*ret));
    ret->match_so_far = -1;

    if (ret == NULL) {
        perror("Allocating proc struct");
        exit(-1);
    }

    build_version_emul_init("Software Emulation");

    return ret;
}

int proc_mmio_write64(struct proc *proc, void *offset, uint64_t data)
{
    if (offset == &MMIO->text_search) {
        memcpy(proc->needle, &data, sizeof(data));
        proc->match_so_far = -1;
        proc->offset = 0;
    } else if (offset == &MMIO->text_search[8]) {
        memcpy(&proc->needle[8], &data, sizeof(data));
        proc->match_so_far = -1;
        proc->offset = 0;
    }


    return 0;
}

int proc_mmio_write32(struct proc *proc, void *offset, uint32_t data)
{
    return 0;
}

int proc_mmio_read64(struct proc *proc, void *offset, uint64_t *data)
{
    if (build_version_mmio_read(&MMIO->version, offset,
                                data, sizeof(*data)) == 0)
        return 0;

    *data = 0;

    return 0;

}

int proc_mmio_read32(struct proc *proc, void *offset, uint32_t *data)
{
    if (build_version_mmio_read(&MMIO->version, offset,
                                data, sizeof(*data)) == 0)
        return 0;

    *data = 0;

    return 0;
}

static int memcpy_proc(struct proc *proc, int flags, const void *src, void *dst,
                       size_t len, int always_write, int *dirty, size_t *dst_len)
{
    if (always_write) {
        memcpy(dst, src, len);
        *dirty = 1;
    }

    *dst_len = len;

    return 0;
}

static int lfsr_proc(struct proc *proc, int flags, const void *src, void *dst,
                     size_t len, int always_write, int *dirty, size_t *dst_len)
{
    int *idst = (int *) dst;

    for (size_t i = 0; i < len / sizeof(*idst); i++)
        *idst++ = rand();

    *dirty = 1;
    *dst_len = len;

    return 0;
}

static inline int check_str(struct proc *proc, const char *haystack,
                            size_t haystack_len, int needle_len,
                            unsigned long *count, int start, int i)
{
    int j;
    for (j = start; j < needle_len; j++, i++) {
        if (i >= haystack_len) {
            proc->match_so_far = j;
            break;
        }

        if (haystack[i] != proc->needle[j]) {
            break;
        }
    }

    return j == needle_len;
}

static unsigned long count_sub_str(struct proc *proc, const char *haystack,
                                   int32_t *res, size_t haystack_len)
{
    int needle_len = strlen(proc->needle);
    unsigned long count = 0;

    if (proc->match_so_far >= 0) {
        if (check_str(proc, haystack, haystack_len, needle_len, &count,
                      proc->match_so_far, 0))
        {
            *res++ = -proc->match_so_far;
            count++;
        }
        proc->match_so_far = -1;
    }

    for (size_t i = 0; i < haystack_len; i++) {
        if (check_str(proc, haystack, haystack_len, needle_len, &count,
                      0,  i))
        {
            *res++ = i;
            count++;
        }
    }

    proc->offset += haystack_len;

    return count;
}



static int text_proc(struct proc *proc, int flags, const void *src, void *dst,
                     size_t len, int always_write, int *dirty, size_t *dst_len)
{
    int32_t *res = dst;

    unsigned long found = count_sub_str(proc, src, res, len);
    int res_per_line = CAPI_CACHELINE_BYTES/sizeof(*res);
    int top = (found + res_per_line - 1) & ~(res_per_line - 1);

    for (int i = found; i < top; i++)
        res[i] = INT32_MAX;

    *dst_len = top * sizeof(*res);
    *dirty = found > 0;

    return 0;
}

int proc_run(struct proc *proc, int flags, const void *src, void *dst,
             size_t len, int always_write, int *dirty, size_t *dst_len)
{
    if (flags & WQ_PROC_LFSR_FLAG)
        return lfsr_proc(proc, flags, src, dst, len, always_write,
                         dirty, dst_len);
    else if (flags & WQ_PROC_MEMCPY_FLAG)
        return memcpy_proc(proc, flags, src, dst, len, always_write,
                           dirty, dst_len);
    else
        return text_proc(proc, flags, src, dst, len, always_write,
                         dirty, dst_len);
}
