/**
 *  \file capi_spr.c
 *  \brief
 *        This file contains CAPI implementation of Splitter Renderer Module
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_spr_i.h"
#include "imcl_fwk_intent_api.h"

static capi_err_t capi_spr_end(capi_t *_pif);

static capi_err_t capi_spr_set_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr);

static capi_err_t capi_spr_get_param(capi_t *                _pif,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr);

static capi_err_t capi_spr_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static capi_err_t capi_spr_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

static const capi_vtbl_t vtbl = { capi_spr_process,        capi_spr_end,
                                  capi_spr_set_param,      capi_spr_get_param,
                                  capi_spr_set_properties, capi_spr_get_properties };

/*==============================================================================
 Local Function forward declaration
 ==============================================================================*/

/*==============================================================================
 Public Function Implementation
 ==============================================================================*/

/*
 This function is used to query the static properties to create the CAPI.

 param[in] init_set_prop_ptr: Pointer to the initializing property list
 param[in, out] static_prop_ptr: Pointer to the static property list

 return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_spr_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
{
   return capi_spr_get_properties((capi_t *)NULL, static_prop_ptr);
}

/*
 This function is used init the CAPI lib.

 param[in] capi_ptr: Pointer to the CAPI lib.
 param[in] init_set_prop_ptr: Pointer to the property list that needs to be
 initialized

 return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_spr_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == init_set_prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "NULL capi_ptr[%p], init_set_prop_ptr[%p]", capi_ptr, init_set_prop_ptr);
      return CAPI_EFAILED;
   }

   capi_spr_t *me_ptr = (capi_spr_t *)((capi_ptr));
   me_ptr->vtbl       = &vtbl;

   // Initialize module instance ID to 0
   me_ptr->miid = 0;
   memset(&(me_ptr->flags), 0, sizeof(spr_flags_t));

   // Set the init properties.
   result = capi_spr_set_properties((capi_t *)me_ptr, init_set_prop_ptr);

   // Intialize SPR driver library, its done after init props since we need heap ID for the lib init.
   result |= spr_driver_init(me_ptr->heap_id, &me_ptr->drv_ptr, (intptr_t)me_ptr);

   // Set the preferred chunk size on the driver.
   result |= spr_driver_set_chunk_size(me_ptr->drv_ptr, DEFAULT_CIRC_BUF_CHUNK_SIZE);

   // Set the default primary output port idx to UMAX_32. Will be updated later on based on active port state
   me_ptr->primary_output_arr_idx = UMAX_32;

   // Initial ts is 0
   me_ptr->spr_out_drift_info.spr_acc_drift.time_stamp_us = 0;

   // Set drift function for other modules to read drift
   result = capi_spr_init_out_drift_info(&me_ptr->spr_out_drift_info, spr_read_acc_out_drift);
   if (CAPI_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: Failed to initialize drift read function");
      return result;
   }

   me_ptr->flags.is_timer_disabled = FALSE; // By default SPR runs in timer enable mode

   // Raise data trigger in ST container event
   capi_spr_raise_event_data_trigger_in_st_cntr(me_ptr);

   return result;
}

/*
 This function is used to set the properties to the CAPI

 param[in] capi_ptr: Pointer to the CAPI.
 param[in] proplist_ptr: Pointer to the property list that needs to be set

 return: CAPI_EOK(0) on success else failure error code
 */
