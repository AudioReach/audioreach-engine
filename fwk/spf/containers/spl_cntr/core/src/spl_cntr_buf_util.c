/**
 * \file spl_cntr_buf_util.c
 * \brief
 *     This file contains spl_cntr utility functions for managing external port buffers (input and output).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"
#include "capi_fwk_extns_sync.h"

/* =======================================================================
Static Function Declarations.
========================================================================== */
static uint32_t spl_cntr_calc_ext_out_buf_deliver_size(
   spl_cntr_t              *me_ptr,
   spl_cntr_ext_out_port_t *ext_out_port_ptr); // use calc everywhere
static ar_result_t spl_cntr_pack_external_output(spl_cntr_t              *me_ptr,
                                                 spl_cntr_ext_out_port_t *ext_out_port_ptr,
                                                 spf_msg_data_buffer_t   *out_buf_ptr);
static bool_t      spl_cntr_check_if_new_threshold_is_multiple(spl_cntr_t *me_ptr,
                                                               uint32_t    agg_thresh_samples_per_channel,
                                                               uint32_t    agg_thresh_sample_rate,
                                                               uint32_t    new_thresh_samples_per_channel,
                                                               uint32_t    new_thresh_sample_rate,
                                                               uint32_t   *multiple_ptr);
static bool_t spl_cntr_threshold_a_greater_than_b(spl_cntr_t *me_ptr,
                                                  uint64_t    a_samples,
                                                  uint64_t    a_sr,
                                                  uint64_t    b_samples,
                                                  uint64_t    b_sr);

static bool_t spl_cntr_ext_in_port_check_timestamp_discontinuity(spl_cntr_t             *me_ptr,
                                                                 spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                 spf_msg_data_buffer_t  *data_msg_buf_ptr);

/* =======================================================================
Static Function Definitions
========================================================================== */

/**
 * Checks for timestamp discontinuity between most recent timestamp in the external input port list vs the
 * timestamp in the data msg.
 */
static bool_t spl_cntr_ext_in_port_check_timestamp_discontinuity(spl_cntr_t             *me_ptr,
                                                                 spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                 spf_msg_data_buffer_t  *data_msg_buf_ptr)
{
   bool_t                     TS_IS_VALID_TRUE   = TRUE;
   spl_topo_timestamp_info_t *timestamp_ptr      = NULL;
   int64_t                    expected_ts        = 0;
   uint64_t                  *FRAC_TIME_PTR_NULL = NULL;
   int64_t                    input_ts           = data_msg_buf_ptr->timestamp;

   // If there are no timestamps at the local buffer, there is no timestamp discontinuity.
   if (!ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Not checking for external input idx = %ld miid = 0x%lx ts disc due to no buffered timestamps",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
      return FALSE;
   }

   // Extrapolate from the most recent timestamp.
   timestamp_ptr = ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid
                      ? &(ext_in_port_ptr->topo_buf.newest_timestamp_info)
                      : &(ext_in_port_ptr->topo_buf.buf_timestamp_info);

   // Extrapolate the timestamp. ts = ts + (distance between ts offset and end of local buffer)
   expected_ts =
      timestamp_ptr->timestamp + (topo_bytes_per_ch_to_us(ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len -
                                                             timestamp_ptr->offset_bytes_per_ch,
                                                          &(ext_in_port_ptr->cu.media_fmt),
                                                          FRAC_TIME_PTR_NULL));

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "ext in port idx = %ld miid = 0x%lx checking for ts disc input ts %ld, local buf ts %ld, local buff ts "
                "ts_offset_bytes_per_ch %ld, expected ts %ld ",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                input_ts,
                timestamp_ptr->timestamp,
                timestamp_ptr->offset_bytes_per_ch,
                expected_ts);
#endif

   bool_t ts_disc = gen_topo_is_timestamp_discontinuous1(TS_IS_VALID_TRUE, expected_ts, TS_IS_VALID_TRUE, input_ts);

   if (ts_disc)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Detected timestamp discontinuity on ext input port idx %ld miid 0x%lx. Expected ts %ld, incoming "
                   "ts "
                   "%ld. Cant buffer data, must "
                   "process old data first.",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   expected_ts,
                   input_ts);
   }
   return ts_disc;
}

/**
 * Output buffers are setup unpacked (allows for topo_process to write to
 * output buffers that already contain some data in them), but they must be
 * packed before sending downstream.
 */
static ar_result_t spl_cntr_pack_external_output(spl_cntr_t              *me_ptr,
                                                 spl_cntr_ext_out_port_t *ext_out_port_ptr,
                                                 spf_msg_data_buffer_t   *out_buf_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    buf_offset_1 = ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   uint32_t    buf_offset_2 = ext_out_port_ptr->topo_buf.buf_ptr[0].max_data_len;
   uint32_t    buf_available_size;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "In spl_cntr_pack_external_output");
#endif

   uint32_t actual_data_len_per_buf = ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   uint32_t max_data_len_per_buf    = ext_out_port_ptr->topo_buf.buf_ptr[0].max_data_len;
   for (uint32_t i = 1; i < ext_out_port_ptr->cu.media_fmt.pcm.num_channels; i++)
   {
      /* checking for any holes in the buffer. If yes,  */
      if (buf_offset_1 != buf_offset_2)
      {
         buf_available_size = (max_data_len_per_buf * ext_out_port_ptr->cu.media_fmt.pcm.num_channels) - buf_offset_1;
         memsmove(((int8_t *)(ext_out_port_ptr->topo_buf.buf_ptr[0].data_ptr) + buf_offset_1),
                  buf_available_size,
                  ext_out_port_ptr->topo_buf.buf_ptr[i].data_ptr,
                  actual_data_len_per_buf);
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "spl_cntr_pack_external_output: after memscpy");
#endif
      }
      buf_offset_1 += actual_data_len_per_buf;
      buf_offset_2 += max_data_len_per_buf;
   }

   out_buf_ptr->actual_size       = buf_offset_1;
   out_buf_ptr->max_size          = buf_offset_2;
   out_buf_ptr->metadata_list_ptr = NULL;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "spl_cntr_pack_external_output: actual size: %d, max size: %d",
                out_buf_ptr->actual_size,
                out_buf_ptr->max_size);
#endif

   return result;
}

/**
 * outbuf_size should be initialized
 */
static inline ar_result_t spl_cntr_get_required_out_buf_size_and_count(spl_cntr_t              *me_ptr,
                                                                       spl_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                       uint32_t                *num_out_buf_ptr,
                                                                       uint32_t                *req_out_buf_size,
                                                                       uint32_t                 specified_out_buf_size,
                                                                       uint32_t                 metadata_size)
{
   ar_result_t result = cu_determine_ext_out_buffering(&me_ptr->cu, &ext_out_port_ptr->gu);

   if (APM_SUB_GRAPH_DIRECTION_RX == me_ptr->topo.t_base.gu.sg_list_ptr->sg_ptr->direction &&
       cu_has_voice_sid(&me_ptr->cu))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "ext out port idx = %ld miid = 0x%lx num_reg_bufs was %ld, is now 2",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->cu.icb_info.icb.num_reg_bufs);
      ext_out_port_ptr->cu.icb_info.icb.num_reg_bufs = 2;
   }

   *num_out_buf_ptr =
      ext_out_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_out_port_ptr->cu.icb_info.icb.num_reg_prebufs;

   return result;
}

/**
 * Zeros out fields of the external output port associated with a buffer that was just
 * dropped or delivered. Drops all metadata on the output port.
 */
ar_result_t spl_cntr_clear_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result           = AR_EOK;
   spl_topo_output_port_t *int_out_port_ptr = NULL;

   VERIFY(result, ext_out_port_ptr);

   int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (ext_out_port_ptr->topo_buf.buf_ptr)
   {
      // Only the first buf_ptr is allocated, so don't free it. But still set the
      // actual data len to 0.
      for (uint32_t i = 0; i < ext_out_port_ptr->topo_buf.num_bufs; i++)
      {
         ext_out_port_ptr->topo_buf.buf_ptr[i].data_ptr = NULL;

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
         ext_out_port_ptr->topo_buf.buf_ptr[i].actual_data_len = 0;
         ext_out_port_ptr->topo_buf.buf_ptr[i].max_data_len    = 0;
      }
#else
      }
      // optimization: update only first buffer lengths outside the for loop
      ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len = 0;
      ext_out_port_ptr->topo_buf.buf_ptr[0].max_data_len    = 0;
#endif

      ext_out_port_ptr->topo_buf.timestamp             = 0;
      ext_out_port_ptr->topo_buf.timestamp_is_valid    = FALSE;
      ext_out_port_ptr->topo_buf.bytes_consumed_per_ch = 0;
      ext_out_port_ptr->topo_buf.end_of_frame          = FALSE;

      // DO NOT CLEAR below fields: These need to live beyond lifetime of external output buffer. They
      // are relevant for the ts_disc temp buffer which is stored in the external port's internal buffer.
      // ext_out_port_ptr->topo_buf.timestamp;
      // ext_out_port_ptr->topo_buf.timestamp_is_valid;
      // ext_out_port_ptr->topo_buf.timestamp_discontinuity;
   }

   if (int_out_port_ptr->md_list_ptr)
   {
      bool_t IS_DROPPED_TRUE = TRUE;
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Destroying all metadata for output port idx %ld miid 0x%lx",
                   int_out_port_ptr->t_base.gu.cmn.index,
                   int_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

      TRY(result,
          gen_topo_destroy_all_metadata(me_ptr->topo.t_base.gu.log_id,
                                        (void *)int_out_port_ptr->t_base.gu.cmn.module_ptr,
                                        &(int_out_port_ptr->md_list_ptr),
                                        IS_DROPPED_TRUE));

      // At this point, md_list is NULL so it cannot contain any flushing eos. Set marker to FALSE.
      int_out_port_ptr->t_base.common.sdata.flags.marker_eos = FALSE;
   }

   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Get the required size of external port input buffer. This size refers to only
 * the size of the actual data buffer.
 */
