/**
 * \file capi_smart_sync_utils.c
 * \brief
 *       Implementation of utility functions for smart sync module.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_smart_sync_i.h"
#include "module_cmn_api.h"
#include "spf_list_utils.h"
#include "voice_timer_wrapper.h"
#include "posal.h"

// TODO(CG): Check these values
const uint32_t SMART_SYNC_BW   = 1 * 1024 * 1024;
const uint32_t SMART_SYNC_KPPS = 30;

static capi_err_t capi_smart_sync_propagate_metadata(capi_smart_sync_t *        me_ptr,
                                                     capi_stream_data_t *       input,
                                                     capi_stream_data_t *       output,
                                                     capi_smart_sync_in_port_t *in_port_ptr,
                                                     uint32_t                   bytes_before);

static capi_err_t capi_smart_sync_send_ttr_metadata(capi_smart_sync_t *me_ptr, capi_stream_data_t *output);

static capi_err_t capi_smart_sync_check_send_packet_token(capi_smart_sync_t * me_ptr,
                                                          capi_stream_data_t *out_stream_ptr);

static capi_err_t capi_smart_sync_port_open(capi_smart_sync_t *me_ptr,
                                            bool_t             is_primary,
                                            bool_t             is_input,
                                            uint32_t           port_index);

static capi_err_t capi_smart_sync_port_start(capi_smart_sync_t *me_ptr,
                                             bool_t             is_primary,
                                             bool_t             is_input,
                                             uint32_t           port_index);

static capi_err_t capi_smart_sync_port_stop(capi_smart_sync_t *me_ptr,
                                            bool_t             is_primary,
                                            bool_t             is_input,
                                            uint32_t           port_index);

static capi_err_t capi_smart_sync_port_close(capi_smart_sync_t *me_ptr, bool_t is_primary, bool_t is_input);

static capi_err_t capi_send_smart_sync_state_to_client(capi_smart_sync_t *me_ptr,
                                                       uint32_t           is_sycned);

/* =========================================================================
 * Function definitions
 * =========================================================================*/

/**
 * Check if we're ready to subscribe to the voice timer. All of below most hold:
 * 1. Not already subscribed: !(me_ptr->vfr_timestamp_us_ptr)
 * 2. voice_proc_start_signal_ptr exists
 * 3. Primary port is started
 * 4. voice_proc_info set param came: me_ptr->voice_proc_info.vfr_cycle_duration_ms
 */
bool_t capi_smart_sync_can_subscribe_to_vt(capi_smart_sync_t *me_ptr)
{
   return ((!(me_ptr->vfr_timestamp_us_ptr)) && (me_ptr->voice_proc_start_signal_ptr) &&
           (me_ptr->voice_resync_signal_ptr) && (CAPI_PORT_STATE_STARTED == me_ptr->primary_in_port_info.cmn.state) &&
           (me_ptr->voice_proc_info.vfr_cycle_duration_ms));
}

/**
 * Function to send the output media format using the callback function
 */
capi_err_t capi_smart_sync_raise_process_event(capi_smart_sync_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (me_ptr->events_config.enable != TRUE)
   {
      capi_result = capi_cmn_update_process_check_event(&me_ptr->cb_info, TRUE);
      if (CAPI_EOK == capi_result)
      {
         me_ptr->events_config.enable = TRUE;
      }
   }
   return capi_result;
}

static capi_err_t capi_smart_sync_check_send_packet_token(capi_smart_sync_t *me_ptr, capi_stream_data_t *out_stream_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (20000 == me_ptr->threshold_us)
   {
      me_ptr->skip_send_ttr_metadata = FALSE;
   }
   if (me_ptr->num_bytes_copied_to_output == me_ptr->threshold_bytes_per_ch)
   {
      // If container threshold amount of data is already generated in this topo-process context then avoid generating
      // more data until next topo-process
      me_ptr->can_process = FALSE;

      if (!me_ptr->skip_send_ttr_metadata)
      {
         if (10000 == me_ptr->threshold_us)
         {
            me_ptr->skip_send_ttr_metadata = TRUE;
         }

         capi_result = capi_smart_sync_send_ttr_metadata(me_ptr, out_stream_ptr);

         if (CAPI_EOK != capi_result)
         {
            AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Failed to send ttr metadata");
            return capi_result;
         }

         if (VFR_CYCLE_DURATION_40MS == me_ptr->voice_proc_info.vfr_cycle_duration_ms)
         {
            if (TTR_PACKET_TOKEN_P2 == me_ptr->current_packet_token)
            {
               me_ptr->current_packet_token = TTR_PACKET_TOKEN_P1;
            }
            else
            {
               me_ptr->current_packet_token++;
            }
         }
      }
      else
      {
         me_ptr->skip_send_ttr_metadata = FALSE;
      }

      me_ptr->num_bytes_copied_to_output = 0;
   }

   return capi_result;
}

/**
 * Reset circular buffers.
 */
void capi_smart_sync_reset_buffers(capi_smart_sync_t *me_ptr)
{
   uint32_t num_channels = 0;
   uint32_t mem_size     = 0;

   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: resetting buffers");

   me_ptr->primary_in_port_info.circ_buf.actual_data_len_per_ch = 0;
   me_ptr->primary_in_port_info.circ_buf.read_index             = 0;
   me_ptr->primary_in_port_info.circ_buf.write_index            = 0;
   me_ptr->primary_in_port_info.cur_out_buf_timestamp_us        = 0;
   me_ptr->primary_in_port_info.is_ts_valid                     = FALSE;

   me_ptr->secondary_in_port_info.circ_buf.actual_data_len_per_ch = 0;
   me_ptr->secondary_in_port_info.circ_buf.read_index             = 0;
   me_ptr->secondary_in_port_info.circ_buf.write_index            = 0;
   me_ptr->secondary_in_port_info.cur_out_buf_timestamp_us        = 0;
   me_ptr->secondary_in_port_info.is_ts_valid                     = FALSE;

   // memset circ buf zeros is needed due to optimized version of padding initial zeros.
   if (me_ptr->primary_in_port_info.circ_buf.bufs_ptr)
   {
      num_channels = me_ptr->primary_in_port_info.media_fmt.format.num_channels;
      mem_size     = me_ptr->primary_in_port_info.circ_buf.max_data_len_per_ch * num_channels;
      memset(me_ptr->primary_in_port_info.circ_buf.bufs_ptr, 0, mem_size);
   }

   if (me_ptr->secondary_in_port_info.circ_buf.bufs_ptr)
   {
      num_channels = me_ptr->secondary_in_port_info.media_fmt.format.num_channels;
      mem_size     = me_ptr->secondary_in_port_info.circ_buf.max_data_len_per_ch * num_channels;
      memset(me_ptr->secondary_in_port_info.circ_buf.bufs_ptr, 0, mem_size);
   }
}
/**
 * Reset module to VFR resync state.
 */
capi_err_t capi_smart_sync_resync_module_state(capi_smart_sync_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   bool_t     DISABLE     = FALSE;

   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: resyncing module state");

   me_ptr->state                           = SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK;
   me_ptr->is_proc_tick_notif_rcvd         = FALSE;
   me_ptr->out_to_drop_from_next_vfr_cycle = me_ptr->out_generated_this_vfr_cycle;

   me_ptr->primary_in_port_info.zeros_were_padded   = FALSE;
   me_ptr->secondary_in_port_info.zeros_were_padded = FALSE;

   me_ptr->received_eof = FALSE;

   capi_smart_sync_reset_buffers(me_ptr);

   capi_smart_sync_raise_event_change_container_trigger_policy(me_ptr, OUTPUT_BUFFER_TRIGGER);
   capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, DISABLE);

   return capi_result;
}

