/**
 * \file capi_priority_sync_data_utils.c
 * \brief
 *     Implementation of utility functions for capi data handling (process function, data buffers, data processing, etc)
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_priority_sync_i.h"
#include "module_cmn_api.h"
#include "spf_list_utils.h"

/* =========================================================================
 * Static function declarations
 * =========================================================================*/
static bool_t     capi_priority_sync_sdata_has_dfg(capi_priority_sync_t *me_ptr, capi_stream_data_v2_t *sdata_ptr);
static capi_err_t capi_priority_sync_check_clear_sec_eos_dfg(capi_priority_sync_t *        me_ptr,
                                                             capi_priority_sync_in_port_t *sec_in_port_ptr,
                                                             capi_stream_data_v2_t *       sec_in_sdata_ptr);

static capi_err_t capi_priority_sync_sdata_destroy_eos_dfg_metadata(capi_priority_sync_t * me_ptr,
                                                                    capi_stream_data_v2_t *sdata_ptr);

/* =========================================================================
 * Function definitions
 * =========================================================================*/

/**
 * Checks each input port for presence of eof/eos/dfg. If found:
 * 1. Sets port state to stop.
 * 2. Drops internally buffered data on that port.
 * 3. Propogates data flow gap on output.
 */
capi_err_t capi_priority_sync_check_handle_flow_gap(capi_priority_sync_t *me_ptr,
                                                    capi_stream_data_t *  input[],
                                                    capi_stream_data_t *  output[])
{
   capi_err_t capi_result           = CAPI_EOK;
   uint32_t   NUM_PATHS             = 2;
   uint32_t   PRIMARY_PATH_LOOP_IDX = 0;

   // if there is no EOF then nothing to handle.
   if (!(me_ptr->primary_in_port_info.flags.proc_ctx_has_eof || me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof))
   {
      return CAPI_EOK;
   }

   // If primary has EOF then set it on secondary as well to reset the secondary path.
   me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof = TRUE;

   PS_MSG(me_ptr->miid,
          DBG_HIGH_PRIO,
          "handling EOF, primary_has_eof %d, secondary_has_eof %d",
          me_ptr->primary_in_port_info.flags.proc_ctx_has_eof,
          me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof);

   for (uint32_t path_idx = 0; path_idx < NUM_PATHS; path_idx++)
   {
      bool_t                        is_primary = (path_idx == PRIMARY_PATH_LOOP_IDX);
      capi_priority_sync_in_port_t *in_port_ptr =
         is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
      uint32_t in_index = in_port_ptr->cmn.index;

      capi_priority_sync_out_port_t *out_port_ptr =
         is_primary ? &me_ptr->primary_out_port_info : &me_ptr->secondary_out_port_info;
      uint32_t out_index = out_port_ptr->cmn.index;

      // if some EOF related metadata is stuck in capi/internal buffer then propagate it to output.
      if (in_port_ptr->flags.proc_ctx_has_eof)
      {
         // if path is running and EOS/DFG is not already sent on output then propagate metadata from internal buffer
         if (PRIORITY_SYNC_FLOW_STATE_FLOWING == in_port_ptr->cmn.flow_state &&
             !in_port_ptr->flags.proc_ctx_sent_eos_dfg)
         {
            //if output port is started then can propagate metadata to output
            if (DATA_PORT_STATE_STARTED == out_port_ptr->cmn.state)
            {
               if (input[in_index]->flags.end_of_frame)
               {
                  // move all mds from capi input buffer to the end of capi output buffer
                  capi_priority_sync_manual_metadata_prop_single_port(me_ptr,
                                                                      in_port_ptr,
                                                                      (capi_stream_data_v2_t *)input[in_index],
                                                                      (capi_stream_data_v2_t *)output[out_index]);
               }
               else
               {
                  // move all mds from internal buffer to the end of capi output buffer
                  capi_priority_sync_manual_metadata_prop_single_port(me_ptr,
                                                                      in_port_ptr,
                                                                      &in_port_ptr->int_stream,
                                                                      (capi_stream_data_v2_t *)output[out_index]);
               }
            }
            //if output is not started then drop the metadata from input
            else
            {
               capi_stream_data_v2_t *in_v2_stream_ptr = (capi_stream_data_v2_t *)input[in_index];

               // Destroy metadata associated with this port
               capi_priority_sync_destroy_md_list(me_ptr, &(in_v2_stream_ptr->metadata_list_ptr));
               in_v2_stream_ptr->flags.marker_eos = 0;

               in_port_ptr->flags.proc_ctx_sec_cleared_eos_dfg |=
                  (in_port_ptr->flags.proc_ctx_has_dfg || in_port_ptr->flags.proc_ctx_has_eos) ? TRUE : FALSE;
            }
         }

         // clear the internal buffer (drop data and md) now.
         capi_priority_sync_clear_buffered_data(me_ptr, is_primary);

         // clear the eof flag here if path exists
         if(PRIORITY_SYNC_PORT_INDEX_INVALID != in_index)
         {
            input[in_index]->flags.end_of_frame = FALSE;
         }
      }

      // For all started ports, check if data flow gap flag is set or eos was sent. If so, that port becomes stopped.
      if (PRIORITY_SYNC_FLOW_STATE_FLOWING == in_port_ptr->cmn.flow_state)
      {
         if (in_port_ptr->flags.proc_ctx_sent_eos_dfg || in_port_ptr->flags.proc_ctx_sec_cleared_eos_dfg)
         {
            capi_result |= capi_priority_sync_in_port_flow_gap(me_ptr, is_primary);
         }
      }

      if (DATA_PORT_STATE_CLOSED != in_port_ptr->cmn.state && DATA_PORT_STATE_CLOSED != out_port_ptr->cmn.state &&
          capi_priority_sync_media_fmt_is_valid(me_ptr, is_primary))
      {
         // Set EOF on both output
         // this is to ensure that for the next module, both ports satisfies trigger condition.

         // if EOF is propagated on one output port with zero data and another output port also has zero data then not
         // setting eof on both ports will result in hang.
         output[out_index]->flags.end_of_frame = TRUE;

         // If the primary port sent eos but the secondary port didn't, clone eos onto the secondary port. Downstream
         // modules must be informed of data flow state.
         if (is_primary && in_port_ptr->flags.proc_ctx_sent_eos_dfg)
         {
            capi_priority_sync_in_port_t * sec_in_port_ptr  = &me_ptr->secondary_in_port_info;
            capi_priority_sync_out_port_t *sec_out_port_ptr = &me_ptr->secondary_out_port_info;
            uint32_t                       sec_out_index    = sec_out_port_ptr->cmn.index;

            if (!sec_in_port_ptr->flags.proc_ctx_sent_eos_dfg && (DATA_PORT_STATE_STARTED == sec_out_port_ptr->cmn.state) &&
                capi_priority_sync_media_fmt_is_valid(me_ptr, FALSE))
            {
               // Find the EOS metadata on the output port.
               capi_stream_data_v2_t *pri_out_sdata_ptr = (capi_stream_data_v2_t *)output[out_index];
               capi_stream_data_v2_t *sec_out_sdata_ptr = (capi_stream_data_v2_t *)output[sec_out_index];

               if (NULL == sec_out_sdata_ptr)
               {
                  PS_MSG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "can't send EOS on secondary port, secondary out sdata is NULL.");
                  return CAPI_EFAILED;
               }

               bool_t                eos_dfg_found = FALSE;
               module_cmn_md_list_t *list_ptr      = pri_out_sdata_ptr->metadata_list_ptr;
               module_cmn_md_list_t *node_ptr      = NULL;
               while (list_ptr)
               {
                  module_cmn_md_t *md_ptr = list_ptr->obj_ptr;
                  if ((MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id) || (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id))
                  {
                     node_ptr      = list_ptr;
                     eos_dfg_found = TRUE;
                     break;
                  }

                  list_ptr = list_ptr->next_ptr;
               }

               if (!eos_dfg_found)
               {
                  PS_MSG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "primary port sent eos/dfg this process call but couldn't "
                         "find EOS/DFG in the primary output mdlist.");
                  return CAPI_EFAILED;
               }

               PS_MSG(me_ptr->miid,
                      DBG_HIGH_PRIO,
                      "cloning EOS/DFG from primary to secondary path (idx %ld).",
                      sec_out_index);

               module_cmn_md_list_t *metadata_list_ptr = NULL;
               capi_result = me_ptr->metadata_handler.metadata_clone(me_ptr->metadata_handler.context_ptr,
                                                                     node_ptr->obj_ptr,
                                                                     &metadata_list_ptr,
                                                                     me_ptr->heap_info);

               if (CAPI_SUCCEEDED(capi_result))
               {
                  module_cmn_md_t *md_ptr = metadata_list_ptr->obj_ptr;
                  md_ptr->offset = capi_cmn_bytes_to_samples_per_ch(sec_out_sdata_ptr->buf_ptr[0].actual_data_len,
                                                                    sec_in_port_ptr->media_fmt.format.bits_per_sample,
                                                                    1);
                  spf_list_merge_lists((spf_list_node_t **)&sec_out_sdata_ptr->metadata_list_ptr,
                                       (spf_list_node_t **)&metadata_list_ptr);

                  sec_out_sdata_ptr->flags.marker_eos    = (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id);
                  sec_out_sdata_ptr->flags.end_of_frame  = TRUE;
                  sec_in_port_ptr->flags.proc_ctx_sent_eos_dfg = TRUE;
                  sec_in_port_ptr->flags.proc_ctx_has_eof      = TRUE;
               }
            }
         }
      }
   }

   return capi_result;
}

