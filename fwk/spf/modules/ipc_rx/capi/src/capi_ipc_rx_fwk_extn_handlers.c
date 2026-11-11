/*==============================================================================
@file capi_ipc_rx_fwk_extn_handlers.c
@brief This file implements framework extension handlers for the IPC RX CAPI module.

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
 * \brief Handles IPC port control messages for the IPC_RX module.
 *
 * This function processes control commands received from the framework via GPR packets.
 * It primarily handles media format updates (PARAM_ID_MEDIA_FORMAT) and ensures
 * proper state management during data flow. It also acknowledges the GPR packet
 * upon completion.
 *
 * \param[in] capi_ptr     Pointer to the CAPI IPC_RX instance (`capi_ipc_rx_t`). Must not be NULL.
 * \param[in] is_ext_input Boolean flag indicating if the message is from an external input. (Not used in this handler,
 * for compatibility)
 * \param[in] port_index   The index of the port associated with the message. (Not directly used for control messages,
 * for compatibility)
 * \param[in] pkt_ptr      Pointer to the GPR packet containing the control command. Must not be NULL.
 *
 * \return AR_EOK on success.
 *         AR_EBADPARAM if `capi_ptr` or `pkt_ptr` is NULL.
 *         AR_EUNSUPPORTED if an unsupported opcode is received, or if a media format update
 *                         is received while data is actively flowing (DFS_FLOWING).
 *         Other `ar_result_t` error codes propagated from `ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client`
 *         or `__gpr_cmd_end_command`.
 */
ar_result_t capi_ipc_rx_fwk_extn_ipc_port_ctrl_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr)
{
   ar_result_t result = AR_EOK;

   // Validate input parameters
   if (NULL == capi_ptr || NULL == pkt_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: Fwk extn IPC port control handler received NULL pointer. capi_ptr: 0x%p, pkt_ptr: 0x%p.",
                 capi_ptr,
                 pkt_ptr);
      return AR_EBADPARAM;
   }

   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)(capi_ptr);

   IPC_RX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "INFO: Entered capi_ipc_rx_fwk_extn_ipc_port_ctrl_msg_handler for opcode 0x%lX.",
              pkt_ptr->opcode);

   // Handle the control command based on its opcode
   switch (pkt_ptr->opcode)
   {
      /* ToDo: This handling should move to set param, since ipc rx is setting it through set param */
      case PARAM_ID_MEDIA_FORMAT:
      {
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Processing PARAM_ID_MEDIA_FORMAT control command.");

         // If data is currently flowing, a media format change is not supported
         if (IPC_RX_DFS_FLOWING == me_ptr->dfs)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Input media format control command received while data is flowing (DFS_FLOWING). Not "
                       "supported.");
            result = AR_EUNSUPPORTED;
         }
         else
         {
            // Handle the incoming media format from the GPR client
            result = ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(
               me_ptr, pkt_ptr, FALSE /* data_cmd: This is a control command, not a data command */);
            if (AR_DID_FAIL(result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to handle incoming media format from GPR client, result: %lu.",
                          result);
            }
         }
         break;
      }

      case GPR_IBASIC_RSP_RESULT:
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Received GPR_IBASIC_RSP_RESULT for the PARAM_ID_DOWNSTREAM_FRAME_LENGTH command");
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "freeing the packet inside Ibasic response");
         gpr_ibasic_rsp_result_t *gpr_rsp_ptr = (gpr_ibasic_rsp_result_t *)GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
         IPC_RX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Test client ISR received GPR_IBASIC_RSP_RESULT, status(%lu),opcode(0x%lx)",
                    gpr_rsp_ptr->status,
                    gpr_rsp_ptr->opcode);

         if (gpr_rsp_ptr->status != AR_EOK)
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "GPR_IBASIC_RSP_RESULT failed, status(%lu),clientToken(%lu)",
                       gpr_rsp_ptr->status,
                       pkt_ptr->token);
         }
         __gpr_cmd_free(pkt_ptr);
         break;
      }

      default:
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Received unknown or unsupported opcode 0x%lX in control message handler.",
                    pkt_ptr->opcode);
         result = AR_EUNSUPPORTED;
         break;
      }
   }

   // End the GPR command regardless of the outcome to release the packet
   ar_result_t gpr_cmd_end_result = __gpr_cmd_end_command(pkt_ptr, result);
   if (AR_DID_FAIL(gpr_cmd_end_result))
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Error: Failed to end GPR command 0x%lX, result: %lu.",
                 pkt_ptr->opcode,
                 gpr_cmd_end_result);
      // If ending the GPR command failed, it's a more critical error, so combine results
      result |= gpr_cmd_end_result;
   }

   return result;
}

