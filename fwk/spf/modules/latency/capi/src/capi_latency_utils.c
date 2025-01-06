/* =========================================================================
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 * @file capi_latency_utils.cpp
 *
 * C source file to implement the Audio Post Processor Interface for
 * latency Module
 */

#include "capi_latency_utils.h"

// static funtions declaration
static capi_err_t capi_latency_raise_kpps_event(capi_latency_t *me_ptr);
static capi_err_t capi_latency_raise_bandwidth_event(capi_latency_t *me_ptr);
static capi_err_t capi_latency_raise_process_event(capi_latency_t *me_ptr);

static bool_t latency_is_supported_v2_media_type(capi_latency_t *me_ptr, const capi_media_fmt_v2_t *format_ptr);

static bool_t capi_delay_is_enabled(capi_latency_t *me_ptr);

static bool_t capi_delay_media_type_v2_changed(const capi_media_fmt_v2_t *m1, const capi_media_fmt_v2_t *m2);

static void capi_delay_delayline_set(capi_delay_delayline_t *delayline_ptr,
                                     uint32_t                delayline_length_in_samples,
                                     uint32_t                bits_per_sample,
                                     void                   *buf_ptr);

static void capi_delay_delayline_reset(capi_delay_delayline_t *delayline_ptr);

static bool_t latency_is_supported_v2_media_type(capi_latency_t *me_ptr, const capi_media_fmt_v2_t *format_ptr)
{
   if ((CAPI_FIXED_POINT != format_ptr->header.format_header.data_format))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI latency: unsupported data format %lu",
             (uint32_t)format_ptr->header.format_header.data_format);
      return FALSE;
   }

   if ((format_ptr->format.bits_per_sample != 16) && (format_ptr->format.bits_per_sample != 32))
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI to DELAY: Invalid bits_per_sample %lu.", format_ptr->format.bits_per_sample);
      return FALSE;
   }

   if ((format_ptr->format.num_channels == 0) || (CAPI_MAX_CHANNELS_V2 < format_ptr->format.num_channels))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI to DELAY: Invalid num_channels %lu. Max channels: %lu",
             format_ptr->format.num_channels,
             CAPI_MAX_CHANNELS_V2);
      return FALSE;
   }

   if ((CAPI_DEINTERLEAVED_UNPACKED != format_ptr->format.data_interleaving) && (format_ptr->format.num_channels != 1))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI DELAY: Interleaved bitstream data is not supported. Received %u",
             format_ptr->format.data_interleaving);
      return FALSE;
   }

   if (0 == format_ptr->format.data_is_signed)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CAPI DELAY: unsigned data is not supported. Received %lu",
             format_ptr->format.data_is_signed);
      return FALSE;
   }

   for (uint32_t chan = 0; chan < format_ptr->format.num_channels; chan++)
   {
      if ((PCM_MAX_CHANNEL_MAP_V2 < format_ptr->channel_type[chan]) || (1 > format_ptr->channel_type[chan]))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Only upto %u channel maps supported. Received channel map %lu.",
                PCM_MAX_CHANNEL_MAP_V2,
                format_ptr->channel_type[chan]);
         return CAPI_EUNSUPPORTED;
      }
      if (format_ptr->channel_type[chan] > PCM_MAX_CHANNEL_MAP)
      { // If a higher channel map in the media format occurs even once in the execution history,
        // the flag will be set to true from that point onward.
         me_ptr->higher_channel_map_present = TRUE;
      }
   }
   return TRUE;
}

void capi_latency_init_config(capi_latency_t *me_ptr)
{
   memset(&me_ptr->lib_config, 0, sizeof(capi_latency_module_config_t));

   me_ptr->heap_mem.heap_id                             = POSAL_HEAP_DEFAULT;
   me_ptr->lib_config.enable                            = TRUE;
   me_ptr->lib_config.mem_ptr                           = NULL;
   me_ptr->lib_config.mchan_config                      = NULL;
   me_ptr->cache_delay_v2.cache_delay_per_config_v2_ptr = NULL;
   me_ptr->cfg_mode                                     = LATENCY_MODE_GLOBAL;
   return;
}

