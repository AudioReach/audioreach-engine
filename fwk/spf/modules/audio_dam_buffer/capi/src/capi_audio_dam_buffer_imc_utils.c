/**
 *   \file capi_audio_dam_buffer_imc_utils.c
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
static capi_err_t capi_audio_dam_imcl_set_hdlr_drain_history_data(capi_audio_dam_t *                   me_ptr,
                                                                  uint32_t                             op_arr_index,
                                                                  param_id_audio_dam_data_flow_ctrl_t *cfg_ptr);

static capi_err_t capi_audio_dam_raise_allow_duty_cycling(capi_audio_dam_t *me_ptr, bool_t allow_duty_cycling)
{
   capi_err_t result         = CAPI_EOK;

   intf_extn_event_id_allow_duty_cycling_v2_t event_payload;
   AR_MSG(DBG_HIGH_PRIO, "CAPI_DAM: Raise allow_duty_cycling: %d", allow_duty_cycling);

   event_payload.allow_duty_cycling = allow_duty_cycling;

   /* Create event */
   capi_event_data_to_dsp_service_t to_send;
   to_send.param_id                = INTF_EXTN_EVENT_ID_ALLOW_DUTY_CYCLING;
   to_send.payload.actual_data_len = sizeof(intf_extn_event_id_allow_duty_cycling_v2_t);
   to_send.payload.max_data_len    = sizeof(intf_extn_event_id_allow_duty_cycling_v2_t);
   to_send.payload.data_ptr        = (int8_t *)&event_payload;

   /* Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(to_send);
   event_info.payload.max_data_len    = sizeof(to_send);
   event_info.payload.data_ptr        = (int8_t *)&to_send;

   result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);

   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI_DAM: Failed to raise INTF_EXTN_EVENT_ID_ALLOW_DUTY_CYCLING event");
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO,"CAPI_DAM: Raised INTF_EXTN_EVENT_ID_ALLOW_DUTY_CYCLING event allow_duty_cycling:%d", event_payload.allow_duty_cycling);
   }
   return result;
}

/*==============================================================================
   Public Function Implementation
==============================================================================*/

capi_err_t capi_audio_dam_imcl_set_hdlr_ouput_ch_cfg(capi_audio_dam_t *me_ptr,
                                                     uint32_t          op_arr_index,
                                                     vw_imcl_header_t *header_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_output_ch_cfg_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: IMC Param id 0x%lx Bad param size %lu",
              (uint32_t)header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_output_ch_cfg_t *cfg_ptr = (param_id_audio_dam_output_ch_cfg_t *)(header_ptr + 1);

   if (header_ptr->actual_data_len <
       (sizeof(param_id_audio_dam_output_ch_cfg_t) + (sizeof(uint32_t) * cfg_ptr->num_channels)))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: IMC Param id 0x%lx Bad param size %lu",
              (uint32_t)header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   // Allocate and register as stream writer with the given port.
   uint32_t out_cfg_size = sizeof(audio_dam_output_port_cfg_t) + (sizeof(channel_map_t) * cfg_ptr->num_channels);

   audio_dam_output_port_cfg_t *out_cfg_params =
      (audio_dam_output_port_cfg_t *)posal_memory_malloc(out_cfg_size, me_ptr->heap_id);
   if (NULL == out_cfg_params)
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Param id 0x%lx, memory couldn't be allocated for the internal "
              "struts.");
      return CAPI_ENOMEMORY;
   }
   memset(out_cfg_params, 0, out_cfg_size);

   channel_map_t *output_ch_map   = (channel_map_t *)(out_cfg_params + 1);
   out_cfg_params->output_port_id = me_ptr->out_port_info_arr[op_arr_index].port_id;
   out_cfg_params->num_channels   = cfg_ptr->num_channels;
   for (uint32_t idx = 0; idx < cfg_ptr->num_channels; idx++)
   {
      output_ch_map[idx].input_ch_id  = cfg_ptr->channels_ids[idx];
      output_ch_map[idx].output_ch_id = idx + 1; // assign default channel map
   }

   // Reinitalize the output port with new configuration.
   capi_check_and_reinit_output_port(me_ptr, op_arr_index, out_cfg_params);

   return result;
}

