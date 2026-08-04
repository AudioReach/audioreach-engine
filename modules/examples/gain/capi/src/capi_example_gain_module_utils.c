/**
 * \file capi_example_gain_module_utils.c
 *  
 * \brief
 *  
 *     Example Gain Module
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "capi_example_gain_module_structs.h"

/*------------------------------------------------------------------------
 * Functions
 * -----------------------------------------------------------------------*/

/* =========================================================================
 * FUNCTION : capi_example_gain_module_module_init_media_fmt_v2
 * DESCRIPTION: Function to initialize the media format of the gain module
 * =========================================================================*/
capi_err_t capi_example_gain_module_module_init_media_fmt_v2(capi_example_gain_module_media_fmt_t *media_fmt_ptr)
{
   media_fmt_ptr->header.format_header.data_format = CAPI_FIXED_POINT;
   media_fmt_ptr->format.minor_version             = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.bits_per_sample           = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.bitstream_format          = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.data_interleaving         = CAPI_INVALID_INTERLEAVING;
   media_fmt_ptr->format.data_is_signed            = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.num_channels              = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.q_factor                  = CAPI_DATA_FORMAT_INVALID_VAL;
   media_fmt_ptr->format.sampling_rate             = CAPI_DATA_FORMAT_INVALID_VAL;

   for (uint32_t j = 0; j < CAPI_MAX_CHANNELS_V2; j++)
   {
      media_fmt_ptr->channel_type[j] = (uint16_t)CAPI_DATA_FORMAT_INVALID_VAL;
   }
   return CAPI_EOK;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_raise_process_event
 * DESCRIPTION: Function to raise the process check event using the
 * callback function to inform the framework if module is enabled or disabled
 * =========================================================================*/
capi_err_t capi_example_gain_module_raise_process_event(capi_example_gain_module_t *me_ptr, uint32_t enable)
{
   capi_err_t capi_result = CAPI_EOK;

   /* Raise event only if enable flag changed*/
   if (me_ptr->events_info.enable != enable)
   {
      if (NULL == me_ptr->cb_info.event_cb)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Event callback is not set, Unable to raise process check event!");
         return CAPI_EBADPARAM;
      }

      /* Save the new enable flag in the capi struct*/
      me_ptr->events_info.enable = enable;

      /* Event struct for process check/enable */
      capi_event_process_state_t event;
      event.is_enabled = enable;

      /* Event parent struct used for all events */
      capi_event_info_t event_info;

      /* This is set to false if the event being raised is not port specific */
      event_info.port_info.is_valid      = FALSE;
      event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(event);
      event_info.payload.data_ptr                                          = (int8_t *)&event;

      capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_PROCESS_STATE, &event_info);
      if (CAPI_FAILED(capi_result))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "capi_example_gain_module : Failed to send process check event with result %d",
                (int)capi_result);
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO, "capi_example_gain_module : Raising process check event with enable set to %lu", enable);
      }
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_raise_process_event
 * DESCRIPTION: Function to raise the process check event using the
 * callback function to inform the framework if module is enabled or disabled
 * =========================================================================*/