/**
 * Reset module to initial state, where sync is needed.
 */
capi_err_t capi_smart_sync_reset_module_state(capi_smart_sync_t *me_ptr, bool_t reset_buffer)
{
   capi_err_t capi_result = CAPI_EOK;
   bool_t     DISABLE     = FALSE;

   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: resetting module state");

   me_ptr->state                      = SMART_SYNC_STATE_BEFORE_FIRST_PROC_TICK;
   me_ptr->num_bytes_copied_to_output = 0;
   me_ptr->is_proc_tick_notif_rcvd    = FALSE;

   if(reset_buffer)
   {
      capi_smart_sync_reset_buffers(me_ptr);
   }
   /* Reset packet token too so that its populated properly
    * for the first packet corresponding to the next cycle */
   me_ptr->current_packet_token = TTR_PACKET_TOKEN_P1;

   me_ptr->primary_in_port_info.zeros_were_padded   = FALSE;
   me_ptr->secondary_in_port_info.zeros_were_padded = FALSE;

   me_ptr->out_generated_this_vfr_cycle    = 0;
   me_ptr->out_to_drop_from_next_vfr_cycle = 0;

   me_ptr->cached_first_vfr_occurred = FALSE;
   me_ptr->vfr_timestamp_at_cur_proc_tick = 0;

   me_ptr->received_eof = FALSE;

   capi_smart_sync_raise_event_change_container_trigger_policy(me_ptr, OUTPUT_BUFFER_TRIGGER);
   capi_result |= capi_smart_sync_raise_event_toggle_threshold_n_sync_state(me_ptr, DISABLE);

   return capi_result;
}

/**
 * Function to send the toggle threshold change event.
 */
capi_err_t capi_smart_sync_raise_event_toggle_threshold_n_sync_state( capi_smart_sync_t *me_ptr,
                                                                      bool_t enable_threshold)
{
   capi_err_t        capi_result = CAPI_EOK;
   capi_event_info_t event_info;
   event_info.port_info.is_valid = FALSE;

   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: capi event callback is not set, unable to raise enable threshold event!");
      return CAPI_EBADPARAM;
   }

   // Package the fwk event within the data_to_dsp capi event.
   capi_event_data_to_dsp_service_t                    evt = { 0 };
   fwk_extn_sync_event_id_enable_threshold_buffering_t event;
   event.enable_threshold_buffering = enable_threshold;

   evt.param_id                = FWK_EXTN_SYNC_EVENT_ID_ENABLE_THRESHOLD_BUFFERING;
   evt.token                   = 0;
   evt.payload.actual_data_len = sizeof(fwk_extn_sync_event_id_enable_threshold_buffering_t);
   evt.payload.data_ptr        = (int8_t *)&event;
   evt.payload.max_data_len    = sizeof(fwk_extn_sync_event_id_enable_threshold_buffering_t);

   event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_service_t);
   event_info.payload.data_ptr        = (int8_t *)&evt;
   event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_service_t);

   me_ptr->is_threshold_disabled = !enable_threshold;

   capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);
   if (CAPI_FAILED(capi_result))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Failed to raise enable threshold event with %lu", capi_result);
   }

   AR_MSG(DBG_MED_PRIO, "capi smart sync: raised event for enable threshold, enable = %ld!", enable_threshold);

   // send sync state to the client
   uint32_t is_synced = enable_threshold ? TRUE : FALSE;
   capi_send_smart_sync_state_to_client(me_ptr, is_synced);

   return capi_result;
}

/**
 * Function to send the change in container trigger policy event.
 */
capi_err_t capi_smart_sync_raise_event_change_container_trigger_policy(
   capi_smart_sync_t *        me_ptr,
   container_trigger_policy_t container_trigger_policy)
{
   capi_err_t        capi_result = CAPI_EOK;
   capi_event_info_t event_info;
   event_info.port_info.is_valid = FALSE;

   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: capi event callback is not set, unable to raise enable threshold event!");
      return CAPI_EBADPARAM;
   }

   me_ptr->trigger_policy = container_trigger_policy;

   // Package the fwk event within the data to dsp service capi event.
   capi_event_data_to_dsp_service_t             evt = { 0 };
   capi_event_change_container_trigger_policy_t evt_payload;

   evt_payload.container_trigger_policy = container_trigger_policy;

   evt.param_id                = FWK_EXTN_VOICE_DELIVERY_EVENT_ID_CHANGE_CONTAINER_TRIGGER_POLICY;
   evt.token                   = 0;
   evt.payload.data_ptr        = (int8_t *)(&evt_payload);
   evt.payload.actual_data_len = sizeof(capi_event_change_container_trigger_policy_t);
   evt.payload.max_data_len    = sizeof(capi_event_change_container_trigger_policy_t);

   event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_service_t);
   event_info.payload.data_ptr        = (int8_t *)&evt;
   event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_service_t);
   capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);
   if (CAPI_FAILED(capi_result))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Failed to raise event with %lu", capi_result);
   }

   #ifdef SMART_SYNC_DEBUG
   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: raised event to change container trigger policy to %lu (0=timer, 1=buffer)",
          (uint32_t)(container_trigger_policy));
   #endif

   return capi_result;
}

/* Sends event FWK_EXTN_VOICE_DELIVERY_EVENT_ID_UPDATE_SYNC_STATE to the client */
static capi_err_t capi_send_smart_sync_state_to_client(capi_smart_sync_t *me_ptr, uint32_t is_sycned)
{
   capi_err_t result = CAPI_EOK;
   if (0 == me_ptr->evt_dest_address)
   {
      AR_MSG_ISLAND(DBG_MED_PRIO, "capi_send_smart_sync_state_to_client: Event client not registered");
      return result;
   }

   capi_event_info_t                                 event_info;
   capi_event_data_to_dsp_client_v2_t                event;
   fwk_extn_voice_delivery_event_update_sync_state_t payload;

   payload.is_synced = is_sycned;

   event.event_id                     = FWK_EXTN_VOICE_DELIVERY_EVENT_ID_UPDATE_SYNC_STATE;
   event.payload.actual_data_len      = sizeof(fwk_extn_voice_delivery_event_update_sync_state_t);
   event.payload.max_data_len         = sizeof(fwk_extn_voice_delivery_event_update_sync_state_t);
   event.payload.data_ptr             = (int8_t *)&payload;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
   event_info.payload.data_ptr        = (int8_t *)&event;
   event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);
   event.dest_address                 = me_ptr->evt_dest_address;
   event.token                        = me_ptr->evt_dest_token;

   AR_MSG_ISLAND(DBG_HIGH_PRIO, "capi_send_smart_sync_state_to_client: raised is_synced:%lu state", is_sycned);

   result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_CLIENT_V2, &event_info);
   if (CAPI_EOK != result)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO,
                    "capi_send_smart_sync_state_to_client: Failed raised is_synced:%lu state",
                    is_sycned);
   }

   return result;
}

/**
 * Function to raise various events of the ec sync.
 */
capi_err_t capi_smart_sync_raise_event(capi_smart_sync_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync:  Event callback is not set. Unable to raise events!");
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   me_ptr->events_config.kpps        = SMART_SYNC_KPPS;
   me_ptr->events_config.delay_in_us = 0;

   capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, me_ptr->events_config.kpps);
   capi_result |=
      capi_cmn_update_bandwidth_event(&me_ptr->cb_info, me_ptr->events_config.code_bw, me_ptr->events_config.data_bw);
   capi_result |= capi_cmn_update_algo_delay_event(&me_ptr->cb_info, me_ptr->events_config.delay_in_us);
   capi_result |= capi_smart_sync_raise_process_event(me_ptr);
   return capi_result;
}

