/**
 * \file gen_cntr_rd_sh_mem_ep.c
 * \brief
 *     This file contains functions for read shared memory end point
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "gen_cntr_i.h"
#include "apm.h"
#include "media_fmt_extn_api.h"
#include "gen_cntr_cmn_sh_mem.h"

static ar_result_t gen_cntr_reg_evt_rd_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                 gen_cntr_module_t *module_ptr,
                                                 topo_reg_event_t * event_cfg_payload_ptr,
                                                 bool_t             is_register);
static ar_result_t gen_cntr_handle_set_cfg_to_rd_sh_mem_ep(gen_cntr_t                        *me_ptr,
                                                           gen_cntr_module_t                 *gen_cntr_module_ptr,
                                                           uint32_t                           param_id,
                                                           int8_t                            *param_data_ptr,
                                                           uint32_t                           param_size,
                                                           spf_cfg_data_type_t                cfg_type,
                                                           cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);
static uint32_t gen_cntr_get_metadata_length_for_read_cmd_v2(gen_cntr_t *             me_ptr,
                                                             gen_cntr_ext_out_port_t *ext_out_port_ptr);
static ar_result_t gen_cntr_recreate_out_buf_gpr_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        uint32_t                 req_out_buf_size,
                                                        uint32_t                 num_data_msg,
                                                        uint32_t                 num_bufs_per_data_msg_v2);

static void gen_cntr_write_metadata_in_client_buffer(gen_cntr_t *             me_ptr,
                                                     gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                     uint32_t                 frame_offset_in_ext_buf,
                                                     bool_t *                 release_out_buf_ptr);

static ar_result_t gen_cntr_process_rdep_shmem_peer_client_property_config(gen_cntr_t *  me_ptr,
                                                                           spf_handle_t *handle_ptr,
                                                                           int8_t *      param_data_ptr,
                                                                           uint32_t      param_size);

ar_result_t gen_cntr_raise_ts_disc_event_from_rd_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                           gen_cntr_module_t *module_ptr,
                                                           bool_t             ts_valid,
                                                           int64_t            timestamp_disc_us);

const gen_cntr_fwk_module_vtable_t rd_sh_mem_ep_vtable = {
   .set_cfg             = gen_cntr_handle_set_cfg_to_rd_sh_mem_ep,
   .reg_evt             = gen_cntr_reg_evt_rd_sh_mem_ep,
   .raise_evt           = NULL,
   .raise_ts_disc_event = gen_cntr_raise_ts_disc_event_from_rd_sh_mem_ep,
};

const gen_cntr_ext_out_vtable_t gpr_client_ext_out_vtable = {
   .setup_bufs       = gen_cntr_output_buf_set_up_gpr_client,
   .write_data       = gen_cntr_write_data_for_gpr_client,
   .fill_frame_md    = gen_cntr_fill_frame_metadata,
   .get_filled_size  = gen_cntr_get_amount_of_data_in_gpr_client_buf,
   .flush            = gen_cntr_flush_output_data_queue_gpr_client,
   .recreate_out_buf = gen_cntr_recreate_out_buf_gpr_client,
   .prop_media_fmt   = NULL,
   .write_metadata   = gen_cntr_write_metadata_in_client_buffer,
   .setup_topo_buf   = gen_cntr_rd_sh_mem_setup_topo_buffer,
};

ar_result_t gen_cntr_create_rd_sh_mem_ep(gen_cntr_t            *me_ptr,
                                         gen_topo_module_t     *module_ptr,
                                         gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // make sure it has only one output port. GPR client can be only one as GPR commands don't have port-id
   VERIFY(result, 1 == module_ptr->gu.max_output_ports);
   // Make sure that the only out port of this module is also an external port.
   VERIFY(result, NULL != module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr);

   // there's no CAPI for SH MEM EP

   spf_handle_t *gpr_cb_handle = &module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr->this_handle;

   module_ptr->flags.inplace = TRUE;

   // The require_data_buf for RD_SHMEM_EP is set to TRUE.
   // If RD_SHMEM_EP is the only module in the container, we should process every frame data at the input
   // immediately and not accumulate the buffers at the input of the container to fill the complete input buffer.
   // This is needed if the Encoder is in the previous container and an integral number of frames needs to be rendered
   // to the client. ( MDF will have this scenario for Encoder offload use-case)
   module_ptr->flags.requires_data_buf = TRUE;

   gen_topo_input_port_t *input_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
   input_port_ptr->common.flags.port_has_threshold = FALSE;
   gen_topo_output_port_t *output_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
   output_port_ptr->common.flags.port_has_threshold = FALSE;

   TRY(result, __gpr_cmd_register(module_ptr->gu.module_instance_id, graph_init_ptr->gpr_cb_fn, gpr_cb_handle));

   gen_cntr_module_t *gen_cntr_module_ptr        = (gen_cntr_module_t *)module_ptr;
   gen_cntr_module_ptr->fwk_module_ptr->vtbl_ptr = &rd_sh_mem_ep_vtable;

   /// KPPS for RD SH MEM EP comes from ext-out port voting.

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_init_gpr_client_ext_out_port(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   ext_out_port_ptr->flags.fill_ext_buf    = TRUE;
   ext_out_port_ptr->max_frames_per_buffer = (uint32_t)(-1);
   ext_out_port_ptr->vtbl_ptr              = &gpr_client_ext_out_vtable;

   MALLOC_MEMSET(ext_out_port_ptr->buf.md_buf_ptr,
                 gen_cntr_md_buf_t,
                 sizeof(gen_cntr_md_buf_t),
                 me_ptr->cu.heap_id,
                 result);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_reg_evt_rd_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                 gen_cntr_module_t *module_ptr,
                                                 topo_reg_event_t * event_cfg_payload_ptr,
                                                 bool_t             is_register)
{
   ar_result_t result                    = AR_EOK;
   uint32_t    event_id                  = event_cfg_payload_ptr->event_id;
   uint32_t    event_config_payload_size = event_cfg_payload_ptr->event_cfg.actual_data_len;

   switch (event_id)
   {
      case DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
      case DATA_EVENT_ID_RD_SH_MEM_EP_EOS:
      case EVENT_ID_RD_SH_MEM_EP_TIMESTAMP_DISC_DETECTION:
      {
         if (0 == event_config_payload_size)
         {
            result = gen_cntr_cache_set_event_prop(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);
            if (is_register && AR_SUCCEEDED(result))
            {
               if ((DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT == event_id))
               {
                  gen_topo_output_port_t *out_port_ptr =
                     (gen_topo_output_port_t *)module_ptr->topo.gu.output_port_list_ptr->op_port_ptr;
                  gen_cntr_ext_out_port_t *ext_out_port_ptr =
                     (gen_cntr_ext_out_port_t *)out_port_ptr->gu.ext_out_port_ptr;
                  topo_media_fmt_t *ext_out_media_fmt_ptr = &ext_out_port_ptr->cu.media_fmt;
                  if (topo_is_valid_media_fmt(ext_out_media_fmt_ptr))
                  {
                     GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                  DBG_HIGH_PRIO,
                                  "When registering event, media format is valid. Trying to raise event");
                     gen_cntr_send_media_fmt_to_gpr_client(me_ptr, ext_out_port_ptr, event_id, TRUE);
                  }
               }
               else if(EVENT_ID_RD_SH_MEM_EP_TIMESTAMP_DISC_DETECTION == event_id)
               {
                   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Registered for Timestamp Disc event");
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
         if (NULL != me_ptr->cu.offload_info_ptr)
         {
            /* handle the following events for MDF
             OFFLOAD_EVENT_ID_SH_MEM_EP_OPERATING_FRAME_SIZE:
             OFFLOAD_DATA_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
             OFFLOAD_EVENT_ID_UPSTREAM_PEER_PORT_PROPERTY:
             OFFLOAD_EVENT_ID_UPSTREAM_STATE:
             OFFLOAD_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT:
             */
            result = gen_cntr_offload_reg_evt_rd_sh_mem_ep(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);
         }
         else
         {
            TOPO_MSG(me_ptr->topo.gu.log_id,
                     DBG_ERROR_PRIO,
                     " Unsupported event id 0x%lX for framework module-id 0x%lX",
                     event_id,
                     module_ptr->topo.gu.module_id);
            result = AR_EUNSUPPORTED;
         }
         break;
      }
   }

   return result;
}

/**
 * this module doesn't support PARAM_ID_MODULE_ENABLE
 */