static capi_err_t capi_example_gain_module_raise_kpps_event(capi_example_gain_module_t *me_ptr, uint32_t kpps)
{
   capi_err_t result = CAPI_EOK;

   /* Raise event only if the code or data bandwidth changed*/
   if (me_ptr->events_info.kpps != kpps)
   {
      /* Save the new values in the capi struct*/
      me_ptr->events_info.kpps = kpps;

      /* Event struct for kpps */
      capi_event_KPPS_t event;
      event.KPPS = kpps;

      /* Event info parent struct used for all events */
      capi_event_info_t event_info;

      /* This is set to false if the event being raised is not port specific */
      event_info.port_info.is_valid = FALSE;

      /* Populate the actual data length of the event payload*/
      event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(capi_event_KPPS_t);
      event_info.payload.data_ptr                                          = (int8_t *)&event;

      /* Raise callback event with opcode CAPI_EVENT_KPPS*/
      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_KPPS, &event_info);
      if (CAPI_FAILED(result))
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Failed to send KPPS raise event with %lu", result);
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_raise_process_event
 * DESCRIPTION: Function to raise the process check event using the
 * callback function to inform the framework if module is enabled or disabled
 * =========================================================================*/
static capi_err_t capi_example_gain_module_raise_bandwidth_event(capi_example_gain_module_t *me_ptr,
                                                         uint32_t            code_bandwidth,
                                                         uint32_t            data_bandwidth)
{
   capi_err_t result = CAPI_EOK;

   /* Raise event only if the code or data bandwidth changed*/
   if ((me_ptr->events_info.code_bw != code_bandwidth) || (me_ptr->events_info.data_bw != data_bandwidth))
   {
      /* Save the new values in the capi struct*/
      me_ptr->events_info.code_bw = code_bandwidth;
      me_ptr->events_info.data_bw = data_bandwidth;

      /* Event struct for bandwidth */
      capi_event_bandwidth_t event;
      event.code_bandwidth = code_bandwidth;
      event.data_bandwidth = data_bandwidth;

      /* Event info parent struct used for all events */
      capi_event_info_t event_info;
      /* This is set to false if the event being raised is not port specific */
      event_info.port_info.is_valid = FALSE;
      /* Populate the actual data length of the event payload*/
      event_info.payload.actual_data_len = sizeof(event);
      event_info.payload.max_data_len    = sizeof(event);
      event_info.payload.data_ptr        = (int8_t *)&event;

      /* Raise callback event with opcode CAPI_EVENT_BANDWIDTH*/
      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_BANDWIDTH, &event_info);
      if (CAPI_FAILED(result))
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Failed to send bandwidth raise event with %lu", result);
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_raise_process_event
 * DESCRIPTION: Function to raise the process check event using the
 * callback function to inform the framework if module is enabled or disabled
 * =========================================================================*/
static capi_err_t capi_example_gain_module_raise_algo_delay_event(capi_example_gain_module_t *me_ptr, uint32_t delay_in_us)
{
   capi_err_t result = CAPI_EOK;

   /* Raise event only if the delay value changed*/
   if (me_ptr->events_info.delay_in_us != delay_in_us)
   {
      /* Save the new value in the capi struct*/
      me_ptr->events_info.delay_in_us = delay_in_us;

      /* Event struct for bandwidth */
      capi_event_algorithmic_delay_t event;
      event.delay_in_us = delay_in_us;

      /* Event info parent struct used for all events */
      capi_event_info_t event_info;
      /* This is set to false if the event being raised is not port specific */
      event_info.port_info.is_valid = FALSE;
      /* Populate the actual data length of the event payload*/
      event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(event);
      event_info.payload.data_ptr                                          = (int8_t *)&event;

      /* Raise callback event with opcode CAPI_EVENT_ALGORITHMIC_DELAY*/
      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_ALGORITHMIC_DELAY, &event_info);

      if (CAPI_FAILED(result))
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Failed to send delay raise event with %lu", result);
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_raise_event
 * DESCRIPTION: Function to raise kpps, bandwidth and algorithmic delay events
 * for the gain module
 * =========================================================================*/
capi_err_t capi_example_gain_module_raise_events(capi_example_gain_module_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == me_ptr->cb_info.event_cb)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module: Event callback is not set. Unable to raise events!");
      return CAPI_EFAILED;
   }

   /* scale KPPS based on num channels and sampling rate */
   uint32_t kpps = CAPI_EXAMPLE_GAIN_MODULE_KPPS_8KHZ_MONO_CH * me_ptr->operating_media_fmt.format.num_channels *
                   (me_ptr->operating_media_fmt.format.sampling_rate / 8000);

   /* Raise kpps, bw and algo delay events by passing in necessary values*/
   capi_result |= capi_example_gain_module_raise_kpps_event(me_ptr, kpps);

   capi_result |= capi_example_gain_module_raise_bandwidth_event(me_ptr, 0 /*code bw*/, 0 /*data bw*/);

   capi_result |= capi_example_gain_module_raise_algo_delay_event(me_ptr, 0 /*delay in us*/);

   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_output_media_fmt_event_v2
 * DESCRIPTION: Function to raiseoutput media format event
 * =========================================================================*/
capi_err_t capi_example_gain_module_output_media_fmt_event_v2(capi_event_callback_info_t *  cb_info_ptr,
                                                              capi_example_gain_module_media_fmt_t *out_media_fmt)
{
   capi_err_t result = CAPI_EOK;
   if ((NULL == cb_info_ptr->event_cb) || (NULL == out_media_fmt))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_example_gain_module : Event callback is not set or media fmt is NULL, Unable to raise output "
             "media fmt v2 event!");
      return CAPI_EBADPARAM;
   }

   capi_event_info_t event_info;
   event_info.port_info.port_index    = 0;
   event_info.port_info.is_valid      = TRUE;
   event_info.port_info.is_input_port = FALSE;
   event_info.payload.actual_data_len = (sizeof(capi_standard_data_format_v2_t) + sizeof(capi_set_get_media_format_t) +
                                         (sizeof(out_media_fmt->channel_type[0]) * out_media_fmt->format.num_channels));
   event_info.payload.max_data_len = sizeof(capi_example_gain_module_media_fmt_t);
   event_info.payload.data_ptr     = (int8_t *)out_media_fmt;

   result = cb_info_ptr->event_cb(cb_info_ptr->event_context, CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2, &event_info);

   if (CAPI_FAILED(result))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_example_gain_module: Failed to send output media format updated event V2 with result %d",
             (int)result);
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO,
             "capi_example_gain_module: Raised output media fmt event with Sampling rate = %lu, num channels = %lu, bits per "
             "sample =%lu",
             out_media_fmt->format.sampling_rate,
             out_media_fmt->format.num_channels,
             out_media_fmt->format.bits_per_sample);
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_process_set_properties
 * DESCRIPTION:  Implementation of set properties
 * =========================================================================*/