capi_err_t capi_smart_sync_circ_buf_adjust_for_overflow(capi_smart_sync_t *me_ptr, bool_t is_primary, uint32_t write_index)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_smart_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;

   /* If older input is dropped, adjust read marker in the circular buffer,
    * adjust output buffer time stamp. */
   if (in_port_ptr->circ_buf.actual_data_len_per_ch > in_port_ptr->circ_buf.max_data_len_per_ch)
   {
      uint64_t *FRACT_TIME_PTR_NULL = NULL;
      uint32_t  NUM_CH_1            = 1;
      uint32_t  bytes_overflow_per_ch =
         in_port_ptr->circ_buf.actual_data_len_per_ch - in_port_ptr->circ_buf.max_data_len_per_ch;
      uint32_t overflow_us = capi_cmn_bytes_to_us(bytes_overflow_per_ch,
                                                  in_port_ptr->media_fmt.format.sampling_rate,
                                                  in_port_ptr->media_fmt.format.bits_per_sample,
                                                  NUM_CH_1,
                                                  FRACT_TIME_PTR_NULL);

      in_port_ptr->circ_buf.actual_data_len_per_ch = in_port_ptr->circ_buf.max_data_len_per_ch;
      in_port_ptr->circ_buf.read_index             = write_index;

      in_port_ptr->cur_out_buf_timestamp_us += overflow_us;

#ifdef SMART_SYNC_DEBUG_HIGH
      AR_MSG(DBG_MED_PRIO,
             "Circular buffer overflowed, input idx %ld, overflow_per_ch %ld, overflow_us %ld, new out buf ts lsw %ld.",
             in_port_ptr->cmn.index,
             bytes_overflow_per_ch,
             overflow_us,
             (uint32_t)in_port_ptr->cur_out_buf_timestamp_us);
#endif
   }

   return capi_result;
}

static capi_err_t capi_smart_sync_drop_md(capi_smart_sync_t * me_ptr,
                                          capi_stream_data_t *input,
                                          bool_t *            is_eof_dropped_ptr)
{
   capi_stream_data_v2_t *in_stream_ptr = (capi_stream_data_v2_t *)input;
   // No metadata in stream
   if (in_stream_ptr && in_stream_ptr->metadata_list_ptr)
   {
      // Clear all buffered metadata in the internal input list.
      for (module_cmn_md_list_t *node_ptr = in_stream_ptr->metadata_list_ptr; node_ptr;)
      {
         bool_t                IS_DROPPED_TRUE = TRUE;
         module_cmn_md_list_t *next_ptr        = node_ptr->next_ptr;
         if (me_ptr->metadata_handler.metadata_destroy)
         {
            me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                      node_ptr,
                                                      IS_DROPPED_TRUE,
                                                      &in_stream_ptr->metadata_list_ptr);
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "capi smart sync:: Error: metadata handler not provided, can't drop metadata.");
            return CAPI_EFAILED;
         }
         node_ptr = next_ptr;
      }
      if (in_stream_ptr->flags.end_of_frame && is_eof_dropped_ptr)
      {
         *is_eof_dropped_ptr |= TRUE;
      }
      in_stream_ptr->flags.end_of_frame = FALSE;
      in_stream_ptr->flags.marker_eos   = FALSE;
   }
   return CAPI_EOK;
}

/**
 Buffers all data from input into the circular buffer. If it overflows the buffer size, drop older data.
 */
capi_err_t capi_smart_sync_buffer_new_data(capi_smart_sync_t * me_ptr,
                                           capi_stream_data_t *input[],
                                           bool_t              is_primary,
                                           bool_t *            is_eof_dropped_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t in_index = is_primary ? me_ptr->primary_in_port_info.cmn.index : me_ptr->secondary_in_port_info.cmn.index;
   capi_smart_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;

   if (SMART_SYNC_PORT_INDEX_INVALID == in_index ||
       !capi_smart_sync_media_fmt_is_valid(me_ptr, is_primary)) // port is not opened or media format is invalid
   {
      return CAPI_EOK;
   }

   if (!in_port_ptr->circ_buf.bufs_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: internal circular buffer for proc samples not allocated! ");
      return CAPI_EFAILED;
   }

   if (!(input && input[in_index]->buf_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: input not present");
      return CAPI_EFAILED;
   }

   // Nothing to buffer.
   if (0 == input[in_index]->buf_ptr[0].actual_data_len)
   {
      if (input[in_index]->flags.end_of_frame)
      {
         AR_MSG(DBG_HIGH_PRIO, "capi smart sync: dropping metadata from in_index %lu as eof received.", in_index);
         capi_smart_sync_drop_md(me_ptr, input[in_index], is_eof_dropped_ptr);
      }
      return capi_result;
   }

   uint32_t write_index         = in_port_ptr->circ_buf.write_index;
   uint32_t bytes_copied_per_ch = 0;

   // Copy data from input to circ buffer.
   for (uint32_t ch = 0; ch < in_port_ptr->media_fmt.format.num_channels; ch++)
   {
      bytes_copied_per_ch     = 0;
      uint32_t bytes_to_copy  = 0;
      uint32_t circ_buf_size  = in_port_ptr->circ_buf.max_data_len_per_ch;
      uint32_t rem_lin_size   = 0;
      uint32_t input_buf_size = input[in_index]->buf_ptr[ch].actual_data_len;
      write_index             = in_port_ptr->circ_buf.write_index;

      int8_t *circ_buf_start_addr = in_port_ptr->circ_buf.bufs_ptr + (ch * in_port_ptr->circ_buf.max_data_len_per_ch);
      int8_t *write_ptr           = NULL;
      int8_t *read_ptr            = NULL;

      while (input_buf_size > 0)
      {
         write_ptr = (int8_t *)(circ_buf_start_addr + write_index);

         rem_lin_size = circ_buf_size - write_index;

         bytes_to_copy = MIN(input_buf_size, rem_lin_size);

         read_ptr = input[in_index]->buf_ptr[ch].data_ptr + bytes_copied_per_ch;

         memscpy(write_ptr, bytes_to_copy, read_ptr, bytes_to_copy);

         input_buf_size -= bytes_to_copy;

         bytes_copied_per_ch += bytes_to_copy;

         write_index += bytes_to_copy;

         if (write_index >= circ_buf_size)
         {
            write_index = 0;
         }
      }
   }

   in_port_ptr->circ_buf.write_index = write_index;
   in_port_ptr->circ_buf.actual_data_len_per_ch += bytes_copied_per_ch;

   capi_result |= capi_smart_sync_circ_buf_adjust_for_overflow(me_ptr, is_primary, write_index);

#ifdef SMART_SYNC_DEBUG_HIGH
   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: after buffering new data on input idx %ld, actual_data_len_per_ch: %d, "
          "max_data_len_per_ch: %d ",
          in_port_ptr->cmn.index,
          in_port_ptr->circ_buf.actual_data_len_per_ch,
          in_port_ptr->circ_buf.max_data_len_per_ch);
#endif

   //Drop metadata during buffering mode.
   capi_smart_sync_drop_md(me_ptr, input[in_index], is_eof_dropped_ptr);

   return capi_result;
}

