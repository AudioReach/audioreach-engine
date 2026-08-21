/**
 * \file posal_time.c
 * \brief
 *  This file contains stub implementations of the UTC time APIs for Linux targets.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/* ----------------------------------------------------------------------------
 * Include Files
 * ------------------------------------------------------------------------- */
#include "posal_time.h"
#include "ar_error_codes.h"

/* ----------------------------------------------------------------------------
 * Function Definitions
 * ------------------------------------------------------------------------- */

uint32_t posal_get_time_module_size()
{
   return 0;
}

int32_t posal_reset_utc_time_module(void *utc_time_module_ptr)
{
   return AR_EUNSUPPORTED;
}

int32_t posal_query_utc_time_from_nw(void *utc_time_module_ptr, uint32_t time_unit)
{
   return AR_EUNSUPPORTED;
}

int32_t posal_date_time_get_utc_time(void *utc_time_module_ptr,
                                     posal_time qtime,
                                     uint32_t requested_time_unit,
                                     uint32_t *utc_time_msw,
                                     uint32_t *utc_time_lsw)
{
   return AR_EUNSUPPORTED;
}
