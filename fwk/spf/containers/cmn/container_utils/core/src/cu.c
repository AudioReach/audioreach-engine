/**
 * \file cu.c
 *
 * \brief
 *     Container utilities.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */
#include "container_utils.h"

/*==============================================================================
   Global Defines
==============================================================================*/
#include "cu_i.h"
#include "gen_cntr.h"
#include "spl_cntr.h"
#include "olc.h"
#include "spf_svc_utils.h"
#include "icb.h"

// Global counter to return a unique id.
static uint32_t cu_global_unique_id = 1; // 0 reserved for not initialized.

/**
 * Create function of a container
 */
typedef ar_result_t (*spf_cntr_create_function_t)(cntr_cmn_init_params_t *init_param_ptr,
                                                  spf_handle_t          **cntr_handle,
                                                  uint32_t                cntr_type);

typedef void (*spf_cntr_dump_debug_info_t)(spf_handle_t *cntr_handle, int8_t *start_address, uint32_t max_size);

/**
 * the index is used for getting entry point function
 * and for getting the index for logging.
 */
const uint32_t global_cntr_list_t[] = {
   APM_CONTAINER_TYPE_ID_SC, // 0
   APM_CONTAINER_TYPE_ID_GC, // 1
   APM_CONTAINER_TYPE_ID_OLC, // 2
   APM_CONTAINER_TYPE_ID_PTC // 3
};

const spf_cntr_create_function_t global_cntr_create_ftable[] = {
   spl_cntr_create, // APM_CONTAINER_TYPE_ID_SC
   gen_cntr_create, // APM_CONTAINER_TYPE_ID_GC
   olc_create,      // APM_CONTAINER_TYPE_ID_OLC
   gen_cntr_create, // APM_CONTAINER_TYPE_ID_PTC
};

const spf_cntr_dump_debug_info_t global_dump_debug_info_ftable[] = {
   NULL, // APM_CONTAINER_TYPE_ID_SC
   NULL, // APM_CONTAINER_TYPE_ID_GC
   NULL, // APM_CONTAINER_TYPE_ID_OLC
   NULL, // APM_CONTAINER_TYPE_ID_PTC
};

/* =======================================================================
Static Function Declarations
========================================================================== */

/* =======================================================================
Static Function Definitions
========================================================================== */
static ar_result_t cu_get_index_for_cntr_type(uint32_t cntr_type_id, uint32_t *cntr_index_ptr)
{
   *cntr_index_ptr = (uint32_t)-1;
   for (uint32_t i = 0; i < SIZE_OF_ARRAY(global_cntr_list_t); i++)
   {
      if (global_cntr_list_t[i] == cntr_type_id)
      {
         *cntr_index_ptr = i;
         return AR_EOK;
      }
   }
   return AR_EFAILED;
}
static ar_result_t cu_cmn_get_container_type_id(apm_container_cfg_t *container_cfg_ptr, uint32_t *type_id_ptr)
{
   ar_result_t result = AR_EOK;
   *type_id_ptr       = 0;

   apm_prop_data_t *cntr_prop_ptr = (apm_prop_data_t *)(container_cfg_ptr + 1);

   for (uint32_t i = 0; i < container_cfg_ptr->num_prop; i++)
   {
      switch (cntr_prop_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_CONTAINER_TYPE:
         {
            if (cntr_prop_ptr->prop_size < sizeof(apm_cont_prop_id_type_t))
            {
               return AR_ENORESOURCE;
            }

            apm_cont_prop_id_type_t *type_ptr = (apm_cont_prop_id_type_t *)(cntr_prop_ptr + 1);
            if (1 != type_ptr->version)
            {
               AR_MSG(DBG_ERROR_PRIO, "version must be one");
               return AR_EBADPARAM;
            }

            *type_id_ptr = type_ptr->type_id.type;
         }
         break;
      }

      cntr_prop_ptr =
         (apm_prop_data_t *)((uint8_t *)cntr_prop_ptr + cntr_prop_ptr->prop_size + sizeof(apm_prop_data_t));
   }

   if (0 == *type_id_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Container Type IDs not given");
      return AR_EFAILED;
   }
   return result;
}

