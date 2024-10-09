/**
 * \file spf_thread_pool.c
 * \brief
 *    This file contains utility functions for managing/using thread pool utility.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_thread_pool_i.h"

#ifdef SPF_THREAD_POOL_TEST
#include "spf_thread_pool_test.h"
#endif

/*********************************************************************************/
// global structure for thread pools
spf_thread_pool_t g_spf_tp;

static void spf_thread_pool_join_all_worker_threads(spf_thread_pool_inst_t *tp_inst_ptr);

static ar_result_t spf_thread_pool_update_instance_internal(spf_thread_pool_inst_t **spf_tp_inst_ptr,
                                                            uint32_t                 req_stack_size,
                                                            uint32_t                 num_of_worker_threads,
                                                            uint32_t                 client_log_id);

/*********************************************************************************/

ar_result_t spf_thread_pool_init()
{
   ar_result_t result = AR_EOK;
   memset(&g_spf_tp, 0, sizeof(g_spf_tp));

   // initialize the global mutex
   if (AR_FAILED(result = posal_mutex_create(&g_spf_tp.pool_lock, POSAL_HEAP_DEFAULT)))
   {
      AR_MSG(DBG_ERROR_PRIO, "spf thread pool init failed, result 0x%x", result);
   }

   // initialize the default thread pool which will be reserved and not updated dynamically
   spf_thread_pool_get_instance(&g_spf_tp.default_thread_pool_ptr,
                                POSAL_HEAP_DEFAULT,
                                SPF_DEFAULT_THREAD_POOL_PRIO,
                                FALSE, /*is_dedicated_pool*/
                                SPF_DEFAULT_THREAD_POOL_STACK_SIZE,
                                SPF_DEFAULT_THREAD_POOL_NUM_OF_WORKER_THREADS,
                                0);

#ifdef SPF_THREAD_POOL_TEST
   AR_MSG(DBG_ERROR_PRIO, "Launching thread for Thread Pool Test");
   // launch the Thread Pool tests
   if AR_FAILED(result = spf_thread_pool_test_init())
   {
	   AR_MSG(DBG_ERROR_PRIO, "Launching thread for Thread Pool Test- FAILED.");
   }
#endif

   return result;
}

ar_result_t spf_thread_pool_deinit()
{
   ar_result_t result = AR_EOK;

#ifdef SPF_THREAD_POOL_TEST
   // join test thread
   spf_thread_pool_test_deinit();
#endif

   // go through all the thread pool and release their instances until they are destroyed.
   posal_mutex_lock(g_spf_tp.pool_lock);
   while (g_spf_tp.pool_head_ptr)
   {
      spf_thread_pool_inst_t *tp_inst_ptr = (spf_thread_pool_inst_t *)g_spf_tp.pool_head_ptr->obj_ptr;
      spf_thread_pool_release_instance(&tp_inst_ptr, 0);
   }
   posal_mutex_unlock(g_spf_tp.pool_lock);

   posal_mutex_destroy(&g_spf_tp.pool_lock);

   return result;
}

