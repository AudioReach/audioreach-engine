/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file capi_mfc_osrc_island.cpp
 *
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
 * =========================================================================*/
#include "capi_mfc_osrc_i.h"
#include "capi_mfc_osrc_utils.h"
#define ALIGN4(o)         (((o)+3)&(~3))


static capi_vtbl_t vtbl = { capi_mfc_osrc_process,        capi_mfc_osrc_end,
                            capi_mfc_osrc_set_param,      capi_mfc_osrc_get_param,
                            capi_mfc_osrc_set_properties, capi_mfc_osrc_get_properties };

capi_vtbl_t *capi_mfc_osrc_get_vtable()
{
   return &vtbl;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Sets either a parameter value or a parameter structure containing multiple parameters. In the event of a failure, the
   appropriate error code is returned.
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_set_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr)
{

   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG( DBG_ERROR_PRIO, "set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
   }
   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)(_pif);

   if (param_id == FWK_EXTN_DM_PARAM_ID_SET_SAMPLES)
   {
      if (!capi_mfc_osrc_is_dm_enabled(me_ptr))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "FWK_EXTN_DM_PARAM_ID_SET_SAMPLES not supported: is_dm_enabled=%d",
                 capi_mfc_osrc_is_dm_enabled(me_ptr));
         return CAPI_EUNSUPPORTED;
      }

      if (!me_ptr->input_media_fmt_received)
      {
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Inp media fmt not received yet, cannot set DM samples");
         return CAPI_ENOTREADY;
      }

      if (params_ptr->actual_data_len < sizeof(fwk_extn_dm_param_id_req_samples_t))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "FWK_EXTN_DM_PARAM_ID_SET_SAMPLES: payload too small %lu < %zu",
                 params_ptr->actual_data_len,
                 sizeof(fwk_extn_dm_param_id_req_samples_t));
         return CAPI_ENEEDMORE;
      }
      fwk_extn_dm_param_id_req_samples_t *req_samples_ptr =
         (fwk_extn_dm_param_id_req_samples_t *)params_ptr->data_ptr;
      // only single port for now
      if (CAPI_MFC_OSRC_MAX_IN_PORTS != req_samples_ptr->num_ports)
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Set param id 0x%lx, mismatched port count %lu",
                 param_id,
                 req_samples_ptr->num_ports);
         CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         return capi_result;
      }

      if ((FWK_EXTN_DM_FIXED_OUTPUT_MODE == me_ptr->dm_info.dm_mode) && (!req_samples_ptr->is_input))
      {
         if (FWK_EXTN_DM_PARAM_ID_SET_SAMPLES == param_id)
         {
            // handle fixed output
            // get samples needed, port index can be ignored for single port
            me_ptr->dm_info.req_out_samples = req_samples_ptr->req_samples[0].samples_per_channel;
            capi_result |= capi_mfc_osrc_get_fixed_out_samples(me_ptr);

            // set to lib
            if (me_ptr->pc[0].osrc_rs_lib_ptr)
            {
               osrc_rs_lib_set_dm_config(me_ptr->pc[0].osrc_rs_lib_ptr,
                                         me_ptr->dm_info.dm_mode,
                                         me_ptr->dm_info.req_out_samples);
            }
         }
         else
         {
            capi_result |=
               capi_mfc_osrc_get_max_in_samples(me_ptr, req_samples_ptr->req_samples[0].samples_per_channel);
         }
      }
      else if ((FWK_EXTN_DM_FIXED_INPUT_MODE == me_ptr->dm_info.dm_mode) && (req_samples_ptr->is_input))
      {
         me_ptr->dm_info.req_out_samples = 0; // wont be used inside lib
         if (me_ptr->pc[0].osrc_rs_lib_ptr)
         {
            osrc_rs_lib_set_dm_config(me_ptr->pc[0].osrc_rs_lib_ptr,
                                      me_ptr->dm_info.dm_mode,
                                      me_ptr->dm_info.req_out_samples);
         }
      }
      else
      {
         // error
         CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      }
      return capi_result;
   }

   return capi_mfc_osrc_set_param_non_island(me_ptr, param_id, port_info_ptr, params_ptr);

}

