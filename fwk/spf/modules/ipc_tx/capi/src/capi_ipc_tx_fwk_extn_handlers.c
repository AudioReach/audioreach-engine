/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
 * =========================================================================*/

/**
 * \file capi_ipc_tx_fwk_extn_handlers.cpp
 *
 * C source file to implement the framework extension handlers for IPC_TX module
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     ------------------------------------------------

 * =========================================================================*/

/**----------------------------------------------------------------------------
** Include Files
** -------------------------------------------------------------------------*/

#include "capi_ipc_tx_utils.h"

ar_result_t capi_ipc_tx_fwk_extn_ipc_port_ctrl_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr)
{
   ar_result_t capi_result = AR_EOK;
   // handle GPR_IBASIC_RSP_RESULT for the DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT command

   if ((NULL == capi_ptr) || (NULL == pkt_ptr))
   {
      IPC_TX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Fwk extn IPC port handler received bad pointer, 0x%p, 0x%p",
                 capi_ptr,
                 pkt_ptr);
      return CAPI_EBADPARAM;
   }
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)(capi_ptr);
   switch (pkt_ptr->opcode)
   {
      case PARAM_ID_DOWNSTREAM_FRAME_LENGTH:
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Command control handler received PARAM_ID_DOWNSTREAM_FRAME_LENGTH command");
         // get media format update cmd payload
         param_id_downstream_frame_length_t *ds_fr_len_ptr =
            GPR_PKT_GET_PAYLOAD(param_id_downstream_frame_length_t, pkt_ptr);
         if (NULL == ds_fr_len_ptr)
         {
            IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Fwk extn IPC port handler received bad command payload, 0x%p", ds_fr_len_ptr);

            // free the packet before exiting
            __gpr_cmd_free(pkt_ptr);
            return CAPI_EBADPARAM;
         }
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Received PARAM_ID_DOWNSTREAM_FRAME_LENGTH command. us: %ld, bytes: %ld",
                    ds_fr_len_ptr->peer_frame_len.frame_len_us,
                    ds_fr_len_ptr->peer_frame_len.frame_len_bytes);

         me_ptr->downstream_fm_len.frame_len_us = ds_fr_len_ptr->peer_frame_len.frame_len_us;
         me_ptr->downstream_fm_len.frame_len_bytes = ds_fr_len_ptr->peer_frame_len.frame_len_bytes;
         me_ptr->is_ds_frame_len_received          = TRUE;

         // free the packet before exiting
         __gpr_cmd_free(pkt_ptr);
         break;
      }
      case GPR_IBASIC_RSP_RESULT:
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Received GPR_IBASIC_RSP_RESULT for the DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT command");
         IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "freeing the packet inside Ibasic response");
         gpr_ibasic_rsp_result_t *gpr_rsp_ptr = (gpr_ibasic_rsp_result_t *)GPR_PKT_GET_PAYLOAD(void, pkt_ptr);
         IPC_TX_MSG(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "Test client ISR received GPR_IBASIC_RSP_RESULT, status(%lu),opcode(0x%lx)",
                    gpr_rsp_ptr->status,
                    gpr_rsp_ptr->opcode);

         if (gpr_rsp_ptr->status != AR_EOK)
         {
            IPC_TX_MSG(me_ptr->miid,
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
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Received unknown opcode 0x%lx. Freeing it.", pkt_ptr->opcode);
         __gpr_cmd_free(pkt_ptr);
         break;
      }
   }
   return capi_result;
}

/**
 * \brief Handles the IPC port data message for the write done event.
 * This function processes the write done event received from the framework extension handler,
 * checks the validity of the input parameters and the opcode, and extracts the write done payload from the GPR packet.
 * It then checks the status of the data and metadata writes, and if both are successful,
 * updates the output trigger information and resets the shared pointer to indicate that the container should stop
 * listening to the external output port queue.
 * The function logs messages to indicate the progress and results of the
 * operation, and returns an error code if any of the checks fail.
 *
 * \param capi_ptr Pointer to the capi_t structure.
 * \param is_ext_input Indicates whether the input is from an external source.
 * \param port_index Index of the IPC port.
 * \param pkt_ptr Pointer to the GPR packet.
 *
 * \return ar_result_t Result of the operation, which is either an error code or a success code.
 */
