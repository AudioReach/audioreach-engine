/**
 * \file amdb_thread.c
 * \brief
 *     This file contains AMDB Module Implementation
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/
#include "amdb_thread_i.h"
#include "amdb_cmd_handler.h"
#include "gpr_ids_domains.h"
#include "gpr_api_inline.h"
#include "ar_guids.h"
#include "amdb_api.h"
#include "spf_msg_util.h"
#include "spf_macros.h"
#include "amdb_offload_utils.h"

#include "irm.h"

/** AMDB thread instance global object */
static amdb_thread_t g_amdb_thread;

/** Pointer to AMDB thread global struct */
amdb_thread_t *g_amdb_thread_ptr;

static char           amdb_cmd_q_name[] = "amdb_cmd_q";
static char           amdb_rsp_q_name[] = "amdb_rsp_q";
static const uint32_t amdb_cmd_q_mask   = 0x00000002UL;
static const uint32_t amdb_rsp_q_mask   = 0x00000004UL;
static const uint32_t amdb_sys_q_mask   = 0x00000008UL;

static char AMDB_THREAD_NAME[] = "AMDB";
/** AMDB service thread stack size in bytes */
#define AMDB_THREAD_STACK_SIZE 4096

#define AMDB_MAX_SYS_Q_ELEMENTS 8
#define AMDB_PREALLOC_SYS_Q_ELEMENTS 8

