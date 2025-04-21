/* ===========================================================================
  @file capi_sh_mem_pull_push_mode.c
  @brief This file contains CAPI implementation of shared memory
         pull and push mode Module

   Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
   SPDX-License-Identifier: BSD-3-Clause-Clear
==============================================================================*/

/*--------------------------------------------------------------------------
 * Include files and Macro definitions
 * ------------------------------------------------------------------------ */
#include "pull_push_mode.h"
#include "sh_mem_pull_push_mode_api.h"
#include "capi_properties.h"
#include "capi.h"
#include "ar_msg.h"
#include "capi_sh_mem_pull_push_mode.h"

#define CAPI_PM_STACK_SIZE 4096 // TODO: To be measured

/* Number of CAPI Framework extension needed */
#define PM_NUM_FRAMEWORK_EXTENSIONS 2

/*------------------------------------------------------------------------
 * Static declarations
 * -----------------------------------------------------------------------*/

static capi_err_t capi_pm_end(capi_t *_pif);

static capi_err_t capi_pm_set_param(capi_t                 *_pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t             *params_ptr);

static capi_err_t capi_pm_get_param(capi_t                 *_pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t             *params_ptr);

static capi_err_t capi_pm_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_pm_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static const capi_vtbl_t pull_mode_vtbl = { pull_mode_read_input,   capi_pm_end,
                                            capi_pm_set_param,      capi_pm_get_param,
                                            capi_pm_set_properties, capi_pm_get_properties };

static const capi_vtbl_t push_mode_vtbl = { push_mode_write_output, capi_pm_end,
                                            capi_pm_set_param,      capi_pm_get_param,
                                            capi_pm_set_properties, capi_pm_get_properties };

static capi_err_t capi_pm_process_get_properties(capi_pm_t *me_ptr, capi_proplist_t *proplist_ptr);

static void capi_pm_check_n_enable_module_buffer_access_extension(capi_pm_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;

   // note that push/pull module only process CAPI_INTERLEAVED format, hence just checking for validaty is sufficient
   bool_t need_to_enable_extension = (TRUE == pull_push_check_media_fmt_validity(&me_ptr->pull_push_mode_info)) &&
                                     (CAPI_INTERLEAVED == me_ptr->pull_push_mode_info.media_fmt.data_interleaving);

   // check if circular buffer size is set and mulitple of container framelength
   if (me_ptr->pull_push_mode_info.shared_circ_buf_size && me_ptr->frame_dur_us)
   {
      uint32_t frame_size_in_bytes = capi_cmn_us_to_bytes(me_ptr->frame_dur_us,
                                                          me_ptr->pull_push_mode_info.media_fmt.sample_rate,
                                                          me_ptr->pull_push_mode_info.media_fmt.bits_per_sample,
                                                          me_ptr->pull_push_mode_info.media_fmt.num_channels);

      // check if circ buf size is integral multiple of the container frame size duration
      if (me_ptr->pull_push_mode_info.shared_circ_buf_size % frame_size_in_bytes)
      {
         need_to_enable_extension = FALSE;
      }
      else
      {
         need_to_enable_extension = need_to_enable_extension && TRUE;
      }

      PULL_PUSH_MSG(me_ptr->pull_push_mode_info.miid,
                    DBG_HIGH_PRIO,
                    "circ buf size %lu frame size %lu is mod buf extn enabled ? %lu",
                    me_ptr->pull_push_mode_info.shared_circ_buf_size,
                    frame_size_in_bytes,
                    need_to_enable_extension);
   }
   else
   {
      // nothing do if buf size or cntr frame duration are not known.
      return;
   }

   if (need_to_enable_extension == me_ptr->pull_push_mode_info.is_mod_buf_access_enabled)
   {
      // nothing do if already set
      return;
   }

   if (PUSH_MODE == me_ptr->pull_push_mode_info.mode)
   {
      result = capi_cmn_intf_extn_event_module_input_buffer_reuse(me_ptr->pull_push_mode_info.miid,
                                                                  &me_ptr->cb_info,
                                                                  0,                        // port_index
                                                                  need_to_enable_extension, // enable
                                                                  (uint32_t)&me_ptr->pull_push_mode_info,
                                                                  push_module_buf_mgr_extn_get_input_buf);
   }
   else // source
   {
      result = capi_cmn_intf_extn_event_module_output_buffer_reuse(me_ptr->pull_push_mode_info.miid,
                                                                   &me_ptr->cb_info,
                                                                   0,                        // port_index
                                                                   need_to_enable_extension, // enable
                                                                   (uint32_t)&me_ptr->pull_push_mode_info,
                                                                   pull_module_buf_mgr_extn_return_output_buf);
   }

   me_ptr->pull_push_mode_info.is_mod_buf_access_enabled = CAPI_FAILED(result) ? FALSE : need_to_enable_extension;
}

