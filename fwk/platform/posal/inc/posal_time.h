/* ======================================================================== */
/**
 * \file posal_time.h
 * \brief
 *   This file contains definitions for common time related APIs.
*/

/* =========================================================================
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
========================================================================= */

#ifndef _POSAL_TIME_H_
#define _POSAL_TIME_H_

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include "stringl.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus
/**
* @brief This value is to prevent a successful return on the very first bootup
* of the device before any network is camped.
*/
#define TEN_YEARS_IN_SECONDS 315532800

/*
Call Flow:
  module:
    (at Setup time)
    size = posal_get_time_module_size
    time_module = malloc(size)
    posal_reset_utc_time_module(time_module)

    (1st process call)
    posal_query_utc_time_from_nw(time_module, USEC) // read nw time at 1st process call, in micro-second

    (time to interpolate the qtimer ticks with utc time)
    posal_date_time_get_utc_time(time_module, curr_qtimer_ticks, &utc_output_time_msw, &utc_output_time_lsw)

    (module end)
    free(time_module)
*/


/*time in ticks*/
typedef uint64_t posal_time;

/* API to get the time module struct size to allocate in the calling module*/
uint32_t posal_get_time_module_size();

/* reset the allocated time module variables. to be called at setup time*/
int32_t posal_reset_utc_time_module(void *utc_time_module_ptr);

/* read the UTC time from NW, needs to be called when caller wants to update the reference UTC time */
int32_t posal_query_utc_time_from_nw(void *utc_time_module_ptr, uint32_t time_unit);

/* get the corresponding UTC time for qtimer ticks */
int32_t posal_date_time_get_utc_time(void *utc_time_module_ptr, posal_time qtime, uint32_t requested_time_unit, uint32_t *utc_time_msw, uint32_t *utc_time_lsw);

#ifdef __cplusplus
}
#endif //__cplusplus
#endif // #ifndef _POSAL_TIME_H_