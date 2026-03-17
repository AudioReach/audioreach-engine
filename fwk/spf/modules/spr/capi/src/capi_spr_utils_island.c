/*==========================================================================
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 *   \file capi_spr_utils.c
 *   \brief
 *        This file contains CAPI implementation of Splitter Renderer Module utilities
 */

#include "capi_spr_i.h"

/*==============================================================================
   Local Definitions
==============================================================================*/

/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Function Implementation
==============================================================================*/

#ifdef DEBUG_SPR_MODULE
/*------------------------------------------------------------------------------
  Function name: capi_spr_print_media_fmt
    Debug utility to print the media format from the SPR module
* ------------------------------------------------------------------------------*/
void capi_spr_print_media_fmt(capi_spr_t *me_ptr, capi_media_fmt_v2_t *media_fmt_ptr)
{
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_MED_PRIO,
           "media format: data_format %lu, bitstream format 0x%lX , ch=%lu, SR=%lu, bit_width=%lu, "
           "Q_fct=%lu, is_signed %lu",
           media_fmt_ptr->header.format_header.data_format,
           media_fmt_ptr->format.bitstream_format,
           media_fmt_ptr->format.num_channels,
           media_fmt_ptr->format.sampling_rate,
           media_fmt_ptr->format.bits_per_sample,
           media_fmt_ptr->format.q_factor,
           media_fmt_ptr->format.data_is_signed);
}
#endif

/*------------------------------------------------------------------------------
  Function name: spr_does_strm_reader_have_data
   Query if any of the stream reader has any unread bytes left
* ------------------------------------------------------------------------------*/
capi_err_t spr_does_strm_reader_have_data(capi_spr_t *me_ptr, bool_t *has_data_ptr)
{
   if (!me_ptr || !has_data_ptr)
   {
      return CAPI_EBADPARAM;
   }

   *has_data_ptr = FALSE;

   uint32_t num_unread_bytes = 0;

   for (uint32_t outport_index = 0; outport_index < me_ptr->max_output_ports; outport_index++)
   {
      uint32_t arr_index           = spr_get_arr_index_from_port_index(me_ptr, outport_index, FALSE);
      uint32_t output_unread_bytes = 0;
      if (IS_INVALID_PORT_INDEX(arr_index))
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "warning: Output index is unknown, port_index=%u", outport_index);
         continue;
      }

      spr_output_port_info_t *out_port_ptr = &me_ptr->out_port_info_arr[arr_index];

      // If stream reader exists and the port is in active state, look for unread bytes
      if (out_port_ptr->strm_reader_ptr && (out_port_ptr->port_state & (DATA_PORT_STATE_STARTED)))
      {
         if (AR_DID_FAIL(spr_stream_reader_get_unread_bytes(out_port_ptr->strm_reader_ptr, &output_unread_bytes)))
         {
            continue;
         }
         else
         {
            if (output_unread_bytes)
            {
               *has_data_ptr = TRUE;
               SPR_MSG_ISLAND(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "port_index %d, output_unread_bytes = %d, num_unread_bytes = %d",
                       me_ptr->out_port_info_arr[arr_index].port_index,
                       output_unread_bytes,
                       num_unread_bytes);
               num_unread_bytes = output_unread_bytes;
            }
         }
      }
   }

   return CAPI_EOK;
}

/*==============================================================================
   Function : capi_spr_set_up_output

   output port must be created at open or at input media format.
   this is the only way to ensure that before start media format can propagate (at prepare).
   media format propagation is essential for determining path-delay before start.
==============================================================================*/
capi_err_t capi_spr_set_up_output(capi_spr_t *me_ptr, uint32_t outport_index, bool_t need_to_reinitialize)
{
   capi_err_t result = CAPI_EOK;

   if (!me_ptr->flags.is_input_media_fmt_set)
   {
      return result;
   }

   uint32_t arr_index = spr_get_arr_index_from_port_index(me_ptr, outport_index, FALSE);

   if (IS_INVALID_PORT_INDEX(arr_index))
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "warning: Output index is unknown, port_index=%u", outport_index);
      return CAPI_EOK;
   }

   if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_info_arr[arr_index].port_state)
   {
      if (CAPI_EOK == capi_spr_check_and_init_output_port(me_ptr, arr_index, need_to_reinitialize))
      {
         capi_spr_check_and_raise_output_media_format_event(me_ptr, arr_index);
      }
   }
   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_check_and_init_input_port
   Check if media format and port config is received and then allocate stream
   writer for the particular index.
 * ------------------------------------------------------------------------------*/