/* =======================================================================
API Function Definitions
========================================================================== */

ar_result_t cu_init(cu_base_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   /* Create general purpose signal */
   if (AR_EOK != (result = posal_channel_create(&me_ptr->gp_channel_ptr, me_ptr->heap_id)))
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Failed to create gp channel with result: ", result);
      return result;
   }

   if (AR_EOK != (result = posal_signal_create(&me_ptr->gp_signal_ptr, me_ptr->heap_id)))
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Failed to create gp signal with result: ", result);
      return result;
   }

   /* Add general purpose signal to channel */
   posal_channel_add_signal(me_ptr->gp_channel_ptr, me_ptr->gp_signal_ptr, 0x1);

   return result;
}

ar_result_t cu_deinit(cu_base_t *me_ptr)
{
   cu_operate_on_delay_paths(me_ptr, 0, CU_PATH_DELAY_OP_REMOVE);

   if (NULL != me_ptr->gp_signal_ptr)
   {
      /* Release signal bit in mask */
      cu_release_bit_in_bit_mask(me_ptr, posal_signal_get_channel_bit(me_ptr->gp_signal_ptr));

      /* Destroy general purpose signal */
      posal_signal_destroy(&me_ptr->gp_signal_ptr);
   }
   if (NULL != me_ptr->gp_channel_ptr)
   {
      /* Destroy general purpose signal */
      posal_channel_destroy(&me_ptr->gp_channel_ptr);
   }

   return AR_EOK;
}

