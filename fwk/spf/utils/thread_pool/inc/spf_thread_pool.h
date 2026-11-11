#ifndef __SPF_THREAD_POOL_H
#define __SPF_THREAD_POOL_H

/**
 * \file spf_thread_pool.h
 * \brief
 *    This file contains utility functions for managing/using thread pool utility.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ar_defs.h"
#include "posal.h"
#include "spf_list_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Structure declarations
==============================================================================*/

// Default Thread pool priority
#define SPF_DEFAULT_THREAD_POOL_PRIO (posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID))

// Default Thread pool stack size
#define SPF_DEFAULT_THREAD_POOL_STACK_SIZE (16 * 1024)

// Default Thread pool number of worker threads
#define SPF_DEFAULT_THREAD_POOL_NUM_OF_WORKER_THREADS (4)

// callback function for the job
typedef ar_result_t (*spf_thread_pool_job_func)(void *job_context_ptr);

typedef struct spf_thread_pool_inst_t spf_thread_pool_inst_t;

/* Job structure to offload it to SPF thread pool.
 * Any response signaling or error propagation should be part of callback function itself.
 * Structure memory is owned by the client, and should not be released/modified while job is pending or running.
 */
typedef struct spf_thread_pool_job_t
{
   spf_thread_pool_job_func job_func_ptr;    // callback function for the job.
   void                    *job_context_ptr; // callback context for the job
   ar_result_t              job_result;      // result returned by the callback function
   posal_signal_t           job_signal_ptr;  // signal which is set by the thread pool after completing the job.
   bool_t                   job_handle_freed_in_job_context;
} spf_thread_pool_job_t;

/*==============================================================================
   Function declarations
==============================================================================*/

/*
 * Initializes the thread pool
 */
ar_result_t spf_thread_pool_init();

/*
 * De-Initializes the thread pool
 */
ar_result_t spf_thread_pool_deinit();

/*
 * Get an instance from the thread pool.
 * Client can specify the number of worker threads, their heap-id, thread priority and stack-size required.
 * Client can specify if they need a dedicated thread pool (not shared with other clients) by specifying
 * is_dedicated_pool as TRUE.
 */
ar_result_t spf_thread_pool_get_instance(spf_thread_pool_inst_t **spf_thread_pool_inst_pptr,
                                         POSAL_HEAP_ID            heap_id,               // static parameter
                                         posal_thread_prio_t      thread_prio,           // static parameter
                                         bool_t                   is_dedicated_pool,     // static parameter
                                         uint32_t                 req_stack_size,        // dynamic parameter
                                         uint32_t                 num_of_worker_threads, // dynamic parameter
                                         uint32_t                 client_log_id);

/*
 * Update a TP instance
 * Client can specify the updated number of worker threads and stack-size.
 */
ar_result_t spf_thread_pool_update_instance(spf_thread_pool_inst_t **spf_thread_pool_inst_pptr,
                                            uint32_t                 req_stack_size,
                                            uint32_t                 num_of_worker_threads,
                                            uint32_t                 client_log_id);

/*
 * Release an instance from the thread pool.
 */
ar_result_t spf_thread_pool_release_instance(spf_thread_pool_inst_t **spf_thread_pool_inst_pptr,
                                             uint32_t                 client_log_id);

/*
 * Push a job to offload.
 *
 * function is non-blocking and will return as soon as job is pushed to the thread pool queue.
 *
 * "priority" is used to order the job in the thread pool queue. Higher priority job are handled first.
 * Higher the value, higher the priority.
 */
ar_result_t spf_thread_pool_push_job(spf_thread_pool_inst_t *spf_thread_pool_inst_ptr,
                                     spf_thread_pool_job_t  *job_ptr,
                                     uint32_t                priority);

/*
 * Push a job to offload.
 *
 * If "job_ptr->job_signal_ptr" is NULL then function is non-blocking and will return as soon as job is pushed to the
 * thread pool queue.
 *
 * If "job_ptr->job_signal_ptr" is initialized then function is blocking and will return only after
 * job is completed.
 *
 * "priority" is used to order the job in the thread pool queue. Higher priority job are handled first.
 * Higher the value, higher the priority.
 */
ar_result_t spf_thread_pool_push_job_with_wait(spf_thread_pool_inst_t *spf_thread_pool_inst_ptr,
                                               spf_thread_pool_job_t  *job_ptr,
                                               uint32_t                priority);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__SPF_THREAD_POOL_H*/