static ar_result_t gen_cntr_handle_set_cfg_to_rd_sh_mem_ep(gen_cntr_t                        *me_ptr,
                                                           gen_cntr_module_t                 *gen_cntr_module_ptr,
                                                           uint32_t                           param_id,
                                                           int8_t                            *param_data_ptr,
                                                           uint32_t                           param_size,
                                                           spf_cfg_data_type_t                cfg_type,
                                                           cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result     = AR_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gen_cntr_module_ptr;
   switch (param_id)
   {
      case PARAM_ID_RD_SH_MEM_CFG:
      {
         if (gen_topo_is_module_sg_started(module_ptr))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Setting RD_SH_MEM_CFG is not supported in start state");
            result = AR_EUNSUPPORTED;
         }
         else
         {
            // has only one port.
            gen_cntr_ext_out_port_t *ext_out_port_ptr =
               (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
            VERIFY(result, param_size >= sizeof(param_id_rd_sh_mem_cfg_t));
            param_id_rd_sh_mem_cfg_t *cfg_ptr = (param_id_rd_sh_mem_cfg_t *)(param_data_ptr);

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_MED_PRIO,
                         " PARAM_ID_RD_SH_MEM_CFG: enable frame metadata = 0x%lx, num_frames_per_buffer %lu",
                         cfg_ptr->metadata_control_flags,
                         cfg_ptr->num_frames_per_buffer);

            uint32_t enable_frame_metadata = tu_get_bits(cfg_ptr->metadata_control_flags,
                                                         RD_EP_CFG_MD_CNTRL_FLAGS_BIT_MASK_ENABLE_ENCODER_FRAME_MD,
                                                         RD_EP_CFG_MD_CNTRL_FLAGS_SHIFT_ENABLE_ENCODER_FRAME_MD);

            uint32_t md_mf_enable = tu_get_bits(cfg_ptr->metadata_control_flags,
                                                RD_EP_CFG_MD_CNTRL_FLAGS_BIT_MASK_ENABLE_MEDIA_FORMAT_MD,
                                                RD_EP_CFG_MD_CNTRL_FLAGS_SHIFT_ENABLE_MEDIA_FORMAT_MD);

            ext_out_port_ptr->flags.frame_metadata_enable = enable_frame_metadata > 0;
            ext_out_port_ptr->flags.md_mf_enable          = md_mf_enable > 0;
            ext_out_port_ptr->max_frames_per_buffer       = cfg_ptr->num_frames_per_buffer;
            if (RD_SH_MEM_NUM_FRAMES_IN_BUF_AS_MUCH_AS_POSSIBLE == cfg_ptr->num_frames_per_buffer)
            {
               ext_out_port_ptr->flags.fill_ext_buf    = TRUE;
               ext_out_port_ptr->max_frames_per_buffer = (uint32_t)(-1);
               // for first frame let's assume we need to encode lot of frames. this value will be corrected after
               // encoding first frame.
            }
            else
            {
               ext_out_port_ptr->flags.fill_ext_buf = FALSE;
            }
         }
         break;
      }

      case PARAM_ID_SH_MEM_PEER_CLIENT_PROPERTY_CONFIG:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Processing RD_EP shmem peer client config update from RDEP Client");

         // has only one port.
         gen_cntr_ext_out_port_t *ext_out_port_ptr =
            (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;

         TRY(result,
             gen_cntr_process_rdep_shmem_peer_client_property_config(me_ptr,
                                                                     &ext_out_port_ptr->gu.this_handle,
                                                                     param_data_ptr,
                                                                     param_size));

         break;
      }

      default:
      {
         if (NULL != me_ptr->cu.offload_info_ptr)
         {
            /* handle the following set configuration for MDF
             PARAM_ID_RD_EP_CLIENT_CFG
             PARAM_ID_PEER_PORT_PROPERTY_UPDATE
             */
            result = gen_cntr_offload_handle_set_cfg_to_rd_sh_mem_ep(me_ptr,
                                                                     gen_cntr_module_ptr,
                                                                     param_id,
                                                                     param_data_ptr,
                                                                     param_size,
                                                                     cfg_type);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Unsupported param-id 0x%lx", param_id);
            result = AR_EUNSUPPORTED;
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_create_inband_md_data_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (NULL == ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr)
   {
      gpr_cmd_gpr_packet_pool_info_t gpr_packet_pool_info;
      result = __gpr_cmd_get_gpr_packet_info(&gpr_packet_pool_info);

      MALLOC_MEMSET(ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr,
                    int8_t,
                    gpr_packet_pool_info.bytes_per_min_size_packet,
                    me_ptr->cu.heap_id,
                    result);
      ext_out_port_ptr->buf.md_buf_ptr->inband_md_buf_size =
         gpr_packet_pool_info.bytes_per_min_size_packet - sizeof(data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t);
   }
   else
   {
      THROW(result, AR_EALREADY);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_output_buf_set_up_gpr_client_v2(gen_cntr_t *             me_ptr,
                                                            gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t   result     = AR_EOK;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_out_port_ptr->out_buf_gpr_client.payload_ptr);

   data_cmd_rd_sh_mem_ep_data_buffer_v2_t *read_cmd_ptr =
      GPR_PKT_GET_PAYLOAD(data_cmd_rd_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   if (!read_cmd_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "NULL payload from GPR packet!");
      return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EBADPARAM);
   }

   // if out data buffer and meta-data buffer size is zero, return it immediately
   if ((0 == read_cmd_ptr->data_buf_size) && (0 == read_cmd_ptr->md_buf_size))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Returning an output buffer with data and metadata of zero size!");
      return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EBADPARAM);
   }

   // if output buffer is not 8 byte aligned return it
   if (read_cmd_ptr->data_buf_addr_lsw & CACHE_ALIGNMENT)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Output buffer not %lu byte aligned, returning it!",
                   CACHE_ALIGNMENT + 1);
      return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EBADPARAM);
   }

   // if output md buffer is not 8 byte aligned return it
   if (read_cmd_ptr->md_buf_addr_lsw & CACHE_ALIGNMENT)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Output md buffer not %lu byte aligned, returning it!",
                   CACHE_ALIGNMENT + 1);
      ext_out_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // metadata buffer error
      return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
   }

   if (read_cmd_ptr->data_buf_size)
   {
      uint64_t virtual_addr = 0;
      if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                   read_cmd_ptr->data_mem_map_handle,
                                                                                   read_cmd_ptr->data_buf_addr_lsw,
                                                                                   read_cmd_ptr->data_buf_addr_msw,
                                                                                   read_cmd_ptr->data_buf_size,
                                                                                   TRUE, // is_ref_counted
                                                                                   &virtual_addr)))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Phy to Virt Failed(paddr,vaddr)-->(%lx%lx,%lx)\n for the data buffer address",
                      read_cmd_ptr->data_buf_addr_lsw,
                      read_cmd_ptr->data_buf_addr_msw,
                      (virtual_addr));

         return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EBADPARAM);
      }

      ext_out_port_ptr->buf.mem_map_handle = read_cmd_ptr->data_mem_map_handle;
      ext_out_port_ptr->buf.data_ptr       = (int8_t *)(virtual_addr);
      ext_out_port_ptr->buf.max_data_len   = read_cmd_ptr->data_buf_size;
   }
   else
   {
      ext_out_port_ptr->buf.mem_map_handle = 0;
      ext_out_port_ptr->buf.data_ptr       = NULL;
      ext_out_port_ptr->buf.max_data_len   = 0;
   }

   if (read_cmd_ptr->md_buf_size) // ooB , make it two level
   {
      if (read_cmd_ptr->md_mem_map_handle) // valid memory handle
      {
         uint64_t virtual_addr = 0;
         if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                      read_cmd_ptr->md_mem_map_handle,
                                                                                      read_cmd_ptr->md_buf_addr_lsw,
                                                                                      read_cmd_ptr->md_buf_addr_msw,
                                                                                      read_cmd_ptr->md_buf_size,
                                                                                      TRUE, // is_ref_counted
                                                                                      &virtual_addr)))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Phy to Virt Failed(paddr,vaddr)-->(%lx%lx,%lx)\n for the metadata buffer address",
                         read_cmd_ptr->data_buf_addr_lsw,
                         read_cmd_ptr->data_buf_addr_msw,
                         (virtual_addr));
            ext_out_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // metadata buffer error
            return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
         }

         ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle  = read_cmd_ptr->md_mem_map_handle;
         ext_out_port_ptr->buf.md_buf_ptr->data_ptr        = (int8_t *)(virtual_addr);
         ext_out_port_ptr->buf.md_buf_ptr->max_data_len    = read_cmd_ptr->md_buf_size;
         ext_out_port_ptr->buf.md_buf_ptr->actual_data_len = 0;
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "read buffer with valid size %lu and no memory handle 0x%lx",
                      read_cmd_ptr->md_buf_size,
                      read_cmd_ptr->md_mem_map_handle);
         ext_out_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // metadata buffer error
         return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
      }
   }
   else
   {
      if (NULL == ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr)
      {
         gen_cntr_create_inband_md_data_buf(me_ptr, ext_out_port_ptr);
      }
      ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle = 0;
      ext_out_port_ptr->buf.md_buf_ptr->data_ptr =
         ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr + sizeof(data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t);
      ext_out_port_ptr->buf.md_buf_ptr->max_data_len    = ext_out_port_ptr->buf.md_buf_ptr->inband_md_buf_size;
      ext_out_port_ptr->buf.md_buf_ptr->actual_data_len = 0;
      // memory to support in-band meta-data would be created dynamically
      // inband metadata buffer size will be based on minimum packet size from GPR
   }

   // if the minimum metadata size is non zero, we need to check if he new buffer has the required size.
   if (0 < ext_out_port_ptr->buf.md_buf_ptr->min_md_size_in_next_buffer)
   {
      if (ext_out_port_ptr->buf.md_buf_ptr->min_md_size_in_next_buffer <=
          ext_out_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         if (TRUE == ext_out_port_ptr->flags.out_media_fmt_changed)
         {
            result = gen_cntr_send_media_fmt_to_gpr_client(me_ptr,
                                                           ext_out_port_ptr,
                                                           DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT,
                                                           FALSE);
            return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
         }
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Output metadata buffer size is too less to write minimum metadata information"
                      " mimimum Meta data size = %d, md_buffer size available = %lu",
                      ext_out_port_ptr->buf.md_buf_ptr->min_md_size_in_next_buffer,
                      ext_out_port_ptr->buf.md_buf_ptr->max_data_len);
         ext_out_port_ptr->buf.md_buf_ptr->status = AR_ENEEDMORE; // metadata buffer error
         return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
      }
   }

   if (ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame > ext_out_port_ptr->buf.md_buf_ptr->max_data_len)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Output metadata buffer size is too less to write per frame metadata information"
                   " per frame Meta data size estimate = %d, md_buffer size available = %lu",
                   ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame,
                   ext_out_port_ptr->buf.md_buf_ptr->max_data_len);
      ext_out_port_ptr->buf.md_buf_ptr->status = AR_ENEEDMORE; // metadata buffer error
      return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
   }

   if (ext_out_port_ptr->buf.md_buf_ptr->max_data_len) // for OOB/Inband, ensure the md_buf size is sufficient
   {
      uint32_t enc_frame_md_size_required = 0;
      if (TRUE == ext_out_port_ptr->flags.fill_ext_buf)
      {
         // Leave space for one frame worth of metadata; will be corrected after encoding first frame.
         enc_frame_md_size_required = gen_cntr_get_metadata_length_for_read_cmd_v2(me_ptr, ext_out_port_ptr);
      }
      else
      {
         enc_frame_md_size_required = ext_out_port_ptr->max_frames_per_buffer *
                                      gen_cntr_get_metadata_length_for_read_cmd_v2(me_ptr, ext_out_port_ptr);
      }

      // check if encoder frame md size is less than than metadata buffer size
      uint32_t md_buf_size_available =
         ext_out_port_ptr->buf.md_buf_ptr->max_data_len - ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;
      if ((enc_frame_md_size_required > md_buf_size_available))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Output metadata buffer size is too  less to write encoder frame data:"
                      " Encoder Meta data size = %d, md_buffer size available = %lu",
                      enc_frame_md_size_required,
                      md_buf_size_available);
         ext_out_port_ptr->buf.md_buf_ptr->status = AR_ENEEDMORE; // metadata buffer error
         return gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
      }
   }

   ext_out_port_ptr->buf.md_buf_ptr->is_md_buffer_api_supported = TRUE;
   ext_out_port_ptr->num_frames_in_buf                          = 0;

   return result;
}

