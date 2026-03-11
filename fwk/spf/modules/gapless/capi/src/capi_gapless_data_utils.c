/**
 * \file capi_gapless_data_utils.c
 * \brief
 *     Implementation of utility functions for capi data handling (process function, data buffers, data processing, etc).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_gapless_i.h"

static capi_err_t gapless_handle_pending_operating_media_fmt(capi_gapless_t *me_ptr, capi_stream_data_t *input[]);

/**
 * Handle pending output media format event. This is pending after switching streams when inputs have different
 * media formats. Take the operating media format from the newly active input port, raise output media format, and
 * mark input as consumed in order to return early without consuming/producing data.
 */
static capi_err_t gapless_handle_pending_operating_media_fmt(capi_gapless_t *me_ptr, capi_stream_data_t *input[])
{
   capi_err_t capi_result                     = CAPI_EOK;
   me_ptr->has_pending_operating_media_format = FALSE;

   capi_gapless_in_port_t *active_in_port_ptr = capi_gapless_get_active_in_port(me_ptr);
   if (!active_in_port_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error getting active input port while pending operating media format was set!");
      return CAPI_EFAILED;
   }

   // This should always return true since we're sending in the active input port.
   if (capi_gapless_should_set_operating_media_format(me_ptr, active_in_port_ptr))
   {
      capi_result |= capi_gapless_set_operating_media_format(me_ptr, &(active_in_port_ptr->media_fmt));
   }

   // Mark inputs as unconsumed.
   for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
   {
      capi_gapless_in_port_t *in_port_ptr = capi_gapless_get_in_port_from_index(me_ptr, i);

      if (in_port_ptr && (DATA_PORT_STATE_STARTED == in_port_ptr->cmn.state))
      {
         capi_stream_data_v2_t *in_sdata_ptr = (capi_stream_data_v2_t *)input[i];
         if (in_sdata_ptr && in_sdata_ptr->buf_ptr)
         {
            for (uint32_t i = 0; i < in_sdata_ptr->bufs_num; i++)
            {
               in_sdata_ptr->buf_ptr[i].actual_data_len = 0;
            }
         }
      }
   }
   return capi_result;
}

/**
 * Checks if a stream data has any data. This is true if actual data len is nonzero OR eof is set
 * (for flushing EOS without data case).
 */
bool_t gapless_sdata_has_data(capi_gapless_t *me_ptr, capi_stream_data_v2_t *sdata_ptr)
{
   uint32_t input_size = (sdata_ptr && sdata_ptr->buf_ptr) ? sdata_ptr->buf_ptr[0].actual_data_len : 0;
   return (0 != input_size) || (sdata_ptr && (sdata_ptr->flags.end_of_frame));
}

/**
 * Processes an input buffer and generates an output buffer.
 */
