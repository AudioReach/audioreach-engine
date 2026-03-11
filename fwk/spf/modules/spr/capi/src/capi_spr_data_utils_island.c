/**
 *   \file capi_spr_data_utils.c
 *   \brief
 *        This file contains CAPI implementation of Splitter Renderer Module data utilities
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_spr_i.h"

/*==============================================================================
   Local Function forward declaration
==============================================================================*/
static void   spr_fill_out_with_zeros(capi_spr_t *me_ptr, capi_stream_data_t *output_ptr);
static bool_t spr_should_input_stream_process(capi_stream_data_v2_t *input_ptr);
static bool_t spr_output_stream_has_space(capi_spr_t *me_ptr, capi_stream_data_t *output_ptr);
static void spr_process_input_metadata(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_strm_ptr);
/*==============================================================================
   Function Implementation
==============================================================================*/

/*------------------------------------------------------------------------------
  Function name: spr_setup_input_stream_to_process

  Identifies which input stream has to be consumed in this process call based
  on the following criteria

  1. The incoming stream is always considered as long as it has valid data/metadata
  2. If there is a cached media format change, then add the incoming stream to the
      tail of this list. If there is no data left in the hold buffer, then pop the
      head of the media format list and use it as the input stream to process in this
      call.
  3. Honor the time of arrival by pushing to the end of the hold list if there is no
      media format cached.
  4. The head node of the list is always used for render decision

  ----------Honor time of arrival by implementing this order----------------
  INCOMING STREAM  -> MF LIST (TAIL) or HOLD LIST (TAIL)
  RENDER DECISION  -> HOLD LIST (HEAD) or MF LIST (HEAD)
  --------------------------------------------------------------------------

 * ------------------------------------------------------------------------------*/
static capi_stream_data_v2_t *spr_setup_input_stream_to_process(capi_spr_t *         me_ptr,
                                                         capi_stream_data_t **capi_input_pptr,
                                                         capi_err_t *         result_ptr)
{
   if (!result_ptr || !capi_input_pptr)
   {
      return NULL;
   }

   capi_stream_data_v2_t *proc_strm_ptr = NULL;
   capi_stream_data_v2_t *input_ptr     = (capi_stream_data_v2_t *)(capi_input_pptr[0]);

   if (!input_ptr)
   {
      *result_ptr = CAPI_EOK;
      return NULL;
   }

   *result_ptr = CAPI_EOK;

   // Determine if this input stream should be processed
   //  TRUE if the stream has metadata/data
   //  FALSE if the stream has erasure without metadata
   bool_t process_input = spr_should_input_stream_process(input_ptr);

#ifdef DEBUG_SPR_MODULE
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_HIGH_PRIO,
           "process: incoming stream timestamp %ld, is_valid %d, process_input %d is erasure %d",
           input_ptr->timestamp,
           input_ptr->flags.is_timestamp_valid,
           process_input,
           input_ptr->flags.erasure);