ar_result_t gen_cntr_output_buf_set_up_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   gpr_packet_t *packet_ptr;
   uint32_t      gpr_opcode;

   if (ext_out_port_ptr->out_buf_gpr_client.payload_ptr != NULL)
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Out buf is already present while processing output data Q. MIID = 0x%x, "
                   "port_id= 0x%x ",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
#endif
      return AR_EOK;
   }

   // Take next msg off the q
   if (AR_DID_FAIL(result = posal_queue_pop_front(ext_out_port_ptr->gu.this_handle.q_ptr,
                                                  (posal_queue_element_t *)&(ext_out_port_ptr->out_buf_gpr_client))))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "GEN_CNTR failed receive output buffers %d", result);
      ext_out_port_ptr->out_buf_gpr_client.payload_ptr = NULL;
      return result;
   }

   if (SPF_MSG_CMD_GPR != ext_out_port_ptr->out_buf_gpr_client.msg_opcode)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Received non-GPR message on output data q w/ opcode = 0x%lx",
                   ext_out_port_ptr->out_buf_gpr_client.msg_opcode);

      result = spf_msg_ack_msg(&ext_out_port_ptr->out_buf_gpr_client, AR_EUNSUPPORTED);
      ext_out_port_ptr->out_buf_gpr_client.payload_ptr = NULL;
      return result;
   }

   packet_ptr = (gpr_packet_t *)(ext_out_port_ptr->out_buf_gpr_client.payload_ptr);
   if (!packet_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "GPR packet pointer is NULL!");
      // nothing else can be done, return error
      return AR_EBADPARAM;
   }

   gpr_opcode = packet_ptr->opcode;

   switch (gpr_opcode)
   {
      case DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2:
      {
         result = gen_cntr_output_buf_set_up_gpr_client_v2(me_ptr, ext_out_port_ptr);
         break;

      } /* case DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2 */

      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Unexpected opCode for data write 0x%lx", gpr_opcode);
         // don't know what this command is, so cannot provide payload for acknowledgement
         result = cu_gpr_generate_ack(&me_ptr->cu, packet_ptr, AR_EFAILED, NULL, 0, 0);
         ext_out_port_ptr->out_buf_gpr_client.payload_ptr = NULL;
         result |= AR_EFAILED; // return error
         break;
      }

   } // end of switch(pMe->gpQMsg.opcode)
   return result;
}

/**
 * for one frame.
 */
static uint32_t gen_cntr_get_metadata_length_for_read_cmd_v2(gen_cntr_t *             me_ptr,
                                                             gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   uint32_t metadata_length = 0;

   if (!ext_out_port_ptr->out_buf_gpr_client.payload_ptr)
   {
      return metadata_length;
   }

   if (ext_out_port_ptr->flags.frame_metadata_enable)
   {
      metadata_length = ALIGN_4_BYTES(sizeof(metadata_header_t) + sizeof(module_cmn_md_encoder_per_frame_info_t));
   }
   else
   {
      metadata_length = 0;
   }

   return metadata_length;
}

static ar_result_t gen_cntr_release_gpr_client_buffer_v2(gen_cntr_t *             me_ptr,
                                                         gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                         ar_result_t              errCode)
{
   ar_result_t result       = AR_EOK;
   uint32_t    out_buf_size = ext_out_port_ptr->buf.actual_data_len;
   if (!ext_out_port_ptr->out_buf_gpr_client.payload_ptr)
   {
      return result;
   }

   gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_out_port_ptr->out_buf_gpr_client.payload_ptr);
   data_cmd_rd_sh_mem_ep_data_buffer_v2_t *read_cmd_ptr =
      (data_cmd_rd_sh_mem_ep_data_buffer_v2_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);

   data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2_t read_done_evt;

   int8_t * rd_done_payload_ptr  = NULL;
   uint32_t rd_done_payload_size = 0;

   memset(&read_done_evt, 0, sizeof(read_done_evt));

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Outgoing timestamp: %lu (0x%lx%lx). valid=%d, out_buf_size=%lu",
                (uint32_t)ext_out_port_ptr->next_out_buf_ts.value,
                (uint32_t)(ext_out_port_ptr->next_out_buf_ts.value >> 32),
                (uint32_t)ext_out_port_ptr->next_out_buf_ts.value,
                ext_out_port_ptr->next_out_buf_ts.valid,
                out_buf_size);
#endif

   if (read_cmd_ptr)
   {
      // fill up buf addr
      read_done_evt.data_buf_addr_lsw   = read_cmd_ptr->data_buf_addr_lsw;
      read_done_evt.data_buf_addr_msw   = read_cmd_ptr->data_buf_addr_msw;
      read_done_evt.data_mem_map_handle = read_cmd_ptr->data_mem_map_handle;

      // Fill up the md buff address
      read_done_evt.md_buf_addr_lsw   = read_cmd_ptr->md_buf_addr_lsw;
      read_done_evt.md_buf_addr_msw   = read_cmd_ptr->md_buf_addr_msw;
      read_done_evt.md_mem_map_handle = read_cmd_ptr->md_mem_map_handle;

      gen_topo_timestamp_t ts = ext_out_port_ptr->next_out_buf_ts;

      // Fill time stamp info and timestamp flag
      read_done_evt.timestamp_lsw = (uint32_t)ts.value;
      read_done_evt.timestamp_msw = (uint32_t)(ts.value >> 32);

      read_done_evt.flags = 0;

      // time stamp is valid only if we have any encoded data
      cu_set_bits(&read_done_evt.flags,
                  ts.valid,
                  RD_SH_MEM_EP_BIT_MASK_TIMESTAMP_VALID_FLAG,
                  RD_SH_MEM_EP_SHIFT_TIMESTAMP_VALID_FLAG);

      read_done_evt.data_size  = out_buf_size;
      read_done_evt.num_frames = ext_out_port_ptr->num_frames_in_buf;
      read_done_evt.md_size    = ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;

      if (!read_done_evt.num_frames || !read_done_evt.data_size)
      {
         cu_set_bits(&read_done_evt.flags,
                     0,
                     RD_SH_MEM_EP_BIT_MASK_TIMESTAMP_VALID_FLAG,
                     RD_SH_MEM_EP_SHIFT_TIMESTAMP_VALID_FLAG);
      }

      // fix this typecast; may be status field needs to be changed to lStatus in ISOD
      read_done_evt.data_status = (uint32_t)(errCode | ext_out_port_ptr->buf.status);
      read_done_evt.md_status   = (uint32_t)ext_out_port_ptr->buf.md_buf_ptr->status;

      if (NULL != ext_out_port_ptr->buf.data_ptr)
      {
         posal_memorymap_shm_decr_refcount(apm_get_mem_map_client(), ext_out_port_ptr->buf.mem_map_handle);

         ext_out_port_ptr->buf.mem_map_handle  = 0;
         ext_out_port_ptr->buf.actual_data_len = 0;
         ext_out_port_ptr->buf.data_ptr        = 0;
      }

      if ((read_done_evt.md_size) && (0 == ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle))
      {
         rd_done_payload_ptr  = ext_out_port_ptr->buf.md_buf_ptr->inband_buf_ptr;
         rd_done_payload_size = sizeof(read_done_evt) + ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;
         memscpy(rd_done_payload_ptr, sizeof(read_done_evt), &read_done_evt, sizeof(read_done_evt));
      }
      else
      {
         rd_done_payload_ptr  = (int8_t *)&read_done_evt;
         rd_done_payload_size = sizeof(read_done_evt);
      }

      if (NULL != ext_out_port_ptr->buf.md_buf_ptr->data_ptr)
      {
         if (ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle)
         {
            posal_memorymap_shm_decr_refcount(apm_get_mem_map_client(),
                                              ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle);
         }

         ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle  = 0;
         ext_out_port_ptr->buf.md_buf_ptr->max_data_len    = 0;
         ext_out_port_ptr->buf.md_buf_ptr->data_ptr        = NULL;
         ext_out_port_ptr->buf.md_buf_ptr->status          = AR_EOK;
         ext_out_port_ptr->buf.md_buf_ptr->actual_data_len = 0;
      }
   }
   else
   {
      read_done_evt.data_status = (uint32_t)errCode;
      read_done_evt.md_status   = (uint32_t)ext_out_port_ptr->buf.md_buf_ptr->status;
      rd_done_payload_ptr       = (int8_t *)&read_done_evt;
      rd_done_payload_size      = sizeof(read_done_evt);
   }

   // Return  buffer to client. DATA_CMD_RD_SH_MEM_EP_DATA_BUFFER_V2, ack opCode will be filled by below function
   cu_gpr_generate_ack(&me_ptr->cu,
                       packet_ptr,
                       errCode,
                       (void *)rd_done_payload_ptr,
                       rd_done_payload_size,
                       DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_DONE_V2);

   // set payload of gpQMsg to null to indicate that we are not holding on to any
   // output buffer
   ext_out_port_ptr->out_buf_gpr_client.payload_ptr = NULL;
   ext_out_port_ptr->buf.actual_data_len            = 0;
   ext_out_port_ptr->buf.max_data_len               = 0;
   ext_out_port_ptr->buf.data_ptr                   = NULL;

   ext_out_port_ptr->buf.md_buf_ptr->max_data_len    = 0;
   ext_out_port_ptr->buf.md_buf_ptr->actual_data_len = 0;
   ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle  = 0;
   ext_out_port_ptr->buf.md_buf_ptr->data_ptr        = NULL;
   ext_out_port_ptr->buf.md_buf_ptr->status          = AR_EOK;

   // usually ext buffer gets cleared during the post process. but if the mf was raised, post process will not be
   // called, and old buffer needs to be released here. Also if internal ports in nblc are borrowing ext buffer free it
   // here.
   result |= gen_cntr_clear_borrowed_ext_out_buffer_from_int_ports(me_ptr,
                                                                   ext_out_port_ptr,
                                                                   (gen_topo_output_port_t *)
                                                                      ext_out_port_ptr->gu.int_out_port_ptr);

   return result;
}