capi_err_t gapless_process(capi_gapless_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t             capi_result     = CAPI_EOK;
   capi_stream_data_v2_t *out_sdata_ptr   = output ? (capi_stream_data_v2_t *)output[0] : NULL;
   bool_t                 produced_output = FALSE;

   // Try to send reset session time metadata if we sent eos on the previous process call. Don't overwrite to FALSE in
   // case we haven't sent reset session time md yet.
   if (me_ptr->sent_eos_this_process_call)
   {
      me_ptr->send_rst_md = TRUE;
   }

   // Recompute this value on each process call.
   me_ptr->sent_eos_this_process_call = FALSE;

   capi_gapless_out_port_t *out_port_ptr = capi_gapless_get_out_port(me_ptr);

   if (!out_port_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error getting output port");
      return CAPI_EFAILED;
   }

   if (me_ptr->has_pending_operating_media_format)
   {
      AR_MSG(DBG_ERROR_PRIO, "Returning early from process due to raising output media format event.");
      return (capi_result | gapless_handle_pending_operating_media_fmt(me_ptr, input));
   }

   if (me_ptr->pass_through_mode)
   {
      // Try to buffer data from the input to the internal buffer.
      if (CAPI_EOK != (capi_result |= gapless_buffer_input(me_ptr, input)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_buffer_input.", capi_result);
         return capi_result;
      }

      // Try to write data from the internal buffer to the output. Do this after buffering input in case the
      // internal buffer was empty before buffering input.
      if (CAPI_EOK != (capi_result |= gapless_write_output(me_ptr, input, output)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_write_output.", capi_result);
         return capi_result;
      }
   }
   else
   {
      // Try to write data from the internal buffer to the output. Do this before buffering input to make
      // space for input if the internal buffer is full.
      if (CAPI_EOK != (capi_result |= gapless_write_output(me_ptr, input, output)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_write_output.", capi_result);
         return capi_result;
      }

      // Try to buffer data from the input to the internal buffer.
      if (CAPI_EOK != (capi_result |= gapless_buffer_input(me_ptr, input)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_buffer_input.", capi_result);
         return capi_result;
      }

      // Try to write data from the internal buffer to the output. Do this after buffering input in case the
      // internal buffer was empty before buffering input.
      // Don't generate output if output media format is pending (new output will be at the next media format).
      if (!me_ptr->has_pending_operating_media_format)
      {
         if (CAPI_EOK != (capi_result |= gapless_write_output(me_ptr, input, output)))
         {
            AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_write_output.", capi_result);
            return capi_result;
         }
      }
      else
      {
#ifdef CAPI_GAPLESS_DEBUG
         AR_MSG(DBG_MED_PRIO, "Not doing a second write_output call since output media format is pending.");
#endif
      }
   }

   // Send reset session time md on first data buffer of new session.
   produced_output = (out_sdata_ptr && (out_sdata_ptr->buf_ptr) && (out_sdata_ptr->buf_ptr[0].actual_data_len));
   if (me_ptr->metadata_handler.metadata_create)
   {
      if ((me_ptr->send_rst_md) && produced_output)
      {
         // Send reset session time md.
         module_cmn_md_t *md_ptr           = NULL;
         uint32_t         RST_SIZE_ZERO    = 0;     // No payload for reset session time.
         bool_t           IS_OUTBAND_FALSE = FALSE; // Inband
         capi_result = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                                &(out_sdata_ptr->metadata_list_ptr),
                                                                RST_SIZE_ZERO,
                                                                me_ptr->heap_info,
                                                                IS_OUTBAND_FALSE,
                                                                &md_ptr);
         if (CAPI_FAILED(capi_result))
         {
            AR_MSG(DBG_ERROR_PRIO, "creating metadata failed %lx", capi_result);
            return AR_EOK;
         }
         md_ptr->metadata_id = MODULE_CMN_MD_ID_RESET_SESSION_TIME;
         md_ptr->offset      = 0;

         // Set to FALSE since we just sent RST metadata.
         me_ptr->send_rst_md = FALSE;

         AR_MSG(DBG_MED_PRIO, "MD_DBG: Sent reset session time, metadata ptr 0x%p", md_ptr);
      }
   }

   // Check if the trigger policy needs to be changed.
   if (CAPI_EOK != (capi_result |= gapless_check_update_trigger_policy(me_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_check_update_trigger_policy.", capi_result);
      return capi_result;
   }

   return capi_result;
}

/**
 * Writes as much data as possible from input to output. Assumes output begins empty. Also propagates metadata.
 * If EOS was sent to the output, returns that eos metadata. TODO(claguna): What if multiple EOS get sent in one
 * process call?
 */
capi_err_t gapless_pass_through(capi_gapless_t *        me_ptr,
                                capi_gapless_in_port_t *in_port_ptr,
                                capi_stream_data_v2_t * in_sdata_ptr,
                                capi_stream_data_v2_t * out_sdata_ptr,
                                module_cmn_md_t **      eos_md_pptr)
{
   capi_err_t result = CAPI_EOK;

   if (!in_port_ptr || !in_sdata_ptr || !out_sdata_ptr || !eos_md_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "gapless_pass_through bad args in_port_ptr 0x%lx, in_sdata_ptr 0x%lx, out_sdata_ptr 0x%lx, eos_md_pptr "
             "0x%lx",
             in_port_ptr,
             in_sdata_ptr,
             out_sdata_ptr,
             eos_md_pptr);
      return CAPI_EFAILED;
   }

   capi_media_fmt_v2_t *in_media_fmt_ptr = &(in_port_ptr->media_fmt);

   if (!(in_sdata_ptr) || !(in_sdata_ptr->buf_ptr) || !(in_sdata_ptr->buf_ptr[0].data_ptr))
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO, "No data on active input port - returning early.");
#endif

      return result;
   }

   bool_t mark_input_unconsumed = FALSE;
   if ((!out_sdata_ptr) || (!out_sdata_ptr->buf_ptr) || (!out_sdata_ptr->buf_ptr[0].data_ptr))
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO,
             "Output stream data was not provided: sdata_ptr 0x%lx, buf_ptr 0x%lx, data_ptr 0x%lx. Not consuming "
             "input.",
             out_sdata_ptr,
             (out_sdata_ptr ? out_sdata_ptr->buf_ptr : NULL),
             ((out_sdata_ptr && out_sdata_ptr->buf_ptr) ? out_sdata_ptr->buf_ptr[0].data_ptr : NULL));
#endif
      mark_input_unconsumed = TRUE;
   }

   if ((!mark_input_unconsumed) && (out_sdata_ptr->buf_ptr[0].actual_data_len > 0))
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO, "Output sdata not empty. Not consuming input.");
#endif
      mark_input_unconsumed = TRUE;
   }

   if ((in_sdata_ptr) && (in_sdata_ptr->buf_ptr) && (in_sdata_ptr->buf_ptr[0].data_ptr))
   {
      uint32_t in_size_before = in_sdata_ptr->buf_ptr[0].actual_data_len;

      for (uint32_t ch_idx = 0; ch_idx < in_media_fmt_ptr->format.num_channels; ch_idx++)
      {
         if (mark_input_unconsumed)
         {
            in_sdata_ptr->buf_ptr[ch_idx].actual_data_len = 0;
         }
         else
         {
            uint32_t write_size = memscpy(out_sdata_ptr->buf_ptr[ch_idx].data_ptr,
                                          out_sdata_ptr->buf_ptr[ch_idx].max_data_len,
                                          in_sdata_ptr->buf_ptr[ch_idx].data_ptr,
                                          in_sdata_ptr->buf_ptr[ch_idx].actual_data_len);

            in_sdata_ptr->buf_ptr[ch_idx].actual_data_len  = write_size;
            out_sdata_ptr->buf_ptr[ch_idx].actual_data_len = write_size;
         }
      }

      if (!mark_input_unconsumed)
      {
         uint32_t is_input_fully_consumed = (in_size_before == in_sdata_ptr->buf_ptr[0].actual_data_len);
         gapless_propagate_metadata(me_ptr,
                                    in_sdata_ptr,
                                    in_media_fmt_ptr,
                                    out_sdata_ptr,
                                    is_input_fully_consumed,
                                    in_size_before);

         // If EOS appeared on the output after propagating metadata, return it to the caller.
         *eos_md_pptr = gapless_find_eos(out_sdata_ptr);

         // If the input eos was consumed, raise early eos.
         if (*eos_md_pptr)
         {
            result |= gapless_raise_early_eos_event(me_ptr, in_port_ptr, in_sdata_ptr, *eos_md_pptr);
            me_ptr->sent_eos_this_process_call = TRUE;
            if (me_ptr->is_gapless_cntr_duty_cycling)
            {

               result = gapless_raise_allow_duty_cycling_event(me_ptr, FALSE);
               if (AR_EFAILED == result)
               {
                  AR_MSG(DBG_HIGH_PRIO, "delay_buf: Failed to raise island blocking event as part of early eos");
                  return CAPI_EFAILED;
               }
            }
         }
      }
   }

   return result;
}

