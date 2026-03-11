/**
 * \file cu_async_cmd_handle.c
 *
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "container_utils.h"
#include "spf_thread_pool.h"

/***************************************************************************/
// number of worker threads container, needs to offload the command processing
#define CU_THREAD_POOL_WORKER_THREADS (1)

// if container frame size is less than this then thread pool will be mandatorily used.
#define CU_FRAME_SIZE_TO_USE_THREAD_POOL_US (2000)

typedef struct cu_async_cmd_handle_t
{
   spf_thread_pool_inst_t *tp_handle; /*< SPF custome thread pool handle to process commands. */
   spf_thread_pool_job_t tp_job; /*< Job Offloaded to the Thread Pool. */
   posal_signal_t tp_signal; /**< Synchronization Signal which is used from the thread-pool thread to the wake up the
              main container thread. */

} cu_async_cmd_handle_t;

static ar_result_t cu_thread_pool_process_job(void *ctx_ptr);
static ar_result_t cu_thread_pool_signal_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index);

/***************************************************************************/

static bool_t cu_async_cmd_handling_needed(cu_base_t *cu_ptr)
{
   // less than 2ms container always use thread pool without any restriction on stack requirement
   // higher frame size container should use thread pool only if stack is less than equal to 16K size.
   if (cu_ptr->actual_stack_size <= SPF_DEFAULT_THREAD_POOL_STACK_SIZE)
   {
      return TRUE;
   }
   else if (cu_ptr->cntr_frame_len.frame_len_us > 0 &&
            cu_ptr->cntr_frame_len.frame_len_us <= CU_FRAME_SIZE_TO_USE_THREAD_POOL_US)
   {
      return TRUE;
   }
   return FALSE;
}

ar_result_t cu_async_cmd_handle_init(cu_base_t *cu_ptr, uint32_t sync_signal_bit_mask)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (cu_ptr->async_cmd_handle || !cu_async_cmd_handling_needed(cu_ptr))
   {
      return AR_EOK;
   }

   MALLOC_MEMSET(cu_ptr->async_cmd_handle,
                 cu_async_cmd_handle_t,
                 sizeof(cu_async_cmd_handle_t),
                 POSAL_HEAP_DEFAULT,
                 result);

   TRY(result,
       spf_thread_pool_get_instance(&cu_ptr->async_cmd_handle->tp_handle,
                                    POSAL_HEAP_DEFAULT,
                                    posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID),
                                    FALSE, /*is_dedicated_pool*/
                                    cu_ptr->actual_stack_size,
                                    CU_THREAD_POOL_WORKER_THREADS,
                                    cu_ptr->gu_ptr->log_id));

   /*Initialize the signal used by the helper thread to wakeup main thread.*/
   TRY(result,
       cu_init_signal(cu_ptr,
                      sync_signal_bit_mask,
                      cu_thread_pool_signal_handler,
                      &cu_ptr->async_cmd_handle->tp_signal));

   // Job function/context is always same.
   cu_ptr->async_cmd_handle->tp_job.job_context_ptr = cu_ptr;
   cu_ptr->async_cmd_handle->tp_job.job_func_ptr    = cu_thread_pool_process_job;

   CATCH(result, CU_MSG_PREFIX, cu_ptr->gu_ptr->log_id)
   {
      cu_async_cmd_handle_deinit(cu_ptr);
   }

   return result;
}

ar_result_t cu_async_cmd_handle_deinit(cu_base_t *cu_ptr)
{
   if (cu_ptr->async_cmd_handle)
   {
      spf_thread_pool_release_instance(&cu_ptr->async_cmd_handle->tp_handle, cu_ptr->gu_ptr->log_id);

      cu_deinit_signal(cu_ptr, &cu_ptr->async_cmd_handle->tp_signal);

      MFREE_NULLIFY(cu_ptr->async_cmd_handle);
   }

   return AR_EOK;
}

