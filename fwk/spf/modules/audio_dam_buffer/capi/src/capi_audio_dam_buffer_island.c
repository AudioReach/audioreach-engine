/**
 *   \file capi_audio_dam_buffer_island.c
 *   \brief
 *        This file contains CAPI implementation of Audio Dam buffer module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_audio_dam_buffer_i.h"

static capi_vtbl_t vtbl = { capi_audio_dam_buffer_process,        capi_audio_dam_buffer_end,
                            capi_audio_dam_buffer_set_param,      capi_audio_dam_buffer_get_param,
                            capi_audio_dam_buffer_set_properties, capi_audio_dam_buffer_get_properties };

capi_vtbl_t *capi_audio_dam_buffer_get_vtable()
{
   return &vtbl;
}

uint32_t capi_dam_get_ctrl_port_arr_idx_from_ctrl_port_id(capi_audio_dam_t *me_ptr, uint32_t ctrl_port_id)
{
   uint32_t available_ctrl_arr_idx = me_ptr->max_output_ports;
   for (uint32_t idx = 0; idx < me_ptr->max_output_ports; idx++)
   {
      if (ctrl_port_id == me_ptr->imcl_port_info_arr[idx].port_id)
      {
         return idx;
      }
      else if (!me_ptr->imcl_port_info_arr[idx].port_id)
      {
         available_ctrl_arr_idx = idx;
      }
   }

   if (available_ctrl_arr_idx != me_ptr->max_output_ports)
   {
      DAM_MSG_ISLAND(me_ptr->miid,
                     DBG_LOW_PRIO,
                     "capi_audio_dam: Mapping Ctrl Port ID =0x%x to index=0x%x",
                     ctrl_port_id,
                     available_ctrl_arr_idx);
      me_ptr->imcl_port_info_arr[available_ctrl_arr_idx].port_id = ctrl_port_id;
      return available_ctrl_arr_idx;
   }

   DAM_MSG_ISLAND(me_ptr->miid,
                  DBG_ERROR_PRIO,
                  "capi_audio_dam: Ctrl Port ID = 0x%lx to index mapping not found.",
                  ctrl_port_id);
   return UMAX_32;
}

static capi_err_t capi_audio_dam_handle_and_drop_metadata(capi_audio_dam_t   *me_ptr,
                                                          capi_stream_data_t *input,
                                                          uint32_t            ip_port_index)
{
   capi_err_t             capi_result   = CAPI_EOK;
   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input;

   for (module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr; node_ptr;)
   {
      bool_t                IS_DROPPED_TRUE = TRUE;
      module_cmn_md_list_t *next_ptr        = node_ptr->next_ptr;

      // handle pcm frame metadata
      if (MD_ID_ENCODER_FRAME_LENGTH_INFO == node_ptr->obj_ptr->metadata_id)
      {
         capi_audio_dam_handle_pcm_frame_info_metadata(me_ptr,
                                                       input,
                                                       ip_port_index,
                                                       (md_encoder_pcm_frame_length_t *)&node_ptr->obj_ptr
                                                          ->metadata_buf[0]);
      }
      else if (MODULE_CMN_MD_ID_EOS == node_ptr->obj_ptr->metadata_id)
      {
         DAM_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "EOS rcvd");

         for (uint32_t op_arr_index = 0; op_arr_index < me_ptr->max_output_ports; op_arr_index++)
         {
            /** do not reset the circ buffer if gate is open*/
            if (!me_ptr->out_port_info_arr[op_arr_index].is_gate_opened)
            {

               // adjust rd ptr inorder to drop all the data
               uint32_t read_offset_in_us = 0;
               audio_dam_stream_read_adjust(me_ptr->out_port_info_arr[op_arr_index].strm_reader_ptr,
                                          read_offset_in_us,
                                          NULL,
                                          FALSE);
            }
         }
      }

      if (me_ptr->metadata_handler.metadata_destroy)
      {
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   IS_DROPPED_TRUE,
                                                   &in_stream_ptr->metadata_list_ptr);
      }
      else
      {
         return CAPI_EFAILED;
      }
      node_ptr = next_ptr;
   }

   // EOF/EOS will be dropped from the module.
   in_stream_ptr->flags.end_of_frame = FALSE;
   in_stream_ptr->flags.marker_eos   = FALSE;

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_audio_dam_buffer_process
  Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
