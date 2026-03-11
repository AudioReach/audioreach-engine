/**
 * \file irm_offload_utils.c
 * \brief
 *     This file contains utility functions for IRM command handling
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "irm_offload_utils.h"
#include "ar_msg.h"
#include "irm_dev_cfg.h"
#include "irm_api.h"
#include "apm_offload_mem.h"
#include "irm_i.h"
#include "spf_svc_calib.h"

#define IRM_OFFLOAD_DBG
static uint16_t irm_token = 0;

ar_result_t irm_clean_up_proc_id_cmd_ctrl(irm_t *irm_ptr, uint32_t proc_domain_id)
{
   ar_result_t result = AR_EOK;

   for (spf_list_node_t *list_ptr = irm_ptr->cmd_ctrl_list; list_ptr;)
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         irm_cmd_ctrl_t *cur_cmd_ctrl_ptr = (irm_cmd_ctrl_t *)cur_obj_ptr;
         if (proc_domain_id == cur_cmd_ctrl_ptr->dst_domain_id)
         {
            gpr_packet_t *cmd_gpr_payload_ptr = (gpr_packet_t *)cur_cmd_ctrl_ptr->cmd_msg.payload_ptr;
            __gpr_cmd_end_command(cmd_gpr_payload_ptr, AR_EFAILED); // send fail response to master's client
            if (NULL != cur_cmd_ctrl_ptr->loaned_mem_ptr)
            {
               apm_offload_memory_free(&cur_cmd_ctrl_ptr->ret_info);
            }
            LIST_ADVANCE(list_ptr); // advance the list before deleting it
            result |= irm_clear_cmd_ctrl(irm_ptr, cur_cmd_ctrl_ptr);
            continue;
         }
      }
      LIST_ADVANCE(list_ptr);
   }

   return result;
}

// TODO:pbm Move to ext folder and test with stub
ar_result_t irm_set_cmd_ctrl(irm_t           *irm_ptr,
                             spf_msg_t       *msg_ptr,
                             void            *master_payload_ptr,
                             bool_t           is_out_of_band,
                             irm_cmd_ctrl_t **curr_cmd_ctrl_pptr,
                             uint32_t         dst_domain_id)
{
   ar_result_t     result = AR_EOK;
   gpr_packet_t   *gpr_pkt_ptr;
   irm_cmd_ctrl_t *cmd_ctrl_ptr;
   uint32_t        cmd_opcode;

   /** Get GPR packet pointer */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   cmd_opcode = gpr_pkt_ptr->opcode;

   cmd_ctrl_ptr = (irm_cmd_ctrl_t *)posal_memory_malloc(sizeof(irm_cmd_ctrl_t), irm_ptr->heap_id);

   if (NULL == cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM offload Cmd Ctrl: Failed to allocate memory for cmd ctrl node. Failing");
      return AR_ENOMEMORY;
   }

   /** Return the current cmd ctrl under process */
   *curr_cmd_ctrl_pptr = cmd_ctrl_ptr;

   /** Cache the GK message in command control */
   memscpy(&cmd_ctrl_ptr->cmd_msg, sizeof(spf_msg_t), msg_ptr, sizeof(spf_msg_t));

   cmd_ctrl_ptr->token = irm_token++;

   /** Save GPR command opcode */
   cmd_ctrl_ptr->cmd_opcode = cmd_opcode;

   /** Save Loaned mem ptr */
   cmd_ctrl_ptr->loaned_mem_ptr = NULL;

   cmd_ctrl_ptr->master_payload_ptr = master_payload_ptr;

   cmd_ctrl_ptr->is_out_of_band = is_out_of_band;

   cmd_ctrl_ptr->dst_domain_id = dst_domain_id;

   cmd_ctrl_ptr->num_resp_pending = 0; // This gets set when the cmds are actually sent
   cmd_ctrl_ptr->bytes_written    = 0;

   cmd_ctrl_ptr->get_cfg_rsp_cmd_ctrl_list_ptr = NULL;

   spf_list_insert_tail(&irm_ptr->cmd_ctrl_list, cmd_ctrl_ptr, irm_ptr->heap_id, TRUE /* use_pool*/);

   return result;
}

