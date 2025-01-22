/**
 * \file gen_topo_data_process_island.c
 * \brief
 *     This file contains functions for signal trigger topo common data processing
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo_i.h"
#include "gen_topo_capi.h"
#include "gen_topo_buf_mgr.h"
#include "irm_cntr_prof_util.h"

/* returns whether TS is discontinuous */
bool_t gen_topo_check_copy_incoming_ts_util_(gen_topo_t *           topo_ptr,
                                             gen_topo_module_t *    next_module_ptr,
                                             gen_topo_input_port_t *next_in_port_ptr,
                                             int64_t                incoming_ts,
                                             bool_t                 incoming_ts_valid,
                                             bool_t                 continue_ts)
{
   capi_stream_data_v2_t *next_sdata_ptr        = &next_in_port_ptr->common.sdata;
   uint32_t               next_in_id            = next_in_port_ptr->gu.cmn.id;
   int64_t                next_data_expected_ts = next_sdata_ptr->timestamp;
   bool_t                 ts_disc               = FALSE;

   /** if input is PCM/packetized, then we can always make a check for discontinuity as TS is kept updated when data is
    * consumed.
    * but if input is raw, then check can be made if output at least is PCM/packetized (input -> output linear
    * relationship only for SISO).
    *
    * if at least one side is PCM only we keep next_data_expected_ts updated (in module_process)
    *
    * no need to check if processing has not began or if at least one is not valid
    */

   {
      bool_t is_input_pcm_pack  = SPF_IS_PACKETIZED_OR_PCM(next_in_port_ptr->common.media_fmt_ptr->data_format);
      bool_t is_output_pcm_pack = FALSE;
      bool_t disable_ts_check   = next_in_port_ptr->flags.disable_ts_disc_check;
      if (next_module_ptr->gu.flags.is_siso)
      {
         gen_topo_output_port_t *out_port_ptr =
            (gen_topo_output_port_t *)next_module_ptr->gu.output_port_list_ptr->op_port_ptr;

         is_output_pcm_pack = SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format);

         /* In packetizer (COP) cases, output is COP. Output may not have zero stuffing. This means
          * we cannot derive bytes-to-time from data len. there's no way to verify TS continuity, module must do
          * This condition covers disabling discontinuity checks at all outputs having COP format*/
         if (SPF_COMPR_OVER_PCM_PACKETIZED == out_port_ptr->common.media_fmt_ptr->data_format)
         {
            disable_ts_check = TRUE;
         }
      }

      /* This condition covers disabling discontinuity checks at all inputs(like EP) having COP format  */
      if (SPF_COMPR_OVER_PCM_PACKETIZED == next_in_port_ptr->common.media_fmt_ptr->data_format)
      {
         disable_ts_check = TRUE;
      }

      if (continue_ts || next_sdata_ptr->flags.ts_continue)
      {
         disable_ts_check = TRUE;
      }
      // check for TS discontinuity only when ts_check is not disabled and when data flow has began (no need to check
      // for first buffer)
      if (!disable_ts_check && (TOPO_DATA_FLOW_STATE_FLOWING == next_in_port_ptr->common.data_flow_state))
      {
         // if input is PCM we can update expected ts by data already present, but if only output is PCM,
         // we can compare only if there's no more input left.
         uint32_t len_per_buf = next_in_port_ptr->common.bufs_ptr[0].actual_data_len;
         if (is_input_pcm_pack || (is_output_pcm_pack && (0 == len_per_buf)))
         {
            if (next_sdata_ptr->flags.is_timestamp_valid && is_input_pcm_pack)
            {
               if (next_in_port_ptr->common.flags.is_pcm_unpacked)
               {
                  next_data_expected_ts +=
                     (int64_t)topo_bytes_per_ch_to_us(len_per_buf, next_in_port_ptr->common.media_fmt_ptr, NULL);
               }
               else
               {
                  next_data_expected_ts +=
                     (int64_t)topo_bytes_to_us(len_per_buf, next_in_port_ptr->common.media_fmt_ptr, NULL);
               }
            }

            /**
             * If we had propagated EOF, then topo's history is cleared. There's no need to distinguish old and new
             * data. Hence even if there's TS discont, we don't have to flag it.
             */
            if (!next_in_port_ptr->flags.was_eof_set)
            {
               ts_disc = gen_topo_is_timestamp_discontinuous(next_sdata_ptr->flags.is_timestamp_valid,
                                                             next_data_expected_ts,
                                                             incoming_ts_valid,
                                                             incoming_ts);

               if (ts_disc)
               {
                  /* If the container supports callback to notify timestamp discontinuity events forward this
                   * notification as soon as it is detected. */
                  if (topo_ptr->topo_to_cntr_vtable_ptr->notify_ts_disc_evt)
                  {
                     ar_result_t result =
                        topo_ptr->topo_to_cntr_vtable_ptr
                           ->notify_ts_disc_evt(topo_ptr,
                                                incoming_ts_valid & next_sdata_ptr->flags.is_timestamp_valid,
                                                next_data_expected_ts - incoming_ts,
                                                next_module_ptr->gu.path_index);

                     if (AR_EOK != result)
                     {
                        TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                                        DBG_HIGH_PRIO,
                                        "Failed notifying notify_ts_disc_evt %lu",
                                        result);
                     }
                  }

                  if (!(/*topo_ptr->flags.is_signal_triggered &&*/ topo_ptr->flags.is_signal_triggered_active))
                  {
                     TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                                     DBG_ERROR_PRIO,
                                     "timestamp discontinuity detected for module 0x%lX,0x%lx. expected (valid: %u, "
                                     "ts: %ld us), incoming (valid: %u, ts: %ld us). current actual len_per_buf %lu",
                                     next_module_ptr->gu.module_instance_id,
                                     next_in_id,
                                     next_sdata_ptr->flags.is_timestamp_valid,
                                     (uint32_t)next_data_expected_ts,
                                     incoming_ts_valid,
                                     (uint32_t)incoming_ts,
                                     len_per_buf);
                  }
#ifdef VERBOSE_DEBUGGING
                  else
                  {
                     TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                                     DBG_ERROR_PRIO,
                                     "timestamp discontinuity detected for module 0x%lX,0x%lx. expected (valid: %u, "
                                     "ts: %ld us), incoming (valid: %u, ts: %ld us). current actual len_per_buf %lu",
                                     next_module_ptr->gu.module_instance_id,
                                     next_in_id,
                                     next_sdata_ptr->flags.is_timestamp_valid,
                                     (uint32_t)next_data_expected_ts,
                                     incoming_ts_valid,
                                     (uint32_t)incoming_ts,
                                     len_per_buf);
                  }
#endif
               }
            }
#ifdef VERBOSE_DEBUGGING
            else
            {

               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_LOW_PRIO,
                               "ignoring ts discont check for Module 0x%lx,0x%lx",
                               next_module_ptr->gu.module_instance_id,
                               next_in_id);
            }
#endif
         }
      }

      next_in_port_ptr->ts_to_sync.ts_continue = continue_ts;
      next_in_port_ptr->ts_to_sync.ts_valid    = incoming_ts_valid;
      next_in_port_ptr->ts_to_sync.ivalue      = incoming_ts;
      next_in_port_ptr->ts_to_sync.fvalue      = 0;

      // need to sync to timestamp only when we are copying new data, otherwise, we will be replacing old timestamp
      // (which is updated based on bytes_to_time logic after process)
      // for WMA pro, all bytes are consumed after need-more, but we cannot sync yet. this is done through checking for
      // need-more flag. however, first time need-more is true yet we can sync to TS
      // note: in gen_topo_sync_to_input_timestamp, need-more is not checked for syncing.
      // therefore, only way to sync would be by coming back here. For first module, this func is called only when ext
      // buf trigger occurs.
      // hence for first module, if sync is not done here due to need-more being set (bytes_from_prev_buf=0), then it
      // will never be synced.
      // in HW EP, at ext-in, multiple bufs may be popped. For every buf we cannot overwrite ts.
      // We can ovewrite only if next buf is empty.

      // if there's old data, we cannot copy new timestamp.
      if (0 == next_in_port_ptr->common.bufs_ptr[0].actual_data_len)
      {
         next_sdata_ptr->timestamp                = next_in_port_ptr->ts_to_sync.ivalue;
         next_sdata_ptr->flags.is_timestamp_valid = next_in_port_ptr->ts_to_sync.ts_valid;
         next_sdata_ptr->flags.ts_continue        = next_in_port_ptr->ts_to_sync.ts_continue;
      }
   }
   return ts_disc;
}

/**
 * called after module process
 */
ar_result_t gen_topo_sync_to_input_timestamp(gen_topo_t *           topo_ptr,
                                             gen_topo_input_port_t *in_port_ptr,
                                             uint32_t               data_consumed_in_process_per_buf)
{
   ar_result_t result = AR_EOK;

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX: Sync to input timestamp:bytes_from_prev_buf %lu, data_consumed_in_process_per_buf "
                   "%lu, "
                   "ts_to_sync (lsw) %lu, TS valid %u TS cont %lu",
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   in_port_ptr->bytes_from_prev_buf,
                   data_consumed_in_process_per_buf,
                   (uint32_t)in_port_ptr->ts_to_sync.ivalue,
                   in_port_ptr->ts_to_sync.ts_valid,
                   in_port_ptr->ts_to_sync.ts_continue);
#endif

   if (!in_port_ptr->bytes_from_prev_buf)
   {
      return result;
   }

   uint32_t data_consumed_from_new_buf_per_ch = 0;

   // if data consumed in input port for this frame is less than data from prev buf, then prev buf is fully consumed.
   if (data_consumed_in_process_per_buf >= in_port_ptr->bytes_from_prev_buf)
   {
      data_consumed_from_new_buf_per_ch = data_consumed_in_process_per_buf - in_port_ptr->bytes_from_prev_buf;
      data_consumed_from_new_buf_per_ch = gen_topo_convert_len_per_buf_to_len_per_ch(in_port_ptr->common.media_fmt_ptr,
                                                                                     data_consumed_from_new_buf_per_ch);
      in_port_ptr->bytes_from_prev_buf = 0;
   }
   else // otherwise, CAPI had left more than one frame.
   {
      /*
      Ideally CAPI must not return need-more when there is more than one frame left.
      Some CAPIs do this and this results in issues.
      The key logic for handling timestamps in partial frame cases [in client buf] is: when need-more is returned we
      store bytes_from_prev_buf as the num of bytes from prev client buf.
      When client gives more data & we decode the next frame, we subtract off the bytes_from_prev_buf with amount of
      data consumed in the process call. When this results in zero or negative,
      this means that the data from prev client is over and we can consider next buf. This is when we sync to input
      timestamp.

      However, when some decoders such as aac decoder consistently returns need-more with multiple frames, we will
      never get a chance to sync to input timestamp.
      In discontinuous input timestamp cases it can cause issues. Further, at EOS, also we will sync to timestamp at
      wrong time, as we don't know clear boundary b/w 2 client bufs.

      Since we cannot handle this case anyway, we will not subtract by data_consumed_in_process_per_buf => we will never
      sync to input TS*/
      // aac_dec_nt_38

      // in_port_ptr->bytes_from_prev_buf -= data_consumed_in_process_per_buf;
   }

   if (SPF_RAW_COMPRESSED == in_port_ptr->common.media_fmt_ptr->data_format)
   {
      if ((0 == in_port_ptr->bytes_from_prev_buf) && (FALSE == in_port_ptr->ts_to_sync.ts_continue))
      {
         // for raw compr input, if prev buf data was present in the input, and it became zero now, then sync to
         // input TS.
         in_port_ptr->common.sdata.timestamp                = in_port_ptr->ts_to_sync.ivalue;
         in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;
         in_port_ptr->common.sdata.flags.ts_continue        = in_port_ptr->ts_to_sync.ts_continue;
      }
   }
   else
   {
      // for PCM or packetized, move the input timestamp by data already copied
      // once all prev data is copied sync to last synced ts.
      if (0 == in_port_ptr->bytes_from_prev_buf)
      {
         in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;
         if (in_port_ptr->ts_to_sync.ts_valid)
         {
            in_port_ptr->common.sdata.timestamp =
               (in_port_ptr->ts_to_sync.ivalue + (int64_t)topo_bytes_per_ch_to_us(data_consumed_from_new_buf_per_ch,
                                                                                  in_port_ptr->common.media_fmt_ptr,
                                                                                  &in_port_ptr->ts_to_sync.fvalue));
         }
      }
      else
      {
         if (in_port_ptr->ts_to_sync.ts_valid)
         {
            in_port_ptr->ts_to_sync.ivalue =
               (in_port_ptr->ts_to_sync.ivalue + (int64_t)topo_bytes_per_ch_to_us(data_consumed_from_new_buf_per_ch,
                                                                                  in_port_ptr->common.media_fmt_ptr,
                                                                                  &in_port_ptr->ts_to_sync.fvalue));
         }
      }
   }
   return result;
}

static bool_t gen_topo_need_to_copy_md(gen_topo_module_t *    module_ptr,
                                       capi_stream_data_v2_t *prev_sdata_ptr,
                                       capi_stream_data_v2_t *next_sdata_ptr)
{
   return (prev_sdata_ptr->metadata_list_ptr || prev_sdata_ptr->flags.marker_eos || module_ptr->int_md_list_ptr ||
           next_sdata_ptr->metadata_list_ptr || next_sdata_ptr->flags.end_of_frame ||
           prev_sdata_ptr->flags.end_of_frame || next_sdata_ptr->flags.marker_eos);
}

ar_result_t gen_topo_copy_data_from_prev_to_next(gen_topo_t *            topo_ptr,
                                                 gen_topo_module_t *     next_module_ptr,
                                                 gen_topo_input_port_t * next_in_port_ptr,
                                                 gen_topo_output_port_t *prev_out_port_ptr,
                                                 bool_t                  after)

{
   ar_result_t            result           = AR_EOK;
   topo_buf_t *           next_bufs_ptr    = next_in_port_ptr->common.bufs_ptr;
   topo_buf_t *           prev_bufs_ptr    = prev_out_port_ptr->common.bufs_ptr;
   topo_media_fmt_t *     next_med_fmt_ptr = next_in_port_ptr->common.media_fmt_ptr;
   topo_media_fmt_t *     prev_med_fmt_ptr = prev_out_port_ptr->common.media_fmt_ptr;
   capi_stream_data_v2_t *prev_sdata_ptr   = &prev_out_port_ptr->common.sdata;
   bool_t                 need_to_copy_md =
      gen_topo_need_to_copy_md(next_module_ptr, &prev_out_port_ptr->common.sdata, &next_in_port_ptr->common.sdata);

   bool_t ts_adjust_needed =
      prev_sdata_ptr->flags.is_timestamp_valid && SPF_IS_PACKETIZED_OR_PCM(prev_med_fmt_ptr->data_format);

   uint32_t total_bytes_already_in_next_for_md_prop = 0, total_bytes_already_in_prev_for_md_prop = 0;

   if (need_to_copy_md)
   {
      total_bytes_already_in_prev_for_md_prop = gen_topo_get_actual_len_for_md_prop(&prev_out_port_ptr->common);
      total_bytes_already_in_next_for_md_prop = gen_topo_get_actual_len_for_md_prop(&next_in_port_ptr->common);
   }

   if (next_bufs_ptr[0].data_ptr == prev_bufs_ptr[0].data_ptr)
   {
#ifdef SAFE_MODE
      if ((0 != next_bufs_ptr[0].actual_data_len) || (next_bufs_ptr[0].max_data_len != prev_bufs_ptr[0].max_data_len) ||
          (prev_out_port_ptr->common.sdata.bufs_num != next_in_port_ptr->common.sdata.bufs_num))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX: in-port-id 0x%lx: Prev out buf and next in buf data_ptr same, but next buf "
                         "has "
                         "%lu. max len: next %lu, prev %lu, bufs_num: prev %lu, next %lu",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id,
                         next_bufs_ptr[0].actual_data_len,
                         next_bufs_ptr[0].max_data_len,
                         prev_bufs_ptr[0].max_data_len,
                         prev_out_port_ptr->common.sdata.bufs_num,
                         next_in_port_ptr->common.sdata.bufs_num);
      }
