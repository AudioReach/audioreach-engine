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

/**----------------------------------------------------------------------------
** Function Definitions
** -------------------------------------------------------------------------*/

static inline uint16_t capi_interleaved_to_generic_interleaved(capi_interleaving_t capi_data_interleaving)
{
    uint16_t pcm_interleaving = CAPI_INTERLEAVED;
    switch (capi_data_interleaving)
    {
    case CAPI_INTERLEAVED:
        pcm_interleaving = PCM_INTERLEAVED;
        break;
    case CAPI_DEINTERLEAVED_PACKED:
        pcm_interleaving = PCM_DEINTERLEAVED_PACKED;
        break;
    case CAPI_DEINTERLEAVED_UNPACKED_V2:
        pcm_interleaving = PCM_DEINTERLEAVED_UNPACKED;
        break;
    default:
        break;
    }
    return pcm_interleaving;
}
//todo will remove this later
void capi_ipc_tx_copy_inp_media_fmt(capi_media_fmt_v2_t *dest_ptr, capi_media_fmt_v2_t *src_ptr)
{ // todo, change the pointers to pass by value.
   memscpy(dest_ptr, sizeof(capi_media_fmt_v2_t), src_ptr, sizeof(capi_media_fmt_v2_t));
}

capi_err_t capi_ipc_tx_update_media_fmt(capi_ipc_tx_t       *me_ptr,
                                        capi_media_fmt_v2_t  inp_data_fmt,
                                        uint32_t             param_actual_data_len)
{
   capi_err_t capi_result = CAPI_EOK;

   uint32_t        data_buffer_size = 0;
   uint32_t        payload_size     = 0;
   uint8_t        *wr_payload_ptr   = NULL;
   media_format_t *wr_media_fmt_ptr = NULL;

   // Handling of compressed raw data
   if (CAPI_RAW_COMPRESSED == inp_data_fmt.header.format_header.data_format)
   {
      payload_size = sizeof(media_format_t) + param_actual_data_len;

      wr_payload_ptr = (uint8_t *)posal_memory_malloc(payload_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
      if (NULL == wr_payload_ptr)
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Couldn't allocate memory for compressed media format payload for IPC module");
         return CAPI_ENOMEMORY;
      }
      memset(wr_payload_ptr, 0, payload_size);

      wr_media_fmt_ptr           = (media_format_t *)(wr_payload_ptr);
      uint8_t *raw_media_fmt_ptr = (uint8_t *)(wr_payload_ptr + sizeof(media_format_t));

      // Fill the media_format_t structure
      wr_media_fmt_ptr->data_format  = DATA_FORMAT_RAW_COMPRESSED;
      wr_media_fmt_ptr->fmt_id       = inp_data_fmt.format.bitstream_format;
      wr_media_fmt_ptr->payload_size = param_actual_data_len;

      // Fill the raw media format details
      memscpy((void *)raw_media_fmt_ptr, param_actual_data_len, (void *)&inp_data_fmt, param_actual_data_len);

      data_buffer_size =
         IPC_TX_RAW_COMP_BUFFER_SIZE; // tbd: update this with the frame_bytes we receive from a new fwk extn
   }
   // Handling for PCM DATA format
   else if (MEDIA_FMT_ID_PCM == inp_data_fmt.format.bitstream_format)
   {
      payload_size = sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t) +
                     ALIGN_4_BYTES(inp_data_fmt.format.num_channels * sizeof(uint8_t));

      wr_payload_ptr = (uint8_t *)posal_memory_malloc(payload_size, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
      if (NULL == wr_payload_ptr)
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Couldn't allocate memory for PCM media format payload for IPC module");
         return CAPI_ENOMEMORY;
      }
      memset(wr_payload_ptr, 0, payload_size);

      wr_media_fmt_ptr                          = (media_format_t *)(wr_payload_ptr);
      payload_media_fmt_pcm_t *pcm_fmt_extn_ptr = (payload_media_fmt_pcm_t *)(wr_payload_ptr + sizeof(media_format_t));

      uint8_t *channel_mapping = (uint8_t *)(pcm_fmt_extn_ptr + 1);

      if (CAPI_FIXED_POINT == inp_data_fmt.header.format_header.data_format)
      {
         // Fill the  media_format_t structure
         wr_media_fmt_ptr->data_format = DATA_FORMAT_FIXED_POINT;
         wr_media_fmt_ptr->fmt_id      = inp_data_fmt.format.bitstream_format;
         wr_media_fmt_ptr->payload_size =
            sizeof(payload_media_fmt_pcm_t) + pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t);
      }
      else if (CAPI_FLOATING_POINT == inp_data_fmt.header.format_header.data_format)
      {
         // Fill the  media_format_t structure
         wr_media_fmt_ptr->data_format = DATA_FORMAT_FLOATING_POINT;
         wr_media_fmt_ptr->fmt_id      = inp_data_fmt.format.bitstream_format;
         wr_media_fmt_ptr->payload_size =
            sizeof(payload_media_fmt_pcm_t) + pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t);
      }

      pcm_fmt_extn_ptr->alignment       = PCM_LSB_ALIGNED;
      pcm_fmt_extn_ptr->bit_width       = inp_data_fmt.format.bits_per_sample;
      pcm_fmt_extn_ptr->bits_per_sample = inp_data_fmt.format.bits_per_sample;
      pcm_fmt_extn_ptr->endianness      = PCM_LITTLE_ENDIAN;
      pcm_fmt_extn_ptr->interleaved  = capi_interleaved_to_generic_interleaved(inp_data_fmt.format.data_interleaving);
      pcm_fmt_extn_ptr->num_channels = inp_data_fmt.format.num_channels;
      pcm_fmt_extn_ptr->q_factor     = inp_data_fmt.format.q_factor;
      pcm_fmt_extn_ptr->sample_rate  = inp_data_fmt.format.sampling_rate;

      if (PCM_DEINTERLEAVED_UNPACKED == pcm_fmt_extn_ptr->interleaved)
      {
         // IPC TX will always pack the data and send to IPC RX
         pcm_fmt_extn_ptr->interleaved = PCM_DEINTERLEAVED_PACKED;
      }
      for (uint32_t ch = 0; ch < inp_data_fmt.format.num_channels; ch++)
      {
         // channel_mapping[ch] = (uint8_t)inp_data_fmt.channel_type[ch]; //potential chance of
         // overflow
         channel_mapping[ch] = (uint8_t)MIN(inp_data_fmt.channel_type[ch], 255);
      }
      /*data_buffer_size =
          (me_ptr->frame_length_info.frame_dur_ms * inp_data_fmt.format.num_channels *
         (inp_data_fmt.format.bits_per_sample >> 3) * (inp_data_fmt.format.sampling_rate / 1000));*/ //TBD: could there be any issue if using ms instead of us

      uint32_t unit_frame_size = CAPI_CMN_BITS_TO_BYTES(inp_data_fmt.format.bits_per_sample) *tu_get_unit_frame_size(inp_data_fmt.format.sampling_rate);

      me_ptr->sh_buf_info.frame_size_per_ch_in_bytes = unit_frame_size * me_ptr->frame_length_info.frame_dur_ms;
      me_ptr->sh_buf_info.frame_size_in_bytes  =
                  me_ptr->inp_media_fmt.format.num_channels * me_ptr->sh_buf_info.frame_size_per_ch_in_bytes;

      data_buffer_size =  me_ptr->sh_buf_info.frame_size_in_bytes;
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Unexpected media fmt type 0x%lx\n",
                 inp_data_fmt.format.bitstream_format);
      CAPI_SET_ERROR(capi_result, CAPI_EBADPARAM);
      return capi_result;
   }
   me_ptr->sh_buf_info.data_buff_size = ALIGN_128_BYTES(data_buffer_size);

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid,
              DBG_HIGH_PRIO,
              "new data size %lu & aligned: new data size %d",
              data_buffer_size,
              me_ptr->sh_buf_info.data_buff_size);