bool_t irm_offload_any_cmd_pending(irm_t *irm_ptr)
{
   for (spf_list_node_t *list_ptr = irm_ptr->cmd_ctrl_list; list_ptr; LIST_ADVANCE(list_ptr))
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         irm_cmd_ctrl_t *cur_cmd_ctrl_ptr = (irm_cmd_ctrl_t *)cur_obj_ptr;
         if (cur_cmd_ctrl_ptr->num_resp_pending > 0)
         {
            return TRUE;
         }
      }
   }
   return FALSE;
}

static irm_cmd_ctrl_t *irm_get_cmd_ctrl_obj(irm_t *irm_ptr, uint32_t token)
{
   for (spf_list_node_t *list_ptr = irm_ptr->cmd_ctrl_list; list_ptr; LIST_ADVANCE(list_ptr))
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         irm_cmd_ctrl_t *cur_cmd_ctrl_ptr = (irm_cmd_ctrl_t *)cur_obj_ptr;
         if (token == cur_cmd_ctrl_ptr->token)
         {
            return cur_cmd_ctrl_ptr;
         }
      }
   }
   return NULL;
}

static void irm_insert_get_cfg_cmd_ctrl(irm_t *irm_ptr, irm_cmd_ctrl_t *cmd_ctrl_ptr, uint16_t token, void *dest_ptr)
{

   irm_get_cfg_resp_cmd_ctrl_t *new_get_cfg_rsp_cmd_ctrl =
      (irm_get_cfg_resp_cmd_ctrl_t *)posal_memory_malloc(sizeof(irm_get_cfg_resp_cmd_ctrl_t), irm_ptr->heap_id);

   if (NULL == new_get_cfg_rsp_cmd_ctrl)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to allocate memory for new_get_cfg_rsp_cmd_ctrl");
      return;
   }
   new_get_cfg_rsp_cmd_ctrl->set_cfg_orig_resp_ptr = dest_ptr;
   new_get_cfg_rsp_cmd_ctrl->token                 = token;
   spf_list_insert_tail(&cmd_ctrl_ptr->get_cfg_rsp_cmd_ctrl_list_ptr,
                        new_get_cfg_rsp_cmd_ctrl,
                        irm_ptr->heap_id,
                        TRUE /* use_pool*/);
}

static irm_get_cfg_resp_cmd_ctrl_t *irm_get_get_cfg_cmd_ctrl_rsp(irm_cmd_ctrl_t *cmd_ctrl_ptr, uint16_t token)
{
   for (spf_list_node_t *list_ptr = cmd_ctrl_ptr->get_cfg_rsp_cmd_ctrl_list_ptr; list_ptr; LIST_ADVANCE(list_ptr))
   {
      void *cur_obj_ptr = list_ptr->obj_ptr;
      if (NULL != cur_obj_ptr)
      {
         irm_get_cfg_resp_cmd_ctrl_t *cur_get_cfg_cmd_ctrl_ptr = (irm_get_cfg_resp_cmd_ctrl_t *)cur_obj_ptr;
         if (token == cur_get_cfg_cmd_ctrl_ptr->token)
         {
            return cur_get_cfg_cmd_ctrl_ptr;
         }
      }
   }
   return NULL;
}