ar_result_t cu_init_signal(cu_base_t         *cu_ptr,
                           uint32_t           bit_mask,
                           cu_queue_handler_t q_func_ptr,
                           posal_signal_t    *signal_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   *signal_ptr = NULL;
   TRY(result, posal_signal_create(signal_ptr, cu_ptr->heap_id));

   /* Add signal to the base channel with the bit mask */
   TRY(result, posal_channel_add_signal(cu_ptr->channel_ptr, *signal_ptr, bit_mask));

   cu_set_handler_for_bit_mask(cu_ptr, bit_mask, q_func_ptr);

   /* Start listening to the mask : always*/
   cu_start_listen_to_mask(cu_ptr, bit_mask);

   CATCH(result, CU_MSG_PREFIX, cu_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t cu_deinit_signal(cu_base_t *cu_ptr, posal_signal_t *signal_ptr)
{
   if (NULL == signal_ptr || NULL == *signal_ptr)
   {
      return AR_EBADPARAM;
   }
   // Get channel bit for the signal
   uint32_t bitmask = posal_signal_get_channel_bit(*signal_ptr);

   // Stop listening to the mask
   cu_stop_listen_to_mask(cu_ptr, bitmask);

   // Destroy the trigger signal
   posal_signal_clear(*signal_ptr);
   posal_signal_destroy(signal_ptr);

   return AR_EOK;
}

ar_result_t cntr_cmn_create(cntr_cmn_init_params_t *init_param_ptr, spf_handle_t **cntr_handle)
{
   ar_result_t result  = AR_EOK;
   uint32_t    type_id = 0;

   if (AR_DID_FAIL(result = cu_cmn_get_container_type_id(init_param_ptr->container_cfg_ptr, &type_id)))
   {
      return result;
   }
   uint32_t cntr_index = 0;

   if (AR_DID_FAIL(result = cu_get_index_for_cntr_type(type_id, &cntr_index)))
   {
      return result;
   }

#ifdef HEAP_PROFILING
   CU_MSG(0, DBG_HIGH_PRIO, "APM allocation for containers started, seq_id 0x%lx", init_param_ptr->log_seq_id);
#endif

   result = global_cntr_create_ftable[cntr_index](init_param_ptr, cntr_handle, type_id);

#ifdef HEAP_PROFILING
   if (*cntr_handle)
   {
      CU_MSG(0,
             DBG_HIGH_PRIO,
             "APM allocation for containers done, seq_id 0x%lx, cmd_handle 0x%lx",
             init_param_ptr->log_seq_id,
             (*cntr_handle)->cmd_handle_ptr);
   }
#endif
   return result;
}

ar_result_t cntr_cmn_destroy(spf_handle_t *cntr_handle)
{
   ar_result_t result = AR_EOK;

#ifdef HEAP_PROFILING
   spf_cmd_handle_t *cmd_handler_ptr = cntr_handle->cmd_handle_ptr;
   CU_MSG(0, DBG_HIGH_PRIO, "APM deallocation for containers started, cmd_handle 0x%lx", cmd_handler_ptr);
#endif

   posal_thread_join(cntr_handle->cmd_handle_ptr->thread_id, &result);
   posal_memory_free(cntr_handle);

#ifdef HEAP_PROFILING
   CU_MSG(0, DBG_HIGH_PRIO, "APM deallocation for containers done, cmd_handle 0x%lx", cmd_handler_ptr);
#endif

   return result;
}

/* =======================================================================
Internal Function Definitions
========================================================================== */

ar_result_t cu_check_launch_thread(cu_base_t *me_ptr,
                                   uint32_t   new_stack_size,
                                   uint32_t   new_root_stack_size,
                                   int32_t    thread_priority,
                                   char      *thread_name,
                                   bool_t    *thread_launched_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   posal_thread_t old_thread_id = me_ptr->cmd_handle.thread_id;

   // Can reuse thread if stack size did not change.
   if (cu_check_thread_relaunch_required(me_ptr, new_stack_size, new_root_stack_size))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Creating new thread. old stack size %lu, new stack size %lu, priority %lu, root-thread-stack-size %lu",
             me_ptr->actual_stack_size,
             new_stack_size,
             thread_priority,
             new_root_stack_size);

      // If there was a previous thread, it must exit. The newly created thread
      // won't exit looking at me_ptr->thread_id_to_exit as the id will be
      // different.
      me_ptr->thread_id_to_exit = old_thread_id;

      if (AR_DID_FAIL(result = posal_thread_launch3(&(me_ptr->cmd_handle.thread_id),
                                                    thread_name,
                                                    new_stack_size,
                                                    new_root_stack_size,
                                                    thread_priority,
                                                    cu_workloop_entry,
                                                    (void *)me_ptr,
                                                    me_ptr->heap_id,
                                                    me_ptr->configured_sched_policy,
                                                    me_ptr->configured_core_affinity)))
      {
         // Restoring the thread ID from the cached value because on failure posal_thread_launch clears the thread ID.
         me_ptr->cmd_handle.thread_id = old_thread_id;

         CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to launch thread!");

         THROW(result, result);
      }

      me_ptr->actual_stack_size      = new_stack_size;
      me_ptr->root_thread_stack_size = new_root_stack_size;
      *thread_launched_ptr           = TRUE;

      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "old thread id = 0x%lX, new thread id = 0x%lX",
             posal_thread_get_tid_v2(old_thread_id),
             posal_thread_get_tid_v2(me_ptr->cmd_handle.thread_id));
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "cntr_lauch_thread failed result = %d", result);
   }

   return result;
}

