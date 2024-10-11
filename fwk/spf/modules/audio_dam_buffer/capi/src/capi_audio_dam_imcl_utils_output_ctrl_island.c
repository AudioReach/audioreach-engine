/**
 *   \file capi_audio_dam_buffer_imc_utils_island.c
 *   \brief
 *        This file contains CAPI IMC utils implementation of Audio Dam buffer module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_audio_dam_buffer_i.h"

/*==============================================================================
   Local Defines
==============================================================================*/

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

capi_err_t capi_audio_dam_imcl_handle_gate_open(capi_audio_dam_t *                   me_ptr,
                                                uint32_t                             op_arr_index,
                                                param_id_audio_dam_data_flow_ctrl_t *cfg_ptr)
{
   capi_err_t result = CAPI_EOK;

   // return if gate is not ready to open
   if (me_ptr->out_port_info_arr[op_arr_index].is_gate_opened || !is_dam_output_port_initialized(me_ptr, op_arr_index))
   {
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_HIGH_PRIO,
                     "capi_audio_dam: Warning! Not opening the gate for port_id0x%lx, is_already_open%lu",
                     me_ptr->out_port_info_arr[op_arr_index].port_id,
                     me_ptr->out_port_info_arr[op_arr_index].is_gate_opened);
      return CAPI_EOK;
   }

   // Open the gate
   me_ptr->out_port_info_arr[op_arr_index].is_gate_opened = TRUE;

   // adjust read ptr and get actual unread data length
   uint32_t actual_unread_len_in_us = 0;
   result = audio_dam_stream_read_adjust(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                         cfg_ptr->read_offset_in_us,
                                         &actual_unread_len_in_us,
                                         me_ptr->out_port_info_arr[op_arr_index].is_peer_aad);

   // Send unread data length to detection engine module
   if (FALSE == me_ptr->out_port_info_arr[op_arr_index].is_peer_aad)
   {
      result = capi_audio_dam_imcl_send_unread_len(me_ptr,
                                                   actual_unread_len_in_us,
                                                   me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id);
   }

   me_ptr->out_port_info_arr[op_arr_index].ftrt_unread_data_len_in_us = actual_unread_len_in_us;

   // When gate is opened,
   // For data TP, make output triggerable port [affinity present].
   // For signal TP, make the port optional non-triggerable port.
   if (me_ptr->out_port_info_arr[op_arr_index].is_started)
   {
      /** since gate is open, set triggerable policy present */
      me_ptr->signal_tp.trigger_groups_ptr[0]
         .out_port_grp_affinity_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

      me_ptr->data_tp.trigger_groups_ptr[0]
         .out_port_grp_affinity_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;

      /** make port non-triggerable policy Invalid */
      me_ptr->data_tp.non_trigger_group_ptr
         ->out_port_grp_policy_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_NON_TRIGGER_INVALID;

      me_ptr->signal_tp.non_trigger_group_ptr
         ->out_port_grp_policy_ptr[me_ptr->out_port_info_arr[op_arr_index].port_index] =
         FWK_EXTN_PORT_NON_TRIGGER_INVALID;

      capi_audio_dam_change_trigger_policy(me_ptr);
   }

   // Reorder based on best channels. Acoustic Activity Detection usecase doesnt support best channels.
   if (FALSE == me_ptr->out_port_info_arr[op_arr_index].is_peer_aad)
   {
      capi_audio_dam_reorder_chs_at_gate_open(me_ptr, op_arr_index, cfg_ptr);

      // In AAD usecase, do not update KPPS in process context.
      // And its not required to bump up KPPS since there is no IPC exchange in AAD opens the gate.
      capi_audio_dam_buffer_update_kpps_vote(me_ptr);
   }

   return result;
}

