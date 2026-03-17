/**
 * \file capi_history_buffer_utils.c
 *
 * \brief
 *
 *     This file contains CAPI APIs for History Buffer Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "capi_history_buffer_i.h"
#include "capi_fwk_extns_island.h"
#include "capi_cmn.h"
/*----------------------------------------------------------------------------
 * Local declerations
 * -------------------------------------------------------------------------*/

static capi_err_t capi_history_buffer_handle_input_media_format(capi_history_buffer_t *me_ptr,
                                                                  capi_port_info_t *       port_info_ptr,
                                                                  capi_buf_t *             payload_ptr);

static capi_err_t capi_handle_history_buffer_event_client_registration(capi_history_buffer_t *me_ptr,
                                                                  capi_port_info_t *       port_info_ptr,
                                                                  capi_buf_t *             payload_ptr);

static capi_err_t capi_history_buffer_validate_input_media_format(
   capi_history_buffer_t *             me_ptr,
   uint32_t                              input_port_index,
   capi_hist_buf_media_fmt_v2_t *data_format_ptr);

/* =========================================================================
 * FUNCTION : capi_history_buffer_validate_input_media_format
 *
 * DESCRIPTION:
 *    Validates input media format. Currently checks only data interleaving
 * type. Add checks depending on the libary specifications.
 * ========================================================================= */
static capi_err_t capi_history_buffer_validate_input_media_format(
   capi_history_buffer_t *             me_ptr,
   uint32_t                              input_port_index,
   capi_hist_buf_media_fmt_v2_t *data_format_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (CAPI_FIXED_POINT != data_format_ptr->header.format_header.data_format)
   {
      DE_DBG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "Error! invalid data format %d",
             (int)data_format_ptr->header.format_header.data_format);
      return CAPI_EBADPARAM;
   }

   capi_standard_data_format_v2_t *pcm_format_ptr = &data_format_ptr->format;

   if (CAPI_DEINTERLEAVED_UNPACKED != pcm_format_ptr->data_interleaving)
   {
      DE_DBG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "Error! Invalid data interleaving type %lu",
             pcm_format_ptr->data_interleaving);
      return CAPI_EBADPARAM;
   }

   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_media_fmt_equal
 *
 * DESCRIPTION:
 *    Checks if the there is any change in input media format.
 * ========================================================================= */
bool_t capi_history_buffer_media_fmt_equal(capi_hist_buf_media_fmt_v2_t *media_fmt_1_ptr,
                                      capi_hist_buf_media_fmt_v2_t *media_fmt_2_ptr)
{
   bool_t are_dif = FALSE;

   // clang-format off
   are_dif |= (media_fmt_1_ptr->header.format_header.data_format != media_fmt_2_ptr->header.format_header.data_format);
   are_dif |= (media_fmt_1_ptr->format.bits_per_sample           != media_fmt_2_ptr->format.bits_per_sample);
   are_dif |= (media_fmt_1_ptr->format.bitstream_format          != media_fmt_2_ptr->format.bitstream_format);
   are_dif |= (media_fmt_1_ptr->format.data_interleaving         != media_fmt_2_ptr->format.data_interleaving);
   are_dif |= (media_fmt_1_ptr->format.data_is_signed            != media_fmt_2_ptr->format.data_is_signed);
   are_dif |= (media_fmt_1_ptr->format.num_channels              != media_fmt_2_ptr->format.num_channels);
   are_dif |= (media_fmt_1_ptr->format.q_factor                  != media_fmt_2_ptr->format.q_factor);
   are_dif |= (media_fmt_1_ptr->format.sampling_rate             != media_fmt_2_ptr->format.sampling_rate);
   // clang-format on

   return !are_dif;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_update_raise_bandwidth_event
 *
 * DESCRIPTION:
 *    Updates module data bandwidth/code bandwidth whenever there is a change.
 * This parameter can change with calibration or media format.
 * ========================================================================= */
static void capi_history_buffer_update_raise_bandwidth_event(capi_history_buffer_t *me_ptr)
{
   if (NULL == me_ptr->cb_info.event_cb)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "event callback is not set. Unable to raise bandwidth event!");
      return;
   }

   capi_err_t capi_result = CAPI_EOK;

   capi_event_bandwidth_t event = { 0 };
   capi_event_info_t      event_info;

   /* TODO: update appropriate bandwidth numbers here. */
   event.code_bandwidth = HISTORY_BUFFER_CODE_BW;
   event.data_bandwidth = HISTORY_BUFFER_DATA_BW;

   DE_DBG(me_ptr->miid,
          DBG_MED_PRIO,
          "Raising bw event, code_bw = %lu, data_bw = %lu",
          event.code_bandwidth,
          event.data_bandwidth);

   event_info.port_info.is_valid   = false;
   event_info.payload.max_data_len = event_info.payload.actual_data_len = sizeof(event);

   event_info.payload.data_ptr = (int8_t *)&event;
   capi_result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_BANDWIDTH, &event_info);
   if (CAPI_FAILED(capi_result))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to send bandwidth update event with %lu", capi_result);
   }
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_update_raise_kpps_event
 *
 * DESCRIPTION:
 *    Updates module's KPPS requirements. This parameter is obtained by
 * profiling.
 * ========================================================================= */