#endif

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      " copy_data_from_prev_to_next - same buf: next module 0x%lX: in-port-id 0x%lx: after%u, "
                      "bytes_to_copy_per_buf = %lu, bufs_num %lu",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id,
                      after,
                      prev_bufs_ptr[0].actual_data_len,
                      next_in_port_ptr->common.sdata.bufs_num);
#endif

      // adjust timestamp forward by amount of data copied.
      if (ts_adjust_needed)
      {
         prev_sdata_ptr->timestamp +=
            (int64_t)topo_bytes_to_us(gen_topo_get_total_actual_len(&prev_out_port_ptr->common),
                                      prev_med_fmt_ptr,
                                      &prev_out_port_ptr->common.fract_timestamp_pns);
      }

      // for unpacked v1/v2 first buf len can be assumed to be same as all buffers.
      // this same logic applies to even interleaved because there is only one buffer
      for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&next_in_port_ptr->common); b++)
      {
         next_bufs_ptr[b].actual_data_len = prev_bufs_ptr[b].actual_data_len;
         prev_bufs_ptr[b].actual_data_len = 0;
      }
   }
   else
   {
      if (SPF_IS_PCM_DATA_FORMAT(next_med_fmt_ptr->data_format) &&
          (TOPO_DEINTERLEAVED_PACKED == next_med_fmt_ptr->pcm.interleaving))
      {
         // there's only one buffer in this case.

         uint32_t empty_space_in_next_inp = (next_bufs_ptr[0].max_data_len - next_bufs_ptr[0].actual_data_len);
         // this func assumes next_bufs_ptr[0].data_ptr exists, hence we can directly use next_bufs_ptr[0].max_data_len
         uint32_t bytes_copied = MIN(empty_space_in_next_inp, prev_bufs_ptr[0].actual_data_len);

         if (ts_adjust_needed)
         {
            prev_sdata_ptr->timestamp += (int64_t)topo_bytes_to_us(bytes_copied,
                                                                   prev_med_fmt_ptr,
                                                                   &prev_out_port_ptr->common.fract_timestamp_pns);
         }

#ifdef VERBOSE_DEBUGGING
         uint32_t bytes_already_in_next = next_bufs_ptr[0].actual_data_len;
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " copy_data_from_prev_to_next - deint-packed: next module 0x%lX: in-port-id 0x%lx: after%u, "
                         "bytes_to_copy = %lu, bufs_num %lu, bytes_already_in_next %lu",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id,
                         after,
                         bytes_copied,
                         next_in_port_ptr->common.sdata.bufs_num,
                         bytes_already_in_next);
#endif

         uint32_t prev_port_len_per_ch, bytes_copied_per_ch;
         topo_div_two_nums(prev_bufs_ptr[0].actual_data_len,
                           &prev_port_len_per_ch,
                           bytes_copied,
                           &bytes_copied_per_ch,
                           next_med_fmt_ptr->pcm.num_channels);

         if (0 == next_bufs_ptr[0].actual_data_len)
         {
            TOPO_MEMSCPY_NO_RET(next_bufs_ptr[0].data_ptr,
                                empty_space_in_next_inp,
                                prev_bufs_ptr[0].data_ptr, // always starts from beginning due to memmove
                                bytes_copied,
                                topo_ptr->gu.log_id,
                                "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                prev_out_port_ptr->gu.cmn.id,
                                next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                next_in_port_ptr->gu.cmn.id);
         }
         else
         {

            uint32_t new_out_spacing, existing_bytes_per_ch, empty_space_in_next_inp_per_ch;
            topo_div_three_nums((next_bufs_ptr[0].actual_data_len + bytes_copied),
                                &new_out_spacing,
                                next_bufs_ptr[0].actual_data_len,
                                &existing_bytes_per_ch,
                                empty_space_in_next_inp,
                                &empty_space_in_next_inp_per_ch,
                                next_med_fmt_ptr->pcm.num_channels);

            // make space for new data by moving old data apart
            for (uint32_t ch = next_med_fmt_ptr->pcm.num_channels; ch > 0; ch--)
            {
               TOPO_MEMSMOV_NO_RET(next_bufs_ptr[0].data_ptr + (ch - 1) * new_out_spacing,
                                   existing_bytes_per_ch,
                                   next_bufs_ptr[0].data_ptr + (ch - 1) * existing_bytes_per_ch,
                                   existing_bytes_per_ch,
                                   topo_ptr->gu.log_id,
                                   "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   prev_out_port_ptr->gu.cmn.id,
                                   next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   next_in_port_ptr->gu.cmn.id);
            }

            // copy new data to next
            for (uint32_t ch = 0; ch < next_med_fmt_ptr->pcm.num_channels; ch++)
            {
               TOPO_MEMSMOV_NO_RET(next_bufs_ptr[0].data_ptr + existing_bytes_per_ch + ch * new_out_spacing,
                                   empty_space_in_next_inp_per_ch,
                                   prev_bufs_ptr[0].data_ptr + ch * prev_port_len_per_ch,
                                   bytes_copied_per_ch, // always starts from beginning due to memmove
                                   topo_ptr->gu.log_id,
                                   "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   prev_out_port_ptr->gu.cmn.id,
                                   next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   next_in_port_ptr->gu.cmn.id);
            }
         }

         // move data to beginning in the prev port
         uint32_t prev_port_new_len_per_ch = (prev_port_len_per_ch - bytes_copied_per_ch);
         if (prev_port_new_len_per_ch)
         {
            for (uint32_t ch = 0; ch < prev_med_fmt_ptr->pcm.num_channels; ch++)
            {
               TOPO_MEMSMOV_NO_RET(prev_bufs_ptr[0].data_ptr + (ch * prev_port_new_len_per_ch),
                                   prev_port_new_len_per_ch,
                                   prev_bufs_ptr[0].data_ptr + bytes_copied_per_ch + (ch * prev_port_len_per_ch),
                                   prev_port_new_len_per_ch,
                                   topo_ptr->gu.log_id,
                                   "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   prev_out_port_ptr->gu.cmn.id,
                                   next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   next_in_port_ptr->gu.cmn.id);
            }
         }
         prev_bufs_ptr[0].actual_data_len = (prev_port_new_len_per_ch * prev_med_fmt_ptr->pcm.num_channels);
         next_bufs_ptr[0].actual_data_len += bytes_copied;
      }
      else if (next_in_port_ptr->common.flags.is_pcm_unpacked)
      {
         /** Handles PCM unpacked V2/V1 */

         // Note: Since the data is deinterleaved unpacked (both V2/V1), data lengths are expected to be same on
         // all the channels hence relying on first bufs lengths to move data to all the chs.

         // Calculating bytes to be yet copied from prev output to next input
         uint32_t bytes_copied_per_buf =
            MIN(next_bufs_ptr[0].max_data_len - next_bufs_ptr[0].actual_data_len, prev_bufs_ptr[0].actual_data_len);

         // bytes that will remain in prev output after moving 'bytes_copied_per_buf' to the next input
         uint32_t bytes_left_in_prev_after_copy = prev_bufs_ptr[0].actual_data_len - bytes_copied_per_buf;

         // copy data from prev to next
         for (int32_t b = 0; b < prev_out_port_ptr->common.sdata.bufs_num; b++)
         {
            TOPO_MEMSMOV_NO_RET(next_bufs_ptr[b].data_ptr + next_bufs_ptr[0].actual_data_len,
                                bytes_copied_per_buf,
                                prev_bufs_ptr[b].data_ptr,
                                bytes_copied_per_buf,
                                topo_ptr->gu.log_id,
                                "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                prev_out_port_ptr->gu.cmn.id,
                                next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                next_in_port_ptr->gu.cmn.id);

            // move data to beginning
            if (bytes_left_in_prev_after_copy)
            {
               TOPO_MEMSMOV_NO_RET(prev_bufs_ptr[b].data_ptr,
                                   prev_bufs_ptr[0].max_data_len,
                                   prev_bufs_ptr[b].data_ptr + bytes_copied_per_buf,
                                   bytes_left_in_prev_after_copy,
                                   topo_ptr->gu.log_id,
                                   "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   prev_out_port_ptr->gu.cmn.id,
                                   next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   next_in_port_ptr->gu.cmn.id);
            }
         }

         // updates only first ch lens for unpacked V1/V2
         next_bufs_ptr[0].actual_data_len += bytes_copied_per_buf;
         prev_bufs_ptr[0].actual_data_len -= bytes_copied_per_buf;

         if (ts_adjust_needed)
         {
            prev_sdata_ptr->timestamp +=
               (int64_t)topo_bytes_per_ch_to_us(bytes_copied_per_buf,
                                                prev_med_fmt_ptr,
                                                &prev_out_port_ptr->common.fract_timestamp_pns);
         }

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " copy_data_from_prev_to_next - generic: next module 0x%lX: in-port-id 0x%lx: after%u, "
                         "bytes_copied_per_buf = %lu, bufs_num %lu",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id,
                         after,
                         bytes_copied_per_buf,
                         next_in_port_ptr->common.sdata.bufs_num);
#endif
      }
      else
      {
         // for deint-raw-compr, each buf can have diff length. interleaved also comes here where > 1 ch exist but only
         // one buf

         uint32_t total_bytes_copied = 0;
         for (int32_t b = 0; b < prev_out_port_ptr->common.sdata.bufs_num; b++)
         {
            uint32_t bytes_copied_per_buf;
            TOPO_MEMSMOV(bytes_copied_per_buf,
                         next_bufs_ptr[b].data_ptr + next_bufs_ptr[b].actual_data_len,
                         next_bufs_ptr[b].max_data_len - next_bufs_ptr[b].actual_data_len,
                         prev_bufs_ptr[b].data_ptr,
                         prev_bufs_ptr[b].actual_data_len,
                         topo_ptr->gu.log_id,
                         "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                         prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         prev_out_port_ptr->gu.cmn.id,
                         next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         next_in_port_ptr->gu.cmn.id);

            next_bufs_ptr[b].actual_data_len += bytes_copied_per_buf;
            prev_bufs_ptr[b].actual_data_len -= bytes_copied_per_buf;
            total_bytes_copied += bytes_copied_per_buf;

            // move data to beginning
            if (prev_bufs_ptr[b].actual_data_len)
            {
               TOPO_MEMSMOV_NO_RET(prev_bufs_ptr[b].data_ptr,
                                   prev_bufs_ptr[b].max_data_len,
                                   prev_bufs_ptr[b].data_ptr + bytes_copied_per_buf,
                                   prev_bufs_ptr[b].actual_data_len,
                                   topo_ptr->gu.log_id,
                                   "P2N: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   prev_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   prev_out_port_ptr->gu.cmn.id,
                                   next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                   next_in_port_ptr->gu.cmn.id);
            }
         }
         if (ts_adjust_needed)
         {
            prev_sdata_ptr->timestamp += (int64_t)topo_bytes_to_us(total_bytes_copied,
                                                                   prev_med_fmt_ptr,
                                                                   &prev_out_port_ptr->common.fract_timestamp_pns);
         }

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " copy_data_from_prev_to_next - generic: next module 0x%lX: in-port-id 0x%lx: after%u, "
                         "total_bytes_copied = %lu, bufs_num %lu",
                         next_module_ptr->gu.module_instance_id,
                         next_in_port_ptr->gu.cmn.id,
                         after,
                         total_bytes_copied,
                         next_in_port_ptr->common.sdata.bufs_num);
#endif
      }
   }

   // movement of DFG/EOS resets ports, hence do this last.
   if (need_to_copy_md)
   {
      uint32_t total_bytes_copied =
         gen_topo_get_actual_len_for_md_prop(&next_in_port_ptr->common) - total_bytes_already_in_next_for_md_prop;

      gen_topo_move_md_from_prev_to_next(next_module_ptr,
                                         next_in_port_ptr,
                                         prev_out_port_ptr,
                                         total_bytes_copied,
                                         total_bytes_already_in_prev_for_md_prop,
                                         total_bytes_already_in_next_for_md_prop);
   }

   return result;
}

/**
 * copies data from prev to next module, handles media fmt and timestamp discontinuity between modules.
 * when these 2 events occur, EOF flag is set to true.
 * the flag is cleared when input data of the next module is fully consumed.
 *
 * Between 2 modules EOF may be set due to above reasons or due to propagation from prev module.
 */
GEN_TOPO_STATIC ar_result_t gen_topo_check_copy_between_modules(gen_topo_t *            topo_ptr,
                                                                gu_module_list_t *      module_list_ptr,
                                                                gen_topo_module_t *     prev_module_ptr,
                                                                gen_topo_input_port_t * next_in_port_ptr,
                                                                gen_topo_output_port_t *prev_out_port_ptr)
{
   ar_result_t            result           = AR_EOK;
   bool_t                 discontinuity    = FALSE;
   bool_t                 is_force_process = FALSE;
   gen_topo_module_t *    next_module_ptr  = (gen_topo_module_t *)module_list_ptr->module_ptr;
   topo_buf_t *           next_bufs_ptr    = next_in_port_ptr->common.bufs_ptr;
   topo_buf_t *           prev_bufs_ptr    = prev_out_port_ptr->common.bufs_ptr;
   capi_stream_data_v2_t *next_sdata_ptr   = &next_in_port_ptr->common.sdata;
   capi_stream_data_v2_t *prev_sdata_ptr   = &prev_out_port_ptr->common.sdata;
   uint32_t               prev_sdata_flags = prev_sdata_ptr->flags.word;
   uint32_t               next_sdata_flags = next_sdata_ptr->flags.word; // for debug

   bool_t prev_has_data_next_can_accept = FALSE;
   if (prev_bufs_ptr[0].actual_data_len)
   {
      uint32_t empty_space_in_next_inp_per_buf = next_bufs_ptr[0].data_ptr
                                                    ? (next_bufs_ptr[0].max_data_len - next_bufs_ptr[0].actual_data_len)
                                                    : next_in_port_ptr->common.max_buf_len_per_buf;
      prev_has_data_next_can_accept = (empty_space_in_next_inp_per_buf > 0);
   }

   if (prev_out_port_ptr->common.flags.media_fmt_event)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "media format changed between module 0x%lX and Module 0x%lX. next buf actual len %lu",
                      prev_module_ptr->gu.module_instance_id,
                      next_module_ptr->gu.module_instance_id,
                      next_bufs_ptr[0].actual_data_len);

      // propagate only after next module's input has no data left.
      // Only try to propogate media fomrat if the next port state is started because MF will only be propogated if the
      // port state is started this will avoid infinite loop in GC.
      // Ref Test Case: dtmf_gen_cfg_cmd_seq_1 , dtmf_gen_cfg_cmd_seq_2 & dtmf_gen_cfg_cmd_seq_3
      if ((0 == next_bufs_ptr[0].actual_data_len) && (TOPO_PORT_STATE_STARTED == next_in_port_ptr->common.state))
      {
         gen_topo_propagate_media_fmt_from_module(topo_ptr, TRUE /* is_data_path*/, module_list_ptr);

         // ensures we break process, propagate MF from next module, assign threshold, bufs_num and come back.
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, media_fmt_event);

         // Copying media fmt could results in thresh change. after finishing thresh handling etc, we can copy buf.
         // Copying data now causes unnecessary buf creation so leave this function here.
         return result;
      }

      // if partial data is present at next module, set discontinuity (force-process) so that existing data is first
      // consumed.
      discontinuity = TRUE;
   }

   // check for timestamp discontinuity if next buf has nonzero data & we are copying data.
   // next input timestamp is updated to have the expected timestmap
   // after process in module_process, but existing data needs to be accounted here.
   // for raw compr cases, we cannot determine expected TS

   if (prev_has_data_next_can_accept)
   {
      discontinuity |= gen_topo_check_copy_incoming_ts(topo_ptr,
                                                       next_module_ptr,
                                                       next_in_port_ptr,
                                                       prev_sdata_ptr->timestamp,
                                                       (bool_t)prev_sdata_ptr->flags.is_timestamp_valid,
                                                       prev_sdata_ptr->flags.ts_continue /*continue timestamp */);
   }

   if (discontinuity && /*topo_ptr->flags.is_signal_triggered &&*/ topo_ptr->flags.is_signal_triggered_active)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      "Module 0x%lX: Discontinuity ignored for signal triggerred mode",
                      next_module_ptr->gu.module_instance_id);
      discontinuity = FALSE;
   }
