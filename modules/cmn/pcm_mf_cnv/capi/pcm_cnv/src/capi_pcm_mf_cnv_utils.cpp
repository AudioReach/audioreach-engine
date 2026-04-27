/* =========================================================================
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 * @file capi_pcm_mf_cnv_utils.cpp
 *
 * C source file to implement the Audio Post Processor Interface for
 * PCM_CNV Module
 */

#include "capi_pcm_mf_cnv_utils.h"

/*______________________________________________________________________________________________________________________

   DESCRIPTION:
   Resets the buffer lengths to zero. This can happen if some thing went wrong with the buffers during process
______________________________________________________________________________________________________________________*/
void capi_pcm_mf_cnv_reset_buffer_len(capi_pcm_mf_cnv_t *me_ptr,
                                      capi_buf_t *       src_buf_ptr,
                                      capi_buf_t *       dest_buf_ptr,
                                      uint32_t           port)
{
   if (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->in_media_fmt[port].format.data_interleaving)
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

   if (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->in_media_fmt[port].format.data_interleaving)
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

capi_err_t capi_pcm_mf_cnv_realloc_scratch_buf(capi_pcm_mf_cnv_t *me_ptr, capi_buf_t *sb_ptr, uint32_t max_data_len)
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
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Failed to allocate scratch buffer", me_ptr->type);
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
capi_err_t capi_pcm_mf_cnv_allocate_scratch_buffer(capi_pcm_mf_cnv_t *me_ptr, uint32_t max_data_len_per_ch)
{
   capi_err_t      result          = CAPI_EOK;
   uint32_t        port            = 0;
   uint32_t        b1_max_data_len = 0;
   uint32_t        r1_max_data_len = 0;
   uint32_t        c_max_data_len  = 0;
   uint32_t        r2_max_data_len = 0;
   uint32_t        b2_max_data_len = 0;
   uint32_t        max_data_len    = max_data_len_per_ch * me_ptr->in_media_fmt[port].format.num_channels;
   pc_media_fmt_t *inp_mf_ptr      = &me_ptr->pc[port].core_lib.input_media_fmt;
   pc_media_fmt_t *out_mf_ptr      = NULL;

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] Initial max data len %d", me_ptr->type, max_data_len);

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

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] b1 %lu, r1 %lu, c %lu, r2 %lu, b2 %lu",
           me_ptr->type,
           b1_max_data_len,
           r1_max_data_len,
           c_max_data_len,
           r2_max_data_len,
           b2_max_data_len);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] Final max data len %d", me_ptr->type, max_data_len);
   if (0 == me_ptr->pc[port].core_lib.no_of_buffering_stages)
   {
      return result;
   }
   else if (1 == me_ptr->pc[port].core_lib.no_of_buffering_stages)
   {
      // Catching the capi_err_t of capi_pcm_mf_cnv_realloc_scratch_buf in result variable that are CAPI_EOK and
      // CAPI_ENOMEMORY
      result = capi_pcm_mf_cnv_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_1[port], max_data_len);
      if (CAPI_FAILED(result))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "[%u] Scratch buffer 1 allocation failed, size = %lu, result = %lu",
                 me_ptr->type,
                 max_data_len,
                 result);
      }
      else
      {
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "[%u] Scratch buffer 1 allocated, size = %lu, result = %lu",
                 me_ptr->type,
                 max_data_len,
                 result);
      }
   }
   else
   {
      // Catching the capi_err_t of capi_pcm_mf_cnv_realloc_scratch_buf in result variable that are CAPI_EOK and
      // CAPI_ENOMEMORY
      result = capi_pcm_mf_cnv_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_1[port], max_data_len);
      if (CAPI_FAILED(result))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "[%u] Scratch buffer 1 allocation failed, so not reallocting memory for scratch buffer 2, size = %lu, "
                 "result = %lu",
                 me_ptr->type,
                 max_data_len,
                 result);
      }
      else
      {
         // Catching the capi_err_t of capi_pcm_mf_cnv_realloc_scratch_buf in result variable that are CAPI_EOK and
         // CAPI_ENOMEMORY
         result |= capi_pcm_mf_cnv_realloc_scratch_buf(me_ptr, &me_ptr->scratch_buf_2[port], max_data_len);
         if (CAPI_FAILED(result))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Scratch buffer 1 allocated but buffer 2 failed so freeing the scratch buffer 1, size = %lu, "
                    "result = %lu",
                    me_ptr->type,
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
                    "[%u] Scratch buffer 1 and 2 allocated, size = %lu, result = %lu",
                    me_ptr->type,
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
capi_err_t capi_pcm_mf_cnv_handle_data_port_op(capi_pcm_mf_cnv_t *me_ptr, capi_buf_t *params_ptr)
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
   if (data_ptr->num_ports > CAPI_PCM_CNV_MAX_IN_PORTS)
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

      // update the port state
      if (data_ptr->is_input_port)
      {
         me_ptr->in_port_state = port_state;
      }
      else
      {
         me_ptr->out_port_state = port_state;
      }

      // Handle port op specific things here.
      switch (data_ptr->opcode)
      {
         case INTF_EXTN_DATA_PORT_OPEN:
         {
            // nothing to do
            break;
         }
         case INTF_EXTN_DATA_PORT_START:
         {
            // unsuspend hw rs if it was suspended to
            if (me_ptr->in_port_state == DATA_PORT_STATE_STARTED && me_ptr->out_port_state == DATA_PORT_STATE_STARTED)
            {
               // if hw resampler is being used un-suspend, not that hwsw_rs_lib_ptr is not freed
               if (me_ptr->input_media_fmt_received && me_ptr->pc[PRI_IN_PORT_INDEX].hwsw_rs_lib_ptr)
               {
                  CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Both inputs are started and rs library memory is initated");

                  bool_t   IS_SUSPEND_FALSE   = FALSE;
                  uint32_t need_to_init_hw_rs = FALSE;
                  hwsw_rs_lib_set_hwrs_suspend_resume(IS_SUSPEND_FALSE,
                                                      me_ptr->pc[PRI_IN_PORT_INDEX].hwsw_rs_lib_ptr,
                                                      &need_to_init_hw_rs);
                  if (TRUE == need_to_init_hw_rs)
                  {
                     hwsw_rs_lib_check_create_resampler_instance(me_ptr->pc[PRI_IN_PORT_INDEX].hwsw_rs_lib_ptr,
                                                                 me_ptr->heap_info.heap_id);
                  }
               }
            }

            break;
         }
         case INTF_EXTN_DATA_PORT_SUSPEND:
         case INTF_EXTN_DATA_PORT_STOP:
         {
            // Suspend if ip/op is not started
            if (me_ptr->in_port_state != DATA_PORT_STATE_STARTED || me_ptr->out_port_state != DATA_PORT_STATE_STARTED)
            {
               // if hw resampler is being used suspend
               if (me_ptr->input_media_fmt_received && me_ptr->pc[PRI_IN_PORT_INDEX].hwsw_rs_lib_ptr)
               {
                  bool_t   IS_SUSPEND_TRUE = TRUE;
                  uint32_t dummy           = FALSE;
                  hwsw_rs_lib_set_hwrs_suspend_resume(IS_SUSPEND_TRUE,
                                                      me_ptr->pc[PRI_IN_PORT_INDEX].hwsw_rs_lib_ptr,
                                                      &dummy);
               }
            }

            break;
         }
         case INTF_EXTN_DATA_PORT_CLOSE:
         {
            // nothing to do
            break;
         }
         default:
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Port operation - Unsupported opcode: %lu", data_ptr->opcode);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         }
      }
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________

   DESCRIPTION:
   Checks if the channel type is valid
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_check_ch_type(capi_pcm_mf_cnv_t *me_ptr,
                                         const uint16_t *   channel_type,
                                         const uint32_t     array_size)
{
   for (uint8_t i = 0; (i < array_size) && (i < CAPI_MAX_CHANNELS_V2); i++)
   {
      if ((channel_type[i] < (uint16_t)PCM_CHANNEL_L) || (channel_type[i] > (uint16_t)PCM_MAX_CHANNEL_MAP_V2))
      {
         CNV_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "[%u] Unsupported channel type channel idx %d, channel type %d",
                 me_ptr->type,
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
static void *capi_pcm_mf_cnv_chmixer_get_coef_set_ptr(capi_pcm_mf_cnv_t *const me_ptr)
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
   capi_pcm_mf_cnv_chmixer_coef_set_t *coef_set_ptr = me_ptr->config.coef_sets_ptr;
   int16_t *                           coef_ptr     = NULL;

   for (int32_t i = (int32_t) (me_ptr->config.num_coef_sets) - 1; i >= 0; i--)
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
capi_err_t capi_pcm_mf_cnv_handle_input_media_fmt(capi_pcm_mf_cnv_t *  me_ptr,
                                                  capi_media_fmt_v2_t *temp_media_fmt_ptr,
                                                  uint32_t             port)
{
   capi_err_t capi_result = CAPI_EOK;

#ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] Received Input media format", me_ptr->type);
#endif

   if (((CAPI_MFC == me_ptr->type) && (TRUE != pcm_mf_cnv_mfc_is_supported_media_type(temp_media_fmt_ptr))) ||
       ((CAPI_MFC != me_ptr->type) && (TRUE != pcm_mf_cnv_is_supported_media_type(temp_media_fmt_ptr))))
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

   // configured media fmt is initialized to CAPI_MAX_FORMAT_TYPE
   // If configured media fmt is not updated and is same as CAPI_MAX_FORMAT_TYPE then out_media_fmt would be same as
   // in_media_fmt, else will copy the configured_media_fmt value to out_media_fmt.
   if (CAPI_MAX_FORMAT_TYPE == configured_media_fmt->data_format)
   {
      me_ptr->out_media_fmt[port].header.format_header.data_format =
         me_ptr->in_media_fmt[port].header.format_header.data_format;
   }
   else
   {
      me_ptr->out_media_fmt[port].header.format_header.data_format = configured_media_fmt->data_format;
   }

   if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width) // bit-width
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

   // To make sure lib checks pass
   if (CAPI_MFC == me_ptr->type)
   {
      me_ptr->extn_in_media_fmt[port].alignment = me_ptr->extn_out_media_fmt[port].alignment = PC_LSB_ALIGNED;
      me_ptr->extn_in_media_fmt[port].endianness = me_ptr->extn_out_media_fmt[port].endianness = PC_LITTLE_ENDIAN;
      me_ptr->extn_in_media_fmt[port].bit_width = capi_pcm_mf_cnv_mfc_set_extn_inp_mf_bitwidth(in_std_ptr->q_factor);
      if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width) // bit-width
      {
         me_ptr->extn_out_media_fmt[port].bit_width = me_ptr->extn_in_media_fmt[port].bit_width;
      }
   }

   // resampler_reinit is set to FALSE
   capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, FALSE);
   capi_result |= capi_pcm_mf_cnv_check_and_raise_output_media_format_event(me_ptr);

   // CNV_MSG( me_ptr->miid,DBG_HIGH_PRIO, "[%u] Input media format - Done", me_ptr->type);
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the output media fmt is valid, if so raises the out put media fmt event
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_check_and_raise_output_media_format_event(capi_pcm_mf_cnv_t *me_ptr)
{
   uint32_t   port        = 0;
   capi_err_t capi_result = CAPI_EOK;

   if (((CAPI_MFC == me_ptr->type) && (TRUE == pcm_mf_cnv_mfc_is_supported_media_type(&me_ptr->out_media_fmt[port]))) ||
       ((CAPI_MFC != me_ptr->type) && (TRUE == pcm_mf_cnv_is_supported_media_type(&me_ptr->out_media_fmt[port]))))
   {
      capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->out_media_fmt[port], FALSE, port);
   }
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function raises dm event to the fwk to inform the input samples required/output produced
  ____________________________________________________________________________________________________________________*/
static capi_err_t capi_pcm_mf_cnv_raise_dm_event(capi_pcm_mf_cnv_t *                 me_ptr,
                                                 fwk_extn_dm_param_id_req_samples_t *req_samples_ptr,
                                                 uint32_t                            event_id)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_event_data_to_dsp_service_t event_payload;
   capi_event_info_t                event_info;
   event_payload.param_id                = event_id;
   event_payload.token                   = 0;
   event_payload.payload.data_ptr        = (int8_t *)req_samples_ptr;
   event_payload.payload.actual_data_len = sizeof(fwk_extn_dm_param_id_req_samples_t);
   event_payload.payload.max_data_len    = sizeof(fwk_extn_dm_param_id_req_samples_t);
   event_info.payload.data_ptr           = (int8_t *)&event_payload;
   event_info.payload.actual_data_len    = sizeof(event_payload);
   event_info.payload.max_data_len       = sizeof(event_payload);
   event_info.port_info.is_valid         = FALSE;
   capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info);
   if (CAPI_FAILED(capi_result))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "[%u] Failed to send report samples event with result %d",
              me_ptr->type,
              capi_result);
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function is used to query the HW resampler for expected input samples to produce fixed out samples.
   Update the return value at expected_in_samples_per_ch_ptr.
  ____________________________________________________________________________________________________________________*/
