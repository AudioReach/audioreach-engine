/**
 * \file spdm_media_fmt_handler.c
 * \brief
 *     This file contains the media format handling code for the satellite Graph Management
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* =======================================================================
Include Files For Module
========================================================================== */

#include "spdm_i.h"
#include "media_fmt_extn_api.h"
#include "offload_sp_api.h"

/* =======================================================================
Static Function Definitions
========================================================================== */
/* function to handle the input media format to the write input port
 * The function would parse the media format in GK format and convert
 * into standard media format that in compliance with the client format
 */

/**
 * @brief
 * Handles and translates the input media format for a specified port
 * in the satellite graph.
 *
 * This function is responsible for interpreting the media format received
 * from the client (in GK format), validating it, and converting it into a
 * standard format that complies with the expectations of the satellite
 * graph modules. It supports multiple data formats including PCM (fixed
 * and floating point) and raw compressed formats. The function allocates
 * memory for the translated format, populates the appropriate payload
 * structures, and prepares the data for transmission to the write endpoint
 * (WR EP) module.
 *
 * Depending on whether the format is being applied to the data path or
 * control path, it constructs the appropriate GPR packet and updates the
 * active data handler accordingly. It also ensures that the IPC buffer
 * is allocated for subsequent data transmission.
 *
 * Preventive checks are performed to ensure all pointers and parameters
 * are valid. Memory allocation failures and unsupported formats are
 * gracefully handled with appropriate error logging.
 *
 * @param[in] spgm_ptr         Pointer to the SPGM (Satellite Graph Manager)
 *                             information structure.
 * @param[in] media_format_ptr Pointer to the input media format structure
 *                             received from the client.
 * @param[in] port_index       Index of the port for which the media format
 *                             is being updated.
 * @param[in] is_data_path     Boolean flag indicating whether the update
 *                             applies to the data path (true) or control
 *                             path (false).
 *
 * @return
 * AR_EOK on success, or an appropriate error code on failure such as
 * AR_ENOMEMORY or AR_EUNSUPPORTED.
 */