/*
 * if there's no discontinuity, data can be copied immediately.
 *    Timestamp or metadata pertaining to beginning of buf can be copied only if next buf has no data left.
 * If there's discontinuity, data (& TS) can be copied only after next buf is drained.
 *    Media fmt also propagated only after data is drained.
 *
 * Buffer is needed to be obtained (get_buf), at EOS even if prev_has_data_next_can_accept is False.
 *
 * If next port already has flushing eos, we can not copy data. Thus only if next_has_end_flags is due to
 * discontinuity,
 * we cannot copy new data, but if it's due to EOS we can. When this happens flushing EOS is changed to non-flushing
 * by gen_topo_check_modify_flushing_eos
 *
 * If EOS is non-flushing, then we should have no partial data
 * But if partial data occurs, its ok to behave like flushing EoS, where we push out algo delay, squeeze etc.
 *
 * non-flushing EOS is treated like begin metadata
 */
#ifdef VERBOSE_DEBUGGING
   bool_t copy_begin_flags = FALSE; /* whether to copy metadata pertaining to beginning of buf, such as TS, erasure */
   bool_t copy_end_flags   = FALSE; /* whether to copy metadata pertaining to end of buf, such as TS discont*/
#endif
   bool_t get_buf   = FALSE; /* whethet to get a buf. a buf is required to call process on module.  */
   bool_t copy_data = FALSE; /* whether to copy data to the next port buf */

   bool_t next_has_end_flags = next_sdata_ptr->flags.end_of_frame;

   // discontinuity is about changes from prev modules. force_process is about both next and prev
   // Do not mark discontinuity for EOS as discontinuity is not before the data but afterwards.
   //   E.g., if we have 2 ms in next module and 3 ms in prev module followed by EOS, then 2+3+eos is the sequence, not
   //   2+eos+3.
   bool_t prev_has_end_flags = prev_sdata_ptr->flags.end_of_frame || prev_sdata_ptr->flags.marker_eos;

   // In general, always process if there's metadata (for non-EOS, if MD is stuck inside the module we don't call)
   is_force_process =
      (prev_sdata_ptr->metadata_list_ptr || next_sdata_ptr->metadata_list_ptr || discontinuity || next_has_end_flags ||
       prev_has_end_flags || next_sdata_ptr->flags.marker_eos || prev_sdata_ptr->flags.erasure);

   gen_topo_data_need_t in_needs_data = gen_topo_in_port_needs_data(topo_ptr, next_in_port_ptr);
   /** Copy if there's data to copy and downstream needs it, except at EoS */
   if (((prev_has_data_next_can_accept) && (!((GEN_TOPO_DATA_BLOCKED | GEN_TOPO_DATA_NOT_NEEDED) & in_needs_data))) ||
       is_force_process)
   {
// copy_begin_flags: timestamp, would be overwritten if copied before actual len becomes zero.
#ifdef VERBOSE_DEBUGGING
      copy_begin_flags = (0 == next_bufs_ptr[0].actual_data_len);
      copy_end_flags   = TRUE;
#endif

      if (0 == next_bufs_ptr[0].actual_data_len)
      {
         // timestamp is handled in gen_topo_check_copy_incoming_ts
         next_sdata_ptr->flags.erasure = prev_sdata_ptr->flags.erasure;
      }

      // even if next module actual_data_len can never become zero, EoS has to go; even for media fmt discontinuity.

      // if (copy_end_flags)
      {
         // When propagating EoS, EoS may get stuck at some input. previous modules would be cleared. Hence OR with both
         // prev and next. EOF is cleared either either by module (if it supports metadata) or fwk after processing.
         next_sdata_ptr->flags.end_of_frame |= (next_has_end_flags || prev_has_end_flags);

         // If previously EOF was processed, and we get another discontinuity we don't need to set EOF again.
         // Reason: earlier EOF would've cleared history. See doc of was_eof_set field.
         if (!next_in_port_ptr->flags.was_eof_set)
         {
            next_sdata_ptr->flags.end_of_frame |= discontinuity;
         }

         // clear after copying to next port
         prev_sdata_ptr->flags.end_of_frame = FALSE;
      }

      if (discontinuity)
      {
         if (0 == next_bufs_ptr[0].actual_data_len)
         {
            get_buf   = TRUE;
            copy_data = FALSE;
            // during discontinuity we set EOF, and hence we cannot copy new data.
            // doing so will cause confusion as to whether new data is after EOF or before.
         }
      }
      else
      {
         get_buf   = TRUE;
         copy_data = !next_has_end_flags || next_sdata_ptr->flags.marker_eos;
         // flushing EOS will be changed to non-flushing if new data is copied.
      }
   }

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX -> next module 0x%lX: next timestamp %lu (0x%lx%lx). prev_has_data_next_can_accept = "
                   "%u. port cmn flags prev0x%lX, next0x%lX",
                   prev_module_ptr->gu.module_instance_id,
                   next_module_ptr->gu.module_instance_id,
                   (int32_t)next_sdata_ptr->timestamp,
                   (int32_t)(next_sdata_ptr->timestamp >> 32),
                   (int32_t)next_sdata_ptr->timestamp,
                   prev_has_data_next_can_accept,
                   prev_out_port_ptr->common.flags.word,
                   next_in_port_ptr->common.flags.word);

   // Flags: last in capi_stream_flags_t is MSB.  Ver1|   Version0|Eras|M3|M2|  M1|EOS|EOF|TSValid
   uint32_t temp = (is_force_process << 6) | (discontinuity << 5) | (copy_begin_flags << 4) | (get_buf << 3) |
                   (copy_data << 2) | (copy_end_flags << 1) | in_needs_data;
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "  temp_flags0x%lX prev_sdata_flags0x%lX -> next_sdata_flags0x%lX, next_port_flag0x%lX, "
                   "next bytes_from_prev_buf %lu, prev meta_data_ptr 0x%lX, next meta_data_ptr 0x%lX",
                   temp,
                   prev_sdata_flags,
                   next_sdata_flags,
                   next_in_port_ptr->flags.word,
                   next_in_port_ptr->bytes_from_prev_buf,
                   prev_sdata_ptr->metadata_list_ptr,
                   next_sdata_ptr->metadata_list_ptr);
#else
   (void)prev_sdata_flags;
   (void)next_sdata_flags;
#endif

   if (prev_has_data_next_can_accept || prev_bufs_ptr[0].actual_data_len)
   {
      // clear need-more input if there's data to copy or if the prev buf has some data
      // not clearing here results in unnecessarily waiting for more client bufs under TS discon or under EOF
      // propagation
      // test: wmastd_dec_nt_9 which sets EOF on the input buffers. EOF propagates only after module produces no more
      // output,
      // if need more is not cleared then 2 clients buffers would be obtained where next buf overwrites first buf's TS.
      next_in_port_ptr->flags.need_more_input = FALSE;
      // this flag is used in gen_topo_in_port_needs_input_buf
   }

   if (get_buf)
   {
      /*Ex: if each leg of the SAL input is a separate subgraph, we need to check if this
             input port's state is "started" before we allocate buffers for it.
             If upstream of any input port is not started, we continue.*/
      if (TOPO_PORT_STATE_STARTED == next_in_port_ptr->common.state)
      {
         result = gen_topo_check_get_in_buf_from_buf_mgr(topo_ptr, next_in_port_ptr, prev_out_port_ptr);
         if (AR_DID_FAIL(result))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Failed to get buf mgr buffer for Module 0x%lX, in_port 0x%lx",
                            next_module_ptr->gu.module_instance_id,
                            next_in_port_ptr->gu.cmn.id);
            // continue with other ports if any.
         }
      }
   }
   else if ((GEN_TOPO_DATA_BLOCKED & in_needs_data) && (GEN_TOPO_DATA_TRIGGER == topo_ptr->proc_context.curr_trigger))
   {
      // On data triggers, If port is blocked then module capi doesnt expect buffer on the blocked port.
      // So fwk needs to force return the held buffer.
      //
      // note: Usually the ports will not hold a empty buffer. But if its a low latency buffer,
      // it can be held at the port for optimization (refer buffer manager). Marking force return = true,
      // will return the low latency buffer if its empty.
      next_in_port_ptr->common.flags.force_return_buf = TRUE;
      gen_topo_input_port_return_buf_mgr_buf(topo_ptr, next_in_port_ptr);
   }

   // if (copy_data && ) we do this inside gen_topo_copy_data_from_prev_to_next
   if (!(copy_data && prev_has_data_next_can_accept))
   {
      uint32_t total_bytes_in_next = gen_topo_get_actual_len_for_md_prop(&next_in_port_ptr->common);
      uint32_t total_bytes_in_prev = gen_topo_get_actual_len_for_md_prop(&prev_out_port_ptr->common);
      gen_topo_move_md_from_prev_to_next(next_module_ptr,
                                         next_in_port_ptr,
                                         prev_out_port_ptr,
                                         0 /* bytes copied*/,
                                         total_bytes_in_prev,
                                         total_bytes_in_next);
   }

   // if we have data or metadata, move to flow-begin
   if (prev_has_data_next_can_accept || next_in_port_ptr->common.sdata.metadata_list_ptr)
   {
      gen_topo_handle_data_flow_begin(topo_ptr, &next_in_port_ptr->common, &next_in_port_ptr->gu.cmn);
   }

   gen_topo_handle_eof_history(next_in_port_ptr, prev_has_data_next_can_accept);

   // if a port is at gap, then, since module may not be called, it's better to clear EOF here.
   if (next_in_port_ptr->common.sdata.flags.end_of_frame &&
       (TOPO_DATA_FLOW_STATE_AT_GAP == next_in_port_ptr->common.data_flow_state))
   {
      next_in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      "Resetting EOF at port as it's at gap 0x%lX, in_port 0x%lx",
                      next_module_ptr->gu.module_instance_id,
                      next_in_port_ptr->gu.cmn.id);
#endif
   }

   if (copy_data && prev_has_data_next_can_accept && next_bufs_ptr[0].data_ptr)
   {
      gen_topo_copy_data_from_prev_to_next(topo_ptr,
                                           next_module_ptr,
                                           next_in_port_ptr,
                                           prev_out_port_ptr,
                                           FALSE /*after */);

      // If previous output buffer has data set a flag to copy remaining data after module process
      // assume all channels are finished (deint-raw compr is the only case where this can go wrong)
      //
      // Important: If media format event is set, should NOT set data_pending_in_prev though there is pending data in
      // prev output. Else it can result in erroneous copy of new mf data from prev output to next input with old mf,
      // and media format will never be propagated since next input has the erroneous data. tests: rve_rx_tx_low_power
      if (prev_out_port_ptr->common.bufs_ptr[0].actual_data_len > 0)
      {
         gen_topo_process_context_t *pc                                                     = &topo_ptr->proc_context;
         pc->in_port_scratch_ptr[next_in_port_ptr->gu.cmn.index].flags.data_pending_in_prev = TRUE;
      }

      // If data is copied from output to input then mark this true.
      // there can be a case where input is copied but module's trigger policy is not satisfied
      // test: pb_sync_gen_cntr_sal_8
      topo_ptr->proc_context.process_info.anything_changed = TRUE;
   }
   // trying to release here helps case where get_buf is FALSE and prev_out has no data.
   // at EOS we cannot return buf as we need buf to call module process
   if (!get_buf)
   {
      gen_topo_return_buf_mgr_buf_for_both(topo_ptr, next_in_port_ptr, prev_out_port_ptr);
   }

   return AR_EOK;
}

/**
 * for output:
 *    output may have old data & before calling process on CAPI, data_ptr should be pointed to empty space.
 *    For num proc loops > 1, sdata will be repopulated with the pc->scratch sdata, so that we dont lose the aggregated
 *    sdata lengths while looping.
 */
ar_result_t gen_topo_populate_sdata_bufs(gen_topo_t *            topo_ptr,
                                         gen_topo_module_t *     module_ptr,
                                         gen_topo_output_port_t *out_port_ptr,
                                         bool_t                  err_check)
{
   ar_result_t                 result = AR_EOK;
   capi_buf_t *                bufs;
   gen_topo_process_context_t *pc_ptr      = &topo_ptr->proc_context;
   topo_media_fmt_t *          med_fmt_ptr = out_port_ptr->common.media_fmt_ptr;

// when stale out is present we don't call module_process
// however, num_proc_loop logic will result in module_process being called multiple times.

#ifdef ERROR_CHECK_MODULE_PROCESS
   if (err_check)
   {
      if ((out_port_ptr->common.bufs_ptr[0].actual_data_len) && (
#ifdef VERBOSE_DEBUGGING
                                                                   1 ||
#endif
                                                                   (module_ptr->num_proc_loops == 1)))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX, port id = 0x%x, num_old_bytes=%lu",
                         module_ptr->gu.module_instance_id,
                         out_port_ptr->gu.cmn.id,
                         out_port_ptr->common.bufs_ptr[0].actual_data_len);
      }
   }