static void capi_pcm_cnv_hwrs_get_exp_in_samples(capi_pcm_mf_cnv_t *me_ptr,
                                                 uint32_t           req_out_samples,
                                                 uint32_t *         expected_in_samples_per_ch_ptr)
{
   uint32_t output_samples_per_ch = req_out_samples;

   // get frame size
   uint32_t out_samples_per_ms   = me_ptr->out_media_fmt[0].format.sampling_rate / 1000;
   uint32_t in_samples_per_ms    = me_ptr->in_media_fmt[0].format.sampling_rate / 1000;
   uint32_t out_frame_size_in_ms = output_samples_per_ch / out_samples_per_ms;

   const uint32_t EXTRA_1MS_FOR_VARIABLE_INPUT = 1;

   // assuming input buffer has 1ms extra samples, this is just to indicate HW resampler that enough input is available
   // to produce fixed output. In general HW resampler may need only at max one extra sample to fill the output.
   uint32_t max_available_inp_samples = (out_frame_size_in_ms + EXTRA_1MS_FOR_VARIABLE_INPUT) * in_samples_per_ms;

   uint32_t expected_in_samples_per_ch  = 0;
   uint32_t expected_out_samples_per_ch = 0;
   hwsw_rs_lib_process_get_hw_process_info(me_ptr->pc[0].hwsw_rs_lib_ptr,
                                           max_available_inp_samples,
                                           output_samples_per_ch,
                                           &expected_in_samples_per_ch,
                                           &expected_out_samples_per_ch);

   // this is the actual no of samples required to fill the expected output
   *expected_in_samples_per_ch_ptr = expected_in_samples_per_ch;

#ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] HW resampler requested, available_in=%lu exp_in=%lu, exp_out=%lu",
           me_ptr->type,
           max_available_inp_samples,
           expected_in_samples_per_ch,
           expected_out_samples_per_ch);
