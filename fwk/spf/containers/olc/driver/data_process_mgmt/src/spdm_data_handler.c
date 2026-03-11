/**
 * \file spdm_data_handler.c
 * \brief
 *     This file contains Satellite Graph Management functions for data handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"
#include "irm_cntr_prof_util.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

ar_result_t spdm_get_databuf_from_rd_datapool(spgm_info_t *spgm_ptr, read_data_port_obj_t *rd_ptr)
{
   ar_result_t            result            = AR_EALREADY;
   bool_t                 found_node        = FALSE;
   data_buf_pool_node_t  *data_buf_node_ptr = NULL;
   shmem_data_buf_pool_t *data_pool_ptr     = &rd_ptr->db_obj.buf_pool;

   if (NULL == rd_ptr->db_obj.active_buf_node_ptr)
   {
      found_node = spdm_get_available_data_buf_node(data_pool_ptr->port_db_list_ptr,
                                                    data_pool_ptr->num_valid_data_buf_in_list,
                                                    &data_buf_node_ptr);
      if (FALSE == found_node)
      {
         return AR_EFAILED;
         // Will be tried again when the read done is received
      }
      else
      {
         rd_ptr->db_obj.active_buf_node_ptr = data_buf_node_ptr;
         result                             = AR_EOK;
      }
   }

   return result;
}

static ar_result_t validate_read_buf_state(data_buf_pool_node_t *data_buf_node_ptr,
                                           spgm_info_t          *spgm_ptr,
                                           uint32_t              port_index)
{
   ar_result_t result       = AR_EOK;
   uint32_t    buffer_state = 0;

   buffer_state |= (data_buf_node_ptr->buf_in_use << 0);
   buffer_state |= ((0 != data_buf_node_ptr->offset) << 4);

   if (buffer_state == 0)
   {
      return result;
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data: validate read buffer failed %lu ", buffer_state);
      return AR_EFAILED;
   }

   return result;
}

static ar_result_t spdm_recreate_rd_data_buffer(spgm_info_t          *spgm_ptr,
                                                data_buf_pool_node_t *data_buf_node_ptr,
                                                read_data_port_obj_t *rd_ptr,
                                                uint32_t              port_index)
{
   ar_result_t result            = AR_EOK;
   uint32_t    new_data_buf_size = 0;
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "read_data: shm buf re-allocation "
               "prev_md+data_size %lu, new_md+data_size %lu",
               data_buf_node_ptr->data_buf_size + data_buf_node_ptr->meta_data_buf_size,
               (data_buf_node_ptr->meta_data_buf_size + rd_ptr->db_obj.buf_pool.buf_size));

   if (AR_EOK != (result = sgm_shmem_free(&data_buf_node_ptr->ipc_data_buf)))
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data: shm buf free failed");
      return result;
   }

   new_data_buf_size = ALIGN_128_BYTES(data_buf_node_ptr->meta_data_buf_size); // md_size
   new_data_buf_size += ALIGN_128_BYTES(rd_ptr->db_obj.buf_pool.buf_size);     // data buf size
   new_data_buf_size += 3 * GAURD_PROTECTION_BYTES;                           // start, end, mid

   result = sgm_shmem_alloc(new_data_buf_size, spgm_ptr->sgm_id.sat_pd, &data_buf_node_ptr->ipc_data_buf);
   if (AR_EOK != result)
   {

      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data: shm buf re-allocation failed");
      return result;
   }

   data_buf_node_ptr->data_buf_size = rd_ptr->db_obj.buf_pool.buf_size;
   return result;
}

ar_result_t spdm_send_read_data_buffer(spgm_info_t          *spgm_ptr,
                                       data_buf_pool_node_t *data_buf_node_ptr,
                                       read_data_port_obj_t *rd_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    rd_ep_port_id      = rd_ptr->port_info.ctrl_cfg.rw_ep_miid;
   uint32_t    rd_client_port_id  = rd_ptr->port_info.ctrl_cfg.rw_client_miid;
   uint32_t    port_index         = rd_ptr->port_info.ctrl_cfg.sdm_port_index;
   uint32_t    shmem_offset       = 0;
   bool_t      create_rd_pool_buf = FALSE;

   rd_ep_data_header_t *rd_cmd_ptr = NULL;

   if (AR_EOK != (result = validate_read_buf_state(data_buf_node_ptr, spgm_ptr, port_index)))
   {
      return result;
   }

   if (0 == data_buf_node_ptr->meta_data_buf_size)
   {
      data_buf_node_ptr->meta_data_buf_size = 2048;
      create_rd_pool_buf                    = TRUE;
   }

   if ((rd_ptr->db_obj.buf_pool.buf_size > data_buf_node_ptr->data_buf_size) || create_rd_pool_buf)
   {
      result = spdm_recreate_rd_data_buffer(spgm_ptr, data_buf_node_ptr, rd_ptr, port_index);
      if (AR_EOK != result)
      {
         return result;
      }
   }

   rd_cmd_ptr   = &rd_ptr->db_obj.data_header.read_data;
   shmem_offset = data_buf_node_ptr->ipc_data_buf.mem_attr.offset;

   uint8_t *rd_guard_info_ptr = (uint8_t *)data_buf_node_ptr->ipc_data_buf.shm_mem_ptr;
   memscpy(rd_guard_info_ptr,
           GAURD_PROTECTION_BYTES,
           &rd_ptr->db_obj.mem_gaurd_header,
           sizeof(sdm_data_port_guard_header_info_t));

   // fill read data buffer
   rd_cmd_ptr->read_data.data_buf_addr_lsw   = shmem_offset + GAURD_PROTECTION_BYTES;
   rd_cmd_ptr->read_data.data_buf_addr_msw   = 0;
   rd_cmd_ptr->read_data.data_mem_map_handle = data_buf_node_ptr->ipc_data_buf.mem_attr.sat_handle;
   rd_cmd_ptr->read_data.data_buf_size       = data_buf_node_ptr->data_buf_size;

   // fill read metadata buffer
   rd_cmd_ptr->read_data.md_buf_addr_lsw =
      shmem_offset + ALIGN_128_BYTES(data_buf_node_ptr->data_buf_size) + 2 * GAURD_PROTECTION_BYTES;
   rd_cmd_ptr->read_data.md_buf_addr_msw   = 0;
   rd_cmd_ptr->read_data.md_mem_map_handle = data_buf_node_ptr->ipc_data_buf.mem_attr.sat_handle;
   rd_cmd_ptr->read_data.md_buf_size       = data_buf_node_ptr->meta_data_buf_size;

   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(rd_ep_data_header_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)rd_cmd_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = rd_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = rd_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2;
   spgm_ptr->process_info.active_data_hndl.token        = data_buf_node_ptr->token;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "read_data: send read buffer of size %lu md_size %lu with token %lu",
               rd_cmd_ptr->read_data.data_buf_size,
               rd_cmd_ptr->read_data.md_buf_size,
               data_buf_node_ptr->token);
#endif

   if (AR_EOK != (result = sgm_ipc_send_data_pkt(spgm_ptr)))
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data: failed to send the read data buffer");
      return result;
   }

   // mark the buffer in use
   data_buf_node_ptr->buf_in_use = TRUE;
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));

   return result;
}

ar_result_t spdm_process_data_release_read_buffer(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   read_data_port_obj_t *rd_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data: process begin to release read ipc buffer ");
#endif

   rd_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_ptr));

   // if the active buffer node is valid, implies we have a valid read buffer to send
   if (rd_ptr->db_obj.active_buf_node_ptr)
   {
      if (AR_EOK != (result = spdm_send_read_data_buffer(spgm_ptr, rd_ptr->db_obj.active_buf_node_ptr, rd_ptr)))
      {
         OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data: failed to send IPC read buffer to satellite");
         return result;
         // This should not fail. we need to debug if this happen
      }
      rd_ptr->db_obj.active_buf_node_ptr->buf_in_use = TRUE; // indicate the buffer is in transaction
      rd_ptr->db_obj.active_buf_node_ptr             = NULL;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
      OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data: process completed, IPC read buffer sent to satellite");
#endif
   }
   else
   {
      // not expected, but we dont error here. Wait for the IPC read done buffer
      OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "read_data: NO IPC read buffer with OLC to send to satellite");
   }

   rd_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ipc_read_data_done_evnt;

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t spdm_process_render_read_done_data(spgm_info_t            *spgm_ptr,
                                               uint32_t                port_index,
                                               sdm_cnt_ext_data_buf_t *output_data_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id           = 0;
   uint32_t data_filled      = 0;
   uint32_t src_data_to_fill = 0;
   uint32_t copy_size        = 0;

   uint8_t              *src_data_ptr    = NULL;
   data_buf_pool_node_t *rd_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_ptr          = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != output_data_ptr));
   output_data_ptr->data_buf.actual_data_len = 0;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data_done: render data begin");
#endif

   rd_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_ptr));

   rd_buf_node_ptr = rd_ptr->db_obj.active_buf_node_ptr;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "read_data_done: render read data of size %lu md_size %lu",
               rd_buf_node_ptr->data_done_header.read_data_done.data_size,
               rd_buf_node_ptr->data_done_header.read_data_done.md_size);
#endif

   src_data_to_fill = rd_buf_node_ptr->data_done_header.read_data_done.data_size;
   src_data_ptr     = (uint8_t *)rd_buf_node_ptr->ipc_data_buf.shm_mem_ptr + GAURD_PROTECTION_BYTES;

   if ((src_data_to_fill <= output_data_ptr->data_buf.max_data_len) && (0 < output_data_ptr->data_buf.max_data_len) &&
       (0 < src_data_to_fill))
   {

      // copy data from IPC read buffer to external container output buffer
      copy_size = memscpy(output_data_ptr->data_buf.data_ptr,
                          output_data_ptr->data_buf.max_data_len,
                          src_data_ptr,
                          src_data_to_fill);

      rd_buf_node_ptr->offset = copy_size;
      data_filled             = copy_size;

      // copy the time-stamp information for the data
      output_data_ptr->buf_ts->value = ((uint64_t)rd_buf_node_ptr->data_done_header.read_data_done.timestamp_msw) << 32;
      output_data_ptr->buf_ts->value |= rd_buf_node_ptr->data_done_header.read_data_done.timestamp_lsw;
      output_data_ptr->buf_ts->valid = cu_get_bits(rd_buf_node_ptr->data_done_header.read_data_done.flags,
                                                   DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
                                                   DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

      // Extract the metadata from the read done buffer
      TRY(result,
          spdm_read_meta_data(spgm_ptr,
                              rd_buf_node_ptr,
                              output_data_ptr->md_list_pptr,
                              rd_ptr->port_info.ctrl_cfg.rw_client_miid,
                              port_index));

      rd_buf_node_ptr->offset     = 0;
      rd_buf_node_ptr->buf_in_use = FALSE;

      output_data_ptr->data_buf.actual_data_len     = data_filled;
      rd_ptr->db_obj.data_buf.offset                = 0;
      rd_ptr->db_obj.data_buf.data_buf.max_data_len = 0;
   }
   else if (0 == src_data_to_fill)
   {
      rd_buf_node_ptr->offset = 0;
      // Data buffer can be empty, but we can have meta data in the buffer
      // Extract the metadata from the read done buffer
      TRY(result,
          spdm_read_meta_data(spgm_ptr,
                              rd_buf_node_ptr,
                              output_data_ptr->md_list_pptr,
                              rd_ptr->port_info.ctrl_cfg.rw_client_miid,
                              port_index));

      output_data_ptr->data_buf.actual_data_len     = 0;
      rd_ptr->db_obj.data_buf.offset                = 0;
      rd_ptr->db_obj.data_buf.data_buf.max_data_len = 0;
      rd_buf_node_ptr->offset                       = 0;
      rd_buf_node_ptr->buf_in_use                   = FALSE;
   }

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data_done: render data complete");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t spdm_process_data_read_done(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id                         = 0;
   uint32_t token                          = 0;
   uint32_t ext_out_port_channel_bit_index = 0;

   read_data_port_obj_t                            *rd_ptr             = NULL;
   shmem_data_buf_pool_t                           *read_data_pool_ptr = NULL;
   data_buf_pool_node_t                            *data_buf_node_ptr  = NULL;
   data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *read_done_ptr      = NULL;
   gen_topo_module_t                               *module_ptr         = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data_done: start processing");
#endif

   VERIFY(result, (NULL != packet_ptr));
   VERIFY(result, (DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2 == packet_ptr->opcode));
   token = packet_ptr->token;

   read_done_ptr = (data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   VERIFY(result,
          ((AR_EOK == read_done_ptr->data_status) |
           (AR_ENEEDMORE ==
            read_done_ptr->data_status))); // OLC_CA : Need to check further the implication of a failure

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "read_data_done: processing pkt token %lu, buffer_status %lu buf_size %lu md_size %lu",
               token,
               read_done_ptr->data_status,
               read_done_ptr->data_size,
               read_done_ptr->md_size);
#endif

   // Get the port data object
   rd_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_ptr));
   // Get the port data pool reference
   read_data_pool_ptr = &rd_ptr->db_obj.buf_pool;

   // Get the buffer node from the read pool given the token
   if (AR_EOK != (result = spdm_get_data_buf_node(spgm_ptr, read_data_pool_ptr, token, &data_buf_node_ptr)))
   {
      // Failure is fatal and not expected. This can lead to data path hang and needs to be analysed
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "read_data_done: failed to get the buffer node with "
                  "the specified buffer token %lu ",
                  token);
      return result;
   }
   module_ptr =
      (gen_topo_module_t *)gu_find_module(spgm_ptr->cu_ptr->gu_ptr, rd_ptr->port_info.ctrl_cfg.rw_client_miid);

   if (NULL == module_ptr)
   {
	   OLC_SDM_MSG(OLC_SDM_ID,
		   DBG_ERROR_PRIO,
		   "read_data_done: module_ptr is NULL");
	   return AR_EFAILED;
   }

   IRM_PROFILE_MODULE_PROCESS_BEGIN(module_ptr->prof_info_ptr);

   // Verify the shared memory pointer and invalidate the memory
   VERIFY(result, (NULL != data_buf_node_ptr->ipc_data_buf.shm_mem_ptr));
   if (AR_EOK != (result = posal_cache_invalidate((uint32_t)data_buf_node_ptr->ipc_data_buf.shm_mem_ptr,
                                                  data_buf_node_ptr->ipc_data_buf.shm_alloc_size)))
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data_done: failed to cache invalidate the read buffer");
      return (AR_EPANIC | result);
   }

   if (NULL == rd_ptr->db_obj.active_buf_node_ptr)
   {
      rd_ptr->db_obj.active_buf_node_ptr                                  = data_buf_node_ptr;
      rd_ptr->db_obj.active_buf_node_ptr->data_done_header.read_data_done = *read_done_ptr;
   }
   else
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data_done: active buffer read node is not free. unexpected");
      VERIFY(result, (NULL == rd_ptr->db_obj.active_buf_node_ptr)); // active buffer node should be free
   }

   ext_out_port_channel_bit_index = cu_get_bit_index_from_mask(rd_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);

   TRY(result, olc_get_read_ext_output_buf(spgm_ptr->cu_ptr, ext_out_port_channel_bit_index, &rd_ptr->db_obj.data_buf));

   TRY(result, spdm_process_render_read_done_data(spgm_ptr, port_index, &rd_ptr->db_obj.data_buf));

   rd_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ext_out_port_buf;

   // function to deliver the buffer down stream
   TRY(result,
       olc_read_done_handler(spgm_ptr->cu_ptr,
                             ext_out_port_channel_bit_index,
                             rd_ptr->db_obj.data_buf.data_buf.actual_data_len,
                             rd_ptr->port_info.ctrl_cfg.sat_rd_ep_opfs_bytes));

   IRM_PROFILE_MODULE_PROCESS_END(module_ptr->prof_info_ptr, spgm_ptr->cu_ptr->gu_ptr->prof_mutex);

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read_data_done: process completed");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

// read data link process control decision
ar_result_t spdm_read_dl_pcd(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                           log_id = 0;
   read_data_port_obj_t              *rd_ptr = NULL;
   read_ipc_data_link_process_state_t rdl_state;

   rd_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_ptr));

   rdl_state = rd_ptr->port_info.ctrl_cfg.data_link_ps.rd_state;

   switch (rdl_state)
   {
      case wait_for_ext_out_port_buf:
      {
         cu_start_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }
      case wait_for_ipc_read_data_done_evnt:
      {
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_start_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }
      case hold_processing_output_queues:
      {
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, rd_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }
      default:
      {
         OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "invalid read data link state %lu", (uint32_t)rdl_state);
         result = AR_EFAILED;
         break;
      }
   }

#ifdef SGM_ENABLE_READ_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "read_data_link_state %lu channel mask 0x%lx",
               (uint32_t)rdl_state,
               spgm_ptr->cu_ptr->curr_chan_mask);
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

/* Function to get an available buffer node from the write data pool and set to the active buffer
 * The node state is checked to determine if available buffer with the OLC to write data.
 * If the size of buffer is not sufficient, the shared memory would be re-allocated by the caller
 */