ar_result_t cu_create_send_icb_info_msg_to_upstreams(cu_base_t        *base_ptr,
                                                     cu_ext_in_port_t *ext_in_port_ptr,
                                                     gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_msg_t                      msg;
   spf_msg_header_t              *cmd_header_ptr;
   spf_msg_cmd_inform_icb_info_t *cmd_ptr;
   uint32_t                       cmd_size = sizeof(spf_msg_cmd_inform_icb_info_t);
   cmd_size                                = GET_SPF_MSG_REQ_SIZE(cmd_size);

   TRY(result,
       spf_msg_create_msg(&msg,
                          &cmd_size,
                          SPF_MSG_CMD_INFORM_ICB_INFO,
                          NULL,
                          NULL,
                          gu_ext_in_port_ptr->upstream_handle.spf_handle_ptr,
                          base_ptr->heap_id));

   cmd_header_ptr                              = (spf_msg_header_t *)msg.payload_ptr;
   cmd_ptr                                     = (spf_msg_cmd_inform_icb_info_t *)&cmd_header_ptr->payload_start;
   cmd_ptr->downstream_frame_len_samples       = base_ptr->cntr_frame_len.frame_len_samples;
   cmd_ptr->downstream_frame_len_us            = base_ptr->cntr_frame_len.frame_len_us;
   cmd_ptr->downstream_sample_rate             = base_ptr->cntr_frame_len.sample_rate;
   cmd_ptr->downstream_period_us               = base_ptr->period_us;
   cmd_ptr->downstream_consumes_variable_input = ext_in_port_ptr->icb_info.flags.variable_input;
   cmd_ptr->downstream_is_self_real_time       = ext_in_port_ptr->icb_info.flags.is_real_time;
   cmd_ptr->downstream_set_single_buffer_mode  = ext_in_port_ptr->icb_info.flags.is_default_single_buffering_mode;
   cmd_ptr->downstream_sid                     = gu_ext_in_port_ptr->sg_ptr->sid;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "ICB: informing frame length (%lu, %lu, %lu), period in us (%lu) to upstream of Module 0x%lX, %lu. "
          "real-time %u,variable-size %u ",
          base_ptr->cntr_frame_len.frame_len_samples,
          base_ptr->cntr_frame_len.sample_rate,
          base_ptr->cntr_frame_len.frame_len_us,
          base_ptr->period_us,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          gu_ext_in_port_ptr->int_in_port_ptr->cmn.index,
          ext_in_port_ptr->icb_info.flags.is_real_time,
          ext_in_port_ptr->icb_info.flags.variable_input);

   if (AR_DID_FAIL(result = spf_msg_send_cmd(&msg, gu_ext_in_port_ptr->upstream_handle.spf_handle_ptr)))
   {
      spf_msg_return_msg(&msg);
   }

   // just send message. shouldn't wait for response as it can result in deadlock (if other contr also sends a cmd
   // & waits for us)

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

void cu_update_cntr_proc_duration(cu_base_t *base_ptr)
{
   if (!base_ptr->flags.is_cntr_proc_dur_set_paramed)
   {
      // div by Q4 => numerator needs to be multiplied by 2^4.
      uint32_t cntr_proc_duration =
         (base_ptr->cntr_frame_len.frame_len_us << 4) / base_ptr->pm_info.weighted_kpps_scale_factor_q4;

      if (cntr_proc_duration != base_ptr->cntr_proc_duration)
      {
         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, proc_dur_change);
         base_ptr->cntr_proc_duration = cntr_proc_duration;
      }
   }
}

/**
 * updates container proc duration.
 */
