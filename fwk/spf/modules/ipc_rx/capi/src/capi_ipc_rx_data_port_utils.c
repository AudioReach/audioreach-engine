/*==============================================================================
@file capi_ipc_rx_data_port_utils.c
@brief This file implements data port utility functions for the IPC RX CAPI module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
/* clang-format off */
/* No Edit History for automated refactoring. */
/* clang-format on */

/*------------------------------------------------------------------------
 * Include Files
 * -----------------------------------------------------------------------*/
#include "capi_ipc_rx_utils.h"

/*------------------------------------------------------------------------
 * Function Definitions
 * -----------------------------------------------------------------------*/

/**
 * \brief Handles interface extension data port operations (OPEN, CLOSE, START, STOP).
 *
 * This function processes data port operations for both input and output ports.
 * Each operation type (OPEN, CLOSE, START, STOP) is delegated to a specific
 * handler function.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] params_ptr Pointer to the CAPI buffer containing parameters for the operation.
 *                       Must not be NULL, and its `data_ptr` must not be NULL.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr`, `params_ptr`, or `params_ptr->data_ptr` is NULL,
 *                        or if the number of ports in the operation exceeds module capabilities,
 *                        or if an invalid port index is provided.
 *         CAPI_ENEEDMORE if the payload size is insufficient to contain the data port operation structure.
 *         CAPI_EUNSUPPORTED if an unknown or unsupported opcode is received.
 *         Other CAPI error codes if any of the specific port handling functions (`capi_ipc_rx_handle_port_open`, etc.) fail.
 */
capi_err_t capi_ipc_rx_handle_intf_extn_data_port_operation(capi_ipc_rx_t *me_ptr, capi_buf_t *params_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   // Validate input pointers
   if (NULL == me_ptr || NULL == params_ptr || NULL == params_ptr->data_ptr)
   {
      IPC_RX_MSG(me_ptr ? me_ptr->miid : MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL pointer received for me_ptr, params_ptr or params_ptr->data_ptr.");
      return CAPI_EBADPARAM;
   }

   // Validate payload size for the base structure
   if (params_ptr->actual_data_len < sizeof(intf_extn_data_port_operation_t))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Invalid payload size for port operation: %lu, expected at least %lu.",
                 params_ptr->actual_data_len, sizeof(intf_extn_data_port_operation_t));
      return CAPI_ENEEDMORE;
   }

   intf_extn_data_port_operation_t *data_ptr = (intf_extn_data_port_operation_t *)(params_ptr->data_ptr);

   // Validate payload size for all port entries
   if (params_ptr->actual_data_len < (sizeof(intf_extn_data_port_operation_t) + (data_ptr->num_ports * sizeof(intf_extn_data_port_id_idx_map_t))))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Invalid payload size for port operation: %lu, not enough space for %lu ports.",
                 params_ptr->actual_data_len, data_ptr->num_ports);
      return CAPI_ENEEDMORE;
   }

   // Validate the number of ports against module's capabilities
   if ((data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->num_port_info.num_input_ports)) ||
       (!data_ptr->is_input_port && (data_ptr->num_ports > me_ptr->num_port_info.num_output_ports)))
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Invalid number of ports in operation. is_input_port=%u, num_ports=%lu, max_input_ports=%lu, max_output_ports=%lu",
                 data_ptr->is_input_port, data_ptr->num_ports, me_ptr->num_port_info.num_input_ports, me_ptr->num_port_info.num_output_ports);
      return CAPI_EBADPARAM;
   }

   // Iterate through each port specified in the operation and apply the opcode
   for (uint32_t iter = 0; iter < data_ptr->num_ports; iter++)
   {
      uint32_t port_id    = data_ptr->id_idx[iter].port_id;
      uint32_t port_index = data_ptr->id_idx[iter].port_index;

      IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Port operation [0x%lX] on port_index=%lu, port_id=0x%lx, is_input_port=%u",
                 data_ptr->opcode, port_index, port_id, data_ptr->is_input_port);

      // Validate port index before proceeding with the operation
      if ((data_ptr->is_input_port && (port_index >= me_ptr->num_port_info.num_input_ports)) ||
          (!data_ptr->is_input_port && (port_index >= me_ptr->num_port_info.num_output_ports)))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Invalid port index %lu for port_id 0x%lx. Max input ports: %lu, Max output ports: %lu",
                    port_index, port_id, me_ptr->num_port_info.num_input_ports, me_ptr->num_port_info.num_output_ports);
         return CAPI_EBADPARAM; // Return immediately on invalid index to prevent further errors
      }

      // Perform action based on the operation opcode
      switch (data_ptr->opcode)
      {
         case INTF_EXTN_DATA_PORT_OPEN:
         {
            capi_result = capi_ipc_rx_handle_port_open(me_ptr, port_id, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_START:
         {
            capi_result = capi_ipc_rx_handle_port_start(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_STOP:
         {
            capi_result = capi_ipc_rx_handle_port_stop(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         case INTF_EXTN_DATA_PORT_CLOSE:
         {
            capi_result = capi_ipc_rx_handle_port_close(me_ptr, port_index, data_ptr->is_input_port);
            break;
         }
         default:
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Unsupported data port operation opcode: 0x%lx.", data_ptr->opcode);
            CAPI_SET_ERROR(capi_result, CAPI_EUNSUPPORTED);
            break;
         }
      }

      // If any operation for a port fails, log it and return the error.
      // This ensures that the first encountered error stops further processing.
      if (CAPI_FAILED(capi_result))
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Port operation 0x%lX failed for port_id 0x%x, is_input=%u with result 0x%lx.",
                    data_ptr->opcode, port_id, data_ptr->is_input_port, capi_result);
         return capi_result;
      }

      // Update global module state after a successful data port operation.
      // This implicitly affects the module's data flow and trigger behavior.
      me_ptr->dfs                = IPC_RX_DFS_AT_GAP;
      me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;

      IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Port operation 0x%lX completed successfully for port_id 0x%x, is_input=%u.",
                 data_ptr->opcode, port_id, data_ptr->is_input_port);
   }
   return capi_result;
}