/**
 * Recalculate threshold bytes based on threshold and media format.
 */
static capi_err_t capi_priority_sync_calc_threshold_bytes_and_delay(capi_priority_sync_t *me_ptr, capi_priority_sync_in_port_t *in_port_ptr )
{
   capi_err_t capi_result = CAPI_EOK;
   bool_t     is_primary  = (in_port_ptr == &me_ptr->primary_in_port_info);

   // Use primary input media format as reference, though both ports should have the same media format.
   uint32_t us                         = me_ptr->module_config.threshold_us;
   uint32_t sr                         = in_port_ptr->media_fmt.format.sampling_rate;
   uint32_t bps                        = in_port_ptr->media_fmt.format.bits_per_sample;
   in_port_ptr->threshold_bytes_per_ch = capi_cmn_us_to_bytes_per_ch(us, sr, bps);

   if (!me_ptr->is_ts_based_sync)
   {
      if (!is_primary)
      {
         // extra milisecond data is allocated for secondary port. We allow one ms of jitter between primary and
         // secondary.
         in_port_ptr->delay_bytes_per_ch = capi_cmn_us_to_bytes_per_ch(PRIORITY_SYNC_UPSTREAM_FRAME_SIZE_US, sr, bps);
      }
   }
   else
   {
      // extra space added to allow for zero padding during timestamp based synchronization.
      in_port_ptr->delay_bytes_per_ch = capi_cmn_us_to_bytes_per_ch(PRIORITY_SYNC_TS_SYNC_WINDOW_US, sr, bps);
   }

   PS_MSG(me_ptr->miid,
          DBG_HIGH_PRIO,
          "port index = %ld, threshold_bytes_per_ch %lu, delay_bytes_per_ch %lu",
          in_port_ptr->cmn.index,
          in_port_ptr->threshold_bytes_per_ch,
          in_port_ptr->delay_bytes_per_ch);

   return capi_result;
}

/**
 * Helper function to allocate capi buffer memory for the port buffer.
 */
capi_err_t capi_priority_sync_allocate_port_buffer(capi_priority_sync_t *        me_ptr,
                                                   capi_priority_sync_in_port_t *in_port_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_priority_sync_calc_threshold_bytes_and_delay(me_ptr, in_port_ptr);

   uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;

   PS_MSG(me_ptr->miid, DBG_MED_PRIO, "allocating input port buffer, port index = %ld", in_port_ptr->cmn.index);

   // If it already exists, free the buffer.
   if (in_port_ptr->int_stream.buf_ptr)
   {
      capi_priority_sync_deallocate_port_buffer(me_ptr, in_port_ptr);
   }

   // Allocate buffers to fit the threshold plus one extra upstream frame.
   uint32_t buf_size_bytes = in_port_ptr->delay_bytes_per_ch + in_port_ptr->threshold_bytes_per_ch;

   // Allocate and zero memory. Memory is allocated in one contiguous block, starting out with
   // capi_buf_t structures and followed by data.
   uint32_t mem_size = (sizeof(capi_buf_t) + buf_size_bytes) * num_channels;

   in_port_ptr->int_stream.buf_ptr =
      (capi_buf_t *)posal_memory_malloc(mem_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
   if (NULL == in_port_ptr->int_stream.buf_ptr)
   {
      PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Couldn't allocate memory for input port buffer.");
      return CAPI_ENOMEMORY;
   }
   memset(in_port_ptr->int_stream.buf_ptr, 0, sizeof(capi_buf_t) * num_channels);

   // Struct memory is already addressable from the bufs_ptr array. Use mem_ptr to setup channel pointers
   // starting at address following capi_buf_t structs.
   int8_t *mem_ptr = ((int8_t *)(in_port_ptr->int_stream.buf_ptr)) + (sizeof(capi_buf_t) * num_channels);

   in_port_ptr->int_stream.bufs_num = num_channels;
   for (uint32_t ch = 0; ch < num_channels; ch++)
   {
      capi_buf_t *buf_ptr      = &in_port_ptr->int_stream.buf_ptr[ch];
      buf_ptr->actual_data_len = 0;
      buf_ptr->max_data_len    = buf_size_bytes;
      buf_ptr->data_ptr        = mem_ptr;

      // Move mem_ptr to next channel.
      mem_ptr += buf_size_bytes;
   }

   // to reset flag/md
   capi_priority_sync_clear_buffered_data(me_ptr, (in_port_ptr == &me_ptr->primary_in_port_info));

   return capi_result;
}

/**
 * Helper function to free capi buffer memory for the port buffer.
 */
void capi_priority_sync_deallocate_port_buffer(capi_priority_sync_t *me_ptr, capi_priority_sync_in_port_t *in_port_ptr)
{
   if (in_port_ptr->int_stream.buf_ptr)
   {
      posal_memory_free(in_port_ptr->int_stream.buf_ptr);
      in_port_ptr->int_stream.buf_ptr  = NULL;
      in_port_ptr->int_stream.bufs_num = 0;
   }
}

/**
 * Check if passed in port (primary/secondary) has threshold amount of data buffered. More data
 * cannot be buffered since the size of the buffers is the same as the threshold amount.
 * Boolean passed to either check in the process buffer (input) or in the me_ptr internal buffer.
 */
static bool_t capi_priority_sync_port_meets_threshold(capi_priority_sync_t *me_ptr,
                                                      bool_t                is_primary,
                                                      capi_stream_data_t *  input[],
                                                      bool_t                check_process_buf)
{
   capi_buf_t *                  buf_ptr = NULL;
   capi_priority_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
   uint32_t data_len = 0;

   if (DATA_PORT_STATE_STARTED == in_port_ptr->cmn.state)
   {
      if (in_port_ptr->int_stream.buf_ptr)
      {
         data_len = in_port_ptr->int_stream.buf_ptr[0].actual_data_len;
      }
      else
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "capi priority sync process(): ERROR input port not provided. is_primary: %ld",
                is_primary);
         return FALSE;
      }

      if (check_process_buf)
      {
         buf_ptr = input[in_port_ptr->cmn.index]->buf_ptr;

         if (!buf_ptr)
         {
            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "capi priority sync process(): ERROR input port not provided. is_primary: %ld",
                   is_primary);
            return FALSE;
         }
         data_len += buf_ptr->actual_data_len;
      }

      // Assume all channels have same amount of data buffered.
      return data_len >= in_port_ptr->threshold_bytes_per_ch;
   }

   return FALSE;
}

/**
 * Mark input as unconsumed.
 */
capi_err_t capi_priority_sync_mark_input_unconsumed(capi_priority_sync_t *me_ptr,
                                                    capi_stream_data_t *  input[],
                                                    bool_t                is_primary)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t in_index = is_primary ? me_ptr->primary_in_port_info.cmn.index : me_ptr->secondary_in_port_info.cmn.index;
   capi_priority_sync_in_port_t *in_port_ptr =
      is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
   // handle only started ports.
   if (DATA_PORT_STATE_STARTED == in_port_ptr->cmn.state)
   {
      // Validate that port indices are present. Otherwise skip for this port.
      if ((!(input[in_index] && input[in_index]->buf_ptr)))
      {
         PS_MSG(me_ptr->miid,
                DBG_MED_PRIO,
                "input is_primary %ld not present, can't be marked as unconsumed.",
                is_primary);
         return capi_result;
      }

      for (uint32_t ch = 0; ch < input[in_index]->bufs_num; ch++)
      {
         input[in_index]->buf_ptr[ch].actual_data_len = 0;
      }
   }

   return capi_result;
}

/**
 * Buffers data on input into port buffers. Find port indices into input arg based on id-idx mapping.
 * This checks if threshold is exceeded on either port and errors if so.
 * If exceeded on secondary we need to drop old data and print. If exceeded on input we need to
 * send out and then later buffer remaining input (TODO(claguna) check if required).
 */