capi_err_t capi_smart_sync_pad_initial_zeroes(capi_smart_sync_t *me_ptr,
                                              bool_t             is_primary,
                                              uint32_t           expected_in_actual_data_len_per_ch)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_smart_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;

   uint32_t num_bytes_zero_padded = expected_in_actual_data_len_per_ch - in_port_ptr->circ_buf.actual_data_len_per_ch;

   // Need to mark as TRUE even if no zeros are padded, since this is used to confirm that port is synchronized.
   in_port_ptr->zeros_were_padded = TRUE;

   if (0 == num_bytes_zero_padded)
   {
      AR_MSG(DBG_MED_PRIO,
             "capi smart sync: circ buf is full, not padding zeros, is primary %ld, proc_samples_us %ld ",
             is_primary,
             me_ptr->voice_proc_info.voice_proc_start_samples_us);
      return capi_result;
   }

   /* No need memset with zeroes for padding initial zeroes as zero memset is done after buffer allocation.
    * Adjusting read index of the circular buffer should be sufficient */

   /* Adjusting read index in the circular buffer */
   in_port_ptr->circ_buf.read_index             = in_port_ptr->circ_buf.write_index;
   in_port_ptr->circ_buf.actual_data_len_per_ch = expected_in_actual_data_len_per_ch;

   /* Adjust timestamp by subtracting duration of zeros. */
   uint32_t zeros_us = capi_cmn_bytes_to_us(num_bytes_zero_padded,
                                            in_port_ptr->media_fmt.format.sampling_rate,
                                            in_port_ptr->media_fmt.format.bits_per_sample,
                                            1,
                                            NULL);
   in_port_ptr->cur_out_buf_timestamp_us -= (int64_t)zeros_us;

   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: post zero padding output buffer timestamp msw: %ld, lsw: %ld, zeros bytes %ld, zeros_us: "
          "%ld, expected_in_actual_data_len_per_ch %ld, proc_samples_us %ld",
          (int32_t)(in_port_ptr->cur_out_buf_timestamp_us >> 32),
          (int32_t)(in_port_ptr->cur_out_buf_timestamp_us),
          num_bytes_zero_padded,
          zeros_us,
          expected_in_actual_data_len_per_ch,
          me_ptr->voice_proc_info.voice_proc_start_samples_us);

   return capi_result;
}

/*
 * Pushes one full frame of zeros to the secondary port output, without adjusting secondary port
 * circular buffer at all.
 */
capi_err_t capi_smart_sync_underrun_secondary_port(capi_smart_sync_t *me_ptr, capi_stream_data_t *output[])
{
   capi_err_t capi_result        = CAPI_EOK;
   bool_t     SECONDARY_PORT     = FALSE;
   uint32_t   out_index          = me_ptr->secondary_out_port_info.cmn.index;
   bool_t     is_secondary_valid = (out_index != SMART_SYNC_PORT_INDEX_INVALID) ? TRUE : FALSE;

   if ((!is_secondary_valid) || (CAPI_PORT_STATE_STARTED != me_ptr->secondary_in_port_info.cmn.state) ||
       !capi_smart_sync_media_fmt_is_valid(me_ptr, SECONDARY_PORT))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "capi smart sync underrun(): not underruning on secondary port, idx valid %ld, port state 0x%lx, mf valid "
             "%ld",
             is_secondary_valid,
             me_ptr->secondary_in_port_info.cmn.state,
             capi_smart_sync_media_fmt_is_valid(me_ptr, SECONDARY_PORT));

      return capi_result;
   }

   /* Validate that port buffers are present. */
   if ((!(output[out_index] && output[out_index]->buf_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync underrun(): output buffers not present output idx: %ld, out ptr 0x%lx.",
             out_index,
             output[out_index] ? output[out_index]->buf_ptr : 0);
      return CAPI_EFAILED;
   }

   uint32_t underrun_bytes_per_ch = MIN(me_ptr->threshold_bytes_per_ch, output[out_index]->buf_ptr[0].max_data_len);

   if (underrun_bytes_per_ch < me_ptr->threshold_bytes_per_ch)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync underrun(): warning: less than threshold data provided (thresh %ld, output max data len "
             "%ld).",
             me_ptr->threshold_bytes_per_ch,
             output[out_index]->buf_ptr[0].max_data_len);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: underruning %ld zeros bytes per ch for secondary port",
          me_ptr->threshold_bytes_per_ch);

   uint32_t num_channels = me_ptr->secondary_in_port_info.media_fmt.format.num_channels;
   for (uint32_t ch = 0; ch < num_channels; ch++)
   {
      memset(output[out_index]->buf_ptr[ch].data_ptr, 0, underrun_bytes_per_ch);
      output[out_index]->buf_ptr[ch].actual_data_len = underrun_bytes_per_ch;
   }

   output[out_index]->flags.is_timestamp_valid = FALSE;
   return capi_result;
}

/**
 * This function is used to move data from the circular buffer to the output.
 * TODO(claguna): Metadata propagation
 */
capi_err_t capi_smart_sync_output_buffered_data(capi_smart_sync_t * me_ptr,
                                                capi_stream_data_t *output[],
                                                bool_t              is_primary)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t out_index =
      is_primary ? me_ptr->primary_out_port_info.cmn.index : me_ptr->secondary_out_port_info.cmn.index;
   capi_smart_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;

   if (SMART_SYNC_PORT_INDEX_INVALID == out_index) // port is not opened.
   {
      return CAPI_EOK;
   }

   if (!(output[out_index] && output[out_index]->buf_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: output not present");
      return CAPI_EFAILED;
   }

   if (!in_port_ptr->circ_buf.bufs_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: internal circular buffer for proc samples not allocated! ");
      return CAPI_EFAILED;
   }

   uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;
   uint32_t total_copy_size =
      MIN(output[out_index]->buf_ptr[0].max_data_len, in_port_ptr->circ_buf.actual_data_len_per_ch);
   uint32_t circ_buf_size    = in_port_ptr->circ_buf.max_data_len_per_ch;
   uint32_t read_index       = in_port_ptr->circ_buf.read_index;
   uint32_t num_bytes_copied = 0, rem_lin_size, bytes_to_copy;
   int8_t * write_ptr = NULL, *circ_buf_start_addr = NULL, *read_ptr = NULL;

#ifdef SMART_SYNC_DEBUG_HIGH
   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: total_copy_size: %u, read_index: %u", total_copy_size, read_index);

   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: output buf max_data_len: %u, actual_data_len: %u",
          output[out_index]->buf_ptr[0].max_data_len,
          output[out_index]->buf_ptr[0].actual_data_len);
#endif

   // For voice cases it is expected that there will be no stale data, so if this function is called we should be
   // guaranteed that all data will be able to be output. Process logic checks ensure input is the right length before
   // calling the function. Note that this is not called during steady state - during steady state input is passed to
   // output without going into the circular buffer.
   if (total_copy_size < me_ptr->threshold_bytes_per_ch)
   {
#ifdef SMART_SYNC_DEBUG_HIGH
      AR_MSG(DBG_MED_PRIO,
             "Unable to generate threshold amount of output. is_primary %ld, output_max_data_len %ld, input "
             "actual_data_len %ld, threshold %ld.",
             is_primary,
             output[out_index]->buf_ptr[0].max_data_len,
             in_port_ptr->circ_buf.actual_data_len_per_ch,
             me_ptr->threshold_bytes_per_ch);
#endif
      return CAPI_EFAILED;
   }
   else
   {
      total_copy_size = me_ptr->threshold_bytes_per_ch;
   }

   // Copy data from circ buffer to output
   for (uint32_t ch = 0; ch < num_channels; ch++)
   {
      circ_buf_start_addr = in_port_ptr->circ_buf.bufs_ptr + (ch * in_port_ptr->circ_buf.max_data_len_per_ch);
      write_ptr           = output[out_index]->buf_ptr[ch].data_ptr + output[out_index]->buf_ptr[ch].actual_data_len;
      num_bytes_copied    = 0;
      // Re-init circular read buffer index for each channel
      read_index          = in_port_ptr->circ_buf.read_index;

      while (num_bytes_copied < total_copy_size)
      {
         read_ptr = (int8_t *)(circ_buf_start_addr + read_index);

         write_ptr += num_bytes_copied;

         rem_lin_size = circ_buf_size - read_index;

         bytes_to_copy = MIN((total_copy_size - num_bytes_copied), rem_lin_size);

         memscpy(write_ptr, bytes_to_copy, read_ptr, bytes_to_copy);

         num_bytes_copied += bytes_to_copy;

         read_index += bytes_to_copy;

         if (read_index == circ_buf_size)
         {
            read_index = 0;
         }
      }

      output[out_index]->buf_ptr[ch].actual_data_len = num_bytes_copied;
   }

   in_port_ptr->circ_buf.read_index = read_index;
   in_port_ptr->circ_buf.actual_data_len_per_ch -= num_bytes_copied;

   if (!me_ptr->out_to_drop_from_next_vfr_cycle && is_primary)
   {
      // do not update these if this buffer is going to be dropped for resync.
      me_ptr->num_bytes_copied_to_output += num_bytes_copied;
      me_ptr->out_generated_this_vfr_cycle += num_bytes_copied;
      capi_result |= capi_smart_sync_check_send_packet_token(me_ptr, output[out_index]);
   }

   /* update time stamp */
   output[out_index]->flags.is_timestamp_valid = TRUE;
   output[out_index]->timestamp                = in_port_ptr->cur_out_buf_timestamp_us;
   in_port_ptr->cur_out_buf_timestamp_us += me_ptr->threshold_us;