uint32_t spl_cntr_calc_required_ext_in_buf_size(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   topo_pcm_pack_med_fmt_t *media_format_ptr = &ext_in_port_ptr->cu.media_fmt.pcm;
   uint64_t                 num_samples_nom =
      topo_us_to_samples(me_ptr->topo.cntr_frame_len.frame_len_us, media_format_ptr->sample_rate);

   if ((0 != ext_in_port_ptr->max_process_samples))
   {
      if (ext_in_port_ptr->max_process_samples < (uint32_t)num_samples_nom)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_ERROR_PRIO,
                      "DM Max samples %lu set, but smaller than nominal %lu, using nom",
                      ext_in_port_ptr->max_process_samples,
                      (uint32_t)num_samples_nom);
#endif
      }
      else
      {
         num_samples_nom = ext_in_port_ptr->max_process_samples;
      }
   }
   return num_samples_nom * (media_format_ptr->bits_per_sample >> 3) * media_format_ptr->num_channels;
}

/**
 * Get the required size of external port output buffers. This size refers to only
 * the size of the actual data buffer.
 */
uint32_t spl_cntr_calc_required_ext_out_buf_size(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   uint32_t buffer_size        = 0;
   bool_t   FOR_DELIVERY_FALSE = FALSE;
   buffer_size                 = spl_topo_calc_buf_size(&(me_ptr->topo),
                                        me_ptr->topo.cntr_frame_len,
                                        &(ext_out_port_ptr->cu.media_fmt),
                                        FOR_DELIVERY_FALSE);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Buffer size for ext out port idx = %ld miid = 0x%lx is %ld. output sr = %ld, output "
                "bits_per_sample = %ld, output num ch = %ld, nominal frame duration us %ld",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                buffer_size,
                ext_out_port_ptr->cu.media_fmt.pcm.sample_rate,
                ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample,
                ext_out_port_ptr->cu.media_fmt.pcm.num_channels,
                me_ptr->topo.cntr_frame_len.frame_len_us);
#endif

   return buffer_size;
}

/**
 * Get the amount that an external output should be filled before we deliver it downstream.
 */
static uint32_t spl_cntr_calc_ext_out_buf_deliver_size(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   uint32_t deliver_size = 0;

   bool_t FOR_DELIVERY_TRUE = TRUE;

   deliver_size = spl_topo_calc_buf_size(&(me_ptr->topo),
                                         me_ptr->topo.cntr_frame_len,
                                         &(ext_out_port_ptr->cu.media_fmt),
                                         FOR_DELIVERY_TRUE);

   // if output is variable then we should allow delivery of buffer if it has less than the container frame len data.
   // this is to handle the use case when dm module produce less data.
   if (ext_out_port_ptr->cu.icb_info.flags.variable_output)
   {
      uint32_t one_ms_in_bytes     = 0;
      uint32_t one_sample_in_bytes = 0;

      one_ms_in_bytes     = topo_us_to_bytes(1000, &ext_out_port_ptr->cu.media_fmt);
      one_sample_in_bytes = topo_samples_to_bytes(1, &ext_out_port_ptr->cu.media_fmt);

      if (deliver_size > one_ms_in_bytes)
      {
         deliver_size -= one_ms_in_bytes;
      }
      else if (deliver_size > one_sample_in_bytes)
      {
         deliver_size -= one_sample_in_bytes;
      }
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Delivery size for ext out port idx = %ld miid = 0x%lx is %ld. output sr = %ld, output "
                "bits_per_sample = %ld, output num ch = %ld, nominal frame duration us %ld",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                deliver_size,
                ext_out_port_ptr->cu.media_fmt.pcm.sample_rate,
                ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample,
                ext_out_port_ptr->cu.media_fmt.pcm.num_channels,
                me_ptr->topo.cntr_frame_len.frame_len_us);
#endif

   return deliver_size;
}

// check if a sample rate is fractional for a frame size or not.
static bool_t spl_cntr_is_fractional_rate(uint64_t frame_us, uint32_t sample_rate)
{
   return (((frame_us * (uint64_t)sample_rate) % (uint64_t)NUM_US_PER_SEC) > 0);
}

// check all the external and source module ports to find the fractional sampling rate.
static uint32_t spl_cntr_get_fractional_sampling_rate_for_cntr_frame_len(spl_cntr_t *me_ptr)
{
   uint32_t configured_frame_size_us       = me_ptr->threshold_data.configured_frame_size_us;
   bool_t   pick_first_valid_sampling_rate = (0 == configured_frame_size_us) ? TRUE : FALSE;

   // Check if input sample rate is fractional
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr =
         (gen_topo_input_port_t *)ext_in_port_list_ptr->ext_in_port_ptr->int_in_port_ptr;

      // skip the ports which are in variable data path because extra buffer will anyway be created (prebuffer) in ICB.
      /* For example: SC (with configured frame size 10ms) converting 11025Hz to 48KHz.
       * MFC will be in fixed output.
       * If we pick 11025Hz rate to decide the container frame len then it will be 9977us and in this case external
       * output port buffer size will be 479 samples which will cause jitter for downstream. In this case we should
       * ignore 11025 and use 48K instead for container frame len.
       *
       * If the conversion is from 48K to 11025Hz. Then fractional rate will be on fixed data path and we should pick
       * 11025Hz.
       */
      if (in_port_ptr->common.flags.is_mf_valid)
      {
         if (pick_first_valid_sampling_rate ||
             (!ext_in_port_ptr->cu.icb_info.flags.variable_input &&
              spl_cntr_is_fractional_rate(configured_frame_size_us,
                                          in_port_ptr->common.media_fmt_ptr->pcm.sample_rate)))
         {
            return in_port_ptr->common.media_fmt_ptr->pcm.sample_rate;
         }
      }
   }

   // Source module output ports also count as external input ports. We should also check the sampling rate
   // of all output ports of source modules.
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gu_module_t *module_ptr = (gu_module_t *)module_list_ptr->module_ptr;
         if (TOPO_MODULE_TYPE_SOURCE == spl_topo_get_module_port_type((spl_topo_module_t *)module_ptr))
         {
            for (gu_output_port_list_t *out_port_list_ptr = module_ptr->output_port_list_ptr; out_port_list_ptr;
                 LIST_ADVANCE(out_port_list_ptr))
            {
               gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

               if (out_port_ptr->common.flags.is_mf_valid &&
                   (pick_first_valid_sampling_rate ||
                    spl_cntr_is_fractional_rate(configured_frame_size_us,
                                                out_port_ptr->common.media_fmt_ptr->pcm.sample_rate)))
               {
                  return out_port_ptr->common.media_fmt_ptr->pcm.sample_rate;
               }
            }
         }
      }
   }

   // Check if output sample rate is fractional
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t  *out_port_ptr =
         (gen_topo_output_port_t *)ext_out_port_list_ptr->ext_out_port_ptr->int_out_port_ptr;

      // skip the ports which are in variable data path because extra buffer will anyway be created (prebuffer) in ICB.
      if (out_port_ptr->common.flags.is_mf_valid)
      {
         if (pick_first_valid_sampling_rate ||
             (!ext_out_port_ptr->cu.icb_info.flags.variable_output &&
              spl_cntr_is_fractional_rate(configured_frame_size_us,
                                          out_port_ptr->common.media_fmt_ptr->pcm.sample_rate)))
         {
            return out_port_ptr->common.media_fmt_ptr->pcm.sample_rate;
         }
      }
   }

   return 0;
}

/**
 * Reallocates the external input port local buffer according to the required size.
 */
ar_result_t spl_cntr_check_resize_ext_in_buffer(spl_cntr_t             *me_ptr,
                                                spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                uint32_t                required_size_bytes)
{
   INIT_EXCEPTION_HANDLING
   uint32_t result               = AR_EOK;
   uint32_t buf_size_per_channel = 0;
   bool_t   FOR_DELIVERY_TRUE    = TRUE;
   uint32_t nominal_bytes        = 0;
   bool_t   need_to_recreate     = FALSE;

   // Only create if size is different.
   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      need_to_recreate = TRUE;
   }
   // Need to resize if total buffer size is different, or num channels changed.
   else if ((required_size_bytes !=
             (ext_in_port_ptr->topo_buf.buf_ptr[0].max_data_len * ext_in_port_ptr->topo_buf.num_bufs)) ||
            (ext_in_port_ptr->topo_buf.num_bufs != ext_in_port_ptr->cu.media_fmt.pcm.num_channels))
   {
      need_to_recreate = TRUE;
   }
   else
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Not recreating external input buffer because the size did not change, size = %ld.",
                   required_size_bytes);
#endif
   }

   if (need_to_recreate)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "spl_cntr_resize_ext_in_buffer input idx %ld miid 0x%lx: current num_bufs: %d, num_channels: %d",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->topo_buf.num_bufs,
                   ext_in_port_ptr->cu.media_fmt.pcm.num_channels);
#endif

      if (0 == ext_in_port_ptr->cu.media_fmt.pcm.num_channels)
      {
         return result;
      }

      // Need to free buf_ptr[0].data_ptr before freeing buf_ptr.
      if (ext_in_port_ptr->topo_buf.buf_ptr && ext_in_port_ptr->topo_buf.buf_ptr[0].data_ptr)
      {
         // Flush the local buffer to reset the state of buffered data (if any).
         // for e.g the timestamps in the list are no longer valid.
         spl_cntr_ext_in_port_flush_local_buffer(me_ptr, ext_in_port_ptr);
         MFREE_NULLIFY(ext_in_port_ptr->topo_buf.buf_ptr[0].data_ptr);
      }

      if (ext_in_port_ptr->topo_buf.num_bufs != ext_in_port_ptr->cu.media_fmt.pcm.num_channels)
      {
         uint32_t size = ext_in_port_ptr->cu.media_fmt.pcm.num_channels * sizeof(capi_buf_t);

         if (ext_in_port_ptr->topo_buf.buf_ptr)
         {
            MFREE_NULLIFY(ext_in_port_ptr->topo_buf.buf_ptr);
         }

         /* allocating memory for capi v2 bufs*/
         MALLOC_MEMSET(ext_in_port_ptr->topo_buf.buf_ptr, capi_buf_t, size, me_ptr->cu.heap_id, result);

         ext_in_port_ptr->topo_buf.num_bufs = ext_in_port_ptr->cu.media_fmt.pcm.num_channels;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "spl_cntr_resize_ext_in_buffer: ext in port buf ptr: 0x%lx, size allocated: %d",
                      ext_in_port_ptr->topo_buf.buf_ptr,
                      size);