ar_result_t capi_ipc_tx_fwk_extn_ipc_port_data_msg_handler(void         *capi_ptr,
                                                           uint32_t      is_ext_input,
                                                           uint32_t      port_index,
                                                           gpr_packet_t *pkt_ptr,
                                                           uint32_t      ipc_data_msg_flag_mask)
{
   ar_result_t capi_result = AR_EOK;
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)(capi_ptr);

   if (NULL == capi_ptr)
   {
      IPC_TX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Fwk extn IPC port handler received bad pointer, 0x%p, 0x%p",
                 capi_ptr,
                 pkt_ptr);
      return CAPI_EBADPARAM;
   }

   me_ptr->need_to_overrun = FALSE;
   if (FWK_EXT_IPC_PORT_FLAG_OVERRUN & ipc_data_msg_flag_mask)
   {
      me_ptr->need_to_overrun = TRUE;

      // update the trigger as not needed since module is going to underrun in the process context.
      me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED;
      return capi_result;
   }

   if (NULL == pkt_ptr)
   {
      IPC_TX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Fwk extn IPC port handler received bad pointer, 0x%p, 0x%p",
                 capi_ptr,
                 pkt_ptr);
      return CAPI_EBADPARAM;
   }

   if (DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2 != pkt_ptr->opcode) // todo review: are we only expecting this API?
   {
      IPC_TX_MSG(MIID_UNKNOWN,
                 DBG_ERROR_PRIO,
                 "Write done: Fwk extn IPC port handler received bad opcode, 0x%lx",
                 pkt_ptr->opcode);

      __gpr_cmd_free(pkt_ptr); /* Always free the original packet after processing */

      return CAPI_EBADPARAM;
   }

   // Extract write done payload from GPR packet
   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t *write_done_payload_ptr =
      (data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t *)GPR_PKT_GET_PAYLOAD(void, pkt_ptr);

   // Check for valid payload
   if (NULL == write_done_payload_ptr)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "Write done: Fwk extn IPC port handler received NULL payload ptr for buffer done event, 0x%p",
                 write_done_payload_ptr);
      __gpr_cmd_free(pkt_ptr);
      return CAPI_EBADPARAM;
   }

   // update the ext_port_trigger_shared_ptr
   // update the shared trigger ptr so that the contanier
   // stops listening to the ext output port queue and process is triggered
   me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NOT_NEEDED;

   // populate the payload
   // me_ptr->write_done_payload.data_buf_addr_lsw   = write_done_payload_ptr->data_buf_addr_lsw;
   // me_ptr->write_done_payload.data_buf_addr_msw   = write_done_payload_ptr->data_buf_addr_msw;
   // me_ptr->write_done_payload.data_mem_map_handle = write_done_payload_ptr->data_mem_map_handle;
   // me_ptr->write_done_payload.data_status         = write_done_payload_ptr->data_status;
   // me_ptr->write_done_payload.md_buf_addr_lsw     = write_done_payload_ptr->md_buf_addr_lsw;
   // me_ptr->write_done_payload.md_buf_addr_msw     = write_done_payload_ptr->md_buf_addr_msw;
   // me_ptr->write_done_payload.md_mem_map_handle   = write_done_payload_ptr->md_mem_map_handle;

   // this is offset: me_ptr->sh_buf_info.curr_buff = (uint8_t*)write_done_payload_ptr->data_buf_addr_lsw;
   // correct it to point to the start of the shared buffer - shm_mem_ptr