capi_err_t capi_audio_dam_imcl_set_hdlr_flow_ctrl_v2(capi_audio_dam_t *me_ptr,
                                                     uint32_t          op_arr_index,
                                                     vw_imcl_header_t *header_ptr,
                                                     uint32_t *        unread_bytes_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_data_flow_ctrl_v2_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "capi_audio_dam: Param id 0x%lx Bad param size %lu",
              (uint32_t)header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_data_flow_ctrl_v2_t *cfg_ptr = (param_id_audio_dam_data_flow_ctrl_v2_t *)(header_ptr + 1);

   // set it to zero by default, will be updated if gate is being opened.
   me_ptr->out_port_info_arr[op_arr_index].ftrt_unread_data_len_in_us = 0;

   // open the gate only if its un-opened
   switch (cfg_ptr->gate_ctrl)
   {
      case AUDIO_DAM_GATE_CLOSE:
      {
         result = capi_audio_dam_imcl_handle_gate_close(me_ptr, op_arr_index);
         break;
      }
      case AUDIO_DAM_GATE_OPEN:
      {
         DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "DAM: Open Request v2 Received");
         result =
            capi_audio_dam_imcl_handle_gate_open(me_ptr, op_arr_index, (param_id_audio_dam_data_flow_ctrl_t *)cfg_ptr);
         break;
      }
      case AUDIO_DAM_BATCH_STREAM:
      case AUDIO_DAM_BATCH_STREAM_WITH_ISLAND_DUTY_CYCLING:
      {
         DAM_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "DAM: Batch Stream Request with batch_period = %lu",
                 cfg_ptr->read_offset_in_us);

         audio_dam_stream_reader_enable_batching_mode(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                                      TRUE /**enable batching mode*/,
                                                      cfg_ptr->read_offset_in_us);

         result =
            capi_audio_dam_imcl_handle_gate_open(me_ptr, op_arr_index, (param_id_audio_dam_data_flow_ctrl_t *)cfg_ptr);

         if (AUDIO_DAM_BATCH_STREAM_WITH_ISLAND_DUTY_CYCLING == cfg_ptr->gate_ctrl)
         {
            me_ptr->is_island_duty_cycle_enabled = TRUE;

            capi_audio_dam_raise_allow_duty_cycling(me_ptr, TRUE);
         }
         break;
      }
      case AUDIO_DAM_DRAIN_HISTORY:
      {
         DAM_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "DAM: History Drain Request with read offset = %lu",
                 cfg_ptr->read_offset_in_us);

         result = capi_audio_dam_imcl_set_hdlr_drain_history_data(me_ptr,
                                                                  op_arr_index,
                                                                  (param_id_audio_dam_data_flow_ctrl_t *)cfg_ptr);
      }
      default:
      {
         DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "DAM: Received Unsupported Gate Status");
         return CAPI_EUNSUPPORTED;
      }
   }

   // return unread bytes
   audio_dam_get_stream_reader_unread_bytes(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr, unread_bytes_ptr);

   return result;
}

static capi_err_t capi_audio_dam_imcl_set_hdlr_drain_history_data(capi_audio_dam_t *                   me_ptr,
                                                                  uint32_t                             op_arr_index,
                                                                  param_id_audio_dam_data_flow_ctrl_t *cfg_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (TRUE == me_ptr->out_port_info_arr[op_arr_index].is_gate_opened)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_audio_dam: Gate is already opened for ctrl_port_it 0x%x. Rejecting Flush request.",
             me_ptr->out_port_info_arr[op_arr_index].ctrl_port_id);
      return CAPI_ENEEDMORE;
   }

   capi_audio_dam_imcl_handle_gate_open(me_ptr, op_arr_index, cfg_ptr);

   AR_MSG(DBG_LOW_PRIO, "capi_audio_dam: History in us Drain request %lu", cfg_ptr->read_offset_in_us);

   me_ptr->out_port_info_arr[op_arr_index].is_drain_history = TRUE;

   // if there is no history then mark for gate close.
   if (0 == me_ptr->out_port_info_arr[op_arr_index].ftrt_unread_data_len_in_us)
   {
      me_ptr->out_port_info_arr[op_arr_index].is_pending_gate_close = TRUE;
      AR_MSG(DBG_HIGH_PRIO, "DAM: Port index  %lu gate close. FLUSH complete", op_arr_index);
   }
   return result;
}