#endif
      }

      // At this point buf_ptr should exist.
      VERIFY(result, ext_in_port_ptr->topo_buf.buf_ptr);

      MALLOC_MEMSET(ext_in_port_ptr->topo_buf.buf_ptr[0].data_ptr,
                    int8_t,
                    required_size_bytes,
                    me_ptr->cu.heap_id,
                    result);

      buf_size_per_channel = required_size_bytes / ext_in_port_ptr->topo_buf.num_bufs;

      for (uint32_t i = 0; i < ext_in_port_ptr->topo_buf.num_bufs; i++)
      {
         /* allocating data ptr of each buffer from the same memory chunk */
         ext_in_port_ptr->topo_buf.buf_ptr[i].data_ptr =
            (int8_t *)(ext_in_port_ptr->topo_buf.buf_ptr[0].data_ptr) + (i * buf_size_per_channel);
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "spl_cntr_resize_ext_in_buffer: data_ptr: 0x%lx, channel: %d, buf_size_per_channel: %d",
                      ext_in_port_ptr->topo_buf.buf_ptr[i].data_ptr,
                      i,
                      buf_size_per_channel);
#endif

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
         ext_in_port_ptr->topo_buf.buf_ptr[i].max_data_len    = buf_size_per_channel;
         ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len = 0;
      }
#else
      }
      // optimization: update only first buffer lengths outside the for loop
      ext_in_port_ptr->topo_buf.buf_ptr[0].max_data_len    = buf_size_per_channel;
      ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = 0;
#endif

   }

   // Find nominal samples which should be equivalent to deliver size when media format doesn't change through
   // the topology. Exaple 44.1k, delivery size should be 44 samples, nominal samples should also be 44 samples.
   nominal_bytes                    = spl_topo_calc_buf_size(&(me_ptr->topo),
                                          me_ptr->topo.cntr_frame_len,
                                          &ext_in_port_ptr->cu.media_fmt,
                                          FOR_DELIVERY_TRUE);
   ext_in_port_ptr->nominal_samples = topo_bytes_to_samples_per_ch(nominal_bytes, &ext_in_port_ptr->cu.media_fmt);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "spl_cntr nominal samples is  %ld  for ext in port idx %ld miid 0x%lx",
                ext_in_port_ptr->nominal_samples,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

ar_result_t spl_cntr_buffer_held_input_metadata(spl_cntr_t             *me_ptr,
                                                spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                spf_msg_data_buffer_t  *data_msg_buf_ptr,
                                                uint32_t                local_buf_bytes_before_per_ch,
                                                uint32_t                held_data_msg_consumed_bytes_before,
                                                uint32_t                total_bytes_buffered_per_ch)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result                   = AR_EOK;
   bool_t                 new_flushing_eos_arrived = FALSE;
   spl_topo_input_port_t *int_in_port_ptr          = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // Buffer the any metadata which is within the amount of data copied (held_data_msg_consumed_bytes).
   if (data_msg_buf_ptr->metadata_list_ptr)
   {
      module_cmn_md_list_t *node_ptr = data_msg_buf_ptr->metadata_list_ptr;

      while (node_ptr)
      {
         // Since we might move the node to a different list, we need to find the next node in the loop up front.
         module_cmn_md_list_t *next_node_ptr = node_ptr->next_ptr;
         module_cmn_md_t      *md_ptr        = node_ptr->obj_ptr;

         // calculate the number of bytes per channel consumed at this point, including this process call
         uint32_t held_data_msg_consumed_samples_after_per_ch =
            topo_bytes_to_samples(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch, &ext_in_port_ptr->cu.media_fmt);

         // if metadata offset is within the number of samples consumed from this buffer so far, move it to the
         // internal list. else, leave it in the input data message itself so that it is consumed along with
         // the right sample in successive process calls
         if (md_ptr->offset <= held_data_msg_consumed_samples_after_per_ch)
         {
            bool_t ADD_TRUE       = TRUE;
            bool_t SUBTRACT_FALSE = FALSE;

            // Adjust offset of the metadata.
            //
            // data_msg_start      copy_start  md_orig_offset   copy_end      data_msg_end
            // |-------------------|-----------*----------------|-------------|
            //
            // local_buf_start   actual_data_len      max_data_len
            // |-----------------|          *         |
            //                              ^ md will go here
            //
            // The data from copy_start to copy_end got moved to the local_buf.
            // New md offset = md_offset_from_copy_start + old_local_buf_actual_data_len.
            // md_offset_from_copy_start = md_orig_offset - copy_start.
            gen_topo_do_md_offset_math(me_ptr->topo.t_base.gu.log_id,
                                       &md_ptr->offset,
                                       held_data_msg_consumed_bytes_before,
                                       &(ext_in_port_ptr->cu.media_fmt),
                                       SUBTRACT_FALSE);

            spf_list_move_node_to_another_list((spf_list_node_t **)&(
                                                  int_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                               (spf_list_node_t *)node_ptr,
                                               (spf_list_node_t **)&data_msg_buf_ptr->metadata_list_ptr);
            gen_topo_do_md_offset_math(me_ptr->topo.t_base.gu.log_id,
                                       &md_ptr->offset,
                                       local_buf_bytes_before_per_ch * ext_in_port_ptr->cu.media_fmt.pcm.num_channels,
                                       &(ext_in_port_ptr->cu.media_fmt),
                                       ADD_TRUE);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "ext input port idx %ld miid 0x%lx, buffering md ptr 0x%lx, offset after buffering %ld",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                         md_ptr,
                         md_ptr->offset);
#endif

            // Set eos on the external input port. Port state will move to flow gap for flushing EOS only after
            // all EOS leave the container.
            if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
            {
               TRY(result,
                   spl_cntr_ext_in_port_set_eos(me_ptr,
                                                ext_in_port_ptr,
                                                node_ptr,
                                                &int_in_port_ptr->t_base.common.sdata.metadata_list_ptr,
                                                &new_flushing_eos_arrived));
            }

            // If DFG arrives at the external input, move port to flow gap state immediately. To be consistent with topo
            // handling, it would be better to do this after dfg leaves the external input port. However to accomplish
            // that, we would have to implement another callback in the topo for dfg_left_ext_ip. Since dfg causes
            // force_process/block input, moving to flow gap before dfg leaves the external input port should be the
            // same as moving to flow gap after dfg leaves.
            if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
            {
               bool_t IS_MAX_FALSE = FALSE;
               if (md_ptr->offset <
                   topo_bytes_to_samples_per_ch(spl_cntr_ext_in_port_get_buf_len(&ext_in_port_ptr->gu, IS_MAX_FALSE),
                                                &ext_in_port_ptr->cu.media_fmt))
               {
                  gen_topo_capi_metadata_destroy((void *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr,
                                                 node_ptr,
                                                 TRUE /*is_dropped*/,
                                                 &data_msg_buf_ptr->metadata_list_ptr,
												 0,
												 FALSE);

                  SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                               DBG_MED_PRIO,
                               "clearing DFG since data after DFG is already present");
               }
            }
         }
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "ext input port idx %ld miid 0x%lx, NOT buffering md ptr 0x%lx, offset before %ld, "
                         "total_bytes_buffered_per_ch %ld",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                         md_ptr,
                         md_ptr->offset,
                         total_bytes_buffered_per_ch);
         }
#endif

         node_ptr = next_node_ptr;
      }
   }

   if (spl_topo_input_port_has_dfg_or_flushing_eos(&int_in_port_ptr->t_base))
   {
      // If a new EOS arrived, we need to start pushing zeros on the external input port's module.
      spl_topo_module_t *first_module_ptr = (spl_topo_module_t *)int_in_port_ptr->t_base.gu.cmn.module_ptr;
      TRY(result,
          spl_topo_ip_modify_md_when_new_data_arrives(&(me_ptr->topo),
                                                      first_module_ptr,
                                                      int_in_port_ptr,
                                                      total_bytes_buffered_per_ch,
                                                      new_flushing_eos_arrived));
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

static uint32_t spl_cntr_ext_in_port_get_required_data(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   spl_topo_input_port_t *in_port_ptr     = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   uint32_t               available_bytes = 0;
   uint32_t               required_data   = 0;
   uint32_t               required_data2  = 0;

   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      return 0;
   }

   if (ext_in_port_ptr->topo_buf.end_of_frame)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Don't need data on external input port id = 0x%x, miid = 0x%lx: force process is true",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
      return 0;
   }

   // get the data needed on this input port.
   if (ext_in_port_ptr->next_process_samples_valid)
   {
      // Don't need to check the data buffered at nblc end as it is already considered in next_process_samples.
      required_data = ext_in_port_ptr->next_process_samples * (ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);
   }
   else
   {
      required_data = ext_in_port_ptr->nominal_samples * (ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);

      required_data2 = required_data;

      // get the required data on this external input port's nblc end port.
      if ((in_port_ptr->t_base.nblc_end_ptr) &&
          (in_port_ptr->t_base.nblc_end_ptr != (gen_topo_input_port_t *)in_port_ptr))
      {
         required_data2 = spl_topo_get_in_port_required_data(&me_ptr->topo,
                                                             (spl_topo_input_port_t *)in_port_ptr->t_base.nblc_end_ptr);

         if (required_data2)
         {
            // Scaled samples can be used to scale bytes as well.
            required_data2 =
               spl_topo_get_scaled_samples(&me_ptr->topo,
                                           required_data2,
                                           in_port_ptr->t_base.nblc_end_ptr->common.media_fmt_ptr->pcm.sample_rate,
                                           in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);
         }
      }
      else if (NULL == in_port_ptr->t_base.nblc_end_ptr)
      {
         /* If input port's nblc end is null then it means that there is no
          * buffering/mimo/trigger policy module after this input port.
          * so we should check if connected external output port is started or not.
          * */
         spl_topo_output_port_t *out_port_ptr =
            (spl_topo_output_port_t *)in_port_ptr->t_base.gu.cmn.module_ptr->output_port_list_ptr->op_port_ptr;
         spl_topo_output_port_t *ext_out_port_ptr = (spl_topo_output_port_t *)out_port_ptr->t_base.nblc_end_ptr;

         if (TOPO_PORT_STATE_STARTED != ext_out_port_ptr->t_base.common.state)
         {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "Don't need data on external input port id = 0x%x, miid = 0x%lx:"
                         " connected external output port is not started.",
                         out_port_ptr->t_base.gu.cmn.id,
                         out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);

#endif
            required_data2 = 0;
         }
      }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "external input port id = 0x%x, miid = 0x%lx:,"
                   " data needed at self %d bytes per channel,"
                   " data needed at nblc end %d bytes per channel,",
                   in_port_ptr->t_base.gu.cmn.id,
                   in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                   required_data,
                   required_data2);