#endif // ERROR_CHECK_MODULE_PROCESS

   // for 1 looping, the sdata buf_ptr is already assigned properly. For more than one loop case, we neeed to use buf
   // from scratch mem as CAPI would need need data_ptr starting from zero (for output).
   if (1 == module_ptr->num_proc_loops)
   {
      return AR_EOK;
   }

   bufs = pc_ptr->out_port_scratch_ptr[out_port_ptr->gu.cmn.index].bufs;

   if (out_port_ptr->common.flags.is_pcm_unpacked) // Handles pcm unpacked V1 and V2
   {
      // update for both unpacked V1 and V2
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         bufs[b].data_ptr =
            out_port_ptr->common.bufs_ptr[b].data_ptr + out_port_ptr->common.bufs_ptr[0].actual_data_len;
      }
      bufs[0].actual_data_len = 0;
      bufs[0].max_data_len =
         out_port_ptr->common.bufs_ptr[0].max_data_len - out_port_ptr->common.bufs_ptr[0].actual_data_len;

      // update rest of the channel lengths, only for unpacked V1
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         for (uint32_t b = 1; b < out_port_ptr->common.sdata.bufs_num; b++)
         {
            bufs[b].actual_data_len = 0;
            bufs[b].max_data_len =
               out_port_ptr->common.bufs_ptr[0].max_data_len - out_port_ptr->common.bufs_ptr[0].actual_data_len;
         }
      }
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == med_fmt_ptr->data_format)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX, num_proc_loops > 1 not supported",
               module_ptr->gu.module_instance_id);
   }
   else
   {
      // pcm packed case, interleaved
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         bufs[b].data_ptr =
            out_port_ptr->common.bufs_ptr[b].data_ptr + out_port_ptr->common.bufs_ptr[b].actual_data_len;
         bufs[b].actual_data_len = 0;
         bufs[b].max_data_len =
            out_port_ptr->common.bufs_ptr[b].max_data_len - out_port_ptr->common.bufs_ptr[b].actual_data_len;
      }
   }

   out_port_ptr->common.sdata.buf_ptr = bufs;
   return result;
}

/**
 * Update output buffer after process
 */
ar_result_t gen_topo_update_out_buf_len_after_process(gen_topo_t *            topo_ptr,
                                                      gen_topo_output_port_t *out_port_ptr,
                                                      bool_t                  err_check,
                                                      uint32_t *              prev_actual_data_len)
{
   gen_topo_process_context_t *pc_ptr              = &topo_ptr->proc_context;
   gen_topo_module_t *         module_ptr          = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   topo_media_fmt_t *          med_fmt_ptr         = out_port_ptr->common.media_fmt_ptr;
   capi_stream_data_v2_t *     sdata_ptr           = pc_ptr->out_port_sdata_pptr[out_port_ptr->gu.cmn.index];
   uint32_t                    supposed_len_per_ch = 0;

   // use sdata_ptr->bufs because buffers actual could be from  out_port_ptr->common.bufs_ptr (num_proc_loops=1) or from
   // pc_ptr->bufs (num_proc_loops>1)

   if ((1 == module_ptr->num_proc_loops) && !err_check)
   {
      return AR_EOK;
   }

   if (err_check)
   {
      if ((SPF_IS_PCM_DATA_FORMAT(med_fmt_ptr->data_format) &&
           (TOPO_DEINTERLEAVED_PACKED == med_fmt_ptr->pcm.interleaving)) ||
          (SPF_DEINTERLEAVED_RAW_COMPRESSED == med_fmt_ptr->data_format))
      {
         // After process we need to rearrange if we had old data
         // [oldL, oldR, newL, newR] -> [oldL, newL, oldR, newR]
         // don't check out_port_ptr->common.bufs_ptr[0].actual_data_len because it's updated by CAPI in this process
         // call.
         if (prev_actual_data_len[0])
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX, Port id 0x%lx, stale data %lu not allowed ",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            prev_actual_data_len[0]);
         }
      }

      // Only for unpacked V1: check if all the chs have same actual len after process
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         supposed_len_per_ch = sdata_ptr->buf_ptr[0].actual_data_len;

         for (uint32_t ch = 0; ch < med_fmt_ptr->pcm.num_channels; ch++)
         {
            if (supposed_len_per_ch != sdata_ptr->buf_ptr[ch].actual_data_len)
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_ERROR_PRIO,
                               "Module 0x%lX, Port id 0x%lx, all channels must have same amount of data (interleaving "
                               "issue),"
                               "supposed_len_per_ch = %d and actual_data_len = %d",
                               module_ptr->gu.module_instance_id,
                               out_port_ptr->gu.cmn.id,
                               supposed_len_per_ch,
                               sdata_ptr->buf_ptr[ch].actual_data_len);
               return AR_EFAILED;
            }
         }
      }
   }

   // aggregate output data after every loop
   for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&out_port_ptr->common); b++)
   {
      // for num_proc loops only pointers are diff and need aggregation.
      if ((capi_buf_t *)out_port_ptr->common.bufs_ptr != sdata_ptr->buf_ptr)
      {
         out_port_ptr->common.bufs_ptr[b].actual_data_len += sdata_ptr->buf_ptr[b].actual_data_len;

         // important: for output common.sdata.bufs_ptr is assigned with the scratch buf array, and it is different from
         // the common.bufs_ptr. common.bufs_ptr stores the aggregated length in case of num_proc_loops > 1. But scratch
         // buffer has lengths corresponding to the current loop. Hence scratch lengths life time is only during proc
         // context and we can skip resetting it after loop, because it will be reset in the next process context in
         // gen_topo_populate_sdata_bufs(). This behavior is different for input port both are same and assigned in
         // gen_topo_initialize_bufs_sdata()
         //
         // Most importantly, output sdata actual lengths should not be reset before calling the attached
         // module process. else attached see's input length as zero.
         //
         // sdata_ptr->buf_ptr[b].actual_data_len = 0;
      }

#ifdef ERROR_CHECK_MODULE_PROCESS
      if (err_check)
      {
         if (out_port_ptr->common.bufs_ptr[b].actual_data_len > out_port_ptr->common.bufs_ptr[b].max_data_len)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX, Port id 0x%lx, buffer overflow (max %lu, actual %lu)",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            out_port_ptr->common.bufs_ptr[b].max_data_len,
                            out_port_ptr->common.bufs_ptr[b].actual_data_len);
         }
      }
#endif
   }

   return AR_EOK;
}

#ifdef ERROR_CHECK_MODULE_PROCESS
ar_result_t gen_topo_err_check_inp_buf_len_after_process(gen_topo_module_t     *module_ptr,
                                                         gen_topo_input_port_t *in_port_ptr,
                                                         uint32_t              *prev_actual_data_len)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_t            *topo_ptr    = module_ptr->topo_ptr;
   capi_stream_data_v2_t *sdata_ptr   = &in_port_ptr->common.sdata;

   result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                         &in_port_ptr->common,
                                         TRUE /**is_input*/,
                                         in_port_ptr->gu.cmn.index,
                                         module_ptr,
                                         TRUE);
   if (AR_FAILED(result))
   {
      return AR_EFAILED;
   }

   uint32_t num_sdata_bufs_to_check = gen_topo_get_num_sdata_bufs_to_update(&in_port_ptr->common);
   for (uint32_t b = 0; b < num_sdata_bufs_to_check; b++)
   {
      if (prev_actual_data_len[b] < sdata_ptr->buf_ptr[b].actual_data_len)
      {
         gen_topo_capi_algorithmic_reset(topo_ptr->gu.log_id,
                                         module_ptr->capi_ptr,
                                         TRUE,
                                         TRUE,
                                         in_port_ptr->gu.cmn.index);

         gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
         return AR_EFAILED;
      }

      if ((prev_actual_data_len[b] == sdata_ptr->buf_ptr[0].actual_data_len) &&
          (prev_actual_data_len[b] != sdata_ptr->buf_ptr[b].actual_data_len))
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX: process after: if input buffer is consumed fully in first buf, it must be "
                         "fully "
                         "consumed in all. bug in CAPI",
                         module_ptr->gu.module_instance_id);
         return AR_EFAILED;
      }
   }

   // Only for unpacked V1: check if all the channels have same actual lengths
   if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == in_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
   {
      uint32_t supposed_len_per_ch = 0;

      supposed_len_per_ch = sdata_ptr->buf_ptr[0].actual_data_len;
      for (uint32_t ch = 1; ch < num_sdata_bufs_to_check; ch++)
      {
         if (supposed_len_per_ch != sdata_ptr->buf_ptr[ch].actual_data_len)
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " same bytes not consumed on all channels. module 0x%lX, port-id 0x%lx, expected: %lu, "
                            "actual: %lu ch %d",
                            in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                            in_port_ptr->gu.cmn.id,
                            supposed_len_per_ch,
                            sdata_ptr->buf_ptr[ch].actual_data_len,
                            ch);
            return AR_EFAILED;
         }
      }
   }

   return AR_EOK;
}
#endif

/** If there is remaining data, moves data to the begining of the buffer.
 *  If there is no data left, resets actual_data len to zero.*/
ar_result_t gen_topo_move_data_to_beginning_after_process(gen_topo_t *           topo_ptr,
                                                          gen_topo_input_port_t *in_port_ptr,
                                                          uint32_t               remaining_size_after_per_buf,
                                                          uint32_t *             prev_actual_data_len)
{
   topo_media_fmt_t *med_fmt_ptr = in_port_ptr->common.media_fmt_ptr;
   topo_buf_t *      bufs_ptr    = in_port_ptr->common.bufs_ptr;

   // This function needs to update actual data len

   // remaining_size_after_per_buf is based on first ch or buf only, which is sufficient to check if any data remains.
   // Exact amount varies per buf.
   // whether remaining length is zero or not, the 'else' logic works.
   if (SPF_IS_PCM_DATA_FORMAT(med_fmt_ptr->data_format))
   {
      if (remaining_size_after_per_buf && (TOPO_DEINTERLEAVED_PACKED == med_fmt_ptr->pcm.interleaving))
      {
         uint32_t new_ch_spacing = 0, old_ch_spacing = 0, bytes_used_per_ch = 0;
         topo_div_three_nums(remaining_size_after_per_buf,
                             &new_ch_spacing,
                             (bufs_ptr[0].actual_data_len + remaining_size_after_per_buf),
                             &old_ch_spacing,
                             bufs_ptr[0].actual_data_len,
                             &bytes_used_per_ch,
                             med_fmt_ptr->pcm.num_channels);

         for (uint32_t ch = med_fmt_ptr->pcm.num_channels; ch > 0; ch--)
         {
            TOPO_MEMSMOV_NO_RET(bufs_ptr[0].data_ptr + (ch - 1) * new_ch_spacing,
                                new_ch_spacing,
                                bufs_ptr[0].data_ptr + bytes_used_per_ch + (ch - 1) * old_ch_spacing,
                                new_ch_spacing,
                                topo_ptr->gu.log_id,
                                "E2B: (0x%lX, 0x%lX) ", // end to begin
                                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                in_port_ptr->gu.cmn.id);
         }
      }
      else if(remaining_size_after_per_buf)
      {
         /** Handles unpacked V1, unpacked V2 and interleaved formats*/

         // move data for all chs based on remaining len of the first ch buffer.
         uint32_t actual_data_len_per_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;
         uint32_t max_data_len_per_buf    = in_port_ptr->common.bufs_ptr[0].max_data_len;

         for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            TOPO_MEMSMOV_NO_RET(in_port_ptr->common.bufs_ptr[b].data_ptr,
                                max_data_len_per_buf,
                                in_port_ptr->common.bufs_ptr[b].data_ptr + actual_data_len_per_buf,
                                remaining_size_after_per_buf,
                                topo_ptr->gu.log_id,
                                "E2B: (0x%lX, 0x%lX) ", // end to begin
                                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                in_port_ptr->gu.cmn.id);
         }
      }

      // update only first buf's remaining len, for all PCM interleaving types.
      in_port_ptr->common.bufs_ptr[0].actual_data_len = remaining_size_after_per_buf;
   }
   else // non pcm format
   {
      // don't use remaining_size_after_per_buf, as each buf can have diff data (for deint-raw-compr
      for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
      {
         uint32_t remaining = prev_actual_data_len[b] - in_port_ptr->common.bufs_ptr[b].actual_data_len;
         if (remaining) // avoid call to memsmove
         {
            TOPO_MEMSMOV_NO_RET(in_port_ptr->common.bufs_ptr[b].data_ptr,
                                in_port_ptr->common.bufs_ptr[b].max_data_len,
                                in_port_ptr->common.bufs_ptr[b].data_ptr +
                                   in_port_ptr->common.bufs_ptr[b].actual_data_len,
                                remaining,
                                topo_ptr->gu.log_id,
                                "E2B: (0x%lX, 0x%lX) ", // end to begin
                                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                in_port_ptr->gu.cmn.id);
         }
         in_port_ptr->common.bufs_ptr[b].actual_data_len = remaining;
      }
   }
   return AR_EOK;
}

capi_err_t gen_topo_copy_input_to_output(gen_topo_t *        topo_ptr,
                                         gen_topo_module_t * module_ptr,
                                         capi_stream_data_t *inputs[],
                                         capi_stream_data_t *outputs[])
{
   capi_err_t result = CAPI_EOK;

   gu_input_port_list_t * in_port_list_ptr = module_ptr->gu.input_port_list_ptr;
   gen_topo_input_port_t *in_port_ptr      = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
   uint32_t               ip_idx           = in_port_ptr->gu.cmn.index;

   // memcpy input to output
   // works for only one input port. for metadata only one output port must be present (as we are not cloning here).
   if (!module_ptr->gu.flags.is_siso)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      " Module 0x%lX: for memcpy from input to output, only one input or output must be present ",
                      module_ptr->gu.module_instance_id);
      return CAPI_EFAILED;
   }

   // Disabled placeholder decoder/encoder also enter here.

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      // Copy available data as is. Don't check for threshold or for media-format.
      // this is inline with the behavior of requires_data_buf = FALSE & port_has_no_threshold
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      uint32_t                op_idx       = out_port_ptr->gu.cmn.index;
      if (inputs[ip_idx]->bufs_num != outputs[op_idx]->bufs_num)
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Module 0x%lX: bufs_num must match between input[0x%lx] (%lu) and output[0x%lx] (%lu)",
                         module_ptr->gu.module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         inputs[ip_idx]->bufs_num,
                         out_port_ptr->gu.cmn.id,
                         outputs[op_idx]->bufs_num);
      }

      if (in_port_ptr->common.flags.is_pcm_unpacked)
      {
         uint32_t out_max_data_len   = outputs[op_idx]->buf_ptr[0].max_data_len;
         uint32_t in_actual_data_len = inputs[ip_idx]->buf_ptr[0].actual_data_len;
         for (uint32_t i = 0; i < MIN(inputs[ip_idx]->bufs_num, outputs[op_idx]->bufs_num); i++)
         {
            if (!inputs[ip_idx]->buf_ptr[i].data_ptr || !outputs[op_idx]->buf_ptr[i].data_ptr)
            {
               continue;
            }

            if (inputs[ip_idx]->buf_ptr[i].data_ptr != outputs[op_idx]->buf_ptr[i].data_ptr)
            {
               TOPO_MEMSCPY_NO_RET(outputs[op_idx]->buf_ptr[i].data_ptr,
                                   out_max_data_len,
                                   inputs[ip_idx]->buf_ptr[i].data_ptr,
                                   in_actual_data_len,
                                   topo_ptr->gu.log_id,
                                   "I2O: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                                   module_ptr->gu.module_instance_id,
                                   in_port_ptr->gu.cmn.id,
                                   module_ptr->gu.module_instance_id,
                                   out_port_ptr->gu.cmn.id);
            }
         }
         outputs[op_idx]->buf_ptr[0].actual_data_len = MIN(out_max_data_len, in_actual_data_len);
      }
      else // intlerleaved, packed and non-pcm
      {
         for (uint32_t i = 0; i < MIN(inputs[ip_idx]->bufs_num, outputs[op_idx]->bufs_num); i++)
         {
            if (!inputs[ip_idx]->buf_ptr[i].data_ptr || !outputs[op_idx]->buf_ptr[i].data_ptr)
            {
               continue;
            }

            uint32_t  out_max_data_len    = outputs[op_idx]->buf_ptr[i].max_data_len;
            uint32_t  in_actual_data_len  = inputs[ip_idx]->buf_ptr[i].actual_data_len;
            uint32_t *out_actual_data_len = &(outputs[op_idx]->buf_ptr[i].actual_data_len);

            if (inputs[ip_idx]->buf_ptr[i].data_ptr != outputs[op_idx]->buf_ptr[i].data_ptr)
            {
               TOPO_MEMSMOV((*out_actual_data_len),
                            outputs[op_idx]->buf_ptr[i].data_ptr,
                            out_max_data_len,
                            inputs[ip_idx]->buf_ptr[i].data_ptr,
                            in_actual_data_len,
                            topo_ptr->gu.log_id,
                            "I2O: (0x%lX, 0x%lX) -> (0x%lX, 0x%lX)",
                            module_ptr->gu.module_instance_id,
                            in_port_ptr->gu.cmn.id,
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id);
            }
            else
            {
               *out_actual_data_len = MIN(out_max_data_len, in_actual_data_len);
            }
         }
      }
   }

   return result;
}