/**
 * Buffers input data into the internal buffers. If there is no active port yet, makes a port active
 * if input data was available (first come first serve. In case that input arrives on both ports at
 * the same time, either can be made active).
 */
capi_err_t gapless_buffer_input(capi_gapless_t *me_ptr, capi_stream_data_t *input[])
{
   capi_err_t result = CAPI_EOK;

   // If there's no active port, try and get an active port (looks for a port where data was provided
   // on the input).
   capi_gapless_in_port_t *active_in_port_ptr = capi_gapless_get_active_in_port(me_ptr);
   if (NULL == active_in_port_ptr)
   {
      if (CAPI_EOK != (result |= capi_gapless_check_assign_new_active_in_port(me_ptr, input)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from capi_gapless_check_assign_new_active_in_port", result);
         return result;
      }

      capi_gapless_in_port_t *active_in_port_ptr = capi_gapless_get_active_in_port(me_ptr);

      // Not an error in the case that last output port sent EOS.
      if (NULL == active_in_port_ptr)
      {
#ifdef CAPI_GAPLESS_DEBUG
         AR_MSG(DBG_MED_PRIO, "No active input ports yet.");
#endif
         return CAPI_EOK;
      }
   }

   // In pass_through_mode, no buffering is necessary.
   if (me_ptr->pass_through_mode)
   {
      return result;
   }

   // Try to buffer data on both active and inactive ports.
   for (uint32_t i = 0; i < me_ptr->num_in_ports; i++)
   {
      capi_gapless_in_port_t *in_port_ptr  = capi_gapless_get_in_port_from_index(me_ptr, i);
      capi_stream_data_v2_t * in_sdata_ptr = (capi_stream_data_v2_t *)input[i];

      if (in_port_ptr && (DATA_PORT_STATE_STARTED == in_port_ptr->cmn.state) &&
          gapless_sdata_has_data(me_ptr, in_sdata_ptr))
      {
         module_cmn_md_t *eos_ptr              = gapless_find_eos(in_sdata_ptr);
         bool_t           input_has_eos_before = (NULL != eos_ptr);

         if (in_sdata_ptr->flags.is_timestamp_valid)
         {
            // We can't guarantee timestamp continuity during stream switch. Further, timestamp discontinuity handling
            // in the framework could lead to a gap. Therefore we should mark all timestamps as invalid. Clients should
            // not be expecting timestamps in a gapless use case so we don't expect to hit this code in real use cases.
            if (!(in_port_ptr->found_valid_timestamp))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Found valid timestamps on input port idx %ld. Marking timestamps invalid for the rest of the "
                      "use case.",
                      in_port_ptr->cmn.index);
               in_port_ptr->found_valid_timestamp = TRUE;
            }

            in_sdata_ptr->flags.is_timestamp_valid = FALSE;
         }

         result |= gapless_write_delay_buffer(me_ptr, in_port_ptr, in_sdata_ptr);

         bool_t input_has_eos_after = (NULL != (gapless_find_eos(in_sdata_ptr)));

         // If eos moved from the input to the delay buffer, clear input eof and raise early eos.
         if (input_has_eos_before && (!input_has_eos_after))
         {
            result |= gapless_raise_early_eos_event(me_ptr, in_port_ptr, in_sdata_ptr, eos_ptr);
            if (me_ptr->is_gapless_cntr_duty_cycling)
            {

               result = gapless_raise_allow_duty_cycling_event(me_ptr, FALSE);
               if (AR_EFAILED == result)
               {
                  AR_MSG(DBG_HIGH_PRIO, "delay_buf: Failed to raise island blocking event as part of early eos");
                  return CAPI_EFAILED;
               }
            }
         }
      }
   }

   return result;
}