#endif

   // If there is a pending media format change, then the incoming data belongs to the tail of this list
   if (spr_has_cached_mf(me_ptr))
   {
      if (process_input)
      {
         *result_ptr = capi_spr_add_input_to_mf_list(me_ptr, input_ptr);
      }

      // Fetch the head of the media format list
      spr_mf_handler_t *head_mf_obj_ptr = spr_get_cached_mf_list_head_obj_ptr(me_ptr);
      if (!head_mf_obj_ptr)
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "Failed to get head of media format list though it exists");
         *result_ptr = CAPI_EFAILED;
         return NULL;
      }
      spf_list_node_t *mf_strm_list_ptr = head_mf_obj_ptr->int_buf.buf_list_ptr;

      // If this media format is already in effect, use the head of this node's int_buffer as the input stream
      // provided there is no hold buffer
      if (head_mf_obj_ptr->is_applied && mf_strm_list_ptr && !spr_avsync_does_hold_buf_exist(me_ptr->avsync_ptr))
      {
         input_ptr     = (capi_stream_data_v2_t *)capi_spr_get_list_head_obj_ptr(mf_strm_list_ptr);
         process_input = TRUE;
      }
      else // either this media format is not applied or there is data in the hold buffer
      {
         process_input = FALSE;
      }
   }

   // if hold buffer exists, then always use the head node for render decision.
   if (spr_avsync_does_hold_buf_exist(me_ptr->avsync_ptr))
   {
      // if process_input is set to TRUE, then add it to the hold buffer list.
      if (process_input)
      {
         *result_ptr = capi_spr_add_input_to_hold_list(me_ptr, input_ptr);

         if (AR_DID_FAIL(*result_ptr))
         {
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Failed to add input stream to hold list with error 0x%x",
                    *result_ptr);
            spr_handle_metadata(me_ptr, (capi_stream_data_t **)&input_ptr, (capi_stream_data_t **)NULL, TRUE /*drop*/);
         }
      }
      // Add new input stream first and then get the head node from the updated list.
      // This is to handle the corner case where the hold buffer is overflowing (head node freed)
      proc_strm_ptr = (capi_stream_data_v2_t *)spr_avsync_get_hold_buf_head_obj_ptr(me_ptr->avsync_ptr);
   }
   else if (process_input)
   {
      // if hold buffer doesn't exist and the input has to be processed, return the input_ptr provided
      proc_strm_ptr = input_ptr;
   }
   else
   {
      // nothing to be processed in this call. (happens for erasure buffer with no metadata)
      proc_strm_ptr = NULL;
   }

   return proc_strm_ptr;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_process_input

  Operates on the data & metadata based on the render decision value as below :-

     - Render : handles metadata and writes the stream to circular buffer
     - Drop   : drops metadata & changes trigger policy to pull input
     - Hold   : if not the head of the hold buffer list, add to the tail
                else do nothing
 * ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_process_input(capi_spr_t *           me_ptr,
                                          capi_stream_data_v2_t *input_ptr,
                                          capi_stream_data_t *   output[],
                                          render_decision_t      render_decision)
{
   capi_err_t result = CAPI_EOK;

   // If input is at gap and receives valid input_strm_ptr, move to active state
   if (capi_spr_check_if_input_is_at_gap(me_ptr))
   {
      uint32_t PORT_IS_ACTIVE = 0;
      capi_spr_set_is_input_at_gap(me_ptr, PORT_IS_ACTIVE);
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "input port is active now");
#endif
   }

   if (RENDER == render_decision)
   {
      // If after drop, there is a render decision, then metadata cannot be handled till the output is available
      // Write to circular buffer only if both input and output capi streams are available i.e. timer tick
      // TODO: Revisit this part if this was render after drop. Since circular buffer does not handle metadata
      // today, this is a temporary fix.
      if (input_ptr && output)
      {
         result = spr_handle_metadata(me_ptr, (capi_stream_data_t **)&input_ptr, output, FALSE /*do not drop*/);

         // Consume input only if erasure is not set. This is to handle the case where MD comes with erasure data
         if (!input_ptr->flags.erasure)
         {
            result |= spr_stream_write(me_ptr->in_port_info_arr[0].strm_writer_ptr,
                                       input_ptr->buf_ptr,
                                       input_ptr->flags.is_timestamp_valid,
                                       input_ptr->timestamp);

#ifdef DEBUG_SPR_MODULE
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "process: Input consumed timestamp %ld is_ts_valid %d actual_data_len %d",
                    input_ptr->timestamp,
                    input_ptr->flags.is_timestamp_valid,
                    input_ptr->buf_ptr->actual_data_len);
#endif
            // handle only if avs sync is enabled
            if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
            {
               result |= capi_spr_avsync_update_input_info(me_ptr, (capi_stream_data_t *)input_ptr);
            }
         }
#ifdef DEBUG_SPR_MODULE
         else
         {
            SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Not writing erasure buffer into SPR");
         }
#endif
      } // if input & output streams exist

      capi_spr_change_trigger_policy(me_ptr, FALSE /* no need to drop*/, FALSE /* do not force */);
   }

   else if (DROP == render_decision)
   {
      // Even if output is NULL, drop metadata right away
      result = spr_handle_metadata(me_ptr, (capi_stream_data_t **)&input_ptr, output, TRUE /*drop*/);

      // Change trigger policy to drop for FTRT
      capi_spr_change_trigger_policy(me_ptr, TRUE /* need to drop*/, FALSE /*force*/);
   }

   else if (HOLD == render_decision)
   {
      // if no hold buffer is configured, then SPR cannot hold the data, drop right away
      if (!spr_avsync_is_hold_buf_configured(me_ptr->avsync_ptr))
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Hold buffer not configured. dropping %d bytes per channel",
                 input_ptr->buf_ptr->actual_data_len);
      }
      else
      {
         // Check if the render decision was made on the head of the hold buffer list. If so, do nothing.
         if (spr_avsync_is_input_strm_hold_buf_head(me_ptr->avsync_ptr, (void *)input_ptr))
         {
#ifdef AVSYNC_DEBUG
            SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "Not adding stream to hold list again");
#endif
         }
         else
         {
            // This input needs to be added to hold list.
            // If this input was from the cached mf list, then simply move the node from mf list to hold list
            // Else, create a new node for the input

            if (spr_has_cached_mf(me_ptr) && (spr_cached_mf_is_input_strm_head(me_ptr, input_ptr)))
            {

#ifdef DEBUG_SPR_MODULE
               SPR_MSG_ISLAND(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "process: input strm timestamp %ld belongs to cached mf list and is being moved to the hold "
                       "buffer. ",
                       input_ptr->timestamp);
#endif
               spr_mf_handler_t *mf_handler_ptr = spr_get_cached_mf_list_head_obj_ptr(me_ptr);

               result = capi_spr_move_int_buf_node(spr_get_cached_mf_list_head_strm_node(me_ptr),
                                                   &mf_handler_ptr->int_buf,
                                                   &me_ptr->avsync_ptr->hold_buffer,
                                                   &me_ptr->operating_mf);
            }
            else
            {
               result = capi_spr_add_input_to_hold_list(me_ptr, input_ptr);

               if (AR_DID_FAIL(result))
               {
                  SPR_MSG_ISLAND(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Failed to add input stream to hold list with error 0x%x",
                          result);
                  spr_handle_metadata(me_ptr, (capi_stream_data_t **)&input_ptr, output, TRUE /*drop*/);
               }
            }
         }

         capi_spr_change_trigger_policy(me_ptr, FALSE /*need_to_drop */, FALSE /* force */);
      }
   }
   else
   {
      result |= CAPI_EBADPARAM;
   }

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_insert_eos_for_us_gap
  	  caller has to check if this function is required to be called against
  	  the inser_eos flag
* ------------------------------------------------------------------------------*/
static void capi_spr_insert_eos_for_us_gap(capi_spr_t *me_ptr, capi_stream_data_v2_t *out_stream_ptr)
{
   module_cmn_md_t *new_md_ptr = NULL;
   uint32_t offset = 0;
   capi_heap_id_t heap_mem = {0};
   heap_mem.heap_id = me_ptr->heap_id;
   capi_err_t       res        = me_ptr->metadata_handler.metadata_create(me_ptr->metadata_handler.context_ptr,
                                                             &out_stream_ptr->metadata_list_ptr,
                                                             sizeof(module_cmn_md_eos_t),
															 heap_mem, //me_ptr->heap_mem,
                                                             FALSE /*is_out_band*/,
                                                             &new_md_ptr);
   if (CAPI_SUCCEEDED(res))
   {
      new_md_ptr->metadata_id           = MODULE_CMN_MD_ID_EOS;
      offset                            = out_stream_ptr->buf_ptr ? out_stream_ptr->buf_ptr->actual_data_len : 0;
      new_md_ptr->offset                = capi_cmn_bytes_to_samples_per_ch(offset, me_ptr->operating_mf.format.bits_per_sample, 1);
      module_cmn_md_eos_t *eos_md_ptr   = (module_cmn_md_eos_t *)&new_md_ptr->metadata_buf;
      eos_md_ptr->flags.is_flushing_eos = TRUE;
      eos_md_ptr->flags.is_internal_eos = TRUE;
      eos_md_ptr->cntr_ref_ptr          = NULL;
      new_md_ptr->tracking_ptr          = NULL;

      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "Created and inserted internal, flushing eos at output offset :%lu", new_md_ptr->offset);
   }
}



