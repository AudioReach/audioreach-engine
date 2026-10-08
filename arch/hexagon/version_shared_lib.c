/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

typedef struct note_type
{
   int  size_name;
   int  size_desc;
   int  type;
   char name[100];
} note_type;

const note_type so_ver __attribute__((section(".note.lib.ver")))
                       __attribute__((visibility("default")))
                       __attribute__((aligned(0x1000))) = {
   100,
   0,
   0,
   { CAPI_SO_VERSION },
};
