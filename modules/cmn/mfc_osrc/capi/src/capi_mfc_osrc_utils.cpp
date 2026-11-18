/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file capi_mfc_osrc_utils.cpp
 *
 */
/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
 * =========================================================================*/

#include "capi_mfc_osrc_utils.h"

/*______________________________________________________________________________________________________________________

   DESCRIPTION:
   Resets the buffer lengths to zero. This can happen if some thing went wrong with the buffers during process
______________________________________________________________________________________________________________________*/
void capi_mfc_osrc_reset_buffer_len(capi_mfc_osrc_t *me_ptr,
                                      capi_buf_t *       src_buf_ptr,
                                      capi_buf_t *       dest_buf_ptr,
                                      uint32_t           port)
{
   if (CAPI_DEINTERLEAVED_UNPACKED_V2 != me_ptr->in_media_fmt[port].format.data_interleaving)
   {
      for (uint32_t ch = 0; ch < me_ptr->in_media_fmt[port].format.num_channels; ch++)
      {
         src_buf_ptr[ch].actual_data_len = 0;
      }
   }
   else
   {
      src_buf_ptr->actual_data_len = 0;
   }

   if (CAPI_DEINTERLEAVED_UNPACKED_V2 != me_ptr->in_media_fmt[port].format.data_interleaving)
   {
      for (uint32_t ch = 0; ch < me_ptr->out_media_fmt[port].format.num_channels; ch++)
      {
         dest_buf_ptr[ch].actual_data_len = 0;
      }
   }
   else
   {
      dest_buf_ptr->actual_data_len = 0;
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Re-allocate the scratch buffer if needed
  ____________________________________________________________________________________________________________________*/

capi_err_t capi_mfc_osrc_realloc_scratch_buf(capi_mfc_osrc_t *me_ptr, capi_buf_t *sb_ptr, uint32_t max_data_len)
{
   capi_err_t result = CAPI_EOK;
   if ((0 != max_data_len) && (max_data_len != sb_ptr->max_data_len))
   {
      if (NULL != sb_ptr->data_ptr)
      {
         posal_memory_free(sb_ptr->data_ptr);
         sb_ptr->data_ptr = NULL;
      }

      sb_ptr->data_ptr = (int8_t *)posal_memory_malloc(max_data_len, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
      if (NULL == sb_ptr->data_ptr)
      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to allocate scratch buffer");
         result = CAPI_ENOMEMORY;
         // Setting sb_ptr->max_data_len = 0 when scratch buffer allocation failed
         sb_ptr->max_data_len = 0;
      }
      else
      {
         // Setting sb_ptr->max_data_len = max_data_len when scratch buffer allocated
         sb_ptr->max_data_len = max_data_len;
      }
   }
   return result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Allocates scratch buffer based on max data length
   Re-scales the maximum length based on maximum channel and if any intermediate byte up-conversions are present
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_allocate_scratch_buffer(capi_mfc_osrc_t *me_ptr, uint32_t max_data_len_per_ch)
{
   capi_err_t      result          = CAPI_EOK;
   uint32_t        port            = 0;
   uint32_t        b1_max_data_len = 0;
   uint32_t        r1_max_data_len = 0;
   uint32_t        c_max_data_len  = 0;
   uint32_t        r2_max_data_len = 0;
   uint32_t        b2_max_data_len = 0;
   uint32_t        max_data_len    = max_data_len_per_ch * me_ptr->in_media_fmt[port].format.num_channels;
   mfc_media_fmt_t *inp_mf_ptr      = &me_ptr->pc[port].core_lib.input_media_fmt;
   mfc_media_fmt_t *out_mf_ptr      = NULL;

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Initial max data len %d", max_data_len);

   b1_max_data_len = max_data_len;
   if (me_ptr->pc[port].core_lib.flags.BYTE_CNV_PRE)
   {
      out_mf_ptr      = &me_ptr->pc[port].core_lib.pc_proc_info[BYTE_CNV_PRE].output_media_fmt;
      b1_max_data_len = (b1_max_data_len * (out_mf_ptr->word_size >> 3)) / (inp_mf_ptr->word_size >> 3);
      inp_mf_ptr      = out_mf_ptr;
   }

   r1_max_data_len = b1_max_data_len;
   if (me_ptr->pc[port].core_lib.flags.RESAMPLER_PRE)
   {
      out_mf_ptr           = &me_ptr->pc[port].core_lib.pc_proc_info[RESAMPLER_PRE].output_media_fmt;
      uint32_t in_samples  = CAPI_CMN_CEIL(r1_max_data_len, (inp_mf_ptr->num_channels * (inp_mf_ptr->word_size >> 3)));
      uint32_t out_samples = CAPI_CMN_CEIL(in_samples * out_mf_ptr->sampling_rate, inp_mf_ptr->sampling_rate);
      r1_max_data_len      = out_samples * (out_mf_ptr->word_size >> 3) * out_mf_ptr->num_channels;
      inp_mf_ptr           = out_mf_ptr;
   }

   c_max_data_len = r1_max_data_len;
   if (me_ptr->pc[port].core_lib.flags.CHANNEL_MIXER)
   {
      out_mf_ptr     = &me_ptr->pc[port].core_lib.pc_proc_info[CHANNEL_MIXER].output_media_fmt;
      c_max_data_len = (c_max_data_len * out_mf_ptr->num_channels) / (inp_mf_ptr->num_channels);
      inp_mf_ptr     = out_mf_ptr;
   }

   r2_max_data_len = c_max_data_len;
   if (me_ptr->pc[port].core_lib.flags.RESAMPLER_POST)
   {
      out_mf_ptr           = &me_ptr->pc[port].core_lib.pc_proc_info[RESAMPLER_POST].output_media_fmt;
      uint32_t in_samples  = CAPI_CMN_CEIL(r2_max_data_len, (inp_mf_ptr->num_channels * (inp_mf_ptr->word_size >> 3)));
      uint32_t out_samples = CAPI_CMN_CEIL(in_samples * out_mf_ptr->sampling_rate, inp_mf_ptr->sampling_rate);
      r2_max_data_len      = out_samples * (out_mf_ptr->word_size >> 3) * out_mf_ptr->num_channels;
      inp_mf_ptr           = out_mf_ptr;
   }

   b2_max_data_len = r2_max_data_len;
   if (me_ptr->pc[port].core_lib.flags.BYTE_CNV_POST)
   {
      out_mf_ptr      = &me_ptr->pc[port].core_lib.pc_proc_info[BYTE_CNV_POST].output_media_fmt;
      b2_max_data_len = (b2_max_data_len * (out_mf_ptr->word_size >> 3)) / (inp_mf_ptr->word_size >> 3);
      inp_mf_ptr      = out_mf_ptr;
   }

   max_data_len = max_data_len >= b1_max_data_len ? max_data_len : b1_max_data_len;
   max_data_len = max_data_len >= r1_max_data_len ? max_data_len : r1_max_data_len;
   max_data_len = max_data_len >= c_max_data_len ? max_data_len : c_max_data_len;
   max_data_len = max_data_len >= r2_max_data_len ? max_data_len : r2_max_data_len;
   max_data_len = max_data_len >= b2_max_data_len ? max_data_len : b2_max_data_len;

   #ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "b1 %lu, r1 %lu, c %lu, r2 %lu, b2 %lu",
           b1_max_data_len,
           r1_max_data_len,
           c_max_data_len,
           r2_max_data_len,
           b2_max_data_len);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Final max data len %d", max_data_len);
   #endif

   if (0 == me_ptr->pc[port].core_lib.no_of_buffering_stages)
   {
      return result;
   }
   else if (1 == me_ptr->pc[port].core_lib.no_of_buffering_stages)
   {
      result = capi_mfc_osrc_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_1[port], max_data_len);
      if (CAPI_FAILED(result))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Scratch buffer 1 allocation failed, size = %lu, result = %lu",
                 max_data_len,
                 result);
      }
      else
      {
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Scratch buffer 1 allocated, size = %lu, result = %lu",
                 max_data_len,
                 result);
      }
   }
   else
   {
      result = capi_mfc_osrc_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_1[port], max_data_len);
      if (CAPI_FAILED(result))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Scratch buffer 1 allocation failed, so not reallocting memory for scratch buffer 2, size = %lu, "
                 "result = %lu",
                 max_data_len,
                 result);
      }
      else
      {
         result |= capi_mfc_osrc_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_2[port], max_data_len);
         if (CAPI_FAILED(result))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Scratch buffer 1 allocated but buffer 2 failed so freeing the scratch buffer 1, size = %lu, "
                    "result = %lu",
                    max_data_len,
                    result);
            // Freeing scratch buffer 1 when scratch buffer 2 failed and resetting the value of max_data_len = 0
            posal_memory_free(me_ptr->scratch_buf_1[port].data_ptr);
            me_ptr->scratch_buf_1[port].data_ptr     = NULL;
            me_ptr->scratch_buf_1[port].max_data_len = 0;
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Scratch buffer 1 and 2 allocated, size = %lu, result = %lu",
                    max_data_len,
                    result);
         }
      }
   }
   return result;
}

