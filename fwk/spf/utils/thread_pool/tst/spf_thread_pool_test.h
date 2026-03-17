#ifndef __SPF_THREAD_POOL_TEST_H__
#define __SPF_THREAD_POOL_TEST_H__
/***
 * \file spf_thread_pool_test.h
 * \brief
 *    Header file for Thread Pool functionality tests.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_thread_pool.h"

/* Initialization and De-initialization of the Test thread */
ar_result_t spf_thread_pool_test_init();
ar_result_t spf_thread_pool_test_deinit();

#endif //__SPF_THREAD_POOL_TEST_H__