static capi_err_t capi_pm_raise_output_media_fmt_event(capi_pm_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (PUSH_MODE == me_ptr->pull_push_mode_info.mode)
   {
      return result;
   }

   capi_media_fmt_v2_t media_fmt;
   media_fmt.header.format_header.data_format = CAPI_FIXED_POINT;
   media_fmt.format.bitstream_format          = me_ptr->pull_push_mode_info.media_fmt.fmt_id;
   media_fmt.format.bits_per_sample           = me_ptr->pull_push_mode_info.media_fmt.bits_per_sample;
   media_fmt.format.num_channels              = me_ptr->pull_push_mode_info.media_fmt.num_channels;
   media_fmt.format.q_factor                  = me_ptr->pull_push_mode_info.media_fmt.Q_format;
   media_fmt.format.data_is_signed            = me_ptr->pull_push_mode_info.media_fmt.is_signed;
   media_fmt.format.sampling_rate             = me_ptr->pull_push_mode_info.media_fmt.sample_rate;
   media_fmt.format.data_interleaving         = me_ptr->pull_push_mode_info.media_fmt.data_interleaving;

   for (uint32_t i = 0; i < media_fmt.format.num_channels; i++)
   {
      media_fmt.channel_type[i] = me_ptr->pull_push_mode_info.media_fmt.channel_map[i];
   }

   result = capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &media_fmt, FALSE, 0);

   capi_pm_check_n_enable_module_buffer_access_extension(me_ptr);

   return result;
}

static inline uint32_t align_to_8_byte(const uint32_t num)
{
   return ((num + 7) & (0xFFFFFFF8));
}

capi_err_t capi_pm_raise_event_to_clients(capi_pm_t *me, uint32_t event_id, void *payload_ptr, uint32_t payload_len)
{
   capi_err_t                         result = CAPI_EOK;
   capi_event_info_t                  event_info;
   capi_event_data_to_dsp_client_v2_t event;

   for (int i = 0; i < MAX_EVENT_CLIENTS; i++)
   {
      if ((event_id == me->pull_push_mode_info.event_client_info[i].event_id) &&
          (0 != me->pull_push_mode_info.event_client_info[i].dest_addr))
      {
         event.event_id                     = event_id;
         event.payload.actual_data_len      = payload_len;
         event.payload.max_data_len         = payload_len;
         event.payload.data_ptr             = (int8_t *)payload_ptr;
         event_info.port_info.is_valid      = false;
         event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
         event_info.payload.data_ptr        = (int8_t *)&event;
         event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);
         event.dest_address                 = me->pull_push_mode_info.event_client_info[i].dest_addr;
         event.token                        = me->pull_push_mode_info.event_client_info[i].token;

         result = me->cb_info.event_cb(me->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_CLIENT_V2, &event_info);
         if (CAPI_EOK != result)
         {
            PULL_PUSH_MSG(me->pull_push_mode_info.miid,
                          DBG_ERROR_PRIO,
                          "capi_pm_raise_event_to_clients: Failed to raise event id 0x%lx",
                          event_id);
         }
      }
   }

   return result;
}

capi_err_t capi_pull_mode_get_static_properties(capi_proplist_t *init_set_properties,
                                                capi_proplist_t *static_properties)
{
   PULL_PUSH_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, "Enter get static prop");

   capi_err_t capi_result = CAPI_EOK;

   if (NULL != static_properties)
   {
      capi_result = capi_pm_process_get_properties((capi_pm_t *)NULL, static_properties);

      if (CAPI_FAILED(capi_result))
      {
         return capi_result;
      }
   }

   return capi_result;
}

capi_err_t capi_push_mode_get_static_properties(capi_proplist_t *init_set_properties,
                                                capi_proplist_t *static_properties)
{
   PULL_PUSH_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, "Enter get static prop");

   capi_err_t capi_result = CAPI_EOK;

   if (NULL != static_properties)
   {
      capi_result = capi_pm_process_get_properties((capi_pm_t *)NULL, static_properties);

      if (CAPI_FAILED(capi_result))
      {
         return capi_result;
      }
   }

   return capi_result;
}

static capi_err_t capi_pm_process_init(capi_pm_t *me_ptr, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   PULL_PUSH_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, " Enter init---------");

   if (NULL == me_ptr || NULL == init_set_properties)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Init received bad pointer, 0x%p, 0x%p", me_ptr, init_set_properties);

      return CAPI_EBADPARAM;
   }
   capi_result = capi_pm_set_properties((capi_t *)me_ptr, init_set_properties);

   if (CAPI_EOK != capi_result)
   {
      PULL_PUSH_MSG(me_ptr->pull_push_mode_info.miid,
                    DBG_ERROR_PRIO,
                    "Set basic properties failed with result %lu",
                    capi_result);
      return capi_result;
   }
   me_ptr->pull_push_mode_info.media_fmt.data_interleaving = CAPI_DEINTERLEAVED_UNPACKED_V2;
   prio_query_t prio_query = { .is_interrupt_trig = FALSE, .static_req_id = SPF_THREAD_STAT_IST_ID };
   posal_thread_calc_prio(&prio_query, &me_ptr->pull_push_mode_info.ist_priority);

   capi_result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   return capi_result;
}