capi_err_t capi_latency_check_channel_map_delay_cfg(delay_param_per_ch_cfg_t *delay_cfg_ptr, uint32_t num_config)
{
   uint64_t channel_mask = 0;

   for (uint32_t count = 0; count < num_config; count++)
   {
      uint64_t channel_map =
         ((uint64_t)delay_cfg_ptr[count].channel_mask_msb << 32) | (uint64_t)delay_cfg_ptr[count].channel_mask_lsb;

      //Channel mask Zero is invalid,it should be given properly as per channel mapping on which channel the delay to be set
      if (channel_map == 0)
      {
    	  AR_MSG(DBG_HIGH_PRIO," CAPI latency : Received invalid Channel mask of zero on all channels for config %u",count);
    	  return CAPI_EBADPARAM;
      }

      if (channel_mask & channel_map)
      {
         return CAPI_EBADPARAM;
      }
      channel_mask |= channel_map;
   }
   return CAPI_EOK;
}

void capi_delay_set_delay(capi_latency_t *me_ptr)
{
   for (uint32_t i = 0; i < me_ptr->media_fmt.format.num_channels; i++)
   {
      for (uint32_t count = 0; count < me_ptr->cache_delay.num_config; count++)
      {
         uint64_t channel_map = ((uint64_t)me_ptr->cache_delay.cache_delay_per_config[count].channel_mask_msb << 32) |
                                (uint64_t)me_ptr->cache_delay.cache_delay_per_config[count].channel_mask_lsb;
         if (((uint64_t)1 << me_ptr->media_fmt.channel_type[i]) & channel_map)
         {
            me_ptr->lib_config.mchan_config[i].delay_in_us =
               me_ptr->cache_delay.cache_delay_per_config[count].delay_in_us;
         }
      }
   }

   capi_delay_calc_delay_in_samples(me_ptr);

   return;
}

uint32_t capi_latency_get_max_delay(capi_latency_t *me_ptr)
{
   uint32_t delay_in_us = me_ptr->lib_config.mchan_config[0].delay_in_us;

   for (uint32_t count = 1; count < me_ptr->media_fmt.format.num_channels; count++)
   {
      if (delay_in_us < me_ptr->lib_config.mchan_config[count].delay_in_us)
      {
         delay_in_us = me_ptr->lib_config.mchan_config[count].delay_in_us;
      }
   }
   return delay_in_us;
}

/*===========================================================================
    FUNCTION : capi_latency_init_events
    DESCRIPTION: Function to init all the events with invalid values.
    DEPENDENCY: only call from init function.
===========================================================================*/
void capi_latency_init_events(capi_latency_t *const me_ptr)
{
   me_ptr->events_config.enable = TRUE;

   // Setting events to maximum(invalid) value.
   me_ptr->events_config.kpps        = 0x7FFFFFFF;
   me_ptr->events_config.data_bw     = 0x7FFFFFFF;
   me_ptr->events_config.delay_in_us = 0x7FFFFFFF;

   return;
}

/* =========================================================================
 * FUNCTION : capi_latency_raise_delay_event
 * DESCRIPTION: Function to send the delay using the callback function
 * =========================================================================*/
