/**
 * \file capi_ipc_tx_data_port_utils.c
 *
 * \brief
 *
 *
 *
 *
 * \copyright
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 */

/**----------------------------------------------------------------------------
 ** Include Files
 ** -------------------------------------------------------------------------*/

#include "capi_ipc_tx_utils.h"

/* =========================================================================
 * FUNCTION : capi_ipc_tx_handle_intf_extn_data_port_operation
 *
 * DESCRIPTION:
 * Performs data port operation like OPEN, CLOSE, START, STOP.
 *
 * OPEN -
 *  Framework gives the port_id to port_index mapping through this operation,
 *  for the newly opened ports. Module can allocate any dyanmic resource per
 *  port if needed. Note that for input port media format is not available at
 *  this point. So module can only allocate resources which are independent of
 *  the input media format. Note that module can receive data buffers only in
 *  the START state.
 *
 * CLOSE -
 *  Closes the given input/output port_index. Destroy dynamic resources for the
 *  port.
 *  Reinitialize when the port re-opens.
 *
 *  START -
 *  This indicates module can start to expect data buffers on this ports.
 *
 *  STOP -
 *  This indicates module must stop expecting data buffers on this ports.
 *
 *
 * ========================================================================= */

capi_err_t capi_ipc_tx_handle_intf_extn_data_port_operation(capi_ipc_tx_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   if (NULL == params_ptr->data_ptr)
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Data port operation received null buffer");
      return CAPI_EBADPARAM;
   }
   if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
   {
      IPC_TX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid payload size for port operation %d",
                   params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);
   if (params_ptr->actual_data_len <
       sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t)))
   {
      IPC_TX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid payload size for port operation %d",
                   params_ptr->actual_data_len);
      return CAPI_ENEEDMORE;
   }

   // Check if the number of input/output ports being operated are within the max number of ports
   if (data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->num_port_info.num_input_ports))
   {
      IPC_TX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid input ports. num_ports =%d, max_input_ports = %d",
                   data_ptr->num_ports,
                   me_ptr->num_port_info.num_input_ports);
      return CAPI_EBADPARAM;
   }

   if (!data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->num_port_info.num_output_ports))
   {
      IPC_TX_MSG(me_ptr->miid,
                   DBG_ERROR_PRIO,
                   "Invalid output ports. num_ports =%d, max_output_ports = %d",
                   data_ptr->num_ports,
                   me_ptr->num_port_info.num_output_ports);
      return CAPI_EBADPARAM;
   }

   // Iterate and perform port operation on each input/output ports.
   for (uint32_t iter = 0; iter < data_ptr->num_ports; iter++)
   {
      uint32_t port_id = data_ptr->id_idx[iter].port_id;
      uint32_t port_index = data_ptr->id_idx[iter].port_index;

      IPC_TX_MSG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Port operation 0x%x performed on port_index= %lu, port_id= 0x%lx is_input_port= %d ",
                   data_ptr->opcode,
                   port_index,
                   data_ptr->id_idx[iter].port_id,
                   data_ptr->is_input_port);

      if ((data_ptr->is_input_port && (port_index >= me_ptr->num_port_info.num_input_ports)) ||
          (!data_ptr->is_input_port && (port_index >= me_ptr->num_port_info.num_output_ports)))
      {
         IPC_TX_MSG(me_ptr->miid,
                      DBG_ERROR_PRIO,
                      "Bad parameter in id-idx map on port_id 0x%lx, port_index = %lu, max in ports = %d, max "
                      "out ports = %d ",
                      iter,
                      port_index,
                      me_ptr->num_port_info.num_input_ports,
                      me_ptr->num_port_info.num_output_ports);
         return CAPI_EBADPARAM;
      }

      switch (data_ptr->opcode)
      {
         case INTF_EXTN_DATA_PORT_OPEN:
         {
            capi_result = capi_ipc_tx_handle_port_open(me_ptr, port_id, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_START:
         {
            capi_result = capi_ipc_tx_handle_port_start(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_STOP:
         {
            capi_result = capi_ipc_tx_handle_port_stop(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_CLOSE:
         {
            capi_result = capi_ipc_tx_handle_port_close(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         default:
         {
            IPC_TX_MSG(me_ptr->miid,
                        DBG_ERROR_PRIO,
                        "Port operation - Unsupported opcode: 0x%x",
                        data_ptr->opcode);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      IPC_TX_MSG(me_ptr->miid,
                   DBG_HIGH_PRIO,
                   "Port operation 0x%lx done with result 0x%lx, for port id 0x%x is_input = 0x%x",
                   data_ptr->opcode,
                   capi_result,
                   port_id,
                   data_ptr->is_input_port);
   }
   return capi_result;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_handle_port_open
 *
 * DESCRIPTION:
 *  Handle input/output port OPEN. Allocate any dynamic resources if required.
 * ========================================================================= */

capi_err_t capi_ipc_tx_handle_port_open(capi_ipc_tx_t *me_ptr, uint32_t port_id, uint32_t port_index, bool_t is_input)
{
   capi_err_t result = CAPI_EOK;

   if (is_input) // cache state for input port
   {
      // Cache the input port ID,index and state.
      me_ptr->in_port_info[port_index].port_id = port_id;
      me_ptr->in_port_info[port_index].port_index = port_index;
      me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_OPENED;
   }
   else // cache state for output port
   {
      // Cache the output port ID ,index and state.
      me_ptr->out_port_info[port_index].port_id = port_id;
      me_ptr->out_port_info[port_index].port_index = port_index;
      me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_OPENED;
      // Check if any media format is pending and send it through the control path.
      // -> we are handling this part in set property (when inp MF is received)
      // buit what about the case if out port is closed and opened again but MF is not rececived again?
   }

   IPC_TX_MSG(me_ptr->miid, DBG_MED_PRIO, "opening port_index %lu", port_index);
   return result;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_handle_port_close
 *
 * DESCRIPTION:
 *  Handle input/output port CLOSE. De-allocate if any dynamic resources are
 *  allocated earlier.
 * ========================================================================= */
capi_err_t capi_ipc_tx_handle_port_close(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   capi_err_t result = CAPI_EOK;
   if (is_input)
   {
      if (DATA_PORT_STATE_CLOSED != me_ptr->in_port_info[port_index].port_state)
      {
         me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_CLOSED;
         me_ptr->is_inp_media_fmt_received = FALSE;
         // Destroy input port
         // Destroy input port
      }
   }
   else
   {
      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_info[port_index].port_state)
      {
         me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_CLOSED;
         IPC_TX_MSG(me_ptr->miid, DBG_MED_PRIO, "closing out port_index %lu", port_index);

         // Destroy input buffers is any
         //  Destroy output port
      }
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_handle_port_start
 *
 * DESCRIPTION:
 *  Handle input/output port START. Indicates module can expect data buffers
 *  on this input/output port from next process cycle if trigger present.
 * ========================================================================= */
capi_err_t capi_ipc_tx_handle_port_start(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   capi_err_t result = CAPI_EOK;

   if (is_input)
   {
      // todo: if port if already closed, then we may not start add chcek here
      // also, start the data flow
      me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_STARTED;
   }
   else
   {
      // Mark port as started
      me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_STARTED;

         // if dps is OPEN, meaning there is no data processing going on, so directly send the media format

         // check if any pending media format, apply and set it
      if(me_ptr->is_inp_media_fmt_pending || me_ptr->frame_length_info.is_frame_len_received)
      {
         result = capi_ipc_tx_update_media_fmt(me_ptr, me_ptr->inp_media_fmt, me_ptr->inp_media_fmt_size);
         if (result != CAPI_EOK)
         {
            IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to update input media format");
            return result;
         }
         me_ptr->is_inp_media_fmt_pending = FALSE;
      // }
         result = capi_ipc_tx_raise_event(me_ptr);
         if (result != CAPI_EOK)
         {
            IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to raise event");
            return result; // TBD: should we return error in process?
         }
      }
   }

   return result;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_handle_port_stop
 *
 * DESCRIPTION:
 *  Handle input/output port STOP. Indicates module must stop expecting data buffers
 *  on this input/output port.
 * ========================================================================= */
capi_err_t capi_ipc_tx_handle_port_stop(capi_ipc_tx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   capi_err_t result = CAPI_EOK;
   if (is_input)
   {
      if (DATA_PORT_STATE_STARTED == me_ptr->in_port_info[port_index].port_state)
      {  // todo: if port started then, stop // Mark port as stopped
         me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
	  }
	  else
	  {
	     me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
		 IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Warning! Received i/p port Stop before Start");
	  }
   }
   else
   {
      if (DATA_PORT_STATE_STARTED == me_ptr->out_port_info[port_index].port_state)
      {
         // Mark port as stopped
         me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
	  }
      else
	  {
	     me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
		 IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Warning! Received o/p port Stop before Start");
     }

     // Change trigger status to port buffer needed so that the module stops triggering process
     me_ptr->output_trigger_info            = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
     me_ptr->sh_buf_info.is_prebuffers_sent = FALSE;
   }
   return result;
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_destroy_input_port
 *
 * DESCRIPTION:
 *  Destorys input port dynamic resources or reset the port state.
 * Function is called during port CLOSE or module END.
 * ========================================================================= */
void capi_ipc_tx_destroy_input_port(capi_ipc_tx_t *me_ptr, uint32_t port_index)
{
   /* Destory metdata in the internal list*/
   //capi_ipc_tx_destroy_md_list(me_ptr, &me_ptr->in_port_info[port_index].md_list_ptr);
// temp
   me_ptr->in_port_info[port_index].md_list_ptr = NULL;
   /* Free any dyanmic memory if allocated for the given port */

   /* Memset the control port info structure */
   memset(&me_ptr->in_port_info[port_index], 0, sizeof(ipc_tx_input_port_info_t));
}

/* =========================================================================
 * FUNCTION : capi_ipc_tx_destroy_output_port
 *
 * DESCRIPTION:
 *  Destorys output ports dynamic resources or reset the port state.
 * Function is called during port CLOSE or module END.
 * ========================================================================= */
void capi_ipc_tx_destroy_output_port(capi_ipc_tx_t *me_ptr, uint32_t port_idx)
{
   /* destroy any buffers if allocated for the given port */

   /* Memset the control port info structure */
   memset(&me_ptr->out_port_info[port_idx], 0, sizeof(ipc_tx_output_port_info_t));
}