void gen_topo_process_attached_module_to_output(gen_topo_t             *topo_ptr,
                                                gen_topo_module_t      *module_ptr,
                                                gen_topo_output_port_t *out_port_ptr)
{
   capi_err_t attached_proc_result = CAPI_EOK;
      if (!out_port_ptr->gu.attached_module_ptr)
      {
      return;
      }

         gen_topo_module_t *out_attached_module_ptr = (gen_topo_module_t *)out_port_ptr->gu.attached_module_ptr;
      gen_topo_input_port_t *attached_mod_ip_port_ptr =
         (gen_topo_input_port_t *)out_attached_module_ptr->gu.input_port_list_ptr->ip_port_ptr;

         if (TOPO_DATA_FLOW_STATE_FLOWING == out_port_ptr->common.data_flow_state)
         {
            gen_topo_handle_data_flow_begin(topo_ptr,
                                            &(attached_mod_ip_port_ptr->common),
                                            &(attached_mod_ip_port_ptr->gu.cmn));
         }

         if (out_attached_module_ptr->flags.disabled || (!out_port_ptr->common.flags.is_mf_valid) ||
             (!out_port_ptr->common.sdata.buf_ptr) ||
             ((0 == out_port_ptr->common.sdata.buf_ptr[0].actual_data_len) &&
              (NULL == out_port_ptr->common.sdata.metadata_list_ptr)) ||
             (attached_mod_ip_port_ptr->common.flags.module_rejected_mf))
         {
#ifdef VERBOSE_DEBUGGING
      uint32_t mask = ((out_attached_module_ptr->flags.disabled << 2) | (out_port_ptr->common.flags.is_mf_valid << 1) |
                attached_mod_ip_port_ptr->common.flags.module_rejected_mf);
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_MED_PRIO,
                     "Skipping process on attached elementary module miid 0x%lx for input port idx %ld miid 0x%lx, "
                     "(0X%X - B2:disable, B1:mf_valid, B0:rej_mf), no buffer %ld, no data provided %ld, md list "
                     "ptr "
                     "NULL %ld",
                     out_attached_module_ptr->gu.module_instance_id,
                     out_port_ptr->gu.cmn.index,
                     module_ptr->gu.module_instance_id,
                     mask,
                     (NULL == out_port_ptr->common.sdata.buf_ptr),
                     (NULL == out_port_ptr->common.sdata.buf_ptr)
                        ? 0
                        : (0 == out_port_ptr->common.sdata.buf_ptr[0].actual_data_len),
                     (NULL == out_port_ptr->common.sdata.metadata_list_ptr));
#endif
      return;
         }

         uint32_t out_port_idx = out_port_ptr->gu.cmn.index;

         // If attached doesnt support unpacked V2, update lens for all the channels,
         // because host may be operating with unpacked V2
         if (GEN_TOPO_MF_PCM_UNPACKED_V1 == attached_mod_ip_port_ptr->common.flags.is_pcm_unpacked)
         {
            capi_stream_data_v2_t *output_sdata_ptr        = topo_ptr->proc_context.out_port_sdata_pptr[out_port_idx];
            uint32_t               actual_data_len_per_buf = output_sdata_ptr->buf_ptr[0].actual_data_len;
            uint32_t               max_data_len_per_buf    = output_sdata_ptr->buf_ptr[0].max_data_len;
            for (uint32_t i = 1; i < output_sdata_ptr->bufs_num; i++)
            {
               output_sdata_ptr->buf_ptr[i].actual_data_len = actual_data_len_per_buf;
               output_sdata_ptr->buf_ptr[i].max_data_len    = max_data_len_per_buf;
            }
         }

#ifdef VERBOSE_DEBUGGING
         PRINT_PORT_INFO_AT_PROCESS(out_attached_module_ptr->gu.module_instance_id,
                                    out_port_ptr->gu.cmn.id,
                                    out_port_ptr->common,
                                    attached_proc_result,
                                    "output",
                                    "before");
#endif

         // clang-format off
         IRM_PROFILE_MOD_PROCESS_SECTION(out_attached_module_ptr->prof_info_ptr,  topo_ptr->gu.prof_mutex,
         attached_proc_result =
            out_attached_module_ptr->capi_ptr->vtbl_ptr
               ->process(out_attached_module_ptr->capi_ptr,
                         (capi_stream_data_t **)&(topo_ptr->proc_context.out_port_sdata_pptr[out_port_idx]),
                         (capi_stream_data_t **)&(topo_ptr->proc_context.out_port_sdata_pptr[out_port_idx]));

         );
         // clang-format on

#ifdef VERBOSE_DEBUGGING
         PRINT_PORT_INFO_AT_PROCESS(out_attached_module_ptr->gu.module_instance_id,
                                    out_port_ptr->gu.cmn.id,
                                    out_port_ptr->common,
                                    attached_proc_result,
                                    "output",
                                    "after");
#endif

#ifdef VERBOSE_DEBUGGING
         // Don't ignore need more for attached modules.
         if (CAPI_FAILED(attached_proc_result))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Attached elementary module miid 0x%lx for output port idx %ld id 0x%lx returned error 0x%lx "
                     "during process",
                     out_attached_module_ptr->gu.module_instance_id,
                     out_port_ptr->gu.cmn.index,
                     module_ptr->gu.module_instance_id,
                     attached_proc_result);
         }
#endif
    }

void gen_topo_process_attached_elementary_modules(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   /**
    * Process output side attached point modules.
    */
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      gen_topo_process_attached_module_to_output(topo_ptr, module_ptr, out_port_ptr);
   }
}

/**
 * return code includes need_more
 */
GEN_TOPO_STATIC ar_result_t gen_topo_module_process(gen_topo_t *       topo_ptr,
                                                    gen_topo_module_t *module_ptr,
                                                    bool_t *           terminate_ptr,
                                                    bool_t             is_final_loop)
{
   capi_err_t          result                           = CAPI_EOK;
   capi_err_t          proc_result                      = CAPI_EOK;
   ar_result_t         local_result                     = AR_EOK;
   bool_t              need_more                        = FALSE; // insufficient input
   bool_t              any_out_produced                 = FALSE;
   bool_t              is_first_out_port                = TRUE;
   bool_t              is_first_in_port                 = TRUE;
   bool_t              at_least_one_eof                 = FALSE;
   bool_t              input_fields_set                 = FALSE;
   bool_t              output_fields_set                = FALSE;
   bool_t              input_has_metadata_or_eos        = FALSE;
   bool_t              is_siso_input_raw_and_output_pcm = FALSE;
   bool_t              fwk_owns_md_prop                 = gen_topo_fwk_owns_md_prop(module_ptr);
   int64_t             input_ts                         = 0;
   uint64_t            bytes_to_time                    = 0; // for incrementing input TS
   uint32_t            in_bytes_before_for_all_ch       = 0; // for MD prop
   uint32_t            in_bytes_consumed_for_all_ch     = 0; // for SISO, for MD prop
   uint32_t            out_bytes_produced_for_all_ch    = 0; // for SISO, for MD prop and TS
   capi_stream_flags_t input_flags = {.word = 0 };
   capi_stream_flags_t output_flags = {.word = 0 };
   bool_t              err_check = FALSE;
#if defined(ERROR_CHECK_MODULE_PROCESS)
   err_check = TRUE;
// bool_t err_check = ((AR_GUID_OWNER_QC != (module_ptr->gu.module_id & AR_GUID_OWNER_MASK)));
#endif

   uint32_t op_idx = 0, ip_idx = 0;
   uint32_t m_iid = module_ptr->gu.module_instance_id;

   gen_topo_process_context_t *pc               = &topo_ptr->proc_context;
   gen_topo_process_info_t *   process_info_ptr = &pc->process_info;
   gen_topo_input_port_t *     in_port_ptr      = NULL;
   gen_topo_output_port_t *    out_port_ptr     = NULL;
   capi_stream_data_v2_t *     sdata_ptr        = NULL;

   // *terminate_ptr = FALSE; /* caller inits to false so no need to reset*/

   gen_topo_reset_process_context_sdata(pc, module_ptr);

   // Graph utils puts the ports in the index order.
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      in_port_ptr                                = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      ip_idx                                     = in_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &in_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->in_port_scratch_ptr[ip_idx];

      if ((1 == module_ptr->gu.num_input_ports) && (1 == module_ptr->gu.max_input_ports))
      {
         input_fields_set = TRUE;
         input_ts         = sdata_ptr->timestamp;
         input_flags      = sdata_ptr->flags;
         if (SPF_IS_RAW_COMPR_DATA_FMT(in_port_ptr->common.media_fmt_ptr->data_format))
         {
            is_siso_input_raw_and_output_pcm = TRUE;
         }
      }

      // pushes zeros only when fwk owns metadata (checks are inside)
      if (topo_is_sdata_flag_EOS_or_EOF_set(sdata_ptr))
      {
         gen_topo_push_zeros_at_eos(topo_ptr, module_ptr, in_port_ptr);
         at_least_one_eof |= sdata_ptr->flags.end_of_frame;
         input_has_metadata_or_eos |= sdata_ptr->flags.marker_eos;
      }

      // Note that these scratch flags are implicitly reset if eos/eof is not present.
      scr_data_ptr->flags.prev_eos_dfg = sdata_ptr->flags.marker_eos;
      scr_data_ptr->flags.prev_eof     = sdata_ptr->flags.end_of_frame;

      if (sdata_ptr->metadata_list_ptr)
      {
         input_has_metadata_or_eos = TRUE;
         // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
         // island while copying md at container input or propagating md
         scr_data_ptr->flags.prev_eos_dfg |= gen_topo_md_list_has_dfg(sdata_ptr->metadata_list_ptr);
      }

      scr_data_ptr->prev_actual_data_len[0] = in_port_ptr->common.bufs_ptr[0].actual_data_len;
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == in_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         // for unpacked V1 set sdata lengths for all the channels
         uint32_t actual_data_len = in_port_ptr->common.sdata.buf_ptr[0].actual_data_len;
         uint32_t max_data_len    = in_port_ptr->common.sdata.buf_ptr[0].max_data_len;
         for (uint32_t b = 1; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            in_port_ptr->common.sdata.buf_ptr[b].actual_data_len = actual_data_len;
            in_port_ptr->common.sdata.buf_ptr[b].max_data_len    = max_data_len;
         }
      }
      else if (GEN_TOPO_MF_NON_PCM_UNPACKED == in_port_ptr->common.flags.is_pcm_unpacked)
      {
         /** If not unpacked, need to cache prev lengths for all the buffers. */

         for (uint32_t b = 1; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            scr_data_ptr->prev_actual_data_len[b] = in_port_ptr->common.bufs_ptr[b].actual_data_len;
         }
      }

      in_bytes_before_for_all_ch = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);

#ifdef VERBOSE_DEBUGGING
      PRINT_PORT_INFO_AT_PROCESS(m_iid, in_port_ptr->gu.cmn.id, in_port_ptr->common, result, "input", "before");
#endif

      pc->in_port_sdata_pptr[ip_idx] = &in_port_ptr->common.sdata;

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &in_port_ptr->common,
                                            TRUE /**is_input*/,
                                            in_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            FALSE);
      if (AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &out_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      if ((1 == module_ptr->gu.num_output_ports) && (1 == module_ptr->gu.max_output_ports))
      {
         output_fields_set = TRUE;
         output_flags      = sdata_ptr->flags;
         if ((is_siso_input_raw_and_output_pcm) &&
             (!SPF_IS_PCM_DATA_FORMAT(out_port_ptr->common.media_fmt_ptr->data_format)))
         {
            is_siso_input_raw_and_output_pcm = FALSE;
         }
      }

      // store bytes from prev looping. except for looping, in gen_cntr we dont call module if there's prev data
      scr_data_ptr->prev_actual_data_len[0] = out_port_ptr->common.bufs_ptr[0].actual_data_len;
      if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->common.flags.is_pcm_unpacked) && module_ptr->capi_ptr)
      {
         // for unpacked V1 set sdata lengths for all the channels
         uint32_t actual_data_len = out_port_ptr->common.sdata.buf_ptr[0].actual_data_len;
         uint32_t max_data_len    = out_port_ptr->common.sdata.buf_ptr[0].max_data_len;
         for (uint32_t b = 1; b < out_port_ptr->common.sdata.bufs_num; b++)
         {
            out_port_ptr->common.sdata.buf_ptr[b].actual_data_len = actual_data_len;
            out_port_ptr->common.sdata.buf_ptr[b].max_data_len    = max_data_len;
         }
      }
      else if (GEN_TOPO_MF_NON_PCM_UNPACKED == out_port_ptr->common.flags.is_pcm_unpacked)
      {
         // cache prev lengths for all the buffers if not unpacked.
         for (uint32_t b = 1; b < out_port_ptr->common.sdata.bufs_num; b++)
         {
            scr_data_ptr->prev_actual_data_len[b] = out_port_ptr->common.bufs_ptr[b].actual_data_len;
         }
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: output port id 0x%lx, process before: length_per_buf %lu of %lu. buff addr 0x%p",
                      m_iid,
                      out_port_ptr->gu.cmn.id,
                      out_port_ptr->common.bufs_ptr[0].actual_data_len,
                      out_port_ptr->common.bufs_ptr[0].max_data_len,
                      out_port_ptr->common.bufs_ptr[0].data_ptr);
#endif

      /**
       * Choose one input port timestamp for the output ports (SISO). Modules may override it.
       * TS can be copied independent of metadata
       *
       * Metadata is not copied to output before process (including EOS).
       *
       * If there is stale output data, don't overwrite output ts with input ts.
       * This can happen in FTRT [Dam module] case.
       */
      if (input_fields_set && (0 == out_port_ptr->common.bufs_ptr[0].actual_data_len))
      {
         sdata_ptr->timestamp                = input_ts - module_ptr->algo_delay;
         sdata_ptr->flags.is_timestamp_valid = input_flags.is_timestamp_valid;
         sdata_ptr->flags.ts_continue        = input_flags.ts_continue;
         sdata_ptr->flags.erasure            = input_flags.erasure;
         if ((is_siso_input_raw_and_output_pcm) && (input_flags.ts_continue))
         {
            sdata_ptr->flags.ts_continue        = FALSE;
            sdata_ptr->flags.is_timestamp_valid = TRUE;
         }
      }

      if ((module_ptr->num_proc_loops) > 1 || err_check)
      {
         if (AR_EOK != (local_result = gen_topo_populate_sdata_bufs(topo_ptr, module_ptr, out_port_ptr, err_check)))
         {
            return local_result;
         }
      }

      // if there's any metadata in the output bufs, move it to separate list (happens when looping)
      scr_data_ptr->md_list_ptr = NULL;
      if (sdata_ptr->metadata_list_ptr)
      {
         spf_list_merge_lists((spf_list_node_t **)&scr_data_ptr->md_list_ptr,
                              (spf_list_node_t **)&sdata_ptr->metadata_list_ptr);
      }

      pc->out_port_sdata_pptr[op_idx] = &out_port_ptr->common.sdata;

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &out_port_ptr->common,
                                            FALSE /**is_input*/,
                                            out_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            FALSE);
      if (AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif
   }

   /**
    * At input of process, the input->actual_len is the size of input & data starts from data_ptr.
    *                      the output->actual_len is uninitialized & CAPI can write from data_ptr.
    *                                remaining data is from data_ptr+actual_len
    * At output of process, the input->actual_len is the amount of data consumed (read) by CAPI.
    *                      the output->actual_len is output data, & data starts from data_ptr.
    */

