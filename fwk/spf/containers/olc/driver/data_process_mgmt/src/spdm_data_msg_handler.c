/**
 * \file spdm_data_msg_handler.c
 * \brief
 *     This file contains the data message handling functions
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spdm_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

ar_result_t spdm_process_send_wr_eos(spgm_info_t *spgm_ptr, module_cmn_md_eos_flags_t *flags, uint32_t port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != spgm_ptr->process_info.wdp_obj_ptr[port_index]));

#ifdef SGM_ENABLE_EOS_HANLDLING_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "md_dbg: processing eos");
#endif

   /* if the incoming EOS is internal, we need to propagate the corresponding state to the satellite. */
   if (TRUE == flags->is_internal_eos)
   {
      return spdm_process_us_port_state_change(spgm_ptr, port_index);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t sgm_flush_write_data_port(spgm_info_t *spgm_ptr,
                                      uint32_t     port_index,
                                      bool_t       is_flush,
                                      bool_t       is_post_processing)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   write_data_port_obj_t *                          wd_port_obj_ptr     = NULL;
   posal_queue_t *                                  data_q_ptr          = NULL;
   spf_msg_t *                                      data_q_msg          = NULL;
   gpr_packet_t *                                   packet_ptr          = NULL;
   shmem_data_buf_pool_t *                          write_data_pool_ptr = NULL;
   data_buf_pool_node_t *                           data_buf_node_ptr   = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   wd_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
   VERIFY(result, (NULL != wd_port_obj_ptr));

   if (wd_port_obj_ptr->db_obj.active_buf_node_ptr)
   {
      memset(&wd_port_obj_ptr->db_obj.data_buf, 0, sizeof(sdm_cnt_ext_data_buf_t));
      wd_port_obj_ptr->db_obj.active_buf_node_ptr->buf_in_use = FALSE;
      wd_port_obj_ptr->db_obj.active_buf_node_ptr->offset     = 0;
      wd_port_obj_ptr->db_obj.active_buf_node_ptr             = NULL;
   }

   data_q_ptr = wd_port_obj_ptr->port_info.this_handle.q_ptr;
   data_q_msg = &wd_port_obj_ptr->port_info.input_buf_q_msg;

   VERIFY(result, (NULL != data_q_ptr));

   while ((posal_channel_poll(posal_queue_get_channel(data_q_ptr), posal_queue_get_channel_bit(data_q_ptr))))
   {
      // Take next message from the queue.
      result = posal_queue_pop_front(data_q_ptr, (posal_queue_element_t *)(data_q_msg));
      if (AR_EOK != result)
      {
         break;
      }
      packet_ptr = (gpr_packet_t *)data_q_msg->payload_ptr;

      VERIFY(result, (NULL != packet_ptr));

      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "write msg handler, processing data queue flush (%lX) token (%lx) , pkt_ptr 0x%lx",
                  packet_ptr->opcode,
                  packet_ptr->token,
                  packet_ptr);

      VERIFY(result, (DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2 == packet_ptr->opcode));
      write_data_pool_ptr = &wd_port_obj_ptr->db_obj.buf_pool;

      if (AR_EOK !=
          (result = spdm_get_data_buf_node(spgm_ptr, write_data_pool_ptr, packet_ptr->token, &data_buf_node_ptr)))
      {
         if (NULL != packet_ptr)
         {
            __gpr_cmd_free(packet_ptr);
         }
         return result;
      }

      data_buf_node_ptr->buf_in_use = FALSE;
      data_buf_node_ptr->offset     = 0;

      if (NULL != packet_ptr)
      {
         __gpr_cmd_free(packet_ptr);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
      if (NULL != packet_ptr)
      {
         __gpr_cmd_free(packet_ptr);
      }
   }

   if ((NULL != wd_port_obj_ptr) && (NULL != spgm_ptr))
   {
      if ((is_flush) && (!is_post_processing))
      {
         wd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.wr_state = hold_processing_input_queues;
      }
      else
      {
         wd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.wr_state = wait_for_ext_in_port_data;
      }
      spdm_write_dl_pcd(spgm_ptr, port_index);
   }

   return result;
}