capi_err_t capi_pull_mode_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   PULL_PUSH_MSG(MIID_UNKNOWN, DBG_LOW_PRIO, " Enter capi_pull_mode_init-----");

   if (NULL == _pif || NULL == init_set_properties)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Init received bad pointer, 0x%p, 0x%p", _pif, init_set_properties);

      return CAPI_EBADPARAM;
   }

   capi_pm_t *me_ptr = (capi_pm_t *)_pif;
   memset((void *)me_ptr, 0, sizeof(capi_pm_t));

   me_ptr->vtbl.vtbl_ptr = &pull_mode_vtbl;

   me_ptr->pull_push_mode_info.mode = PULL_MODE;

   capi_result = capi_pm_process_init(me_ptr, init_set_properties);
   PULL_PUSH_MSG(me_ptr->pull_push_mode_info.miid, DBG_LOW_PRIO, "Initialization completed !!");

   return capi_result;
}

capi_err_t capi_push_mode_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   capi_err_t capi_result = CAPI_EOK;

   PULL_PUSH_MSG(MIID_UNKNOWN, DBG_HIGH_PRIO, " Enter capi_push_mode_init------");

   if (NULL == _pif || NULL == init_set_properties)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Init received bad pointer, 0x%p, 0x%p", _pif, init_set_properties);

      return CAPI_EBADPARAM;
   }

   capi_pm_t *me_ptr = (capi_pm_t *)_pif;
   memset((void *)me_ptr, 0, sizeof(capi_pm_t));

   me_ptr->vtbl.vtbl_ptr = &push_mode_vtbl;

   me_ptr->pull_push_mode_info.mode = PUSH_MODE;

   memset(&me_ptr->pull_push_mode_info.media_fmt, 0, sizeof(pm_media_fmt_t));
   capi_result = capi_pm_process_init(me_ptr, init_set_properties);

   return capi_result;
}

static capi_err_t capi_pm_end(capi_t *_pif)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == _pif)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "End received bad pointer, 0x%p", _pif);
      return CAPI_EBADPARAM;
   }
   capi_pm_t *me_ptr = (capi_pm_t *)_pif;

   pull_push_mode_deinit(&(me_ptr->pull_push_mode_info));

   return capi_result;
}