/*------------------------------------------------------------------------------
  Function name: spr_process_output_metadata
    Parse the metadata at the output port(s) of SPR and convert any flushing
    eos to non-flushing
* ------------------------------------------------------------------------------*/
static void spr_process_output_metadata(capi_spr_t *me_ptr, capi_stream_data_v2_t *output_strm_ptr)
{
   // If no MD to propagate, do nothing
   if ((!output_strm_ptr->metadata_list_ptr) && (!me_ptr->flags.insert_int_eos_for_dfg))
   {
      return;
   }

   // As SPR keeps inserting zeroes with active output ports, modify relevant MD in the output port list
   // to reflect this
   capi_err_t            result   = CAPI_EOK;
   module_cmn_md_list_t *node_ptr = output_strm_ptr->metadata_list_ptr;

   if (me_ptr->flags.is_timer_disabled)
   {
	   if(me_ptr->flags.insert_int_eos_for_dfg)
	   {
		   capi_spr_insert_eos_for_us_gap(me_ptr, output_strm_ptr);
		   output_strm_ptr->flags.marker_eos = TRUE;
		   output_strm_ptr->flags.end_of_frame = TRUE;
	   }

	  // Carry the flushing EOS on the input so that it will get carried
      output_strm_ptr->flags.marker_eos |= me_ptr->flags.has_flushing_eos;
   }
   else
   {
      while (node_ptr)
      {
         uint32_t md_id = ((module_cmn_md_t *)node_ptr->obj_ptr)->metadata_id;

         // 1. Internal EOS is dropped
         // 2. Flushing EOS is converted to non-flushing
         // 3. DFG is dropped at the input of the SPR itself.
         result = me_ptr->metadata_handler.metadata_modify_at_data_flow_start(me_ptr->metadata_handler.context_ptr,
                                                                              node_ptr,
                                                                              &output_strm_ptr->metadata_list_ptr);

         if (CAPI_FAILED(result))
         {
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "SPR_MD_DBG: Failed to modify_md ID 0x%X with error %x",
                    md_id,
                    result);
         }

         node_ptr = node_ptr->next_ptr;
      }

      // At the output of SPR, there will never be a flushing EOS. So mark this as false always
      output_strm_ptr->flags.marker_eos = FALSE;
   }
}


/*------------------------------------------------------------------------------
  Function name: capi_spr_process_output

  Reads the circular buffer & generates output. this function also handles
  underrun scenario in the circular buffer (both partial & completely empty)
 * ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_process_output(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_ptr, capi_stream_data_t *output[])
{
   capi_err_t result      = CAPI_EOK;
   int64_t    in_strm_ts  = 0;
   int64_t    input_ts    = 0;
   uint32_t   input_flag  = 0;
   bool_t     is_ts_valid = FALSE;

   // TODO: Revisit after the circular buffer updates
   if (input_ptr && !input_ptr->flags.erasure)
   {
      in_strm_ts  = input_ptr->timestamp;
      input_flag  = input_ptr->flags.word;
      is_ts_valid = input_ptr->flags.is_timestamp_valid;
   }

   for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
   {
      // Generate output only if,
      //      1. Stream reader is created [means port is opened and cfg is received on this port]
      if ((NULL == me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr))
      {
         continue;
      }

      // Get port index from the arr index.
      uint32_t          port_index                 = me_ptr->out_port_info_arr[arr_idx].port_index;
      bool_t            is_erasure                 = FALSE;
      uint32_t          num_underrun_zeroes_per_ch = 0;
      underrun_status_t status                     = 0;

      // Re-assign for every output port since input_ts can be modified as part of hold logic timestamp
      // extrapolation.
      input_ts = in_strm_ts;

      // output buffers sanity check.
      if ((NULL == output[port_index]) || (NULL == output[port_index]->buf_ptr) ||
          (NULL == output[port_index]->buf_ptr[0].data_ptr))
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Output buffers not available port_id=0x%x ", port_index);
         continue;
      }

      if (output[port_index]->buf_ptr->max_data_len > me_ptr->frame_dur_bytes_per_ch)
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "process: warning: output %d max_data_len %d is greater than frame dur %d ",
                 port_index,
                 output[port_index]->buf_ptr->max_data_len,
                 me_ptr->frame_dur_bytes_per_ch);
      }

      uint32_t prev_output_actual_data_len = output[port_index]->buf_ptr->actual_data_len;

      if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
      {
         // Check if any hold zeroes have to be filled for this buffer.
         // TODO: Partial data can be present in the circular buffer due to this. Need to migrate to
         // circular buffer utils to handle the metadata/timestamps smoothly. For now the error might be okay
         // since it is non-flushing EOS
         capi_spr_avsync_check_fill_hold_zeroes(me_ptr, (capi_stream_data_v2_t *)output[port_index], &input_ts);
      }

      if (spr_output_stream_has_space(me_ptr, (capi_stream_data_t *)output[port_index]))
      {
         // Read the data from the circular buffer
         result = spr_stream_read(me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr, output[port_index]->buf_ptr);
         if (result == AR_ENEEDMORE)
         {
            uint32_t curr_output_actual_data_len = output[port_index]->buf_ptr[0].actual_data_len;

            // It is truly an underrun only if spr is unable to deliver frame duration worth data
            if (spr_output_stream_has_space(me_ptr, (capi_stream_data_t *)output[port_index]))
            {
               // set erasure only if entire buffer is filled with zeroes (i.e empty circular buffer)
               // TODO: for now partial underrun is not treated as erasure since we do not have any metadata for it.
               if (curr_output_actual_data_len == prev_output_actual_data_len)
               {
                  is_erasure      = TRUE;
                  int64_t incr_ts = capi_cmn_bytes_to_us(me_ptr->frame_dur_bytes_per_ch,
                                                         me_ptr->operating_mf.format.sampling_rate,
                                                         me_ptr->operating_mf.format.bits_per_sample,
                                                         1 /*num_channels*/,
                                                         (uint64_t *)NULL);

                  // Timestamp is extrapolated from the previous buffer only in the case erasure buffer
                  input_ts    = me_ptr->out_port_info_arr[arr_idx].prev_output_ts + incr_ts;
                  is_ts_valid = me_ptr->out_port_info_arr[arr_idx].is_prev_ts_valid;

