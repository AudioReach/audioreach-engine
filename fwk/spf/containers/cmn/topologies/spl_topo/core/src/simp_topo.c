/**
 * \file simp_topo.c
 * \brief
 *     implementation for the simplified topo to be run only when in steady state.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "simp_topo_i.h"

// get the minimum required data to trigger process on an input port.
static uint32_t simp_topo_get_in_required_bytes_per_ch(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   spl_topo_module_t *module_ptr    = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
   uint32_t           required_data = 0;

   if (!module_ptr->t_base.flags.is_nblc_boundary_module)
   { // if module is in nblc chain then just one sample is sufficient to trigger process
      required_data = topo_samples_to_bytes_per_ch(1, in_port_ptr->t_base.common.media_fmt_ptr);
   }
   else if (GEN_TOPO_DM_FIXED_OUTPUT_MODE == module_ptr->t_base.flags.dm_mode)
   {
      required_data = topo_samples_to_bytes_per_ch(in_port_ptr->req_samples_info.samples_in,
                                                   in_port_ptr->t_base.common.media_fmt_ptr);
   }
   else if (module_ptr->threshold_data.is_threshold_module)
   {
      required_data = spl_topo_get_module_threshold_bytes_per_channel(&in_port_ptr->t_base.common, module_ptr);
   }
   else if (module_ptr->t_base.flags.need_sync_extn) // todo: make sync as threshold so extra check and division can be avoided
   {
      uint32_t req_samples = spl_topo_get_scaled_samples(topo_ptr,
                                                         topo_ptr->cntr_frame_len.frame_len_samples,
                                                         topo_ptr->cntr_frame_len.sample_rate,
                                                         in_port_ptr->t_base.common.media_fmt_ptr->pcm.sample_rate);

      required_data = topo_samples_to_bytes_per_ch(req_samples, in_port_ptr->t_base.common.media_fmt_ptr);
   }
   else
   { // ideally this case should never be executed.
      required_data = topo_samples_to_bytes_per_ch(1, in_port_ptr->t_base.common.media_fmt_ptr);
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "spl_topo mpd miid 0x%lx input port idx = %ld required bytes per channel %ld, ",
            module_ptr->t_base.gu.module_instance_id,
            in_port_ptr->t_base.gu.cmn.index,
            required_data);

#endif

   return required_data;
}

// Sets up the input port's stream data structure based on the connected output-port.
static ar_result_t simp_topo_setup_int_in_port_sdata(spl_topo_t *            topo_ptr,
                                                     spl_topo_input_port_t * in_port_ptr,
                                                     spl_topo_output_port_t *connected_out_port_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   if (connected_out_port_ptr->md_list_ptr)
   {
      // transfer md from output to input
      spf_list_merge_lists((spf_list_node_t **)&(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                           (spf_list_node_t **)&(connected_out_port_ptr->md_list_ptr));
   }

   DBG_VERIFY(result, connected_out_port_ptr->t_base.common.bufs_ptr[0].data_ptr);

   simp_topo_copy_sdata_flags(&(in_port_ptr->t_base.common.sdata), &(connected_out_port_ptr->t_base.common.sdata));
   simp_topo_clear_sdata_flags(&(connected_out_port_ptr->t_base.common.sdata));

   simp_topo_populate_int_input_sdata_bufs(topo_ptr, in_port_ptr, connected_out_port_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
   spl_topo_module_t *module_ptr = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;

   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Before process: internal input port idx = %ld, miid = 0x%lx actual data len 1 ch = %ld of %ld, num "
            "bufs %d dfg %d eos %ld eof %ld",
            in_port_ptr->t_base.gu.cmn.index,
            module_ptr->t_base.gu.module_instance_id,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len,
            in_port_ptr->t_base.common.sdata.bufs_num,
            gen_topo_md_list_has_dfg(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
            in_port_ptr->t_base.common.sdata.flags.marker_eos,
            in_port_ptr->t_base.common.sdata.flags.end_of_frame);

   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Before process: internal input port idx = %ld, miid = 0x%lx ts = %ld",
            in_port_ptr->t_base.gu.cmn.index,
            module_ptr->t_base.gu.module_instance_id,
            in_port_ptr->t_base.common.sdata.timestamp);
#endif

   DBG_CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

// Sets up the input port's stream data structure from the external buffer
ar_result_t simp_topo_setup_ext_in_port_sdata(spl_topo_t *topo_ptr, spl_topo_input_port_t *in_port_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING
   ar_result_t                 result           = AR_EOK;
   spl_topo_ext_buf_t *        ext_buf_ptr      = in_port_ptr->ext_in_buf_ptr;
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;
   capi_buf_t *scratch_bufs_ptr   = proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].bufs;
   uint64_t *  FRAC_TIME_PTR_NULL = NULL;

   // We can't consume more data than exists. Checking max_data_len due to pushing zeros.
   DBG_VERIFY(result, ext_buf_ptr->bytes_consumed_per_ch <= ext_buf_ptr->buf_ptr[0].max_data_len);

   // If the topo has already consumed some data, we need to adjust the sdata buffer pointers to
   // the start of unconsumed data. This happens in the case of multiple threshold modules with
   // different thresholds (module w/ smaller threshold needs to get called multiple times).

   // clang-format off
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
   for (uint32_t buf_idx = 0; buf_idx < ext_buf_ptr->num_bufs; buf_idx++)
   {
      scratch_bufs_ptr[buf_idx].data_ptr = ext_buf_ptr->buf_ptr[buf_idx].data_ptr + ext_buf_ptr->bytes_consumed_per_ch;
      scratch_bufs_ptr[buf_idx].actual_data_len = ext_buf_ptr->buf_ptr[buf_idx].actual_data_len - ext_buf_ptr->bytes_consumed_per_ch;
      scratch_bufs_ptr[buf_idx].max_data_len = ext_buf_ptr->buf_ptr[buf_idx].max_data_len - ext_buf_ptr->bytes_consumed_per_ch;
   }
#else
   for (uint32_t buf_idx = 0; buf_idx < ext_buf_ptr->num_bufs; buf_idx++)
   {
      scratch_bufs_ptr[buf_idx].data_ptr = ext_buf_ptr->buf_ptr[buf_idx].data_ptr + ext_buf_ptr->bytes_consumed_per_ch;
   }
   scratch_bufs_ptr[0].actual_data_len = ext_buf_ptr->buf_ptr[0].actual_data_len - ext_buf_ptr->bytes_consumed_per_ch;
   scratch_bufs_ptr[0].max_data_len    = ext_buf_ptr->buf_ptr[0].max_data_len - ext_buf_ptr->bytes_consumed_per_ch;
#endif
   // clang-format on

   in_port_ptr->t_base.common.sdata.buf_ptr  = scratch_bufs_ptr;
   in_port_ptr->t_base.common.sdata.bufs_num = ext_buf_ptr->num_bufs;

   if (ext_buf_ptr->timestamp_is_valid)
   {
      // Shift the timestamp by the amount of bytes consumed per channel.
      in_port_ptr->t_base.common.sdata.timestamp =
         ext_buf_ptr->timestamp + topo_bytes_per_ch_to_us(ext_buf_ptr->bytes_consumed_per_ch,
                                                          in_port_ptr->t_base.common.media_fmt_ptr,
                                                          FRAC_TIME_PTR_NULL);
   }

   in_port_ptr->t_base.common.sdata.flags.is_timestamp_valid  = ext_buf_ptr->timestamp_is_valid;
   in_port_ptr->t_base.common.sdata.flags.stream_data_version = CAPI_STREAM_V2;

   // Since we moved the data ptr, we also have to adjust the md offsets.
   if (in_port_ptr->t_base.common.sdata.metadata_list_ptr && ext_buf_ptr->bytes_consumed_per_ch)
   {
      bool_t SUBTRACT_FALSE = FALSE;
      gen_topo_metadata_adj_offset(&(topo_ptr->t_base),
                                   in_port_ptr->t_base.common.media_fmt_ptr,
                                   in_port_ptr->t_base.common.sdata.metadata_list_ptr,
                                   ext_buf_ptr->bytes_consumed_per_ch * ext_buf_ptr->num_bufs,
                                   SUBTRACT_FALSE);
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Before process: external input port idx = %ld, miid = 0x%lx len of 1 ch: %ld of %ld, num bufs %d",
            in_port_ptr->t_base.gu.cmn.index,
            in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            ext_buf_ptr->buf_ptr->max_data_len,
            ext_buf_ptr->num_bufs);
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Before process: external input port idx = %ld, miid = 0x%lx eos %d, dfg %d, eof %ld, metadata_list_ptr "
            "0x%lx, ts %ld, ts_is_valid %ld",
            in_port_ptr->t_base.gu.cmn.index,
            in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->t_base.common.sdata.flags.marker_eos,
            gen_topo_md_list_has_dfg(in_port_ptr->t_base.common.sdata.metadata_list_ptr),
            in_port_ptr->t_base.common.sdata.flags.end_of_frame,
            in_port_ptr->t_base.common.sdata.metadata_list_ptr,
            (int32_t)in_port_ptr->t_base.common.sdata.timestamp,
            in_port_ptr->t_base.common.sdata.flags.is_timestamp_valid);
#endif

   DBG_CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Sets up the input port related fields of the process context for a particular input port.
 * It also check if input port is process ready or not.
 */