ar_result_t spdm_handle_input_media_format_update(spgm_info_t *spgm_ptr,
                                                  void *       media_format_ptr,
                                                  uint32_t     port_index,
                                                  bool_t       is_data_path)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t wr_ep_port_id     = 0;
   uint32_t wr_client_port_id = 0;
   uint32_t payload_size      = 0;
   uint32_t buffer_size       = 0;
   uint32_t token             = 0;

   uint8_t *               ctrl_path_mf_payload_ptr = NULL;
   uint8_t *               payload_ptr              = NULL;
   media_format_t *        wr_media_fmt_ptr         = NULL;
   spf_std_media_format_t *std_ptr                  = NULL;
   spf_msg_media_format_t *input_media_fmt_ptr      = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   VERIFY(result, (NULL != media_format_ptr));
   input_media_fmt_ptr = (spf_msg_media_format_t *)media_format_ptr;

   VERIFY(result, (NULL != spgm_ptr->process_info.wdp_obj_ptr[port_index]));

   // Write EP module IID in the satellite graph associated with this port
   wr_ep_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_ep_miid;
   // Write Client module IID in OLC associated with this port
   wr_client_port_id = spgm_ptr->process_info.wdp_obj_ptr[port_index]->port_info.ctrl_cfg.rw_client_miid;

   (void)sgm_get_unique_token(spgm_ptr, &token);

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "handle input MF, is_data_path %lu use_token 0x%lx", is_data_path, token);

   // Handling for PCM DATA format
   if (SPF_IS_PCM_DATA_FORMAT(input_media_fmt_ptr->df))
   {
      std_ptr = (spf_std_media_format_t *)&input_media_fmt_ptr->payload_start;

      // calculate the size of the payload to send the media format to the Satellite Graph
      payload_size = sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t) +
                     ALIGN_4_BYTES(std_ptr->num_channels * sizeof(std_ptr->channel_map[0]));

      // allocate the memory to send the pcm media format to the WR EP MIID
      payload_ptr = (uint8_t *)posal_memory_malloc(payload_size, spgm_ptr->cu_ptr->heap_id);
      if (NULL == payload_ptr)
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "Failed to allocate memory to create the payload for sending PCM input media format");
         // Failed to create memory, processing cannot happen further.
         THROW(result, AR_ENOMEMORY);
      }

      memset(payload_ptr, 0, payload_size);

      // Fill the media format payload
      wr_media_fmt_ptr                          = (media_format_t *)(payload_ptr);
      payload_media_fmt_pcm_t *pcm_fmt_extn_ptr = (payload_media_fmt_pcm_t *)(payload_ptr + sizeof(media_format_t));

      uint8_t *channel_mapping = (uint8_t *)(pcm_fmt_extn_ptr + 1);

      if (SPF_FIXED_POINT == input_media_fmt_ptr->df)
      {
         // Fill the  media_format_t structure
         wr_media_fmt_ptr->data_format = DATA_FORMAT_FIXED_POINT;
         wr_media_fmt_ptr->fmt_id      = MEDIA_FMT_ID_PCM_EXTN;
         wr_media_fmt_ptr->payload_size =
            sizeof(payload_media_fmt_pcm_t) + pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t);
      }
      else if (SPF_FLOATING_POINT == input_media_fmt_ptr->df)
      {
         // Fill the  media_format_t structure
         wr_media_fmt_ptr->data_format = DATA_FORMAT_FLOATING_POINT;
         wr_media_fmt_ptr->fmt_id      = MEDIA_FMT_ID_PCM_EXTN;
         wr_media_fmt_ptr->payload_size =
            sizeof(payload_media_fmt_pcm_t) + pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t);
      }
      // fill the pcm media format structure
      //  data is always interleaved
      pcm_fmt_extn_ptr->sample_rate     = std_ptr->sample_rate;
      pcm_fmt_extn_ptr->bit_width       = std_ptr->bits_per_sample;
      pcm_fmt_extn_ptr->alignment       = PCM_LSB_ALIGNED;
      pcm_fmt_extn_ptr->bits_per_sample = std_ptr->bits_per_sample;
      pcm_fmt_extn_ptr->q_factor        = std_ptr->q_format;
      pcm_fmt_extn_ptr->endianness      = PCM_LITTLE_ENDIAN;
      pcm_fmt_extn_ptr->num_channels    = std_ptr->num_channels;
      pcm_fmt_extn_ptr->interleaved     = PCM_DEINTERLEAVED_PACKED;

      memscpy(channel_mapping,
              pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t),
              std_ptr->channel_map,
              pcm_fmt_extn_ptr->num_channels * sizeof(uint8_t));

      uint32_t bytes_per_sample = TOPO_BITS_TO_BYTES(std_ptr->bits_per_sample);
      buffer_size =
         DEFAULT_FRAME_SIZE_IN_UNITS * bytes_per_sample * std_ptr->num_channels * (std_ptr->sample_rate / 1000);
   }
   // OLC_CA packetized data is not supported
   else if (SPF_IS_PACKETIZED(input_media_fmt_ptr->df))
   {
      return AR_EUNSUPPORTED;
   }
   // Handling of compressed raw data
   else if (SPF_RAW_COMPRESSED == input_media_fmt_ptr->df)
   {
      // calculate the size of the payload to send the media format to the Satellite Graph
      payload_size = sizeof(media_format_t) + input_media_fmt_ptr->actual_size;

      // allocate the memory to send the compressed media format to the WR EP MIID
      payload_ptr = (uint8_t *)posal_memory_malloc(payload_size, spgm_ptr->cu_ptr->heap_id);
      if (NULL == payload_ptr)
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "Failed to allocate memory for gpr payload for raw compressed media format");
         THROW(result, AR_ENOMEMORY);
      }
      memset(payload_ptr, 0, payload_size);

      wr_media_fmt_ptr           = (media_format_t *)(payload_ptr);
      uint8_t *raw_media_fmt_ptr = (uint8_t *)(payload_ptr + sizeof(media_format_t));

      // Fill the  media_format_t structure
      wr_media_fmt_ptr->data_format  = DATA_FORMAT_RAW_COMPRESSED;
      wr_media_fmt_ptr->fmt_id       = input_media_fmt_ptr->fmt_id;
      wr_media_fmt_ptr->payload_size = input_media_fmt_ptr->actual_size;

      // Fill the raw media format details
      memscpy((void *)raw_media_fmt_ptr,
              input_media_fmt_ptr->actual_size,
              (void *)input_media_fmt_ptr->payload_start,
              input_media_fmt_ptr->actual_size);

      buffer_size = DEFAULT_FRAME_SIZE_COMPRESSED_DATA;
   }

   if (TRUE == is_data_path)
   {
      // Fill the active data handler with the information to be filled in the GPR packet
      spgm_ptr->process_info.active_data_hndl.payload_size = payload_size;
      spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)payload_ptr;
      spgm_ptr->process_info.active_data_hndl.src_port     = wr_client_port_id;
      spgm_ptr->process_info.active_data_hndl.dst_port     = wr_ep_port_id;
      spgm_ptr->process_info.active_data_hndl.opcode       = DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT;
      spgm_ptr->process_info.active_data_hndl.token        = token;
   }
   else
   {
      uint32_t set_ctrl_path_mf_psize = payload_size + sizeof(apm_module_param_data_t) + sizeof(apm_cmd_header_t);
      ctrl_path_mf_payload_ptr = (uint8_t *)posal_memory_malloc(set_ctrl_path_mf_psize, spgm_ptr->cu_ptr->heap_id);
      if (NULL == ctrl_path_mf_payload_ptr)
      {
         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_ERROR_PRIO,
                     "Failed to allocate memory for gpr payload for the control path media format");
         THROW(result, AR_ENOMEMORY);
      }
      memset(ctrl_path_mf_payload_ptr, 0, set_ctrl_path_mf_psize);

      apm_cmd_header_t *cmd_header    = (apm_cmd_header_t *)ctrl_path_mf_payload_ptr;
      cmd_header->payload_address_lsw = 0;
      cmd_header->payload_address_msw = 0;
      cmd_header->mem_map_handle      = 0;
      cmd_header->payload_size        = payload_size + sizeof(apm_module_param_data_t);

      apm_module_param_data_t *param_ptr = (apm_module_param_data_t *)(cmd_header + 1);
      param_ptr->error_code              = 0;
      param_ptr->param_id                = PARAM_ID_MEDIA_FORMAT;
      param_ptr->param_size              = payload_size;
      param_ptr->module_instance_id      = wr_ep_port_id;

      param_ptr = param_ptr + 1;
      memscpy(param_ptr, payload_size, payload_ptr, payload_size);

      // Fill the active data handler with the information to be filled in the GPR packet
      spgm_ptr->process_info.active_data_hndl.payload_size = set_ctrl_path_mf_psize;
      spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)ctrl_path_mf_payload_ptr;
      spgm_ptr->process_info.active_data_hndl.src_port     = wr_client_port_id;
      spgm_ptr->process_info.active_data_hndl.dst_port     = wr_ep_port_id;
      spgm_ptr->process_info.active_data_hndl.opcode       = APM_CMD_SET_CFG;
      spgm_ptr->process_info.active_data_hndl.token        = token;
   }

   // Send the media format to the corresponding WR EP MIID
   TRY(result, sgm_ipc_send_data_pkt(spgm_ptr));

   // allocate the IPC shared buffer memory
   TRY(result, spdm_alloc_ipc_data_buffers(spgm_ptr, buffer_size, port_index, IPC_WRITE_DATA));

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "handle input MF completed "
               "token 0x%lx, payload size %lu",
               token,
               payload_size);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (NULL != payload_ptr)
   {
      posal_memory_free(payload_ptr);
   }

   if (NULL != ctrl_path_mf_payload_ptr)
   {
      posal_memory_free(ctrl_path_mf_payload_ptr);
   }

   return result;
}