#ifdef DEBUG_SPR_MODULE
                  SPR_MSG_ISLAND(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "process: underrun ts update incr_ts %ld prev out ts %ld",
                          incr_ts,
                          me_ptr->out_port_info_arr[arr_idx].prev_output_ts);
#endif
               }


               if(!me_ptr->flags.is_timer_disabled)
               {
            	   num_underrun_zeroes_per_ch = output[port_index]->buf_ptr[0].max_data_len - curr_output_actual_data_len;
            	   spr_fill_out_with_zeros(me_ptr, output[port_index]);
               }
               else
               {
                  SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "Not inserting zeros as SPR Timer is disabled ");
                  num_underrun_zeroes_per_ch = 0;
               }
            }
         }
      }
      else
      {
         // Full frame of hold zeroes
         is_erasure                 = TRUE;
         num_underrun_zeroes_per_ch = output[port_index]->buf_ptr[0].actual_data_len;
      }

      uint32_t new_output_actual_data_len = output[port_index]->buf_ptr->actual_data_len;

      // If there is any flushing eos, mark it non-flushing since SPR would always underrun. The out stream eos flag is
      // set to FALSE in this process call
      spr_process_output_metadata(me_ptr, ((capi_stream_data_v2_t *)output[port_index]));

      bool_t eos_flag = output[port_index]->flags.marker_eos;
      output[port_index]->flags.word |= input_flag;
      output[port_index]->flags.marker_eos         = eos_flag;
      output[port_index]->flags.is_timestamp_valid = is_ts_valid;
      output[port_index]->flags.erasure            = is_erasure;
      // TODO: in case of variable delay, cache & pick from circular buffer? Revisit after circular buffer upgrade
      output[port_index]->timestamp = input_ts;

      if (arr_idx == me_ptr->primary_output_arr_idx && (input_ptr))
      {
         uint32_t num_bytes_filled = new_output_actual_data_len - prev_output_actual_data_len;

         if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
         {
            result |= capi_spr_avsync_update_output_info(me_ptr, is_erasure, output[port_index], num_bytes_filled);
         }
      }
      output[port_index]->timestamp                       = input_ts;
      me_ptr->out_port_info_arr[arr_idx].prev_output_ts   = input_ts;
      me_ptr->out_port_info_arr[arr_idx].is_prev_ts_valid = is_ts_valid;

      if (num_underrun_zeroes_per_ch)
      {
         capi_spr_check_raise_underrun_event(me_ptr, &status, arr_idx);
         me_ptr->underrun_info.underrun_counter++;
         bool_t is_steady_state = (DATA_NOT_AVAILABLE == status);
         if (capi_spr_check_print_underrun(&me_ptr->underrun_info, is_steady_state))
         {
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "process: underrun detected at output port index %d inserted %d zeroes (in bytes) per ch "
                    "is_erasure "
                    "%d, reason %d (no input1, hold2, drop3, inputatgap4), No of occurance since last print %d.",
                    port_index,
                    num_underrun_zeroes_per_ch,
                    is_erasure,
                    status,
                    me_ptr->underrun_info.underrun_counter);
            me_ptr->underrun_info.underrun_counter = 0;
         }
      }

#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_HIGH_PRIO,
              "process: out timestamp %ld,%ld for port index %d ts valid %d actual_data_len %d",
              (uint32_t)(input_ts>>32),
              (uint32_t)input_ts,
              port_index,
              output[port_index]->flags.is_timestamp_valid,
              output[port_index]->buf_ptr->actual_data_len);
#endif
   } // end of max_output_ports loop
   me_ptr->flags.insert_int_eos_for_dfg = FALSE;

   spr_avsync_clear_hold_duration(me_ptr->avsync_ptr);

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_evaluate_simple_process_criteria
   Evaluates if SPR can process data in an optimal fashion
 * ------------------------------------------------------------------------------*/
