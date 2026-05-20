/**
 * \file spdm_port_event_handler.c
 * \brief
 *     This file contains the event handling code for the satellite Graph handling
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spdm_i.h"
#include "media_fmt_extn_api.h"
#include "offload_sp_api.h"
#include "offload_path_delay_api.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

ar_result_t spgm_handle_event_opfs(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id       = 0;
   uint32_t payload_size = 0;
   uint32_t port_index   = 0;

   shmem_ep_frame_size_t *opfs_event_ptr = NULL;
   log_id                                = spgm_ptr->sgm_id.log_id;

   OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "Process OPFS event, ignore port index");

   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
   VERIFY(result, (0 != payload_size));
   opfs_event_ptr = (shmem_ep_frame_size_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
   VERIFY(result, (NULL != opfs_event_ptr));

   if (RD_SHARED_MEM_EP == opfs_event_ptr->ep_module_type)
   {
      TRY(result,
          sgm_get_data_port_index_given_rw_ep_miid(spgm_ptr, IPC_READ_DATA, opfs_event_ptr->ep_miid, &port_index));

      OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "OPFS event, output buffer size %lu ", opfs_event_ptr->buf_size_in_bytes);

      read_data_port_obj_t *rd_port_obj_ptr = spgm_ptr->process_info.rdp_obj_ptr[port_index];
      VERIFY(result, (NULL != rd_port_obj_ptr));

      rd_port_obj_ptr->port_info.ctrl_cfg.sat_rd_ep_opfs_bytes = opfs_event_ptr->buf_size_in_bytes;

      // without sending the read buffers, the output media format event is not coming.
      // This needs some more exploration from GEN_CNTR aspect. // point to discuss in review
      TRY(result,
          sgm_recreate_output_buffers(spgm_ptr, rd_port_obj_ptr->port_info.ctrl_cfg.sat_rd_ep_opfs_bytes, port_index));
      TRY(result, sgm_send_all_read_buffers(spgm_ptr, port_index));
   }
   else if (WR_SHARED_MEM_EP == opfs_event_ptr->ep_module_type)
   {
      TRY(result,
          sgm_get_data_port_index_given_rw_ep_miid(spgm_ptr, IPC_WRITE_DATA, opfs_event_ptr->ep_miid, &port_index));

      OLC_SDM_MSG(OLC_SDM_ID, DBG_HIGH_PRIO, "OPFS event, input buffer size %lu ", opfs_event_ptr->buf_size_in_bytes);

      write_data_port_obj_t *wr_port_obj_ptr = spgm_ptr->process_info.wdp_obj_ptr[port_index];
      VERIFY(result, (NULL != wr_port_obj_ptr));
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

ar_result_t spgm_handle_event_clone_md(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr)
{

   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   log_id       = 0;
   uint32_t                   payload_size = 0;
   metadata_tracking_event_t *md_te_ptr    = NULL;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "MD_DBG: process MD clone event");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);

   if (sizeof(metadata_tracking_event_t) <= payload_size)
   {
      md_te_ptr                                  = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
      spf_list_node_t *       curr_cont_node_ptr = (spf_list_node_t *)((uint64_t)md_te_ptr->token_lsw);
      sdm_tracking_md_node_t *md_node_ref_ptr    = (sdm_tracking_md_node_t *)(curr_cont_node_ptr->obj_ptr);

      md_node_ref_ptr->num_ref_count++;
      if (md_node_ref_ptr->num_ref_count > md_node_ref_ptr->max_ref_count)
      {
         md_node_ref_ptr->max_ref_count = md_node_ref_ptr->num_ref_count;
      }
   }
   else
   {
      THROW(result, AR_EBADPARAM);
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

static bool_t spdm_check_if_node_in_list(spgm_info_t *     spgm_ptr,
                                         spf_list_node_t * node_ptr,
                                         spf_list_node_t **tr_md_list_ptr)
{

   spf_list_node_t *curr_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = *tr_md_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      if (curr_node_ptr == node_ptr)
      {
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

ar_result_t spgm_handle_tracking_md_event(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                   log_id       = 0;
   uint32_t                   payload_size = 0;
   metadata_tracking_event_t *md_te_ptr    = NULL;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "MD_DBG: process tracking metadata event");

   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != packet_ptr));
   payload_size = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);

   if (packet_ptr->src_domain_id != packet_ptr->dst_domain_id)
   {
      if (sizeof(metadata_tracking_event_t) <= payload_size)
      {
         md_te_ptr = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);

         spf_list_node_t *       curr_cont_node_ptr = (spf_list_node_t *)((uint64_t)md_te_ptr->token_lsw);
         sdm_tracking_md_node_t *md_node_ref_ptr    = (sdm_tracking_md_node_t *)(curr_cont_node_ptr->obj_ptr);

         bool_t is_node_in_list =
            spdm_check_if_node_in_list(spgm_ptr,
                                       curr_cont_node_ptr,
                                       (spf_list_node_t **)&spgm_ptr->process_info.tr_md.md_list_ptr);

         if (FALSE == is_node_in_list)
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_HIGH_PRIO,
                        "MD_DBG: tr_md_event, node not in list. Not expected in general, "
                        "unless flush happended on the read queue before.");
            return result;
         }

         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_HIGH_PRIO,
                     "MD_DBG: tr_md_event details MD_ID 0x%lX, source_miid 0x%lx, miid 0x%lx",
                     md_te_ptr->metadata_id,
                     md_te_ptr->source_module_instance,
                     md_te_ptr->module_instance_id);

         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_HIGH_PRIO,
                     "MD_DBG: tr_md_event details status 0x%lx, token (lsw, msw) 0x%lx,0x%lx, flags 0x%lx",
                     md_te_ptr->status,
                     md_te_ptr->token_lsw,
                     md_te_ptr->token_msw,
                     md_te_ptr->flags);

         if ((md_node_ref_ptr) && (md_node_ref_ptr->md_ptr))
         {

            bool_t free_md_instance = FALSE;
            bool_t is_dropped       = TRUE;

            if (1 == md_te_ptr->status)
            {
               free_md_instance = TRUE;
            }
            else
            {
               uint32_t    rd_port_index = 0;
               ar_result_t temp_result   = AR_EOK;

               temp_result = sgm_get_data_port_index_given_rw_ep_miid(spgm_ptr,
                                                                      IPC_READ_DATA,
                                                                      md_te_ptr->module_instance_id,
                                                                      &rd_port_index);
               if (temp_result)
               {
                  free_md_instance = TRUE;
                  is_dropped       = FALSE;
               }
            }

            if (free_md_instance) // indicates the the metadata has been been dropped or consumed by sink module
            {
               OLC_SGM_MSG(OLC_SGM_ID,
                           DBG_HIGH_PRIO,
                           "MD_DBG: process tr_md_event,  dropping MD,  ref_count %lu, max_ref_count %lu",
                           md_node_ref_ptr->num_ref_count,
                           md_node_ref_ptr->max_ref_count);

               module_cmn_md_t *     ref_md_ptr = md_node_ref_ptr->md_ptr->obj_ptr;
               module_cmn_md_list_t *node_ptr   = (module_cmn_md_list_t *)(md_node_ref_ptr->md_ptr);
               bool_t                pool_used  = FALSE;
               if (ref_md_ptr)
               {
                  // incrementing ref count just so that when tracking event function decrements
                  // with spf_ref_counter_remove_ref() count is non zero
                  if ((1 < md_node_ref_ptr->num_ref_count) && (1 < md_node_ref_ptr->max_ref_count))
                  {
                     spf_ref_counter_add_ref((void *)ref_md_ptr->tracking_ptr);
                  }
                  gen_topo_raise_tracking_event(spgm_ptr->cu_ptr->gu_ptr->log_id,
                                                md_te_ptr->source_module_instance,
                                                node_ptr,
                                                !is_dropped,
                                                NULL,
                                                FALSE);

                  md_node_ref_ptr->num_ref_count--;
                  if ((0 == md_node_ref_ptr->num_ref_count) && (1 == md_node_ref_ptr->max_ref_count))
                  {
                     if (ref_md_ptr)
                     {
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
                     OLC_SGM_MSG(OLC_SGM_ID,
                                 DBG_HIGH_PRIO,
                                 "MD_DBG: process tr_md_event,  dropping MD and delete MD from internal list as ref is "
                                 "zero, ref_count %lu, max_ref_count %lu",
                                 md_node_ref_ptr->num_ref_count,
                                 md_node_ref_ptr->max_ref_count);

                     if (md_node_ref_ptr->md_ptr)
                     {
                        pool_used = spf_lpi_pool_is_addr_from_md_pool(md_node_ref_ptr->md_ptr->obj_ptr);
                        gen_topo_check_free_md_ptr((void **)&(md_node_ref_ptr->md_ptr->obj_ptr), pool_used);
                     }
                     spf_list_delete_node_update_head((spf_list_node_t **)&node_ptr,
                                                      (spf_list_node_t **)&md_node_ref_ptr->md_ptr,
                                                      TRUE);

                     spf_list_delete_node_update_head((spf_list_node_t **)&curr_cont_node_ptr,
                                                      (spf_list_node_t **)&spgm_ptr->process_info.tr_md.md_list_ptr,
                                                      FALSE);

                     posal_memory_free(md_node_ref_ptr);
                  }
               }
            }
         }
      }
      else
      {
         THROW(result, AR_EBADPARAM);
      }
   }
   else
   {
      // cases where the event comes within the same SPF process domain.
      if (sizeof(metadata_tracking_event_t) <= payload_size)
      {
         md_te_ptr = (metadata_tracking_event_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);

         uint32_t is_last_instance = tu_get_bits(md_te_ptr->flags,
                                                 MD_TRACKING_FLAGS_BIT_MASK_LAST_INSTANCE,
                                                 MD_TRACKING_FLAGS_IS_LAST_INSTANCE_FLAG);

         uint32_t    rd_port_index = 0;
         ar_result_t temp_result   = AR_EOK;

         temp_result = sgm_get_data_port_index_given_rw_client_miid(spgm_ptr,
                                                                    IPC_READ_DATA,
                                                                    md_te_ptr->source_module_instance,
                                                                    &rd_port_index);


         if (AR_EOK == temp_result)
         {

            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_HIGH_PRIO,
                        "MD_DBG: process MD 0x%lX render event for RD client module 0x%lx with port index %ld, "
                        "md_sink_module 0x%lX token(msw,lsw) 0x%lX,0x%lX ",
                        md_te_ptr->metadata_id,
                        md_te_ptr->source_module_instance,
                        rd_port_index,
                        md_te_ptr->module_instance_id,
                        md_te_ptr->token_msw,
                        md_te_ptr->token_lsw);

            spdm_set_rd_ep_md_rendered_config(spgm_ptr, rd_port_index, md_te_ptr, is_last_instance);
         }
         else
         {
        	// if the rd_client is closed by the time the MD is propagated and rendered, it would also imply the
        	// the RD_EP on the satellite is closed as well. (RD_CLIENT & RD_EP should be part of same sub graph)
        	// There is no need to send this config to render, as the md node would be free during the external port
        	// close for RD_EP module in the satellite
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_HIGH_PRIO,
                        "MD_DBG: process MD 0x%lX render event for RD client module 0x%lx failed to get the port "
                        "index, possible when the module is closed, md_sink_module 0x%lX token(msw,lsw) 0x%lX,0x%lX ",
                        md_te_ptr->metadata_id,
                        md_te_ptr->source_module_instance,
                        md_te_ptr->module_instance_id,
                        md_te_ptr->token_msw,
                        md_te_ptr->token_lsw);
         }
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}
