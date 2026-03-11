/**
 * \file apm.c
 *
 * \brief
 *
 *     This file contains APM Module Implementation
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#include "apm_gpr_cmd_handler.h"
#include "apm_gpr_cmd_rsp_hdlr.h"
#include "apm_spf_cmd_hdlr.h"
#include "apm_msg_rsp_handler.h"
#include "apm_gpr_if.h"
#include "apm_offload_memmap_utils.h"
#include "apm_ext_cmn.h"

#include "irm.h"

/* clang-format off */

/** APM module instance global object */
apm_t g_apm_info;

/** APM service thread stack size in bytes */
#define APM_THREAD_STACK_SIZE  (4096)

/**
 * Function pointer for the cmd/rsp queue handlers
 */
typedef ar_result_t (*apm_process_q_func_t)(apm_t *apm_info_ptr, spf_msg_t *msg_ptr);

/** Function pointer table for cmd/rsp queue handlers  */
const apm_process_q_func_t apm_process_q_ftable[][APM_MAX_NUM_PROC_Q] =
{
   { apm_cmdq_gpr_cmd_handler,     apm_cmdq_spf_cmd_handler }, /** Command Q GPR & GK msg handlers  */
   { apm_rspq_gpr_cmd_rsp_handler, apm_rsp_q_msg_handler   }  /** Response Q GPR & GK msg handlers */

};

/* clang-format on */

/**==============================================================================
   Public Function definitions
==============================================================================*/

ar_result_t apm_process_q(apm_t *apm_info_ptr, uint32_t channel_status)
{
   ar_result_t result = AR_EOK;
   uint32_t    curr_bit_pos;
   spf_msg_t   msg_pkt;
   uint32_t    opcode_list_idx;

   if (!channel_status)
   {
      return AR_EOK;
   }

   /** loop over all the set bits in the channel status and call
    *  corresponding queue handlers */

   do
   {
      curr_bit_pos = apm_get_bit_index_from_channel_status(channel_status);

      /** Pop the message buffer from the queue corresponding to
       *  bit set */
      if (AR_EOK != (result = posal_queue_pop_front((posal_queue_t *)apm_info_ptr->q_list_ptr[curr_bit_pos],
                                                    (posal_queue_element_t *)&msg_pkt)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_process_q():"
                " Failed to pop buf from queue idx, result: 0x%lx",
                result);

         return result;
      }

      /** Get the opcode index as GPR or GK type */
      opcode_list_idx = (SPF_MSG_CMD_GPR == msg_pkt.msg_opcode) ? APM_MSG_TYPE_GPR : APM_MSG_TYPE_GK;

      /** Call the process function  */
      result = apm_process_q_ftable[curr_bit_pos][opcode_list_idx](apm_info_ptr, &msg_pkt);

      /** Clear the processed bit in the channel status */
      apm_clear_bit_index_in_channel_status(&channel_status, curr_bit_pos);

   } while (channel_status);

   return result;
}

/** This function is the main work loop for the service.
 */
static ar_result_t apm_work_loop(void *arg_ptr)
{
   ar_result_t result = AR_EOK;

   /** Service Instance */
   apm_t *apm_info_ptr = (apm_t *)arg_ptr;

   /** Set up mask for listening to the service cmd queue and
    *  kill signal for APM thread */
   apm_info_ptr->curr_wait_mask |= (APM_CMD_Q_MASK | APM_RSP_Q_MASK | APM_KILL_SIG_MASK);

   AR_MSG(DBG_HIGH_PRIO, "Entering APM workloop...");

   /** Enter forever loop */
   for (;;)
   {
      /** Block on any one or more of selected queues to get a msg */
      apm_info_ptr->channel_status = posal_channel_wait(apm_info_ptr->channel_ptr, apm_info_ptr->curr_wait_mask);

      /** Check if the KILL signal is received */
      if (apm_info_ptr->channel_status & APM_KILL_SIG_MASK)
      {
         posal_signal_clear(apm_info_ptr->kill_signal_ptr);

         /** Return from the workloop */
         return AR_EOK;
      }

      /** Handle all system queue cmds first */
      if (apm_info_ptr->ext_utils.sys_util_vtbl_ptr &&
          apm_info_ptr->ext_utils.sys_util_vtbl_ptr->apm_sys_util_process_fptr)
      {
         result = apm_info_ptr->ext_utils.sys_util_vtbl_ptr->apm_sys_util_process_fptr(apm_info_ptr);
      }

      /** Exhaust all response queues next */
      uint32_t resp_q_status = 0;

      while ((resp_q_status = posal_channel_poll(apm_info_ptr->channel_ptr, APM_RSP_Q_MASK)))
      {
         result = apm_process_q(apm_info_ptr, resp_q_status);
      }

      /** Process other queues and also any response queues now */
      apm_info_ptr->channel_status = posal_channel_poll(apm_info_ptr->channel_ptr, apm_info_ptr->curr_wait_mask);

      /** Handle any new command and/or previously pending commands */
      result = apm_process_q(apm_info_ptr, apm_info_ptr->channel_status);

   } /** forever loop */

   return result;
}