void capi_spr_evaluate_simple_process_criteria(capi_spr_t *me_ptr, bool_t is_input_ts_valid)
{
   if (!me_ptr)
   {
      return;
   }

   bool_t can_use_simple_process = FALSE;

   do
   {
      // SPR has to be inplace
      if (!me_ptr->flags.is_inplace)
      {
         break;
      }

      // SPR has to be programmed with render_mode IMMEDIATE
      if (SPR_RENDER_MODE_IMMEDIATE != spr_avsync_get_render_mode(me_ptr->avsync_ptr))
      {
         break;
      }

      // SPR input TS needs to be invalid to choose simple process
      if (is_input_ts_valid)
      {
         break;
      }

      can_use_simple_process = TRUE;

   } while (0);

   //#ifdef DEBUG_SPR_MODULE
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_HIGH_PRIO,
           "process: uses_simple_process [prev %d, curr %d]",
           me_ptr->flags.uses_simple_process,
           can_use_simple_process);
   //#endif

   me_ptr->flags.uses_simple_process = can_use_simple_process;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_simple_process_input
   Simplified SPR input processing utility.
 * ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_simple_process_input(capi_spr_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   if (!input) // To keep KW happy
   {
      return CAPI_EOK;
   }
   capi_stream_data_v2_t *proc_input_strm_ptr = (capi_stream_data_v2_t *)input[0];
   capi_err_t             result              = CAPI_EOK;

   // Absorb reset session time MD before processing the input
   spr_process_input_metadata(me_ptr, proc_input_strm_ptr);

   // If AVSync is enabled, update the necessary variables
   if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      avsync_t *avsync_ptr = me_ptr->avsync_ptr;
      if (!proc_input_strm_ptr->flags.erasure)
      {
         spr_avsync_set_dfg_flag(avsync_ptr, FALSE);
#ifdef AVSYNC_DEBUG_SIMPLE_PROCESS
         SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: simple process: Data flow begin");
#endif
      }

      if (UMAX_32 != me_ptr->primary_output_arr_idx)
      {
         (void)spr_aggregate_path_delay(me_ptr, &me_ptr->out_port_info_arr[me_ptr->primary_output_arr_idx]);
      }
      avsync_ptr->flags.is_first_buf_rcvd     = TRUE;
      avsync_ptr->flags.is_first_buf_rendered = TRUE;
      avsync_ptr->curr_wall_clock_us          = posal_timer_get_time();

      avsync_ptr->av_reset_info.mode = me_ptr->reset_sess_time_info.mode;
      spr_avsync_set_render_decision(avsync_ptr, RENDER);

      capi_spr_avsync_update_input_info(me_ptr, (capi_stream_data_t *)input);
#ifdef AVSYNC_DEBUG_SIMPLE_PROCESS
      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: simple process: render buffer");
#endif
   }

   // If input is at gap and receives valid input_strm_ptr, move to active state
   if (capi_spr_check_if_input_is_at_gap(me_ptr))
   {
      uint32_t PORT_IS_ACTIVE = 0;
      capi_spr_set_is_input_at_gap(me_ptr, PORT_IS_ACTIVE);
#ifdef DEBUG_SPR_MODULE_SIMPLE_PROCESS
      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "input port is active now");
#endif
   }

   if (FALSE == me_ptr->flags.has_rendered_first_buf)
   {
      me_ptr->flags.has_rendered_first_buf = TRUE;
   }

   if (input && output)
   {
      result |= spr_handle_metadata(me_ptr, input, output, FALSE /*do not drop*/);
   }
   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_simple_process_output
   Simplified SPR output processing utility.
 * ------------------------------------------------------------------------------*/
static capi_err_t capi_spr_simple_process_output(capi_spr_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   uint32_t               input_flag          = 0;
   int64_t                input_ts            = 0;
   capi_stream_data_v2_t *proc_strm_input_ptr = NULL;

   for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
   {
      // Make sure there is a valid stream reader is available i.e port is open & cfg is received
      if (NULL == me_ptr->out_port_info_arr[arr_idx].strm_reader_ptr)
      {
         continue;
      }

      uint32_t port_index = me_ptr->out_port_info_arr[arr_idx].port_index;
      bool_t   is_erasure = FALSE;

      // Sanity check for capi output buffers
      if ((NULL == output[port_index]) || (NULL == output[port_index]->buf_ptr) ||
          (NULL == output[port_index]->buf_ptr[0].data_ptr) ||
          (output[port_index]->buf_ptr->actual_data_len == output[port_index]->buf_ptr->max_data_len))
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Output buffers not available port_idx=0x%x ", port_index);
         continue;
      }

      uint32_t          prev_output_actual_data_len = output[port_index]->buf_ptr->actual_data_len;
      uint32_t          num_underrun_zeros_per_ch   = 0;
      underrun_status_t status;
      if (input && input[0] && input[0]->buf_ptr && input[0]->buf_ptr->actual_data_len)
      {
         proc_strm_input_ptr = (capi_stream_data_v2_t *)input[0];
         is_erasure          = proc_strm_input_ptr->flags.erasure;
         input_flag          = proc_strm_input_ptr->flags.word;
         input_ts            = proc_strm_input_ptr->timestamp;

         for (uint32_t buf_idx = 0; buf_idx < proc_strm_input_ptr->bufs_num; buf_idx++)
         {
            if (proc_strm_input_ptr->buf_ptr[buf_idx].data_ptr != output[port_index]->buf_ptr[buf_idx].data_ptr)
            {
               memscpy(output[port_index]->buf_ptr[buf_idx].data_ptr,
                       output[port_index]->buf_ptr[buf_idx].max_data_len,
                       proc_strm_input_ptr->buf_ptr[buf_idx].data_ptr,
                       proc_strm_input_ptr->buf_ptr[buf_idx].actual_data_len);
            }

            output[port_index]->buf_ptr[buf_idx].actual_data_len =
               SPR_MIN(proc_strm_input_ptr->buf_ptr[buf_idx].actual_data_len,
                       output[port_index]->buf_ptr[buf_idx].max_data_len);

            proc_strm_input_ptr->buf_ptr[buf_idx].actual_data_len =
               output[port_index]->buf_ptr[buf_idx].actual_data_len;
         }

         if (spr_output_stream_has_space(me_ptr, output[port_index]))
         {
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Warning: Output buffers for port_idx=0x%x not filled completely actual length = %d frame dur %d ",
                    port_index,
                    output[port_index]->buf_ptr->actual_data_len,
                    me_ptr->frame_dur_bytes_per_ch);
            if (!me_ptr->flags.is_timer_disabled)
            {
                 spr_fill_out_with_zeros(me_ptr, output[port_index]);
            }
         }
      }
      else
      {
         if (!me_ptr->flags.is_timer_disabled)
         {
            is_erasure = TRUE;
            spr_fill_out_with_zeros(me_ptr, output[port_index]);
         }
         else
         {
            SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "Not inserting zeros as SPR Timer is disabled ");
         }
      }

      if (is_erasure)
      {
         if (!me_ptr->flags.is_timer_disabled)
         {
            num_underrun_zeros_per_ch = output[port_index]->buf_ptr[0].actual_data_len;
         }
         else
         {
            SPR_MSG_ISLAND(me_ptr->miid, DBG_LOW_PRIO, "Warning: Ignoring underrun for timer disable Mode");
         }
      }

      uint32_t new_output_actual_data_len = output[port_index]->buf_ptr->actual_data_len;

      spr_process_output_metadata(me_ptr, (capi_stream_data_v2_t *)output[port_index]);

      bool_t eos_flag = output[port_index]->flags.marker_eos;
      output[port_index]->flags.word |= input_flag;
      output[port_index]->flags.marker_eos         = eos_flag;
      output[port_index]->flags.is_timestamp_valid = FALSE;
      output[port_index]->flags.erasure            = is_erasure;
      // TODO: in case of variable delay, cache & pick from circular buffer? Revisit after circular buffer upgrade
      output[port_index]->timestamp                       = input_ts;
      me_ptr->out_port_info_arr[arr_idx].prev_output_ts   = input_ts;
      me_ptr->out_port_info_arr[arr_idx].is_prev_ts_valid = FALSE;

      if (arr_idx == me_ptr->primary_output_arr_idx && (proc_strm_input_ptr))
      {
         uint32_t num_bytes_filled = new_output_actual_data_len - prev_output_actual_data_len;
         result |= capi_spr_avsync_update_output_info(me_ptr, is_erasure, output[port_index], num_bytes_filled);
      }

      if (num_underrun_zeros_per_ch)
      {
         capi_spr_check_raise_underrun_event(me_ptr, &status, arr_idx);
         me_ptr->underrun_info.underrun_counter++;
         bool_t is_steady_state = (DATA_NOT_AVAILABLE == status);
         if (capi_spr_check_print_underrun(&me_ptr->underrun_info, is_steady_state))
         {
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "simple process: underrun detected at output port index %d inserted %d zeroes (in bytes) per ch "
                    "is_erasure "
                    "%d, reason %d (no input1, hold2, drop3, inputatgap4)",
                    port_index,
                    num_underrun_zeros_per_ch,
                    is_erasure,
                    status);

            me_ptr->underrun_info.underrun_counter = 0;
         }
      }

   } // end of for loop
   me_ptr->flags.insert_int_eos_for_dfg = FALSE;
   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_fill_out_with_zeros
   Fills the capi stream with zeroes for underrun scenario
 * ------------------------------------------------------------------------------*/
