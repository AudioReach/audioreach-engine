/**
 * \file amdb_offload_utils.c
 * \brief
 *     This file contains utility functions for AMDB command handling
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "amdb_offload_utils.h"
#include "amdb_api.h"
#include "spf_svc_calib.h"
#include "apm.h" //needed for mem-map handles
#include "apm_offload_mem.h"

static uint32_t amdb_token = 0;

ar_result_t amdb_clean_up_proc_id_cmd_ctrl(amdb_thread_t *amdb_info_ptr, uint32_t proc_domain_id)
{
   ar_result_t result = AR_EOK;

   for (spf_list_node_t *list_ptr = amdb_info_ptr->cmd_ctrl_list; list_ptr;)
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         amdb_cmd_ctrl_t *cur_cmd_ctrl_ptr = (amdb_cmd_ctrl_t *)cur_obj_ptr;
         if (proc_domain_id == cur_cmd_ctrl_ptr->dst_domain_id)
         {
            gpr_packet_t *cmd_gpr_payload_ptr = (gpr_packet_t *)cur_cmd_ctrl_ptr->cmd_msg.payload_ptr;
            __gpr_cmd_end_command(cmd_gpr_payload_ptr, AR_EFAILED); // send fail response to master's client
            if (NULL != cur_cmd_ctrl_ptr->loaned_mem_ptr)
            {
               apm_offload_memory_free(&cur_cmd_ctrl_ptr->ret_info);
            }
            LIST_ADVANCE(list_ptr); // advance the list before deleting it
            result |= amdb_clear_cmd_ctrl(amdb_info_ptr, cur_cmd_ctrl_ptr);
            continue;
         }
      }
      LIST_ADVANCE(list_ptr);
   }

   return result;
}

ar_result_t amdb_set_cmd_ctrl(amdb_thread_t *   amdb_info_ptr,
                              spf_msg_t *       msg_ptr,
                              void *            master_payload_ptr,
                              bool_t            is_out_of_band,
                              amdb_cmd_ctrl_t **curr_cmd_ctrl_pptr,
                              uint32_t          dst_domain_id)
{
   ar_result_t      result = AR_EOK;
   gpr_packet_t *   gpr_pkt_ptr;
   amdb_cmd_ctrl_t *cmd_ctrl_ptr;
   uint32_t         cmd_opcode;

   /** Get GPR packet pointer */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   cmd_ctrl_ptr = (amdb_cmd_ctrl_t *)posal_memory_malloc(sizeof(amdb_cmd_ctrl_t), amdb_info_ptr->heap_id);

   if (NULL == cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB offload Cmd Ctrl: Failed to allocate memory for cmd ctrl node. Failing");
      return AR_ENOMEMORY;
   }

   /** Return the current cmd ctrl under process */
   *curr_cmd_ctrl_pptr = cmd_ctrl_ptr;

   /** Cache the GK message in command control */
   memscpy(&cmd_ctrl_ptr->cmd_msg, sizeof(spf_msg_t), msg_ptr, sizeof(spf_msg_t));

   cmd_ctrl_ptr->token = amdb_token++;

   /** Save GPR command opcode */
   cmd_ctrl_ptr->cmd_opcode = cmd_opcode;

   /** Save Loaned mem ptr */
   cmd_ctrl_ptr->loaned_mem_ptr = NULL;

   cmd_ctrl_ptr->master_payload_ptr = master_payload_ptr;

   cmd_ctrl_ptr->is_out_of_band = is_out_of_band;

   cmd_ctrl_ptr->dst_domain_id = dst_domain_id;

   spf_list_insert_tail(&amdb_info_ptr->cmd_ctrl_list, cmd_ctrl_ptr, amdb_info_ptr->heap_id, TRUE /* use_pool*/);

   return result;
}

ar_result_t amdb_clear_cmd_ctrl(amdb_thread_t *amdb_info_ptr, amdb_cmd_ctrl_t *amdb_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   if (!amdb_info_ptr || !amdb_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "amdb_clear_cmd_ctrl(): AMDB info ptr(0x%lX) and/or AMDB cmd ctrl ptr(0x%lX) is NULL");

      return AR_EFAILED;
   }
   spf_list_find_delete_node(&amdb_info_ptr->cmd_ctrl_list, (void *)amdb_cmd_ctrl_ptr, TRUE /* use_pool*/);

   posal_memory_free(amdb_cmd_ctrl_ptr);

   return result;
}