ar_result_t spdm_get_databuf_from_wr_datapool(spgm_info_t *spgm_ptr, write_data_port_obj_t *wr_ptr)
{
   ar_result_t            result            = AR_EALREADY;
   bool_t                 found_node        = FALSE;
   data_buf_pool_node_t  *data_buf_node_ptr = NULL;
   shmem_data_buf_pool_t *data_pool_ptr     = &wr_ptr->db_obj.buf_pool;
   // caller to ensure the input arguments are valid

   if (NULL == wr_ptr->db_obj.active_buf_node_ptr)
   {
      // Function to find an buffer node available in the write buffer pool in OLC
      found_node = spdm_get_available_data_buf_node(data_pool_ptr->port_db_list_ptr,
                                                    data_pool_ptr->num_valid_data_buf_in_list,
                                                    &data_buf_node_ptr);
      if (FALSE == found_node)
      {
#ifdef SGM_ENABLE_WITE_DATA_FLOW_LEVEL_MSG
         uint32_t port_index = wr_ptr->port_info.ctrl_cfg.sdm_port_index;
         OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write_data: shm buf node not available");
#endif
         return AR_EFAILED;
         // Will be tried again when the write done is received
      }
      else
      {
         wr_ptr->db_obj.active_buf_node_ptr = data_buf_node_ptr;
         result                             = AR_EOK;
      }
   }

   return result;
}