static capi_err_t capi_pm_set_param(capi_t                 *_pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t             *params_ptr)
{
   if (NULL == _pif || NULL == params_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN,
                    DBG_ERROR_PRIO,
                    "Set param received bad pointer, 0x%p, 0x%p",
                    _pif,
                    params_ptr);

      return CAPI_EBADPARAM;
   }

   capi_err_t capi_result = CAPI_EOK;
   capi_pm_t *me_ptr      = (capi_pm_t *)_pif;
   uint32_t   miid        = me_ptr->pull_push_mode_info.miid;

   void *param_payload_ptr = (void *)params_ptr->data_ptr;

   if (NULL == param_payload_ptr)
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "Set param received NULL payload pointer");

      return CAPI_EBADPARAM;
   }

   PULL_PUSH_MSG(miid,
                 DBG_HIGH_PRIO,
                 "capi_pm_set_param: set param received for param id, 0x%lX miid: %d",
                 param_id,
                 miid);

   switch (param_id)
   {
      case PARAM_ID_SH_MEM_PULL_PUSH_MODE_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(sh_mem_pull_push_mode_cfg_t))
         {
            PULL_PUSH_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Set param param id 0x%lX. Size not enough %lu",
                          param_id,
                          params_ptr->actual_data_len);
            capi_result = CAPI_EBADPARAM;
         }
         else
         {
            sh_mem_pull_push_mode_cfg_t *pull_push_mode_cfg_ptr = (sh_mem_pull_push_mode_cfg_t *)params_ptr->data_ptr;
            capi_result = pull_push_mode_init(&(me_ptr->pull_push_mode_info), pull_push_mode_cfg_ptr);
            if (capi_result)
            {
               PULL_PUSH_MSG(miid,
                             DBG_ERROR_PRIO,
                             "Set param failed for param id 0x%lX, result %d",
                             param_id,
                             capi_result);
            }
         }
         break;
      }
      case PARAM_ID_MEDIA_FORMAT:
      {
         if (params_ptr->actual_data_len < sizeof(media_format_t))
         {
            PULL_PUSH_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Set param param id 0x%lX. Size not enough %lu",
                          param_id,
                          params_ptr->actual_data_len);
            capi_result = CAPI_EBADPARAM;
         }
         else
         {
            media_format_t *media_fmt = (media_format_t *)params_ptr->data_ptr;
            if ((MEDIA_FMT_ID_PCM != media_fmt->fmt_id) || (DATA_FORMAT_FIXED_POINT != media_fmt->data_format))
            {
               PULL_PUSH_MSG(miid,
                             DBG_ERROR_PRIO,
                             "Set param param id 0x%lX. Invalid fmt_id 0x%lX OR data_format 0x%lX",
                             param_id,
                             media_fmt->fmt_id,
                             media_fmt->data_format);
               capi_result = CAPI_EBADPARAM;
            }
            else
            {
               if ((params_ptr->actual_data_len < (sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t))) ||
                   (media_fmt->payload_size < sizeof(payload_media_fmt_pcm_t)))
               {
                  PULL_PUSH_MSG(miid,
                                DBG_ERROR_PRIO,
                                "Set param param id 0x%lX. Size not enough %lu. media_fmt->payload_size = %lu",
                                param_id,
                                sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t),
                                media_fmt->payload_size);
                  capi_result = CAPI_EBADPARAM;
               }
               else
               {
                  /** for pull-mode client gives input media fmt thru this param-id.
                   *  for push-mode this param-id is indication of what media fmt the client is expecting.
                   *  we just need to store it and compare with input media fmt. */
                  if (PULL_MODE == me_ptr->pull_push_mode_info.mode)
                  {
                     me_ptr->pull_push_mode_info.is_disabled = FALSE;

                     // copy incoming media fmt as input media fmt.
                     capi_err_t local_result = pull_push_mode_set_inp_media_fmt(&(me_ptr->pull_push_mode_info),
                                                                                media_fmt,
                                                                                &me_ptr->pull_push_mode_info.media_fmt);

                     if ((CAPI_EOK == local_result) &&
                         (TRUE == pull_push_check_media_fmt_validity(&(me_ptr->pull_push_mode_info))))
                     {
                        capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, CAPI_PM_KPPS);
                        capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->cb_info, 0, 0);
                        capi_result |= capi_cmn_update_process_check_event(&me_ptr->cb_info, 1);
                        capi_result |= capi_cmn_update_algo_delay_event(&me_ptr->cb_info, 0);
                        capi_pm_raise_output_media_fmt_event(me_ptr);
                     }
                     else
                     {
                        capi_result |= local_result;
                        PULL_PUSH_MSG(miid,
                                      DBG_ERROR_PRIO,
                                      "Set param failed for param id 0x%lx, result %d",
                                      param_id,
                                      capi_result);
                     }
                  }
                  else
                  {
                     pm_media_fmt_t *med_ptr = &me_ptr->pull_push_mode_info.media_fmt;
                     pm_media_fmt_t *cfg_ptr = &me_ptr->pull_push_mode_info.cfg_media_fmt;

                     // copy incoming media fmt as cfg media fmt.
                     capi_err_t local_result =
                        pull_push_mode_set_inp_media_fmt(&(me_ptr->pull_push_mode_info), media_fmt, cfg_ptr);
                     if (CAPI_EOK == local_result)
                     {
                        return local_result;
                     }

                     // if input media fmt has arrived, then check if it matches configured media fmt
                     if (0 != med_ptr->num_channels)
                     {
                        if ((med_ptr->num_channels != cfg_ptr->num_channels) ||
                            (med_ptr->sample_rate != cfg_ptr->sample_rate) ||
                            (med_ptr->bits_per_sample != cfg_ptr->bits_per_sample) ||
                            (med_ptr->Q_format != cfg_ptr->Q_format))
                        {
                           PULL_PUSH_MSG(miid,
                                         DBG_ERROR_PRIO,
                                         "Push mode: Media format from client must match input media format.");
                           capi_result = CAPI_EBADPARAM;
                        }
                     }
                  }
               }
            }
         }

         break;
      }
      case PARAM_ID_DATA_INTERLEAVING:
      {
         if (PULL_MODE == me_ptr->pull_push_mode_info.mode)
         {
            if (params_ptr->actual_data_len < sizeof(param_id_module_data_interleaving_t))
            {
               PULL_PUSH_MSG(miid,
                             DBG_ERROR_PRIO,
                             "Set param param id 0x%lX. Size not enough %lu",
                             param_id,
                             params_ptr->actual_data_len);
               capi_result = CAPI_EBADPARAM;
            }

            param_id_module_data_interleaving_t *push_pull_intl_ptr =
               (param_id_module_data_interleaving_t *)params_ptr->data_ptr;

            // convert the PCM interleaving value to CAPI interleaving
            pcm_to_capi_interleaved_with_native_param_v2(&me_ptr->pull_push_mode_info.media_fmt.data_interleaving,
                                                         push_pull_intl_ptr->data_interleaving,
                                                         CAPI_INVALID_INTERLEAVING);
            capi_pm_raise_output_media_fmt_event(me_ptr);
            PULL_PUSH_MSG(miid,
                          DBG_LOW_PRIO,
                          "Data interleaving set to %d (0 - intlvd, 1 - packed, 3 - unpacked v2)",
                          me_ptr->pull_push_mode_info.media_fmt.data_interleaving);
         }
         else
         {
            capi_result |= CAPI_EUNSUPPORTED;
         }
         break;
      }
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if ((params_ptr->actual_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t)) &&
             (TRUE == port_info_ptr->is_input_port))
         {
            fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
               (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

            PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN, entry");

            pull_push_mode_set_fwk_ext_inp_media_fmt(&(me_ptr->pull_push_mode_info), extn_ptr);

            if (PUSH_MODE == me_ptr->pull_push_mode_info.mode)
            {
               pm_media_fmt_t *med_ptr = &me_ptr->pull_push_mode_info.media_fmt;
               pm_media_fmt_t *cfg_ptr = &me_ptr->pull_push_mode_info.cfg_media_fmt;

               // if cfg media format has come has arrived, then check if it matches configured media fmt
               if (0 != cfg_ptr->num_channels)
               {
                  if ((med_ptr->num_channels != cfg_ptr->num_channels) ||
                      (med_ptr->sample_rate != cfg_ptr->sample_rate) ||
                      (med_ptr->bits_per_sample != cfg_ptr->bits_per_sample) ||
                      (med_ptr->Q_format != cfg_ptr->Q_format))
                  {
                     PULL_PUSH_MSG(miid,
                                   DBG_ERROR_PRIO,
                                   "Push mode: Media format from client must match input media format.");
                  }
               }
            }
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            PULL_PUSH_MSG(miid,
                          DBG_ERROR_PRIO,
                          "set param failed because of length issues, 0x%p, 0x%p, in_len = %d, needed_len = "
                          "%d",
                          _pif,
                          param_id,
                          params_ptr->actual_data_len,
                          sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t));
         }
         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_FRAME_DURATION:
      {
         uint16_t param_size = params_ptr->actual_data_len;
         if (param_size < sizeof(fwk_extn_param_id_container_frame_duration_t))
         {
            PULL_PUSH_MSG(miid,
                          DBG_ERROR_PRIO,
                          "Param id 0x%lx Bad param size %lu",
                          (uint32_t)param_id,
                          param_size);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_container_frame_duration_t *fm_dur =
            (fwk_extn_param_id_container_frame_duration_t *)params_ptr->data_ptr;
         me_ptr->frame_dur_us = fm_dur->duration_us;

         PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "Received container frame duration %lu ", me_ptr->frame_dur_us);

         capi_pm_check_n_enable_module_buffer_access_extension(me_ptr);
         break;
      }
      default:
      {
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);

         break;
      }
   }
   return capi_result;
}