capi_err_t capi_audio_dam_imcl_handle_gate_close(capi_audio_dam_t *me_ptr, uint32_t op_arr_index)
{
   capi_err_t result = CAPI_EOK;
   // return if gate is not ready to open
   if (!me_ptr->out_port_info_arr[op_arr_index].is_gate_opened)
   {
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_HIGH_PRIO,
                     "DAM: Port  %lu gate already closed ",
                     me_ptr->out_port_info_arr[op_arr_index].port_id);
      return result;
   }

   // If output is started we need to push EOS and close the gate in process context.
   if (me_ptr->out_port_info_arr[op_arr_index].is_started)
   {
      me_ptr->out_port_info_arr[op_arr_index].is_pending_gate_close = TRUE;
   }
   else // o/p not started
   {
      // no need to wait until process to push EOS and close the gate.
      // Output is not started so close it immediately.
      capi_check_and_close_the_gate(me_ptr, op_arr_index, FALSE);
      me_ptr->out_port_info_arr[op_arr_index].is_pending_gate_close = FALSE;
   }

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_HIGH_PRIO,
                  "DAM: Port  %lu got gate close - marking as pending:%lu out port started%lu ",
                  me_ptr->out_port_info_arr[op_arr_index].port_id,
                  me_ptr->out_port_info_arr[op_arr_index].is_pending_gate_close,
                  me_ptr->out_port_info_arr[op_arr_index].is_started);

   return result;
}

static capi_err_t capi_audio_dam_imcl_set_hdlr_flow_ctrl(capi_audio_dam_t *me_ptr,
                                                         uint32_t          op_arr_index,
                                                         vw_imcl_header_t *header_ptr,
                                                         uint32_t *        unread_bytes_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_data_flow_ctrl_t))
   {
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_ERROR_PRIO,
                     "capi_audio_dam: Param id 0x%lx Bad param size %lu",
                     (uint32_t)header_ptr->opcode,
                     header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_data_flow_ctrl_t *cfg_ptr = (param_id_audio_dam_data_flow_ctrl_t *)(header_ptr + 1);

   // set it to zero by default, will be updated if gate is being opened.
   me_ptr->out_port_info_arr[op_arr_index].ftrt_unread_data_len_in_us = 0;

   // open the gate only if its un-opened
   if (cfg_ptr->is_gate_open)
   {
      result = capi_audio_dam_imcl_handle_gate_open(me_ptr, op_arr_index, cfg_ptr);
   }
   else // handle gate close
   {
      result = capi_audio_dam_imcl_handle_gate_close(me_ptr, op_arr_index);
   }

   // return unread bytes
   audio_dam_get_stream_reader_unread_bytes(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr, unread_bytes_ptr);

   return result;
}

