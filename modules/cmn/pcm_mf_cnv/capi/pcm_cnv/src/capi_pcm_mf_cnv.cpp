/* =========================================================================
   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 * @file capi_pcm_mf_cnv.cpp
 *
 * Cpp source file to implement the Common Audio Post Processor Interface
 * PCM_CNV block
 */

#include "capi_pcm_mf_cnv_utils.h"
#include "capi_pcm_mf_cnv_i.h"

capi_err_t capi_pcm_mf_cnv_common_get_static_properties(capi_proplist_t *      init_set_properties,
                                                        capi_proplist_t *      static_properties,
                                                        capi_pcm_mf_cnv_type_t type);

capi_err_t capi_pcm_mf_cnv_common_init(capi_t *_pif, capi_proplist_t *init_set_properties, capi_pcm_mf_cnv_type_t type);

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM_CNV function to get the static properties
 ______________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_cnv_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "CNV get static properties called");
   return capi_pcm_mf_cnv_common_get_static_properties(init_set_properties, static_properties, CAPI_PCM_CONVERTER);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Initialize the CAPI PCM CNV Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_cnv_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "CNV get static properties called");
   return capi_pcm_mf_cnv_common_init(_pif, init_set_properties, CAPI_PCM_CONVERTER);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM ENC function to get the static properties
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_enc_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "ENC get static properties called");
   return capi_pcm_mf_cnv_common_get_static_properties(init_set_properties, static_properties, CAPI_PCM_ENCODER);
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Initialize the CAPI PCM ENC Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_enc_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "ENC get static properties called");
   return capi_pcm_mf_cnv_common_init(_pif, init_set_properties, CAPI_PCM_ENCODER);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM DEC function to get the static properties
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_dec_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "DEC get static properties called");
   return capi_pcm_mf_cnv_common_get_static_properties(init_set_properties, static_properties, CAPI_PCM_DECODER);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Initialize the CAPI PCM DEC Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_dec_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "DEC get static properties called");
   return capi_pcm_mf_cnv_common_init(_pif, init_set_properties, CAPI_PCM_DECODER);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM_CNV function to get the static properties
 ______________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "MFC get static properties called");
   return capi_pcm_mf_cnv_common_get_static_properties(init_set_properties, static_properties, CAPI_MFC);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Initialize the CAPI PCM CNV Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "MFC get static properties called");
   return capi_pcm_mf_cnv_common_init(_pif, init_set_properties, CAPI_MFC);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM_CNV function to get the static properties
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_common_get_static_properties(capi_proplist_t *      init_set_properties,
                                                        capi_proplist_t *      static_properties,
                                                        capi_pcm_mf_cnv_type_t type)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL != static_properties)
   {
      capi_result = capi_pcm_mf_cnv_process_get_properties((capi_pcm_mf_cnv_t *)NULL, static_properties, type);
      if (CAPI_FAILED(capi_result))
      {
         // CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "[%d] get static properties failed!", type);
         return capi_result;
      }
   }
   else
   {
      CNV_MSG(MIID_UNKNOWN,
              DBG_ERROR_PRIO,
              " %d: Get static properties received bad pointer, 0x%p",
              type,
              static_properties);
   }

   return capi_result;
}

/*______________________________________________________________________________________________________________________

 DESCRIPTION:
 Initialize the CAPI PCM_CNV Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_common_init(capi_t *_pif, capi_proplist_t *init_set_properties, capi_pcm_mf_cnv_type_t type)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == init_set_properties)
   {
      CNV_MSG(MIID_UNKNOWN,
              DBG_ERROR_PRIO,
              " %d: Init received bad pointer, 0x%p, 0x%p",
              type,
              _pif,
              init_set_properties);

      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_pcm_mf_cnv_t));

   me_ptr->vtbl.vtbl_ptr = capi_pcm_mf_cnv_get_vtable();

   me_ptr->type = type;

   me_ptr->lib_enable = FALSE;

   if ((CAPI_PCM_DECODER == me_ptr->type))
   {
      me_ptr->perf_mode     = CAPI_PCM_PERF_MODE_LOW_LATENCY;
      me_ptr->frame_size_us = 1000;
   }
   else
   {
      me_ptr->perf_mode     = CAPI_PCM_PERF_MODE_INVALID;
      me_ptr->frame_size_us = 0;
      /* frame_size_type, frame_size_in_samples and frame_size_in_microsecond are set to zero at line 166
       while memsetting the me_ptr */
   }

   for (uint32_t i = 0; i < CAPI_PCM_CNV_MAX_IN_PORTS; i++)
   {
      capi_cmn_init_media_fmt_v2(&me_ptr->in_media_fmt[i]);
   }

   for (uint32_t i = 0; i < CAPI_PCM_CNV_MAX_OUT_PORTS; i++)
   {
      capi_cmn_init_media_fmt_v2(&me_ptr->out_media_fmt[i]);
   }

   for (uint32_t i = 0; i < CAPI_PCM_CNV_MAX_OUT_PORTS; i++)
   {
      me_ptr->configured_media_fmt[i].data_format         = CAPI_MAX_FORMAT_TYPE;
      me_ptr->configured_media_fmt[i].sampling_rate       = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.alignment       = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.bit_width       = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.endianness      = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.interleaved     = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.num_channels    = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.q_factor        = PARAM_VAL_NATIVE;
      me_ptr->configured_media_fmt[i].fmt.bits_per_sample = PARAM_VAL_NATIVE;
   }

   if (NULL != init_set_properties)
   {
      capi_result |= capi_pcm_mf_cnv_process_set_properties(me_ptr, init_set_properties);

      if (CAPI_FAILED(capi_result))
      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Initialization Set Property Failed", type);
         return capi_result;
      }
   }

   if (CAPI_MFC == type)
   {
      me_ptr->dm_info.is_dm_disabled = TRUE;
      capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_DISABLED_DM);
   }
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "[%u] Initialization completed !!", me_ptr->type);

   capi_result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 PCM_CNV End function, returns the library to the uninitialized state and frees all the memory that was allocated.
 This function also frees the virtual function table.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "End received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)(_pif);
   capi_pcm_mf_cnv_mfc_deinit(me_ptr);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "End done");
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Free coef sets pointer and clear the related
 ____________________________________________________________________________________________________________________*/