static ar_result_t simp_topo_setup_proc_ctx_in_port(spl_topo_t *           topo_ptr,
                                                    spl_topo_input_port_t *in_port_ptr,
                                                    bool_t *               can_process_ptr)
{
   DBG_INIT_EXCEPTION_HANDLING

   ar_result_t                 result           = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;

   uint32_t required_bytes_per_ch = simp_topo_get_in_required_bytes_per_ch(topo_ptr, in_port_ptr);

   if (in_port_ptr->simp_conn_out_port_t)
   { // for internal port, get the data from the previous output port
      spl_topo_output_port_t *connected_out_port_ptr = in_port_ptr->simp_conn_out_port_t;
      uint32_t                data_available_per_ch = connected_out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len;

      if (data_available_per_ch < required_bytes_per_ch)
      {
         *can_process_ptr = FALSE;
         return result;
      }

      // Connected port is internal cases.
      result = simp_topo_setup_int_in_port_sdata(topo_ptr, in_port_ptr, connected_out_port_ptr);
      DBG_VERIFY(result, AR_SUCCEEDED(result));
   }
   else
   { // for external port, get the data from the external buffer.

      uint32_t data_available_per_ch =
         in_port_ptr->ext_in_buf_ptr->buf_ptr[0].actual_data_len - in_port_ptr->ext_in_buf_ptr->bytes_consumed_per_ch;

      if (data_available_per_ch < required_bytes_per_ch)
      {
         *can_process_ptr = FALSE;
         return result;
      }

      // Connected port is an external buffer case.
      result = simp_topo_setup_ext_in_port_sdata(topo_ptr, in_port_ptr);
      DBG_VERIFY(result, AR_SUCCEEDED(result));
   }

   // Set previous actual data len, so we can determine if all input data was consumed after process.
   // All actual data lengths should be equal, so using first channel is enough.
   proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0] =
      in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;
   proc_context_ptr->in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index] = &in_port_ptr->t_base.common.sdata;

   if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == in_port_ptr->t_base.common.flags.is_pcm_unpacked) &&
       ((gen_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr)->capi_ptr)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: input port idx = %ld, miid = 0x%lx setting up sdata for unpacked V1",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      capi_stream_data_v2_t *in_port_sdata_ptr = proc_context_ptr->in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index];
      for (uint32_t i = 1; i < in_port_ptr->t_base.common.sdata.bufs_num; i++)
      {
         in_port_sdata_ptr->buf_ptr[i].actual_data_len = in_port_sdata_ptr->buf_ptr[0].actual_data_len;
         in_port_sdata_ptr->buf_ptr[i].max_data_len    = in_port_sdata_ptr->buf_ptr[0].max_data_len;
      }
   }

   DBG_CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Setup sdata bufs before process. For output, should point past any existing data in buffers.
 * - in SPL_CNTR only deinterleaved unpacked is accepted.
 * - in SPL_CNTR for input ports, buffers live in the connected output ports, so use the connected output
 *   ports to adjust ptrs and then transfer over to the input port.
 */
static inline ar_result_t simp_topo_populate_int_output_sdata_bufs(spl_topo_t *            topo_ptr,
                                                                   spl_topo_output_port_t *out_port_ptr)

{
   ar_result_t result = AR_EOK;

#if SAFE_MODE
   // Verifying mf from either the output port itself or the input port's connected output port.
   if (!gen_topo_is_pcm_unpacked_v2(&in_port_ptr->t_base.common))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)&out_port_ptr->t_base.gu.cmn.module_ptr;
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               "Failed in setting up the sdata for module 0x%x. Media format is not PCM deinterleaved.",
               module_ptr->gu.module_instance_id);
      return AR_EFAILED;
   }
#endif

   // For EOS handling in SC, lengths get messed up if same sdata.buf is used. virt_nt_8.
   gen_topo_process_context_t *pc_ptr = &topo_ptr->t_base.proc_context;
   capi_buf_t                 *bufs   = pc_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].bufs;

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
   for (uint32_t b = 0; b < out_port_ptr->t_base.common.sdata.bufs_num; b++)
   {
      bufs[b].data_ptr =
         out_port_ptr->t_base.common.bufs_ptr[b].data_ptr + out_port_ptr->t_base.common.bufs_ptr[b].actual_data_len;
      bufs[b].actual_data_len = 0;
      bufs[b].max_data_len =
         out_port_ptr->t_base.common.bufs_ptr[b].max_data_len - out_port_ptr->t_base.common.bufs_ptr[b].actual_data_len;
   }
#else
   for (uint32_t b = 0; b < out_port_ptr->t_base.common.sdata.bufs_num; b++)
   {
      bufs[b].data_ptr =
         out_port_ptr->t_base.common.bufs_ptr[b].data_ptr + out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len;
   }
   bufs[0].actual_data_len = 0;
   bufs[0].max_data_len =
      out_port_ptr->t_base.common.bufs_ptr[0].max_data_len - out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len;
#endif

   out_port_ptr->t_base.common.sdata.buf_ptr = bufs;

   return result;
}

/*
 * Manages local output port buffers either by adjusting offsets for an existing partially filled buffer
 * or allocating a new buffer from buffer manager. Initializes sdata for port.
 */
static ar_result_t simp_topo_setup_int_out_port_sdata(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                   result = AR_EOK;
   gen_topo_port_scratch_data_t *out_scratch_ptr =
      &(topo_ptr->t_base.proc_context.out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index]);

   int8_t *orig_buf_ptr = (int8_t *)out_port_ptr->t_base.common.bufs_ptr[0].data_ptr;

   // Get a buffer from the buffer manager if we aren't holding one.
   if (!orig_buf_ptr)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Topo getting output buffer, port index %lu, miid 0x%lx",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      TRY(result, spl_topo_get_output_port_buffer(topo_ptr, out_port_ptr));
   }

   simp_topo_populate_int_output_sdata_bufs(topo_ptr, out_port_ptr);

   // Cache the original timestamp of this output port.
   if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
   {
      out_scratch_ptr->timestamp = out_port_ptr->t_base.common.sdata.timestamp;
   }
   out_scratch_ptr->flags.is_timestamp_valid = out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid;
#ifdef SAFE_MODE
   for (uint32_t b = 0; b < out_port_ptr->t_base.common.sdata.bufs_num; b++)
   {
      out_scratch_ptr->prev_actual_data_len[b] = out_port_ptr->t_base.common.bufs_ptr[b].actual_data_len;
   }
#else
   // only zeroth element used in SC
   out_scratch_ptr->prev_actual_data_len[0] = out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len;