static ar_result_t irm_clear_get_cfg_cmd_crtl(irm_cmd_ctrl_t              *cmd_ctrl_ptr,
                                              irm_get_cfg_resp_cmd_ctrl_t *get_cfg_cmd_ctrl_ptr)
{

   if (!cmd_ctrl_ptr || !get_cfg_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "irm_clear_cmd_ctrl(): IRM cmd crtl ptr(0x%lX) and/or IRM get cfg cmd ctrl ptr(0x%lX) is NULL");
      return AR_EFAILED;
   }

   spf_list_find_delete_node(&cmd_ctrl_ptr->get_cfg_rsp_cmd_ctrl_list_ptr,
                             (void *)get_cfg_cmd_ctrl_ptr,
                             TRUE /* use_pool*/);

   posal_memory_free(get_cfg_cmd_ctrl_ptr);

   return AR_EOK;
}

ar_result_t irm_clear_cmd_ctrl(irm_t *irm_ptr, irm_cmd_ctrl_t *irm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   if (!irm_ptr || !irm_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "irm_clear_cmd_ctrl(): IRM info ptr(0x%lX) and/or IRM cmd ctrl ptr(0x%lX) is NULL");
      return AR_EFAILED;
   }
   spf_list_find_delete_node(&irm_ptr->cmd_ctrl_list, (void *)irm_cmd_ctrl_ptr, TRUE /* use_pool*/);

   posal_memory_free(irm_cmd_ctrl_ptr);

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Function to route the command to the destination domain.
 Caller will free the GPR packet in case of any error.
 This function should only manage the memory allocated within.
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_route_cmd_to_satellite(irm_t     *irm_ptr,
                                       spf_msg_t *msg_ptr,
                                       uint32_t   dst_domain_id,
                                       uint8_t   *irm_payload_ptr)
{
   ar_result_t result = AR_EOK;

   /** Get the pointer to GPR command */
   gpr_packet_t     *pkt_ptr             = (gpr_packet_t *)msg_ptr->payload_ptr;
   void             *payload_ptr         = GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
   gpr_packet_t     *new_cmd_gpr_ptr     = NULL;
   void             *new_cmd_payload_ptr = NULL;
   apm_cmd_header_t *new_apm_header_ptr  = NULL;
   bool_t            is_out_of_band      = FALSE;
   uint32_t          irm_payload_size    = 0;
   irm_cmd_ctrl_t   *curr_cmd_ctrl_ptr   = NULL;

   /** Set the command control for this command */
   if (AR_EOK != (result = irm_set_cmd_ctrl(irm_ptr,
                                            msg_ptr,
                                            (void *)irm_payload_ptr,
                                            is_out_of_band,
                                            &curr_cmd_ctrl_ptr,
                                            dst_domain_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Setting cmd ctrl failed. Dst Domain ID = %lu", dst_domain_id);
      return result;
   }

   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = pkt_ptr->dst_domain_id; // Master ID
   args.src_port      = pkt_ptr->dst_port;
   args.dst_domain_id = dst_domain_id; // satellite domain id
   args.dst_port      = pkt_ptr->dst_port;
   args.client_data   = 0;
   args.token         = curr_cmd_ctrl_ptr->token;
   args.opcode        = pkt_ptr->opcode; // Will use this token during response tracking and for bookkeeping
   args.payload_size  = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(pkt_ptr->header);
   args.ret_packet    = &new_cmd_gpr_ptr;

#ifdef IRM_OFFLOAD_DBG
   AR_MSG(DBG_HIGH_PRIO, "IRM: Offload: Routing GPR PKT Payload size %lu", args.payload_size);
#endif

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == new_cmd_gpr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Allocating new cmd pkt failed with %lu", result);
      result |= AR_ENOMEMORY;
      goto __bailout_route_cmd_1;
   }

   /* prepare the cmd payload, for OOB size will be just for header and payload is copied seperateely */
   new_cmd_payload_ptr = GPR_PKT_GET_PAYLOAD(void, new_cmd_gpr_ptr);
   memscpy(new_cmd_payload_ptr, args.payload_size, payload_ptr, args.payload_size);
   new_apm_header_ptr = (apm_cmd_header_t *)new_cmd_payload_ptr;
   is_out_of_band     = (0 != new_apm_header_ptr->mem_map_handle);

   /*If the command is received out of band, let's copy into the loaned mem for the sat */
   if (is_out_of_band)
   {
      irm_payload_size = new_apm_header_ptr->payload_size;
      curr_cmd_ctrl_ptr->loaned_mem_ptr = (void *)apm_offload_memory_malloc(dst_domain_id, irm_payload_size, &curr_cmd_ctrl_ptr->ret_info);

      if (NULL == curr_cmd_ctrl_ptr->loaned_mem_ptr)
      {
         result = AR_ENOMEMORY;
         AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Error while allocating memory for sat domain id %lu", dst_domain_id);
         goto __bailout_route_cmd_2;
      }

      memscpy(curr_cmd_ctrl_ptr->loaned_mem_ptr, irm_payload_size, irm_payload_ptr, irm_payload_size);
      posal_cache_flush_v2((posal_mem_addr_t)curr_cmd_ctrl_ptr->loaned_mem_ptr, irm_payload_size);

#ifdef IRM_OFFLOAD_DBG
      AR_MSG(DBG_HIGH_PRIO,
             "Copied payload of size %lu, into loaned mem for sat ID %lu. Sat mem handle is %lu",
             irm_payload_size,
             dst_domain_id,
			 curr_cmd_ctrl_ptr->ret_info.sat_handle);
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
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: sending command to sat failed with %lu", result);
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
   irm_clear_cmd_ctrl(irm_ptr, curr_cmd_ctrl_ptr);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Function to send the command to the destination domain.
 Caller will free the GPR packet in case of any error.
 This function should only manage the memory allocated within.
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_send_get_cfg_cmd_to_satellite(irm_t          *irm_ptr,
                                              uint32_t        opcode,
                                              uint32_t        param_id,
                                              uint32_t        host_domain_id,
                                              uint32_t        dst_domain_id,
                                              uint32_t        payload_size,
                                              uint8_t        *resp_ptr,
                                              uint8_t        *payload_ptr,
                                              irm_cmd_ctrl_t *curr_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   /** Get the pointer to GPR command */
   gpr_packet_t            *new_cmd_gpr_ptr       = NULL;
   void                    *new_cmd_payload_ptr   = NULL;
   apm_cmd_header_t        *new_apm_header_ptr    = NULL;
   apm_module_param_data_t *new_param_payload_ptr = NULL;

   uint16_t get_cfg_cmd_ctrl_token = curr_cmd_ctrl_ptr->num_resp_pending + 1;
   uint32_t token                  = (uint32_t)get_cfg_cmd_ctrl_token << 16 | curr_cmd_ctrl_ptr->token;

   irm_insert_get_cfg_cmd_ctrl(irm_ptr, curr_cmd_ctrl_ptr, get_cfg_cmd_ctrl_token, resp_ptr);

   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = host_domain_id; // Master ID
   args.src_port      = IRM_MODULE_INSTANCE_ID;
   args.dst_domain_id = dst_domain_id; // satellite domain id
   args.dst_port      = IRM_MODULE_INSTANCE_ID;
   args.client_data   = 0;
   args.token         = token; // Will use this token during response tracking and for bookkeeping
   args.opcode        = opcode;
   args.payload_size  = sizeof(apm_cmd_header_t) + sizeof(apm_module_param_data_t) + payload_size;
   args.ret_packet    = &new_cmd_gpr_ptr;

#ifdef IRM_OFFLOAD_DBG
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: Offload: Routing get cfg payload size %lu to sat pd: 0x%x",
          args.payload_size,
          dst_domain_id);
#endif

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == new_cmd_gpr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Allocating new cmd pkt failed with %lu", result);
      result |= AR_ENOMEMORY;
      goto __bailout_set_cfg_cmd_1;
   }

   /* prepare the cmd payload, for OOB size will be just for header and payload is copied seperateely */
   new_cmd_payload_ptr = GPR_PKT_GET_PAYLOAD(void, new_cmd_gpr_ptr);

   // memscpy(new_cmd_payload_ptr, args.payload_size, payload_ptr, args.payload_size);
   new_apm_header_ptr = (apm_cmd_header_t *)new_cmd_payload_ptr;

   memset(new_apm_header_ptr, 0, sizeof(apm_cmd_header_t));
   new_apm_header_ptr->payload_size = sizeof(apm_module_param_data_t) + payload_size;
   new_param_payload_ptr            = (apm_module_param_data_t *)(new_apm_header_ptr + 1);

   new_param_payload_ptr->module_instance_id = IRM_MODULE_INSTANCE_ID;
   new_param_payload_ptr->param_id           = param_id;
   new_param_payload_ptr->param_size         = payload_size;

   uint8_t *new_payload = (uint8_t *)(new_param_payload_ptr + 1);
   memscpy(new_payload, payload_size, payload_ptr, payload_size);

   if (AR_EOK != (result = __gpr_cmd_async_send(new_cmd_gpr_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: sending command to sat failed with %lu", result);
      goto __bailout_set_cfg_cmd_2;
   }
   ++curr_cmd_ctrl_ptr->num_resp_pending;
   return result;

__bailout_set_cfg_cmd_2:
   __gpr_cmd_free(new_cmd_gpr_ptr);
   if (NULL != curr_cmd_ctrl_ptr->loaned_mem_ptr)
   {
      apm_offload_memory_free(&curr_cmd_ctrl_ptr->ret_info);
   }
__bailout_set_cfg_cmd_1:
   // Note: caller responds to the client
   irm_clear_cmd_ctrl(irm_ptr, curr_cmd_ctrl_ptr);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Function to route the basic rsp from the sat to client
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_route_basic_rsp_to_client(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t            *basic_rsp_pkt_ptr     = (gpr_packet_t *)msg_ptr->payload_ptr;
   gpr_packet_t            *cmd_gpr_payload_ptr   = NULL;
   gpr_ibasic_rsp_result_t *basic_rsp_payload_ptr = NULL;
   irm_cmd_ctrl_t          *ctrl_obj_ptr          = NULL;
   apm_cmd_header_t        *cmd_apm_header_ptr    = NULL;
   uint32_t                 local_token           = basic_rsp_pkt_ptr->token;

   // just to print the ocpode
   basic_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, basic_rsp_pkt_ptr);
   result                = basic_rsp_payload_ptr->status;
   AR_MSG(DBG_HIGH_PRIO,
          "IRM Offload: Received Basic Response on Master for cmd opcode 0x%lx with status %d",
          basic_rsp_payload_ptr->opcode,
          result);

   ctrl_obj_ptr = irm_get_cmd_ctrl_obj(irm_ptr, local_token);

   // If the cached command was cleared for some reason, ignore
   if (NULL == ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Offload: Basic Rsp Handler: WARNING: ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   spf_msg_t *original_msg_ptr = &ctrl_obj_ptr->cmd_msg;
   if (NULL == original_msg_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Offload: Original msg not present.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   if (SPF_MSG_CMD_GPR == original_msg_ptr->msg_opcode)
   {
      cmd_gpr_payload_ptr = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
      cmd_apm_header_ptr  = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, cmd_gpr_payload_ptr);

      if (ctrl_obj_ptr->is_out_of_band)
      {
         posal_cache_invalidate_v2((posal_mem_addr_t)ctrl_obj_ptr->loaned_mem_ptr, cmd_apm_header_ptr->payload_size);
         memscpy(ctrl_obj_ptr->master_payload_ptr,
                 cmd_apm_header_ptr->payload_size,
                 ctrl_obj_ptr->loaned_mem_ptr,
                 cmd_apm_header_ptr->payload_size);
         posal_cache_flush_v2((posal_mem_addr_t)ctrl_obj_ptr->master_payload_ptr, cmd_apm_header_ptr->payload_size);
      }

      __gpr_cmd_free(basic_rsp_pkt_ptr);
      __gpr_cmd_end_command(cmd_gpr_payload_ptr, result); // send response to master's client
      if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
      {
         apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
      }
   }
   else
   {
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      result = spf_msg_ack_msg(original_msg_ptr, result);
   }
   irm_clear_cmd_ctrl(irm_ptr, ctrl_obj_ptr);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Function to route the get_cfg rsp from the sat to client
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_route_get_cfg_rsp_to_client(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;
   /** Get the pointer to GPR command */
   gpr_packet_t                *basic_rsp_pkt_ptr     = (gpr_packet_t *)msg_ptr->payload_ptr;
   apm_cmd_rsp_get_cfg_t       *basic_rsp_payload_ptr = NULL;
   irm_cmd_ctrl_t              *ctrl_obj_ptr          = NULL;
   irm_get_cfg_resp_cmd_ctrl_t *get_cfg_ctrl_obj_ptr  = NULL;
   uint16_t                     cmd_ctrl_token        = basic_rsp_pkt_ptr->token & 0x0000FFFF;
   uint16_t                     get_cfg_token         = basic_rsp_pkt_ptr->token >> 16;

   // just to print the ocpode
   basic_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, basic_rsp_pkt_ptr);
   result                = basic_rsp_payload_ptr->status;
   AR_MSG(DBG_HIGH_PRIO, "IRM Offload: Received Get Cfg Response on Master with status %d", result);

   ctrl_obj_ptr = irm_get_cmd_ctrl_obj(irm_ptr, cmd_ctrl_token);

   // If the cached command was cleared for some reason, ignore
   if (NULL == ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Offload: Get Cfg Rsp Handler: WARNING: ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   get_cfg_ctrl_obj_ptr = irm_get_get_cfg_cmd_ctrl_rsp(ctrl_obj_ptr, get_cfg_token);

   if (NULL == get_cfg_ctrl_obj_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "IRM: Offload: Get Cfg Rsp Handler: WARNING: get_cfg_ctrl_obj_ptr is not present. Ignoring.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   spf_msg_t *original_msg_ptr = &ctrl_obj_ptr->cmd_msg;
   if (NULL == original_msg_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Offload: Original msg not present.");
      __gpr_cmd_free(basic_rsp_pkt_ptr);
      return result;
   }

   // Get the module param data pointer
   apm_module_param_data_t *module_param_data_ptr = (apm_module_param_data_t *)(basic_rsp_payload_ptr + 1);
   uint8_t                 *param_data_ptr        = (uint8_t *)(module_param_data_ptr + 1);

   // need to copy based on per param basis
   switch (module_param_data_ptr->param_id)
   {
      case PARAM_ID_IRM_SYSTEM_CAPABILITIES:
      {
         AR_MSG(DBG_HIGH_PRIO,
                "IRM Offload: Copy system capabilites payload to original packet, payload of size %d to 0x%x",
                module_param_data_ptr->param_size,
                get_cfg_ctrl_obj_ptr->set_cfg_orig_resp_ptr);
         // Assume if we got to this point we got the payload we are expecting
         param_data_ptr += sizeof(param_id_irm_system_capabilities_t);
         memscpy(get_cfg_ctrl_obj_ptr->set_cfg_orig_resp_ptr,
                 sizeof(irm_system_capabilities_t),
                 param_data_ptr,
                 module_param_data_ptr->param_size);
         ctrl_obj_ptr->bytes_written += module_param_data_ptr->param_size;
         break;
      }
      case PARAM_ID_IRM_METRIC_CAPABILITIES:
      {
         AR_MSG(DBG_HIGH_PRIO,
                "IRM Offload: Copy metric capabilites payload to original packet, payload of size %d to 0x%x",
                module_param_data_ptr->param_size,
                get_cfg_ctrl_obj_ptr->set_cfg_orig_resp_ptr);
         param_data_ptr += sizeof(param_id_irm_metric_capabilities_t);
         uint32_t param_size =
            module_param_data_ptr->param_size -
            sizeof(param_id_irm_metric_capabilities_t); // we skip the param id struct because it already is in the dest
                                                        // but earlier in the payload
         uint8_t *tmp_dst = (uint8_t *)get_cfg_ctrl_obj_ptr->set_cfg_orig_resp_ptr + ctrl_obj_ptr->bytes_written;
         memscpy(tmp_dst, param_size, param_data_ptr, param_size);
         ctrl_obj_ptr->bytes_written += param_size;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "IRM Offload: Error: response recieved for unknown param id: 0x%x",
                module_param_data_ptr->param_id);
         __gpr_cmd_free(basic_rsp_pkt_ptr);
         return AR_ENOTIMPL;
      }
   }

   // update fsm
   irm_clear_get_cfg_cmd_crtl(ctrl_obj_ptr, get_cfg_ctrl_obj_ptr);

   ctrl_obj_ptr->num_resp_pending--;

   if (ctrl_obj_ptr->num_resp_pending > 0)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "IRM Offload: Don't send get cfg rsp to client yet, num rsp pending: %d",
             ctrl_obj_ptr->num_resp_pending);

      // We still have to wait on some get cfg responses, so we don't send the response yet
      return result;
   }

   AR_MSG(DBG_HIGH_PRIO, "IRM Offload: Return get cfg resp to client");
   if (SPF_MSG_CMD_GET_CFG == original_msg_ptr->msg_opcode)
   {
      // Respond to APM
      result = spf_msg_ack_msg(original_msg_ptr, result);
   }
   else if (SPF_MSG_CMD_GPR == original_msg_ptr->msg_opcode)
   {
      gpr_packet_t     *original_gpr_pkt_ptr = (gpr_packet_t *)original_msg_ptr->payload_ptr;
      apm_cmd_header_t *cmd_header_ptr       = NULL;
      uint8_t          *cmd_payload_ptr      = NULL;
      gpr_packet_t     *gpr_rsp_pkt_ptr      = NULL;
      uint32_t          alignment_size       = 0;

      // Get command header pointer
      cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, original_gpr_pkt_ptr);

      result = spf_svc_get_cmd_payload_addr(IRM_MODULE_INSTANCE_ID,
                                            original_gpr_pkt_ptr,
                                            &gpr_rsp_pkt_ptr,
                                            (uint8_t **)&cmd_payload_ptr,
                                            &alignment_size,
                                            NULL,
                                            apm_get_mem_map_client());
	  if (cmd_payload_ptr)
	  {		  
         result = irm_send_get_cfg_gpr_resp(gpr_rsp_pkt_ptr,
                                         original_gpr_pkt_ptr,
                                         cmd_header_ptr->payload_size,
                                         cmd_payload_ptr,
                                         ctrl_obj_ptr->is_out_of_band);
	  }
	  else
	  {
		  AR_MSG(DBG_ERROR_PRIO, "IRM: cmd payload NULL");
	  }

      gpr_packet_t *cmd_gpr_payload_ptr = (gpr_packet_t *)ctrl_obj_ptr->cmd_msg.payload_ptr;
      if (SPF_MSG_CMD_GPR == original_msg_ptr->msg_opcode)
      {
         __gpr_cmd_free(basic_rsp_pkt_ptr);
         __gpr_cmd_free(cmd_gpr_payload_ptr); // send response to master's client
         if (NULL != ctrl_obj_ptr->loaned_mem_ptr)
         {
            apm_offload_memory_free(&ctrl_obj_ptr->ret_info);
         }
      }
      else
      {
         __gpr_cmd_free(basic_rsp_pkt_ptr);
         __gpr_cmd_free(cmd_gpr_payload_ptr);
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Unknown msg opcode 0x%x", original_msg_ptr->msg_opcode);
   }

   irm_clear_cmd_ctrl(irm_ptr, ctrl_obj_ptr);

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Function to route the APM command to satellite
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_route_apm_cmd_to_satellite(irm_t *irm_ptr, spf_msg_t *msg_ptr, uint32_t dst_domain_id)
{
   ar_result_t                   result             = AR_EOK;
   spf_msg_header_t             *msg_header_ptr     = (spf_msg_header_t *)msg_ptr->payload_ptr;
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   apm_module_param_data_t     **param_data_pptr    = (apm_module_param_data_t **)param_data_cfg_ptr->param_data_pptr;
   bool_t                        is_out_of_band     = FALSE;
   irm_cmd_ctrl_t               *curr_cmd_ctrl_ptr  = NULL;
   gpr_packet_t                 *new_cmd_gpr_ptr    = NULL;
   apm_cmd_header_t             *cmd_header_ptr     = NULL;
   uint8_t                      *cmd_payload_ptr    = NULL;
   uint32_t                      host_domain_id     = 0xFFFFFFFF;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   uint32_t size = sizeof(apm_cmd_header_t);
   for (uint32_t i = 0; i < param_data_cfg_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)param_data_pptr[i];
      size += sizeof(apm_module_param_data_t) + param_data_ptr->param_size;
   }

   if (AR_EOK != (result = irm_set_cmd_ctrl(irm_ptr, msg_ptr, NULL, is_out_of_band, &curr_cmd_ctrl_ptr, dst_domain_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Setting cmd ctrl failed. Dst Domain ID = %lu", dst_domain_id);
      return result;
   }

   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = host_domain_id; // Master ID
   args.src_port      = IRM_MODULE_INSTANCE_ID;
   args.dst_domain_id = dst_domain_id; // satellite domain id
   args.dst_port      = IRM_MODULE_INSTANCE_ID;
   args.client_data   = 0;
   args.token         = (uint32_t)curr_cmd_ctrl_ptr->token;
   args.opcode        = APM_CMD_SET_CFG;
   args.payload_size  = size;
   args.ret_packet    = &new_cmd_gpr_ptr;

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == new_cmd_gpr_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: Allocating new cmd pkt failed with %lu", result);
      result |= AR_ENOMEMORY;
      goto __bailout_route_apm_cmd_1;
   }

   size -= sizeof(apm_cmd_header_t);
   // Get command header pointer
   cmd_header_ptr                      = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, new_cmd_gpr_ptr);
   cmd_header_ptr->mem_map_handle      = 0;
   cmd_header_ptr->payload_address_lsw = 0;
   cmd_header_ptr->payload_address_msw = 0;
   cmd_header_ptr->payload_size        = ALIGN_8_BYTES(size);

   // Get the module param data pointer
   cmd_payload_ptr = (uint8_t *)(cmd_header_ptr + 1);

   for (uint32_t i = 0; i < param_data_cfg_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)param_data_pptr[i];
      uint32_t                 local_size     = sizeof(apm_module_param_data_t) + param_data_ptr->param_size;
      cmd_payload_ptr += memscpy((void *)cmd_payload_ptr, local_size, (void *)param_data_ptr, local_size);
   }

   if (AR_EOK != (result = __gpr_cmd_async_send(new_cmd_gpr_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "IRM: Offload: sending command to sat failed with %lu", result);
      goto __bailout_route_apm_cmd_2;
   }

   return result;

__bailout_route_apm_cmd_2:
   __gpr_cmd_free(new_cmd_gpr_ptr);

__bailout_route_apm_cmd_1:
   // Note: caller responds to the client
   irm_clear_cmd_ctrl(irm_ptr, curr_cmd_ctrl_ptr);
   result = spf_msg_ack_msg(msg_ptr, result);
   return result;
}