ar_result_t spf_thread_pool_get_instance(spf_thread_pool_inst_t **spf_tp_inst_ptr,
                                         POSAL_HEAP_ID            heap_id,
                                         posal_thread_prio_t      thread_prio,
                                         bool_t                   is_dedicated_pool,
                                         uint32_t                 req_stack_size,
                                         uint32_t                 num_of_worker_threads,
                                         uint32_t                 client_log_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_thread_pool_inst_t *tp_inst_ptr = NULL;

   VERIFY(result, (spf_tp_inst_ptr));

   // take the global thread pool lock
   posal_mutex_lock(g_spf_tp.pool_lock);

#ifdef DEBUG_SPF_THREAD_POOL
   AR_MSG(DBG_HIGH_PRIO,
          "0x%x: thread pool get instance called with thread prio 0x%x, heap_id 0x%x, stack size 0x%x, worker threads "
          "%d",
          client_log_id,
          thread_prio,
          heap_id,
          req_stack_size,
          num_of_worker_threads);
#endif

   // remove tracking information from the heap_id
   heap_id = GET_HEAP_ID_WITH_ISLAND_INFO(heap_id);

   if (!is_dedicated_pool)
   {
      uint32_t total_stack_memory_required_if_prev_tp_reused = 0;
      // find if any existing thread pool can be reused or not.
      for (spf_list_node_t *tp_list_ptr = g_spf_tp.pool_head_ptr; tp_list_ptr; LIST_ADVANCE(tp_list_ptr))
      {
         spf_thread_pool_inst_t *temp_tp_inst_ptr = (spf_thread_pool_inst_t *)tp_list_ptr->obj_ptr;

         if (heap_id == temp_tp_inst_ptr->heap_id && thread_prio == temp_tp_inst_ptr->thread_priority &&
             !temp_tp_inst_ptr->is_dedicated_pool)
         {
            // found an existing tp with same heap id and thread priority

            uint32_t num_active_wt_count = spf_list_count_elements(temp_tp_inst_ptr->worker_thread_list_ptr);

            uint32_t total_stack_memory_required_if_this_tp_reused =
               MAX(req_stack_size, temp_tp_inst_ptr->req_stack_size) * MAX(num_of_worker_threads, num_active_wt_count);

            // todo: add some more overhead due to tp instance memory and new worther threads
            uint32_t total_stack_memory_required_if_new_tp_created =
               (temp_tp_inst_ptr->req_stack_size * num_active_wt_count) + (req_stack_size * num_of_worker_threads);

            if (total_stack_memory_required_if_this_tp_reused <= total_stack_memory_required_if_new_tp_created)
            {
               if (!tp_inst_ptr ||
                   total_stack_memory_required_if_this_tp_reused <= total_stack_memory_required_if_prev_tp_reused)
               {
                  tp_inst_ptr                                   = temp_tp_inst_ptr;
                  total_stack_memory_required_if_prev_tp_reused = total_stack_memory_required_if_this_tp_reused;
               }
            }
         }
      }
   }

   // if thread pools is not found then create an instance for it.
   if (!tp_inst_ptr)
   {
      tp_inst_ptr = (spf_thread_pool_inst_t *)posal_memory_malloc(sizeof(spf_thread_pool_inst_t), heap_id);
      VERIFY(result, (tp_inst_ptr));

      memset(tp_inst_ptr, 0, sizeof(spf_thread_pool_inst_t));

      tp_inst_ptr->log_id            = g_spf_tp.pool_id_incr++;
      tp_inst_ptr->heap_id           = heap_id;
      tp_inst_ptr->thread_priority   = thread_prio;
      tp_inst_ptr->req_stack_size    = SPF_THREAD_POOL_MIN_STACK_SIZE;
      tp_inst_ptr->is_dedicated_pool = is_dedicated_pool;

      // create channel for the thread pool queue
      TRY(result, posal_channel_create(&tp_inst_ptr->channel_ptr, heap_id));

      // create worker thread synchronization lock
      TRY(result, posal_mutex_create(&tp_inst_ptr->wt_sync_lock, heap_id));

      // create thread pool queue and add to the channel
      char                    q_name[POSAL_DEFAULT_NAME_LEN];
      posal_queue_init_attr_t q_attr;
      snprintf(q_name, POSAL_DEFAULT_NAME_LEN, "TPOOL_%d", (int)tp_inst_ptr->log_id);
      posal_queue_attr_init(&q_attr);
      posal_queue_attr_set_heap_id(&q_attr, heap_id);
      posal_queue_attr_set_max_nodes(&q_attr, SPF_THREAD_POOL_MAX_QUEUE_NODES);
      posal_queue_attr_set_prealloc_nodes(&q_attr, SPF_THREAD_POOL_PREALLOC_QUEUE_NODES);
      posal_queue_attr_set_name(&q_attr, q_name);
      posal_queue_attr_set_priority_queue_mode(&q_attr, TRUE);
      TRY(result, posal_queue_create_v1(&tp_inst_ptr->queue_ptr, &q_attr));
      TRY(result, posal_channel_addq(tp_inst_ptr->channel_ptr, tp_inst_ptr->queue_ptr, SPF_THREAD_POOL_QUEUE_BIT_MASK));

      // create kill signal and add to the channel
      TRY(result, posal_signal_create(&tp_inst_ptr->kill_signal, heap_id));
      TRY(result,
          posal_channel_add_signal(tp_inst_ptr->channel_ptr,
                                   tp_inst_ptr->kill_signal,
                                   SPF_THREAD_POOL_KILL_SIGNAL_BIT_MASK));

      // add thread pool instance to the global thread pool list
      TRY(result, spf_list_insert_tail(&g_spf_tp.pool_head_ptr, (void *)tp_inst_ptr, POSAL_HEAP_DEFAULT, FALSE));
   }

   CATCH(result, "0x%x: thread pool get instance failed.", client_log_id)
   {
      spf_thread_pool_release_instance(&tp_inst_ptr, client_log_id);
   }
   else
   {
      // increment the client reference counter
      tp_inst_ptr->client_ref_counter++;

      if (AR_SUCCEEDED(result = spf_thread_pool_update_instance_internal(&tp_inst_ptr,
                                                                         req_stack_size,
                                                                         num_of_worker_threads,
                                                                         client_log_id)))
      {
         // return the thread pool instance pointer if everything is successful
         *spf_tp_inst_ptr = tp_inst_ptr;
      }
   }

   // release the global lock
   posal_mutex_unlock(g_spf_tp.pool_lock);

   return result;
}