#endif
   out_port_ptr->t_base.common.sdata.flags.stream_data_version = CAPI_STREAM_V2;

   // sdata fields besides buf_ptrs and num_bufs are not initialized here. They will
   // be set by capiv2 process(), or copied over by the fwk in case of single input
   // single output.

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Sets up the output port's stream data structure based on the spl_topo_ext_buf.
 */
static void simp_topo_setup_ext_out_port_sdata(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   gen_topo_process_context_t *pc_ptr           = &(topo_ptr->t_base.proc_context);
   capi_buf_t *                scratch_bufs_ptr = pc_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].bufs;

   spl_topo_ext_buf_t *ext_buf_ptr = NULL;

   ext_buf_ptr = (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
      out_port_ptr->t_base.gu.ext_out_port_ptr);

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
   for (uint32_t i = 0; i < ext_buf_ptr->num_bufs; i++)
   {
      capi_buf_t *cur_buf_ptr = &scratch_bufs_ptr[i];

      // Set up sdata buffers to point to first unused data.
      cur_buf_ptr->data_ptr        = ext_buf_ptr->buf_ptr[i].data_ptr + ext_buf_ptr->buf_ptr[i].actual_data_len;
      cur_buf_ptr->actual_data_len = 0;
      cur_buf_ptr->max_data_len    = ext_buf_ptr->buf_ptr[i].max_data_len -  ext_buf_ptr->buf_ptr[i].actual_data_len;
   }
#else

   uint32_t ext_actual_data_len_per_buf = ext_buf_ptr->buf_ptr[0].actual_data_len;
   uint32_t ext_max_data_len_per_buf    = ext_buf_ptr->buf_ptr[0].max_data_len;
   for (uint32_t i = 0; i < ext_buf_ptr->num_bufs; i++)
   {
      scratch_bufs_ptr[i].data_ptr = ext_buf_ptr->buf_ptr[i].data_ptr + ext_actual_data_len_per_buf;
   }
   scratch_bufs_ptr[0].actual_data_len = 0;
   scratch_bufs_ptr[0].max_data_len    = ext_max_data_len_per_buf - ext_actual_data_len_per_buf;
#endif

   pc_ptr->out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0] =
      ext_buf_ptr->buf_ptr[0].actual_data_len;

   out_port_ptr->t_base.common.sdata.buf_ptr                   = scratch_bufs_ptr;
   out_port_ptr->t_base.common.sdata.flags.stream_data_version = CAPI_STREAM_V2;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Before process: external output port idx %ld miid 0x%lx actual data len 1 ch = %ld of %ld, num bufs %d "
            "eof %ld",
            out_port_ptr->t_base.gu.cmn.index,
            out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            ext_buf_ptr->buf_ptr[0].actual_data_len,
            ext_buf_ptr->buf_ptr[0].max_data_len,
            ext_buf_ptr->num_bufs,
            out_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

   // Cache the original timestamp of this output port.
   if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
   {
      topo_ptr->t_base.proc_context.out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].timestamp =
         out_port_ptr->t_base.common.sdata.timestamp;
   }
   topo_ptr->t_base.proc_context.out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].flags.is_timestamp_valid =
      out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid;
}

/**
 * Sets up the output port related fields of the process context for a particular output port.
 */
ar_result_t simp_topo_setup_proc_ctx_out_port(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // Update the output port's sdata fields. Then sets the process context's sdata_ptr to point to
   // the output port's sdata fields.
   gen_topo_process_context_t *proc_context_ptr = &topo_ptr->t_base.proc_context;

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      simp_topo_setup_ext_out_port_sdata(topo_ptr, out_port_ptr);
   }
   else
   {
      TRY(result, simp_topo_setup_int_out_port_sdata(topo_ptr, out_port_ptr));

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: internal output port idx %ld miid 0x%lx actual data len 1 ch = %ld of %ld, num bufs "
               "%d eof %ld",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
               out_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len,
               out_port_ptr->t_base.common.sdata.bufs_num,
               out_port_ptr->t_base.common.sdata.flags.end_of_frame);
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: internal output port idx %ld miid 0x%lx timestamp %ld",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->t_base.common.sdata.timestamp);
#endif
   }

   proc_context_ptr->out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index] = &out_port_ptr->t_base.common.sdata;

   if ((GEN_TOPO_MF_PCM_UNPACKED_V1 == out_port_ptr->t_base.common.flags.is_pcm_unpacked) &&
       ((gen_topo_module_t *)out_port_ptr->t_base.gu.cmn.module_ptr)->capi_ptr)
   {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Before process: output port idx = %ld, miid = 0x%lx setting up sdata for unpacked V1",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif

      capi_stream_data_v2_t *out_port_sdata_ptr =
         proc_context_ptr->out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index];
      for (uint32_t i = 1; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
      {
         out_port_sdata_ptr->buf_ptr[i].actual_data_len = out_port_sdata_ptr->buf_ptr[0].actual_data_len;
         out_port_sdata_ptr->buf_ptr[i].max_data_len    = out_port_sdata_ptr->buf_ptr[0].max_data_len;
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Adjust an interal input buffer after spl_topo_process(). Returns the input buffer if it is fully
 * consumed, otherwise removes any consumed data from the buffer.
 */
ar_result_t simp_topo_adjust_int_in_buf(spl_topo_t *            topo_ptr,
                                        spl_topo_input_port_t * in_port_ptr,
                                        spl_topo_output_port_t *connected_out_port_ptr)
{
   ar_result_t                 result               = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr     = &topo_ptr->t_base.proc_context;
   uint32_t                    consumed_data_per_ch = 0;
   capi_stream_data_v2_t *     in_sdata_ptr         = &in_port_ptr->t_base.common.sdata;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "After process: Internal input port idx %ld miid 0x%lx buffer actual data len = %ld of %ld, num bufs %d "
            "eos %ld eof %ld",
            in_port_ptr->t_base.gu.cmn.index,
            in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].max_data_len,
            in_port_ptr->t_base.common.sdata.bufs_num,
            in_port_ptr->t_base.common.sdata.flags.marker_eos,
            in_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

   consumed_data_per_ch = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len; // only deint-unpacked supported.

   if (consumed_data_per_ch ==
       proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0])
   {
      // Return buffers to the buffer manager.
      spl_topo_return_output_buf(topo_ptr, connected_out_port_ptr);

      // if buffer is returned then clear the timestamp
      in_sdata_ptr->flags.is_timestamp_valid = false;
      in_sdata_ptr->timestamp                = 0;
   }
   else
   {
      // adjust the timestamp
      if (in_sdata_ptr->flags.is_timestamp_valid)
      {
         uint64_t *FRAC_TIME_PTR_NULL = NULL;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Timestamp before adjust: ts = %lld, is_valid = %ld",
                  in_sdata_ptr->timestamp,
                  in_sdata_ptr->flags.is_timestamp_valid);
#endif

         in_sdata_ptr->timestamp +=
            topo_bytes_per_ch_to_us(consumed_data_per_ch, in_port_ptr->t_base.common.media_fmt_ptr, FRAC_TIME_PTR_NULL);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Timestamp after adjust: ts = %lld, is_valid = %ld",
                  in_sdata_ptr->timestamp,
                  in_sdata_ptr->flags.is_timestamp_valid);
#endif
      }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Input port idx %ld miid 0x%lx buffer was partially consumed (consumed %ld of %ld) per ch",
               in_port_ptr->t_base.gu.cmn.index,
               in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               consumed_data_per_ch,
               proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0]);
#endif

      if (gen_topo_buf_mgr_wrapper_get_ref_count(&connected_out_port_ptr->t_base.common) > 1)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Inplace module 0x%lx didnt consume all data. This will cause data corruption!!. Bug in Capi!!",
                  in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
         // this causes data corruption. moving remaining data to beginning causes overwriting output (for inplace)
      }
      // The input buffer was partially consumed. To make space for new
      // output, we need to remove any consumed data from the buffer.
      spl_topo_adjust_buf(topo_ptr,
                          connected_out_port_ptr->t_base.common.bufs_ptr,
                          &in_port_ptr->t_base.common.sdata,
                          consumed_data_per_ch,
                          in_port_ptr->t_base.common.media_fmt_ptr);
   }

   return result;
}