/*===========================================================================
    FUNCTION : capi_dyanmic_resampler_handle_data_port_op
    DESCRIPTION: Function to handle data port operations.
===========================================================================*/
capi_err_t capi_mfc_osrc_handle_data_port_op(capi_mfc_osrc_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == params_ptr->data_ptr)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Data port operation set param, received null buffer");
      return CAPI_EBADPARAM;
   }
   if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid payload size for port operation %d", params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }
   intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);

   if (params_ptr->actual_data_len <
       sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t)))
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid payload size for port operation %d", params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   // validate num input ports.
   if (data_ptr->num_ports > CAPI_MFC_OSRC_MAX_IN_PORTS)
   {
      AR_MSG(DBG_ERROR_PRIO, "parameter, num_ports %d", data_ptr->num_ports);
      return CAPI_EBADPARAM;
   }

   for (uint32_t i = 0; i < data_ptr->num_ports; i++)
   {
      // get port state from the port operation
      intf_extn_data_port_state_t port_state = intf_extn_data_port_op_to_port_state(data_ptr->opcode);

      CNV_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "Received Data Port operation %lx, is_input_port %lu, port state %lx",
              data_ptr->opcode,
              data_ptr->is_input_port,
              port_state);
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________

   DESCRIPTION:
   Checks if the channel type is valid
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_check_ch_type(capi_mfc_osrc_t *me_ptr,
                                         const uint16_t *   channel_type,
                                         const uint32_t     array_size)
{
   for (uint8_t i = 0; (i < array_size) && (i < CAPI_MAX_CHANNELS_V2); i++)
   {
      if ((channel_type[i] < (uint16_t)PCM_CHANNEL_L) || (channel_type[i] > (uint16_t)PCM_MAX_CHANNEL_MAP_V2))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Unsupported channel type channel idx %d, channel type %d",
                 (int)i,
                 (int)channel_type[i]);
         return CAPI_EBADPARAM;
      }
   }

   return CAPI_EOK;
}

