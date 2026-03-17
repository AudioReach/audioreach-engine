/**
 *   \file capi_congestion_buf.c
 *   \brief
 *        This file contains CAPI implementation of Congestion Buffer module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_congestion_buf_i.h"


capi_err_t capi_congestion_buf_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr);


/*==============================================================================
   Local Function forward declaration
==============================================================================*/

/*==============================================================================
   Public Function Implementation
==============================================================================*/

capi_err_t capi_congestion_buf_get_static_properties(capi_proplist_t *init_set_prop_ptr,
                                                     capi_proplist_t *static_prop_ptr)
{
   return capi_congestion_buf_get_properties(NULL, static_prop_ptr);
}

/*
  This function is used init the CAPI lib.

  param[in] capi_ptr: Pointer to the CAPI lib.
  param[in] init_set_prop_ptr: Pointer to the property list that needs to be
            initialized

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_congestion_buf_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr || NULL == init_set_prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "NULL capi_ptr[%p], init_set_prop_ptr[%p]", capi_ptr, init_set_prop_ptr);
      return CAPI_EFAILED;
   }

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)((capi_ptr));

   memset(me_ptr, 0, sizeof(capi_congestion_buf_t));

   me_ptr->vtbl = capi_congestion_buf_get_vtbl(); // assigning the vtbl with all function pointers

   /* Set the init properties. */
   result = capi_congestion_buf_set_properties((capi_t *)me_ptr, init_set_prop_ptr);

   /* Congestion buffer requires data trigger policy in a signal triggered module
    * so requesting to allow this trigger policy */
   result |= capi_congestion_buf_event_dt_in_st_cntr(me_ptr);

   return result;
}

/*==============================================================================
   Local Function Implementation
==============================================================================*/