#ifdef SMART_SYNC_DEBUG_HIGH
   AR_MSG(DBG_HIGH_PRIO, "capi smart sync: num_bytes_copied to output buf: %u", num_bytes_copied);

   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: output buffer timestamp msw: %d, lsw: %d ",
          (int32_t)(output[out_index]->timestamp >> 32),
          (int32_t)(output[out_index]->timestamp));
#endif

   /* TODO(CG): undo this after fixing overrun issue during voice call start and time stamp discontinuity
    * issues during resync */
   output[out_index]->flags.is_timestamp_valid = FALSE;
   return capi_result;
}

/**
 * Pass through data from passed in input index to passed in output index.
 */
capi_err_t capi_smart_sync_pass_through_data(capi_smart_sync_t * me_ptr,
                                             capi_stream_data_t *input[],
                                             capi_stream_data_t *output[],
                                             bool_t              is_primary,
                                             bool_t *            is_eof_propagated_ptr)
{
   capi_err_t capi_result          = CAPI_EOK;
   uint32_t   bytes_to_copy_per_ch = 0;
   uint32_t   bytes_copied_per_ch  = 0;
   uint32_t   in_index = is_primary ? me_ptr->primary_in_port_info.cmn.index : me_ptr->secondary_in_port_info.cmn.index;
   uint32_t   out_index =
      is_primary ? me_ptr->primary_out_port_info.cmn.index : me_ptr->secondary_out_port_info.cmn.index;

   capi_smart_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;

   uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;

   /* Validate that port buffers are present. */
   if ((!(input[in_index] && input[in_index]->buf_ptr)) || (!(output[out_index] && output[out_index]->buf_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync process(): input or output buffers not present input idx: %ld, in ptr: 0x%lx, "
             "output idx: %ld, out ptr 0x%lx.",
             in_index,
             input[in_index]->buf_ptr,
             out_index,
             output[out_index]->buf_ptr);
      return CAPI_EFAILED;
   }

   bytes_to_copy_per_ch = MIN(input[in_index]->buf_ptr[0].actual_data_len, output[out_index]->buf_ptr[0].max_data_len);

   if (bytes_to_copy_per_ch < me_ptr->threshold_bytes_per_ch)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync process(): less than threshold data provided during pass_through input idx: %ld, "
             "in actual data len %ld, "
             "output idx: %ld, out max data len %ld. Continuing to pass through existing data.",
             in_index,
             input[in_index]->buf_ptr[0].actual_data_len,
             out_index,
             output[out_index]->buf_ptr[0].max_data_len);
   }
   else
   {
      bytes_to_copy_per_ch = me_ptr->threshold_bytes_per_ch;
   }

   uint32_t bytes_before = input[in_index]->buf_ptr[0].actual_data_len;
   for (uint32_t ch = 0; ch < num_channels; ch++)
   {
      bytes_copied_per_ch = memscpy(output[out_index]->buf_ptr[ch].data_ptr,
                                    output[out_index]->buf_ptr[ch].max_data_len,
                                    input[in_index]->buf_ptr[ch].data_ptr,
                                    bytes_to_copy_per_ch);

      output[out_index]->buf_ptr[ch].actual_data_len = bytes_copied_per_ch;
      input[in_index]->buf_ptr[ch].actual_data_len   = bytes_copied_per_ch;
   }

   /* usually less data arrives in case of EOS. We can skip appending zeros as priority sync will do that anyway.
    * But here we are intentionally appending zeros to meet threshold (instead of relying on priority sync) so that
    * "out_generated_this_vfr_cycle" is updated correctly. "out_generated_this_vfr_cycle" should be updated because
    * this helps us to identify the VFR boundary.
    */
   if (bytes_copied_per_ch < me_ptr->threshold_bytes_per_ch)
   {
      uint32_t zeros_to_append =
         MIN(me_ptr->threshold_bytes_per_ch - bytes_copied_per_ch,
             output[out_index]->buf_ptr[0].max_data_len - output[out_index]->buf_ptr[0].actual_data_len);

      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync process(): appending zeros output idx: %ld, bytes_per_ch %lu.",
             out_index,
             zeros_to_append);

      for (uint32_t ch = 0; ch < num_channels; ch++)
      {
         memset(output[out_index]->buf_ptr[ch].data_ptr + bytes_copied_per_ch, 0, zeros_to_append);

         output[out_index]->buf_ptr[ch].actual_data_len += zeros_to_append;
      }
      bytes_copied_per_ch += zeros_to_append;
   }

   capi_result |=
      capi_smart_sync_propagate_metadata(me_ptr, input[in_index], output[out_index], in_port_ptr, bytes_before);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Failed to propagate metadata");
      return capi_result;
   }

   /* check and update trigger policy and send voice packet token for primary path. */
   if (is_primary)
   {
      me_ptr->num_bytes_copied_to_output   += bytes_copied_per_ch;
      me_ptr->out_generated_this_vfr_cycle += bytes_copied_per_ch;

      capi_result |= capi_smart_sync_check_send_packet_token(me_ptr, output[out_index]);
   }

   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: Passing through data for steady state. For input idx %ld, Consumed/generated %lu bytes. "
          "out_generated_this_vfr_cycle %lu (pri only), out_required_per_vfr_cycle %lu (pri only)",
          in_index,
          bytes_to_copy_per_ch,
          me_ptr->out_generated_this_vfr_cycle,
          me_ptr->out_required_per_vfr_cycle);

   // Copy flags/timestamp through correct port.
   output[out_index]->flags            = input[in_index]->flags;
   input[in_index]->flags.end_of_frame = FALSE;

   /* update time stamp */
   if (in_port_ptr->is_ts_valid)
   {
      output[out_index]->flags.is_timestamp_valid = TRUE;

      AR_MSG(DBG_MED_PRIO,
             "claguna debug capi smart sync: calculated out timestamp msw: %d, lsw: %d, input timestamp msw %d, lsw: "
             "%d",
             (int32_t)(in_port_ptr->cur_out_buf_timestamp_us >> 32),
             (int32_t)(in_port_ptr->cur_out_buf_timestamp_us),
             (int32_t)(input[in_index]->timestamp >> 32),
             (int32_t)(input[in_index]->timestamp));

      if (input[in_index]->flags.is_timestamp_valid)
      {
         output[out_index]->timestamp = input[in_index]->timestamp;

         // Always update our cached timestamp to be the current timestamp for most accuracy.
         in_port_ptr->cur_out_buf_timestamp_us = output[out_index]->timestamp;

         AR_MSG(DBG_MED_PRIO,
                "claguna debug capi smart sync: output buffer timestamp from input - msw: %d, lsw: %d ",
                (int32_t)(output[out_index]->timestamp >> 32),
                (int32_t)(output[out_index]->timestamp));
      }
      else
      {
         // In case of EOF + no data, we still need to send a valid timestamp - used the cached timestamp in this case.
         output[out_index]->timestamp = in_port_ptr->cur_out_buf_timestamp_us;

         AR_MSG(DBG_MED_PRIO,
                "capi smart sync: output buffer timestamp from calculated - msw: %d, lsw: %d ",
                (int32_t)(output[out_index]->timestamp >> 32),
                (int32_t)(output[out_index]->timestamp));
      }

      // Extrapolating the cached timestamp to tne next frame.
      in_port_ptr->cur_out_buf_timestamp_us += me_ptr->threshold_us;
   }

   if (input[in_index]->flags.is_timestamp_valid)
   {
      output[out_index]->timestamp = input[in_index]->timestamp;
   }

   /* TODO(CG): undo this after fixing overrun issue during voice call start and time stamp discontinuity
    * issues during resync */
   output[out_index]->flags.is_timestamp_valid = FALSE;

   if (output[out_index]->flags.end_of_frame && is_eof_propagated_ptr)
   {
      *is_eof_propagated_ptr |= TRUE;
   }

   return capi_result;
}