static amdb_cmd_ctrl_t *amdb_get_cmd_ctrl_obj(amdb_thread_t *amdb_info_ptr, uint32_t token)
{
   for (spf_list_node_t *list_ptr = amdb_info_ptr->cmd_ctrl_list; list_ptr; LIST_ADVANCE(list_ptr))
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         amdb_cmd_ctrl_t *cur_cmd_ctrl_ptr = (amdb_cmd_ctrl_t *)cur_obj_ptr;
         if (token == cur_cmd_ctrl_ptr->token)
         {
            return cur_cmd_ctrl_ptr;
         }
      }
   }
   return NULL;
}

/*
 * Function to route the command to the destination domain.
 * Caller will free the GPR packet in case of any error.
 * This function should only manage the memory allocated within.
 */
ar_result_t amdb_route_cmd_to_satellite(amdb_thread_t *amdb_info_ptr,
                                        spf_msg_t *    msg_ptr,
                                        uint32_t       dst_domain_id,
                                        uint8_t *      amdb_payload_ptr)
{
   ar_result_t result = AR_EOK;

   /** Get the pointer to GPR command */
   gpr_packet_t *    pkt_ptr             = (gpr_packet_t *)msg_ptr->payload_ptr;
   void *            payload_ptr         = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
   gpr_packet_t *    new_cmd_gpr_ptr     = NULL;
   void *            new_cmd_payload_ptr = NULL;
   apm_cmd_header_t *new_apm_header_ptr  = NULL;
   bool_t            is_out_of_band      = FALSE;
   uint32_t          amdb_payload_size   = 0;
   amdb_cmd_ctrl_t * curr_cmd_ctrl_ptr   = NULL;
   /** Set the command control for this command */
   if (AR_EOK != (result = amdb_set_cmd_ctrl(amdb_info_ptr,
                                             msg_ptr,
                                             (void *)amdb_payload_ptr,
                                             is_out_of_band,
                                             &curr_cmd_ctrl_ptr,
                                             dst_domain_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Setting cmd ctrl failed. Dst Domain ID = %lu", dst_domain_id);
      return result;
   }

   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id; // Master ID
   args.src_port      = pkt_ptr->dst_port;
   args.dst_domain_id = dst_domain_id; // satellite domain id
   args.dst_port      = pkt_ptr->dst_port;
   args.client_data   = 0;
   args.token         = curr_cmd_ctrl_ptr->token;
   // Will use this token during response tracking and for bookkeeping
   args.opcode = pkt_ptr->opcode;
   // should automatically be correct regardless of in/outof band
   args.payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(pkt_ptr->header);
   args.ret_packet   = &new_cmd_gpr_ptr;

#ifdef AMDB_OFFLOAD_DBG
   AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Routing GPR PKT Payload size %lu", args.payload_size);
#endif

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == new_cmd_gpr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Allocating new cmd pkt failed with %lu", result);
      result |= AR_ENOMEMORY;
      goto __bailout_route_cmd_1;
   }
   /* prepare the cmd payload */
   new_cmd_payload_ptr = GPR_PKT_GET_PAYLOAD(void, new_cmd_gpr_ptr);
   memscpy(new_cmd_payload_ptr, args.payload_size, payload_ptr, args.payload_size);

   new_apm_header_ptr = (apm_cmd_header_t *)new_cmd_payload_ptr;
   is_out_of_band     = (0 != new_apm_header_ptr->mem_map_handle);
   /*First, if the command is received out of band, let's copy into the loaned mem for the sat*/
   if (is_out_of_band)
   {
      amdb_payload_size = new_apm_header_ptr->payload_size;
      curr_cmd_ctrl_ptr->loaned_mem_ptr =
         (void *)apm_offload_memory_malloc(dst_domain_id, amdb_payload_size, &curr_cmd_ctrl_ptr->ret_info);

      if (NULL == curr_cmd_ctrl_ptr->loaned_mem_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Error while allocating memory for sat domain id %lu", dst_domain_id);
         return AR_ENOMEMORY;
      }

      memscpy(curr_cmd_ctrl_ptr->loaned_mem_ptr, amdb_payload_size, amdb_payload_ptr, amdb_payload_size);
      posal_cache_flush_v2(&curr_cmd_ctrl_ptr->loaned_mem_ptr, amdb_payload_size);

#ifdef AMDB_OFFLOAD_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "Copied payload of size %lu, into loaned mem for sat ID %lu. Sat mem handle is %lu",
             amdb_payload_size,
             dst_domain_id,
             ret_info.sat_handle);
#endif
      // replace the mem handle with the sat handle
      new_apm_header_ptr->mem_map_handle      = curr_cmd_ctrl_ptr->ret_info.sat_handle;
      new_apm_header_ptr->payload_address_lsw = curr_cmd_ctrl_ptr->ret_info.offset; // offset mode mapped
      new_apm_header_ptr->payload_address_msw = 0;
      curr_cmd_ctrl_ptr->is_out_of_band       = TRUE;
   }

   if (AR_EOK != (result = __gpr_cmd_async_send(new_cmd_gpr_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: sending command to sat failed with %lu", result);
      goto __bailout_route_cmd_2;
   }

   return result;

__bailout_route_cmd_2:
   __gpr_cmd_free(new_cmd_gpr_ptr);
   if (NULL != curr_cmd_ctrl_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&curr_cmd_ctrl_ptr->ret_info);
   }
__bailout_route_cmd_1:
   // Note: caller responds to the client
   amdb_clear_cmd_ctrl(amdb_info_ptr, curr_cmd_ctrl_ptr);
   return result;
}

ar_result_t amdb_route_basic_rsp_to_client(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t *           basic_rsp_pkt_ptr     = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t *           cmd_gpr_payload_ptr   = NULL;
   gpr_ibasic_rsp_result_t *basic_rsp_payload_ptr = NULL;
   amdb_cmd_ctrl_t *        ctrl_obj_ptr          = NULL;
   apm_cmd_header_t *       cmd_apm_header_ptr    = NULL;

   // just to print the ocpode
   basic_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, basic_rsp_pkt_ptr);
   result                = basic_rsp_payload_ptr->status;
   AR_MSG(DBG_HIGH_PRIO,
          "AMDB Offload: Received Basic Response on Master for cmd opcode 0x%lx with status %d",
          basic_rsp_payload_ptr->opcode,
          result);

   ctrl_obj_ptr = amdb_get_cmd_ctrl_obj(amdb_info_ptr, basic_rsp_pkt_ptr->token);

   // If the cached command was cleared for some reason, ignore
   if (NULL == ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "AMDB: Offload: Basic Rsp Handler: WARNING: ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   cmd_gpr_payload_ptr = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
   cmd_apm_header_ptr  = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, cmd_gpr_payload_ptr);

   if (ctrl_obj_ptr->is_out_of_band)
   {
      posal_cache_invalidate_v2(&ctrl_obj_ptr->loaned_mem_ptr, cmd_apm_header_ptr->payload_size);
      memscpy(ctrl_obj_ptr->master_payload_ptr,
              cmd_apm_header_ptr->payload_size,
              ctrl_obj_ptr->loaned_mem_ptr,
              cmd_apm_header_ptr->payload_size);
      posal_cache_flush_v2(&ctrl_obj_ptr->master_payload_ptr, cmd_apm_header_ptr->payload_size);
   }

   __gpr_cmd_free(basic_rsp_pkt_ptr);
   __gpr_cmd_end_command(cmd_gpr_payload_ptr, result); // send response to master's client
   if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
   }
   amdb_clear_cmd_ctrl(amdb_info_ptr, ctrl_obj_ptr);

   return result;
}

