/**
 * \file spdm_meta_data_handler.c
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
#include "offload_metatdata_api.h"
/* =======================================================================
Static Function Definitions
========================================================================== */
#define MAGIC_MD_TRACKING_KEY 0xBABADEAD
ar_result_t spdm_read_meta_data(spgm_info_t *          spgm_ptr,
                                data_buf_pool_node_t * db_node_ptr,
                                module_cmn_md_list_t **md_list_pptr,
                                uint32_t               rd_client_module_iid,
                                uint32_t               port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   uint32_t log_id                    = 0;
   uint32_t md_rd_offset              = 0;
   uint32_t md_payload_size           = 0;
   uint32_t md_header_extn_size       = 0;
   uint32_t is_md_header_extn_enabled = 0;

   module_cmn_md_flags_t flags;

   metadata_header_t *md_data_header_ptr = NULL;
   gen_topo_module_t *module_ptr         = NULL;
   module_cmn_md_t *  new_md_ptr         = NULL;
   module_cmn_md_t *  ref_md_ptr         = NULL;
   uint8_t *          rd_md_ptr          = NULL;
   uint8_t *           temp_ptr           = NULL;

   intf_extn_param_id_metadata_handler_t handler;
   capi_heap_id_t                        heap_info;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != md_list_pptr));

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "MD_DBG: read metadata, process start for rd client miid 0x%lx port index %lu",
               rd_client_module_iid,
               port_index);