ar_result_t gen_cntr_release_gpr_client_buffer(gen_cntr_t *             me_ptr,
                                               gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                               ar_result_t              errCode)
{
   ar_result_t result = AR_EOK;
   if (!ext_out_port_ptr->out_buf_gpr_client.payload_ptr)
   {
      return result;
   }

#ifdef VERBOSE_DEBUGGING
   if (CLIENT_ID_OLC == ext_out_port_ptr->client_config.cid.client_id)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "read_buf_size rcxd : %lu out_buf_size : %lu",
                   ext_out_port_ptr->client_config.read_data_buf_size,
                   ext_out_port_ptr->buf.actual_data_len);
   }
#endif
   result = gen_cntr_release_gpr_client_buffer_v2(me_ptr, ext_out_port_ptr, errCode);

   return result;
}

ar_result_t gen_cntr_flush_cache_and_release_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   // Flush the shared mem map region
   uint32_t client_buf_size = ext_out_port_ptr->buf.max_data_len;

   if (ext_out_port_ptr->buf.data_ptr)
   {
      posal_cache_flush_v2(&ext_out_port_ptr->buf.data_ptr, client_buf_size);
   }

   if ((ext_out_port_ptr->buf.md_buf_ptr->mem_map_handle) && (ext_out_port_ptr->buf.md_buf_ptr->data_ptr))
   {
      posal_cache_flush_v2(&ext_out_port_ptr->buf.md_buf_ptr->data_ptr, ext_out_port_ptr->buf.md_buf_ptr->max_data_len);
   }

   // discard the remaining input pcm samples and release the output buffer
   gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);

   return AR_EOK;
}

/**
 * Recursive
 * no need to add recursion counters here as if any failure were to occur due to recursion depth check,
 * then it would've happened at thresh prop.
 */
static gen_topo_input_port_t *gen_cntr_find_previous_pcm_in_port(gen_topo_input_port_t *in_port_ptr)
{
   if (SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format))
   {
      return in_port_ptr;
   }
   else
   {
      if ((in_port_ptr->gu.conn_out_port_ptr) &&
          (1 == in_port_ptr->gu.conn_out_port_ptr->cmn.module_ptr->num_input_ports) &&
          (1 == in_port_ptr->gu.conn_out_port_ptr->cmn.module_ptr->max_input_ports))
      {
         return gen_cntr_find_previous_pcm_in_port((gen_topo_input_port_t *)in_port_ptr->gu.conn_out_port_ptr->cmn
                                                      .module_ptr->input_port_list_ptr->ip_port_ptr);
      }
      else
      {
         return NULL;
      }
   }
}