/**
 * Adjust an external input buffer after spl_topo_process(). Sets the bytes_consumed_per_ch
 * field (used by fwk layer) according to the actual_data_len field.
 */
ar_result_t simp_topo_adjust_ext_in_buf(spl_topo_t *           topo_ptr,
                                        spl_topo_input_port_t *in_port_ptr,
                                        spl_topo_ext_buf_t *   ext_buf_ptr)
{
   ar_result_t result = AR_EOK;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   spl_topo_module_t *module_ptr = (spl_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr;
#endif

   // Since we moved the data ptr, we also have to readjust the md offsets. Do this before changing
   // bytes_consumed_per_ch or we lose our reference amount.
   if (in_port_ptr->t_base.common.sdata.metadata_list_ptr && ext_buf_ptr->bytes_consumed_per_ch)
   {
      bool_t ADD_TRUE = TRUE;
      gen_topo_metadata_adj_offset(&(topo_ptr->t_base),
                                   in_port_ptr->t_base.common.media_fmt_ptr,
                                   in_port_ptr->t_base.common.sdata.metadata_list_ptr,
                                   ext_buf_ptr->bytes_consumed_per_ch * ext_buf_ptr->num_bufs,
                                   ADD_TRUE);
   }

   // In case of flushing zeroes and partially consumed data, framework memsmove zeroes when it does not have to.
   ext_buf_ptr->bytes_consumed_per_ch += in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "After process: external input idx %ld miid 0x%lx buffer bytes_consumed_per_ch = %ld, actual_data_len = "
            "%ld eof %ld",
            in_port_ptr->t_base.gu.cmn.index,
            module_ptr->t_base.gu.module_instance_id,
            ext_buf_ptr->bytes_consumed_per_ch,
            in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len,
            in_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

   return result;
}

/**
 * Adjust an input port's buffer after process based on data consumed. Return the buffer
 * to the topo buffer manager if it is empty. Adjust the connected output buffer
 * so it reflects the adjustments in the input buffer. Different cases are required for internal and
 * external ports.
 */
static ar_result_t simp_topo_adjust_in_buf(spl_topo_t *           topo_ptr,
                                           spl_topo_input_port_t *in_port_ptr,
                                           bool_t *               data_was_consumed_ptr)
{
   ar_result_t                 result           = AR_EOK;

   // Check if data was consumed is same for all types of input ports: nonzero actual data len.
   *data_was_consumed_ptr = (0 != in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len);

   if (in_port_ptr->simp_conn_out_port_t)
   {
      // Connected port is internal cases.
      simp_topo_adjust_int_in_buf(topo_ptr, in_port_ptr, in_port_ptr->simp_conn_out_port_t);

      // Send sdata (includes timestamps, but not metadata) back to the connected output port.
      simp_topo_copy_sdata_flags(&(in_port_ptr->simp_conn_out_port_t->t_base.common.sdata),
                                 &(in_port_ptr->t_base.common.sdata));
   }
   else
   {
      // Connected port is external cases -> setup buffer.
      simp_topo_adjust_ext_in_buf(topo_ptr, in_port_ptr, in_port_ptr->ext_in_buf_ptr);
   }

   // In Spl topo bufs_ptr, and bufs_num are temporary for input.
   in_port_ptr->t_base.common.bufs_ptr       = NULL;
   in_port_ptr->t_base.common.sdata.bufs_num = 0;

   return result;
}

/**
 * Adjust an internal output buffer after process based on data consumed.
 */
void simp_topo_adjust_int_out_buf(spl_topo_t *topo_ptr, spl_topo_output_port_t *out_port_ptr)
{
   bool_t IS_EXT_OP_FALSE = FALSE;

   uint32_t prev_actual_data_len_all_ch =
      topo_ptr->t_base.proc_context.out_port_scratch_ptr[out_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0] *
      out_port_ptr->t_base.common.sdata.bufs_num;

   if (out_port_ptr->t_base.common.bufs_ptr[0].data_ptr)
   {

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      for (uint32_t b = 0; b < out_port_ptr->t_base.common.sdata.bufs_num; b++)
      {
         out_port_ptr->t_base.common.bufs_ptr[b].actual_data_len +=
            out_port_ptr->t_base.common.sdata.buf_ptr[b].actual_data_len;
      }
#else
      out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len +=
         out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3_FORCE
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "After process: internal output port idx = %ld miid = 0x%x actual len: %ld of %ld (per buf), "
               "num_channels %ld dfg %ld eos %ld eof %ld",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len,
               out_port_ptr->t_base.common.bufs_ptr[0].max_data_len,
               out_port_ptr->t_base.common.media_fmt_ptr->pcm.num_channels,
               gen_topo_md_list_has_dfg(out_port_ptr->t_base.common.sdata.metadata_list_ptr),
               out_port_ptr->t_base.common.sdata.flags.marker_eos,
               out_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif

      if (0 == out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len)
      {
         // Return the output buffer if the output buffer is empty (means no
         // output was produced and no output was present before processing).
         // This will most likely happen during backwards kick, when sending
         // a dummy input buffer.
         spl_topo_return_output_buf(topo_ptr, out_port_ptr);
      }

      if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
      {
         // Timestamp handling. Checks to keep or replace timestamp in the output buffer. Also checks
         // for timestamp discontinuity.
         spl_topo_handle_internal_timestamp_discontinuity(topo_ptr,
                                                          out_port_ptr,
                                                          prev_actual_data_len_all_ch,
                                                          IS_EXT_OP_FALSE);
      }

      // Set EOF
      out_port_ptr->t_base.common.sdata.flags.end_of_frame |= out_port_ptr->flags.ts_disc;

      // end of frame should be set to the topo connected input port
      spl_topo_input_port_t *conn_in_port_ptr = (spl_topo_input_port_t *)out_port_ptr->t_base.gu.conn_in_port_ptr;
      conn_in_port_ptr->t_base.common.sdata.flags.end_of_frame = out_port_ptr->t_base.common.sdata.flags.end_of_frame;

      return;
   }
}

/**
 * Adjust an external output buffer after process based on data consumed.
 */
ar_result_t simp_topo_adjust_ext_out_buf(spl_topo_t *            topo_ptr,
                                         spl_topo_output_port_t *out_port_ptr,
                                         spl_topo_ext_buf_t *    ext_buf_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result                      = AR_EOK;
   uint32_t    prev_actual_data_len_all_ch = ext_buf_ptr->buf_ptr[0].actual_data_len * ext_buf_ptr->num_bufs;
   bool_t      IS_EXT_OP_TRUE              = TRUE;

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
   for (uint32_t i = 0; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
   {
      ext_buf_ptr->buf_ptr[i].actual_data_len += out_port_ptr->t_base.common.sdata.buf_ptr[i].actual_data_len;
#else
   {
      // optimization: update only first buffer and avoids for loop
      uint32_t i = 0;
      ext_buf_ptr->buf_ptr[i].actual_data_len += out_port_ptr->t_base.common.sdata.buf_ptr[i].actual_data_len;
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "After process: external output port idx = %ld miid = 0x%x actual len: %d of %ld, for channel %d, num "
               "bufs %d",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               ext_buf_ptr->buf_ptr[i].actual_data_len,
               ext_buf_ptr->buf_ptr[i].max_data_len,
               i,
               ext_buf_ptr->num_bufs);
#endif

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "After process: external output port idx = %ld miid = 0x%x eos_marker %d dfg %d eof %ld",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->t_base.common.sdata.flags.marker_eos,
               gen_topo_md_list_has_dfg(out_port_ptr->t_base.common.sdata.metadata_list_ptr),
               out_port_ptr->t_base.common.sdata.flags.end_of_frame);
#endif
   }

   if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
   {
      spl_topo_handle_internal_timestamp_discontinuity(topo_ptr,
                                                       out_port_ptr,
                                                       prev_actual_data_len_all_ch,
                                                       IS_EXT_OP_TRUE);

      // If ts_disc was detected, we need to send all data before the ts_discontinuity immediately but we shouldn't
      // send the data after the discontinuity. So we query a topo buf to hold the data after the ts disc. This buffer
      // stored in the actual external port's internal port since that isn't being used for any other purpose.
      //
      // Presence of an internal buffer at the actual external port is later used to force buffer delivery. And the
      // next time we pop an external output buffer, we check if we have any pending data in the internal buffer and
      // if so, we copy that to the external buffer and deliver it immediately.
      //
      // If there's no data, then there's no reason to do this transfer.
      if (out_port_ptr->flags.ts_disc)
      {
         TRY(result, spl_topo_transfer_data_to_ts_temp_buf(topo_ptr, out_port_ptr));
      }
   }

   // Transfer ts_disc info to the external port and clear from the internal port.
   ext_buf_ptr->timestamp_is_valid      = out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid;
   ext_buf_ptr->timestamp_discontinuity = out_port_ptr->flags.ts_disc;
   ext_buf_ptr->ts_disc_pos_bytes       = out_port_ptr->ts_disc_pos_bytes;
   ext_buf_ptr->disc_timestamp          = out_port_ptr->disc_timestamp;

   spl_topo_clear_output_timestamp_discontinuity(topo_ptr, out_port_ptr);

   // Transfer EOF to external port structure.
   if (out_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      ext_buf_ptr->end_of_frame                            = out_port_ptr->t_base.common.sdata.flags.end_of_frame;
      out_port_ptr->t_base.common.sdata.flags.end_of_frame = FALSE;
   }

   if (out_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
   {
      ext_buf_ptr->timestamp = out_port_ptr->t_base.common.sdata.timestamp;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Writing timestamp from topo to external out port idx = %ld miid = 0x%x: ts = %lld, is_valid = %ld",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id,
               ext_buf_ptr->timestamp,
               ext_buf_ptr->timestamp_is_valid);
#endif
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }
   return result;
}

void simp_topo_adjust_out_buf_md(spl_topo_t *            topo_ptr,
                                 spl_topo_output_port_t *out_port_ptr,
                                 uint32_t                amount_data_generated)
{
   bool_t new_flushing_eos_arrived = FALSE;
   if (out_port_ptr->t_base.common.sdata.metadata_list_ptr)
   {
      // A new flushing eos arrived if it's in the output port's sdata's md list. Md generated from earlier process
      // calls lives directly in spl_topo_output_port->md_list_ptr.
      new_flushing_eos_arrived =
         out_port_ptr->t_base.common.sdata.metadata_list_ptr
            ? gen_topo_md_list_has_flushing_eos(out_port_ptr->t_base.common.sdata.metadata_list_ptr)
            : FALSE;

      // Transfer any remaining md back from the output sdata md list to the output port md list.
      spl_topo_transfer_md_from_out_sdata_to_out_port(topo_ptr, out_port_ptr);
   }

   // if the output or connected input doesn't have flushing EOS or DFG then no adjustment is required.
   if (out_port_ptr->t_base.common.sdata.flags.end_of_frame)
   {
      // Check for to remove existing DFG metadata/internal EOS, and to move existing flushing EOS to nonflushing.
      spl_topo_op_modify_md_when_new_data_arrives(topo_ptr,
                                                  out_port_ptr,
                                                  amount_data_generated,
                                                  new_flushing_eos_arrived);
   }
}
/**
 * Adjust an output port's buffer after process based on data consumed. Different cases are required for
 * internal/external ports.
 */
static void simp_topo_adjust_out_buf(spl_topo_t *            topo_ptr,
                                     spl_topo_output_port_t *out_port_ptr,
                                     bool_t *                data_is_generated)
{
   uint32_t amount_data_generated = 0;
   bool_t   is_end_of_frame       = FALSE;

   amount_data_generated = out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

   // Check if data was generated is same for all types of output ports: nonzero actual data len.
   *data_is_generated = (0 != amount_data_generated);

   if (out_port_ptr->t_base.gu.ext_out_port_ptr)
   {
      // External port case.
      spl_topo_ext_buf_t *ext_out_buf_ptr =
         (spl_topo_ext_buf_t *)topo_ptr->t_base.topo_to_cntr_vtable_ptr->ext_out_port_get_ext_buf(
            out_port_ptr->t_base.gu.ext_out_port_ptr);
      simp_topo_adjust_ext_out_buf(topo_ptr, out_port_ptr, ext_out_buf_ptr);

      is_end_of_frame |= (ext_out_buf_ptr->timestamp_discontinuity || ext_out_buf_ptr->end_of_frame);
   }
   else
   {
      // Internal port case.
      simp_topo_adjust_int_out_buf(topo_ptr, out_port_ptr);

      is_end_of_frame |= out_port_ptr->t_base.common.sdata.flags.end_of_frame;
   }

   simp_topo_adjust_out_buf_md(topo_ptr, out_port_ptr, amount_data_generated);

   if (is_end_of_frame)
   {
      topo_ptr->simpt_event_flags.check_eof = TRUE;
#ifdef SPL_SIPT_DBG
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Output port idx = %ld, miid = 0x%lx check_eof becomes TRUE.",
               out_port_ptr->t_base.gu.cmn.index,
               out_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
   }
}

static ar_result_t simp_topo_process_attached_modules(spl_topo_t *topo_ptr, spl_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; out_port_list_ptr;
        LIST_ADVANCE(out_port_list_ptr))
   {
      spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      //continue if port is inactive/there is no attached module on it
      if (out_port_ptr->flags.port_inactive || !out_port_ptr->simp_attached_module_list_ptr)
      {
         continue;
      }

      // We only pass the attached module's output port index stream data to the attached module. Elementary modules
      // are always SISO so they expect to have only a single sdata in the stream data array for inputs and outputs.
      uint32_t               out_port_idx        = out_port_ptr->t_base.gu.cmn.index;
      capi_stream_data_v2_t *out_port_sdata_ptr = topo_ptr->t_base.proc_context.out_port_sdata_pptr[out_port_idx];

      if ((0 == out_port_sdata_ptr->buf_ptr[0].actual_data_len) && (NULL == out_port_sdata_ptr->metadata_list_ptr))
      {
         continue;
      }

      for (gu_module_list_t *module_list_ptr = out_port_ptr->simp_attached_module_list_ptr; module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         capi_err_t attached_proc_result = CAPI_EOK;

         spl_topo_module_t *out_attached_module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Before process elementary module miid 0x%lx for output port idx %ld id 0x%lx",
                  out_attached_module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id);
#endif

         // Before calling capi process, for unpacked V1 update lens for all the channels
         gen_topo_input_port_t *attached_mod_ip_port_ptr =
            (gen_topo_input_port_t *)out_attached_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
         if (GEN_TOPO_MF_PCM_UNPACKED_V1 == attached_mod_ip_port_ptr->common.flags.is_pcm_unpacked)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Before attached process: output port idx = %ld, attached miid = 0x%lx setting up sdata for "
                     "unpacked V1",
                     out_port_ptr->t_base.gu.cmn.index,
                     out_attached_module_ptr->t_base.gu.module_instance_id);
#endif

            for (uint32_t i = 1; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
            {
               out_port_sdata_ptr->buf_ptr[i].actual_data_len = out_port_sdata_ptr->buf_ptr[0].actual_data_len;
               out_port_sdata_ptr->buf_ptr[i].max_data_len    = out_port_sdata_ptr->buf_ptr[0].max_data_len;
            }
         }

         simp_topo_set_process_begin(topo_ptr);

         // clang-format off
         IRM_PROFILE_MOD_PROCESS_SECTION(out_attached_module_ptr->t_base.prof_info_ptr, topo_ptr->t_base.gu.prof_mutex,
         attached_proc_result =
            out_attached_module_ptr->t_base.capi_ptr->vtbl_ptr->process(out_attached_module_ptr->t_base.capi_ptr,
                                                                        (capi_stream_data_t **)&out_port_sdata_ptr,
                                                                        (capi_stream_data_t **)&out_port_sdata_ptr);
         );
         // clang-format on

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4

        TOPO_MSG(topo_ptr->t_base.gu.log_id,DBG_LOW_PRIO,"M_iid 0x%lX output ts_valid - %d , TS[MSW, LSW] - [%d, %d]",
                out_attached_module_ptr->t_base.gu.module_instance_id,
                out_port_sdata_ptr[out_port_idx].flags.is_timestamp_valid,
                (uint32_t )(out_port_sdata_ptr[out_port_idx].timestamp >>32),
                (uint32_t )out_port_sdata_ptr[out_port_idx].timestamp);
#endif

         simp_topo_set_process_end(topo_ptr);

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "After process elementary module miid 0x%lx for output port idx %ld id 0x%lx",
                  out_attached_module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->t_base.gu.cmn.index,
                  module_ptr->t_base.gu.module_instance_id);
