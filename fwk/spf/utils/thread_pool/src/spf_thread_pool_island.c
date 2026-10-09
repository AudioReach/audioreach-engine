/**
 * \file spf_thread_pool_island.c
 * \brief
 *    This file contains utility functions for managing/using thread pool utility.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spf_thread_pool_i.h"

extern spf_thread_pool_t g_spf_tp;

ar_result_t spf_thread_pool_worker_thread_entry(void *ctx_ptr) // todo: rename me_ptr
{
   ar_result_t                           result = AR_EOK;
   spf_thread_pool_worker_thread_inst_t *wt_ptr = (spf_thread_pool_worker_thread_inst_t *)ctx_ptr;

   spf_thread_pool_check_join_worker_threads(wt_ptr->tp_inst_ptr);

   AR_MSG(DBG_HIGH_PRIO,
          "worker thread id 0x%x launched. stack size 0x%x, thread priority 0x%x, thread pool id 0x%x",
          posal_thread_get_curr_tid(),
          wt_ptr->stack_size,
          wt_ptr->tp_inst_ptr->thread_priority,
          wt_ptr->tp_inst_ptr->log_id);

   uint32_t wait_mask = (SPF_THREAD_POOL_QUEUE_BIT_MASK | SPF_THREAD_POOL_KILL_SIGNAL_BIT_MASK);

   while (TRUE)
   {
      spf_msg_t              msg            = { 0 };
      spf_thread_pool_job_t *job_ptr        = NULL;
      uint32_t               channel_status = 0;

      posal_mutex_lock(wt_ptr->tp_inst_ptr->wt_sync_lock);

      channel_status = posal_channel_wait(wt_ptr->tp_inst_ptr->channel_ptr, wait_mask);

      if (channel_status & SPF_THREAD_POOL_KILL_SIGNAL_BIT_MASK)
      {
         posal_mutex_unlock(wt_ptr->tp_inst_ptr->wt_sync_lock);
         // don't clear the kill signal so that the other worker threads can also wake up and terminate
         break;
      }

      if (wt_ptr->stack_size < wt_ptr->tp_inst_ptr->req_stack_size)
      {
         posal_mutex_unlock(wt_ptr->tp_inst_ptr->wt_sync_lock);

         posal_mutex_lock(g_spf_tp.pool_lock);
         posal_island_trigger_island_exit();

         spf_thread_pool_launch_worker_threads(wt_ptr->tp_inst_ptr);
         wt_ptr->is_terminated = TRUE;
         posal_mutex_unlock(g_spf_tp.pool_lock);
         break;
      }

      posal_queue_pop_front(wt_ptr->tp_inst_ptr->queue_ptr, (posal_queue_element_t *)&msg);

      posal_mutex_unlock(wt_ptr->tp_inst_ptr->wt_sync_lock);


      if (msg.payload_ptr)
      {
         job_ptr                = (spf_thread_pool_job_t *)msg.payload_ptr;
         wt_ptr->active_job_ptr = job_ptr;

         // job ptr may be destoryed in the context of job itself hence cannot use the job ptr after completing the job
         // if job ptr gets freed in the job context, there is no way to return on
         spf_thread_pool_job_t temp_job = *job_ptr;

         spf_thread_pool_job_t *temp_job_ptr = job_ptr;
         if (job_ptr->job_handle_freed_in_job_context)
         {
            temp_job     = *job_ptr;
            temp_job_ptr = &temp_job;
         }

         if (temp_job_ptr->job_func_ptr)
         {
            //posal_signal_t temp_signal = temp_job_ptr->job_signal_ptr;
            ar_result_t    result      = temp_job_ptr->job_func_ptr(temp_job_ptr->job_context_ptr);

            if (result != AR_ETERMINATED)
            {
               temp_job_ptr->job_result = result;
               if (temp_job_ptr->job_signal_ptr)
               {
                  posal_signal_send(temp_job_ptr->job_signal_ptr);
               }
            }
         }

         wt_ptr->active_job_ptr = NULL;
      }

      // set the base thread priority in case if job function has changed it
      posal_thread_set_prio(wt_ptr->tp_inst_ptr->thread_priority);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "worker thread id 0x%x terminated. stack size 0x%x, thread priority 0x%x, thread pool id 0x%x",
          posal_thread_get_curr_tid(),
          wt_ptr->stack_size,
          wt_ptr->tp_inst_ptr->thread_priority,
          wt_ptr->tp_inst_ptr->log_id);

   return result;
}

ar_result_t spf_thread_pool_push_job(spf_thread_pool_inst_t *spf_thread_pool_inst_ptr,
                                     spf_thread_pool_job_t  *job_ptr,
                                     uint32_t                priority)
{
   ar_result_t result = AR_EOK;
   spf_msg_t   msg;
   msg.payload_ptr = job_ptr;

   INIT_EXCEPTION_HANDLING

   VERIFY(result, job_ptr);

   TRY(result,
       posal_queue_push_back_with_priority(spf_thread_pool_inst_ptr->queue_ptr,
                                           (posal_queue_element_t *)(&msg),
                                           priority));
   CATCH(result, "thread pool push job failed!")
   {
   }

   return result;
}

ar_result_t spf_thread_pool_push_job_with_wait(spf_thread_pool_inst_t *spf_thread_pool_inst_ptr,
                                               spf_thread_pool_job_t  *job_ptr,
                                               uint32_t                priority)
{
   ar_result_t result = AR_EOK;

   if (AR_SUCCEEDED(result = spf_thread_pool_push_job(spf_thread_pool_inst_ptr, job_ptr, priority)))
   {
      // blocking wait to complete the job if signal is valid.
      if (job_ptr->job_signal_ptr)
      {
         posal_channel_wait(posal_signal_get_channel(job_ptr->job_signal_ptr),
                            posal_signal_get_channel_bit(job_ptr->job_signal_ptr));
         posal_signal_clear(job_ptr->job_signal_ptr);
      }
   }

   return result;
}