#endif

   module_ptr = (gen_topo_module_t *)gu_find_module(spgm_ptr->cu_ptr->gu_ptr, rd_client_module_iid);
   gen_topo_populate_metadata_extn_vtable(module_ptr, &handler);
   heap_info.heap_id = (uint32_t)spgm_ptr->cu_ptr->heap_id;

   md_payload_size = db_node_ptr->data_done_header.read_data_done.md_size;
   rd_md_ptr       = (uint8_t *)db_node_ptr->ipc_data_buf.shm_mem_ptr;
   rd_md_ptr       = rd_md_ptr + (ALIGN_128_BYTES(db_node_ptr->data_buf_size)) + 2 * GAURD_PROTECTION_BYTES;

   if (0 < md_payload_size)
   {
      while ((md_rd_offset + sizeof(metadata_header_t)) <= md_payload_size)
      {
         md_data_header_ptr = (metadata_header_t *)(rd_md_ptr + md_rd_offset);

         if ((md_rd_offset + sizeof(metadata_header_t) + md_data_header_ptr->payload_size) > md_payload_size)
         {
            OLC_SDM_MSG(OLC_SDM_ID,
                        DBG_ERROR_PRIO,
                        "MD_DBG: read metadata, failed to process MD payload for RD client miid 0x%lx port index %lu"
                        "offset %lu md_payload_size %lu rd_md_payload_size %lu",
                        rd_client_module_iid,
                        port_index,
                        md_rd_offset,
                        md_data_header_ptr->payload_size,
                        md_payload_size);
            result = AR_EBADPARAM;
            break;
         }

         flags.word = 0;
         gen_topo_convert_client_md_flag_to_int_md_flags(md_data_header_ptr->flags, &flags);

         if (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE == flags.tracking_mode)
         {
            // create metadata with tracking would create the node and add it to the list
            TRY(result,
                handler.metadata_create_with_tracking(handler.context_ptr,
                                                      md_list_pptr,
                                                      md_data_header_ptr->payload_size,
                                                      heap_info,
                                                      md_data_header_ptr->metadata_id,
                                                      flags,
                                                      NULL,
                                                      &new_md_ptr));

            new_md_ptr->metadata_id = md_data_header_ptr->metadata_id;
            new_md_ptr->offset      = md_data_header_ptr->offset;
            new_md_ptr->actual_size = memscpy(new_md_ptr->metadata_buf,
                                              new_md_ptr->max_size,
                                              (void *)(md_data_header_ptr + 1),
                                              md_data_header_ptr->payload_size);
         }
         else
         {
            is_md_header_extn_enabled = tu_get_bits(md_data_header_ptr->flags,
                                                    MD_HEADER_FLAGS_BIT_MASK_NEEDS_HEADER_EXTN_PRESENT,
                                                    MD_HEADER_FLAGS_SHIFT_HEADER_EXTN_PRESENT);

            uint32_t is_md_originated_in_satellite = 0;
            if (is_md_header_extn_enabled)
            {
               temp_ptr                                   = (uint8_t *)(md_data_header_ptr + 1);
               temp_ptr                                   = temp_ptr + ALIGN_4_BYTES(md_data_header_ptr->payload_size);
               metadata_header_extn_t *md_header_extn_ptr = (metadata_header_extn_t *)(temp_ptr);
               md_header_extn_size = ALIGN_4_BYTES(sizeof(metadata_header_extn_t) + md_header_extn_ptr->payload_size);
               if (PARAM_ID_MD_EXTN_MD_ORIGIN_CFG == md_header_extn_ptr->metadata_extn_param_id)
               {
                  param_id_md_extn_md_origin_cfg_t *md_origin_cfg_ptr =
                     (param_id_md_extn_md_origin_cfg_t *)(md_header_extn_ptr + 1);
                  is_md_originated_in_satellite = md_origin_cfg_ptr->is_md_originated_in_src_domain;
               }
            }

            if (0 == is_md_originated_in_satellite) // implies MD in propagated by OLC in Master SPF and propagated back
                                                    // to OLC
            {
#if defined(__x86_64__) || defined(__LP64__) || defined(_WIN64)
               // don't check against encrypted value for 64bit
               if (md_data_header_ptr->token_lsw)
               {
                  spf_list_node_t *curr_cont_node_ptr = (spf_list_node_t *)(((uint64_t)md_data_header_ptr->token_msw << 32) | md_data_header_ptr->token_lsw);
#else
               if ((md_data_header_ptr->token_lsw)
                   && (md_data_header_ptr->token_msw == (md_data_header_ptr->token_lsw ^ MAGIC_MD_TRACKING_KEY)))
               {
                  spf_list_node_t *curr_cont_node_ptr = (spf_list_node_t *)(md_data_header_ptr->token_lsw);
#endif
                  sdm_tracking_md_node_t *md_node_ref_ptr    = (sdm_tracking_md_node_t *)(curr_cont_node_ptr->obj_ptr);
                  module_cmn_md_list_t *  node_ptr           = (module_cmn_md_list_t *)(md_node_ref_ptr->md_ptr);
                  ref_md_ptr                                 = md_node_ref_ptr->md_ptr->obj_ptr;

                  if (1 == md_node_ref_ptr->num_ref_count)
                  {
                     ref_md_ptr->metadata_id        = md_data_header_ptr->metadata_id;
                     ref_md_ptr->metadata_flag.word = flags.word;
                     ref_md_ptr->offset             = md_data_header_ptr->offset;

                     ref_md_ptr->actual_size = memscpy(ref_md_ptr->metadata_buf,
                                                       ref_md_ptr->max_size,
                                                       (void *)(md_data_header_ptr + 1),
                                                       md_data_header_ptr->payload_size);

                     spf_list_move_node_to_another_list((spf_list_node_t **)md_list_pptr,
                                                        (spf_list_node_t *)node_ptr,
                                                        (spf_list_node_t **)&md_node_ref_ptr->md_ptr);

                     spf_list_delete_node_update_head((spf_list_node_t **)&curr_cont_node_ptr,
                                                      (spf_list_node_t **)&spgm_ptr->process_info.tr_md.md_list_ptr,
                                                      FALSE);
                     md_node_ref_ptr->num_ref_count = 0;
                     posal_memory_free(md_node_ref_ptr);
                  }
                  else if (1 < md_node_ref_ptr->num_ref_count)
                  {
                     module_cmn_md_list_t *new_md_list_ptr = NULL;
                     TRY(result, handler.metadata_clone(handler.context_ptr, ref_md_ptr, md_list_pptr, heap_info));
                     result = spf_list_get_tail_node((spf_list_node_t *)(*md_list_pptr),
                                                     (spf_list_node_t **)&new_md_list_ptr);

                     if (new_md_list_ptr)
                     {
                        new_md_ptr = new_md_list_ptr->obj_ptr;

                        new_md_ptr->metadata_id        = md_data_header_ptr->metadata_id;
                        new_md_ptr->metadata_flag.word = flags.word;
                        new_md_ptr->max_size           = ref_md_ptr->max_size;
                        new_md_ptr->offset             = md_data_header_ptr->offset;

                        new_md_ptr->actual_size = memscpy(new_md_ptr->metadata_buf,
                                                          new_md_ptr->max_size,
                                                          (void *)(md_data_header_ptr + 1),
                                                          md_data_header_ptr->payload_size);
                     }
                     md_node_ref_ptr->num_ref_count--;
                  }
                  else
                  {
                     result = AR_EUNEXPECTED;
                  }
               }
               else
               {
                  result = AR_EUNEXPECTED;
               }
            }
            else if (1 == is_md_originated_in_satellite) // implies MD in originated in Satellite SPF and propagated
                                                         // back to OLC
            {
               // indicates that the Metadata is created in Satellite with tracking.
               // MD can be created either from HLOS client command or internally in SPF satellite by any module
               capi_heap_id_t           heap_info;
               module_cmn_md_tracking_t tracking_info;

               heap_info.heap_id = (uint32_t)spgm_ptr->cu_ptr->heap_id;

               tracking_info.tracking_payload.flags.word    = 0; // set the flags to zero
               tracking_info.tracking_payload.src_domain_id = spgm_ptr->sgm_id.master_pd;
               tracking_info.tracking_payload.dst_domain_id = spgm_ptr->sgm_id.master_pd;
               tracking_info.tracking_payload.src_port      = rd_client_module_iid;
               tracking_info.tracking_payload.dest_port     = spgm_ptr->sgm_id.cont_id;
               tracking_info.heap_info.heap_id              = heap_info.heap_id;

               tracking_info.tracking_payload.token_lsw = md_data_header_ptr->token_lsw;
               tracking_info.tracking_payload.token_msw = md_data_header_ptr->token_msw;

               if (MODULE_CMN_MD_ID_EOS != md_data_header_ptr->metadata_id)
               {
                  tracking_info.tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_GENERIC_EVENT;
                  // create metadata with tracking would create the node and add it to the list
                  result = handler.metadata_create_with_tracking(handler.context_ptr,
                                                                 md_list_pptr,
                                                                 md_data_header_ptr->payload_size,
                                                                 heap_info,
                                                                 md_data_header_ptr->metadata_id,
                                                                 flags,
                                                                 &tracking_info,
                                                                 &new_md_ptr);

                  new_md_ptr->offset = md_data_header_ptr->offset;
               }
               else
               {
                  module_cmn_md_eos_ext_t *eos_metadata_ptr =
                     (module_cmn_md_eos_ext_t *)(((uint8_t *)(md_data_header_ptr + 1) + md_header_extn_size));

                  tracking_info.tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_GENERIC_EVENT;

                  uint32_t         INPUT_PORT_ID_NONE = 0;
                  topo_media_fmt_t mf                 = { 0 };

                  read_data_port_obj_t *rd_ptr                         = spgm_ptr->process_info.rdp_obj_ptr[port_index];
                  uint32_t              ext_out_port_channel_bit_index = 0;

                  ext_out_port_channel_bit_index =
                     cu_get_bit_index_from_mask(rd_ptr->port_info.ctrl_cfg.cnt_ext_port_bit_mask);

                  TRY(result, olc_get_ext_out_media_fmt(spgm_ptr->cu_ptr, ext_out_port_channel_bit_index, &mf));

                  uint32_t offset = md_data_header_ptr->offset;
                  if (SPF_IS_PACKETIZED_OR_PCM(mf.data_format))
                  {
                     offset = topo_samples_to_bytes(offset, &mf);
                  }

                  result = gen_topo_create_eos_for_cntr(spgm_ptr->cu_ptr->topo_ptr,
                                                        NULL,
                                                        INPUT_PORT_ID_NONE,
                                                        heap_info.heap_id,
                                                        md_list_pptr,
                                                        &flags,
                                                        &tracking_info,
                                                        &eos_metadata_ptr->flags,
                                                        offset,
                                                        &mf);
               }
            }
            else
            {
               result = AR_EUNEXPECTED;
            }
         }

         md_rd_offset +=
            sizeof(metadata_header_t) + ALIGN_4_BYTES(md_data_header_ptr->payload_size) + md_header_extn_size;
         new_md_ptr          = NULL;
         md_header_extn_size = 0; // reset the value
      }
   }

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "read md, process done");
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* function to parse the incoming metadata to the container and fill in the write buffer */
ar_result_t spdm_write_meta_data(spgm_info_t *          spgm_ptr,
                                 data_buf_pool_node_t * data_buf_node_ptr,
                                 module_cmn_md_list_t **md_list_pptr,
                                 uint32_t               wr_client_module_iid,
                                 uint32_t               port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id            = 0;
   uint32_t md_avail_buf_size = 0;
   uint32_t num_md_ele        = 0;
   uint32_t md_payload_size   = 0;

   module_cmn_md_list_t *node_ptr           = NULL;
   module_cmn_md_list_t *next_ptr           = NULL;
   metadata_header_t *   md_data_header_ptr = NULL;
   gen_topo_module_t *   module_ptr         = NULL;
   uint8_t *             src_md_payload_ptr = NULL;
   uint8_t *             dst_md_payload_ptr = NULL;
   uint8_t *             temp_buf_md_ptr    = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "MD_DGB: Write metadata, process start for wr_client miid 0x%lx, port_index %lu",
               wr_client_module_iid,
               port_index);