capi_err_t capi_check_and_init_input_port(capi_spr_t *me_ptr, uint32_t arr_index, bool_t need_to_reinitialize)
{
   // if need to reinit flag is set, destory the existing handle and intialize again.
   if (need_to_reinitialize && me_ptr->in_port_info_arr[arr_index].strm_writer_ptr)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "Port is being reset. input port %lu", arr_index);
      spr_stream_writer_destroy(me_ptr->drv_ptr, &me_ptr->in_port_info_arr[arr_index].strm_writer_ptr);
   }

   if ((NULL == me_ptr->in_port_info_arr[arr_index].strm_writer_ptr) && (TRUE == me_ptr->flags.is_input_media_fmt_set) &&
       (DATA_PORT_STATE_CLOSED != me_ptr->in_port_info_arr[arr_index].port_state))
   {
      // Convert us to bytes.
      uint32_t pre_buffering_delay_in_bytes = 0;

      // Create unique channel id for each channel using input port index and channel map.
      uint32_t channel_ids[MAX_CHANNELS_PER_STREAM];
      for (uint32_t i = 0; i < me_ptr->operating_mf.format.num_channels; i++)
      {
         channel_ids[i] = me_ptr->operating_mf.channel_type[i];
      }

      uint32_t size = capi_cmn_us_to_bytes_per_ch(me_ptr->frame_dur_us,
                                                  me_ptr->operating_mf.format.sampling_rate,
                                                  me_ptr->operating_mf.format.bits_per_sample);
      ar_result_t result = spr_stream_writer_create(me_ptr->drv_ptr,
                                                    size, // writers base buffer size
                                                    pre_buffering_delay_in_bytes,
                                                    me_ptr->operating_mf.format.num_channels,
                                                    channel_ids,
                                                    &me_ptr->in_port_info_arr[arr_index].strm_writer_ptr);
      if (AR_EOK != result)
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Input port can't be initiated. port_index =%d, result %d",
                 arr_index,
                 result);
         return CAPI_EFAILED;
      }
      else
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_MED_PRIO,
                 "Input port idx[%lu] stream writer initialized successfully.size%lu",
                 arr_index,
                 size);
      }
   }
   else
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Input port not ready to initialize. port_index =%d.", arr_index);
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: spr_has_old_mf_data_pending
   Checks if the old media format has any data pending in the cached list
