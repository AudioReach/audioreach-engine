/**
 * \file capi_gate.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_gate_i.h"

/*==============================================================================
 Local Defines
 ==============================================================================*/

static capi_err_t capi_gate_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

static capi_err_t capi_gate_end(capi_t *_pif);

static capi_err_t capi_gate_set_param(capi_t *                _pif,
                                      uint32_t                param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *            params_ptr);

static capi_err_t capi_gate_get_param(capi_t *                _pif,
                                      uint32_t                param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *            params_ptr);

static capi_err_t capi_gate_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_gate_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static const capi_vtbl_t vtbl = { capi_gate_process,        capi_gate_end,
                                  capi_gate_set_param,      capi_gate_get_param,
                                  capi_gate_set_properties, capi_gate_get_properties };

/* clang-format on */

/*==========================================================================
 Function Definitions
 ========================================================================== */

/*------------------------------------------------------------------------
 Function name: capi_gate_get_static_properties
 DESCRIPTION: Function to get the static properties of data marker module
 -----------------------------------------------------------------------*/
capi_err_t capi_gate_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   AR_MSG(DBG_HIGH_PRIO, "capi_gate: Enter Get static Properties");

   if (NULL != static_prop_ptr)
   {
      capi_result |= capi_gate_get_properties((capi_t *)NULL, static_prop_ptr);
   }
   return capi_result;
}

/*------------------------------------------------------------------------
 Function name: capi_gate_init
 DESCRIPTION: Initialize the Data Marker module.
 -----------------------------------------------------------------------*/
capi_err_t capi_gate_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   if ((NULL == _pif) || (NULL == init_set_properties))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: Init received NULL pointer");
      return CAPI_EBADPARAM;
   }

   capi_err_t   result = CAPI_EOK;
   capi_gate_t *me_ptr = (capi_gate_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_gate_t));
   me_ptr->vtbl_ptr = &vtbl; // assigning the vtbl with all function pointers

   AR_MSG(DBG_HIGH_PRIO, "capi_gate: Setting Init Properties");
   result = capi_gate_set_properties((capi_t *)me_ptr, init_set_properties);

   // set default
   // calculated as encoder frame size/ Slimbus sampling rate = 96/96k* NUM_US_PER_SEC
   me_ptr->ep_transmit_delay_us = 1000;

   return result;
}

/*------------------------------------------------------------------------
 Function name: capi_gate_set_properties
 DESCRIPTION: This function is used set properties of the CAPI.
 -----------------------------------------------------------------------*/
static capi_err_t capi_gate_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{

   if (!_pif || !props_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: set_prop(): Error! Received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_err_t   capi_result = CAPI_EOK;
   capi_gate_t *me_ptr      = (capi_gate_t *)_pif;
   //uint32_t     buffer_size;
   capi_prop_t *current_prop_ptr;

   capi_result =
      capi_cmn_set_basic_properties(props_ptr, &me_ptr->heap_mem, &me_ptr->event_cb_info, TRUE /*port info*/);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: Set basic properties failed with result %lu", capi_result);
   }

   // This module does not expect media type changes at run
   // Only certain properties are expected to be set
   // for remainder, just print a message and continue
   uint32_t i;
   for (i = 0; i < props_ptr->props_num; i++)
   {
      current_prop_ptr        = &(props_ptr->prop_ptr[i]);
      capi_buf_t *payload_ptr = &(current_prop_ptr->payload);
      //buffer_size             = payload_ptr->actual_data_len;
      switch (current_prop_ptr->id)
      {
         case CAPI_EVENT_CALLBACK_INFO:
         case CAPI_INTERFACE_EXTENSIONS:
         case CAPI_HEAP_ID:
         case CAPI_PORT_DATA_THRESHOLD:
         case CAPI_PORT_NUM_INFO:
         case CAPI_ALGORITHMIC_RESET:
         {
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            /* Validate the MF payload */
            if (payload_ptr->actual_data_len <
                sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: Invalid media format size %d", payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: Set property id 0x%lx, received null buffer", current_prop_ptr->id);
               capi_result |= CAPI_EBADPARAM;
               break;
            }

            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            // /* Validate data format, interleaving and num channels */
            if ((CAPI_FIXED_POINT != media_fmt_ptr->header.format_header.data_format) ||
                (CAPI_MAX_CHANNELS_V2 < media_fmt_ptr->format.num_channels))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: Unsupported Data format %lu or num_channels %lu. Max channels: %lu",
                      media_fmt_ptr->header.format_header.data_format,
                      media_fmt_ptr->format.num_channels,
					  CAPI_MAX_CHANNELS_V2);
               capi_result |= CAPI_EBADPARAM;
               break;
            }

            uint32_t size_to_copy = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                    (media_fmt_ptr->format.num_channels * sizeof(capi_channel_type_t));

            // accept as the operating Media format
            memscpy(&me_ptr->operating_mf, size_to_copy, media_fmt_ptr, payload_ptr->actual_data_len);

            me_ptr->inp_mf_received = TRUE;

            capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->event_cb_info, &me_ptr->operating_mf, FALSE, 0);

            capi_result |= capi_cmn_update_kpps_event(&me_ptr->event_cb_info, CAPI_GATE_KPPS);