#endif

         // Don't ignore need more for attached modules.
         if (CAPI_FAILED(attached_proc_result))
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_ERROR_PRIO,
                     "elementary module miid 0x%lx for output port idx %ld id 0x%lx returned error 0x%lx during "
                     "process",
                     out_attached_module_ptr->t_base.gu.module_instance_id,
                     out_port_ptr->t_base.gu.cmn.index,
                     module_ptr->t_base.gu.module_instance_id,
                     attached_proc_result);
            return AR_EFAILED;
         }
      }
   }

   return result;
}

/* Internal port connections are different for SIMP topo and SPL topo.
 * If we are breaking from simp-topo therefore need to ensure that intenral uncosumed buffers are allocated to the right
 * output ports for simp topo processing.*/
static inline void simp_topo_update_internal_buffers_for_spl_topo_process(spl_topo_t *topo_ptr)
{

   for (gu_module_list_t *current_list_ptr = topo_ptr->simpt_sorted_module_list_ptr; current_list_ptr;
        LIST_ADVANCE(current_list_ptr))
   {
      spl_topo_module_t *curr_module_ptr = (spl_topo_module_t *)current_list_ptr->module_ptr;

      // loop through each module output port in simpt_sorted_module_list to check if any unconsumed data/md
      for (gu_output_port_list_t *out_port_list_ptr = curr_module_ptr->t_base.gu.output_port_list_ptr;
           out_port_list_ptr;
           LIST_ADVANCE(out_port_list_ptr))
      {
         spl_topo_output_port_t *out_port_ptr     = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         spl_topo_output_port_t *dst_out_port_ptr = NULL;

         // if internal connection is same for simp and spl topo then no action required.
         if (out_port_ptr->flags.port_inactive || !out_port_ptr->t_base.gu.conn_in_port_ptr ||
             !out_port_ptr->simp_conn_in_port_t ||
             (out_port_ptr->t_base.gu.conn_in_port_ptr == (gu_input_port_t *)out_port_ptr->simp_conn_in_port_t))
         {
            continue;
         }

         // find out the correct output port in the spl-topo
         dst_out_port_ptr = (spl_topo_output_port_t *)out_port_ptr->simp_conn_in_port_t->t_base.gu.conn_out_port_ptr;

         // move md and data buffer to the correct output port for spl-topo processing
         simp_topo_copy_sdata_flags(&dst_out_port_ptr->t_base.common.sdata, &out_port_ptr->t_base.common.sdata);
         simp_topo_clear_sdata_flags(&out_port_ptr->t_base.common.sdata);

         if (NULL != out_port_ptr->md_list_ptr)
         {
            // transfer md from output to input
            spf_list_merge_lists((spf_list_node_t **)&(dst_out_port_ptr->md_list_ptr),
                                 (spf_list_node_t **)&(out_port_ptr->md_list_ptr));
         }

         if (spl_topo_int_out_port_has_data(topo_ptr, out_port_ptr))
         {
            // clang-format off
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
            for (uint32_t i = 0; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
            {
               dst_out_port_ptr->t_base.common.bufs_ptr[i].data_ptr        = out_port_ptr->t_base.common.bufs_ptr[i].data_ptr;
               dst_out_port_ptr->t_base.common.bufs_ptr[i].actual_data_len = out_port_ptr->t_base.common.bufs_ptr[i].actual_data_len;

               out_port_ptr->t_base.common.bufs_ptr[i].data_ptr        = NULL;
               out_port_ptr->t_base.common.bufs_ptr[i].actual_data_len = 0;
            }
#else
            for (uint32_t i = 0; i < out_port_ptr->t_base.common.sdata.bufs_num; i++)
            {
               dst_out_port_ptr->t_base.common.bufs_ptr[i].data_ptr = out_port_ptr->t_base.common.bufs_ptr[i].data_ptr;
               out_port_ptr->t_base.common.bufs_ptr[i].data_ptr     = NULL;
            }
            dst_out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len = out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len;
            out_port_ptr->t_base.common.bufs_ptr[0].actual_data_len = 0;
#endif
            // clang-format on
         }
      }
   }
}

/**
 * Process the topology.
 */
ar_result_t simp_topo_process(spl_topo_t *topo_ptr, uint8_t path_index)
{
   INIT_EXCEPTION_HANDLING

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Entering topo process, path index 0x%x", path_index);
#endif

   ar_result_t                 result                = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr      = &topo_ptr->t_base.proc_context;

   // Reset state changed flags.
   topo_ptr->proc_info.state_changed_flags.data_moved   = FALSE;
   topo_ptr->proc_info.state_changed_flags.mf_moved     = FALSE;
   topo_ptr->proc_info.state_changed_flags.event_raised = FALSE;

   // Forward kick loop.
   for (gu_module_list_t *current_list_ptr = topo_ptr->simpt_sorted_module_list_ptr; current_list_ptr;
        LIST_ADVANCE(current_list_ptr))
   {
      uint32_t num_iters = 1;

      // Get buffers, allocate sdata pointers, call cmn_module_process
      // manage input buffers.
      spl_topo_module_t *    curr_module_ptr = (spl_topo_module_t *)current_list_ptr->module_ptr;
      spl_topo_module_type_t module_type     = TOPO_MODULE_TYPE_SINGLE_PORT;

      if ((SPL_TOPO_INVALID_PATH_INDEX != path_index) && (path_index != curr_module_ptr->t_base.gu.path_index))
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_5
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "Topo process, ignoring miid 0x%lx with path index 0x%x. ",
                  curr_module_ptr->t_base.gu.module_instance_id,
                  curr_module_ptr->t_base.gu.path_index);
#endif
         continue;
      }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "Topo process inner loop begin mid 0x%lx",
               curr_module_ptr->t_base.gu.module_instance_id);