/**
 * \brief Handles the OPEN operation for an input or output port.
 *
 * Initializes and caches the port's ID and index, and sets its state to OPENED.
 * For input ports, it also initializes the `is_media_fmt_received` flag.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_id    The unique ID of the port to open.
 * \param[in] port_index The index of the port (e.g., 0 for the first, 1 for the second).
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` is NULL or if `port_index` is out of bounds.
 */
capi_err_t capi_ipc_rx_handle_port_open(capi_ipc_rx_t *me_ptr, uint32_t port_id, uint32_t port_index, bool_t is_input)
{
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_handle_port_open.");
      return CAPI_EBADPARAM;
   }

   if (is_input)
   {
      // Validate input port index
      if (port_index >= IPC_RX_MAX_INPUT_PORTS)
      {
          IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Input port_index %lu out of bounds (max %lu) during OPEN.", port_index, IPC_RX_MAX_INPUT_PORTS);
          return CAPI_EBADPARAM;
      }
      // Store port information and set state to OPENED
      me_ptr->in_port_info[port_index].port_id            = port_id;
      me_ptr->in_port_info[port_index].port_index         = port_index;
      me_ptr->in_port_info[port_index].port_state         = DATA_PORT_STATE_OPENED;
      me_ptr->in_port_info[port_index].is_media_fmt_received = FALSE; // Media format not yet received for a newly opened port
   }
   else // Output port
   {
      // Validate output port index
      if (port_index >= IPC_RX_MAX_OUTPUT_PORTS)
      {
          IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Output port_index %lu out of bounds (max %lu) during OPEN.", port_index, IPC_RX_MAX_OUTPUT_PORTS);
          return CAPI_EBADPARAM;
      }
      // Store port information and set state to OPENED
      me_ptr->out_port_info[port_index].port_id    = port_id;
      me_ptr->out_port_info[port_index].port_index = port_index;
      me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_OPENED;
   }

   IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Port %s (ID 0x%lx, Index %lu) opened successfully.",
              (is_input ? "input" : "output"), port_id, port_index);
   return CAPI_EOK;
}

/**
 * \brief Handles the CLOSE operation for an input or output port.
 *
 * This function is responsible for deallocating any dynamic resources
 * associated with the specified port and resetting its state to CLOSED.
 * It leverages `capi_ipc_rx_destroy_input_port` or `capi_ipc_rx_destroy_output_port`
 * for resource cleanup.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_index The index of the port to close.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` is NULL or if `port_index` is out of bounds.
 */