static void spr_fill_out_with_zeros(capi_spr_t *me_ptr, capi_stream_data_t *output_ptr)
{
   uint32_t output_size       = MIN(output_ptr->buf_ptr->max_data_len, me_ptr->frame_dur_bytes_per_ch);
   uint32_t num_zeroes_per_ch = output_size - output_ptr->buf_ptr->actual_data_len;

   if (!num_zeroes_per_ch)
   {
      return;
   }

   for (uint32_t j = 0; j < me_ptr->operating_mf.format.num_channels; j++)
   {
      int8_t *update_ptr = output_ptr->buf_ptr[j].data_ptr + output_ptr->buf_ptr[j].actual_data_len;
      memset((void *)update_ptr, 0, num_zeroes_per_ch);

      output_ptr->buf_ptr[j].actual_data_len += num_zeroes_per_ch;
   }
}

/*------------------------------------------------------------------------------
  Function name: spr_should_input_stream_process
   Validates if the input stream can be processed or not. Any input buffer with
   data or metadata is processed
 * ------------------------------------------------------------------------------*/
static bool_t spr_should_input_stream_process(capi_stream_data_v2_t *input_ptr)
{
   // validate input ptr
   if (!input_ptr)
   {
      return FALSE;
   }

   // if input stream has metadata, always process (even with erasure)
   if (input_ptr->metadata_list_ptr)
   {
      return TRUE;
   }

   // if input stream has erasure and no metadata, do not process
   if (input_ptr->flags.erasure)
   {
      return FALSE;
   }

   // If there is no data in this stream, do not process
   if (input_ptr->buf_ptr && !input_ptr->buf_ptr->actual_data_len)
   {
      return FALSE;
   }

   // always return TRUE
   return TRUE;
}

/*------------------------------------------------------------------------------
  Function name: spr_output_stream_has_space
   Returns if the output stream has space left or not
 * ------------------------------------------------------------------------------*/
static bool_t spr_output_stream_has_space(capi_spr_t *me_ptr, capi_stream_data_t *output_ptr)
{
   if (!me_ptr || !output_ptr)
   {
      return FALSE;
   }

   if (!output_ptr->buf_ptr)
   {
      return FALSE;
   }

   return (output_ptr->buf_ptr->actual_data_len < me_ptr->frame_dur_bytes_per_ch);
}