#endif

      // minimum of data needed on this input port and nblc end input port.
      // this avoids unnecessary data pile up.
      required_data = MIN(required_data, required_data2);
   }

   // when checking from the container "bytes_consumed_per_ch" will always be zero.
   // but when checking from topo during multiple iteration, it can be positive.
   available_bytes =
      ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len - ext_in_port_ptr->topo_buf.bytes_consumed_per_ch;

   if (required_data > available_bytes)
   {
      required_data = required_data - available_bytes;
   }
   else
   {
      required_data = 0;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "external input port id = 0x%x, miid = 0x%lx:,"
                "already has %ld bytes per ch, required more %ld bytes per ch",
                in_port_ptr->t_base.gu.cmn.id,
                in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
                available_bytes,
                required_data);
#endif

   return required_data;
}

// get free space available in external input buffer in bytes.
uint32_t spl_cntr_ext_in_get_free_space(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      return 0;
   }

   uint32_t input_data_needed = spl_cntr_ext_in_port_get_required_data(me_ptr, ext_in_port_ptr);
   input_data_needed =
      MIN(input_data_needed,
          (ext_in_port_ptr->topo_buf.buf_ptr[0].max_data_len - ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len));
   return input_data_needed;
}

/**
 * Buffers a held input data message into the external port local buffering.
 */
ar_result_t spl_cntr_buffer_held_input_data(spl_cntr_t             *me_ptr,
                                            spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                            uint32_t               *data_needed_bytes_per_ch_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result           = AR_EOK;
   spf_msg_header_t *     header_ptr       = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   int8_t *               input_data_ptr   = NULL;
   //int8_t *               log_write_ptr    = NULL;
   spf_msg_data_buffer_t *data_msg_buf_ptr = NULL;
   uint32_t               num_bytes_copied_per_buf = 0;
   uint32_t               input_data_size_per_channel           = 0;
   uint32_t               held_data_msg_unconsumed_bytes_per_ch = 0;
   uint32_t               held_data_msg_consumed_bytes_per_ch   = 0;
   bool_t                 timestamp_is_valid                    = FALSE;
   uint32_t               local_buf_bytes_before_per_ch         = 0;
   uint32_t               local_buf_bytes_after_per_ch          = 0;
   uint32_t               total_bytes_buffered_per_ch           = 0;
   uint32_t               held_data_msg_consumed_bytes_before =
      topo_bytes_per_ch_to_bytes(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch, &ext_in_port_ptr->cu.media_fmt);
   spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

#ifdef SPL_CNTR_LOG_AT_INPUT
   const uint32_t IN_FILE_STR_CH_IDX = 21;
   const int32_t  IN_FILENAME_LEN    = 26;
   char           file_str_arr[IN_FILENAME_LEN + 1];
   const char    *file_str_ptr = "spl_cntr_ext_in_port_0_ch_0.raw";
#endif

   DBG_VERIFY(result, (NULL != header_ptr));
   DBG_VERIFY(result, (NULL != data_needed_bytes_per_ch_ptr));

   if (SPF_MSG_DATA_BUFFER != ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      return AR_EOK;
   }

   data_msg_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;
   input_data_size_per_channel =
      topo_bytes_to_bytes_per_ch(data_msg_buf_ptr->actual_size, &ext_in_port_ptr->cu.media_fmt);

   held_data_msg_consumed_bytes_per_ch = ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch;

   // Size of remaining input data per channel.
   held_data_msg_unconsumed_bytes_per_ch = input_data_size_per_channel - held_data_msg_consumed_bytes_per_ch;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "ext input port idx %ld miid 0x%lx, Buffering held data msg to the local buffer. Before buffering: "
                "actual "
                "size = %ld, held_data_msg_consumed_bytes: %ld, 1 ch local buf actual_datal_len %ld, md ptr 0x%lx",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                data_msg_buf_ptr->actual_size,
                held_data_msg_consumed_bytes_before,
                ext_in_port_ptr->topo_buf.buf_ptr ? ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len : 0,
                data_msg_buf_ptr->metadata_list_ptr);
#endif

   /* off setting the input data ptr by number of bytes consumed per channel */
   input_data_ptr = (int8_t *)(data_msg_buf_ptr->data_buf) + held_data_msg_consumed_bytes_per_ch;
   // log_write_ptr  = input_data_ptr;

   // Copy the held msg's timestamp. Only do this if we haven't buffered any data yet. TS handling not required for
   // voice use case.
   if ((0 == ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch) && (!me_ptr->cu.voice_info_ptr))
   {
      // Copy the timestamp before pushing data so it's easy to calculate the timestamp offset.
      // No need to copy the timestamp if it isn't valid.
      // We copy the timestamp from the input only the first time we write data from the held msg to the local buffer.
      // 0 data consumed -> copy timestamp, any data consumed -> don't copy timestamp.
      timestamp_is_valid = cu_get_bits(data_msg_buf_ptr->flags,
                                       DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                                       DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);
      // validated timestamp only if there is any data in the buffer.
      if (timestamp_is_valid && (0 != data_msg_buf_ptr->actual_size))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "SPL_CNTR new timestamp found on data msg for ext input port idx %ld miid 0x%lx: %ld.",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      (uint32_t)data_msg_buf_ptr->timestamp);
#endif

         bool_t ts_disc = spl_cntr_ext_in_port_check_timestamp_discontinuity(me_ptr, ext_in_port_ptr, data_msg_buf_ptr);

         if (ts_disc)
         {
            if (!in_port_ptr->t_base.flags.is_threshold_disabled_prop)
            {
               ext_in_port_ptr->topo_buf.timestamp_discontinuity = TRUE;
               ext_in_port_ptr->topo_buf.first_frame_after_gap   = FALSE;
               return result;
            }
            else
            {
               // if threshold is disabled then ignore the discontinuity
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_HIGH_PRIO,
                            "Ignoring the disc on ext input port idx %ld miid 0x%lx.",
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
            }
         }

         TRY(result, spl_cntr_ext_in_port_push_timestamp_to_local_buf(me_ptr, ext_in_port_ptr));
      }
   }
   // For VPTX, to handle data drops on secondary port (due to signal miss), we check for timestamp discontinuity and
   // pad zeros to fill the discontinuity.
   else if ((0 == ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch) &&
            spl_cntr_fwk_extn_voice_delivery_found(me_ptr, NULL) && (0 == ext_in_port_ptr->vptx_ts_zeros_to_push_us))
   {
      timestamp_is_valid = cu_get_bits(data_msg_buf_ptr->flags,
                                       DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                                       DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);
      // validated timestamp only if there is any data in the buffer.
      if (timestamp_is_valid && (0 != data_msg_buf_ptr->actual_size))
      {
         TRY(result, spl_cntr_fwk_extn_voice_delivery_handle_timestamp(me_ptr, ext_in_port_ptr, data_msg_buf_ptr));
      }
   }

   // get the available space at external input buffer.
   if (0 == *data_needed_bytes_per_ch_ptr)
   {
      *data_needed_bytes_per_ch_ptr = spl_cntr_ext_in_get_free_space(me_ptr, ext_in_port_ptr);
   }

   // VPTX attempts to fill timestamp discontinuity with zeros in order to robustly handle data drops, for example due
   // to signal miss in upstream speaker ep.
   if ((0 != ext_in_port_ptr->vptx_ts_zeros_to_push_us) && spl_cntr_fwk_extn_voice_delivery_found(me_ptr, NULL))
   {
      TRY(result,
          spl_cntr_fwk_extn_voice_delivery_push_ts_zeros(me_ptr, ext_in_port_ptr, data_needed_bytes_per_ch_ptr));
   }

   local_buf_bytes_before_per_ch = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;

   uint32_t prev_actual_data_len_per_buf = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   for (uint32_t i = 0; i < ext_in_port_ptr->cu.media_fmt.pcm.num_channels; i++)
   {
      /* appending new input data to the buffer */
      TOPO_MEMSCPY(num_bytes_copied_per_buf,
                   ((int8_t *)(ext_in_port_ptr->topo_buf.buf_ptr[i].data_ptr) + prev_actual_data_len_per_buf),
                   *data_needed_bytes_per_ch_ptr,
                   input_data_ptr,
                   held_data_msg_unconsumed_bytes_per_ch,
                   me_ptr->topo.t_base.gu.log_id,
                   "E2I: (0x%lX, 0x%lX)",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "ext input port idx %ld miid 0x%lx, channel %ld, appending data from input msg to the local "
                   "buffer, local buffer filled %ld of %ld bytes, held_data_msg_unconsumed_bytes_per_ch: %ld, "
                   "num_bytes_copied_per_buf: %ld",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   i,
                   prev_actual_data_len_per_buf,
                   ext_in_port_ptr->topo_buf.buf_ptr[0].max_data_len,
                   held_data_msg_unconsumed_bytes_per_ch,
                   num_bytes_copied_per_buf);
#endif

      input_data_ptr = input_data_ptr + input_data_size_per_channel;

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      /* incrementing the actual data length of the buffer */
      ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len += num_bytes_copied_per_buf;
   }
#else
   }
   // optimization: update only first buffer lengths outside the for loop
   ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len += num_bytes_copied_per_buf;
#endif

   // update the avaialbe space at external input buffer.
   *data_needed_bytes_per_ch_ptr -= num_bytes_copied_per_buf;

   /* Incrementing the input data consumed for the current input data msg */
   ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch += num_bytes_copied_per_buf;

   // We should never consume more of the input data msg than exists.
   DBG_VERIFY(result,
              topo_bytes_per_ch_to_bytes(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch,
                                         &ext_in_port_ptr->cu.media_fmt) <= data_msg_buf_ptr->actual_size);

   local_buf_bytes_after_per_ch = ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
   total_bytes_buffered_per_ch  = local_buf_bytes_after_per_ch - local_buf_bytes_before_per_ch;

   // Buffer metadata.
   TRY(result,
       spl_cntr_buffer_held_input_metadata(me_ptr,
                                           ext_in_port_ptr,
                                           data_msg_buf_ptr,
                                           local_buf_bytes_before_per_ch,
                                           held_data_msg_consumed_bytes_before,
                                           total_bytes_buffered_per_ch));