ar_result_t amdb_route_get_cfg_rsp_to_client(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t *         get_cfg_rsp_pkt_ptr          = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t *         original_cmd_gpr_payload_ptr = NULL;
   apm_cmd_rsp_get_cfg_t *get_cfg_rsp_payload_ptr      = NULL;
   amdb_cmd_ctrl_t *      ctrl_obj_ptr                 = NULL;
   apm_cmd_header_t *     cmd_apm_header_ptr           = NULL;

   // just to print the ocpode
   get_cfg_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, get_cfg_rsp_pkt_ptr);
   result                  = get_cfg_rsp_payload_ptr->status;
   AR_MSG(DBG_HIGH_PRIO, "AMDB Offload: Received get-cfg Response on Master with status %d", result);

   ctrl_obj_ptr = amdb_get_cmd_ctrl_obj(amdb_info_ptr, get_cfg_rsp_pkt_ptr->token);

   // If the cached command was cleared for some reason, ignore
   if (NULL == ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "AMDB: Offload: Basic Rsp Handler: WARNING: ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(get_cfg_rsp_pkt_ptr);
      return result;
   }

   original_cmd_gpr_payload_ptr = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
   cmd_apm_header_ptr           = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, original_cmd_gpr_payload_ptr);

   uint8_t *     payload_ptr      = NULL;
   uint32_t      alignment_size   = 0;
   gpr_packet_t *gpr_rsp_pkt_ptr  = NULL;
   uint8_t **    cmd_payload_pptr = (uint8_t **)&payload_ptr;
   *cmd_payload_pptr              = NULL;

   /* For OOB payloads, payload_ptr will be filled with correct dereferenced oob ptr
    * For in-band which needs response, this will allocate, copy and assign the payload to payload_ptr
    * For in-band payload which doesn't need response, payload_ptr will be filled from apm header */
   result = spf_svc_get_cmd_payload_addr(AMDB_MODULE_INSTANCE_ID,
                                         original_cmd_gpr_payload_ptr,
                                         &gpr_rsp_pkt_ptr,
                                         cmd_payload_pptr,
                                         &alignment_size,
                                         NULL,
                                         apm_get_mem_map_client());

   if (ctrl_obj_ptr->is_out_of_band)
   {
      posal_cache_invalidate_v2(&ctrl_obj_ptr->loaned_mem_ptr, cmd_apm_header_ptr->payload_size);
      memscpy(ctrl_obj_ptr->master_payload_ptr,
              cmd_apm_header_ptr->payload_size,
              ctrl_obj_ptr->loaned_mem_ptr,
              cmd_apm_header_ptr->payload_size);
      posal_cache_flush_v2(&ctrl_obj_ptr->master_payload_ptr, cmd_apm_header_ptr->payload_size);
   }

   __gpr_cmd_free(get_cfg_rsp_pkt_ptr);

   apm_cmd_rsp_get_cfg_t *cmd_get_cfg_rsp_ptr = NULL;
   if (!ctrl_obj_ptr->is_out_of_band)
   {
      cmd_get_cfg_rsp_ptr         = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, gpr_rsp_pkt_ptr);
      cmd_get_cfg_rsp_ptr->status = result;
      result                      = __gpr_cmd_async_send(get_cfg_rsp_pkt_ptr);
      if (AR_EOK != result)
      {
         __gpr_cmd_free(get_cfg_rsp_pkt_ptr);
         result = AR_EFAILED;
      }
      __gpr_cmd_end_command(original_cmd_gpr_payload_ptr, result);
   }
   else
   {
      posal_cache_flush_v2(&payload_ptr, cmd_apm_header_ptr->payload_size);
      apm_cmd_rsp_get_cfg_t cmd_get_cfg_rsp = { 0 };
      cmd_get_cfg_rsp.status                = result;

      gpr_cmd_alloc_send_t args;
      args.src_domain_id = original_cmd_gpr_payload_ptr->dst_domain_id;
      args.dst_domain_id = original_cmd_gpr_payload_ptr->src_domain_id;
      args.src_port      = original_cmd_gpr_payload_ptr->dst_port;
      args.dst_port      = original_cmd_gpr_payload_ptr->src_port;
      args.token         = original_cmd_gpr_payload_ptr->token;
      args.opcode        = APM_CMD_RSP_GET_CFG;
      args.payload       = &cmd_get_cfg_rsp;
      args.payload_size  = sizeof(apm_cmd_rsp_get_cfg_t);
      args.client_data   = 0;
      result             = __gpr_cmd_alloc_send(&args);
      __gpr_cmd_free(original_cmd_gpr_payload_ptr);
   }
   if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
   }
   amdb_clear_cmd_ctrl(amdb_info_ptr, ctrl_obj_ptr);
   AR_MSG(DBG_MED_PRIO, "AMDB: done responding to get cfg.");

   return result;
}