#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "Write done: data_buf_addr_lsw 0x%lx data status %lu md status %lu, me_ptr->output_trigger_info %lu "
              "&me_ptr->output_trigger_info 0x%lx, num_ipc_bufs_created %lu",
              write_done_payload_ptr->data_buf_addr_lsw,
              write_done_payload_ptr->data_status,
              write_done_payload_ptr->md_status,
              me_ptr->output_trigger_info,
              &me_ptr->output_trigger_info,
              me_ptr->sh_buf_info.num_ipc_bufs_created);
#endif
   if ((AR_EOK != write_done_payload_ptr->data_status) || (AR_EOK != write_done_payload_ptr->md_status))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Write done: Fwk extn IPC port handler received bad status");
      // TBD: do we need to return from here?
   }

   __gpr_cmd_free(pkt_ptr);
   // if ((DATA_PORT_STATE_CLOSED == me_ptr->in_port_info[port_index].port_state) && (FWK_EXT_IPC_PORT_FLAG_DESTROY_BUFS
   // == ipc_data_msg_flag_mask))
   // {
   //    capi_result = capi_ipc_tx_free_shmem(me_ptr);
   //    return capi_result;
   // }

   uint32_t buffer_index = 0;
   while (buffer_index < me_ptr->sh_buf_info.num_ipc_bufs_created)
   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Write done: buffer_index %lu num_ipc_bufs_created %lx data_mem_map_handle %lx master_handle %lx "
                 "data_buf_addr_lsw %lx offset %lx",
                 buffer_index,
                 me_ptr->sh_buf_info.num_ipc_bufs_created,
                 write_done_payload_ptr->data_mem_map_handle,
                 me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.sat_handle,
                 write_done_payload_ptr->data_buf_addr_lsw,
                 me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset);
#endif

      if ((write_done_payload_ptr->data_mem_map_handle ==
           me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.sat_handle) &&
          (write_done_payload_ptr->data_buf_addr_lsw ==
           me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset))
      {

#ifdef DEBUG_IPC_TX
         IPC_TX_MSG(me_ptr->miid,
                    DBG_LOW_PRIO,
                    "Write done:  me_ptr->sh_buf_info.shared_mem_buf_handle[%lu].mem_attr.offset 0x%lx, port_state "
                    "0x%lx, ipc_data_msg_flag_mask %lu, buffer status 0X%x",
                    buffer_index,
                    me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].mem_attr.offset,
                    me_ptr->out_port_info[port_index].port_state,
                    ipc_data_msg_flag_mask,
                    me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].status);
#endif

         me_ptr->sh_buf_info.curr_buff_index = buffer_index;

         if (((DATA_PORT_STATE_STOPPED == me_ptr->out_port_info[port_index].port_state) &&
              (FWK_EXT_IPC_PORT_FLAG_DESTROY_BUFS == ipc_data_msg_flag_mask)) ||
             (IPC_BUF_READY_TO_DESTROY == me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].status))
         {
            uint32_t buffer_index       = me_ptr->sh_buf_info.curr_buff_index;
            capi_result                 = capi_ipc_tx_free_shmem(me_ptr, buffer_index);
            me_ptr->output_trigger_info = FWK_EXTN_IPC_PORT_BUFFER_NEEDED;

            return capi_result;
         }
         else if ((me_ptr->sh_buf_info.data_buff_size + me_ptr->sh_buf_info.metadata_buff_size) !=
                  me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].shm_alloc_size)
         {
#ifdef DEBUG_IPC_TX
            IPC_TX_MSG(me_ptr->miid,
                       DBG_LOW_PRIO,
                       "Write done: Fwk extn IPC port handler, size of returned buffer: %lu is not same as needed "
                       "size: %lu or marked for destroy :%d",
                       me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].shm_alloc_size,
                       me_ptr->sh_buf_info.shmem_alloc_size,
                       me_ptr->sh_buf_info.shared_mem_buf_handle[buffer_index].status);