capi_err_t capi_congestion_buf_set_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   if ((NULL == capi_ptr) || (NULL == proplist_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Set property failed. received bad pointer in set_property");
      return CAPI_EFAILED;
   }

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)capi_ptr;

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   if (NULL == prop_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Set property failed. received bad pointer in prop_ptr");
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
                      "congestion_buf: CAPI V2 FAILED Set Property id 0x%x Bad param size %u",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }
            capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;

            if (data_ptr->num_input_ports != CAPI_CONGESTION_BUF_MAX_INPUT_PORTS ||
                data_ptr->num_output_ports != CAPI_CONGESTION_BUF_MAX_OUTPUT_PORTS)
            {
               AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Set property failed. Num port info is not valid.");
               return CAPI_EFAILED;
            }

            break;
         }
         case CAPI_HEAP_ID:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_heap_id_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "congestion_buf:  Set Property failed. id= 0x%x Bad param size %u",
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
                      "congestion_buf: Set Property Failed. id= 0x%x Bad param size %u",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;

            me_ptr->event_cb_info.event_cb      = data_ptr->event_cb;
            me_ptr->event_cb_info.event_context = data_ptr->event_context;

            break;
         }
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               return CAPI_EFAILED;
            }

            if (!prop_ptr[i].port_info.is_valid)
            {
               AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Media format port info is invalid");
               return CAPI_ENEEDMORE;
            }

            // validate the MF payload
            if (payload_ptr->actual_data_len < sizeof(capi_cmn_raw_media_fmt_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Invalid media format size %d", payload_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            capi_cmn_raw_media_fmt_t *data_ptr = (capi_cmn_raw_media_fmt_t *)payload_ptr->data_ptr;

            if (NULL == data_ptr || (prop_ptr[i].port_info.is_valid && prop_ptr[i].port_info.port_index != 0) ||
                ((data_ptr->header.format_header.data_format != CAPI_RAW_COMPRESSED) &&
                 (data_ptr->header.format_header.data_format != CAPI_DEINTERLEAVED_RAW_COMPRESSED)))
            {
               AR_MSG(DBG_ERROR_PRIO, "congestion_buf: failed to set inp mf due to invalid/unexpected values");
               payload_ptr->actual_data_len = 0;
               capi_result |= CAPI_EFAILED;
               break;
            }

            /* If media format was already received before then clear it */
            if (me_ptr->is_input_mf_received)
            {
               me_ptr->is_deint         = 0;
               me_ptr->bitstream_format = 0;
               memset(&me_ptr->mf, 0, sizeof(capi_deint_mf_combined_t));
            }

            switch (data_ptr->header.format_header.data_format)
            {
               case CAPI_DEINTERLEAVED_RAW_COMPRESSED:
               {
                  me_ptr->is_deint = TRUE;

                  memscpy(&me_ptr->mf,
                          sizeof(capi_deint_mf_combined_t),
                          payload_ptr->data_ptr,
                          payload_ptr->actual_data_len);
                  AR_MSG(DBG_HIGH_PRIO,
                         "Input Media Format : Deinterleaved Raw Compressed - num bufs %u",
                         me_ptr->mf.deint_raw.bufs_num);
                  break;
               }
               case CAPI_RAW_COMPRESSED:
               {
                  /* Valid / Unset Bitstream Format */
                  me_ptr->bitstream_format = data_ptr->format.bitstream_format;
                  break;
               }
               default:
               {
                  AR_MSG(DBG_HIGH_PRIO,
                         "Set, failed to set Property id 0x%lx due to invalid/unexpected values",
                         (uint32_t)prop_ptr[i].id);
                  payload_ptr->actual_data_len = 0;
                  return (capi_result | CAPI_EFAILED);
               }
            }

            /* Input media format is received */
            me_ptr->is_input_mf_received = TRUE;

            capi_result |= congestion_buf_raise_output_media_format_event(me_ptr);

            /* TODO: Pending - Confirm these values*/
            capi_result |= capi_cmn_update_kpps_event(&me_ptr->event_cb_info, CONGESTION_BUFFER_KPPS);

            /* TODO: Pending - Confirm these values*/
            capi_result |= capi_cmn_update_bandwidth_event(&me_ptr->event_cb_info,
                                                           CONGESTION_BUFFER_CODE_BW,
                                                           CONGESTION_BUFFER_DATA_BW);

            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "congestion_buf: Set property failed. received un-supported prop_id 0x%x",
                   (uint32_t)prop_ptr[i].id);
            return CAPI_EUNSUPPORTED;
         }
      } /* Outer switch - Generic CAPI Properties */
   }    /* Loop all properties */

   return capi_result;
}