capi_err_t capi_audio_dam_buffer_process(capi_t *capi_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_audio_dam: received bad property pointer");
      return CAPI_EFAILED;
   }

   if ((NULL == input) && (NULL == output))
   {
      return result;
   }

   capi_audio_dam_t *me_ptr = (capi_audio_dam_t *)capi_ptr;

   if (input)
   {
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_input_ports; arr_idx++)
      {
         // Check the port index from arr index
         uint32_t port_index = me_ptr->in_port_info_arr[arr_idx].port_index;

         // input port buffers sanity check
         // if buffer ptrs are null return
         if (NULL == input[port_index] || (NULL == input[port_index]->buf_ptr))
         {
            continue;
         }

         result |= capi_audio_dam_handle_and_drop_metadata(me_ptr, input[port_index], port_index);

         // input port buffers sanity check
         if ((NULL == me_ptr->in_port_info_arr[arr_idx].strm_writer_ptr) ||
             (NULL == input[port_index]->buf_ptr[0].data_ptr))
         {
            continue;
         }

         // Usually num configured inp chs is same as input media format, even if they are not same module handles
         // graciously in the following way,
         // 1. If num inp chs is less than configured chs, module buffers available inp num chs and ignore other chs.
         // 2. if num inp chs is greater than configured, module should buffer configured chs and drop others.
         uint32_t actual_input_chs =
            MIN(me_ptr->in_port_info_arr[arr_idx].strm_writer_ptr->num_channels, input[port_index]->bufs_num);

         // Write data into the dam buffers using dam buffers.
         result |= audio_dam_stream_write(me_ptr->in_port_info_arr[arr_idx].strm_writer_ptr,
                                          actual_input_chs,
                                          input[port_index]->buf_ptr,
                                          input[port_index]->flags.is_timestamp_valid,
                                          input[port_index]->timestamp);

#ifdef AUDIO_DAM_TS_DEBUG
         AR_MSG(DBG_HIGH_PRIO,
                "dam_write: buf ts: %lu is_valid: %lu actual_data_len:%lu ",
                input[port_index]->timestamp,
                input[port_index]->flags.is_timestamp_valid,
                input[port_index]->buf_ptr[0].actual_data_len);
#endif
      }
   }

   if (output)
   {
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
      {
         // Generate output only if,
         //      1. Steam reader is created [means port is Started and circular buffers are initialized]
         //      2. Gate is opened by the detection engine, after detection.
         if (!me_ptr->out_port_info_arr[arr_idx].is_gate_opened || !is_dam_output_port_initialized(me_ptr, arr_idx) ||
             !me_ptr->out_port_info_arr[arr_idx].is_started)
         {
            continue;
         }

         // Get port index from the arr index.
         uint32_t port_index = me_ptr->out_port_info_arr[arr_idx].port_index;

         // output buffers sanity check.
         if ((NULL == output[port_index]) || (NULL == output[port_index]->buf_ptr) ||
             (NULL == output[port_index]->buf_ptr[0].data_ptr))
         {
#ifdef DEBUG_AUDIO_DAM_BUFFER_MODULE
            DAM_MSG_ISLAND(me_ptr->miid,
                           DBG_MED_PRIO,
                           "warning: output buffers not available port_id=0x%x ",
                           me_ptr->out_port_info_arr[arr_idx].port_id);
#endif
            continue;
         }

         // IF the buffer has stale data.
         if ((output[port_index]->buf_ptr->actual_data_len == output[port_index]->buf_ptr->max_data_len) ||
             (0 == output[port_index]->buf_ptr->max_data_len))
         {
            continue;
         }

         if (me_ptr->out_port_info_arr[arr_idx].is_pending_gate_close)
         {
            // insert eos marker
            bool_t skip_voting_on_eos = me_ptr->out_port_info_arr[arr_idx].is_peer_aad ? TRUE : FALSE;
            if (CAPI_EOK == (result = capi_dam_insert_flushing_eos_at_out_port(me_ptr, output[port_index], skip_voting_on_eos, FLUSHING_EOS)))
            {
               capi_check_and_close_the_gate(me_ptr, arr_idx, FALSE);
               me_ptr->out_port_info_arr[arr_idx].is_pending_gate_close = FALSE;
            }
            continue; // done with this arr index, don't have to update ts
         }

         // Usually num configured output chs is same as output media format, even if they are not same module handles
         // graciously in the following way,
         // 1. If num output chs is less than configured chs, module produces output num chs and ignore other chs.
         // 2. if num output chs is greater than configured, module produces configured chs and drop others.
         uint32_t num_output_chs =
            MIN(me_ptr->out_port_info_arr[arr_idx].actual_output_num_chs, output[port_index]->bufs_num);

         uint32_t output_frame_len_us = 0;
         bool_t   is_timestamp_valid  = FALSE;

         if (me_ptr->out_port_info_arr[arr_idx].is_dcm_duty_cycling_enabled)
         {
            bool_t is_batching_req_met = audio_dam_if_batching_req_met(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr);
            if (is_batching_req_met)
            {
               DAM_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "voting for island exit");
               posal_island_trigger_island_exit();
               capi_dam_duty_cycling_buf_send_message_to_dcm(me_ptr, (uint32_t)SPF_MSG_CMD_DCM_REQ_FOR_ISLAND_EXIT);
               me_ptr->out_port_info_arr[arr_idx].ready_for_island_entry = FALSE;
               DAM_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "voting for island exit done");
            }
         }

         result = audio_dam_stream_read(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr,
                                        num_output_chs,
                                        output[port_index]->buf_ptr,
                                        &is_timestamp_valid,
                                        &output[port_index]->timestamp,
                                        &output_frame_len_us);

         if (AR_ENEEDMORE == result)
         {
            // continue reading data from other output ports
            continue;
         }
         else if (AR_EOK == result) // If read was successful update the current timestamp.
         {
            if (me_ptr->out_port_info_arr[arr_idx].pending_eos)
            {
               posal_island_trigger_island_exit();
               capi_audio_dam_handle_pending_eos(me_ptr, output, arr_idx, port_index);
            }

            output[port_index]->flags.is_timestamp_valid = is_timestamp_valid;

            if (me_ptr->out_port_info_arr[arr_idx].ftrt_unread_data_len_in_us)
            {
               // decrement ftrt unread length based on ouput actual data length
               me_ptr->out_port_info_arr[arr_idx].ftrt_unread_data_len_in_us =
                  (me_ptr->out_port_info_arr[arr_idx].ftrt_unread_data_len_in_us < output_frame_len_us)
                     ? 0
                     : (me_ptr->out_port_info_arr[arr_idx].ftrt_unread_data_len_in_us - output_frame_len_us);

               if (!me_ptr->out_port_info_arr[arr_idx].ftrt_unread_data_len_in_us)
               {
                  if (me_ptr->out_port_info_arr[arr_idx].is_drain_history)
                  {
                     // mark it as pending and handle at next process call
                     me_ptr->out_port_info_arr[arr_idx].is_pending_gate_close = TRUE;
                     DAM_MSG_ISLAND(me_ptr->miid,
                                    DBG_HIGH_PRIO,
                                    "DAM: Port index %lu gate close. history drain is complete",
                                    arr_idx);
                  }
                  DAM_MSG_ISLAND(me_ptr->miid,
                                 DBG_HIGH_PRIO,
                                 "DAM_DRIVER: Stream reader ftrt data is drained, moving to RT mode out_port 0x%x ",
                                 me_ptr->out_port_info_arr[arr_idx].port_id);
               }
            }
            if (me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr->is_batch_streaming)
            {
               capi_audio_dam_buffer_update_kpps_vote(me_ptr);
            }
#ifdef DEBUG_AUDIO_DAM_BUFFER_MODULE
            uint32_t temp_unread_bytes = 0;
            audio_dam_get_stream_reader_unread_bytes(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr,
                                                     &temp_unread_bytes);

            DAM_MSG_ISLAND(me_ptr->miid,
                           DBG_LOW_PRIO,
                           "dam_read: Port_id: %lu buf_ts: %lu, is_valid: %ld, actual_data_len: %lu, "
                           "un_read_bytes: %lu",
                           me_ptr->out_port_info_arr[arr_idx].port_id,
                           output[port_index]->timestamp,
                           output[port_index]->flags.is_timestamp_valid,
                           output[port_index]->buf_ptr[0].actual_data_len,
                           temp_unread_bytes);
#endif
         }
         else
         {
            return CAPI_EFAILED;
         }
      }
   }
   return CAPI_EOK;
}