#endif

            capi_result = capi_ipc_tx_manage_buffer(me_ptr,
                                                    me_ptr->sh_buf_info.data_buff_size,
                                                    me_ptr->sh_buf_info.metadata_buff_size);
            if (capi_result != CAPI_EOK)
            {
               IPC_TX_MSG(me_ptr->miid,
                          DBG_ERROR_PRIO,
                          "Write done: Fwk extn IPC port handler, failed to manage buffer");
               // return from here?
            }
         }
         break; // Exit the loop once the matching buffer index is found
      }
      buffer_index++;
   }
   return capi_result;
}


capi_err_t capi_ipc_tx_check_n_enable_buffer_access_extn(capi_ipc_tx_t *me_ptr)
{
   capi_err_t capi_result = CAPI_EOK;

   /** check if extension needs to be enabled*/
   bool_t is_enable = FALSE;
   if (me_ptr->is_inp_media_fmt_received &&
         ((CAPI_CMN_IS_PCM_FORMAT(me_ptr->inp_media_fmt.header.format_header.data_format) &&
         (CAPI_INTERLEAVED == me_ptr->inp_media_fmt.format.data_interleaving ||
          CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->inp_media_fmt.format.data_interleaving))))
   {
      is_enable = TRUE;
   }

   // nothing to do
   if (is_enable == me_ptr->is_mod_buf_access_enabled)
   {
      return capi_result;
   }

   intf_extn_event_id_module_buffer_access_enable_v2_t cfg = {0};
   cfg.enable = is_enable;
   cfg.buffer_mgr_cb_handle = (uint32_t)me_ptr;
   cfg.get_port_buf_fn      = capi_ipc_tx_intf_extn_get_mod_input_buf;
   cfg.return_port_buf_fn   = capi_ipc_tx_intf_extn_return_mod_input_buf;

   capi_result = capi_cmn_intf_extn_event_module_port_buffer_reuse_v2(me_ptr->miid,
                                                                     &me_ptr->cb_info,
                                                                     0,                // port_index
                                                                     TRUE,             // is_input_port
                                                                     &cfg);

   me_ptr->is_mod_buf_access_enabled = CAPI_FAILED(capi_result) ? FALSE : is_enable;
   return CAPI_EOK;
}

capi_err_t capi_ipc_tx_intf_extn_get_mod_input_buf(uint32_t    handle,
                                                   uint32_t    port_index,
                                                   uint32_t   *num_bufs_ptr,
                                                   capi_buf_t *buffer_ptr)
{
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)handle;
   // uint32_t curr_buff_index = me_ptr->sh_buf_info.curr_buff_index;
   uint32_t curr_buff_size = me_ptr->sh_buf_info.data_buff_size;
   uint32_t num_bufs       = *num_bufs_ptr;

   // is get is called only for input ports
   if (me_ptr->curr_shared_buf_ptr)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Cannot query another buf without returning prev buffer 0x%lx",
                 me_ptr->curr_shared_buf_ptr);
      capi_cmn_crash();
      return CAPI_EFAILED;
   }

   if (buffer_ptr->max_data_len > curr_buff_size)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Invalid buf size %lu cannot be > %lu",
                 buffer_ptr->max_data_len,
                 curr_buff_size);
      return CAPI_EFAILED;
   }

   if (FWK_EXTN_IPC_PORT_BUFFER_NEEDED == me_ptr->output_trigger_info)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Buffer not present for the output trigger info %d");
      return CAPI_EFAILED;
   }

   if (FALSE == me_ptr->is_mod_buf_access_enabled)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Module buffer access extension is not enabled, get buf failed");
      return CAPI_EFAILED;
   }

   if (me_ptr->sh_buf_info.data_buff_size < buffer_ptr->max_data_len)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Shared buffer size %lu is less than requested buffer size %lu",
                 me_ptr->sh_buf_info.data_buff_size,
                 buffer_ptr->max_data_len);
      return CAPI_EFAILED;
   }