/*______________________________________________________________________________________________________________________

   DESCRIPTION:
   Finds the Co-efficient ptr form the given input and output channel map and sets it to the library.
   If the co-efficient ptr is available, it returns the valid co-efficient ptr
______________________________________________________________________________________________________________________*/
static void *capi_mfc_osrc_chmixer_get_coef_set_ptr(capi_mfc_osrc_t *const me_ptr)
{
   uint32_t             port          = 0;
   capi_media_fmt_v2_t *inp_media_fmt = &(me_ptr->in_media_fmt[port]);
   capi_media_fmt_v2_t *out_media_fmt = &(me_ptr->out_media_fmt[port]);

   ChMixerChType in_ch_type[CH_MIXER_MAX_NUM_CH];
   for (uint32_t i = 0; i < inp_media_fmt->format.num_channels; i++)
   {
      in_ch_type[i] = (ChMixerChType)inp_media_fmt->channel_type[i];
   }

   ChMixerChType out_ch_type[CH_MIXER_MAX_NUM_CH];
   for (uint32_t i = 0; i < out_media_fmt->format.num_channels; i++)
   {
      out_ch_type[i] = (ChMixerChType)out_media_fmt->channel_type[i];
   }

   // Find the coef set which matches the current media format
   capi_mfc_osrc_chmixer_coef_set_t *coef_set_ptr = me_ptr->config.coef_sets_ptr;
   int16_t *                           coef_ptr     = NULL;

   for (int8_t i = me_ptr->config.num_coef_sets - 1; i >= 0; i--)
   {
      if ((me_ptr->config.num_coef_sets - 1) != i) // except the first iteration
      {
         coef_set_ptr++;
      }

      if (coef_set_ptr->num_in_ch != inp_media_fmt->format.num_channels)
      {
         continue;
      }
      if (coef_set_ptr->num_out_ch != out_media_fmt->format.num_channels)
      {
         continue;
      }

      uint32_t j = 0;
      for (j = 0; j < inp_media_fmt->format.num_channels; j++)
      {
         if (coef_set_ptr->in_ch_map[j] != inp_media_fmt->channel_type[j])
         {
            break;
         }
      }

      if (j < inp_media_fmt->format.num_channels)
      {
         continue;
      }

      for (j = 0; j < out_media_fmt->format.num_channels; j++)
      {
         if (coef_set_ptr->out_ch_map[j] != out_media_fmt->channel_type[j])
         {
            break;
         }
      }

      if (j == out_media_fmt->format.num_channels)
      {
         coef_ptr = coef_set_ptr->coef_ptr;
         break;
      }
   }

   return coef_ptr;
}
/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Checks if the media format sent is valid
   3. Copies the input media fmt variables
   2. If the configured param is 0, sets the output media fmt variables as well
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_handle_input_media_fmt(capi_mfc_osrc_t *  me_ptr,
                                                  capi_media_fmt_v2_t *temp_media_fmt_ptr,
                                                  uint32_t             port)
{
   capi_err_t capi_result = CAPI_EOK;


   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Received Input media format");


   if (me_ptr->input_media_fmt_received)
   {
      capi_standard_data_format_v2_t *cur = &me_ptr->in_media_fmt[port].format;
      capi_standard_data_format_v2_t *inc = &temp_media_fmt_ptr->format;
      if (cur->sampling_rate     == inc->sampling_rate     &&
          cur->bits_per_sample   == inc->bits_per_sample   &&
          cur->num_channels      == inc->num_channels      &&
          cur->data_interleaving == inc->data_interleaving &&
          cur->q_factor          == inc->q_factor)
      {
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO,
                 "input media fmt unchanged (SR=%lu bps=%lu ch=%lu) — skipping lib_init",
                 cur->sampling_rate,
                 cur->bits_per_sample,
                 cur->num_channels);
         return CAPI_EOK;
      }
   }
   if(TRUE != mfc_osrc_mfc_is_supported_media_type(temp_media_fmt_ptr))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "CAPI_INPUT_MEDIA_FORMAT: failed, invalid parameters, FORMAT = data_format = "
              "%u, "
              "bps = %u, bitstream_format = %u, data_interleaving = %u, data_is_signed = %u, num_channels = %u, "
              "q_factor = %u, sampling_rate = %u",
              temp_media_fmt_ptr->header.format_header.data_format,
              temp_media_fmt_ptr->format.bits_per_sample,
              temp_media_fmt_ptr->format.bitstream_format,
              temp_media_fmt_ptr->format.data_interleaving,
              temp_media_fmt_ptr->format.data_is_signed,
              temp_media_fmt_ptr->format.num_channels,
              temp_media_fmt_ptr->format.q_factor,
              temp_media_fmt_ptr->format.sampling_rate);
      CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
      return capi_result;
   }

   // copy and save the input media fmt
   me_ptr->in_media_fmt[port] = *temp_media_fmt_ptr;

   capi_standard_data_format_v2_t *        in_std_ptr           = &me_ptr->in_media_fmt[port].format;
   capi_standard_data_format_v2_t *        out_std_ptr          = &me_ptr->out_media_fmt[port].format;
   capi_pc_configured_mf_t *               configured_media_fmt = &me_ptr->configured_media_fmt[port];
   fwk_extn_pcm_param_id_media_fmt_extn_t *in_extn_media_ptr    = &me_ptr->extn_in_media_fmt[port];
   fwk_extn_pcm_param_id_media_fmt_extn_t *out_extn_media_ptr   = &me_ptr->extn_out_media_fmt[port];


   if (CAPI_MAX_FORMAT_TYPE == configured_media_fmt->data_format)
   {
      me_ptr->out_media_fmt[port].header.format_header.data_format =
         me_ptr->in_media_fmt[port].header.format_header.data_format;
   }
   else
   {
      me_ptr->out_media_fmt[port].header.format_header.data_format = configured_media_fmt->data_format;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width) 
   {
      out_extn_media_ptr->bit_width = in_extn_media_ptr->bit_width;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bits_per_sample)
   {
      out_std_ptr->bits_per_sample = in_std_ptr->bits_per_sample;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.num_channels)
   {
      out_std_ptr->num_channels = in_std_ptr->num_channels;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.interleaved)
   {
      out_std_ptr->data_interleaving = in_std_ptr->data_interleaving;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.num_channels)
   {
      out_std_ptr->num_channels = in_std_ptr->num_channels;
      memscpy(&out_std_ptr->channel_type,
              sizeof(uint16_t) * in_std_ptr->num_channels,
              &in_std_ptr->channel_type,
              sizeof(uint16_t) * in_std_ptr->num_channels);
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.q_factor)
   {
      out_std_ptr->q_factor = in_std_ptr->q_factor;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->sampling_rate)
   {
      out_std_ptr->sampling_rate = in_std_ptr->sampling_rate;
   }

   out_std_ptr->bitstream_format = in_std_ptr->bitstream_format;
   out_std_ptr->data_is_signed   = in_std_ptr->data_is_signed;

   me_ptr->extn_in_media_fmt[port].alignment = me_ptr->extn_out_media_fmt[port].alignment = MFC_LSB_ALIGNED;
   me_ptr->extn_in_media_fmt[port].endianness = me_ptr->extn_out_media_fmt[port].endianness = MFC_LITTLE_ENDIAN;
   me_ptr->extn_in_media_fmt[port].bit_width = capi_mfc_osrc_mfc_set_extn_inp_mf_bitwidth(in_std_ptr->q_factor);
   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width) 
   {
      me_ptr->extn_out_media_fmt[port].bit_width = me_ptr->extn_in_media_fmt[port].bit_width;
   }

   capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);
   capi_result |= capi_mfc_osrc_check_and_raise_output_media_format_event(me_ptr);

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the output media fmt is valid, if so raises the out put media fmt event
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_check_and_raise_output_media_format_event(capi_mfc_osrc_t *me_ptr)
{
   uint32_t   port        = 0;
   capi_err_t capi_result = CAPI_EOK;

   if (mfc_osrc_mfc_is_supported_media_type(&me_ptr->out_media_fmt[port]))
   {
      /* Only raise if the output media format has actually changed.
       * Raising the same format again causes gen_cntr to re-run threshold
       * propagation mid-inner-loop in the upstream STREAM container, which
       * exits the loop without returning the write buffer (deadlock). */
      bool_t changed = TRUE;
      if (me_ptr->out_media_fmt_raised_valid[port])
      {
         capi_media_fmt_v2_t *prev = &me_ptr->out_media_fmt_raised[port];
         capi_media_fmt_v2_t *curr = &me_ptr->out_media_fmt[port];
         capi_standard_data_format_v2_t *pf = &prev->format;
         capi_standard_data_format_v2_t *cf = &curr->format;

         if (pf->sampling_rate      == cf->sampling_rate      &&
             pf->bits_per_sample    == cf->bits_per_sample     &&
             pf->num_channels       == cf->num_channels        &&
             pf->data_interleaving  == cf->data_interleaving   &&
             pf->q_factor           == cf->q_factor)
         {
            changed = FALSE;
         }
      }

      if (changed)
      {
         capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->out_media_fmt[port], FALSE, port);
         if (CAPI_EOK == capi_result)
         {
            me_ptr->out_media_fmt_raised[port]       = me_ptr->out_media_fmt[port];
            me_ptr->out_media_fmt_raised_valid[port] = TRUE;
         }
      }
   }
   return capi_result;
}



/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
  This function is used to calculate the max input samples req to produce max output samples
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_get_max_in_samples(capi_mfc_osrc_t *me_ptr, uint32_t max_out_samples)
{
   capi_err_t capi_result = CAPI_EOK;

   // just return 1 ms larger for now
   // check if input media format is known and output sampling rate is known
   if ((!me_ptr->input_media_fmt_received) ||
       (CAPI_DATA_FORMAT_INVALID_VAL == me_ptr->in_media_fmt[0].format.sampling_rate) ||
       (CAPI_DATA_FORMAT_INVALID_VAL == me_ptr->out_media_fmt[0].format.sampling_rate))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "DM get max samples cannot be returned, input/output media format not set");
      return capi_result;
   }
   // scale samples for input and add 1 ms to it
   uint64_t duration_us   = ((uint64_t)max_out_samples * 1000000) / me_ptr->out_media_fmt[0].format.sampling_rate;
   uint32_t input_samples = (uint32_t)(((duration_us + 1000) * me_ptr->in_media_fmt[0].format.sampling_rate) / 1000000);
   fwk_extn_dm_param_id_req_samples_t req_samples;
   req_samples.is_input                           = TRUE;
   req_samples.num_ports                          = 1;
   req_samples.req_samples[0].port_index          = 0;
   req_samples.req_samples[0].samples_per_channel = input_samples;
   capi_result |= capi_mfc_osrc_raise_dm_event(me_ptr, &req_samples, FWK_EXTN_DM_EVENT_ID_REPORT_MAX_SAMPLES);

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "set max out samples to %d, expected max in samples %d",
           max_out_samples,
           input_samples);
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function checks if it is a fractional resampling scenario, called from lib init after inp and out mf are known
  ____________________________________________________________________________________________________________________*/
