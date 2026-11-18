/**
 * \file gen_cntr_ipc_fwk_ext.c
 * \brief
 *
 *
 * \copyright
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_cntr_i.h"
#include "gen_cntr_utils.h"
#include "apm.h"
#include "cu_ipc_fwk_ext.h"

ar_result_t gen_cntr_get_and_update_fwk_extn_ipc_port_msg_cb_info(cu_base_t   *base_ptr,
                                                                  gu_module_t *gu_module_ptr,
                                                                  bool_t       is_ext_input,
                                                                  uint32_t     port_index)
{
   gen_cntr_module_t *module_ptr = (gen_cntr_module_t *)gu_module_ptr;

   fwk_extn_prop_ipc_msg_callback_info_t cb_info = { 0 };
   ar_result_t                           result  = gen_topo_get_ipc_port_callback_info((gen_topo_t *)base_ptr->topo_ptr,
                                                            &module_ptr->topo,
                                                            &cb_info,
                                                            is_ext_input,
                                                            port_index);
   if (AR_FAILED(result))
   {
      GEN_CNTR_MSG(base_ptr->gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "Failed to get callback info from IPC Module 0x%lX ",
                   gu_module_ptr->module_instance_id);
      return result;
   }

   if (NULL == module_ptr->cu.ipc_port_cb_info_ptr)
   {
      GEN_CNTR_MSG(base_ptr->gu_ptr->log_id,
                   DBG_HIGH_PRIO,
                   "callback info memory not initialized for module 0x%lX ",
                   gu_module_ptr->module_instance_id);
      return result;
   }

   // cache the callback info in the async signal's object
   module_ptr->cu.ipc_port_cb_info_ptr->ext_port_trigger_shared_ptr = cb_info.ext_port_trigger_shared_ptr;
   module_ptr->cu.ipc_port_cb_info_ptr->ctrl_msg_handler            = cb_info.ctrl_msg_handler;
   module_ptr->cu.ipc_port_cb_info_ptr->data_msg_handler            = cb_info.data_msg_handler;

   module_ptr->cu.ipc_port_cb_info_ptr->callback_handle_ptr = cb_info.callback_handle_ptr;

   // share the trigger info to the topo.
   module_ptr->topo.ext_port_trigger_shared_ptr = cb_info.ext_port_trigger_shared_ptr;

   GEN_CNTR_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "Successfully queried the callback info from Module 0x%lX ",
                gu_module_ptr->module_instance_id);

   return AR_EOK;
}

/*** Error check function - end */
ar_result_t gen_cntr_handle_ipc_data_link_info_event(gen_topo_t        *topo_ptr,
                                                     gen_topo_module_t *mod_ptr,
                                                     capi_event_info_t *event_info_ptr)
{
   capi_buf_t                       *payload       = &event_info_ptr->payload;
   capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);
   gen_cntr_module_t                *module_ptr    = (gen_cntr_module_t *)mod_ptr;

   fwk_extn_event_ipc_data_link_info_t *cfg_ptr =
      (fwk_extn_event_ipc_data_link_info_t *)dsp_event_ptr->payload.data_ptr;

   if (NULL == module_ptr->cu.ipc_port_cb_info_ptr)
   {
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_ERROR_PRIO,
                   "Faled handling IPC data link info event from Module 0x%lX ",
                   mod_ptr->gu.module_instance_id);
      return AR_EFAILED;
   }

   module_ptr->cu.ipc_port_cb_info_ptr->peer_port_id        = cfg_ptr->peer_port_id;
   module_ptr->cu.ipc_port_cb_info_ptr->peer_module_iid     = cfg_ptr->peer_module_iid;
   module_ptr->cu.ipc_port_cb_info_ptr->peer_proc_domain_id = cfg_ptr->peer_proc_domain_id;

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_LOW_PRIO,
                "Module 0x%lX: received IPC port peer info: (port id %lu, peer module id 0x%lx peer proc domain id "
                "%lu) ",
                module_ptr->topo.gu.module_instance_id,
                cfg_ptr->peer_port_id,
                cfg_ptr->peer_module_iid,
                cfg_ptr->peer_proc_domain_id);

   return AR_EOK;
}