// Function to parse the meta-data of the read EP module and update the external output metadata buffer
// CA : For EOS, we need another for prop vs event
static void gen_cntr_write_metadata_in_client_buffer(gen_cntr_t *             me_ptr,
                                                     gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                     uint32_t                 frame_offset_in_ext_buf,
                                                     bool_t *                 release_out_buf_ptr)
{
   uint32_t last_module_id                  = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_id;
   ext_out_port_ptr->buf.md_buf_ptr->status = AR_EOK;
   if (MODULE_ID_RD_SHARED_MEM_EP != last_module_id)
   {
      ext_out_port_ptr->buf.md_buf_ptr->status = AR_EUNEXPECTED;
   }

   uint32_t md_avail_buf_size      = 0; // available md buffer size to fill
   uint32_t meta_data_num_elements = 0; // number of md elements filled in this call
   uint32_t md_element_size        = 0; // size of the md filled in this call
   uint32_t client_md_flags        = 0;

   module_cmn_md_list_t *node_ptr           = NULL;
   module_cmn_md_list_t *temp_node_ptr      = NULL;
   metadata_header_t *   md_data_header_ptr = NULL;
   module_cmn_md_t *     md_ptr             = NULL;
   uint8_t *             src_md_payload_ptr = NULL;
   uint8_t *             dst_md_payload_ptr = NULL;
   int8_t *              client_buf_md_ptr  = NULL;

   node_ptr          = ext_out_port_ptr->md_list_ptr;
   client_buf_md_ptr = ext_out_port_ptr->buf.md_buf_ptr->data_ptr + ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;
   md_avail_buf_size =
      (ext_out_port_ptr->buf.md_buf_ptr->max_data_len - ext_out_port_ptr->buf.md_buf_ptr->actual_data_len);

   while ((node_ptr))
   {
      md_ptr = node_ptr->obj_ptr;

      if (NULL != md_ptr)
      {
         if (MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE ==
             md_ptr->metadata_flag.needs_propagation_to_client_buffer)
         {
            md_element_size = ALIGN_4_BYTES(md_ptr->actual_size + sizeof(metadata_header_t));
            if (md_element_size <= md_avail_buf_size)
            {
               md_data_header_ptr = (metadata_header_t *)client_buf_md_ptr;
               gen_topo_convert_int_md_flags_to_client_md_flag(md_ptr->metadata_flag, &client_md_flags);
               md_data_header_ptr->metadata_id = md_ptr->metadata_id;

               md_data_header_ptr->payload_size = md_ptr->actual_size;
               md_data_header_ptr->flags        = client_md_flags;
               md_data_header_ptr->offset       = frame_offset_in_ext_buf + md_ptr->offset; // frame offset + md_offset

               //#ifdef VERBOSE_DEBUGGING
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_HIGH_PRIO,
                            "MD_DBG: SPF to CLIENT: Wrote MD ID %lx, buffer offset %d",
                            md_ptr->metadata_id,
                            md_data_header_ptr->offset);
               //#endif

               if (NULL != md_ptr->tracking_ptr)
               {
                  module_cmn_md_tracking_payload_t *tracking_payload_ptr =
                     (module_cmn_md_tracking_payload_t *)md_ptr->tracking_ptr;
                  md_data_header_ptr->token_lsw = tracking_payload_ptr->token_lsw;
                  md_data_header_ptr->token_msw = tracking_payload_ptr->token_msw;
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

               dst_md_payload_ptr = (uint8_t *)(md_data_header_ptr + 1);

               memscpy(dst_md_payload_ptr, md_data_header_ptr->payload_size, src_md_payload_ptr, md_ptr->actual_size);

               meta_data_num_elements++;
               ext_out_port_ptr->buf.md_buf_ptr->actual_data_len += md_element_size;
               client_buf_md_ptr += md_element_size;
               md_avail_buf_size -= md_element_size;

               if (MODULE_CMN_MD_ID_BUFFER_END == md_data_header_ptr->metadata_id)
               {
                  /* Set flag to release buffer when we get end md, even if there is space to fill more frames in the
                   * output
                   * For Codec 2.0 we need to release output buffer irrespective of if we get more input*/
                  *release_out_buf_ptr = TRUE;

                  //#ifdef VERBOSE_DEBUGGING
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Release buf due to receiving end md");
                  //#endif
               }

               temp_node_ptr = node_ptr;
               node_ptr      = node_ptr->next_ptr;

               gen_topo_capi_metadata_destroy((void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                              temp_node_ptr,
                                              FALSE /*is_dropped*/,
                                              &ext_out_port_ptr->md_list_ptr,
											  0,
											  FALSE); // rendered
            }
            else
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_ERROR_PRIO,
                            "MD_DBG: failed to propagate metadata id %lx from SPF to CLIENT  ",
                            md_ptr->metadata_id);
               ext_out_port_ptr->buf.md_buf_ptr->status = AR_ENEEDMORE;
               break;
            }
         }
         else
         {
            node_ptr = node_ptr->next_ptr;
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

static ar_result_t gen_cntr_fill_frame_metadata_v2(gen_cntr_t *             me_ptr,
                                                   gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                   uint32_t                 bytes_per_process,
                                                   uint32_t                 frame_offset_in_ext_buf,
                                                   bool_t *                 release_out_buf_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_timestamp_t    ts              = { 0 };
   uint32_t                bytes_per_frame = bytes_per_process;
   uint32_t                md_filled       = 0;
   gen_topo_output_port_t *out_port_ptr    = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   uint32_t one_frame_metadata_len = gen_cntr_get_metadata_length_for_read_cmd_v2(me_ptr, ext_out_port_ptr);
   if (0 == ext_out_port_ptr->num_frames_in_buf)
   {
      if (ext_out_port_ptr->flags.fill_ext_buf)
      {
         /**  based on first frame in this out-buffer, decide the number of frames that will be filled.
          *   if we cannot hold so many frames we will release anyway. if first frame is too large,
          *   then we will release earlier.
          *   ext_out_port_ptr->buf.actual_data_len = first frame length + first frame metadata (if any)*/
         ext_out_port_ptr->max_frames_per_buffer =
            ext_out_port_ptr->buf.max_data_len / ext_out_port_ptr->buf.actual_data_len;

#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "This out buffer can hold max frames %lu. Total size = %lu, First frame size %lu",
                      ext_out_port_ptr->max_frames_per_buffer,
                      ext_out_port_ptr->buf.max_data_len,
                      ext_out_port_ptr->buf.actual_data_len);
#endif

         // need to move first frame down to accommodate for metadata.
         if ((ext_out_port_ptr->flags.frame_metadata_enable) && (one_frame_metadata_len))
         {
            // max number of enc frames assuming no other md is filled
            uint32_t max_num_enc_frame_md = capi_cmn_divide(ext_out_port_ptr->buf.md_buf_ptr->max_data_len , one_frame_metadata_len);

            ext_out_port_ptr->max_frames_per_buffer =

               (ext_out_port_ptr->max_frames_per_buffer > max_num_enc_frame_md)
                  ? max_num_enc_frame_md
                  : ext_out_port_ptr->max_frames_per_buffer;
         }
      }
   }

   ts.value = out_port_ptr->common.sdata.timestamp;
   ts.valid = out_port_ptr->common.sdata.flags.is_timestamp_valid;

   md_filled = ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;
   if (ext_out_port_ptr->flags.frame_metadata_enable && ext_out_port_ptr->out_buf_gpr_client.payload_ptr)
   {
      if ((ext_out_port_ptr->buf.md_buf_ptr->actual_data_len + one_frame_metadata_len) <=
          ext_out_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         metadata_header_t *md_header_info_ptr =
            (metadata_header_t *)(ext_out_port_ptr->buf.md_buf_ptr->data_ptr +
                                  ext_out_port_ptr->buf.md_buf_ptr->actual_data_len);

         md_header_info_ptr->metadata_id  = MODULE_CMN_MD_ID_ENCODER_FRAME_INFO;
         md_header_info_ptr->offset       = frame_offset_in_ext_buf;
         md_header_info_ptr->payload_size = sizeof(module_cmn_md_encoder_per_frame_info_t);
         md_header_info_ptr->token_lsw    = 0;
         md_header_info_ptr->token_msw    = 0;
         md_header_info_ptr->flags        = 0x0; // to external client, no tracking, is last, sample associated

         module_cmn_md_encoder_per_frame_info_t *enc_per_frame_info_ptr =
            (module_cmn_md_encoder_per_frame_info_t *)(md_header_info_ptr + 1);

         enc_per_frame_info_ptr->flags = 0;
         cu_set_bits(&(enc_per_frame_info_ptr->flags),
                     ts.valid,
                     RD_SH_MEM_EP_BIT_MASK_TIMESTAMP_VALID_FLAG,
                     RD_SH_MEM_EP_SHIFT_TIMESTAMP_VALID_FLAG);

         enc_per_frame_info_ptr->timestamp_lsw = (uint32_t)ts.value;
         enc_per_frame_info_ptr->timestamp_msw = (uint32_t)(ts.value >> 32);
         enc_per_frame_info_ptr->frame_size    = bytes_per_frame;

         ext_out_port_ptr->buf.md_buf_ptr->actual_data_len += one_frame_metadata_len;

         //#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Fill enc frame md: offset %d, md len filled %d, frame_size %d, flags: %lx",
                      md_header_info_ptr->offset,
                      ext_out_port_ptr->buf.md_buf_ptr->actual_data_len,
                      enc_per_frame_info_ptr->frame_size,
                      enc_per_frame_info_ptr->flags);
         //#endif
      }
   }

   // Fill all other metadata after frame-processing into the Client buffer.
   // The handling is slight different for RD client for HLOS versus OLC.
   if (ext_out_port_ptr->vtbl_ptr->write_metadata)
   {
      ext_out_port_ptr->vtbl_ptr->write_metadata(me_ptr,
                                                 ext_out_port_ptr,
                                                 frame_offset_in_ext_buf,
                                                 release_out_buf_ptr);
   }

   /** determined the metadata filled during this frame processing and update the max md per frame
    * Max MD per frame is a running statistical data used to ensure we have
    * sufficient MD buf space to process next frame
    **/
   md_filled = ext_out_port_ptr->buf.md_buf_ptr->actual_data_len - md_filled;
   if (md_filled > ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame)
   {
      ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame = md_filled;
   }

   /** check if we have sufficient md space to process further  */
   if (0 < ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame)
   {
      uint32_t md_buf_space_avail = 0;
      md_buf_space_avail =
         ext_out_port_ptr->buf.md_buf_ptr->max_data_len - ext_out_port_ptr->buf.md_buf_ptr->actual_data_len;

      // minimum MD size required to process next frame
      if (md_buf_space_avail < ext_out_port_ptr->buf.md_buf_ptr->max_md_size_per_frame)
      {
         *release_out_buf_ptr = TRUE;
         // this will set the release output buffer flag in the frame processing and release the buffer

         //#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Release buf due to md buf being completely filled");
         //#endif
      }
   }

   // if md_data cannot be filled, we drop all the meta.
   // if tracking is enabled, a tracking event would be raised . md_status says error. (need more);
   // The dropping and raising an event would be done while rendering the buffer
   // Check if flag has been set during md handling

   return result;
}

/**
 * fixed max num frames:
 *    Without metadata:    when client buf is received,actual length = 0;
 *                         in post_process: nothing special
 *                         in fill_metadata:
 *    with metadata :      when client buf is received, actual length = max-frames * length of metadata
 *                         in post_process:
 *                         in fill_metadata:
 * filling as much as possible:
 *    without metadata:    when client buf is received, actual length = 0;
 *                         in post_process:
 *                         in fill_metadata: max_frames_per_buffer is determined
 *    with metadata:       when client buf is received,actual length = 1 * length of metadata.
 *                         in post_process: nothing special
 *                         in fill_metadata: max_frames_per_buffer is determined &
 *                            first frame data is moved down to make space for other frames metadata
 *
 *
 */
ar_result_t gen_cntr_fill_frame_metadata(gen_cntr_t *             me_ptr,
                                         gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                         uint32_t                 bytes_per_process,
                                         uint32_t                 frame_offset_in_ext_buf,
                                         bool_t *                 release_out_buf_ptr)
{
   ar_result_t result = AR_EOK;

   result = gen_cntr_fill_frame_metadata_v2(me_ptr,
                                            ext_out_port_ptr,
                                            bytes_per_process,
                                            frame_offset_in_ext_buf,
                                            release_out_buf_ptr);

   return result;
}

void gen_cntr_propagate_metadata_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   if (!ext_out_port_ptr->md_list_ptr)
   {
      return;
   }

   uint32_t              last_module_id = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_id;
   module_cmn_md_list_t *node_ptr       = ext_out_port_ptr->md_list_ptr;
   module_cmn_md_list_t *next_ptr       = NULL;

   while (node_ptr)
   {
      next_ptr = node_ptr->next_ptr;

      // detach
      node_ptr->next_ptr = NULL;
      node_ptr->prev_ptr = NULL;

      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;
      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         // module_cmn_md_eos_t *eos_metadata_ptr = NULL;

         uint32_t is_out_band = md_ptr->metadata_flag.is_out_of_band;

         if (is_out_band)
         {
            // eos_metadata_ptr = (module_cmn_md_eos_t *)md_ptr->metadata_ptr;
         }
         else
         {
            // eos_metadata_ptr = (module_cmn_md_eos_t *)&(md_ptr->metadata_buf);
         }

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "MD_DBG: Raising rendered EOS event!, tracking payload = 0x%p",
                      md_ptr->tracking_ptr);
      }
      else if (MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id)
      {
         // ignore & destroy
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "MD_DBG: Unexpected metadata ID 0x%lX at module 0x%lX output. Destroying",
                      md_ptr->metadata_id,
                      last_module_id);
      }

      bool_t is_dropped = FALSE;
      // Client metadata should have already been written to read buffer and removed from list.
      // if it is still in the list, it implies it is getting dropped
      if (MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_ENABLE ==
          md_ptr->metadata_flag.needs_propagation_to_client_buffer)
      {
         is_dropped = TRUE;
      }

      gen_topo_capi_metadata_destroy((void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                     node_ptr,
                                     is_dropped /*is_dropped*/,
                                     &ext_out_port_ptr->md_list_ptr,
									 0,
									 FALSE); // rendered

      node_ptr = next_ptr;
   }
}

ar_result_t gen_cntr_write_data_for_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
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

   result = gen_cntr_flush_cache_and_release_out_buf(me_ptr, ext_out_port_ptr);

   gen_cntr_propagate_metadata_gpr_client(me_ptr, ext_out_port_ptr);

   return result;
}

uint32_t gen_cntr_get_amount_of_data_in_gpr_client_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   if (!ext_out_port_ptr->buf.data_ptr || !ext_out_port_ptr->out_buf_gpr_client.payload_ptr)
   {
      return 0;
   }

   return ext_out_port_ptr->buf.actual_data_len;
}