capi_err_t capi_example_gain_module_process_set_properties(capi_example_gain_module_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK, capi_result2 = CAPI_EOK;

   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Set property received null ptr");
      return CAPI_EBADPARAM;
   }

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;
   uint32_t     i          = 0;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &(prop_array[i].payload);

      switch (prop_array[i].id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_event_callback_info_t))
            {
               capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;
               me_ptr->cb_info.event_cb             = data_ptr->event_cb;
               me_ptr->cb_info.event_context        = data_ptr->event_context;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_HEAP_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_heap_id_t))
            {
               capi_heap_id_t *data_ptr  = (capi_heap_id_t *)payload_ptr->data_ptr;
               me_ptr->heap_info.heap_id = data_ptr->heap_id;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_port_num_info_t))
            {
               capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;
               if ((CAPI_EXAMPLE_GAIN_MODULE_MAX_IN_PORTS < data_ptr->num_input_ports) ||
                   (CAPI_EXAMPLE_GAIN_MODULE_MAX_OUT_PORTS < data_ptr->num_output_ports))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: Set property id 0x%lx,number of input/output ports cannot be more than "
                         "%d",
                         (uint32_t)prop_array[i].id,
                         CAPI_EXAMPLE_GAIN_MODULE_MAX_OUT_PORTS);
                  capi_result |= CAPI_ENEEDMORE;
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_example_gain_module_media_fmt_t))
            {
               AR_MSG(DBG_HIGH_PRIO, "capi_example_gain_module: Received Input media format");

               capi_example_gain_module_media_fmt_t *media_fmt_ptr = (capi_example_gain_module_media_fmt_t *)(payload_ptr->data_ptr);

               /* Number of channels should be between 0 and 32*/
               if ((0 >= media_fmt_ptr->format.num_channels) ||
                   (CAPI_MAX_CHANNELS_V2 < media_fmt_ptr->format.num_channels))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: Unsupported number of channels %lu",
                         media_fmt_ptr->format.num_channels);
                  return CAPI_EBADPARAM;
               }

               /* Calculate size of channel map after validating number of channels*/
               uint32_t channel_map_size =
                  (media_fmt_ptr->format.num_channels * sizeof(media_fmt_ptr->channel_type[0]));

               uint32_t required_size =
                  sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) + channel_map_size;

               /* Validate the size of payload again including channel map size */
               if (payload_ptr->actual_data_len < required_size)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: Not valid media format size %lu, required size %lu",
                         payload_ptr->actual_data_len,
                         required_size);
                  return CAPI_ENEEDMORE;
               }

               /* Validate data format*/
               if (CAPI_FIXED_POINT != media_fmt_ptr->header.format_header.data_format)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: unsupported data format %lu",
                         (uint32_t)media_fmt_ptr->header.format_header.data_format);
                  return CAPI_EBADPARAM;
               }

               /* Validate bits per sample*/
               if ((16 != media_fmt_ptr->format.bits_per_sample) && (32 != media_fmt_ptr->format.bits_per_sample))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: only supports 16 and 32 bit data. Received %lu.",
                         media_fmt_ptr->format.bits_per_sample);
                  return CAPI_EBADPARAM;
               }

               /* Validate interleaving*/
               if (media_fmt_ptr->format.num_channels != 1 && media_fmt_ptr->format.data_interleaving != CAPI_DEINTERLEAVED_UNPACKED)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Interleaved data not supported.");
                  return CAPI_EBADPARAM;
               }

               /* Validate data signed/unsigned*/
               if (!media_fmt_ptr->format.data_is_signed)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module: Unsigned data not supported.");
                  return CAPI_EBADPARAM;
               }

               /* Validate sample rate*/
               if ((0 >= media_fmt_ptr->format.sampling_rate) || (384000 < media_fmt_ptr->format.sampling_rate))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: Unsupported sampling rate %lu",
                         media_fmt_ptr->format.sampling_rate);
                  return CAPI_EBADPARAM;
               }

               /* Copy and save the input media format after passing all validation checks */
               memscpy(&me_ptr->operating_media_fmt, required_size, media_fmt_ptr, payload_ptr->actual_data_len);

               /* raise event for output media format */
               capi_result |= capi_example_gain_module_raise_events(me_ptr);
               capi_result |=
                  capi_example_gain_module_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->operating_media_fmt);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Set property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_ALGORITHMIC_RESET:
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            /* No implementation for this module*/
            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "capi_example_gain_module: Set property id %x. Not supported.", prop_array[i].id);
            break;
         }
      }
      /* If the result is EOK or EUNSUPPORTED, it is not a real error*/
      if ((CAPI_EOK != capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         CAPI_SET_ERROR(capi_result2, capi_result);
      }
   }

   return capi_result2;
}