capi_err_t capi_smart_sync_subscribe_to_voice_timer(capi_smart_sync_t *me_ptr)
{
   capi_err_t                   capi_result = CAPI_EOK;
   voice_timer_subscribe_info_t sub_info={0};

   sub_info.avtimer_timestamp_us_pptr = &(me_ptr->vfr_timestamp_us_ptr);
   sub_info.client_id                 = me_ptr->module_instance_id;
   sub_info.direction                 = TX_DIR;
   sub_info.offset_us                 = me_ptr->voice_proc_info.voice_proc_start_offset_us;
   sub_info.resync_status_pptr        = &(me_ptr->resync_status_ptr);
   sub_info.resync_signal_ptr         = (posal_signal_t)(me_ptr->voice_resync_signal_ptr);
   sub_info.first_vfr_occurred_pptr   = &(me_ptr->first_vfr_occurred_ptr);
   sub_info.signal_ptr                = (posal_signal_t)(me_ptr->voice_proc_start_signal_ptr);
   sub_info.vfr_cycle_us              = (me_ptr->voice_proc_info.vfr_cycle_duration_ms * 1000);
   sub_info.vfr_mode                  = me_ptr->voice_proc_info.vfr_mode;
   sub_info.vsid                      = me_ptr->voice_proc_info.vsid;
   sub_info.intr_counter_ptr          = NULL;
   sub_info.abs_vfr_timestamp         = 0;

   if (CAPI_EOK != (capi_result = voice_timer_wrapper_subscribe(&sub_info)))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: voice timer subscription failed with %d", capi_result);
      return CAPI_EFAILED;
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO, "capi smart sync: subscribed to voice timer");
   }

   return capi_result;
}

capi_err_t capi_smart_sync_unsubscribe_to_voice_timer(capi_smart_sync_t *me_ptr)
{
   capi_err_t                     capi_result = CAPI_EOK;
   voice_timer_unsubscribe_info_t payload;

   payload.client_id = me_ptr->module_instance_id;
   payload.direction = TX_DIR;
   payload.vfr_mode  = me_ptr->voice_proc_info.vfr_mode;
   payload.vsid      = me_ptr->voice_proc_info.vsid;

   if (CAPI_EOK != (capi_result = voice_timer_wrapper_unsubscribe(&payload)))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: voice timer subscription failed, result: 0x%lx", capi_result);
   }

   posal_signal_clear((posal_signal_t)me_ptr->voice_proc_start_signal_ptr);
   posal_signal_clear((posal_signal_t)me_ptr->voice_resync_signal_ptr);

   return capi_result;
}

/**
 * Helper function to free proc samples circular buffer buffer.
 */
void capi_smart_sync_deallocate_internal_circ_buf(capi_smart_sync_t *me_ptr, capi_smart_sync_in_port_t *in_port_ptr)
{
   if (in_port_ptr->circ_buf.bufs_ptr)
   {
      posal_memory_free(in_port_ptr->circ_buf.bufs_ptr);
      in_port_ptr->circ_buf.bufs_ptr = NULL;
   }
}

/**
 * Helper function to allocate proc samples cicular buffer buffer.
 */
capi_err_t capi_smart_sync_allocate_internal_circ_buf(capi_smart_sync_t *me_ptr, capi_smart_sync_in_port_t *in_port_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;

   in_port_ptr->circ_buf.max_data_len_per_ch =
      capi_cmn_us_to_bytes_per_ch(me_ptr->voice_proc_info.voice_proc_start_samples_us,
                                  in_port_ptr->media_fmt.format.sampling_rate,
                                  in_port_ptr->media_fmt.format.bits_per_sample);

   AR_MSG(DBG_HIGH_PRIO,
          "capi smart sync: max_data_len_per_ch: %u, voice_proc_start_samples_us: %u, sampling_rate: %u, "
          "bits_per_sample: %u",
          in_port_ptr->circ_buf.max_data_len_per_ch,
          me_ptr->voice_proc_info.voice_proc_start_samples_us,
          in_port_ptr->media_fmt.format.sampling_rate,
          in_port_ptr->media_fmt.format.bits_per_sample);

   // If it already exists, free the buffer.
   if (in_port_ptr->circ_buf.bufs_ptr)
   {
      capi_smart_sync_deallocate_internal_circ_buf(me_ptr, in_port_ptr);
   }

   // Allocate and zero memory.
   uint32_t mem_size = in_port_ptr->circ_buf.max_data_len_per_ch * num_channels;

   in_port_ptr->circ_buf.bufs_ptr = (int8_t *)posal_memory_malloc(mem_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
   if (NULL == in_port_ptr->circ_buf.bufs_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Couldn't allocate memory for proc samples buffer.");
      return CAPI_ENOMEMORY;
   }

   memset(in_port_ptr->circ_buf.bufs_ptr, 0, mem_size);
   in_port_ptr->circ_buf.read_index  = 0;
   in_port_ptr->circ_buf.write_index = 0;

   return capi_result;
}

bool_t capi_smart_sync_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr)
{
   if (CAPI_FIXED_POINT != format_ptr->header.format_header.data_format)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: unsupported data format %lu",
             (uint32_t)format_ptr->header.format_header.data_format);
      return FALSE;
   }

   if ((16 != format_ptr->format.bits_per_sample) && (32 != format_ptr->format.bits_per_sample))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: only supports 16 and 32 bit data. Received %lu.",
             format_ptr->format.bits_per_sample);
      return FALSE;
   }

   if ((format_ptr->format.data_interleaving != CAPI_DEINTERLEAVED_UNPACKED) && (format_ptr->format.num_channels != 1))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync : Interleaved data not supported.");
      return FALSE;
   }

   if (!format_ptr->format.data_is_signed)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Unsigned data not supported.");
      return FALSE;
   }

   if ((format_ptr->format.num_channels == 0) || (format_ptr->format.num_channels > SMART_SYNC_MAX_CHANNELS))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: unsupported number of channels. "
             "Received %lu.",
             format_ptr->format.num_channels);
      return FALSE;
   }

   return TRUE;
}