capi_err_t capi_priority_sync_buffer_new_data(capi_priority_sync_t *me_ptr,
                                              capi_stream_data_t *  input[],
                                              bool_t                buffer_primary,
                                              bool_t                buffer_secondary)
{
   capi_err_t capi_result = CAPI_EOK;

   // Constants to help loop over primary and secondary paths.
   uint32_t NUM_PATHS    = 2;
   uint32_t PRIMARY_PATH = 0;

   for (uint32_t path_idx = 0; path_idx < NUM_PATHS; path_idx++)
   {
      bool_t                        is_primary = (path_idx == PRIMARY_PATH);
      capi_priority_sync_in_port_t *in_port_ptr =
         is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
      uint32_t               in_index     = in_port_ptr->cmn.index;

      // Skip the port if we're not supposed to buffer it.
      if ((is_primary && !buffer_primary) || (!is_primary && !buffer_secondary) ||
          (DATA_PORT_STATE_STARTED != in_port_ptr->cmn.state))
      {
         continue;
      }

      capi_stream_data_v2_t *input_v2_ptr = (capi_stream_data_v2_t *)input[in_index];

      if (!in_port_ptr->int_stream.buf_ptr)
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "capi priority sync process(): input buffer not allocated for port: is_primary = %ld",
                is_primary);
         return CAPI_EFAILED;
      }

      if (!(input_v2_ptr && input_v2_ptr->buf_ptr))
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "capi priority sync process(): input not present for port: is_primary = %ld",
                is_primary);
         return CAPI_EFAILED;
      }

      uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;

#ifdef CAPI_PRIORITY_SYNC_DEBUG
      PS_MSG(me_ptr->miid,
             DBG_MED_PRIO,
             "buffering data on port is_primary = %ld. data in internal buffer before: %ld of %ld per channel, "
             "timestamp lsw %lu",
             is_primary,
             in_port_ptr->int_stream.buf_ptr[0].actual_data_len,
             in_port_ptr->int_stream.buf_ptr[0].max_data_len,
             (uint32_t)in_port_ptr->int_stream.timestamp);

      PS_MSG(me_ptr->miid,
             DBG_MED_PRIO,
             "buffering data on port is_primary = %ld. New data to buffer: %ld per channel. timestam lsw %lu, is valid "
             "%d",
             is_primary,
             input_v2_ptr->buf_ptr[0].actual_data_len,
             (uint32_t)input_v2_ptr->timestamp,
             input_v2_ptr->flags.is_timestamp_valid);
#endif

      uint32_t input_bytes_before         = input_v2_ptr->buf_ptr[0].actual_data_len;
      uint32_t bytes_in_buf_before_per_ch = in_port_ptr->int_stream.buf_ptr[0].actual_data_len;
      uint32_t bytes_in_buf_after_per_ch  = 0;
      uint32_t in_bytes_consumed_per_ch   = 0;
      uint32_t drop_size_per_ch           = 0;

      // Copy data from input to buffer.
      for (uint32_t ch = 0; ch < num_channels; ch++)
      {
         int8_t *write_ptr =
            in_port_ptr->int_stream.buf_ptr[ch].data_ptr + in_port_ptr->int_stream.buf_ptr[ch].actual_data_len;
         uint32_t write_size =
            in_port_ptr->int_stream.buf_ptr[ch].max_data_len - in_port_ptr->int_stream.buf_ptr[ch].actual_data_len;
         uint32_t copy_size = memscpy(write_ptr,
                                      write_size,
                                      input_v2_ptr->buf_ptr[ch].data_ptr,
                                      input_v2_ptr->buf_ptr[0].actual_data_len);

         in_port_ptr->int_stream.buf_ptr[ch].actual_data_len += copy_size;

         if (copy_size != input_v2_ptr->buf_ptr[ch].actual_data_len)
         {
            // 1. data is not dropped for primary.
            // 2. data is also not dropped when timestamp based synchronization is enabled. Because dropping the data
            //    will result timestamp to go out of sync.
            // 3. if eos/dfg is already cleared in this proc-context then all input data must be consumed.
            // 4. if parameter is set to avoid buffer overflow on secondary path (for ICMD)
            if (!in_port_ptr->flags.proc_ctx_sec_cleared_eos_dfg &&
                (is_primary || (me_ptr->is_ts_based_sync) || (me_ptr->avoid_sec_buf_overflow)))
            {
               PS_MSG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Error: Can't buffer all input on port is_primary: %ld! input amt = "
                      "%ld bytes, able to buffer %ld bytes. Remaining bytes marked as unconsumed.",
                      is_primary,
                      input_v2_ptr->buf_ptr[ch].actual_data_len,
                      copy_size);
               input_v2_ptr->buf_ptr[ch].actual_data_len = copy_size;
            }
            else
            {
               drop_size_per_ch = input_v2_ptr->buf_ptr[ch].actual_data_len - copy_size;

#ifdef CAPI_PRIORITY_SYNC_DEBUG
               PS_MSG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Overflowed buffer on secondary path. This is expected if only "
                      "secondary path is running. Scooting over data to make room for new data, and dropping oldest "
                      "data. Amount dropped = %ld",
                      drop_size_per_ch);
#endif

               // First move data over to make room for remaining data.
               write_ptr          = in_port_ptr->int_stream.buf_ptr[ch].data_ptr;
               write_size         = in_port_ptr->int_stream.buf_ptr[ch].max_data_len;
               int8_t * read_ptr  = in_port_ptr->int_stream.buf_ptr[ch].data_ptr + drop_size_per_ch;
               uint32_t read_size = in_port_ptr->int_stream.buf_ptr[ch].actual_data_len - drop_size_per_ch;
               memsmove(write_ptr, write_size, read_ptr, read_size);
               in_port_ptr->int_stream.buf_ptr[ch].actual_data_len -= drop_size_per_ch;

               // Now copy the remaining data.
               write_ptr =
                  in_port_ptr->int_stream.buf_ptr[ch].data_ptr + in_port_ptr->int_stream.buf_ptr[ch].actual_data_len;
               write_size = in_port_ptr->int_stream.buf_ptr[ch].max_data_len -
                            in_port_ptr->int_stream.buf_ptr[ch].actual_data_len;
               read_ptr  = input_v2_ptr->buf_ptr[ch].data_ptr + copy_size;
               read_size = drop_size_per_ch;
               memsmove(write_ptr, write_size, read_ptr, read_size);
               copy_size = memscpy(write_ptr, write_size, read_ptr, read_size);

               in_port_ptr->int_stream.buf_ptr[ch].actual_data_len += copy_size;
            }
         }
      }

      // Copy timestamp to input port if it is valid.
      if (input_v2_ptr->flags.is_timestamp_valid && !in_port_ptr->int_stream.flags.is_timestamp_valid &&
          input_bytes_before)
      {
         in_port_ptr->int_stream.timestamp =
            input_v2_ptr->timestamp - capi_cmn_bytes_to_us(bytes_in_buf_before_per_ch,
                                                           in_port_ptr->media_fmt.format.sampling_rate,
                                                           in_port_ptr->media_fmt.format.bits_per_sample,
                                                           1, // Per channel
                                                           NULL);
         in_port_ptr->int_stream.flags.is_timestamp_valid = TRUE;
      }
      else if (!input_v2_ptr->flags.is_timestamp_valid && me_ptr->is_ts_based_sync)
      {
         // if incoming timestamp is invalid then validate the internal buffer timestamp.
         // in this case, interpolated timestamp from the internal buffer will be used
         in_port_ptr->int_stream.flags.is_timestamp_valid = TRUE;
      }

      // If data was dropped in the internal buffer, also drop metadata in the internal buffer and shift the rest of the
      // offsets accordingly.
      if (0 != drop_size_per_ch)
      {
         bool_t   SUBTRACT_FALSE = FALSE;
         bool_t   ADD_TRUE       = TRUE;
         uint32_t offset_end     = 0;

         capi_priority_sync_do_md_offset_math(&offset_end,
                                              drop_size_per_ch * num_channels,
                                              &in_port_ptr->media_fmt,
                                              ADD_TRUE);

         capi_priority_sync_destroy_md_within_range(me_ptr,
                                                    &in_port_ptr->media_fmt,
                                                    &(in_port_ptr->int_stream.metadata_list_ptr),
                                                    offset_end);

         capi_priority_sync_adj_offset(&in_port_ptr->media_fmt,
                                       in_port_ptr->int_stream.metadata_list_ptr,
                                       drop_size_per_ch * num_channels,
                                       SUBTRACT_FALSE);

         // Adjust timestamp to input port if it is valid.
         if (in_port_ptr->int_stream.flags.is_timestamp_valid)
         {
            in_port_ptr->int_stream.timestamp += capi_cmn_bytes_to_us(drop_size_per_ch,
                                                                      in_port_ptr->media_fmt.format.sampling_rate,
                                                                      in_port_ptr->media_fmt.format.bits_per_sample,
                                                                      1, // Per channel
                                                                      NULL);
         }

         //adjust initial bytes in internal buffer based on the dropped data.
         //it will be used later to propagate metadata.
         bytes_in_buf_before_per_ch =
            bytes_in_buf_before_per_ch > drop_size_per_ch ? (bytes_in_buf_before_per_ch - drop_size_per_ch) : 0;
      }

      bytes_in_buf_after_per_ch = in_port_ptr->int_stream.buf_ptr[0].actual_data_len;
      in_bytes_consumed_per_ch  = input_v2_ptr->buf_ptr[0].actual_data_len;

      if (me_ptr->metadata_handler.metadata_propagate)
      {
         uint32_t                   ALGO_DELAY_ZERO = 0; // Sync has no algo delay.
         intf_extn_md_propagation_t input_md_info;
         memset(&input_md_info, 0, sizeof(input_md_info));
         input_md_info.df                          = in_port_ptr->media_fmt.header.format_header.data_format;
         input_md_info.len_per_ch_in_bytes         = in_bytes_consumed_per_ch;
         input_md_info.initial_len_per_ch_in_bytes = input_bytes_before;
         input_md_info.buf_delay_per_ch_in_bytes   = 0;
         input_md_info.bits_per_sample             = in_port_ptr->media_fmt.format.bits_per_sample;
         input_md_info.sample_rate                 = in_port_ptr->media_fmt.format.sampling_rate;

         intf_extn_md_propagation_t output_md_info;
         memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
         output_md_info.len_per_ch_in_bytes         = (bytes_in_buf_after_per_ch - bytes_in_buf_before_per_ch);
         output_md_info.initial_len_per_ch_in_bytes = bytes_in_buf_before_per_ch;

         me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                     input_v2_ptr,
                                                     &(in_port_ptr->int_stream),
                                                     NULL,
                                                     ALGO_DELAY_ZERO,
                                                     &input_md_info,
                                                     &output_md_info);