#ifdef DEBUG_IPC_TX
   uint32_t num_bufs_per_strm = 1;
   if (CAPI_CMN_IS_PCM_FORMAT(me_ptr->inp_media_fmt.header.format_header.data_format) &&
       (CAPI_DEINTERLEAVED_UNPACKED_V2 == me_ptr->inp_media_fmt.format.data_interleaving))
   {
      num_bufs_per_strm = me_ptr->inp_media_fmt.format.num_channels;
   }

   if (num_bufs != num_bufs_per_strm)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Invalid num_bufs %lu cannot be > %lu num_channels",
                 num_bufs,
                 num_bufs_per_strm);
      return CAPI_EFAILED;
   }

   IPC_TX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "MOD_BUF_ACCESS_DEBUG: curr_buff_index %lu shm_mem_ptr 0x%p data_buff_size %lu num_bufs %lu "
              "frame_size_per_ch_in_bytes %lu frame_size_in_bytes %lu",
              me_ptr->sh_buf_info.curr_buff_index,
              me_ptr->sh_buf_info.shared_mem_buf_handle[me_ptr->sh_buf_info.curr_buff_index].shm_mem_ptr,
              me_ptr->sh_buf_info.data_buff_size,
              num_bufs,
              me_ptr->sh_buf_info.frame_size_per_ch_in_bytes,
              me_ptr->sh_buf_info.frame_size_in_bytes);
#endif

   // if module input unpacked, IPC module needs to convert to packed.
   int8_t *buf_ptr =
      (int8_t *)me_ptr->sh_buf_info.shared_mem_buf_handle[me_ptr->sh_buf_info.curr_buff_index].shm_mem_ptr;

   if (me_ptr->need_to_overrun)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Warning! Buffer not present, using overrun buffer 0x%p",
                 me_ptr->sh_buf_info.overrun_buffer_ptr);

      buf_ptr = (int8_t *)me_ptr->sh_buf_info.overrun_buffer_ptr;
   }

   // cache the shared buffer ptr
   me_ptr->curr_shared_buf_ptr = buf_ptr;

   uint32_t sh_buf_max_bytes_per_ch =
      (num_bufs > 1) ? me_ptr->sh_buf_info.frame_size_per_ch_in_bytes : me_ptr->sh_buf_info.frame_size_in_bytes;

   for (uint32_t i = 0; i < num_bufs; i++)
   {
      buffer_ptr[i].data_ptr = buf_ptr;

      // actual len is expected to reset at this point
      // buffer_ptr->actual_data_len = 0;

      buf_ptr += sh_buf_max_bytes_per_ch;
   }


   return CAPI_EOK;
}

capi_err_t capi_ipc_tx_intf_extn_return_mod_input_buf(uint32_t    handle,
                                                      uint32_t    port_index,
                                                      uint32_t   *num_bufs_ptr,
                                                      capi_buf_t *buffer_ptr)
{
   capi_ipc_tx_t *me_ptr = (capi_ipc_tx_t *)handle;

   if(FALSE == me_ptr->is_mod_buf_access_enabled)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Module buffer access extension is not enabled, return buf is not expected");
      capi_cmn_crash();
   }

   // is get is called only for input ports
   if (buffer_ptr->data_ptr != me_ptr->curr_shared_buf_ptr)
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "MOD_BUF_ACCESS_DEBUG: Unexpected! buffer 0x%p was not shared cannot be returned to module 0x%p",
                 buffer_ptr->data_ptr,
                 me_ptr->curr_shared_buf_ptr);
      return CAPI_EFAILED;
   }

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_LOW_PRIO,
              "MOD_BUF_ACCESS_DEBUG: Fwk returned shared buffer 0x%p to module",
              me_ptr->curr_shared_buf_ptr);
#endif

   me_ptr->curr_shared_buf_ptr = NULL;
   return CAPI_EOK;
}