ar_result_t spf_thread_pool_update_instance(spf_thread_pool_inst_t **spf_tp_inst_pptr,
                                            uint32_t                 req_stack_size,
                                            uint32_t                 num_of_worker_threads,
                                            uint32_t                 client_log_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (spf_tp_inst_pptr));
   VERIFY(result, (*spf_tp_inst_pptr));

   spf_thread_pool_inst_t *tp_inst_ptr = *spf_tp_inst_pptr;

   posal_mutex_lock(g_spf_tp.pool_lock);

   // if multiple clients are using this thread pool instance then release from this instance and assign the proper
   // instance or create a new one.
   if (tp_inst_ptr->client_ref_counter > 1)
   {
      TRY(result, spf_thread_pool_release_instance(spf_tp_inst_pptr, client_log_id));

      TRY(result,
          spf_thread_pool_get_instance(spf_tp_inst_pptr,
                                       tp_inst_ptr->heap_id,
                                       tp_inst_ptr->thread_priority,
                                       tp_inst_ptr->is_dedicated_pool,
                                       req_stack_size,
                                       num_of_worker_threads,
                                       client_log_id));
   }
   else
   {
      // if this is the only client using this tp instance then we can update it directly
      TRY(result,
          spf_thread_pool_update_instance_internal(spf_tp_inst_pptr,
                                                   req_stack_size,
                                                   num_of_worker_threads,
                                                   client_log_id));
   }

   CATCH(result, "thread pool instance update Failed! ")
   {
   }

   posal_mutex_unlock(g_spf_tp.pool_lock);

   return result;
}

ar_result_t spf_thread_pool_release_instance(spf_thread_pool_inst_t **spf_tp_inst_pptr, uint32_t client_log_id)
{
   if (!spf_tp_inst_pptr || !(*spf_tp_inst_pptr))
   {
      return AR_EOK;
   }

   ar_result_t             result      = AR_EOK;
   spf_thread_pool_inst_t *tp_inst_ptr = *spf_tp_inst_pptr;

   bool_t destroy_tp_instance = FALSE;

   posal_mutex_lock(g_spf_tp.pool_lock);
   if (tp_inst_ptr->client_ref_counter > 0)
   {
      tp_inst_ptr->client_ref_counter--;
      AR_MSG(DBG_HIGH_PRIO,
             "0x%x: releasing thread pool instance id 0x%x, client_ref_count %lu",
             client_log_id,
             tp_inst_ptr->log_id,
             tp_inst_ptr->client_ref_counter);
   }

   if (0 == tp_inst_ptr->client_ref_counter)
   {
      destroy_tp_instance = TRUE;
      spf_list_find_delete_node(&g_spf_tp.pool_head_ptr, tp_inst_ptr, FALSE);

      spf_thread_pool_join_all_worker_threads(tp_inst_ptr);
   }
   posal_mutex_unlock(g_spf_tp.pool_lock);

   if (destroy_tp_instance)
   {
      AR_MSG(DBG_HIGH_PRIO, "0x%x: destroying thread pool instance id 0x%x.", client_log_id, tp_inst_ptr->log_id);

      if (tp_inst_ptr->queue_ptr)
      {
         spf_svc_drain_cmd_queue(tp_inst_ptr->queue_ptr);
         posal_queue_destroy(tp_inst_ptr->queue_ptr);
      }

      if (tp_inst_ptr->kill_signal)
      {
         posal_signal_destroy(&tp_inst_ptr->kill_signal);
      }

      if (tp_inst_ptr->channel_ptr)
      {
         posal_channel_destroy(&tp_inst_ptr->channel_ptr);
      }

      if (tp_inst_ptr->wt_sync_lock)
      {
         posal_mutex_destroy(&tp_inst_ptr->wt_sync_lock);
      }

      posal_memory_free(tp_inst_ptr);
   }

   *spf_tp_inst_pptr = NULL;

   return result;
}