#ifdef CAPI_PRIORITY_SYNC_DEBUG
         PS_MSG(me_ptr->miid,
                DBG_HIGH_PRIO,
                "capi priority sync md_prop(): in_index = %d input consumed = %d algo delay = %d buff data before = %d "
                "after prop eos in %d eos out %d",
                in_index,
                in_bytes_consumed_per_ch,
                ALGO_DELAY_ZERO,
                bytes_in_buf_before_per_ch,
                input_v2_ptr->flags.marker_eos,
                in_port_ptr->int_stream.flags.marker_eos);
#endif

         // update end of frame for internal buffer
         if (input_bytes_before == in_bytes_consumed_per_ch)
         {
            // if full input is consumed then copy from input stream directly
            in_port_ptr->int_stream.flags.end_of_frame = input_v2_ptr->flags.end_of_frame;
            input_v2_ptr->flags.end_of_frame           = FALSE;
         }

         //check if internal buffer has dfg/eos/eof
         in_port_ptr->flags.proc_ctx_has_dfg = capi_priority_sync_sdata_has_dfg(me_ptr, &in_port_ptr->int_stream);
         in_port_ptr->flags.proc_ctx_has_eos = in_port_ptr->int_stream.flags.marker_eos;
         in_port_ptr->flags.proc_ctx_has_eof = in_port_ptr->int_stream.flags.end_of_frame;
      }

#ifdef CAPI_PRIORITY_SYNC_DEBUG
      PS_MSG(me_ptr->miid,
             DBG_MED_PRIO,
             "capi priority sync process(): buffering data on port is_primary = %ld. space in internal buffer after: "
             "%ld "
             "of %ld. numbers are for single channel.",
             is_primary,
	     in_port_ptr->int_stream.buf_ptr[0].actual_data_len,
	     in_port_ptr->int_stream.buf_ptr[0].max_data_len);
#endif
   }

   return capi_result;
}

/**
 * Send all buffered data/md/eof/timestamp through output.
 * Ports should have threshold amountof data buffered when calling this function if not,
 * then pads data with zeros up to the threshold length.
 * If starting then zeros areo padded at the beginning of data,
 * If stopping the zeros are padded at the end of data.
 */