#ifdef PROC_DELAY_DEBUG
   uint64_t time_before = posal_timer_get_time();
#endif

   if (module_ptr->capi_ptr && (!module_ptr->bypass_ptr))
   {
      pc->process_info.is_in_mod_proc_context = TRUE;

      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,
      result = module_ptr->capi_ptr->vtbl_ptr->process(module_ptr->capi_ptr,
                                                       (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                                       (capi_stream_data_t **)pc->out_port_sdata_pptr);
      );
      // clang-format on

      pc->process_info.is_in_mod_proc_context = FALSE;
      if (is_siso_input_raw_and_output_pcm)
      {
         capi_stream_data_t *input_stream_temp_ptr = (capi_stream_data_t *)pc->in_port_sdata_pptr[0];
         if ((input_stream_temp_ptr) && (input_stream_temp_ptr->flags.ts_continue))
         {

            capi_stream_data_t *output_stream_temp_ptr = (capi_stream_data_t *)pc->out_port_sdata_pptr[0];
            if (output_stream_temp_ptr)
            {
               output_stream_temp_ptr->flags.ts_continue        = FALSE;
               output_stream_temp_ptr->flags.is_timestamp_valid = TRUE;
            }
         }
      }
   }
   else // PCM use cases, SH MEM EP, bypass use cases etc
   {
      // metadata prop is taken care in gen_topo_propagate_metadata
      // clang-format off
      IRM_PROFILE_MOD_PROCESS_SECTION(module_ptr->prof_info_ptr, topo_ptr->gu.prof_mutex,
      result = gen_topo_copy_input_to_output(topo_ptr,
                                             module_ptr,
                                             (capi_stream_data_t **)pc->in_port_sdata_pptr,
                                             (capi_stream_data_t **)pc->out_port_sdata_pptr);
      );
      // clang-format on
   }
#ifdef PROC_DELAY_DEBUG
   if (IS_VOICE_SCENARIO_ID(module_ptr->gu.sg_ptr->sid))
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "PROC_DELAY_DEBUG: GC Module 0x%lX: Process took %lu us",
                      m_iid,
                      (uint32_t)(posal_timer_get_time() - time_before));
   }
#endif

   proc_result = result;
   need_more   = (result == CAPI_ENEEDMORE);

   /**
    * loop over output ports to do sanity checks, adjust lengths and populate few variables.
    */
   bool_t atleast_one_elementary_mod_is_present = FALSE;
   is_first_out_port                            = TRUE;
   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      sdata_ptr                                  = &out_port_ptr->common.sdata;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      atleast_one_elementary_mod_is_present |= (NULL != out_port_ptr->gu.attached_module_ptr);

#ifdef SAFE_MODE
      if (CAPI_STREAM_V2 != sdata_ptr->flags.stream_data_version)
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Warning: Module 0x%lX: output port id 0x%lx, Invalid stream data version %lu, flags 0x%lX",
                         m_iid,
                         out_port_ptr->gu.cmn.id,
                         sdata_ptr->flags.stream_data_version,
                         sdata_ptr->flags.word);
      }
#endif

#ifdef ERROR_CHECK_MODULE_PROCESS
      result = gen_topo_validate_port_sdata(topo_ptr->gu.log_id,
                                            &out_port_ptr->common,
                                            FALSE /**is_input*/,
                                            out_port_ptr->gu.cmn.index,
                                            module_ptr,
                                            TRUE);
      if (AR_FAILED(result))
      {
         return AR_EFAILED;
      }
#endif

      /* Since 61937 depacketizer can mark EOF to indicate that output has integral number of frames, we skip error
       * check for it. */
      if (AMDB_MODULE_TYPE_DEPACKETIZER != module_ptr->gu.module_type)
      {
         // for SISO modules check if module propagated metadata by mistake.
         if (fwk_owns_md_prop && (sdata_ptr->metadata_list_ptr ||
                                  (output_fields_set && ((output_flags.end_of_frame ^ sdata_ptr->flags.end_of_frame) ||
                                                         (output_flags.marker_eos ^ sdata_ptr->flags.marker_eos)))))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: output port id 0x%lx, module is not supposed to propagate metadata but yet "
                            "it did. ignoring module changes. md list ptr 0x%lX, flags (before: 0x%lX, after: 0x%lX). "
                            "output_fields_set%u",
                            m_iid,
                            out_port_ptr->gu.cmn.id,
                            sdata_ptr->metadata_list_ptr,
                            output_flags.word,
                            sdata_ptr->flags.word,
                            output_fields_set);

            sdata_ptr->flags.end_of_frame = output_flags.end_of_frame;
            sdata_ptr->flags.marker_eos   = output_flags.marker_eos;
            sdata_ptr->metadata_list_ptr  = NULL;
         }
      }

      if (AR_EOK != gen_topo_update_out_buf_len_after_process(topo_ptr,
                                                              out_port_ptr,
                                                              err_check,
                                                              scr_data_ptr->prev_actual_data_len))
      {
         return AR_EFAILED;
      }

      // adjust timestamp for data already in the buffer (if any)
      // If no output data is generated in this process cycle, then retain previous TS.
      if (sdata_ptr->flags.is_timestamp_valid &&
          SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format) &&
          ((out_port_ptr->common.bufs_ptr[0].actual_data_len - scr_data_ptr->prev_actual_data_len[0]) > 0))
      {
         if (out_port_ptr->common.flags.is_pcm_unpacked)
         {
            sdata_ptr->timestamp -=
               topo_bytes_per_ch_to_us(scr_data_ptr->prev_actual_data_len[0], out_port_ptr->common.media_fmt_ptr, NULL);
         }
         else
         {
            sdata_ptr->timestamp -=
               topo_bytes_to_us(scr_data_ptr->prev_actual_data_len[0], out_port_ptr->common.media_fmt_ptr, NULL);
         }
      }

#ifdef VERBOSE_DEBUGGING
      PRINT_PORT_INFO_AT_PROCESS(m_iid, out_port_ptr->gu.cmn.id, out_port_ptr->common, proc_result, "output", "after");
#endif

      // for PCM & packetized output store the output duration as frame duration
      if (is_first_out_port) // otherwise bytes_to_time is overwritten
      {
         is_first_out_port = FALSE;
         // subtract prev bytes to take care of offset adjustments
         out_bytes_produced_for_all_ch =
            (out_port_ptr->common.bufs_ptr[0].actual_data_len - scr_data_ptr->prev_actual_data_len[0]);
         // see logic inside gen_topo_get_actual_len_for_md_prop
         if (SPF_DEINTERLEAVED_RAW_COMPRESSED != out_port_ptr->common.media_fmt_ptr->data_format)
         {
            out_bytes_produced_for_all_ch *= out_port_ptr->common.sdata.bufs_num;
         }

         if (sdata_ptr->flags.is_timestamp_valid &&
             SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format))
         {
            bytes_to_time = topo_bytes_to_us(out_bytes_produced_for_all_ch,
                                             out_port_ptr->common.media_fmt_ptr,
                                             &out_port_ptr->common.fract_timestamp_pns);
         }
      }
      // if we data or metadata is produced, move to flow-begin
      if ((out_port_ptr->common.bufs_ptr[0].actual_data_len > scr_data_ptr->prev_actual_data_len[0]) ||
          out_port_ptr->common.sdata.metadata_list_ptr || out_port_ptr->common.sdata.flags.erasure)
      {
         gen_topo_handle_data_flow_begin(topo_ptr, &out_port_ptr->common, &out_port_ptr->gu.cmn);

         // useful for nblc modules to process without checking for triggers.
         out_port_ptr->any_data_produced = TRUE;

         // since output/metada is produced by module mark the change.
         process_info_ptr->anything_changed = TRUE;
      }
   }

   /**
    * loop over input port to adjust length, check error etc
    */
   is_first_in_port = TRUE;
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      in_port_ptr                                      = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      ip_idx                                           = in_port_ptr->gu.cmn.index;
      sdata_ptr                                        = &in_port_ptr->common.sdata;
      topo_media_fmt_t *            med_fmt_ptr        = in_port_ptr->common.media_fmt_ptr;
      topo_buf_t *                  bufs_ptr           = in_port_ptr->common.bufs_ptr;
      gen_topo_port_scratch_data_t *scr_data_ptr       = &pc->in_port_scratch_ptr[ip_idx];
      bool_t                        local_need_more    = need_more;
      bool_t                        any_input_consumed = FALSE;

#ifdef ERROR_CHECK_MODULE_PROCESS
      if (err_check)
      {
         if (AR_EOK !=
             gen_topo_err_check_inp_buf_len_after_process(module_ptr, in_port_ptr, scr_data_ptr->prev_actual_data_len))
         {
            return AR_EFAILED;
         }
      }
#endif

      // by default disable TS check. Enable it only if we could update input TS based on data consumed at the input
      // or data produced at the output
      in_port_ptr->flags.disable_ts_disc_check = TRUE;

      uint32_t remaining_size_after = scr_data_ptr->prev_actual_data_len[0] - bufs_ptr[0].actual_data_len;

      any_input_consumed = (remaining_size_after != scr_data_ptr->prev_actual_data_len[0]);

      if (!in_port_ptr->flags.processing_began)
      {
         in_port_ptr->flags.processing_began = CAPI_SUCCEEDED(result);
         // before gen_topo_move_data_to_beginning_after_process
      }

      /**
       * if no input remains
       * if input didn't get consumed & output didn't get produced
       */
      if (local_need_more || (0 == remaining_size_after) || (!any_input_consumed && !any_out_produced))
      {
         // else EoS is overwritten
         *terminate_ptr      = TRUE;
         result              = CAPI_ENEEDMORE;
         local_need_more     = TRUE;
         bool_t is_in_filled = (remaining_size_after == bufs_ptr[0].max_data_len);
         // end_of_frame helps stop inf loop in case module don't clear EOF, however, doing so causes some genuine tests
         // to hang as need_more_input won't be set. wmastd_dec_nt_9
         // For pause module (trigger policy module) in container with WR EP and MFC (DM module), inf loop is possible
         //  if we don't check for !in_port_ptr->flags.need_more_input (as MFC keeps returning need-more but due to
         //  anything_changed set as TRUE we keep looping back.
         //  if we don't check for !in_port_ptr->flags.need_more_input (as MFC keeps returning need-more but due to
         //  anything_changed set as TRUE we keep looping back.
         if (!is_in_filled /*&& !sdata_ptr->flags.end_of_frame*/ && !in_port_ptr->flags.need_more_input)
         {
            in_port_ptr->flags.need_more_input = TRUE;
            // removing anything_changed can cause hangs in nt mode tests
            process_info_ptr->anything_changed = TRUE;
         }
      }

      /** when processing signal trigger , module must finish input.
       * Otherwise, it can cause backlog & upstream can lose regular timing.
       * (E.g. SPR and TRM)
       * Also if module doesn't require-data-buffering it must consume all input.
       *
       * Don't drop when module is raising media fmt or threshold as we will call the module again.
       *
       * Dont drop when data is non-PCM. Sincle LCM threshold is only honored for pcm modules, this dropping
       * to meet threshold reqs should also take place only for pcm modules.
       *
       * For eg: Added this condition to support cop depack module in EP container. When depack raises threshold, it
       * is not included in the lcm calculation(pack format not considered when pcm threshold module is present)
       * and container operates at hwep raised threshold
       * */
      if ((is_final_loop || (TRUE == *terminate_ptr)) && (0 != remaining_size_after) &&
          !gen_topo_any_process_call_events(topo_ptr))
      {
         bool_t is_dropped = FALSE;

         // Drop partial data in signal trigerred container only if the data is not in DM module's variable nblc path
         if ((GEN_TOPO_SIGNAL_TRIGGER == pc->curr_trigger) && gen_topo_is_port_in_realtime_path(&in_port_ptr->common) &&
             SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format) &&
             (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr, in_port_ptr)))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX: in-port-id 0x%lx: dropping %lu bytes_per_buf as modules must consume all "
                            "input at signal trigger. ",
                            m_iid,
                            in_port_ptr->gu.cmn.id,
                            remaining_size_after);
            // if we dont do this EP clients don't get uniform timing. Also ICB will have to be increased if we hold.
            is_dropped = TRUE;
         }
         else if (!module_ptr->flags.requires_data_buf)
         {
            // Do not drop data is data is pending due to EOF on other input ports.
            // Multi-input modules may not consume input data on some ports if EOF is set on others ports.
            // Test: upd_test_with_discont.
            // For single port cases, we drop unconsumed input data at EOF anyway.
            bool_t this_port_had_eof = scr_data_ptr->flags.prev_eof;
            if (at_least_one_eof && !this_port_had_eof)
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_HIGH_PRIO,
                               "Module 0x%lX: in-port-id 0x%lx: NOT dropping %lu bytes_per_buf even though modules "
                               "with "
                               "requires-data-buffering FALSE must consume all input, due to at least one EOF",
                               m_iid,
                               in_port_ptr->gu.cmn.id,
                               remaining_size_after);
            }
            else
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_ERROR_PRIO,
                               "Module 0x%lX: in-port-id 0x%lx: dropping %lu bytes_per_buf as modules with "
                               "requires-data-buffering FALSE must consume all input",
                               m_iid,
                               in_port_ptr->gu.cmn.id,
                               remaining_size_after);
               is_dropped = TRUE;
            }
         }

         if (is_dropped)
         {
            // if metadata is stuck inside module, it's NOT dropped here. When fwk is responsible for prop, it's
            // handled thru propagation below.
            for (uint32_t b = 0; b < gen_topo_get_num_sdata_bufs_to_update(&in_port_ptr->common); b++)
            {
               bufs_ptr[b].actual_data_len = scr_data_ptr->prev_actual_data_len[b];
            }
            // even for modules that propagate metadata, drop data at the input port.
            // Call with keep_buffer_associated_md set to true. This is needed to ensure dfg, eos,
            // start end md are not dropped along with the data
            if (in_port_ptr->common.sdata.metadata_list_ptr)
            {
               // Dont need to call island exit here even when md lib is compiled in nlpi because we would have exited
               // island while copying md at container input or propagating md
               gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                       module_ptr,
                                                       &in_port_ptr->common,
                                                       gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                       TRUE /*keep_eos_and_ba_md*/);
            }
         }
      }