capi_err_t capi_audio_dam_imcl_trigger_island_entry(capi_audio_dam_t *me_ptr,
                                                   uint32_t          op_arr_index,
                                                   vw_imcl_header_t *header_ptr)
{
   DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi_audio_dam: capi_audio_dam_imcl_trigger_island_entry");
   capi_err_t result = CAPI_EOK;
   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_allow_dcm_island_entry_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI V2 DAM: IMC Param id 0x%lx Invalid payload size for incoming data %d",
              header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_allow_dcm_island_entry_t *cfg_ptr = (param_id_audio_dam_allow_dcm_island_entry_t *)(header_ptr + 1);

   // Store the flag
   me_ptr->out_port_info_arr[op_arr_index].trigger_island_entry = cfg_ptr->process_done;

   uint32_t pending_bytes_to_read = 0;
   if (!me_ptr->is_island_duty_cycle_enabled)
   {
      // return error
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "DAM is not DCM enabled");
      return CAPI_EUNSUPPORTED;
   }

   audio_dam_get_stream_reader_pending_bytes(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr, &pending_bytes_to_read);

   if (!is_dam_output_port_initialized(me_ptr, op_arr_index))
   {
      DAM_MSG(me_ptr->miid,
                     DBG_ERROR_PRIO,
                     "Output port not initialized [%lu]",
                     pending_bytes_to_read);
      return CAPI_ENOTREADY;
   }


   // island entry conditions, pending_bytes -> 0, DCM mode enabled and port is intialized
   if (pending_bytes_to_read == 0)
   {
      result |= capi_dam_duty_cycling_buf_send_message_to_dcm(me_ptr, (uint32_t)SPF_MSG_CMD_DCM_REQ_FOR_UNBLOCK_ISLAND_ENTRY);
   }
   else
   {
      DAM_MSG(me_ptr->miid,
            DBG_MED_PRIO, "Batching stream data buffering started. Triggering Island Entry pending pending bytes [%lu]", pending_bytes_to_read);
   }
   return result;
}

capi_err_t capi_audio_dam_imcl_set_hdlr_resize(capi_audio_dam_t *me_ptr,
                                               uint32_t          op_arr_index,
                                               vw_imcl_header_t *header_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_buffer_resize_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI V2 DAM: IMC Param id 0x%lx Invalid payload size for incoming data %d",
              header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_buffer_resize_t *cfg_ptr = (param_id_audio_dam_buffer_resize_t *)(header_ptr + 1);

   // Store the requested resize in us.
   me_ptr->out_port_info_arr[op_arr_index].requested_resize_in_us = cfg_ptr->resize_in_us;

   // Check if output port is initalized.
   if (is_dam_output_port_initialized(me_ptr, op_arr_index))
   {
      result = capi_audio_dam_resize_buffers(me_ptr, op_arr_index);
   }
   return result;
}