#ifdef SPL_CNTR_LOG_AT_INPUT
   memscpy(file_str_arr, IN_FILENAME_LEN * sizeof(char), file_str_ptr, IN_FILENAME_LEN * sizeof(char));
   file_str_arr[IN_FILENAME_LEN] = '\0';

   for (uint32_t i = 0; i < ext_in_port_ptr->cu.media_fmt.pcm.num_channels; i++)
   {
      bool_t opened = TRUE;

      file_str_arr[IN_FILE_STR_CH_IDX] = '0' + i;

      FILE *file = fopen(file_str_arr, "a");
      if (file == NULL)
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "Error opening file!\n");
         opened = FALSE;
      }

      if (opened)
      {
         fwrite(log_write_ptr, 1, num_bytes_copied_per_buf, file);
         fclose(file);
      }

      log_write_ptr += input_data_size_per_channel;
   }
#endif

   ext_in_port_ptr->topo_buf.first_frame_after_gap = FALSE;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

#if 0
/**
 * Creates temporary output buffer of size buf_size.
 */
ar_result_t spl_cntr_alloc_temp_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "In spl_cntr_alloc_temp_out_buf for ext out port idx = %ld, miid = 0x%lx",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

   uint32_t buf_size = spl_cntr_calc_required_ext_out_buf_size(me_ptr, ext_out_port_ptr);

   if (ext_out_port_ptr->temp_out_buf_ptr)
   {
      if (buf_size == ext_out_port_ptr->temp_out_buf_size)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_LOW_PRIO,
                      "Temporary buffer already exists for ext out port idx = %ld, miid = 0x%lx, no need to "
                      "reallocate.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

         return result;
      }

      MFREE_NULLIFY(ext_out_port_ptr->temp_out_buf_ptr);
   }

   ext_out_port_ptr->temp_out_buf_ptr = (int8_t *)posal_memory_malloc(buf_size, me_ptr->cu.heap_id);
   if (!ext_out_port_ptr->temp_out_buf_ptr)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Insufficient memory to create temporary output buffer. It requires %lu bytes",
                   buf_size);
      return AR_ENOMEMORY;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "allocated temporary output buffer of size %lu for ext out port idx = %ld, miid = 0x%lx",
                buf_size,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

   ext_out_port_ptr->temp_out_buf_size = buf_size;

   return result;
}
#endif
/**
 * Creates external output buffers of size buf_size. Creates buffers until
 * num_bufs_allocated reaches buf_q_num_elements, and stores them in
 * ext_port_ptr's buffer queue.
 */
ar_result_t spl_cntr_create_ext_out_bufs(spl_cntr_t              *me_ptr,
                                         spl_cntr_ext_out_port_t *ext_port_ptr,
                                         uint32_t                 buf_size,
                                         uint32_t                 num_out_bufs)
{
   ar_result_t result = AR_EOK;

   if (0 == buf_size)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_HIGH_PRIO, "Not creating output bufers, as buffer size is zero.");
      return result;
   }

   if (num_out_bufs > CU_MAX_OUT_BUF_Q_ELEMENTS)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Warning: eventhough ICB determined %lu buffers, creating only %lu",
                   num_out_bufs,
                   CU_MAX_OUT_BUF_Q_ELEMENTS);
      num_out_bufs = CU_MAX_OUT_BUF_Q_ELEMENTS;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "Creating %lu external output buffers of size %lu for external out port idx %ld miid 0x%lx",
                (num_out_bufs - ext_port_ptr->cu.num_buf_allocated),
                buf_size,
                ext_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);

   result = spf_svc_create_and_push_buffers_to_buf_queue(ext_port_ptr->gu.this_handle.q_ptr,
                                                         buf_size,
                                                         num_out_bufs,
                                                         ext_port_ptr->gu.downstream_handle.spf_handle_ptr,
                                                         gu_get_downgraded_heap_id(me_ptr->topo.t_base.heap_id,
                                                                                   ext_port_ptr->gu.downstream_handle
                                                                                      .heap_id),
                                                         &ext_port_ptr->cu.num_buf_allocated);

   return result;
}

/*
 * Checks whether there is any data buffered in the external port's output buffer.
 */
bool_t spl_cntr_is_output_buffer_empty(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   bool_t is_empty = FALSE;

   is_empty = !(ext_out_port_ptr->topo_buf.buf_ptr && ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len);

   return is_empty;
}

/**
 * Sets up the external output port's topo_buf_t structure after popping an output buffer
 * from the buffer queue.
 */
ar_result_t spl_cntr_init_after_getting_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   uint32_t               result      = AR_EOK;
   uint32_t               buf_size    = 0;
   spf_msg_header_t      *header_ptr  = NULL;
   spf_msg_data_buffer_t *out_buf_ptr = NULL;
   int8_t                *buf_ptr     = NULL;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "entering spl_cntr_init_after_getting_out_buf");
#endif

   // Tasks only relevant if the buffer manager node or temporary buffer exists.
   if (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      header_ptr  = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
      out_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;
      buf_ptr     = (int8_t *)out_buf_ptr->data_buf;

      out_buf_ptr->actual_size = 0;
      // Clear output buffer flags explicitly. This was leading to buffers being
      // incorrectly tagged as prebuffers.
      out_buf_ptr->flags = 0;
      buf_size           = out_buf_ptr->max_size;
   }
   else if (NULL != ext_out_port_ptr->temp_out_buf_ptr)
   {
      buf_ptr  = ext_out_port_ptr->temp_out_buf_ptr;
      buf_size = ext_out_port_ptr->temp_out_buf_size;
   }
   else
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "spl_cntr_init_after_getting_out_buf: out port buffer node and temporary out buf are null");
      return AR_EFAILED;
   }

   uint32_t buf_size_per_channel = topo_bytes_to_bytes_per_ch(buf_size, &ext_out_port_ptr->cu.media_fmt);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "spl_cntr_init_after_getting_out_buf: out port num bufs: %d, num channels: %d",
                ext_out_port_ptr->topo_buf.num_bufs,
                ext_out_port_ptr->cu.media_fmt.pcm.num_channels);
#endif

   if (ext_out_port_ptr->topo_buf.num_bufs != ext_out_port_ptr->cu.media_fmt.pcm.num_channels)
   {
      if (ext_out_port_ptr->topo_buf.buf_ptr)
      {
         MFREE_NULLIFY(ext_out_port_ptr->topo_buf.buf_ptr);
      }

      uint32_t size = ext_out_port_ptr->cu.media_fmt.pcm.num_channels * sizeof(capi_buf_t);

      MALLOC_MEMSET(ext_out_port_ptr->topo_buf.buf_ptr, capi_buf_t, size, me_ptr->cu.heap_id, result);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "spl_cntr_init_after_getting_out_buf: ext_out_port buf_ptr: 0x%lx",
                   ext_out_port_ptr->topo_buf.buf_ptr);
#endif
   }

   for (uint32_t i = 0; i < ext_out_port_ptr->cu.media_fmt.pcm.num_channels; i++)
   {
      ext_out_port_ptr->topo_buf.buf_ptr[i].data_ptr = buf_ptr + (i * buf_size_per_channel);
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "spl_cntr_init_after_getting_out_buf: ext_out_port buf data_ptr: 0x%lx, channel: %d, "
                   "buf_size_per_channel: "
                   "%d",
                   ext_out_port_ptr->topo_buf.buf_ptr[i].data_ptr,
                   i,
                   buf_size_per_channel);
#endif

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      ext_out_port_ptr->topo_buf.buf_ptr[i].actual_data_len = 0;
      ext_out_port_ptr->topo_buf.buf_ptr[i].max_data_len    = buf_size_per_channel;
   }
#else
   }
   // optimization: update only first buffer lengths outside the for loop
   ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len = 0;
   ext_out_port_ptr->topo_buf.buf_ptr[0].max_data_len    = buf_size_per_channel;
#endif

   ext_out_port_ptr->topo_buf.num_bufs = ext_out_port_ptr->cu.media_fmt.pcm.num_channels;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Return a held output buffer and flush the local port buf
 */
ar_result_t spl_cntr_return_held_out_buf(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   if (ext_out_port_ptr && ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
   }

   spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr);

   return AR_EOK;
}

/* if a buffer is recreated, then
 * ext_port_ptr->out_data_buf_node.pBuffer is NULL
 * and the caller must go back to work loop.
 */
ar_result_t spl_cntr_check_realloc_ext_out_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_port_ptr)
{
   ar_result_t       result     = AR_EOK;
   spf_msg_header_t *buffer_ptr = (spf_msg_header_t *)ext_port_ptr->cu.out_bufmgr_node.buf_ptr;

   if (NULL == buffer_ptr)
   {
      return result;
   }

   spf_msg_data_buffer_t *data_buf_ptr = (spf_msg_data_buffer_t *)&buffer_ptr->payload_start;
   uint32_t               max_size     = data_buf_ptr->max_size;
   // if buf size or count doesn't match then recreate.
   uint32_t num_bufs_needed =
      ext_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_port_ptr->cu.icb_info.icb.num_reg_prebufs;
   uint32_t num_bufs_to_destroy = num_bufs_needed >= ext_port_ptr->cu.num_buf_allocated
                                     ? 0
                                     : (ext_port_ptr->cu.num_buf_allocated - num_bufs_needed);

   if ((data_buf_ptr->max_size != ext_port_ptr->cu.buf_max_size) || (num_bufs_to_destroy > 0))
   {

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_LOW_PRIO, " Destroyed 1 external buffers 0x%p", buffer_ptr);
      // Free the buffer
      posal_memory_free(buffer_ptr);
      ext_port_ptr->cu.num_buf_allocated--;
      ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
   }

   uint32_t num_bufs_to_create =
      num_bufs_needed > ext_port_ptr->cu.num_buf_allocated ? num_bufs_needed - ext_port_ptr->cu.num_buf_allocated : 0;

   if ((max_size != ext_port_ptr->cu.buf_max_size) || (num_bufs_to_create > 0))
   {
      // one buf was destroyed, so create one more.
      result = spl_cntr_create_ext_out_bufs(me_ptr,
                                            ext_port_ptr,
                                            ext_port_ptr->cu.buf_max_size,
                                            ext_port_ptr->cu.num_buf_allocated + 1);
      if (AR_DID_FAIL(result))
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, " Buffer recreate failed %d", result);
      }
   }
   return result;
}