#endif

   me_ptr->is_inp_media_fmt_pending = TRUE;
   if (DATA_PORT_STATE_CLOSED != me_ptr->out_port_info[0].port_state)
   {
      bool_t is_data_path = (DATA_PORT_STATE_STARTED == me_ptr->in_port_info[0].port_state);
      // if dps is OPEN, meaning there is no data processing going on, so directly send the media format
      capi_result = capi_ipc_tx_create_send_media_fmt(me_ptr, wr_payload_ptr, is_data_path, payload_size);
      if (CAPI_EOK != capi_result)
      {
         IPC_TX_MSG(me_ptr->miid, DBG_ERROR_PRIO, "Failed to send media fmt from control path\n");
         return capi_result;
      }
      me_ptr->is_inp_media_fmt_pending = FALSE;
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "Unexpected state 0x%x",
                 me_ptr->out_port_info[0].port_state); // todo: cache and apply in process call
   }

   // After sending the media format to the IPC RX module, create the buffers
   if (wr_payload_ptr)
   {
      posal_memory_free(wr_payload_ptr);
   }

   // Allocate the shared buffer with the default buffer count(2) if the media format is received for the first time and
   // have a valid size
   if ((!me_ptr->is_num_bufs_received) && (data_buffer_size))
   {
      capi_result |= capi_ipc_tx_manage_buffer(me_ptr, data_buffer_size, me_ptr->sh_buf_info.metadata_buff_size);
      if (CAPI_EOK != capi_result)
      {
         return capi_result;
      }
   }
   else
   {
#ifdef DEBUG_IPC_TX
      IPC_TX_MSG(me_ptr->miid,
                 DBG_LOW_PRIO,
                 "Did not allocate buffers with mf (nchs: %lu,bps: %lu, Sr: %lu ) is not set or frame "
                 "size %lu ms or buffers are already allocated",
                 inp_data_fmt.format.num_channels,
                 inp_data_fmt.format.bits_per_sample,
                 inp_data_fmt.format.sampling_rate,
                 me_ptr->frame_length_info.frame_dur_ms);
#endif
   }
   return capi_result;
}