capi_err_t capi_latency_raise_delay_event(capi_latency_t *me_ptr)
{
   capi_err_t            capi_result   = CAPI_EOK;
   int32_t               delay_in_us   = 0;
   static const uint32_t NUM_US_IN_SEC = 1000000;

   if (me_ptr->is_media_fmt_received == TRUE)
   {
      delay_in_us = capi_latency_get_max_delay(me_ptr);

      delay_in_us -= ((me_ptr->negative_delay_samples * NUM_US_IN_SEC) / me_ptr->media_fmt.format.sampling_rate);

      delay_in_us = MAX(delay_in_us, 0);
   }
   if (delay_in_us != me_ptr->events_config.delay_in_us)
   {
      capi_result = capi_cmn_update_algo_delay_event(&me_ptr->cb_info, delay_in_us);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI latency: Failed to send algorithmic delay update event with %lu", capi_result);
      }
      else
      {
         me_ptr->events_config.delay_in_us = delay_in_us;
      }
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_latency_raise_kpps_event
 * DESCRIPTION: Function to send the KPPS using the callback function
 * =========================================================================*/
static capi_err_t capi_latency_raise_kpps_event(capi_latency_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   kpps_new;
   if (me_ptr->media_fmt.format.num_channels <= 8)
   {
      kpps_new = LATENCY_KPPS;
   }
   else if (me_ptr->media_fmt.format.num_channels <= 16)
   {
      kpps_new = LATENCY_KPPS_16;
   }
   else
   {
      kpps_new = LATENCY_KPPS_32;
   }

   if (kpps_new != me_ptr->events_config.kpps)
   {

      capi_result = capi_cmn_update_kpps_event(&me_ptr->cb_info, kpps_new);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI latency: Failed to send KPPS update event with %lu", capi_result);
      }
      else
      {
         me_ptr->events_config.kpps = kpps_new;
      }
   }
   return capi_result;
}

static capi_err_t capi_latency_raise_bandwidth_event(capi_latency_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t bw_new;

   if (me_ptr->media_fmt.format.num_channels <= 8)
   {
      bw_new = LATENCY_BW;
   }
   else if (me_ptr->media_fmt.format.num_channels <= 16)
   {
      bw_new = LATENCY_BW_16;
   }
   else
   {
      bw_new = LATENCY_BW_32;
   }

   if (bw_new != me_ptr->events_config.data_bw)
   {
      capi_result = capi_cmn_update_bandwidth_event(&me_ptr->cb_info, 0, bw_new);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPI latency: Failed to send bandwidth update event with %lu", capi_result);
      }
      else
      {
         me_ptr->events_config.data_bw = bw_new;
      }
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_latency_raise_process_event
 * DESCRIPTION: Function to send the output media format using the
 *              callback function
 * =========================================================================*/
capi_err_t capi_latency_raise_process_event(capi_latency_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI latency:  Event callback is not set. Unable to raise events!");
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   bool_t enable = TRUE;
   if (me_ptr->is_media_fmt_received == TRUE)
   {
      enable = me_ptr->lib_config.enable ? (capi_delay_is_enabled(me_ptr) ? TRUE : FALSE) : FALSE;
   }
   else
   {
      enable = TRUE;
   }
   if (me_ptr->events_config.enable != (uint32_t)enable)
   {
      me_ptr->events_config.enable = (uint32_t)enable;
      capi_result = capi_cmn_update_process_check_event(&me_ptr->cb_info, me_ptr->events_config.enable);
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_latency_raise_event
 * DESCRIPTION: Function to raise various events of the latency module
 * =========================================================================*/
capi_err_t capi_latency_raise_event(capi_latency_t *me_ptr, bool_t media_fmt_update)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI latency:  Event callback is not set. Unable to raise events!");
      CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
      return capi_result;
   }

   if (media_fmt_update)
   {
      // raise events which are only media format dependent
      capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->media_fmt, FALSE, 0);
      capi_result |= capi_latency_raise_bandwidth_event(me_ptr);
   }

   capi_result |= capi_latency_raise_process_event(me_ptr);
   capi_result |= capi_latency_raise_kpps_event(me_ptr);
   capi_result |= capi_latency_raise_delay_event(me_ptr);

   return capi_result;
}

void capi_latency_algo_reset(capi_latency_t *me_ptr)
{
   if (me_ptr->is_media_fmt_received == TRUE && FIRST_FRAME != me_ptr->state)
   {
      for (uint32_t i = 0; i < me_ptr->media_fmt.format.num_channels; i++)
      {
         capi_delay_delayline_reset(&me_ptr->lib_config.mchan_config[i].delay_line);
      }
      me_ptr->state = FIRST_FRAME;
      if (me_ptr->negative_delay_samples > 0)
      {
         me_ptr->negative_delay_samples = 0;
         capi_latency_raise_delay_event(me_ptr);
      }
   }
}