#endif
   return;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function is used to query the resampler of req input samples to produce fixed out samples
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_get_fixed_out_samples(capi_pcm_mf_cnv_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   // if it is the software dyn resampler
   if (NULL != me_ptr->pc[0].hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO] &&
       me_ptr->pc[0].hwsw_rs_lib_ptr->sw_rs_mem_ptr[STAGE_ZERO]->drs_mem_ptr.pStructMem)
   {
      // compute input samples required
      uint32_t expected_in_samples = pc_get_fixed_out_samples(&me_ptr->pc[0], me_ptr->dm_info.req_out_samples);

      me_ptr->dm_info.expected_in_samples = expected_in_samples;
   }
   else // HW rs
   {
      capi_pcm_cnv_hwrs_get_exp_in_samples(me_ptr,
                                           me_ptr->dm_info.req_out_samples,
                                           &me_ptr->dm_info.expected_in_samples);
   }

#ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] Fixed out samples %lu, in req %lu",
           me_ptr->type,
           me_ptr->dm_info.req_out_samples,
           me_ptr->dm_info.expected_in_samples);
#endif

   if (me_ptr->dm_info.expected_in_samples)
   {
      // raise event
      fwk_extn_dm_param_id_req_samples_t req_samples;
      req_samples.is_input                           = TRUE;
      req_samples.num_ports                          = 1;
      req_samples.req_samples[0].port_index          = 0;
      req_samples.req_samples[0].samples_per_channel = me_ptr->dm_info.expected_in_samples;
      capi_result |= capi_pcm_mf_cnv_raise_dm_event(me_ptr, &req_samples, FWK_EXTN_DM_EVENT_ID_REPORT_SAMPLES);
   }

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
  This function is used to calculate the max input samples req to produce max output samples
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_get_max_in_samples(capi_pcm_mf_cnv_t *me_ptr, uint32_t max_out_samples)
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
              "[%u] DM get max samples cannot be returned, input/output media format not set",
              me_ptr->type);
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
   capi_result |= capi_pcm_mf_cnv_raise_dm_event(me_ptr, &req_samples, FWK_EXTN_DM_EVENT_ID_REPORT_MAX_SAMPLES);

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] set max out samples to %d, expected max in samples %d",
           me_ptr->type,
           max_out_samples,
           input_samples);
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   This function checks if it is a fractional resampling scenario, called from lib init after inp and out mf are known
  ____________________________________________________________________________________________________________________*/