ar_result_t spdm_flush_read_meta_data(spgm_info_t *                                    spgm_ptr,
                                      data_buf_pool_node_t *                           db_node_ptr,
                                      uint32_t                                         rd_client_module_iid,
                                      uint32_t                                         port_index,
                                      data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *read_done_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t              log_id          = 0;
   uint32_t              md_rd_offset    = 0;
   uint32_t              md_payload_size = 0;
   bool_t                pool_used       = FALSE;
   module_cmn_md_flags_t flags;

   metadata_header_t *md_data_header_ptr = NULL;
   module_cmn_md_t *  ref_md_ptr         = NULL;
   uint8_t *          rd_md_ptr          = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "Flush read md, process start");
#endif
   md_payload_size = read_done_ptr->md_size;
   rd_md_ptr       = (uint8_t *)db_node_ptr->ipc_data_buf.shm_mem_ptr;
   rd_md_ptr       = rd_md_ptr + (ALIGN_128_BYTES(db_node_ptr->data_buf_size)) + 2 * GAURD_PROTECTION_BYTES;

   if (0 < md_payload_size)
   {
      while ((md_rd_offset + sizeof(metadata_header_t)) <= md_payload_size)
      {
         md_data_header_ptr = (metadata_header_t *)(rd_md_ptr + md_rd_offset);

         if ((md_rd_offset + sizeof(metadata_header_t) + md_data_header_ptr->payload_size) > md_payload_size)
         {
            OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read md, failed to process md payload");
            result = AR_EBADPARAM;
            break;
         }

         OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 10");

         flags.word = 0;
         gen_topo_convert_client_md_flag_to_int_md_flags(md_data_header_ptr->flags, &flags);

         if (MODULE_CMN_MD_ID_EOS == md_data_header_ptr->metadata_id)
         {
            module_cmn_md_eos_t *eos_metadata_ptr = (module_cmn_md_eos_t *)(md_data_header_ptr + 1);
            eos_metadata_ptr->cntr_ref_ptr        = NULL;
         }

         if ((MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY == flags.tracking_mode) ||
             (MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME == flags.tracking_mode))
         {
            spf_list_node_t *       curr_cont_node_ptr = (spf_list_node_t *)((uint64_t)md_data_header_ptr->token_lsw);
            sdm_tracking_md_node_t *md_node_ref_ptr    = (sdm_tracking_md_node_t *)(curr_cont_node_ptr->obj_ptr);
            if (md_node_ref_ptr->md_ptr)
            {
               ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;

               if (1 == md_node_ref_ptr->num_ref_count)
               {
                  if ((1 < md_node_ref_ptr->num_ref_count) && (1 < md_node_ref_ptr->max_ref_count))
                  {
                     spf_ref_counter_add_ref((void *)ref_md_ptr->tracking_ptr);
                  }
                  OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 11");
                  module_cmn_md_list_t *temp_node_ptr = (module_cmn_md_list_t *)(md_node_ref_ptr->md_ptr);
                  gen_topo_raise_tracking_event(spgm_ptr->cu_ptr->topo_ptr,
                                                rd_client_module_iid,
                                                temp_node_ptr,
                                                TRUE,
                                                NULL,
												FALSE);

                  md_node_ref_ptr->num_ref_count--;

                  if ((0 == md_node_ref_ptr->num_ref_count) && (1 == md_node_ref_ptr->max_ref_count))
                  {
                     if (ref_md_ptr)
                     {
                        OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 12");
                        // generic metadata is assumed to not require deep cloning
                        uint32_t is_out_band = ref_md_ptr->metadata_flag.is_out_of_band;
                        if (is_out_band)
                        {
                           pool_used = spf_lpi_pool_is_addr_from_md_pool(ref_md_ptr->metadata_ptr);
                           gen_topo_check_free_md_ptr(&(ref_md_ptr->metadata_ptr), pool_used);
                        }
                     }
                  }

                  if (0 == md_node_ref_ptr->num_ref_count)
                  {
                     OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 13");
                     if (md_node_ref_ptr->md_ptr)
                     {
                        pool_used = spf_lpi_pool_is_addr_from_md_pool(md_node_ref_ptr->md_ptr->obj_ptr);
                        gen_topo_check_free_md_ptr((void **)&(md_node_ref_ptr->md_ptr->obj_ptr), pool_used);
                     }
                     spf_list_delete_node_update_head((spf_list_node_t **)&temp_node_ptr,
                                                      (spf_list_node_t **)&md_node_ref_ptr->md_ptr,
                                                      TRUE);

                     spf_list_delete_node_update_head((spf_list_node_t **)&curr_cont_node_ptr,
                                                      (spf_list_node_t **)&spgm_ptr->process_info.tr_md.md_list_ptr,
                                                      FALSE);

                     posal_memory_free(md_node_ref_ptr);
                  }
               }
               else if (1 < md_node_ref_ptr->num_ref_count)
               {
                  if ((1 < md_node_ref_ptr->num_ref_count) && (1 < md_node_ref_ptr->max_ref_count))
                  {
                     spf_ref_counter_add_ref((void *)ref_md_ptr->tracking_ptr);
                  }
                  OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 1");
                  module_cmn_md_list_t *temp_node_ptr = (module_cmn_md_list_t *)(md_node_ref_ptr->md_ptr);
                  gen_topo_raise_tracking_event(spgm_ptr->cu_ptr->topo_ptr,
                                                rd_client_module_iid,
                                                temp_node_ptr,
                                                TRUE,
                                                NULL,
												FALSE);
                  OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process here 2");
                  md_node_ref_ptr->num_ref_count--;
               }
               else
               {
                  result = AR_EUNEXPECTED;
               }
            }
         }

         md_rd_offset += sizeof(metadata_header_t) + md_data_header_ptr->payload_size;
         OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process done %lu", md_rd_offset);
      }
   }

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "flush read md, process done");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t spdm_process_flush_read_done_data(spgm_info_t *                                    spgm_ptr,
                                              uint32_t                                         port_index,
                                              data_buf_pool_node_t *                           data_buf_node_ptr,
                                              data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *read_done_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;
   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   data_buf_pool_node_t *rd_buf_node_ptr = NULL;
   read_data_port_obj_t *rd_ptr          = NULL;

   rd_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_ptr));
   rd_buf_node_ptr = data_buf_node_ptr;

   // Extract the metadata from the read done buffer
   TRY(result,
       spdm_flush_read_meta_data(spgm_ptr,
                                 rd_buf_node_ptr,
                                 rd_ptr->port_info.ctrl_cfg.rw_client_miid,
                                 port_index,
                                 read_done_ptr));

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "spdm_process_flush_read_done_data, process done");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