static void capi_history_buffer_update_raise_kpps_event(capi_history_buffer_t *me_ptr)
{
   if (NULL == me_ptr->cb_info.event_cb)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Event callback is not set. Unable to raise kpps event!");
      return;
   }

   capi_err_t        result = CAPI_EOK;
   capi_event_KPPS_t event;
   capi_event_info_t event_info;

   /* TODO: update appropriate KPPS requirement for the module here. This needs to be profiled
      and calculated. */
   uint32_t kpps = HISTORY_BUFFER_KPPS_REQUIREMENT;

   if (me_ptr->kpps != kpps)
   {
      me_ptr->kpps = event.KPPS          = kpps;
      event_info.port_info.is_valid      = false;
      event_info.payload.actual_data_len = event_info.payload.max_data_len = sizeof(event);
      event_info.payload.data_ptr                                          = (int8_t *)&event;

      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_KPPS, &event_info);
      if (CAPI_FAILED(result))
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to send KPPS update event with %lu", result);
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Reported %lu KPPS", kpps);
      }
   }
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_update_raise_algorithmic_delay_event
 *
 * DESCRIPTION:
 *    Updates module data path delay whenever there is a change. This parameter can
 * change with calibration or media format.
 * ========================================================================= */
static void capi_history_buffer_update_raise_algorithmic_delay_event(capi_history_buffer_t *me_ptr)
{
   if (NULL == me_ptr->cb_info.event_cb)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Event callback is not set. Unable to raise delay event!");
      return;
   }

   capi_err_t                     result = CAPI_EOK;
   capi_event_algorithmic_delay_t event;
   capi_event_info_t              event_info;

   /* TODO: update appropriate amount of algorithim delay in micro seconds. */
   uint32_t data_path_delay_in_us = HISTORY_BUFFER_DEFAULT_DELAY_IN_US;

   if (me_ptr->delay_us != data_path_delay_in_us)
   {
      me_ptr->delay_us = data_path_delay_in_us;

      event.delay_in_us                  = data_path_delay_in_us;
      event_info.port_info.is_valid      = false;
      event_info.payload.actual_data_len = sizeof(event);
      event_info.payload.max_data_len    = sizeof(event);
      event_info.payload.data_ptr        = (int8_t *)&event;

      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_ALGORITHMIC_DELAY, &event_info);
      if (CAPI_FAILED(result))
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to send delay update event with %lu", result);
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "reported algo delay = %ld", data_path_delay_in_us);
      }
   }
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_get_properties
 *
 * DESCRIPTION:
 *    Handle get properties. It is a helper function used to get the static
 * as well as dynamic properties of the module.
 *
 * Static properties include memory requirement, stack size, requires data
 * buffering info, list of framework extensions needed and interface
 * extensions supported by the module.
 *
 * Dynamic propeteries queried from the module is usually output media format.
 * But since this module doesn't have output ports. It doesnt need to implement
 * that param
 *
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_get_properties(capi_history_buffer_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   uint32_t   i;
   capi_err_t result = CAPI_EOK;

   capi_prop_t *prop_array = proplist_ptr->prop_ptr;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *   payload_ptr          = &(prop_array[i].payload);
      const uint32_t payload_max_data_len = payload_ptr->max_data_len;

      switch (prop_array[i].id)
      {
      case CAPI_INIT_MEMORY_REQUIREMENT: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_init_memory_requirement_t))
         {
            capi_init_memory_requirement_t *data_ptr = (capi_init_memory_requirement_t *)(payload_ptr->data_ptr);
            data_ptr->size_in_bytes                  = sizeof(capi_history_buffer_t);
            payload_ptr->actual_data_len             = sizeof(capi_init_memory_requirement_t);
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
         break;
      }
      case CAPI_STACK_SIZE: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_stack_size_t))
         {
            capi_stack_size_t *data_ptr  = (capi_stack_size_t *)payload_ptr->data_ptr;
            data_ptr->size_in_bytes      = HISTORY_BUFFER_STACK_SIZE_REQUIREMENT;
            payload_ptr->actual_data_len = sizeof(capi_stack_size_t);
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
         break;
      }
      case CAPI_IS_INPLACE: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_is_inplace_t))
         {
            capi_is_inplace_t *data_ptr = (capi_is_inplace_t *)payload_ptr->data_ptr;

            // Histrory Buffer is not an inplace module
            data_ptr->is_inplace = FALSE;

            payload_ptr->actual_data_len = sizeof(capi_is_inplace_t);
            break;
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
      }
      case CAPI_REQUIRES_DATA_BUFFERING: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_requires_data_buffering_t))
         {
            capi_requires_data_buffering_t *data_ptr = (capi_requires_data_buffering_t *)payload_ptr->data_ptr;
            data_ptr->requires_data_buffering        = FALSE;
            payload_ptr->actual_data_len             = sizeof(capi_requires_data_buffering_t);
            break;
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
      }
      case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_num_needed_framework_extensions_t))
         {
            capi_num_needed_framework_extensions_t *data_ptr =
               (capi_num_needed_framework_extensions_t *)payload_ptr->data_ptr;

            // Update number of extensions here
            data_ptr->num_extensions = HISTORY_BUFFER_NUM_FWK_EXTENSIONS;

            payload_ptr->actual_data_len = sizeof(capi_num_needed_framework_extensions_t);
            break;
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
      }
      case CAPI_NEEDED_FRAMEWORK_EXTENSIONS: /* Static property */
      {
         if (payload_max_data_len >= sizeof(capi_framework_extension_id_t))
         {

            /* NOTE: This example doesnt support any framework extensions, if it does
                     support update the list here. */
            if (HISTORY_BUFFER_NUM_FWK_EXTENSIONS > 0)
            {
               // update fwk extn list ptr and actual data length of the return payload_ptr
               // capi_framework_extension_id_t *data_ptr = (capi_framework_extension_id_t *)payload_ptr->data_ptr;
            }
            break;
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Get property id 0x%lx failed! Bad param size %lu",
                   prop_array[i].id,
                   payload_max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            break;
         }
      }
      case CAPI_INTERFACE_EXTENSIONS: /* Static property */
      {
         if (payload_ptr->max_data_len >= sizeof(capi_interface_extns_list_t))
         {
            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                             (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
            {
               AR_MSG(DBG_ERROR_PRIO, "Invalid interface extension list size %lu", payload_ptr->max_data_len);
               CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
            }
            else
            {
               capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
                  (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

               for (uint32_t i = 0; i < intf_ext_list->num_extensions; i++)
               {
                  switch (curr_intf_extn_desc_ptr->id)
                  {
                  case INTF_EXTN_IMCL:
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
                  AR_MSG(DBG_HIGH_PRIO,
                         "Interface extn = 0x%lx, is_supported = %d",
                         curr_intf_extn_desc_ptr->id,
                         (int)curr_intf_extn_desc_ptr->is_supported);
                  curr_intf_extn_desc_ptr++;
               }
            }
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "Bad interface extenstion param size %lu", payload_ptr->max_data_len);
            CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
         }
         break;
      }
      default:
      {
         CAPI_SET_ERROR(result, CAPI_EUNSUPPORTED);
      }
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_set_properties
 *
 * DESCRIPTION:
 *    Handle set properties. It is a helper function called by the capi_*init()
 * and capi_*set_properties()
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_set_properties(capi_history_buffer_t *me_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t   capi_result = CAPI_EOK;
   capi_prop_t *prop_array  = proplist_ptr->prop_ptr;

   uint32_t i;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *   payload_ptr             = &(prop_array[i].payload);
      const uint32_t payload_actual_data_len = payload_ptr->actual_data_len;

      if (payload_actual_data_len > 0 && !payload_ptr->data_ptr)
      {
         DE_DBG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "Set property id 0x%lx, Bad param size %lu or ptr %p",
                prop_array[i].id,
                payload_actual_data_len,
                payload_ptr);
         CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
         return capi_result;
      }

      switch (prop_array[i].id)
      {
      case CAPI_MODULE_INSTANCE_ID:
      {
         if (payload_actual_data_len < sizeof(capi_module_instance_id_t))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Set property id 0x%lx, Bad param size %lu",
                   prop_array[i].id,
                   payload_actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
         if (data_ptr == NULL)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "callback pointer is NULL");
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         }
         else
         {
            /* Module instance ID, used in debug messages.*/
            me_ptr->miid = data_ptr->module_instance_id;
         }
         break;
      }
      case CAPI_EVENT_CALLBACK_INFO:
      {
         if (payload_actual_data_len < sizeof(capi_event_callback_info_t))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Set property id 0x%lx, Bad param size %lu",
                   prop_array[i].id,
                   payload_actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;
         if (data_ptr == NULL)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "callback pointer is NULL");
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
         }
         else
         {
            /* Call back info, for raising any events to the framework*/
            me_ptr->cb_info = *data_ptr;
         }
         break;
      }
      case CAPI_PORT_NUM_INFO:
      {
         if (payload_actual_data_len < sizeof(capi_port_num_info_t))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Set property id 0x%lx, Bad param size %lu",
                   prop_array[i].id,
                   payload_actual_data_len);
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         capi_port_num_info_t *num_port_info = (capi_port_num_info_t *)payload_ptr->data_ptr;

         /*Verify number of max input/output ports supported by the module

           Currently the module excepts number ports must be equal to the
           supported max number of the modules. In some case, number of ports
           is less than or equal to Max in/out ports. This check can be changed
           if module can support <= max.
          */
         if ((num_port_info->num_input_ports != HISTORY_BUFFER_MAX_INPUT_PORTS) ||
             (num_port_info->num_output_ports != HISTORY_BUFFER_MAX_OUTPUT_PORTS))
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "incorrect number of input (%lu) or output (%lu) ports",
                   num_port_info->num_input_ports,
                   num_port_info->num_output_ports);
            CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
            break;
         }

         me_ptr->num_port_info = *num_port_info;

         DE_DBG(me_ptr->miid,
                DBG_MED_PRIO,
                "num_input_ports: %lu, num_output_ports: %lu",
                me_ptr->num_port_info.num_input_ports,
                me_ptr->num_port_info.num_output_ports);

         break;
      }
      case CAPI_HEAP_ID:
      {
         if (payload_actual_data_len < sizeof(capi_heap_id_t))
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "heap id bad size");
            CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            break;
         }

         capi_heap_id_t *heap_id_ptr = (capi_heap_id_t *)payload_ptr->data_ptr;

         /* This the heap ID used for any dynamic memory allocation by the module.*/
         me_ptr->heap_id = (POSAL_HEAP_ID)heap_id_ptr->heap_id;
         break;
      }

      case CAPI_INPUT_MEDIA_FORMAT_V2:
      {
         capi_result = capi_history_buffer_handle_input_media_format(me_ptr, &prop_array[i].port_info, payload_ptr);
         break;
      }
      case CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2:
      {
         capi_result = capi_handle_history_buffer_event_client_registration(me_ptr, &prop_array[i].port_info, payload_ptr);
         break;
      }
      default:
      {
         DE_DBG(me_ptr->miid, DBG_MED_PRIO, "Unsupported set property id 0x%lx", prop_array[i].id);
         capi_result = CAPI_EUNSUPPORTED;
      }
      }
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_handle_history_buffer_event_client_registration
 *
 * DESCRIPTION:
 *    Helper function to handle detection event registration/deregisration by the DSP clients.
 * Currently History buffer module supports only EVENT_ID_DETECTION_GENERIC_INFO event.
 * If any other event id is pass registeration fails.,
 * ========================================================================= */