* ------------------------------------------------------------------------------*/
bool_t spr_has_old_mf_data_pending(capi_spr_t *me_ptr)
{
   if (spr_has_cached_mf(me_ptr))
   {
      spr_mf_handler_t *mf_handler_ptr =
         (spr_mf_handler_t *)capi_spr_get_list_head_obj_ptr(me_ptr->in_port_info_arr->mf_handler_list_ptr);

      // If there is any stream left in the applied media format list, drain that first
      if (mf_handler_ptr && mf_handler_ptr->is_applied && mf_handler_ptr->int_buf.buf_list_ptr)
      {
         return TRUE;
      }
   }
   return FALSE;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_reinit_ports
   During a media format change, validate if the ports can be re-init. If so,
   proceed right away. Else, cache the media format & wait to drain any pending
   data
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_check_reinit_ports(capi_spr_t *         me_ptr,
                                              capi_media_fmt_v2_t *media_fmt_ptr,
                                              bool_t               reinit_spr_ports,
                                              bool_t               check_cache_mf)
{
   capi_err_t  result       = CAPI_EOK;
   ar_result_t ar_result    = AR_EOK;
   uint32_t    in_arr_index = 0;

   // No data is pending, re-init right away
   if (reinit_spr_ports)
   {
      if (!me_ptr->flags.is_input_media_fmt_set)
      {
         me_ptr->flags.is_input_media_fmt_set = TRUE;
      }

      me_ptr->operating_mf = *media_fmt_ptr;

      result |= capi_check_and_init_input_port(me_ptr, in_arr_index, reinit_spr_ports);

      for (uint32_t outport_index = 0; outport_index < me_ptr->max_output_ports; outport_index++)
      {
         result |= capi_spr_set_up_output(me_ptr, outport_index, reinit_spr_ports);
      }
      // TODO: Error state handling for media format failure

      // Calculate the integer sampler rate us here to prevent recalc in process, changes if inp mf
      // e.g. for frameduration 2100us at 48K, this will be 100*10^6 which will be equivalent to 2083us.
      me_ptr->integ_sr_us =
         NUM_US_PER_SEC *
         (((me_ptr->operating_mf.format.sampling_rate / NUM_MS_PER_SEC) * me_ptr->frame_dur_us) / NUM_US_PER_MS);

      capi_spr_update_frame_duration_in_bytes(me_ptr);
   }
   else // There is some data left within the SPR module
   {
      if (check_cache_mf)
      {
         // Cache the media fmt & incoming data till the old data is drained.
         spr_mf_handler_t *mf_handler_ptr =
            (spr_mf_handler_t *)posal_memory_malloc(sizeof(spr_mf_handler_t), (POSAL_HEAP_ID)me_ptr->heap_id);

         if (!mf_handler_ptr)
         {
            return CAPI_ENOMEMORY; // RR TODO: ERROR state?
         }

         memset(mf_handler_ptr, 0, sizeof(spr_mf_handler_t));

         mf_handler_ptr->media_fmt             = *media_fmt_ptr;
         spr_input_port_info_t *input_port_ptr = &me_ptr->in_port_info_arr[in_arr_index];

         if (AR_EOK != (ar_result = spf_list_insert_tail((spf_list_node_t **)&input_port_ptr->mf_handler_list_ptr,
                                                         (void *)mf_handler_ptr,
                                                         me_ptr->heap_id,
                                                         TRUE)))
         {
            // TODO: Maintain error state
            posal_memory_free(mf_handler_ptr);
            return CAPI_EFAILED;
         }
#ifdef DEBUG_SPR_MODULE
         SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "Caching media format");
         capi_spr_print_media_fmt(me_ptr, media_fmt_ptr);
#endif
      }
   }

   return result;
}

/*==============================================================================
   Function : capi_spr_calc_set_timer
   Calculates drift and sets up next timer
==============================================================================*/
capi_err_t capi_spr_calc_set_timer(capi_spr_t *me_ptr)
{
   capi_err_t result        = CAPI_EOK;
   int64_t    prev_drift_us = 0, curr_drift_us = 0, inst_drift_us = 0, qt_adj_us = 0;
   uint64_t   calc_sr_us = 0, frame_dur_us = 0, curr_time_us = 0;

   if(me_ptr->flags.is_timer_disabled)
      return result;

   if (UMAX_32 == me_ptr->primary_output_arr_idx)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "capi_spr_calc_set_timer without setting up primary out port.");
      return CAPI_EFAILED;
   }

   uint32_t         ctrl_port_id  = me_ptr->out_port_info_arr[me_ptr->primary_output_arr_idx].ctrl_port_id;
   spr_ctrl_port_t *port_info_ptr = spr_get_ctrl_port_instance(me_ptr, ctrl_port_id);

   if ((port_info_ptr) && (CTRL_PORT_PEER_CONNECTED == port_info_ptr->state))
   {
      prev_drift_us = port_info_ptr->acc_drift.acc_drift_us;
      capi_spr_imcl_get_drift(me_ptr, port_info_ptr);
      curr_drift_us = port_info_ptr->acc_drift.acc_drift_us;

      if (me_ptr->counter > 1)
      {
         inst_drift_us = curr_drift_us - prev_drift_us;
      }
   }

   /* Accumulate inst drift here, will be updated with correction later to reflect pending amount */
   me_ptr->spr_pending_drift_us += inst_drift_us;

   if (me_ptr->spr_pending_drift_us > 0)
   {
      if (me_ptr->spr_pending_drift_us >= SPR_MAX_US_CORR_PER_MS_IN_QT_INT)
      {
         // should be +ve
         qt_adj_us = (int64_t)(SPR_MAX_US_CORR_PER_MS_IN_QT_INT * me_ptr->frame_dur_us) / NUM_US_PER_MS;
         qt_adj_us = MAX(qt_adj_us, SPR_MAX_US_CORR_PER_MS_IN_QT_INT);
         qt_adj_us = MIN(qt_adj_us, me_ptr->spr_pending_drift_us);
      }
   }
   /* -ve => QT is slower than hwep. Means hwep is faster than QT.
    *  So, QT period should be decreased to match hwep rate. */
   else if (me_ptr->spr_pending_drift_us < 0)
   {
      if (me_ptr->spr_pending_drift_us <= -SPR_MAX_US_CORR_PER_MS_IN_QT_INT)
      {
         // should be -ve
         qt_adj_us = -(int64_t)(SPR_MAX_US_CORR_PER_MS_IN_QT_INT * me_ptr->frame_dur_us) / NUM_US_PER_MS;
         qt_adj_us = MIN(qt_adj_us, -SPR_MAX_US_CORR_PER_MS_IN_QT_INT);
         qt_adj_us = MAX(qt_adj_us, me_ptr->spr_pending_drift_us);
      }
   }

   /* Update the pending drift with what has been corrected */
   me_ptr->spr_pending_drift_us -= qt_adj_us;
   /* Reported drift should be what has been corrected */
   me_ptr->spr_out_drift_info.spr_acc_drift.acc_drift_us += qt_adj_us;

   // when inp mf is not received use nominal frame length
   if (!me_ptr->flags.is_input_media_fmt_set)
   {
      me_ptr->absolute_start_time_us += (int64_t)me_ptr->frame_dur_us;
      frame_dur_us = 0;
   }
   else // calc based on sampling rate
   {
      calc_sr_us   = me_ptr->integ_sr_us * me_ptr->counter;
      frame_dur_us = calc_sr_us / me_ptr->operating_mf.format.sampling_rate;

      // Increment counter for next process
      me_ptr->counter++;
   }

   me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us =
      me_ptr->absolute_start_time_us + frame_dur_us + me_ptr->spr_out_drift_info.spr_acc_drift.acc_drift_us;

   /* Get current time */
   curr_time_us = (uint64_t)posal_timer_get_time();