// This function is flush all the element of the read queue.
// The function pops all the elements from the queue and adds to the buffer pool.
// Only an explicit destroy function will release all the memory allocated for the buffers

ar_result_t sgm_flush_read_data_port(spgm_info_t *spgm_ptr,
                                     uint32_t     port_index,
                                     bool_t       is_flush,
                                     bool_t       is_flush_post_processing)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   posal_queue_t *data_q_ptr = NULL;
   spf_msg_t *    data_q_msg = NULL;
   gpr_packet_t * packet_ptr = NULL;

   read_data_port_obj_t * rd_port_obj_ptr    = NULL;
   shmem_data_buf_pool_t *read_data_pool_ptr = NULL;
   data_buf_pool_node_t * data_buf_node_ptr  = NULL;

   data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *read_done_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id          = spgm_ptr->sgm_id.log_id;
   rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
   VERIFY(result, (NULL != rd_port_obj_ptr));

   if (rd_port_obj_ptr->db_obj.active_buf_node_ptr)
   {
      memset(&rd_port_obj_ptr->db_obj.data_buf, 0, sizeof(sdm_cnt_ext_data_buf_t));
      rd_port_obj_ptr->db_obj.active_buf_node_ptr->buf_in_use = FALSE;

      if ((is_flush) && (!is_flush_post_processing))
      {
         rd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = hold_processing_output_queues;
      }
      else
      {
         rd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ext_out_port_buf;
      }
      spdm_read_dl_pcd(spgm_ptr, port_index);
   }

   data_q_ptr = rd_port_obj_ptr->port_info.this_handle.q_ptr;
   data_q_msg = &rd_port_obj_ptr->port_info.output_data_q_msg;

   VERIFY(result, (NULL != data_q_ptr));

   while ((posal_channel_poll(posal_queue_get_channel(data_q_ptr), posal_queue_get_channel_bit(data_q_ptr))))
   {
      // Take next message from the queue.
      result = posal_queue_pop_front(data_q_ptr, (posal_queue_element_t *)(data_q_msg));
      if (AR_EOK != result)
      {
         break;
      }
      packet_ptr = (gpr_packet_t *)data_q_msg->payload_ptr;

      VERIFY(result, (NULL != packet_ptr));

      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_HIGH_PRIO,
                  "read msg handler, processing data queue flush (%lX) token (%lx) , pkt_ptr 0x%lx",
                  packet_ptr->opcode,
                  packet_ptr->token,
                  packet_ptr);

      VERIFY(result, (DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2 == packet_ptr->opcode));

      read_done_ptr      = (data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
      read_data_pool_ptr = &rd_port_obj_ptr->db_obj.buf_pool;

      if (AR_EOK !=
          (result = spdm_get_data_buf_node(spgm_ptr, read_data_pool_ptr, packet_ptr->token, &data_buf_node_ptr)))
      {
         if (NULL != packet_ptr)
         {
            __gpr_cmd_free(packet_ptr);
         }
         return result;
      }

      // Verify the shared memory pointer and invalidate the memory
      VERIFY(result, (NULL != data_buf_node_ptr->ipc_data_buf.shm_mem_ptr));
      if (AR_EOK != (result = posal_cache_invalidate((uint32_t)data_buf_node_ptr->ipc_data_buf.shm_mem_ptr,
                                                     data_buf_node_ptr->ipc_data_buf.shm_alloc_size)))
      {
         OLC_SDM_MSG(OLC_SDM_ID, DBG_ERROR_PRIO, "read_data_done: failed to cache invalidate the read buffer");
         return (AR_EPANIC | result);
      }

      TRY(result, spdm_process_flush_read_done_data(spgm_ptr, port_index, data_buf_node_ptr, read_done_ptr));

      data_buf_node_ptr->buf_in_use = FALSE;
      data_buf_node_ptr->offset     = 0;

      if ((is_flush) && (is_flush_post_processing))
      {
         spdm_send_read_data_buffer(spgm_ptr, data_buf_node_ptr, rd_port_obj_ptr);
         rd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ext_out_port_buf;
         spdm_read_dl_pcd(spgm_ptr, port_index);
      }

      memset(&rd_port_obj_ptr->db_obj.data_buf, 0, sizeof(sdm_cnt_ext_data_buf_t));

      if (NULL != packet_ptr)
      {
         __gpr_cmd_free(packet_ptr);
      }
   }

   if ((is_flush_post_processing))
   {
      rd_port_obj_ptr->db_obj.active_buf_node_ptr = NULL;
      sgm_send_all_read_buffers(spgm_ptr, port_index);
      rd_port_obj_ptr->port_info.ctrl_cfg.data_link_ps.rd_state = wait_for_ext_out_port_buf;
      spdm_read_dl_pcd(spgm_ptr, port_index);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
      if (NULL != packet_ptr)
      {
         __gpr_cmd_free(packet_ptr);
      }
   }

   return result;
}