capi_err_t capi_latency_process_set_properties(capi_latency_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t   capi_result = CAPI_EOK;
   capi_prop_t *prop_array  = proplist_ptr->prop_ptr;
   uint32_t     i           = 0;

   if (NULL == prop_array || NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Latency:  Set property, received null property array or me_ptr.");
      return CAPI_EBADPARAM;
   }

   capi_result = capi_cmn_set_basic_properties(proplist_ptr, &me_ptr->heap_mem, &me_ptr->cb_info, TRUE);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI Latency: Set basic properties failed with result %lu", capi_result);
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
            capi_latency_algo_reset(me_ptr);
            CAPI_SET_ERROR(capi_result, CAPI_EOK);
            break;
         }

         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            uint32_t required_size = CAPI_LATENCY_MF_V2_MIN_SIZE;
            if (payload_ptr->actual_data_len >= required_size)
            {

               AR_MSG(DBG_HIGH_PRIO, "CAPI latency: Received Input media format");

               capi_media_fmt_v2_t *data_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

               if (!latency_is_supported_v2_media_type(me_ptr, data_ptr))
               {
                  CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
                  break;
               }
               required_size += data_ptr->format.num_channels * sizeof(capi_channel_type_t);

               if (payload_ptr->actual_data_len < required_size)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "CAPI latency: Set property id 0x%lx Bad param size %lu",
                         (uint32_t)prop_array[i].id,
                         payload_ptr->actual_data_len);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
                  break;
               }

               if (capi_delay_media_type_v2_changed(&me_ptr->media_fmt, data_ptr))
               {
                  me_ptr->is_media_fmt_received = TRUE;

                  capi_delay_destroy_buffer(me_ptr);

                  me_ptr->media_fmt.format = data_ptr->format;
                  memscpy(me_ptr->media_fmt.channel_type,
                          sizeof(me_ptr->media_fmt.channel_type),
                          data_ptr->channel_type,
                          data_ptr->format.num_channels * sizeof(data_ptr->channel_type[0]));
                  me_ptr->media_fmt.format.minor_version = CAPI_MEDIA_FORMAT_MINOR_VERSION;

                  me_ptr->lib_config.mchan_config =
                     (capi_latency_per_chan_t *)posal_memory_malloc(me_ptr->media_fmt.format.num_channels *
                                                                       sizeof(capi_latency_per_chan_t),
                                                                    (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
                  if (NULL == me_ptr->lib_config.mchan_config)
                  {
                     AR_MSG(DBG_FATAL_PRIO,
                            "CAPIv2 Delay: No memory to store Multi channel config.Requires %lu bytes",
                            me_ptr->media_fmt.format.num_channels * sizeof(capi_latency_per_chan_t));
                     return CAPI_ENOMEMORY;
                  }
                  memset(me_ptr->lib_config.mchan_config,
                         0,
                         me_ptr->media_fmt.format.num_channels * sizeof(capi_latency_per_chan_t));

                  if ((VERSION_V1 == me_ptr->cfg_version) && (FALSE == me_ptr->higher_channel_map_present))
                  {
                     if (me_ptr->cache_delay.cache_delay_per_config != NULL)
                     {
                        capi_delay_set_delay(me_ptr);
                     }
                  }
                  else if (me_ptr->cache_delay_v2.cache_delay_per_config_v2_ptr != NULL)
                  {
                     capi_delay_set_delay_v2(me_ptr);
                  }

                  capi_result |= capi_delay_create_buffer(me_ptr, NULL);
                  if (CAPI_FAILED(capi_result))
                  {
                     for (uint32_t count = 0; count < me_ptr->media_fmt.format.num_channels; count++)
                     {
                        me_ptr->lib_config.mchan_config[count].delay_in_us = 0;
                     }
                     AR_MSG(DBG_ERROR_PRIO, "CAPIv2 delay failed to allocate delay line, setting delay to 0");
                  }
               }

               // raise event for output media format
               capi_result |= capi_latency_raise_event(me_ptr, TRUE);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI latency: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }

         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI latency: Set property id %#x. Not supported.", prop_array[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPI latency: Set property for %#x failed with opcode %lu",
                prop_array[i].id,
                capi_result);
      }
   }

   return capi_result;
}