bool_t capi_mfc_osrc_is_fractional_media_type(const capi_mfc_osrc_t *me_ptr)
{
   uint64_t in_sr  = (uint64_t)me_ptr->in_media_fmt[0].format.sampling_rate;
   uint64_t out_sr = (uint64_t)me_ptr->out_media_fmt[0].format.sampling_rate;

   if (in_sr == 0 || out_sr == 0 ||
       in_sr == (uint64_t)CAPI_DATA_FORMAT_INVALID_VAL ||
       out_sr == (uint64_t)CAPI_DATA_FORMAT_INVALID_VAL)
   {
      CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO,
              "is_fractional: SR not yet valid (in=%lu out=%lu), returning FALSE",
              in_sr, out_sr);
      return FALSE;
   }

   if (me_ptr->frame_size_us)
   {
      uint64_t in_frame_size  = (uint64_t)capi_cmn_us_to_samples(me_ptr->frame_size_us, in_sr);
      uint64_t out_frame_size = (uint64_t)capi_cmn_us_to_samples(me_ptr->frame_size_us, out_sr);

      CNV_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "in_frame_size %lu, out_frame_size %lu",
              in_frame_size,
              out_frame_size);

      return (in_frame_size * out_sr == out_frame_size * in_sr) ? FALSE : TRUE;
   }
   else
   {
      CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "frame size is not set.");
      return ((in_sr % 1000) || (out_sr % 1000)) ? TRUE : FALSE;
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   1. check and update dm enable or disable to the fwk
   2. DM is disabled if frame-size ratio (in samples) is same as sampling rate ratio.
   3. DM is enabled if frame-size ratio (in samples) is not same as sampling rate ratio.
______________________________________________________________________________________________________________________*/
void capi_mfc_osrc_update_dm_disable(capi_mfc_osrc_t *me_ptr)
{
   if (me_ptr->input_media_fmt_received)
   {
      // Check should be for inp/out fractional media formats
      if (capi_mfc_osrc_is_fractional_media_type(me_ptr))
      {
         me_ptr->dm_info.is_dm_disabled = FALSE;
         capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_ENABLED_DM);
      }
      else 
      {
         me_ptr->dm_info.is_dm_disabled = TRUE;
         capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_DISABLED_DM);
      }
   }
}
/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Translates the cached OSResampler quality parameters from capi_mfc_osrc_t into
   (num_taps, max_filters, rs_flags) on the osrc_resampler_lib_t pointed to by
   pc[0].osrc_rs_lib_ptr, then recreates the resampler context so the new quality
   settings take effect immediately.

   Must be called after mfc_osrc_init() returns successfully (which allocates osrc_rs_lib_ptr
   with default quality settings and calls resampleFixedRatioInit once internally).
   This function overwrites those defaults with the user-configured values and
   calls osrc_rs_lib_check_create_resampler_instance() to rebuild the context.

   If osrc_rs_lib_ptr is NULL (no resampling active), this is a no-op.

   Preset → (num_taps, max_filters) mapping (matches osrc_pc_converter.h documentation):
     0 →  48 taps,  48 filters  (~16 KB stereo)
     1 → 156 taps, 320 filters  (~219 KB stereo)
     2 → 380 taps, 380 filters  (~620 KB stereo)
     3 → 988 taps, 988 filters  (~3956 KB stereo)
  ____________________________________________________________________________________________________________________*/
void capi_mfc_osrc_apply_quality_cfg(capi_mfc_osrc_t *me_ptr)
{
   static const uint32_t preset_taps[4]    = { 48,  156,  380,  988 };
   static const uint32_t preset_filters[4] = { 48,  320,  380,  988 };

   osrc_resampler_lib_t *osrc_lib = me_ptr->pc[0].osrc_rs_lib_ptr;
   if (NULL == osrc_lib)
   {
      /* No resampling active — nothing to configure. */
      return;
   }

   uint32_t preset = me_ptr->osrc_resampler_cfg.quality_preset;
   if (preset > 3)
   {
      preset = 0;
   }

   osrc_lib->num_taps    = preset_taps[preset];
   osrc_lib->max_filters = preset_filters[preset];

   /* Build rs_flags from the individual boolean knobs.
    * INCLUDE_LOWPASS is added automatically inside osrc_rs_lib_check_create_resampler_instance()
    * for downsampling and must NOT be set here. */
   int rs_flags = 0;
   if (me_ptr->osrc_resampler_cfg.use_interpolation)
   {
      rs_flags |= SUBSAMPLE_INTERPOLATE;
   }
   if (me_ptr->osrc_resampler_cfg.use_blackman_harris)
   {
      rs_flags |= BLACKMAN_HARRIS;
   }
   osrc_lib->rs_flags = rs_flags;

   #ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "OSResampler quality cfg: preset=%d num_taps=%d max_filters=%d rs_flags=0x%x — recreating resampler",
           preset,
           osrc_lib->num_taps, osrc_lib->max_filters, osrc_lib->rs_flags);
   #endif

   /* Rebuild the resampler context with the updated quality params. */
   ar_result_t result = osrc_rs_lib_check_create_resampler_instance(osrc_lib,
                                                                     me_ptr->heap_info.heap_id);
   if (AR_EOK != result)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO,
              "OSResampler quality cfg: osrc_rs_lib_check_create_resampler_instance failed 0x%lx",
              result);
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   1. Copies the media format from capi structure to pcm lib media fmt
   2. Classifies the media format combo for both input and output
   3. Finds the channel mixer co-efficient ptr based on the media format
   4. Inits pcm library
   5. Raises kpps events based on the module order
   6. Resets max_data_len_per_ch, so that the scratch buffer allocation logic should be revisited during process
  ____________________________________________________________________________________________________________________*/