#endif

      module_type = spl_topo_get_module_port_type(curr_module_ptr);

      // reset the process context sdata
      gen_topo_reset_process_context_sdata(proc_context_ptr, &curr_module_ptr->t_base);

      num_iters = curr_module_ptr->t_base.num_proc_loops;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_MED_PRIO,
               "module 0x%lx: Number of process iterations: %u",
               curr_module_ptr->t_base.gu.module_instance_id,
               num_iters);
#endif

      for (uint32_t i = 0; i < num_iters; i++)
      {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_MED_PRIO,
                  "module 0x%lx: begin iteration %ld",
                  curr_module_ptr->t_base.gu.module_instance_id,
                  i);
#endif

         bool_t   input_has_metadata = FALSE;
         uint32_t input_size_before  = 0;
         bool_t   is_last_iteration  = (i == (num_iters - 1)) ? TRUE : FALSE;
         bool_t can_process = TRUE;

         // Setup input ports.
         for (gu_input_port_list_t *in_port_list_ptr = curr_module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
              LIST_ADVANCE(in_port_list_ptr))
         {
            spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            if (in_port_ptr->flags.port_inactive)
            {
               continue;
            }

            result             = simp_topo_setup_proc_ctx_in_port(topo_ptr, in_port_ptr, &can_process);
            DBG_VERIFY(result, AR_SUCCEEDED(result));

            if (!can_process)
            {
               // if first iteration itself can't continue then switch to special topo immediately.
               if (0 == i)
               {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
                  TOPO_MSG(topo_ptr->t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "Module skipped in simplified topo 0x%lx on iteration %d!, Will continue with special topo",
                           curr_module_ptr->t_base.gu.module_instance_id,
                           i);
#endif
                  topo_ptr->simpt1_flags.backwards_kick = TRUE;
                  goto __end_of_topo_process_;
               }

               // if this is not the first iteration then some data may have been generated in previous iterations.
               // so continue in simp-topo to process the data generated in previous iterations and then move to
               // special-topo.
               break;
            }

            // Check if eos or metadata exists for this iteration of this module. Needed below for fwk propagation of
            // md.
            input_has_metadata |=
               ((in_port_ptr->t_base.common.sdata.metadata_list_ptr) || (curr_module_ptr->t_base.int_md_list_ptr));


#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &in_port_ptr->t_base.common,
                                                TRUE /**is_input*/,
                                                in_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                FALSE));