ar_result_t amdb_route_load_rsp_to_client(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t *    pkt_ptr             = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t *    cmd_gpr_payload_ptr = NULL;
   gpr_packet_t *    new_rsp_gpr_ptr     = NULL;
   void *            new_rsp_payload_ptr, *payload_ptr = NULL;
   amdb_cmd_ctrl_t * ctrl_obj_ptr       = NULL;
   apm_cmd_header_t *cmd_apm_header_ptr = NULL;

   ctrl_obj_ptr = amdb_get_cmd_ctrl_obj(amdb_info_ptr, pkt_ptr->token);
   // If the cached command was cleared for some reason, ignore
   if (NULL == ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "AMDB: Offload: Load Rsp Handler: WARNING: ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(pkt_ptr);
      return result;
   }

   // validation
   if (ctrl_obj_ptr->cmd_opcode != AMDB_CMD_LOAD_MODULES)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "AMDB: Offload: Load Rsp Handler: ctrl_obj_ptr->cmd_opcode 0x%lx is Incorrect. Bailing out.",
             ctrl_obj_ptr->cmd_opcode);

      goto __bailout_load_rsp_hdlr_1;
   }

   // original GPR ptr
   cmd_gpr_payload_ptr = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
   cmd_apm_header_ptr  = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, cmd_gpr_payload_ptr);
   // get current sat_load rsp gpr payload
   payload_ptr = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
   /*If out of band, satellite would have already flushed the payload to loaned mem,
     and also an in-band gpr command with the full response. We need to flush the same.
     If inband, we'll just get the whole payload as the gpr response.*/

   AR_MSG(DBG_HIGH_PRIO,
          "Received MDF AMDB Load RSP sat domain %lu, port 0x%lx",
          pkt_ptr->src_domain_id,
          pkt_ptr->src_port);

   if (ctrl_obj_ptr->is_out_of_band)
   {
      posal_cache_invalidate_v2(&ctrl_obj_ptr->loaned_mem_ptr, cmd_apm_header_ptr->payload_size);
      memscpy(ctrl_obj_ptr->master_payload_ptr,
              cmd_apm_header_ptr->payload_size,
              ctrl_obj_ptr->loaned_mem_ptr,
              cmd_apm_header_ptr->payload_size);
      posal_cache_flush_v2(&ctrl_obj_ptr->master_payload_ptr, cmd_apm_header_ptr->payload_size);
   }

   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id; // Master ID
   args.src_port      = pkt_ptr->dst_port;
   args.dst_domain_id = cmd_gpr_payload_ptr->src_domain_id;
   args.dst_port      = cmd_gpr_payload_ptr->src_port;
   args.client_data   = 0;
   args.token         = cmd_gpr_payload_ptr->token;
   args.opcode        = AMDB_CMD_RSP_LOAD_MODULES;
   // should automatically be correct regardless of in/outof band
   args.payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(pkt_ptr->header);
   args.ret_packet   = &new_rsp_gpr_ptr;

