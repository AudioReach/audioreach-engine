/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <posal.h>
#include <spf_main.h>
#include <gpr_api.h>

int main(void)
{
    int rc = 0;
    posal_init();

    rc = gpr_init();
    if (0 != rc) {
        printf("gpr_init() failed with status %d\n", rc);
        return rc;
    }

    rc = spf_framework_pre_init();
    if (0 != rc) {
        printf("spf_framework_pre_init() failed with status %d\n", rc);
        return rc;
    }

    rc = spf_framework_post_init();
    if (0 != rc) {
        printf("spf_framework_post_init() failed with status %d\n", rc);
        return rc;
    }

    printf("spf framework initialized.\n", rc);

    return 0;
}