ar_result_t gen_cntr_send_media_fmt_to_gpr_client(gen_cntr_t *             me_ptr,
                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                  uint32_t                 reg_mf_event_id,
                                                  bool_t                   raise_only_event)
{
   ar_result_t result = AR_EOK;
   // This is like SR_CM event in elite.
   media_format_t  media_fmt    = { 0 };
   media_format_t *med_fmt_ptr  = NULL;
   uint32_t        payload_size = 0;

   typedef struct pcm_med_fmt_t
   {
      media_format_t          main;
      payload_media_fmt_pcm_t pcm;
      uint8_t                 chan_map[MAX_NUM_CHANNELS];
   } pcm_med_fmt_t;

   pcm_med_fmt_t pcm_media_fmt;

   if (((SPF_IS_PCM_DATA_FORMAT(ext_out_port_ptr->cu.media_fmt.data_format)) &&
        ((MEDIA_FMT_ID_PCM == ext_out_port_ptr->cu.media_fmt.fmt_id) || (0 == ext_out_port_ptr->cu.media_fmt.fmt_id))))
   {
      if (TU_IS_ANY_DEINTERLEAVED_UNPACKED(ext_out_port_ptr->cu.media_fmt.pcm.interleaving) ||
          (TOPO_DEINTERLEAVED_PACKED == ext_out_port_ptr->cu.media_fmt.pcm.interleaving))
      {
         // print error here, instead of earlier because earlier media fmt may be stray media fmts which might be
         // overridden later
         // but this is final; after this there'll be data flow.
         TOPO_MSG(me_ptr->topo.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Rd sh mem EP Module 0x%lX is receiving deinterleaved data. This won't work for client. Can be "
                  "used for communication in mdf framework",
                  ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
         // deinterleaved won't work because client APIs are not designed for deint data.

         ext_out_port_ptr->cu.media_fmt.fmt_id = MEDIA_FMT_ID_PCM;

         pcm_media_fmt.main.data_format =
            gen_topo_convert_spf_data_fmt_public_data_format(ext_out_port_ptr->cu.media_fmt.data_format);
         pcm_media_fmt.pcm.endianness =
            gen_topo_convert_gen_topo_endianness_to_public_endianness(ext_out_port_ptr->cu.media_fmt.pcm.endianness);
         pcm_media_fmt.main.fmt_id       = ext_out_port_ptr->cu.media_fmt.fmt_id;
         pcm_media_fmt.main.payload_size = sizeof(payload_media_fmt_pcm_t);

         pcm_media_fmt.pcm.interleaved = gen_topo_convert_gen_topo_interleaving_to_public_interleaving(
            ext_out_port_ptr->cu.media_fmt.pcm.interleaving);
         // pcm_media_fmt_extn.pcm_extn.reserved = 0;

         pcm_media_fmt.pcm.alignment = PCM_LSB_ALIGNED;
         // in Ext API: bits_per_sample = topo bit width, sample_word_size = topo bits per sample (CAPI has only bits
         // per
         // sample which means sample word size)
         pcm_media_fmt.pcm.bit_width       = ext_out_port_ptr->cu.media_fmt.pcm.bit_width;
         pcm_media_fmt.pcm.bits_per_sample = ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample;
         pcm_media_fmt.pcm.num_channels    = ext_out_port_ptr->cu.media_fmt.pcm.num_channels;
         pcm_media_fmt.pcm.q_factor        = ext_out_port_ptr->cu.media_fmt.pcm.q_factor;
         pcm_media_fmt.pcm.sample_rate     = ext_out_port_ptr->cu.media_fmt.pcm.sample_rate;

         for (uint32_t i = 0; i < pcm_media_fmt.pcm.num_channels; i++)
         {
            pcm_media_fmt.chan_map[i] = ext_out_port_ptr->cu.media_fmt.pcm.chan_map[i];
         }

         med_fmt_ptr  = &pcm_media_fmt.main;
         payload_size = sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t) +
                        ALIGN_4_BYTES(pcm_media_fmt.pcm.num_channels * sizeof(int8_t));
      }
      else if (TOPO_INTERLEAVED == ext_out_port_ptr->cu.media_fmt.pcm.interleaving)
      {
         ext_out_port_ptr->cu.media_fmt.fmt_id = MEDIA_FMT_ID_PCM;

         pcm_media_fmt.main.data_format =
            gen_topo_convert_spf_data_fmt_public_data_format(ext_out_port_ptr->cu.media_fmt.data_format);
         pcm_media_fmt.pcm.endianness =
            gen_topo_convert_gen_topo_endianness_to_public_endianness(ext_out_port_ptr->cu.media_fmt.pcm.endianness);
         pcm_media_fmt.main.fmt_id       = ext_out_port_ptr->cu.media_fmt.fmt_id;
         pcm_media_fmt.main.payload_size = sizeof(payload_media_fmt_pcm_t);
         pcm_media_fmt.pcm.alignment     = PCM_LSB_ALIGNED;
         pcm_media_fmt.pcm.interleaved   = gen_topo_convert_gen_topo_interleaving_to_public_interleaving(
            ext_out_port_ptr->cu.media_fmt.pcm.interleaving);
         // in Ext API: bits_per_sample = topo bit width, sample_word_size = topo bits per sample (CAPI has only bits
         // per
         // sample which means sample word size)
         pcm_media_fmt.pcm.bit_width       = ext_out_port_ptr->cu.media_fmt.pcm.bit_width;
         pcm_media_fmt.pcm.bits_per_sample = ext_out_port_ptr->cu.media_fmt.pcm.bits_per_sample;
         pcm_media_fmt.pcm.num_channels    = ext_out_port_ptr->cu.media_fmt.pcm.num_channels;
         pcm_media_fmt.pcm.q_factor        = ext_out_port_ptr->cu.media_fmt.pcm.q_factor;
         pcm_media_fmt.pcm.sample_rate     = ext_out_port_ptr->cu.media_fmt.pcm.sample_rate;

         for (uint32_t i = 0; i < pcm_media_fmt.pcm.num_channels; i++)
         {
            pcm_media_fmt.chan_map[i] = ext_out_port_ptr->cu.media_fmt.pcm.chan_map[i];
         }

         med_fmt_ptr  = &pcm_media_fmt.main;
         payload_size = sizeof(media_format_t) + sizeof(payload_media_fmt_pcm_t) +
                        ALIGN_4_BYTES(pcm_media_fmt.pcm.num_channels * sizeof(int8_t));
      }
      else
      {
         return AR_EBADPARAM;
      }
   }
   else if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
   {
      media_fmt.data_format =
         gen_topo_convert_spf_data_fmt_public_data_format(ext_out_port_ptr->cu.media_fmt.data_format);
      media_fmt.fmt_id       = ext_out_port_ptr->cu.media_fmt.fmt_id;
      media_fmt.payload_size = ext_out_port_ptr->cu.media_fmt.raw.buf_size;
      med_fmt_ptr            = &media_fmt;
      payload_size           = sizeof(media_format_t) + media_fmt.payload_size;
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "GEN_CNTR: deinterleaved raw compr media format");
      return AR_EBADPARAM;
   }
   else
   {
      media_fmt.data_format =
         gen_topo_convert_spf_data_fmt_public_data_format(ext_out_port_ptr->cu.media_fmt.data_format);
      media_fmt.fmt_id       = ext_out_port_ptr->cu.media_fmt.fmt_id;
      media_fmt.payload_size = 0;
      med_fmt_ptr            = &media_fmt;
      payload_size           = sizeof(media_format_t);
   }

   gen_topo_module_t *module_ptr          = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;

   if ((NULL == module_ptr) || (NULL == gen_cntr_module_ptr->fwk_module_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "GEN_CNTR: Not raising event id 0x%lX", reg_mf_event_id);
      goto __gen_cntr_send_mf_to_client_bail;
   }

   if (NULL != gen_cntr_module_ptr->cu.event_list_ptr)
   {
      spf_list_node_t *client_list_ptr;
      cu_find_client_info(me_ptr->topo.gu.log_id,
                          reg_mf_event_id,
                          gen_cntr_module_ptr->cu.event_list_ptr,
                          &client_list_ptr);

      if ((NULL != client_list_ptr) && (NULL != med_fmt_ptr))
      {
         if (0 == med_fmt_ptr->data_format)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "GEN_CNTR: Not raising media format event id 0x%lX , "
                         "data_format = %u, fmt_id = 0x%lX ",
                         reg_mf_event_id,
                         med_fmt_ptr->data_format,
                         med_fmt_ptr->fmt_id);
         }
         else
         {
            if (0 == med_fmt_ptr->payload_size)
            {
               // media_fmt.payload_size is zero as framework module cannot take care of all formats. for packetized we
               // could define one more media fmt
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_MED_PRIO,
                            "Warning: Payload size will be zero in this media format event to client");
            }

            // some modules don't propagate fmt-id, so assume PCM in case it's fixd point and fmt-id is 0.
            if (((DATA_FORMAT_FIXED_POINT == med_fmt_ptr->data_format) ||
                 (DATA_FORMAT_FLOATING_POINT == med_fmt_ptr->data_format)) &&
                ((MEDIA_FMT_ID_PCM == med_fmt_ptr->fmt_id) || (0 == med_fmt_ptr->fmt_id)))
            {
               uint8_t *payload = (uint8_t *)(med_fmt_ptr + 1);
               if (TU_IS_ANY_DEINTERLEAVED_UNPACKED(ext_out_port_ptr->cu.media_fmt.pcm.interleaving) ||
                   (TOPO_DEINTERLEAVED_PACKED == ext_out_port_ptr->cu.media_fmt.pcm.interleaving))
               {
                  med_fmt_ptr->fmt_id = MEDIA_FMT_ID_PCM_EXTN;
               }
               else if (TOPO_INTERLEAVED == ext_out_port_ptr->cu.media_fmt.pcm.interleaving)
               {
                  med_fmt_ptr->fmt_id = MEDIA_FMT_ID_PCM;
               }

               payload_media_fmt_pcm_t *pcm_ptr = (payload_media_fmt_pcm_t *)(payload);
               if (NULL != pcm_ptr)
               {
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_HIGH_PRIO,
                               "GEN_CNTR: Raising DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT to client, data_format = "
                               "FIXED_POINT, fmt_id = 0x%lX, alignment = %lu, bits_per_sample = %lu, "
                               "endianness = %lu, num_channels = %lu, q_factor = %lu, sample_rate = %lu,"
                               "sample_word_size = %lu",
                               med_fmt_ptr->fmt_id,
                               pcm_ptr->alignment,
                               pcm_ptr->bit_width,
                               pcm_ptr->endianness,
                               pcm_ptr->num_channels,
                               pcm_ptr->q_factor,
                               pcm_ptr->sample_rate,
                               pcm_ptr->bits_per_sample);
               }
            }
            else
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_HIGH_PRIO,
                            "GEN_CNTR: Raising media format event id 0x%lX, data_format = %u, fmt_id = 0x%lX ",
                            reg_mf_event_id,
                            med_fmt_ptr->data_format,
                            med_fmt_ptr->fmt_id);
            }

            for (cu_client_info_t *client_info_ptr = (cu_client_info_t *)(client_list_ptr->obj_ptr);
                 (NULL != client_list_ptr);
                 LIST_ADVANCE(client_list_ptr))
            {
               if (TU_IS_ANY_DEINTERLEAVED_UNPACKED(ext_out_port_ptr->cu.media_fmt.pcm.interleaving) ||
                   (TOPO_DEINTERLEAVED_PACKED == ext_out_port_ptr->cu.media_fmt.pcm.interleaving))
               {
                  switch (client_info_ptr->dest_domain_id)
                  {
                     case APM_PROC_DOMAIN_ID_MDSP:
                     case APM_PROC_DOMAIN_ID_ADSP:
                     case APM_PROC_DOMAIN_ID_APPS:
                     case APM_PROC_DOMAIN_ID_CDSP:
                     case APM_PROC_DOMAIN_ID_SDSP:
                     case APM_PROC_DOMAIN_ID_GDSP_0:
                     case APM_PROC_DOMAIN_ID_GDSP_1:
                     case APM_PROC_DOMAIN_ID_APPS_2:
                     {
                        break;
                     }
                     default:
                     {
                        // we can introduce crash here
                        GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                                     DBG_ERROR_PRIO,
                                     "Rd sh mem EP Module 0x%lX is sending deinterleaved data. "
                                     "This won't work for client. ",
                                     ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
                     }
                  }
               }
               gpr_packet_t *      gpr_pkt_mf_ptr         = NULL;
               void *              gpr_pkt_mf_payload_ptr = NULL;
               gpr_cmd_alloc_ext_t args;

               args.src_domain_id = client_info_ptr->dest_domain_id;
               args.dst_domain_id = client_info_ptr->src_domain_id;
               args.src_port      = ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id;
               args.dst_port      = client_info_ptr->src_port;
               args.token         = client_info_ptr->token;
               args.opcode        = reg_mf_event_id;
               // args.payload       = med_fmt_ptr;
               args.payload_size = payload_size;
               args.ret_packet   = &gpr_pkt_mf_ptr;
               args.client_data  = 0;
               result            = __gpr_cmd_alloc_ext(&args);
               if (AR_DID_FAIL(result) || NULL == gpr_pkt_mf_ptr)
               {
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_ERROR_PRIO,
                               "Allocating MF pkt to send to dsp client failed with %lu",
                               result);
                  return AR_ENOMEMORY;
               }
               gpr_pkt_mf_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_mf_ptr);

               if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
               {
                  memscpy(gpr_pkt_mf_payload_ptr, args.payload_size, med_fmt_ptr, sizeof(media_format_t));
                  int8_t * mf_ptr = (int8_t *)gpr_pkt_mf_payload_ptr + sizeof(media_format_t);
                  uint32_t sz     = args.payload_size - sizeof(media_format_t);
                  if (ext_out_port_ptr->cu.media_fmt.raw.buf_ptr && (sz > 0))
                  {
                     memscpy(mf_ptr,
                             args.payload_size - sizeof(media_format_t),
                             ext_out_port_ptr->cu.media_fmt.raw.buf_ptr,
                             med_fmt_ptr->payload_size);
                  }
               }
               else
               {
                  memscpy(gpr_pkt_mf_payload_ptr, args.payload_size, med_fmt_ptr, args.payload_size);
               }

               if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_mf_ptr)))
               {
                  result = AR_EFAILED;
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_ERROR_PRIO,
                               "Sending MF pkt to client failed with %lu",
                               result);
                  __gpr_cmd_free(gpr_pkt_mf_ptr);
               }
            }
         }
      }

      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to raise media format to client");
      }
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "GEN_CNTR: Not raising event id 0x%lX", reg_mf_event_id);
      if (FALSE == ext_out_port_ptr->flags.md_mf_enable)
      {
         ext_out_port_ptr->flags.out_media_fmt_changed = FALSE;
         goto __gen_cntr_send_mf_to_client_bail;
      }
   }

   if (TRUE == raise_only_event)
   {
      if (FALSE == ext_out_port_ptr->flags.md_mf_enable)
      {
         ext_out_port_ptr->flags.out_media_fmt_changed = FALSE;
      }
      return AR_EOK;
   }

   if (ext_out_port_ptr->flags.md_mf_enable)
   {
      // raise mf event again later and write md later
      // avoid freeing the raw compressed payload
      if (0 == ext_out_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Media format metadata enabled, but rd buffer not received "
                      "yet, raise mf event and write mf md later");
         // setting the flag as TRUE.
         ext_out_port_ptr->flags.out_media_fmt_changed = TRUE;
         return AR_EOK;
      }

      // check if the md buffer has enough space to write the media format as MD.
      if ((ext_out_port_ptr->buf.md_buf_ptr->actual_data_len + sizeof(metadata_header_t) + payload_size) <=
          ext_out_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         metadata_header_t *md_header_info_ptr =
            (metadata_header_t *)(ext_out_port_ptr->buf.md_buf_ptr->data_ptr +
                                  ext_out_port_ptr->buf.md_buf_ptr->actual_data_len);

         md_header_info_ptr->metadata_id  = MODULE_CMN_MD_ID_MEDIA_FORMAT;
         md_header_info_ptr->offset       = 0;
         md_header_info_ptr->payload_size = payload_size;
         md_header_info_ptr->token_lsw    = 0;
         md_header_info_ptr->token_msw    = 0;
         md_header_info_ptr->flags        = 0x0; // to external client, no tracking, is last, sample associated

         uint8_t *md_payload_ptr = (uint8_t *)(md_header_info_ptr + 1);

         if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
         {
            memscpy(md_payload_ptr, payload_size, med_fmt_ptr, sizeof(media_format_t));
            int8_t * mf_ptr = (int8_t *)md_payload_ptr + sizeof(media_format_t);
            uint32_t sz     = payload_size - sizeof(media_format_t);
            if (ext_out_port_ptr->cu.media_fmt.raw.buf_ptr && (sz > 0))
            {
               memscpy(mf_ptr,
                       payload_size - sizeof(media_format_t),
                       ext_out_port_ptr->cu.media_fmt.raw.buf_ptr,
                       med_fmt_ptr->payload_size);
            }
         }
         else
         {
            memscpy(md_payload_ptr, payload_size, med_fmt_ptr, payload_size);
         }

         ext_out_port_ptr->buf.md_buf_ptr->actual_data_len += (sizeof(metadata_header_t) + payload_size);

         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "GEN_CNTR: SPF to CLIENT: Writing Media format to MD buf MD_ID 0x%lX, size %lu flags 0x%lX",
                      md_header_info_ptr->metadata_id,
                      md_header_info_ptr->payload_size,
					  md_header_info_ptr->flags);

         // MD MF is propagated to the read buffer
         ext_out_port_ptr->flags.out_media_fmt_changed                = FALSE;
         ext_out_port_ptr->buf.md_buf_ptr->min_md_size_in_next_buffer = 0;
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "GEN_CNTR: Dropping data %lu in bytes and md %lu in bytes, "
                      "failed to write Media format to MD buf MD_ID 0x%lX, "
                      "Client need to increase MD buffer size",
                      ext_out_port_ptr->buf.actual_data_len,
                      ext_out_port_ptr->buf.md_buf_ptr->actual_data_len,
                      (uint32_t)MODULE_CMN_MD_ID_MEDIA_FORMAT);

         ext_out_port_ptr->buf.md_buf_ptr->status |= AR_ENOMEMORY;
         // Setting the MD size to zero, as any MD filled would not make much meaning with data being dropped.
         ext_out_port_ptr->buf.md_buf_ptr->actual_data_len = 0;
         // setting the out fill size as zero. dropping any data
         ext_out_port_ptr->buf.actual_data_len = 0;
         // setting num frames also to 0 since we are dropping data
         ext_out_port_ptr->num_frames_in_buf = 0;

         ext_out_port_ptr->buf.md_buf_ptr->min_md_size_in_next_buffer = sizeof(metadata_header_t) + payload_size;

         return AR_EFAILED;
      }
   }
   else
   {
      // MD MF is not enabled and event is raised if client is registered
      ext_out_port_ptr->flags.out_media_fmt_changed = FALSE;
   }