/* function to send the write buffer from OLC to the satellite graph WR EP module
 */
static ar_result_t spdm_send_write_data_buffer(spgm_info_t          *spgm_ptr,
                                               data_buf_pool_node_t *data_buf_node_ptr,
                                               uint32_t              port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t               wr_ep_port_id     = 0;
   uint32_t               wr_client_port_id = 0;
   write_data_port_obj_t *wr_port_ptr       = NULL;
   wr_ep_data_header_t   *wr_data_cmd_ptr   = NULL;

   VERIFY(result, (port_index < SPDM_MAX_IO_PORTS));
   VERIFY(result, (NULL != data_buf_node_ptr));
   wr_port_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wr_port_ptr));

   wr_ep_port_id     = wr_port_ptr->port_info.ctrl_cfg.rw_ep_miid;
   wr_client_port_id = wr_port_ptr->port_info.ctrl_cfg.rw_client_miid;

   VERIFY(result, (NULL != data_buf_node_ptr->ipc_data_buf.shm_mem_ptr));
   if (AR_EOK != (result = posal_cache_flush((uint32_t)data_buf_node_ptr->ipc_data_buf.shm_mem_ptr,
                                             data_buf_node_ptr->ipc_data_buf.shm_alloc_size)))
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "write_data: data buffer cache flush failed");
      return (AR_EPANIC | result);
   }

   wr_data_cmd_ptr = &wr_port_ptr->db_obj.data_header.write_data;

   // Write data payload
   wr_data_cmd_ptr->write_data.data_buf_addr_lsw =
      data_buf_node_ptr->ipc_data_buf.mem_attr.offset + GAURD_PROTECTION_BYTES;
   wr_data_cmd_ptr->write_data.data_buf_addr_msw   = 0;
   wr_data_cmd_ptr->write_data.data_mem_map_handle = data_buf_node_ptr->ipc_data_buf.mem_attr.sat_handle;
   wr_data_cmd_ptr->write_data.data_buf_size       = data_buf_node_ptr->offset;

   // Write metadata payload
   wr_data_cmd_ptr->write_data.md_buf_addr_lsw =
      data_buf_node_ptr->ipc_data_buf.mem_attr.offset + data_buf_node_ptr->data_buf_size + 2 * GAURD_PROTECTION_BYTES;
   wr_data_cmd_ptr->write_data.md_buf_addr_msw = 0;
   if (data_buf_node_ptr->rw_md_data_info.metadata_buf_size)
   {
      wr_data_cmd_ptr->write_data.md_mem_map_handle = data_buf_node_ptr->ipc_data_buf.mem_attr.sat_handle;
   }
   else
   {
      wr_data_cmd_ptr->write_data.md_mem_map_handle = 0;
   }
   wr_data_cmd_ptr->write_data.md_buf_size = data_buf_node_ptr->rw_md_data_info.metadata_buf_size;

   wr_data_cmd_ptr->write_data.flags = (data_buf_node_ptr->inbuf_ts.valid << 31);
   wr_data_cmd_ptr->write_data.flags |= (data_buf_node_ptr->inbuf_ts.ts_continue << 29);
   wr_data_cmd_ptr->write_data.timestamp_lsw = data_buf_node_ptr->inbuf_ts.value;
   wr_data_cmd_ptr->write_data.timestamp_msw = data_buf_node_ptr->inbuf_ts.value >> 32;

   // update the active data handler with GPR packet info for the write data command
   spgm_ptr->process_info.active_data_hndl.payload_size = sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t);
   spgm_ptr->process_info.active_data_hndl.payload_ptr  = (uint8_t *)wr_data_cmd_ptr;
   spgm_ptr->process_info.active_data_hndl.src_port     = wr_client_port_id;
   spgm_ptr->process_info.active_data_hndl.dst_port     = wr_ep_port_id;
   spgm_ptr->process_info.active_data_hndl.opcode       = DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2;
   spgm_ptr->process_info.active_data_hndl.token        = data_buf_node_ptr->token;

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "write_data: send buffer of size %lu md_size %lu ",
               wr_data_cmd_ptr->write_data.data_buf_size,
               wr_data_cmd_ptr->write_data.md_buf_size);