capi_err_t capi_priority_sync_send_buffered_data(capi_priority_sync_t *     me_ptr,
                                                        capi_stream_data_t *       output[],
                                                        capi_priority_sync_state_t synced_state)
{
   capi_err_t capi_result = CAPI_EOK;

   //Buffer validation
   {
      capi_priority_sync_in_port_t * pri_in_port_ptr  = &me_ptr->primary_in_port_info;
      capi_priority_sync_out_port_t *pri_out_port_ptr = &me_ptr->primary_out_port_info;
      capi_priority_sync_in_port_t * sec_in_port_ptr  = &me_ptr->secondary_in_port_info;
      capi_priority_sync_out_port_t *sec_out_port_ptr = &me_ptr->secondary_out_port_info;

      uint32_t pri_out_index = pri_out_port_ptr->cmn.index;
      uint32_t sec_out_index = sec_out_port_ptr->cmn.index;


      if (!(output[pri_out_index]->buf_ptr && output[pri_out_index]->buf_ptr[0].data_ptr))
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "output not present for primary port: index = %ld, ptr = 0x%lx",
                pri_out_index,
                output[pri_out_index]->buf_ptr);
         return CAPI_EFAILED;
      }

      // Primary input port must have threshold amount of data,
      // partial data is never sent from priority sync even in case of EOF.
      if (pri_in_port_ptr->int_stream.buf_ptr[0].actual_data_len < pri_in_port_ptr->threshold_bytes_per_ch)
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "can't send buffered data, less than threshold amt is buffered on "
                "primary "
                "input: %ld of %ld bytes per ch.",
                pri_in_port_ptr->int_stream.buf_ptr[0].actual_data_len,
                pri_in_port_ptr->threshold_bytes_per_ch);
         return CAPI_EFAILED;
      }

      // output buffer should have space for threshold amount of data.
      if (output[pri_out_index]->buf_ptr[0].max_data_len < pri_in_port_ptr->threshold_bytes_per_ch)
      {
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "can't send buffered data, less than threshold amt was provided on "
                "primary "
                "output: %ld of %ld bytes per ch.",
                output[pri_out_index]->buf_ptr[0].max_data_len,
                pri_in_port_ptr->threshold_bytes_per_ch);
         return CAPI_EFAILED;
      }

      bool_t secondary_ready = (DATA_PORT_STATE_STARTED == sec_out_port_ptr->cmn.state) &&
                               capi_priority_sync_media_fmt_is_valid(me_ptr, FALSE);

      // If secondary downstream is NRT then we should not check the buffer availability or the size.
      // -Downstream may be hung and will not provide buffer, we can not hang the primary path because of that.
      //	in this case, try to output as much data as possible and drop the rest.
      if (secondary_ready && (PRIORITY_SYNC_FTRT != me_ptr->secondary_out_port_info.cmn.prop_state.ds_rt))
      {
         if (!(output[sec_out_index]->buf_ptr && output[sec_out_index]->buf_ptr[0].data_ptr))
         {
            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "output not present for secondary port: "
                   "index = %ld, ptr = 0x%lx",
                   sec_out_index,
                   output[sec_out_index]->buf_ptr);
            return CAPI_EFAILED;
         }

         // output buffer should have space for threshold amount of data.
         if (output[sec_out_index]->buf_ptr[0].max_data_len < sec_in_port_ptr->threshold_bytes_per_ch)
         {
            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "can't send buffered data, less than threshold amt was provided on "
                   "secondary output: %ld of %ld bytes per ch.",
                   output[sec_out_index]->buf_ptr[0].max_data_len,
                   sec_in_port_ptr->threshold_bytes_per_ch);
            return CAPI_EFAILED;
         }
      }
   }

   // Constants to help loop over primary and secondary paths.
   uint32_t NUM_PATHS    = 2;
   uint32_t PRIMARY_PATH = 0;
   for (uint32_t path_idx = 0; path_idx < NUM_PATHS; path_idx++)
   {
      bool_t                        is_primary = (path_idx == PRIMARY_PATH);
      capi_priority_sync_in_port_t *in_port_ptr =
         is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
      capi_priority_sync_out_port_t *out_port_ptr =
         is_primary ? &me_ptr->primary_out_port_info : &me_ptr->secondary_out_port_info;

      uint32_t               capi_out_index = out_port_ptr->cmn.index;

      bool_t out_port_ready = (DATA_PORT_STATE_STARTED == out_port_ptr->cmn.state) &&
                              (CAPI_DATA_FORMAT_INVALID_VAL != in_port_ptr->media_fmt.format.num_channels);

      if (!(out_port_ready && output[capi_out_index]->buf_ptr && output[capi_out_index]->buf_ptr[0].data_ptr))
      {
         continue;
      }

      uint32_t num_channels = in_port_ptr->media_fmt.format.num_channels;

      uint32_t bytes_consumed_from_int_buf = 0;
      uint32_t bytes_before_in_int_buf     = in_port_ptr->int_stream.buf_ptr[0].actual_data_len;

      uint32_t read_size = MIN(bytes_before_in_int_buf, in_port_ptr->threshold_bytes_per_ch);

      int64_t ts = in_port_ptr->int_stream.timestamp;

      uint32_t zero_bytes_per_ch = in_port_ptr->threshold_bytes_per_ch - read_size;

      bool_t prepend_zeros =
         (!is_primary) && (0 < zero_bytes_per_ch) && (PRIORITY_SYNC_STATE_STARTING == me_ptr->synced_state);
      uint32_t prepended_zeros = 0;

      //for timestamp based sync; prepending should not happen.
      if (prepend_zeros)
      {
         prepended_zeros = MIN(zero_bytes_per_ch, output[capi_out_index]->buf_ptr[0].max_data_len);

         PS_MSG(me_ptr->miid,
                DBG_MED_PRIO,
                "prepending secondary port with %ld bytes per channel of initial zeros",
                prepended_zeros);

         // Prepend secondary port with initial zeros.
         for (uint32_t ch = 0; ch < num_channels; ch++)
         {
            int8_t *write_ptr = output[capi_out_index]->buf_ptr[ch].data_ptr;
            memset(write_ptr, 0, prepended_zeros);
            output[capi_out_index]->buf_ptr[ch].actual_data_len = prepended_zeros;
         }

         // Adjust secondary timestamp by subtracting duration of zeros.
         if (in_port_ptr->int_stream.flags.is_timestamp_valid)
         {
            uint64_t *FRACT_TIME_PTR_NULL = NULL;
            uint32_t  NUM_CH_1            = 1;
            uint32_t  zeros_us            = capi_cmn_bytes_to_us(prepended_zeros,
                                                     in_port_ptr->media_fmt.format.sampling_rate,
                                                     in_port_ptr->media_fmt.format.bits_per_sample,
                                                     NUM_CH_1,
                                                     FRACT_TIME_PTR_NULL);
            ts -= (int64_t)zeros_us;
         }
      }

      // Copy data from buffer to output.
      for (uint32_t ch = 0; ch < num_channels; ch++)
      {
         uint32_t copy_size =
            memscpy(output[capi_out_index]->buf_ptr[ch].data_ptr + output[capi_out_index]->buf_ptr[ch].actual_data_len,
                    output[capi_out_index]->buf_ptr[ch].max_data_len -
                       output[capi_out_index]->buf_ptr[ch].actual_data_len,
                    in_port_ptr->int_stream.buf_ptr[ch].data_ptr,
                    read_size);

         bytes_consumed_from_int_buf = copy_size;

         output[capi_out_index]->buf_ptr[ch].actual_data_len += copy_size;
         in_port_ptr->int_stream.buf_ptr[ch].actual_data_len -= copy_size;

         // Moving unconsumed data to the top.
         if (in_port_ptr->int_stream.buf_ptr[ch].actual_data_len)
         {
            // Write to the beginning of the buffer.
            int8_t * write_ptr        = in_port_ptr->int_stream.buf_ptr[ch].data_ptr;
            uint32_t write_size       = in_port_ptr->int_stream.buf_ptr[ch].max_data_len;
            int8_t * read_ptr         = in_port_ptr->int_stream.buf_ptr[ch].data_ptr + copy_size;
            uint32_t unconsumed_bytes = in_port_ptr->int_stream.buf_ptr[ch].actual_data_len;
            memscpy(write_ptr, write_size, read_ptr, unconsumed_bytes);
         }
      }

      if (output[capi_out_index]->buf_ptr[0].actual_data_len < in_port_ptr->threshold_bytes_per_ch)
      {
         // Do the trailing zero padding.
         uint32_t trailing_zero_bytes_per_ch =
            in_port_ptr->threshold_bytes_per_ch - output[capi_out_index]->buf_ptr[0].actual_data_len;

         PS_MSG(me_ptr->miid,
                DBG_MED_PRIO,
                "postpending is_primary %d port with %ld bytes per channel of "
                "trailing "
                "zeros",
                is_primary,
                trailing_zero_bytes_per_ch);

         for (uint32_t ch = 0; ch < num_channels; ch++)
         {
            uint32_t write_offset = output[capi_out_index]->buf_ptr[ch].actual_data_len;

            int8_t * write_ptr  = output[capi_out_index]->buf_ptr[ch].data_ptr + write_offset;
            uint32_t write_size = output[capi_out_index]->buf_ptr[ch].max_data_len - write_offset;
            write_size          = MIN(write_size, trailing_zero_bytes_per_ch);

            memset(write_ptr, 0, write_size);

            output[capi_out_index]->buf_ptr[ch].actual_data_len += write_size;
         }
      }

      // Propagate metadata
      if (me_ptr->metadata_handler.metadata_propagate)
      {
         uint32_t                   ALGO_DELAY_ZERO = 0;
         intf_extn_md_propagation_t input_md_info;
         memset(&input_md_info, 0, sizeof(input_md_info));
         input_md_info.df                          = in_port_ptr->media_fmt.header.format_header.data_format;
         input_md_info.len_per_ch_in_bytes         = bytes_consumed_from_int_buf;
         input_md_info.initial_len_per_ch_in_bytes = bytes_before_in_int_buf;
         // This would only be needed for prepending zeros, which only happens on the secondary port.
         input_md_info.buf_delay_per_ch_in_bytes = 0;
         input_md_info.bits_per_sample           = in_port_ptr->media_fmt.format.bits_per_sample;
         input_md_info.sample_rate               = in_port_ptr->media_fmt.format.sampling_rate;

         intf_extn_md_propagation_t output_md_info;
         memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));
         output_md_info.initial_len_per_ch_in_bytes = prepended_zeros;
         output_md_info.len_per_ch_in_bytes         = output[capi_out_index]->buf_ptr[0].actual_data_len - prepended_zeros;
         output_md_info.buf_delay_per_ch_in_bytes   = 0;

         bool_t prev_eos_out                      = output[capi_out_index]->flags.marker_eos;
         output[capi_out_index]->flags.marker_eos = FALSE;

         capi_result |= me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                                    &(in_port_ptr->int_stream),
                                                                    (capi_stream_data_v2_t*)output[capi_out_index],
                                                                    NULL,
                                                                    ALGO_DELAY_ZERO,
                                                                    &input_md_info,
                                                                    &output_md_info);

         // if internal buffer is fully consumed then propagate the eof.
         if (bytes_before_in_int_buf == bytes_consumed_from_int_buf)
         {
            output[capi_out_index]->flags.end_of_frame = in_port_ptr->int_stream.flags.end_of_frame;
            in_port_ptr->int_stream.flags.end_of_frame = FALSE;

            // if end of frame is set then check if eos or dfg is moved
            if (output[capi_out_index]->flags.end_of_frame)
            {
               // if eos or dfg is moved from internal buffer to the output then mark flag which will be used to move
               // the input port to at-gap.
               if (in_port_ptr->flags.proc_ctx_has_eos && !(in_port_ptr->int_stream.flags.marker_eos))
               {
                  in_port_ptr->flags.proc_ctx_sent_eos_dfg = TRUE;
               }
               else if (in_port_ptr->flags.proc_ctx_has_dfg &&
                        !(capi_priority_sync_sdata_has_dfg(me_ptr, &in_port_ptr->int_stream)))
               {
                  in_port_ptr->flags.proc_ctx_sent_eos_dfg = TRUE;
               }
            }
         }

         output[capi_out_index]->flags.marker_eos |= prev_eos_out;

#ifdef CAPI_PRIORITY_SYNC_DEBUG
         PS_MSG(me_ptr->miid,
                DBG_HIGH_PRIO,
                "out_index = %d out gen = %d per ch. flags.proc_ctx_sent_eos_dfg  %d",
                capi_out_index,
                output[capi_out_index]->buf_ptr[0].actual_data_len,
		in_port_ptr->flags.proc_ctx_sent_eos_dfg);
#endif
      }

      // adjust the metadata in internal buffer
      if (bytes_before_in_int_buf != bytes_consumed_from_int_buf)
      {
         bool_t SUBTRACT_FALSE = FALSE;

         capi_priority_sync_adj_offset(&in_port_ptr->media_fmt,
                                       in_port_ptr->int_stream.metadata_list_ptr,
                                       (bytes_consumed_from_int_buf * in_port_ptr->media_fmt.format.num_channels),
                                       SUBTRACT_FALSE);
      }

      // Assign timestamps if they are valid. This should be done before dropping data, which
      // resets the timestamps/is_valid fields.
      if (in_port_ptr->int_stream.flags.is_timestamp_valid)
      {
         output[capi_out_index]->timestamp                = ts; //adjusted for prepended zeros

         //invalidating the timestamp for secondary output port.
         //this can cause discontinuity if zeros were rendered earlier.
         output[capi_out_index]->flags.is_timestamp_valid = (is_primary)? TRUE: FALSE;

         // interpolate the timestamp in the internal buffer in case if incoming timestamp is invalid.
         in_port_ptr->int_stream.timestamp += capi_cmn_bytes_to_us(bytes_consumed_from_int_buf,
                                                                   in_port_ptr->media_fmt.format.sampling_rate,
                                                                   in_port_ptr->media_fmt.format.bits_per_sample,
                                                                   1, /*Per channel*/
                                                                   NULL);

         // Internal buffer timestamp will be updated when new data is buffered
         in_port_ptr->int_stream.flags.is_timestamp_valid = FALSE;
      }
   }

   return capi_result;
}

/**
 * Clear all internally buffered data on the primary or secondary port.
 */