static capi_err_t capi_spr_set_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if ((NULL == capi_ptr) || (NULL == proplist_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: Set property failed. received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_spr_t *me_ptr = (capi_spr_t *)capi_ptr;

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;
   if (NULL == prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: Set property failed. received bad pointer in prop_ptr");
      return CAPI_EFAILED;
   }

   uint32_t i;

   for (i = 0; i < proplist_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_port_num_info_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr: CAPI V2 FAILED Set Property id 0x%x Bad param size %u",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }
            capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;

            // Port related structures
            if (NULL != me_ptr->in_port_info_arr || NULL != me_ptr->out_port_info_arr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_spr: Set property failed. Port number info is already set.");
               return CAPI_EFAILED;
            }
            if (1 != data_ptr->num_input_ports)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_spr: Only one input port supported in SPR");
               return CAPI_EFAILED;
            }

            // Assign max input ports.
            me_ptr->max_input_ports  = data_ptr->num_input_ports;
            me_ptr->max_output_ports = data_ptr->num_output_ports;

            // Each output port can support two control port plus one for the stream RM module
            me_ptr->max_ctrl_ports = (2 * me_ptr->max_output_ports) + 1;

            // Create port related structures based on the port info.
            capi_result |= capi_spr_create_port_structures(me_ptr);

            capi_result |= capi_spr_create_trigger_policy_mem(me_ptr);

            break;
         }
         case CAPI_HEAP_ID:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_heap_id_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr:  Set Property failed. id= 0x%x Bad param size %u",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_heap_id_t *data_ptr = (capi_heap_id_t *)payload_ptr->data_ptr;
            me_ptr->heap_id          = (POSAL_HEAP_ID)data_ptr->heap_id;

            break;
         }
         case CAPI_EVENT_CALLBACK_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_event_callback_info_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr: Set Property Failed. id= 0x%x Bad param size %u",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;
            me_ptr->event_cb_info.event_cb       = data_ptr->event_cb;
            me_ptr->event_cb_info.event_context  = data_ptr->event_context;

            break;
         }
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                        = data_ptr->module_instance_id;
               SPR_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "This module-id 0x%08lX, instance-id 0x%08lX",
                       data_ptr->module_id,
                       me_ptr->miid);

               capi_spr_avsync_set_miid(me_ptr->avsync_ptr, me_ptr->miid);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr: Set, Param id 0x%lx Bad param size %lu",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == payload_ptr->data_ptr)
            {
               SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               return CAPI_EFAILED;
            }

            /* Validate the MF payload */
            if (payload_ptr->actual_data_len <
                sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
            {
               SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid media format size %d", payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            if (!prop_ptr[i].port_info.is_valid)
            {
               SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Media format port info is invalid");
               return CAPI_ENEEDMORE;
            }
            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            // Validate the media format
            // Check data format. Only fixed point is supported.
            if (media_fmt_ptr->header.format_header.data_format != CAPI_FIXED_POINT)
            {
               SPR_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Unsupported data format = 0x%x. ",
                       media_fmt_ptr->header.format_header.data_format);
               return CAPI_EBADPARAM;
            }

            // Check data format. Only fixed point is supported.
            if ((media_fmt_ptr->format.num_channels != 1) &&
                (CAPI_DEINTERLEAVED_UNPACKED != media_fmt_ptr->format.data_interleaving))
            {
               SPR_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Unsupported data interleaving type= 0x%x, only deinterleaved unpacked is "
                       "supported. ",
                       media_fmt_ptr->format.data_interleaving);
               return CAPI_EBADPARAM;
            }

            // Check if the number of channels match with the input port set config
            if (media_fmt_ptr->format.num_channels > MAX_CHANNELS_PER_STREAM)
            {
               SPR_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Unsupported mf num_channels= %d,",
                       media_fmt_ptr->format.num_channels);
               return CAPI_EBADPARAM;
            }

            SPR_MSG(me_ptr->miid, DBG_MED_PRIO, "Received valid input media format.");

            // If media format is received for the first time cache the media format.
            if (FALSE == me_ptr->flags.is_input_media_fmt_set)
            {
               capi_err_t result = capi_spr_handle_media_fmt_change(me_ptr, media_fmt_ptr, TRUE /*check_cache_mf*/);
               SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "First media format handled with result 0x%x", result);

               me_ptr->flags.is_input_media_fmt_set = TRUE;
            }
            else
            {
               if (!capi_cmn_media_fmt_equal(&me_ptr->operating_mf, media_fmt_ptr))
               {
                  capi_err_t result = capi_spr_handle_media_fmt_change(me_ptr, media_fmt_ptr, TRUE /*check_cache_mf*/);
                  SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Media format change handled with result 0x%x", result);
               }
               else
               {
                  SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "No change in media format ");
               }
            }

            break;
         }
         case CAPI_ALGORITHMIC_RESET:
         {
            // if the port info is not valid, then do nothing
            if (!prop_ptr[i].port_info.is_valid)
            {
               break;
            }

            SPR_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "algo reset issued on port_index %d is_input %d",
                    prop_ptr[i].port_info.port_index,
                    prop_ptr[i].port_info.is_input_port);

            // if reset is issued on any output port, do not reset session clock
            if (!prop_ptr[i].port_info.is_input_port)
            {
               break;
            }

            SPR_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "algo reset honored on port_index %d is_input %d",
                    prop_ptr[i].port_info.port_index,
                    prop_ptr[i].port_info.is_input_port);

            capi_spr_check_raise_session_time_reset_event(me_ptr);
            capi_spr_avsync_reset_session_clock_params(me_ptr->avsync_ptr);
            me_ptr->flags.has_rcvd_first_buf     = FALSE;
            me_ptr->flags.has_rendered_first_buf = FALSE;

            // Hold buffer will have stale data compared to session clock, hence destroy hold buffer at algo reset
            capi_spr_destroy_hold_buf_list(me_ptr);

            bool_t APPLY_TAIL_MF = TRUE;
            capi_spr_destroy_cached_mf_list(me_ptr, APPLY_TAIL_MF);

            break;
         }
         case CAPI_CUSTOM_PROPERTY:
         {
            capi_custom_property_t *cust_prop_ptr    = (capi_custom_property_t *)payload_ptr->data_ptr;
            void *                  cust_payload_ptr = (void *)(cust_prop_ptr + 1);

            switch (cust_prop_ptr->secondary_prop_id)
            {
               case FWK_EXTN_PROPERTY_ID_STM_TRIGGER:
               {
                  if (payload_ptr->actual_data_len < sizeof(capi_custom_property_t) + sizeof(capi_prop_stm_trigger_t))
                  {
                     SPR_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Property id 0x%lx Insufficient payload size %d",
                             (uint32_t)cust_prop_ptr->secondary_prop_id,
                             payload_ptr->actual_data_len);
                     return CAPI_EBADPARAM;
                  }

                  // Get stm info
                  capi_prop_stm_trigger_t *trig_ptr     = (capi_prop_stm_trigger_t *)cust_payload_ptr;
                  me_ptr->signal_ptr                    = trig_ptr->signal_ptr;
                  me_ptr->flags.signal_trigger_set_flag = TRUE;

                  // Will check inside if timer is enabled
                  capi_result |= spr_timer_enable(me_ptr);

                  break;
               }
               case FWK_EXTN_PROPERTY_ID_STM_CTRL:
               {
                  if (payload_ptr->actual_data_len < sizeof(capi_prop_stm_ctrl_t))
                  {
                     SPR_MSG(me_ptr->miid,
                             DBG_ERROR_PRIO,
                             "Property id 0x%lx Bad param size %lu",
                             (uint32_t)cust_prop_ptr->secondary_prop_id,
                             payload_ptr->actual_data_len);
                     capi_result |= CAPI_ENEEDMORE;
                     break;
                  }
                  capi_prop_stm_ctrl_t *timer_en = (capi_prop_stm_ctrl_t *)cust_payload_ptr;
                  me_ptr->flags.stm_ctrl_enable  = timer_en->enable;

                  SPR_MSG(me_ptr->miid,
                          DBG_HIGH_PRIO,
                          "Calling timer enable/disable from stm control counter %d, enable %d",
                          me_ptr->counter,
                          me_ptr->flags.stm_ctrl_enable);

                  // to make sure signal is received
                  if (me_ptr->flags.signal_trigger_set_flag)
                  {
                     capi_result |= spr_timer_enable(me_ptr);
                  }

                  CAPI_CMN_UNDERRUN_INFO_RESET(me_ptr->underrun_info);

                  break;
               }
               default:
               {
                  SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Unknown Custom Property[%d]", cust_prop_ptr->secondary_prop_id);
                  capi_result |= CAPI_EUNSUPPORTED;
                  break;
               }
            }
            break;
         }
         case CAPI_REGISTER_EVENT_DATA_TO_DSP_CLIENT_V2:
         {

            if (NULL == payload_ptr->data_ptr)
            {
               SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               capi_result |= CAPI_EFAILED;
               break;
            }

            /* Validate the payload size */
            if (payload_ptr->actual_data_len < sizeof(capi_register_event_to_dsp_client_v2_t))
            {
               SPR_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Invalid size %d for register event to dsp client",
                       payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }

            capi_register_event_to_dsp_client_v2_t *reg_event_ptr =
               (capi_register_event_to_dsp_client_v2_t *)(payload_ptr->data_ptr);

            capi_result |= capi_spr_process_register_event_to_dsp_client(me_ptr, reg_event_ptr);

            break;
         }
         default:
         {
#if 0
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_spr: Set property failed. received un-supported prop_id 0x%x",
                   (uint32_t)prop_ptr[i].id);