static capi_err_t capi_handle_history_buffer_event_client_registration(capi_history_buffer_t *me_ptr,
                                                                  capi_port_info_t *       port_info_ptr,
                                                                  capi_buf_t *             payload_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!me_ptr || !payload_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "NUll ptr is passed ");
      return CAPI_EFAILED;
   }

   /* Validate the payload */
   if (payload_ptr->actual_data_len < sizeof(capi_register_event_to_dsp_client_v2_t))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid payload size %d", payload_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   DE_DBG(me_ptr->miid, DBG_MED_PRIO, "Received CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2");

   if(NULL == payload_ptr->data_ptr)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid event payload data ptr");
      return CAPI_EFAILED;
   }
   capi_register_event_to_dsp_client_v2_t *reg_event_ptr =
      (capi_register_event_to_dsp_client_v2_t *)(payload_ptr->data_ptr);

   switch (reg_event_ptr->event_id)
   {
   case EVENT_ID_DETECTION_ENGINE_GENERIC_INFO: /* Currently History buffer module supports only this event/*/
   {
      DE_DBG(me_ptr->miid,
             DBG_HIGH_PRIO,
             "Received Register/Dergister for the event EVENT_ID_DETECTION_ENGINE_GENERIC_INFO");
      break;
   }
   default:
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Unsupported event ID[%d]", reg_event_ptr->event_id);
      return CAPI_EUNSUPPORTED;
   }
   }

   if (reg_event_ptr->is_register)
   {
      /* Cache the new dsp client info into an empty slot in table */
      uint32_t empty_slot_idx = MAX_TRIGGERED_EVENT_CLIENTS;
      for (uint32_t idx = 0; idx < MAX_TRIGGERED_EVENT_CLIENTS; idx++)
      {
         if (0 == me_ptr->reg_clients_info[idx].dest_address)
         {
            // cache the first available slot
            empty_slot_idx = idx;
         }
         else if ((reg_event_ptr->event_id == me_ptr->reg_clients_info[idx].event_id) &&
                  reg_event_ptr->dest_address == me_ptr->reg_clients_info[idx].dest_address)
         {
            // if a duplicate registration exists, return error
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Failed! Duplicate Registration for the event 0x%lx from client with dest address 0x%llu",
                   reg_event_ptr->event_id,
                   reg_event_ptr->dest_address);
            return CAPI_EFAILED;
         }
      } // end for loop

      /* if the empty slot is not available, return error */
      if (empty_slot_idx == MAX_TRIGGERED_EVENT_CLIENTS)
      {
         DE_DBG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "Max clients %lu registered. Failed registrating from client with address 0x%llu",
                MAX_TRIGGERED_EVENT_CLIENTS,
                reg_event_ptr->dest_address);
         return CAPI_EFAILED;
      }
      else // empty slot is available
      {
         /* Cache destination address of the event client and token. this info is needed
            for raising the detection event. */

         me_ptr->reg_clients_info[empty_slot_idx] = *reg_event_ptr;
      }
   }
   else // deregister the client
   {
      // search for the client in the slots
      bool_t is_client_found = FALSE;
      for (uint32_t idx = 0; idx < MAX_TRIGGERED_EVENT_CLIENTS; idx++)
      {
         if ((reg_event_ptr->dest_address == me_ptr->reg_clients_info[idx].dest_address) &&
             (reg_event_ptr->event_id == me_ptr->reg_clients_info[idx].event_id))
         {
            // reset the client registration info.
            memset(&me_ptr->reg_clients_info[idx], 0, sizeof(capi_register_event_to_dsp_client_v2_t));
            is_client_found = TRUE;
            break; // out of the loop
         }
      }

      /* Return failure, if the client to be de-registered is not registered */
      if (!is_client_found)
      {
         DE_DBG(me_ptr->miid,
                DBG_ERROR_PRIO,
                "Couldn't find client with event_id 0x%llx address 0x%llx in the reg list to deregister. Failing",
                reg_event_ptr->event_id,
                reg_event_ptr->dest_address);
         return CAPI_EFAILED;
      }
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_input_media_format
 *
 * DESCRIPTION:
 *    Helper function to handle Input media format set property.
 * ========================================================================= */
static capi_err_t capi_history_buffer_handle_input_media_format(capi_history_buffer_t *me_ptr,
                                                                  capi_port_info_t *       port_info_ptr,
                                                                  capi_buf_t *             payload_ptr)
{
   capi_err_t       capi_result     = CAPI_EOK;
   capi_port_info_t input_port_info = *port_info_ptr;

   if (!input_port_info.is_input_port || !input_port_info.is_valid)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Error!! property is not for input media format port");
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }

   // validate the MF payload
   if (payload_ptr->actual_data_len < sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid input smedia format size %d", payload_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   capi_hist_buf_media_fmt_v2_t *data_format_ptr =
      (capi_hist_buf_media_fmt_v2_t *)(payload_ptr->data_ptr);

   uint32_t input_port_index           = input_port_info.port_index;

   DE_DBG(me_ptr->miid, DBG_LOW_PRIO, "setting input media format on port index 0x%lx", input_port_index);

   /* Validate input media format based on the port index */
   capi_result = capi_history_buffer_validate_input_media_format(me_ptr, input_port_index, data_format_ptr);
   if (CAPI_FAILED(capi_result))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid media format received on input port index %lu", input_port_index);
      return capi_result;
   }

   /* If the media format doesn't change usually there is nothing to be handled by the module. */
   if (capi_history_buffer_media_fmt_equal(&me_ptr->in_port_info[input_port_index].media_fmt, data_format_ptr))
   {
      DE_DBG(me_ptr->miid,
             DBG_HIGH_PRIO,
             "Input media format did not change, nothing to do. returning with result 0x%lx",
             capi_result);

      CAPI_SET_ERROR(capi_result, CAPI_EOK);
      return capi_result;
   }

   /* Cache the new input media format and update the media format status */
   memscpy(&me_ptr->in_port_info[input_port_index].media_fmt,
           sizeof(capi_hist_buf_media_fmt_v2_t),
           data_format_ptr,
           sizeof(capi_hist_buf_media_fmt_v2_t));

   me_ptr->in_port_info[input_port_index].is_media_fmt_received = TRUE;

   DE_DBG(me_ptr->miid,
          DBG_HIGH_PRIO,
          "Received Input media format: sample_rate = %lu, num_channels = %lu, bps = %lu",
          me_ptr->in_port_info[input_port_index].media_fmt.format.sampling_rate,
          me_ptr->in_port_info[input_port_index].media_fmt.format.num_channels,
          me_ptr->in_port_info[input_port_index].media_fmt.format.bits_per_sample);

   /*
      # IMPORTANT NOTE #
            In this module case, output port is not present so we dont need to raise
      output media format. If module supports output ports, module developer must assign appropriate
      output media format depending upon the module behavior and raise output media format event.
    */

   /* Raise KPPS, bandwidth and delay events */
   capi_history_buffer_update_raise_kpps_event(me_ptr);
   capi_history_buffer_update_raise_bandwidth_event(me_ptr);
   capi_history_buffer_update_raise_algorithmic_delay_event(me_ptr);

   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_raise_event_to_dsp_client
 * DESCRIPTION: Raises an event to the registered DSP client.
 * ========================================================================= */
static capi_err_t capi_history_buffer_raise_event_to_dsp_client(capi_history_buffer_t *me_ptr,
                                                           uint32_t                 event_id,
                                                           void *                   event_payload_ptr,
                                                           uint32_t                 event_payload_size)
{
   capi_err_t                         result = CAPI_EOK;
   capi_event_info_t                  event_info;
   capi_event_data_to_dsp_client_v2_t event;

   if (NULL == event_payload_ptr)
   {
      DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Error! Sending NULL ptr as event payload ");
      return CAPI_EFAILED;
   }

   DE_DBG(me_ptr->miid, DBG_LOW_PRIO, "Raising event id 0x%lx to the registered clients", event_id);

   for (int i = 0; i < MAX_TRIGGERED_EVENT_CLIENTS; i++)
   {
      /* check if a client has been registered for this event id. */
      if ((me_ptr->reg_clients_info[i].event_id != event_id) || (0 == me_ptr->reg_clients_info[i].dest_address))
      {
         continue;
      }

      event.event_id                     = event_id;
      event.payload.actual_data_len      = event_payload_size;
      event.payload.max_data_len         = event_payload_size;
      event.payload.data_ptr             = event_payload_ptr;
      event_info.port_info.is_valid      = FALSE;
      event_info.payload.actual_data_len = sizeof(capi_event_data_to_dsp_client_v2_t);
      event_info.payload.data_ptr        = (int8_t *)&event;
      event_info.payload.max_data_len    = sizeof(capi_event_data_to_dsp_client_v2_t);
      event.dest_address                 = me_ptr->reg_clients_info[i].dest_address;
      event.token                        = me_ptr->reg_clients_info[i].token;

      DE_DBG(me_ptr->miid,
             DBG_HIGH_PRIO,
             "Raised detection event for the client v2 0x%lx token 0x%lx",
             me_ptr->reg_clients_info[i].dest_address,
             me_ptr->reg_clients_info[i].token);

      result = me_ptr->cb_info.event_cb(me_ptr->cb_info.event_context, CAPI_EVENT_DATA_TO_DSP_CLIENT_V2, &event_info);
      if (CAPI_EOK != result)
      {
         DE_DBG(me_ptr->miid,
                DBG_HIGH_PRIO,
                "Failed to raise detection event for the client v2 0x%lx token 0x%lx",
                me_ptr->reg_clients_info[i].dest_address,
                me_ptr->reg_clients_info[i].token);
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_populate_trigger_info_and_raise_event
 * DESCRIPTION: Populates FTRT info into the event payload and
 * raises the event
 * ========================================================================= */
capi_err_t capi_populate_trigger_info_and_raise_event(capi_history_buffer_t *me_ptr)
{
   capi_err_t                         result = CAPI_EOK;

   if (FALSE == me_ptr->latest_trigger_info.is_kw_detected)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Trigger detection not set. Not ready to raise an event.");
      return CAPI_EFAILED;
   }

   if (TRUE == me_ptr->latest_trigger_info.waiting_for_ftrt_len)
   {
      DE_DBG(me_ptr->miid, DBG_HIGH_PRIO, "Waiting for FTRT info from DAM, cannot raise detection event yet.");
      return CAPI_EOK; // this is not an error
   }

   /* populate event payload if generic event info payload is configured */
   if (me_ptr->event_payload_ptr)
   {
      DE_DBG(me_ptr->miid, DBG_LOW_PRIO, "Populating trigger detection event payload.");

      me_ptr->event_payload_ptr->status = me_ptr->latest_trigger_info.is_kw_detected;

      int8_t *event_blob_ptr = (int8_t *)me_ptr->event_payload_ptr + sizeof(event_id_detection_engine_generic_info_t);

      /*check whether keyword indices info is needed to be populated in variable payload*/
      if (me_ptr->event_mode & EVENT_INFO_FTRT_LENGTH)
      {
         DE_DBG(me_ptr->miid, DBG_LOW_PRIO, "Populating FTRT data length info");
         detection_event_info_header_t *header_ptr = (detection_event_info_header_t *)event_blob_ptr;
         header_ptr->key_id                        = KEY_ID_FTRT_DATA_INFO;
         header_ptr->payload_size                  = sizeof(ftrt_data_info_t);
         event_blob_ptr += sizeof(detection_event_info_header_t);

         ftrt_data_info_t *kw_pos_info_ptr = (ftrt_data_info_t *)event_blob_ptr;

         /*Populate ftrt_data__info into the event payload */
         memscpy(kw_pos_info_ptr,
                 sizeof(ftrt_data_info_t),
                 &me_ptr->latest_trigger_info.ftrt_info,
                 sizeof(ftrt_data_info_t));
      }
   }

   // Send the event to DSP client.
   result = capi_history_buffer_raise_event_to_dsp_client(me_ptr,
                                                     EVENT_ID_DETECTION_ENGINE_GENERIC_INFO,
                                                     me_ptr->event_payload_ptr,
                                                     me_ptr->event_payload_size);
   if (CAPI_FAILED(result))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to send Generic detection trigger event to DSP clients.");
   }

   // Discard the cached detection info after raising the event.
   memset(&me_ptr->latest_trigger_info, 0, sizeof(detection_trigger_info_t));

   // vote for island rentry (If we have voted against after detection we have to remove the vote)
   result = capi_cmn_raise_island_vote_event(&me_ptr->cb_info, CAPI_CMN_ISLAND_VOTE_ENTRY);

   return result;
}