ar_result_t cu_handle_frame_len_change(cu_base_t *base_ptr, icb_frame_length_t *fm_info_ptr, uint32_t period_us)
{
   ar_result_t result            = AR_EOK;
   bool_t      period_changed    = FALSE;
   bool_t      frame_len_changed = FALSE;

   if (period_us != base_ptr->period_us)
   {
      period_changed = TRUE;
   }

   // is_cntr_period_set_paramed by VCPM, then only overwrite
   if (!base_ptr->flags.is_cntr_period_set_paramed)
   {
      base_ptr->period_us = period_us;
   }

   if ((0 == fm_info_ptr->frame_len_samples) && (0 == fm_info_ptr->sample_rate) && (0 == fm_info_ptr->frame_len_us))
   {
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "ICB: all fields of frame len zero.");
   }
   else if (fm_info_ptr != &(base_ptr->cntr_frame_len))
   {
      /**
       * if sample rate is nonzero and if sample rates or frame len changed
       * OR
       * if frame len in us changed
       */
      if (base_ptr->cntr_frame_len.sample_rate && fm_info_ptr->sample_rate)
      {
         frame_len_changed = (base_ptr->cntr_frame_len.sample_rate != fm_info_ptr->sample_rate) ||
                             (base_ptr->cntr_frame_len.frame_len_samples != fm_info_ptr->frame_len_samples);
      }
      else if (base_ptr->cntr_frame_len.sample_rate)
      {
         frame_len_changed = (base_ptr->cntr_frame_len.frame_len_samples / base_ptr->cntr_frame_len.sample_rate) !=
                             fm_info_ptr->frame_len_us;
      }
      else if (fm_info_ptr->sample_rate)
      {
         frame_len_changed =
            (fm_info_ptr->frame_len_samples / fm_info_ptr->sample_rate) != base_ptr->cntr_frame_len.frame_len_us;
      }
      else
      {
         frame_len_changed = (base_ptr->cntr_frame_len.frame_len_us != fm_info_ptr->frame_len_us);
      }

      // in spite of errors do this
      base_ptr->cntr_frame_len.frame_len_samples = fm_info_ptr->frame_len_samples;
      base_ptr->cntr_frame_len.frame_len_us      = fm_info_ptr->frame_len_us;
      base_ptr->cntr_frame_len.sample_rate       = fm_info_ptr->sample_rate;
      if (frame_len_changed)
      {
         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, frame_len_change);
      }

      cu_update_cntr_proc_duration(base_ptr);

      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Container frame length (%lu samples, %lu Hz, %lu us). Period %lu us, proc duration %lu us, "
             "is_cntr_proc_dur_set_paramed%u frame len changed %d",
             base_ptr->cntr_frame_len.frame_len_samples,
             base_ptr->cntr_frame_len.sample_rate,
             base_ptr->cntr_frame_len.frame_len_us,
             base_ptr->period_us,
             base_ptr->cntr_proc_duration,
             base_ptr->flags.is_cntr_proc_dur_set_paramed,
             frame_len_changed);
   }

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *gu_ext_in_port_ptr = (gu_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      cu_ext_in_port_t *ext_in_port_ptr =
         (cu_ext_in_port_t *)(((uint8_t *)gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset));

      if (!gu_ext_in_port_ptr->upstream_handle.spf_handle_ptr)
      {
#if 0
         // shared memory clients
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "ICB: Warning: No upstream, not sending ICB msg (OK for sh mem EP)");
#endif
         continue;
      }

      if (!ext_in_port_ptr->prop_info.did_inform_us_of_frame_len_and_var_ip || frame_len_changed || period_changed)
      {
         cu_create_send_icb_info_msg_to_upstreams(base_ptr, ext_in_port_ptr, gu_ext_in_port_ptr);
         ext_in_port_ptr->prop_info.did_inform_us_of_frame_len_and_var_ip = TRUE;
      }
   }

   if (frame_len_changed)
   {
      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = base_ptr->gu_ptr->ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         gu_ext_out_port_t *gu_ext_out_port_ptr = (gu_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         cu_ext_out_port_t *ext_out_port_ptr =
            (cu_ext_out_port_t *)(((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset));
         // When frame len changes, we inform downstream container of upstream max frame len at run time
         // Set only if MF is valid on the external port because framelength alone cannot be sent to the
         // peer containers.
         if ((ext_out_port_ptr->media_fmt.data_format != SPF_UNKNOWN_DATA_FORMAT))
         {
            ext_out_port_ptr->flags.upstream_frame_len_changed = TRUE;
         }
      }
   }

   return result;
}