static inline capi_smart_sync_cmn_port_t *capi_smart_sync_get_port_cmn(capi_smart_sync_t *me_ptr,
                                                                       bool_t             is_primary,
                                                                       bool_t             is_input)
{
   capi_smart_sync_cmn_port_t *port_cmn_ptr = NULL;
   if (is_primary)
   {
      if (is_input)
      {
         port_cmn_ptr = &me_ptr->primary_in_port_info.cmn;
      }
      else
      {
         port_cmn_ptr = &me_ptr->primary_out_port_info.cmn;
      }
   }
   else
   {
      if (is_input)
      {
         port_cmn_ptr = &me_ptr->secondary_in_port_info.cmn;
      }
      else
      {
         port_cmn_ptr = &me_ptr->secondary_out_port_info.cmn;
      }
   }
   return port_cmn_ptr;
}

/**
 * Handling for port open. Fields have already been validated.
 * Store the port index, move port state to STOPPED. Note that we allocate
 * the port buffer for both ports when proc samples is configured. Note that port info
 * exists regardless of whether ports have been opened since we expect both primary
 * and secondary to be opened at some point.
 */
static capi_err_t capi_smart_sync_port_open(capi_smart_sync_t *me_ptr,
                                            bool_t             is_primary,
                                            bool_t             is_input,
                                            uint32_t           port_index)
{
   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: handling port open, is_primary = %ld, is_input = %ld, port index = %ld",
          is_primary,
          is_input,
          port_index);

   capi_err_t                  capi_result  = CAPI_EOK;
   capi_smart_sync_cmn_port_t *port_cmn_ptr = capi_smart_sync_get_port_cmn(me_ptr, is_primary, is_input);

   // Check if already opened. This shouldn't happen.
   if (CAPI_PORT_STATE_CLOSED != port_cmn_ptr->state)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: Port already opened. is_primary %lu, is_input %lu",
             is_primary,
             is_input);
      return CAPI_EFAILED;
   }

   port_cmn_ptr->index = port_index;
   port_cmn_ptr->state = CAPI_PORT_STATE_STOPPED;

   return capi_result;
}

static capi_err_t capi_smart_sync_port_start(capi_smart_sync_t *me_ptr,
                                             bool_t             is_primary,
                                             bool_t             is_input,
                                             uint32_t           port_index)
{
   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: handling port start, is_primary = %ld, is_input = %ld, port index = %ld",
          is_primary,
          is_input,
          port_index);

   capi_err_t                  capi_result  = CAPI_EOK;
   capi_smart_sync_cmn_port_t *port_cmn_ptr = capi_smart_sync_get_port_cmn(me_ptr, is_primary, is_input);

   if (port_cmn_ptr->index != port_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: invalid port index. is_primary %lu, is_input %lu", is_primary, is_input);
      return CAPI_EFAILED;
   }

   if (CAPI_PORT_STATE_STARTED == port_cmn_ptr->state)
   {
      AR_MSG(DBG_LOW_PRIO, "capi smart sync: port already started. is_primary %lu, is_input %lu", is_primary, is_input);
      return CAPI_EOK;
   }

   port_cmn_ptr->state = CAPI_PORT_STATE_STARTED;

   /* Register with voice timer if not registered already */
   if (capi_smart_sync_can_subscribe_to_vt(me_ptr))
   {
      capi_result |= capi_smart_sync_subscribe_to_voice_timer(me_ptr);
   }

   /* Send TTR metadata for 2nd and 4th packets */
   me_ptr->skip_send_ttr_metadata = TRUE;

   return capi_result;
}

static capi_err_t capi_smart_sync_port_stop(capi_smart_sync_t *me_ptr,
                                            bool_t             is_primary,
                                            bool_t             is_input,
                                            uint32_t           port_index)
{
   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: handling port stop, is_primary = %ld, is_input = %ld, port index = %ld",
          is_primary,
          is_input,
          port_index);

   capi_err_t                  capi_result  = CAPI_EOK;
   capi_smart_sync_cmn_port_t *port_cmn_ptr = capi_smart_sync_get_port_cmn(me_ptr, is_primary, is_input);

   if (port_cmn_ptr->index != port_index)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: invalid port index. is_primary %lu, is_input %lu", is_primary, is_input);
      return CAPI_EFAILED;
   }

   if (CAPI_PORT_STATE_STOPPED == port_cmn_ptr->state)
   {
      AR_MSG(DBG_LOW_PRIO, "capi smart sync: port already stopped. is_primary %lu, is_input %lu", is_primary, is_input);
      return CAPI_EOK;
   }

   port_cmn_ptr->state = CAPI_PORT_STATE_STOPPED;

   if (is_primary)
   {
      capi_smart_sync_reset_module_state(me_ptr, TRUE);
   }

   return capi_result;
}

/**
 * Handling for port close. Fields have already been validated.
 * Invalidate the port index, deallocate port buffer, move port state to CLOSED.
 */
static capi_err_t capi_smart_sync_port_close(capi_smart_sync_t *me_ptr, bool_t is_primary, bool_t is_input)
{
   AR_MSG(DBG_MED_PRIO,
          "capi smart sync: handling port close, is_primary = %ld, is_input = %ld.",
          is_primary,
          is_input);

   capi_err_t                  capi_result  = CAPI_EOK;
   capi_smart_sync_cmn_port_t *port_cmn_ptr = capi_smart_sync_get_port_cmn(me_ptr, is_primary, is_input);

   // Check if already closed. This shouldn't happen.
   if (CAPI_PORT_STATE_CLOSED == port_cmn_ptr->state)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: Port already closed. is_primary %lu, is_input %lu",
             is_primary,
             is_input);
      return CAPI_EFAILED;
   }

   port_cmn_ptr->index = SMART_SYNC_PORT_INDEX_INVALID;
   port_cmn_ptr->state = CAPI_PORT_STATE_CLOSED;

   // Deallocate port buffer if it exists (only for input ports).
   if (is_input)
   {
      capi_smart_sync_deallocate_internal_circ_buf(me_ptr, (capi_smart_sync_in_port_t *)port_cmn_ptr);
   }

   return capi_result;
}

/**
 * Handles port operation set properties. Payload is validated and then each individual
 * operation is delegated to opcode-specific functions.
 */