static capi_err_t capi_pm_get_param(capi_t                 *_pif,
                                    uint32_t                param_id,
                                    const capi_port_info_t *port_info_ptr,
                                    capi_buf_t             *params_ptr)
{
   if (NULL == _pif || NULL == params_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN,
                    DBG_ERROR_PRIO,
                    "Get param received bad pointer, 0x%p, 0x%p",
                    _pif,
                    params_ptr);

      return CAPI_EOK;
   }

   capi_err_t capi_result = CAPI_EOK;
   capi_pm_t *me_ptr      = (capi_pm_t *)_pif;
   uint32_t   miid        = me_ptr->pull_push_mode_info.miid;

   void *param_payload_ptr = (void *)params_ptr->data_ptr;

   if (NULL == param_payload_ptr)
   {
      PULL_PUSH_MSG(miid, DBG_ERROR_PRIO, "Set param received NULL payload pointer");

      return CAPI_EBADPARAM;
   }

   switch (param_id)
   {
      case FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN:
      {
         if ((params_ptr->actual_data_len >= sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t)))
         {
            fwk_extn_pcm_param_id_media_fmt_extn_t *extn_ptr =
               (fwk_extn_pcm_param_id_media_fmt_extn_t *)(params_ptr->data_ptr);

            PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "GET Param FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN, entry");
            extn_ptr->alignment  = me_ptr->pull_push_mode_info.media_fmt.alignment;
            extn_ptr->bit_width  = me_ptr->pull_push_mode_info.media_fmt.bit_width;
            extn_ptr->endianness = me_ptr->pull_push_mode_info.media_fmt.endianness;
            PULL_PUSH_MSG(miid, DBG_HIGH_PRIO, "GET Param FWK_EXTN_PCM_PARAM_ID_MEDIA_FORMAT_EXTN, exit");
         }
         else
         {
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            PULL_PUSH_MSG(miid,
                          DBG_ERROR_PRIO,
                          "get param failed because of length issues, 0x%p, 0x%p, in_len = %d, needed_len = "
                          "%d",
                          _pif,
                          param_id,
                          params_ptr->actual_data_len,
                          sizeof(fwk_extn_pcm_param_id_media_fmt_extn_t));
         }
         break;
      }

      default:
      {
         CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);

         break;
      }
   }
   return capi_result;
}