#endif

         }

         if (!can_process)
         { // continue with next module in the simplified topo.

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Module skipped in simplified topo 0x%lx on iteration %d!, Will continue with next module",
                     curr_module_ptr->t_base.gu.module_instance_id,
                     i);
#endif
            i = num_iters; //to avoid running more iterations for this module
            continue;
         }

         // Setup output ports.
         for (gu_output_port_list_t *out_port_list_ptr = curr_module_ptr->t_base.gu.output_port_list_ptr;
              out_port_list_ptr;
              LIST_ADVANCE(out_port_list_ptr))
         {
            spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (out_port_ptr->flags.port_inactive)
            {
               continue;
            }

            TRY(result, simp_topo_setup_proc_ctx_out_port(topo_ptr, out_port_ptr));

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &out_port_ptr->t_base.common,
                                                FALSE /**is_input*/,
                                                out_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                FALSE));
#endif
         }

         // For single input and output port, copy over input buffer attributes such as timestamp and flags to
         // output, since all modules may not take care of this.
         if (TOPO_MODULE_TYPE_SINGLE_PORT == module_type)
         {
            // SISO -> Only one input port, only one output port.
            spl_topo_input_port_t *in_port_ptr =
               (spl_topo_input_port_t *)curr_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
            spl_topo_output_port_t *out_port_ptr =
               (spl_topo_output_port_t *)curr_module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

            input_size_before = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

            // Copy sdata flags from input to output for SISO module. Do not clear the input till process is
            // complete. Don't transfer md until after process.
            out_port_ptr->t_base.common.sdata.flags = in_port_ptr->t_base.common.sdata.flags;

            if (in_port_ptr->t_base.common.sdata.flags.is_timestamp_valid)
            {
               simp_topo_fwk_estimate_process_timestamp(topo_ptr,
                                                        curr_module_ptr,
                                                        proc_context_ptr
                                                           ->in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index],
                                                        proc_context_ptr
                                                           ->out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index]);
            }
         }
         else if ((TOPO_MODULE_TYPE_SINK == module_type) && (1 == curr_module_ptr->t_base.gu.num_input_ports))
         {
            // metadata propagation for one input Sink module
            spl_topo_input_port_t *in_port_ptr =
               (spl_topo_input_port_t *)curr_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;

            input_size_before = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;
         }

         if (curr_module_ptr->t_base.bypass_ptr)
         {
            // SISO -> Only one input port, only one output port.
            spl_topo_input_port_t *in_port_ptr =
               (spl_topo_input_port_t *)curr_module_ptr->t_base.gu.input_port_list_ptr->ip_port_ptr;
            spl_topo_output_port_t *out_port_ptr =
               (spl_topo_output_port_t *)curr_module_ptr->t_base.gu.output_port_list_ptr->op_port_ptr;

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_MED_PRIO,
                     "Topo bypassing process, module id 0x%8x",
                     curr_module_ptr->t_base.gu.module_instance_id);
#endif

            simp_topo_bypass_input_to_output((capi_stream_data_t *)topo_ptr->t_base.proc_context
                                                .in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index],
                                             (capi_stream_data_t *)topo_ptr->t_base.proc_context
                                                .out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index]);
         }
         else
         {
            TRY(result, simp_topo_process_module(topo_ptr, curr_module_ptr));
         }

         if (input_has_metadata)
         {
            spl_topo_propagate_metadata(topo_ptr,
                                        curr_module_ptr,
                                        input_has_metadata,
                                        input_size_before,
                                        SPL_TOPO_PROCESS);
         }

         result = simp_topo_process_attached_modules(topo_ptr, curr_module_ptr);
         DBG_VERIFY(result, AR_SUCCEEDED(result));

         // Check how much data was consumed on each input port, and take actions accordingly.
         for (gu_input_port_list_t *in_port_list_ptr = curr_module_ptr->t_base.gu.input_port_list_ptr; in_port_list_ptr;
              LIST_ADVANCE(in_port_list_ptr))
         {
            spl_topo_input_port_t *in_port_ptr = (spl_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

            if (in_port_ptr->flags.port_inactive)
            {
               continue;
            }

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &in_port_ptr->t_base.common,
                                                TRUE /**is_input*/,
                                                in_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                TRUE));
#endif

            bool_t consumed_input_on_port = FALSE;

            if (is_last_iteration) // if last iteration
            {
               // if module didn't consume all the data from input port then we may need to
               // trigger it again.
               if (in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len <
                   proc_context_ptr->in_port_scratch_ptr[in_port_ptr->t_base.gu.cmn.index].prev_actual_data_len[0])
               {
                  topo_ptr->simpt1_flags.backwards_kick = TRUE;
               }
            }

            simp_topo_adjust_in_buf(topo_ptr, in_port_ptr, &consumed_input_on_port);
            topo_ptr->proc_info.state_changed_flags.data_moved |= consumed_input_on_port;
         }

         // Adjust actual len on output buffers.
         // New length = old length + amount of newly consumed data.
         for (gu_output_port_list_t *out_port_list_ptr = curr_module_ptr->t_base.gu.output_port_list_ptr;
              out_port_list_ptr;
              LIST_ADVANCE(out_port_list_ptr))
         {
            spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (out_port_ptr->flags.port_inactive)
            {
               continue;
            }

#ifdef ERROR_CHECK_MODULE_PROCESS
               TRY(result,
                   gen_topo_validate_port_sdata(topo_ptr->t_base.gu.log_id,
                                                &out_port_ptr->t_base.common,
                                                FALSE /**is_input*/,
                                                out_port_ptr->t_base.gu.cmn.index,
                                                &curr_module_ptr->t_base,
                                                TRUE));
#endif

            bool_t generated_data_on_port = FALSE;

            simp_topo_adjust_out_buf(topo_ptr, out_port_ptr, &generated_data_on_port);
            topo_ptr->proc_info.state_changed_flags.data_moved |= generated_data_on_port;
         }

         if (!spl_topo_can_use_simp_topo(topo_ptr))
         {
            topo_ptr->simpt1_flags.backwards_kick = TRUE;
            goto __end_of_topo_process_;
         }
      } // end of module iteration
   }    // end of topo iteration

__end_of_topo_process_:

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
      topo_ptr->simpt1_flags.backwards_kick = TRUE;
   }

   if (topo_ptr->simpt1_flags.backwards_kick)
   {
      simp_topo_update_internal_buffers_for_spl_topo_process(topo_ptr);
   }
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Topo process end");
#endif

   return result;
}

