/**
 * \file ar_osal_timer.c
 *
 * \brief
 *      This file has implementation of timer operations.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <time.h>
#include <math.h>
#include "ar_osal_error.h"
#include "ar_osal_heap.h"

#ifdef __ZEPHYR__
#define CLOCK_ID CLOCK_MONOTONIC
#else
#define CLOCK_ID CLOCK_BOOTTIME
#endif

/**
 * \brief ar_timer_get_time_in_us
 *        Gets the wall clock time in microseconds
 * \return
 *        Wall clock time in microseconds.
 */
uint64_t ar_timer_get_time_in_us(void)
{
    uint64_t us = 0;
    struct timespec ts;

    if (!clock_gettime(CLOCK_ID, &ts))
        us = ((ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000LL));

    return us;
}

/**
 * \brief ar_timer_get_time_in_ms
 *        Gets the wall clock time in milliseconds
 * \return
 *        Wall clock time in milliseconds.
 */
uint64_t ar_timer_get_time_in_ms(void)
{
    uint64_t ms = 0;
    struct timespec ts;

    if(!clock_gettime(CLOCK_ID, &ts))
        ms = ((ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL));

    return ms;
}