ar_result_t capi_mfc_osrc_lib_init(capi_mfc_osrc_t *me_ptr, bool_t rs_reinit)
{
   ar_result_t                             result             = AR_EOK;
   uint32_t                                port               = 0;
   capi_standard_data_format_v2_t *        in_std_ptr         = &me_ptr->in_media_fmt[port].format;
   capi_standard_data_format_v2_t *        out_std_ptr        = &me_ptr->out_media_fmt[port].format;
   fwk_extn_pcm_param_id_media_fmt_extn_t *in_extn_media_ptr  = &me_ptr->extn_in_media_fmt[port];
   fwk_extn_pcm_param_id_media_fmt_extn_t *out_extn_media_ptr = &me_ptr->extn_out_media_fmt[port];
   mfc_media_fmt_t                          input_media_fmt;
   mfc_media_fmt_t                          output_media_fmt;

   if (!((mfc_osrc_mfc_is_supported_media_type(&me_ptr->in_media_fmt[port])) && 
         (mfc_osrc_mfc_is_supported_media_type(&me_ptr->out_media_fmt[port])) &&
         (CAPI_PCM_VALID == mfc_osrc_is_supported_extn_media_type(in_extn_media_ptr)) &&
         (CAPI_PCM_VALID == mfc_osrc_is_supported_extn_media_type(out_extn_media_ptr))))
   {
      return AR_EOK;
   }

   input_media_fmt.sampling_rate = in_std_ptr->sampling_rate;
   input_media_fmt.bit_width     = in_extn_media_ptr->bit_width;
   input_media_fmt.word_size     = in_std_ptr->bits_per_sample;
   input_media_fmt.q_factor      = in_std_ptr->q_factor;
   input_media_fmt.interleaving  = capi_interleaved_to_mfc(in_std_ptr->data_interleaving);
   input_media_fmt.endianness    = (mfc_endian_t)in_extn_media_ptr->endianness;
   input_media_fmt.alignment     = (mfc_alignment_t)in_extn_media_ptr->alignment;
   input_media_fmt.data_format   = capi_dataformat_to_mfc(me_ptr->in_media_fmt[port].header.format_header.data_format);
   input_media_fmt.byte_combo    = mfc_osrc_classify_mf(&input_media_fmt);
   input_media_fmt.num_channels  = in_std_ptr->num_channels;
   input_media_fmt.channel_type  = &in_std_ptr->channel_type[0];

   output_media_fmt.sampling_rate = out_std_ptr->sampling_rate;
   output_media_fmt.bit_width     = out_extn_media_ptr->bit_width;
   output_media_fmt.word_size     = out_std_ptr->bits_per_sample;
   output_media_fmt.q_factor      = out_std_ptr->q_factor;
   output_media_fmt.interleaving  = capi_interleaved_to_mfc(out_std_ptr->data_interleaving);
   output_media_fmt.endianness    = (mfc_endian_t)out_extn_media_ptr->endianness;
   output_media_fmt.alignment     = (mfc_alignment_t)out_extn_media_ptr->alignment;
   output_media_fmt.data_format   = capi_dataformat_to_mfc(me_ptr->out_media_fmt[port].header.format_header.data_format);
   output_media_fmt.byte_combo    = mfc_osrc_classify_mf(&output_media_fmt);
   output_media_fmt.num_channels  = out_std_ptr->num_channels;
   output_media_fmt.channel_type  = &out_std_ptr->channel_type[0];

   if ((MFC_INVALID_CMBO == input_media_fmt.byte_combo) || (MFC_INVALID_CMBO == output_media_fmt.byte_combo))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "Invalid byte combo set for INPUT bw: %d, bps: %d and qfactor: %d or OUTPUT "
              "bw: %d, bps: %d and qfactor: %d",
              input_media_fmt.bit_width,
              input_media_fmt.word_size,
              input_media_fmt.q_factor,
              output_media_fmt.bit_width,
              output_media_fmt.word_size,
              output_media_fmt.q_factor);
      return AR_EFAILED;
   }

#ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "AFTER SETTING INPUT");
   capi_mfc_osrc_print_media_fmt(me_ptr, &input_media_fmt, port);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "AFTER SETTING OUTPUT");
   capi_mfc_osrc_print_media_fmt(me_ptr, &output_media_fmt, port);
#endif

   result = mfc_osrc_init(&me_ptr->pc[port],
                    (mfc_media_fmt_t *)&input_media_fmt,
                    (mfc_media_fmt_t *)&output_media_fmt,
                    (void *)capi_mfc_osrc_chmixer_get_coef_set_ptr(me_ptr),
                    (uint32_t)me_ptr->heap_info.heap_id,
                    &me_ptr->lib_enable,
                    me_ptr->miid,
                    0 /* type: always MFC */);

   #ifdef MFC_OSRC_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "PC Init result: ,");
   #endif

   if ((AR_EOK != result) || (me_ptr->lib_enable == FALSE))
   {
      me_ptr->lib_enable = FALSE;
      capi_mfc_osrc_raise_process_check_event(me_ptr);
      /* Free osrc_rs_lib_ptr before base mfc_osrc_deinit — allocated by mfc_osrc_init() */
      if (NULL != me_ptr->pc[port].osrc_rs_lib_ptr)
      {
         osrc_rs_lib_deinit(me_ptr->pc[port].osrc_rs_lib_ptr);
         posal_memory_free(me_ptr->pc[port].osrc_rs_lib_ptr);
         me_ptr->pc[port].osrc_rs_lib_ptr = NULL;
      }
      mfc_osrc_deinit(&me_ptr->pc[port]);
      return result;
   }

   /* Apply user-configured OSResampler quality params (quality_preset,
    * use_interpolation, use_blackman_harris) to the osrc_resampler_lib_t
    * that mfc_osrc_init() just allocated, overriding the defaults set by
    * osrc_rs_lib_init().  This rebuilds the resampler context so the
    * new quality settings take effect before the first process() call. */
   capi_mfc_osrc_apply_quality_cfg(me_ptr); //TODO: check if we can do it along the init and media fmt application

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "PC Init Done, Pre modules -E1-%lu, Float_to_Fixed-%lu, I1-%lu, B1-%lu, R1-%lu, C-%lu",
           me_ptr->pc[port].core_lib.flags.ENDIANNESS_PRE,
           me_ptr->pc[port].core_lib.flags.DATA_CNV_FLOAT_TO_FIXED,
           me_ptr->pc[port].core_lib.flags.INT_DEINT_PRE,
           me_ptr->pc[port].core_lib.flags.BYTE_CNV_PRE,
           me_ptr->pc[port].core_lib.flags.RESAMPLER_PRE,
           me_ptr->pc[port].core_lib.flags.CHANNEL_MIXER);

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "PC Init Done , Post modules R2-%lu, B2-%lu, I2-%lu, Fixed_to_Float-%lu, E2-%lu",
           me_ptr->pc[port].core_lib.flags.RESAMPLER_POST,
           me_ptr->pc[port].core_lib.flags.BYTE_CNV_POST,
           me_ptr->pc[port].core_lib.flags.INT_DEINT_POST,
           me_ptr->pc[port].core_lib.flags.DATA_CNV_FIXED_TO_FLOAT,
           me_ptr->pc[port].core_lib.flags.ENDIANNESS_POST);

   // this flag should be set before dm mode setting.
   me_ptr->input_media_fmt_received = TRUE;

   /* Pre-allocate scratch buffers BEFORE raising any events.
    *
    * raise_process_check_event + update_and_raise_events below trigger
    * gen_cntr_handle_port_data_thresh_change in the container, which can
    * cause process() to be called immediately if write buffers are already
    * queued.  If the scratch buffer is not pre-allocated by that point,
    * check_scratch_buffer fires a heap malloc inside process(), which causes
    * the container to skip registering the ICB ext_out wakeup callback for
    * c1 — so c1 is never re-kicked after the startup burst fills the ICB,
    * and DATA WR BUFFER DONE is never sent (deadlock).
    *
    * Pre-allocating here — before any event is raised — guarantees
    * max_data_len_per_ch is non-zero when the first process() fires.
    */
   if (me_ptr->in_media_fmt[0].format.sampling_rate > 0 &&
       me_ptr->in_media_fmt[0].format.bits_per_sample > 0)
   {
      uint32_t in_sr        = me_ptr->in_media_fmt[0].format.sampling_rate;
      uint32_t bps          = me_ptr->in_media_fmt[0].format.bits_per_sample >> 3;
      uint32_t in_samp_1ms  = (in_sr + 999) / 1000; //TODO: check in mfc whether in_sr mod 1000
      uint32_t max_per_ch   = ALIGN_4_BYTES(in_samp_1ms * bps);

      capi_err_t scratch_result =
         capi_mfc_osrc_allocate_scratch_buffer(me_ptr, max_per_ch);
      //TODO: check how are scratch buffers used either a simple memcpy from capi to fwk or used by lib for scratch ops

      if (CAPI_EOK == scratch_result)
      {
         me_ptr->max_data_len_per_ch = max_per_ch;
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO,
                 "lib_init: pre-alloc scratch buf done (per_ch=%lu bytes)",
                 max_per_ch);
      }
      else
      {
         me_ptr->max_data_len_per_ch = 0;
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO,
                 "lib_init: pre-alloc scratch buf failed (per_ch=%lu) — will retry in process",
                 max_per_ch);
      }
   }
   else
   {
      me_ptr->max_data_len_per_ch = 0;
   }

   capi_mfc_osrc_raise_process_check_event(me_ptr);
   capi_mfc_osrc_update_and_raise_events(me_ptr);

   // if not fractional, and dm mode is configured, raise disabled event
   capi_mfc_osrc_update_dm_disable(me_ptr);

   return AR_EOK;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the pcm cnv media fmt is has all the supported values
  ____________________________________________________________________________________________________________________*/