capi_err_t capi_priority_sync_clear_buffered_data(capi_priority_sync_t *me_ptr, bool_t primary_path)
{
   capi_err_t                    capi_result = CAPI_EOK;
   capi_priority_sync_in_port_t *in_port_ptr =
      primary_path ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
   capi_buf_t *bufs_ptr = in_port_ptr->int_stream.buf_ptr;

   // Nothing to do if buffer wasn't allocated yet.
   if (bufs_ptr && bufs_ptr[0].actual_data_len > 0)
   {
      PS_MSG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "data dropped: is_primary_path %lu, bytes_per_ch %lu",
             primary_path,
             bufs_ptr[0].actual_data_len);
      // Drop data by setting actual data length back to zero.
      for (uint32_t ch = 0; ch < in_port_ptr->int_stream.bufs_num; ch++)
      {
         bufs_ptr[ch].actual_data_len = 0;
      }
   }

   // Reset timestamp.
   in_port_ptr->int_stream.flags.word = 0;
   in_port_ptr->int_stream.timestamp  = 0;

   in_port_ptr->int_stream.flags.stream_data_version = CAPI_STREAM_V2;

   //mark data discontinuous
   in_port_ptr->data_continous = FALSE;

   // Destroy metadata associated with this port
   capi_priority_sync_destroy_md_list(me_ptr, &(in_port_ptr->int_stream.metadata_list_ptr));

   return capi_result;
}

