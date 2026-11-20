/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file capi_mfc_osrc.cpp
 *
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
 * =========================================================================*/

#include "capi_mfc_osrc_utils.h"
#include "capi_mfc_osrc_i.h"
#include "capi_mfc_osrc.h"

capi_err_t capi_mfc_osrc_common_get_static_properties(capi_proplist_t *init_set_properties,
                                                        capi_proplist_t *static_properties);

capi_err_t capi_mfc_osrc_common_init(capi_t *_pif, capi_proplist_t *init_set_properties);

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM_CNV function to get the static properties
 ______________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "MFC get static properties called");
   return capi_mfc_osrc_common_get_static_properties(init_set_properties, static_properties);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Initialize the CAPI PCM CNV Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   // CNV_MSG(me_ptr->miid,DBG_HIGH_PRIO, "MFC get static properties called");
   return capi_mfc_osrc_common_init(_pif, init_set_properties);
}

/*______________________________________________________________________________________________________________________
 DESCRIPTION:
 Capi_v2 PCM_CNV function to get the static properties
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_common_get_static_properties(capi_proplist_t *init_set_properties,
                                                        capi_proplist_t *static_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL != static_properties)
   {
      capi_result = capi_mfc_osrc_process_get_properties((capi_mfc_osrc_t *)NULL, static_properties);
      if (CAPI_FAILED(capi_result))
      {
         CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "get static properties failed!");
         return capi_result;
      }
   }
   else
   {
      CNV_MSG(MIID_UNKNOWN,
              DBG_ERROR_PRIO,
              " Get static properties received bad pointer, 0x%p",
              static_properties);
   }

   return capi_result;
}

/*______________________________________________________________________________________________________________________

 DESCRIPTION:
 Initialize the CAPI PCM_CNV Module. This function can allocate memory.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_common_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == _pif || NULL == init_set_properties)
   {
      CNV_MSG(MIID_UNKNOWN,
              DBG_ERROR_PRIO,
              " Init received bad pointer, 0x%p, 0x%p",
              _pif,
              init_set_properties);

      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_mfc_osrc_t));

   me_ptr->vtbl.vtbl_ptr = capi_mfc_osrc_get_vtable(); 

   me_ptr->lib_enable = FALSE;

   me_ptr->perf_mode     = CAPI_PCM_PERF_MODE_INVALID;

   me_ptr->frame_size_us = 0;

   for (uint32_t i = 0; i < CAPI_MFC_OSRC_MAX_IN_PORTS; i++)
   {
      capi_cmn_init_media_fmt_v2(&me_ptr->in_media_fmt[i]);
   }

   for (uint32_t i = 0; i < CAPI_MFC_OSRC_MAX_OUT_PORTS; i++)
   {
      capi_cmn_init_media_fmt_v2(&me_ptr->out_media_fmt[i]);
   }

   for (uint32_t i = 0; i < CAPI_MFC_OSRC_MAX_OUT_PORTS; i++)
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
      capi_result |= capi_mfc_osrc_process_set_properties(me_ptr, init_set_properties);

      if (CAPI_FAILED(capi_result))
      {
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Initialization Set Property Failed");
         return capi_result;
      }
   }

   /* OSResampler quality defaults — match OSRC_RS_DEFAULT_* in osrc_pc_converter.h.
    * memset zeroed the struct, so explicitly set non-zero defaults here. */
   me_ptr->osrc_resampler_cfg.quality_preset      = 0; /* Preset 0: 48 taps, 48 filters */
   me_ptr->osrc_resampler_cfg.use_interpolation   = 1; /* SUBSAMPLE_INTERPOLATE on by default */
   me_ptr->osrc_resampler_cfg.use_blackman_harris = 1; /* BLACKMAN_HARRIS on by default */

   me_ptr->dm_info.is_dm_disabled = TRUE;
   capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_DISABLED_DM);

   capi_result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 PCM_CNV End function, returns the library to the uninitialized state and frees all the memory that was allocated.
 This function also frees the virtual function table.
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "End received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)(_pif);

   #ifdef MFC_OSRC_profiling
   capi_mfc_osrc_print_kpps(me_ptr);
   #endif

   capi_mfc_osrc_mfc_deinit(me_ptr);

   CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "End done");
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Free coef sets pointer and clear the related
 ____________________________________________________________________________________________________________________*/
static void capi_free_coef_sets(capi_mfc_osrc_t *me_ptr)
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