#ifdef VERBOSE_DEBUGGING
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: Input port id 0x%lx: process after: inLengh consumed per buf %lu, result 0x%lx, "
                      "Flags0x%lX, need_more_input %u, anything_changed%u",
                      m_iid,
                      in_port_ptr->gu.cmn.id,
                      bufs_ptr[0].actual_data_len,
                      proc_result,
                      sdata_ptr->flags.word,
                      in_port_ptr->flags.need_more_input,
                      process_info_ptr->anything_changed);
#endif

      // buf_ptr->actual_data_len is updated here
      if (at_least_one_eof && fwk_owns_md_prop)
      {
         gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);
         gen_topo_handle_end_of_frame_after_process(topo_ptr,
                                                    module_ptr,
                                                    in_port_ptr,
                                                    scr_data_ptr->prev_actual_data_len,
                                                    local_need_more);
      }

      if (sdata_ptr->flags.is_timestamp_valid)
      {
         // if input is PCM/Packetized then update the timestamp based on the input data consumed.
         if (SPF_IS_PACKETIZED_OR_PCM(med_fmt_ptr->data_format))
         {
            // actual_data_len=consumed amount, whether process is successful or not.
            if (in_port_ptr->common.flags.is_pcm_unpacked)
            {
               bytes_to_time = topo_bytes_per_ch_to_us(bufs_ptr[0].actual_data_len,
                                                       med_fmt_ptr,
                                                       &in_port_ptr->common.fract_timestamp_pns);
            }
            else
            {
               bytes_to_time =
                  topo_bytes_to_us(bufs_ptr[0].actual_data_len, med_fmt_ptr, &in_port_ptr->common.fract_timestamp_pns);
            }
         }
         else
         {
            // bytes_to_time is updated based on the the output data generated.
         }

         /**
          * input TS is incremented either by frame size or by amount of data dropped.
          * Frame size obtained from PCM/packetized side or from input side if input & output both are
          * PCM/Packetized.
          * it's important to have input override output because we may drop lot of data before generating
          * output. for decoder: erroneous bit streams can have lot of data drop before successful decode.
          * however, time-stamp cannot be recovered unless new input buffer is picked. for depacketizer +
          * decoder: data drops are counted in correctly.
          */
         sdata_ptr->timestamp += (int64_t)bytes_to_time;

         // When some input is consumed, if corresponding time duration is known, then input timestamp is updated
         // properly and we can enable the disc check.
         if ((bufs_ptr[0].actual_data_len > 0) && bytes_to_time)
         {
            in_port_ptr->flags.disable_ts_disc_check = FALSE;
         }
      }
      else // is_timestamp_valid= false
      {
         // when TS is invalid, TS disc check can be enabled (validity diff)
         in_port_ptr->flags.disable_ts_disc_check = FALSE;
      }

      if (is_first_in_port)
      {
         in_bytes_consumed_for_all_ch = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);
         is_first_in_port             = FALSE;
      }
   }

   // handles default propagation for SISO modules
   gen_topo_propagate_metadata(topo_ptr,
                               module_ptr,
                               input_has_metadata_or_eos,
                               in_bytes_before_for_all_ch,
                               in_bytes_consumed_for_all_ch,
                               out_bytes_produced_for_all_ch);

   if (atleast_one_elementary_mod_is_present)
   {
      gen_topo_process_attached_elementary_modules(topo_ptr, module_ptr);
   }

#ifdef ERROR_CHECK_MODULE_PROCESS
   if (err_check)
   {
      // Dont need to call island exit here even when md lib is compiled in nlpi because we would have exited
      // island while copying md at container input or propagating md
      gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(topo_ptr);
      gen_topo_validate_metadata_eof(module_ptr);
   }
#endif

   /**
    * loop over input port once more to check need more, moving data back to beginning etc
    * Also handle metadata offset update (this has to be done after metadata prop)
    */
   is_first_in_port = TRUE;
   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      in_port_ptr                                         = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      ip_idx                                              = in_port_ptr->gu.cmn.index;
      sdata_ptr                                           = &in_port_ptr->common.sdata;
      topo_buf_t *                  bufs_ptr              = in_port_ptr->common.bufs_ptr;
      gen_topo_port_scratch_data_t *scr_data_ptr          = &pc->in_port_scratch_ptr[ip_idx];
      uint32_t                      data_consumed_per_buf = bufs_ptr[0].actual_data_len;
      process_info_ptr->anything_changed |= (data_consumed_per_buf > 0);

      uint32_t remaining_size_after_per_buf = scr_data_ptr->prev_actual_data_len[0] - data_consumed_per_buf;

      // For inplace modules, if input got consumed and there's output data remaining, it's an error
      if (gen_topo_is_inplace_or_disabled_siso(module_ptr) && remaining_size_after_per_buf &&
          out_bytes_produced_for_all_ch)
      {
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         " Either the inplace module 0x%lx didnt consume all data or inplace "
                         "module used with looping which is not supported ",
                         module_ptr->gu.module_instance_id);
         // this causes data corruption. moving remaining data to beginning causes overwriting output (for inplace)
      }

      gen_topo_move_data_to_beginning_after_process(topo_ptr,
                                                    in_port_ptr,
                                                    remaining_size_after_per_buf,
                                                    scr_data_ptr->prev_actual_data_len);

      // metadata logic common for both modules that supports_metadata and those don't
      bool_t has_eos_dfg = sdata_ptr->flags.marker_eos;
      if (sdata_ptr->metadata_list_ptr)
      {
         uint32_t bytes_consumed_for_md_prop =
            gen_topo_get_bytes_for_md_prop_from_len_per_buf(&in_port_ptr->common, data_consumed_per_buf);
         gen_topo_metadata_adj_offset_after_process_for_inp(topo_ptr, in_port_ptr, bytes_consumed_for_md_prop);

         // Dont need to call island exit here even when md lib is compiled in nlpi, because we would have exited
         // island while copying md in process context
         // check if metadata is present in the output buffer
         has_eos_dfg |= gen_topo_md_list_has_dfg(sdata_ptr->metadata_list_ptr);
      }

      gen_topo_sync_to_input_timestamp(topo_ptr, in_port_ptr, data_consumed_per_buf);

      // if flushing EOS leaves the input port, reset the input port. this helps in resetting timestamp related
      // variables after EOS prop. moves port to at-gap.
      if (pc->in_port_scratch_ptr[ip_idx].flags.prev_eos_dfg && !has_eos_dfg)
      {
         topo_basic_reset_input_port(topo_ptr, in_port_ptr, TRUE /*use_bufmgr*/);
         pc->in_port_scratch_ptr[ip_idx].flags.prev_eos_dfg = FALSE;
      }

      // In case module only clears EOF but leaves the input, then setting anything_changed helps
      //    in looping through the topo one more time to process the data (data triggers only).
      // E.g. upd_test_with_discont
      if (scr_data_ptr->flags.prev_eof && !(sdata_ptr->flags.end_of_frame))
      {
         process_info_ptr->anything_changed = TRUE;
         scr_data_ptr->flags.prev_eof       = FALSE;
      }

      // for sink modules if any data was consumed, think of it as full frame being processed.
      // this is needed incase of venc (where mailbox is the sink).
      // This frame delivery event is used in CDRX-40 usecases for dynamic de-voting of resources.
      if (module_ptr->gu.flags.is_sink && (data_consumed_per_buf > 0) && topo_ptr->flags.need_to_handle_frame_done)
      {
         if (topo_ptr->topo_to_cntr_vtable_ptr->handle_frame_done)
         {
            topo_ptr->topo_to_cntr_vtable_ptr->handle_frame_done(topo_ptr, module_ptr->gu.path_index);
         }
      }
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      out_port_ptr                               = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
      op_idx                                     = out_port_ptr->gu.cmn.index;
      gen_topo_port_scratch_data_t *scr_data_ptr = &pc->out_port_scratch_ptr[op_idx];

      // metadata logic common for both modules that supports_metadata and those don't
      // update offset of new metadata to be from the beginning of the buffer
      if (out_port_ptr->common.sdata.metadata_list_ptr)
      {
         uint32_t bytes_existing = scr_data_ptr->prev_actual_data_len[0];
         if (SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format) &&
             (TU_IS_ANY_DEINTERLEAVED_UNPACKED(out_port_ptr->common.media_fmt_ptr->pcm.interleaving)))
         {
            bytes_existing = scr_data_ptr->prev_actual_data_len[0] * out_port_ptr->common.sdata.bufs_num;
         }
         gen_topo_metadata_adj_offset_after_process_for_out(topo_ptr,
                                                            module_ptr,
                                                            out_port_ptr,
                                                            bytes_existing,
                                                            proc_result);

         process_info_ptr->anything_changed = TRUE;

         // merge back the metadata from scratch list to main list keeping in mind the order
         spf_list_node_t *temp_list_ptr               = (spf_list_node_t *)out_port_ptr->common.sdata.metadata_list_ptr;
         out_port_ptr->common.sdata.metadata_list_ptr = pc->out_port_scratch_ptr[op_idx].md_list_ptr;
         spf_list_merge_lists((spf_list_node_t **)&out_port_ptr->common.sdata.metadata_list_ptr, &temp_list_ptr);
         pc->out_port_scratch_ptr[op_idx].md_list_ptr = NULL;
      }
      else
      {
         out_port_ptr->common.sdata.metadata_list_ptr = pc->out_port_scratch_ptr[op_idx].md_list_ptr;
      }

      process_info_ptr->anything_changed |= out_port_ptr->common.sdata.flags.end_of_frame;
   }

   if (!(CAPI_SUCCEEDED(result) || (result == CAPI_ENEEDMORE)))
   {
      gen_topo_exit_island_temporarily(topo_ptr);
      // All error handling to be done outside island
      gen_topo_print_process_error(topo_ptr, module_ptr);
   }

   return capi_err_to_ar_result(result);
}

/**
 * Must not return without calling gen_topo_return_buf_mgr_buf_for_both if call to get buf was made.
 * Calling gen_topo_return_buf_mgr_buf_for_both ensures that buf is only a the last module output ptr.
 */
ar_result_t gen_topo_topo_process(gen_topo_t        *topo_ptr,
                                  gu_module_list_t **start_module_list_pptr,
                                  uint8_t           *path_index_ptr)
{
   ar_result_t              result           = AR_EOK;
   gen_topo_process_info_t *process_info_ptr = &topo_ptr->proc_context.process_info;

#ifdef VERBOSE_DEBUGGING
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "in gen_topo_topo_process, for path index %hu",
                   (path_index_ptr ? *path_index_ptr : 0));
#endif

   for (gu_module_list_t *module_list_ptr = *start_module_list_pptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      if (path_index_ptr && (module_ptr->gu.path_index != *path_index_ptr))
      {
         continue;
      }

      gen_topo_process_context_t *pc                            = &topo_ptr->proc_context;
      bool_t                      prev_output_produced_any_data = FALSE;
      bool_t out_has_no_trigger = FALSE; // by default assume output has trigger (works for sink modules)
      bool_t inp_has_no_trigger = FALSE;

      /* Call module processe only if it the module is active */
      if (!module_ptr->flags.active || !gen_topo_top_level_trigger_condition_satisfied(module_ptr))
      {
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX is not active or top level trigger cond not satisfied; skipping process",
                         module_ptr->gu.module_instance_id);
#endif
         continue;
      }

      // except for first module, handle change in media format based on the output of previous CAPI
      // also Copy output of previous CAPI to next CAPI input.

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         gen_topo_module_t *    prev_module_ptr = NULL;
         // note: even though data pending is in out buf, it's stored at input port index. different prev ports may have
         // same index.
         pc->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.data_pending_in_prev = FALSE;

         // do this only for internal ports.
         if (in_port_list_ptr->ip_port_ptr->conn_out_port_ptr)
         {
            gen_topo_output_port_t *prev_out_ptr =
               (gen_topo_output_port_t *)in_port_list_ptr->ip_port_ptr->conn_out_port_ptr;
            prev_module_ptr = (gen_topo_module_t *)prev_out_ptr->gu.cmn.module_ptr;

            // check if prev module produced any output
            prev_output_produced_any_data = prev_out_ptr->any_data_produced;

            // copy output of first CAPI to input of second except for first one.
            // first one has input already copied earlier
            // for last buffer output buffer is the dec svc output buffer. already assigned.

            // Start state is checked inside
            (void)gen_topo_check_copy_between_modules(topo_ptr,
                                                      module_list_ptr,
                                                      prev_module_ptr,
                                                      in_port_ptr,
                                                      prev_out_ptr);
         }

         if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            inp_has_no_trigger = TRUE;
         }

         /* When EOF is set there is no need to sync to the buffered-timestamp (ts_to_sync).
          * Will sync to the new buffer's timestamp once this EOF is consumed.
          *
          * If bytes_from_prev_buf were update due to insufficient data
          * and port was waiting for a new buffer but instead end_of_frame is marked or
          * threshold is disabled by SYNC (MFC at input can consume partial data) then
          * need to reset bytes_from_prev_buf otherwise we will sync to wrong timestamp
          * after process.
          * test for eof: sync_with_mux_demux_2_port_record_5
          *
          * If bytes_from_prev_buf is same as actual-data-len then it means no new data
          * is copied so no need to sync to buffered-timestamp (ts_to_sync) after process.
          * test for MFC consuming partial data during threshold disabled: sync_with_mux_demux_2_port_record_6
          *
          * This check is outside gen_topo_check_copy_between_modules because these conditions can be true for external
          * input port as well.
          *
          * All these bytes_from_prev_buf related issues are coming because timestamp and data is not synchronously
          * copied in gen_topo_check_copy_between_modules
          */
         if (in_port_ptr->common.sdata.flags.end_of_frame ||
             (in_port_ptr->bytes_from_prev_buf == in_port_ptr->common.bufs_ptr[0].actual_data_len))
         {
            // bytes_from_prev_buf will be update again in gen_topo_input_port_is_trigger_present
            in_port_ptr->bytes_from_prev_buf = 0;
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         out_port_ptr->any_data_produced = FALSE;

         bool_t out_has_trigger = gen_topo_out_port_has_trigger(topo_ptr, out_port_ptr);
#ifdef TRIGGER_DEBUG
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX, out port id 0x%lx, out_has_trigger%u ",
                         module_ptr->gu.module_instance_id,
                         out_port_ptr->gu.cmn.id,
                         out_has_trigger);
#endif
         if (out_has_trigger)
         {
            // in HW-EP overrun cases, ext buf is NULL and topo-buf will be obtained. This is one case
            // where peer-svc buf is not used for last module.
            result = gen_topo_check_get_out_buf_from_buf_mgr(topo_ptr, module_ptr, out_port_ptr);
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_ERROR_PRIO,
                               " Module 0x%lX: Output port id 0x%lx, error getting buffer",
                               module_ptr->gu.module_instance_id,
                               out_port_ptr->gu.cmn.id);
               // continue with other outputs
            }
         }
         else
         {
            out_has_no_trigger = TRUE;

            // If out_has_trigger is false, port could be blocked and the capi is not expecting
            // buffer on the blocked port, on data triggers. So fwk needs to force return the held buffer.
            //
            // note: Usually the ports will not hold a empty buffer. But if its a low latency buffer,
            // it can be held at the port for optimization (refer buffer manager). Marking force return = true,
            // will return the low latency buffer if its empty.
            //
            // testcase: voice_call_HO_1. At handover, trm module blocks output port during resync and
            // module expects buffer only on the inputs on data trigger. If output port has a buffer,
            // module returns/prints an error.
            out_port_ptr->common.flags.force_return_buf = TRUE;
            gen_topo_output_port_return_buf_mgr_buf(topo_ptr, out_port_ptr);
         }
      }

      bool_t any_ext_trigger_not_satisfied = FALSE;

      // if module is not at the nblc boundary and previous output produced data, then
      // we can always call module process. With stale output we shouldn't call module. hence check out_has_trigger.
      // similar if MF is propagated b/w modules, then input buf may not be there. test: virt_tu_7
      bool_t trigger_cond_satisfied = FALSE;
      if (!module_ptr->flags.is_nblc_boundary_module && prev_output_produced_any_data && !out_has_no_trigger &&
          !inp_has_no_trigger)
      {
         trigger_cond_satisfied = TRUE;
#ifdef TRIGGER_DEBUG
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: NBLC module trigger condition is satisfied",
                         module_ptr->gu.module_instance_id);