static capi_err_t capi_pm_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == props_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Set property received null property array");
      return CAPI_EBADPARAM;
   }

   capi_pm_t *me_ptr = (capi_pm_t *)_pif;
   if (NULL == me_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Set property received null property array");
      return CAPI_EBADPARAM;
   }
   pull_push_mode_t *pm_info_ptr = &(me_ptr->pull_push_mode_info);

   capi_result = capi_cmn_set_basic_properties(props_ptr, &me_ptr->heap_mem, &me_ptr->cb_info, FALSE);
   if (CAPI_EOK != capi_result)
   {
      PULL_PUSH_MSG(pm_info_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Set basic properties failed with result %lu",
                    capi_result);
      return capi_result;
   }

   capi_prop_t *prop_ptr = props_ptr->prop_ptr;

   for (uint32_t i = 0; i < props_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->pull_push_mode_info.miid    = data_ptr->module_instance_id;
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_LOW_PRIO,
                             "This module_instance_id 0x%08lX",
                             data_ptr->module_instance_id);
            }
            else
            {
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Set, Param id 0x%lx Bad param size %lu",
                             (uint32_t)prop_ptr[i].id,
                             payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            PULL_PUSH_MSG(pm_info_ptr->miid, DBG_HIGH_PRIO, "Set property received for input media fmt");
            me_ptr->pull_push_mode_info.is_disabled = FALSE;

            /* If the query happens for module output port */
            if (!prop_ptr[i].port_info.is_input_port)
            {
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_ERROR_PRIO,
                             "incorrect port info, output port[%d]",
                             prop_ptr[i].id);

               capi_result |= CAPI_EBADPARAM;
               break;
            }

            /* Validate the MF payload */
            if (payload_ptr->max_data_len < sizeof(capi_pm_media_fmt_t))
            {
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Not valid media format size %d",
                             payload_ptr->actual_data_len);

               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            /* Return error if circular buffer is not configured */
            if (0 == me_ptr->pull_push_mode_info.shared_circ_buf_size)
            {
               PULL_PUSH_MSG(pm_info_ptr->miid, DBG_ERROR_PRIO, "circular buffer is not configured");
               me_ptr->pull_push_mode_info.is_disabled = TRUE;
               capi_result                             = CAPI_EFAILED;
            }

            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            typedef struct pm_media_format_t
            {
               media_format_t          fmt;
               payload_media_fmt_pcm_t pcm;
               uint8_t                 channel_mapping[CAPI_MAX_CHANNELS_V2];
            } pm_media_format_t;
            pm_media_format_t media_fmt;

            media_fmt.fmt.data_format     = 0;
            media_fmt.fmt.payload_size    = sizeof(payload_media_fmt_pcm_t);
            media_fmt.fmt.fmt_id          = me_ptr->pull_push_mode_info.media_fmt.fmt_id;
            media_fmt.pcm.num_channels    = media_fmt_ptr->format.num_channels;
            media_fmt.pcm.bit_width       = QFORMAT_TO_BIT_WIDTH(media_fmt_ptr->format.q_factor);
            media_fmt.pcm.q_factor        = media_fmt_ptr->format.q_factor;
            media_fmt.pcm.sample_rate     = media_fmt_ptr->format.sampling_rate;
            media_fmt.pcm.bits_per_sample = media_fmt_ptr->format.bits_per_sample;
            media_fmt.pcm.endianness      = me_ptr->pull_push_mode_info.media_fmt.endianness;
            media_fmt.pcm.alignment       = me_ptr->pull_push_mode_info.media_fmt.alignment;

            for (int i = 0; i < media_fmt_ptr->format.num_channels; i++)
            {
               media_fmt.channel_mapping[i] = media_fmt_ptr->channel_type[i];
            }

            capi_result = pull_push_mode_set_inp_media_fmt(&(me_ptr->pull_push_mode_info),
                                                           &media_fmt.fmt,
                                                           &me_ptr->pull_push_mode_info.media_fmt);
            me_ptr->pull_push_mode_info.media_fmt.data_interleaving = media_fmt_ptr->format.data_interleaving;

            if ((CAPI_EOK == capi_result) &&
                (TRUE == pull_push_check_media_fmt_validity(&(me_ptr->pull_push_mode_info))))
            {
               capi_result |= capi_cmn_update_kpps_event(&me_ptr->cb_info, CAPI_PM_KPPS);
               capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->cb_info, 0, 0);
               capi_result |= capi_cmn_update_process_check_event(&me_ptr->cb_info, 1);
               capi_result |= capi_cmn_update_algo_delay_event(&me_ptr->cb_info, 0);
            }
            else
            {
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Set prop failed for prop id 0x%lx, result %d",
                             prop_ptr[i].id,
                             capi_result);
            }

            capi_pm_check_n_enable_module_buffer_access_extension(me_ptr);

            break;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            PULL_PUSH_MSG(pm_info_ptr->miid, DBG_HIGH_PRIO, "Set property received for algorithmic reset");

            if (NULL != me_ptr->pull_push_mode_info.shared_pos_buf_ptr)
            {
               PULL_PUSH_MSG(pm_info_ptr->miid, DBG_HIGH_PRIO, "Resetting shared position structure");
               memset(me_ptr->pull_push_mode_info.shared_pos_buf_ptr,
                      0,
                      sizeof(sh_mem_pull_push_mode_position_buffer_t));

               me_ptr->pull_push_mode_info.next_read_index = 0;
            }
            break;
         }
         case CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2:
         {
            PULL_PUSH_MSG(pm_info_ptr->miid,
                          DBG_HIGH_PRIO,
                          "capi_sh_mem_pull_push_mode: Set property received for registering event data to dsp client "
                          "v2");

            /* Validate the payload */
            if (payload_ptr->actual_data_len < sizeof(capi_register_event_to_dsp_client_v2_t))
            {
               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_ERROR_PRIO,
                             "capi_sh_mem_pull_push_mode: Invalid payload size %d",
                             payload_ptr->actual_data_len);

               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            capi_register_event_to_dsp_client_v2_t *reg_event_ptr =
               (capi_register_event_to_dsp_client_v2_t *)(payload_ptr->data_ptr);

            // check if the event is valid
            if ((EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK != reg_event_ptr->event_id) &&
                (EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER != reg_event_ptr->event_id))
            {
               PULL_PUSH_MSG(pm_info_ptr->miid, DBG_HIGH_PRIO, "Unsupported event ID[%d]", reg_event_ptr->event_id);
               return CAPI_EUNSUPPORTED;
            }

            // iterate through the table and cache an empty slot index & also
            // cache the index if a client is already registered.
            bool_t  is_empty_slot_found      = FALSE;
            int32_t empty_slot_index         = -1;
            bool_t  is_already_registered    = FALSE;
            int32_t already_registered_index = -1;
            for (uint32_t i = 0; i < MAX_EVENT_CLIENTS; i++)
            {
               /* check if this is an empty slot */
               if (!is_empty_slot_found && 0 == me_ptr->pull_push_mode_info.event_client_info[i].dest_addr &&
                   0 == me_ptr->pull_push_mode_info.event_client_info[i].token &&
                   0 == me_ptr->pull_push_mode_info.event_client_info[i].event_id)
               {
                  is_empty_slot_found = TRUE;
                  empty_slot_index    = i;
               }

               /* checking if the client is already registered*/
               if (reg_event_ptr->dest_address == me_ptr->pull_push_mode_info.event_client_info[i].dest_addr &&
                   reg_event_ptr->token == me_ptr->pull_push_mode_info.event_client_info[i].token &&
                   reg_event_ptr->event_id == me_ptr->pull_push_mode_info.event_client_info[i].event_id)
               {
                  is_already_registered    = TRUE;
                  already_registered_index = i;
               }
            }

            if (1 == reg_event_ptr->is_register)
            {
               // validation
               if ((me_ptr->pull_push_mode_info.num_clients_registered >= MAX_EVENT_CLIENTS) || !is_empty_slot_found)
               {
                  PULL_PUSH_MSG(pm_info_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_sh_mem_pull_push_mode: Reached max clients %lu, cannot register event id 0x%lx",
                                me_ptr->pull_push_mode_info.num_clients_registered,
                                reg_event_ptr->event_id);
                  return CAPI_EFAILED;
               }
               else if (is_already_registered)
               {
                  PULL_PUSH_MSG(pm_info_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_sh_mem_pull_push_mode: Event id 0x%lx already registered index %lu",
                                reg_event_ptr->event_id,
                                already_registered_index);
                  return CAPI_EFAILED;
               }

               // cache new client info
               me_ptr->pull_push_mode_info.event_client_info[empty_slot_index].dest_addr = reg_event_ptr->dest_address;
               me_ptr->pull_push_mode_info.event_client_info[empty_slot_index].token     = reg_event_ptr->token;
               me_ptr->pull_push_mode_info.event_client_info[empty_slot_index].event_id  = reg_event_ptr->event_id;
               me_ptr->pull_push_mode_info.num_clients_registered++;

               PULL_PUSH_MSG(pm_info_ptr->miid,
                             DBG_HIGH_PRIO,
                             "capi_sh_mem_pull_push_mode: Registering event_id 0x%lx at index %lu total_clients %lu",
                             reg_event_ptr->event_id,
                             empty_slot_index,
                             me_ptr->pull_push_mode_info.num_clients_registered);

               // handle event specific payloads
               if (EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK == reg_event_ptr->event_id)
               {
                  event_cfg_sh_mem_pull_push_mode_watermark_t *event_config_payload =
                     (event_cfg_sh_mem_pull_push_mode_watermark_t *)(reg_event_ptr->event_cfg.data_ptr);
                  capi_result = pull_push_mode_watermark_levels_init(&(me_ptr->pull_push_mode_info),
                                                                     event_config_payload->num_water_mark_levels,
                                                                     (event_cfg_sh_mem_pull_push_mode_watermark_level_t
                                                                         *)(event_config_payload + 1),
                                                                     me_ptr->heap_mem.heap_id);
               }
               else
               {
                  // no special handling required for EOS event register
               }
            }
            else if (0 == reg_event_ptr->is_register)
            {
               // check if there are no clients registered.
               if (0 == me_ptr->pull_push_mode_info.num_clients_registered)
               {
                  capi_result |= CAPI_EFAILED;
                  PULL_PUSH_MSG(pm_info_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_sh_mem_pull_push_mode: zero clients registered for watermark event");
                  return capi_result;
               }

               // free clients resources
               if (already_registered_index >= 0)
               {
                  /* reset destination address and token, decrement num of clients registered*/
                  memset(&me_ptr->pull_push_mode_info.event_client_info[already_registered_index],
                         0,
                         sizeof(event_client_info_t));
                  me_ptr->pull_push_mode_info.num_clients_registered--;

                  PULL_PUSH_MSG(pm_info_ptr->miid,
                                DBG_HIGH_PRIO,
                                "capi_sh_mem_pull_push_mode: Deregistering event_id 0x%lx at index %lu total_clients "
                                "%lu ",
                                reg_event_ptr->event_id,
                                already_registered_index,
                                me_ptr->pull_push_mode_info.num_clients_registered);
               }
               else
               {
                  capi_result |= CAPI_EFAILED;
                  PULL_PUSH_MSG(pm_info_ptr->miid,
                                DBG_ERROR_PRIO,
                                "capi_sh_mem_pull_push_mode: client requested for de-registration not found for event "
                                "id "
                                "0x%lx",
                                reg_event_ptr->event_id);
               }
            }
            break;
         }
         default:
         {
            PULL_PUSH_MSG(pm_info_ptr->miid, DBG_HIGH_PRIO, "Unknown Prop[0x%lX]", prop_ptr[i].id);

            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } /* Outer switch - Generic CAPI Properties */

   } /* Loop all properties */
   return capi_result;
}