/*If client doesn't send the heap ID property, we assume Default heap.
We have to validate only if the property is received*/
ar_result_t cu_parse_get_self_and_peer_heap_ids(apm_container_cfg_t *container_cfg_ptr,
                                                POSAL_HEAP_ID       *self_heap_id_ptr,
                                                POSAL_HEAP_ID       *peer_heap_id_ptr)
{
   apm_prop_data_t *cntr_prop_ptr;
   *self_heap_id_ptr = POSAL_HEAP_DEFAULT;
   *peer_heap_id_ptr = POSAL_HEAP_DEFAULT;

   if (!container_cfg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Container cfg not given during create. Error");
      return AR_EFAILED;
   }

   cntr_prop_ptr = (apm_prop_data_t *)(container_cfg_ptr + 1);

   for (uint32_t i = 0; i < container_cfg_ptr->num_prop; i++)
   {
      switch (cntr_prop_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_HEAP_ID:
         {
            if (cntr_prop_ptr->prop_size < sizeof(apm_cont_prop_id_heap_id_t))
            {
               return AR_EBADPARAM;
            }
            apm_cont_prop_id_heap_id_t *heap_cfg_ptr = (apm_cont_prop_id_heap_id_t *)(cntr_prop_ptr + 1);

            *self_heap_id_ptr = gu_get_heap_id_from_heap_prop(heap_cfg_ptr->heap_id);
            break;
         }
         case APM_CONTAINER_PROP_ID_PEER_HEAP_ID:
         {
            if (cntr_prop_ptr->prop_size < sizeof(apm_cont_prop_id_peer_heap_id_t))
            {
               return AR_EBADPARAM;
            }
            apm_cont_prop_id_peer_heap_id_t *heap_cfg_ptr = (apm_cont_prop_id_peer_heap_id_t *)(cntr_prop_ptr + 1);

            *peer_heap_id_ptr = gu_get_heap_id_from_heap_prop(heap_cfg_ptr->heap_id);
            break;
         }
         default:
         {
            break;
         }
      }

      cntr_prop_ptr =
         (apm_prop_data_t *)((uint8_t *)cntr_prop_ptr + cntr_prop_ptr->prop_size + sizeof(apm_prop_data_t));
   }

   // If self-heap-id is island-heap, then peer-heap is also island. Need to use PEER_HEAP_ID only if self heap is
   // not island.
   *peer_heap_id_ptr = POSAL_IS_ISLAND_HEAP_ID(*self_heap_id_ptr) ? *self_heap_id_ptr : *peer_heap_id_ptr;

   return AR_EOK;
}

uint32_t cu_get_next_unique_id(cu_base_t *base_ptr)
{
   uint32_t unique_id = cu_global_unique_id;
   cu_global_unique_id++;
   return unique_id;
}

ar_result_t cu_set_cntr_type_bits_in_log_id(uint32_t cntr_type, uint32_t *log_id_ptr)
{
   ar_result_t result     = AR_EOK;
   uint32_t    cntr_index = 0;

   if (AR_DID_FAIL(result = cu_get_index_for_cntr_type(cntr_type, &cntr_index)))
   {
      return result;
   }
   // cntr_index +1 for backward compatibility
   cu_set_bits(log_id_ptr, cntr_index + 1, LOG_ID_CNTR_TYPE_MASK, LOG_ID_CNTR_TYPE_SHIFT);
   return result;
}

void cntr_cmn_dump_debug_info(spf_handle_t *cntr_handle, uint32_t type_id, int8_t *start_address, uint32_t max_size)
{
   return;
}

/**
 * Assign if instance variable has zero (not configured even once);
 * otherwise, verify that incoming variable is same as instance var.
 */