ar_result_t gen_cntr_handle_ipc_port_gpr_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   gpr_packet_t *packet_ptr         = (gpr_packet_t *)base_ptr->cmd_msg.payload_ptr;
   uint32_t      module_instance_id = packet_ptr->dst_port;

   gu_module_t *gu_module_ptr = gu_find_module(base_ptr->gu_ptr, module_instance_id);
   if (NULL == gu_module_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed handling IPC cmd msg 0x%lx Module 0x%lx not found",
             packet_ptr->opcode,
             module_instance_id);
      __gpr_cmd_end_command(packet_ptr, AR_EFAILED);
      return result;
   }

   cu_module_t *module_ptr = (cu_module_t *)((uint8_t *)gu_module_ptr + base_ptr->module_cu_offset);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:IPC GPR cmd: Executing GPR command, opcode(%lX) token(%lx), handle_rest %u",
                packet_ptr->opcode,
                packet_ptr->token,
                cu_is_any_handle_rest_pending(base_ptr));

   switch (packet_ptr->opcode)
   {
      case SPF_IPC_FWK_EXTN_GPR_CMD_PEER_PORT_PROPERTY_UPDATE:
      {
         SPF_MANAGE_CRITICAL_SECTION
         SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
         result = cu_handle_ipc_peer_port_property_update_gpr_cmd(base_ptr);
         SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

         break;
      }
      case SPF_IPC_FWK_EXTN_GPR_CMD_UPSTREAM_IPC_TX_STOPPED_ACK:
      {
         result = cu_handle_upstream_ipc_tx_stopped_ack(base_ptr);
         break;
      }
      case SPF_IPC_FWK_EXTN_GPR_CMD_INFORM_ICB_INFO:
      {
         spf_msg_cmd_inform_icb_info_t *ds_icb_info_ptr =
            (spf_msg_cmd_inform_icb_info_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
         CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "ds_icb_info_ptr 0x%lx", ds_icb_info_ptr);
         if (gu_module_ptr->ipc_output_port_list_ptr)
         {
            gu_ext_out_port_t *ipc_ext_out_port_ptr =
               gu_module_ptr->ipc_output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
            if (ipc_ext_out_port_ptr)
            {
               result = cu_ext_out_handle_icb_info_from_downstream(base_ptr,
                                                                   ds_icb_info_ptr,
                                                                   ipc_ext_out_port_ptr /*ext port*/);
            }
         }
         else
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "Couldn't find an ipc out port for module 0x%lx",
                   module_instance_id);
         }
         break;
      }
      default:
      {
         result = cu_ipc_port_ctrl_msg_trigger(base_ptr);
         break;
      }
   }

   // skip sending reponse from the event since GPR basic rsp would have been already returned at this point.
   gen_cntr_handle_events_after_cmds(me_ptr, FALSE /* skips sending rsp */, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GPR cmd: Done executing GPR command, result=0x%lx, handle_rest %u",
                result,
                cu_is_any_handle_rest_pending(base_ptr));

   return result;
}

bool_t gen_cntr_check_if_ipc_ext_out_needs_buffer(gen_cntr_t *me_ptr)
{
   for (gu_ext_out_port_list_t *ipc_ext_out_port_list_ptr = me_ptr->topo.gu.ipc_ext_out_port_list_ptr;
        (NULL != ipc_ext_out_port_list_ptr);
        LIST_ADVANCE(ipc_ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ipc_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ipc_ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t *ipc_out_port_ptr = (gen_topo_output_port_t *)ipc_ext_out_port_ptr->gu.int_out_port_ptr;
      gen_topo_module_t      *module_ptr       = (gen_topo_module_t *)ipc_out_port_ptr->gu.cmn.module_ptr;

      // query if module has the trigger.
      fwk_extn_ipc_port_trigger_t ipc_out_trigger =
         gen_topo_get_ipc_port_trigger_from_module(&me_ptr->topo,
                                                   module_ptr,
                                                   FALSE,
                                                   ipc_out_port_ptr->gu.cmn.id,
                                                   &ipc_out_port_ptr->common);

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "wait_for_trig: Ext out (0x%lx, 0x%lx) port ipc_ext_out_trigger:%lu ",
                   module_ptr->gu.module_instance_id,
                   ipc_out_port_ptr->gu.cmn.id,
                   ipc_out_trigger);
#endif

      if (ipc_out_trigger != FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED)
      {
         return TRUE;
      }
   }

   return FALSE;
}