static void simp_topo_bypass_container_propagate_md(spl_topo_input_port_t * in_port_ptr,
                                                    spl_topo_output_port_t *out_port_ptr,
                                                    uint32_t                input_size_before_per_ch)
{
   uint32_t          in_bytes_before    = 0;
   uint32_t          in_bytes_consumed  = 0;
   uint32_t          out_bytes_produced = 0;
   topo_media_fmt_t *in_med_fmt_ptr     = in_port_ptr->t_base.common.media_fmt_ptr;
   topo_media_fmt_t *out_med_fmt_ptr    = out_port_ptr->t_base.common.media_fmt_ptr;

   {
      uint32_t num_ch   = in_med_fmt_ptr->pcm.num_channels;
      in_bytes_consumed = in_port_ptr->t_base.common.sdata.buf_ptr
                             ? in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len * num_ch
                             : 0;
      in_bytes_before = input_size_before_per_ch * num_ch;
   }

   {
      uint32_t num_ch    = out_med_fmt_ptr->pcm.num_channels;
      out_bytes_produced = out_port_ptr->t_base.common.sdata.buf_ptr
                              ? out_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len * num_ch
                              : 0;
   }

   // Get the metadata handler
   intf_extn_param_id_metadata_handler_t handler;
   gen_topo_populate_metadata_extn_vtable((gen_topo_module_t *)in_port_ptr->t_base.gu.cmn.module_ptr, &handler);

   intf_extn_md_propagation_t input_md_info;

   memset(&input_md_info, 0, sizeof(input_md_info));
   input_md_info.df = gen_topo_convert_spf_data_format_to_capi_data_format(in_med_fmt_ptr->data_format);
   input_md_info.len_per_ch_in_bytes         = BYTES_TO_BYTES_PER_CH(in_bytes_consumed, in_med_fmt_ptr);
   input_md_info.initial_len_per_ch_in_bytes = BYTES_TO_BYTES_PER_CH(in_bytes_before, in_med_fmt_ptr);
   input_md_info.buf_delay_per_ch_in_bytes   = 0; // module does not report buffering delay. so assumed to be zero
   input_md_info.bits_per_sample             = in_med_fmt_ptr->pcm.bits_per_sample;
   input_md_info.sample_rate                 = in_med_fmt_ptr->pcm.sample_rate;

   intf_extn_md_propagation_t output_md_info;

   memset(&output_md_info, 0, sizeof(output_md_info));
   output_md_info.df = gen_topo_convert_spf_data_format_to_capi_data_format(out_med_fmt_ptr->data_format);
   output_md_info.len_per_ch_in_bytes       = BYTES_TO_BYTES_PER_CH(out_bytes_produced, out_med_fmt_ptr);
   output_md_info.buf_delay_per_ch_in_bytes = 0; // module does not report buffering delay. so assumed to be zero
   output_md_info.bits_per_sample           = out_med_fmt_ptr->pcm.bits_per_sample;
   output_md_info.sample_rate               = out_med_fmt_ptr->pcm.sample_rate;

   handler.metadata_propagate(handler.context_ptr,
                              &in_port_ptr->t_base.common.sdata,
                              &out_port_ptr->t_base.common.sdata,
                              NULL,
                              0 /*algo_delay*/,
                              &input_md_info,
                              &output_md_info);
}

/**
 * Process the topology.
 */
ar_result_t simp_topo_bypass(spl_topo_t *topo_ptr)
{
   INIT_EXCEPTION_HANDLING

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_2
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Entering topo bypass process");
#endif

   ar_result_t                 result                = AR_EOK;
   gen_topo_process_context_t *proc_context_ptr      = &topo_ptr->t_base.proc_context;

   // Reset state changed flags.
   topo_ptr->proc_info.state_changed_flags.data_moved   = FALSE;
   topo_ptr->proc_info.state_changed_flags.mf_moved     = FALSE;
   topo_ptr->proc_info.state_changed_flags.event_raised = FALSE;

   //contianer input and output port ptr.
   spl_topo_input_port_t* in_port_ptr = (spl_topo_input_port_t*)topo_ptr->t_base.gu.ext_in_port_list_ptr->ext_in_port_ptr->int_in_port_ptr;
   spl_topo_output_port_t* out_port_ptr = (spl_topo_output_port_t*)topo_ptr->t_base.gu.ext_out_port_list_ptr->ext_out_port_ptr->int_out_port_ptr;

   spl_topo_module_t* in_port_module_ptr = (spl_topo_module_t*)in_port_ptr->t_base.gu.cmn.module_ptr;
   spl_topo_module_t* ou_port_module_ptr = (spl_topo_module_t*)out_port_ptr->t_base.gu.cmn.module_ptr;

   {
      // reset the process context sdata
      gen_topo_reset_process_context_sdata(proc_context_ptr, &in_port_module_ptr->t_base);
      gen_topo_reset_process_context_sdata(proc_context_ptr, &ou_port_module_ptr->t_base);

      bool_t   input_has_metadata = FALSE;
      uint32_t input_size_before  = 0;

      // Setup input ports.
      {
         bool_t can_process = TRUE;
         result             = simp_topo_setup_proc_ctx_in_port(topo_ptr, in_port_ptr, &can_process);
         DBG_VERIFY(result, AR_SUCCEEDED(result));

         if (!can_process)
         {
#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
            TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "this can't happen!!");
#endif
            topo_ptr->simpt1_flags.backwards_kick = TRUE;
            goto __end_of_topo_process_;
         }

         // check if metadata exists
         input_has_metadata = (in_port_ptr->t_base.common.sdata.metadata_list_ptr)? TRUE: FALSE;
      }

      // Setup output ports.
      {
         TRY(result, simp_topo_setup_proc_ctx_out_port(topo_ptr, out_port_ptr));
      }

      // For single input and output port, copy over input buffer attributes such as timestamp and flags to
      // output, since all modules may not take care of this.
      {
         input_size_before = in_port_ptr->t_base.common.sdata.buf_ptr[0].actual_data_len;

         // Copy sdata flags from input to output for SISO module. Do not clear the input till process is
         // complete. Don't transfer md until after process.
         out_port_ptr->t_base.common.sdata.flags     = in_port_ptr->t_base.common.sdata.flags;
         out_port_ptr->t_base.common.sdata.timestamp = in_port_ptr->t_base.common.sdata.timestamp;
      }

      {

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_3
         TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Topo bypassing container");
#endif

         simp_topo_bypass_input_to_output((capi_stream_data_t *)topo_ptr->t_base.proc_context
                                             .in_port_sdata_pptr[in_port_ptr->t_base.gu.cmn.index],
                                          (capi_stream_data_t *)topo_ptr->t_base.proc_context
                                             .out_port_sdata_pptr[out_port_ptr->t_base.gu.cmn.index]);
      }

      if (input_has_metadata)
      {
         simp_topo_bypass_container_propagate_md(in_port_ptr, out_port_ptr, input_size_before);
      }

      result = simp_topo_process_attached_modules(topo_ptr, ou_port_module_ptr);
      DBG_VERIFY(result, AR_SUCCEEDED(result));

      // Check how much data was consumed on each input port, and take actions accordingly.
      {
         bool_t consumed_input_on_port = FALSE;
         simp_topo_adjust_in_buf(topo_ptr, in_port_ptr, &consumed_input_on_port);
         topo_ptr->proc_info.state_changed_flags.data_moved |= consumed_input_on_port;
      }

      // Adjust actual len on output buffers.
      {
         bool_t generated_data_on_port = FALSE;
         simp_topo_adjust_out_buf(topo_ptr, out_port_ptr, &generated_data_on_port);
         topo_ptr->proc_info.state_changed_flags.data_moved |= generated_data_on_port;
      }

      if (!spl_topo_can_use_simp_topo(topo_ptr))
      {
         topo_ptr->simpt1_flags.backwards_kick = TRUE;
         goto __end_of_topo_process_;
      }
   } // end of topo iteration

__end_of_topo_process_:

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
      topo_ptr->simpt1_flags.backwards_kick = TRUE;
   }

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   TOPO_MSG(topo_ptr->t_base.gu.log_id, DBG_MED_PRIO, "Topo bypass process end");
#endif

   return result;
}