ar_result_t apm_init(apm_t *apm_info_ptr)
{
   ar_result_t result     = AR_EOK;

   /** Register with posal memory map */
   if (AR_EOK != (result = posal_memorymap_register(&apm_info_ptr->memory_map_client, APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to register with memory map", apm_info_ptr->channel_status);

      return result;
   }

   /** Register with power manager */
   if (AR_EOK != (result = posal_power_mgr_register(apm_info_ptr->pm_info.register_info,
                                                    &apm_info_ptr->pm_info.pm_handle_ptr,
                                                    NULL, /* Using NULL signal so that wrapper creates one locally */
                                                    APM_LOG_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM INIT: Failed to register with PM, result: %lu", result);
      return result;
   }

   AR_MSG(DBG_HIGH_PRIO, "Power Manager register by APM. Result %lu.", result);

   if (AR_EOK != (result = apm_ext_utils_init(apm_info_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM INIT: Failed to int ext utils vtbl, result: %lu", result);
   }

   return result;
}

ar_result_t apm_deinit(apm_t *apm_info_ptr)
{

#ifndef DISABLE_DEINIT
   ar_result_t result = AR_EOK;

   /** De-register with  GPR    */
   if (AR_EOK != (result = __gpr_cmd_deregister(APM_MODULE_INSTANCE_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to de-register with GPR, result: %lu", result);
   }

   /** de-register with posal memory map */
   if (AR_EOK != (result = posal_memorymap_unregister(apm_info_ptr->memory_map_client)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to de-register with memory map, result: %lu", result);
   }

   /** Deregister with power manager */
   if (posal_power_mgr_is_registered(apm_info_ptr->pm_info.pm_handle_ptr))
   {
      result = posal_power_mgr_deregister(&apm_info_ptr->pm_info.pm_handle_ptr, APM_LOG_ID);

      AR_MSG(DBG_HIGH_PRIO, "Power Manager deregister by APM result %lu", result);
   }

   /** Extn utils deienit */
   result = apm_ext_utils_deinit(apm_info_ptr);

#endif //#ifndef DISABLE_DEINIT

   return AR_EOK;
}

/**
  Destroys the APM command handler thread and clean up
  resources.

  return: error code.
 */

void apm_destroy()
{
   ar_result_t exit_status  = 0;
   apm_t *     apm_info_ptr = &g_apm_info;

   /** De-init APM service */
   apm_deinit(apm_info_ptr);

   /** Send Kill Signal to APM service workloop */
   posal_signal_send(apm_info_ptr->kill_signal_ptr);

   /** Wait for the thread join */
   posal_thread_join(apm_info_ptr->cmd_handle.thread_id, &exit_status);

   /** Destroy all the processing queues */
   for (uint32_t list_idx = 0; list_idx < APM_MAX_NUM_PROC_Q; list_idx++)
   {
      posal_queue_destroy((posal_queue_t *)apm_info_ptr->q_list_ptr[list_idx]);
   }

   /** Destroy the service Kill signal */
   posal_signal_destroy(&apm_info_ptr->kill_signal_ptr);
   posal_signal_destroy(&apm_info_ptr->gp_signal_ptr);

   /** Destroy the channel */
   posal_channel_destroy(&apm_info_ptr->channel_ptr);

   AR_MSG(DBG_HIGH_PRIO, "Completed apm_destroy() ...");

   return;
}

/** Creates the APM command handler thread.  */
ar_result_t apm_create()
{
   ar_result_t             result            = AR_EOK;
   int32_t                 gpr_result        = AR_EOK;
   apm_t *                 apm_info_ptr      = &g_apm_info;
   posal_thread_prio_t     apm_thread_prio   = 0;
   char                    apm_thread_name[] = "APM";
   prio_query_t            query_tbl;
   posal_queue_init_attr_t q_attr;
   uint32_t sched_policy = 0, affinity_mask = 0;

   apm_q_info_t q_info_list[APM_MAX_NUM_PROC_Q] = { { "apm_cmd_q", APM_MAX_CMD_Q_ELEMENTS, APM_CMD_Q_MASK },
                                                    { "apm_rsp_q", APM_MAX_RSP_Q_ELEMENTS, APM_RSP_Q_MASK } };

   AR_MSG(DBG_HIGH_PRIO, "Entering apm_create() ...");

   /** Clear the global structure */
   memset(apm_info_ptr, 0, sizeof(apm_t));

   /** Set up channel */
   if (AR_DID_FAIL(result = posal_channel_create(&apm_info_ptr->channel_ptr, APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to create channel, result: %lu", result);
      return result;
   }

   /** Create kill signal */
   if (AR_DID_FAIL(result = posal_signal_create(&apm_info_ptr->kill_signal_ptr, APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to create Kill Signal: %lu", result);
      return result;
   }

   /** Create gp signal */
   if (AR_DID_FAIL(result = posal_signal_create(&apm_info_ptr->gp_signal_ptr, APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to create gp Signal: %lu", result);
      return result;
   }

   /** Add kill signal to the channel */
   if (AR_EOK !=
       (result = posal_channel_add_signal(apm_info_ptr->channel_ptr, apm_info_ptr->kill_signal_ptr, APM_KILL_SIG_MASK)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to add svc kill signal to channel, result: %lu", result);

      return result;
   }

   /** Add GP signal to the channel */
   if (AR_EOK !=
       (result = posal_channel_add_signal(apm_info_ptr->channel_ptr, apm_info_ptr->gp_signal_ptr, APM_DONT_CARE_MASK)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to add gp signal to channel, result: %lu", result);

      return result;
   }

   for (uint32_t list_idx = 0; list_idx < APM_MAX_NUM_PROC_Q; list_idx++)
   {
      /** Create APM thread queues */
      posal_queue_set_attributes(&q_attr,
                                 APM_INTERNAL_STATIC_HEAP_ID,
                                 q_info_list[list_idx].num_q_elem, /** Max q elements */
                                 q_info_list[list_idx].num_q_elem, /** Max preallocated q elements */
                                 q_info_list[list_idx].q_name);

      if (AR_DID_FAIL(result = posal_queue_create_v1(&(apm_info_ptr->q_list_ptr[list_idx]), &q_attr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to init APM cmd Q, result: %lu", result);
         return result;
      }

      if (AR_DID_FAIL(result = posal_channel_addq(apm_info_ptr->channel_ptr,
                                                  apm_info_ptr->q_list_ptr[list_idx],
                                                  q_info_list[list_idx].q_sig_mask)))
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to add cmdq to channel, result: %lu", result);

         return result;
      }
   }

   /** Initialize APM module */
   if (AR_EOK != (result = apm_init(apm_info_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_create(): Failed to init APM module, result: %lu", result);

      goto __bail_out_create;
   }

   /** Frame duration is not required for static command handler
    *  threads */
   query_tbl.frame_duration_us = 0;
   query_tbl.is_interrupt_trig = FALSE;
   query_tbl.static_req_id     = SPF_THREAD_STAT_APM_ID;

   if (AR_EOK != (result = posal_thread_determine_attributes(&query_tbl, &apm_thread_prio, &sched_policy, &affinity_mask)))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to get thread priority result: %lu", result);
      return result;
   }

   /** Launch the thread */
   if (AR_DID_FAIL(result = posal_thread_launch3(&apm_info_ptr->cmd_handle.thread_id,
                                                apm_thread_name,
                                                APM_THREAD_STACK_SIZE,
                                                0,
                                                apm_thread_prio,
                                                apm_work_loop,
                                                (void *)apm_info_ptr,
                                                APM_INTERNAL_STATIC_HEAP_ID,
                                                sched_policy,
                                                affinity_mask)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to launch APM Thread, result: %lu", result);

      goto __bail_out_create;
   }

   /** Get the system queue ptr if it is present */
   posal_queue_t *sys_q_ptr = NULL;
   if (apm_info_ptr->ext_utils.sys_util_vtbl_ptr &&
       apm_info_ptr->ext_utils.sys_util_vtbl_ptr->apm_sys_util_get_sys_q_handle_fptr)
   {
      sys_q_ptr = apm_info_ptr->ext_utils.sys_util_vtbl_ptr->apm_sys_util_get_sys_q_handle_fptr();
      if(NULL == sys_q_ptr)
      {
         result = AR_EFAILED;
         AR_MSG(DBG_ERROR_PRIO, "Failed to get valid spf_sys_q");
         goto __bail_out_create;
      }
   }

   /** Update command handle */
   spf_msg_init_cmd_handle(&apm_info_ptr->cmd_handle,
                           apm_info_ptr->cmd_handle.thread_id,
                           (posal_queue_t *)apm_info_ptr->q_list_ptr[APM_CMD_Q_IDX],
                           sys_q_ptr);

   /** Update APM handle */
   spf_msg_init_handle(&apm_info_ptr->handle,
                       &apm_info_ptr->cmd_handle,
                       (posal_queue_t *)apm_info_ptr->q_list_ptr[APM_RSP_Q_IDX]);

   /** Register with IRM */
   if (AR_EOK != (result = irm_register_static_module(APM_MODULE_INSTANCE_ID, APM_INTERNAL_STATIC_HEAP_ID, posal_thread_get_tid_v2(apm_info_ptr->handle.cmd_handle_ptr->thread_id))))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM INIT: Failed to register with IRM, result: 0x%8x", result);
   }

   /** Register with GPR */
   if (AR_EOK != (gpr_result = __gpr_cmd_register(APM_MODULE_INSTANCE_ID, apm_gpr_call_back_f, &apm_info_ptr->handle)))
   {
      AR_MSG(DBG_ERROR_PRIO, "APM INIT: Failed to register with GPR, result: 0x%8x", gpr_result);
   }

   uint32_t host_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   // Set the start value of container count based on processor domain.
   // This would help different SPF instances to start with unique log_id
   apm_info_ptr->graph_info.container_count = (uint16_t)(256 * host_domain_id);

   AR_MSG(DBG_HIGH_PRIO, "APM thread launched successfully");

   return result;

__bail_out_create:

   AR_MSG(DBG_ERROR_PRIO, "APM create failed, destroying ...");

   /** Destroy APM and free-up all partially allocated resource */
   apm_destroy();

   return result;
}

uint32_t apm_get_mem_map_client()
{
   return g_apm_info.memory_map_client;
}

spf_handle_t *apm_get_apm_handle()
{
   return &g_apm_info.handle;
}

apm_ext_utils_t *apm_get_ext_utils_ptr()
{
   return &g_apm_info.ext_utils;
}

#if USES_DEBUG_DEV_ENV
void apm_print_mem_req()
{
   printf(  "APM"
            "\nPer Subgraph                  %lu"
            "\nPer Container                 %lu"
            "\nPer SG, per container         %lu"
            "\nPer Module                    %lu"
            "\nPer Container port            %lu"
            "\nPer Module port conn          %lu"
            "\n",
             sizeof(apm_sub_graph_t) + sizeof(spf_list_node_t),
             sizeof(apm_container_t) + sizeof(apm_cont_graph_t) + 2 * sizeof(spf_list_node_t),
             sizeof(apm_pspc_module_list_t) + sizeof(spf_list_node_t),
             sizeof(apm_module_t) + sizeof(spf_list_node_t),
             sizeof(apm_cont_port_connect_info_t) + sizeof(spf_list_node_t),
             sizeof(spf_module_port_conn_t) + sizeof(spf_list_node_t)

         );
}
#endif