#endif

   // Send the data packet to the satellite Graph
   if (AR_EOK != (result = sgm_ipc_send_data_pkt(spgm_ptr)))
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "write_data: failed to send the buffer of data_size %lu md_size %lu",
                  wr_data_cmd_ptr->write_data.data_buf_size,
                  wr_data_cmd_ptr->write_data.md_buf_size);

      return result;
   }

   data_buf_node_ptr->buf_in_use = TRUE;

   // reset the active data handle
   memset(&spgm_ptr->process_info.active_data_hndl, 0, sizeof(spgm_ipc_data_obj_t));
   memset(wr_data_cmd_ptr, 0, sizeof(wr_ep_data_header_t));

   CATCH(result, OLC_MSG_PREFIX, spgm_ptr->sgm_id.log_id)
   {
   }

   return result;
}

static ar_result_t spdm_recreate_wr_data_buffer(spgm_info_t          *spgm_ptr,
                                                data_buf_pool_node_t *write_data_buf_node_ptr,
                                                uint32_t              new_input_data_size,
                                                uint32_t              new_input_md_size,
                                                uint32_t              port_index)
{
   ar_result_t result            = AR_EOK;
   uint32_t    new_data_buf_size = 0;
   uint32_t    new_md_size       = 0;

   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_HIGH_PRIO,
               "write_data: shm buf re-allocation "
               "prev_data_size %lu, new_data_size %lu "
               "prev_md_size %lu, new_md_size %lu ",
               write_data_buf_node_ptr->data_buf_size,
               new_input_data_size,
               write_data_buf_node_ptr->meta_data_buf_size,
               new_input_md_size);

   if (AR_EOK != (result = sgm_shmem_free(&write_data_buf_node_ptr->ipc_data_buf)))
   {
      return result;
   }

   new_data_buf_size = ALIGN_128_BYTES(MAX(write_data_buf_node_ptr->data_buf_size, ALIGN_128_BYTES(new_input_data_size)));
   new_md_size = ALIGN_128_BYTES(MAX(write_data_buf_node_ptr->meta_data_buf_size, ALIGN_128_BYTES(new_input_md_size)));

   if (AR_EOK != (result = sgm_shmem_alloc(new_data_buf_size + new_md_size + 3 * GAURD_PROTECTION_BYTES,
                                           spgm_ptr->sgm_id.sat_pd,
                                           &write_data_buf_node_ptr->ipc_data_buf)))
   {
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "write_data: failed to re-alloc write shm buf from MDF heap, buffer size %lu",
                  new_data_buf_size + new_md_size);
      return result;
   }

   write_data_buf_node_ptr->data_buf_size      = new_data_buf_size;
   write_data_buf_node_ptr->meta_data_buf_size = new_md_size;

   return result;
}