/**
 * Copies data from the active input port to the output. If EOS is encountered on the active stream,
 * the other stream becomes active and we continue writing from the other stream. Changes the EOS flag
 * accordingly:
 *
 * 1. If there's data on the other stream, EOS becomes nonflushing (data from the other stream will naturally flush out
 *    EOS).
 * 2. If there's no data on the other stream, EOS becomes flushing (downstream must insert zeros to flush out EOS).
 */
capi_err_t gapless_write_output(capi_gapless_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t              result             = CAPI_EOK;
   capi_gapless_in_port_t *active_in_port_ptr = capi_gapless_get_active_in_port(me_ptr);

   // This will happen on the first process call on the first write_output, which happens before buffering input.
   if (NULL == active_in_port_ptr)
   {
#ifdef CAPI_GAPLESS_DEBUG
      AR_MSG(DBG_MED_PRIO, "No active input ports for gapless_write_output. Ok on first call.");
#endif
      return CAPI_EOK;
   }

   capi_stream_data_v2_t *out_sdata_ptr       = output ? ((capi_stream_data_v2_t *)output[0]) : NULL;
   capi_stream_data_v2_t *active_in_sdata_ptr = (capi_stream_data_v2_t *)input[active_in_port_ptr->cmn.index];

   // read_delay_buffer needs to return whether eos moved to output, so we don't touch the same eos twice even though
   // we are writing twice.
   module_cmn_md_t *eos_md_ptr = NULL;

   if (me_ptr->pass_through_mode)
   {
      if (CAPI_EOK !=
          (result |= gapless_pass_through(me_ptr, active_in_port_ptr, active_in_sdata_ptr, out_sdata_ptr, &eos_md_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_todo_read_pass_through.", result);
         return result;
      }
   }
   else
   {
      if (CAPI_EOK != (result |= gapless_read_delay_buffer(me_ptr, active_in_port_ptr, out_sdata_ptr, &eos_md_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Error code 0x%lx returned from gapless_todo_read_delay_buffer.", result);
         return result;
      }
   }

   // Check if EOS on output. If no eos, we are done.
   if (NULL == eos_md_ptr)
   {
      return result;
   }

   // If we reach this position it means there's EOS on the output port.
   me_ptr->sent_eos_this_process_call = TRUE;

   capi_gapless_in_port_t *other_in_port_ptr = capi_gapless_get_other_in_port(me_ptr);

   // Check if there's any data on the other input port.
   bool_t other_stream_has_data = capi_gapless_other_stream_has_data(me_ptr, other_in_port_ptr);

   // Check if there's any new data in the active input port. if new data is present then convert EOS to non-flushing
   bool_t active_input_port_has_new_data = !(capi_gapless_is_delay_buffer_empty(me_ptr, active_in_port_ptr));

   // Get EOS metadata.
   uint32_t is_out_band = eos_md_ptr->metadata_flag.is_out_of_band;

   module_cmn_md_eos_t *eos_metadata_ptr = is_out_band ? (module_cmn_md_eos_t *)eos_md_ptr->metadata_ptr
                                                       : (module_cmn_md_eos_t *)&(eos_md_ptr->metadata_buf);

   // Sanity checks on metadata structure.
   if ((!eos_metadata_ptr) || (!eos_md_ptr->tracking_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "Error: EOS md ptr 0x%lx or core ptr is NULL.", eos_metadata_ptr);
      return CAPI_EFAILED;
   }

   if (active_input_port_has_new_data)
   {

      AR_MSG(DBG_MED_PRIO,
             "SISO EOS sent to output port. Buffered data found on the same port idx %ld",
             active_in_port_ptr->cmn.index);

      eos_metadata_ptr->flags.is_flushing_eos = MODULE_CMN_MD_EOS_NON_FLUSHING;
      active_in_sdata_ptr->flags.end_of_frame = FALSE;

      if (out_sdata_ptr)
      {
         out_sdata_ptr->flags.end_of_frame = FALSE;
         out_sdata_ptr->flags.marker_eos   = FALSE;
      }

      result = gapless_write_output(me_ptr, input, output);
   }
   else
   {

      if (!other_stream_has_data)
      {
         // Unmark active stream.
         me_ptr->active_in_port_index = GAPLESS_PORT_INDEX_INVALID;

         AR_MSG(DBG_MED_PRIO,
                "EOS sent to output port. No data buffered from other port, so active port was unmarked.");
      }
      else
      {
         // Other stream becomes active.
         me_ptr->active_in_port_index = other_in_port_ptr->cmn.index;

         AR_MSG(DBG_MED_PRIO,
                "EOS sent to output port. Buffered data found on other port idx %ld, that port becomes active.",
                other_in_port_ptr->cmn.index);

         // If media formats between streams are different, we can't continue writing in the same buffer. Mark output
         // media format as pending, mark current stream as and quit.
         if (capi_cmn_media_fmt_equal(&(active_in_port_ptr->media_fmt), &(other_in_port_ptr->media_fmt)))
         {
            // Recursive call to try and write output. This time the other stream will be active, so we'll write from
            // that. Note: We don't try to buffer input data from the other stream, so if the delay buffer is empty but
            // the other stream has data, we won't reach this point. However that's not expected since when we send EOS
            // we would expect there to be data in the other stream's delay buffer, if the client sent data on time.
            // This won't cause recursion unless both of the below are true:
            // 1. We keep seeing EOS each time we read from the delay buffer
            // 2. At the same time the other delay buffer still has data.
            // Neither of these cases should be true more than once.

            // Mark EOS as nonflushing. Mark output as not EOF, not marker_eos.
            eos_metadata_ptr->flags.is_flushing_eos = MODULE_CMN_MD_EOS_NON_FLUSHING;
            active_in_sdata_ptr->flags.end_of_frame = FALSE;

            if (out_sdata_ptr)
            {
               out_sdata_ptr->flags.end_of_frame = FALSE;
               out_sdata_ptr->flags.marker_eos   = FALSE;
            }

            result = gapless_write_output(me_ptr, input, output);
         }
         else
         {
            AR_MSG(DBG_MED_PRIO,
                   "Switched streams (from input index %ld to input index %ld) with different input media "
                   "formats. Marking EOS as flushing.",
                   active_in_port_ptr->cmn.index,
                   other_in_port_ptr->cmn.index);
            me_ptr->has_pending_operating_media_format = TRUE;
            active_in_sdata_ptr->flags.end_of_frame    = FALSE;

            if (out_sdata_ptr)
            {
               out_sdata_ptr->flags.end_of_frame = TRUE;
            }
         }
      }
   }

   return result;
}

/**
 * Checks if there is any data buffered up in the inactive stream's delay buffer. Note that we don't have to
 * check the input stream data of the other port. If data is sitting in the input stream but not buffered,
 * the gapless delay buffer wasn't configured to be large enough.
 */
bool_t capi_gapless_other_stream_has_data(capi_gapless_t *me_ptr, capi_gapless_in_port_t *other_in_port_ptr)
{
   bool_t other_stream_has_data = FALSE;
   if (me_ptr->pass_through_mode)
   {
      // Set this is FALSE for pass-through case to ensure we only copy data from one port during one process
      // call. This is ok since gapless playback isn't guaranteed for pass_through_mode.
      other_stream_has_data = FALSE;
   }
   else
   {
      if (other_in_port_ptr)
      {
         other_stream_has_data = !capi_gapless_is_delay_buffer_empty(me_ptr, other_in_port_ptr);
      }
   }
   return other_stream_has_data;
}

/**
 * If there is an EOS metadata (either flushing or nonflushing) in the metadata list of the stream data,
 * returns it. Returns NULL otherwise.
 */
module_cmn_md_t *gapless_find_eos(capi_stream_data_v2_t *sdata_ptr)
{
   module_cmn_md_t *eos_md_ptr = NULL;

   if (!sdata_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "sdata_ptr was NULL, returning NULL.");
      return NULL;
   }

   module_cmn_md_list_t *list_ptr = sdata_ptr->metadata_list_ptr;
   while (list_ptr)
   {
      module_cmn_md_t *md_ptr = list_ptr->obj_ptr;

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         /*
		 1.Only external EOS are expected at Gapless module because it is part of first stream subgraph.
         2.Ideally, we should return the last EOS from the list, if there are multiples non-flushing EOSes at different offsets.
		 */
         eos_md_ptr = md_ptr;
         break;
      }

      list_ptr = list_ptr->next_ptr;
   }

   return eos_md_ptr;
}

/**
 * Used ONLY in pass through mode. Propagates metadata from the input directly to the output.
 */
capi_err_t gapless_propagate_metadata(capi_gapless_t *       me_ptr,
                                      capi_stream_data_v2_t *in_stream_ptr,
                                      capi_media_fmt_v2_t *  in_media_fmt_ptr,
                                      capi_stream_data_v2_t *out_stream_ptr,
                                      bool_t                 is_input_fully_consumed,
                                      uint32_t               in_size_before)
{
   capi_err_t result = CAPI_EOK;

   module_cmn_md_list_t **DUMMY_INT_LIST_PPTR = NULL;
   uint32_t               ALGO_DELAY_ZERO     = 0;

   bool input_had_eos = FALSE;
   if (in_stream_ptr->metadata_list_ptr)
   {
      uint32_t prev_out_marker_eos = out_stream_ptr->flags.marker_eos;
      bool_t   new_out_marker_eos  = FALSE;

      intf_extn_md_propagation_t input_md_info;
      memset(&input_md_info, 0, sizeof(input_md_info));
      input_md_info.df                          = in_media_fmt_ptr->header.format_header.data_format;
      input_md_info.len_per_ch_in_bytes         = out_stream_ptr->buf_ptr[0].actual_data_len;
      input_md_info.initial_len_per_ch_in_bytes = in_size_before;
      input_md_info.bits_per_sample             = in_media_fmt_ptr->format.bits_per_sample;
      input_md_info.sample_rate                 = in_media_fmt_ptr->format.sampling_rate;

      intf_extn_md_propagation_t output_md_info;
      memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
      output_md_info.initial_len_per_ch_in_bytes = 0;
      out_stream_ptr->flags.marker_eos           = FALSE;
      output_md_info.len_per_ch_in_bytes         = out_stream_ptr->buf_ptr[0].actual_data_len;

      input_had_eos = in_stream_ptr->flags.marker_eos;
      me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                  in_stream_ptr,
                                                  out_stream_ptr,
                                                  DUMMY_INT_LIST_PPTR,
                                                  ALGO_DELAY_ZERO,
                                                  &input_md_info,
                                                  &output_md_info);

      new_out_marker_eos = out_stream_ptr->flags.marker_eos;
      out_stream_ptr->flags.marker_eos |= prev_out_marker_eos;

      if (new_out_marker_eos)
      {
         AR_MSG(DBG_HIGH_PRIO, "flushing eos was propagated - clearing from input");
      }
   }

   // EOF propagation
   if (input_had_eos)
   {
      // EOF propagation during EOS: propagate only once input EOS goes to output.
      if (out_stream_ptr->flags.marker_eos && !in_stream_ptr->flags.marker_eos)
      {
         if (in_stream_ptr->flags.end_of_frame)
         {
            in_stream_ptr->flags.end_of_frame  = FALSE;
            out_stream_ptr->flags.end_of_frame = TRUE;
            AR_MSG(DBG_HIGH_PRIO, "EOF was propagated");
         }
      }
   }
   else
   {
      if (is_input_fully_consumed)
      {
         if (in_stream_ptr->flags.end_of_frame)
         {
            in_stream_ptr->flags.end_of_frame  = FALSE;
            out_stream_ptr->flags.end_of_frame = TRUE;
            AR_MSG(DBG_HIGH_PRIO, "EOF was propagated");
         }
      }
   }

   return result;
}

/**
 * Calls metadata_destroy on each node in the passed in metadata list.
 */
capi_err_t capi_gapless_destroy_md_list(capi_gapless_t *me_ptr, module_cmn_md_list_t **md_list_pptr)
{
   module_cmn_md_list_t *next_ptr = NULL;
   for (module_cmn_md_list_t *node_ptr = *md_list_pptr; node_ptr;)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      next_ptr               = node_ptr->next_ptr;
      if (me_ptr->metadata_handler.metadata_destroy)
      {
         me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                   node_ptr,
                                                   IS_DROPPED_TRUE,
                                                   md_list_pptr);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "capi ec sync: Error: metadata handler not provided, can't drop metadata.");
         return CAPI_EFAILED;
      }
      node_ptr = next_ptr;
   }

   return CAPI_EOK;
}