static capi_err_t capi_audio_dam_imc_set_param_handler_per_output(capi_audio_dam_t *me_ptr,
                                                                  uint32_t          ctrl_port_id,
                                                                  capi_buf_t *      intent_buf_ptr,
                                                                  uint32_t          op_arr_index,
                                                                  uint32_t *        unread_bytes_ptr)
{
   capi_err_t result = CAPI_EOK;
   // accessing the wrong payload.. need to do + sizeof(incoming payload struct to access the actual data)
   int8_t * payload_ptr  = intent_buf_ptr->data_ptr + sizeof(intf_extn_param_id_imcl_incoming_data_t);
   uint32_t payload_size = intent_buf_ptr->actual_data_len - sizeof(intf_extn_param_id_imcl_incoming_data_t);

   uint32_t ctrl_arr_index = capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(me_ptr, ctrl_port_id);
   if (IS_INVALID_PORT_INDEX(ctrl_arr_index))
   {
      return CAPI_EBADPARAM;
   }

   if (FALSE == me_ptr->out_port_info_arr[op_arr_index].is_open)
   {
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_HIGH_PRIO,
                     "Warning: output port 0x%lx is not opened yet",
                     me_ptr->out_port_info_arr[op_arr_index].port_id);
   }

   while (payload_size >= sizeof(vw_imcl_header_t))
   {
      vw_imcl_header_t *header_ptr = (vw_imcl_header_t *)payload_ptr;
      switch (header_ptr->opcode)
      {
         /*Parameters handled in ISLAND */
         case PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL:
         {
            result |= capi_audio_dam_imcl_set_hdlr_flow_ctrl(me_ptr, op_arr_index, header_ptr, unread_bytes_ptr);
            break;
         }
         case PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL_V2:
         {
            result |= capi_audio_dam_imcl_set_hdlr_flow_ctrl_v2(me_ptr, op_arr_index, header_ptr, unread_bytes_ptr);
            break;
         }
         /*Parameters handled in NON-ISLAND */
         case PARAM_ID_AUDIO_DAM_IMCL_PEER_INFO:
         {
            result |= capi_audio_dam_imcl_set_hdlr_peer_info(me_ptr, op_arr_index, header_ptr);
            break;
         }
         case PARAM_ID_AUDIO_DAM_RESIZE:
         {
            result |= capi_audio_dam_imcl_set_hdlr_resize(me_ptr, op_arr_index, header_ptr);
            break;
         }
         case PARAM_ID_AUDIO_DAM_OUTPUT_CH_CFG:
         {
            result |= capi_audio_dam_imcl_set_hdlr_ouput_ch_cfg(me_ptr, op_arr_index, header_ptr);
            break;
         }
         case PARAM_ID_AUDIO_DAM_VIRTUAL_WRITER_INFO:
         {
            result |= capi_audio_dam_imcl_set_virt_writer_cfg(me_ptr, ctrl_arr_index, op_arr_index, header_ptr);
            break;
         }
         case PARAM_ID_AUDIO_DAM_ALLOW_DCM_ISLAND_ENTRY:
         {
            result |= capi_audio_dam_imcl_trigger_island_entry(me_ptr, op_arr_index, header_ptr);
            break;
         }
         default:
         {
            DAM_MSG_ISLAND(me_ptr->miid,
                           DBG_ERROR_PRIO,
                           "CAPI V2 DAM: Unsupported opcode for incoming data over IMCL 0x%lx",
                           header_ptr->opcode);
            return CAPI_EUNSUPPORTED;
         }
      }

      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_HIGH_PRIO,
                     "CAPI V2 DAM: IMC Set param 0x%x done. payload size = %lu result 0x%x",
                     header_ptr->opcode,
                     header_ptr->actual_data_len,
                     result);

      // increment by header and param actual length
      uint32_t cur_param_size = (sizeof(vw_imcl_header_t) + header_ptr->actual_data_len);
      payload_ptr += cur_param_size;
      payload_size -= cur_param_size;
   }

   return result;
}

capi_err_t capi_audio_dam_imc_set_param_handler(capi_audio_dam_t *me_ptr,
                                                uint32_t          ctrl_port_id,
                                                capi_buf_t *      intent_buf_ptr)
{
   capi_err_t result = CAPI_EOK;
   // get mapped output ports to the ctrl port
   uint32_t   mapped_op_arr_idxs[CAPI_AUDIO_DAM_MAX_OUTPUT_PORTS];
   uint32_t   num_mapped_output_ports = 0;
   capi_err_t err =
      get_output_arr_index_from_ctrl_port_id(me_ptr, ctrl_port_id, &num_mapped_output_ports, mapped_op_arr_idxs);
   if (CAPI_FAILED(err))
   {
      return CAPI_EBADPARAM;
   }

   // iterate through mapped output ports and handle the imcl set param
   uint32_t prev_unread_bytes = 0;
   for (uint32_t idx = 0; idx < num_mapped_output_ports; idx++)
   {
      uint32_t op_arr_idx = mapped_op_arr_idxs[idx];

      uint32_t unread_bytes = 0;
      result |= capi_audio_dam_imc_set_param_handler_per_output(me_ptr,
                                                                ctrl_port_id,
                                                                intent_buf_ptr,
                                                                op_arr_idx,
                                                                &unread_bytes);
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_HIGH_PRIO,
                     "DAM: result 0x%lx IMCL set param on op port id 0x%lx unread_bytes:%lu",
                     result,
                     me_ptr->out_port_info_arr[op_arr_idx].port_id,
                     unread_bytes);

      // check if all the output ports have same amount of unread bytes
      if (idx > 0 && (unread_bytes != prev_unread_bytes))
      {
         DAM_MSG_ISLAND(me_ptr->miid,
                        DBG_HIGH_PRIO,
                        "Warning: Output port read pointers are out of sync, port id 0x%lx unread_bytes: %lu "
                        "prev_unread_bytes: %lu",
                        me_ptr->out_port_info_arr[op_arr_idx].port_id,
                        unread_bytes,
                        prev_unread_bytes);
      }

      prev_unread_bytes = unread_bytes;
   }
   return result;
}