capi_err_t capi_latency_process_get_properties(capi_latency_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = align_to_8_byte(sizeof(capi_latency_t));
   mod_prop.stack_size         = LATENCY_STACK_SIZE;
   mod_prop.num_fwk_extns      = 0;
   mod_prop.fwk_extn_ids_arr   = NULL;
   mod_prop.is_inplace         = FALSE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_latency: Get common basic properties failed with capi_result %lu", capi_result);
      return capi_result;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &(prop_array[i].payload);

      switch (prop_array[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_STACK_SIZE:
         case CAPI_MAX_METADATA_SIZE:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_interface_extns_list_t))
            {
               capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
               if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                                (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_latency: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
                         payload_ptr->max_data_len);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
               }
               else
               {
                  capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
                     (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

                  for (uint32_t i = 0; i < intf_ext_list->num_extensions; i++)
                  {
                     switch (curr_intf_extn_desc_ptr->id)
                     {
                        case INTF_EXTN_STM_TS:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        default:
                        {
                           curr_intf_extn_desc_ptr->is_supported = FALSE;
                           break;
                        }
                     }
                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI latency: Get property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_output_media_format_size_t))
            {
               capi_output_media_format_size_t *data_ptr = (capi_output_media_format_size_t *)payload_ptr->data_ptr;
               data_ptr->size_in_bytes                   = sizeof(capi_standard_data_format_t);
               payload_ptr->actual_data_len              = sizeof(capi_output_media_format_size_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI latency: Get property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }

         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            uint32_t required_size = CAPI_LATENCY_MF_V2_MIN_SIZE;
            if (payload_ptr->max_data_len >= CAPI_LATENCY_MF_V2_MIN_SIZE)
            {
               capi_media_fmt_v2_t *data_ptr = (capi_media_fmt_v2_t *)payload_ptr->data_ptr;

               if (NULL == me_ptr || (prop_array[i].port_info.is_valid && prop_array[i].port_info.port_index != 0))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "CAPI latency: Get property id 0x%lx failed due to invalid/unexpected values",
                         (uint32_t)prop_array[i].id);
                  CAPI_SET_ERROR(capi_result, CAPI_EFAILED);
                  break;
               }

               uint32_t channel_map_size       = sizeof(capi_channel_type_t) * data_ptr->format.num_channels;
               uint32_t channel_map_size_align = CAPI_LATENCY_ALIGN_4_BYTE(channel_map_size);
               required_size += channel_map_size_align;

               if (payload_ptr->max_data_len < required_size)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "CAPI latency: Get property id 0x%lx Bad param size %lu",
                         (uint32_t)prop_array[i].id,
                         payload_ptr->max_data_len);
                  CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
                  break;
               }

               data_ptr->header = me_ptr->media_fmt.header;
               data_ptr->format = me_ptr->media_fmt.format;
               memscpy(data_ptr->channel_type,
                       channel_map_size,
                       me_ptr->media_fmt.channel_type,
                       me_ptr->media_fmt.format.num_channels * sizeof(me_ptr->media_fmt.channel_type[0]));

               payload_ptr->actual_data_len = required_size;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "CAPI latency: Get property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }

         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "CAPI latency: Get property for ID %#x. Not supported.", prop_array[i].id);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CAPI latency: Get property for %#x failed with opcode %lu",
                prop_array[i].id,
                capi_result);
      }
   }

   return capi_result;
}