/**
 * The delay buffer util can't write into an output buffer with partial data. So we have to hide the
 * partial data from the delay buffer util by adjusting data pointers and setting actual data length to 0.
 * Caches the previous actual data len and flags etc to help readjust the output sdata after process.
 */
capi_err_t gapless_setup_output_sdata(capi_gapless_t *       me_ptr,
                                      capi_stream_data_v2_t *temp_sdata_ptr,
                                      capi_stream_data_v2_t *out_sdata_ptr,
                                      uint32_t *             prev_actual_data_len_per_ch_ptr)
{
   if ((!me_ptr) || (!temp_sdata_ptr) || (!out_sdata_ptr) || (!prev_actual_data_len_per_ch_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Bad arguments me_ptr 0x%lx, temp_sdata_ptr 0x%lx, out_sdata_ptr 0x%lx, prev_actual_data_len_per_ch_ptr "
             "0x%lx",
             me_ptr,
             temp_sdata_ptr,
             out_sdata_ptr,
             prev_actual_data_len_per_ch_ptr);
      return CAPI_EFAILED;
   }

   // Copy flags and metadata from out_sdata_ptr to temp_sdata_ptr. (store old values before process)
   memscpy(&(temp_sdata_ptr->flags),
           sizeof(temp_sdata_ptr->flags),
           &(out_sdata_ptr->flags),
           sizeof(out_sdata_ptr->flags));
   temp_sdata_ptr->metadata_list_ptr = out_sdata_ptr->metadata_list_ptr;

   // Remove the metadata list so we can only change offsets of new metadata after process.
   out_sdata_ptr->metadata_list_ptr = NULL;

   *prev_actual_data_len_per_ch_ptr = out_sdata_ptr->buf_ptr[0].actual_data_len;

   // Setup data pointers to point to empty space.
   for (uint32_t ch_index = 0; ch_index < out_sdata_ptr->bufs_num; ch_index++)
   {
      out_sdata_ptr->buf_ptr[ch_index].data_ptr += (*prev_actual_data_len_per_ch_ptr);
      out_sdata_ptr->buf_ptr[ch_index].actual_data_len -= (*prev_actual_data_len_per_ch_ptr);
      out_sdata_ptr->buf_ptr[ch_index].max_data_len -= (*prev_actual_data_len_per_ch_ptr);
   }
   return CAPI_EOK;
}