/*------------------------------------------------------------------------------
    Function name: spr_process_input_metadata
   Processes input metadata before the input is consumed. Currently absorbs
   the reset session time metadata from gapless module & session time scale
   metadata from TSM module.
* ------------------------------------------------------------------------------*/
static void spr_process_input_metadata(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_strm_ptr)
{
   // If no MD to process, do nothing
   if (!(input_strm_ptr && input_strm_ptr->metadata_list_ptr && me_ptr))
   {
      return;
   }

   // Check if there is any MD to reset session time.
   capi_err_t            result   = CAPI_EOK;
   module_cmn_md_list_t *node_ptr = input_strm_ptr->metadata_list_ptr;
   module_cmn_md_list_t *next_ptr = NULL;
   while (node_ptr)
   {
      next_ptr       = node_ptr->next_ptr;
      uint32_t md_id = ((module_cmn_md_t *)node_ptr->obj_ptr)->metadata_id;

      if (MODULE_CMN_MD_ID_RESET_SESSION_TIME == md_id)
      {
         result = me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                            node_ptr,
                                                            TRUE /*dropped*/,
                                                            &input_strm_ptr->metadata_list_ptr);

         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "SPR_MD_DBG: Found Reset Session Time MD. Absorbed with result 0x%x",
                 result);

         if(SPR_SESSION_TIME_RESET_MODE_DEFAULT == spr_avsync_get_reset_session_time_mode(me_ptr->avsync_ptr))
         {
            capi_spr_check_raise_session_time_reset_event(me_ptr);
            capi_spr_avsync_reset_session_clock_for_gapless(me_ptr->avsync_ptr);
         }
         else
         {
            SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: skipping gapless session time reset");
         }

      }

      if(MODULE_CMN_MD_ID_SCALE_SESSION_TIME == md_id)
      {
    	  module_cmn_md_t *md_ptr = (module_cmn_md_t*)node_ptr->obj_ptr;
    	  //TODO: Assumed inband for now.
    	  md_session_time_scale_t *tsm_md_ptr  = (md_session_time_scale_t *)&(md_ptr->metadata_buf);

    	  if(is_spr_avsync_enabled(me_ptr->avsync_ptr))
    	  {
    	      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "SPR_MD_DBG: Found Scale Session Time MD");
    	      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "SPR_MD_DBG: current speed factor = %d new speed factor = %d offset = %d", me_ptr->avsync_ptr->tsm_info.tsm_speed_factor, tsm_md_ptr->speed_factor, md_ptr->offset);

            // If MD offset is not at the beginning, cache and handle the calculations accordingly.
            // Say current speed factor is 1x and new speed factor is 0.5x with an offset N,
            // For samples 0 to N -> apply speed factor 1x
            // For samples N to end -> apply new speed factor 0.5x
            if (0 != md_ptr->offset)
            {
               me_ptr->avsync_ptr->cached_tsm_info.offset       = md_ptr->offset;
               me_ptr->avsync_ptr->cached_tsm_info.speed_factor = tsm_md_ptr->speed_factor;

               me_ptr->avsync_ptr->flags.is_pending_tsm_cfg = TRUE;
            }
            else
            {
               me_ptr->avsync_ptr->tsm_info.tsm_speed_factor = tsm_md_ptr->speed_factor;
            }

            // Note that the moment we get the TSM MD, we switch to the scaled calculation mode. This is reset
            // as part of input port reset
            me_ptr->avsync_ptr->flags.is_timescaled_data = TRUE;
        }
    	  else
    	  {
    	      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "SPR_MD_DBG: AVSync not enabled. Ignoring Scale Session Time MD");
        }

    	  result = me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr, node_ptr, TRUE/*dropped*/, &input_strm_ptr->metadata_list_ptr);

         SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO,
               "SPR_MD_DBG: Found Scale Session Time MD. Absorbed with result 0x%x",
               result);
      }

      node_ptr = next_ptr;
   }
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_check_remove_processed_int_buf
   After process, check & remove the input stream from the appropriate internal list
 * ------------------------------------------------------------------------------*/
static void capi_spr_check_remove_processed_int_buf(capi_spr_t *      me_ptr,
                                                   render_decision_t render_decision,
                                                   void *            input_strm_ptr)
{
   // if decision is to hold, then, do not delete the capi stream as it is already added to the hold list
   if (HOLD == render_decision)
   {
      return;
   }

   spr_int_buffer_t *buf_ptr = NULL;
   // First check if this input belongs to any of the cached lists in SPR

   if (spr_avsync_is_input_strm_hold_buf_head(me_ptr->avsync_ptr, input_strm_ptr))
   {
      buf_ptr = &me_ptr->avsync_ptr->hold_buffer;
   }
   else if (spr_has_cached_mf(me_ptr) && (spr_cached_mf_is_input_strm_head(me_ptr, input_strm_ptr)))
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_HIGH_PRIO,
              "input strm timestamp %ld belongs to cached mf list",
              ((capi_stream_data_v2_t *)input_strm_ptr)->timestamp);
#endif
      spr_mf_handler_t *mf_handler_ptr =
         (spr_mf_handler_t *)capi_spr_get_list_head_obj_ptr(me_ptr->in_port_info_arr->mf_handler_list_ptr);
      buf_ptr = &mf_handler_ptr->int_buf;
   }

   if (buf_ptr)
   {
      capi_spr_destroy_int_buf_node(me_ptr,
                                    buf_ptr,
                                    (void *)input_strm_ptr,
                                    TRUE /* check head node */,
                                    &me_ptr->operating_mf,
                                    FALSE /* data already consumed */);

      input_strm_ptr = NULL;

      // if the node came from the cached media format list, then check & pop the mf node if all the
      // buffers are consumed
      if (!buf_ptr->is_used_for_hold)
      {
         if (!buf_ptr->buf_list_ptr)
         {
            spr_mf_handler_t *mf_handler_ptr =
               (spr_mf_handler_t *)spf_list_pop_head((spf_list_node_t **)&me_ptr->in_port_info_arr->mf_handler_list_ptr,
                                                     TRUE /*pool_used*/);
#ifdef DEBUG_SPR_MODULE
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "popped cached mf node with sr %d",
                    mf_handler_ptr->media_fmt.format.sampling_rate);
#endif

            posal_memory_free(mf_handler_ptr);

            mf_handler_ptr = NULL;
         }
      }
   }
}

