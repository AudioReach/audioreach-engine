/**
 * \file gen_cntr_offload_utils.c
 * \brief
 *     This file contains utility functions for required for off-loading in MDF
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "media_fmt_extn_api.h"
#include "offload_sp_api.h"
#include "offload_metatdata_api.h"

static ar_result_t gen_cntr_recreate_out_buf_olc_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        uint32_t                 req_out_buf_size,
                                                        uint32_t                 num_data_msg,
                                                        uint32_t                 num_bufs_per_data_msg_v2);

static ar_result_t gen_cntr_copy_olc_client_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                             gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                             uint32_t *              bytes_copied_per_buf_ptr);

static ar_result_t gen_cntr_send_cmd_path_media_fmt_to_olc_client(gen_cntr_t *             me_ptr,
                                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr);

static ar_result_t gen_cntr_write_data_for_olc_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);

static void gen_cntr_write_metadata_in_olc_rd_client_buffer(gen_cntr_t *             me_ptr,
                                                            gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                            uint32_t                 frame_offset_in_ext_buf,
                                                            bool_t *                 release_out_buf_ptr);

// clang-format off

const gen_cntr_ext_in_vtable_t gpr_olc_client_ext_in_vtable = {
   .on_trigger               = gen_cntr_input_dataQ_trigger_gpr_client,
   .read_data                = gen_cntr_copy_olc_client_input_to_int_buf,
   .is_data_buf              = gen_cntr_is_input_a_gpr_client_data_buffer,
   .process_pending_data_cmd = gen_cntr_process_pending_data_cmd_gpr_client,
   .free_input_buf           = gen_cntr_free_input_data_cmd_gpr_client,
   .frame_len_change_notif   = gen_cntr_send_operating_framesize_event_to_wr_shmem_client,
   .setup_topo_buf           = NULL,
    // todo: assign .setup_topo_buf= gen_cntr_wr_shm_setup_int_port_buf() for optimizations.
    // refer write shared memory ep behavior.
};

const gen_cntr_ext_out_vtable_t gpr_olc_client_ext_out_vtable = {
   .setup_bufs       = gen_cntr_output_buf_set_up_gpr_client,
   .write_data       = gen_cntr_write_data_for_olc_client,
   .fill_frame_md    = gen_cntr_fill_frame_metadata,
   .get_filled_size  = gen_cntr_get_amount_of_data_in_gpr_client_buf,
   .flush            = gen_cntr_flush_output_data_queue_gpr_client,
   .recreate_out_buf = gen_cntr_recreate_out_buf_olc_client,
   .prop_media_fmt   = gen_cntr_send_cmd_path_media_fmt_to_olc_client,
   .write_metadata   = gen_cntr_write_metadata_in_olc_rd_client_buffer,
   .setup_topo_buf   = NULL,
    // todo: assign .setup_topo_buf= gen_cntr_rd_sh_mem_setup_topo_buffer() for optimizations.
    // refer read shared memory ep behavior.
};

// clang-format on

// Function to parse the meta-data of the read EP module and update the external output metadata buffer
// CA : For EOS, we need another for prop vs event
static void gen_cntr_write_metadata_in_olc_rd_client_buffer(gen_cntr_t *             me_ptr,
                                                            gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                            uint32_t                 frame_offset_in_ext_buf,
                                                            bool_t *                 release_out_buf_ptr)
{
   uint32_t last_module_id                  = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_id;
   ext_out_port_ptr->buf.md_buf_ptr->status = AR_EOK; // initialize the md status
   if (MODULE_ID_RD_SHARED_MEM_EP != last_module_id)
   {
      ext_out_port_ptr->buf.md_buf_ptr->status = AR_EUNEXPECTED;
   }

   uint32_t md_avail_buf_size                  = 0; // available md buffer size to fill
   uint32_t meta_data_num_elements             = 0; // number of md elements filled in this call
   uint32_t md_element_size                    = 0; // size of the md filled in this call
   uint32_t md_payload_size                    = 0;
   uint32_t client_md_flags                    = 0;
   uint32_t need_to_cache_md                   = 0;
   uint32_t header_extn_size                   = 0;
   uint32_t is_internal_md_created_in_local_pd = 0;
   uint32_t actual_md_copy_size                = 0;

   module_cmn_md_list_t *            node_ptr             = NULL;
   module_cmn_md_list_t *            temp_node_ptr        = NULL;
   metadata_header_t *               md_data_header_ptr   = NULL;
   module_cmn_md_t *                 md_ptr               = NULL;
   module_cmn_md_tracking_payload_t *tracking_payload_ptr = NULL;
   uint8_t *                         src_md_payload_ptr   = NULL;
   uint8_t *                         dst_md_payload_ptr   = NULL;
   int8_t *                          client_buf_md_ptr    = NULL;

   node_ptr          = ext_out_port_ptr->md_list_ptr;
   client_buf_md_ptr = ext_out_port_ptr->buf.md_buf_ptr->data_ptr + ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;
   md_avail_buf_size =
      (ext_out_port_ptr->buf.md_buf_ptr->max_data_len - ext_out_port_ptr->buf.md_buf_ptr->actual_data_len);

   while ((node_ptr))
   {
      md_ptr                             = node_ptr->obj_ptr;
      need_to_cache_md                   = 0;
      header_extn_size                   = 0;
      is_internal_md_created_in_local_pd = 0;
      if (NULL != md_ptr)
      {
         if ((MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id) && (release_out_buf_ptr))
         {
            *release_out_buf_ptr = TRUE;
         }

         if (NULL != md_ptr->tracking_ptr)
         {
            if (MODULE_CMN_MD_IS_EXTERNAL_CLIENT_MD == md_ptr->metadata_flag.is_client_metadata)
            {
               is_internal_md_created_in_local_pd = 1;
            }
            else if (MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD == md_ptr->metadata_flag.is_client_metadata)
            {
               if (md_ptr->tracking_ptr->src_domain_id == md_ptr->tracking_ptr->dst_domain_id)
               {
                  is_internal_md_created_in_local_pd = 1;
               }
            }
            if (is_internal_md_created_in_local_pd)
            {
               header_extn_size =
                  ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + sizeof(param_id_md_extn_md_origin_cfg_t));
            }
         }

         if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
         {
            // EOS payload is internal and only a subset of the payload is propagated in MDF cases
            md_payload_size = sizeof(module_cmn_md_eos_ext_t);
            md_element_size = ALIGN_4_BYTES(md_payload_size) + sizeof(metadata_header_t) + header_extn_size;
         }
         else
         {
            md_payload_size = md_ptr->actual_size;
            md_element_size = ALIGN_4_BYTES(md_ptr->actual_size) + sizeof(metadata_header_t) + header_extn_size;
         }

         if (md_element_size <= md_avail_buf_size)
         {
            md_data_header_ptr = (metadata_header_t *)client_buf_md_ptr;
            gen_topo_convert_int_md_flags_to_client_md_flag(md_ptr->metadata_flag, &client_md_flags);
            md_data_header_ptr->metadata_id  = md_ptr->metadata_id;
            md_data_header_ptr->payload_size = md_payload_size;
            md_data_header_ptr->flags        = client_md_flags;
            md_data_header_ptr->offset       = frame_offset_in_ext_buf + md_ptr->offset; // frame offset + md_offset

            if (NULL != md_ptr->tracking_ptr)
            {
               tracking_payload_ptr = (module_cmn_md_tracking_payload_t *)md_ptr->tracking_ptr;
               if ((MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD == md_ptr->metadata_flag.is_client_metadata) &&
                   (!is_internal_md_created_in_local_pd))
               {

                  md_data_header_ptr->token_lsw = tracking_payload_ptr->token_lsw;
                  md_data_header_ptr->token_msw = tracking_payload_ptr->token_msw;
               }
               else if (is_internal_md_created_in_local_pd)
               {
                  uint64_t node_address = (uint64_t)node_ptr;
            	  md_data_header_ptr->token_lsw = (uint32_t)node_address;
                  md_data_header_ptr->token_msw = (uint32_t)(node_address >> 32);
                  need_to_cache_md              = 1; // caching would be needed only if created in satellite
               }
            }
            else
            {
               md_data_header_ptr->token_lsw = 0;
               md_data_header_ptr->token_msw = 0;
            }

            uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;
            if (is_out_band)
            {
               src_md_payload_ptr = (uint8_t *)md_ptr->metadata_ptr;
            }
            else
            {
               src_md_payload_ptr = (uint8_t *)&(md_ptr->metadata_buf);
            }

            //#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "MD_DBG: SPF to OLC CLIENT: Wrote MD ID %lx, buffer offset %d payload_size %lu"
                         " flags 0x%x  token (lsw, msw) 0x%lx 0x%lx",
                         md_ptr->metadata_id,
                         md_data_header_ptr->offset,
                         md_data_header_ptr->payload_size,
                         md_data_header_ptr->flags,
                         md_data_header_ptr->token_lsw,
                         md_data_header_ptr->token_msw);
            //#endif

            dst_md_payload_ptr = (uint8_t *)(md_data_header_ptr + 1);
            actual_md_copy_size =
               memscpy(dst_md_payload_ptr, md_data_header_ptr->payload_size, src_md_payload_ptr, md_payload_size);
            dst_md_payload_ptr += ALIGN_4_BYTES(actual_md_copy_size);

            if (header_extn_size)
            {
               uint32_t md_header_extension_set = 1;
               uint32_t p_size                  = 0;

               // Setting the bit in the flags
               tu_set_bits(&md_data_header_ptr->flags,
                           md_header_extension_set,
                           MD_HEADER_FLAGS_BIT_MASK_NEEDS_HEADER_EXTN_PRESENT,
                           MD_HEADER_FLAGS_SHIFT_HEADER_EXTN_PRESENT);

               // fill the header extn details
               metadata_header_extn_t *md_header_extn_ptr = (metadata_header_extn_t *)(dst_md_payload_ptr);
               md_header_extn_ptr->metadata_extn_param_id = PARAM_ID_MD_EXTN_MD_ORIGIN_CFG;
               md_header_extn_ptr->payload_size           = sizeof(param_id_md_extn_md_origin_cfg_t);

               p_size = sizeof(metadata_header_extn_t);

               param_id_md_extn_md_origin_cfg_t *param_payload_ptr =
                  (param_id_md_extn_md_origin_cfg_t *)(md_header_extn_ptr + 1);

               // fill the md header extn payload
               param_payload_ptr->is_md_originated_in_src_domain = is_internal_md_created_in_local_pd;
               param_payload_ptr->domain_id                      = tracking_payload_ptr->src_domain_id;

               p_size += sizeof(param_id_md_extn_md_origin_cfg_t);

               header_extn_size = ALIGN_4_BYTES(p_size);
               //dst_md_payload_ptr += header_extn_size;

               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_HIGH_PRIO,
                            "MD_DBG: SPF to OLC CLIENT: Wrote MD ID %lx, header extn enabled, extn_size %lu, extn_id "
                            "0x%lx extn_id_size %lu",
                            md_ptr->metadata_id,
                            header_extn_size,
                            md_header_extn_ptr->metadata_extn_param_id,
                            md_header_extn_ptr->payload_size);
               //#endif
            }

            meta_data_num_elements++;
            ext_out_port_ptr->buf.md_buf_ptr->actual_data_len += md_element_size;
            client_buf_md_ptr += md_element_size;
            md_avail_buf_size -= md_element_size;

            temp_node_ptr = node_ptr;
            node_ptr      = node_ptr->next_ptr;
            if (!need_to_cache_md)
            {
               gen_topo_capi_metadata_destroy((void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                              temp_node_ptr,
                                              FALSE /*is_dropped*/,
                                              &ext_out_port_ptr->md_list_ptr,
                                              0,
                                              TRUE); // rendered
            }
            else
            {
               // do not destroy the node, if client is SPF master. // tracking mode is last
               spf_list_move_node_to_another_list((spf_list_node_t **)&ext_out_port_ptr->cached_md_list_ptr,
                                                  (spf_list_node_t *)temp_node_ptr,
                                                  (spf_list_node_t **)&ext_out_port_ptr->md_list_ptr);
            }
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "MD_DBG: failed to propagate metadata id %lx from SPF to OLC CLIENT  ",
                         md_ptr->metadata_id);
            ext_out_port_ptr->buf.md_buf_ptr->status = AR_ENEEDMORE;
            break;
         }
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "MD_DBG: failed to propagate metadata id, md obj is null. unexpected ");
         break;
      }
   }

   if (0 != meta_data_num_elements)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "MD_DBG: Num metadata elements 0x%lX",
                   meta_data_num_elements);
   }
}