#ifdef GATE_DBG
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_gate: Input Media format set prop: bits per sample: %lu bytes, num in/out channels "
                   "%lu",
                   me_ptr->operating_mf.format.bits_per_sample,
                   me_ptr->operating_mf.format.num_channels);
#endif // GATE_DBG
            break;
         } // CAPI_INPUT_MEDIA_FORMAT_V2
         default:
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_gate: set_prop(): Received unsupported prop_id 0x%lx, ignoring",
                   (uint32_t)current_prop_ptr->id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
   }

   return capi_result;
}
/*------------------------------------------------------------------------
 Function name: capi_gate_get_properties
 DESCRIPTION: Function to get the properties from the Data-Marker module
 -----------------------------------------------------------------------*/
static capi_err_t capi_gate_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t   capi_result = CAPI_EOK;
   capi_gate_t *me_ptr      = (capi_gate_t *)_pif;
   uint32_t     i;

   if (NULL == props_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: Get property received null property array");
      return CAPI_EBADPARAM;
   }
   capi_prop_t *prop_ptr = props_ptr->prop_ptr;

   uint32_t fwk_extn_ids[GATE_NUM_FRAMEWORK_EXTENSIONS] = { 0 };
   fwk_extn_ids[0]                                      = FWK_EXTN_CONTAINER_PROC_DURATION;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = sizeof(capi_gate_t);
   mod_prop.stack_size         = GATE_STACK_SIZE;
   mod_prop.num_fwk_extns      = GATE_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = TRUE;
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0;

   capi_result = capi_cmn_get_basic_properties(props_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: Get common basic properties failed with result %lu", capi_result);
   }

   // iterating over the properties
   for (i = 0; i < props_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_PORT_DATA_THRESHOLD: // ignore this (1).
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            break;
         }
         // end static props
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: pif is NULL for get output mf");
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            uint32_t ret_size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                (me_ptr->operating_mf.format.num_channels * sizeof(capi_channel_type_t));

            /* Validate the MF payload */
            if (payload_ptr->max_data_len < sizeof(capi_media_fmt_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_gate: Invalid media format size %d", payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
            memscpy(media_fmt_ptr, ret_size, &me_ptr->operating_mf, ret_size);
            payload_ptr->actual_data_len = sizeof(capi_media_fmt_v2_t);
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
                         "capi_gate: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
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
                            "capi_gate: CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = "
                            "%d",
                            curr_intf_extn_desc_ptr->id,
                            (int)curr_intf_extn_desc_ptr->is_supported);

                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: CAPI_INTERFACE_EXTENSIONS Bad param size %lu",
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_gate: Unknown Prop[0x%lX]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }    // for
   return capi_result;
}