/**
 * @brief
 * Processes incoming media format events from the GPR packet.
 *
 * This function handles the media format events received from the GPR packet,
 * allocates memory for the payload, and invokes the media format event handler.
 * It updates the read port state and resets the data buffer.
 *
 * Preventive checks are performed to ensure all pointers and parameters
 * are valid. Memory allocation failures are gracefully handled with
 * appropriate error logging.
 *
 * @param[in] spgm_ptr    Pointer to the SPGM (Satellite Graph Manager)
 *                        information structure.
 * @param[in] packet_ptr  Pointer to the GPR packet containing the media format event.
 * @param[in] port_index  Index of the port for which the media format event is being processed.
 * @param[in] is_data_path Boolean flag indicating whether the event applies to the data path (true) or control path
 * (false).
 *
 * @return
 * AR_EOK on success, or an appropriate error code on failure such as
 * AR_ENOMEMORY or AR_EINVALID.
 */

ar_result_t spdm_process_media_format_event(spgm_info_t * spgm_ptr,
                                            gpr_packet_t *packet_ptr,
                                            uint32_t      port_index,
                                            bool_t        is_data_path)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id                 = 0;
   uint32_t payload_size           = 0;
   uint32_t cnt_ext_port_bit_index = 0;

   uint8_t *             rsp_ptr         = NULL;
   uint8_t *             payload_ptr     = NULL;
   read_data_port_obj_t *rd_port_obj_ptr = NULL;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "process MF event, is_data_path %lu", is_data_path);

   log_id          = spgm_ptr->sgm_id.log_id;
   rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_port_obj_ptr));

   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
   VERIFY(result, (0 != payload_size));
   rsp_ptr = (uint8_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   VERIFY(result, (NULL != rsp_ptr));

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "process MF event, sent from 0x%lx to 0x%lx, mf_psize %lu ",
               rd_port_obj_ptr->port_info.ctrl_cfg.rw_ep_miid,
               rd_port_obj_ptr->port_info.ctrl_cfg.rw_client_miid,
               payload_size);

   payload_ptr = (uint8_t *)posal_memory_malloc(payload_size, spgm_ptr->cu_ptr->heap_id);
   if (NULL != payload_ptr)
   {
      rd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ext_out_port_buf;
      memscpy(payload_ptr, payload_size, rsp_ptr, payload_size);
      cnt_ext_port_bit_index = cu_get_bit_index_from_mask(rd_port_obj_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
      olc_media_fmt_event_handler(spgm_ptr->cu_ptr,
                                  cnt_ext_port_bit_index,
                                  payload_ptr,
                                  payload_size,
                                  rd_port_obj_ptr->port_info.ctrl_cfg.sat_rd_ep_opfs_bytes,
                                  is_data_path);
      memset(&rd_port_obj_ptr->db_obj.data_buf, 0, sizeof(sdm_cnt_ext_data_buf_t));
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "process MF event, failed to allocate memory of size %lu", payload_size);
      THROW(result, AR_ENOMEMORY);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if (NULL != payload_ptr)
   {
      posal_memory_free(payload_ptr);
   }

   return result;
}