/**
 * The delay buffer util can't write into an output buffer with partial data. So we have to hide the
 * partial data from the delay buffer util by adjusting data pointers and setting actual data length to 0.
 * This function readjusts pointers and actual data lengths to original values. Metadata and flags are
 * also handled.
 */
capi_err_t gapless_adjust_output_sdata(capi_gapless_t *       me_ptr,
                                       capi_stream_data_v2_t *temp_sdata_ptr,
                                       capi_stream_data_v2_t *out_sdata_ptr,
                                       uint32_t               prev_actual_data_len_per_ch)
{
   bool_t ADD_TRUE = TRUE;
   if ((!me_ptr) || (!temp_sdata_ptr) || (!out_sdata_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Bad arguments me_ptr 0x%lx, temp_sdata_ptr 0x%lx, out_sdata_ptr 0x%lx.",
             me_ptr,
             temp_sdata_ptr,
             out_sdata_ptr);
      return CAPI_EFAILED;
   }

   // Extend output sdata pointers back to their original location.
   for (uint32_t ch_index = 0; ch_index < out_sdata_ptr->bufs_num; ch_index++)
   {
      out_sdata_ptr->buf_ptr[ch_index].data_ptr -= prev_actual_data_len_per_ch;
      out_sdata_ptr->buf_ptr[ch_index].actual_data_len += prev_actual_data_len_per_ch;
      out_sdata_ptr->buf_ptr[ch_index].max_data_len += prev_actual_data_len_per_ch;
   }

   // Adjust offsets of all newly generated data.
   gapless_metadata_adj_offset(&(me_ptr->operating_media_fmt),
                               out_sdata_ptr->metadata_list_ptr,
                               (prev_actual_data_len_per_ch * me_ptr->operating_media_fmt.format.num_channels),
                               ADD_TRUE);

   // Merge metadata lists and store in output_sdata_ptr. When merging, the source goes after the dest, so using
   // temp as dst to maintain metadata in sorted offset order.
   ar_result_t result = spf_list_merge_lists((spf_list_node_t **)&(temp_sdata_ptr->metadata_list_ptr),
                                             (spf_list_node_t **)&(out_sdata_ptr->metadata_list_ptr));
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Error merging metadata lists while adjusting output");
      return CAPI_EFAILED;
   }

   out_sdata_ptr->metadata_list_ptr = temp_sdata_ptr->metadata_list_ptr;

   // Flag aggregation:
   // EOF        - Original EOF would have gotten cleared already - in the case of multiple writes. Second EOF should
   //              take effect. No action needed.
   // marker_eos - Same as EOF. No action needed.
   // erasure    - Not expected in these use cases. No action needed.
   // timestamp  - Not expected in these use cases. No action needed.
   return CAPI_EOK;
}