bool_t mfc_osrc_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr)
{
   if ((CAPI_FIXED_POINT != format_ptr->header.format_header.data_format) &&
       (CAPI_FLOATING_POINT != format_ptr->header.format_header.data_format))
   {
      return FALSE;
   }

   // Return if the format is invalid
   if (CAPI_DATA_FORMAT_INVALID_VAL == format_ptr->format.bits_per_sample ||
       CAPI_DATA_FORMAT_INVALID_VAL == format_ptr->format.data_interleaving ||
       CAPI_DATA_FORMAT_INVALID_VAL == format_ptr->format.data_is_signed ||
       CAPI_DATA_FORMAT_INVALID_VAL == format_ptr->format.num_channels ||
       CAPI_DATA_FORMAT_INVALID_VAL == format_ptr->format.sampling_rate)
   {
      return FALSE;
   }

   if (CAPI_FIXED_POINT == format_ptr->header.format_header.data_format)
   {
      if ((16 != format_ptr->format.bits_per_sample) &&
          (24 != format_ptr->format.bits_per_sample) && // for packed mode.
          (32 != format_ptr->format.bits_per_sample))
      {
         return FALSE;
      }
   }
   else if ((CAPI_FLOATING_POINT == format_ptr->header.format_header.data_format))
   {
      if ((32 != format_ptr->format.bits_per_sample) && (64 != format_ptr->format.bits_per_sample))
      {
         return FALSE;
      }
   }

   if ((CAPI_INTERLEAVED != format_ptr->format.data_interleaving) &&
       (CAPI_DEINTERLEAVED_PACKED != format_ptr->format.data_interleaving) &&
       (CAPI_DEINTERLEAVED_UNPACKED_V2 != format_ptr->format.data_interleaving))
   {
      return FALSE;
   }

   if (TRUE != format_ptr->format.data_is_signed)
   {
      return FALSE;
   }

   if ((0 == format_ptr->format.num_channels) || (CAPI_MAX_CHANNELS_V2 < format_ptr->format.num_channels))
   {
      return FALSE;
   }

   for (uint32_t i = 0; i < format_ptr->format.num_channels; i++)
   {
      if ((format_ptr->format.channel_type[i] < (uint16_t)PCM_CHANNEL_L) ||
          (format_ptr->format.channel_type[i] > (uint16_t)PCM_MAX_CHANNEL_MAP_V2))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Unsupported channel type channel idx %d, channel type %d received",
                (int)i,
                (int)format_ptr->format.channel_type[i]);
         return FALSE;
      }
   }

   if ((384000 < format_ptr->format.sampling_rate) || (0 >= format_ptr->format.sampling_rate))
   {
      return FALSE;
   }
   return TRUE;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the input mfc media fmt is supported
  ____________________________________________________________________________________________________________________*/
bool_t mfc_osrc_mfc_is_supported_media_type(const capi_media_fmt_v2_t *fmt_ptr)
{
   if (NULL == fmt_ptr)
   {
      return FALSE;
   }

   if (CAPI_DEINTERLEAVED_UNPACKED_V2 != fmt_ptr->format.data_interleaving)
   {
      return FALSE;
   }

   if (TRUE != fmt_ptr->format.data_is_signed)
   {
      return FALSE;
   }

   if ((fmt_ptr->format.num_channels > CAPI_MAX_CHANNELS_V2) || (fmt_ptr->format.num_channels <= 0))
   {
      return FALSE;
   }

   for (uint32_t i = 0; i < fmt_ptr->format.num_channels; i++)
   {
      if ((fmt_ptr->format.channel_type[i] < (uint16_t)PCM_CHANNEL_L) ||
          (fmt_ptr->format.channel_type[i] > (uint16_t)PCM_MAX_CHANNEL_MAP_V2))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Unsupported channel type channel idx %d, channel type %d received",
                (int)i,
                (int)fmt_ptr->format.channel_type[i]);
         return FALSE;
      }
   }

   if ((fmt_ptr->format.bits_per_sample != 16) && (fmt_ptr->format.bits_per_sample != 24) &&
       (fmt_ptr->format.bits_per_sample != 32))
   {
      return FALSE;
   }

   if ((fmt_ptr->format.sampling_rate > 384000) || (fmt_ptr->format.sampling_rate <= 0))
   {
      return FALSE;
   }
   return TRUE;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the framework extension for media fmt structure has supported values
  ____________________________________________________________________________________________________________________*/