/* function to write the data from the input port to the satellite WR EP module
 * the function check if the write buffer is available and fills the data to send it to
 * the satellite Graph
 */
ar_result_t spdm_process_data_write(spgm_info_t            *spgm_ptr,
                                    uint32_t                port_index,
                                    sdm_cnt_ext_data_buf_t *input_data_ptr,
                                    bool_t                 *is_buffer_consumed) // todo : VB will need to update
{
   ar_result_t result      = AR_EOK;
   ar_result_t temp_result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id           = 0;
   uint32_t wr_shm_data_size = 0;
   uint32_t meta_data_size   = 0;

   bool_t needs_shm_buf_resize = FALSE;
   bool_t is_data_present      = FALSE;
   bool_t is_md_present        = FALSE;

   uint8_t               *wr_shm_data_ptr         = NULL;
   data_buf_pool_node_t  *write_data_buf_node_ptr = NULL;
   write_data_port_obj_t *wr_ptr                  = NULL;

   *is_buffer_consumed = FALSE;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write_data: processing begin");
#endif

   VERIFY(result, (NULL != input_data_ptr));

   VERIFY(result, (port_index < SPDM_MAX_IO_PORTS));
   wr_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wr_ptr));
   VERIFY(result, (NULL == wr_ptr->db_obj.active_buf_node_ptr));

   // check and find if an write buffer node is available in the buffer pool
   TRY(result, spdm_get_databuf_from_wr_datapool(spgm_ptr, wr_ptr));

   VERIFY(result, (NULL != wr_ptr->db_obj.active_buf_node_ptr));
   write_data_buf_node_ptr             = wr_ptr->db_obj.active_buf_node_ptr;
   write_data_buf_node_ptr->buf_in_use = FALSE;

   if (AR_EOK !=
       (result = spdm_get_write_meta_data_size(spgm_ptr, port_index, *input_data_ptr->md_list_pptr, &meta_data_size)))
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "write_data: failed to get metadata size");
      return result;
   }

   needs_shm_buf_resize |=
      (write_data_buf_node_ptr->data_buf_size < ALIGN_128_BYTES(input_data_ptr->data_buf.actual_data_len));
   needs_shm_buf_resize |= (write_data_buf_node_ptr->meta_data_buf_size < ALIGN_128_BYTES(meta_data_size));

   if (needs_shm_buf_resize)
   {
      TRY(result,
          spdm_recreate_wr_data_buffer(spgm_ptr,
                                       write_data_buf_node_ptr,
                                       input_data_ptr->data_buf.actual_data_len,
                                       meta_data_size,
                                       port_index));
   }

   is_data_present = ((input_data_ptr->data_buf.actual_data_len <= write_data_buf_node_ptr->data_buf_size) &&
                      (0 < input_data_ptr->data_buf.actual_data_len));

   is_md_present = ((meta_data_size <= write_data_buf_node_ptr->meta_data_buf_size) && (0 < meta_data_size));

   // fill the data in the buffer
   if (is_data_present || is_md_present)
   {
      uint8_t *wr_guard_info_ptr = (uint8_t *)write_data_buf_node_ptr->ipc_data_buf.shm_mem_ptr;
      memscpy(wr_guard_info_ptr,
              GAURD_PROTECTION_BYTES,
              &wr_ptr->db_obj.mem_gaurd_header,
              sizeof(sdm_data_port_guard_header_info_t));

      if (is_data_present)
      {
         wr_shm_data_size = write_data_buf_node_ptr->data_buf_size;
         wr_shm_data_ptr  = (uint8_t *)write_data_buf_node_ptr->ipc_data_buf.shm_mem_ptr + GAURD_PROTECTION_BYTES;

         write_data_buf_node_ptr->inbuf_ts = *input_data_ptr->buf_ts;
         write_data_buf_node_ptr->offset   = memscpy(wr_shm_data_ptr,
                                                   wr_shm_data_size,
                                                   input_data_ptr->data_buf.data_ptr,
                                                   input_data_ptr->data_buf.actual_data_len);
      }

      // write ipc buffer is has data, send to satellite graph

      if (is_md_present)
      {
         result = spdm_write_meta_data(spgm_ptr,
                                       write_data_buf_node_ptr,
                                       input_data_ptr->md_list_pptr,
                                       wr_ptr->port_info.ctrl_cfg.rw_client_miid,
                                       port_index);
      }
      else
      {
         write_data_buf_node_ptr->rw_md_data_info.num_md_elements   = 0;
         write_data_buf_node_ptr->rw_md_data_info.metadata_buf_size = 0;
      }

      if ((0 < write_data_buf_node_ptr->offset) || (0 < write_data_buf_node_ptr->rw_md_data_info.num_md_elements))
      {
         if (AR_EOK != (result = spdm_send_write_data_buffer(spgm_ptr, write_data_buf_node_ptr, port_index)))
         {
            *is_buffer_consumed                = TRUE; // Marking the external input buffer as consumed
            wr_ptr->db_obj.active_buf_node_ptr = NULL;
            return result;
         }
         else
         {
            wr_ptr->db_obj.active_buf_node_ptr = NULL;
            *is_buffer_consumed                = TRUE;
         }
      }
   }
   else
   {
      wr_ptr->db_obj.active_buf_node_ptr = NULL;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   if ((NULL != spgm_ptr) && (NULL != wr_ptr))
   {
      // check and find if an write buffer node is available in the buffer pool
      if (AR_EOK != (temp_result = spdm_get_databuf_from_wr_datapool(spgm_ptr, wr_ptr)))
      {
         wr_ptr->port_info.ctrl_cfg.data_link_ps.wr_state = wait_for_ipc_write_data_done_evnt;
         result                                           = AR_EOK;
      }
      else
      {
         wr_ptr->db_obj.active_buf_node_ptr = NULL;
      }
   }

   return result;
}