bool_t capi_pcm_mf_cnv_is_fractional_media_type(const capi_pcm_mf_cnv_t *me_ptr)
{
   uint64_t in_sr  = (uint64_t)me_ptr->in_media_fmt[0].format.sampling_rate;
   uint64_t out_sr = (uint64_t)me_ptr->out_media_fmt[0].format.sampling_rate;

   if (me_ptr->frame_size_us)
   {
      uint64_t in_frame_size  = (uint64_t)capi_cmn_us_to_samples(me_ptr->frame_size_us, in_sr);
      uint64_t out_frame_size = (uint64_t)capi_cmn_us_to_samples(me_ptr->frame_size_us, out_sr);

      CNV_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "[%u] in_frame_size %lu, out_frame_size %lu",
              me_ptr->type,
              in_frame_size,
              out_frame_size);

      return (in_frame_size * out_sr == out_frame_size * in_sr) ? FALSE : TRUE;
   }
   else
   {
      CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] frame size is not set.", me_ptr->type);
      return ((in_sr % 1000) || (out_sr % 1000)) ? TRUE : FALSE;
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   1. check and update dm enable or disable to the fwk
   2. DM is disabled if frame-size ratio (in samples) is same as sampling rate ratio.
   3. DM is enabled if frame-size ratio (in samples) is not same as sampling rate ratio.