#ifdef AMDB_OFFLOAD_DBG
   AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Load RSP Payload size %lu", args.payload_size);
#endif

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == new_rsp_gpr_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: Allocating new Load rsp pkt failed with %lu", result);
      goto __bailout_load_rsp_hdlr_1;
   }

   /* prepare the cmd payload */
   new_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(void, new_rsp_gpr_ptr);
   memscpy(new_rsp_payload_ptr, args.payload_size, payload_ptr, args.payload_size);

   if (AR_EOK != (result = __gpr_cmd_async_send(new_rsp_gpr_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "AMDB: Offload: sending Load RSP to client failed with %lu", result);
      goto __bailout_load_rsp_hdlr_2;
   }

   // Free the sat load resp packet as we don't respond to it
   __gpr_cmd_free(pkt_ptr);
   // Free the original command packet
   __gpr_cmd_free(cmd_gpr_payload_ptr);
   if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
   }
   // Clear AMDB cmd message control
   amdb_clear_cmd_ctrl(amdb_info_ptr, ctrl_obj_ptr);
   return result;

__bailout_load_rsp_hdlr_2:
   __gpr_cmd_free(new_rsp_gpr_ptr);
__bailout_load_rsp_hdlr_1:
   __gpr_cmd_free(pkt_ptr);
   __gpr_cmd_end_command(cmd_gpr_payload_ptr, result);
   if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
   }
   amdb_clear_cmd_ctrl(amdb_info_ptr, ctrl_obj_ptr);
   return result;
}