/* function to release the write buffer and add it back to the write buffer pool
 * The buffer node is determined based on the buffer token value
 */
static ar_result_t spdm_release_write_buffer(spgm_info_t *spgm_ptr, write_data_port_obj_t *wr_port_obj, uint32_t token)
{
   ar_result_t            result              = AR_EOK;
   data_buf_pool_node_t  *data_buf_node_ptr   = NULL;
   shmem_data_buf_pool_t *write_data_pool_ptr = NULL;
   // Input arguments are expected to verified by the caller

   write_data_pool_ptr = &wr_port_obj->db_obj.buf_pool;

   // Get the write data buffer node given the token
   if (AR_EOK != (result = spdm_get_data_buf_node(spgm_ptr, write_data_pool_ptr, token, &data_buf_node_ptr)))
   {
      uint32_t port_index = wr_port_obj->port_info.ctrl_cfg.sdm_port_index;

      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_ERROR_PRIO,
                  "write_done: pkt token %lu, "
                  "Failed to get the data buffer node to release the write buffer",
                  token);
      return result;
   }

   // release the buffer by setting the appropriate flags
   if (NULL != data_buf_node_ptr)
   {
      data_buf_node_ptr->buf_in_use = FALSE;
      data_buf_node_ptr->offset     = 0;
   }
   else
   {
      return AR_EUNEXPECTED;
   }

   // need to handle deprecation or re-allocation of buffer
   // todo : VB will need to update

   return result;
}