______________________________________________________________________________________________________________________*/
void capi_pcm_mf_cnv_update_dm_disable(capi_pcm_mf_cnv_t *me_ptr)
{
   if ((CAPI_MFC == me_ptr->type) && me_ptr->input_media_fmt_received)
   {
      // Check should be for inp/out fractional media formats
      if (capi_pcm_mf_cnv_is_fractional_media_type(me_ptr))
      {
         me_ptr->dm_info.is_dm_disabled = FALSE;
         capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_ENABLED_DM);
      }
      else // if it is not fractional disable dm
      {
         me_ptr->dm_info.is_dm_disabled = TRUE;
         capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_DISABLED_DM);
      }
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
ar_result_t capi_pcm_mf_cnv_lib_init(capi_pcm_mf_cnv_t *me_ptr, bool_t fir_iir_rs_switch)
{
   ar_result_t                             result             = AR_EOK;
   uint32_t                                port               = 0;
   capi_standard_data_format_v2_t *        in_std_ptr         = &me_ptr->in_media_fmt[port].format;
   capi_standard_data_format_v2_t *        out_std_ptr        = &me_ptr->out_media_fmt[port].format;
   fwk_extn_pcm_param_id_media_fmt_extn_t *in_extn_media_ptr  = &me_ptr->extn_in_media_fmt[port];
   fwk_extn_pcm_param_id_media_fmt_extn_t *out_extn_media_ptr = &me_ptr->extn_out_media_fmt[port];
   pc_media_fmt_t                          input_media_fmt;
   pc_media_fmt_t                          output_media_fmt;

   if (CAPI_MFC != me_ptr->type)
   {
      if (!((pcm_mf_cnv_is_supported_media_type(&me_ptr->in_media_fmt[port])) &&
            (pcm_mf_cnv_is_supported_media_type(&me_ptr->out_media_fmt[port])) &&
            (CAPI_PCM_VALID == pcm_mf_cnv_is_supported_extn_media_type(in_extn_media_ptr)) &&
            (CAPI_PCM_VALID == pcm_mf_cnv_is_supported_extn_media_type(out_extn_media_ptr))))
      {
         return AR_EOK;
      }
   }
   else
   {
      if (!((pcm_mf_cnv_mfc_is_supported_media_type(&me_ptr->in_media_fmt[port])) &&
            (pcm_mf_cnv_mfc_is_supported_media_type(&me_ptr->out_media_fmt[port])) &&
            (CAPI_PCM_VALID == pcm_mf_cnv_is_supported_extn_media_type(in_extn_media_ptr)) &&
            (CAPI_PCM_VALID == pcm_mf_cnv_is_supported_extn_media_type(out_extn_media_ptr))))
      {
         return AR_EOK;
      }
   }

   input_media_fmt.sampling_rate = in_std_ptr->sampling_rate;
   input_media_fmt.bit_width     = in_extn_media_ptr->bit_width;
   input_media_fmt.word_size     = in_std_ptr->bits_per_sample;
   input_media_fmt.q_factor      = in_std_ptr->q_factor;
   input_media_fmt.interleaving  = capi_interleaved_to_pc(in_std_ptr->data_interleaving);
   input_media_fmt.endianness    = (pc_endian_t)in_extn_media_ptr->endianness;
   input_media_fmt.alignment     = (pc_alignment_t)in_extn_media_ptr->alignment;
   input_media_fmt.data_format   = capi_dataformat_to_pc(me_ptr->in_media_fmt[port].header.format_header.data_format);
   input_media_fmt.byte_combo    = pc_classify_mf(&input_media_fmt);
   input_media_fmt.num_channels  = in_std_ptr->num_channels;
   input_media_fmt.channel_type  = &in_std_ptr->channel_type[0];

   output_media_fmt.sampling_rate = out_std_ptr->sampling_rate;
   output_media_fmt.bit_width     = out_extn_media_ptr->bit_width;
   output_media_fmt.word_size     = out_std_ptr->bits_per_sample;
   output_media_fmt.q_factor      = out_std_ptr->q_factor;
   output_media_fmt.interleaving  = capi_interleaved_to_pc(out_std_ptr->data_interleaving);
   output_media_fmt.endianness    = (pc_endian_t)out_extn_media_ptr->endianness;
   output_media_fmt.alignment     = (pc_alignment_t)out_extn_media_ptr->alignment;
   output_media_fmt.data_format   = capi_dataformat_to_pc(me_ptr->out_media_fmt[port].header.format_header.data_format);
   output_media_fmt.byte_combo    = pc_classify_mf(&output_media_fmt);
   output_media_fmt.num_channels  = out_std_ptr->num_channels;
   output_media_fmt.channel_type  = &out_std_ptr->channel_type[0];

   if ((PC_INVALID_CMBO == input_media_fmt.byte_combo) || (PC_INVALID_CMBO == output_media_fmt.byte_combo))
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "[%u] Invalid byte combo set for INPUT bw: %d, bps: %d and qfactor: %d or OUTPUT "
              "bw: %d, bps: %d and qfactor: %d",
              me_ptr->type,
              input_media_fmt.bit_width,
              input_media_fmt.word_size,
              input_media_fmt.q_factor,
              output_media_fmt.bit_width,
              output_media_fmt.word_size,
              output_media_fmt.q_factor);
      return AR_EFAILED;
   }

#ifdef PCM_CNV_DEBUG
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] AFTER SETTING INPUT", me_ptr->type);
   capi_pcm_mf_cnv_print_media_fmt(me_ptr, &input_media_fmt, port);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] AFTER SETTING OUTPUT", me_ptr->type);
   capi_pcm_mf_cnv_print_media_fmt(me_ptr, &output_media_fmt, port);
#endif

   result = pc_init((pc_lib_t *)&me_ptr->pc[port],
                    (pc_media_fmt_t *)&input_media_fmt,
                    (pc_media_fmt_t *)&output_media_fmt,
                    (void *)capi_pcm_mf_cnv_chmixer_get_coef_set_ptr(me_ptr),
                    (uint32_t)me_ptr->heap_info.heap_id,
                    &me_ptr->lib_enable,
                    fir_iir_rs_switch,
                    me_ptr->miid,
                    me_ptr->type);

   if ((AR_EOK != result) || (me_ptr->lib_enable == FALSE))
   {
      me_ptr->lib_enable = FALSE;
      capi_pcm_mf_cnv_raise_process_check_event(me_ptr);
      pc_deinit((pc_lib_t *)&me_ptr->pc[port]);
      return result;
   }

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] PC Init Done, Pre modules -E1-%lu, Float_to_Fixed-%lu, I1-%lu, B1-%lu, R1-%lu, C-%lu",
           me_ptr->type,
           me_ptr->pc[port].core_lib.flags.ENDIANNESS_PRE,
           me_ptr->pc[port].core_lib.flags.DATA_CNV_FLOAT_TO_FIXED,
           me_ptr->pc[port].core_lib.flags.INT_DEINT_PRE,
           me_ptr->pc[port].core_lib.flags.BYTE_CNV_PRE,
           me_ptr->pc[port].core_lib.flags.RESAMPLER_PRE,
           me_ptr->pc[port].core_lib.flags.CHANNEL_MIXER);

   CNV_MSG(me_ptr->miid,
           DBG_HIGH_PRIO,
           "[%u] PC Init Done , Post modules R2-%lu, B2-%lu, I2-%lu, Fixed_to_Float-%lu, E2-%lu",
           me_ptr->type,
           me_ptr->pc[port].core_lib.flags.RESAMPLER_POST,
           me_ptr->pc[port].core_lib.flags.BYTE_CNV_POST,
           me_ptr->pc[port].core_lib.flags.INT_DEINT_POST,
           me_ptr->pc[port].core_lib.flags.DATA_CNV_FIXED_TO_FLOAT,
           me_ptr->pc[port].core_lib.flags.ENDIANNESS_POST);

   // this flag should be set before dm mode setting.
   me_ptr->input_media_fmt_received = TRUE;

   // raise TRUE or FALSE
   capi_pcm_mf_cnv_raise_process_check_event(me_ptr);
   capi_pcm_mf_cnv_update_and_raise_events(me_ptr);

   // if not fractional, and dm mode is configured, raise disabled event
   capi_pcm_mf_cnv_update_dm_disable(me_ptr);

   // We keep allocated length 0 and actually recalculate the required length
   // during the 1st process call.
   // Also, this helps in realloc-ing the scratch buffer sizes if there is any change in media fmt dynamically
   me_ptr->max_data_len_per_ch = 0;

   // CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] lib init done", me_ptr->type);

   return AR_EOK;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Checks if the pcm cnv media fmt is has all the supported values
  ____________________________________________________________________________________________________________________*/