static void capi_free_coef_sets(capi_pcm_mf_cnv_t *me_ptr)
{
   // Freeing older memory
   if (NULL != me_ptr->config.coef_sets_ptr)
   {
      posal_memory_free(me_ptr->config.coef_sets_ptr);
      me_ptr->config.coef_sets_ptr = NULL;
      me_ptr->config.num_coef_sets = 0;
      me_ptr->coef_payload_size    = 0;
   }
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Convert set param data format received to capi data format
 ____________________________________________________________________________________________________________________*/

static capi_err_t capi_pcm_mf_cnv_map_dataformat_to_capi(param_id_pcm_output_format_cfg_t *header_ptr,
                                                         capi_pc_configured_mf_t *         configured_media_fmt)
{

   if (DATA_FORMAT_FIXED_POINT == header_ptr->data_format)
   {
      configured_media_fmt->data_format = CAPI_FIXED_POINT;
   }
   else if (DATA_FORMAT_FLOATING_POINT == header_ptr->data_format)
   {
      configured_media_fmt->data_format = CAPI_FLOATING_POINT;
   }
   else
   {
      return CAPI_EBADPARAM;
   }
   return CAPI_EOK;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Sets either a parameter value or a parameter structure containing multiple parameters. In the event of a failure, the
 appropriate error code is returned.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_set_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr)
{

   capi_err_t capi_result = CAPI_EOK;
   uint32_t   port        = 0;
   if (NULL == _pif || NULL == params_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
   }
   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)(_pif);

   switch (param_id)
   {
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         capi_result = capi_pcm_mf_cnv_handle_data_port_op(me_ptr, params_ptr);
         break;
      }
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if (!((CAPI_PCM_CONVERTER == me_ptr->type) || (CAPI_PCM_DECODER == me_ptr->type) ||
               (CAPI_PCM_ENCODER == me_ptr->type)))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         if ((TRUE == port_info_ptr->is_valid) && (TRUE != port_info_ptr->is_input_port))
         {
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            break;
         }

         if (params_ptr->actual_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t))
         {
            fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
               (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

            capi_pc_configured_mf_t *configured_media_fmt = &me_ptr->configured_media_fmt[port];

            if (CAPI_PCM_ERROR == pcm_mf_cnv_is_supported_extn_media_type(extn_ptr))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "[%u] Set param received bad param, 0x%p, alignment = %d, bit_width = %d, "
                       "endianness = %d",
                       me_ptr->type,
                       param_id,
                       extn_ptr->alignment,
                       extn_ptr->bit_width,
                       extn_ptr->endianness);
               return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            }

            if (CAPI_DATA_FORMAT_INVALID_VAL != extn_ptr->bit_width)
            {
               me_ptr->extn_in_media_fmt[port].bit_width = extn_ptr->bit_width;
            }

            me_ptr->extn_in_media_fmt[port].alignment = extn_ptr->alignment;

            if (CAPI_DATA_FORMAT_INVALID_VAL != extn_ptr->endianness)
            {
               me_ptr->extn_in_media_fmt[port].endianness = extn_ptr->endianness;
            }

            if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width)
            {
               me_ptr->extn_out_media_fmt[port].bit_width = me_ptr->extn_in_media_fmt[port].bit_width;
            }

            if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.alignment)
            {
               me_ptr->extn_out_media_fmt[port].alignment = me_ptr->extn_in_media_fmt[port].alignment;
            }

            if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.endianness)
            {
               me_ptr->extn_out_media_fmt[port].endianness = me_ptr->extn_in_media_fmt[port].endianness;
            }
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] set param failed because of length issues, 0x%p, 0x%p, in_len = %d, "
                    "needed_len "
                    "= %d",
                    me_ptr->type,
                    _pif,
                    param_id,
                    params_ptr->actual_data_len,
                    sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t));
         }
         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    param_id,
                    params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_container_frame_duration_t *fm_dur =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;

         // This variable is updated for only for MFC to determine if it is a fractional resampling case
         me_ptr->frame_size_us = fm_dur->duration_us;

         pc_set_cntr_frame_size(&me_ptr->pc[0], me_ptr->frame_size_us);
         capi_pcm_mf_cnv_update_dm_disable(me_ptr);
         break;
      }
      case FWK_EXTN_PARAM_ID_THRESHOLD_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_threshold_cfg_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Param id 0x%lx Bad param size %lu",
                    me_ptr->type,
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            return CAPI_EUNSUPPORTED;
         }
         fwk_extn_param_id_threshold_cfg_t *fm_dur = (fwk_extn_param_id_threshold_cfg_t *)params_ptr->data_ptr;

         // This variable is updated only for PCM DEC and PCM ENC to raise threshold based on SG perf mode
         me_ptr->frame_size_us = fm_dur->duration_us;
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] Received frame_size_us = %ld", me_ptr->type, fm_dur->duration_us);
         break;
      }
      case PARAM_ID_PCM_OUTPUT_FORMAT_CFG:
      {
         if (!((CAPI_PCM_CONVERTER == me_ptr->type) || (CAPI_PCM_DECODER == me_ptr->type) ||
               (CAPI_PCM_ENCODER == me_ptr->type)))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_PCM_OUTPUT_FORMAT_CFG not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         if (params_ptr->actual_data_len >= sizeof(param_id_pcm_output_format_cfg_t))
         {
            param_id_pcm_output_format_cfg_t *header_ptr = (param_id_pcm_output_format_cfg_t *)(params_ptr->data_ptr);
            if (MEDIA_FMT_ID_PCM != header_ptr->fmt_id)
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "[%u] Set: OUTPUT FORMAT CFG: Unsupported fmt_id %d provided",
                       me_ptr->type,
                       header_ptr->fmt_id);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               return capi_result;
            }

            payload_pcm_output_format_cfg_t *data_ptr =
               (payload_pcm_output_format_cfg_t *)(params_ptr->data_ptr + sizeof(param_id_pcm_output_format_cfg_t));
            if (!pcm_mf_cnv_is_supported_out_media_type(data_ptr, header_ptr->data_format))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "[%u] Set: OUTPUT FORMAT CFG: Unsupported parameters provided",
                       me_ptr->type);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               return capi_result;
            }

            capi_standard_data_format_v2_t *        out_std_ptr          = &me_ptr->out_media_fmt[port].format;
            capi_standard_data_format_v2_t *        inp_std_ptr          = &me_ptr->in_media_fmt[port].format;
            fwk_extn_pcm_param_id_media_fmt_extn_t *out_extn_ptr         = &me_ptr->extn_out_media_fmt[port];
            fwk_extn_pcm_param_id_media_fmt_extn_t *inp_extn_ptr         = &me_ptr->extn_in_media_fmt[port];
            capi_pc_configured_mf_t *               configured_media_fmt = &me_ptr->configured_media_fmt[port];
            payload_pcm_output_format_cfg_t *       fmt                  = &configured_media_fmt->fmt;

            pick_if_not_unset(&fmt->bit_width, data_ptr->bit_width);
            pick_if_not_unset(&fmt->alignment, data_ptr->alignment);
            pick_if_not_unset(&fmt->bits_per_sample, data_ptr->bits_per_sample);
            pick_if_not_unset(&fmt->q_factor, data_ptr->q_factor);
            pick_if_not_unset(&fmt->endianness, data_ptr->endianness);
            pick_if_not_unset(&fmt->interleaved, data_ptr->interleaved);
            pick_if_not_unset(&fmt->num_channels, data_ptr->num_channels);

            if ((PARAM_VAL_UNSET != fmt->num_channels) && (PARAM_VAL_NATIVE != fmt->num_channels))
            {
               uint8_t *channel_mapping = (uint8_t *)(data_ptr + 1);
               for (uint32_t ch = 0; ch < data_ptr->num_channels; ch++)
               {
                  configured_media_fmt->channel_type[ch] = (uint16_t)channel_mapping[ch];
               }
            }

            // important: module overwrites to V2 if configuration is set to V1.

            pcm_to_capi_interleaved_with_native(&out_std_ptr->data_interleaving,
                                                fmt->interleaved,
                                                inp_std_ptr->data_interleaving);

            pick_config_or_input(&out_std_ptr->bits_per_sample, fmt->bits_per_sample, inp_std_ptr->bits_per_sample);
            pick_config_or_input(&out_std_ptr->q_factor, fmt->q_factor, inp_std_ptr->q_factor);
            pick_config_or_input(&out_std_ptr->num_channels, fmt->num_channels, inp_std_ptr->num_channels);
            pick_config_or_input(&out_extn_ptr->alignment, fmt->alignment, inp_extn_ptr->alignment);
            pick_config_or_input(&out_extn_ptr->bit_width, fmt->bit_width, inp_extn_ptr->bit_width);
            pick_config_or_input(&out_extn_ptr->endianness, fmt->endianness, inp_extn_ptr->endianness);

            if ((0 != out_std_ptr->num_channels) && (CAPI_DATA_FORMAT_INVALID_VAL != out_std_ptr->num_channels))
            {
               uint16_t *src_channel_type = (PARAM_VAL_NATIVE == fmt->num_channels)
                                               ? &me_ptr->in_media_fmt[port].channel_type[0]
                                               : &configured_media_fmt->channel_type[0];

               memscpy(&out_std_ptr->channel_type[0],
                       sizeof(capi_channel_type_t) * out_std_ptr->num_channels,
                       src_channel_type,
                       sizeof(capi_channel_type_t) * out_std_ptr->num_channels);
            }

            out_std_ptr->data_is_signed = TRUE; // this param assumes data is signed.

            // Error for unsupported data format already caught above
            capi_pcm_mf_cnv_map_dataformat_to_capi(header_ptr, configured_media_fmt);

            if (CAPI_FLOATING_POINT == configured_media_fmt->data_format)
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
               // If the out data format is floating point then q factor value would be invalid.
               out_std_ptr->q_factor = CAPI_DATA_FORMAT_INVALID_VAL;
            }

            // resampler_reinit is set to FALSE
            capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, FALSE);
            capi_result |= capi_pcm_mf_cnv_check_and_raise_output_media_format_event(me_ptr);
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Param id 0x%lx Bad param size %lu",
                    me_ptr->type,
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         }
         break;
      }
      case PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT:
      {
         if (!(CAPI_MFC == me_ptr->type))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT received", me_ptr->type);

         if (params_ptr->actual_data_len >= sizeof(param_id_mfc_output_media_fmt_t))
         {
            param_id_mfc_output_media_fmt_t *data_ptr =
               reinterpret_cast<param_id_mfc_output_media_fmt_t *>(params_ptr->data_ptr);

            int16_t num_ch = data_ptr->num_channels < 0 ? 0 : data_ptr->num_channels;
            uint32_t required_size =
               sizeof(param_id_mfc_output_media_fmt_t) + (num_ch * sizeof(uint16));
            if (params_ptr->max_data_len < required_size)
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "MFC: MFC_PARAM_ID_OUTPUT_MEDIA_FORMAT invalid param size %lu, required_size %d",
                       params_ptr->max_data_len,
                       required_size);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               break;
            }

            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT SR: %lu CH: %lu BW: %lu ",
                    me_ptr->type,
                    data_ptr->sampling_rate,
                    data_ptr->num_channels,
                    data_ptr->bit_width);

            capi_pc_configured_mf_t *       configured_media_fmt = &me_ptr->configured_media_fmt[port];
            capi_standard_data_format_v2_t *out_std_ptr          = &me_ptr->out_media_fmt[port].format;
            capi_standard_data_format_v2_t *inp_std_ptr          = &me_ptr->in_media_fmt[port].format;

            // Sampling rate
            if (((384000 >= data_ptr->sampling_rate) && (0 < data_ptr->sampling_rate)) ||
                (PARAM_VAL_NATIVE == data_ptr->sampling_rate))
            {
               // Valid or native input
               configured_media_fmt->sampling_rate = data_ptr->sampling_rate;
            }
            else if (PARAM_VAL_UNSET != data_ptr->sampling_rate)
            {
               // Invalid input, abort
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "MFC: Sampling rate must be > 0 and <= 384kHz. Received %lu",
                       data_ptr->sampling_rate);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            // Channels
            if (((CAPI_MAX_CHANNELS_V2 >= data_ptr->num_channels) && (0 < data_ptr->num_channels)) ||
                (PARAM_VAL_NATIVE == data_ptr->num_channels))
            {
               // valid or native
               configured_media_fmt->fmt.num_channels = data_ptr->num_channels;
            }
            else if (PARAM_VAL_UNSET != data_ptr->num_channels)
            {
               // Invalid input, abort
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "MFC: Number of channels must be between > 0 and <= %d. Received %lu",
                       CAPI_MAX_CHANNELS_V2,
                       data_ptr->num_channels);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            bool_t ch_type_unsup = FALSE;
            // This check should be done only if channels are valid i.e. not 0 or native(-1) or unset(-2)
            if ((PARAM_VAL_NATIVE != data_ptr->num_channels) && (PARAM_VAL_UNSET != data_ptr->num_channels))
            {
               for (uint32_t i = 0; i < data_ptr->num_channels; i++)
               {
                  if ((data_ptr->channel_type[i] < (uint16_t)PCM_CHANNEL_L) ||
                      (data_ptr->channel_type[i] > (uint16_t)PCM_MAX_CHANNEL_MAP_V2))
                  {
                     CNV_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "MFC: Unsupported channel type channel idx %d, channel type %d received",
                             (int)i,
                             (int)data_ptr->channel_type[i]);
                     ch_type_unsup = TRUE;
                     break;
                  }
               }
               if (ch_type_unsup)
               {
                  CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
                  break;
               }
            }

            if ((CAPI_MAX_CHANNELS_V2 >= data_ptr->num_channels) && (0 < data_ptr->num_channels))
            {
               for (uint32_t ch = 0; ch < data_ptr->num_channels; ch++)
               {
                  configured_media_fmt->channel_type[ch] = (uint16_t)data_ptr->channel_type[ch];
               }
            }

            // Bitwidth
            if (((16 == data_ptr->bit_width) || (24 == data_ptr->bit_width) || (32 == data_ptr->bit_width) ||
                 (PARAM_VAL_NATIVE == data_ptr->bit_width)))
            {
               configured_media_fmt->fmt.bit_width = data_ptr->bit_width;
            }
            else if (PARAM_VAL_UNSET != data_ptr->bit_width)
            {
               // Invalid input, abort
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "MFC: Bit width must either be 16 or 24 or 32 bits per sample. Received %lu",
                       data_ptr->bit_width);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            pick_config_or_input(&out_std_ptr->sampling_rate,
                                 configured_media_fmt->sampling_rate,
                                 inp_std_ptr->sampling_rate);

            pick_config_or_input(&out_std_ptr->num_channels,
                                 configured_media_fmt->fmt.num_channels,
                                 inp_std_ptr->num_channels);

            if ((0 != out_std_ptr->num_channels) && (CAPI_DATA_FORMAT_INVALID_VAL != out_std_ptr->num_channels))
            {
               uint16_t *src_channel_type = (PARAM_VAL_NATIVE == configured_media_fmt->fmt.num_channels)
                                               ? &me_ptr->in_media_fmt[port].channel_type[0]
                                               : &configured_media_fmt->channel_type[0];

               memscpy(&out_std_ptr->channel_type[0],
                       sizeof(capi_channel_type_t) * out_std_ptr->num_channels,
                       src_channel_type,
                       sizeof(capi_channel_type_t) * out_std_ptr->num_channels);
            }

            capi_pcm_mf_cnv_mfc_set_output_bps_qf(configured_media_fmt);

            pick_config_or_input(&out_std_ptr->bits_per_sample,
                                 configured_media_fmt->fmt.bits_per_sample,
                                 inp_std_ptr->bits_per_sample);

            // set corresponding q factor and bits per sample
            // In this function out_std_ptr->bits_per_sample changes
            pick_config_or_input(&out_std_ptr->q_factor, configured_media_fmt->fmt.q_factor, inp_std_ptr->q_factor);

            // for internal checks if bw is set, use it else copy bw derived from input mf
            if (PARAM_VAL_NATIVE == configured_media_fmt->fmt.bit_width) // bit-width
            {
               me_ptr->extn_out_media_fmt[port].bit_width = me_ptr->extn_in_media_fmt[port].bit_width;
            }
            else
            {
               me_ptr->extn_out_media_fmt[port].bit_width = configured_media_fmt->fmt.bit_width;
            }

            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Output bw set =%lu, bw configured %lu, inp extn bw %lu",
                    me_ptr->extn_out_media_fmt[port].bit_width,
                    configured_media_fmt->fmt.bits_per_sample,
                    me_ptr->extn_in_media_fmt[port].bit_width);

            me_ptr->extn_out_media_fmt[port].alignment  = PC_LSB_ALIGNED;
            me_ptr->extn_out_media_fmt[port].endianness = PC_LITTLE_ENDIAN;

            out_std_ptr->data_is_signed = TRUE; // this param assumes data is signed.

            // resampler_reinit is set to FALSE
            capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, FALSE);

            if (me_ptr->input_media_fmt_received)
            { // raise only if we have the full MF
               capi_result |= capi_pcm_mf_cnv_check_and_raise_output_media_format_event(me_ptr);
            }
            else if ((PARAM_VAL_NATIVE != configured_media_fmt->sampling_rate) &&
                     (PARAM_VAL_NATIVE != configured_media_fmt->fmt.num_channels) &&
                     (PARAM_VAL_NATIVE != configured_media_fmt->fmt.bits_per_sample))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "[%u] Raising out mf because received all valid fields, "
                       "eventhough input mf is not yet received",
                       me_ptr->type);

               // Implies all valid fields and that out qfactor and channel types are also correctly set
               // Setting other fields assuming defaults
               out_std_ptr->bitstream_format = MEDIA_FMT_ID_PCM;

               // MFC only supports unpacked, hence
               out_std_ptr->data_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;

               out_std_ptr->data_is_signed    = TRUE;
               out_std_ptr->minor_version     = CAPI_MEDIA_FORMAT_MINOR_VERSION;
               capi_result |= capi_pcm_mf_cnv_check_and_raise_output_media_format_event(me_ptr);
            }

            CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] Set: MFC_PARAM_ID_OUTPUT_MEDIA_FORMAT: Done", me_ptr->type);
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_CNV: MFC_PARAM_ID_OUTPUT_MEDIA_FORMAT invalid param size %lu",
                    params_ptr->max_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }
      case PARAM_ID_CHMIXER_COEFF:
      {
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] PARAM_ID_CHMIXER_COEFF called", me_ptr->type);

         uint32_t param_size = params_ptr->actual_data_len;
         if (param_size >= sizeof(param_id_chmixer_coeff_t))
         {
            size_t   req_param_size = 0, req_param_size_al = 0;
            uint32_t index = 0, total_payload_size = 0, total_payload_aligned_size = 0;

            const param_id_chmixer_coeff_t *const p_param = (param_id_chmixer_coeff_t *)params_ptr->data_ptr;

            param_size = param_size - sizeof(param_id_chmixer_coeff_t);

            // error check for coefficients index
            if (0 == p_param->num_coeff_tbls)
            {
               CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Set param received zero coefficients", me_ptr->type);

               // Freeing older memory
               capi_free_coef_sets(me_ptr);

               // resampler_reinit is set to FALSE
               capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, FALSE);
               break;
            }

            // pointer to the first array
            uint8_t *first_data_ptr   = (uint8_t *)(sizeof(param_id_chmixer_coeff_t) + params_ptr->data_ptr);
            uint8_t *new_coef_arr_ptr = first_data_ptr;

            if (param_size < sizeof(chmixer_coeff_t))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "[%u] Set coeff, Bad param size %lu, required param size %lu",
                       me_ptr->type,
                       param_size,
                       sizeof(chmixer_coeff_t));
               return CAPI_ENEEDMORE;
            }
            chmixer_coeff_t *coeff_param_ptr = (chmixer_coeff_t *)first_data_ptr;

            // Size and error check loop
            for (index = 0; index < p_param->num_coeff_tbls; index++)
            {
               // Size of each tbl
               req_param_size =
                  sizeof(chmixer_coeff_t) + (sizeof(uint16_t) * coeff_param_ptr->num_output_channels) +
                  (sizeof(uint16_t) * coeff_param_ptr->num_input_channels) +
                  (sizeof(int16_t) * coeff_param_ptr->num_output_channels * coeff_param_ptr->num_input_channels);

               // if req param size is not 4byte align add padding len and store in req_param_size_aligned
               req_param_size_al = ALIGN_4_BYTES(req_param_size);

               if (param_size < req_param_size_al)
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "[%u] Set coeff, Bad param size %lu, required param size %lu",
                          me_ptr->type,
                          param_size,
                          req_param_size_al);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
                  return capi_result;
               }

               // error check for num channels
               if ((coeff_param_ptr->num_input_channels > CAPI_MAX_CHANNELS_V2) ||
                   (0 == coeff_param_ptr->num_input_channels) ||
                   (coeff_param_ptr->num_output_channels > CAPI_MAX_CHANNELS_V2) ||
                   (0 == coeff_param_ptr->num_output_channels))
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "[%u] Set coeff, invalid number of channels. inp ch %hu, out ch %hu",
                          me_ptr->type,
                          coeff_param_ptr->num_input_channels,
                          coeff_param_ptr->num_output_channels);
                  return CAPI_EBADPARAM;
               }

               const uint16_t *out_ch_map = (uint16_t *)((uint8_t *)coeff_param_ptr + sizeof(chmixer_coeff_t));
               const uint16_t *in_ch_map  = out_ch_map + coeff_param_ptr->num_output_channels;

               // error check for channel type
               capi_result |= capi_pcm_mf_cnv_check_ch_type(me_ptr, in_ch_map, coeff_param_ptr->num_input_channels);
               capi_result |= capi_pcm_mf_cnv_check_ch_type(me_ptr, out_ch_map, coeff_param_ptr->num_output_channels);
               if (CAPI_FAILED(capi_result))
               {
                  CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Set coeff, invalid channel type", me_ptr->type);
                  return capi_result;
               }

               // Update size
               param_size = param_size - req_param_size_al;

               // Update param size and ptrs to point to the next coeff array provided its not last coefficient payload
               if (index != (p_param->num_coeff_tbls - 1))
               {
                  if (param_size < sizeof(chmixer_coeff_t))
                  {
                     CNV_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "[%u] Set coeff, Bad param size %lu, required param size %lu",
                             me_ptr->type,
                             param_size,
                             sizeof(chmixer_coeff_t));
                     return CAPI_ENEEDMORE;
                  }

                  // update to move to the next array
                  new_coef_arr_ptr = (uint8_t *)(new_coef_arr_ptr + req_param_size_al);
                  coeff_param_ptr  = (chmixer_coeff_t *)(new_coef_arr_ptr);
               }

               // Update total payload size
               total_payload_size += req_param_size;
               total_payload_aligned_size += req_param_size_al;
            }

            if (me_ptr->coef_payload_size != total_payload_size)
            {
               // Freeing older memory
               capi_free_coef_sets(me_ptr);

               uint32_t size_to_malloc =
                  total_payload_size +
                  (p_param->num_coeff_tbls * (sizeof(capi_pcm_mf_cnv_chmixer_coef_set_t) - sizeof(chmixer_coeff_t)));

               me_ptr->config.coef_sets_ptr =
                  (capi_pcm_mf_cnv_chmixer_coef_set_t *)posal_memory_malloc(size_to_malloc,
                                                                            (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
               if (NULL == me_ptr->config.coef_sets_ptr)
               {
                  CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Set param failed to allocate memory", me_ptr->type);
                  return CAPI_ENOMEMORY;
               }
            }

            // For first iteration
            // Start of chmixer channel arrays
            uint8_t *new_coeff_arr = (uint8_t *)me_ptr->config.coef_sets_ptr;
            // start of channel maps
            uint8_t *new_ch_map_arr =
               new_coeff_arr + (p_param->num_coeff_tbls * sizeof(capi_pcm_mf_cnv_chmixer_coef_set_t));

            // reset coeff param ptr to point to start of first array input cfg
            coeff_param_ptr = (chmixer_coeff_t *)first_data_ptr;

            // Processing loop
            for (index = 0; index < p_param->num_coeff_tbls; index++)
            {
               uint32_t padding = 0;
               // Size of each tbl
               uint32_t tbl_size =
                  (sizeof(uint16_t) * coeff_param_ptr->num_output_channels) +
                  (sizeof(uint16_t) * coeff_param_ptr->num_input_channels) +
                  (sizeof(int16_t) * coeff_param_ptr->num_output_channels * coeff_param_ptr->num_input_channels);

               if (0 != (tbl_size % 4))
               {
                  padding = 1; // it can only be 2bytes less since every entry is 2bytes
               }

               // input ptrs
               const uint16_t *out_ch_map = (uint16_t *)((uint8_t *)coeff_param_ptr + sizeof(chmixer_coeff_t));
               const uint16_t *in_ch_map  = out_ch_map + coeff_param_ptr->num_output_channels;
               const int16_t * coef_ptr   = (int16_t *)(in_ch_map + coeff_param_ptr->num_input_channels);

               // Assign pointers after malloc
               capi_pcm_mf_cnv_chmixer_coef_set_t *coef_set = (capi_pcm_mf_cnv_chmixer_coef_set_t *)new_coeff_arr;
               coef_set->num_in_ch                          = coeff_param_ptr->num_input_channels;
               coef_set->num_out_ch                         = coeff_param_ptr->num_output_channels;

               coef_set->out_ch_map = (uint16_t *)(new_ch_map_arr);
               coef_set->in_ch_map  = (uint16_t *)(coef_set->out_ch_map + coef_set->num_out_ch);
               coef_set->coef_ptr   = (int16_t *)(coef_set->in_ch_map + coef_set->num_in_ch);

#ifdef PCM_CNV_DEBUG

               uint8_t i = 0;
               uint8_t j = 0;

               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "[%u] SET PARAM FOR COEFF: Inp Number of Channels %lu",
                       me_ptr->type,
                       coeff_param_ptr->num_input_channels);

               for (i = 0; i < coef_set->num_in_ch; i++)
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "[%u] Inp Channel_Type[%hhu]: %d",
                          me_ptr->type,
                          i,
                          in_ch_map[i]);
               }

               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "[%u] Out Number of Channels %lu",
                       me_ptr->type,
                       coeff_param_ptr->num_output_channels);

               for (j = 0; j < coef_set->num_out_ch; j++)
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "[%u] Out Channel_Type[%hhu]: %d",
                          me_ptr->type,
                          j,
                          out_ch_map[j]);
               }

               uint8_t a = i * j;
               for (j = 0; j < a; j++)
               {
                  CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] coefficients: %x", me_ptr->type, *(coef_ptr + j));
               }