#endif
   VERIFY(result, (NULL != md_list_pptr));
   node_ptr = *md_list_pptr;
   VERIFY(result, (NULL != node_ptr));

   VERIFY(result, (NULL != data_buf_node_ptr));
   temp_buf_md_ptr = (uint8_t *)data_buf_node_ptr->ipc_data_buf.shm_mem_ptr + data_buf_node_ptr->data_buf_size +
                     2 * GAURD_PROTECTION_BYTES;
   md_avail_buf_size = data_buf_node_ptr->meta_data_buf_size;

   module_ptr = (gen_topo_module_t *)gu_find_module(spgm_ptr->cu_ptr->gu_ptr, wr_client_module_iid);
   VERIFY(result, (NULL != module_ptr));

   data_buf_node_ptr->rw_md_data_info.num_md_elements   = 0;
   data_buf_node_ptr->rw_md_data_info.metadata_buf_size = 0;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;
      md_data_header_ptr      = (metadata_header_t *)temp_buf_md_ptr;
      if (md_avail_buf_size >= (sizeof(metadata_header_t) + md_ptr->max_size))
      {

         md_data_header_ptr->metadata_id     = md_ptr->metadata_id;
         md_data_header_ptr->offset          = md_ptr->offset;
         md_data_header_ptr->token_lsw       = 0;
         md_data_header_ptr->token_msw       = 0;
         module_cmn_md_flags_t metadata_flag = md_ptr->metadata_flag;
         metadata_flag.is_client_metadata    = MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD;
         gen_topo_convert_int_md_flags_to_client_md_flag(metadata_flag, &md_data_header_ptr->flags);

         uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;
         if (is_out_band)
         {
            src_md_payload_ptr = (uint8_t *)md_ptr->metadata_ptr;
         }
         else
         {
            src_md_payload_ptr = (uint8_t *)&(md_ptr->metadata_buf);
         }

         dst_md_payload_ptr = (uint8_t *)(md_data_header_ptr + 1);

         if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
         {
            // EOS payload is internal and only a subset of the payload is
            // propagated in MDF cases
            md_payload_size = sizeof(module_cmn_md_eos_ext_t);
         }
         else
         {
            md_payload_size = md_ptr->actual_size;
         }

         md_data_header_ptr->payload_size = memscpy(dst_md_payload_ptr,
                                                    md_avail_buf_size - sizeof(metadata_header_t),
                                                    src_md_payload_ptr,
                                                    md_payload_size);

         temp_buf_md_ptr += (sizeof(metadata_header_t) + md_data_header_ptr->payload_size);
         md_avail_buf_size -= (sizeof(metadata_header_t) + md_data_header_ptr->payload_size);
         num_md_ele++;

         OLC_SDM_MSG(OLC_SDM_ID,
                     DBG_MED_PRIO,
                     "MD_DBG : write metadata: md_id (0x%lx) size %lu offset %lu md_avail_buf_size %lu",
                     md_data_header_ptr->metadata_id,
                     md_data_header_ptr->payload_size,
                     md_data_header_ptr->offset,
                     md_avail_buf_size);

         // If the tracking mode is disable, we can safely destroy the metadata at the IPC input
         if (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE == md_ptr->metadata_flag.tracking_mode)
         {
            gen_topo_capi_metadata_destroy((void *)module_ptr,
                                           node_ptr,
                                           FALSE /*is_dropped*/,
                                           md_list_pptr,
                                           0,
                                           FALSE); // rendered
         }
         else
         {
            sdm_tracking_md_node_t *md_node_ref_ptr = NULL;
            md_node_ref_ptr = (sdm_tracking_md_node_t *)posal_memory_malloc(sizeof(sdm_tracking_md_node_t),
                                                                            (POSAL_HEAP_ID)spgm_ptr->cu_ptr->heap_id);

            if (md_node_ref_ptr)
            {
               memset((void *)md_node_ref_ptr, 0, sizeof(sdm_tracking_md_node_t));
               md_node_ref_ptr->num_ref_count = 1;
               md_node_ref_ptr->max_ref_count = 1;

               spf_list_move_node_to_another_list((spf_list_node_t **)&(md_node_ref_ptr->md_ptr),
                                                  (spf_list_node_t *)node_ptr,
                                                  (spf_list_node_t **)md_list_pptr);

               result = spf_list_insert_tail((spf_list_node_t **)&spgm_ptr->process_info.tr_md.md_list_ptr,
                                             md_node_ref_ptr,
                                             (POSAL_HEAP_ID)spgm_ptr->cu_ptr->heap_id,
                                             FALSE /* use pool */);

               spf_list_node_t *curr_cont_node_ptr = NULL;
               spf_list_get_tail_node(spgm_ptr->process_info.tr_md.md_list_ptr, &curr_cont_node_ptr);

               // token stores the address of the metadata node pointer.
               // For 32bit, the LSW will have the original value and msw will have a encrypted value
               // For 64bit, the LSW stores the lower 32bits and MSW stores the higher 32bits.
               // the token is not expected to be returned back to client
#if defined(__x86_64__) || defined(__LP64__) || defined(_WIN64)
               md_data_header_ptr->token_lsw = (uint32_t)((uint64_t)curr_cont_node_ptr & 0xFFFFFFFF);
               md_data_header_ptr->token_msw = (uint32_t)(((uint64_t)curr_cont_node_ptr >> 32) & 0xFFFFFFFF);
#else
               md_data_header_ptr->token_lsw = (uint32_t)curr_cont_node_ptr;
               md_data_header_ptr->token_msw = md_data_header_ptr->token_lsw ^ MAGIC_MD_TRACKING_KEY;
#endif
               uint32_t temp_flags =
                  (MD_HEADER_FLAGS_TRACKING_EVENT_POLICY_EACH << MD_HEADER_FLAGS_SHIFT_TRACKING_EVENT_POLICY_FLAG);
               temp_flags |= (MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME
                              << MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);
               md_data_header_ptr->flags |= temp_flags;

               temp_flags =
                  (MD_HEADER_FLAGS_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY << MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);
               temp_flags = ~temp_flags;
               md_data_header_ptr->flags &= temp_flags;
            }
            else
            {
               result = AR_ENOMEMORY;

               OLC_SDM_MSG(OLC_SDM_ID,
                           DBG_HIGH_PRIO,
                           "MD_DBG : write metadata: md_id (0x%lx) failed to cache the MD node, "
                           "disabling tracking in the propagation and destroy in OLC",
                           md_data_header_ptr->metadata_id);

               // disabling the tracking since we are not able to store the node.
               uint32_t set_tracking_disable =
                  (MODULE_CMN_MD_TRACKING_CONFIG_DISABLE << MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);

               tu_set_bits(&md_data_header_ptr->flags,
                           set_tracking_disable,
                           MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
                           MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG);

               // destroy the node immediately, since we disabled the tracking
               gen_topo_capi_metadata_destroy((void *)module_ptr,
                                              node_ptr,
                                              TRUE /*is_dropped*/,
                                              md_list_pptr,
                                              0, // sink_miid
                                              FALSE); // override control disabled
            }
         }

         node_ptr = next_ptr;
      }
      else
      {
         result = AR_EUNEXPECTED;
         break;
      }
   }

   data_buf_node_ptr->rw_md_data_info.num_md_elements   = num_md_ele;
   data_buf_node_ptr->rw_md_data_info.metadata_buf_size = data_buf_node_ptr->meta_data_buf_size - md_avail_buf_size;

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID,
               DBG_MED_PRIO,
               "write md: done, num_md_ele %lu, md_buf_size %lu",
               data_buf_node_ptr->rw_md_data_info.num_md_elements,
               data_buf_node_ptr->rw_md_data_info.metadata_buf_size);