#endif
      }
      else
      {
         trigger_cond_satisfied =
            gen_topo_is_module_trigger_condition_satisfied(module_ptr, &any_ext_trigger_not_satisfied);
      }

      // Call the module if tigger conditions are met.
      if (trigger_cond_satisfied)
      {
         // if the module supports DM extension set samples required to be produced depending upon the mode.
         // In future if fractional rate is supported for mfc in island we need to move dm extension code into
         // Island, for now gen_topo_is_dm_enabled check avoids crash for mfc in island for non fractional sampling
         // rate.
         if (!POSAL_IS_ISLAND_HEAP_ID(topo_ptr->heap_id) && (module_ptr->flags.need_dm_extn)
               && (gen_topo_is_dm_enabled(module_ptr)))
         {
            gen_topo_updated_expected_samples_for_dm_modules(topo_ptr, module_ptr);
         }

         /** module process
          * return code: fail, success, need-more */
         bool_t terminate = FALSE;
         for (uint32_t loop = 0; loop < module_ptr->num_proc_loops; loop++)
         {
            bool_t is_final_loop = (loop == (module_ptr->num_proc_loops - 1));
            result |= gen_topo_module_process(topo_ptr, module_ptr, &terminate, is_final_loop);
            if (terminate)
            {
               break;
            }
         }
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t * in_port_ptr  = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         gen_topo_output_port_t *prev_out_ptr = (gen_topo_output_port_t *)in_port_ptr->gu.conn_out_port_ptr;

         // if module raises need-more and buf has some old data, then timestamp cannot be replaced
         // immediately. need to wait till the data is drained (for decode).
         // Since bytes_from_prev_buf is referred to even after process (gen_topo_sync_to_input_timestamp), we cannot
         // unconditionally assign it here.
         // tests:
         // >> aac_dec_nt_38 (has bytes_from_prev_buf still nonzero after 2nd need-more),
         // >> ctm_rx_tx_48k_tty_full (where module may not get called but data may be copied)
         // >> in wmapro_dec_nt_1, even though bytes_from_prev_buf exists capi marks buf as fully consumed.
         // >> wmastd_dec_nt_9 - EOF + TS testing
         // >> consider only need-more from process call, not from prev process call or first time (gen_cntr_spr_7)
         // >> rec_spl_cntr_timestamp_discontinuity_1 - EOF due to discont. module says need more and we never sync
         // to input
         //    TS (logic in gen_topo_check_copy_incoming_ts)
         // >> pb_pause_13_dfg - stale data gets stored as bytes_from_prev_buf if we dont check for
         //    trigger_cond_satisfied
         // >> split_a2dp_1 - bytes need to be updated to make sure it doesnt sync to new ts with old data
         // >> rve_rx_tx_noise - 4096 input, 10 ms threshold module (ts_synced added due to this)

         if (trigger_cond_satisfied) // trigger_cond_satisfied means that the "need_more_input" is updated to the
                                     // latest.
         {
            if ((in_port_ptr->flags.need_more_input) && (in_port_ptr->common.bufs_ptr))
            {
               /*In some scenarios for RD EP, after EOS moves to output list, input buffers might be freed due to input
                *port being at-gap. In such cases, even if there is pending data at the output of the previous module,
                *we should not copy */
               /* We should not copy the data for the modules where inplace buffers are assigned for input and output
                * port. It is possible that there are some valid data in context of output port and copying more data on
                * input port will overwrite it.
                * */
               if (prev_out_ptr && (pc->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.data_pending_in_prev) &&
                   (in_port_ptr->common.bufs_ptr[0].data_ptr) && !gen_topo_is_inplace_or_disabled_siso(module_ptr))
               {
                  // Try to copy data one more time so that total number of buffers needed from buf mgr is reduced.
                  // we dont call process on a module until output is empty. So any pending data in prev_out_ptr causes
                  // more loops and more buffers.

                  (void)gen_topo_copy_data_from_prev_to_next(topo_ptr,
                                                             module_ptr,
                                                             in_port_ptr,
                                                             prev_out_ptr,
                                                             TRUE /*after */);

                  // reset since data is copied
                  // for requires_data_buffering modules, copying any data should clear the need_more_input flag.
                  in_port_ptr->flags.need_more_input = FALSE;

                  // for non_requires_data_buffering modules, need to evaluate if more data is needed or not.
                  // update need_more_input and bytes_from_prev_buf if need_more requirement is still valid after
                  // copying the data
                  gen_topo_input_port_is_trigger_present(topo_ptr, in_port_ptr, NULL);
               }
               else
               {
                  // if no data is copied from the previous output and need_more is set then data available is not
                  // sufficient, in this case mark the current data as data from prev_buf, which will be used to adjust
                  // timestamp when new data is copied
                  in_port_ptr->bytes_from_prev_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;
               }

#ifdef VERBOSE_DEBUGGING
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_LOW_PRIO,
                               " Module 0x%lX: input port id 0x%lx, bytes_from_prev_buf %lu",
                               module_ptr->gu.module_instance_id,
                               in_port_ptr->gu.cmn.id,
                               in_port_ptr->bytes_from_prev_buf);
#endif
            }

            if (prev_out_ptr)
            {
               // clear erasure for prev port if module process was successful
               prev_out_ptr->common.sdata.flags.erasure = FALSE;
            }
         }
         else if ((GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger) && (in_port_ptr->common.bufs_ptr) &&
                  (in_port_ptr->common.bufs_ptr[0].actual_data_len) &&
                  gen_topo_is_port_in_realtime_path(&in_port_ptr->common) &&
                  (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_in_port_in_dm_variable_nblc(topo_ptr, in_port_ptr)))
         {
            // Drop partial data in signal trigerred container only if the data is not sin DM module's variable nblc
            // path

            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            " Module 0x%lX: input port id 0x%lx, dropping %lu for signal trigger as trigger condition "
                            "not satisfied.",
                            module_ptr->gu.module_instance_id,
                            in_port_ptr->gu.cmn.id,
                            in_port_ptr->common.bufs_ptr[0].actual_data_len);

            if (in_port_ptr->common.sdata.metadata_list_ptr)
            {
               // Dont need to exit island even if md lib is compiled in non island, since we will already be in nlpi
               // due to exiting island when copying md from prev to next
               gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                       module_ptr,
                                                       &in_port_ptr->common,
                                                       gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common),
                                                       FALSE /*keep_eos_and_ba_md*/);
            }

            gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
         }

         if (prev_out_ptr)
         {
            // release only output, input will be release after data drop conditions are checked.
            gen_topo_output_port_return_buf_mgr_buf(topo_ptr, prev_out_ptr);
         }

         gen_topo_input_port_return_buf_mgr_buf(topo_ptr, in_port_ptr);
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr     = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         gen_topo_input_port_t * conn_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;

         /* If this output port is blocking the state propagation (#INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP)
          * then there can be a scenario where this output port is started (due to sg start) but connected_in_port
          * is stopped/suspended (due to propagated state). In this case, we need to drop the data here at the output
          * port. We can not drop the data on the connected input port because connected module processing will be
          * skipped.
          */
         if ((TOPO_PORT_STATE_STARTED == out_port_ptr->common.state) &&
             (out_port_ptr->common.flags.is_upstream_realtime) && (conn_in_port_ptr) &&
             (TOPO_PORT_STATE_STARTED != conn_in_port_ptr->common.state))
         {
            gen_topo_exit_island_temporarily(topo_ptr);
            // move all err handling out of LPI
            gen_topo_drop_data_after_mod_process(topo_ptr, module_ptr, out_port_ptr);
         }

         /** check and release bufs, this will help release earlier (when next-in has its own buf
          * and enable this buf to be re-used as out buf for next module. */
         gen_topo_output_port_return_buf_mgr_buf(topo_ptr, out_port_ptr);
      }

      result = AR_EOK; // overwrite result

      /**
       * check after calling gen_topo_check_copy_between_modules, which propagates media fmt
       */
      if (gen_topo_any_process_call_events(topo_ptr))
      {
#ifdef VERBOSE_DEBUGGING
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_LOW_PRIO,
                         " Module 0x%lX: topo has pending media fmt or port thresh or process state before processing "
                         "this module.",
                         module_ptr->gu.module_instance_id);
#endif
         /** if we continue processing then thresh may not be sufficient.
          * Even if thresh is sufficient, if thresh decreases later, then data would be dropped.
          * it's enough to do this before process. after-process cases also handled thru this only.*/
         process_info_ptr->anything_changed = TRUE;
         /**
          * if media fmt or thresh event occurs when process is called (decoder)
          * then after copying this data to next module (assuming next module in sorted list is module connected to
          * dec) handle all events in the fmwk, and come back here.
          *
          * If we don't continue from the module next to decoder, and instead start from the beginning,
          * then we will have more buffers stuck inside the topo (2 decoded frames).
          */
         *start_module_list_pptr = module_list_ptr;
         break; // breaks topo process
      }

      /**
       * when probing for activity of data trigger policy modules, if there's no activity
       * then break as soon as all trigger policy modules are done.
       * This should be checked after above event handling.
       */
      if (process_info_ptr->probing_for_tpm_activity)
      {
         if (gen_topo_is_module_data_trigger_policy_active(module_ptr))
         {
            process_info_ptr->num_data_tpm_done++;
         }

         if (!process_info_ptr->anything_changed)
         {
            if (process_info_ptr->num_data_tpm_done >= topo_ptr->num_data_tpm)
            {
#ifdef VERBOSE_DEBUGGING
               TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                               DBG_LOW_PRIO,
                               " Module 0x%lX: breaking topo process loop as nothing changed. num_data_tpm_done %lu >= "
                               "num_data_tpm %lu ",
                               module_ptr->gu.module_instance_id,
                               process_info_ptr->num_data_tpm_done,
                               topo_ptr->num_data_tpm);
#endif
               break; // breaks topo process
            }
         }
      }
      *start_module_list_pptr = NULL;
   }

   return result;
}

/**
 * Returns true if the max data length of this output port's buffer is known. This is true if the media format
 * has been set or if a threshold was raised on this port.
 */
bool_t gen_topo_output_port_is_size_known(void *ctx_topo_ptr, void *ctx_out_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ctx_out_port_ptr;

   return (out_port_ptr->common.flags.is_mf_valid || out_port_ptr->common.flags.port_has_threshold);
}

#ifdef ERROR_CHECK_MODULE_PROCESS
ar_result_t gen_topo_validate_port_sdata(uint32_t                log_id,
                                         gen_topo_common_port_t *cmn_port_ptr,
                                         bool_t                  is_input,
                                         uint32_t                port_index,
                                         gen_topo_module_t*      module_ptr,
                                         bool_t                  PROCESS_DONE)
{
   // validation not needed for non PCM/ non unpacked formats.
   if (!gen_topo_is_pcm_any_unpacked(cmn_port_ptr))
   {
      return AR_EOK;
   }

   bool_t is_unpacked_v2 = (GEN_TOPO_MF_PCM_UNPACKED_V2 == cmn_port_ptr->flags.is_pcm_unpacked);
   bool_t is_bypassed    = module_ptr->bypass_ptr || !module_ptr->capi_ptr;
   for (uint32_t b = 1; b < cmn_port_ptr->sdata.bufs_num; b++)
   {
      // if the buffer is assigned to the first channel, but not to rest of the channels then its a failure
      if (cmn_port_ptr->sdata.buf_ptr[0].data_ptr && !cmn_port_ptr->sdata.buf_ptr[b].data_ptr)
      {
         TOPO_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "Error! NULL buf ptr for ch[%lu]. PROCESS_DONE=%lu is_input=%lu port "
                  "idx=%ld, miid=0x%lx is_unpacked_v2:%lu max=%lu",
                  b,
                  PROCESS_DONE,
                  is_input,
                  port_index,
                  module_ptr->gu.module_instance_id,
                  is_unpacked_v2,
                  cmn_port_ptr->sdata.buf_ptr[b].max_data_len);
         return AR_EFAILED;
      }

      // For unpacked V2, only first ch must have valid lens
      if (is_unpacked_v2 && (cmn_port_ptr->sdata.buf_ptr[b].actual_data_len || cmn_port_ptr->sdata.buf_ptr[b].max_data_len))
      {
         TOPO_MSG(log_id,
                  DBG_HIGH_PRIO,
                  "Warning! Unexpected buf lengths for UNPACKED V2, fixing it! PROCESS_DONE[%lu]: is_input:%lu port "
                  "idx = %ld, miid = 0x%lx ch[%lu] actual:%lu max:%lu",
                  PROCESS_DONE,
                  is_input,
                  port_index,
                  module_ptr->gu.module_instance_id,
                  b,
                  cmn_port_ptr->sdata.buf_ptr[b].actual_data_len,
                  cmn_port_ptr->sdata.buf_ptr[b].max_data_len);
#ifdef SAFE_MODE
         /** Ideally modules are not expected to read/write lengths only from first buffer, resetting to zero since its
          * safe mode.*/
         cmn_port_ptr->sdata.buf_ptr[b].actual_data_len = 0;
         cmn_port_ptr->sdata.buf_ptr[b].max_data_len    = 0;
#endif
      }
      // For unpacked V1, all chs must have same lens as the first ch
      // bypassed modules need to update only first ch
      else if (!is_unpacked_v2 && !is_bypassed &&
               ((cmn_port_ptr->sdata.buf_ptr[0].actual_data_len != cmn_port_ptr->sdata.buf_ptr[b].actual_data_len) ||
                (cmn_port_ptr->sdata.buf_ptr[0].max_data_len != cmn_port_ptr->sdata.buf_ptr[b].max_data_len)))
      {
         /** if buf lengths do not match with the first ch buffer it could be some issue with fwk or module. If its
            before process it means fwk didnt not setup sdata properly. If its after process, then module must have not
            update correctly.*/

         AR_MSG_ISLAND(DBG_ERROR_PRIO,
                       "Error! Unexpected buf lengths for UNPACKED V1, Bailing out! PROCESS_DONE[%lu]: is_input:%lu "
                       "port idx = %ld, miid = 0x%lx ch[%lu] actual len (%lu != %lu) max len (%lu != %lu)",
                       PROCESS_DONE,
                       is_input,
                       port_index,
                       module_ptr->gu.module_instance_id,
                       b,
                       cmn_port_ptr->sdata.buf_ptr[b].actual_data_len,
                       cmn_port_ptr->sdata.buf_ptr[0].actual_data_len,
                       cmn_port_ptr->sdata.buf_ptr[b].max_data_len,
                       cmn_port_ptr->sdata.buf_ptr[0].max_data_len);
         return AR_EFAILED;
      }
   }
   return AR_EOK;
}
#endif