static capi_err_t capi_gate_set_param(capi_t *                _pif,
                                      uint32_t                param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *            params_ptr)
{

   if ((NULL == _pif) || (NULL == params_ptr) || (NULL == params_ptr->data_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_gate: Set param received bad pointer, _pif 0x%lx, params_ptr 0x%lx",
             (uint32_t)_pif,
             (uint32_t)params_ptr);
      return CAPI_EBADPARAM;
   }

   capi_err_t   capi_result = CAPI_EOK;
   capi_gate_t *me_ptr      = (capi_gate_t *)_pif;
   AR_MSG(DBG_HIGH_PRIO, "capi_gate: Set param received id 0x%lx", (uint32_t)param_id);

   switch (param_id)
   {
      case PARAM_ID_GATE_DEADLINE_OFFSET:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_gate_deadline_offset_t))
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_gate: Invalid payload size for param %x, size %d",
                   param_id,
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         param_id_gate_deadline_offset_t *payload_ptr =
            (param_id_gate_deadline_offset_t *)params_ptr->data_ptr;

         const int32_t max_deadline_offset_in_us = 10000;
         const int32_t min_deadline_offset_in_us = -10000;
         if ((payload_ptr->deadline_offset_us > max_deadline_offset_in_us) ||
             (payload_ptr->deadline_offset_us < min_deadline_offset_in_us))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: Invalid deadline offset value %ld", payload_ptr->deadline_offset_us);
            return CAPI_EBADPARAM;
         }
         me_ptr->deadline_offset_us = payload_ptr->deadline_offset_us;

         AR_MSG(DBG_HIGH_PRIO, "capi_gate: Setting deadline offset in us %d", payload_ptr->deadline_offset_us);
         break;
      }
      case FWK_EXTN_PARAM_ID_CONTAINER_PROC_DURATION:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_container_proc_duration_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: Invalid payload size for param %x, size %d",
                   param_id,
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         fwk_extn_param_id_container_proc_duration_t *payload_ptr =
            (fwk_extn_param_id_container_proc_duration_t *)params_ptr->data_ptr;

         me_ptr->proc_dur_us       = payload_ptr->proc_duration_us;
         me_ptr->proc_dur_received = TRUE;

         AR_MSG(DBG_HIGH_PRIO, "capi_gate: Setting proc delay %d", payload_ptr->proc_duration_us);
         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_PORT_OPERATION:
      {
         /** Set the control port operation */
         if (CAPI_EOK != (capi_result = capi_gate_imcl_port_operation(me_ptr, port_info_ptr, params_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_gate: Failed to set control port operation with result %d", capi_result);
         }
         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_INCOMING_DATA:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_incoming_data_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_gate: Invalid payload size for param %x, size %d",
                   param_id,
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         intf_extn_param_id_imcl_incoming_data_t *payload_ptr =
            (intf_extn_param_id_imcl_incoming_data_t *)params_ptr->data_ptr;

         imc_param_header_t *imc_cfg_hdr_ptr = (imc_param_header_t *)(payload_ptr + 1);

         switch (imc_cfg_hdr_ptr->opcode)
         {
            case IMCL_PARAM_ID_BT_DEADLINE_TIME:
            {
               if (sizeof(param_id_imcl_bt_deadline_time) > imc_cfg_hdr_ptr->actual_data_len)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_gate: Invalid payload size for timer drift %d, required = %d",
                         imc_cfg_hdr_ptr->actual_data_len,
                         sizeof(param_id_imcl_bt_deadline_time));
                  return CAPI_ENEEDMORE;
               }

               param_id_imcl_bt_deadline_time *deadline_time_ptr =
                  (param_id_imcl_bt_deadline_time *)(imc_cfg_hdr_ptr + 1);

               if (deadline_time_ptr->open_gate_immediately)
               {
                  // Open the gate - this is set by conn proxy sink for usecases where gate
                  // is not required.
                  me_ptr->gate_opened = TRUE;
               }

               /* Save the deadline time*/
               me_ptr->deadline_time_us  = deadline_time_ptr->deadline_time_us;
               me_ptr->frame_interval_us = deadline_time_ptr->frame_interval_us;

               //Use transmit delay reported by conn proxy module, if not, calculate
               if (deadline_time_ptr->is_ep_transmit_delay_valid)
               {
                  me_ptr->ep_transmit_delay_us = deadline_time_ptr->ep_transmit_delay_us;
               }
               else
               {
                  /* Assuming 96K since slimbus always operates at this SR, cop packetizer always outputs at 16 bit*/
                  me_ptr->ep_transmit_delay_us = (deadline_time_ptr->frame_size_bytes * NUM_US_PER_SEC) / (96000 * 2);
               }

               me_ptr->deadline_time_intent_received = TRUE;

               AR_MSG(DBG_HIGH_PRIO,
                      "capi_gate: IMCL port incoming data, deadline time = %dus frame interval in us %d, encoded "
                      "frame size %d, ep "
                      "transmit delay %d",
                      deadline_time_ptr->deadline_time_us,
                      me_ptr->frame_interval_us,
                      deadline_time_ptr->frame_size_bytes,
                      me_ptr->ep_transmit_delay_us);
               break;
            }
            default:
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_gate: IMCL port incoming data, Unsupported Param id :0x%x \n",
                      imc_cfg_hdr_ptr->opcode);
               break;
            }
         }
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "capi_gate: Set, unsupported param ID 0x%x", (int)param_id);
         capi_result |= CAPI_EUNSUPPORTED;
         break;
      }
   }

   return capi_result;
}