#ifdef DEBUG_SPR_MODULE
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_HIGH_PRIO,
           "counter %d: new timestamp %ld = abs_time + time_us_based_on_sr %lu + total corrected drift(output "
           "drift) %ld curr_time_us %ld inst drift %d, inst adj drift %d, pending drift %ld",
           me_ptr->counter,
           me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us,
           frame_dur_us,
           me_ptr->spr_out_drift_info.spr_acc_drift.acc_drift_us,
           curr_time_us,
           inst_drift_us,
           qt_adj_us,
           me_ptr->spr_pending_drift_us);
#endif

   /**< Next interrupt time to be programmed is less than current time,
    *  which indicates signal miss.*/
   if (me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us < curr_time_us)
   {
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_ERROR_PRIO,
              "module signal miss, timestamp %d, curr time %d",
              me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us,
              curr_time_us);

#ifdef SIM
      *((volatile uint32_t *)0) = 0; // induce a crash

#else  // handling for on-tgt

      // Counter to know how many frames needed to be added
      uint64_t curr_counter = me_ptr->counter;

      while (me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us < curr_time_us)
      {
         // when inp mf is not received use nominal frame length
         if (!me_ptr->flags.is_input_media_fmt_set)
         {
            me_ptr->absolute_start_time_us += (int64_t)me_ptr->frame_dur_us;
            frame_dur_us = 0;
         }
         else // calc based on sampling rate
         {
            calc_sr_us   = me_ptr->integ_sr_us * me_ptr->counter;
            frame_dur_us = calc_sr_us / me_ptr->operating_mf.format.sampling_rate;

            // Increment counter which is used for calculating absolute-time based frame duration, incrementing by 1 is
            // equivalent to adding one frame dur ms
            me_ptr->counter++;
         }

         // New output timestamp
         me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us =
            me_ptr->absolute_start_time_us + frame_dur_us + me_ptr->spr_out_drift_info.spr_acc_drift.acc_drift_us;
      }

      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_HIGH_PRIO,
              "Counter: %ld: Added frame duration %d, %d times, to ensure next timer tick is in the future, timestamp "
              "%d, curr time "
              "%d",
              me_ptr->counter,
              frame_dur_us,
              (me_ptr->counter - curr_counter),
              me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us,
              curr_time_us);
