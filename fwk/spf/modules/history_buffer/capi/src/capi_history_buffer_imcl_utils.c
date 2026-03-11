/**
 * \file capi_history_buffer_imcl_utils.c
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

/**----------------------------------------------------------------------------
 ** Include Files
 ** -------------------------------------------------------------------------*/

#include "capi_history_buffer_i.h"
#include "capi_intf_extn_imcl.h"

/* =========================================================================
 * FUNCTION : history_buffer_get_ctrl_port_index
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t history_buffer_get_ctrl_port_index(capi_history_buffer_t *me_ptr, uint32_t ctrl_port_id, uint32_t *ctrl_port_idx_ptr)
{
   uint32_t available_ctrl_arr_idx = HISTORY_BUFFER_MAX_CONTROL_PORTS;
   for (uint32_t idx = 0; idx < HISTORY_BUFFER_MAX_CONTROL_PORTS; idx++)
   {
      if (ctrl_port_id == me_ptr->ctrl_port_info[idx].port_id)
      {
         *ctrl_port_idx_ptr = idx;
         return AR_EOK;
      }
      else if (0 == me_ptr->ctrl_port_info[idx].port_id)
      {
         available_ctrl_arr_idx = idx;
      }
   }

   if (available_ctrl_arr_idx != HISTORY_BUFFER_MAX_CONTROL_PORTS)
   {
      DE_DBG(me_ptr->miid,
             DBG_HIGH_PRIO,
             "mapping control port ID = 0x%x to index 0x%x",
             ctrl_port_id,
             available_ctrl_arr_idx);

      me_ptr->ctrl_port_info[available_ctrl_arr_idx].port_id = ctrl_port_id;
      *ctrl_port_idx_ptr                                     = available_ctrl_arr_idx;
      return AR_EOK;
   }

   DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Ctrl Port ID = %lu to index mapping not found.", ctrl_port_id);
   *ctrl_port_idx_ptr = 0xFFFFFFFF;
   return CAPI_EFAILED;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_intf_extn_ctrl_port_operation
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_intf_extn_ctrl_port_operation(capi_history_buffer_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t result = CAPI_EOK;
   if (NULL == params_ptr->data_ptr)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Control port operation received null buffer");
      CAPI_SET_ERROR(result, CAPI_EBADPARAM);
      return result;
   }
   if (params_ptr->actual_data_len < sizeof(intf_extn_param_id_imcl_port_operation_t))
   {
      DE_DBG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "Invalid payload size for ctrl port operation %d",
             params_ptr->actual_data_len);
      CAPI_SET_ERROR(result, CAPI_ENEEDMORE);
      return result;
   }

   intf_extn_param_id_imcl_port_operation_t *port_op_ptr =
      (intf_extn_param_id_imcl_port_operation_t *)(params_ptr->data_ptr);

   switch (port_op_ptr->opcode)
   {
   case INTF_EXTN_IMCL_PORT_OPEN:
   {
      if (port_op_ptr->op_payload.data_ptr)
      {
         intf_extn_imcl_port_open_t *port_open_ptr = (intf_extn_imcl_port_open_t *)port_op_ptr->op_payload.data_ptr;

         /*Size Validation*/
         uint32_t num_ports = port_open_ptr->num_ports;

         /* Check validity of the control port count. More than max cannot be active */
         if (num_ports + me_ptr->num_opened_ctrl_ports > HISTORY_BUFFER_MAX_CONTROL_PORTS)
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Only supports a max of %lu control ports. Trying to open %lu",
                   HISTORY_BUFFER_MAX_CONTROL_PORTS,
                   num_ports + me_ptr->num_opened_ctrl_ports);
            return CAPI_EBADPARAM;
         }

        /* Validate the open operation payload size */
         uint32_t valid_size =
            sizeof(intf_extn_imcl_port_open_t) + (num_ports * sizeof(intf_extn_imcl_id_intent_map_t));
         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO,
                   "Invalid payload size for ctrl port OPEN %d",
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         // Iterate through each port and handle open operation.
         // cache port_id, port_index and list of intents supported by the control port.
         for (uint32_t port_iter = 0; port_iter < num_ports; port_iter++)
         {
            /* Sanity check: Size Validation */
            valid_size += (port_open_ptr->intent_map[port_iter].num_intents * sizeof(uint32_t));
            if (port_op_ptr->op_payload.actual_data_len < valid_size)
            {
               DE_DBG(me_ptr->miid, DBG_ERROR_PRIO,
                      "Invalid payload size for ctrl port open %d",
                      params_ptr->actual_data_len);
               return CAPI_ENEEDMORE;
            }

            /*  Sanity check: Check validity of number of intents per control link */
            if (port_open_ptr->intent_map[port_iter].num_intents > HISTORY_BUFFER_MAX_INTENTS_PER_CTRL_PORT)
            {
               DE_DBG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Number of intents=%lu is not supported per control port. ",
                      port_open_ptr->intent_map[port_iter].num_intents);
               return CAPI_ENEEDMORE;
            }

            uint32_t ctrl_port_id = port_open_ptr->intent_map[port_iter].port_id;

            /* Module must assign an available control port index */
            uint32_t ctrl_port_idx =0;
            if (AR_EOK != history_buffer_get_ctrl_port_index(me_ptr, ctrl_port_id, &ctrl_port_idx))
            {
               return CAPI_EFAILED;
            }

            /* Sanity check: If the port is already opened return failure */
            if (me_ptr->ctrl_port_info[ctrl_port_idx].state != CTRL_PORT_CLOSED)
            {
               DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Control port id: 0x%lx is already opened. ", ctrl_port_id);
               return CAPI_EFAILED;
            }

            /* Check validity of intent id list for each port.*/
            for (uint32_t intent_iter = 0; intent_iter < port_open_ptr->intent_map[port_iter].num_intents;
                 intent_iter++)
            {
               if (!capi_history_buffer_validate_intent_id(me_ptr,
                                                      port_open_ptr->intent_map[port_iter].intent_arr[intent_iter]))
               {
                  DE_DBG(me_ptr->miid,
                         DBG_ERROR_PRIO,
                         "Opening Control port id: 0x%x with unsupported intent ID: 0x%x ",
                         ctrl_port_id,
                         port_open_ptr->intent_map[port_iter].intent_arr[intent_iter]);
                  return CAPI_EFAILED;
               }

               DE_DBG(me_ptr->miid,
                      DBG_MED_PRIO,
                      "Opening Control port id: 0x%x with intent ID: 0x%x ",
                      ctrl_port_id,
                      port_open_ptr->intent_map[port_iter].intent_arr[intent_iter]);

               /* Cache intent id list per control port. */
               me_ptr->ctrl_port_info[ctrl_port_idx].intent_list_arr[intent_iter] =
                  port_open_ptr->intent_map[port_iter].intent_arr[intent_iter];
            }

            /* Update the control port state to OPENED*/
            me_ptr->ctrl_port_info[ctrl_port_idx].num_intents = port_open_ptr->intent_map[port_iter].num_intents;
            me_ptr->ctrl_port_info[ctrl_port_idx].state       = CTRL_PORT_OPENED;
            me_ptr->ctrl_port_info[ctrl_port_idx].peer_miid   = port_open_ptr->intent_map[port_iter].peer_module_instance_id;
            me_ptr->ctrl_port_info[ctrl_port_idx].peer_port_id = port_open_ptr->intent_map[port_iter].peer_port_id;


              /* Module can use capi_history_buffer_imcl_register_for_recurring_bufs() to register recurring
               buffers. In this module, we are assuming module requires recurring buffers. we regestering
               recurring buffers at this point.*/
            result = capi_history_buffer_imcl_register_for_recurring_bufs(&me_ptr->cb_info,
                                                                     ctrl_port_id,
                                                                     HISTORY_BUFFER_RECURRING_BUF_SIZE,
                                                                     HISTORY_BUFFER_RECURRING_BUFS_NUMS);
            if (CAPI_FAILED(result))
            {
               DE_DBG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Failed registering for recurring buffers size %lu count %lu",
                      HISTORY_BUFFER_RECURRING_BUF_SIZE,
                      HISTORY_BUFFER_RECURRING_BUFS_NUMS);
               return result;
            }

            me_ptr->num_opened_ctrl_ports++;

            DE_DBG(me_ptr->miid,
                   DBG_MED_PRIO,
                   "Opening Control port id: 0x%x with num_intent=%lu intent ID[0]: 0x%x num_opened_ctrl_ports:%lu ",
                   ctrl_port_id,
                   me_ptr->ctrl_port_info[ctrl_port_idx].num_intents,
                   port_open_ptr->intent_map[port_iter].intent_arr[0],
                   me_ptr->num_opened_ctrl_ports);
         }
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Ctrl port open expects a payload. Failing.");
         return CAPI_EFAILED;
      }
      break;
   }
   case INTF_EXTN_IMCL_PORT_PEER_CONNECTED:
   {
      if (port_op_ptr->op_payload.data_ptr)
      {
         intf_extn_imcl_port_start_t *peer_connected_ptr =
            (intf_extn_imcl_port_start_t *)port_op_ptr->op_payload.data_ptr;

         /*number of ports*/
         uint32_t num_ports = peer_connected_ptr->num_ports;

         /* Validate port operation payload size */
         uint32_t valid_size = sizeof(intf_extn_imcl_port_start_t) + (num_ports * sizeof(uint32_t));
         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid payload size %lu for ctrl port peer connected valid_size %lu",
                   port_op_ptr->op_payload.actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /* Iterate through each port and update the state to peer connected. */
         for (uint32_t iter = 0; iter < num_ports; iter++)
         {
            uint32_t ctrl_port_id = peer_connected_ptr->port_id_arr[iter];

            /* Get the port index for the port id being close.*/
            uint32_t ctrl_port_idx = 0;
            if (AR_EOK != history_buffer_get_ctrl_port_index(me_ptr, ctrl_port_id, &ctrl_port_idx))
            {
               DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid control port id: 0x%x ", ctrl_port_id);
               return CAPI_EFAILED;
            }

            /* check if the port is already in connected state. */
            if (me_ptr->ctrl_port_info[ctrl_port_idx].state == CTRL_PORT_PEER_CONNECTED)
            {
               DE_DBG(me_ptr->miid,
                      DBG_HIGH_PRIO,
                      "Warning! Control port id: 0x%lx peer is already connected",
                      ctrl_port_id);
            }

            me_ptr->ctrl_port_info[ctrl_port_idx].state = CTRL_PORT_PEER_CONNECTED;

            DE_DBG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Ctrl port id 0x%lx port index %lu peer is connected",
                   ctrl_port_id,
                   ctrl_port_idx);

            /* TODO: Handle peer port connected state. Module can send any init time messages at this point to
               peer module. In this module, we are just updating the state to peer connected and sending resize
               to Dam module.

               Module can use capi_history_buffer_imcl_send_to_peer() if it needs to send any intents. */

            /* Send history buffer resize command to the DAM Module, to adjust the size of circular buffer */
            if (CAPI_FAILED(capi_history_buffer_imcl_send_resize_to_dam(me_ptr)))
            {
               AR_MSG(DBG_HIGH_PRIO, "Unable to send history buffer resize to DAM");
            }
         }
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Ctrl port peer connected expects a payload. Failing.");
         return CAPI_EFAILED;
      }
      break;
   }
   case INTF_EXTN_IMCL_PORT_PEER_DISCONNECTED:
   {
      if (port_op_ptr->op_payload.data_ptr)
      {
         intf_extn_imcl_port_stop_t *peer_disconnected_ptr =
            (intf_extn_imcl_port_stop_t *)port_op_ptr->op_payload.data_ptr;

         /*number of ports*/
         uint32_t num_ports = peer_disconnected_ptr->num_ports;

         /* Validate disconnect payload size */
         uint32_t valid_size = sizeof(intf_extn_imcl_port_stop_t) + (num_ports * sizeof(uint32_t));
         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid payload size for ctrl port peer disconnected %d",
                   params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /* Iterate through each port and update the state to peer disconnected. */
         for (uint32_t iter = 0; iter < num_ports; iter++)
         {
            /* port id being disconnected */
            uint32_t ctrl_port_id = peer_disconnected_ptr->port_id_arr[iter];

            /* Get the port index for the port id being close.*/
            uint32_t ctrl_port_idx = 0;
            if (AR_EOK != history_buffer_get_ctrl_port_index(me_ptr, ctrl_port_id, &ctrl_port_idx))
            {
               DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid control port id: 0x%x ", ctrl_port_id);
               return CAPI_EFAILED;
            }

            /* check if the port is already in disconnected state. */
            if (me_ptr->ctrl_port_info[ctrl_port_idx].state == CTRL_PORT_PEER_DISCONNECTED)
            {
               DE_DBG(me_ptr->miid,
                      DBG_HIGH_PRIO,
                      "Warning! Control port id: 0x%lx peer is already disconnected",
                      ctrl_port_id);
            }

            /* TODO: Handle peer port disconnected state. Module cannot send any intents messages
               at this point. In this module, we are just updating the state to peer connected. */

            me_ptr->ctrl_port_info[ctrl_port_idx].state = CTRL_PORT_PEER_DISCONNECTED;

            DE_DBG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Ctrl port id 0x%lx port index %lu peer is disconnected",
                   ctrl_port_id,
                   ctrl_port_idx);
         }
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Ctrl port peer disconnected expects a payload. Failing.");
         return CAPI_EFAILED;
      }
      break;
   }
   case INTF_EXTN_IMCL_PORT_CLOSE:
   {
      if (port_op_ptr->op_payload.data_ptr)
      {
         intf_extn_imcl_port_close_t *port_close_ptr = (intf_extn_imcl_port_close_t *)port_op_ptr->op_payload.data_ptr;

         /* Validate number of ports being closed */
         uint32_t num_ports = port_close_ptr->num_ports;

         if (num_ports > me_ptr->num_opened_ctrl_ports)
         {
            DE_DBG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Supports max of %lu control ports. Trying to close %lu ports",
                   HISTORY_BUFFER_MAX_CONTROL_PORTS,
                   num_ports);
            return CAPI_EBADPARAM;
         }

         /* Validate close payload size */
         uint32_t valid_size = sizeof(intf_extn_imcl_port_close_t) + (num_ports * sizeof(uint32_t));
         if (port_op_ptr->op_payload.actual_data_len < valid_size)
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Invalid payload size for ctrl port close %d", params_ptr->actual_data_len);
            return CAPI_ENEEDMORE;
         }

         /* Iterate through each port and close the port. */
         for (uint32_t iter = 0; iter < num_ports; iter++)
         {
            uint32_t ctrl_port_id = port_close_ptr->port_id_arr[iter];

            /* Get the port index for the port id being close.*/
            uint32_t ctrl_port_idx =0 ;
            if (AR_EOK != history_buffer_get_ctrl_port_index(me_ptr, ctrl_port_id, &ctrl_port_idx))
            {
               DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Trying to close invalid control port id: 0x%x ", ctrl_port_id);
               return CAPI_EFAILED;
            }

            /* Handle port close */
            capi_history_buffer_destroy_control_port(me_ptr, ctrl_port_idx);

            /* update no of opened ports*/
            me_ptr->num_opened_ctrl_ports--;

            DE_DBG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Control port id: 0x%lx port index %lu is closed. num_opened_ctrl_ports: %lu",
                   ctrl_port_id,
                   ctrl_port_idx,
                   me_ptr->num_opened_ctrl_ports);

         }
      }
      else
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Ctrl port close expects a payload. Failing.");
         return CAPI_EFAILED;
      }
      break;
   }
   default:
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received unsupported ctrl port opcode %lu", port_op_ptr->opcode);
      return CAPI_EUNSUPPORTED;
   }
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_destroy_control_port
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
void capi_history_buffer_destroy_control_port(capi_history_buffer_t *me_ptr, uint32_t ctrl_port_idx)
{
   /* Free any dyanmic memory if allocated for the given port */

   /* Memset the control port info structure */
   memset(&me_ptr->ctrl_port_info[ctrl_port_idx], 0, sizeof(history_buffer_ctrl_port_info_t));
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_validate_intent_id
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
bool_t capi_history_buffer_validate_intent_id(capi_history_buffer_t *me_ptr, uint32_t intent_id)
{
   switch (intent_id)
   {
   case INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL:
   {
      return TRUE;
   }
   default:
   {
      DE_DBG(me_ptr->miid,
             DBG_ERROR_PRIO,
             "unsupported intent id 0x%lx",
             intent_id);
      return FALSE;
   }
   }
   return FALSE;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_check_intent_id_support
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
bool_t capi_history_buffer_check_intent_id_support(capi_history_buffer_t *me_ptr,
                                              uint32_t                 ctrl_port_idx,
                                              uint32_t                 intent_id)
{
   for (uint32_t idx = 0; idx < me_ptr->ctrl_port_info[ctrl_port_idx].num_intents; idx++)
   {
      if (me_ptr->ctrl_port_info[ctrl_port_idx].intent_list_arr[idx] == intent_id)
      {
         return TRUE;
      }
   }

   return FALSE;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_handle_incoming_imc_message
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_handle_incoming_imc_message(capi_history_buffer_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == params_ptr->data_ptr)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Incoming imc message received null payload");
      return CAPI_EBADPARAM;
   }

   intf_extn_param_id_imcl_incoming_data_t *intent_hdr_ptr =
      (intf_extn_param_id_imcl_incoming_data_t *)params_ptr->data_ptr;

   /* Get corresponding control port index for a given port_id*/
   uint32_t ctrl_port_idx;
   uint32_t ctrl_port_id = intent_hdr_ptr->port_id;
   if (AR_EOK != history_buffer_get_ctrl_port_index(me_ptr, ctrl_port_id, &ctrl_port_idx))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received intent on an invalid control port id 0x%x  \n", ctrl_port_id);
      return CAPI_EFAILED;
   }

   /* Sanity check of the port state */
   if (me_ptr->ctrl_port_info[ctrl_port_idx].state == CTRL_PORT_PEER_DISCONNECTED ||
       me_ptr->ctrl_port_info[ctrl_port_idx].state == CTRL_PORT_CLOSED)
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received message on an invalid control port 0x%x \n", ctrl_port_id);
      return CAPI_EFAILED;
   }

   /* History buffer module currently can expect an incoming IMC message from DAM module only.
      So check if the current control port ID support intent id INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL.
   */
   if (!capi_history_buffer_check_intent_id_support(me_ptr, ctrl_port_idx, INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL))
   {
      DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received IMC message on an invalid control port 0x%x \n", ctrl_port_id);
      return CAPI_EFAILED;
   }

   /* Intent message over INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL control link have the following format.
      So iterate through the intent payload and extract each param.
    */
   uint32_t payload_size = params_ptr->actual_data_len - + sizeof(intf_extn_param_id_imcl_incoming_data_t);
   int8_t * payload_ptr  = params_ptr->data_ptr + sizeof(intf_extn_param_id_imcl_incoming_data_t);
   while (payload_size >= sizeof(vw_imcl_header_t))
   {
      vw_imcl_header_t *header_ptr = (vw_imcl_header_t *)payload_ptr;
      switch (header_ptr->opcode)
      {
         case PARAM_ID_AUDIO_DAM_UNREAD_DATA_LENGTH:
         {
            if (header_ptr->actual_data_len < sizeof(param_id_audio_dam_unread_bytes_t))
            {
               DE_DBG(me_ptr->miid,
                     DBG_ERROR_PRIO,
                     "Invalid payload size 0x%x for param id 0x%lx  \n",
                     header_ptr->actual_data_len,
                     header_ptr->opcode);
               return CAPI_EFAILED;
            }

            param_id_audio_dam_unread_bytes_t *param_ptr = (param_id_audio_dam_unread_bytes_t *)(header_ptr + 1);

            DE_DBG(me_ptr->miid,
                  DBG_HIGH_PRIO,
                  "Received unread data length %lu from DAM mdoule\n",
                  param_ptr->unread_in_us);

            /* Check if the module is waiting for ftrt length. Module can wait for FTRT length if generic history buffer
               event mode EVENT_INFO_FTRT_LENGTH is set. */
            if (me_ptr->latest_trigger_info.waiting_for_ftrt_len)
            {
               /* Populate FTRT info into the latest trigger info*/
               me_ptr->latest_trigger_info.ftrt_info.ftrt_data_length_in_us = param_ptr->unread_in_us;

               // clear the wait flag
               me_ptr->latest_trigger_info.waiting_for_ftrt_len = FALSE;

               /* Raise the pending trigger event. */
               result = capi_populate_trigger_info_and_raise_event(me_ptr);
            }

            break;
         }
         default:
         {
            DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received invalid IMC param_id 0x%lx is received. ", header_ptr->opcode);
            return CAPI_EBADPARAM;
         }
      }

      payload_ptr += (sizeof(vw_imcl_header_t) + header_ptr->actual_data_len);

      if (payload_size >= (sizeof(vw_imcl_header_t) + header_ptr->actual_data_len))
      {
         payload_size -= (sizeof(vw_imcl_header_t) + header_ptr->actual_data_len);
      }
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_register_for_recurring_bufs
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_imcl_register_for_recurring_bufs(capi_event_callback_info_t *event_cb_info_ptr,
                                                           uint32_t                    ctrl_port_id,
                                                           uint32_t                    buf_size,
                                                           uint32_t                    num_bufs)
{
   capi_err_t result = CAPI_EOK;

   /** Populate event payload */
   event_id_imcl_recurring_buf_info_t event_payload;
   event_payload.port_id  = ctrl_port_id;
   event_payload.buf_size = buf_size;
   event_payload.num_bufs = num_bufs;

   /** Populate event type header */
   capi_event_data_to_dsp_service_t event_header;
   event_header.param_id                = INTF_EXTN_EVENT_ID_IMCL_RECURRING_BUF_INFO;
   event_header.payload.actual_data_len = sizeof(event_id_imcl_recurring_buf_info_t);
   event_header.payload.max_data_len    = sizeof(event_id_imcl_recurring_buf_info_t);
   event_header.payload.data_ptr        = (int8_t *)&event_payload;

   /** Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(event_header);
   event_info.payload.max_data_len    = sizeof(event_header);
   event_info.payload.data_ptr        = (int8_t *)&event_header;

   /** Invoke event callback */
   if (CAPI_EOK !=
       (result =
           event_cb_info_ptr->event_cb(event_cb_info_ptr->event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to register for recurring bufs on ctrl_port_id[0x%lX]", ctrl_port_id);
   }
   else
   {
      AR_MSG(DBG_HIGH_PRIO,
             "Registered for [%lu] recurring bufs of size[%lu  bytes] on ctrl_port_id[0x%lX]",
             num_bufs,
             buf_size,
             ctrl_port_id);
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_data_send
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
static capi_err_t capi_history_buffer_imcl_data_send(capi_event_callback_info_t *event_cb_info_ptr,
                                                capi_buf_t *                ctrl_data_buf_ptr,
                                                uint32_t                    ctrl_port_id,
                                                imcl_outgoing_data_flag_t   flags)
{
   capi_err_t result = CAPI_EOK;

   /** Populate event payload */
   event_id_imcl_outgoing_data_t event_payload;
   event_payload.port_id             = ctrl_port_id;
   event_payload.buf.actual_data_len = ctrl_data_buf_ptr->actual_data_len;
   event_payload.buf.data_ptr        = ctrl_data_buf_ptr->data_ptr;
   event_payload.buf.max_data_len    = ctrl_data_buf_ptr->max_data_len;
   event_payload.flags               = flags;

   /** Populate event type header */
   capi_event_data_to_dsp_service_t event_header;
   event_header.param_id                = INTF_EXTN_EVENT_ID_IMCL_OUTGOING_DATA;
   event_header.payload.actual_data_len = sizeof(event_id_imcl_outgoing_data_t);
   event_header.payload.max_data_len    = sizeof(event_id_imcl_outgoing_data_t);
   event_header.payload.data_ptr        = (int8_t *)&event_payload;

   /** Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(event_header);
   event_info.payload.max_data_len    = sizeof(event_header);
   event_info.payload.data_ptr        = (int8_t *)&event_header;

   /** Invoke event callback */
   if (CAPI_EOK !=
       (result =
           event_cb_info_ptr->event_cb(event_cb_info_ptr->event_context, CAPI_EVENT_DATA_TO_DSP_SERVICE, &event_info)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to sent ctrl data buf on ctrl port id[0x%lX]", ctrl_port_id);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Sent ctrl data Buf of size[%lu bytes] on ctrl port ID[0x%lX], send_to_peer[%d, 0/1: Returned/Sent]",
             ctrl_data_buf_ptr->max_data_len,
             ctrl_port_id,
             flags.should_send);
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_get_recurring_buf
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_imcl_get_recurring_buf(capi_event_callback_info_t *event_cb_info_ptr,
                                                 uint32_t                    ctrl_port_id,
                                                 capi_buf_t *                rec_buf_ptr)
{
   capi_err_t result;

   /** Populate event payload */
   event_id_imcl_get_recurring_buf_t event_payload;
   event_payload.port_id      = ctrl_port_id;
   event_payload.buf.data_ptr = NULL;

   /** Populate event type header */
   capi_event_get_data_from_dsp_service_t event_header;
   event_header.param_id                = INTF_EXTN_EVENT_ID_IMCL_GET_RECURRING_BUF;
   event_header.payload.actual_data_len = sizeof(event_id_imcl_get_recurring_buf_t);
   event_header.payload.max_data_len    = sizeof(event_id_imcl_get_recurring_buf_t);
   event_header.payload.data_ptr        = (int8_t *)&event_payload;

   /** Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(event_header);
   event_info.payload.max_data_len    = sizeof(event_header);
   event_info.payload.data_ptr        = (int8_t *)&event_header;

   /** Invoke event callback */
   if (CAPI_EOK != (result = event_cb_info_ptr->event_cb(event_cb_info_ptr->event_context,
                                                         CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE,
                                                         &event_info)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to send request to get recurring buf on ctrl_port_id[0x%lX]", ctrl_port_id);

      rec_buf_ptr->data_ptr     = NULL;
      rec_buf_ptr->max_data_len = 0;

      return result;
   }

   /** Populate the return pointer */
   rec_buf_ptr->data_ptr        = event_payload.buf.data_ptr;
   rec_buf_ptr->max_data_len    = event_payload.buf.max_data_len;
   rec_buf_ptr->actual_data_len = event_payload.buf.actual_data_len;

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_get_one_time_buf
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_imcl_get_one_time_buf(capi_event_callback_info_t *event_cb_info_ptr,
                                                uint32_t                    ctrl_port_id,
                                                uint32_t                    req_buf_size,
                                                capi_buf_t *                ot_buf_ptr)
{
   capi_err_t result;

   /** Populate event payload */
   event_id_imcl_get_one_time_buf_t event_payload;
   event_payload.port_id             = ctrl_port_id;
   event_payload.buf.actual_data_len = req_buf_size;

   /** Populate event type header */
   capi_event_get_data_from_dsp_service_t event_header;
   event_header.param_id                = INTF_EXTN_EVENT_ID_IMCL_GET_ONE_TIME_BUF;
   event_header.payload.actual_data_len = sizeof(event_id_imcl_get_one_time_buf_t);
   event_header.payload.max_data_len    = sizeof(event_id_imcl_get_one_time_buf_t);
   event_header.payload.data_ptr        = (int8_t *)&event_payload;

   /** Create event info */
   capi_event_info_t event_info;
   event_info.port_info.is_input_port = FALSE;
   event_info.port_info.is_valid      = FALSE;
   event_info.payload.actual_data_len = sizeof(event_header);
   event_info.payload.max_data_len    = sizeof(event_header);
   event_info.payload.data_ptr        = (int8_t *)&event_header;

   /** Invoke event callback */
   if (CAPI_EOK != (result = event_cb_info_ptr->event_cb(event_cb_info_ptr->event_context,
                                                            CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE,
                                                            &event_info)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to send request to get one-time buf on ctrl_port_id[0x%lX]", ctrl_port_id);

      ot_buf_ptr->data_ptr     = NULL;
      ot_buf_ptr->max_data_len = 0;

      return result;
   }

   /** Populate the return pointer */
   ot_buf_ptr->data_ptr        = event_payload.buf.data_ptr;
   ot_buf_ptr->max_data_len    = event_payload.buf.max_data_len;
   ot_buf_ptr->actual_data_len = event_payload.buf.actual_data_len;

   return result;
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_imcl_send_to_peer
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_imcl_send_to_peer(capi_event_callback_info_t *event_cb_info_ptr,
                                            capi_buf_t *                ctrl_data_buf_ptr,
                                            uint32_t                    ctrl_port_id,
                                            imcl_outgoing_data_flag_t   flags)
{
   return capi_history_buffer_imcl_data_send(event_cb_info_ptr, ctrl_data_buf_ptr, ctrl_port_id, flags);
}

/* =========================================================================
 * FUNCTION : capi_history_buffer_send_flow_ctrl_msg_to_dam
 *
 * Refer capi_history_buffer_imcl_utils.h for function usage.
 * ========================================================================= */
capi_err_t capi_history_buffer_send_flow_ctrl_msg_to_dam(capi_history_buffer_t *            me_ptr,
                                                    param_id_audio_dam_data_flow_ctrl_t *param_ptr)
{
   capi_err_t result = CAPI_EOK;

   /* Check the control ports that are connected and check supported Intent IDs.
      Need to check if each control port supports the intent ID INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL
      indicating its a control port to DAM */
   for (uint32_t ctrl_port_idx = 0; ctrl_port_idx < HISTORY_BUFFER_MAX_CONTROL_PORTS; ctrl_port_idx++)
   {
      /* Send intents to only connected port and if control port supports intent id detection ctrl */
      if ((me_ptr->ctrl_port_info[ctrl_port_idx].state != CTRL_PORT_PEER_CONNECTED) &&
          capi_history_buffer_check_intent_id_support(me_ptr, ctrl_port_idx, INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL))
      {
         continue;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "Sending IMC message through control port id 0x%lx",
             me_ptr->ctrl_port_info[ctrl_port_idx].port_id);

      // get control port id for the port index.
      uint32_t control_port_id = me_ptr->ctrl_port_info[ctrl_port_idx].port_id;

      /* Step A - Get recurring buffers*/
      // Request recurring buffers for this particular control port id.
      capi_buf_t buffer;
      capi_history_buffer_imcl_get_recurring_buf(&me_ptr->cb_info, control_port_id, &buffer);
      if (NULL == buffer.data_ptr || buffer.max_data_len < sizeof(vw_imcl_header_t))
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received NULL intent buffer or invalid size %lu", buffer.max_data_len);
         result = CAPI_EFAILED;
      }
      else /* Step B- Populate and send the buffer */
      {
         // create the payload with outgoing param ids.
         struct dam_flow_ctrl_payload_t
         {
            vw_imcl_header_t                    header;
            param_id_audio_dam_data_flow_ctrl_t cfg;
         };

         struct dam_flow_ctrl_payload_t *temp = (struct dam_flow_ctrl_payload_t *)buffer.data_ptr;

         // populate param payload
         uint32_t param_len =
            sizeof(param_id_audio_dam_data_flow_ctrl_t) + (param_ptr->num_best_channels * sizeof(uint32_t));

         temp->header.opcode          = PARAM_ID_AUDIO_DAM_DATA_FLOW_CTRL;
         temp->header.actual_data_len = param_len;

         // copy param struct param_id_audio_dam_data_flow_ctrl_t payload to intent buffer
         memscpy(&temp->cfg, buffer.max_data_len - sizeof(vw_imcl_header_t), param_ptr, param_len);

         buffer.actual_data_len = param_len + sizeof(vw_imcl_header_t);

         /*To send data over to the IMCL peer*/
         imcl_outgoing_data_flag_t flags;
         flags.should_send = TRUE; // FALSE == buffer is returned to fwk and not sent to peer module.
         flags.is_trigger  = TRUE; // FALSE == polling, refer intf extension for more details
         result            = capi_history_buffer_imcl_send_to_peer(&me_ptr->cb_info, &buffer, control_port_id, flags);
      }
   }
   return result;
}

capi_err_t capi_history_buffer_imcl_send_resize_to_dam(capi_history_buffer_t *me_ptr)
{
   capi_err_t result = CAPI_EOK;
   if(!me_ptr->hist_buffer_duration_msec)
   {
     AR_MSG(DBG_MED_PRIO, "Warning: history buffering configuration not received yet.");
      // Resize requirement is not set yet.
      return CAPI_ENOTREADY;
   }

   /* Check the control ports that are connected and check supported Intent IDs.
      Need to check if each control port supports the intent ID INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL
      indicating its a control port to DAM */
   for (uint32_t ctrl_port_idx = 0; ctrl_port_idx < HISTORY_BUFFER_MAX_CONTROL_PORTS; ctrl_port_idx++)
   {
      /* Send intents to only connected port and if control port supports intent id detection ctrl */
      if ((me_ptr->ctrl_port_info[ctrl_port_idx].state != CTRL_PORT_PEER_CONNECTED) &&
          capi_history_buffer_check_intent_id_support(me_ptr, ctrl_port_idx, INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL))
      {
         continue;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "Sending resize IMC message through control port id 0x%lx",
             me_ptr->ctrl_port_info[ctrl_port_idx].port_id);

      // get control port id for the port index.
      uint32_t control_port_id = me_ptr->ctrl_port_info[ctrl_port_idx].port_id;

      /* Step A - Get recurring buffers*/
      // Request recurring buffers for this particular control port id.
      capi_buf_t buffer;
      capi_history_buffer_imcl_get_recurring_buf(&me_ptr->cb_info, control_port_id, &buffer);
      if (NULL == buffer.data_ptr || buffer.max_data_len < sizeof(vw_imcl_header_t))
      {
         DE_DBG(me_ptr->miid, DBG_ERROR_PRIO, "Received NULL intent buffer or invalid size %lu", buffer.max_data_len);
         result = CAPI_EFAILED;
      }
      else /* Step B- Populate and send the buffer */
      {
         // create the payload with outgoing param ids.
         struct dam_resize_payload_t
         {
            vw_imcl_header_t                    header;
            param_id_audio_dam_buffer_resize_t  cfg;
         };

         struct dam_resize_payload_t *temp = (struct dam_resize_payload_t *)buffer.data_ptr;

         // populate param payload
         uint32_t param_len =  sizeof(param_id_audio_dam_buffer_resize_t);

         temp->header.opcode          = PARAM_ID_AUDIO_DAM_RESIZE;
         temp->header.actual_data_len = param_len;

         // copy param struct param_id_audio_dam_buffer_resize_t payload to intent buffer
         temp->cfg.resize_in_us = me_ptr->hist_buffer_duration_msec * 1000;

         buffer.actual_data_len = param_len + sizeof(vw_imcl_header_t);

         /*To send data over to the IMCL peer*/
         imcl_outgoing_data_flag_t flags;
         flags.should_send = TRUE; // FALSE == buffer is returned to fwk and not sent to peer module.
         flags.is_trigger  = TRUE; // FALSE == polling, refer intf extension for more details
         result            = capi_history_buffer_imcl_send_to_peer(&me_ptr->cb_info, &buffer, control_port_id, flags);
      }
   }
   return result;
}