/**
 * \brief Handles IPC port data messages for the IPC_RX module.
 *
 * This function processes data-related commands received from the framework via GPR packets.
 * It handles data buffers (DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2) and media format updates
 * (DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT) that arrive as data messages. It also supports
 * flush operations for incoming data.
 *
 * \param[in] capi_ptr           Pointer to the CAPI IPC_RX instance (`capi_ipc_rx_t`). Must not be NULL.
 * \param[in] is_ext_input       Boolean flag indicating whether the input is from an external source. (Not used in this
 * handler, for compatibility)
 * \param[in] port_index         The index of the IPC port. (Not directly used in this handler, for compatibility)
 * \param[in] pkt_ptr            Pointer to the GPR packet containing the data command. Must not be NULL.
 * \param[in] ipc_data_msg_flag_mask A mask indicating special flags for the data message, e.g., FLUSH.
 *
 * \return AR_EOK on success.
 *         AR_EBADPARAM if `capi_ptr` or `pkt_ptr` is NULL.
 *         Other `ar_result_t` error codes propagated from internal handling functions
 *         like `ipc_rx_flush_handling`, `ipc_rx_input_data_buffer_set_up_gpr_client_v2`,
 *         `ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client`, or `__gpr_cmd_end_command`.
 */
ar_result_t capi_ipc_rx_fwk_extn_ipc_port_data_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr,
                                                           uint32_t      ipc_data_msg_flag_mask)
{
   ar_result_t result = AR_EOK;
   ar_result_t status = AR_EOK;

   // Validate input parameters
   if (NULL == capi_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: Fwk extn IPC port data handler received NULL pointer. capi_ptr: 0x%p, pkt_ptr: 0x%p.",
                 capi_ptr,
                 pkt_ptr);
      return AR_EBADPARAM;
   }

   capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)capi_ptr;

   if (FWK_EXT_IPC_PORT_FLAG_UNDERRUN & ipc_data_msg_flag_mask)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "INFO: Setting allow underrun flag in capi_ipc_rx_fwk_extn_ipc_port_data_msg_handler");

      me_ptr->need_to_underrun = TRUE;

      // if data flow is at gap, it could be need optionally or not needed optionally
      me_ptr->input_trigger_info =
         (IPC_RX_DFS_FLOWING == me_ptr->dfs) ? FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED : me_ptr->input_trigger_info;
      return AR_EOK;
   }

   IPC_RX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "INFO: Entered capi_ipc_rx_fwk_extn_ipc_port_data_msg_handler for opcode 0x%lX, flags: 0x%lX.",
              pkt_ptr->opcode,
              ipc_data_msg_flag_mask);

   // If a flush command is indicated, handle it immediately
   if (FWK_EXT_IPC_PORT_FLAG_FLUSH == ipc_data_msg_flag_mask)
   {
      ar_result_t flush_result = ipc_rx_flush_handling(me_ptr, pkt_ptr, FALSE);
      if (AR_DID_FAIL(flush_result))
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Failed to handle flush for GPR packet 0x%lX, result: %lu.",
                    pkt_ptr->opcode,
                    flush_result);
         result |= flush_result; // Combine results, but continue processing if possible
      }
      // For flush, processing stops here as the packet is handled by ipc_rx_flush_handling
      return result;
   }

   // note that pkt is optional for FLUSH, hence checking nullness after handling flush cmd.
   if (NULL == pkt_ptr)
   {
      IPC_RX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Error: Fwk extn IPC port data handler received NULL pointer. capi_ptr: 0x%p, pkt_ptr: 0x%p.",
                 capi_ptr,
                 pkt_ptr);
      return AR_EBADPARAM;
   }

   // Handle the data command based on its opcode
   switch (pkt_ptr->opcode)
   {
      case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
      {
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Processing DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2 data command.");
         result = ipc_rx_input_data_buffer_set_up_gpr_client_v2(me_ptr, pkt_ptr);
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to set up input data buffer from GPR client, result: %lu.",
                       result);
         }
         break;
      }

      case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
      {
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Processing DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT data command.");

         // Handle the incoming media format as a data command
         result = ipc_rx_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(
            me_ptr, pkt_ptr, TRUE /* data_cmd: This is a data command carrying MF */);
         if (AR_DID_FAIL(result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to handle incoming media format from GPR client, result: %lu.",
                       result);
         }

         // Send acknowledgment only if there is no pending media format to be processed
         if (FALSE == me_ptr->is_pending_mf)
         {
            status = result; // Use the result of handling the media format as the status for ending the command
            ar_result_t gpr_cmd_end_result = __gpr_cmd_end_command(pkt_ptr, status);
            if (AR_DID_FAIL(gpr_cmd_end_result))
            {
               IPC_RX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Error: Failed to end GPR command 0x%lX for media format, result: %lu.",
                          pkt_ptr->opcode,
                          gpr_cmd_end_result);
               result |= gpr_cmd_end_result; // Combine results
            }
         }
         // If there is a pending MF, the pkt_ptr will be stored and acknowledged later.
         break;
      }

      default:
      {
         IPC_RX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Error: Received unknown or unsupported opcode 0x%lX in data message handler.",
                    pkt_ptr->opcode);
         result = AR_EUNSUPPORTED;
         // For unsupported opcodes, still end the command to prevent leakage
         ar_result_t gpr_cmd_end_result = __gpr_cmd_end_command(pkt_ptr, result);
         if (AR_DID_FAIL(gpr_cmd_end_result))
         {
            IPC_RX_MSG(me_ptr->miid,
                       DBG_ERROR_PRIO,
                       "Error: Failed to end unsupported GPR command 0x%lX, result: %lu.",
                       pkt_ptr->opcode,
                       gpr_cmd_end_result);
         }
         break;
      }
   }

   return result;
}