#endif // SIM
   }

   /** Set up timer for specified absolute duration */
   int32_t rc =
      posal_timer_oneshot_start_absolute(me_ptr->timer, me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us);
   if (rc)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "one shot timer start failed result: %lu", rc);
      return CAPI_EFAILED;
   }

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_add_input_to_mf_list
   Adds the input stream to the tail of the cached mf list
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_add_input_to_mf_list(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_ptr)
{
   capi_err_t result = CAPI_EOK;

   spf_list_node_t *tail_ptr = NULL;
   ar_result_t ar_result = spf_list_get_tail_node((spf_list_node_t *)me_ptr->in_port_info_arr[0].mf_handler_list_ptr,
                                                  (spf_list_node_t **)&tail_ptr);
   // TODO: Check handle error state
   if (AR_DID_FAIL(ar_result) || !tail_ptr)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Failed to get tail list from mf_handler_list with error 0x%x", ar_result);
      return CAPI_EFAILED;
   }

   spr_mf_handler_t *mf_handler_ptr = (spr_mf_handler_t *)(tail_ptr->obj_ptr);

   result = capi_spr_create_int_buf_node(me_ptr,
                                         &mf_handler_ptr->int_buf,
                                         input_ptr,
                                         me_ptr->heap_id,
                                         &mf_handler_ptr->media_fmt);

   if (CAPI_FAILED(result))
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Failed to create int buf node with error 0x%x", result);
   }

   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_get_arr_index_from_port_index
   Given port index, fetch the corresponding array index to be used for the internal
   book keeping port structures.
 * ------------------------------------------------------------------------------*/
uint32_t spr_get_arr_index_from_port_index(capi_spr_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   if (!is_input)
   {
      for (uint32_t arr_index = 0; arr_index < me_ptr->max_output_ports; arr_index++)
      {
         if (DATA_PORT_STATE_CLOSED == me_ptr->out_port_info_arr[arr_index].port_state)
         {
            continue;
         }
         if (port_index == me_ptr->out_port_info_arr[arr_index].port_index)
         {
            return arr_index;
         }
      }
   }
   else
   {
      for (uint32_t arr_index = 0; arr_index < me_ptr->max_input_ports; arr_index++)
      {
         if (DATA_PORT_STATE_CLOSED == me_ptr->in_port_info_arr[arr_index].port_state)
         {
            continue;
         }
         if (port_index == me_ptr->in_port_info_arr[arr_index].port_index)
         {
            return arr_index;
         }
      }
   }

   SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Port index = %lu to index mapping not found.", port_index);
   return UMAX_32;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_raise_underrun_event
    Identifies the reason for the underrun and raises the event to the client
    if registered.
* ------------------------------------------------------------------------------*/
void capi_spr_check_raise_underrun_event(capi_spr_t *me_ptr, underrun_status_t *status_ptr, uint32_t output_arr_idx)
{
   if (!me_ptr || !status_ptr)
   {
      return;
   }

   event_id_spr_underrun_t event_payload;
   capi_err_t              result = CAPI_EOK;

   // Determine the reason for the underrun in SPR as follows :-
   //   1. If AVSync is enabled, then check the previous_render_decision cached. If the render decision
   //       was HOLD/DROP, mark this as underrun due to that.
   //   2. If the render decision is set as INVALID/ AVSync is disabled, then it is always due to
   //       lack of data from upstream
   //   3. If input port is at gap, then do not report the event as underrun. This happens due to SPR
   //        receiving EOS/DFG (both intended actions from the client)

   if (capi_spr_check_if_input_is_at_gap(me_ptr))
   {
      *status_ptr = INPUT_AT_GAP;
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "underrun due to input at gap (eos/pause). Ignoring event");
#endif
      return;
   }
   else if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      render_decision_t decision = spr_avsync_get_render_decision(me_ptr->avsync_ptr);

      if (HOLD == decision)
      {
         *status_ptr          = DATA_HELD;
         event_payload.status = UNDERRUN_STATUS_INPUT_HOLD;
      }
      else if (DROP == decision)
      {
         *status_ptr          = DATA_DROPPED;
         event_payload.status = UNDERRUN_STATUS_INPUT_DROP;
      }
      else
      {
         *status_ptr          = DATA_NOT_AVAILABLE;
         event_payload.status = UNDERRUN_STATUS_INPUT_NOT_AVAILABLE;
      }
   }
   else
   {
      *status_ptr          = DATA_NOT_AVAILABLE;
      event_payload.status = UNDERRUN_STATUS_INPUT_NOT_AVAILABLE;
   }

   if (0 == me_ptr->underrun_event_info.dest_address)
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "No client registered for underrun event. Returning");
#endif
      return;
   }

   if (output_arr_idx != me_ptr->primary_output_arr_idx)
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Not primary output port idx. Returning");
#endif
      return;
   }

   capi_event_info_t event_info;
   event_info.port_info.is_valid = FALSE;

   capi_event_data_to_dsp_client_v2_t evt = { 0 };
   evt.event_id                           = EVENT_ID_SPR_UNDERRUN;
   evt.token                              = me_ptr->underrun_event_info.token;
   evt.dest_address                       = me_ptr->underrun_event_info.dest_address;
   evt.payload.actual_data_len            = sizeof(event_id_spr_underrun_t);
   evt.payload.data_ptr                   = (int8_t *)&event_payload;
   evt.payload.max_data_len               = sizeof(event_id_spr_underrun_t);

   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
   event_info.payload.data_ptr        = (int8_t *)&evt;
   event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);

   result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                           CAPI_EVENT_DATA_TO_DSP_CLIENT_V2,
                                           &event_info);

   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_MED_PRIO,
           "Raised EVENT_ID_SPR_UNDERRUN with status %d (nodata1, hold2, drop3) with result 0x%x",
           event_payload.status,
           result);
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_raise_session_time_reset_event
    Raises the session time reset event to the client if registered.