/* function to send the operating frame-size to the RD client */
ar_result_t gen_cntr_offload_send_opfs_event_to_rd_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t           result     = AR_EOK;
   shmem_ep_frame_size_t frame_size = { 0 };

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;

   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   if ((NULL == module_ptr) || (NULL == gen_cntr_module_ptr->fwk_module_ptr) ||
       (NULL == gen_cntr_module_ptr->cu.event_list_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "GEN_CNTR: Not raising DATA_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE");
      return AR_EFAILED;
   }

   spf_list_node_t *client_list_ptr;
   cu_find_client_info(me_ptr->topo.gu.log_id,
                       OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE,
                       gen_cntr_module_ptr->cu.event_list_ptr,
                       &client_list_ptr);

   if ((NULL != client_list_ptr))
   {
      frame_size.buf_size_in_bytes = ext_out_port_ptr->cu.buf_max_size;
      frame_size.ep_miid           = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id;
      frame_size.ep_module_type    = RD_SHARED_MEM_EP;
      for (cu_client_info_t *client_info_ptr = (cu_client_info_t *)(client_list_ptr->obj_ptr);
           (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {

         gpr_packet_t *      gpr_pkt_op_fs_ptr         = NULL;
         void *              gpr_pkt_op_fs_payload_ptr = NULL;
         gpr_cmd_alloc_ext_t args;

         args.src_domain_id = client_info_ptr->dest_domain_id;
         args.dst_domain_id = client_info_ptr->src_domain_id;
         args.src_port      = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id;
         args.dst_port      = ext_out_port_ptr->client_config.cid.gpr_port_id;
         args.token         = client_info_ptr->token;
         args.opcode        = OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE;
         args.payload_size  = sizeof(shmem_ep_frame_size_t);
         args.ret_packet    = &gpr_pkt_op_fs_ptr;
         args.client_data   = 0;
         result             = __gpr_cmd_alloc_ext(&args);
         if (AR_DID_FAIL(result) || NULL == gpr_pkt_op_fs_ptr)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Allocating op fs pkt to send to dsp client failed with %lu",
                         result);
            return AR_ENOMEMORY;
         }
         gpr_pkt_op_fs_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_op_fs_ptr);
         memscpy(gpr_pkt_op_fs_payload_ptr, args.payload_size, &frame_size, args.payload_size);

         if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_op_fs_ptr)))
         {
            result = AR_EFAILED;
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Sending OP FS event pkt to client failed with %lu",
                         result);
            __gpr_cmd_free(gpr_pkt_op_fs_ptr);
         }
      }
   }

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Failed to raise operating frame-size update to OLC RD SHMEM client");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Sucessfully raised operating "
                   "frame-size %lu to OLC RD SHMEM client",
                   frame_size.buf_size_in_bytes);
   }

   return result;
}