/*------------------------------------------------------------------------
 Function name: capi_spr_simple_process
 Simplified process to consume an input buffer and generate an output buffer.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_spr_simple_process(capi_spr_t *me_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t result = CAPI_EOK;

   if (input)
   {
      result = capi_spr_simple_process_input(me_ptr, input, output);
   }

   if (output)
   {
      result |= capi_spr_simple_process_output(me_ptr, input, output);
   }

   return result;
}

/*------------------------------------------------------------------------
 Function name: capi_spr_process
 Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
capi_err_t capi_spr_process(capi_t *capi_ptr, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t             result              = CAPI_EOK;
   capi_stream_data_v2_t *proc_input_strm_ptr = NULL;
   render_decision_t      render_decision     = INVALID;
   bool_t                 is_input_ts_valid   = FALSE;

   if (NULL == capi_ptr)
   {
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_spr: received NULL instance pointer in process");
      return CAPI_EFAILED;
   }

   if (!input && !output)
   {
      return result;
   }

   capi_spr_t *me_ptr = (capi_spr_t *)capi_ptr;

   // Set up next timer till we get inp mf
   // After that only if it is has output buffer - it will be available only in timer triggered mode
   // not get called in drop scenario
   // if input media fmt is not received, it will set timer based on nominal frame dur

   me_ptr->flags.has_flushing_eos = FALSE;

   if (!me_ptr->flags.is_input_media_fmt_set)
   {
      result |= capi_spr_calc_set_timer(me_ptr);
      SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "process invoked without input media format set");
      return result;
   }
   else
   {
      // If Any output is present, set timer
      for (uint32_t port_index = 0; port_index < me_ptr->max_output_ports; port_index++)
      {
         if ((output[port_index]) && (output[port_index]->buf_ptr) && (output[port_index]->buf_ptr[0].data_ptr))
         {
            result |= capi_spr_calc_set_timer(me_ptr);
            break;
         }
      }
   }

   if (input)
   {
      proc_input_strm_ptr = (capi_stream_data_v2_t *)input[0];

      // If SPR has not yet processed its first input
      if (!me_ptr->flags.has_rcvd_first_buf)
      {
         // If this input buffer has to be processed, evaluate the simple process criteria
         if (spr_should_input_stream_process(proc_input_strm_ptr) && proc_input_strm_ptr)
         {
            me_ptr->flags.has_rcvd_first_buf = TRUE;
            capi_spr_evaluate_simple_process_criteria(me_ptr, proc_input_strm_ptr->flags.is_timestamp_valid);
         }
         else
         {
            // Simply output zeroes
            return capi_spr_simple_process_output(me_ptr, NULL, output);
         }
      }

      is_input_ts_valid = proc_input_strm_ptr->flags.is_timestamp_valid;
   }

   // For simple process, TS needs to be invalid always
   if (me_ptr->flags.uses_simple_process && !is_input_ts_valid)
   {
      return capi_spr_simple_process(me_ptr, input, output);
   }

   if (me_ptr->flags.uses_simple_process)
   {
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_HIGH_PRIO,
              "process: transition is_simple_process = %d is_ts_valid = %d",
              me_ptr->flags.uses_simple_process,
              is_input_ts_valid);
   }

   me_ptr->flags.uses_simple_process = FALSE;

   // Regular process with AVSync features such as hold/cached media fmt handling etc.
   // Before processing any data, check if any pending media format change can be applied
   // If there is no pending media format continue
   if (me_ptr->in_port_info_arr[0].mf_handler_list_ptr)
   {
      bool_t     was_mf_applied = FALSE;
      capi_err_t mf_result      = capi_spr_check_apply_mf_change(me_ptr, &was_mf_applied);
      if (CAPI_FAILED(mf_result))
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "error 0x%x in applying media format", mf_result);
         return mf_result;
      }

      if (was_mf_applied)
      {
         SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "media format was applied. return from process");
         return CAPI_EOK;
         // TODO: Check if it is okay to set timer at 954
      }
   }

   capi_err_t input_setup_result = CAPI_EOK;

   // If there is a hold buffer, always use the head of the list to make the render decision
   // The current input if any is pushed to the tail to honor buffer arrival time
   // Erasure buffers are considered for process only if it contains metadata
   proc_input_strm_ptr = spr_setup_input_stream_to_process(me_ptr, input, &input_setup_result);

   if (CAPI_FAILED(input_setup_result))
   {
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_ERROR_PRIO,
              "Failed to setup input stream to process with error 0x%x",
              input_setup_result);
      return input_setup_result;
   }

   if (proc_input_strm_ptr)
   {
#ifdef DEBUG_SPR_MODULE
      SPR_MSG_ISLAND(me_ptr->miid,
              DBG_LOW_PRIO,
              "processing input with timestamp %ld",
              (uint32_t)proc_input_strm_ptr->timestamp);
#endif
      // Check for reset session time metadata before making render decision
      spr_process_input_metadata(me_ptr, proc_input_strm_ptr);

      // If avsync itself is not enabled, return
      if (is_spr_avsync_enabled(me_ptr->avsync_ptr))
      {
         result |= capi_spr_avsync_setup_first_input(me_ptr, (capi_stream_data_t *)proc_input_strm_ptr);

         result |= capi_spr_avsync_make_render_decision(me_ptr, (capi_stream_data_t *)proc_input_strm_ptr, &render_decision);
      }
      else
      {
         // if av_sync is not enabled, RENDER data by default, refer capi_spr_avsync_make_render_decision()
         render_decision = RENDER;
      }

      spr_avsync_set_render_decision(me_ptr->avsync_ptr, render_decision);

      result |= capi_spr_process_input(me_ptr, proc_input_strm_ptr, output, render_decision);
   }
   else
   {
      // in case of erasure frame, the proc_input_strm_ptr would be NULL and hence indicate that no
      // render decision was made
      spr_avsync_set_render_decision(me_ptr->avsync_ptr, INVALID);
   }

   if (output)
   {
      result |= capi_spr_process_output(me_ptr, proc_input_strm_ptr, output);
   }

   // check if the input stream processed in this call was from the any of the int buffer lists and remove it.
   // currently spr has lists for hold buffer and cached media format
   capi_spr_check_remove_processed_int_buf(me_ptr, render_decision, proc_input_strm_ptr);

   return result;
}