/**
 * Pops a buffer manager node from the queue and handle setup of all output
 * buffer related fields. If we could not pop from the queue, returns
 * AR_ENEEDMORE.
 */
ar_result_t spl_cntr_get_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result           = AR_EOK;
   spl_topo_output_port_t *int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Trying to get a buffer when we already have a buffer!");
#endif
      return AR_EFAILED;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Trying to get a buffer:popping new buffer!");
#endif

   // All output buffers are expected to be the proper size. If any are not, that means
   // the nominal frame size or media format changed after creating the buffers. To handle
   // that case, whenever we pop a buffer we check if it is the wrong size. If so, we reallocate
   // a new buffer at the proper size and push it to the back of the queue.
   //
   // Continue checking until we pop a buffer of the right size.
   while ((posal_channel_poll_inline(me_ptr->cu.channel_ptr, ext_out_port_ptr->cu.bit_mask)) &&
          (!ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr))
   {
      // Dequeue the output buffer mgr node.
      result = posal_queue_pop_front(ext_out_port_ptr->gu.this_handle.q_ptr,
                                     (posal_queue_element_t *)&(ext_out_port_ptr->cu.out_bufmgr_node));

      // If popping failed, ensure buf_ptr is NULL and return.
      if (AR_EOK != result)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "Failed to pop output on ext out port idx %ld, miid 0x%lx, result 0x%lx.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      result);
#endif
         ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
         return result;
      }

      result = spl_cntr_check_realloc_ext_out_buffer(me_ptr, ext_out_port_ptr);

      if (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
      {
         break;
      }
   }

   if (!ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Cant get output buffer: no output buffers in queue on ext out port idx %ld, miid 0x%lx.",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

      return AR_ENEEDMORE;
   }

   TRY(result, spl_cntr_init_after_getting_out_buf(me_ptr, ext_out_port_ptr));

   // If we have a pending timestamp discontinuity at the external output port, then we should
   // copy that data into this output buffer and send it immediately. Then try again to get an output
   // buffer. Note that since we don't trigger processing audio, this would only happen once (ts_disc is cleared
   // after first call) and we can't end up in an infinite loop.
   if (ext_out_port_ptr->topo_buf.timestamp_discontinuity)
   {
      TRY(result,
          spl_topo_transfer_data_from_ts_temp_buf(&(me_ptr->topo), int_out_port_ptr, &(ext_out_port_ptr->topo_buf)));
      TRY(result, spl_cntr_deliver_output_buffer(me_ptr, ext_out_port_ptr));
      return spl_cntr_get_output_buffer(me_ptr, ext_out_port_ptr);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Delivers an output buffer (data msg) downstream.
 * - Populates the output buffer manager node.
 * - Converts the buffer manager node to a spf_msg and pushes to the output
 *   buffer queue.
 *    - If the push fails, returns the output buffer (buffer is dropped)
 * - Updates relevant spl_cntr_ext_out_port_t fields now that buffer is delivered.
 */
ar_result_t spl_cntr_deliver_output_buffer(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING

   ar_result_t             result           = AR_EOK;
   bool_t                  FOR_DELIVERY     = TRUE;
   spl_topo_output_port_t *int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (!(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "post gpd: can't deliver buffer, out_bufmgr_node.buf_ptr on port idx = %ld, miid = 0x%lx "
                   "bit_mask 0x%lx is null!",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->cu.bit_mask);
      return result;
   }

   if (!ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "downstream not connected. dropping buffer");
      cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
      spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr);
      return AR_EFAILED;
   }

   // Considered as data did move if an output buffer gets delivered.
   me_ptr->topo.proc_info.state_changed_flags.data_moved |= TRUE;

   if ((!ext_out_port_ptr->topo_buf.buf_ptr->actual_data_len) && (!int_out_port_ptr->md_list_ptr))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_LOW_PRIO,
                   "Buf to deliver is of size 0. Dropping buffer instead.");
      cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
      spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr);
      return result;
   }

   topo_port_state_t ds_downgraded_state =
      cu_get_external_output_ds_downgraded_port_state( &ext_out_port_ptr->cu);

   // can happen if downstream is stopped.
   if (TOPO_PORT_STATE_STARTED != ds_downgraded_state)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_LOW_PRIO,
                   "External output port's downstream state (0x%lx)is not started. Dropping buffer with %lu bytes.",
                   ds_downgraded_state,
                   ext_out_port_ptr->topo_buf.buf_ptr->actual_data_len);
      cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
      spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr);
      return result;
   }

   posal_bufmgr_node_t    out_buf_node    = ext_out_port_ptr->cu.out_bufmgr_node;
   spf_msg_header_t      *header_ptr      = (spf_msg_header_t *)(out_buf_node.buf_ptr);
   spf_msg_data_buffer_t *out_buf_msg_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;

#ifdef SPL_CNTR_LOG_AT_OUTPUT
   const uint32_t OUT_FILE_STR_CH_IDX = 22;
   const int32_t  OUT_FILENAME_LEN    = 27;
   char           file_str_arr[OUT_FILENAME_LEN + 1];
   const char    *file_str_ptr = "spl_cntr_ext_out_port_0_ch_0.raw";

   memscpy(file_str_arr, OUT_FILENAME_LEN * sizeof(char), file_str_ptr, OUT_FILENAME_LEN * sizeof(char));
   file_str_arr[OUT_FILENAME_LEN] = '\0';

   for (uint32_t i = 0; i < ext_out_port_ptr->cu.media_fmt.pcm.num_channels; i++)
   {
      bool_t   opened                   = TRUE;
      uint32_t channel_size             = ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len;
      int8_t  *log_write_ptr            = (int8_t *)ext_out_port_ptr->topo_buf.buf_ptr[i].data_ptr;
      file_str_arr[OUT_FILE_STR_CH_IDX] = '0' + i;

      FILE *file = fopen(file_str_arr, "a");
      if (file == NULL)
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "Error opening file!\n");
         opened = FALSE;
      }

      if (opened)
      {
         fwrite(log_write_ptr, 1, channel_size, file);
         fclose(file);
      }
   }
#endif

   spl_cntr_pack_external_output(me_ptr, ext_out_port_ptr, out_buf_msg_ptr);

   // Write the timestamp to the data msg.
   out_buf_msg_ptr->timestamp = ext_out_port_ptr->topo_buf.timestamp;
   cu_set_bits(&out_buf_msg_ptr->flags,
               ext_out_port_ptr->topo_buf.timestamp_is_valid,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

   if (!ext_out_port_ptr->cu.icb_info.is_prebuffer_sent)
   {
      cu_handle_prebuffer(&me_ptr->cu,
                          &ext_out_port_ptr->gu,
                          out_buf_msg_ptr,
                          spl_topo_calc_buf_size(&(me_ptr->topo),
                                                 me_ptr->topo.cntr_frame_len,
                                                 &(ext_out_port_ptr->pending_media_fmt),
                                                 FOR_DELIVERY));
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Sending timestamp to ext out port idx = %ld, miid = 0x%lx, ts = %lu, flags = 0x%lx",
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                (uint32_t)out_buf_msg_ptr->timestamp,
                out_buf_msg_ptr->flags);
#endif

#ifdef PROC_DELAY_DEBUG
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   if (IS_VOICE_SCENARIO_ID(module_ptr->gu.sg_ptr->sid))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "PROC_DELAY_DEBUG: SC Module 0x%lX: Ext output data sent from port 0x%lX",
                   module_ptr->gu.module_instance_id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
   }
#endif

   gen_topo_check_realloc_md_list_in_peer_heap_id(me_ptr->topo.t_base.gu.log_id,
                                                  &(ext_out_port_ptr->gu),
                                                  &(int_out_port_ptr->md_list_ptr));

   bool_t out_buf_has_flushing_eos_dfg =
      int_out_port_ptr->md_list_ptr ? gen_topo_md_list_has_flushing_eos_or_dfg(int_out_port_ptr->md_list_ptr) : FALSE;

   // Move md from ext out port to data msg. We always deliver the entire buffer, so we can also deliver
   // all metadata.
   bool_t OUT_BUF_HAS_FLUSHING_EOS_UNUSED = FALSE;
   gen_topo_populate_metadata_for_peer_cntr(&(me_ptr->topo.t_base),
                                            &(ext_out_port_ptr->gu),
                                            &(int_out_port_ptr->md_list_ptr),
                                            &(out_buf_msg_ptr->metadata_list_ptr),
                                            &OUT_BUF_HAS_FLUSHING_EOS_UNUSED);

   // At this point, md_list is NULL so it cannot contain any flushing eos. Set marker to FALSE.
   int_out_port_ptr->t_base.common.sdata.flags.marker_eos = FALSE;

   spf_msg_t *data_msg_ptr = spf_msg_convt_buf_node_to_msg(&out_buf_node,
                                                           SPF_MSG_DATA_BUFFER,
                                                           NULL,
                                                           NULL,
                                                           0,
                                                           &ext_out_port_ptr->gu.this_handle);

   result = posal_queue_push_back(ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr,
                                  (posal_queue_element_t *)data_msg_ptr);
   if (AR_DID_FAIL(result))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "Failed to deliver buffer dowstream. Dropping");
      cu_return_out_buf(&me_ptr->cu, &ext_out_port_ptr->gu);
   }

   // Raise the frame delivery done event.
   TRY(result, spl_cntr_handle_frame_done(me_ptr, ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index));

   spl_cntr_clear_output_buffer(me_ptr, ext_out_port_ptr);

   if (out_buf_has_flushing_eos_dfg)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Resetting output port idx %ld miid 0x%lx after delivering ext out buf with dfg/eos.",
                   int_out_port_ptr->t_base.gu.cmn.index,
                   int_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      bool_t USE_BUFMGR_FALSE = FALSE;
      topo_basic_reset_output_port(&(me_ptr->topo.t_base), &(int_out_port_ptr->t_base), USE_BUFMGR_FALSE);
      ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = FALSE;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Delivered an output buffer with actual size = %ld, output port idx %ld miid 0x%lx",
                out_buf_msg_ptr->actual_size,
                int_out_port_ptr->t_base.gu.cmn.index,
                int_out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * destroys (ext_port_ptr->cu.num_buf_allocated - num_bufs_to_keep) num of buffers,
 * where num_bufs_to_keep can be different from num_buf_allocated
 */