/* function to send the operating frame-size to the WR client */
ar_result_t gen_cntr_send_operating_framesize_event_to_wr_shmem_client(gen_cntr_t *            me_ptr,
                                                                       gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   shmem_ep_frame_size_t frame_size      = { 0 };
   spf_list_node_t *     client_list_ptr = NULL;

   gen_topo_module_t *module_ptr          = (gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr;
   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   if ((NULL == module_ptr) || (NULL == gen_cntr_module_ptr->fwk_module_ptr) ||
       (NULL == gen_cntr_module_ptr->cu.event_list_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "GEN_CNTR: Not raising DATA_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE");
      return AR_EFAILED;
   }

   cu_find_client_info(me_ptr->topo.gu.log_id,
                       OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE,
                       gen_cntr_module_ptr->cu.event_list_ptr,
                       &client_list_ptr);

   if ((NULL != client_list_ptr))
   {
      frame_size.buf_size_in_bytes = me_ptr->cu.cntr_frame_len.frame_len_samples;
      frame_size.ep_miid           = ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id;
      frame_size.ep_module_type    = WR_SHARED_MEM_EP;
      for (cu_client_info_t *client_info_ptr = (cu_client_info_t *)(client_list_ptr->obj_ptr);
           (NULL != client_list_ptr);
           LIST_ADVANCE(client_list_ptr))
      {

         gpr_packet_t *      gpr_pkt_op_fs_ptr         = NULL;
         void *              gpr_pkt_op_fs_payload_ptr = NULL;
         gpr_cmd_alloc_ext_t args;

         args.src_domain_id = client_info_ptr->dest_domain_id;
         args.dst_domain_id = client_info_ptr->src_domain_id;
         args.src_port      = ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id;
         args.dst_port      = ext_in_port_ptr->client_config.cid.gpr_port_id;
         args.token         = client_info_ptr->token;
         args.opcode        = OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE;
         args.payload_size  = sizeof(shmem_ep_frame_size_t);
         args.ret_packet    = &gpr_pkt_op_fs_ptr;
         args.client_data   = 0;
         result             = __gpr_cmd_alloc_ext(&args);
         if (AR_DID_FAIL(result) || NULL == gpr_pkt_op_fs_ptr)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Allocating op fs pkt to send to wr shmem client failed with %lu",
                         result);
            return AR_ENOMEMORY;
         }
         gpr_pkt_op_fs_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_op_fs_ptr);
         memscpy(gpr_pkt_op_fs_payload_ptr, args.payload_size, &frame_size, args.payload_size);

         if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_op_fs_ptr)))
         {
            result = AR_EFAILED;
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Sending OP FS event pkt to wr shmem client failed with %lu",
                         result);
            __gpr_cmd_free(gpr_pkt_op_fs_ptr);
         }
      }
   }

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Failed to raise operating frame-size update to OLC WR SHMEM client");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Sucessfully raised operating "
                   "frame-size %lu to OLC WR SHMEM client",
                   frame_size.buf_size_in_bytes);
   }

   return result;
}