capi_err_t capi_audio_dam_imcl_set_hdlr_peer_info(capi_audio_dam_t *me_ptr,
                                                  uint32_t          op_arr_index,
                                                  vw_imcl_header_t *header_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_imcl_peer_info_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI V2 DAM: IMC Param id 0x%lx Invalid payload size for incoming data %d",
              header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   param_id_audio_dam_imcl_peer_info_t *cfg_ptr = (param_id_audio_dam_imcl_peer_info_t *)(header_ptr + 1);

   DAM_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "Received IMC PARAM_ID_AUDIO_DAM_IMCL_PEER_INFO is_peer_aad:%lu is_valid:%lu",
           cfg_ptr->is_aad_usecase,
           cfg_ptr->is_preferred_heap_id_valid);

   if (cfg_ptr->is_aad_usecase)
   {
      /* If IMCL peer is Acoustic Activity Detection disable TGP Dynamic switch to defualt policy.

         Acoustic Activity Detection's dam module is placed in the middle of topology and is not connected
         to ext output port directly unlike othere Dam instances. Switching TGP to
         default at gate close can lead to drops at Dams input port. EOS could be
         stuck at Dams output due to DS data pile up and so output trigger may not
         be available, leading to dropping RT data at dam's input.
      */
      me_ptr->cannot_toggle_to_default_tgp = TRUE;
      me_ptr->out_port_info_arr[op_arr_index].is_peer_aad = TRUE;

      // update TGP
      capi_audio_dam_change_trigger_policy(me_ptr);
   }

   // cache the heap id with updated tracking info
   if (cfg_ptr->is_preferred_heap_id_valid)
   {
      me_ptr->out_port_info_arr[op_arr_index].is_peer_heap_id_valid = TRUE;
      me_ptr->out_port_info_arr[op_arr_index].peer_heap_id =
         (POSAL_HEAP_ID)MODIFY_HEAP_ID_FOR_MEM_TRACKING(me_ptr->heap_id, cfg_ptr->preferred_heap_id);

      result = capi_audio_dam_resize_buffers(me_ptr, op_arr_index);
   }
   else
   {
      // invalidate the existing heap ID.
      me_ptr->out_port_info_arr[op_arr_index].is_peer_heap_id_valid = FALSE;
   }

   return result;
}

capi_err_t capi_audio_dam_imcl_send_unread_len(capi_audio_dam_t *me_ptr,
                                               uint32_t          unread_len_in_us,
                                               uint32_t          ctrl_port_id)
{
   capi_err_t result = CAPI_EOK;
   capi_buf_t buf;
   buf.data_ptr                = NULL;
   buf.max_data_len            = 0;
   uint32_t total_payload_size = sizeof(vw_imcl_header_t) + sizeof(param_id_audio_dam_unread_bytes_t);

   // Get one time buf
   capi_cmn_imcl_get_one_time_buf(&me_ptr->event_cb_info, ctrl_port_id, total_payload_size, &buf);
   if (!buf.data_ptr || buf.max_data_len < total_payload_size)
   {
      DAM_MSG(me_ptr->miid, DBG_ERROR_PRIO, "DAM: Getting one time buffer failed");
      return CAPI_EFAILED;
   }

   vw_imcl_header_t *out_cfg_ptr = (vw_imcl_header_t *)buf.data_ptr;

   out_cfg_ptr->opcode          = PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH;
   out_cfg_ptr->actual_data_len = sizeof(param_id_audio_dam_unread_bytes_t);

   param_id_audio_dam_unread_bytes_t *unread_cfg_ptr =
      (param_id_audio_dam_unread_bytes_t *)(buf.data_ptr + sizeof(vw_imcl_header_t));

   unread_cfg_ptr->unread_in_us = unread_len_in_us;

   buf.actual_data_len = total_payload_size;

   // Send data ready to the peer module
   imcl_outgoing_data_flag_t flags;
   flags.should_send = TRUE;
   flags.is_trigger  = TRUE;
   result            = capi_cmn_imcl_send_to_peer(&me_ptr->event_cb_info, &buf, ctrl_port_id, flags);

   return result;
}

capi_err_t capi_audio_dam_resize_buffers(capi_audio_dam_t *me_ptr, uint32_t op_arr_idx)
{
   ar_result_t result = AR_EOK;

   uint32_t resize_in_us = me_ptr->out_port_info_arr[op_arr_idx].requested_resize_in_us;

   // check if any peer heap ID is received
   POSAL_HEAP_ID peer_heap_id = capi_audio_dam_get_peer_heap_id(me_ptr, op_arr_idx);

   AR_MSG(DBG_HIGH_PRIO,
          "Output port id 0x%lx resize_request_in_us: %lu HID: %lu",
          me_ptr->out_port_info_arr[op_arr_idx].port_id,
          resize_in_us,
          peer_heap_id);

   result = audio_dam_stream_reader_req_resize(me_ptr->out_port_info_arr[op_arr_idx].strm_reader_ptr,
                                               resize_in_us,
                                               peer_heap_id);
   if (AR_DID_FAIL(result))
   {
      return CAPI_EFAILED;
   }

   return CAPI_EOK;
}

