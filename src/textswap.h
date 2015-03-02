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
//     Definitions describing the textswap RTL
//
////////////////////////////////////////////////////////////////////////

#ifndef TEXTSWAP_H
#define TEXTSWAP_H

#include <capi/wqueue.h>
#include <capi/snooper.h>
#include <capi/wqueue_emul.h>
#include <capi/build_version.h>

#include <string.h>

struct mmio {
    struct build_version_mmio version;
    struct wqueue_mmio wq;
    struct snooper_mmio snooper;
    uint64_t lfsr_seed;
    char text_search[16];
};

#define MMIO ((struct mmio *) 0)

static inline void textswap_set_phrase(struct cxl_afu_h *afu_h, const char *phrase)
{
    char temp[16];
    memset(temp, 0, sizeof(temp));
    strncpy(temp, phrase, 16);

    uint64_t *d = (uint64_t *) temp;
    cxl->mmio_write64(afu_h, &MMIO->text_search[0], *d++);
    cxl->mmio_write64(afu_h, &MMIO->text_search[8], *d);
}

enum {
    WQ_PROC_MEMCPY_FLAG  = (1 << 14),
    WQ_PROC_LFSR_FLAG    = (1 << 15),
};

#endif
