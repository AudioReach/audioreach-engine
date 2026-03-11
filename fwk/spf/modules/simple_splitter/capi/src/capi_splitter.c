/**
 * \file capi_splitter.c
 * \brief
 *     Source file to implement the CAPI Interface for Simple Splitter (SPLITTER) Module.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_splitter.h"
#include "capi_splitter_utils.h"
#include "spf_list_utils.h"

capi_err_t capi_splitter_check_and_raise_dynamic_inplace(capi_splitter_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   bool_t can_be_inplace = FALSE;
   uint32_t num_opened_output_ports = 0, num_active_output_ports = 0;
   for(uint32_t i = 0; i < me_ptr->num_out_ports ; i++)
   {
      if(DATA_PORT_STATE_CLOSED != me_ptr->out_port_state_arr[i].state)
      {
         num_opened_output_ports++;
      }

      if(DATA_PORT_STATE_STARTED == me_ptr->out_port_state_arr[i].state)
      {
         num_active_output_ports++;
      }
   }

   // num of opened and started ports must be one to be inplace
   if((num_opened_output_ports == 1) && (num_active_output_ports == 1))
   {
      can_be_inplace = TRUE;
   }

   if(can_be_inplace != me_ptr->flags.is_inplace)
   {
      capi_result = capi_cmn_raise_dynamic_inplace_event(&me_ptr->cb_info, can_be_inplace);
      if(CAPI_FAILED(capi_result))
      {
         me_ptr->flags.is_inplace = FALSE;
      }
      else
      {
         me_ptr->flags.is_inplace = can_be_inplace;
      }
   }

   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_splitter_get_static_properties
  DESCRIPTION: Function to get the static properties of ss module
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties)
{
   capi_err_t capi_result = CAPI_EOK;
   //AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Enter Get static Properties");
   if (NULL != static_properties)
   {
      capi_result |= capi_splitter_get_properties((capi_t *)NULL, static_properties);
   }
   return capi_result;
}

/*------------------------------------------------------------------------
  Function name: capi_splitter_init
  DESCRIPTION: Initialize the CAPIv2 ss module and library.
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_init(capi_t *_pif, capi_proplist_t *init_set_properties)
{
   if ((NULL == _pif) || (NULL == init_set_properties))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Init received NULL pointer");
      return CAPI_EBADPARAM;
   }
   // AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Initializing");

   capi_splitter_t *me_ptr = (capi_splitter_t *)_pif;
   memset(me_ptr, 0, sizeof(capi_splitter_t));
   me_ptr->vtbl.vtbl_ptr = capi_splitter_get_vtbl(); // assigning the vtbl with all function pointers

   // init oprating mf
   capi_cmn_init_media_fmt_v2(&me_ptr->operating_mf);

   // AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Setting Init Properties");
   //  should contain EVENT_CALLBACK_INFO
   capi_err_t result = capi_splitter_set_properties((capi_t *)me_ptr, init_set_properties);

   result |= capi_cmn_raise_deinterleaved_unpacked_v2_supported_event(&me_ptr->cb_info);

   return result;
}

/*------------------------------------------------------------------------
  Function name: capi_splitter_end
  DESCRIPTION: Returns the library to the uninitialized state and frees the
  memory that was allocated by module. This function also frees the virtual
  function table.
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_end(capi_t *_pif)
{
   capi_err_t       capi_result = CAPI_EOK;
   capi_splitter_t *me_ptr      = (capi_splitter_t *)_pif;
   if (NULL == me_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: End received bad pointer, 0x%p", me_ptr);
      return CAPI_EBADPARAM;
   }
   if (NULL != me_ptr->out_port_state_arr)
   {
      posal_memory_free((void *)me_ptr->out_port_state_arr);
   }

   if (NULL != me_ptr->out_port_md_prop_cfg_ptr)
   {
      posal_memory_free((void *)me_ptr->out_port_md_prop_cfg_ptr);
   }


   if (NULL != me_ptr->out_port_ts_prop_cfg_ptr)
   {
      posal_memory_free((void *)me_ptr->out_port_ts_prop_cfg_ptr);
   }

   me_ptr->vtbl.vtbl_ptr = NULL;

#ifdef SPLITTER_DBG_LOW
   AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Module ended");
#endif // SPLITTER_DBG_LOW
   return capi_result;
}
/*------------------------------------------------------------------------
  Function name: capi_splitter_set_param
  DESCRIPTION: Function to set parameter value\structure.
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_set_param(capi_t *                _pif,
                                   uint32_t                param_id,
                                   const capi_port_info_t *port_info_ptr,
                                   capi_buf_t *            params_ptr)
{
   capi_err_t       capi_result = CAPI_EOK;
   capi_splitter_t *me_ptr      = (capi_splitter_t *)(_pif);
   if (NULL == _pif || NULL == params_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: set param received bad pointer, 0x%p, 0x%p", _pif, params_ptr);
      return CAPI_EBADPARAM;
   }
   switch (param_id)
   {
      case PARAM_ID_SPLITTER_TIMESTAMP_PROP_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_splitter_timestamp_prop_cfg_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_splitter: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_splitter_timestamp_prop_cfg_t *payload_ptr =
            (param_id_splitter_timestamp_prop_cfg_t *)params_ptr->data_ptr;
         uint32_t required_data_len = sizeof(param_id_splitter_timestamp_prop_cfg_t);

         // validate the param len and get the actual required data len
         {
            if (params_ptr->actual_data_len < required_data_len)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Param id 0x%lx Bad param size %lu",
                      (uint32_t)param_id,
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            if (0 == payload_ptr->num_ports)
            {
               return AR_EOK;
            }

            required_data_len += (payload_ptr->num_ports * sizeof(per_port_ts_cfg_t));

            if (params_ptr->actual_data_len < required_data_len)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Param id 0x%lx Bad param size %lu",
                      (uint32_t)param_id,
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }
         }

         if (me_ptr->out_port_ts_prop_cfg_ptr)
         {
            posal_memory_free(me_ptr->out_port_ts_prop_cfg_ptr);
            me_ptr->out_port_ts_prop_cfg_ptr = NULL;
         }

         me_ptr->out_port_ts_prop_cfg_ptr =
            (param_id_splitter_timestamp_prop_cfg_t *)posal_memory_malloc(required_data_len,
                                                                         (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
         if (NULL == me_ptr->out_port_ts_prop_cfg_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Malloc Fail while allocating out_port_cache_arr");
            capi_result |= CAPI_ENOMEMORY;
            break;
         }

         // cache the ts config
         memscpy(me_ptr->out_port_ts_prop_cfg_ptr,
                 required_data_len,
                 params_ptr->data_ptr,
                 params_ptr->actual_data_len);

         capi_splitter_update_all_opened_port_ts_config_flag(me_ptr);

#ifdef SPLITTER_DBG_LOW
         AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Received Timestamp Config");
#endif // SPLITTER_DBG_LOW
         break;
      }
      case PARAM_ID_SPLITTER_METADATA_PROP_CFG:
      {
         if (params_ptr->actual_data_len < sizeof(param_id_splitter_metadata_prop_cfg_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_splitter: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         param_id_splitter_metadata_prop_cfg_t *payload_ptr =
            (param_id_splitter_metadata_prop_cfg_t *)params_ptr->data_ptr;
         uint32_t required_data_len = sizeof(param_id_splitter_metadata_prop_cfg_t);

         // validate the param len and get the actual required data len
         {
            if (params_ptr->actual_data_len < required_data_len)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Param id 0x%lx Bad param size %lu",
                      (uint32_t)param_id,
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            if (0 == payload_ptr->num_ports)
            {
               return AR_EOK;
            }
            for (uint32_t i = 0; i < payload_ptr->num_ports; i++)
            {
               if (params_ptr->actual_data_len < required_data_len + sizeof(per_port_md_cfg_t))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_splitter: Param id 0x%lx Bad param size %lu",
                         (uint32_t)param_id,
                         params_ptr->actual_data_len);
                  return CAPI_ENEEDMORE;
               }

               per_port_md_cfg_t *per_port_md_cfg_ptr = (per_port_md_cfg_t *)(params_ptr->data_ptr + required_data_len);

               required_data_len +=
                  sizeof(per_port_md_cfg_t) + (per_port_md_cfg_ptr->num_white_listed_md * sizeof(uint32_t));
            }

            if (params_ptr->actual_data_len < required_data_len)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Param id 0x%lx Bad param size %lu",
                      (uint32_t)param_id,
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }
         }

         if (me_ptr->out_port_md_prop_cfg_ptr)
         {
            posal_memory_free(me_ptr->out_port_md_prop_cfg_ptr);
            me_ptr->out_port_md_prop_cfg_ptr = NULL;
         }

         me_ptr->out_port_md_prop_cfg_ptr =
            (param_id_splitter_metadata_prop_cfg_t *)posal_memory_malloc(required_data_len,
                                                                         (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
         if (NULL == me_ptr->out_port_md_prop_cfg_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Malloc Fail while allocating out_port_cache_arr");
            capi_result |= CAPI_ENOMEMORY;
            break;
         }

         //cache the md wl info
         memscpy(me_ptr->out_port_md_prop_cfg_ptr,
                 required_data_len,
                 params_ptr->data_ptr,
                 params_ptr->actual_data_len);

         capi_splitter_check_if_any_port_is_open_and_update_eos_flag(me_ptr);

#ifdef SPLITTER_DBG_LOW
         AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Received MD Config");
#endif // SPLITTER_DBG_LOW
         break;
      }
      case FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN:
      {
         if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Param id 0x%lx received null buffer", (uint32_t)param_id);
            capi_result |= CAPI_EBADPARAM;
            break;
         }

         if (params_ptr->actual_data_len < sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_splitter: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }

         fwk_extn_param_id_trigger_policy_cb_fn_t *payload_ptr =
            (fwk_extn_param_id_trigger_policy_cb_fn_t *)params_ptr->data_ptr;
         me_ptr->tgp.tg_policy_cb = *payload_ptr;

         break;
      }
      case INTF_EXTN_PARAM_ID_METADATA_HANDLER:
      {
         if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_metadata_handler_t))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "capi_splitter: Param id 0x%lx Bad param size %lu",
                   (uint32_t)param_id,
                   params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         intf_extn_param_id_metadata_handler_t *payload_ptr =
            (intf_extn_param_id_metadata_handler_t *)params_ptr->data_ptr;
         me_ptr->metadata_handler = *payload_ptr;
         break;
      }
      case INTF_EXTN_PARAM_ID_DATA_PORT_OPERATION:
      {
         if (NULL == me_ptr->out_port_state_arr)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Port structures memory should have been allocated for max ports during OPEN. Error");
            capi_result |= CAPI_EFAILED;
            break;
         }
         if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_splitter: payload size for port operation %d", params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
         if (NULL == params_ptr->data_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set property id 0x%lx, received null buffer", param_id);
            capi_result |= CAPI_EBADPARAM;
            break;
         }
         intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);
         if (params_ptr->actual_data_len <
             sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t)))
         {
            AR_MSG(DBG_ERROR_PRIO, "capi_splitter: payload size for port operation %d", params_ptr->actual_data_len);
            capi_result |= CAPI_ENEEDMORE;
            break;
         }
		 
		 /* We perform bookkeeping for input as well as output ports in the splitter, 
		  * to track port states, which are later referenced during TGP raise handling
          * This is because there can be multiple ports on either side. In general,
          * we do bookkeeping for whichever side (or both) has multi-port 
		  * capability */
         if (data_ptr->is_input_port)
         {
              switch (data_ptr->opcode)
               {
                  case INTF_EXTN_DATA_PORT_CLOSE:
                  {
                        if (1 == data_ptr->num_ports && 0 == data_ptr->id_idx[0].port_index)
                        {
                            me_ptr->flags.is_ds_rt = FALSE;
                            me_ptr->flags.is_us_rt = FALSE;
                        }
                        break;
                  }
				  case INTF_EXTN_DATA_PORT_OPEN:
                  { 
                       #ifdef SPLITTER_DBG_LOW
                           AR_MSG(DBG_HIGH_PRIO,"capi_splitter: Opening input Port");
                       #endif // SPLITTER_DBG_LOW
                       me_ptr->input_port_state =  DATA_PORT_STATE_OPENED;
                       break;
                  }
                  case INTF_EXTN_DATA_PORT_START:
                  { 
                       #ifdef SPLITTER_DBG_LOW
                           AR_MSG(DBG_HIGH_PRIO,"capi_splitter: Starting input Port");
                       #endif // SPLITTER_DBG_LOW
                       me_ptr->input_port_state =  DATA_PORT_STATE_STARTED;
                       break;
                  }
                  case INTF_EXTN_DATA_PORT_STOP:
                  {
                      #ifdef SPLITTER_DBG_LOW
                          AR_MSG(DBG_HIGH_PRIO,"capi_splitter: Stopping input Port");
                      #endif // SPLITTER_DBG_LOW
                      me_ptr->input_port_state =  DATA_PORT_STATE_STOPPED;
                      break;
                  }
                  default:
                  {
                      AR_MSG(DBG_ERROR_PRIO,"capi_splitter: Port operation - Unsupported opcode: %lu",
                            data_ptr->opcode);
                     CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
                     break;
                  }
            }
         }
         else
         {
            // output port
            if (data_ptr->num_ports > me_ptr->num_out_ports)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Bad parameter, num_ports %d", data_ptr->num_ports);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            for (uint32_t i = 0; i < data_ptr->num_ports; i++)
            {
               if (data_ptr->id_idx[i].port_index >= me_ptr->num_out_ports)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_splitter: Bad parameter in id-idx map on port %lu, port_index = %lu num ports = "
                         "%d",
                         i,
                         data_ptr->id_idx[i].port_index,
                         me_ptr->num_out_ports);
                  capi_result |= CAPI_EBADPARAM;
                  break; // breaks out of for
               }
               // save the port_id at the port_index (array of size max ports)
               switch (data_ptr->opcode)
               {
                  case INTF_EXTN_DATA_PORT_OPEN:
                  {
                     // don't need specific payload
                     me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index].port_id = data_ptr->id_idx[i].port_id;
                     me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index].state   = DATA_PORT_STATE_OPENED;