// function to update the kpps votes.
void capi_audio_dam_buffer_update_kpps_vote(capi_audio_dam_t *me_ptr)
{
   uint32_t agg_kpps_vote = CAPI_AUDIO_DAM_LOW_KPPS;
   for (int i = 0; i < me_ptr->max_output_ports; i++)
   {
      uint32_t kpps_vote = CAPI_AUDIO_DAM_LOW_KPPS;

      /** When gate is opened, bump up the clock by voting for higher KPPS
          This is because, FTRT data of about 1.4s needs to be drained in less than 10 ms.
         If we assume MPPS of 2 for draining, we would need to actually vote (1400*2/10) = 280 MPPS.
         Just voting 500 MPPS to be safe. Vote is removed once FTRT data is fully drained or at algorithmic_reset.*/
      if (me_ptr->out_port_info_arr[i].is_open && me_ptr->out_port_info_arr[i].is_started &&
          me_ptr->out_port_info_arr[i].is_gate_opened)
      {
         if (me_ptr->out_port_info_arr[i].strm_reader_ptr->is_batch_streaming)
         {
            // sometimes 40ms batching will be done at the DMA layer itself
            // therefore DAM will not have FTRT data.
            const uint32_t LOW_COMP_BATCH_PERIOD = 40000;
            if (me_ptr->out_port_info_arr[i].strm_reader_ptr->data_batching_us <=
                   LOW_COMP_BATCH_PERIOD ||                                             // sort of real time streaming
                0 == me_ptr->out_port_info_arr[i].strm_reader_ptr->pending_batch_bytes) // going into buffering mode
            {
               kpps_vote = CAPI_AUDIO_DAM_LOW_KPPS;
            }
            else
            { // in ftrt drain mode
               kpps_vote = CAPI_AUDIO_DAM_HIGH_KPPS;
            }
         }
         else if (me_ptr->out_port_info_arr[i].ftrt_unread_data_len_in_us)
         { // in ftrt drain mode
            kpps_vote = CAPI_AUDIO_DAM_HIGH_KPPS;
         }
      }

      agg_kpps_vote = MAX(agg_kpps_vote, kpps_vote);
   }

   if (me_ptr->kpps_vote != agg_kpps_vote)
   {
      DAM_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "capi_audio_dam: updating kpps %lu", agg_kpps_vote);
      capi_cmn_update_kpps_event(&me_ptr->event_cb_info, agg_kpps_vote);
      me_ptr->kpps_vote = agg_kpps_vote;
   }
}