uint32_t amdb_gpr_call_back_f(gpr_packet_t *gpr_pkt_ptr, void *cb_ctx_ptr)
{
   uint32_t       result = AR_EOK;
   spf_msg_t      msg;
   uint32_t       cmd_opcode;
   spf_handle_t  *dst_handle_ptr = NULL;
   posal_queue_t *temp_ptr       = NULL;

   /* Validate GPR packet pointer */
   if (!gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB GPR CB: GPR pkt ptr is NULL");

      return AR_EFAILED;
   }

   /* Get the GPR command opcode */
   cmd_opcode           = gpr_pkt_ptr->opcode;
   uint32_t opcode_type = ((cmd_opcode & AR_GUID_TYPE_MASK) >> AR_GUID_TYPE_SHIFT);

   AR_MSG(DBG_HIGH_PRIO, "AMDB GPR CB, rcvd cmd opcode[0x%08lX]", cmd_opcode);

   /* Validate GPR callback context pointer */
   if (!cb_ctx_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB GPR CB: CB ctxt ptr is NULL");
      goto __bailout;
   }

   /* Get the destination module handle */
   dst_handle_ptr = (spf_handle_t *)cb_ctx_ptr;
   temp_ptr       = dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr;

   if ((((opcode_type & AR_GUID_TYPE_CONTROL_CMD_RSP) == AR_GUID_TYPE_CONTROL_CMD_RSP)) &&
       ((posal_queue_t *)g_amdb_thread_ptr->amdb_cmd_q_ptr == dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr))
   {
      // MDF Response from the satellite: route it to a different queue
      dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr = (posal_queue_t *)g_amdb_thread_ptr->amdb_rsp_q_ptr;
   }

   /** Compose the GK message payload to be routed to
    *  destination module */
   msg.msg_opcode  = SPF_MSG_CMD_GPR;
   msg.payload_ptr = gpr_pkt_ptr;

   /* Push msg to the destination module queue */
   if (AR_DID_FAIL(result = spf_msg_send_cmd(&msg, dst_handle_ptr)))
   {
      AR_MSG(DBG_HIGH_PRIO, "Failed to push gpr msg to AMDB cmd_q");
      goto __bailout;
   }

   dst_handle_ptr->cmd_handle_ptr->cmd_q_ptr = temp_ptr; // replace it with the actual qptr
   return result;

__bailout:

   /* End the GPR command */
   __gpr_cmd_end_command(gpr_pkt_ptr, result);

   return AR_EOK;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_thread_handle_sat_pd_info(uint32_t num_proc_domain_ids, uint32_t *proc_domain_id_list)
{
   return spf_sys_util_ssr_register(g_amdb_thread.sys_util_handle_ptr, num_proc_domain_ids, proc_domain_id_list);
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_thread_handle_close_all(void *close_all_ptr)
{
   ar_result_t                    result        = AR_EOK;
   amdb_thread_t                 *amdb_info_ptr = &g_amdb_thread;
   spf_msg_t                      msg_pkt;
   uint32_t                       curr_wait_mask = amdb_cmd_q_mask;
   uint32_t                       channel_status;
   param_id_sys_util_close_all_t *close_all_payload_ptr = (param_id_sys_util_close_all_t *)close_all_ptr;

   AR_MSG(DBG_HIGH_PRIO, "AMDB: Flushing CMD queue");
   if (close_all_payload_ptr->is_flush_needed)
   {
      while (amdb_cmd_q_mask & (channel_status = posal_channel_poll(amdb_info_ptr->cmd_channel, curr_wait_mask)))
      {
         /** Pop the message buffer from the cmd queue */
         if (AR_EOK != (result = posal_queue_pop_front((posal_queue_t *)amdb_info_ptr->amdb_cmd_q_ptr,
                                                       (posal_queue_element_t *)&msg_pkt)))
         {
            AR_MSG(DBG_ERROR_PRIO, "amdb_flush_cmd_queue(): Failed to pop buf from svc cmd_q, result: 0x%lx", result);
            return result;
         }
         /** Get the pointer to GPR command */
         gpr_packet_t *gpr_pkt_ptr = (gpr_packet_t *)msg_pkt.payload_ptr;
         __gpr_cmd_end_command(gpr_pkt_ptr, AR_EFAILED);
      }
   }
   // Reset is not needed for AMDB yet
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_thread_handle_svc_status(void *status_ptr)
{
   ar_result_t                     result          = AR_EOK;
   amdb_thread_t                  *amdb_info_ptr   = &g_amdb_thread;
   param_id_sys_util_svc_status_t *proc_status_ptr = (param_id_sys_util_svc_status_t *)status_ptr;

   AR_MSG(DBG_ERROR_PRIO,
          "AMDB: SSR notified to AMDB, cleaning up cached cmd ctrl, proc_domain_id = %lu, status = %lu",
          proc_status_ptr->proc_domain_id,
          proc_status_ptr->status);
   if (SPF_SYS_UTIL_SSR_STATUS_DOWN == proc_status_ptr->status)
   {
      result = amdb_clean_up_proc_id_cmd_ctrl(amdb_info_ptr, proc_status_ptr->proc_domain_id);
   }
   return result;
}

ar_result_t amdb_process_cmd_q(amdb_thread_t *amdb_info_ptr)
{
   ar_result_t result;
   spf_msg_t   msg_pkt;

   /** Pop the message buffer from the cmd queue */
   if (AR_EOK != (result = posal_queue_pop_front((posal_queue_t *)amdb_info_ptr->amdb_cmd_q_ptr,
                                                 (posal_queue_element_t *)&msg_pkt)))
   {
      AR_MSG(DBG_ERROR_PRIO, "amdb_process_cmd_q(): Failed to pop buf from svc cmd_q, result: 0x%lx", result);

      return result;
   }

   /** Call the AMDB svc command queue handler */
   return amdb_cmdq_gpr_cmd_handler(amdb_info_ptr, &msg_pkt);
}

ar_result_t amdb_process_rsp_q(amdb_thread_t *amdb_info_ptr)
{
   ar_result_t result;
   spf_msg_t   msg_pkt;

   /** Pop the message buffer from the rsp queue */
   if (AR_EOK != (result = posal_queue_pop_front((posal_queue_t *)amdb_info_ptr->amdb_rsp_q_ptr,
                                                 (posal_queue_element_t *)&msg_pkt)))
   {
      AR_MSG(DBG_ERROR_PRIO, "amdb_process_rsp_q(): Failed to pop buf from svc rsp_q, result: 0x%lx", result);

      return result;
   }

   /** Call the AMDB svc response queue handler */
   return amdb_rspq_gpr_rsp_handler(amdb_info_ptr, &msg_pkt);
}

ar_result_t amdb_process_q(amdb_thread_t *amdb_info_ptr, uint32_t channel_status)
{
   ar_result_t result = AR_EOK;

   if (!channel_status)
   {
      return AR_EOK;
   }

   if (channel_status & amdb_sys_q_mask)
   {
      result = spf_sys_util_handle_cmd(amdb_info_ptr->sys_util_handle_ptr);

      // return to the workloop immedietly, since the channnel_status will be different now
      return result;
   }

   if (channel_status & amdb_cmd_q_mask)
   {
      result |= amdb_process_cmd_q(amdb_info_ptr);
   }

   if (channel_status & amdb_rsp_q_mask)
   {
      result |= amdb_process_rsp_q(amdb_info_ptr);
   }
   return result;
}

static ar_result_t amdb_work_loop(void *arg_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    kill_sig_mask, curr_wait_mask;
   uint32_t    channel_status;

   /** Service Instance */
   amdb_thread_t *amdb_info_ptr = (amdb_thread_t *)arg_ptr;

   /** Get the signal bit for kill signal */
   kill_sig_mask = posal_signal_get_channel_bit(amdb_info_ptr->kill_signal);

   /** Set up mask for listening to the service cmd,rsp queues and
    *  kill signal for AMDB thread */
   curr_wait_mask = (amdb_cmd_q_mask | amdb_rsp_q_mask | kill_sig_mask);

   if (NULL != amdb_info_ptr->sys_util_handle_ptr)
   {
      curr_wait_mask |= amdb_info_ptr->sys_util_handle_ptr->sys_queue_mask;
   }

   AR_MSG(DBG_HIGH_PRIO, "Entering AMDB workloop...");

   /** Enter forever loop */
   for (;;)
   {
      /** Block on any one or more of selected queues to get a msg */
      channel_status = posal_channel_wait(amdb_info_ptr->cmd_channel, curr_wait_mask);

      /** Check if the KILL signal is received */
      if (channel_status & kill_sig_mask)
      {
         posal_signal_clear(amdb_info_ptr->kill_signal);

         /** Return from the workloop */
         return AR_EOK;
      }

      result = amdb_process_q(amdb_info_ptr, channel_status);
   } /** forever loop */

   return result;
}

ar_result_t amdb_thread_deinit()
{
   ar_result_t result = AR_EOK;

   amdb_thread_t *amdb_info_ptr = &g_amdb_thread;

   if (amdb_info_ptr->thread_launched)
   {
      /** Send Kill Signal to AMDB service workloop */
      posal_signal_send(amdb_info_ptr->kill_signal);

      /** Wait for the thread join */
      posal_thread_join(amdb_info_ptr->amdb_cmd_handle.thread_id, &result);
   }

   result |= spf_sys_util_release_handle(&amdb_info_ptr->sys_util_handle_ptr);

   /** De-init the service CMD queue */
   if (NULL != amdb_info_ptr->amdb_cmd_q_ptr)
   {
      posal_queue_destroy((posal_queue_t *)amdb_info_ptr->amdb_cmd_q_ptr);
   }

   /** De-init the service RSP queue */
   if (NULL != amdb_info_ptr->amdb_rsp_q_ptr)
   {
      posal_queue_destroy((posal_queue_t *)amdb_info_ptr->amdb_rsp_q_ptr);
   }

   /** Destroy the service Kill signal */
   if (0 != amdb_info_ptr->kill_signal)
   {
      posal_signal_destroy(&amdb_info_ptr->kill_signal);
   }

   /** Destroy the channel */
   if (0 != amdb_info_ptr->cmd_channel)
   {
      posal_channel_destroy(&amdb_info_ptr->cmd_channel);
   }

   /** De-register with  GPR    */
   if (AR_EOK != (result = __gpr_cmd_deregister(AMDB_MODULE_INSTANCE_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to de-register with GPR, result: %lu", result);
   }

   AR_MSG(DBG_HIGH_PRIO, "Completed amdb_thread_deinit() ...");

   return result;
}

ar_result_t amdb_thread_init(POSAL_HEAP_ID heap_id)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t    result        = AR_EOK;
   amdb_thread_t *amdb_info_ptr = &g_amdb_thread;

   AR_MSG(DBG_HIGH_PRIO, "Entering amdb_thread_init() ...");

   /** Clear the global structure */
   memset(amdb_info_ptr, 0, sizeof(amdb_thread_t));
   amdb_info_ptr->heap_id = heap_id;
   /** Init the AMDB global pointer */
   g_amdb_thread_ptr = amdb_info_ptr;
   posal_queue_init_attr_t q_attr;

   /** Create service AMDB cmd queue */
   TRY(result,
       posal_queue_set_attributes(&q_attr,
                                  amdb_info_ptr->heap_id,
                                  AMDB_MAX_CMD_Q_ELEMENTS,
                                  AMDB_MAX_CMD_Q_ELEMENTS,
                                  amdb_cmd_q_name));
   TRY(result, posal_queue_create_v1(&(amdb_info_ptr->amdb_cmd_q_ptr), &q_attr));

   /** Create service AMDB rsp queue */
   TRY(result,
       posal_queue_set_attributes(&q_attr,
                                  amdb_info_ptr->heap_id,
                                  AMDB_MAX_RSP_Q_ELEMENTS,
                                  AMDB_MAX_RSP_Q_ELEMENTS,
                                  amdb_rsp_q_name));
   TRY(result, posal_queue_create_v1(&(amdb_info_ptr->amdb_rsp_q_ptr), &q_attr));

   /** Create kill signal */
   TRY(result, posal_signal_create(&amdb_info_ptr->kill_signal, amdb_info_ptr->heap_id));

   /** Set up channel */
   TRY(result, posal_channel_create(&amdb_info_ptr->cmd_channel, amdb_info_ptr->heap_id));

   /** Add service amdb command queue to the channel */
   TRY(result,
       posal_channel_addq(amdb_info_ptr->cmd_channel, (posal_queue_t *)amdb_info_ptr->amdb_cmd_q_ptr, amdb_cmd_q_mask));

   /** Update the amdb command Q mask */
   amdb_info_ptr->amdb_cmd_q_wait_mask = amdb_cmd_q_mask;

   /** Add service amdb rsp queue to the channel */
   TRY(result,
       posal_channel_addq(amdb_info_ptr->cmd_channel, (posal_queue_t *)amdb_info_ptr->amdb_rsp_q_ptr, amdb_rsp_q_mask));

   /** Update the rsp rsp Q mask */
   amdb_info_ptr->amdb_rsp_q_wait_mask = amdb_cmd_q_mask;

   spf_sys_util_vtable vtable = { .spf_sys_util_handle_svc_status  = amdb_thread_handle_svc_status,
                                  .spf_sys_util_handle_close_all   = amdb_thread_handle_close_all,
                                  .spf_sys_util_handle_sat_pd_info = amdb_thread_handle_sat_pd_info };

   amdb_info_ptr->sys_util_handle_ptr = NULL;

   TRY(result,
       spf_sys_util_get_handle(&amdb_info_ptr->sys_util_handle_ptr,
                               &vtable,
                               amdb_info_ptr->cmd_channel,
                               "amdb_sys_q",
                               amdb_sys_q_mask,
                               amdb_info_ptr->heap_id,
                               AMDB_MAX_SYS_Q_ELEMENTS,
                               AMDB_PREALLOC_SYS_Q_ELEMENTS));

   /** Add kill signal to the channel */
   TRY(result,
       posal_channel_add_signal(amdb_info_ptr->cmd_channel, amdb_info_ptr->kill_signal, POSAL_CHANNEL_MASK_DONT_CARE));

   posal_thread_prio_t amdb_thread_prio = 0;
   uint32_t sched_policy = 0, affinity_mask = 0;
   prio_query_t        query_tbl;

   // frame duration is dont care since its static thread prio
   query_tbl.frame_duration_us = 0;
   query_tbl.is_interrupt_trig = FALSE;
   query_tbl.static_req_id     = SPF_THREAD_STAT_AMDB_ID;

   TRY(result, posal_thread_determine_attributes(&query_tbl, &amdb_thread_prio, &sched_policy, &affinity_mask));

   /** Launch the thread */
   TRY(result,
       posal_thread_launch3(&amdb_info_ptr->amdb_cmd_handle.thread_id,
                           AMDB_THREAD_NAME,
                           AMDB_THREAD_STACK_SIZE,
                           0,
                           amdb_thread_prio,
                           amdb_work_loop,
                           (void *)amdb_info_ptr,
                           amdb_info_ptr->heap_id,
                           sched_policy,
                           affinity_mask));

   amdb_info_ptr->thread_launched = TRUE;

   /** Update command handle */
   spf_msg_init_cmd_handle(&amdb_info_ptr->amdb_cmd_handle,
                           amdb_info_ptr->amdb_cmd_handle.thread_id, // Use same thread id
                           (posal_queue_t *)amdb_info_ptr->amdb_cmd_q_ptr,
                           amdb_info_ptr->sys_util_handle_ptr->sys_queue_ptr);

   /** Update AMDB handle */
   spf_msg_init_handle(&amdb_info_ptr->amdb_handle,
                       &amdb_info_ptr->amdb_cmd_handle,
                       (posal_queue_t *)amdb_info_ptr->amdb_rsp_q_ptr);

   TRY(result, __gpr_cmd_register(AMDB_MODULE_INSTANCE_ID, amdb_gpr_call_back_f, &amdb_info_ptr->amdb_handle));

   TRY(result,  irm_register_static_module(AMDB_MODULE_INSTANCE_ID, heap_id, posal_thread_get_tid_v2(amdb_info_ptr->amdb_cmd_handle.thread_id)));

   AR_MSG(DBG_HIGH_PRIO, "AMDB thread launched successfully");

   return result;

   CATCH(result, AMDB_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: create failed, destroying ...");
   }

   /** Destroy AMDB and free-up all partially allocated resource */
   amdb_thread_deinit();

   return result;
}

ar_result_t amdb_thread_reset(bool_t is_flush_needed, bool_t is_reset_needed)
{
   ar_result_t            result              = AR_EOK;
   amdb_thread_t         *amdb_info_ptr       = &g_amdb_thread;
   spf_sys_util_handle_t *sys_util_handle_ptr = amdb_info_ptr->sys_util_handle_ptr;

   result =
      spf_sys_util_sync_push_close_all(sys_util_handle_ptr, amdb_info_ptr->heap_id, is_flush_needed, is_reset_needed);
   return result;
}

ar_result_t amdb_get_spf_handle(void **spf_handle_pptr)
{
   ar_result_t    result        = AR_EOK;
   spf_handle_t **hanndle_pptr  = (spf_handle_t **)spf_handle_pptr;
   amdb_thread_t *amdb_info_ptr = &g_amdb_thread;
   if (NULL == hanndle_pptr)
   {
      return AR_EBADPARAM;
   }

   if (0 != amdb_info_ptr->thread_launched)
   {
      *hanndle_pptr = &(amdb_info_ptr->amdb_handle);
   }
   else
   {
      return AR_ENOTREADY;
   }

   return result;
}