/* function to send the operating frame-size to all the WR clients */
ar_result_t gen_cntr_offload_send_opfs_event_to_wr_client(gen_cntr_t *me_ptr)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = NULL;
   cu_base_t *             base_ptr        = &me_ptr->cu;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gu_ext_in_port_t *gu_ext_in_port_ptr = (gu_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      ext_in_port_ptr                      = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
      if (ext_in_port_ptr->vtbl_ptr->frame_len_change_notif)
      {
         ext_in_port_ptr->vtbl_ptr->frame_len_change_notif(me_ptr, ext_in_port_ptr);
      }
   }

   return result;
}

/* function to handle the peer port property cmd handling from master to satellite.
 * i.e., from the OLC WR/RD CLient module to the RD/WR SHMEM EP modules
 */
ar_result_t gen_cntr_offload_process_peer_port_property_param(gen_cntr_t *  me_ptr,
                                                              spf_handle_t *handle_ptr,
                                                              int8_t *      param_data_ptr,
                                                              uint32_t      param_size)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t req_psize = 0;

   param_id_peer_port_property_t *peer_property_payload_ptr = NULL;

   VERIFY(result, (NULL != me_ptr));

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "processing peer port property configuration command from OLC");

   // validate the function arguments
   VERIFY(result, (NULL != handle_ptr));
   VERIFY(result, (NULL != param_data_ptr));
   VERIFY(result, param_size >= sizeof(param_id_peer_port_property_t));

   peer_property_payload_ptr = (param_id_peer_port_property_t *)param_data_ptr;

   req_psize = sizeof(param_id_peer_port_property_t);
   req_psize += peer_property_payload_ptr->num_properties * sizeof(spf_msg_peer_port_property_info_t);
   if (param_size < req_psize)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "processing peer port property configuration, "
                   "required payload size %lu is less than actual payload size %lu",
                   req_psize,
                   param_size);

      THROW(result, AR_EBADPARAM);
   }

   result = cu_process_peer_port_property_payload(&me_ptr->cu, (int8_t *)peer_property_payload_ptr, handle_ptr);

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Failed to process the peer port property configuration from OLC");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "sucessfully processed the peer port property configuration from OLC");
   }

   spf_msg_peer_port_property_info_t *prop_ptr = (spf_msg_peer_port_property_info_t *)(peer_property_payload_ptr + 1);

   for (uint32_t i = 0; i < peer_property_payload_ptr->num_properties; i++)
   {
      if (PORT_PROPERTY_TOPO_STATE == prop_ptr[i].property_type)
      {
         gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)handle_ptr;
         topo_port_state_t        ds_state         = (topo_port_state_t)prop_ptr[i].property_value;
         if (TOPO_PORT_STATE_STARTED == ds_state && ext_out_port_ptr->cu.buf_max_size > 0)
         {
            gen_cntr_offload_send_opfs_event_to_rd_client(me_ptr, ext_out_port_ptr);
         }
      }
   }
   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return result;
}

/* function to process the data cmd peer port configuration
 * Internal EOS is sent as a data message from the OLC to the satellite,
 * rather than EOS command. */