#ifdef SPLITTER_DBG_LOW
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_splitter: Opening Port ID %lu, Index %lu",
                            data_ptr->id_idx[i].port_id,
                            data_ptr->id_idx[i].port_index);
#endif // SPLITTER_DBG_LOW
                     capi_splitter_update_port_md_flag(me_ptr, data_ptr->id_idx[i].port_index);
                     capi_splitter_update_port_ts_flag(me_ptr, data_ptr->id_idx[i].port_index);
                     if (me_ptr->flags.is_in_media_fmt_set)
                     {
                        capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info,
                                                                          &me_ptr->operating_mf,
                                                                          FALSE,
                                                                          data_ptr->id_idx[i].port_index);
                     }
                     break;
                  }
                  case INTF_EXTN_DATA_PORT_CLOSE:
                  {
#ifdef SPLITTER_DBG_LOW
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_splitter: Closing Port ID %lu, Index %lu",
                            data_ptr->id_idx[i].port_id,
                            data_ptr->id_idx[i].port_index);
#endif // SPLITTER_DBG_LOW

                     memset(&me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index],
                            0,
                            sizeof(me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index]));
                     break;
                  }
                  case INTF_EXTN_DATA_PORT_START:
                  {
#ifdef SPLITTER_DBG_LOW
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_splitter: Starting Port ID %lu, Index %lu",
                            data_ptr->id_idx[i].port_id,
                            data_ptr->id_idx[i].port_index);
