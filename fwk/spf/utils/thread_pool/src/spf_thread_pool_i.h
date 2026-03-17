#ifndef __SPF_THREAD_POOL_I_H
#define __SPF_THREAD_POOL_I_H

/**
 * \file spf_thread_pool_i.h
 * \brief
 *    This file contains utility functions for managing/using thread pool utility.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_thread_pool.h"
#include "spf_list_utils.h"
#include "spf_macros.h"
#include "spf_msg_util.h"
#include "spf_svc_utils.h"

#define DEBUG_SPF_THREAD_POOL

// maximum number of jobs allowed to queue in thread pool instance
#define SPF_THREAD_POOL_MAX_QUEUE_NODES (128)

// number of queue nodes to preallocate
// Increase in number of pre-allocated nodes is specific to SPF Simulation set-up 
// and is needed for avoiding false alarms related to memory leaks
#if defined(SIM)
#define SPF_THREAD_POOL_PREALLOC_QUEUE_NODES (32)
#else
#define SPF_THREAD_POOL_PREALLOC_QUEUE_NODES (16)
#endif //defined(SIM)

// Minimum stack size for the thread pool's worker threads.
#define SPF_THREAD_POOL_MIN_STACK_SIZE (1024)

// Bit mask of the thread pool job queue
#define SPF_THREAD_POOL_QUEUE_BIT_MASK (0X40000000)

// Bit mask of the worker thread kill signal
#define SPF_THREAD_POOL_KILL_SIGNAL_BIT_MASK (0X80000000)

// Maximum number of worker thread to run in a thread pool
#define SPF_THREAD_POOL_MAX_WORKER_THREADS_COUNT (4)

// global structure for thread pools
typedef struct spf_thread_pool_t
{
   posal_mutex_t           pool_lock;               // lock to protect the global structure update
   uint32_t                pool_id_incr;            // next available thread pool instance log id
   spf_list_node_t        *pool_head_ptr;           // list of all thread pools
   spf_thread_pool_inst_t *default_thread_pool_ptr; // default reserved thread pool for default heap and floor priority
} spf_thread_pool_t;

// Structure of thread pool instance.
typedef struct spf_thread_pool_inst_t
{
   uint32_t            log_id;                 // thread pool instance ID (for debug purpose)
   POSAL_HEAP_ID       heap_id;                // heap ID of this thread pool
   posal_thread_prio_t thread_priority;        // base thread priority of the worker threads.
   uint32_t            client_ref_counter;     // number of clients subscribed to this thread pool instance
   uint32_t            req_stack_size;         // maximum stack size required from all clients
   spf_list_node_t    *worker_thread_list_ptr; // list of worker threads
   posal_signal_t      kill_signal;            // kill signal to terminate worker threads
   posal_mutex_t       wt_sync_lock;           // mutex lock used by the worker threads to synchronize queue access.
   posal_channel_t     channel_ptr;            // channel ptr used by the WT to listen on queue and kill-signal
   posal_queue_t      *queue_ptr;              // queue where jobs are pushed.
   bool_t              is_dedicated_pool;      // pool is reserved and should not be shared by other clients.
} spf_thread_pool_inst_t;

// Structure for worker thread instance
typedef struct spf_thread_pool_worker_thread_inst_t
{
   posal_thread_t          thread_id;     // thread ID
   spf_thread_pool_inst_t *tp_inst_ptr;   // thread pool instance pointer
   spf_thread_pool_job_t  *active_job_ptr;// context of job running in this worker thread
   uint32_t                stack_size;    // Current stack size of this worker thread
   bool_t                  is_terminated; // Flag is set if worker thread is terminated
} spf_thread_pool_worker_thread_inst_t;

// utility function to launch one worker thread in the thread pool
ar_result_t spf_thread_pool_launch_worker_threads(spf_thread_pool_inst_t *tp_ptr);

/* utility function to check terminated worker thread and join them*/
void spf_thread_pool_check_join_worker_threads(spf_thread_pool_inst_t *tp_ptr);

// worker thread entry function
ar_result_t spf_thread_pool_worker_thread_entry(void *ctx_ptr);

#endif /*__SPF_THREAD_POOL_I_H*/