/** Check triggers for the IPC external ports */
void gen_cntr_ipc_ports_update_wait_mask(gen_cntr_t *me_ptr,
                                         uint32_t   *in_wait_mask,
                                         uint32_t   *out_wait_mask,
                                         uint32_t   *stop_mask,
                                         uint32_t   *optional_wait_mask)
{
   for (gu_ext_in_port_list_t *ipc_ext_in_port_list_ptr = me_ptr->topo.gu.ipc_ext_in_port_list_ptr;
        (NULL != ipc_ext_in_port_list_ptr);
        LIST_ADVANCE(ipc_ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ipc_ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ipc_ext_in_port_list_ptr->ext_in_port_ptr;

      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ipc_ext_in_port_ptr->gu.int_in_port_ptr;

      gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

      // query if module has the trigger.
      fwk_extn_ipc_port_trigger_t ipc_in_trigger = gen_topo_get_ipc_port_trigger_from_module(&me_ptr->topo,
                                                                                             module_ptr,
                                                                                             TRUE,
                                                                                             in_port_ptr->gu.cmn.id,
                                                                                             &in_port_ptr->common);

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "wait_for_trig: Ext in (0x%lx, 0x%lx) port ipc_in_trigger:%lu",
                   module_ptr->gu.module_instance_id,
                   in_port_ptr->gu.cmn.id,
                   ipc_in_trigger);
#endif

      switch (ipc_in_trigger)
      {
         case FWK_EXTN_IPC_PORT_BUFFER_NEEDED:
         {
            *in_wait_mask |= ipc_ext_in_port_ptr->cu.bit_mask;

            // add the external input port's wait mask to the wait-mask-array.
            me_ptr->wait_mask_arr[module_ptr->gu.path_index] |= ipc_ext_in_port_ptr->cu.bit_mask;
            break;
         }
         case FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED:
         {
            *stop_mask |= ipc_ext_in_port_ptr->cu.bit_mask;
            break;
         }
         case FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY:
         case FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY:
         {
            *optional_wait_mask |= ipc_ext_in_port_ptr->cu.bit_mask;
            break;
         }
         default:
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "wait_for_trig: Ext in (0x%lx, 0x%lx) port ipc_ext_in_trigger:%lu is invalid",
                         module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         ipc_in_trigger);
            break;
         }
      }
   }

   for (gu_ext_out_port_list_t *ipc_ext_out_port_list_ptr = me_ptr->topo.gu.ipc_ext_out_port_list_ptr;
        (NULL != ipc_ext_out_port_list_ptr);
        LIST_ADVANCE(ipc_ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ipc_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ipc_ext_out_port_list_ptr->ext_out_port_ptr;

      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ipc_ext_out_port_ptr->gu.int_out_port_ptr;

      gen_topo_module_t *module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;

      // query if module has the trigger.
      fwk_extn_ipc_port_trigger_t ipc_out_trigger = gen_topo_get_ipc_port_trigger_from_module(&me_ptr->topo,
                                                                                              module_ptr,
                                                                                              FALSE,
                                                                                              out_port_ptr->gu.cmn.id,
                                                                                              &out_port_ptr->common);

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "wait_for_trig: Ext out (0x%lx, 0x%lx) port ipc_ext_out_trigger:%lu ",
                   module_ptr->gu.module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   ipc_out_trigger);
#endif

      switch (ipc_out_trigger)
      {
         case FWK_EXTN_IPC_PORT_BUFFER_NEEDED:
         {
            *out_wait_mask |= ipc_ext_out_port_ptr->cu.bit_mask;

            // add the external output port's wait mask to the wait-mask-array.
            me_ptr->wait_mask_arr[module_ptr->gu.path_index] |= ipc_ext_out_port_ptr->cu.bit_mask;
            break;
         }
         case FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED:
         {
            *stop_mask |= ipc_ext_out_port_ptr->cu.bit_mask;
            break;
         }
         case FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY:
         case FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED_OPTIONALLY:
         {
            *optional_wait_mask |= ipc_ext_out_port_ptr->cu.bit_mask;
            break;
         }
         default:
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "wait_for_trig: Ext out (0x%lx, 0x%lx) port ipc_ext_out_trigger:%lu is invalid ",
                         module_ptr->gu.module_instance_id,
                         out_port_ptr->gu.cmn.id,
                         ipc_out_trigger);
            break;
         }
      }
   }
}