//function to update the data-flow state and to set the proc context flags.
capi_err_t priority_sync_check_for_started_ports(capi_priority_sync_t *me_ptr,
                                                        capi_stream_data_t *  input[],
                                                        capi_stream_data_t *  output[],
                                                        bool_t *              any_data_found_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (!any_data_found_ptr)
   {
      return CAPI_EFAILED;
   }

   if (!input)
   {
      return CAPI_EFAILED;
   }

   bool_t   any_data_found        = FALSE;
   uint32_t NUM_PATHS             = 2;
   uint32_t PRIMARY_PATH_LOOP_IDX = 0;

   // Check if any input ports got started.
   for (uint32_t path_idx = 0; path_idx < NUM_PATHS; path_idx++)
   {
      bool_t                        is_primary = (path_idx == PRIMARY_PATH_LOOP_IDX);
      capi_priority_sync_in_port_t *port_ptr =
         is_primary ? &me_ptr->primary_in_port_info : &me_ptr->secondary_in_port_info;
      uint32_t in_index = port_ptr->cmn.index;

      // reset flags.proc_ctx flags.
      port_ptr->flags.word = 0;

      // Skip ports that aren't started.
      if (DATA_PORT_STATE_STARTED == port_ptr->cmn.state)
      {
         bool_t found_data_on_port = capi_priority_sync_input_has_data(input, in_index);
         any_data_found |= found_data_on_port;

         // For all at_gap input ports, check if data was received. If so, that input port becomes started.
         if (PRIORITY_SYNC_FLOW_STATE_AT_GAP == port_ptr->cmn.flow_state)
         {
            if (found_data_on_port)
            {
               PS_MSG(me_ptr->miid,
                      DBG_MED_PRIO,
                      "input port idx %ld is_primary %ld received first data after stop.Moving to start state.",
                      in_index,
                      is_primary);

               //drop if there is any data stuck from the previous time.
               capi_priority_sync_clear_buffered_data(me_ptr, is_primary);
               port_ptr->cmn.flow_state = PRIORITY_SYNC_FLOW_STATE_FLOWING;

               capi_priority_sync_handle_is_rt_property(me_ptr);
               capi_priority_sync_handle_tg_policy(me_ptr);

               if (!port_ptr->int_stream.buf_ptr)
               {
                  PS_MSG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "Error: Port state moved to started yet internal buffer was not allocated, is_primary %ld",
                         is_primary);
                  return CAPI_EFAILED;
               }
            }
         }

         // if data is discontinuous then there must be valid timestamp for synchronization.
         if (!port_ptr->data_continous && found_data_on_port && me_ptr->is_ts_based_sync &&
             !input[in_index]->flags.is_timestamp_valid)
         {
            PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Port index %d has invalid timestamp", in_index);
            // return CAPI_EFAILED;
         }

         // Set flags.proc_ctx flags.
         port_ptr->flags.proc_ctx_has_eof = (input && input[in_index]) ? input[in_index]->flags.end_of_frame : FALSE;
         port_ptr->flags.proc_ctx_has_dfg =
            capi_priority_sync_sdata_has_dfg(me_ptr, (input ? (capi_stream_data_v2_t *)input[in_index] : NULL));
         port_ptr->flags.proc_ctx_has_eos  = (input && input[in_index]) ? input[in_index]->flags.marker_eos : FALSE;

         if (port_ptr->flags.proc_ctx_has_eof)
         {
            PS_MSG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Port index %d, is_eof %d, is_eos %d, is_dfg %d",
                   in_index,
                   port_ptr->flags.proc_ctx_has_eof,
                   port_ptr->flags.proc_ctx_has_eos,
                   port_ptr->flags.proc_ctx_has_dfg);
         }

         // Secondary port eos/dfg can be absorbed since it will continue to send zeros even when not receiving data.
         if (!is_primary)
         {
            capi_stream_data_v2_t *sec_in_sdata_ptr = (capi_stream_data_v2_t *)(input ? input[in_index] : 0);
            (void)capi_priority_sync_check_clear_sec_eos_dfg(me_ptr, port_ptr, sec_in_sdata_ptr);
         }
      }
   }

   *any_data_found_ptr = any_data_found;
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_priority_sync_check_clear_sec_eos_dfg
  Checks to clear secondary port's eos/dfg. These can be absorbed since the secondary port will
  always send data as long as data appears on the primary path.
  TODO(claguna): Secondary port should generate internal EOS when primary gets eos or dfg. But right now
                 it doesn't matter since fluence will absorb it.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_priority_sync_check_clear_sec_eos_dfg(capi_priority_sync_t *        me_ptr,
                                                             capi_priority_sync_in_port_t *sec_in_port_ptr,
                                                             capi_stream_data_v2_t *       sec_in_sdata_ptr)
{

   capi_err_t capi_result = CAPI_EOK;

   sec_in_port_ptr->flags.proc_ctx_sec_cleared_eos_dfg =
      sec_in_port_ptr->flags.proc_ctx_has_eos || sec_in_port_ptr->flags.proc_ctx_has_dfg;

   if (sec_in_port_ptr->flags.proc_ctx_has_eos)
   {
      sec_in_sdata_ptr->flags.marker_eos = FALSE;
      sec_in_port_ptr->flags.proc_ctx_has_eos  = FALSE;
   }

   if (sec_in_port_ptr->flags.proc_ctx_has_dfg)
   {
      sec_in_port_ptr->flags.proc_ctx_has_dfg = FALSE;
   }

   if (sec_in_port_ptr->flags.proc_ctx_sec_cleared_eos_dfg)
   {
      capi_priority_sync_sdata_destroy_eos_dfg_metadata(me_ptr, sec_in_sdata_ptr);
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: priority_sync_process
  Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
capi_err_t priority_sync_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t            capi_result        = CAPI_EOK;
   capi_priority_sync_t *me_ptr             = (capi_priority_sync_t *)_pif;
   bool_t                PRIMARY_PATH       = TRUE;
   bool_t                SECONDARY_PATH     = FALSE;
   bool_t                ENABLE_THRESHOLD   = TRUE;
   bool_t                CHECK_PROCESS_BUF  = TRUE;
   bool_t                CHECK_INTERNAL_BUF = FALSE;
   bool_t                SEND_PRIMARY       = TRUE;
   bool_t                any_data_found     = FALSE;

   if (!me_ptr->module_config.threshold_us)
   {
#ifdef CAPI_PRIORITY_SYNC_DEBUG
      PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi priority sync process(): Threshold not set, nothing to do.");
#endif

      return CAPI_EFAILED;
   }

   if (CAPI_EOK != (capi_result = priority_sync_check_for_started_ports(me_ptr, input, output, &any_data_found)))
   {
      return capi_result;
   }

   // Handle the case where no data was sent. In this case we need to only handle data flow gap/propagate metadata and
   // return.
   if (!any_data_found)
   {
#ifdef CAPI_PRIORITY_SYNC_DEBUG
      PS_MSG(me_ptr->miid,
             DBG_MED_PRIO,
             "No data sent. Handling data gap flag and returning.");
#endif

      capi_result |= capi_priority_sync_check_handle_flow_gap(me_ptr, input, output);
      return capi_result;
   }

   if ((me_ptr->primary_out_port_info.cmn.state == DATA_PORT_STATE_STARTED) &&
       (output[me_ptr->primary_out_port_info.cmn.index]) &&
       (output[me_ptr->primary_out_port_info.cmn.index]->buf_ptr) &&
       (output[me_ptr->primary_out_port_info.cmn.index]->buf_ptr[0].max_data_len <
        me_ptr->primary_in_port_info.threshold_bytes_per_ch))
   {
      PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "insufficient bytes on primary output port.");
      // Don't consume any input, mark input as 0 length.
      capi_priority_sync_mark_input_unconsumed(me_ptr, input, PRIMARY_PATH);
      capi_priority_sync_mark_input_unconsumed(me_ptr, input, SECONDARY_PATH);
      return CAPI_ENEEDMORE;
   }

   switch (me_ptr->synced_state)
   {
      case PRIORITY_SYNC_STATE_SYNCED:
      {
         if (!capi_priority_sync_is_path_running(me_ptr, PRIMARY_PATH))
         {
            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "state is synced but primary port is not yet running. "
                   "This shouldn't happen.");
            return CAPI_EFAILED;
         }

         bool_t pri_doesnt_meet_threshold =
            !capi_priority_sync_port_meets_threshold(me_ptr, PRIMARY_PATH, input, CHECK_PROCESS_BUF);

         bool_t sec_is_running = capi_priority_sync_is_path_running(me_ptr, SECONDARY_PATH);
         bool_t sec_doesnt_meet_threshold =
            sec_is_running &&
            (!capi_priority_sync_port_meets_threshold(me_ptr, SECONDARY_PATH, input, CHECK_PROCESS_BUF));
         bool_t send_secondary = sec_is_running;

         // if secondary doesn't have threshold amount of data and its upstream is FTRT then mark the eof.
         // no data will be sent on secondary output and internal buffer will be cleared.
         if (sec_is_running && sec_doesnt_meet_threshold &&
             (PRIORITY_SYNC_FTRT == me_ptr->secondary_in_port_info.cmn.prop_state.us_rt))
         {
            me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof = TRUE;
         }

         // If for the primary port, threshold isn't met while eof is set, there's no point in consuming
         // data since the EC will drop it. So drop the data right here. Move all metadata directly to the output.
         // It makes sense to drop data for both primary and secondary since secondary data alone is useless.
         if (pri_doesnt_meet_threshold && me_ptr->primary_in_port_info.flags.proc_ctx_has_eof)
         {
            PS_MSG(me_ptr->miid,
                   DBG_MED_PRIO,
                   "in steady state but did not receive threshold amount of data. "
                   "Eof case, dropping data. pri_doesnt_meet_threshold");

            // Drop input (this happens implicitly by keeping actual data length unchanged without generating/buffering
            // input), and drop input in internal buffers.
            capi_priority_sync_clear_buffered_data(me_ptr, PRIMARY_PATH);
            capi_priority_sync_clear_buffered_data(me_ptr, SECONDARY_PATH);

            capi_priority_sync_check_handle_flow_gap(me_ptr, input, output);

            // Begin synchronizing primary and secondary data.
            me_ptr->synced_state = PRIORITY_SYNC_STATE_STARTING;

            bool_t DISABLE_THRESHOLD = FALSE;
            // Disable threshold.
            capi_priority_sync_raise_event_toggle_threshold(me_ptr, DISABLE_THRESHOLD);

            return capi_result;
         }

         // If the secondary port doesn't meet the threshold, but has an EOF set for cases other than dfg/eos,
         // we can drop the secondary data and continue. If there's EOF as well as dfg/eos, normal handling
         // will take care to pad zeros and propagate metadata.
         if (sec_doesnt_meet_threshold && me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof &&
             (!(me_ptr->secondary_in_port_info.flags.proc_ctx_has_dfg || me_ptr->secondary_in_port_info.flags.proc_ctx_has_eos ||
                me_ptr->secondary_in_port_info.flags.proc_ctx_sec_cleared_eos_dfg)))
         {
            PS_MSG(me_ptr->miid,
                   DBG_MED_PRIO,
                   "in steady state but did not receive threshold amount of data. "
                   "Eof/non dfg-eos case, dropping data. sec_doesnt_meet_threshold.");

            capi_priority_sync_clear_buffered_data(me_ptr, SECONDARY_PATH);
            send_secondary = FALSE;
         }

         // Check if for either port, threshold isn't met while eof is not set. In this case don't consume any data.
         // This shouldn't happen so return CAPI_ENEEDMORE.
         if ((pri_doesnt_meet_threshold && (!me_ptr->primary_in_port_info.flags.proc_ctx_has_eof)) ||
             (sec_doesnt_meet_threshold && (!me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof)))
         {
            // Don't consume any input, mark input as 0 length.
            capi_priority_sync_mark_input_unconsumed(me_ptr, input, PRIMARY_PATH);
            capi_priority_sync_mark_input_unconsumed(me_ptr, input, SECONDARY_PATH);

            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "in steady state but did not receive threshold amount of data. "
                   "Not consuming any data. pri_doesnt_meet_threshold %ld sec_doesnt_meet_threshold %ld",
                   pri_doesnt_meet_threshold,
                   sec_doesnt_meet_threshold);
            return CAPI_ENEEDMORE;
         }

         PS_MSG(me_ptr->miid,
                DBG_MED_PRIO,
                "steady state, doing pass-through. secondary port send data? %ld.",
                send_secondary);

         // Pass through both ports by buffering and then sending from buffer. Need to do this in case
         // the secondary port has data in its buffer.
         capi_result |= capi_priority_sync_buffer_new_data(me_ptr, input, SEND_PRIMARY, send_secondary);
         capi_result |= capi_priority_sync_send_buffered_data(me_ptr, output, me_ptr->synced_state);
         capi_priority_sync_check_handle_flow_gap(me_ptr, input, output);

         //if any port had eof then disable threshold to resync.
         if (me_ptr->primary_in_port_info.flags.proc_ctx_has_eof ||
             me_ptr->secondary_in_port_info.flags.proc_ctx_has_eof)
         {
            // Begin synchronizing primary and secondary data.
            me_ptr->synced_state = PRIORITY_SYNC_STATE_STARTING;

            bool_t DISABLE_THRESHOLD = FALSE;
            // Disable threshold.
            capi_priority_sync_raise_event_toggle_threshold(me_ptr, DISABLE_THRESHOLD);
         }
         return capi_result;
      }
      case PRIORITY_SYNC_STATE_STARTING:
      {

#ifdef CAPI_PRIORITY_SYNC_DEBUG
         PS_MSG(me_ptr->miid, DBG_MED_PRIO, "sync stage, buffering data.");
#endif

         // At this point there is no data gap. So normal starting handling is below.

         // Buffer new data.
         bool_t send_primary   = capi_priority_sync_is_path_running(me_ptr, PRIMARY_PATH);
         bool_t send_secondary = capi_priority_sync_is_path_running(me_ptr, SECONDARY_PATH);
         capi_result |= capi_priority_sync_buffer_new_data(me_ptr, input, send_primary, send_secondary);

         // Send data if we meet the threshold on primary port. If not, we just buffer data and wait for more data.
         if (capi_priority_sync_port_meets_threshold(me_ptr, PRIMARY_PATH, input, CHECK_INTERNAL_BUF))
         {
#ifdef CAPI_PRIORITY_SYNC_DEBUG
            PS_MSG(me_ptr->miid,
                   DBG_MED_PRIO,
                   "capi priority sync process(): sync stage, threshold met. Sending out data and moving to "
                   "steady state.");
#endif

            // Send out buffered data, padding initial zeros to partial secondary.
            capi_result |= capi_priority_sync_send_buffered_data(me_ptr, output, me_ptr->synced_state);
            me_ptr->synced_state = PRIORITY_SYNC_STATE_SYNCED;

            // Enable threshold.
            capi_result |= capi_priority_sync_raise_event_toggle_threshold(me_ptr, ENABLE_THRESHOLD);
            (void)capi_priority_sync_handle_tg_policy(me_ptr);
         }

         // Check for port stop. If so we should drop all data and return.
         capi_priority_sync_check_handle_flow_gap(me_ptr, input, output);

         break;
      }
      default:
      {
         PS_MSG(me_ptr->miid,
                DBG_MED_PRIO,
                "capi priority sync process(): unexpected port state %ld. returning error.");
         capi_result = CAPI_EFAILED;
         break;
      }
   }

   // If threshold is disabled then return ENEEDMORE so that fwk continues buffering
   capi_result |= (me_ptr->threshold_is_disabled) ? CAPI_ENEEDMORE : CAPI_EOK;
   return capi_result;
}

/**
 * Checks if the sdata_ptr has any DFG metadata in it by looping through the metadata list.
 * TODO(claguna): Does this belong in a common place? If so, md_id to find could be an argument.
 */
static bool_t capi_priority_sync_sdata_has_dfg(capi_priority_sync_t *me_ptr, capi_stream_data_v2_t *sdata_ptr)
{
   bool_t has_dfg = FALSE;

   if (!sdata_ptr)
   {
      PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi sync capi_sync_sdata_has_dfg sdata_ptr was NULL, returning FALSE.");
      return FALSE;
   }

   module_cmn_md_list_t *list_ptr = sdata_ptr->metadata_list_ptr;
   while (list_ptr)
   {
      module_cmn_md_t *md_ptr = list_ptr->obj_ptr;
      if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
      {
         has_dfg = TRUE;
         break;
      }

      list_ptr = list_ptr->next_ptr;
   }

   return has_dfg;
}