__gen_cntr_send_mf_to_client_bail:
   if (SPF_RAW_COMPRESSED == ext_out_port_ptr->cu.media_fmt.data_format)
   {
      tu_capi_destroy_raw_compr_med_fmt(&ext_out_port_ptr->cu.media_fmt.raw);
   }
   return result;
}

/**
 * only if client issued a command, we can flush the RD_SH_MEM_EP queue.
 * Otherwise, if we flush, client may push back, leading to infinite loop.
 */
ar_result_t gen_cntr_flush_output_data_queue_gpr_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        bool_t                   is_client_cmd)
{
   // first check if we have any output buffers already popped out

   if (TRUE == ext_out_port_ptr->flags.out_media_fmt_changed)
   {
      gen_cntr_send_media_fmt_to_gpr_client(me_ptr, ext_out_port_ptr, DATA_EVENT_ID_RD_SH_MEM_EP_MEDIA_FORMAT, FALSE);
   }

   if (ext_out_port_ptr->out_buf_gpr_client.payload_ptr && is_client_cmd)
   {
      gen_cntr_flush_cache_and_release_out_buf(me_ptr, ext_out_port_ptr);
   }

   if (ext_out_port_ptr->gu.this_handle.q_ptr)
   {
      if (is_client_cmd)
      {
         // Now release any data buffers in the queue...
         while (AR_EOK == posal_queue_pop_front(ext_out_port_ptr->gu.this_handle.q_ptr,
                                                (posal_queue_element_t *)&ext_out_port_ptr->out_buf_gpr_client))
         {
            gen_cntr_release_gpr_client_buffer(me_ptr, ext_out_port_ptr, AR_EOK);
         }
      }
      // no need to release any output buf from queue since they are empty and also belong to enc.
   }

   return AR_EOK;
}

ar_result_t gen_cntr_rd_ep_num_loops_err_check(gen_topo_t *topo_ptr, gen_topo_module_t *module_ptr)
{
   ar_result_t result = AR_EOK;

   /**num_proc_loops doesn't work if there's metadata to be propagated per frame.
    * E.g. in encoding use cases, there's frame level metadata such as num-pcm-samples per frame, frame timestamp
    * etc*/
   if (MODULE_ID_RD_SHARED_MEM_EP == module_ptr->gu.module_id)
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr->ext_out_port_ptr;
      // metadata enabled cases where fill as much (-1) or more than 1
      if (ext_out_port_ptr->flags.frame_metadata_enable && (ext_out_port_ptr->max_frames_per_buffer != 1))
      {
         gen_topo_input_port_t *in_port_ptr = NULL;
         if ((1 == ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->num_input_ports) &&
             (1 == ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->max_input_ports))
         {
            in_port_ptr = gen_cntr_find_previous_pcm_in_port(
               (gen_topo_input_port_t *)
                  ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->input_port_list_ptr->ip_port_ptr);
         }
         if (in_port_ptr)
         {
            gen_topo_module_t *pcm_module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
            if (1 != pcm_module_ptr->num_proc_loops)
            {
               GEN_CNTR_MSG(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "num_proc_loops %lu != 1 of module 0x%lX when sh mem EP 0x%lX has max_frames_per_buf = %lu",
                            pcm_module_ptr->num_proc_loops,
                            pcm_module_ptr->gu.module_instance_id,
                            module_ptr->gu.module_instance_id,
                            ext_out_port_ptr->max_frames_per_buffer);
               // just print error, we will know metadata is not properly propagated.
            }
         }
         else
         {
            // if no input port is found, then it means either there's no encoder (MDF cases encoder can be outside this
            // cntr),
            // or there are multi-port modules. just print error as we cannot guarantee frame level metadata such as
            // timestamps
            GEN_CNTR_MSG(topo_ptr->gu.log_id,
                         DBG_ERROR_PRIO,
                         "Frame level metadata (timestamp) not supported if "
                         "there's no SISO path to encoder in this cntr");
         }
      }
   }
   return result;
}