bool_t pcm_mf_cnv_is_supported_media_type(const capi_media_fmt_v2_t *format_ptr)
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
bool_t pcm_mf_cnv_mfc_is_supported_media_type(const capi_media_fmt_v2_t *fmt_ptr)
{
   if (NULL == fmt_ptr)
   {
      return FALSE;
   }

   // The underlying APPIs can only support de-interleaved unpacked data.
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
capi_pcm_mf_cnv_result_t pcm_mf_cnv_is_supported_extn_media_type(const fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr)
{
   capi_pcm_mf_cnv_result_t result = CAPI_PCM_VALID;

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

   if ((PC_LSB_ALIGNED != extn_ptr->alignment) && (PC_MSB_ALIGNED != extn_ptr->alignment))
   {
      return CAPI_PCM_ERROR;
   }

   if ((PC_LITTLE_ENDIAN != extn_ptr->endianness) && (PC_BIG_ENDIAN != extn_ptr->endianness))
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
bool_t pcm_mf_cnv_is_supported_out_media_type(const payload_pcm_output_format_cfg_t *format_ptr, uint32_t data_format)
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
   Function to raise various events of the pcm_mf_cnv module
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_raise_process_check_event(capi_pcm_mf_cnv_t *me_ptr)
{
   if ((CAPI_MFC == me_ptr->type) || (me_ptr->type == CAPI_PCM_CONVERTER))
   {
      capi_cmn_update_process_check_event(&me_ptr->cb_info, me_ptr->lib_enable);
   }
   return CAPI_EOK;
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Function to raise various events of the pcm_mf_cnv module
  ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_update_and_raise_events(capi_pcm_mf_cnv_t *me_ptr)
{
   capi_err_t capi_result    = CAPI_EOK;
   uint32_t   port           = 0;
   uint32_t   bw             = 0;
   uint32_t   kpps           = 0;
   uint32_t   new_algo_delay = 0;

   if (NULL == me_ptr->cb_info.event_cb)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u]  Event callback is not set. Unable to raise events!", me_ptr->type);
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   // This additional kpps is added to capi operations during process
   pc_get_kpps_and_bw(&me_ptr->pc[port], &kpps, &bw);
   kpps           = kpps + PCM_CNV_CAPI_KPPS;
   new_algo_delay = pc_get_algo_delay(&me_ptr->pc[port]);

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
capi_err_t capi_pcm_mf_cnv_process_set_properties(capi_pcm_mf_cnv_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t   capi_result = CAPI_EOK;
   capi_prop_t *prop_array  = proplist_ptr->prop_ptr;
   uint32_t     i           = 0;
   uint32_t     port        = 0;

   if (NULL == prop_array)
   {
      CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u]  Set property, received null property ptr.", me_ptr->type);
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_info, &me_ptr->cb_info, TRUE);
   if (CAPI_EOK != capi_result)
   {
      CNV_MSG(me_ptr->miid,
              DBG_ERROR_PRIO,
              "[%u] Set basic properties failed with result %lu",
              me_ptr->type,
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
            // If type is MFC and pre or post resamplers are enabled
            if ((CAPI_MFC == me_ptr->type) &&
                ((me_ptr->pc[0].core_lib.flags.RESAMPLER_PRE) || (me_ptr->pc[0].core_lib.flags.RESAMPLER_POST)))
            {
               if ((IIR_RESAMPLER == me_ptr->pc[0].resampler_type) && (me_ptr->pc[0].iir_rs_lib_ptr))
               {
                  capi_result = iir_rs_lib_clear_algo_memory(me_ptr->pc[0].iir_rs_lib_ptr);
               }
               else if ((FIR_RESAMPLER == me_ptr->pc[0].resampler_type) && (me_ptr->pc[0].hwsw_rs_lib_ptr))
               {
                  capi_result |= hwsw_rs_lib_algo_reset(me_ptr->pc[0].hwsw_rs_lib_ptr);
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
                  if (((CAPI_PCM_CONVERTER != me_ptr->type) && (CAPI_PCM_DECODER != me_ptr->type) &&
                       (CAPI_PCM_ENCODER != me_ptr->type)) ||
                      (!pc_is_floating_point_data_format_supported()))
                  {
                     CNV_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "[%u] Floating point data format not supported by this module/chipset",
                             me_ptr->type);
                     return CAPI_EUNSUPPORTED;
                  }
               }

               capi_result |= capi_pcm_mf_cnv_handle_input_media_fmt(me_ptr, &temp_media_fmt, port);
               if (AR_EOK == capi_result)
               {
                  if ((CAPI_PCM_ENCODER == me_ptr->type) &&
                      ((PCM_ENCODER_CONFIG_FRAME_SIZE_IN_SAMPLES == me_ptr->frame_size.frame_size_type &&
                        0 != me_ptr->frame_size.frame_size_in_samples) ||
                       (PCM_ENCODER_CONFIG_FRAME_SIZE_IN_MICROSECONDS == me_ptr->frame_size.frame_size_type &&
                        0 != me_ptr->frame_size.frame_size_in_us)))
                  {
                     CNV_MSG(me_ptr->miid,
                             DBG_HIGH_PRIO,
                             "[%u] Updating the output frame size for pcm encoder ",
                             me_ptr->type);
                     capi_result |= capi_pcm_encoder_update_frame_size(me_ptr);
                  }
                  else if ((CAPI_PCM_DECODER == me_ptr->type) || (CAPI_PCM_ENCODER == me_ptr->type))
                  {
                     if (me_ptr->frame_size_us)
                     {
                        uint32_t frame_size_ms = me_ptr->frame_size_us / 1000;
                        uint32_t input_port_threshold =
                           frame_size_ms * CAPI_CMN_BITS_TO_BYTES(me_ptr->in_media_fmt[0].format.bits_per_sample) *
                           tu_get_unit_frame_size(me_ptr->in_media_fmt[0].format.sampling_rate) *
                           me_ptr->in_media_fmt[0].format.num_channels;
                        uint32_t output_port_threshold =
                           frame_size_ms * CAPI_CMN_BITS_TO_BYTES(me_ptr->out_media_fmt[0].format.bits_per_sample) *
                           tu_get_unit_frame_size(me_ptr->out_media_fmt[0].format.sampling_rate) *
                           me_ptr->out_media_fmt[0].format.num_channels;
                        if (input_port_threshold > 0)
                        {
                           // input threshold
                           if ((capi_result |= capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info,
                                                                                         input_port_threshold,
                                                                                         TRUE,
                                                                                         0) != CAPI_EOK))
                           {
                              CNV_MSG(me_ptr->miid,
                                      DBG_ERROR_PRIO,
                                      "[%u] failed to set input port threshold value",
                                      me_ptr->type);
                              return CAPI_EFAILED;
                           }
                        }
                        if (output_port_threshold > 0)
                        {
                           // output threshold
                           if ((capi_result |= capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info,
                                                                                         output_port_threshold,
                                                                                         FALSE,
                                                                                         0) != CAPI_EOK))

                           {
                              CNV_MSG(me_ptr->miid,
                                      DBG_ERROR_PRIO,
                                      "[%u] failed to set output port threshold value",
                                      me_ptr->type);
                              return CAPI_EFAILED;
                           }
                        }
                        CNV_MSG(me_ptr->miid,
                                DBG_LOW_PRIO,
                                "[%u] Input port threshold is %d, "
                                "Output port threshold is %d",
                                me_ptr->type,
                                input_port_threshold,
                                output_port_threshold);
                     }
                  }
               }
            }
            else
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "[%u] Set property id 0x%lx Bad param size %lu",
                       me_ptr->type,
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
#if 0
               CNV_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "MF_PCM_CNV: This module-id 0x%08lX, instance-id 0x%08lX",
                       data_ptr->module_id,
                       me_ptr->miid);
#endif
            }
            else
            {
               CNV_MSG(MIID_UNKNOWN,
                       DBG_ERROR_PRIO,
                       "MF_PCM_CNV: Set, Param id 0x%lx Bad param size %lu",
                       (uint32_t)prop_array[i].id,
                       payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_LOGGING_INFO:
         {
            // TODO: to store module logging info
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
#if 0
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] Set property id %#x. Not supported.",
                    me_ptr->type,
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
                 "[%u] Set property for %#x failed with opcode %lu",
                 me_ptr->type,
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
capi_err_t capi_pcm_mf_cnv_process_get_properties(capi_pcm_mf_cnv_t *    me_ptr,
                                                  capi_proplist_t *      proplist_ptr,
                                                  capi_pcm_mf_cnv_type_t type)
{
   capi_err_t        capi_result                 = CAPI_EOK;
   capi_prop_t *     prop_array                  = proplist_ptr->prop_ptr;
   uint32_t          port                        = 0;
   uint32_t          fwk_extn_ids_arr[1]         = { FWK_EXTN_PCM };
   uint32_t          fwk_extn_ids_arr_mfc[2]     = { FWK_EXTN_DM, FWK_EXTN_CONTAINER_FRAME_DURATION };
   uint32_t          fwk_extn_ids_arr_enc_dec[2] = { FWK_EXTN_PCM, FWK_EXTN_THRESHOLD_CONFIGURATION };
   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_pcm_mf_cnv_t));
   mod_prop.stack_size         = PCM_CNV_STACK_SIZE;
   mod_prop.is_inplace         = FALSE;
   mod_prop.req_data_buffering = (CAPI_MFC == type);
   mod_prop.max_metadata_size  = 0;

   if (CAPI_MFC == type)
   {
      mod_prop.num_fwk_extns    = 2;
      mod_prop.fwk_extn_ids_arr = fwk_extn_ids_arr_mfc;
   }
   else if (CAPI_PCM_DECODER == type || CAPI_PCM_ENCODER == type)
   {
      mod_prop.num_fwk_extns    = 2;
      mod_prop.fwk_extn_ids_arr = fwk_extn_ids_arr_enc_dec;
   }
   else
   {
      mod_prop.num_fwk_extns    = 1;
      mod_prop.fwk_extn_ids_arr = fwk_extn_ids_arr;
   }

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
                  case INTF_EXTN_DATA_PORT_OPERATION:
                  {
                     // Only MFC supports data port ops, MFC uses port state to release hw resources upon stop/suspend
                     if (CAPI_MFC == type)
                     {
                        curr_intf_extn_desc_ptr->is_supported = TRUE;
                     }
                     else
                     {
                        curr_intf_extn_desc_ptr->is_supported = FALSE;
                     }
                     break;
                  }
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }
               }