#endif // SPLITTER_DBG_LOW
                     // don't need specific payload
                     me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index].state = DATA_PORT_STATE_STARTED;
                     break;
                  }
                  case INTF_EXTN_DATA_PORT_STOP:
                  {
#ifdef SPLITTER_DBG_LOW
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_splitter: Stopping Port ID %lu, Index %lu",
                            data_ptr->id_idx[i].port_id,
                            data_ptr->id_idx[i].port_index);
#endif // SPLITTER_DBG_LOW
                     // don't need specific payload
                     me_ptr->out_port_state_arr[data_ptr->id_idx[i].port_index].state = DATA_PORT_STATE_STOPPED;
                     break;
                  }
                  default:
                  {
                     AR_MSG(DBG_ERROR_PRIO,
                            "capi_splitter: Port operation - Unsupported opcode: %lu",
                            data_ptr->opcode);
                     CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
                     break;
                  }
               }
            }

            // if num of opened port is one and num started port is one moduel can be
            capi_splitter_check_and_raise_dynamic_inplace(me_ptr);

            // since port state changed, update port property based on the active output ports.
            capi_splitter_update_is_rt_property(me_ptr);
         }
         break;
      } // CAPI_PORT_OPERATION
      case INTF_EXTN_PARAM_ID_IS_RT_PORT_PROPERTY:
      {
         capi_result |= capi_splitter_set_data_port_property(me_ptr, params_ptr);
         break;
      } // CAPI_PORT_OPERATION
      case INTF_EXTN_PARAM_ID_STM_TS:
      {
          if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_stm_ts_t))
          {
             AR_MSG(DBG_ERROR_PRIO,
                    "capi_splitter: Param id 0x%lx Bad param size %lu",
                    (uint32_t)param_id,
                    params_ptr->actual_data_len);
             capi_result |= CAPI_ENEEDMORE;
             break;
          }
          intf_extn_param_id_stm_ts_t *payload_ptr =
             (intf_extn_param_id_stm_ts_t *)params_ptr->data_ptr;
          me_ptr->ts_payload = *payload_ptr;
          break;
      }

      default:
      {
         capi_result = CAPI_EUNSUPPORTED;
      }
   }
   return capi_result;
}
/*------------------------------------------------------------------------
  Function name: capi_splitter_get_param
  DESCRIPTION: Function to get parameter value\structure.
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_get_param(capi_t *                _pif,
                                   uint32_t                param_id,
                                   const capi_port_info_t *port_info_ptr,
                                   capi_buf_t *            params_ptr)
{
   return CAPI_EUNSUPPORTED;
}
/*------------------------------------------------------------------------
  Function name: capi_splitter_set_properties
  DESCRIPTION: Function to set properties to the CHANNEL SPLITTER module
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_set_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t       capi_result = CAPI_EOK;
   capi_splitter_t *me_ptr      = (capi_splitter_t *)_pif;

   if ((NULL == props_ptr) || (NULL == me_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Get property received null arguments");
      return CAPI_EBADPARAM;
   }

   capi_prop_t *prop_ptr = props_ptr->prop_ptr;
   // iterate over the properties
   for (uint32_t i = 0; i < props_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;
      switch (prop_ptr[i].id)
      {
         case CAPI_HEAP_ID:
         {
            if (payload_ptr->max_data_len < sizeof(capi_heap_id_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Set property id 0x%lx, Bad param size %lu",
                      prop_ptr[i].id,
                      payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            capi_heap_id_t *data_ptr = (capi_heap_id_t *)payload_ptr->data_ptr;
            me_ptr->heap_mem.heap_id = data_ptr->heap_id;
            break;
         } // CAPI_HEAP_ID
         case CAPI_EVENT_CALLBACK_INFO:
         {
            if (payload_ptr->max_data_len < sizeof(capi_event_callback_info_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Set property id 0x%lx, Bad param size %lu",
                      prop_ptr[i].id,
                      payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            capi_event_callback_info_t *data_ptr = (capi_event_callback_info_t *)payload_ptr->data_ptr;
            me_ptr->cb_info.event_cb             = data_ptr->event_cb;
            me_ptr->cb_info.event_context        = data_ptr->event_context;
            payload_ptr->actual_data_len         = sizeof(capi_event_callback_info_t);
            break;
         } // CAPI_EVENT_CALLBACK_INFO
         case CAPI_MODULE_INSTANCE_ID:
         {
            if (payload_ptr->actual_data_len >= sizeof(capi_module_instance_id_t))
            {
               capi_module_instance_id_t *data_ptr = (capi_module_instance_id_t *)payload_ptr->data_ptr;
               me_ptr->miid                        = data_ptr->module_instance_id;
               AR_MSG(DBG_LOW_PRIO,
                      "capi_splitter: This module-id 0x%08lX, instance-id 0x%08lX",
                      data_ptr->module_id,
                      me_ptr->miid);
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Set property id 0x%lx, Bad param size %lu",
                      prop_ptr[i].id,
                      payload_ptr->max_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            break;
         } // CAPI_MODULE_INSTANCE_ID
         case CAPI_PORT_NUM_INFO:
         {
            if (payload_ptr->actual_data_len < sizeof(capi_port_num_info_t))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Set property id 0x%lx, Bad param size %lu",
                      prop_ptr[i].id,
                      payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            capi_port_num_info_t *data_ptr = (capi_port_num_info_t *)payload_ptr->data_ptr;
            if (SPLITTER_MAX_INPUT_PORTS < data_ptr->num_input_ports)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Set property num port info - out of range, provided num input ports = %lu, "
                      "num "
                      "output ports = %lu",
                      data_ptr->num_input_ports,
                      data_ptr->num_output_ports);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            // max number of output ports
            me_ptr->num_out_ports = data_ptr->num_output_ports;

#ifdef SPLITTER_DBG_LOW
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_splitter: Port num info set prop: num input ports = 1 and num output ports = %lu",
                   me_ptr->num_out_ports);
#endif // SPLITTER_DBG_LOW

            // allocating memory for the bool array for max output ports
            me_ptr->out_port_state_arr = (capi_splitter_out_port_state_t *)
               posal_memory_malloc(me_ptr->num_out_ports *
                                      (CAPI_ALIGN_8_BYTE(sizeof(capi_splitter_out_port_state_t)) +
                                       CAPI_ALIGN_8_BYTE(sizeof(fwk_extn_port_trigger_affinity_t)) +
                                       CAPI_ALIGN_8_BYTE(sizeof(fwk_extn_port_nontrigger_policy_t))),
                                   (POSAL_HEAP_ID)me_ptr->heap_mem.heap_id);
            if (NULL == me_ptr->out_port_state_arr)
            {
               AR_MSG(DBG_ERROR_PRIO, "Malloc Fail while allocating out_port_state_arr");
               capi_result |= CAPI_ENOMEMORY;
               break;
            }
            // false (inactive) intialize
            memset(me_ptr->out_port_state_arr, 0, me_ptr->num_out_ports * sizeof(capi_splitter_out_port_state_t));

            me_ptr->tgp.out_port_triggerable_affinity_arr = (fwk_extn_port_trigger_affinity_t *)CAPI_ALIGN_8_BYTE(
               me_ptr->out_port_state_arr + me_ptr->num_out_ports);

            me_ptr->tgp.out_port_nontriggerable_policy_arr = (fwk_extn_port_nontrigger_policy_t *)CAPI_ALIGN_8_BYTE(
               me_ptr->tgp.out_port_triggerable_affinity_arr + me_ptr->num_out_ports);

            break;
         } // CAPI_PORT_NUM_INFO
         case CAPI_ALGORITHMIC_RESET:
         {
            //AR_MSG(DBG_HIGH_PRIO, "capi_splitter: Reset, nothing to be done");
            break;
         } // CAPI_ALGORITHMIC_RESET
         case CAPI_INPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr->out_port_state_arr)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "Port structures memory should have been allocated for max ports during OPEN. Error");
               capi_result |= CAPI_EFAILED;
               break;
            }
            /* Validate the MF payload */
            if (payload_ptr->actual_data_len <
                sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Invalid media format size %d", payload_ptr->actual_data_len);
               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            if (NULL == payload_ptr->data_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Set property id 0x%lx, received null buffer", prop_ptr[i].id);
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
            // /* Validate data format, interleaving and num channels */
            if ((CAPI_RAW_COMPRESSED == media_fmt_ptr->header.format_header.data_format) ||
                (CAPI_MAX_CHANNELS_V2 < media_fmt_ptr->format.num_channels))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: Unsupported Data format %lu or num_channels %lu. Max channels: %lu",
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
            me_ptr->flags.is_in_media_fmt_set = TRUE;
            for (uint32_t i = 0; i < me_ptr->num_out_ports; i++)
            {
               if (DATA_PORT_STATE_CLOSED == me_ptr->out_port_state_arr[i].state)
               {
                  continue;
               }
               capi_result |= capi_cmn_output_media_fmt_event_v2(&me_ptr->cb_info, &me_ptr->operating_mf, FALSE, i);
            }
            capi_result |= capi_splitter_update_and_raise_kpps_bw_event(me_ptr);
#ifdef SPLITTER_DBG_LOW
            AR_MSG(DBG_HIGH_PRIO,
                   "capi_splitter: Input Media format set prop: bits per sample: %lu bytes, num in/out channels %lu",
                   me_ptr->operating_mf.format.bits_per_sample,
                   me_ptr->operating_mf.format.num_channels);
#endif // SPLITTER_DBG_LOW
            break;
         } // CAPI_INPUT_MEDIA_FORMAT_V2
         default:
         {
            //AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Unknown Prop[%d]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
   } // for loop
   return capi_result;
}
/*------------------------------------------------------------------------
  Function name: capi_splitter_get_properties
  DESCRIPTION: Function to get the properties from the CHANNEL SPLITTER module
  -----------------------------------------------------------------------*/