capi_err_t capi_ipc_rx_handle_port_close(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_handle_port_close.");
      return CAPI_EBADPARAM;
   }

   if (is_input)
   {
      // Validate input port index
      if (port_index >= IPC_RX_MAX_INPUT_PORTS)
      {
          IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Input port_index %lu out of bounds (max %lu) during CLOSE.", port_index, IPC_RX_MAX_INPUT_PORTS);
          return CAPI_EBADPARAM;
      }
      // Only destroy and close if not already in CLOSED state
      if (DATA_PORT_STATE_CLOSED != me_ptr->in_port_info[port_index].port_state)
      {
         capi_ipc_rx_destroy_input_port(me_ptr, port_index); // Calls destroy which also sets to CLOSED
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Input port (Index %lu) closed and destroyed.", port_index);
      }
      else
      {
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Input port (Index %lu) already closed, no action taken.", port_index);
      }
   }
   else // Output port
   {
      // Validate output port index
      if (port_index >= IPC_RX_MAX_OUTPUT_PORTS)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Output port_index %lu out of bounds (max %lu) during CLOSE.", port_index, IPC_RX_MAX_OUTPUT_PORTS);
         return CAPI_EBADPARAM;
      }
      // Only destroy and close if not already in CLOSED state
      if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_info[port_index].port_state)
      {
         capi_ipc_rx_destroy_output_port(me_ptr, port_index); // Calls destroy which also sets to CLOSED
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Output port (Index %lu) closed and destroyed.", port_index);
      }
      else
      {
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Output port (Index %lu) already closed, no action taken.", port_index);
      }
   }
   return CAPI_EOK;
}

/**
 * \brief Handles the START operation for an input or output port.
 *
 * Sets the specified port's state to STARTED, indicating that the module
 * can begin expecting and processing data buffers on this port. It also
 * updates the module's global data flow state and input trigger information.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_index The index of the port to start.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` is NULL, if `port_index` is out of bounds,
 *                        or if an attempt is made to start a port that is in the CLOSED state.
 */
capi_err_t capi_ipc_rx_handle_port_start(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_handle_port_start.");
      return CAPI_EBADPARAM;
   }

   if (is_input)
   {
      // Validate input port index
      if (port_index >= IPC_RX_MAX_INPUT_PORTS)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Input port_index %lu out of bounds (max %lu) during START.", port_index, IPC_RX_MAX_INPUT_PORTS);
         return CAPI_EBADPARAM;
      }
      // Prevent starting a closed port
      if (DATA_PORT_STATE_CLOSED == me_ptr->in_port_info[port_index].port_state)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Cannot start a closed input port (Index %lu).", port_index);
         return CAPI_EBADPARAM; // Use a specific error code if available
      }
      me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_STARTED;
   }
   else // Output port
   {
      // Validate output port index
      if (port_index >= IPC_RX_MAX_OUTPUT_PORTS)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Output port_index %lu out of bounds (max %lu) during START.", port_index, IPC_RX_MAX_OUTPUT_PORTS);
         return CAPI_EBADPARAM;
      }
      // Prevent starting a closed port
      if (DATA_PORT_STATE_CLOSED == me_ptr->out_port_info[port_index].port_state)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Cannot start a closed output port (Index %lu).", port_index);
         return CAPI_EBADPARAM; // Use a specific error code if available
      }
      me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_STARTED;
   }

   // Update global state for the module, as starting any port affects overall data flow
   me_ptr->dfs                = IPC_RX_DFS_AT_GAP; // Data flow is at gap, waiting for first data buffer
   me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED_OPTIONALLY;

   IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Port %s (Index %lu) started successfully.",
              (is_input ? "input" : "output"), port_index);
   return CAPI_EOK;
}

/**
 * \brief Handles the STOP operation for an input or output port.
 *
 * Sets the specified port's state to STOPPED, indicating that the module
 * should cease expecting data buffers on this port.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_index The index of the port to stop.
 * \param[in] is_input   Boolean flag: TRUE if it's an input port, FALSE if an output port.
 *
 * \return CAPI_EOK on success.
 *         CAPI_EBADPARAM if `me_ptr` is NULL or if `port_index` is out of bounds.
 */
capi_err_t capi_ipc_rx_handle_port_stop(capi_ipc_rx_t *me_ptr, uint32_t port_index, bool_t is_input)
{
   capi_err_t result = CAPI_EOK;
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_handle_port_stop.");
      return CAPI_EBADPARAM;
   }

   if (is_input)
   {
      // Validate input port index
      if (port_index >= IPC_RX_MAX_INPUT_PORTS)
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Input port_index %lu out of bounds (max %lu) during STOP.",
                    port_index,
                    IPC_RX_MAX_INPUT_PORTS);
         return CAPI_EBADPARAM;
      }
      // Only stop if the port is currently in STARTED state
      if (DATA_PORT_STATE_STARTED == me_ptr->in_port_info[port_index].port_state)
      {
         me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Input port (Index %lu) stopped.", port_index);
		 if(me_ptr->ipc_buf_packet_ptr)
		 {
			IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "Freeing data packet as part of input port STOP %lu", port_index);
            result = (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
	     }
      }
      else
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_MED_PRIO,
                    "INFO: Input port (Index %lu) not in STARTED state, no action taken.",
                    port_index);
      }
   }
   else // Output port
   {
      // Validate output port index
      if (port_index >= IPC_RX_MAX_OUTPUT_PORTS)
      {
         IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Output port_index %lu out of bounds (max %lu) during STOP.", port_index, IPC_RX_MAX_OUTPUT_PORTS);
         return CAPI_EBADPARAM;
      }
      // Only stop if the port is currently in STARTED state
      if (DATA_PORT_STATE_STARTED == me_ptr->out_port_info[port_index].port_state)
      {
         me_ptr->out_port_info[port_index].port_state = DATA_PORT_STATE_STOPPED;
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Output port (Index %lu) stopped.", port_index);
      }
      else
      {
         IPC_RX_MSG(me_ptr->miid, DBG_MED_PRIO, "INFO: Output port (Index %lu) not in STARTED state, no action taken.", port_index);
      }
   }
   return CAPI_EOK;
}