capi_mfc_osrc_result_t mfc_osrc_is_supported_extn_media_type(const fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr)
{
   capi_mfc_osrc_result_t result = CAPI_PCM_VALID;

   if ((16 != extn_ptr->bit_width) && (24 != extn_ptr->bit_width) && (32 != extn_ptr->bit_width) &&
       (64 != extn_ptr->bit_width))
   {
      if (CAPI_DATA_FORMAT_INVALID_VAL != extn_ptr->bit_width)
      {
         return CAPI_PCM_ERROR;
      }
      else
      {
         result = CAPI_PCM_INVALID;
      }
   }

   if ((MFC_LSB_ALIGNED != extn_ptr->alignment) && (MFC_MSB_ALIGNED != extn_ptr->alignment))
   {
      return CAPI_PCM_ERROR;
   }

   if ((MFC_LITTLE_ENDIAN != extn_ptr->endianness) && (MFC_BIG_ENDIAN != extn_ptr->endianness))
   {
      if (CAPI_DATA_FORMAT_INVALID_VAL != extn_ptr->endianness)
      {
         return CAPI_PCM_ERROR;
      }
      else
      {
         result = CAPI_PCM_INVALID;
      }
   }

   return result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the output media fmt ptr has supported values
  ____________________________________________________________________________________________________________________*/
bool_t mfc_osrc_is_supported_out_media_type(const payload_pcm_output_format_cfg_t *format_ptr, uint32_t data_format)
{
   capi_err_t result = CAPI_EOK;
   if (DATA_FORMAT_FIXED_POINT == data_format)
   {
      if (CAPI_FAILED(result = capi_cmn_validate_client_pcm_output_cfg(format_ptr)))
      {
         return FALSE;
      }
   }
   else if (DATA_FORMAT_FLOATING_POINT == data_format)
   {
      if (CAPI_FAILED(result = capi_cmn_validate_client_pcm_float_output_cfg(format_ptr)))
      {
         return FALSE;
      }
   }
   else
   {
      return FALSE;
   }

   return TRUE;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Function to raise various events of the mfc_osrc module
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_raise_process_check_event(capi_mfc_osrc_t *me_ptr)
{
   capi_cmn_update_process_check_event(&me_ptr->cb_info, me_ptr->lib_enable);
   return CAPI_EOK;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Function to raise various events of the mfc_osrc module
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_update_and_raise_events(capi_mfc_osrc_t *me_ptr)
{
   capi_err_t capi_result    = CAPI_EOK;
   uint32_t   port           = 0;
   uint32_t   bw             = 0;
   uint32_t   kpps           = 0;
   uint32_t   new_algo_delay = 0;

   if (NULL == me_ptr->cb_info.event_cb)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Event callback is not set. Unable to raise events!");
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   // This additional kpps is added to capi operations during process
   mfc_osrc_get_kpps_and_bw(&me_ptr->pc[port], &kpps, &bw);
   kpps           = kpps + MFC_OSRC_CAPI_KPPS;
   new_algo_delay = mfc_osrc_get_algo_delay(&me_ptr->pc[port]);

   if (kpps != me_ptr->events_config.kpps)
   {
      me_ptr->events_config.kpps = kpps;
      capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, me_ptr->events_config.kpps);
   }

   if (bw != me_ptr->events_config.data_bw)
   {
      me_ptr->events_config.code_bw = 0;
      me_ptr->events_config.data_bw = bw;

      capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->cb_info,
                                                     me_ptr->events_config.code_bw,
                                                     me_ptr->events_config.data_bw);
   }

   if (new_algo_delay != me_ptr->events_config.delay_in_us)
   {
      me_ptr->events_config.delay_in_us = new_algo_delay;
      capi_result |= capi_cmn_update_algo_delay_event(&me_ptr->cb_info, me_ptr->events_config.delay_in_us);
   }
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Set properties. Uses capi cmn functions
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_process_set_properties(capi_mfc_osrc_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t   capi_result = CAPI_EOK;
   capi_prop_t *prop_array  = proplist_ptr->prop_ptr;
   uint32_t     i           = 0;
   uint32_t     port        = 0;

   if (NULL == prop_array)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set property, received null property ptr.");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, TRUE);
   if (CAPI_EOK != capi_result)
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "Set basic properties failed with result %lu",
              capi_result);
      return capi_result;
   }

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &(prop_array[i].payload);

      switch (prop_array[i].id)
      {
         case CAPI_HEAP_ID:
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_PORT_NUM_INFO:
         {
            break;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            // If pre or post resamplers are enabled
            if (((me_ptr->pc[0].core_lib.flags.RESAMPLER_PRE) || (me_ptr->pc[0].core_lib.flags.RESAMPLER_POST)))
            {
               // OSResampler algo reset
               if (me_ptr->pc[0].osrc_rs_lib_ptr)
               {
                  CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "OSRC Algo Reset");
                  capi_result = osrc_rs_lib_algo_reset(me_ptr->pc[0].osrc_rs_lib_ptr);
               }
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (payload_ptr->actual_data_len >=
                sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
            {
               capi_set_get_media_format_t *   main_ptr = (capi_set_get_media_format_t *)payload_ptr->data_ptr;
               capi_standard_data_format_v2_t *data_ptr = (capi_standard_data_format_v2_t *)(main_ptr + 1);
               capi_media_fmt_v2_t             temp_media_fmt;
               temp_media_fmt.header                   = *main_ptr;
               temp_media_fmt.format.minor_version     = data_ptr->minor_version;
               temp_media_fmt.format.bitstream_format  = data_ptr->bitstream_format;
               temp_media_fmt.format.bits_per_sample   = data_ptr->bits_per_sample;
               temp_media_fmt.format.q_factor          = data_ptr->q_factor;
               temp_media_fmt.format.sampling_rate     = data_ptr->sampling_rate;
               temp_media_fmt.format.data_is_signed    = data_ptr->data_is_signed;
               temp_media_fmt.format.data_interleaving = data_ptr->data_interleaving;
               temp_media_fmt.format.num_channels      = data_ptr->num_channels;
               for (uint32_t ch = 0; ch < temp_media_fmt.format.num_channels; ch++)
               {
                  temp_media_fmt.format.channel_type[ch] = data_ptr->channel_type[ch];
               }

               if (CAPI_FLOATING_POINT == temp_media_fmt.header.format_header.data_format)
               {
                  if (!mfc_osrc_is_floating_point_data_format_supported())
                  {
                     CNV_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Floating point data format not supported by this module/chipset");
                     return CAPI_EUNSUPPORTED;
                  }
               }

               capi_result |= capi_mfc_osrc_handle_input_media_fmt(me_ptr, &temp_media_fmt, port);
            }
            else
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set property id 0x%lx Bad param size %lu",
                       (uint32_t)prop_array[i].id,
                       payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                        = data_ptr->module_instance_id;
               me_ptr->pc[0].miid                  = me_ptr->miid;
            }
            else
            {
               CNV_MSG(MIID_UNKNOWN,
                       DBG_ERROR_PRIO,
                       "MF_MFC_OSRC: Set, Param id 0x%lx Bad param size %lu",
                       (uint32_t)prop_array[i].id,
                       payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_LOGGING_INFO:
         {
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT:
         {
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }

         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }

         case CAPI_CUSTOM_INIT_DATA:
         {
            break;
         }

         default:
         {  
         #if MFC_OSRC_DEBUG
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Set property id %#x. Not supported.",
                    prop_array[i].id);
         #endif
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      if (CAPI_FAILED(capi_result))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Set property for %#x failed with opcode %lu",
                 prop_array[i].id,
                 capi_result);
      }
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   get properties
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_process_get_properties(capi_mfc_osrc_t *    me_ptr,
                                                  capi_proplist_t *      proplist_ptr)
{
   capi_err_t        capi_result                 = CAPI_EOK;
   capi_prop_t *     prop_array                  = proplist_ptr->prop_ptr;
   uint32_t          port                        = 0;
   uint32_t          fwk_extn_ids_arr_mfc[2]     = { FWK_EXTN_DM, FWK_EXTN_CONTAINER_FRAME_DURATION };
   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_mfc_osrc_t));
   mod_prop.stack_size         = CAPI_MFC_OSRC_STACK_SIZE; 
   mod_prop.is_inplace         = FALSE;
   /* MFC_OSRC always uses requires-data-buffering=TRUE. Unlike the original
    * MODULE_ID_PCM_CNV which produces DEINTERLEAVED_UNPACKED (V1, same byte count
    * as interleaved input), MFC_OSRC produces DEINTERLEAVED_UNPACKED_V2 where
    * each channel buffer is sized per-channel (half the interleaved total). The
    * framework's drop-and-restore path for requires-data-buffering=FALSE modules
    * fires when input consumed != 0, blocking write-buffer returns. Setting TRUE
    * disables that path for all MFC_OSRC instances. */
   mod_prop.req_data_buffering = TRUE;
   mod_prop.max_metadata_size  = 0;

   mod_prop.num_fwk_extns    = 2;
   mod_prop.fwk_extn_ids_arr = fwk_extn_ids_arr_mfc; 

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "Get common basic properties failed with capi_result %lu", capi_result);

      return capi_result;
   }

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      switch (prop_array[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            capi_buf_t *payload_ptr = &(prop_array[i].payload);
            if (payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_INTERFACE_EXTENSIONS Bad param size %lu", payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                             (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
            {
               AR_MSG(DBG_ERROR_PRIO, "CAPI_INTERFACE_EXTENSIONS invalid param size %lu", payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t i = 0; i < intf_ext_list->num_extensions; i++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  case INTF_EXTN_DATA_PORT_OPERATION: //TODO: check for port var in og mfc and remove
                  {
                     // Only MFC supports data port ops, MFC uses port state to release hw resources upon stop/suspend
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     
                     break;
                  }
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }
               }
               #if MFC_OSRC_DEBUG
               AR_MSG(DBG_HIGH_PRIO,
                      "CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = %d",
                      curr_intf_extn_desc_ptr->id,
                      (int)curr_intf_extn_desc_ptr->is_supported);
               #endif
               curr_intf_extn_desc_ptr++;
            }
            break;
         }

         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr || ((prop_array[i].port_info.is_valid) && (0 != prop_array[i].port_info.port_index)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_MF_MFC_OSRC: Get property id 0x%lx failed due to invalid/unexpected values",
                      (uint32_t)prop_array[i].id);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            capi_result |= capi_cmn_handle_get_output_media_fmt_v2(&prop_array[i], &me_ptr->out_media_fmt[port]);

            break;
         }

         case CAPI_PORT_DATA_THRESHOLD:
         {
            if (NULL == me_ptr)
            {
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI_MF_MFC_OSRC: Get property for ID %#x. failed, NULL me_ptr.",
                      prop_array[i].id);
               break;
            }

            uint32_t threshold = 1;

            capi_result |= capi_cmn_handle_get_port_threshold(&prop_array[i], threshold);

            break;
         }

         default:
         {
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      #if MFC_OSRC_DEBUG
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_HIGH_PRIO,
                "CAPI_MF_MFC_OSRC: Get property for %#x failed with opcode %lu",
                prop_array[i].id,
                capi_result);
      }
      #endif
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   The function sets the output bits per sample and output qfactor based on the output bitwidth received in the
   setparam - PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT
  ____________________________________________________________________________________________________________________*/