/**
 * Checks if the sdata_ptr has any DFG metadata in it by looping through the metadata list.
 * TODO(claguna): Does this belong in a common place? If so, md_id to find could be an argument.
 */
static capi_err_t capi_priority_sync_sdata_destroy_eos_dfg_metadata(capi_priority_sync_t * me_ptr,
                                                                    capi_stream_data_v2_t *sdata_ptr)
{
   if (!sdata_ptr)
   {
      return CAPI_EOK;
   }

   module_cmn_md_list_t *list_ptr = sdata_ptr->metadata_list_ptr;
   module_cmn_md_list_t *next_ptr = NULL;
   while (list_ptr)
   {
      next_ptr                = list_ptr->next_ptr;
      module_cmn_md_t *md_ptr = list_ptr->obj_ptr;

      bool_t destroy_md =
         ((MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id) || (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id));

      if (destroy_md)
      {
         if (me_ptr->metadata_handler.metadata_destroy)
         {
            bool_t IS_DROPPED_TRUE = TRUE;
            me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                      list_ptr,
                                                      IS_DROPPED_TRUE,
                                                      &sdata_ptr->metadata_list_ptr);
         }
         else
         {
            PS_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "capi priority sync: Error: metadata handler not provided, can't drop metadata.");
            return CAPI_EFAILED;
         }
      }

      list_ptr = next_ptr;
   }
   return CAPI_EOK;
}

/**
 * Utility function to propagate metadata during EOF handling.
 * it assumes that all input is consumed and no output is generated.
 */
capi_err_t capi_priority_sync_manual_metadata_prop_single_port(capi_priority_sync_t *        me_ptr,
                                                               capi_priority_sync_in_port_t *in_port_ptr,
                                                               capi_stream_data_v2_t *       in_stream_ptr,
                                                               capi_stream_data_v2_t *       out_stream_ptr)
{
   capi_err_t            result                = CAPI_EOK;
   bool_t                prev_eos_out          = FALSE;
   uint32_t              ALGO_DELAY_ZERO       = 0;
   module_cmn_md_list_t *DUMMY_INT_MD_LIST_PTR = NULL;

   if (!in_stream_ptr || !out_stream_ptr)
   {
      return result;
   }

   // Process meta data only if stream version v2 and eof markers are set
   if ((CAPI_STREAM_V2 != in_stream_ptr->flags.stream_data_version) || !in_stream_ptr->metadata_list_ptr ||
       !in_stream_ptr->flags.end_of_frame)
   {
      return result;
   }

   PS_MSG(me_ptr->miid, DBG_HIGH_PRIO, "propagating md due to EOF, in_port_index %lu", in_port_ptr->cmn.index);

   // Cache old eos value. Assumed output side eos is not used in metadata_propagate.
   prev_eos_out                     = out_stream_ptr->flags.marker_eos;
   out_stream_ptr->flags.marker_eos = FALSE;

   intf_extn_md_propagation_t input_md_info;
   memset(&input_md_info, 0, sizeof(input_md_info));
   input_md_info.df = in_port_ptr->media_fmt.header.format_header.data_format;

   // considering full input consumed
   input_md_info.initial_len_per_ch_in_bytes = (in_stream_ptr->buf_ptr) ? in_stream_ptr->buf_ptr[0].actual_data_len : 0;
   input_md_info.len_per_ch_in_bytes         = (in_stream_ptr->buf_ptr) ? in_stream_ptr->buf_ptr[0].actual_data_len : 0;

   input_md_info.bits_per_sample = in_port_ptr->media_fmt.format.bits_per_sample;
   input_md_info.sample_rate     = in_port_ptr->media_fmt.format.sampling_rate;

   intf_extn_md_propagation_t output_md_info;
   memscpy(&output_md_info, sizeof(output_md_info), &input_md_info, sizeof(input_md_info));

   // considering no output generated
   output_md_info.len_per_ch_in_bytes = 0;
   output_md_info.initial_len_per_ch_in_bytes =
      (out_stream_ptr->buf_ptr) ? out_stream_ptr->buf_ptr[0].actual_data_len : 0;

   if (me_ptr->metadata_handler.metadata_propagate)
   {
      result |=
         me_ptr->metadata_handler.metadata_propagate(me_ptr->metadata_handler.context_ptr,
                                                     in_stream_ptr,
                                                     out_stream_ptr,
                                                     &(DUMMY_INT_MD_LIST_PTR), // Won't be used since algo delay is 0.
                                                     ALGO_DELAY_ZERO,
                                                     &input_md_info,
                                                     &output_md_info);
   }

   out_stream_ptr->flags.end_of_frame = in_stream_ptr->flags.end_of_frame;
   in_stream_ptr->flags.end_of_frame  = FALSE;

   // if end of frame is set then check if eos or dfg is moved
   if (out_stream_ptr->flags.end_of_frame)
   {
      // if eos or dfg is moved from internal buffer to the output then mark flag which will be used to move
      // the input port to at-gap.
      if (in_port_ptr->flags.proc_ctx_has_eos && !(in_stream_ptr->flags.marker_eos))
      {
         PS_MSG(me_ptr->miid, DBG_HIGH_PRIO, "propagated EOS, in_port_index %lu", in_port_ptr->cmn.index);
         in_port_ptr->flags.proc_ctx_sent_eos_dfg = TRUE;
      }
      else if (in_port_ptr->flags.proc_ctx_has_dfg && !(capi_priority_sync_sdata_has_dfg(me_ptr, in_stream_ptr)))
      {
         PS_MSG(me_ptr->miid, DBG_HIGH_PRIO, "propagated DFG, in_port_index %lu", in_port_ptr->cmn.index);
         in_port_ptr->flags.proc_ctx_sent_eos_dfg = TRUE;
      }
   }

   // Add back prev_eos_out, if it existed. Assumes function never clears this from the output side.
   out_stream_ptr->flags.marker_eos |= prev_eos_out;

   return result;
}

/**
 * Calls metadata_destroy on each node in the passed in metadata list.
 */
capi_err_t capi_priority_sync_destroy_md_list(capi_priority_sync_t *me_ptr, module_cmn_md_list_t **md_list_pptr)
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
         PS_MSG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "capi priority sync: Error: metadata handler not provided, can't drop metadata.");
         return CAPI_EFAILED;
      }
      node_ptr = next_ptr;
   }

   return CAPI_EOK;
}

/**
 * Adjusts offsets of all metadata in the metadata list by adding or subtracting bytes_consumed from their offsets.
 */
capi_err_t capi_priority_sync_destroy_md_within_range(capi_priority_sync_t * me_ptr,
                                                      capi_media_fmt_v2_t *  med_fmt_ptr,
                                                      module_cmn_md_list_t **md_list_pptr,
                                                      uint32_t               offset_end)
{
   if (md_list_pptr)
   {
      module_cmn_md_list_t *md_list_ptr = *md_list_pptr;
      if (md_list_ptr)
      {
         module_cmn_md_list_t *node_ptr = md_list_ptr;
         module_cmn_md_list_t *next_ptr = NULL;
         while (node_ptr)
         {
            next_ptr                = node_ptr->next_ptr;
            module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

            if (md_ptr->offset < offset_end)
            {
               bool_t IS_DROPPED_TRUE = TRUE;
               if (me_ptr->metadata_handler.metadata_destroy)
               {
                  me_ptr->metadata_handler.metadata_destroy(me_ptr->metadata_handler.context_ptr,
                                                            node_ptr,
                                                            IS_DROPPED_TRUE,
                                                            md_list_pptr);
               }
               else
               {
                  PS_MSG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "capi priority sync: Error: metadata handler not provided, can't drop metadata.");
                  return CAPI_EFAILED;
               }
            }
            node_ptr = next_ptr;
         }
      }
   }
   else
   {
      PS_MSG(me_ptr->miid, DBG_ERROR_PRIO, "capi priority sync: Error: null args.");
      return CAPI_EFAILED;
   }

   return CAPI_EOK;
}

/**
 * Adjusts offsets of all metadata in the metadata list by adding or subtracting bytes_consumed from their offsets.
 */
void capi_priority_sync_adj_offset(capi_media_fmt_v2_t * med_fmt_ptr,
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

         AR_MSG(DBG_LOW_PRIO,
                "MD_DBG: update offset of md_ptr 0x%x md_id 0x%08lX with offset %lu by bytes_consumed %lu "
                "true_add_false_sub %d, ",
                md_ptr,
                md_ptr->metadata_id,
                md_ptr->offset,
                bytes_consumed,
                true_add_false_sub);

         capi_priority_sync_do_md_offset_math(&md_ptr->offset, bytes_consumed, med_fmt_ptr, true_add_false_sub);

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
void capi_priority_sync_do_md_offset_math(uint32_t *           offset_ptr,
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