#endif
            capi_result |= CAPI_EUNSUPPORTED;
         }
      } /* Outer switch - Generic CAPI Properties */
   }    /* Loop all properties */

   return capi_result;
}

/*
 This function is used to get the properties from the CAPI

 param[in] capi_ptr: Pointer to the CAPI.
 param[in] proplist_ptr: Pointer to the property list that needs to be queried

 return: CAPI_EOK(0) on success else failure error code
 */
static capi_err_t capi_spr_get_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t  capi_result = CAPI_EOK;
   capi_spr_t *me_ptr      = (capi_spr_t *)capi_ptr;
   uint32_t    i;

   if (NULL == proplist_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: Get properties received null arguments");
      return CAPI_EBADPARAM;
   }
   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   capi_basic_prop_t mod_prop;
   // Extensions a) frame duration to get timer duration b) STM for timer c) trigger policy for dropping data
   // d) multi-port buffering for hold/drop of data - this in turns helps mark SPR as nblc end and helps avoid topo
   // buffering
   uint32_t fwk_extn_ids_arr[] = { FWK_EXTN_THRESHOLD_CONFIGURATION,
                                   FWK_EXTN_STM,
                                   FWK_EXTN_TRIGGER_POLICY,
                                   FWK_EXTN_MULTI_PORT_BUFFERING };
   mod_prop.init_memory_req    = sizeof(capi_spr_t);
   mod_prop.stack_size         = CAPI_SPR_MODULE_STACK_SIZE;
   mod_prop.num_fwk_extns      = sizeof(fwk_extn_ids_arr) / sizeof(fwk_extn_ids_arr[0]);
   mod_prop.is_inplace         = FALSE; // not capable of in-place processing of data
   mod_prop.req_data_buffering = FALSE; // requires data buf due to hold situations, where module may not empty input.
   mod_prop.max_metadata_size  = 0;     // NA
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids_arr;

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: Get properties failed with result %lu", capi_result);
      return capi_result;
   }

   // iterating over the properties
   for (i = 0; i < proplist_ptr->props_num; i++)
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
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            // handled in capi common utils.
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            // TODO:
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
                         "capi_spr: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
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
                        case INTF_EXTN_METADATA:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_PATH_DELAY:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_IMCL:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_DATA_PORT_OPERATION:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_PROP_IS_RT_PORT_PROPERTY:
                        {
                           curr_intf_extn_desc_ptr->is_supported = TRUE;
                           break;
                        }
                        case INTF_EXTN_DUTY_CYCLING_ISLAND_MODE:
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
                            "capi_spr: CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = %d",
                            curr_intf_extn_desc_ptr->id,
                            (int)curr_intf_extn_desc_ptr->is_supported);
                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr: CAPI_INTERFACE_EXTENSIONS Bad param size %lu",
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_spr: Get properties Arg 1 = Null while querying MF");
               return CAPI_EBADPARAM;
            }
            // index of the output port
            uint32_t arr_index = spr_get_arr_index_from_port_index(me_ptr, prop_ptr[i].port_info.port_index, FALSE);
            if (IS_INVALID_PORT_INDEX(arr_index))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_spr: Output index is unknown, port_index=%u",
                      prop_ptr[i].port_info.port_index);
               break;
            }

            // Output media format can be know only if the output port configuration is received
            // and stream reader is setup.
            if (NULL == me_ptr->out_port_info_arr[arr_index].strm_reader_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_spr: Output media format is unknown, arr_index=%u", arr_index);
               // still raise event to prevent fwk from thinking we don't support v2 mf.
            }

            capi_media_fmt_v2_t *out_mf_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);

            *out_mf_ptr = me_ptr->operating_mf;

            // Update the actual data length.
            payload_ptr->actual_data_len = sizeof(capi_media_fmt_v2_t);

            break;
         } // CAPI_OUTPUT_MEDIA_FORMAT_V2
         default:
         {
            //AR_MSG(DBG_ERROR_PRIO, "capi_spr: Unknown Prop[0x%lX]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }
   return capi_result;
}