static ar_result_t gen_cntr_recreate_out_buf_gpr_client(gen_cntr_t *             me_ptr,
                                                        gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                        uint32_t                 req_out_buf_size,
                                                        uint32_t                 num_data_msg,
                                                        uint32_t                 num_bufs_per_data_msg_v2)
{
   ext_out_port_ptr->cu.buf_max_size = req_out_buf_size;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Not creating output buffer for DSP client");

   return AR_EOK;
}

/* function to handle the peer port property cmd handling from SPF external Client
 */
static ar_result_t gen_cntr_process_rdep_shmem_peer_client_property_config(gen_cntr_t *  me_ptr,
                                                                           spf_handle_t *handle_ptr,
                                                                           int8_t *      param_data_ptr,
                                                                           uint32_t      param_size)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != me_ptr));
   // validate the function arguments
   VERIFY(result, (NULL != handle_ptr));

   VERIFY(result, (NULL != param_data_ptr));
   VERIFY(result, param_size >= sizeof(param_id_sh_mem_peer_client_property_config_t));

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "processing peer client property configuration command from RDEP Client");

   // the payload format aligns with the internal format. re-using the same implementation
   result = gen_cntr_shmem_cmn_process_and_apply_peer_client_property_configuration(&me_ptr->cu,
                                                                                    handle_ptr,
                                                                                    (int8_t *)param_data_ptr,
                                                                                    param_size);

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Failed to process the peer client property configuration from RDEP Client");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "sucessfully processed the peer client property configuration from RDEP Client");
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return result;
}

/* Raises the timestamp discontunity event to client if registered. This event will be following the
 * APM_EVENT_MODULE_TO_CLIENT with the event ID EVENT_ID_RD_SH_MEM_EP_TIMESTAMP_DISC_DETECTION. To
 * receive this event registered for it through APM_CMD_REGISTER_MODULE_EVENTS. */
ar_result_t gen_cntr_raise_ts_disc_event_from_rd_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                           gen_cntr_module_t *module_ptr,
                                                           bool_t             ts_valid,
                                                           int64_t            timestamp_disc_us)
{
   ar_result_t result = AR_EOK;

   /* Check if any client is registered for any event */
   if (NULL == module_ptr->cu.event_list_ptr)
   {
      return result;
   }

   /* Check if any client is registered for Timestamp Discontinuity Event*/
   spf_list_node_t *client_list_ptr;

   cu_find_client_info(me_ptr->topo.gu.log_id,
                       EVENT_ID_RD_SH_MEM_EP_TIMESTAMP_DISC_DETECTION,
                       module_ptr->cu.event_list_ptr,
                       &client_list_ptr);

   if (NULL == client_list_ptr)
   {
      return result;
   }

   /* Fill event information */
   event_id_rd_sh_mem_ep_timestamp_disc_detection_t ts_disc_event = {0};
   ts_disc_event.flags = 0;
   cu_set_bits(&ts_disc_event.flags,
               ts_valid,
               RD_SH_MEM_EP_BIT_MASK_TIMESTAMP_DISC_DURATION_VALID_FLAG,
               RD_SH_MEM_EP_SHIFT_TIMESTAMP_DISC_DURATION_VALID_FLAG);

   /* Event reports absolute timestamp difference observed in the discontinuity */
   uint64_t ts_diff_us                          = (timestamp_disc_us < 0) ? -timestamp_disc_us : timestamp_disc_us;
   ts_disc_event.timestamp_disc_duration_us_lsw = (uint32_t)ts_diff_us;
   ts_disc_event.timestamp_disc_duration_us_msw = (uint32_t)(ts_diff_us >> 32);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Module 0x%lx received timestamp discontinuty notification : ts_valid = %lu, ts_lsw: %lu, ts_msw: %lu",
                module_ptr->topo.gu.module_instance_id,
                ts_valid,
                (uint32_t)ts_diff_us,
                (uint32_t)(ts_diff_us >> 32));

   /* For each client registered for this event raise the notification */
   for (cu_client_info_t *client_info_ptr = (cu_client_info_t *)(client_list_ptr->obj_ptr); (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      gpr_packet_t *      gpr_pkt_ts_disc_ptr         = NULL;
      void *              gpr_pkt_ts_disc_payload_ptr = NULL;
      gpr_cmd_alloc_ext_t args;

      args.src_domain_id = client_info_ptr->dest_domain_id;
      args.dst_domain_id = client_info_ptr->src_domain_id;
      args.src_port      = module_ptr->topo.gu.module_instance_id;
      args.dst_port      = client_info_ptr->src_port;
      args.token         = client_info_ptr->token;
      args.opcode        = APM_EVENT_MODULE_TO_CLIENT;

      args.payload_size = sizeof(apm_module_event_t) + sizeof(event_id_rd_sh_mem_ep_timestamp_disc_detection_t);
      args.ret_packet   = &gpr_pkt_ts_disc_ptr;
      args.client_data  = 0;
      result            = __gpr_cmd_alloc_ext(&args);

      if (AR_DID_FAIL(result) || NULL == gpr_pkt_ts_disc_ptr)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Allocating timestamp discontinuity notifcation pkt to send to dsp client failed with %lu",
                      result);
         return AR_ENOMEMORY;
      }

      gpr_pkt_ts_disc_payload_ptr = GPR_PKT_GET_PAYLOAD(void, gpr_pkt_ts_disc_ptr);

      apm_module_event_t *event_payload = (apm_module_event_t *)gpr_pkt_ts_disc_payload_ptr;
      event_payload->event_id           = EVENT_ID_RD_SH_MEM_EP_TIMESTAMP_DISC_DETECTION;
      event_payload->event_payload_size = sizeof(event_id_rd_sh_mem_ep_timestamp_disc_detection_t);
      memscpy(event_payload + 1, event_payload->event_payload_size, &ts_disc_event, sizeof(ts_disc_event));

      if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_ts_disc_ptr)))
      {
         result = AR_EFAILED;
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Sending timestamp discontinuity pkt to client failed with %lu",
                      result);
         __gpr_cmd_free(gpr_pkt_ts_disc_ptr);
      }
   }

   return result;
}

ar_result_t gen_cntr_rd_sh_mem_setup_topo_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   // either the input trigger caused us to reach here, or buf recreate happened.
   if ((NULL == ext_out_port_ptr->buf.data_ptr) || (NULL != out_port_ptr->common.bufs_ptr[0].data_ptr))
   {
      // In case of signal triggered containers, if ext buffer is not available, topo buffer will be assigned to
      // internal output during process
      return result;
   }

   uint32_t empty_space_available = ext_out_port_ptr->buf.max_data_len - ext_out_port_ptr->buf.actual_data_len;

   if (empty_space_available && (empty_space_available >= out_port_ptr->common.max_buf_len_per_buf))
   {
      out_port_ptr->common.bufs_ptr[0].data_ptr =
         ext_out_port_ptr->buf.data_ptr + ext_out_port_ptr->buf.actual_data_len;
      out_port_ptr->common.bufs_ptr[0].actual_data_len = 0;
      out_port_ptr->common.bufs_ptr[0].max_data_len    = out_port_ptr->common.max_buf_len_per_buf;

      out_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

#if defined(VERBOSE_DEBUGGING) || defined(BUF_MGMT_DEBUG)
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX: Port 0x%lx, assigned ext output buffer 0x%p to internal port, max_buf_len %lu, "
                   "buf_origin%u",
                   out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   out_port_ptr->common.bufs_ptr[0].data_ptr,
                   out_port_ptr->common.bufs_ptr[0].max_data_len,
                   out_port_ptr->common.flags.buf_origin);
#endif

      /** Since rd shm is inplace assign the buffer to input here itself to optimize assignment for input.

         Rd shm is a special module which is NBLC boundary module due to requires data buf = TRUE, but also
         its inplace. Because of rdbf, Rd shm breaks NBLC chain as a result upstream module's cannot peak at
         ext out and borrow ext buffer. Thats why it needs special handling to make sure upstream (ex: encoder
         output) can borrow buffer from rd shm even though its not NBLC till ext output.

         This doesnt work for all the modules. It works for Rd shm because it only exercise requires data buf
         property to make sure module's process gets called even with partial frame, it never return's
         partially consumed frame after process. Thats why we are able to borrow buffer across Rd shm even
         through its a NBLC boundary module. And ensure borrowed buffer doesn't gets stuck in topo.
      */
      if (out_port_ptr->gu.cmn.module_ptr->input_port_list_ptr &&
          out_port_ptr->gu.cmn.module_ptr->input_port_list_ptr->ip_port_ptr->conn_out_port_ptr)
      {
         gen_topo_input_port_t *in_port_ptr =
            (gen_topo_input_port_t *)out_port_ptr->gu.cmn.module_ptr->input_port_list_ptr->ip_port_ptr;

         if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            in_port_ptr->common.bufs_ptr[0].data_ptr     = out_port_ptr->common.bufs_ptr[0].data_ptr;
            in_port_ptr->common.bufs_ptr[0].max_data_len = out_port_ptr->common.bufs_ptr[0].max_data_len;
            in_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: Port 0x%lx, borrowed ext out buffer 0x%p for the input port, buf_len %lu, "
                         "buf_origin%u",
                         in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         in_port_ptr->common.bufs_ptr[0].data_ptr,
                         in_port_ptr->common.bufs_ptr[0].max_data_len,
                         in_port_ptr->common.flags.buf_origin);
#endif
         }
      }
   }

   return result;
}