static void spl_cntr_destroy_ext_buffers(spl_cntr_t              *me_ptr,
                                         spl_cntr_ext_out_port_t *ext_port_ptr,
                                         uint32_t                 num_bufs_to_keep)
{
   uint32_t num_bufs_to_destroy         = 0;
   uint32_t num_bufs_actually_destroyed = 0;
   if (num_bufs_to_keep < ext_port_ptr->cu.num_buf_allocated)
   {
      num_bufs_to_destroy = (ext_port_ptr->cu.num_buf_allocated - num_bufs_to_keep);
   }

   // Flushing data is already done - maybe move it here

   if (NULL == ext_port_ptr->gu.this_handle.q_ptr)
   {
      return;
   }

   // If we had allocated buffers, need to destroy now.
   if (num_bufs_to_destroy)
   {
      // We don't want to destroy all buffers, so pass in a dummy variable of how many buffers to destroy.
      // It will get decremented by the amount of buffers actually destroyed. We then have to explicitly
      // update the cu.num_buf_allocated according to number of buffers actually destroyed.
      num_bufs_actually_destroyed = num_bufs_to_destroy;
      spf_svc_free_buffers_in_buf_queue_nonblocking(ext_port_ptr->gu.this_handle.q_ptr, &num_bufs_to_destroy);
      num_bufs_actually_destroyed = num_bufs_actually_destroyed - num_bufs_to_destroy;

      ext_port_ptr->cu.num_buf_allocated -= num_bufs_actually_destroyed;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "Destroyed %lu external buffers. num_bufs_kept %lu",
                num_bufs_actually_destroyed,
                ext_port_ptr->cu.num_buf_allocated);
}

ar_result_t spl_cntr_recreate_ext_out_buffers(void *ctx_ptr, gu_ext_out_port_t *gu_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t              result           = AR_EOK;
   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)gu_out_port_ptr;
   cu_base_t               *base_ptr         = (cu_base_t *)ctx_ptr;
   spl_cntr_t              *me_ptr           = (spl_cntr_t *)base_ptr;
   uint32_t                 buf_size         = spl_cntr_calc_required_ext_out_buf_size(me_ptr, ext_out_port_ptr);

   if (buf_size)
   {
      uint32_t metadata_size    = 0;
      uint32_t num_out_bufs     = 0;
      uint32_t req_out_buf_size = buf_size;

      result = spl_cntr_get_required_out_buf_size_and_count(me_ptr,
                                                            ext_out_port_ptr,
                                                            &num_out_bufs,
                                                            &req_out_buf_size,
                                                            buf_size,
                                                            metadata_size);
      if (AR_DID_FAIL(result))
      {
         return AR_EOK;
      }

      // Only create if size is different or number of buffers needed changes
      if ((buf_size != ext_out_port_ptr->cu.buf_max_size) || (num_out_bufs != ext_out_port_ptr->cu.num_buf_allocated))
      {
         bool_t was_holding_out_buf = (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
         if (was_holding_out_buf)
         {
            // If we were holding an output buffer, we need to return it and then get a new
            // buffer at the new size. Deliver any partial data at this time.
            if (spl_cntr_ext_out_port_has_unconsumed_data(me_ptr, ext_out_port_ptr))
            {
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning: found unconsumed data in output buffer while resizing output buffers."
                            "Sending data downstream.");

               TRY(result, spl_cntr_deliver_output_buffer(me_ptr, ext_out_port_ptr));
            }
            else
            {
               spl_cntr_return_held_out_buf(me_ptr, ext_out_port_ptr);
            }
         }

         uint32_t num_bufs_to_keep = (req_out_buf_size != ext_out_port_ptr->cu.buf_max_size) ? 0 : num_out_bufs;

         // non-blocking destroy of the buffers
         spl_cntr_destroy_ext_buffers(me_ptr, ext_out_port_ptr, num_bufs_to_keep);

         // update the buffer max size needed
         ext_out_port_ptr->cu.buf_max_size = req_out_buf_size;

         TRY(result, spl_cntr_create_ext_out_bufs(me_ptr, ext_out_port_ptr, req_out_buf_size, num_out_bufs));

         // If we were holding an output buf, we just returned it, which guarantees that the
         // buffer queue will have at least one element for us to pop.

         spl_cntr_update_gpd_and_cu_bit_mask(me_ptr);
      }
      else
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "Not recreating external output buffers because the size did not change.");
#endif
      }

      // update ext output delivery size
      ext_out_port_ptr->delivery_buf_size = spl_cntr_calc_ext_out_buf_deliver_size(me_ptr, ext_out_port_ptr);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * The external port output buffer size and
 * external port input buffer size are both dependent on the threshold, so this
 * can cause resizing of external port buffers.
 */
ar_result_t spl_cntr_handle_ext_buffer_size_change(void *ctx_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result                = AR_EOK;
   spl_cntr_t             *me_ptr                = (spl_cntr_t *)ctx_ptr;
   gu_ext_in_port_list_t  *ext_in_port_list_ptr  = NULL;
   gu_ext_out_port_list_t *ext_out_port_list_ptr = NULL;
   ext_in_port_list_ptr                          = me_ptr->topo.t_base.gu.ext_in_port_list_ptr;
   bool_t IS_MAX_TRUE                            = TRUE;

   // Need to dermine external input max sizes in case of DM.
   TRY(result, spl_topo_get_required_input_samples(&me_ptr->topo, IS_MAX_TRUE));

   // Check and resize external input port buffers.
   ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr;
   while (ext_in_port_list_ptr)
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      if (!topo_is_valid_media_fmt(&ext_in_port_ptr->cu.media_fmt))
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "Not recreating external input buffer on input idx = %ld miid = 0x%lx, because invalid media "
                      "fmt.",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
         ext_in_port_list_ptr = ext_in_port_list_ptr->next_ptr;
         continue;
      }

      uint32_t buf_size = spl_cntr_calc_required_ext_in_buf_size(me_ptr, ext_in_port_ptr);

      TRY(result, spl_cntr_check_resize_ext_in_buffer(me_ptr, ext_in_port_ptr, buf_size));

      ext_in_port_list_ptr = ext_in_port_list_ptr->next_ptr;
   }

   // Check and resize external output port buffers.
   ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
   while (ext_out_port_list_ptr)
   {
      TRY(result, spl_cntr_recreate_ext_out_buffers((void *)&me_ptr->cu, ext_out_port_list_ptr->ext_out_port_ptr));
      ext_out_port_list_ptr = ext_out_port_list_ptr->next_ptr;
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/*
 * Checks if two thresholds are multiples of each other (for our usage, we pass in aggregated and
 * new thresholds, although any two thresholds can be compared).
 *
 * Check if threshold_a % threshold_b != 0 to see if they are multiples. Works only when a >= b.
 * a >= b is the same as a_samples * b_sr >= b_samples * a_sr.
 * To avoid rounding errors, threshold is represented as (# samples, sample rate) pair.
 * (a_samples / a_sr) % (b_samples / b_sr). Simplify by multiplying both sides by a_sr * b_sr
 * (a_samples * b_sr) % (b_samples * a_sr) and rounding due to division is avoided.
 *
 * Once we know they are multiples, we can do the actual division to get the multiple itself.
 * (a_samples * b_sr) / (b_samples * a_sr).
 */
static bool_t spl_cntr_check_if_new_threshold_is_multiple(spl_cntr_t *me_ptr,
                                                          uint32_t    agg_thresh_samples_per_channel,
                                                          uint32_t    agg_thresh_sample_rate,
                                                          uint32_t    new_thresh_samples_per_channel,
                                                          uint32_t    new_thresh_sample_rate,
                                                          uint32_t   *multiple_ptr)
{
   uint64_t a_samples = new_thresh_samples_per_channel;
   uint64_t a_sr      = new_thresh_sample_rate;

   uint64_t b_samples = agg_thresh_samples_per_channel;
   uint64_t b_sr      = agg_thresh_sample_rate;

   uint64_t a_samples__x__b_sr = a_samples * b_sr;
   uint64_t b_samples__x__a_sr = b_samples * a_sr;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "new_threshold_in_samples_per_channel (a): %u, existing_threshold_in_samples_per_channel (b): %u, "
                "new_threshold_port_sample_rate: %u, existing_threshold_port_sample_rate: %u, a * b_sr: %lu, b * a_sr: "
                "%lu",
                a_samples,
                b_samples,
                a_sr,
                b_sr,
                a_samples__x__b_sr,
                b_samples__x__a_sr);
#endif

   // Swap a and b so a is higher. Besides this requirement a and b are symmetrical.
   if (a_samples__x__b_sr < b_samples__x__a_sr)
   {
      uint64_t temp      = a_samples__x__b_sr;
      a_samples__x__b_sr = b_samples__x__a_sr;
      b_samples__x__a_sr = temp;
   }

   if (!((a_samples__x__b_sr) % (b_samples__x__a_sr)))
   {
      *multiple_ptr = (a_samples__x__b_sr) / (b_samples__x__a_sr);
      return TRUE;
   }

   return FALSE;
}

/*
 * Compare thresholds which are specified in samples and sampling rate.
 */
static bool_t spl_cntr_threshold_a_greater_than_b(spl_cntr_t *me_ptr,
                                                  uint64_t    a_samples,
                                                  uint64_t    a_sr,
                                                  uint64_t    b_samples,
                                                  uint64_t    b_sr)
{
   return (a_samples * b_sr) > (b_samples * a_sr);
}

/**
 * Called when a port raises a threshold event. Updates the port's module's threshold fields, and
 * reaggregates threshold across the container. Then calls another function to recalculate the
 * container frame length, and do handling if it changed.
 */