capi_err_t capi_splitter_get_properties(capi_t *_pif, capi_proplist_t *props_ptr)
{
   capi_err_t       capi_result = CAPI_EOK;
   capi_splitter_t *me_ptr      = (capi_splitter_t *)_pif;
   uint32_t         i;
   uint32_t         fwk_extn_ids[] = { FWK_EXTN_TRIGGER_POLICY };

   if (NULL == props_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Get property received null property array");
      return CAPI_EBADPARAM;
   }
   capi_prop_t *prop_ptr = props_ptr->prop_ptr;

   capi_basic_prop_t mod_prop;
   mod_prop.init_memory_req    = sizeof(capi_splitter_t);
   mod_prop.stack_size         = CAPI_SPLITTER_STACK_SIZE;
   mod_prop.num_fwk_extns      = sizeof(fwk_extn_ids) / sizeof(fwk_extn_ids[0]);
   mod_prop.fwk_extn_ids_arr   = fwk_extn_ids;
   mod_prop.is_inplace         = FALSE; // not capable of in-place processing of data
   mod_prop.req_data_buffering = FALSE;
   mod_prop.max_metadata_size  = 0; // NA

   capi_result = capi_cmn_get_basic_properties(props_ptr, &mod_prop);
   if (CAPI_EOK != capi_result)
   {
      AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Get common basic properties failed with result %lu", capi_result);
      return capi_result;
   }

   // iterating over the properties
   for (i = 0; i < props_ptr->props_num; i++)
   {
      capi_buf_t *payload_ptr = &prop_ptr[i].payload;

      switch (prop_ptr[i].id)
      {
         // begin static props
         case CAPI_INIT_MEMORY_REQUIREMENT:
         case CAPI_STACK_SIZE:
         case CAPI_IS_INPLACE:
         case CAPI_REQUIRES_DATA_BUFFERING:
         case CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_NEEDED_FRAMEWORK_EXTENSIONS:
         case CAPI_PORT_DATA_THRESHOLD: // ignore this.
         case CAPI_OUTPUT_MEDIA_FORMAT_SIZE:
         {
            break;
         }
         // end static props
         case CAPI_OUTPUT_MEDIA_FORMAT_V2:
         {
            if (NULL == me_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: pif is NULL for get OUTPUT MF");
               capi_result |= CAPI_EBADPARAM;
               break;
            }
            uint32_t ret_size = sizeof(capi_set_get_media_format_t) + sizeof(capi_standard_data_format_v2_t) +
                                (me_ptr->operating_mf.format.num_channels * sizeof(capi_channel_type_t));
            /* Validate the MF payload */
            if (payload_ptr->max_data_len < sizeof(capi_media_fmt_v2_t))
            {
               AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Invalid media format size %d", payload_ptr->max_data_len);

               capi_result |= CAPI_ENEEDMORE;
               break;
            }
            capi_media_fmt_v2_t *media_fmt_ptr = (capi_media_fmt_v2_t *)(payload_ptr->data_ptr);
            memscpy(media_fmt_ptr, ret_size, &me_ptr->operating_mf, ret_size);
            payload_ptr->actual_data_len = sizeof(capi_media_fmt_v2_t);
            break;
         } // CAPI_OUTPUT_MEDIA_FORMAT_V2
         case CAPI_INTERFACE_EXTENSIONS:
         {
            if (payload_ptr->max_data_len >= sizeof(capi_interface_extns_list_t))
            {
               capi_interface_extns_list_t *intf_ext_list = (capi_interface_extns_list_t *)payload_ptr->data_ptr;
               if (payload_ptr->max_data_len < (sizeof(capi_interface_extns_list_t) +
                                                (intf_ext_list->num_extensions * sizeof(capi_interface_extn_desc_t))))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "capi_splitter: CAPI_INTERFACE_EXTENSIONS invalid param size %lu",
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
                        case INTF_EXTN_DATA_PORT_OPERATION:
                        case INTF_EXTN_PROP_IS_RT_PORT_PROPERTY:
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
#if 0
                     AR_MSG(DBG_HIGH_PRIO,
                            "capi_splitter: CAPI_INTERFACE_EXTENSIONS intf_ext = 0x%lx, is_supported = %d",
                            curr_intf_extn_desc_ptr->id,
                            (int)curr_intf_extn_desc_ptr->is_supported);
#endif
                     curr_intf_extn_desc_ptr++;
                  }
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "capi_splitter: CAPI_INTERFACE_EXTENSIONS Bad param size %lu",
                      payload_ptr->max_data_len);
               CAPI_SET_ERROR(capi_result, CAPI_ENEEDMORE);
            }
            break;
         }
         default:
         {
            //AR_MSG(DBG_ERROR_PRIO, "capi_splitter: Unknown Prop[0x%lX]", prop_ptr[i].id);
            capi_result |= CAPI_EUNSUPPORTED;
            break;
         }
      }
   }
   return capi_result;
}