void capi_delay_delayline_set(capi_delay_delayline_t *delayline_ptr,
                              uint32_t                delayline_length_in_samples,
                              uint32_t                bits_per_sample,
                              void                   *buf_ptr)
{
   switch (bits_per_sample)
   {
      case BITS_PER_SAMPLE_16:
      {
         delayline_ptr->is16bit        = TRUE;
         delayline_ptr->dl16.delay_buf = (int16 *)(buf_ptr);
         (void)latency_delayline_set(&delayline_ptr->dl16, delayline_length_in_samples);
         break;
      }
      case BITS_PER_SAMPLE_32:
      {
         delayline_ptr->is16bit  = FALSE;
         delayline_ptr->dl32.buf = (int32 *)(buf_ptr);
         (void)latency_delayline32_set(&delayline_ptr->dl32, delayline_length_in_samples);
         break;
      }
      default:
         POSAL_ASSERT(0);
         break;
   }
}

void capi_delay_delayline_reset(capi_delay_delayline_t *delayline_ptr)
{
   if (delayline_ptr->is16bit)
   {
      if (delayline_ptr->dl16.delay_buf != NULL)
      {
         latency_delayline_reset(&delayline_ptr->dl16);
      }
   }
   else
   {
      if (delayline_ptr->dl32.buf != NULL)
      {
         latency_delayline32_reset(&delayline_ptr->dl32);
      }
   }
}

bool_t capi_delay_is_enabled(capi_latency_t *me_ptr)
{
   uint32_t delay_in_us = capi_latency_get_max_delay(me_ptr);

   return (delay_in_us != 0);
}

bool_t capi_delay_media_type_v2_changed(const capi_media_fmt_v2_t *m1, const capi_media_fmt_v2_t *m2)
{
   if (m1->format.bits_per_sample != m2->format.bits_per_sample)
   {
      return TRUE;
   }

   POSAL_ASSERT(m1->format.data_interleaving == m2->format.data_interleaving);
   POSAL_ASSERT(m1->format.data_is_signed == m2->format.data_is_signed);

   if (m1->format.num_channels != m2->format.num_channels)
   {
      return TRUE;
   }
   if (m1->format.sampling_rate != m2->format.sampling_rate)
   {
      return TRUE;
   }

   for (uint32_t i = 0; i < m1->format.num_channels; i++)
   {
      if (m1->channel_type[i] != m2->channel_type[i])
      {
         return TRUE;
      }
   }

   return FALSE;
}

void capi_delay_destroy_buffer(capi_latency_t *me_ptr)
{
   if (me_ptr->lib_config.mchan_config != NULL)
   {
      posal_memory_free(me_ptr->lib_config.mchan_config);
      me_ptr->lib_config.mchan_config = NULL;
   }

   if (NULL != me_ptr->lib_config.mem_ptr)
   {
      posal_memory_free(me_ptr->lib_config.mem_ptr);
   }

   me_ptr->lib_config.mem_ptr = NULL;
}

void capi_delay_calc_delay_in_samples(capi_latency_t *me_ptr)
{
   const uint32_t NUM_US_IN_S = 1000000;
   uint64_t       temp_us     = 0;
   for (uint32_t count = 0; count < me_ptr->media_fmt.format.num_channels; count++)
   {
      if (me_ptr->media_fmt.format.sampling_rate != CAPI_DATA_FORMAT_INVALID_VAL)
      {
         temp_us =
            (uint64_t)me_ptr->lib_config.mchan_config[count].delay_in_us * me_ptr->media_fmt.format.sampling_rate;
         me_ptr->lib_config.mchan_config[count].delay_in_samples = temp_us / NUM_US_IN_S;
      }
      else
      {
         me_ptr->lib_config.mchan_config[count].delay_in_samples = 0;
      }
   }
}