* ------------------------------------------------------------------------------*/
void capi_spr_check_raise_session_time_reset_event(capi_spr_t *me_ptr)
{
   if (!me_ptr)
   {
      return;
   }

   param_id_spr_session_time_t event_payload;
   capi_err_t              result = CAPI_EOK;

   if (0 == me_ptr->session_time_reset_event_info.dest_address)
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "No client registered for session time reset event. Returning");
#endif
      return;
   }

   if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "AVSync not enabled for session time reset event. Returning");
      return;
   }

   param_id_spr_session_time_t *session_time_ptr = &event_payload;
   memset(session_time_ptr, 0, sizeof(param_id_spr_session_time_t));

   int64_t   session_time, absolute_ts, proc_ts;
   bool_t    is_ts_valid = FALSE;
   avsync_t *avsync_ptr  = me_ptr->avsync_ptr;
   session_time = absolute_ts = proc_ts = 0;

   session_time = avsync_ptr->session_clock_us;
   absolute_ts  = avsync_ptr->absolute_time_us;
   proc_ts      = avsync_ptr->proc_timestamp_us;
   is_ts_valid  = avsync_ptr->flags.is_ts_valid;

   session_time_ptr->absolute_time.value_lsw = (uint32_t)(absolute_ts);
   session_time_ptr->absolute_time.value_msw = (uint32_t)(absolute_ts >> 32);
   session_time_ptr->session_time.value_lsw  = (uint32_t)(session_time);
   session_time_ptr->session_time.value_msw  = (uint32_t)(session_time >> 32);
   session_time_ptr->timestamp.value_lsw     = (uint32_t)(proc_ts);
   session_time_ptr->timestamp.value_msw     = (uint32_t)(proc_ts >> 32);
   session_time_ptr->flags |= ((is_ts_valid) << PARAM_ID_SESSION_TIME_SHIFT_IS_TIMESTAMP_VALID);

   capi_event_info_t event_info;
   event_info.port_info.is_valid = FALSE;

   capi_event_data_to_dsp_client_v2_t evt = { 0 };
   evt.event_id                           = EVENT_ID_SPR_SESSION_TIME_RESET;
   evt.token                              = me_ptr->session_time_reset_event_info.token;
   evt.dest_address                       = me_ptr->session_time_reset_event_info.dest_address;
   evt.payload.actual_data_len            = sizeof(param_id_spr_session_time_t);
   evt.payload.data_ptr                   = (int8_t *)&event_payload;
   evt.payload.max_data_len               = sizeof(param_id_spr_session_time_t);

   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
   event_info.payload.data_ptr        = (int8_t *)&evt;
   event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);

   result = me_ptr->event_cb_info.event_cb(me_ptr->event_cb_info.event_context,
                                           CAPI_EVENT_DATA_TO_DSP_CLIENT_V2,
                                           &event_info);

   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_MED_PRIO,
           "Raised EVENT_ID_SPR_SESSION_TIME_RESET with result 0x%x",
           result);
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_update_frame_duration_in_bytes
    Calculates the frame duration in bytes for the module.