void capi_mfc_osrc_mfc_set_output_bps_qf(capi_pc_configured_mf_t *media_fmt_ptr)
{
   uint32_t bit_width = media_fmt_ptr->fmt.bit_width;

   // 16 implies samples in 16-bit words with q15 format
   if (16 == bit_width)
   {
      media_fmt_ptr->fmt.bits_per_sample = 16;
      media_fmt_ptr->fmt.q_factor        = PCM_Q_FACTOR_15;
   }

   // 24 implies samples in 32-bit words with q27 format
   else if (24 == bit_width)
   {
      media_fmt_ptr->fmt.bits_per_sample = 32;
      media_fmt_ptr->fmt.q_factor        = PCM_Q_FACTOR_27;
   }

   // 32 implies samples in 32-bit words with q31 format
   else if (32 == bit_width)
   {
      media_fmt_ptr->fmt.bits_per_sample = 32;
      media_fmt_ptr->fmt.q_factor        = PCM_Q_FACTOR_31;
   }
   else if (PARAM_VAL_NATIVE == bit_width)
   {
      AR_MSG(DBG_HIGH_PRIO, "MFC OSRC: native mode bit_width received");
      media_fmt_ptr->fmt.bits_per_sample = PARAM_VAL_NATIVE;
      media_fmt_ptr->fmt.q_factor        = PARAM_VAL_NATIVE;
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "MFC OSRC: unsupported bit_width received %lu, not setting q factor", bit_width);
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Deinit the pcm mf cnv
____________________________________________________________________________________________________________________*/

void capi_mfc_osrc_mfc_deinit(capi_mfc_osrc_t *me_ptr)
{
   uint32_t port = 0;

   /* Free osrc_rs_lib_ptr before base mfc_osrc_deinit */
   if (NULL != me_ptr->pc[port].osrc_rs_lib_ptr)
   {
      osrc_rs_lib_deinit(me_ptr->pc[port].osrc_rs_lib_ptr);
      posal_memory_free(me_ptr->pc[port].osrc_rs_lib_ptr);
      me_ptr->pc[port].osrc_rs_lib_ptr = NULL;
   }
   mfc_osrc_deinit(&me_ptr->pc[port]);

   if (NULL != me_ptr->config.coef_sets_ptr)
   {
      posal_memory_free(me_ptr->config.coef_sets_ptr);
      me_ptr->config.coef_sets_ptr = NULL;
      me_ptr->config.num_coef_sets = 0;
   }

   if (NULL != me_ptr->scratch_buf_1[port].data_ptr)
   {
      posal_memory_free(me_ptr->scratch_buf_1[port].data_ptr);
      me_ptr->scratch_buf_1->data_ptr = NULL;
   }
   if (NULL != me_ptr->scratch_buf_2[port].data_ptr)
   {
      posal_memory_free(me_ptr->scratch_buf_2[port].data_ptr);
      me_ptr->scratch_buf_2->data_ptr = NULL;
   }
}
/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   The function sets the bitwidth based on qfactor received in input media format
____________________________________________________________________________________________________________________*/

uint32_t capi_mfc_osrc_mfc_set_extn_inp_mf_bitwidth(uint32_t q_factor)
{
   uint32_t bitwidth = 0;

   if (PCM_Q_FACTOR_15 == q_factor)
   {
      bitwidth = 16;
   }
   else if ((PCM_Q_FACTOR_27 == q_factor) || (PCM_Q_FACTOR_23 == q_factor))
   {
      bitwidth = 24;
   }
   else
   {
      bitwidth = 32;
   }

   return bitwidth;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Function to set output buffer size of the pcm encoder
  ____________________________________________________________________________________________________________________*/



#ifdef MFC_OSRC_profiling
void capi_mfc_osrc_profiling(capi_mfc_osrc_t *me_ptr, uint32_t num_samples)
{
   me_ptr->kpps_profile_data.sample_rate = me_ptr->in_media_fmt[0].format.sampling_rate;
   me_ptr->kpps_profile_data.total_cycles =
      me_ptr->kpps_profile_data.total_cycles +
      (me_ptr->kpps_profile_data.end_cycles - me_ptr->kpps_profile_data.start_cycles);
   me_ptr->kpps_profile_data.sample_count += num_samples;
   uint64_t frame_kpps =
      (((me_ptr->kpps_profile_data.end_cycles - me_ptr->kpps_profile_data.start_cycles) / num_samples) *
      me_ptr->kpps_profile_data.sample_rate) /
      (1000);
   if ((frame_kpps > me_ptr->kpps_profile_data.peak_kpps) &&
         (num_samples > (me_ptr->kpps_profile_data.sample_rate / 1000)))
   {
      me_ptr->kpps_profile_data.peak_kpps = frame_kpps;
   }
   
}

void capi_mfc_osrc_print_kpps(capi_mfc_osrc_t *me_ptr)
{
   me_ptr->kpps_profile_data.average_kpps =
      ((me_ptr->kpps_profile_data.total_cycles / me_ptr->kpps_profile_data.sample_count) *
      me_ptr->kpps_profile_data.sample_rate) /
      (1000);
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Profiling Total cycles :%llu", me_ptr->kpps_profile_data.total_cycles);
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Profiling Total Sample processed: %llu", me_ptr->kpps_profile_data.sample_count);
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Profiling Average KPPS: %llu", me_ptr->kpps_profile_data.average_kpps);
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Profiling Peak KPPS : %llu", me_ptr->kpps_profile_data.peak_kpps);
   me_ptr->kpps_profile_data.total_cycles = 0;
   me_ptr->kpps_profile_data.peak_kpps    = 0;
   me_ptr->kpps_profile_data.sample_count = 0;
   me_ptr->kpps_profile_data.average_kpps = 0;
}
#endif