static capi_err_t capi_mfc_osrc_map_dataformat_to_capi(param_id_pcm_output_format_cfg_t *header_ptr,
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
capi_err_t capi_mfc_osrc_set_param_non_island(capi_mfc_osrc_t *     me_ptr,
                                                uint32_t                param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *            params_ptr)
{

   capi_err_t capi_result = CAPI_EOK;
   uint32_t   port        = 0;
   if (NULL == me_ptr || NULL == params_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "set param received bad pointer, 0x%p, 0x%p", me_ptr, params_ptr);
      return CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
   }

   switch (param_id)
   {
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         capi_result = capi_mfc_osrc_handle_data_port_op(me_ptr, params_ptr);
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

         // This variable is updated for only MFC to determine if it is a fractional resampling case
         me_ptr->frame_size_us = fm_dur->duration_us;

         mfc_osrc_set_cntr_frame_size(&me_ptr->pc[0], me_ptr->frame_size_us);
         capi_mfc_osrc_update_dm_disable(me_ptr);
         break;
      }
      case FWK_EXTN_PARAM_ID_THRESHOLD_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_threshold_cfg_t))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            return CAPI_EUNSUPPORTED;
         }
         fwk_extn_param_id_threshold_cfg_t *fm_dur = (fwk_extn_param_id_threshold_cfg_t *)params_ptr->data_ptr;

         me_ptr->frame_size_us = fm_dur->duration_us;
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Received frame_size_us = %ld", fm_dur->duration_us);
         break;
      }
      
      case PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT:
      {
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT received");

         if (params_ptr->actual_data_len >= sizeof(param_id_mfc_output_media_fmt_t))
         {
            param_id_mfc_output_media_fmt_t *data_ptr =
               reinterpret_cast<param_id_mfc_output_media_fmt_t *>(params_ptr->data_ptr);

            uint32_t required_size =
               sizeof(param_id_mfc_output_media_fmt_t) + (data_ptr->num_channels * sizeof(uint16));
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
                    "PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT SR: %lu CH: %lu BW: %lu ",
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

            capi_mfc_osrc_mfc_set_output_bps_qf(configured_media_fmt);

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

            me_ptr->extn_out_media_fmt[port].alignment  = MFC_LSB_ALIGNED;
            me_ptr->extn_out_media_fmt[port].endianness = MFC_LITTLE_ENDIAN;

            out_std_ptr->data_is_signed = TRUE; // this param assumes data is signed.

            // resampler_reinit is set to FALSE
            capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);

            if (me_ptr->input_media_fmt_received)
            { // raise only if we have the full MF
               capi_result |= capi_mfc_osrc_check_and_raise_output_media_format_event(me_ptr);
            }
            else if ((PARAM_VAL_NATIVE != configured_media_fmt->sampling_rate) &&
                     (PARAM_VAL_NATIVE != configured_media_fmt->fmt.num_channels) &&
                     (PARAM_VAL_NATIVE != configured_media_fmt->fmt.bits_per_sample))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "Raising out mf because received all valid fields, "
                       "eventhough input mf is not yet received");

               // Implies all valid fields and that out qfactor and channel types are also correctly set
               // Setting other fields assuming defaults
               out_std_ptr->bitstream_format = MEDIA_FMT_ID_PCM;

               // MFC only supports unpacked, hence
               out_std_ptr->data_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;

               out_std_ptr->data_is_signed    = TRUE;
               out_std_ptr->minor_version     = CAPI_MEDIA_FORMAT_MINOR_VERSION;
               capi_result |= capi_mfc_osrc_check_and_raise_output_media_format_event(me_ptr);
            }

            CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Set: MFC_PARAM_ID_OUTPUT_MEDIA_FORMAT: Done");
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
         CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "PARAM_ID_CHMIXER_COEFF called");

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
               CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param received zero coefficients");

               // Freeing older memory
               capi_free_coef_sets(me_ptr);

               // resampler_reinit is set to FALSE
               capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);
               break;
            }

            // pointer to the first array
            uint8_t *first_data_ptr   = (uint8_t *)(sizeof(param_id_chmixer_coeff_t) + params_ptr->data_ptr);
            uint8_t *new_coef_arr_ptr = first_data_ptr;

            if (param_size < sizeof(chmixer_coeff_t))
            {
               CNV_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Set coeff, Bad param size %lu, required param size %lu",
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
                          "Set coeff, Bad param size %lu, required param size %lu",
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
                          "Set coeff, invalid number of channels. inp ch %hu, out ch %hu",
                          coeff_param_ptr->num_input_channels,
                          coeff_param_ptr->num_output_channels);
                  return CAPI_EBADPARAM;
               }

               const uint16_t *out_ch_map = (uint16_t *)((uint8_t *)coeff_param_ptr + sizeof(chmixer_coeff_t));
               const uint16_t *in_ch_map  = out_ch_map + coeff_param_ptr->num_output_channels;

               // error check for channel type
               capi_result |= capi_mfc_osrc_check_ch_type(me_ptr, in_ch_map, coeff_param_ptr->num_input_channels);
               capi_result |= capi_mfc_osrc_check_ch_type(me_ptr, out_ch_map, coeff_param_ptr->num_output_channels);
               if (CAPI_FAILED(capi_result))
               {
                  CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set coeff, invalid channel type");
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
                             "Set coeff, Bad param size %lu, required param size %lu",
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
                  (p_param->num_coeff_tbls * (sizeof(capi_mfc_osrc_chmixer_coef_set_t) - sizeof(chmixer_coeff_t)));

               me_ptr->config.coef_sets_ptr =
                  (capi_mfc_osrc_chmixer_coef_set_t *)posal_memory_malloc(size_to_malloc,
                                                                            (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
               if (NULL == me_ptr->config.coef_sets_ptr)
               {
                  CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param failed to allocate memory");
                  return CAPI_ENOMEMORY;
               }
            }

            // For first iteration
            // Start of chmixer channel arrays
            uint8_t *new_coeff_arr = (uint8_t *)me_ptr->config.coef_sets_ptr;
            // start of channel maps
            uint8_t *new_ch_map_arr =
               new_coeff_arr + (p_param->num_coeff_tbls * sizeof(capi_mfc_osrc_chmixer_coef_set_t));

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
               capi_mfc_osrc_chmixer_coef_set_t *coef_set = (capi_mfc_osrc_chmixer_coef_set_t *)new_coeff_arr;
               coef_set->num_in_ch                          = coeff_param_ptr->num_input_channels;
               coef_set->num_out_ch                         = coeff_param_ptr->num_output_channels;

               coef_set->out_ch_map = (uint16_t *)(new_ch_map_arr);
               coef_set->in_ch_map  = (uint16_t *)(coef_set->out_ch_map + coef_set->num_out_ch);
               coef_set->coef_ptr   = (int16_t *)(coef_set->in_ch_map + coef_set->num_in_ch);

               #ifdef MFC_OSRC_DEBUG

               uint8_t i = 0;
               uint8_t j = 0;

               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "SET PARAM FOR COEFF: Inp Number of Channels %lu",
                       coeff_param_ptr->num_input_channels);

               for (i = 0; i < coef_set->num_in_ch; i++)
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "Inp Channel_Type[%hhu]: %d",
                          i,
                          in_ch_map[i]);
               }

               CNV_MSG(me_ptr->miid,
                       DBG_HIGH_PRIO,
                       "Out Number of Channels %lu",
                       coeff_param_ptr->num_output_channels);

               for (j = 0; j < coef_set->num_out_ch; j++)
               {
                  CNV_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "Out Channel_Type[%hhu]: %d",
                          j,
                          out_ch_map[j]);
               }

               uint8_t a = i * j;
               for (j = 0; j < a; j++)
               {
                  CNV_MSG(me_ptr->miid, DBG_HIGH_PRIO, "coefficients: %x", *(coef_ptr + j));
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
                  new_coeff_arr = new_coeff_arr + sizeof(capi_mfc_osrc_chmixer_coef_set_t);
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
            capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);
         }
         else
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set coeff, Bad param size %lu", param_size);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }
         break;
      }
      case PARAM_ID_MFC_RESAMPLER_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_mfc_resampler_cfg_t))
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "CAPI PCM_CNV: Bad param size %lu", params_ptr->actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         param_id_mfc_resampler_cfg_t *rs_config = (param_id_mfc_resampler_cfg_t *)(params_ptr->data_ptr);
         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Received PARAM_ID_MFC_RESAMPLER_CFG type %d use_hw_rs: %d dynamic_mode %d delay_type %d"
                 " (IIR/HW/dynamic options ignored by OSResampler; use PARAM_ID_MFC_OSRC_RESAMPLER_CFG for quality)",
                 rs_config->resampler_type,
                 rs_config->use_hw_rs,
                 rs_config->dynamic_mode,
                 rs_config->delay_type);

         /* OSResampler is SW-only and always fixed-ratio — IIR, HW, and dynamic-mode
          * fields are not applicable. Accept for backward compat and trigger reinit.
          * Quality knobs (quality_preset, use_interpolation, use_blackman_harris)
          * are controlled exclusively via PARAM_ID_MFC_OSRC_RESAMPLER_CFG. */
         capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);

         break;
      }
      case PARAM_ID_MFC_OSRC_RESAMPLER_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_mfc_osrc_resampler_cfg_t))
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO,
                    "CAPI PCM_CNV: PARAM_ID_MFC_OSRC_RESAMPLER_CFG bad param size %lu",
                    params_ptr->actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         param_id_mfc_osrc_resampler_cfg_t *osrc_cfg =
            (param_id_mfc_osrc_resampler_cfg_t *)(params_ptr->data_ptr);

         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Received PARAM_ID_MFC_OSRC_RESAMPLER_CFG quality_preset=%d "
                 "use_interpolation=%d use_blackman_harris=%d",
                 osrc_cfg->quality_preset,
                 osrc_cfg->use_interpolation,
                 osrc_cfg->use_blackman_harris);

         if (osrc_cfg->quality_preset <= 3)
         {
            me_ptr->osrc_resampler_cfg.quality_preset = osrc_cfg->quality_preset;
         }
         else
         {
            CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO,
                    "PARAM_ID_MFC_OSRC_RESAMPLER_CFG: invalid quality_preset %d (valid: 0-3), keeping %d",
                    osrc_cfg->quality_preset, me_ptr->osrc_resampler_cfg.quality_preset);
         }
         me_ptr->osrc_resampler_cfg.use_interpolation   = (osrc_cfg->use_interpolation != 0) ? 1 : 0;
         me_ptr->osrc_resampler_cfg.use_blackman_harris = (osrc_cfg->use_blackman_harris != 0) ? 1 : 0;

         capi_result |= capi_mfc_osrc_lib_init(me_ptr, FALSE);

         break;
      }
      case FWK_EXTN_DM_PARAM_ID_CHANGE_MODE:
      {
         // OSResampler MFC supports DM for all MFC instances
         fwk_extn_dm_param_id_change_mode_t *mode_ptr = (fwk_extn_dm_param_id_change_mode_t *)params_ptr->data_ptr;

         CNV_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Set param received FWK_EXTN_DM_PARAM_ID_CHANGE_MODE with mode = %d",
                 mode_ptr->dm_mode);

         if ((FWK_EXTN_DM_FIXED_INPUT_MODE == mode_ptr->dm_mode) ||
             (FWK_EXTN_DM_FIXED_OUTPUT_MODE == mode_ptr->dm_mode))
         {
            if (!capi_mfc_osrc_is_dm_enabled(me_ptr) && (me_ptr->pc[0].osrc_rs_lib_ptr))
            {
               // enable dm mode for OSResampler
               me_ptr->dm_info.is_dm_disabled = FALSE;
               capi_cmn_raise_dm_disable_event(&me_ptr->cb_info, me_ptr->miid, FWK_EXTN_DM_ENABLED_DM);
            }

            if (me_ptr->pc[0].osrc_rs_lib_ptr)
            {
               me_ptr->dm_info.req_out_samples     = 0;
               me_ptr->dm_info.expected_in_samples = 0;
               osrc_rs_lib_set_dm_config(me_ptr->pc[0].osrc_rs_lib_ptr,
                                         FWK_EXTN_DM_FIXED_INPUT_MODE,
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
      case FWK_EXTN_DM_PARAM_ID_SET_MAX_SAMPLES:
      {
         if (!capi_mfc_osrc_is_dm_enabled(me_ptr))
         {
            CNV_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "FWK_EXTN_DM_PARAM_ID_SET_SAMPLES/SET_MAX_SAMPLES not supported: is_dm_enabled=%d",
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
            break;
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
               osrc_rs_lib_set_dm_config(me_ptr->pc[0].osrc_rs_lib_ptr,
                                         me_ptr->dm_info.dm_mode,
                                         me_ptr->dm_info.req_out_samples);
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
            osrc_rs_lib_set_dm_config(me_ptr->pc[0].osrc_rs_lib_ptr,
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

         mfc_osrc_set_consume_partial_input(&me_ptr->pc[0], me_ptr->dm_info.should_consume_partial_input);

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

      default:
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set, unsupported param ID 0x%x", (int)param_id);
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
capi_err_t capi_mfc_osrc_get_param(capi_t *                _pif,
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

   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)_pif;

   switch (param_id)
   {
      case PARAM_ID_MFC_RESAMPLER_CFG:
      {
         if (params_ptr->max_data_len >= sizeof(param_id_mfc_resampler_cfg_t))
         {
            param_id_mfc_resampler_cfg_t *rs_config = (param_id_mfc_resampler_cfg_t *)(params_ptr->data_ptr);
            memset(rs_config, 0, sizeof(param_id_mfc_resampler_cfg_t));
            /* OSResampler is always SW FIR — report FIR (0) with SW (0), non-dynamic (0).
             * OSResampler quality fields are in PARAM_ID_MFC_OSRC_RESAMPLER_CFG. */
            rs_config->resampler_type = 0; /* FIR */
            rs_config->use_hw_rs      = 0; /* SW */
            rs_config->dynamic_mode   = 0;
            rs_config->delay_type     = 0;
            params_ptr->actual_data_len = sizeof(param_id_mfc_resampler_cfg_t);
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         }
         break;
      }
      case PARAM_ID_MFC_OSRC_RESAMPLER_CFG:
      {
         if (params_ptr->max_data_len >= sizeof(param_id_mfc_osrc_resampler_cfg_t))
         {
            param_id_mfc_osrc_resampler_cfg_t *osrc_cfg =
               (param_id_mfc_osrc_resampler_cfg_t *)(params_ptr->data_ptr);
            *osrc_cfg = me_ptr->osrc_resampler_cfg;
            params_ptr->actual_data_len   = sizeof(param_id_mfc_osrc_resampler_cfg_t);
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         }
         break;
      }
      case PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT:
      {
         capi_standard_data_format_v2_t *out_std_ptr = &me_ptr->out_media_fmt[port].format;

         uint32_t size_chmap = (CAPI_DATA_FORMAT_INVALID_VAL != out_std_ptr->num_channels)
                                  ? (out_std_ptr->num_channels * sizeof(uint16_t))
                                  : 0;
         uint32_t required_size = sizeof(param_id_mfc_output_media_fmt_t) + size_chmap;

         if (params_ptr->max_data_len >= required_size)
         {
            param_id_mfc_output_media_fmt_t *mf_ptr =
               reinterpret_cast<param_id_mfc_output_media_fmt_t *>(params_ptr->data_ptr);

            mf_ptr->bit_width     = capi_mfc_osrc_mfc_set_extn_inp_mf_bitwidth(out_std_ptr->q_factor);
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
                    "Get param id 0x%lx, invalid payload size %lu",
                    param_id,
                    params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }
      case FWK_EXTN_DM_PARAM_ID_SET_SAMPLES:
      case FWK_EXTN_DM_PARAM_ID_SET_MAX_SAMPLES:
      {
         if (params_ptr->max_data_len >= sizeof(fwk_extn_dm_param_id_req_samples_t))
         {
            fwk_extn_dm_param_id_req_samples_t *req_samples_ptr =
               (fwk_extn_dm_param_id_req_samples_t *)params_ptr->data_ptr;

            if ((FWK_EXTN_DM_FIXED_OUTPUT_MODE == me_ptr->dm_info.dm_mode) &&
                (FWK_EXTN_DM_PARAM_ID_SET_SAMPLES == param_id) && capi_mfc_osrc_is_dm_enabled(me_ptr))
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
                    "Get param id 0x%lx, invalid payload size %lu",
                    param_id,
                    params_ptr->actual_data_len);
            capi_result = CAPI_ENEEDMORE;
         }
         break;
      }
      
      default:
         CNV_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Get, unsupported param ID 0x%x", (int)param_id);
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
         break;
   }
   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Function to set the properties of PCM_CNV module
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{

   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == props_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Set properties received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)_pif;

   capi_result |= capi_mfc_osrc_process_set_properties(me_ptr, props_ptr);

   return capi_result;
}

/*_____________________________________________________________________________________________________________________
 DESCRIPTION:
 Function to get the properties of PCM_CNV module
 ____________________________________________________________________________________________________________________*/
capi_err_t capi_mfc_osrc_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif || NULL == props_ptr)
   {
      CNV_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get properties received bad pointer, 0x%p", _pif);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   capi_mfc_osrc_t *me_ptr = (capi_mfc_osrc_t *)_pif;

   capi_result |= capi_mfc_osrc_process_get_properties(me_ptr, props_ptr);

   return capi_result;
}