* ------------------------------------------------------------------------------*/
void capi_spr_update_frame_duration_in_bytes(capi_spr_t *me_ptr)
{
   if (!me_ptr)
   {
      return;
   }

   if (!me_ptr->flags.is_input_media_fmt_set || (0 == me_ptr->frame_dur_us))
   {
      return;
   }

   // num_samples * frame_duration * bytes_per_sample
   me_ptr->frame_dur_bytes_per_ch =
      (((me_ptr->operating_mf.format.sampling_rate / NUM_MS_PER_SEC) * me_ptr->frame_dur_us) / NUM_US_PER_MS) *
      (CAPI_CMN_BITS_TO_BYTES(me_ptr->operating_mf.format.bits_per_sample));

   SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Frame duration in bytes per channel %d", me_ptr->frame_dur_bytes_per_ch);

   capi_cmn_update_port_data_threshold_event(&me_ptr->event_cb_info,
                                             (me_ptr->frame_dur_bytes_per_ch *
                                              me_ptr->operating_mf.format.num_channels),
                                             TRUE,
                                             me_ptr->in_port_info_arr[0].port_index);
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_and_init_output_port
   Check if the port config is received and then allocate stream reader
   for the particular index.
 * ------------------------------------------------------------------------------*/
capi_err_t capi_spr_check_and_init_output_port(capi_spr_t *me_ptr, uint32_t arr_index, bool_t need_to_reinitialize)
{

   // if need to reinit flag is set, destory the existing handle and initialize again.
   if (need_to_reinitialize && me_ptr->out_port_info_arr[arr_index].strm_reader_ptr)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "Port is being reset. outport_index %lu", arr_index);
      spr_stream_reader_destroy(me_ptr->drv_ptr, &me_ptr->out_port_info_arr[arr_index].strm_reader_ptr);
   }

   if ((NULL == me_ptr->out_port_info_arr[arr_index].strm_reader_ptr) && me_ptr->flags.is_input_media_fmt_set)
   {
      // Convert downstream setup delay from us to bytes.
      uint32_t downstream_setup_duration_in_bytes = 0;

      // Get the unique channel id for each channel using input port index and channel map.
      uint32_t channel_ids[MAX_CHANNELS_PER_STREAM];
      for (uint32_t i = 0; i < me_ptr->operating_mf.format.num_channels; i++)
      {
         // Set LSB 2 bytes to input channel type.
         channel_ids[i] = me_ptr->operating_mf.channel_type[i];
      }

      ar_result_t result = spr_stream_reader_create(me_ptr->drv_ptr,
                                                    downstream_setup_duration_in_bytes,
                                                    me_ptr->operating_mf.format.num_channels,
                                                    (uint32_t *)channel_ids,
                                                    &me_ptr->out_port_info_arr[arr_index].strm_reader_ptr);
      if (AR_EOK != result)
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Output port can't be initiated. port_index =%d, result %d",
                 me_ptr->out_port_info_arr[arr_index].port_index,
                 result);
         return CAPI_EFAILED;
      }
      else
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Output port initiated successfully. port_index =%d, result %d",
                 me_ptr->out_port_info_arr[arr_index].port_index,
                 result);
      }
   }
   else
   {
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_HIGH_PRIO,
              "Output port not ready to initialize. port_index =%d.",
              me_ptr->out_port_info_arr[arr_index].port_index);
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_and_raise_output_media_format_event
   Raise capi media format event when the appropriate port states & configuration
   is received
 * ------------------------------------------------------------------------------*/
capi_err_t capi_spr_check_and_raise_output_media_format_event(capi_spr_t *me_ptr, uint32_t arr_index)
{
   capi_err_t result = CAPI_EOK;

   if (me_ptr->flags.is_input_media_fmt_set && (DATA_PORT_STATE_CLOSED != me_ptr->out_port_info_arr[arr_index].port_state))
   {
      result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->event_cb_info,
                                                   &me_ptr->operating_mf,
                                                   FALSE,
                                                   me_ptr->out_port_info_arr[arr_index].port_index);
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_MED_PRIO,
              "raised output media format for port_index %d as follows :",
              me_ptr->out_port_info_arr[arr_index].port_index);

      capi_spr_print_media_fmt(me_ptr, &me_ptr->operating_mf);