/*------------------------------------------------------------------------
 Function name: capi_spr_set_param
 Sets either a parameter value or a parameter structure containing multiple
 parameters. In the event of a failure, the appropriate error code is
 returned.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_spr_set_param(capi_t *                capi_ptr,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "capi_spr: Set param failed. received bad property pointer for param_id property, 0x%x",
             param_id);
      return CAPI_EFAILED;
   }

   capi_spr_t *me_ptr = (capi_spr_t *)((capi_ptr));

   switch (param_id)
   {
      case PARAM_ID_SPR_AVSYNC_CONFIG:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(param_id_spr_avsync_config_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Invalid payload size for setting end of delay path %d",
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_spr_avsync_config_t *render_config = (param_id_spr_avsync_config_t *)params_ptr->data_ptr;
         // Update avsync configuration
         result |= capi_spr_process_avsync_config_param(me_ptr, render_config);
         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      case FWK_EXTN_PARAM_ID_THRESHOLD_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_threshold_cfg_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }
         fwk_extn_param_id_threshold_cfg_t *fm_dur = (fwk_extn_param_id_threshold_cfg_t *)params_ptr->data_ptr;
         me_ptr->frame_dur_us                      = fm_dur->duration_us;
         SPR_MSG(me_ptr->miid, DBG_LOW_PRIO, "Frame duration of SPR configured to %lu us", fm_dur->duration_us);

         capi_spr_update_frame_duration_in_bytes(me_ptr);

         break;
      }
      case PARAM_ID_SPR_CTRL_TO_DATA_PORT_MAP:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_spr_ctrl_to_data_port_map_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         result |= capi_spr_update_ctrl_data_port_map(me_ptr, params_ptr);
         capi_spr_send_output_drift_info(me_ptr);

         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_PORT_OPERATION:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_port_operation_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Invalid payload size for ctrl port operation %d",
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         result |= capi_spr_imcl_port_operation(me_ptr, params_ptr);

         break;
      }
      case INTF_EXTN_PARAM_ID_IMCL_INCOMING_DATA:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }
         result |= capi_spr_imcl_handle_incoming_data(me_ptr, params_ptr);
         break;
      }
      case INTF_EXTN_PARAM_ID_RESPONSE_PATH_DELAY:
      {
         result |= spr_set_response_to_path_delay_event(me_ptr, params_ptr);
         break;
      }
      case INTF_EXTN_PARAM_ID_DESTROY_PATH_DELAY:
      {
         result |= spr_set_destroy_path_delay_cfg(me_ptr, params_ptr);
         break;
      }
      case PARAM_ID_SPR_DELAY_PATH_END:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(param_id_spr_delay_path_end_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Invalid payload size for setting end of delay path %d",
                    params_ptr->actual_data_len);
            result |= CAPI_ENEEDMORE;
            break;
         }

         // if this param is sent multiple times for the same module instance then it is ignored.
         // setting up another path for the same path-end will result duplicate path setup in APM.

         // if there are multiple output ports at the path-end module then APM sets up the path delay for the first
         // available output port.

         // if multiple spr output ports are connected to the same path-end module then path delay can only be setup for
         // the one of the output port other output ports of SPR will not get the path delay. This problem can only be
         // fixed if this setparam includes output port id as well.

         param_id_spr_delay_path_end_t *data_ptr        = (param_id_spr_delay_path_end_t *)params_ptr->data_ptr;
         bool_t                         is_setup_needed = TRUE;

         // if the path is already setup then ignore.
         for (uint32_t arr_index = 0; arr_index < me_ptr->max_output_ports; arr_index++)
         {
            if ((0 != me_ptr->out_port_info_arr[arr_index].port_id) &&
                (data_ptr->module_instance_id == me_ptr->out_port_info_arr[arr_index].path_delay.dst_module_iid))
            {
               is_setup_needed = FALSE;
               break;
            }
         }

         if (is_setup_needed)
         {
            // Request APM for delay pointers
            spr_request_path_delay(me_ptr, data_ptr->module_instance_id);
         }

         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         result |= capi_spr_data_port_op_handler(me_ptr, params_ptr);
         break;
      }
      case INTF_EXTN_PARAM_ID_IS_RT_PORT_PROPERTY:
      {
         result |= capi_spr_set_data_port_property(me_ptr, params_ptr);
         break;
      }
      case FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         // Level 1 check
         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Invalid payload size for trigger policy %d",
                    params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         fwk_extn_param_id_trigger_policy_cb_fn_t *payload_ptr =
            (fwk_extn_param_id_trigger_policy_cb_fn_t *)params_ptr->data_ptr;
         me_ptr->data_trigger.policy_chg_cb = *payload_ptr;

         break;
      }
      case INTF_EXTN_PARAM_ID_CNTR_DUTY_CYCLING_ENABLED:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_cntr_duty_cycling_enabled_t))
         {
             SPR_MSG(me_ptr->miid,
                     DBG_ERROR_PRIO,
                     "Invalid payload size for CNTR_DUTY_CYCLING_ENABLED %d",
                     params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_cntr_duty_cycling_enabled_t *payload_ptr =
            (intf_extn_param_id_cntr_duty_cycling_enabled_t *)params_ptr->data_ptr;
         me_ptr->flags.is_cntr_duty_cycling = payload_ptr->is_cntr_duty_cycling;
         SPR_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "SPR configured is_cntr_duty_cycling to %lu",
                 me_ptr->flags.is_cntr_duty_cycling);
         break;
      }
      case PARAM_ID_SPR_SESSION_TIME_RESET_INFO:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         // Level 1 check
         if (params_ptr->actual_data_len < sizeof(param_id_spr_session_time_reset_info_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Invalid payload size for reset session time info %d",
                    params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         param_id_spr_session_time_reset_info_t *reset_info_ptr = (param_id_spr_session_time_reset_info_t *)params_ptr->data_ptr;
         // Update avsync configuration
         result |= capi_spr_process_session_time_reset_param(me_ptr, reset_info_ptr);

         break;
      }
      default:
      {
         SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Unsupported Param id ::0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
   }

   SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Set param 0x%x done 0x%x", param_id, result);
   return result;
}

/*------------------------------------------------------------------------
 Function name: capi_spr_get_param
 Gets either a parameter value or a parameter structure containing multiple
 parameters. In the event of a failure, the appropriate error code is
 returned.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_spr_get_param(capi_t *                capi_ptr,
                                     uint32_t                param_id,
                                     const capi_port_info_t *port_info_ptr,
                                     capi_buf_t *            params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_spr: get: received bad input pointer(s) for param_id 0x%x property", param_id);
      return CAPI_EFAILED;
   }

   capi_spr_t *me_ptr = (capi_spr_t *)((capi_ptr));

   switch (param_id)
   {
      case PARAM_ID_SPR_SESSION_TIME:
      {
         if (NULL == params_ptr->data_ptr)
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "get: Param id 0x%lx received NULL buffer", (uint32_t)param_id);
            capi_result |= CAPI_EBADPARAM;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(param_id_spr_session_time_t))
         {
            SPR_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "get: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         param_id_spr_session_time_t *session_time_ptr = (param_id_spr_session_time_t *)(params_ptr->data_ptr);
         if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
         {
            SPR_MSG(me_ptr->miid, DBG_ERROR_PRIO, "get: attempt to query session time when avsync is not enabled");
            capi_result |= CAPI_EFAILED;
            break;
         }

         memset(session_time_ptr, 0, sizeof(param_id_spr_session_time_t));

         int64_t   session_time, absolute_ts, proc_ts;
         bool_t    is_ts_valid = FALSE;
         avsync_t *avsync_ptr  = me_ptr->avsync_ptr;
         session_time = absolute_ts = proc_ts = 0;

         session_time = avsync_ptr->session_clock_us;
         absolute_ts  = avsync_ptr->absolute_time_us;
         proc_ts      = avsync_ptr->proc_timestamp_us;
         is_ts_valid  = avsync_ptr->flags.is_ts_valid;

         session_time_ptr->absolute_time.value_lsw = (uint32_t)(absolute_ts);
         session_time_ptr->absolute_time.value_msw = (uint32_t)(absolute_ts >> 32);
         session_time_ptr->session_time.value_lsw  = (uint32_t)(session_time);
         session_time_ptr->session_time.value_msw  = (uint32_t)(session_time >> 32);
         session_time_ptr->timestamp.value_lsw     = (uint32_t)(proc_ts);
         session_time_ptr->timestamp.value_msw     = (uint32_t)(proc_ts >> 32);
         session_time_ptr->flags |= ((is_ts_valid) << PARAM_ID_SESSION_TIME_SHIFT_IS_TIMESTAMP_VALID);

         params_ptr->actual_data_len = sizeof(param_id_spr_session_time_t);

         // Debug messages
         SPR_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "get: session_time: [session time msw, session time lsw] = [%lu, %lu] : "
                 "[absolute time msw, absolute time lsw] = [%lu, %lu]:"
                 "[time stamp msw, time stamp lsw] = [%lu, %lu]:"
                 "Flags 0x%x",
                 session_time_ptr->session_time.value_msw,
                 session_time_ptr->session_time.value_lsw,
                 session_time_ptr->absolute_time.value_msw,
                 session_time_ptr->absolute_time.value_lsw,
                 session_time_ptr->timestamp.value_msw,
                 session_time_ptr->timestamp.value_lsw,
                 session_time_ptr->flags);
      }
      break;
      case FWK_EXTN_PARAM_ID_LATEST_TRIGGER_TIMESTAMP_PTR:
      {
         // No need to return this ts ptr because SPR output ts = input TS.
         // Latest trigger TS is used mainly for EC ref path in HW EP. In SPR, even if there's a splitter before
         // SPR, it can propagate input TS (if any) or let it go without TS. ICMD - is from SPR output.
         // see RAT for some more details.
      }
      break;
      default:
      {
         SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "get: Unsupported Param id ::0x%x \n", param_id);
         capi_result = CAPI_EUNSUPPORTED;
      }
      break;
   }

   // SPR_MSG(me_ptr->miid, DBG_HIGH_PRIO, "get: done for param id 0x%x", param_id);
   return capi_result;
}

/*------------------------------------------------------------------------
 Function name: capi_spr_end
 Returns the library to the uninitialized state and frees the memory that
 was allocated by Init(). This function also frees the virtual function
 table.
 * -----------------------------------------------------------------------*/