#define ASSIGN_IF_ZERO_ELSE_VERIFY(result, instance_var, incoming_var)                                                 \
   do                                                                                                                  \
   {                                                                                                                   \
      if (0 == instance_var)                                                                                           \
      {                                                                                                                \
         instance_var = incoming_var;                                                                                  \
      }                                                                                                                \
      else                                                                                                             \
      {                                                                                                                \
         VERIFY(result, instance_var == incoming_var);                                                                 \
      }                                                                                                                \
   } while (0)

ar_result_t cu_parse_container_cfg(cu_base_t *me_ptr, apm_container_cfg_t *container_cfg_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   apm_prop_data_t *cntr_prop_ptr;
   uint32_t         host_domain;
   __gpr_cmd_get_host_domain_id(&host_domain);

   if (!container_cfg_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_LOW_PRIO, "Container cfg not given (Can be ignored)");
      return AR_EOK;
   }

   ASSIGN_IF_ZERO_ELSE_VERIFY(result, me_ptr->gu_ptr->container_instance_id, container_cfg_ptr->container_id);

   cntr_prop_ptr = (apm_prop_data_t *)(container_cfg_ptr + 1);

   for (uint32_t i = 0; i < container_cfg_ptr->num_prop; i++)
   {
      switch (cntr_prop_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_FRAME_SIZE:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_frame_size_t));

            apm_cont_prop_id_frame_size_t *fs_ptr = (apm_cont_prop_id_frame_size_t *)(cntr_prop_ptr + 1);

            switch (fs_ptr->mode)
            {
               case APM_CONTAINER_PROP_FRAME_SIZE_TIME:
               {
                  VERIFY(result,
                         cntr_prop_ptr->prop_size >=
                            sizeof(apm_cont_prop_id_frame_size_t) + sizeof(apm_cont_prop_id_frame_size_time_t));
                  apm_cont_prop_id_frame_size_time_t *temp = (apm_cont_prop_id_frame_size_time_t *)(fs_ptr + 1);
                  me_ptr->conf_frame_len.frame_len_us      = temp->frame_size_us;

                  break;
               }
               case APM_CONTAINER_PROP_FRAME_SIZE_SAMPLES:
               {
                  VERIFY(result,
                         cntr_prop_ptr->prop_size >=
                            sizeof(apm_cont_prop_id_frame_size_t) + sizeof(apm_cont_prop_id_frame_size_samples_t));
                  apm_cont_prop_id_frame_size_samples_t *temp = (apm_cont_prop_id_frame_size_samples_t *)(fs_ptr + 1);
                  me_ptr->conf_frame_len.frame_len_samples    = temp->frame_size_samples;

                  break;
               }
               case APM_CONTAINER_PROP_FRAME_SIZE_DEFAULT:
               default:
                  break;
            }

            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_LOW_PRIO,
                   "0x%x: Configured container frame size: mode = %lu [0: ignore, 1: time, 2: samples],  %lu us, %lu "
                   "samples.",
                   me_ptr->gu_ptr->container_instance_id,
                   fs_ptr->mode,
                   me_ptr->conf_frame_len.frame_len_us,
                   me_ptr->conf_frame_len.frame_len_samples);

            break;
         }
         case APM_CONTAINER_PROP_ID_CONTAINER_TYPE:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_type_t));

            apm_cont_prop_id_type_t *type_ptr = (apm_cont_prop_id_type_t *)(cntr_prop_ptr + 1);
            VERIFY(result, (1 == type_ptr->version));

            VERIFY(result, (me_ptr->cntr_type == type_ptr->type_id.type));

            break;
         }
         case APM_CONTAINER_PROP_ID_GRAPH_POS:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_graph_pos_t));

            apm_cont_prop_id_graph_pos_t *pos_ptr = (apm_cont_prop_id_graph_pos_t *)(cntr_prop_ptr + 1);

            ASSIGN_IF_ZERO_ELSE_VERIFY(result, me_ptr->position, pos_ptr->graph_pos);

            break;
         }
         case APM_CONTAINER_PROP_ID_STACK_SIZE:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_stack_size_t));

            apm_cont_prop_id_stack_size_t *stack_size_ptr = (apm_cont_prop_id_stack_size_t *)(cntr_prop_ptr + 1);

            if (APM_PROP_ID_DONT_CARE == stack_size_ptr->stack_size)
            {
               // max stack size will be used by container in the aggregation logic
               // so we can't directly copy
               me_ptr->configured_stack_size = 0;
            }
            else
            {
               me_ptr->configured_stack_size = stack_size_ptr->stack_size;
            }
            break;
         }
         case APM_CONTAINER_PROP_ID_PROC_DOMAIN:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_proc_domain_t));

            apm_cont_prop_id_proc_domain_t *proc_domain_ptr = (apm_cont_prop_id_proc_domain_t *)(cntr_prop_ptr + 1);

            VERIFY(result,
                   ((host_domain == proc_domain_ptr->proc_domain) ||
                    (APM_PROP_ID_DONT_CARE == proc_domain_ptr->proc_domain)));

            // can always assign the host domain from GPR to cu.
            me_ptr->proc_domain = host_domain;

            break;
         }
         case APM_CONTAINER_PROP_ID_HEAP_ID:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_heap_id_t));

            apm_cont_prop_id_heap_id_t *heap_cfg_ptr = (apm_cont_prop_id_heap_id_t *)(cntr_prop_ptr + 1);

            me_ptr->pm_info.register_info.mode =
               (APM_CONT_HEAP_LOW_POWER == heap_cfg_ptr->heap_id) ? PM_MODE_ISLAND : PM_MODE_DEFAULT;

            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "is an island container (1 - True , 0 - False) %lu. ",
                   (APM_CONT_HEAP_LOW_POWER == heap_cfg_ptr->heap_id));

            break;
         }
         case APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID:
         {
            TRY(result, cu_create_offload_info(me_ptr, cntr_prop_ptr));

            break;
         }
         case APM_CONTAINER_PROP_ID_THREAD_PRIORITY:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_thread_priority_t));

            apm_cont_prop_id_thread_priority_t *prio_cfg_ptr = (apm_cont_prop_id_thread_priority_t *)(cntr_prop_ptr + 1);

            me_ptr->configured_thread_prio = prio_cfg_ptr->priority;

            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Configured container thread priority %lu",
                   me_ptr->configured_thread_prio);

            break;
         }
         case APM_CONTAINER_PROP_ID_THREAD_SCHED_POLICY:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_thread_sched_policy_t));

            apm_cont_prop_id_thread_sched_policy_t *sched_cfg_ptr = (apm_cont_prop_id_thread_sched_policy_t *)(cntr_prop_ptr + 1);

            me_ptr->configured_sched_policy = sched_cfg_ptr->sched_policy;

            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Configured container thread sched policy %lu",
                   me_ptr->configured_sched_policy);

            break;
         }
         case APM_CONTAINER_PROP_ID_THREAD_CORE_AFFINITY:
         {
            VERIFY(result, cntr_prop_ptr->prop_size >= sizeof(apm_cont_prop_id_thread_core_affinity_t));

            apm_cont_prop_id_thread_core_affinity_t *prio_cfg_ptr = (apm_cont_prop_id_thread_core_affinity_t *)(cntr_prop_ptr + 1);

            me_ptr->configured_core_affinity = prio_cfg_ptr->core_affinity;

            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_MED_PRIO,
                   "Configured container thread core affinity 0x%lx",
                   me_ptr->configured_core_affinity);

            break;
         }
         default:
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "WARNING: Unsupported Container property, 0x%X, ignoring",
                   cntr_prop_ptr->prop_id);
         }
      }

      cntr_prop_ptr =
         (apm_prop_data_t *)((uint8_t *)cntr_prop_ptr + cntr_prop_ptr->prop_size + sizeof(apm_prop_data_t));
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}