ar_result_t cu_async_cmd_handle_update(cu_base_t *cu_ptr)
{
   if (cu_ptr->async_cmd_handle)
   {
      if (cu_async_cmd_handling_needed(cu_ptr))
      {
         return spf_thread_pool_update_instance(&cu_ptr->async_cmd_handle->tp_handle,
                                                cu_ptr->actual_stack_size,
                                                CU_THREAD_POOL_WORKER_THREADS,
                                                cu_ptr->gu_ptr->log_id);
      }
      else
      {
         return cu_async_cmd_handle_deinit(cu_ptr);
      }
   }
   return AR_EOK;
}

bool_t cu_async_cmd_handle_check_and_push_cmd(cu_base_t *cu_ptr)
{
   if (cu_ptr->async_cmd_handle && cu_ptr->async_cmd_handle->tp_handle && cu_ptr->flags.is_real_time)
   {
      switch (cu_ptr->cmd_msg.msg_opcode)
      {
         case SPF_MSG_CMD_GRAPH_OPEN:
         case SPF_MSG_CMD_GRAPH_CLOSE:
         case SPF_MSG_CMD_GRAPH_STOP:
         case SPF_MSG_CMD_GRAPH_SUSPEND:
         {
            // since we are pushing the command in the thread pool therefore disable the command queue for further
            // commands until this is processed
            posal_queue_enable_disable_signaling(cu_ptr->cmd_handle.cmd_q_ptr, FALSE);
            if (AR_FAILED(
                   spf_thread_pool_push_job(cu_ptr->async_cmd_handle->tp_handle, &cu_ptr->async_cmd_handle->tp_job, 0)))
            {
               // If command is not pushed then enable the command queue signaling as commands will be processed by the
               // main thread itself
               posal_queue_enable_disable_signaling(cu_ptr->cmd_handle.cmd_q_ptr, TRUE);
               return FALSE;
            }
            return TRUE;
         }
      default:
            return FALSE;
   }
   }
   return FALSE;
}

static ar_result_t cu_thread_pool_process_job(void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   cu_base_t  *cu_ptr = (cu_base_t *)ctx_ptr;
   uint32_t    log_id = cu_ptr->gu_ptr->log_id;

   CU_MSG(log_id, DBG_MED_PRIO, "worker thread 0x%lX, running job.", posal_thread_get_curr_tid());

   result = cu_process_cmd_queue(cu_ptr);

   // if command is pending and should be handled by the main thread then inform main thread
   if (cu_ptr->handle_rest_fn)
   {
      posal_signal_send(cu_ptr->async_cmd_handle->tp_signal);
   }

   if (result != AR_ETERMINATED)
   {
      // since this command is processed therefore enable the command queue signaling for further commands.
      /* Note: In case if tp_signal is set because command is partially pending and also there is another command in the
         queue. Then Container will first process the pending command as tp_signal is higher priority than command-Q.
         This is also the reason why command queue signaling should be enabled only after setting the tp_signal first.*/
      posal_queue_enable_disable_signaling(cu_ptr->cmd_handle.cmd_q_ptr, TRUE);
   }

   CU_MSG(log_id, DBG_MED_PRIO, "worker thread 0x%lX, completed job.", posal_thread_get_curr_tid());

   return result;
}

static ar_result_t cu_thread_pool_signal_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;

   void                  *handle_rest_ctx_ptr = cu_ptr->handle_rest_ctx_ptr;
   cu_handle_rest_of_fn_t handle_rest_fn      = cu_ptr->handle_rest_fn;
   cu_ptr->handle_rest_ctx_ptr                = NULL;
   cu_ptr->handle_rest_fn                     = NULL;

   posal_signal_clear(cu_ptr->async_cmd_handle->tp_signal);

   CU_MSG(cu_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "handling signal from the thread pool");

   // If any command handling was done partially, complete the rest now in main thread context.
   if (handle_rest_fn)
   {
      result = handle_rest_fn(cu_ptr, handle_rest_ctx_ptr);
   }

   MFREE_NULLIFY(handle_rest_ctx_ptr);

   return result;
}