#endif

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* function to parse the incoming metadata to the container on a specified data port
 * and determine the size of metadata to fill in the write buffer.
 */
ar_result_t spdm_get_write_meta_data_size(spgm_info_t *         spgm_ptr,
                                          uint32_t              port_index,
                                          module_cmn_md_list_t *md_list_ptr,
                                          uint32_t *            wr_md_size_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id              = 0;
   uint32_t md_element_buf_size = 0;
   uint32_t write_md_buf_size   = 0;

   module_cmn_md_list_t *node_ptr = NULL;
   module_cmn_md_list_t *next_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
   OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write md, get md size");
#endif

   VERIFY(result, (NULL != wr_md_size_ptr));
   *wr_md_size_ptr = 0;

   if (NULL == md_list_ptr)
   {
#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
      OLC_SDM_MSG(OLC_SDM_ID, DBG_MED_PRIO, "write data processing: get_md_size, md_list is null, md_size is zero");
#endif
      return AR_EOK;
   }

   node_ptr = md_list_ptr;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      // metadata element size : base structure + metadata payload max size
      md_element_buf_size = (sizeof(metadata_header_t) + md_ptr->max_size);
      write_md_buf_size += md_element_buf_size;
#ifdef SGM_ENABLE_METADATA_FLOW_LEVEL_MSG
      OLC_SDM_MSG(OLC_SDM_ID,
                  DBG_MED_PRIO,
                  "write data processing: md_ele_size is %lu, wr_md_buf_size %lu",
                  md_element_buf_size,
                  write_md_buf_size);
#endif

      node_ptr = next_ptr;
   }

   *wr_md_size_ptr = write_md_buf_size;

   if (write_md_buf_size > 0)
   {
      OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "write md: total md_size required %lu", write_md_buf_size);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}