ar_result_t gen_cntr_offload_process_data_cmd_port_property_cfg(gen_cntr_t *            me_ptr,
                                                                gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id                  = 0;
   uint32_t peer_port_property_size = 0;
   uint32_t required_size           = 0;

   gen_topo_input_port_t *                     in_port_ptr                = NULL;
   gpr_packet_t *                              packet_ptr                 = NULL;
   data_cmd_wr_sh_mem_ep_peer_port_property_t *peer_port_property_ptr     = NULL;
   spf_msg_peer_port_property_info_t *         port_property_cfg_ptr      = NULL;
   spf_msg_peer_port_property_info_t *         curr_port_property_cfg_ptr = NULL;

   VERIFY(result, (NULL != me_ptr));
   log_id = me_ptr->topo.gu.log_id;
   VERIFY(result, (NULL != ext_in_port_ptr));
   VERIFY(result, (NULL != ext_in_port_ptr->cu.input_data_q_msg.payload_ptr));

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "processing peer port property data command from OLC");

   packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   // get the payload and validate the pointer
   peer_port_property_ptr = GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_peer_port_property_t, packet_ptr);
   VERIFY(result, NULL != peer_port_property_ptr);

   // get the payload size and validate the minimum required size
   peer_port_property_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
   VERIFY(result, peer_port_property_size >= sizeof(data_cmd_wr_sh_mem_ep_peer_port_property_t));

   // validate the minimum number of properties
   VERIFY(result, peer_port_property_ptr->num_properties >= 1);

   // determine the payload size based on the num properties and validate the size
   required_size = sizeof(data_cmd_wr_sh_mem_ep_peer_port_property_t) +
                   peer_port_property_ptr->num_properties * sizeof(spf_msg_peer_port_property_info_t);
   VERIFY(result, peer_port_property_size >= required_size);

   port_property_cfg_ptr = (spf_msg_peer_port_property_info_t *)(peer_port_property_ptr + 1);

   // parse the peer property payload and handle the data command appropriately
   for (uint32_t i = 0; i < peer_port_property_ptr->num_properties; i++)
   {
      curr_port_property_cfg_ptr = port_property_cfg_ptr + i; // move the pointer by the size of the property structure.

      switch (curr_port_property_cfg_ptr->property_type)
      {
         case PORT_PROPERTY_DATA_FLOW_STATE:
         {
            // Indicate the internal EOS has arrived at OLC input port and the data cmd with data flow gap is
            // is sent to satellite WR EP module. This function inserts an internal EOS at the WR EP input port
            if (TOPO_DATA_FLOW_STATE_AT_GAP == curr_port_property_cfg_ptr->property_value)
            {
               in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
               if ((TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
                   (TOPO_DATA_FLOW_STATE_AT_GAP != in_port_ptr->common.data_flow_state))
               {
                  module_cmn_md_eos_flags_t eos_md_flag = { .word = 0 };
                  eos_md_flag.is_flushing_eos           = MODULE_CMN_MD_EOS_FLUSHING;
                  eos_md_flag.is_internal_eos           = TRUE;

                  result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                                        (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr,
                                                        ext_in_port_ptr->cu.id,
                                                        me_ptr->cu.heap_id,
                                                        &ext_in_port_ptr->md_list_ptr,
                                                        NULL,         /* md_flag_ptr */
                                                        NULL,         /*tracking_payload_ptr*/
                                                        &eos_md_flag, /* eos_payload_flags_ptr */
                                                        gen_cntr_get_bytes_in_ext_in_for_md(ext_in_port_ptr),
                                                        &ext_in_port_ptr->cu.media_fmt);

                  if (AR_SUCCEEDED(result))
                  {
                     me_ptr->topo.flags.process_us_gap = TRUE;
                  }
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_HIGH_PRIO,
                               "MD_DBG: Created EOS for ext in port (0x%0lX, 0x%lx) with result 0x%lx",
                               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                               in_port_ptr->gu.cmn.id,
                               result);
               }
            }
            break;
         }
         default:
         {
            // unsupported property for data command
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "processing peer port property data command from OLC. unsupported property type %lu",
                         curr_port_property_cfg_ptr->property_value);
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* function to process the internal EOS at RD EP and propagate the data flow gap through peer port property
 * configuration*/
ar_result_t gen_cntr_offload_propagate_internal_eos_port_property_cfg(gen_cntr_t *             me_ptr,
                                                                      gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t        result              = AR_EOK;
   gen_topo_module_t *module_ptr          = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   if ((NULL == module_ptr) || (NULL == gen_cntr_module_ptr->fwk_module_ptr) ||
       (NULL == gen_cntr_module_ptr->cu.event_list_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "GEN_CNTR: failed to raise event to OLC for internal EOS propagation, invalid module parameters ");
      return AR_EFAILED;
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_MED_PRIO,
                "GEN_CNTR: raising event to OLC for internal EOS propagation from rd_ep miid 0x%lx",
                module_ptr->gu.module_instance_id);

   spf_msg_peer_port_property_update_t internal_eos_peer_property;
   internal_eos_peer_property.num_properties            = 1;
   internal_eos_peer_property.payload[0].property_type  = PORT_PROPERTY_DATA_FLOW_STATE;
   internal_eos_peer_property.payload[0].property_value = TOPO_DATA_FLOW_STATE_AT_GAP;

   result = cu_propagate_to_parent_container_ext_port(&me_ptr->cu,
                                                      gen_cntr_module_ptr->cu.event_list_ptr,
                                                      OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY,
                                                      module_ptr->gu.module_instance_id,
                                                      (int8_t *)&internal_eos_peer_property);

   if (AR_EOK != result)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "GEN_CNTR: failed to raise event to OLC for internal EOS propagation from rd_ep miid 0x%lx",
                   module_ptr->gu.module_instance_id);
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "GEN_CNTR: sucessfully raised event to OLC for internal EOS propagation from rd_ep miid 0x%lx",
                   module_ptr->gu.module_instance_id);
   }

   return result;
}

ar_result_t gen_cntr_offload_reg_evt_rd_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                  gen_cntr_module_t *module_ptr,
                                                  topo_reg_event_t * event_cfg_payload_ptr,
                                                  bool_t             is_register)
{
   ar_result_t result                    = AR_EOK;
   uint32_t    event_id                  = event_cfg_payload_ptr->event_id;
   uint32_t    event_config_payload_size = event_cfg_payload_ptr->event_cfg.actual_data_len;

   switch (event_id)
   {
      case OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE:
      case OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
      case OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
      case OFFLOAD_EVENT_ID_UPSTREAM_STATE:
      case OFFLOAD_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
      {
         if (0 == event_config_payload_size)
         {
            result = gen_cntr_cache_set_event_prop(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);
            if (is_register && AR_SUCCEEDED(result))
            {
               if ((OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY == event_id) ||
                   (OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY == event_id) ||
                   (OFFLOAD_EVENT_ID_UPSTREAM_STATE == event_id))
               {
                  gen_cntr_ext_out_port_t *ext_out_port_ptr =
                     (gen_cntr_ext_out_port_t *)module_ptr->topo.gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
                  ext_out_port_ptr->cu.prop_info.prop_enabled = TRUE;
               }
            }
         }
         else
         {
            result = AR_EFAILED;
         }
         break;
      }
      default:
      {
         TOPO_MSG(me_ptr->topo.gu.log_id,
                  DBG_ERROR_PRIO,
                  " Unsupported event id 0x%lX for framework module-id 0x%lX",
                  event_id,
                  module_ptr->topo.gu.module_id);
         result = AR_EUNSUPPORTED;
         break;
      }
   }

   return result;
}

/**
 * this module doesn't support PARAM_ID_MODULE_ENABLE
 */