capi_err_t capi_congestion_buf_get_properties(capi_t *capi_ptr, capi_proplist_t *proplist_ptr)
{
   capi_err_t capi_result = CAPI_EOK;
   uint32_t   i;

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)capi_ptr;

   if (NULL == proplist_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Get properties received null arguments");
      return CAPI_EBADPARAM;
   }

   capi_prop_t *prop_ptr = proplist_ptr->prop_ptr;

   /* Frame work extensions required */
   /* TODO: Pending - check if multiport buffering is required */
   uint32_t fwk_extn_ids[CONGESTION_BUF_NUM_FRAMEWORK_EXTENSIONS] = { 0 };
   fwk_extn_ids[0]                                                = FWK_EXTN_MULTI_PORT_BUFFERING;
   fwk_extn_ids[1]                                                = FWK_EXTN_TRIGGER_POLICY;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = sizeof(capi_congestion_buf_t);
   mod_prop.stack_size         = CAPI_CONGESTION_BUF_MODULE_STACK_SIZE;
   mod_prop.num_fwk_extns      = CONGESTION_BUF_NUM_FRAMEWORK_EXTENSIONS;
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = FALSE; /* not capable of in-place processing of data */
   mod_prop.req_data_buffering = FALSE; /* Doesnt require data buffering. */
   mod_prop.max_metadata_size  = 0;     /* Not Applicable */

   capi_result = capi_cmn_get_basic_properties(proplist_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Get properties failed with result %lu", capi_result);
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
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         {
            // handled in capi common utils.
            break;
         }
         case CAPI_PORT_DATA_THRESHOLD:
         {
            break;
         }
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_output_media_format_size_t))
            {
               capi_output_media_format_size_t *data_ptr = (capi_output_media_format_size_t *)payload_ptr->data_ptr;

               if (NULL == me_ptr)
               {
                  AR_MSG(DBG_ERROR_PRIO, "Congestion buf : null me ptr");
                  break;
               }

               if (me_ptr->is_input_mf_received)
               {
                  /* If input media format is received then send what size is required accordingly */
                  if (!me_ptr->is_deint)
                  {
                     data_ptr->size_in_bytes = sizeof(capi_cmn_raw_media_fmt_t);
                  }
                  else
                  {
                     data_ptr->size_in_bytes = sizeof(capi_set_get_media_format_t) +
                                               sizeof(capi_deinterleaved_raw_compressed_data_format_t) +
                                               (me_ptr->mf.deint_raw.bufs_num * sizeof(capi_channel_mask_t));
                  }
               }
               else
               {
                  /* Else send the worst case size required */
                  data_ptr->size_in_bytes = sizeof(capi_deint_mf_combined_t);
               }

               payload_ptr->actual_data_len = sizeof(capi_output_media_format_size_t);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Congestion Buf:  Get, Param id 0x%lx Bad param size %lu",
                      (uint32_t)prop_ptr[i].id,
                      payload_ptr->max_data_len);

               payload_ptr->actual_data_len = 0; // actual len should be num of bytes read(set)/written(get)

               capi_result |= CAPI_ENEEDMORE;
            }

            break;
         }
         case CAPI_INTERFACE_EXTENSIONS:
         {
            capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
            capi_result |=
               ((payload_ptr->max_data_len < sizeof(capi_interface_extns_list_t)) ||
                (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                              (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t)))))
                  ? CAPI_ENEEDMORE
                  : capi_result;

            if (CAPI_FAILED(capi_result))
            {
               payload_ptr->actual_data_len = 0;
               AR_MSG(DBG_ERROR_PRIO, "Insufficient get property size.");
               break;
            }

            capi_interface_extn_desc_t *curr_intf_extn_desc_ptr =
               (capi_interface_extn_desc_t *)(payload_ptr->data_ptr + sizeof(capi_interface_extns_list_t));

            for (uint32_t j = 0; j < intf_ext_list->num_extensions; j++)
            {
               switch (curr_intf_extn_desc_ptr->id)
               {
                  case INTF_EXTN_IMCL:
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  case INTF_EXTN_METADATA:
                     curr_intf_extn_desc_ptr->is_supported = TRUE;
                     break;
                  default:
                  {
                     curr_intf_extn_desc_ptr->is_supported = FALSE;
                     break;
                  }
               }
               curr_intf_extn_desc_ptr++;
            }

            break;
         } // CAPI_INTERFACE_EXTENSIONS
         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Unknown Prop[%d]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      } // switch
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_congestion_buf_set_param
  Sets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_congestion_buf_set_param(capi_t *                capi_ptr,
                                                uint32_t                param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "congestion_buf: Set param failed. received bad property pointer for param_id property, 0x%x",
             param_id);
      return CAPI_EFAILED;
   }

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)((capi_ptr));

   switch (param_id)
   {
      case PARAM_ID_CONGESTION_BUF_CONFIG:
      {
         /* Check if param received is of the right size */
         if (params_ptr->actual_data_len < sizeof(param_id_congestion_buf_config_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "congestion_buf: Invalid payload size for param_id=0x%lx actual_data_len=%lu  ",
                   param_id,
                   params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }

         /* Copy the parameter into local config pointer */
         param_id_congestion_buf_config_t *cfg_ptr = (param_id_congestion_buf_config_t *)params_ptr->data_ptr;
         memscpy(&me_ptr->cfg_ptr,
                 sizeof(param_id_congestion_buf_config_t),
                 cfg_ptr,
                 sizeof(param_id_congestion_buf_config_t));

         AR_MSG(DBG_HIGH_PRIO,
                "congestion_buf: Received congestion buffer delay ms %u "
                "bit rate mode %u bit rate %u frame size mode %u frame size value %u",
                me_ptr->cfg_ptr.congestion_buffer_duration_ms,
                me_ptr->cfg_ptr.bit_rate_mode,
                me_ptr->cfg_ptr.bit_rate,
                me_ptr->cfg_ptr.frame_size_mode,
                me_ptr->cfg_ptr.frame_size_value);

         /* Initialize and Create the buffer*/
         result = capi_congestion_buf_init_create_buf(me_ptr, FALSE /* IS NOT DEBUG PARAM */);

         break;
      }
      case PARAM_ID_CONGESTION_BUF_SIZE_CONFIG:
      {
         /* This is a QACT debug buf size */
         if (params_ptr->actual_data_len < sizeof(param_id_congestion_buf_size_config_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "congestion_buf: Invalid payload size for param_id=0x%lx actual_data_len=%lu  ",
                   param_id,
                   params_ptr->actual_data_len);
            result = CAPI_ENEEDMORE;
            break;
         }

         param_id_congestion_buf_size_config_t *cfg_ptr = (param_id_congestion_buf_size_config_t *)params_ptr->data_ptr;

         /* Store the value and proceed */
         me_ptr->debug_ms = cfg_ptr->congestion_buffer_duration_ms;

         AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Received QACT congestion buffer delay ms", me_ptr->debug_ms);

         result = capi_congestion_buf_init_create_buf(me_ptr, TRUE /* IS DEBUG PARAM */);

         break;
      }
      case FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN:
      {
         if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Set param id 0x%lx, received null buffer", param_id);
            result |= CAPI_EBADPARAM;
            break;
         }

         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "congestion_buf: Invalid payload size for trigger policy %d",
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         fwk_extn_param_id_trigger_policy_cb_fn_t *payload_ptr =
            (fwk_extn_param_id_trigger_policy_cb_fn_t *)params_ptr->data_ptr;

         me_ptr->policy_chg_cb = *payload_ptr;

         /* This is a data triggered module in signal triggered container. The request for data
          * trigger policy is already raised during init.*/

         /* The data trigger for this module occurs when there is input or output available. */
         congestion_buf_change_trigger_policy(me_ptr);

         /* The signal trigger policy for this should be input mandatory output optional non-trig
          * so that the output buffer does not get reset due signal trigger call immediately after
          * data trigger in  gen_cntr_st_prepare_output_buffers_per_ext_out_port */
         congestion_buf_change_signal_trigger_policy(me_ptr);
         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "congestion_buf: Unsupported Param id= 0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
   }

   AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Set param= 0x%x done with result= 0x%x", param_id, result);
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_congestion_buf_get_param
  Gets either a parameter value or a parameter structure containing multiple
  parameters. In the event of a failure, the appropriate error code is
  returned.
 * -----------------------------------------------------------------------*/
capi_err_t capi_congestion_buf_get_param(capi_t *                capi_ptr,
                                                uint32_t                param_id,
                                                const capi_port_info_t *port_info_ptr,
                                                capi_buf_t *            params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == capi_ptr || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "congestion_buf: FAILED received bad property pointer for param_id property, 0x%x",
             param_id);
      return CAPI_EFAILED;
   }

   switch (param_id)
   {
      default:
      {
         AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Unsupported Param id= 0x%x \n", param_id);
         result = CAPI_EUNSUPPORTED;
      }
      break;
   }

   AR_MSG(DBG_HIGH_PRIO, "congestion_buf: Get param done for param id= 0x%x, result= 0x%x", param_id, result);
   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_congestion_buf_end
  Returns the library to the uninitialized state and frees the memory that
  was allocated by Init(). This function also frees the virtual function
  table.
 * -----------------------------------------------------------------------*/
capi_err_t capi_congestion_buf_end(capi_t *capi_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == capi_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "congestion_buf: received null pointer, %p", capi_ptr);
      return CAPI_EFAILED;
   }

   capi_congestion_buf_t *me_ptr = (capi_congestion_buf_t *)((capi_ptr));

   /* Deinit Congestion Buffer driver and destroy raw buffer */
   congestion_buf_driver_deinit(me_ptr);

   me_ptr->vtbl = NULL;

   return result;
}