ar_result_t spl_cntr_handle_int_port_data_thresh_change_event(void *ctx_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)(ctx_ptr);

   uint32_t num_threshold_modules = 0;

   uint32_t          max_threshold_samples_per_ch = 0;
   topo_media_fmt_t *max_threshold_port_mf        = NULL;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *module_ptr                           = (spl_topo_module_t *)module_list_ptr->module_ptr;
         uint32_t           module_threshold_samples_per_channel = 0;
         topo_media_fmt_t  *module_threshold_port_mf             = NULL;

         // clear the module threshold data, will be aggregated across all the ports.
         memset(&module_ptr->threshold_data, 0, sizeof(module_ptr->threshold_data));

         // Aggregate thresholds from all input ports of this modules
         for (gu_input_port_list_t *in_port_list_ptr = module_ptr->t_base.gu.input_port_list_ptr;
              (NULL != in_port_list_ptr);
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            if (in_port_ptr->common.flags.port_has_threshold || in_port_ptr->common.flags.is_mf_valid)
            {
               topo_media_fmt_t *in_port_media_format_ptr = in_port_ptr->common.media_fmt_ptr;
               uint32_t          port_threshold_samples_per_channel =
                  topo_bytes_to_samples_per_ch(in_port_ptr->common.threshold_raised, in_port_media_format_ptr);

               if (!module_threshold_port_mf ||
                   spl_cntr_threshold_a_greater_than_b(me_ptr,
                                                       port_threshold_samples_per_channel,
                                                       in_port_media_format_ptr->pcm.sample_rate,
                                                       module_threshold_samples_per_channel,
                                                       module_threshold_port_mf->pcm.sample_rate))
               {
                  module_threshold_samples_per_channel =
                     topo_bytes_to_samples_per_ch(in_port_ptr->common.threshold_raised, in_port_media_format_ptr);
                  module_threshold_port_mf = in_port_media_format_ptr;
               }
            }
         }

         // Aggregate thresholds from all output ports of this modules
         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (out_port_ptr->common.flags.port_has_threshold || out_port_ptr->common.flags.is_mf_valid)
            {
               topo_media_fmt_t *out_port_media_format_ptr = out_port_ptr->common.media_fmt_ptr;
               uint32_t          port_threshold_samples_per_channel =
                  topo_bytes_to_samples_per_ch(out_port_ptr->common.threshold_raised, out_port_media_format_ptr);

               if (!module_threshold_port_mf ||
                   spl_cntr_threshold_a_greater_than_b(me_ptr,
                                                       port_threshold_samples_per_channel,
                                                       out_port_media_format_ptr->pcm.sample_rate,
                                                       module_threshold_samples_per_channel,
                                                       module_threshold_port_mf->pcm.sample_rate))
               {
                  module_threshold_samples_per_channel =
                     topo_bytes_to_samples_per_ch(out_port_ptr->common.threshold_raised, out_port_media_format_ptr);
                  module_threshold_port_mf = out_port_media_format_ptr;
               }
            }
         }

         if (module_threshold_samples_per_channel)
         {
            num_threshold_modules++;
            module_ptr->threshold_data.is_threshold_module           = TRUE;
            module_ptr->threshold_data.thresh_in_samples_per_channel = module_threshold_samples_per_channel;
            module_ptr->threshold_data.thresh_port_sample_rate       = module_threshold_port_mf->pcm.sample_rate;
         }

         // Find the aggregate threshold (threshold to be used by framework layer). This is the largest
         // of all port-reported thresholds (unit of comparison is duration).
         if (module_ptr->threshold_data.is_threshold_module)
         {
            if (!max_threshold_port_mf ||
                spl_cntr_threshold_a_greater_than_b(me_ptr,
                                                    module_ptr->threshold_data.thresh_in_samples_per_channel,
                                                    module_ptr->threshold_data.thresh_port_sample_rate,
                                                    max_threshold_samples_per_ch,
                                                    max_threshold_port_mf->pcm.sample_rate))
            {
               max_threshold_samples_per_ch = module_ptr->threshold_data.thresh_in_samples_per_channel;
               max_threshold_port_mf        = module_threshold_port_mf;
            }
         }
      }
   }

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;
         uint32_t           num_iters  = 1;

         module_ptr->t_base.num_proc_loops = num_iters;

         if (!module_ptr->threshold_data.is_threshold_module)
         {
            continue;
         }

         // Thresholds must be multiples of each other. Other use cases are not supported.
         if (max_threshold_port_mf &&
             !(spl_cntr_check_if_new_threshold_is_multiple(me_ptr,
                                                           max_threshold_samples_per_ch,
                                                           max_threshold_port_mf->pcm.sample_rate,
                                                           module_ptr->threshold_data.thresh_in_samples_per_channel,
                                                           module_ptr->threshold_data.thresh_port_sample_rate,
                                                           &num_iters)))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%x new threshold %lu samples per ch is not a multiple of overall threshold %lu "
                         "samples per ch",
                         module_ptr->t_base.gu.module_instance_id,
                         module_ptr->threshold_data.thresh_in_samples_per_channel,
                         max_threshold_samples_per_ch);
#ifdef SIM
            posal_err_fatal("Crashing on SIM");
#endif
            return AR_EFAILED;
         }

         // update the number of processing loops
         module_ptr->t_base.num_proc_loops = num_iters;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_HIGH_PRIO,
                      "miid 0x%lx number of process iterations: %u",
                      module_ptr->t_base.gu.module_instance_id,
                      num_iters);
#endif
      }
   }

   // update the aggregated threshold info.
   me_ptr->threshold_data.threshold_port_sample_rate =
      (max_threshold_port_mf) ? max_threshold_port_mf->pcm.sample_rate : 0;
   me_ptr->threshold_data.threshold_in_samples_per_channel = max_threshold_samples_per_ch;
   me_ptr->threshold_data.aggregated_threshold_us =
      topo_samples_to_us(me_ptr->threshold_data.threshold_in_samples_per_channel,
                         me_ptr->threshold_data.threshold_port_sample_rate,
                         NULL);

   // we need to update nblc if threshold is updated.
   gen_topo_assign_non_buf_lin_chains(&me_ptr->topo.t_base);

   // we may have to reconfigure dm modes since some module may have become threshold module
   TRY(result, spl_topo_fwk_ext_update_dm_modes(&me_ptr->topo));

   // Aggregated threshold changing could change the container frame length if the aggregated threshold
   // is larger than the configured frame length. Frame length aggregation and handling if the frame length
   // changes is therefore needed.
   TRY(result, spl_cntr_determine_update_cntr_frame_len_us_from_cfg_and_thresh(me_ptr));

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "Container frame length: %lu samples, %lu us, Aggregated threshold: %lu samples, %lu us",
                me_ptr->cu.cntr_frame_len.frame_len_samples,
                me_ptr->topo.cntr_frame_len.frame_len_us,
                me_ptr->threshold_data.threshold_in_samples_per_channel,
                me_ptr->threshold_data.aggregated_threshold_us);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Aggregates configured frame length and aggregated threshold into the container frame length.
 * If the container frame length changes, updates appropriate fields, updates each module's number
 * of process call iterations, and triggers other threshold changed handling.
 */
ar_result_t spl_cntr_determine_update_cntr_frame_len_us_from_cfg_and_thresh(spl_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result                   = AR_EOK;
   uint32_t           new_cntr_frame_length_us = 0;
   bool_t             is_using_threshold       = (me_ptr->threshold_data.threshold_in_samples_per_channel > 0);
   icb_frame_length_t fm                       = { 0 };

   // If there is a threshold, then aggregate threshold and configured frame size. Otherwise
   // just take configured frame size.
   if (is_using_threshold)
   {
      // Assign sample rate and samples fields of icb structure.
      fm.frame_len_us      = me_ptr->threshold_data.aggregated_threshold_us;
      fm.sample_rate       = me_ptr->threshold_data.threshold_port_sample_rate;
      fm.frame_len_samples = me_ptr->threshold_data.threshold_in_samples_per_channel;
   }
   else
   {
      // If there is no threshold port then pick sampling rate of the first port (in fixed data path)
      // 1. if frame size in time then pick the fractional sample rate to adjust the container frame size according to
      // the fractional rate.
      // 2. if frame size in samples then just pick the first sample rate.
      fm.sample_rate = spl_cntr_get_fractional_sampling_rate_for_cntr_frame_len(me_ptr);
      if (0 == fm.sample_rate)
      {
         fm.sample_rate = 48000;
      }

      if (me_ptr->threshold_data.configured_frame_size_samples)
      {
         fm.frame_len_samples = me_ptr->threshold_data.configured_frame_size_samples;
      }
      else
      {
         // rounded samples.
         fm.frame_len_samples =
            ((uint64_t)me_ptr->threshold_data.configured_frame_size_us * fm.sample_rate) / (uint64_t)NUM_US_PER_SEC;
      }

      // rounded frame size in us
      fm.frame_len_us = topo_samples_to_us(fm.frame_len_samples, fm.sample_rate, NULL);
   }

   new_cntr_frame_length_us = fm.frame_len_us;

   // For voice stream PP's, the only supported configurations are ones which result in 20ms container
   // frame length.
   if (APM_CONT_GRAPH_POS_STREAM == me_ptr->cu.position && cu_has_voice_sid(&me_ptr->cu))
   {
      VERIFY(result, FRAME_LEN_20000_US == new_cntr_frame_length_us);
   }

   // Always assign the new container frame length. Sets the cu.cntr_frame_len fields. Won't send redundant icb
   // messages.
   spl_cntr_set_cntr_frame_len_us(me_ptr, &fm);

   // Handle buffer size changed.
   result = spl_cntr_handle_ext_buffer_size_change(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

ar_result_t spl_cntr_update_input_port_max_samples(gen_topo_t *topo_ptr, gen_topo_input_port_t *topo_inport_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result      = AR_EOK;
   uint32_t               topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t            *me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);
   spl_cntr_input_port_t *inport_ptr  = (spl_cntr_input_port_t *)topo_inport_ptr;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "DM update received for max samples, port index %lu, samples %lu",
                inport_ptr->topo.t_base.gu.cmn.index,
                inport_ptr->topo.req_samples_info_max.samples_in);
#endif

   spl_cntr_ext_in_port_t *ext_inport_ptr = (spl_cntr_ext_in_port_t *)inport_ptr->topo.t_base.gu.ext_in_port_ptr;
   VERIFY(result, ext_inport_ptr);
   if (ext_inport_ptr->max_process_samples < inport_ptr->topo.req_samples_info_max.samples_in)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "max samples new %ld greater than current %lu, reallocating",
                   inport_ptr->topo.req_samples_info_max.samples_in,
                   ext_inport_ptr->max_process_samples);
#endif

      ext_inport_ptr->max_process_samples = inport_ptr->topo.req_samples_info_max.samples_in;
      uint32_t buf_size                   = spl_cntr_calc_required_ext_in_buf_size(me_ptr, ext_inport_ptr);

      TRY(result, spl_cntr_check_resize_ext_in_buffer(me_ptr, ext_inport_ptr, buf_size));
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}