capi_err_t capi_smart_sync_set_properties_port_op(capi_smart_sync_t *me_ptr, capi_buf_t *payload_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == payload_ptr->data_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Set property port operation, received null buffer");
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   // Validate length.
   if (sizeof(intf_extn_data_port_operation_t) > payload_ptr->actual_data_len)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: Set property for port operation, Bad param size %lu",
             payload_ptr->actual_data_len);
      CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
      return capi_result;
   }

   intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(payload_ptr->data_ptr);

   if ((sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t))) >
       payload_ptr->actual_data_len)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: Set property for port operation, Bad param size %lu",
             payload_ptr->actual_data_len);
      CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
      return capi_result;
   }

   uint32_t max_ports = data_ptr->is_input_port ? SMART_SYNC_MAX_IN_PORTS : SMART_SYNC_MAX_OUT_PORTS;
   if (max_ports < data_ptr->num_ports)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi smart sync: Invalid num ports. is_input: %lu, num_ports = %lu, max_input_ports = %lu, "
             "max_output_ports = %lu",
             data_ptr->is_input_port,
             data_ptr->num_ports,
             SMART_SYNC_MAX_IN_PORTS,
             SMART_SYNC_MAX_OUT_PORTS);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   // For each port in the operation payload.
   for (uint32_t iter = 0; iter < data_ptr->num_ports; iter++)
   {
      uint32_t primary_id = data_ptr->is_input_port ? SMART_SYNC_PRIMARY_IN_PORT_ID : SMART_SYNC_PRIMARY_OUT_PORT_ID;

      uint32_t secondary_id =
         data_ptr->is_input_port ? SMART_SYNC_SECONDARY_IN_PORT_ID : SMART_SYNC_SECONDARY_OUT_PORT_ID;

      uint32_t port_id    = data_ptr->id_idx[iter].port_id;
      uint32_t port_index = data_ptr->id_idx[iter].port_index;
      bool_t   is_primary = FALSE;

      // Validate port id and determine if primary or secondary operation.
      if (primary_id == port_id)
      {
         is_primary = TRUE;

         AR_MSG(DBG_LOW_PRIO,
                "Port operation 0x%x performed on primary port idx = %lu, id= %lu is_input_port = %lu",
                data_ptr->opcode,
                port_index,
                data_ptr->id_idx[iter].port_id,
                data_ptr->is_input_port);
      }
      else if (secondary_id == port_id)
      {
         is_primary = FALSE;

         AR_MSG(DBG_LOW_PRIO,
                "Port operation 0x%x performed on secondary port = idx %lu, id= %lu is_input_port = %lu",
                data_ptr->opcode,
                port_index,
                data_ptr->id_idx[iter].port_id,
                data_ptr->is_input_port);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO,
                "capi smart sync: unsupported port idx = %lu, id= %lu is_input_port = %lu. Only static ids for "
                "primary/secondary ports are "
                "supported.",
                port_index,
                data_ptr->id_idx[iter].port_id,
                data_ptr->is_input_port);
         CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         return capi_result;
      }

      // Validate port index doesn't go out of bounds.
      if (port_index >= max_ports)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "capi smart sync: Bad parameter in id-idx map on port %lu in payload, port_index = %lu, "
                "is_input = %lu, max in ports = %d, max out ports = %d",
                iter,
                port_index,
                data_ptr->is_input_port,
                SMART_SYNC_MAX_IN_PORTS,
                SMART_SYNC_MAX_OUT_PORTS);

         CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         return capi_result;
      }

      // Validate that id-index mapping matches what was previously sent, unless mapping doesn't exist yet.
      capi_smart_sync_cmn_port_t *port_cmn_ptr =
         capi_smart_sync_get_port_cmn(me_ptr, is_primary, data_ptr->is_input_port);
      uint32_t prev_index = port_cmn_ptr->index;
      if (SMART_SYNC_PORT_INDEX_INVALID != prev_index)
      {
         if (prev_index != port_index)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi smart sync: Error: id-idx mapping changed on port %lu in payload, port_index = %lu, "
                   "is_input = %lu",
                   iter,
                   port_index,
                   data_ptr->is_input_port);
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            return capi_result;
         }
      }

      switch ((uint32_t)data_ptr->opcode)
      {
         case INTF_EXTN_DATA_PORT_OPEN:
         {
            capi_result |= capi_smart_sync_port_open(me_ptr, is_primary, data_ptr->is_input_port, port_index);
            break;
         }
         case INTF_EXTN_DATA_PORT_START:
         {
            capi_result |= capi_smart_sync_port_start(me_ptr, is_primary, data_ptr->is_input_port, port_index);
            break;
         }
         case INTF_EXTN_DATA_PORT_STOP:
         {
            capi_result |= capi_smart_sync_port_stop(me_ptr, is_primary, data_ptr->is_input_port, port_index);
            break;
         }
         case INTF_EXTN_DATA_PORT_CLOSE:
         {
            capi_result |= capi_smart_sync_port_close(me_ptr, is_primary, data_ptr->is_input_port);
            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "capi smart sync: Port operation opcode %lu. Not supported.", data_ptr->opcode);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }
   }

   return capi_result;
}

/**
 * Initialize smart sync common port. Ports are closed until opened.
 */
capi_err_t capi_smart_sync_init_cmn_port(capi_smart_sync_t *me_ptr, capi_smart_sync_cmn_port_t *cmn_port_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == me_ptr || NULL == cmn_port_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Init received bad pointer, 0x%p, 0x%p", me_ptr, cmn_port_ptr);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   cmn_port_ptr->index = SMART_SYNC_PORT_INDEX_INVALID;
   cmn_port_ptr->state = CAPI_PORT_STATE_CLOSED;

   return capi_result;
}

static capi_err_t capi_smart_sync_propagate_metadata(capi_smart_sync_t *        me_ptr,
                                                     capi_stream_data_t *       input,
                                                     capi_stream_data_t *       output,
                                                     capi_smart_sync_in_port_t *in_port_ptr,
                                                     uint32_t                   bytes_before)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == me_ptr->metadata_handler.context_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: NULL metadata handler");
      return CAPI_EFAILED;
   }

   capi_stream_data_v2_t *in_stream_ptr  = (capi_stream_data_v2_t *)input;
   capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output;

   intf_extn_md_propagation_t input_md_info;
   intf_extn_md_propagation_t output_md_info;

   memset(&input_md_info, 0, sizeof(input_md_info));
   input_md_info.df                          = in_port_ptr->media_fmt.header.format_header.data_format;
   input_md_info.len_per_ch_in_bytes         = in_stream_ptr->buf_ptr[0].actual_data_len;
   input_md_info.initial_len_per_ch_in_bytes = bytes_before;
   input_md_info.bits_per_sample             = in_port_ptr->media_fmt.format.bits_per_sample;
   input_md_info.sample_rate                 = in_port_ptr->media_fmt.format.sampling_rate;

   memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
   output_md_info.len_per_ch_in_bytes         = out_stream_ptr->buf_ptr[0].actual_data_len;
   output_md_info.initial_len_per_ch_in_bytes = 0;
   result |= me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                         in_stream_ptr,
                                                         out_stream_ptr,
                                                         NULL, // internal_list_pptr
                                                         0,    // algo delay
                                                         &input_md_info,
                                                         &output_md_info);

   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: Failed to propagate metadata with result 0x%x", result);
      return result;
   }

   return result;
}

static capi_err_t capi_smart_sync_send_ttr_metadata(capi_smart_sync_t *me_ptr, capi_stream_data_t *output)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == me_ptr->metadata_handler.context_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: NULL metadata handler");
      return CAPI_EFAILED;
   }

   capi_stream_data_v2_t *out_stream_ptr = (capi_stream_data_v2_t *)output;

   module_cmn_md_t *new_md_ptr = NULL;

   result = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                     &(out_stream_ptr->metadata_list_ptr),
                                                     sizeof(md_ttr_t),
                                                     me_ptr->heap_info,
                                                     FALSE,
                                                     &new_md_ptr);
   if (CAPI_FAILED(result))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi smart sync: creating metadata failed %lx", result);
      return AR_EOK;
   }

   new_md_ptr->metadata_id                          = MD_ID_TTR;
   new_md_ptr->offset                               = 0;
   new_md_ptr->metadata_flag.buf_sample_association = MODULE_CMN_MD_BUFFER_ASSOCIATED;
   new_md_ptr->metadata_flag.is_begin_associated_md = TRUE;

   // TTR is at proc tick + voice path delay.
   md_ttr_t *ttr_ptr = (md_ttr_t *)&(new_md_ptr->metadata_buf);
   ttr_ptr->ttr      = (me_ptr->vfr_timestamp_at_cur_proc_tick) + (me_ptr->voice_proc_info.path_delay_us) +
                  (me_ptr->voice_proc_info.voice_proc_start_offset_us);
   ttr_ptr->packet_token = (ttr_packet_token_t)(me_ptr->current_packet_token);

#ifdef SMART_SYNC_DEBUG_LOW
   AR_MSG(DBG_LOW_PRIO,
          "capi smart sync: TTD metadata created. TTR: %d, resync_flag[%d], token [%d]",
          ttr_ptr->ttr,
          ttr_ptr->resync,
          ttr_ptr->packet_token);
#endif

   return result;
}