static capi_err_t capi_spr_end(capi_t *capi_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CAPI SPR end received bad pointer, %p", capi_ptr);
      return CAPI_EFAILED;
   }
   capi_spr_t *me_ptr = (capi_spr_t *)((capi_ptr));

   if (me_ptr->out_port_info_arr)
   {
      // Destroy all the output ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_output_ports; arr_idx++)
      {
         capi_spr_destroy_output_port(me_ptr, arr_idx, FALSE /* Complete Destroy*/);
      }
      posal_memory_free(me_ptr->out_port_info_arr);
      me_ptr->out_port_info_arr = NULL;
   }

   if (me_ptr->in_port_info_arr)
   {
      // Destroy all the input ports and free the port structure memory.
      for (uint32_t arr_idx = 0; arr_idx < me_ptr->max_input_ports; arr_idx++)
      {
         // as part of this, destroy the cached mf lists if any
         capi_spr_destroy_input_port(me_ptr, arr_idx);
      }
      posal_memory_free(me_ptr->in_port_info_arr);
      me_ptr->in_port_info_arr = NULL;
   }

   if (me_ptr->ctrl_port_list_ptr)
   {
      // Destroy control port list
      spf_list_delete_list_and_free_objs((spf_list_node_t **)&me_ptr->ctrl_port_list_ptr, TRUE);
      me_ptr->ctrl_port_list_ptr = NULL;
   }

   // De initialize the SPR driver.
   spr_driver_deinit(&me_ptr->drv_ptr, (intptr_t)me_ptr);

   // Destroy the hold buffer
   capi_spr_destroy_hold_buf_list(me_ptr);

   // Destroy the AVSync driver
   (void)capi_spr_avsync_destroy(&me_ptr->avsync_ptr, me_ptr->miid);

   if (me_ptr->data_trigger.trigger_groups_ptr)
   {
      posal_memory_free(me_ptr->data_trigger.trigger_groups_ptr);
      me_ptr->data_trigger.trigger_groups_ptr    = NULL;
      me_ptr->data_trigger.non_trigger_group_ptr = NULL;
   }

   result |= capi_spr_deinit_out_drift_info(&me_ptr->spr_out_drift_info);

   me_ptr->vtbl = NULL;
   return result;
}