static capi_err_t capi_pm_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == props_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get property received null property array");
      return CAPI_EBADPARAM;
   }

   capi_pm_t *me_ptr = (capi_pm_t *)_pif;
   if (NULL == me_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get property received null property array");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_pm_process_get_properties(me_ptr, props_ptr);

   return capi_result;
}

static capi_err_t capi_pm_process_get_properties(capi_pm_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == proplist_ptr)
   {
      PULL_PUSH_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Get property received null property array");
      return CAPI_EBADPARAM;
   }
   uint32_t miid = (me_ptr) ? me_ptr->pull_push_mode_info.miid : MIID_UNKNOWN;

   uint32_t          fwk_extn_ids_arr[2] = { FWK_EXTN_PCM, FWK_EXTN_CONTAINER_FRAME_DURATION };
   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_pm_t));
   mod_prop.stack_size         = CAPI_PM_STACK_SIZE;
   mod_prop.num_fwk_extns      = PM_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids_arr;
   mod_prop.is_inplace         = FALSE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      PULL_PUSH_MSG(miid,
                    DBG_ERROR_PRIO,
                    "Get common basic properties failed with capi_result %lu",
                    capi_result);
      return capi_result;
   }

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_MAX_METADATA_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            break;
         }

         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr || ((prop_ptr[i].port_info.is_valid) && (0 != prop_ptr[i].port_info.port_index)))
            {
               PULL_PUSH_MSG(miid,
                             DBG_ERROR_PRIO,
                             "Get property id 0x%lx failed due to invalid/unexpected values",
                             (uint32_t)prop_ptr[i].id);
               CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
               break;
            }

            capi_media_fmt_v2_t media_fmt;
            media_fmt.header.format_header.data_format = CAPI_FIXED_POINT;
            media_fmt.format.bitstream_format          = me_ptr->pull_push_mode_info.media_fmt.fmt_id;
            media_fmt.format.bits_per_sample           = me_ptr->pull_push_mode_info.media_fmt.bits_per_sample;
            media_fmt.format.num_channels              = me_ptr->pull_push_mode_info.media_fmt.num_channels;
            media_fmt.format.q_factor                  = me_ptr->pull_push_mode_info.media_fmt.Q_format;
            media_fmt.format.data_is_signed            = me_ptr->pull_push_mode_info.media_fmt.is_signed;
            media_fmt.format.sampling_rate             = me_ptr->pull_push_mode_info.media_fmt.sample_rate;
            media_fmt.format.data_interleaving         = me_ptr->pull_push_mode_info.media_fmt.data_interleaving;

            for (int i = 0; i < media_fmt.format.num_channels; i++)
            {
               media_fmt.channel_type[i] = me_ptr->pull_push_mode_info.media_fmt.channel_map[i];
            }

            capi_result |= capi_cmn_handle_get_output_media_fmt_v2(&prop_ptr[i], &media_fmt);
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            uint32_t threshold = 1;

            capi_result |= capi_cmn_handle_get_port_threshold(&prop_ptr[i], threshold);
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            /** Can pass the list of IE list supported by this module to
             *  be updated in the common utlitlity */
            uint32_t supported_extension_list[] = { INTF_EXTN_IMCL, INTF_EXTN_MODULE_BUFFER_ACCESS };
            uint32_t num_supported_extns        = sizeof(supported_extension_list) / sizeof(uint32_t);
            capi_result                         = capi_cmn_check_and_update_intf_extn_status(num_supported_extns,
                                                                     supported_extension_list,
                                                                     &prop_ptr[i].payload);
            break;
         }
         default:
         {
            PULL_PUSH_MSG(miid,
                          DBG_HIGH_PRIO,
                          "Skipped Get Property for 0x%x. Not supported.",
                          prop_ptr[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            continue;
         }
      }
   }
   return capi_result;
}