/**
 * \brief Updates the media format for the IPC TX module.
 * This function handles the update of the media format for the IPC TX module,
 * sending the new media format to the IPC RX module via either the data path or
 * the control path, depending on the value of the is_data_path parameter.
 *
 * \param me_ptr Pointer to the capi_ipc_tx_t structure.
 * \param input Pointer to the input buffer.
 * \param is_data_path Flag indicating whether to send the media format via the data path or the control path.
 * \param write_payload_size Size of the media fmt payload.
 * \return capi_err_t Result of the operation, which is either an error code or a success code.
 */
capi_err_t capi_ipc_tx_create_send_media_fmt(capi_ipc_tx_t *me_ptr,
                                             uint8_t       *payload_ptr,
                                             uint32_t       is_data_path,
                                             uint32_t       write_payload_size)
{
   capi_err_t capi_result              = CAPI_EOK;
   uint8_t   *ctrl_path_mf_payload_ptr = NULL;

#ifdef DEBUG_IPC_TX
   IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Handling input media format update. is_data_path = %d", is_data_path);
#endif

   gpr_cmd_alloc_send_t args;
   if (is_data_path)
   {
      // Send the media format to the IPC RX module in data path
      args.src_domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;
      args.dst_domain_id = me_ptr->ipc_tx_link_info.peer_proc_domain_id;
      args.src_port      = me_ptr->ipc_tx_link_info.self_module_iid;
      args.dst_port      = me_ptr->ipc_tx_link_info.peer_module_iid;
      args.token         = 0x99;
      args.opcode        = DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT;
      args.payload       = (uint8_t *)payload_ptr;
      args.payload_size  = write_payload_size;
      args.client_data   = 0;
   }
   else
   {
      uint32_t set_ctrl_path_mf_psize = write_payload_size + sizeof(apm_module_param_data_t) + sizeof(apm_cmd_header_t);
      ctrl_path_mf_payload_ptr =
         (uint8_t *)posal_memory_malloc(set_ctrl_path_mf_psize, (POSAL_HEAP_ID)me_ptr->heap_info.heap_id);
      if (NULL == ctrl_path_mf_payload_ptr)
      {
         IPC_TX_MSG(me_ptr->miid,
                    DBG_ERROR_PRIO,
                    "Failed to allocate memory for gpr payload for the control path media format");
         return CAPI_ENOMEMORY;
      }
      memset(ctrl_path_mf_payload_ptr, 0, set_ctrl_path_mf_psize);

      apm_cmd_header_t *cmd_header    = (apm_cmd_header_t *)ctrl_path_mf_payload_ptr;
      cmd_header->payload_address_lsw = 0;
      cmd_header->payload_address_msw = 0;
      cmd_header->mem_map_handle      = 0;
      cmd_header->payload_size        = write_payload_size + sizeof(apm_module_param_data_t);

      apm_module_param_data_t *param_ptr = (apm_module_param_data_t *)(cmd_header + 1);
      param_ptr->error_code              = 0;
      param_ptr->param_id                = PARAM_ID_MEDIA_FORMAT;
      param_ptr->param_size              = write_payload_size;
      param_ptr->module_instance_id      = me_ptr->ipc_tx_link_info.peer_module_iid;

      param_ptr = param_ptr + 1;
      memscpy(param_ptr, write_payload_size, payload_ptr, write_payload_size);

      // Send the media format to the IPC RX module in control path
      args.src_domain_id = me_ptr->ipc_tx_link_info.self_proc_domain_id;
      args.dst_domain_id = me_ptr->ipc_tx_link_info.peer_proc_domain_id;
      args.src_port      = me_ptr->ipc_tx_link_info.self_module_iid;
      args.dst_port      = me_ptr->ipc_tx_link_info.peer_module_iid;
      args.token         = 0x99; // review and update correctly
      args.opcode        = APM_CMD_SET_CFG;
      args.payload       = (uint8_t *)ctrl_path_mf_payload_ptr;
      args.payload_size  = set_ctrl_path_mf_psize;
      args.client_data   = 0;
   }

    capi_result = __gpr_cmd_alloc_send(&args); // fully creates and sends a msg

   if (AR_DID_FAIL(capi_result))
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sending media format from ipc to client failed with 0x%lx", capi_result);
      return capi_result;
   }
   else
   {
      IPC_TX_MSG(me_ptr->miid, DBG_HIGH_PRIO, "Sent Media format from IPC to client");
   }
   if (ctrl_path_mf_payload_ptr)
   {
      posal_memory_free(ctrl_path_mf_payload_ptr);
   }
    return capi_result;
}