#if 0
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
                      "CAPI_MF_PCM_CNV: Get property id 0x%lx failed due to invalid/unexpected values",
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
                      "CAPI_MF_PCM_CNV: Get property for ID %#x. failed, NULL me_ptr.",
                      prop_array[i].id);
               break;
            }

            uint32_t threshold = 1;

            capi_result |= capi_cmn_handle_get_port_threshold(&prop_array[i], threshold);

            break;
         }

         default:
         {
            // AR_MSG(DBG_HIGH_PRIO, "CAPI_MF_PCM_CNV: Get property for ID %#x. Not supported.", prop_array[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

#if 0
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_HIGH_PRIO,
                "CAPI_MF_PCM_CNV: Get property for %#x failed with opcode %lu",
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

void capi_pcm_mf_cnv_mfc_set_output_bps_qf(capi_pc_configured_mf_t *media_fmt_ptr)
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
      AR_MSG(DBG_HIGH_PRIO, "MFC: native mode bit_width received");
      media_fmt_ptr->fmt.bits_per_sample = PARAM_VAL_NATIVE;
      media_fmt_ptr->fmt.q_factor        = PARAM_VAL_NATIVE;
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "MFC: unsupported bit_width received %lu, not setting q factor", bit_width);
   }
}

/*_____________________________________________________________________________________________________________________
   DESCRIPTION:
   Deinit the pcm mf cnv
____________________________________________________________________________________________________________________*/

