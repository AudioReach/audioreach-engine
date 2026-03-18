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

#ifdef __ZEPHYR__
    if (!clock_gettime(CLOCK_MONOTONIC, &ts))
        us = ((ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000LL));
#else
    if (!clock_gettime(CLOCK_BOOTTIME, &ts))
        us = ((ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000LL));
#endif /* __ZEPHYR__ */

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

#ifdef __ZEPHYR__
    if(!clock_gettime(CLOCK_MONOTONIC, &ts))
        ms = ((ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL));
#else
    if(!clock_gettime(CLOCK_BOOTTIME, &ts))
        ms = ((ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL));
#endif /* __ZEPHYR__ */

    return ms;
}