/* function to handle the write buffers received from the satellite graph
 * The Satellite Graph would consume the data and send the empty write buffers
 * in the write done event
 */
ar_result_t spdm_process_data_write_done(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id    = 0;
   uint32_t token     = 0;
   uint32_t bit_index = 0;

   write_data_port_obj_t                           *wr_ptr         = NULL;
   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t *write_done_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write_done: processing begin");
#endif

   VERIFY(result, (NULL != packet_ptr));
   VERIFY(result, (DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2 == packet_ptr->opcode));
   token = packet_ptr->token;

   write_done_ptr = (data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   VERIFY(result, (NULL != write_done_ptr));

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write_done: pkt token %lu status %lu", token, write_done_ptr->data_status);
#endif

   VERIFY(result, (AR_EOK == write_done_ptr->data_status) && (AR_EOK == write_done_ptr->md_status));
   // SDM_CA : check further the implication of a failure // discuss during review

   // Get the port data object
   wr_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wr_ptr));

   // Release the received write buffer to the write buffer pool
   TRY(result, spdm_release_write_buffer(spgm_ptr, wr_ptr, token));

   wr_ptr->port_info.ctrl_cfg.data_link_ps.wr_state = wait_for_ext_in_port_data;

   bit_index = cu_get_bit_index_from_mask(wr_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
   TRY(result, olc_write_done_handler(spgm_ptr->cu_ptr, bit_index));

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

// write data link process control decision
ar_result_t spdm_write_dl_pcd(spgm_info_t *spgm_ptr, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                            log_id = 0;
   write_data_port_obj_t              *wr_ptr = NULL;
   write_ipc_data_link_process_state_t wdl_state;

   wr_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wr_ptr));

   wdl_state = wr_ptr->port_info.ctrl_cfg.data_link_ps.wr_state;

   switch (wdl_state)
   {
      case wait_for_ext_in_port_data:
      {
         cu_start_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }

      case wait_for_ipc_write_data_done_evnt:
      {
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_start_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }
      case hold_processing_input_queues:
      {
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);
         cu_stop_listen_to_mask(spgm_ptr->cu_ptr, wr_ptr->port_info.ctrl_cfg.sat_rw_bit_mask);
         break;
      }
      default:
      {
         OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "invalid write data link state %lu", (uint32_t)wdl_state);
         result = AR_EFAILED;
         break;
      }
   }

#ifdef SGM_ENABLE_WRITE_DATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "write_data_link_state %lu channel mask 0x%lx",
               (uint32_t)wdl_state,
               spgm_ptr->cu_ptr->curr_chan_mask);
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}