void capi_pcm_mf_cnv_mfc_deinit(capi_pcm_mf_cnv_t *me_ptr)
{
   uint32_t port = 0;

   pc_deinit((pc_lib_t *)&me_ptr->pc[port]);

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

uint32_t capi_pcm_mf_cnv_mfc_set_extn_inp_mf_bitwidth(uint32_t q_factor)
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

capi_err_t capi_pcm_encoder_update_frame_size(capi_pcm_mf_cnv_t *me_ptr)
{
   capi_err_t capi_result           = CAPI_EOK;
   uint32_t   frame_size_type       = me_ptr->frame_size.frame_size_type;
   uint32_t   frame_size_in_samples = me_ptr->frame_size.frame_size_in_samples;
   uint32_t   frame_size_in_us      = me_ptr->frame_size.frame_size_in_us;
   uint32_t   frame_size_in_bytes   = 0;
   uint32_t   num_ch                = me_ptr->in_media_fmt[0].format.num_channels;
   uint32_t   bps                   = (me_ptr->in_media_fmt[0].format.bits_per_sample) / 8;
   uint32_t   sr                    = me_ptr->in_media_fmt[0].format.sampling_rate;

   if (1 == frame_size_type)
   {
      frame_size_in_bytes = frame_size_in_samples * num_ch * bps;
   }
   else if (2 == frame_size_type)
   {
      frame_size_in_bytes = (uint32_t)(((uint64_t)(frame_size_in_us * sr) / 1000000) * bps * num_ch);
   }

   CNV_MSG(me_ptr->miid,
           DBG_LOW_PRIO,
           "[%u]PARAM_ID_PCM_ENCODER_FRAME_SIZE  frame_size_type is %d,"
           "frame_size in samples is %d, frame size in microsecond is %d",
           me_ptr->type,
           frame_size_type,
           frame_size_in_samples,
           frame_size_in_us);
   CNV_MSG(me_ptr->miid,
           DBG_LOW_PRIO,
           "[%u]Number of channels is %d, sampling rate is %d,"
           "bytes per sample is %d,so calculated frame_size_in_bytes is %d",
           me_ptr->type,
           num_ch,
           sr,
           bps,
           frame_size_in_bytes);
   if (frame_size_in_bytes > 0)
   {
      // input threshold
      if ((capi_result |=
           capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info, frame_size_in_bytes, TRUE, 0) != CAPI_EOK))
      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] failed to set input port threshold value", me_ptr->type);
         return CAPI_EFAILED;
      }
      // output threshold
      if ((capi_result |=
           capi_cmn_update_port_data_threshold_event(&me_ptr->cb_info, frame_size_in_bytes, FALSE, 0) != CAPI_EOK))

      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] failed to set output port threshold value", me_ptr->type);
         return CAPI_EFAILED;
      }
   }
   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Successfully raised input and output threshold for the pcm encoder module");

   return capi_result;
}