capi_err_t capi_delay_create_buffer(capi_latency_t *me_ptr, uint32_t *old_delay_in_us)
{
   const capi_media_fmt_v2_t *m                    = &me_ptr->media_fmt;
   uint32_t                  *buf_size_per_channel = NULL;
   buf_size_per_channel = (uint32_t *)posal_memory_malloc(m->format.num_channels * sizeof(uint32_t),
                                                          (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
   if (NULL == buf_size_per_channel)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPIv2 Delay failed to allocate memory for buffer size");
      return CAPI_ENOMEMORY;
   }

   uint32_t buf_size = 0;

   for (uint32_t i = 0; i < m->format.num_channels; i++)
   {
      buf_size_per_channel[i] = me_ptr->lib_config.mchan_config[i].delay_in_samples * (m->format.bits_per_sample / 8);
      buf_size += buf_size_per_channel[i];
   }

   if (0 == buf_size)
   {
      me_ptr->lib_config.mem_ptr = NULL;

      for (uint32_t i = 0; i < m->format.num_channels; i++)
      {
         capi_delay_delayline_set(&me_ptr->lib_config.mchan_config[i].delay_line, 0, m->format.bits_per_sample, NULL);
      }
      if (NULL != buf_size_per_channel)
      {
         posal_memory_free(buf_size_per_channel);
         buf_size_per_channel = NULL;
      }
      return CAPI_EOK;
   }
   else
   {
      me_ptr->lib_config.mem_ptr = posal_memory_malloc(buf_size, (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
      if (NULL == me_ptr->lib_config.mem_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "CAPIv2 delay failed to allocate delay line, setting delay to 0");

         for (uint32_t i = 0; i < m->format.num_channels; i++)
         {
            me_ptr->lib_config.mchan_config[i].delay_in_samples = me_ptr->lib_config.mchan_config[i].delay_in_us = 0;
            capi_delay_delayline_set(&me_ptr->lib_config.mchan_config[i].delay_line,
                                     0,
                                     m->format.bits_per_sample,
                                     NULL);
         }
         if (NULL != buf_size_per_channel)
         {
            posal_memory_free(buf_size_per_channel);
            buf_size_per_channel = NULL;
         }
         return CAPI_ENOMEMORY;
      }
      else
      {
         uint8_t *mem_ptr = (uint8_t *)me_ptr->lib_config.mem_ptr;

         //Updating delay line on all the channels
         for(uint32_t i = 0; i < m->format.num_channels; i++)
         {
            uint8_t *ch_delay_ptr = (0 != me_ptr->lib_config.mchan_config[i].delay_in_us)? mem_ptr : NULL;
            capi_delay_delayline_set(&me_ptr->lib_config.mchan_config[i].delay_line, me_ptr->lib_config.mchan_config[i].delay_in_samples,
                                        m->format.bits_per_sample, ch_delay_ptr);
            mem_ptr += buf_size_per_channel[i];
         }
         if (NULL != buf_size_per_channel)
         {
            posal_memory_free(buf_size_per_channel);
            buf_size_per_channel = NULL;
         }
         return CAPI_EOK;
      }
   }
}

void capi_delay_delayline_read(void                   *dest,
                               void                   *src,
                               capi_delay_delayline_t *delayline_ptr,
                               uint32_t                delay,
                               uint32_t                samples)
{
   if (delayline_ptr->is16bit)
   {
      latency_buffer_delay_fill((int16 *)dest, (int16 *)src, &delayline_ptr->dl16, delay, samples);
   }
   else
   {
      latency_delayline32_read((int32 *)dest, (int32 *)src, &delayline_ptr->dl32, delay, samples);
   }
}

void capi_delay_delayline_update(capi_delay_delayline_t *delayline_ptr, void *src, uint32_t samples)
{
   if (delayline_ptr->is16bit)
   {
      latency_delayline_update(&delayline_ptr->dl16, (int16 *)src, samples);
   }
   else
   {
      latency_delayline32_update(&delayline_ptr->dl32, (int32 *)src, samples);
   }
}

void capi_delay_delayline_copy(capi_delay_delayline_t *delayline_dest_ptr, capi_delay_delayline_t *delayline_src_ptr)
{
   delayline_dest_ptr->is16bit = delayline_src_ptr->is16bit;
   if (delayline_src_ptr->is16bit)
   {
      latency_delayline_copy(&delayline_dest_ptr->dl16, &delayline_src_ptr->dl16);
   }
   else
   {
      latency_delayline32_copy(&delayline_dest_ptr->dl32, &delayline_src_ptr->dl32);
   }
}