/* =========================================================================
 * FUNCTION : capi_example_gain_module_process_get_properties
 * DESCRIPTION:  Combined implementation of get static properties and get properties
 * =========================================================================*/
capi_err_t capi_example_gain_module_process_get_properties(capi_example_gain_module_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t   capi_result  = CAPI_EOK;
   capi_err_t   capi_result2 = CAPI_EOK;
   capi_prop_t *prop_array   = proplist_ptr->prop_ptr;

   for (uint32_t i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_array[i].payload;
      if (NULL == payload_ptr->data_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module: Get property id 0x%x, received null buffer", prop_array[i].id);
         capi_result |= CAPI_EBADPARAM;
         break;
      }
      switch (prop_array[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         {
            /* Memory to be allocate for the gain module capi*/
            if (payload_ptr->max_data_len >= sizeof(capi_init_memory_requirement_t))
            {
               capi_init_memory_requirement_t *data_ptr = (capi_init_memory_requirement_t *)(payload_ptr->data_ptr);
               data_ptr->size_in_bytes                  = gain_align_to_8_byte(sizeof(capi_example_gain_module_t));
               payload_ptr->actual_data_len             = sizeof(capi_init_memory_requirement_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_STACK_SIZE:
         {
            /* Gain module stack size*/
            if (payload_ptr->max_data_len >= sizeof(capi_stack_size_t))
            {
               capi_stack_size_t *data_ptr  = (capi_stack_size_t *)payload_ptr->data_ptr;
               data_ptr->size_in_bytes      = GAIN_MODULE_STACK_SIZE;
               payload_ptr->actual_data_len = sizeof(capi_stack_size_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_IS_INPLACE:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_is_inplace_t))
            {
               capi_is_inplace_t *data_ptr  = (capi_is_inplace_t *)payload_ptr->data_ptr;
               data_ptr->is_inplace         = TRUE;
               payload_ptr->actual_data_len = sizeof(capi_is_inplace_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get  property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_REQUIRES_DATA_BUFFERING:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_requires_data_buffering_t))
            {
               capi_requires_data_buffering_t *data_ptr = (capi_requires_data_buffering_t *)payload_ptr->data_ptr;
               data_ptr->requires_data_buffering        = FALSE;
               payload_ptr->actual_data_len             = sizeof(capi_requires_data_buffering_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get  property id 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_output_media_format_size_t))
            {
               capi_output_media_format_size_t *data_ptr = (capi_output_media_format_size_t *)payload_ptr->data_ptr;
               data_ptr->size_in_bytes                   = sizeof(capi_standard_data_format_v2_t);
               payload_ptr->actual_data_len              = sizeof(capi_output_media_format_size_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_ldac_enc: Get, Property 0x%lx Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               payload_ptr->actual_data_len = 0;
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : null ptr while querying output mf");
               capi_result |= CAPI_EBADPARAM;
               break;
            }

            /* The size of the payload is the sum of the size of the media format struct and the channel map size
             * which depends on the number of channels. Every channel will have a uint16_t channel type field*/
            uint32_t total_size =
               sizeof(capi_example_gain_module_media_fmt_t) + (sizeof(me_ptr->operating_media_fmt.format.channel_type[0]) *
                                                       me_ptr->operating_media_fmt.format.num_channels);

            if (payload_ptr->max_data_len >= total_size)
            {
               capi_example_gain_module_media_fmt_t *data_ptr = (capi_example_gain_module_media_fmt_t *)payload_ptr->data_ptr;
               if ((FALSE == prop_array[i].port_info.is_valid) && (TRUE == prop_array[i].port_info.is_input_port))
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Get output media fmt v2 port id not valid or input port");
                  capi_result |= CAPI_EBADPARAM;
               }
               memscpy(data_ptr, total_size, &me_ptr->operating_media_fmt, total_size);
               payload_ptr->actual_data_len = total_size;
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get property_id 0x%lx, Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               payload_ptr->actual_data_len = 0;
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : null ptr while querying threshold");
               return CAPI_EBADPARAM;
            }
            if (payload_ptr->max_data_len >= sizeof(capi_port_data_threshold_t))
            {
               capi_port_data_threshold_t *data_ptr = (capi_port_data_threshold_t *)payload_ptr;
               if (!prop_array[i].port_info.is_valid)
               {
                  AR_MSG(DBG_ERROR_PRIO, "capi_example_gain_module : Get port threshold port id not valid");
                  capi_result |= CAPI_EBADPARAM;
               }
               if (0 != prop_array[i].port_info.port_index)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_example_gain_module: Get property_id 0x%lx, max in/out port is 1. asking for %lu",
                         (uint32_t)prop_array[i].id,
                         prop_array[i].port_info.port_index);
                  capi_result |= CAPI_EBADPARAM;
               }

               /* default threshold is to be set to 1, framework will determine it in that case*/
               data_ptr->threshold_in_bytes = 1;
               payload_ptr->actual_data_len = sizeof(capi_port_data_threshold_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_example_gain_module: Get property_id 0x%lx, Bad param size %lu",
                      (uint32_t)prop_array[i].id,
                      payload_ptr->max_data_len);
               payload_ptr->actual_data_len = 0;
               capi_result |= CAPI_ENEEDMORE;
            }
            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            /* Since it does not support any framework or interface extensions*/
            break;
         }
         default:
         {
            AR_MSG(DBG_HIGH_PRIO, "capi_example_gain_module: Get property for ID %#x. Not supported.", prop_array[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }

      if ((CAPI_EOK != capi_result) && (CAPI_EUNSUPPORTED != capi_result))
      {
         CAPI_SET_ERROR(capi_result2, capi_result);
      }
   }
   return capi_result2;
}