capi_err_t capi_audio_dam_imcl_set_virt_writer_cfg(capi_audio_dam_t *me_ptr,
                                                   uint32_t          ctrl_arr_index,
                                                   uint32_t          op_arr_index,
                                                   vw_imcl_header_t *header_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_imcl_virtual_writer_info_t))
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI V2 DAM: IMC Param id 0x%lx Invalid payload size for incoming data %d",
              header_ptr->opcode,
              header_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   if (!me_ptr->out_port_info_arr[op_arr_index].is_peer_aad)
   {
      DAM_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI V2 DAM: IMC Param id 0x%lx currently unsupported if peer is not AAD module",
              header_ptr->opcode);
      return AR_EFAILED;
   }

   param_id_audio_dam_imcl_virtual_writer_info_t *cfg_ptr =
      (param_id_audio_dam_imcl_virtual_writer_info_t *)(header_ptr + 1);

   bool_t is_reinit_required = FALSE;
   if (cfg_ptr->enable)
   {
      is_reinit_required = TRUE;
      DAM_MSG(me_ptr->miid, DBG_HIGH_PRIO, "CAPI V2 DAM: Switching to virtual writer mode");

      if (NULL == me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr)
      {
         me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr = (param_id_audio_dam_imcl_virtual_writer_info_t *)
            posal_memory_malloc(sizeof(param_id_audio_dam_imcl_virtual_writer_info_t), me_ptr->heap_id);
         if (NULL == me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr)
         {
            DAM_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI V2 DAM: IMC Param id 0x%lx couldnt cache the virt wr cfg, no memory",
                    header_ptr->opcode);
            return AR_EFAILED;
         }
      }
      else
      {
         // no need to re-allocate memory for the structure, we can overwrite exisiting memory
         // since the structure size is same.
      }

      // cache new configuration
      memscpy(me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr,
              sizeof(param_id_audio_dam_imcl_virtual_writer_info_t),
              cfg_ptr,
              sizeof(param_id_audio_dam_imcl_virtual_writer_info_t));

      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "virtual writer: received cfg enable: %lu addr:%lx size:(%luus, %lubytes) wr_pos:(fn:%lx hdl:%lx)",
              cfg_ptr->enable,
              cfg_ptr->circular_buffer_base_address,
              cfg_ptr->circular_buffer_size_in_us,
              cfg_ptr->circular_buffer_size_in_bytes,
              cfg_ptr->get_writer_ptr_fn,
              cfg_ptr->writer_handle);

      DAM_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "virtual writer: received cfg chs:%lu SR:%lu bw:%lu qf:%lu sign:%lu ",
              cfg_ptr->num_channels,
              cfg_ptr->sampling_rate,
              cfg_ptr->bits_per_sample,
              cfg_ptr->q_factor,
              cfg_ptr->data_is_signed);
   }
   else // getting disabled
   {
      /**currently virt mode enabled, need to disabled */
      if (audio_dam_driver_is_virtual_writer_mode(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr))
      {
         is_reinit_required = TRUE;
      }

      if (me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr)
      {
         posal_memory_free(me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr);
         me_ptr->imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr = NULL;
      }
   }

   /** Reinitalize the output port with the virtual writer configuration new
    * imcl_port_info_arr[ctrl_arr_index].virt_wr_cfg_ptr will be passed when initialzing the output*/
   if (is_reinit_required)
   {
      capi_check_and_reinit_output_port(me_ptr, op_arr_index, NULL /** no change in output ch map configuration */);
   }

   return result;
}