/*______________________________________________________________________________________________________________________
   DESCRIPTION:
   Check if the scratch buffer allocated had enough size to handle incoming data.
   LOGIC:
   max_data_len_per_ch will be set to 0 during init or library init (even if scratch buffer is allocated for enc/dec)
   At process time, src_buf_max_len_per_ch is calculated from input buffer
   If this is more than the stored value, scratch buffer allocation logic is called.

   Since scratch buffer allocation depends on the factors like ordering or the modules, calculation is a little complex.
   So we don't want it to be called at every process. To achieve this, we use the variable  me_ptr->max_data_len_per_ch
   Module reordering can only happen at capi_mfc_osrc_lib_init, so this variable is reset to 0 there.
   In other situations, if local_max_data_len_per_ch lesser than stored, we don't have to realloc the scratch buffers
______________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_check_scratch_buffer(capi_mfc_osrc_t *me_ptr,
                                                capi_buf_t *       src_buf_ptr,
                                                capi_buf_t *       dest_buf_ptr,
                                                uint32_t           port)
{
   capi_err_t capi_result            = CAPI_EOK;
   uint32_t   src_buf_max_len_per_ch = 0;

   if (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->in_media_fmt[port].format.data_interleaving)
   {
      src_buf_max_len_per_ch = src_buf_ptr[port].max_data_len;
   }
   else
   {
      src_buf_max_len_per_ch = src_buf_ptr[port].max_data_len / me_ptr->in_media_fmt[port].format.num_channels;
   }

   src_buf_max_len_per_ch = ALIGN4(src_buf_max_len_per_ch);
   if (me_ptr->max_data_len_per_ch < src_buf_max_len_per_ch)
   {
      me_ptr->max_data_len_per_ch = src_buf_max_len_per_ch;
      capi_result                 = capi_mfc_osrc_allocate_scratch_buffer(me_ptr, src_buf_max_len_per_ch);
      if (CAPI_EOK != capi_result)
      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error in pcm convert process - %d", capi_result);
         capi_mfc_osrc_reset_buffer_len(me_ptr, src_buf_ptr, dest_buf_ptr, port);
         me_ptr->max_data_len_per_ch = 0;
      }
   }
   return capi_result;
}

capi_err_t capi_mfc_osrc_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t capi_result        = CAPI_EOK;
   uint32_t   actual_input_bytes = 0;
   bool_t     all_input_consumed = FALSE;
   uint32_t   port               = 0;

   bool_t is_output_buf_present =
      ((output) && (output[port]) && (output[port]->buf_ptr));
   bool_t is_output_present =
      (is_output_buf_present && (output[port]->buf_ptr->data_ptr));
   bool_t is_input_present = ((input) && (input[port]) && (input[port]->buf_ptr) && (input[port]->buf_ptr->data_ptr));

   if (is_input_present && (!is_output_present))
   {
      input[0]->buf_ptr[0].actual_data_len = 0;
   }

   if (!is_input_present || !is_output_buf_present)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_LOW_PRIO,
              "process: bail — is_input_present=%d is_output_buf_present=%d is_output_present=%d",
              (int)is_input_present, (int)is_output_buf_present, (int)is_output_present);
      return AR_EOK;
   }

   actual_input_bytes      = input[0]->buf_ptr->actual_data_len;
   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)_pif;
   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "mfc_osrc process: NULL instance pointer");
      return CAPI_EBADPARAM;
   }

   capi_buf_t *src_buf_ptr  = input[port]->buf_ptr;
   capi_buf_t *dest_buf_ptr = output[port]->buf_ptr;

   #ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
           "process ENTRY: in_actual=%lu in_max=%lu out_actual=%lu out_max=%lu bufs_num=%lu",
           src_buf_ptr->actual_data_len,
           src_buf_ptr->max_data_len,
           dest_buf_ptr->actual_data_len,
           dest_buf_ptr->max_data_len,
           input[port]->bufs_num);
   #endif

   /*We will call process for pcm decoder and pcm enc eventhough lib is disabled and no memory is allocated
   This is because pcm dec and enc need to raise threshold and cannot be disabled
   In this case, memcpy and return without accessing lib memory */
   if (me_ptr->lib_enable == FALSE)
   {
      bool_t is_inplace = (input[0]->buf_ptr[0].data_ptr == output[0]->buf_ptr[0].data_ptr);
      // even if processing is not done, input data needs to be copied to output & output actual len be set.
      uint32_t out_max_data_len = output[0]->buf_ptr[0].max_data_len;
      uint32_t num_bytes_to_cpy = MIN(input[0]->buf_ptr[0].actual_data_len, out_max_data_len);

      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process lib_enable=FALSE (passthrough): in_actual=%lu out_max=%lu bytes_to_cpy=%lu is_inplace=%d",
              input[0]->buf_ptr[0].actual_data_len,
              out_max_data_len,
              num_bytes_to_cpy,
              (int)is_inplace);
      #endif

      for (uint32_t i = 0; i < input[0]->bufs_num; i++)
      {
       if (!is_inplace)
       {
          memscpy(output[0]->buf_ptr[i].data_ptr, out_max_data_len, input[0]->buf_ptr[i].data_ptr, num_bytes_to_cpy);
       }
      }

      bool_t is_v2 = (CAPI_DEINTERLEAVED_UNPACKED_V2 ==
                      me_ptr->in_media_fmt[port].format.data_interleaving);
      output[0]->buf_ptr[0].actual_data_len = num_bytes_to_cpy;
      input[0]->buf_ptr[0].actual_data_len  = num_bytes_to_cpy;
      if(!is_v2)
      {
		  for (uint32_t ch = 1; ch < input[0]->bufs_num; ch++)
		  {
			 output[0]->buf_ptr[ch].actual_data_len = num_bytes_to_cpy;
			 input[0]->buf_ptr[ch].actual_data_len  = num_bytes_to_cpy;
		  } 
	  }



      return CAPI_EOK;
   }

   capi_result = capi_mfc_osrc_check_scratch_buffer(me_ptr, src_buf_ptr, dest_buf_ptr, port);

   if (CAPI_EOK != capi_result)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO,
              "process check_scratch_buffer failed result=0x%lx in_actual=%lu out_max=%lu",
              capi_result,
              src_buf_ptr->actual_data_len, dest_buf_ptr->max_data_len);
      return capi_result;
   }

   // if configured for fixed output, but input samples differ from expected number, reset values
   // if input actual len is more than expected module needs to consume only whats expected.
   // if input is less than expected, module operates in fixed input mode.
   if (capi_mfc_osrc_is_dm_enabled(me_ptr) && (FWK_EXTN_DM_FIXED_OUTPUT_MODE == me_ptr->dm_info.dm_mode))
   {
      uint32_t in_samples = src_buf_ptr->actual_data_len / (me_ptr->in_media_fmt[0].format.bits_per_sample >> 3);

      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process DM fixed-out: in_actual=%lu in_samples=%lu expected_in=%lu req_out=%lu",
              src_buf_ptr->actual_data_len,
              in_samples,
              me_ptr->dm_info.expected_in_samples,
              me_ptr->dm_info.req_out_samples);
      #endif

      if (in_samples < me_ptr->dm_info.expected_in_samples)
      {
         // process with available input samples if EOS/EOF is set, else return need more
         // Also consume available input if got set_param for consume_partial_data.
         if (input[port]->flags.end_of_frame || input[port]->flags.marker_eos ||
             me_ptr->dm_info.should_consume_partial_input)
         {
            #ifdef MFC_OSRC_DEBUG
            CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
                    "process DM: short input (%lu < %lu) but eof/eos/partial set — proceeding",
                    in_samples, me_ptr->dm_info.expected_in_samples);
            #endif
            me_ptr->dm_info.expected_in_samples = 0;
         }
         else 
         {
            #ifdef MFC_OSRC_DEBUG
            CNV_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Need more inp samples! expected %lu, received %lu — returning ENEEDMORE, in_actual=%lu",
                    me_ptr->dm_info.expected_in_samples,
                    in_samples,
                    src_buf_ptr->actual_data_len);
            #endif

            // mark input unconsumed across all channels; zero output ch[1..N-1] for V2
            {
               bool_t is_v2 = (CAPI_DEINTERLEAVED_UNPACKED_V2 ==
                               me_ptr->in_media_fmt[port].format.data_interleaving);
			   if(!is_v2)
			   {
				   for (uint32_t ch = 0; ch < input[port]->bufs_num; ch++)
				   {
					  input[port]->buf_ptr[ch].actual_data_len = 0;
				   }
				   for (uint32_t ch = 1; ch < output[port]->bufs_num; ch++)
				   {
					  output[port]->buf_ptr[ch].actual_data_len = 0;
				   }
			   }

            }
            return CAPI_ENEEDMORE;
         }
      }
      else if (in_samples > me_ptr->dm_info.expected_in_samples)
      {
         uint32_t truncated_bytes =
            me_ptr->dm_info.expected_in_samples * (me_ptr->in_media_fmt[0].format.bits_per_sample >> 3);

         #ifdef MFC_OSRC_DEBUG
         CNV_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Got more samples! expected %lu, received %lu — truncating in_actual %lu->%lu (DATA LOSS CHECK)",
                 me_ptr->dm_info.expected_in_samples,
                 in_samples,
                 src_buf_ptr->actual_data_len,
                 truncated_bytes);
         #endif

         
         src_buf_ptr->actual_data_len = truncated_bytes; 
      }
   }

   // PC library updates only buf[0] lengths; all channels are propagated after the call below.
   me_ptr->pc[port].heap_id = (uint32_t)me_ptr->heap_info.heap_id;

   bool_t needs_resampling = (me_ptr->pc[port].core_lib.flags.RESAMPLER_PRE ||
                               me_ptr->pc[port].core_lib.flags.RESAMPLER_POST);

   if (needs_resampling)
   {
      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process calling mfc_osrc_resampler_process: in_actual=%lu in_max=%lu out_actual=%lu out_max=%lu",
              src_buf_ptr->actual_data_len,
              src_buf_ptr->max_data_len,
              dest_buf_ptr->actual_data_len,
              dest_buf_ptr->max_data_len);
      #endif

      #ifdef MFC_OSRC_profiling
      me_ptr->kpps_profile_data.start_cycles = qurt_get_core_pcycles();
      #endif

      capi_result = ar_result_to_capi_err(mfc_osrc_resampler_process(&me_ptr->pc[port],
                                                     src_buf_ptr,
                                                     dest_buf_ptr,
                                                     &me_ptr->pc[port].core_lib.input_media_fmt,
                                                     &me_ptr->pc[port].core_lib.output_media_fmt));

      #ifdef MFC_OSRC_profiling
      me_ptr->kpps_profile_data.end_cycles = qurt_get_core_pcycles();
      capi_mfc_osrc_profiling(me_ptr, actual_input_bytes / (me_ptr->in_media_fmt[port].format.bits_per_sample >> 3));
      #endif
      
      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process mfc_osrc_resampler_process done: result=0x%lx in_actual_after=%lu out_actual_after=%lu",
              capi_result,
              src_buf_ptr->actual_data_len,
              dest_buf_ptr->actual_data_len);
      #endif


      if (CAPI_ENEEDMORE == capi_result)
      {
         #ifdef MFC_OSRC_DEBUG
         CNV_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "mfc_osrc_resampler_process ENEEDMORE: in_actual_after=%lu eof=%d eos=%d",
                 (input[port]->buf_ptr ? input[port]->buf_ptr[0].actual_data_len : 0),
                 (int)input[port]->flags.end_of_frame,
                 (int)input[port]->flags.marker_eos);
         #endif
         // Drop data for eof case. Mark data as unconsumed for non-eof case.
         if (!input[port]->flags.end_of_frame)
         {
            input[port]->buf_ptr[0].actual_data_len = 0;
         }
         /* Zero buf[1..N-1] on both ports per format contract (already done by
          * mfc_osrc_resampler_process for V2, but repeat here for safety). */
         {
            bool_t is_in_v2  = (CAPI_DEINTERLEAVED_UNPACKED_V2 ==
                                me_ptr->in_media_fmt[port].format.data_interleaving);
            bool_t is_out_v2 = (CAPI_DEINTERLEAVED_UNPACKED_V2 ==
                                me_ptr->out_media_fmt[port].format.data_interleaving);
            for (uint32_t ch = 1; ch < input[port]->bufs_num; ch++)
            {
               if (!is_in_v2)  input[port]->buf_ptr[ch].actual_data_len  = 0;
               if (!is_out_v2) output[port]->buf_ptr[ch].actual_data_len = 0;
            }
         }
         return capi_result;
      }
   }
   else
   {
      /* No resampling needed — run the full pipeline dispatcher which handles all
       * enabled stages (interleaving, byte-morph, channel-mix, etc.) while safely
       * skipping any stage whose process pointer is NULL. */
      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process calling mfc_osrc_process (no resampling): in_actual=%lu out_max=%lu",
              src_buf_ptr->actual_data_len,
              dest_buf_ptr->max_data_len);
      #endif

      #ifdef MFC_OSRC_profiling
      me_ptr->kpps_profile_data.start_cycles = qurt_get_core_pcycles();
      #endif

      capi_result = ar_result_to_capi_err(mfc_osrc_process(&me_ptr->pc[port],
                                                           src_buf_ptr,
                                                           dest_buf_ptr,
                                                           &me_ptr->scratch_buf_1[port],
                                                           &me_ptr->scratch_buf_2[port]));



      #ifdef MFC_OSRC_profiling
      me_ptr->kpps_profile_data.end_cycles = qurt_get_core_pcycles();
      capi_mfc_osrc_profiling(me_ptr, actual_input_bytes / (me_ptr->in_media_fmt[port].format.bits_per_sample >> 3));
      #endif
      
      #ifdef MFC_OSRC_DEBUG
      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process mfc_osrc_process done: result=0x%lx in_actual_after=%lu out_actual_after=%lu",
              capi_result,
              src_buf_ptr->actual_data_len,
              dest_buf_ptr->actual_data_len);
      #endif
   }

   if (0 != capi_result)
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "Error while pcm converter process, inp and out media fmts are ");
      capi_mfc_osrc_print_media_fmt(me_ptr, &me_ptr->pc[port].core_lib.input_media_fmt, port);
      capi_mfc_osrc_print_media_fmt(me_ptr, &me_ptr->pc[port].core_lib.output_media_fmt, port);
   }

   uint32_t is_gapless_case = FALSE;

   if ((me_ptr->initial_silence_in_samples > 0) || (me_ptr->trailing_silence_in_samples > 0))
   {
      capi_media_fmt_v2_t *out_mf              = NULL;
      out_mf                                   = me_ptr->out_media_fmt;
      bool_t                 is_initial        = TRUE;
      uint32_t               num_bytes_removed = 0;
      uint32_t               bytes_per_sample  = (out_mf->format.bits_per_sample) / 8;
      capi_stream_data_v2_t *in_stream_ptr     = (capi_stream_data_v2_t *)input[0];
      module_cmn_md_list_t * metadata_list_ptr = in_stream_ptr->metadata_list_ptr;

      CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
              "process gapless: out_actual=%lu initial_silence=%lu trailing_silence=%lu eos=%d",
              output[0]->buf_ptr->actual_data_len,
              me_ptr->initial_silence_in_samples,
              me_ptr->trailing_silence_in_samples,
              (int)in_stream_ptr->flags.marker_eos);

      if (actual_input_bytes == input[0]->buf_ptr->actual_data_len)
      {
         all_input_consumed = TRUE;
      }
      // Detect last frame based on EOS.
      if (in_stream_ptr->flags.marker_eos && all_input_consumed && me_ptr->trailing_silence_in_samples)
      {

         is_initial = FALSE;

         uint32_t bytes_to_remove_per_channel = me_ptr->trailing_silence_in_samples * bytes_per_sample;
         capi_cmn_gapless_remove_zeroes(&bytes_to_remove_per_channel,
                                        out_mf,
                                        &output[0],
                                        is_initial,
                                        metadata_list_ptr);
         CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
                 "process gapless trailing: removed %lu bytes, out_actual_after=%lu",
                 (me_ptr->trailing_silence_in_samples * bytes_per_sample) - bytes_to_remove_per_channel,
                 output[0]->buf_ptr->actual_data_len);
         me_ptr->trailing_silence_in_samples = 0;
      }

      if ((me_ptr->initial_silence_in_samples > 0) && (output[0]->buf_ptr->actual_data_len > 0))
      {
         uint32_t bytes_to_remove_per_channel = me_ptr->initial_silence_in_samples * bytes_per_sample;
         num_bytes_removed                    = bytes_to_remove_per_channel;
         capi_cmn_gapless_remove_zeroes(&bytes_to_remove_per_channel,
                                        out_mf,
                                        &output[0],
                                        is_initial,
                                        metadata_list_ptr);
         num_bytes_removed -= bytes_to_remove_per_channel;
         #ifdef MFC_OSRC_DEBUG
         CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
                 "process gapless initial: removed %lu bytes, remaining_silence=%lu out_actual_after=%lu",
                 num_bytes_removed,
                 bytes_to_remove_per_channel / bytes_per_sample,
                 output[0]->buf_ptr->actual_data_len);
         #endif
         if (output[0]->flags.is_timestamp_valid && num_bytes_removed)
         {
            is_gapless_case = TRUE;
            output[0]->timestamp =
               output[0]->timestamp + ((num_bytes_removed / bytes_per_sample) / out_mf->format.sampling_rate) * 1000000;
         }
         me_ptr->initial_silence_in_samples = bytes_to_remove_per_channel / bytes_per_sample;
      }
   }

   if (input[port]->flags.is_timestamp_valid && (FALSE == is_gapless_case))
   {
      output[port]->timestamp                = input[port]->timestamp - me_ptr->events_config.delay_in_us;
      output[port]->flags.is_timestamp_valid = TRUE;
   }

   #ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid, DBG_LOW_PRIO,
           "process EXIT: in_consumed=%lu out_produced=%lu result=0x%lx",
           actual_input_bytes - src_buf_ptr->actual_data_len,
           dest_buf_ptr->actual_data_len,
           capi_result);
   #endif

   return capi_result;
}