#endif
               // Copy values
               uint32_t size_to_cpy =
                  ((coef_set->num_in_ch * sizeof(uint16_t)) + (coef_set->num_out_ch * sizeof(uint16_t)) +
                   (coef_set->num_in_ch * coef_set->num_out_ch * sizeof(int16_t)));

               memscpy(coef_set->out_ch_map, size_to_cpy, out_ch_map, size_to_cpy);

               // Update in and out params to point to the next coeff array provided its not last coefficient payload
               if (index != (p_param->num_coeff_tbls - 1))
               {
                  coeff_param_ptr = (chmixer_coeff_t *)(coef_ptr + (coeff_param_ptr->num_output_channels *
                                                                    coeff_param_ptr->num_input_channels) +
                                                        padding);
                  // point to next array
                  new_coeff_arr = new_coeff_arr + sizeof(capi_pcm_mf_cnv_chmixer_coef_set_t);
                  // point to next map
                  new_ch_map_arr = (uint8_t *)(coef_set->coef_ptr + (coef_set->num_out_ch * coef_set->num_in_ch));
               }
            } // end of for loop

            // Update number of coeff sets
            me_ptr->config.num_coef_sets = p_param->num_coeff_tbls;

            // Update data length
            params_ptr->actual_data_len = total_payload_aligned_size;
            me_ptr->coef_payload_size   = total_payload_size;

            // resampler_reinit is set to FALSE
            capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, FALSE);
         }
         else
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Set coeff, Bad param size %lu", me_ptr->type, param_size);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }
         break;
      }
      case PARAM_ID_MFC_RESAMPLER_CFG:
      {
         if (CAPI_MFC != me_ptr->type)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_MFC_RESAMPLER_CFG not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         if (params_ptr->actual_data_len < sizeof(param_id_mfc_resampler_cfg_t))
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "CAPI PCM_CNV: Bad param size %lu", params_ptr->actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         param_id_mfc_resampler_cfg_t *rs_config = (param_id_mfc_resampler_cfg_t *)(params_ptr->data_ptr);
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "[%u] Received PARAM_ID_MFC_RESAMPLER_CFG type %d (0:FIR_RESAMPLER, 1:IIR_RESAMPLER, 2: "
                 "IIR_PREFERRED), use hw rs: %d dynamic mode %d, delay type %d",
                 me_ptr->type,
                 rs_config->resampler_type,
                 rs_config->use_hw_rs,
                 rs_config->dynamic_mode,
                 rs_config->delay_type);

         // Only allow iir resampler to be configured after start if not in dm mode
         // dm mode indirectly checks valid sampling rates AND capi_pcm_mf_cnv_is_fractional_media_type
         if ((IIR_RESAMPLER == rs_config->resampler_type) && (capi_pcm_mf_cnv_is_dm_enabled(me_ptr)))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error! Cannot use iir resampler when dm is enabled, module disabling itself");
            // disable module
            me_ptr->lib_enable = FALSE;
            capi_pcm_mf_cnv_raise_process_check_event(me_ptr);
            pc_deinit((pc_lib_t *)&me_ptr->pc[port]);
            return CAPI_EUNSUPPORTED;
         }

         // Step 1: If iir preferred is selected, set flag and decide the rs type for now
         if (IIR_PREFERRED == rs_config->resampler_type)
         {
            me_ptr->pc[0].iir_pref_set = TRUE;

            // If media formats are already configured and we know dm mode is true, fallback to FIR
            // Otherwise we set to IIR
            if (capi_pcm_mf_cnv_is_dm_enabled(me_ptr))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Cannot use iir resampler when dm is enabled. "
                       "Since rs type is IIR_PREFERRED, falling back to sw fir resampler");

               // Change rs type
               rs_config->resampler_type = FIR_RESAMPLER;
            }
            else
            {
               // Set to IIR, if mfs dont match later in pc lib we will take care
               rs_config->resampler_type = IIR_RESAMPLER;
            }
         }
         else
         {
            me_ptr->pc[0].iir_pref_set = FALSE;
         }

         bool_t fir_iir_rs_switch = FALSE;
         bool_t enter_lib_init    = FALSE;

         // Step 2: if resampler type changed between IIR and FIR, have to reinit so we call lib init
         // By this time resampler type has already been decided as either IIR or FIR
         if (rs_config->resampler_type != me_ptr->pc[0].resampler_type)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] PARAM_ID_MFC_RESAMPLER_CFG: Switching to resampler type %d (0:FIR_RESAMPLER, 1:IIR_RESAMPLER "
                    ")",
                    me_ptr->type,
                    rs_config->resampler_type);
            fir_iir_rs_switch = TRUE;
            enter_lib_init    = TRUE;
         }

         /*Step 3: Check to prevent resampler from re-init if same cfg is received.
          * If FIR resampler: call lib init only if
          a)use hw rs flag changed b) dyn mode is different c) in dyn mode delay type is diff */
         else if ((FIR_RESAMPLER == rs_config->resampler_type) &&
                  ((me_ptr->pc[0].use_hw_rs != rs_config->use_hw_rs) ||
                   (me_ptr->pc[0].dynamic_mode != rs_config->dynamic_mode) ||
                   ((1 == rs_config->dynamic_mode) && (me_ptr->pc[0].delay_type != rs_config->delay_type))))
         {

            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] PARAM_ID_MFC_RESAMPLER_CFG: Config changed, entering lib init",
                    me_ptr->type);
            enter_lib_init = TRUE;
         }

         // saving new config it in capi after comparing
         me_ptr->pc[0].resampler_type = (pc_resampler_type_t)rs_config->resampler_type;
         me_ptr->pc[0].use_hw_rs      = rs_config->use_hw_rs;
         me_ptr->pc[0].dynamic_mode   = rs_config->dynamic_mode;
         me_ptr->pc[0].delay_type     = rs_config->delay_type;

         // If enter lib init is set to true, sending in fir_iir_rs_switch flag for reordering logic inside
         if (enter_lib_init)
         {
            capi_result |= capi_pcm_mf_cnv_lib_init(me_ptr, fir_iir_rs_switch);
         }

         break;
      }
      case FWK_EXTN_DM_PARAM_ID_CHANGE_MODE:
      {
         // if iir was set first it will fail now, in start
         // If iir pref was set, by now it should have already got set to iir or fir
         if ((CAPI_MFC != me_ptr->type) || (FIR_RESAMPLER != me_ptr->pc[0].resampler_type))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_DM_PARAM_ID_CHANGE_MODE not supported by this type, rs type %d",
                    me_ptr->type,
                    me_ptr->pc[0].resampler_type);
            return CAPI_EUNSUPPORTED;
         }
         fwk_extn_dm_param_id_change_mode_t *mode_ptr = (fwk_extn_dm_param_id_change_mode_t *)params_ptr->data_ptr;

         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "[%u] Set param received FWK_EXTN_DM_PARAM_ID_CHANGE_MODE with mode = %d",
                 me_ptr->type,
                 mode_ptr->dm_mode);

         if ((FWK_EXTN_DM_FIXED_INPUT_MODE == mode_ptr->dm_mode) ||
             (FWK_EXTN_DM_FIXED_OUTPUT_MODE == mode_ptr->dm_mode))
         {
            if (!capi_pcm_mf_cnv_is_dm_enabled(me_ptr) && (me_ptr->pc[0].hwsw_rs_lib_ptr))
            {
               // enable the dm mode for FIR resampler
               me_ptr->dm_info.is_dm_disabled = FALSE;
               capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_ENABLED_DM);
            }

            if (me_ptr->pc[0].hwsw_rs_lib_ptr)
            {
               // DM config should be reset at this point
               // when module receives FWK_EXTN_DM_PARAM_ID_SET_SAMPLES setparam then only it should move to fixed-out
               // processing.
               me_ptr->dm_info.req_out_samples     = 0;
               me_ptr->dm_info.expected_in_samples = 0;
               hwsw_rs_lib_set_dm_config(me_ptr->pc[0].hwsw_rs_lib_ptr,
                                         FWK_EXTN_DM_FIXED_INPUT_MODE, // yes, fixed-input
                                         me_ptr->dm_info.req_out_samples);
            }
            me_ptr->dm_info.dm_mode = mode_ptr->dm_mode;
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_CNV: Set param id 0x%lx, invalid payload %lu",
                    param_id,
                    mode_ptr->dm_mode);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         }
         break;
      }
      case FWK_EXTN_DM_PARAM_ID_SET_SAMPLES:
      case FWK_EXTN_DM_PARAM_ID_SET_MAX_SAMPLES:
      {
         if ((CAPI_MFC != me_ptr->type) || (FIR_RESAMPLER != me_ptr->pc[0].resampler_type) ||
             !capi_pcm_mf_cnv_is_dm_enabled(me_ptr))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_DM_PARAM_ID_SET_SAMPLES/SET_MAX_SAMPLES not supported by this "
                    "type,rs type %d is_dm_enabled %d",
                    me_ptr->type,
                    me_ptr->pc[0].resampler_type,
                    capi_pcm_mf_cnv_is_dm_enabled(me_ptr));
            return CAPI_EUNSUPPORTED;
         }

         if (!me_ptr->input_media_fmt_received)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] Inp media fmt not received yet, cannot set DM samples",
                    me_ptr->type);
            return CAPI_ENOTREADY;
         }

         fwk_extn_dm_param_id_req_samples_t *req_samples_ptr =
            (fwk_extn_dm_param_id_req_samples_t *)params_ptr->data_ptr;
         // only single port for now
         if (CAPI_PCM_CNV_MAX_IN_PORTS != req_samples_ptr->num_ports)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Set param id 0x%lx, mismatched port count %lu",
                    me_ptr->type,
                    param_id,
                    req_samples_ptr->num_ports);
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            break;
         }

         if ((FWK_EXTN_DM_FIXED_OUTPUT_MODE == me_ptr->dm_info.dm_mode) && (!req_samples_ptr->is_input))
         {
            if (FWK_EXTN_DM_PARAM_ID_SET_SAMPLES == param_id)
            {
               // handle fixed output
               // get samples needed, port index can be ignored for single port
               me_ptr->dm_info.req_out_samples = req_samples_ptr->req_samples[0].samples_per_channel;
               capi_result |= capi_pcm_mf_cnv_get_fixed_out_samples(me_ptr);

               // set to lib
               hwsw_rs_lib_set_dm_config(me_ptr->pc[0].hwsw_rs_lib_ptr,
                                         me_ptr->dm_info.dm_mode,
                                         me_ptr->dm_info.req_out_samples);
            }
            else
            {
               capi_result |=
                  capi_pcm_mf_cnv_get_max_in_samples(me_ptr, req_samples_ptr->req_samples[0].samples_per_channel);
            }
         }
         else if ((FWK_EXTN_DM_FIXED_INPUT_MODE == me_ptr->dm_info.dm_mode) && (req_samples_ptr->is_input))
         {
            me_ptr->dm_info.req_out_samples = 0; // wont be used inside lib
            hwsw_rs_lib_set_dm_config(me_ptr->pc[0].hwsw_rs_lib_ptr,
                                      me_ptr->dm_info.dm_mode,
                                      me_ptr->dm_info.req_out_samples);
         }
         else
         {
            // error
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         }
         break;
      }

      case FWK_EXTN_DM_PARAM_ID_CONSUME_PARTIAL_INPUT:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_dm_param_id_consume_partial_input_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_DEC: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         fwk_extn_dm_param_id_consume_partial_input_t *payload_ptr =
            (fwk_extn_dm_param_id_consume_partial_input_t *)params_ptr->data_ptr;
         me_ptr->dm_info.should_consume_partial_input = payload_ptr->should_consume_partial_input;
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "CAPI PCM_DEC: Receieved FWK_EXTN_DM_PARAM_ID_CONSUME_PARTIAL_INPUT = %lu",
                 payload_ptr->should_consume_partial_input);

         pc_set_consume_partial_input(&me_ptr->pc[0], me_ptr->dm_info.should_consume_partial_input);

         break;
      }
      case PARAM_ID_REMOVE_INITIAL_SILENCE:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_remove_initial_silence_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_DEC: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_remove_initial_silence_t *payload_ptr = (param_id_remove_initial_silence_t *)params_ptr->data_ptr;
         me_ptr->initial_silence_in_samples             = payload_ptr->samples_per_ch_to_remove + PCM_DECODER_DELAY;
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "CAPI PCM_DEC: Receieved PARAM_ID_REMOVE_INITIAL_SILENCE %lu + %lu samples per ch ",
                 payload_ptr->samples_per_ch_to_remove,
                 PCM_DECODER_DELAY);
         break;
      }
      case PARAM_ID_REMOVE_TRAILING_SILENCE:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_remove_trailing_silence_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_DEC: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_remove_trailing_silence_t *payload_ptr = (param_id_remove_trailing_silence_t *)params_ptr->data_ptr;
         me_ptr->trailing_silence_in_samples             = payload_ptr->samples_per_ch_to_remove > PCM_TRAILING_DELAY
                                                  ? (payload_ptr->samples_per_ch_to_remove - PCM_TRAILING_DELAY)
                                                  : 0;
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "CAPI PCM_DEC: Receieved PARAM_ID_REMOVE_TRAILING_SILENCE %lu - %lu samples per ch",
                 payload_ptr->samples_per_ch_to_remove,
                 PCM_TRAILING_DELAY);
         break;
      }
      case PARAM_ID_PCM_ENCODER_FRAME_SIZE:
      {
         if (CAPI_PCM_ENCODER != me_ptr->type)
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "PARAM_ID_PCM_ENCODER_FRAME_SIZE is only supported for pcm encoder");
            return CAPI_EUNSUPPORTED;
         }
         if (params_ptr->max_data_len >= sizeof(param_id_pcm_encoder_frame_size_t))
         {
            param_id_pcm_encoder_frame_size_t *frame_size_cfg_ptr =
               (param_id_pcm_encoder_frame_size_t *)params_ptr->data_ptr;

            if (((PCM_ENCODER_CONFIG_FRAME_SIZE_IN_SAMPLES == frame_size_cfg_ptr->frame_size_type) &&
                 (frame_size_cfg_ptr->frame_size_in_samples > 0) &&
                 (frame_size_cfg_ptr->frame_size_in_samples <= PCM_ENCODER_CONFIG_MAX_FRAME_SIZE_IN_SAMPLES)) ||
                ((PCM_ENCODER_CONFIG_FRAME_SIZE_IN_MICROSECONDS == frame_size_cfg_ptr->frame_size_type) &&
                 (frame_size_cfg_ptr->frame_size_in_us > 0) &&
                 (frame_size_cfg_ptr->frame_size_in_us <= PCM_ENCODER_CONFIG_MAX_FRAME_SIZE_IN_MICROSECOND)))
            {
               me_ptr->frame_size = *frame_size_cfg_ptr;
            }
            else
            {
               CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Received bad param for PARAM_ID_PCM_ENCODER_FRAME_SIZE");
               return CAPI_EBADPARAM;
            }
            if (me_ptr->input_media_fmt_received)
            {
               capi_result |= capi_pcm_encoder_update_frame_size(me_ptr);
            }
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Get param id 0x%lx, invalid payload size %lu",
                    me_ptr->type,
                    param_id,
                    params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }

      default:
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Set, unsupported param ID 0x%x", me_ptr->type, (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
   }
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Gets either a parameter value or a parameter structure containing multiple parameters. In the event of a failure,
 the
 appropriate error code is returned.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_get_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   port        = 0;
   if (NULL == _pif || NULL == params_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }

   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)_pif;

   switch (param_id)
   {
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if (!((CAPI_PCM_CONVERTER == me_ptr->type) || (CAPI_PCM_DECODER == me_ptr->type) ||
               (CAPI_PCM_ENCODER == me_ptr->type)))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         if ((params_ptr->max_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t)))
         {
            if (TRUE == port_info_ptr->is_input_port)
            {
               fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
                  (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

               extn_ptr->bit_width  = me_ptr->extn_in_media_fmt[port].bit_width;
               extn_ptr->alignment  = me_ptr->extn_in_media_fmt[port].alignment;
               extn_ptr->endianness = me_ptr->extn_in_media_fmt[port].endianness;
            }
            else
            {
               fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
                  (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

               extn_ptr->bit_width  = me_ptr->extn_out_media_fmt[port].bit_width;
               extn_ptr->alignment  = me_ptr->extn_out_media_fmt[port].alignment;
               extn_ptr->endianness = me_ptr->extn_out_media_fmt[port].endianness;
            }

            params_ptr->actual_data_len = sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t);
#if 0
            CNV_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "[%u] FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN, get_param done",
                    me_ptr->type);
#endif
         }
         break;
      }
      case PARAM_ID_MFC_RESAMPLER_CFG:
      {
         if (CAPI_MFC != me_ptr->type)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_MFC_RESAMPLER_CFG not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         if (params_ptr->max_data_len >= sizeof(param_id_mfc_resampler_cfg_t))
         {
            param_id_mfc_resampler_cfg_t *rs_config = (param_id_mfc_resampler_cfg_t *)(params_ptr->data_ptr);
            memset(rs_config, 0, sizeof(param_id_mfc_resampler_cfg_t));
            rs_config->resampler_type = me_ptr->pc[0].resampler_type;

            if (0 == rs_config->resampler_type)
            {
               if (me_ptr->pc[0].hwsw_rs_lib_ptr)
               {
                  rs_config->use_hw_rs    = me_ptr->pc[0].hwsw_rs_lib_ptr->this_resampler_instance_using;
                  rs_config->dynamic_mode = me_ptr->pc[0].hwsw_rs_lib_ptr->sw_rs_config_param.dynamic_mode;
                  rs_config->delay_type   = me_ptr->pc[0].hwsw_rs_lib_ptr->sw_rs_config_param.delay_type;
               }
               else
               {
                  rs_config->use_hw_rs    = me_ptr->pc[0].use_hw_rs;
                  rs_config->dynamic_mode = me_ptr->pc[0].dynamic_mode;
                  rs_config->delay_type   = me_ptr->pc[0].delay_type;
               }
            }
            params_ptr->actual_data_len = sizeof(param_id_mfc_resampler_cfg_t);
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         }
         break;
      }
      case PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT:
      {
         if (!(CAPI_MFC == me_ptr->type))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT not supported by this type",
                    me_ptr->type);
            return CAPI_EUNSUPPORTED;
         }

         capi_standard_data_format_v2_t *out_std_ptr = &me_ptr->out_media_fmt[port].format;

         uint32_t size_chmap = (CAPI_DATA_FORMAT_INVALID_VAL != out_std_ptr->num_channels)
                                  ? (out_std_ptr->num_channels * sizeof(uint16_t))
                                  : 0;
         uint32_t required_size = sizeof(param_id_mfc_output_media_fmt_t) + size_chmap;

         if (params_ptr->max_data_len >= required_size)
         {
            param_id_mfc_output_media_fmt_t *mf_ptr =
               reinterpret_cast<param_id_mfc_output_media_fmt_t *>(params_ptr->data_ptr);

            mf_ptr->bit_width     = capi_pcm_mf_cnv_mfc_set_extn_inp_mf_bitwidth(out_std_ptr->q_factor);
            mf_ptr->num_channels  = out_std_ptr->num_channels;
            mf_ptr->sampling_rate = out_std_ptr->sampling_rate;

            memscpy(mf_ptr->channel_type,
                    params_ptr->max_data_len - sizeof(param_id_mfc_output_media_fmt_t),
                    out_std_ptr->channel_type,
                    size_chmap);

            params_ptr->actual_data_len = required_size;
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "CAPI PCM_CNV: MFC_PARAM_ID_OUTPUT_MEDIA_FORMAT invalid param size %lu",
                    params_ptr->max_data_len);
            params_ptr->actual_data_len = required_size;
            capi_result                 = CAPI_ENEEDMORE;
         }
         break;
      }
      case FWK_EXTN_DM_PARAM_ID_CHANGE_MODE:
      {
         if ((CAPI_MFC != me_ptr->type) || (FIR_RESAMPLER != me_ptr->pc[0].resampler_type))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_DM_PARAM_ID_CHANGE_MODE not supported by this type, rs type %d",
                    me_ptr->type,
                    me_ptr->pc[0].resampler_type);
            return CAPI_EUNSUPPORTED;
         }

         if (params_ptr->max_data_len >= sizeof(fwk_extn_dm_param_id_change_mode_t))
         {
            fwk_extn_dm_param_id_change_mode_t *mode_ptr = (fwk_extn_dm_param_id_change_mode_t *)params_ptr->data_ptr;
            mode_ptr->dm_mode                            = me_ptr->dm_info.dm_mode;
            params_ptr->actual_data_len                  = sizeof(fwk_extn_dm_param_id_change_mode_t);
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Get param id 0x%lx, invalid payload size %lu",
                    param_id,
                    params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }
      case FWK_EXTN_DM_PARAM_ID_SET_SAMPLES:
      case FWK_EXTN_DM_PARAM_ID_SET_MAX_SAMPLES:
      {
         if ((CAPI_MFC != me_ptr->type) || (FIR_RESAMPLER != me_ptr->pc[0].resampler_type))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] FWK_EXTN_DM_PARAM_ID_SET_SAMPLES/SET_MAX_SAMPLES not supported by this "
                    "type,rs type %d",
                    me_ptr->type,
                    me_ptr->pc[0].resampler_type);
            return CAPI_EUNSUPPORTED;
         }

         if (params_ptr->max_data_len >= sizeof(fwk_extn_dm_param_id_req_samples_t))
         {
            fwk_extn_dm_param_id_req_samples_t *req_samples_ptr =
               (fwk_extn_dm_param_id_req_samples_t *)params_ptr->data_ptr;

            if ((FWK_EXTN_DM_FIXED_OUTPUT_MODE == me_ptr->dm_info.dm_mode) &&
                (FWK_EXTN_DM_PARAM_ID_SET_SAMPLES == param_id) && capi_pcm_mf_cnv_is_dm_enabled(me_ptr))
            {
               req_samples_ptr->is_input                           = FALSE;
               req_samples_ptr->num_ports                          = 1;
               req_samples_ptr->req_samples[0].samples_per_channel = me_ptr->dm_info.req_out_samples;
               req_samples_ptr->req_samples[0].port_index          = 0;
               params_ptr->actual_data_len                         = sizeof(fwk_extn_dm_param_id_req_samples_t);
            }
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Get param id 0x%lx, invalid payload size %lu",
                    me_ptr->type,
                    param_id,
                    params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }
      case PARAM_ID_PCM_ENCODER_FRAME_SIZE:
      {
         if (CAPI_PCM_ENCODER != me_ptr->type)
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] PARAM_ID_PCM_ENCODER_FRAME_SIZE not supported by this type",
                    me_ptr->type);
            params_ptr->actual_data_len = 0;
            return CAPI_EUNSUPPORTED;
         }
         if (params_ptr->max_data_len >= sizeof(param_id_pcm_encoder_frame_size_t))
         {
            param_id_pcm_encoder_frame_size_t *pcm_encoder_frame_size =
               (param_id_pcm_encoder_frame_size_t *)params_ptr->data_ptr;
            pcm_encoder_frame_size->frame_size_type       = me_ptr->frame_size.frame_size_type;
            pcm_encoder_frame_size->frame_size_in_samples = me_ptr->frame_size.frame_size_in_samples;
            pcm_encoder_frame_size->frame_size_in_us      = me_ptr->frame_size.frame_size_in_us;

            params_ptr->actual_data_len = sizeof(param_id_pcm_encoder_frame_size_t);
            CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "[%u] PARAM_ID_PCM_ENCODER_FRAME_SIZE, get_param done", me_ptr->type);
         }
         else
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "[%u] Get param id 0x%lx, invalid payload size %lu",
                    me_ptr->type,
                    param_id,
                    params_ptr->actual_data_len);
            params_ptr->actual_data_len = 0;
            capi_result                 = CAPI_ENEEDMORE;
         }
         break;
      }
      default:
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "[%u] Get, unsupported param ID 0x%x", me_ptr->type, (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
   }
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Function to set the properties of PCM_CNV module
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{

   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == props_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Set properties received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)_pif;

   capi_result |= capi_pcm_mf_cnv_process_set_properties(me_ptr, props_ptr);

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Function to get the properties of PCM_CNV module
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_pcm_mf_cnv_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == props_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get properties received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_pcm_mf_cnv_t *me_ptr = (capi_pcm_mf_cnv_t *)_pif;

   capi_result |= capi_pcm_mf_cnv_process_get_properties(me_ptr, props_ptr, me_ptr->type);

   return capi_result;
}