#endif
   }

   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_cached_mf_is_input_strm_head
   Check if the given input stream is the head of the cached media format list
* ------------------------------------------------------------------------------*/
bool_t spr_cached_mf_is_input_strm_head(capi_spr_t *me_ptr, void *input_strm_ptr)
{
   if (spr_has_cached_mf(me_ptr))
   {
      spr_mf_handler_t *mf_obj_ptr = (spr_mf_handler_t *)me_ptr->in_port_info_arr->mf_handler_list_ptr->mf_handler_ptr;

      if (spr_int_buf_is_head_node(mf_obj_ptr->int_buf.buf_list_ptr, input_strm_ptr))
      {
         return TRUE;
      }
   }

   return FALSE;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_apply_mf_change
   From the process context, checks & applies the cached media format if any
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_check_apply_mf_change(capi_spr_t *me_ptr, bool_t *did_mf_change)
{
   spr_input_port_info_t *input_port_ptr = &me_ptr->in_port_info_arr[0];
   capi_err_t             result         = CAPI_EOK;

   if (!input_port_ptr)
   {
      return result;
   }

   if (!did_mf_change)
   {
      return CAPI_EBADPARAM;
   }

   *did_mf_change = FALSE;

   // If there is no pending media format continue
   if (!input_port_ptr->mf_handler_list_ptr)
   {
      return result;
   }

   // If media format change can be applied, raise output media format & re-create stream writers & readers
   spr_mf_handler_t *mf_handler_ptr =
      (spr_mf_handler_t *)capi_spr_get_list_head_obj_ptr(input_port_ptr->mf_handler_list_ptr);

   if (!mf_handler_ptr)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Failed to get MF handler node");
      return CAPI_EFAILED;
   }

   // If this media format is already applied, then there is some data still left in the int buffer list for this mf
   if (mf_handler_ptr->is_applied)
   {
      return result;
   }

   bool_t reinit_spr_ports = spr_can_reinit_with_new_mf(me_ptr);
   bool_t DO_NOT_CACHE_MF  = FALSE;

   if (!reinit_spr_ports)
   {
      return result;
   }

   result = capi_spr_check_reinit_ports(me_ptr, &mf_handler_ptr->media_fmt, reinit_spr_ports, DO_NOT_CACHE_MF);

   if (CAPI_FAILED(result))
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Failed to apply MF change to SPR with error 0x%x", result);
      // TODO: Error state handling for media format change
   }
   else
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "Applied MF change to SPR successfully!");
      mf_handler_ptr->is_applied = TRUE;
      *did_mf_change             = TRUE;
   }

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: spr_can_reinit_with_new_mf
   During a media format change, validates if the module can reinit right away
   or not
* ------------------------------------------------------------------------------*/
bool_t spr_can_reinit_with_new_mf(capi_spr_t *me_ptr)
{
   // if hold buffer is not configured, then media format is applied right away.
   // any data in the circular buffer would be dropped
   if (!spr_avsync_is_hold_buf_configured(me_ptr->avsync_ptr))
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "hold buffer duration is not configured, applying media format right away");
#endif
      return TRUE;
   }

   // If there is a hold buffer duration configured, then check the state of buffering inside spr before
   // applying the media format change
   bool_t strm_reader_has_data = FALSE, is_data_held = FALSE, reinit_spr_ports = FALSE, is_old_mf_data_pending = FALSE;

   (void)spr_does_strm_reader_have_data(me_ptr, &strm_reader_has_data);
   is_data_held           = spr_avsync_does_hold_buf_exist(me_ptr->avsync_ptr);
   is_old_mf_data_pending = spr_has_old_mf_data_pending(me_ptr);

   // If there is no pending data, then reinit SPR data ports right away
   if ((!strm_reader_has_data) && (!is_data_held) && (!is_old_mf_data_pending))
   {
      reinit_spr_ports = TRUE;
   }

#ifdef DEBUG_SPR_MODULE
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_MED_PRIO,
           "handle input mf change, reinit_spr_ports = %d, strm_reader_has_data = %d, hold_buffer_has_data "
           "= %d is_old_mf_data_pending = %d",
           reinit_spr_ports,
           strm_reader_has_data,
           is_data_held,
           is_old_mf_data_pending);
#endif

   return reinit_spr_ports;
}