/////////////////////////////////////////////////////////////////////////////////////////
//////////////////////// Module buffer access extension related functions ///////////////
/////////////////////////////////////////////////////////////////////////////////////////

capi_err_t capi_ipc_rx_check_n_enable_buffer_extn(capi_ipc_rx_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   /** check if extension needs to be enabled*/
   bool_t is_enable = FALSE;

   if ( CAPI_CMN_IS_PCM_FORMAT(me_ptr->output_media_fmt.header.format_header.data_format) &&
      ( (CAPI_INTERLEAVED == me_ptr->output_media_fmt.format.data_interleaving) ||
       (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->output_media_fmt.format.data_interleaving) ) )

   {
      is_enable = TRUE;
   }

   // nothing to do
   if (is_enable == me_ptr->is_mod_buf_access_enabled)
   {
      return capi_result;
   }

   intf_extn_event_id_module_buffer_access_enable_v2_t cfg = { 0 };
   cfg.enable                                              = is_enable;
   cfg.buffer_mgr_cb_handle                                = (uint32_t)me_ptr;
   cfg.get_port_buf_fn = NULL; // this function is not required since gen topo and PTC expect modules to share
   // the buffer as part of the capi proess in the output stream and not through the module interface.
   cfg.return_port_buf_fn = capi_ipc_rx_intf_extn_return_mod_output_buf;

   capi_result = capi_cmn_intf_extn_event_module_port_buffer_reuse_v2(me_ptr->miid,
                                                                      &me_ptr->cb_info,
                                                                      0,     // port_index
                                                                      FALSE, // is_input_port
                                                                      &cfg);

   me_ptr->is_mod_buf_access_enabled = CAPI_FAILED(capi_result) ? FALSE : is_enable;
   return CAPI_EOK;
}

// capi_err_t capi_ipc_rx_intf_extn_get_mod_output_buf(uint32_t    handle,
//                                                    uint32_t    port_index,
//                                                    uint32_t   *num_bufs_ptr,
//                                                    capi_buf_t *buffer_ptr)
// {
//    capi_ipc_rx_t *me_ptr = (capi_ipc_rx_t *)handle;
//    //uint32_t curr_buff_index = me_ptr->sh_data_buf.curr_buff_index;
//    uint32_t num_bufs = *num_bufs_ptr;

//    // is get is called only for input ports
//    if (me_ptr->curr_shared_buf_ptr)
//    {
//       IPC_RX_MSG(me_ptr->miid,
//                  DBG_ERROR_PRIO,
//                  "Cannot query another buf without returning prev buffer 0x%lx",
//                  me_ptr->curr_shared_buf_ptr);
//       capi_cmn_crash();
//       return CAPI_EFAILED;
//    }

//    if(FWK_EXTN_IPC_PORT_BUFFER_NEEDED == me_ptr->output_trigger_info)
//    {
//       IPC_RX_MSG(me_ptr->miid,
//                  DBG_ERROR_PRIO,
//                  "Buffer not present for the output trigger info %d");
//       return CAPI_EFAILED;
//    }

//    uint32_t num_bufs_per_strm = ipc_rx_get_num_bufs_from_mf(me_ptr);
//    uint32_t actual_len_per_buf  = topo_div_num(me_ptr->ipc_buf_actual_data_len, num_bufs_per_strm);

//    if(buffer_ptr->max_data_len > actual_len_per_buf)
//    {
//       IPC_RX_MSG(me_ptr->miid,
//               DBG_ERROR_PRIO,
//               "Invalid buf size %lu cannot be > %lu",
//               buffer_ptr->max_data_len,
//               actual_len_per_buf);
//       return CAPI_EFAILED;
//    }

// #ifdef DEBUG_IPC_TX
//    if(FALSE == me_ptr->is_mod_buf_access_enabled)
//    {
//       IPC_RX_MSG(me_ptr->miid,
//                  DBG_ERROR_PRIO,
//                  "Module buffer access extension is not enabled, get buf failed");
//       return CAPI_EFAILED;
//    }