ar_result_t spf_thread_pool_launch_worker_threads(spf_thread_pool_inst_t *tp_inst_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   posal_mutex_lock(g_spf_tp.pool_lock);

   spf_thread_pool_worker_thread_inst_t *wt_ptr =
      (spf_thread_pool_worker_thread_inst_t *)posal_memory_malloc(sizeof(spf_thread_pool_worker_thread_inst_t),
                                                                  tp_inst_ptr->heap_id);

   VERIFY(result, (wt_ptr));

   memset(wt_ptr, 0, sizeof(spf_thread_pool_worker_thread_inst_t));

   wt_ptr->stack_size  = tp_inst_ptr->req_stack_size;
   wt_ptr->tp_inst_ptr = tp_inst_ptr;

   TRY(result, spf_list_insert_tail(&tp_inst_ptr->worker_thread_list_ptr, wt_ptr, tp_inst_ptr->heap_id, FALSE));

   char t_name[POSAL_DEFAULT_NAME_LEN];
   snprintf(t_name, POSAL_DEFAULT_NAME_LEN, "TPOOL_%d_WT", (int)tp_inst_ptr->log_id);
   TRY(result,
       posal_thread_launch2(&wt_ptr->thread_id,
                            t_name,
                            wt_ptr->stack_size,
                            0,
                            tp_inst_ptr->thread_priority,
                            spf_thread_pool_worker_thread_entry,
                            (void *)wt_ptr,
                            tp_inst_ptr->heap_id));

   CATCH(result, "thread pool worker thread launch failed! ")
   {
      if (wt_ptr)
      {
         spf_list_find_delete_node(&tp_inst_ptr->worker_thread_list_ptr, wt_ptr, FALSE);
         posal_memory_free(wt_ptr);
      }
   }

   posal_mutex_unlock(g_spf_tp.pool_lock);

   return result;
}

void spf_thread_pool_check_join_worker_threads(spf_thread_pool_inst_t *tp_inst_ptr)
{
   posal_mutex_lock(g_spf_tp.pool_lock);

   // Iterate through all the worker thread and join the ones which are terminated
   spf_list_node_t *wt_list_ptr = tp_inst_ptr->worker_thread_list_ptr;

   while (wt_list_ptr)
   {
      ar_result_t dummy_result;

      spf_thread_pool_worker_thread_inst_t *wt_ptr = (spf_thread_pool_worker_thread_inst_t *)wt_list_ptr->obj_ptr;
      LIST_ADVANCE(wt_list_ptr);
      if (wt_ptr->is_terminated)
      {
         posal_thread_join(wt_ptr->thread_id, &dummy_result);
         spf_list_find_delete_node(&tp_inst_ptr->worker_thread_list_ptr, wt_ptr, FALSE);
         posal_memory_free(wt_ptr);
      }
   }

   posal_mutex_unlock(g_spf_tp.pool_lock);
}

static ar_result_t spf_thread_pool_update_instance_internal(spf_thread_pool_inst_t **spf_tp_inst_ptr,
                                                            uint32_t                 req_stack_size,
                                                            uint32_t                 num_of_worker_threads,
                                                            uint32_t                 client_log_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_thread_pool_inst_t *tp_inst_ptr = *spf_tp_inst_ptr;

   uint32_t num_active_wt_count = 0;

   posal_mutex_lock(g_spf_tp.pool_lock);

   // update the stack size if new requirement is higher than the existing stack size
   tp_inst_ptr->req_stack_size = MAX(tp_inst_ptr->req_stack_size, req_stack_size);

   num_active_wt_count = spf_list_count_elements(tp_inst_ptr->worker_thread_list_ptr);

   // launch more worker threads if needed by this client
   // already active worker thread will relaunch themselves if new stack size is higher
   while (num_active_wt_count < num_of_worker_threads && num_active_wt_count < SPF_THREAD_POOL_MAX_WORKER_THREADS_COUNT)
   {
      TRY(result, spf_thread_pool_launch_worker_threads(tp_inst_ptr));

      num_active_wt_count++;
   }

   AR_MSG(DBG_MED_PRIO,
          "0x%x: thread pool instance created/updated with priority 0x%x, client ref count %lu, stack_size 0x%x, "
          "worker "
          "threads %d ",
          client_log_id,
          tp_inst_ptr->thread_priority,
          tp_inst_ptr->client_ref_counter,
          tp_inst_ptr->req_stack_size,
          num_active_wt_count);

   CATCH(result, "thread pool instance update Failed! ")
   {
      spf_thread_pool_release_instance(spf_tp_inst_ptr, client_log_id);
   }

   posal_mutex_unlock(g_spf_tp.pool_lock);

   return result;
}

static void spf_thread_pool_join_all_worker_threads(spf_thread_pool_inst_t *tp_inst_ptr)
{
   posal_mutex_lock(g_spf_tp.pool_lock);

   // send the kill signal, worker thread will start terminating
   posal_signal_send(tp_inst_ptr->kill_signal);

   // iterate through all the worker threads and join them
   while (tp_inst_ptr->worker_thread_list_ptr)
   {
      ar_result_t dummy_result;

      spf_thread_pool_worker_thread_inst_t *wt_ptr =
         (spf_thread_pool_worker_thread_inst_t *)tp_inst_ptr->worker_thread_list_ptr->obj_ptr;

      posal_thread_join(wt_ptr->thread_id, &dummy_result);
      spf_list_find_delete_node(&tp_inst_ptr->worker_thread_list_ptr, wt_ptr, FALSE);
      posal_memory_free(wt_ptr);
   }

   posal_signal_clear(tp_inst_ptr->kill_signal);

   posal_mutex_unlock(g_spf_tp.pool_lock);
}