ar_result_t gen_cntr_offload_handle_set_cfg_to_rd_sh_mem_ep(gen_cntr_t *        me_ptr,
                                                            gen_cntr_module_t * gen_cntr_module_ptr,
                                                            uint32_t            param_id,
                                                            int8_t *            param_data_ptr,
                                                            uint32_t            param_size,
                                                            spf_cfg_data_type_t cfg_type)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gen_cntr_module_ptr;
   switch (param_id)
   {
      case PARAM_ID_RD_EP_CLIENT_CFG:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "Processing RD_EP client configuration cmd set by OLC");

         // has only one port.
         gen_cntr_ext_out_port_t *ext_out_port_ptr =
            (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
         VERIFY(result, param_size >= sizeof(param_id_rd_ep_client_cfg_t));
         param_id_rd_ep_client_cfg_t *rd_ep_client_cfg_ptr = (param_id_rd_ep_client_cfg_t *)(param_data_ptr);

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      " PARAM_ID_RD_EP_CLIENT_CFG: client ID = %lu "
                      "(0/1 : HLOS/OLC), port_id = %lu",
                      rd_ep_client_cfg_ptr->client_id,
                      rd_ep_client_cfg_ptr->gpr_port_id);

         ext_out_port_ptr->client_config.cid.client_id                        = rd_ep_client_cfg_ptr->client_id;
         ext_out_port_ptr->client_config.cid.gpr_port_id                      = rd_ep_client_cfg_ptr->gpr_port_id;
         ext_out_port_ptr->cu.icb_info.flags.is_default_single_buffering_mode = TRUE;
         me_ptr->cu.offload_info_ptr->is_satellite_ep_container               = TRUE;
         ext_out_port_ptr->vtbl_ptr                                           = &gpr_olc_client_ext_out_vtable;

         for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr;
              (NULL != ext_in_port_list_ptr);
              LIST_ADVANCE(ext_in_port_list_ptr))
         {
            gu_ext_in_port_t *gu_ext_in_port_ptr = (gu_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
            cu_ext_in_port_t *ext_in_port_ptr =
               (cu_ext_in_port_t *)(((uint8_t *)gu_ext_in_port_ptr + me_ptr->cu.ext_in_port_cu_offset));

            ext_in_port_ptr->icb_info.flags.is_default_single_buffering_mode = TRUE;
         }
         break;
      }
      case PARAM_ID_PEER_PORT_PROPERTY_UPDATE:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Processing downstream state config update from OLC");

         // has only one port.
         gen_cntr_ext_out_port_t *ext_out_port_ptr =
            (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;

         TRY(result,
             gen_cntr_offload_process_peer_port_property_param(me_ptr,
                                                               &ext_out_port_ptr->gu.this_handle,
                                                               param_data_ptr,
                                                               param_size));

         break;
      }
      case PARAM_ID_RD_EP_MD_RENDERED_CFG:
      {
         // has only one port.
         gen_cntr_ext_out_port_t *ext_out_port_ptr =
            (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;

         VERIFY(result, param_size >= sizeof(param_id_rd_ep_md_rendered_cfg_t));

         param_id_rd_ep_md_rendered_cfg_t *rd_ep_md_rendered_cfg_ptr =
            (param_id_rd_ep_md_rendered_cfg_t *)(param_data_ptr);

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "MD_DBG: Processing MD Rendered configuration update from OLC with"
                      "token (msw,lsw): 0x%lx 0x%lx, is_last_instance %lu, md_sink_miid 0x%lx is_md_dropped %lu",
                      rd_ep_md_rendered_cfg_ptr->md_node_address_msw,
                      rd_ep_md_rendered_cfg_ptr->md_node_address_lsw,
                      rd_ep_md_rendered_cfg_ptr->is_last_instance,
                      rd_ep_md_rendered_cfg_ptr->md_rendered_port_id,
                      rd_ep_md_rendered_cfg_ptr->is_md_dropped);

         module_cmn_md_list_t *temp_node_ptr = NULL;
         uint64_t node_address = (uint64_t)((((uint64_t)(rd_ep_md_rendered_cfg_ptr->md_node_address_msw)) << 32) |
                                            (rd_ep_md_rendered_cfg_ptr->md_node_address_lsw));
         temp_node_ptr         = (module_cmn_md_list_t *)node_address;

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "MD_DBG: Processing MD Rendered configuration update from OLC, Metadata node address 0x%llx",
                      temp_node_ptr);

         // check for valid cached metatdat list
         if (ext_out_port_ptr->cached_md_list_ptr)
         {
            if (rd_ep_md_rendered_cfg_ptr->is_last_instance)
            {
               // destroy this metadata instance if the MD rendered on master is tha last
               gen_topo_capi_metadata_destroy((void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                              temp_node_ptr,
                                              FALSE /*is_dropped*/,
                                              &ext_out_port_ptr->cached_md_list_ptr,
                                              rd_ep_md_rendered_cfg_ptr->md_rendered_port_id,
                                              FALSE); // rendered
            }
            else
            {
               // if the md instance rendered on master is not last, increment the ref count for MD
               // and raise the tracking event, the reference count would be later decreased while raising the event
               gen_topo_module_t *module_ptr =
                  (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
               spf_ref_counter_add_ref(temp_node_ptr->obj_ptr->tracking_ptr);
               gen_topo_raise_tracking_event(module_ptr->topo_ptr,
                                             rd_ep_md_rendered_cfg_ptr->md_rendered_port_id,
                                             temp_node_ptr,
                                             !rd_ep_md_rendered_cfg_ptr->is_md_dropped,
                                             NULL,
                                             FALSE);
            }
         }

         break;
      }

      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Unsupported param-id 0x%lx", param_id);
         result = AR_EUNSUPPORTED;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_offload_reg_evt_wr_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                  gen_cntr_module_t *module_ptr,
                                                  topo_reg_event_t * event_cfg_payload_ptr,
                                                  bool_t             is_register)
{
   ar_result_t result                    = AR_EOK;
   uint32_t    event_id                  = event_cfg_payload_ptr->event_id;
   uint32_t    event_config_payload_size = event_cfg_payload_ptr->event_cfg.actual_data_len;

   if (OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY == event_id)
   {
      if (0 == event_config_payload_size)
      {
         result = gen_cntr_cache_set_event_prop(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);

         if (is_register && AR_SUCCEEDED(result))
         {
            if (OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY == event_id)
            {
               gen_cntr_ext_in_port_t *ext_in_port_ptr =
                  (gen_cntr_ext_in_port_t *)module_ptr->topo.gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;
               ext_in_port_ptr->cu.prop_info.prop_enabled = TRUE;
            }
         }
      }
      else
      {
         result = AR_EFAILED;
      }
   }

   return result;
}