//    if (num_bufs != num_bufs_per_strm)
//    {
//       IPC_RX_MSG(me_ptr->miid,
//                  DBG_ERROR_PRIO,
//                  "Invalid num_bufs %lu cannot be > %lu num_channels",
//                  num_bufs,
//                  num_bufs_per_strm);
//       return CAPI_EFAILED;
//    }
// #endif

//    int8_t* src_ptr = me_ptr->ipc_buf_virtual_addr + (me_ptr->ipc_buf_size - me_ptr->ipc_buf_actual_data_len);
//    // if module input unpacked, IPC module needs to convert to packed.
//    int8_t *buf_ptr = (int8_t*)me_ptr->sh_data_buf.curr_buff;
//    uint32_t sh_buf_max_bytes_per_ch = (num_bufs > 1) ? capi_cmn_divide(me_ptr->sh_data_buf.data_buff_size, num_bufs)
//                                                      : sh_buf_max_bytes_per_ch;

//    for(uint32_t i =0; i < num_bufs; i++)
//    {
//       buffer_ptr[i].data_ptr = buf_ptr;
//       buffer_ptr->actual_data_len = 0;
//       buf_ptr += sh_buf_max_bytes_per_ch;
//    }
//    return CAPI_EOK;
// }

capi_err_t capi_ipc_rx_intf_extn_return_mod_output_buf(uint32_t    handle,
                                                       uint32_t    port_index,
                                                       uint32_t   *num_bufs_ptr,
                                                       capi_buf_t *buffer_ptr)
{
   capi_err_t     capi_result = CAPI_EOK;
   capi_ipc_rx_t *me_ptr      = (capi_ipc_rx_t *)handle;

#ifdef DEBUG_IPC_RX
   IPC_RX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "Buffer shared with fwk is being returned to module 0x%p curr_shared_buf_ptr 0x%p",
              buffer_ptr->data_ptr,
              me_ptr->curr_shared_buf_ptr);
#endif

   // is get is called only for input ports
   if (buffer_ptr->data_ptr != me_ptr->curr_shared_buf_ptr)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Unexpected! buffer was not shared cannot be returned to module 0x%p curr_shared_buf_ptr 0x%p",
                 buffer_ptr->data_ptr,
                 me_ptr->curr_shared_buf_ptr);
      return CAPI_EFAILED;
   }

   if (FALSE == me_ptr->is_mod_buf_access_enabled)
   {
      IPC_RX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Module buffer access extension is not enabled, return buf is not expected");
      capi_cmn_crash();
   }

   if (check_if_in_buffer_range(me_ptr->curr_shared_buf_ptr, me_ptr->ipc_buf_virtual_addr, me_ptr->ipc_buf_size))
   {
      // free the IPC packet if the buffer is empty
      /* Free the input data command and send acknowledgement.
       * This is only done if the IPC buffer was fully consumed by capi_ipc_rx_read_data. */
      if (NULL != me_ptr->ipc_buf_packet_ptr && 0 == me_ptr->ipc_buf_actual_data_len)
      {
         capi_result = (capi_err_t)ipc_rx_free_input_data_cmd(me_ptr, me_ptr->ipc_buf_packet_ptr, AR_EOK, FALSE, TRUE);
         if (AR_DID_FAIL(capi_result))
         {
            IPC_RX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Error: Failed to send ACK for data buffer: %lu", capi_result);
            return capi_result; /* Propagate error */
         }
#ifdef DEBUG_IPC_RX
         IPC_RX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "INFO: Sent ACK for data buffer after full consumption.");
#endif

         /* Set the shared pointer so that the container starts listening to the external input port queue again */
         me_ptr->input_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;
      }
      else if (NULL != me_ptr->ipc_buf_packet_ptr && me_ptr->ipc_buf_actual_data_len > 0)
      {
         /* If IPC buffer still has data, keep it as BUFFER_NOT_NEEDED for next process call
          * No change to me_ptr->input_trigger_info here as it's already NOT_NEEDED */
         IPC_RX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "INFO: IPC buffer not fully consumed (%lu bytes remain). Will continue processing in next call.",
                    me_ptr->ipc_buf_actual_data_len);
      }
   }
   else if (me_ptr->curr_shared_buf_ptr != me_ptr->int_buf_ptr)
   {
      // internal buffer was shared with the fwk, hence nothing to do
      IPC_RX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "INFO: Invalid buffer %p ptr is returned by fwk doesnt match int buf ptr %p",
                 me_ptr->curr_shared_buf_ptr,
                 me_ptr->int_buf_ptr);
   }

   me_ptr->curr_shared_buf_ptr = NULL;
   return CAPI_EOK;
}