/*------------------------------------------------------------------------
 Function name: capi_gate_get_param
 Gets either a parameter value or a parameter structure containing multiple
 parameters. In the event of a failure, the appropriate error code is
 returned.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_gate_get_param(capi_t *                _pif,
                                      uint32_t                param_id,
                                      const capi_port_info_t *port_info_ptr,
                                      capi_buf_t *            params_ptr)
{
   return CAPI_EUNSUPPORTED;
}

/*------------------------------------------------------------------------
 Function name: capi_gate_process
 Processes an input buffer and generates an output buffer.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_gate_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[])
{
   capi_err_t   result = CAPI_EOK;
   capi_gate_t *me_ptr = (capi_gate_t *)_pif;
   uint32_t     length = 0;

   if (!me_ptr->gate_opened)
   {
      result = capi_gate_until_deadline_process(me_ptr, input, output);
   }
   else // once gate is opened regular processing
   {
      for (uint32_t i = 0; i < me_ptr->operating_mf.format.num_channels; i++)
      {
         if (output[0]->buf_ptr[i].data_ptr)
         {
            length = (input[0]->buf_ptr[i].actual_data_len < output[0]->buf_ptr[i].max_data_len)
                        ? input[0]->buf_ptr[i].actual_data_len
                        : output[0]->buf_ptr[i].max_data_len;

            // inplace module so copy only if buffers are different
            if (input[0]->buf_ptr[i].data_ptr != output[0]->buf_ptr[i].data_ptr)
            {
               memscpy(output[0]->buf_ptr[i].data_ptr,
                       output[0]->buf_ptr[i].max_data_len,
                       input[0]->buf_ptr[i].data_ptr,
                       input[0]->buf_ptr[i].actual_data_len);
            }
            // For output, the buf actual len after process is amount of data filled
            // For input, buf actual len after process is amount of data consumed
            output[0]->buf_ptr[i].actual_data_len = length;
            input[0]->buf_ptr[i].actual_data_len  = length;
         }
      }
   }

   return result;
}

/*------------------------------------------------------------------------
 Function name: capi_gate_end
 Returns the library to the uninitialized state and frees the
 memory that was allocated by module. This function also frees the virtual
 function table.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_gate_end(capi_t *_pif)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == _pif)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_gate: End received NULL capi pointer");
      return CAPI_EBADPARAM;
   }
   capi_gate_t *me_ptr = (capi_gate_t *)((_pif));

   me_ptr->vtbl_ptr = NULL;

   return result;
}