/**
 * this module doesn't support PARAM_ID_MODULE_ENABLE
 */
ar_result_t gen_cntr_offload_handle_set_cfg_to_wr_sh_mem_ep(gen_cntr_t *        me_ptr,
                                                            gen_cntr_module_t * gen_cntr_module_ptr,
                                                            uint32_t            param_id,
                                                            int8_t *            param_data_ptr,
                                                            uint32_t            param_size,
                                                            spf_cfg_data_type_t cfg_type)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gen_cntr_module_ptr;

   switch (param_id)
   {
      case PARAM_ID_WR_EP_CLIENT_CFG:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " processing WR_EP client configuration cmd");

         gen_cntr_ext_in_port_t *ext_in_port_ptr =
            (gen_cntr_ext_in_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

         VERIFY(result, param_size >= sizeof(param_id_wr_ep_client_cfg_t));

         param_id_wr_ep_client_cfg_t *wr_ep_client_cfg_ptr = (param_id_wr_ep_client_cfg_t *)(param_data_ptr);

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      "PARAM_ID_WR_EP_CLIENT_CFG: client ID = %lu (0/1 : HLOS/OLC), port_id = %lu",
                      wr_ep_client_cfg_ptr->client_id,
                      wr_ep_client_cfg_ptr->gpr_port_id);

         ext_in_port_ptr->client_config.cid.client_id                        = wr_ep_client_cfg_ptr->client_id;
         ext_in_port_ptr->client_config.cid.gpr_port_id                      = wr_ep_client_cfg_ptr->gpr_port_id;
         ext_in_port_ptr->cu.icb_info.flags.is_default_single_buffering_mode = TRUE;
         me_ptr->cu.offload_info_ptr->is_satellite_ep_container              = TRUE;
         ext_in_port_ptr->vtbl_ptr                                           = &gpr_olc_client_ext_in_vtable;

         for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->cu.gu_ptr->ext_out_port_list_ptr;
              (NULL != ext_out_port_list_ptr);
              LIST_ADVANCE(ext_out_port_list_ptr))
         {
            gu_ext_out_port_t *gu_ext_out_port_ptr = (gu_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
            cu_ext_out_port_t *ext_out_port_ptr =
               (cu_ext_out_port_t *)(((uint8_t *)gu_ext_out_port_ptr + me_ptr->cu.ext_in_port_cu_offset));

            ext_out_port_ptr->icb_info.flags.is_default_single_buffering_mode = TRUE;
         }

         break;
      }
      case PARAM_ID_PEER_PORT_PROPERTY_UPDATE:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Processing upstream state config update from OLC");

         // has only one port.
         gen_cntr_ext_in_port_t *ext_in_port_ptr =
            (gen_cntr_ext_in_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

         TRY(result,
             gen_cntr_offload_process_peer_port_property_param(me_ptr,
                                                               &ext_in_port_ptr->gu.this_handle,
                                                               param_data_ptr,
                                                               param_size));

         break;
      }
      case PARAM_ID_UPSTREAM_STOPPED:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Processing upstream stopped config update from OLC");

         // has only one port.
         gen_cntr_ext_in_port_t *ext_in_port_ptr =
            (gen_cntr_ext_in_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

         ext_in_port_ptr->cu.connected_port_state = TOPO_PORT_STATE_STOPPED;
         gen_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */);

         if (AR_DID_FAIL(result))
         {
            CU_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:UPSTREAM_STOP_CMD: Handling upstream stop message failed for(0x%lX, 0x%lx)",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
         }
         else
         {
            CU_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "CMD:UPSTREAM_STOP_CMD: (0x%lX, 0x%lx) handling upstream stop done",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
         }

         break;
      }
      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Unsupported param-id 0x%lx", param_id);
         result = AR_EUNSUPPORTED;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_offload_pack_write_data(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (CLIENT_ID_OLC == ext_out_port_ptr->client_config.cid.client_id)
   {
      // pack the buffer : convert the de-interleaved unpacked data to packed
      topo_media_fmt_t *med_fmt_ptr = &ext_out_port_ptr->cu.media_fmt;
      topo_media_fmt_t *module_med_fmt_ptr =
         ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.media_fmt_ptr;

      // if module output unpacked, container needs to convert to packed.
      if (SPF_IS_PCM_DATA_FORMAT(module_med_fmt_ptr->data_format) &&
          (TU_IS_ANY_DEINTERLEAVED_UNPACKED(module_med_fmt_ptr->pcm.interleaving)))
      {
         // if actual=max len, then already packed.
         if (ext_out_port_ptr->buf.actual_data_len < ext_out_port_ptr->buf.max_data_len)
         {
            uint32_t num_ch           = med_fmt_ptr->pcm.num_channels;
            uint32_t bytes_per_ch     = capi_cmn_divide(ext_out_port_ptr->buf.actual_data_len, num_ch);
            uint32_t max_bytes_per_ch = capi_cmn_divide(ext_out_port_ptr->buf.max_data_len, num_ch);

            // ch=0 is at the right place already
            for (uint32_t ch = 1; ch < num_ch; ch++)
            {
               memsmove(ext_out_port_ptr->buf.data_ptr + ch * bytes_per_ch,
                        bytes_per_ch,
                        ext_out_port_ptr->buf.data_ptr + ch * max_bytes_per_ch,
                        bytes_per_ch);
            }
         }
      }
   }

   return result;
}

static ar_result_t gen_cntr_copy_olc_client_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                             gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                             uint32_t *              bytes_copied_per_buf_ptr)
{
   ar_result_t            result      = AR_EOK;

   result |= gen_cntr_copy_peer_or_olc_client_input(me_ptr, ext_in_port_ptr, bytes_copied_per_buf_ptr);
   // input logging is done as soon as buf is popped because otherwise deinterleaved data cannot be handled.

   // if we have copied all the data from 'data' (not EOS) buffer, release it
   // PCM decoder use case doesn't come here.

   // if we have copied all the data from 'data' (not EOS) buffer, release it
   if (gen_cntr_is_input_a_gpr_client_data_buffer(me_ptr, ext_in_port_ptr) &&
       (0 == ext_in_port_ptr->buf.actual_data_len))
   {
      result = gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
   }

   return result;
};