/**
 * \brief Destroys (cleans up) resources associated with a specific input port
 * and resets its state to CLOSED.
 *
 * This function is typically called as part of a port CLOSE operation or
 * during the overall module termination (`capi_ipc_rx_end`).
 * It handles freeing any dynamic memory or associated metadata.
 *
 * \param[in,out] me_ptr     Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_index The index of the input port to destroy.
 */
void capi_ipc_rx_destroy_input_port(capi_ipc_rx_t *me_ptr, uint32_t port_index)
{
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_destroy_input_port.");
      return;
   }
   // Validate port index
   if (port_index >= IPC_RX_MAX_INPUT_PORTS)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Input port_index %lu out of bounds (max %lu) during destroy.", port_index, IPC_RX_MAX_INPUT_PORTS);
      return;
   }

   // Free any dynamic memory allocated specifically for this input port.
   // Placeholder for actual resource deallocation.
   // Example: if (me_ptr->in_port_info[port_index].some_dynamic_resource) {
   //    posal_memory_free(me_ptr->in_port_info[port_index].some_dynamic_resource);
   //    me_ptr->in_port_info[port_index].some_dynamic_resource = NULL;
   // }

   // Destroy metadata list if associated with this input port.
   // A dedicated function for destroying metadata lists should be used if available.
   // if (me_ptr->in_port_info[port_index].md_list_ptr)
   // {
   //     // Example: capi_ipc_rx_destroy_md_list(me_ptr, &me_ptr->in_port_info[port_index].md_list_ptr);
   //     me_ptr->in_port_info[port_index].md_list_ptr = NULL; // Clear pointer after destruction
   // }

   // Reset the entire input port info structure to its default (zeroed) state
   memset(&me_ptr->in_port_info[port_index], 0, sizeof(ipc_rx_input_port_info_t));
   me_ptr->in_port_info[port_index].port_state = DATA_PORT_STATE_CLOSED; // Explicitly set state to CLOSED

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Input port (Index %lu) destroyed and reset.", port_index);
}

/**
 * \brief Destroys (cleans up) resources associated with a specific output port
 * and resets its state to CLOSED.
 *
 * This function is typically called as part of a port CLOSE operation or
 * during the overall module termination (`capi_ipc_rx_end`).
 * It handles freeing any dynamic memory.
 *
 * \param[in,out] me_ptr    Pointer to the CAPI IPC_RX instance. Must not be NULL.
 * \param[in] port_idx The index of the output port to destroy.
 */
void capi_ipc_rx_destroy_output_port(capi_ipc_rx_t *me_ptr, uint32_t port_idx)
{
   // Validate input pointer
   if (NULL == me_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN, DBG_ERROR_PRIO, "Error: NULL me_ptr received in capi_ipc_rx_destroy_output_port.");
      return;
   }
   // Validate port index
   if (port_idx >= IPC_RX_MAX_OUTPUT_PORTS)
   {
      IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Output port_idx %lu out of bounds (max %lu) during destroy.", port_idx, IPC_RX_MAX_OUTPUT_PORTS);
      return;
   }

   // Free any dynamic memory allocated specifically for this output port.
   // Placeholder for actual resource deallocation.
   // Example: if (me_ptr->out_port_info[port_idx].some_dynamic_resource) {
   //    posal_memory_free(me_ptr->out_port_info[port_idx].some_dynamic_resource);
   //    me_ptr->out_port_info[port_idx].some_dynamic_resource = NULL;
   // }

   // Reset the entire output port info structure to its default (zeroed) state
   memset(&me_ptr->out_port_info[port_idx], 0, sizeof(ipc_rx_output_port_info_t));
   me_ptr->out_port_info[port_idx].port_state = DATA_PORT_STATE_CLOSED; // Explicitly set state to CLOSED

   IPC_RX_MSG(me_ptr->miid, DBG_LOW_PRIO, "INFO: Output port (Index %lu) destroyed and reset.", port_idx);
}