/**
 * Adjusts offsets of all metadata in the metadata list by adding or subtracting bytes_consumed from their offsets.
 */
void gapless_metadata_adj_offset(capi_media_fmt_v2_t * med_fmt_ptr,
                                 module_cmn_md_list_t *md_list_ptr,
                                 uint32_t              bytes_consumed,
                                 bool_t                true_add_false_sub)
{
   if (md_list_ptr)
   {
      module_cmn_md_list_t *node_ptr = md_list_ptr;
      while (node_ptr)
      {
         module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

#ifdef METADATA_DEBUGGING
         AR_MSG(topo_ptr->gu.log_id,
                DBG_LOW_PRIO,
                "MD_DBG: update offset of md_ptr 0x%x md_id 0x%08lX with offset %lu by bytes_consumed %lu "
                "true_add_false_sub %d, ",
                md_ptr,
                md_ptr->metadata_id,
                md_ptr->offset,
                bytes_consumed,
                true_add_false_sub);
#endif
         gapless_do_md_offset_math(&md_ptr->offset, bytes_consumed, med_fmt_ptr, true_add_false_sub);

         node_ptr = node_ptr->next_ptr;
      }
   }
}

/**
 * Adds or subtracts bytes from the offset.
 *
 * need_to_add - TRUE: convert bytes and add to offset
 *               FALSE: convert bytes and subtract from offset
 */
void gapless_do_md_offset_math(uint32_t *           offset_ptr,
                               uint32_t             bytes,
                               capi_media_fmt_v2_t *med_fmt_ptr,
                               bool_t               need_to_add)
{
   uint32_t samples_per_ch =
      capi_cmn_bytes_to_samples_per_ch(bytes, med_fmt_ptr->format.bits_per_sample, med_fmt_ptr->format.num_channels);

   if (need_to_add)
   {
      *offset_ptr += samples_per_ch;
   }
   else
   {
      if (*offset_ptr >= samples_per_ch)
      {
         *offset_ptr -= samples_per_ch;
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "MD_DBG: offset calculation error. offset becoming negative. setting as zero");
         *offset_ptr = 0;
      }
   }
}