static ar_result_t gen_cntr_recreate_out_buf_olc_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        uint32_t                 req_out_buf_size,
                                                        uint32_t                 num_data_msg,
                                                        uint32_t                 num_bufs_per_data_msg_v2)
{
   ext_out_port_ptr->cu.buf_max_size = req_out_buf_size;
   gen_cntr_offload_send_opfs_event_to_rd_client(me_ptr, ext_out_port_ptr);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Not creating output buffer for DSP client");

   return AR_EOK;
}

ar_result_t gen_cntr_offload_parse_inp_pcm_media_fmt_from_gpr_client(gen_cntr_t *            me_ptr,
                                                                     gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                     media_format_t *        media_fmt_ptr,
                                                                     topo_media_fmt_t *      local_media_fmt_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (MEDIA_FMT_ID_PCM_EXTN == media_fmt_ptr->fmt_id)
   {
      payload_media_fmt_pcm_t *pcm_fmt_extn_ptr;

      VERIFY(result, media_fmt_ptr->payload_size >= (sizeof(payload_media_fmt_pcm_t)));

      pcm_fmt_extn_ptr            = (payload_media_fmt_pcm_t *)(media_fmt_ptr + 1);
      local_media_fmt_ptr->fmt_id = MEDIA_FMT_ID_PCM;
      local_media_fmt_ptr->pcm.interleaving =
         gen_topo_convert_public_interleaving_to_gen_topo_interleaving(pcm_fmt_extn_ptr->interleaved);

      switch (media_fmt_ptr->data_format)
      {
         case DATA_FORMAT_FIXED_POINT:
         {
            local_media_fmt_ptr->data_format = SPF_FIXED_POINT;
            TRY(result, gen_topo_validate_client_pcm_media_format((payload_media_fmt_pcm_t *)pcm_fmt_extn_ptr));

            local_media_fmt_ptr->pcm.q_factor = pcm_fmt_extn_ptr->q_factor;
            break;
         }
         case DATA_FORMAT_FLOATING_POINT:
         {
            local_media_fmt_ptr->data_format = SPF_FLOATING_POINT;
            TRY(result, gen_topo_validate_client_pcm_float_media_format((payload_media_fmt_pcm_t *)pcm_fmt_extn_ptr));

            local_media_fmt_ptr->pcm.q_factor = INVALID_VALUE;
            break;
         }
         default:
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Unsupported data format %d",
                         media_fmt_ptr->data_format);
            result = AR_EUNSUPPORTED;
			return result;
         }
      }

      local_media_fmt_ptr->pcm.bits_per_sample = pcm_fmt_extn_ptr->bits_per_sample;
      local_media_fmt_ptr->pcm.bit_width       = pcm_fmt_extn_ptr->bit_width;
      local_media_fmt_ptr->pcm.endianness =
         gen_topo_convert_public_endianness_to_gen_topo_endianness(pcm_fmt_extn_ptr->endianness);
      VERIFY(result, (TOPO_UNKONWN_ENDIAN != local_media_fmt_ptr->pcm.endianness));
      local_media_fmt_ptr->pcm.sample_rate  = pcm_fmt_extn_ptr->sample_rate;
      local_media_fmt_ptr->pcm.num_channels = pcm_fmt_extn_ptr->num_channels;

      uint8_t *channel_mapping = (uint8_t *)(pcm_fmt_extn_ptr + 1);

      for (uint32_t ch = 0; ch < local_media_fmt_ptr->pcm.num_channels; ch++)
      {
         local_media_fmt_ptr->pcm.chan_map[ch] = channel_mapping[ch];
      }
   }
   else
   {
      return AR_EUNSUPPORTED;
   }
   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_send_cmd_path_media_fmt_to_olc_client(gen_cntr_t *             me_ptr,
                                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   TRY(result,
       gen_cntr_send_media_fmt_to_gpr_client(me_ptr,
                                             ext_out_port_ptr,
                                             OFFLOAD_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT,
                                             FALSE));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_write_data_for_olc_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id,
                      "gen_cntr_write_data_for_gpr_client output[%lu] bytes %lu, frames %lu, out_media_fmt_changed = "
                      "%u",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->buf.actual_data_len,
                      ext_out_port_ptr->num_frames_in_buf,
                      ext_out_port_ptr->flags.out_media_fmt_changed);
#endif
   if (TRUE == ext_out_port_ptr->flags.out_media_fmt_changed)
   {
      result = gen_cntr_send_media_fmt_to_gpr_client(me_ptr,
                                                     ext_out_port_ptr,
                                                     DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT,
                                                     FALSE);
   }

   if (CLIENT_ID_OLC == ext_out_port_ptr->client_config.cid.client_id)
   {
      // For now dont have to send the release buf ptr since it is needed only for c2 use cases
      gen_cntr_write_metadata_in_olc_rd_client_buffer(me_ptr, ext_out_port_ptr, 0, NULL /*release_out_buf_ptr*/);
      result = gen_cntr_offload_pack_write_data(me_ptr, ext_out_port_ptr);
   }

   result = gen_cntr_flush_cache_and_release_out_buf(me_ptr, ext_out_port_ptr);

   gen_cntr_propagate_metadata_gpr_client(me_ptr, ext_out_port_ptr);

   // if there an internal EOS, we need to propagate the state from the satellite
   // graph to the OLC in master using an event
   if ((CLIENT_ID_OLC == ext_out_port_ptr->client_config.cid.client_id) &&
       (ext_out_port_ptr->client_config.pending_internal_eos_event))
   {
      (void)gen_cntr_offload_propagate_internal_eos_port_property_cfg(me_ptr,
                                                                      ext_out_port_ptr); // ignoring error for now
   }

   return result;
}
