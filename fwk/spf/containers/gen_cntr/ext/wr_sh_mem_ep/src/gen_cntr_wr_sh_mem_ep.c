/**
 * \file gen_cntr_wr_sh_mem_ep.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "gen_cntr_utils.h"
#include "apm.h"
#include "gen_cntr_cmn_sh_mem.h"

static inline bool_t gen_cntr_is_eos_opcode(uint32_t opcode)
{
   return ((DATA_CMD_WR_SH_MEM_EP_EOS == opcode));
}

static ar_result_t gen_cntr_reg_evt_wr_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                 gen_cntr_module_t *module_ptr,
                                                 topo_reg_event_t * event_cfg_payload_ptr,
                                                 bool_t             is_register);
static ar_result_t gen_cntr_handle_set_cfg_to_wr_sh_mem_ep(gen_cntr_t                        *me_ptr,
                                                           gen_cntr_module_t                 *gen_cntr_module_ptr,
                                                           uint32_t                           param_id,
                                                           int8_t                            *param_data_ptr,
                                                           uint32_t                           param_size,
                                                           spf_cfg_data_type_t                cfg_type,
                                                           cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);

static ar_result_t gen_cntr_input_data_buffer_set_up_gpr_client_v2(gen_cntr_t *            me_ptr,
                                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr);

static ar_result_t gen_cntr_check_process_eos_from_gpr_client(gen_cntr_t *            me_ptr,
                                                              gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                              uint32_t                bytes_in_ext_in);

static ar_result_t gen_cntr_process_wrep_shmem_peer_client_property_config(gen_cntr_t *  me_ptr,
                                                                           spf_handle_t *handle_ptr,
                                                                           int8_t *      param_data_ptr,
                                                                           uint32_t      param_size);

const gen_cntr_fwk_module_vtable_t wr_sh_mem_ep_vtable = {
   .set_cfg             = gen_cntr_handle_set_cfg_to_wr_sh_mem_ep,
   .reg_evt             = gen_cntr_reg_evt_wr_sh_mem_ep,
   .raise_evt           = NULL,
   .raise_ts_disc_event = NULL,
};

// clang-format off
const gen_cntr_ext_in_vtable_t gpr_client_ext_in_vtable = {
   .on_trigger               = gen_cntr_input_dataQ_trigger_gpr_client,
   .read_data                = gen_cntr_copy_gpr_client_input_to_int_buf,
   .is_data_buf              = gen_cntr_is_input_a_gpr_client_data_buffer,
   .process_pending_data_cmd = gen_cntr_process_pending_data_cmd_gpr_client,
   .free_input_buf           = gen_cntr_free_input_data_cmd_gpr_client,
   .frame_len_change_notif   = NULL,
   .setup_topo_buf           = gen_cntr_wr_shm_setup_int_port_buf
};

// clang-format on

ar_result_t gen_cntr_create_wr_sh_mem_ep(gen_cntr_t *           me_ptr,
                                         gen_topo_module_t *    module_ptr,
                                         gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   /* make sure it has only one input port. GPR client can be only one as GPR commands don't have port-id */
   VERIFY(result, 1 == module_ptr->gu.max_input_ports);

   /* Make sure that the only in port of this module is also an external port.*/
   VERIFY(result, NULL != module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr);

   /* Make sure that output port for this module exists.*/
   VERIFY(result, NULL != module_ptr->gu.output_port_list_ptr);

   /* there's no CAPI for SH MEM EP */

   spf_handle_t *gpr_cb_handle = &module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr->this_handle;

   module_ptr->flags.inplace             = TRUE;
   gen_topo_input_port_t *input_port_ptr = (gen_topo_input_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr;
   input_port_ptr->common.flags.port_has_threshold = FALSE;
   gen_topo_output_port_t *output_port_ptr = (gen_topo_output_port_t *)module_ptr->gu.output_port_list_ptr->op_port_ptr;
   output_port_ptr->common.flags.port_has_threshold = FALSE;

   TRY(result, __gpr_cmd_register(module_ptr->gu.module_instance_id, graph_init_ptr->gpr_cb_fn, gpr_cb_handle));

   // requires data buffering is FALSE by default

   gen_cntr_module_t *gen_cntr_module_ptr        = (gen_cntr_module_t *)module_ptr;
   gen_cntr_module_ptr->fwk_module_ptr->vtbl_ptr = &wr_sh_mem_ep_vtable;

   /// KPPS for WR SH MEM EP comes from ext-in port voting.

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_init_gpr_client_ext_in_port(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   // for OLC case, another vtable is swapped later once we get set-cfg
   ext_port_ptr->vtbl_ptr = &gpr_client_ext_in_vtable;

   MALLOC_MEMSET(ext_port_ptr->buf.md_buf_ptr,
                 gen_cntr_md_buf_t,
                 sizeof(gen_cntr_md_buf_t),
                 me_ptr->cu.heap_id,
                 result);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_reg_evt_wr_sh_mem_ep(gen_cntr_t *       me_ptr,
                                                 gen_cntr_module_t *module_ptr,
                                                 topo_reg_event_t * event_cfg_payload_ptr,
                                                 bool_t             is_register)
{
   ar_result_t result   = AR_EOK;
   uint32_t    event_id = event_cfg_payload_ptr->event_id;

   switch (event_id)
   {
      default:
      {
         if (NULL != me_ptr->cu.offload_info_ptr)
         {
            /* handle the following events for MDF
               OFFLOAD_EVENT_ID_DOWNSTREAM_PEER_PORT_PROPERTY:
            */
            result = gen_cntr_offload_reg_evt_wr_sh_mem_ep(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);
         }
         else
         {

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Unsupported event-id 0x%lx", event_id);
            result = AR_EUNSUPPORTED;
         }
      }
   }

   return result;
}

/**
 * this module doesn't support PARAM_ID_MODULE_ENABLE
 */
static ar_result_t gen_cntr_handle_set_cfg_to_wr_sh_mem_ep(gen_cntr_t                        *me_ptr,
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
      case PARAM_ID_MEDIA_FORMAT:
      {
         if (gen_topo_is_module_sg_started(module_ptr))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         " input media format control cmd received in "
                         "start state.");
            result = AR_EUNSUPPORTED;
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " processing input media format control cmd");

            gen_cntr_ext_in_port_t *ext_in_port_ptr =
               (gen_cntr_ext_in_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

            VERIFY(result, param_size >= sizeof(media_format_t));
            media_format_t *media_fmt_ptr = (media_format_t *)(param_data_ptr);
            result                        = gen_cntr_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(me_ptr,
                                                                                ext_in_port_ptr,
                                                                                media_fmt_ptr,
                                                                                FALSE /*is_data_path*/);
         }
         break;
      }
      case PARAM_ID_SH_MEM_PEER_CLIENT_PROPERTY_CONFIG:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Processing WR_EP shmem peer port config update from WREP Client");

         // has only one port.
         gen_cntr_ext_in_port_t *ext_in_port_ptr =
            (gen_cntr_ext_in_port_t *)module_ptr->gu.input_port_list_ptr->ip_port_ptr->ext_in_port_ptr;

         TRY(result,
             gen_cntr_process_wrep_shmem_peer_client_property_config(me_ptr,
                                                                     &ext_in_port_ptr->gu.this_handle,
                                                                     param_data_ptr,
                                                                     param_size));

         break;
      }
      default:
      {
         if (NULL != me_ptr->cu.offload_info_ptr)
         {
            /* handle the following configuration for MDF
               PARAM_ID_WR_EP_CLIENT_CFG:
               PARAM_ID_PEER_PORT_PROPERTY_UPDATE
               PARAM_ID_UPSTREAM_STOPPED
            */
            result = gen_cntr_offload_handle_set_cfg_to_wr_sh_mem_ep(me_ptr,
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

/**
 * peeks input queue to check if next message is EOS. if so set's last buffer flag
 */
static ar_result_t gen_cntr_peek_and_pop_eos(gen_cntr_t *            me_ptr,
                                             gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                             uint32_t                bytes_in_ext_buf_b4)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_msg_t *input_data_q_msg_ptr;
   result =
      posal_queue_peek_front(ext_in_port_ptr->gu.this_handle.q_ptr, (posal_queue_element_t **)(&input_data_q_msg_ptr));

   if (AR_DID_FAIL(result) || (NULL == input_data_q_msg_ptr))
   {
      return result;
   }

   /* Peek meta data for resync marker. If resync marker is not set, drop the input buffer */
   if (SPF_MSG_CMD_GPR == input_data_q_msg_ptr->msg_opcode)
   {
      gpr_packet_t *packet_ptr = (gpr_packet_t *)(input_data_q_msg_ptr->payload_ptr);

      if (gen_cntr_is_eos_opcode(packet_ptr->opcode))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      " Next message in the queue is EOS (module, port) 0x%08lX, 0x%lx. Popping it right away.",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         // Take next msg off the q
         TRY(result, gen_cntr_get_input_data_cmd(me_ptr, ext_in_port_ptr));

         VERIFY(result, (NULL != ext_in_port_ptr->cu.input_data_q_msg.payload_ptr));

         {
            // get the payload of the msg
            packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

            VERIFY(result, gen_cntr_is_eos_opcode(packet_ptr->opcode));

            // for any EOS metadata that was popped now, the offset is bytes_in_ext_buf_b4.
            // since we are popping new MD while we are

            gen_cntr_check_process_eos_from_gpr_client(me_ptr, ext_in_port_ptr, bytes_in_ext_buf_b4);
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * returns whether the input buffer being held is an data buffer (not EOS, Media fmt etc)
 */
bool_t gen_cntr_is_input_a_gpr_client_data_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t is_data_cmd = FALSE;
   // if no buffer held, then return FALSE
   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return FALSE;
   }

   if (SPF_MSG_CMD_GPR == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      if ((DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2 == packet_ptr->opcode))
      {
         is_data_cmd = TRUE;
      }
   }

   return is_data_cmd;
}

/*
 *
 * for decoder: this func is called as data is copied as is to the internal buffer.
 * for encoder: gen_cntr_copy_peer_cntr_input_to_int_buf is called and data is copied after format conversion.
 *  * this is because, encoder first module input is as per configured media fmt. decoder's first module input is as per
 * input media fmt.
 *
 * At input, bytes_copied_ptr contains max inputs that we can copy.
 */
ar_result_t gen_cntr_copy_gpr_client_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                      gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                      uint32_t *              bytes_copied_per_buf_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   uint32_t bytes_in_ext_buf_b4 = ext_in_port_ptr->buf.actual_data_len;

   if (0 != ext_in_port_ptr->buf.actual_data_len)
   {

      topo_buf_t *module_bufs_ptr = in_port_ptr->common.bufs_ptr;

#if defined(SAFE_MODE) || defined(VERBOSE_DEBUGGING)
      if (in_port_ptr->common.sdata.bufs_num > 1)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "GPR client ext-input is assumed to be only one buffer. currently %lu",
                      in_port_ptr->common.sdata.bufs_num);
      }
#endif
      // buffer is assumed to be interleaved.
      uint32_t max_size = module_bufs_ptr[0].max_data_len;

      uint32_t empty_inp_bytes = max_size - module_bufs_ptr[0].actual_data_len;
      uint32_t bytes_to_copy   = 0;

      // client input is assumed to be interleaved.
      // bytes to be copied = minimum of left over client data or left over empty space in internal buffer
      bytes_to_copy = MIN(ext_in_port_ptr->buf.actual_data_len, empty_inp_bytes);
      bytes_to_copy = MIN(*bytes_copied_per_buf_ptr, bytes_to_copy);

      // since we have only one buf, bytes_to_copy is for all channels as well (if PCM).
      if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->cu.media_fmt.data_format))
      {
         bytes_to_copy =
            topo_truncate_bytes_to_intgeral_samples_on_all_ch(bytes_to_copy, &ext_in_port_ptr->cu.media_fmt);
      }

      /** some data was already copied in previous calls. */
      uint32_t buffer_offset_bytes = (ext_in_port_ptr->buf.max_data_len - ext_in_port_ptr->buf.actual_data_len);
      int8_t * src_ptr             = ext_in_port_ptr->buf.data_ptr + buffer_offset_bytes;

      if (bytes_to_copy)
      {
         if ((0 < buffer_offset_bytes) && (SPF_IS_RAW_COMPR_DATA_FMT(in_port_ptr->common.media_fmt_ptr->data_format)))
         {
            // For raw compressed, if some data is already copied in previous call, then the TS would
            // this buffer copy would set as continue. Also the TS value is set as zero and validity flag is reset.
            if (in_port_ptr->ts_to_sync.ts_valid)
            {
               in_port_ptr->common.sdata.timestamp                = 0;
               in_port_ptr->common.sdata.flags.is_timestamp_valid = FALSE;
               in_port_ptr->common.sdata.flags.ts_continue        = TRUE;
            }
         }

#ifdef MANDATORY_MEM_OP // When TOPO_MEMSCPY_NO_RET points to empty macro, MANDATORY_MEM_OP is defined so that decoding
                        // can happen.
         memscpy((module_bufs_ptr[0].data_ptr + module_bufs_ptr[0].actual_data_len),
                 empty_inp_bytes,
                 src_ptr,
                 bytes_to_copy);
#endif
         int8_t *dest_ptr = (module_bufs_ptr[0].data_ptr + module_bufs_ptr[0].actual_data_len);

         // src and dst ptrs are same if the ext buffer is borrowed by the internal input ports for processing
         if (src_ptr != dest_ptr)
         {
            TOPO_MEMSCPY_NO_RET(dest_ptr,
                                empty_inp_bytes,
                                src_ptr,
                                bytes_to_copy,
                                me_ptr->topo.gu.log_id,
                                "E2I: WR_EP: 0x%lX",
                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
         }

         // book keeping of remaining bytes in client and empty bytes in internal buffer
         ext_in_port_ptr->buf.actual_data_len -= bytes_to_copy;
         module_bufs_ptr[0].actual_data_len += bytes_to_copy;
      }

      *bytes_copied_per_buf_ptr = bytes_to_copy;

      (void)src_ptr;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "gen_cntr_copy_gpr_client_input_to_int_buf bytes copied %lu",
                   bytes_to_copy);
#endif
   }
   else
   {
      *bytes_copied_per_buf_ptr = 0;
   }

   // if we have copied all the data from 'data' (not EOS) buffer, release it
   if (gen_cntr_is_input_a_gpr_client_data_buffer(me_ptr, ext_in_port_ptr) &&
       (0 == ext_in_port_ptr->buf.actual_data_len))
   {
      // free ext buffer if not held at the internal input port, if its held it will be freed after process
      // in gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports()
      if (FALSE == ((GEN_TOPO_BUF_ORIGIN_EXT_BUF == in_port_ptr->common.flags.buf_origin) &&
                    in_port_ptr->common.bufs_ptr[0].data_ptr && in_port_ptr->common.bufs_ptr[0].actual_data_len))
      {
         result = gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }
#ifdef VERBOSE_DEBUGGING
      else // print debug msg that buffer is being held and not released at this point
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      " Module 0x%lX: Port 0x%lx, Ext buffer %p (%lu of %lu) is held by internal input will be freed "
                      "after processing ",
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->common.bufs_ptr[0].data_ptr,
                      in_port_ptr->common.bufs_ptr[0].actual_data_len,
                      in_port_ptr->common.bufs_ptr[0].max_data_len);
      }
#endif

      // if an EOS exists in the queue, pop it now while we still have data from last buffer. This helps gapless cases
      // to remove any trailing zeros in last buffer.
      // if we don't pop EOS now, then gapless module won't know about last buffer.
      gen_cntr_peek_and_pop_eos(me_ptr, ext_in_port_ptr, bytes_in_ext_buf_b4);
   }

   return result;
}

// Function to parse the meta-data from the write buffers and create meta-data nodes
// and add it to the external input port
static ar_result_t gen_cntr_populate_metadata_from_wr_client_buffer(gen_cntr_t *            me_ptr,
                                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result                = AR_EOK;
   uint32_t    md_buffer_read_offset = 0;
   uint32_t    num_md_ele_cnt        = 0;
   uint32_t    md_header_size        = sizeof(metadata_header_t);

   capi_heap_id_t           heap_info;
   module_cmn_md_flags_t    flags;
   module_cmn_md_tracking_t tracking_info;

   int8_t *           md_ptr             = NULL;
   module_cmn_md_t *  new_md_ptr         = NULL;
   metadata_header_t *md_data_header_ptr = NULL;
   gen_topo_module_t *module_ptr         = NULL;
   gpr_packet_t *     packet_ptr         = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   md_ptr            = ext_in_port_ptr->buf.md_buf_ptr->data_ptr;
   heap_info.heap_id = (uint32_t)me_ptr->cu.heap_id;

   tracking_info.tracking_payload.flags.word    = 0; // set the flags to zero
   tracking_info.tracking_payload.src_domain_id = packet_ptr->dst_domain_id;
   tracking_info.tracking_payload.dst_domain_id = packet_ptr->src_domain_id;
   tracking_info.tracking_payload.src_port      = packet_ptr->dst_port;
   tracking_info.tracking_payload.dest_port     = packet_ptr->src_port;
   tracking_info.heap_info.heap_id              = heap_info.heap_id;

   // For OLC as client, replace the source with parent container ID (OLC).
   if (me_ptr->cu.offload_info_ptr)
   {
      tracking_info.tracking_payload.dest_port                  = me_ptr->cu.offload_info_ptr->client_id;
      tracking_info.tracking_payload.flags.enable_cloning_event = MODULE_CMN_MD_TRACKING_ENABLE_CLONING_EVENT;
   }

   if ((NULL != md_ptr) && (0 < ext_in_port_ptr->buf.md_buf_ptr->max_data_len))
   {
      // Get the metadata handler
      intf_extn_param_id_metadata_handler_t handler;
      module_ptr = (gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr;

      gen_topo_populate_metadata_extn_vtable(module_ptr, &handler);

      while ((md_buffer_read_offset + md_header_size) <= ext_in_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         md_data_header_ptr = (metadata_header_t *)(md_ptr + md_buffer_read_offset);
         gen_topo_convert_client_md_flag_to_int_md_flags(md_data_header_ptr->flags, &flags);
         tracking_info.tracking_payload.token_lsw = md_data_header_ptr->token_lsw;
         tracking_info.tracking_payload.token_msw = md_data_header_ptr->token_msw;

         if (MODULE_CMN_MD_ID_EOS != md_data_header_ptr->metadata_id)
         {
            tracking_info.tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_GENERIC_EVENT;
            // create metadata with tracking would create the node and add it to the list
            result = handler.metadata_create_with_tracking(handler.context_ptr,
                                                           &ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr,
                                                           md_data_header_ptr->payload_size,
                                                           heap_info,
                                                           md_data_header_ptr->metadata_id,
                                                           flags,
                                                           &tracking_info,
                                                           &new_md_ptr);
         }
         else
         {
            module_cmn_md_eos_t *eos_metadata_ptr = (module_cmn_md_eos_t *)(md_data_header_ptr + 1);

            if (me_ptr->cu.offload_info_ptr)
            {
               tracking_info.tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_GENERIC_EVENT;
            }
            else
            {
               tracking_info.tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_CUSTOM_EVENT;
            }

            result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                                  (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr,
                                                  ext_in_port_ptr->cu.id,
                                                  me_ptr->cu.heap_id,
                                                  &ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr,
                                                  &flags,
                                                  &tracking_info,
                                                  &eos_metadata_ptr->flags,
                                                  0,
                                                  NULL);

            if (AR_EOK == result)
            {
               me_ptr->total_flush_eos_stuck++;
               module_cmn_md_list_t *new_md_list_ptr = NULL;
               result = spf_list_get_tail_node((spf_list_node_t *)(ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr),
                                               (spf_list_node_t **)&new_md_list_ptr);

               if (new_md_list_ptr)
               {
                  new_md_ptr = new_md_list_ptr->obj_ptr;
               }
               else
               {
                  result = AR_EFAILED;
               }

               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_MED_PRIO,
                            "MD_DBG: gen_cntr received EOS cmd from Client at Module, port id (0x%lX, 0x%lx). "
                            "is_flushing %u, node_ptr 0x%p , offset %lu",
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                            eos_metadata_ptr->flags.is_flushing_eos,
                            new_md_ptr,
                            md_data_header_ptr->offset);
            }
         }

         if (AR_EOK != result)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "failed to create metadata node while inserting metadata from "
                         "write client buffer, md payload size %lu, md_id (0x%lx)",
                         md_data_header_ptr->payload_size,
                         md_data_header_ptr->metadata_id);

            // need to destroy all the metadata that was inserted in the input port
            // drop any metadata that came with the buffer
            gen_topo_destroy_all_metadata(me_ptr->topo.gu.log_id,
                                          (void *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr,
                                          &ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr,
                                          TRUE /* is dropped*/);
            ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr = NULL;
            break;
         }

         // fill the offset
         new_md_ptr->offset = md_data_header_ptr->offset;

         //#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Updated metadata with offset received from client, MD ID (0x%lx) md offset %d flags (0x%lx)",
                      new_md_ptr->metadata_id,
                      new_md_ptr->offset,
                      (uint32_t)new_md_ptr->metadata_flag.word);
         //#endif

         if (MODULE_CMN_MD_ID_EOS != md_data_header_ptr->metadata_id)
         {
            // copy the metadata payload
            // copy the metadata payload
            new_md_ptr->actual_size = memscpy(new_md_ptr->metadata_buf,
                                              new_md_ptr->max_size,
                                              (int8_t *)(md_data_header_ptr + 1),
                                              md_data_header_ptr->payload_size);
         }

         // update the loop control elements
         md_buffer_read_offset += (md_header_size + md_data_header_ptr->payload_size);
         num_md_ele_cnt++;
         new_md_ptr = NULL;
      }

      if (AR_EOK != result)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "failed to process the metadata from the input buffer ");
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      "Metadata from the input buffer inserted to temp metadata list. "
                      "md_buffer_size %lu num_md_element %lu",
                      ext_in_port_ptr->buf.md_buf_ptr->max_data_len,
                      num_md_ele_cnt);
      }
   }
   return result;
}

ar_result_t gen_cntr_handle_flush_input_buffer_metadata(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result                = AR_EOK;
   uint32_t    md_header_size        = sizeof(metadata_header_t);
   uint32_t    md_buffer_read_offset = 0;

   gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   data_cmd_wr_sh_mem_ep_data_buffer_v2_t *pDataPayload =
      GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   if ((0 == pDataPayload->md_buf_size))
   {
      return AR_EOK;
   }

   if ((pDataPayload->data_buf_addr_lsw & CACHE_ALIGNMENT))
   {
      ext_in_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // md status is marked as failure
      return AR_EBADPARAM;
   }

   uint64_t md_buf_virtual_addr   = 0;
   uint32_t wr_shm_md_buffer_size = pDataPayload->md_buf_size;
   if ((wr_shm_md_buffer_size > 0))
   {
      if (pDataPayload->md_mem_map_handle)
      {
         if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                      pDataPayload->md_mem_map_handle,
                                                                                      pDataPayload->md_buf_addr_lsw,
                                                                                      pDataPayload->md_buf_addr_msw,
                                                                                      wr_shm_md_buffer_size,
                                                                                      TRUE, // is_ref_counted
                                                                                      &md_buf_virtual_addr)))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Phy to Virt Failed(paddr,vaddr)-->(%lx%lx,%lx) for metadata buffer address\n",
                         pDataPayload->md_buf_addr_lsw,
                         pDataPayload->md_buf_addr_msw,
                         md_buf_virtual_addr);

            ext_in_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // md status is marked as failure
            return AR_EBADPARAM;
         }
         ext_in_port_ptr->buf.md_buf_ptr->data_ptr = (int8_t *)md_buf_virtual_addr;

         // invalidate the cache before reading from shared memory.
         posal_cache_invalidate_v2(&ext_in_port_ptr->buf.md_buf_ptr->data_ptr, wr_shm_md_buffer_size);
      }
      else
      {
         ext_in_port_ptr->buf.md_buf_ptr->data_ptr =
            (int8_t *)(pDataPayload) + (sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t));
      }
      ext_in_port_ptr->buf.md_buf_ptr->mem_map_handle = pDataPayload->md_mem_map_handle;
      ext_in_port_ptr->buf.md_buf_ptr->max_data_len   = pDataPayload->md_buf_size;
   }
   else
   {
      memset(ext_in_port_ptr->buf.md_buf_ptr, 0, sizeof(gen_cntr_md_buf_t));
      return AR_EOK;
   }

   int8_t *                 md_ptr             = ext_in_port_ptr->buf.md_buf_ptr->data_ptr;
   metadata_header_t *      md_data_header_ptr = NULL;
   module_cmn_md_flags_t    flags;
   module_cmn_md_tracking_t tracking_info;
   tracking_info.tracking_payload.src_domain_id = packet_ptr->dst_domain_id;
   tracking_info.tracking_payload.dst_domain_id = packet_ptr->src_domain_id;
   tracking_info.tracking_payload.src_port      = packet_ptr->dst_port;
   tracking_info.tracking_payload.dest_port     = packet_ptr->src_port;

   // For OLC as client, replace the source with parent container ID (OLC).
   if (me_ptr->cu.offload_info_ptr)
   {
      tracking_info.tracking_payload.dest_port                  = me_ptr->cu.offload_info_ptr->client_id;
      tracking_info.tracking_payload.flags.enable_cloning_event = MODULE_CMN_MD_TRACKING_ENABLE_CLONING_EVENT;
   }

   if ((NULL != md_ptr) && (0 < ext_in_port_ptr->buf.md_buf_ptr->max_data_len))
   {
      while ((md_buffer_read_offset + md_header_size) <= ext_in_port_ptr->buf.md_buf_ptr->max_data_len)
      {
         md_data_header_ptr = (metadata_header_t *)(md_ptr + md_buffer_read_offset);
         gen_topo_convert_client_md_flag_to_int_md_flags(md_data_header_ptr->flags, &flags);
         tracking_info.tracking_payload.token_lsw = md_data_header_ptr->token_lsw;
         tracking_info.tracking_payload.token_msw = md_data_header_ptr->token_msw;

         if (cu_get_bits(md_data_header_ptr->flags,
                         MD_HEADER_FLAGS_BIT_MASK_TRACKING_CONFIG,
                         MD_HEADER_FLAGS_SHIFT_TRACKING_CONFIG_FLAG))
         {
            gen_topo_drop_md(me_ptr->cu.gu_ptr->log_id,
                             &tracking_info.tracking_payload,
                             md_data_header_ptr->metadata_id,
                             flags,
                             tracking_info.tracking_payload.dest_port,
                             FALSE,
                             NULL);
         }

         // update the loop control elements
         md_buffer_read_offset += (md_header_size + md_data_header_ptr->payload_size);
      }
   }

   return result;
}

static ar_result_t gen_cntr_input_data_buffer_set_up_gpr_client_v2(gen_cntr_t *            me_ptr,
                                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

   data_cmd_wr_sh_mem_ep_data_buffer_v2_t *pDataPayload =
      GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   // If payload of input data msg is not valid, return
   if ((NULL == pDataPayload))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Cannot get GPR data payload");
      // return the buffer and wait for new ones
      return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
   }

   ext_in_port_ptr->buf.md_buf_ptr->is_md_parsed_from_input_buf = FALSE;

   // if input data buffer and meta-data buffer size is zero, return it immediately
   if ((0 == pDataPayload->data_buf_size) && (0 == pDataPayload->md_buf_size))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Returning an input buffer with data and metadata of zero size!");
      return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
   }

   // if input data buffer is not cache aligned return it
   if (pDataPayload->data_buf_addr_lsw & CACHE_ALIGNMENT)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   " Input data buffer not %lu byte aligned, returning it!",
                   CACHE_ALIGNMENT + 1);
      return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
   }

   // if input metadata buffer is OOB and is not cache aligned return it
   if (pDataPayload->md_mem_map_handle)
   {
      if (pDataPayload->md_buf_addr_lsw & CACHE_ALIGNMENT)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      " Input metadata buffer not %lu byte aligned, returning it!",
                      CACHE_ALIGNMENT + 1);
         ext_in_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // md status is marked as failure
         return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }
   }
   else // (0 == pDataPayload->md_mem_map_handle)
   {
      uint32_t pkt_size           = GPR_PKT_GET_PAYLOAD_BYTE_SIZE(packet_ptr->header);
      uint32_t total_payload_size = ((sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t)) + pDataPayload->md_buf_size);
      if (pkt_size < total_payload_size)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Input packet size %lu less than required size %lu!",
                      pkt_size,
                      total_payload_size);
         ext_in_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // md status is marked as failure
         return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }
   }

   uint64_t data_buf_virtual_addr   = 0;
   uint32_t wr_shm_data_buffer_size = pDataPayload->data_buf_size;
   if (wr_shm_data_buffer_size > 0)
   {
      // if input buffer do not contain integer PCM samples per channel, return it immediately with error code
      if (SPF_IS_PACKETIZED_OR_PCM(ext_in_port_ptr->cu.media_fmt.data_format))
      {
         uint32_t unit_size = ext_in_port_ptr->cu.media_fmt.pcm.num_channels *
                              TOPO_BITS_TO_BYTES(ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample);

         if (pDataPayload->data_buf_size % unit_size)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Returning an input buffer that do not contain the same "
                         "PCM samples on all channels,buf size %d, unit size %d",
                         pDataPayload->data_buf_size,
                         unit_size);
            return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
         }
      }

      if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                   pDataPayload->data_mem_map_handle,
                                                                                   pDataPayload->data_buf_addr_lsw,
                                                                                   pDataPayload->data_buf_addr_msw,
                                                                                   wr_shm_data_buffer_size,
                                                                                   TRUE, // is_ref_counted
                                                                                   &data_buf_virtual_addr)))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Phy to Virt Failed(paddr,vaddr)-->(%lx%lx,%lx) for data buffer address\n",
                      pDataPayload->data_buf_addr_lsw,
                      pDataPayload->data_buf_addr_msw,
                      data_buf_virtual_addr);

         return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
      }

      //#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Received a wr v2 buffer of size %d", wr_shm_data_buffer_size);
      //#endif
   }

   uint64_t md_buf_virtual_addr   = 0;
   uint32_t wr_shm_md_buffer_size = pDataPayload->md_buf_size;
   if (wr_shm_md_buffer_size > 0)
   {
      if (pDataPayload->md_mem_map_handle)
      {
         if (AR_DID_FAIL(result = posal_memorymap_get_virtual_addr_from_shm_handle_v2(apm_get_mem_map_client(),
                                                                                      pDataPayload->md_mem_map_handle,
                                                                                      pDataPayload->md_buf_addr_lsw,
                                                                                      pDataPayload->md_buf_addr_msw,
                                                                                      wr_shm_md_buffer_size,
                                                                                      TRUE, // is_ref_counted
                                                                                      &md_buf_virtual_addr)))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Phy to Virt Failed(paddr,vaddr)-->(%lx%lx,%lx) for metadata buffer address\n",
                         pDataPayload->md_buf_addr_lsw,
                         pDataPayload->md_buf_addr_msw,
                         md_buf_virtual_addr);

            ext_in_port_ptr->buf.md_buf_ptr->status = AR_EBADPARAM; // md status is marked as failure
            return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
         }
         ext_in_port_ptr->buf.md_buf_ptr->data_ptr = (int8_t *)md_buf_virtual_addr;

         // invalidate the cache before reading from shared memory.
         posal_cache_invalidate_v2(&ext_in_port_ptr->buf.md_buf_ptr->data_ptr, wr_shm_md_buffer_size);
      }
      else
      {
         ext_in_port_ptr->buf.md_buf_ptr->data_ptr =
            (int8_t *)(pDataPayload) + (sizeof(data_cmd_wr_sh_mem_ep_data_buffer_v2_t));
      }
      ext_in_port_ptr->buf.md_buf_ptr->mem_map_handle = pDataPayload->md_mem_map_handle;
      ext_in_port_ptr->buf.md_buf_ptr->max_data_len   = pDataPayload->md_buf_size;
   }
   else
   {
      memset(ext_in_port_ptr->buf.md_buf_ptr, 0, sizeof(gen_cntr_md_buf_t));
   }

   // Populate the metadata from the input buffer to the external input port internal md list
   if (AR_EOK != (result = gen_cntr_populate_metadata_from_wr_client_buffer(me_ptr, ext_in_port_ptr)))
   {
      ext_in_port_ptr->buf.md_buf_ptr->status = result;
      return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
   }
   ext_in_port_ptr->buf.md_buf_ptr->is_md_parsed_from_input_buf = TRUE;

   ext_in_port_ptr->buf.data_ptr        = (data_buf_virtual_addr != 0) ? (int8_t *)data_buf_virtual_addr : NULL;
   ext_in_port_ptr->buf.mem_map_handle  = pDataPayload->data_mem_map_handle;
   ext_in_port_ptr->buf.actual_data_len = pDataPayload->data_buf_size;
   ext_in_port_ptr->buf.max_data_len    = pDataPayload->data_buf_size;

   // invalidate the cache before reading from shared memory.
   posal_cache_invalidate_v2(&ext_in_port_ptr->buf.data_ptr, wr_shm_data_buffer_size);

   // get the end of frame flag
   ext_in_port_ptr->flags.eof =
      cu_get_bits(pDataPayload->flags, WR_SH_MEM_EP_BIT_MASK_EOF_FLAG, WR_SH_MEM_EP_SHIFT_EOF_FLAG);

   // set EoF flag to False in the CAPI, since only the last copy from input buffer
   // assures an integral number of frames in the decoder internal buffer
   in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
   in_port_ptr->common.sdata.flags.marker_eos   = FALSE;
   in_port_ptr->common.sdata.flags.end_of_frame = FALSE;

   // see comments in gen_cntr_input_data_set_up_peer_cntr
   uint32_t end_offset = 0;
   // deinterleaved raw compressed not supported here.
   uint32_t bytes_across_all_ch = gen_topo_get_total_actual_len(&in_port_ptr->common);
   gen_topo_do_md_offset_math(me_ptr->topo.gu.log_id,
                              &end_offset,
                              bytes_across_all_ch + ext_in_port_ptr->buf.actual_data_len,
                              in_port_ptr->common.media_fmt_ptr,
                              TRUE /* need to add */);

   bool_t dst_has_flush_eos = FALSE, dst_has_dfg = FALSE;

   gen_topo_md_list_modify_md_when_new_data_arrives(&me_ptr->topo,
                                                    (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                    &ext_in_port_ptr->md_list_ptr,
                                                    end_offset,
                                                    &dst_has_flush_eos,
                                                    &dst_has_dfg);

   if (AR_EOK == result)
   {
      spf_list_merge_lists((spf_list_node_t **)&ext_in_port_ptr->md_list_ptr,
                           (spf_list_node_t **)&ext_in_port_ptr->buf.md_buf_ptr->md_list_ptr);

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "Metadata from temp md list moved to ext in port md list md_buffer_size %lu",
                   ext_in_port_ptr->buf.md_buf_ptr->max_data_len);
   }

   gen_cntr_copy_timestamp_from_input(me_ptr, ext_in_port_ptr);

   gen_cntr_handle_ext_in_data_flow_begin(me_ptr, ext_in_port_ptr);

   return result;
}

ar_result_t gen_cntr_input_dataQ_trigger_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t      gpr_opcode = 0;
   gpr_packet_t *packet_ptr;

   // Take next msg off the q
   TRY(result, gen_cntr_get_input_data_cmd(me_ptr, ext_in_port_ptr));

   if (NULL != ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      // get the payload of the msg
      packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

      gpr_opcode = packet_ptr->opcode;
   }

   switch (gpr_opcode)
   {
      // Input data(bitstream) + metadata command
      case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
      {
         TRY(result, gen_cntr_input_data_buffer_set_up_gpr_client_v2(me_ptr, ext_in_port_ptr)); // data_v2
         break;
      }

      case DATA_CMD_WR_SH_MEM_EP_EOS:
      {
         // any EOS code must be inside gen_cntr_check_process_eos_from_gpr_client, due to call from peek_and_pop
         TRY(result,
             gen_cntr_check_process_eos_from_gpr_client(me_ptr, ext_in_port_ptr, ext_in_port_ptr->buf.actual_data_len));

         break;
      }
      case DATA_CMD_WR_SH_MEM_EP_PEER_PORT_PROPERTY:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      "Processing US state config update from OLC at Module, port id (0x%lX, 0x%lx)",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         result = gen_cntr_offload_process_data_cmd_port_property_cfg(me_ptr, ext_in_port_ptr);
         result = gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, result, FALSE);
         break;
      }

      case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "GEN_CNTR input media format update cmd from gpr client");

         gen_cntr_check_process_input_media_fmt_data_cmd(me_ptr, ext_in_port_ptr);
         break;
      }

      default:
      {
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Unexpected opCode for data write 0x%lx", gpr_opcode);
            result = AR_EUNSUPPORTED;
            result |= gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EUNSUPPORTED, FALSE);
         }
      }
   } /* switch (gpr_opcode) */

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

static ar_result_t gen_cntr_parse_inp_pcm_media_fmt_from_gpr_client(gen_cntr_t *            me_ptr,
                                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                    media_format_t *        media_fmt_ptr,
                                                                    bool_t                  is_data_path,
                                                                    topo_media_fmt_t *      local_media_fmt_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   switch (media_fmt_ptr->fmt_id)
   {
      case MEDIA_FMT_ID_PCM:
      {
         payload_media_fmt_pcm_t *pcm_fmt_ptr;
         VERIFY(result, media_fmt_ptr->payload_size >= sizeof(payload_media_fmt_pcm_t));

         pcm_fmt_ptr                 = (payload_media_fmt_pcm_t *)(media_fmt_ptr + 1);
         local_media_fmt_ptr->fmt_id = media_fmt_ptr->fmt_id;

         switch (media_fmt_ptr->data_format)
         {
            case DATA_FORMAT_FIXED_POINT:
            {
               local_media_fmt_ptr->data_format = SPF_FIXED_POINT;
               TRY(result, gen_topo_validate_client_pcm_media_format(pcm_fmt_ptr));

               local_media_fmt_ptr->pcm.q_factor = pcm_fmt_ptr->q_factor;
               break;
            }
            case DATA_FORMAT_FLOATING_POINT:
            {
               local_media_fmt_ptr->data_format = SPF_FLOATING_POINT;
               TRY(result, gen_topo_validate_client_pcm_float_media_format(pcm_fmt_ptr));

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
               break;
            }
         }

         local_media_fmt_ptr->pcm.bits_per_sample = pcm_fmt_ptr->bits_per_sample;
         local_media_fmt_ptr->pcm.bit_width       = pcm_fmt_ptr->bit_width;
         local_media_fmt_ptr->pcm.endianness =
            gen_topo_convert_public_endianness_to_gen_topo_endianness(pcm_fmt_ptr->endianness);
         VERIFY(result, (TOPO_UNKONWN_ENDIAN != local_media_fmt_ptr->pcm.endianness));
         local_media_fmt_ptr->pcm.sample_rate  = pcm_fmt_ptr->sample_rate;
         local_media_fmt_ptr->pcm.num_channels = pcm_fmt_ptr->num_channels;
         if ((INVALID_VALUE == pcm_fmt_ptr->interleaved) || (PCM_INTERLEAVED == pcm_fmt_ptr->interleaved))
         {
            local_media_fmt_ptr->pcm.interleaving = TOPO_INTERLEAVED;
         }
         else
         {
            THROW(result, AR_EBADPARAM);
         }
         uint8_t *channel_mapping = (uint8_t *)(pcm_fmt_ptr + 1);
         for (uint32_t ch = 0; ch < local_media_fmt_ptr->pcm.num_channels; ch++)
         {
            local_media_fmt_ptr->pcm.chan_map[ch] = channel_mapping[ch];
         }
         break;
      }
      default:
      {
         result = gen_cntr_offload_parse_inp_pcm_media_fmt_from_gpr_client(me_ptr,
                                                                           ext_in_port_ptr,
                                                                           media_fmt_ptr,
                                                                           local_media_fmt_ptr);
         if (AR_EUNSUPPORTED == result)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Unsupported media format 0x%lX",
                         media_fmt_ptr->fmt_id);
            THROW(result, AR_EUNSUPPORTED);
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * Any media fmt received in stop state is cached.
 * It's applied only if port is in started/prepared states
 */
ar_result_t gen_cntr_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(gen_cntr_t *            me_ptr,
                                                                       gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                       media_format_t *        media_fmt_ptr,
                                                                       bool_t                  is_data_path)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   topo_media_fmt_t local_media_fmt;
   memset(&local_media_fmt, 0, sizeof(topo_media_fmt_t));

   switch (media_fmt_ptr->data_format)
   {
      case DATA_FORMAT_FIXED_POINT:
      case DATA_FORMAT_FLOATING_POINT:
      {
         TRY(result,
             gen_cntr_parse_inp_pcm_media_fmt_from_gpr_client(me_ptr,
                                                              ext_in_port_ptr,
                                                              media_fmt_ptr,
                                                              is_data_path,
                                                              &local_media_fmt));
         break;
      }
      case DATA_FORMAT_RAW_COMPRESSED:
      {
         TRY(result,
             tu_capi_create_raw_compr_med_fmt(me_ptr->topo.gu.log_id,
                                              (uint8_t *)(media_fmt_ptr + 1),
                                              media_fmt_ptr->payload_size,
                                              media_fmt_ptr->fmt_id,
                                              &local_media_fmt,
                                              TRUE /*with_header*/,
                                              me_ptr->cu.heap_id));

         // in order to reduce malloc overhead the struct of buf_ptr includes capi_set_get_media_format_t &
         // capi_raw_compressed_data_format_t at the top.
         local_media_fmt.data_format = SPF_RAW_COMPRESSED;
         local_media_fmt.fmt_id      = media_fmt_ptr->fmt_id;

         break;
      }
      case DATA_FORMAT_IEC61937_PACKETIZED:
      case DATA_FORMAT_IEC60958_PACKETIZED:
      {
         payload_data_fmt_iec_packetized_t *data_fmt_ptr = (payload_data_fmt_iec_packetized_t *)(media_fmt_ptr + 1);

         if (DATA_FORMAT_IEC60958_PACKETIZED == media_fmt_ptr->data_format)
         {
            local_media_fmt.data_format         = SPF_IEC60958_PACKETIZED;
            local_media_fmt.pcm.bits_per_sample = 32;
            local_media_fmt.pcm.bit_width       = 32;
            local_media_fmt.pcm.q_factor        = 31;
         }
         else
         {
            local_media_fmt.data_format         = SPF_IEC61937_PACKETIZED;
            local_media_fmt.pcm.bits_per_sample = 16;
            local_media_fmt.pcm.bit_width       = 16;
            local_media_fmt.pcm.q_factor        = 15;
         }

         local_media_fmt.fmt_id           = media_fmt_ptr->fmt_id;
         local_media_fmt.pcm.sample_rate  = data_fmt_ptr->sample_rate;
         local_media_fmt.pcm.num_channels = data_fmt_ptr->num_channels;
         local_media_fmt.pcm.interleaving = TOPO_INTERLEAVED;
         local_media_fmt.pcm.endianness   = TOPO_LITTLE_ENDIAN;
         break;
      }
      case DATA_FORMAT_GENERIC_COMPRESSED:
      {
         payload_data_fmt_generic_compressed_t *data_fmt_ptr =
            (payload_data_fmt_generic_compressed_t *)(media_fmt_ptr + 1);

         local_media_fmt.data_format         = SPF_GENERIC_COMPRESSED;
         local_media_fmt.fmt_id              = media_fmt_ptr->fmt_id;
         local_media_fmt.pcm.bit_width       = data_fmt_ptr->bits_per_sample;
         local_media_fmt.pcm.bits_per_sample = data_fmt_ptr->bits_per_sample;
         local_media_fmt.pcm.sample_rate     = data_fmt_ptr->sample_rate;
         local_media_fmt.pcm.num_channels    = data_fmt_ptr->num_channels;
         local_media_fmt.pcm.interleaving    = TOPO_INTERLEAVED;
         local_media_fmt.pcm.endianness      = TOPO_LITTLE_ENDIAN;
         break;
      }
      case DATA_FORMAT_DSD_OVER_PCM:
      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      " Unsupported data format %lu",
                      media_fmt_ptr->data_format);
         THROW(result, AR_EUNSUPPORTED);
         break;
      }
   }

   gen_cntr_input_media_format_received(me_ptr,
                                        (gu_ext_in_port_t *)ext_in_port_ptr,
                                        &local_media_fmt,
                                        NULL /*upstream_frame_len*/,
                                        is_data_path);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Set input media format failed ");
   }

   return result;
}

ar_result_t gen_cntr_fill_md_eos_create_config(gen_cntr_t *              me_ptr,
                                               gen_cntr_ext_in_port_t *  ext_in_port_ptr,
                                               module_cmn_md_tracking_t *eos_tracking_ptr,
                                               module_cmn_md_flags_t *   md_flags,
                                               uint32_t *                is_flushing_eos_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    event_policy = WR_SH_MEM_EP_EOS_POLICY_LAST;

   md_flags->word                   = 0;
   md_flags->is_out_of_band         = MODULE_CMN_MD_IN_BAND;
   md_flags->buf_sample_association = MODULE_CMN_MD_SAMPLE_ASSOCIATED;

   if (eos_tracking_ptr)
   {
      if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
      {
         memset(eos_tracking_ptr, 0, sizeof(module_cmn_md_tracking_t));
         return result;
      }

      gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

      // even though EoS is also input_discontinuity, it's handled separately
      data_cmd_wr_sh_mem_ep_eos_t *in_eos_payload_ptr = GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_eos_t, packet_ptr);

      if (NULL == in_eos_payload_ptr)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "MD_DBG: GEN_CNTR received NULL EOS payload");
         gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
         return AR_EBADPARAM;
      }

      event_policy = in_eos_payload_ptr->policy;

      eos_tracking_ptr->tracking_payload.src_domain_id = packet_ptr->dst_domain_id;
      eos_tracking_ptr->tracking_payload.src_port      = packet_ptr->dst_port;
      eos_tracking_ptr->tracking_payload.dst_domain_id = packet_ptr->src_domain_id;
      eos_tracking_ptr->tracking_payload.dest_port     = packet_ptr->src_port;
      eos_tracking_ptr->tracking_payload.token_lsw     = packet_ptr->token;
      eos_tracking_ptr->tracking_payload.token_msw     = 0;

      eos_tracking_ptr->tracking_payload.flags.word                  = 0;
      eos_tracking_ptr->tracking_payload.flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_CUSTOM_EVENT;

      eos_tracking_ptr->heap_info.heap_id = me_ptr->cu.heap_id;

      md_flags->tracking_mode   = MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROP_OR_CONSUME;
      md_flags->tracking_policy = (event_policy == WR_SH_MEM_EP_EOS_POLICY_LAST)
                                     ? MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST
                                     : MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH;

      md_flags->needs_propagation_to_client_buffer = MODULE_CMN_MD_NEEDS_PROPAGATION_TO_CLIENT_BUFFER_DISABLE;

      if (CLIENT_ID_HLOS == ext_in_port_ptr->client_config.cid.client_id)
      {
         md_flags->is_client_metadata = MODULE_CMN_MD_IS_EXTERNAL_CLIENT_MD;
      }
      else
      {
         md_flags->is_client_metadata = MODULE_CMN_MD_IS_INTERNAL_CLIENT_MD;
      }
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "MD_DBG: EOS update create config done, policy 0x%lx ",
                event_policy);

   return result;
}

static ar_result_t gen_cntr_check_process_eos_from_gpr_client(gen_cntr_t *            me_ptr,
                                                              gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                              uint32_t                bytes_in_ext_in)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                 is_flushing = MODULE_CMN_MD_EOS_FLUSHING;
   bool_t                   is_dropped  = FALSE;
   gpr_packet_t *           packet_ptr  = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   uint32_t                 gpr_opcode  = packet_ptr->opcode;
   module_cmn_md_tracking_t eos_tracking_info;
   module_cmn_md_flags_t    md_flags = {.word = 0 };

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_MED_PRIO,
                "MD_DBG: GEN_CNTR received EOS cmd (0x%lx) from DSP client at Module, port id (0x%lX, 0x%lx)",
                gpr_opcode,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

   memset(&eos_tracking_info, 0, sizeof(module_cmn_md_tracking_t));
   gen_cntr_fill_md_eos_create_config(me_ptr, ext_in_port_ptr, &eos_tracking_info, &md_flags, &is_flushing);

   module_cmn_md_eos_flags_t eos_md_flag = {.word = 0 };
   eos_md_flag.is_flushing_eos = is_flushing;

   result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                         (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr,
                                         ext_in_port_ptr->cu.id,
                                         me_ptr->cu.heap_id,
                                         &ext_in_port_ptr->md_list_ptr,
                                         &md_flags,
                                         &eos_tracking_info,
                                         &eos_md_flag,
                                         bytes_in_ext_in,
                                         &ext_in_port_ptr->cu.media_fmt);

   if (AR_EOK == result)
   {
      me_ptr->total_flush_eos_stuck++;
   }
   else
   {
      THROW(result, result);
   }

   // if we are already in data-flow-gap and we get another EOS, we need to first move to flowing state
   gen_cntr_handle_ext_in_data_flow_begin(me_ptr, ext_in_port_ptr);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, result, is_dropped);

   return result;
}

ar_result_t gen_cntr_process_pending_data_cmd_gpr_client(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;

   // process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_CMD_GPR:
      {
         gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
         if (!packet_ptr)
         {
            return AR_EFAILED;
         }

         switch (packet_ptr->opcode)
         {
            case DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2:
               // this happens due to timestamp discontinuity.
               break;

            case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_MED_PRIO,
                            " processing input media format from gpr client. input discont%u",
                            ext_in_port_ptr->flags.input_discontinuity);

               // get media format update cmd payload
               media_format_t *media_fmt_ptr = GPR_PKT_GET_PAYLOAD(media_format_t, packet_ptr);

               if (media_fmt_ptr)
               {
                  result = gen_cntr_data_ctrl_cmd_handle_in_media_fmt_from_gpr_client(me_ptr,
                                                                                      ext_in_port_ptr,
                                                                                      media_fmt_ptr,
                                                                                      TRUE /** data_cmd*/);
               }

               ext_in_port_ptr->flags.pending_mf = FALSE;
               gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, result, FALSE);
               break;
            }

            default:
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, " unknown opcode 0x%lx", packet_ptr->opcode);
               break;
            }
         }
         break;
      }
      default:
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_MED_PRIO,
                      " unknown opcode 0x%lx",
                      ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
         break;
      }
   }

   return result;
}

static void gen_cntr_reset_input_port_buf(gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ext_in_port_ptr->buf.actual_data_len = 0;
   ext_in_port_ptr->buf.max_data_len    = 0;
   ext_in_port_ptr->buf.data_ptr        = NULL;
   ext_in_port_ptr->buf.mem_map_handle  = 0;

   if (ext_in_port_ptr->buf.md_buf_ptr)
   {
      ext_in_port_ptr->buf.md_buf_ptr->max_data_len                = 0;
      ext_in_port_ptr->buf.md_buf_ptr->actual_data_len             = 0;
      ext_in_port_ptr->buf.md_buf_ptr->data_ptr                    = NULL;
      ext_in_port_ptr->buf.md_buf_ptr->mem_map_handle              = 0;
      ext_in_port_ptr->buf.md_buf_ptr->status                      = 0;
      ext_in_port_ptr->buf.md_buf_ptr->is_md_parsed_from_input_buf = FALSE;
   }
}

ar_result_t gen_cntr_free_input_data_buffer_cmd_gpr_client_v2(gen_cntr_t *            me_ptr,
                                                              gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                              ar_result_t             status,
                                                              bool_t                  is_flush)
{
   ar_result_t   result     = AR_EOK;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;

   if (!packet_ptr)
   {
      return AR_EFAILED;
   }

   data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2_t write_done_payload;

   data_cmd_wr_sh_mem_ep_data_buffer_v2_t *write_payload_ptr = NULL;
   write_payload_ptr =
      (data_cmd_wr_sh_mem_ep_data_buffer_v2_t *)GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);

   if (write_payload_ptr)
   {
      write_done_payload.data_buf_addr_lsw   = write_payload_ptr->data_buf_addr_lsw;
      write_done_payload.data_buf_addr_msw   = write_payload_ptr->data_buf_addr_msw;
      write_done_payload.data_mem_map_handle = write_payload_ptr->data_mem_map_handle;
      write_done_payload.data_status         = status;

      write_done_payload.md_buf_addr_lsw   = write_payload_ptr->md_buf_addr_lsw;
      write_done_payload.md_buf_addr_msw   = write_payload_ptr->md_buf_addr_msw;
      write_done_payload.md_mem_map_handle = write_payload_ptr->md_mem_map_handle;

      if (FALSE == is_flush)
      {
         write_done_payload.md_status = (uint32_t)ext_in_port_ptr->buf.md_buf_ptr->status;
      }
      else if (TRUE == is_flush)
      {
         // if the metadata is already parsed, and internal MD is created,
         // We should not raise drop MD events again during the flush handling
         if (FALSE == ext_in_port_ptr->buf.md_buf_ptr->is_md_parsed_from_input_buf)
         {
            result = gen_cntr_handle_flush_input_buffer_metadata(me_ptr, ext_in_port_ptr);
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, " processing flush of input buffer, release MD");
         }
         write_done_payload.md_status = (result); // CA : what should be right error code for flush
      }
   }
   else
   {
      memset((void *)&write_done_payload, 0, sizeof(write_done_payload));
      write_done_payload.data_status = (int32_t)AR_EUNEXPECTED;
      write_done_payload.md_status   = (int32_t)AR_EUNEXPECTED;
   }

   if (NULL != ext_in_port_ptr->buf.data_ptr) // for data commands
   {
      // Invalidating the cache before sending the ACK
      posal_cache_invalidate_v2(&ext_in_port_ptr->buf.data_ptr, ext_in_port_ptr->buf.max_data_len);

      posal_memorymap_shm_decr_refcount(apm_get_mem_map_client(), ext_in_port_ptr->buf.mem_map_handle);
   }

   if (ext_in_port_ptr->buf.md_buf_ptr->mem_map_handle)
   {
      // Invalidating the cache before sending the ACK
      posal_cache_invalidate_v2(&ext_in_port_ptr->buf.md_buf_ptr->data_ptr,
                                ext_in_port_ptr->buf.md_buf_ptr->max_data_len);

      posal_memorymap_shm_decr_refcount(apm_get_mem_map_client(), ext_in_port_ptr->buf.md_buf_ptr->mem_map_handle);
   }

   // reset input buffer params
   gen_cntr_reset_input_port_buf(ext_in_port_ptr);

   result = cu_gpr_generate_ack(&me_ptr->cu,
                                packet_ptr,
                                status,
                                &write_done_payload,
                                sizeof(write_done_payload),
                                DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2);

   return result;
}

/* is_flush flag should be used for flushing case.
 * For EOS: for flushing case EOS needs to be dropped
 * This flag can be used for other scenarios where special handling is required when flushing is done
 */
ar_result_t gen_cntr_free_input_data_cmd_gpr_client(gen_cntr_t             *me_ptr,
                                                    gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                    ar_result_t             status,
                                                    bool_t                  is_flush)
{
   ar_result_t            result       = AR_EOK;
   uint32_t               event_policy = MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST;
   gen_topo_input_port_t *in_port_ptr  = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // clear the buffer from internal input data and MD from internal input if the buffer has been borrowed,
   // ideally it should have been cleared at this point.
   result = gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports(me_ptr, ext_in_port_ptr, in_port_ptr, TRUE);

   if (SPF_MSG_CMD_GPR == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      gpr_packet_t *packet_ptr = (gpr_packet_t *)ext_in_port_ptr->cu.input_data_q_msg.payload_ptr;

      if (!packet_ptr)
      {
         return AR_EFAILED;
      }

      // CA: drop handling for metadata in flush case
      /** messages to be just freed without ack. for others generate_ack is called.*/
      if (gen_cntr_is_eos_opcode(packet_ptr->opcode))
      {
         if (TRUE == is_flush)
         {
            module_cmn_md_tracking_flags_t flags;
            module_cmn_md_flags_t          md_flags;

            flags.word = 0;

            data_cmd_wr_sh_mem_ep_eos_t *ep_eos_ptr = GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_eos_t, packet_ptr);

            event_policy = (ep_eos_ptr->policy == WR_SH_MEM_EP_EOS_POLICY_LAST)
                              ? MODULE_CMN_MD_TRACKING_EVENT_POLICY_LAST
                              : MODULE_CMN_MD_TRACKING_EVENT_POLICY_EACH;
            flags.requires_custom_event = MODULE_CMN_MD_TRACKING_USE_CUSTOM_EVENT;

            module_cmn_md_tracking_payload_t eos_tracking_payload = { flags,
                                                                      packet_ptr->dst_domain_id,
                                                                      packet_ptr->src_domain_id,
                                                                      packet_ptr->dst_port,
                                                                      packet_ptr->src_port,
                                                                      packet_ptr->token,
                                                                      0 };

            md_flags.word            = 0;
            md_flags.tracking_mode   = MODULE_CMN_MD_TRACKING_CONFIG_ENABLE_FOR_DROPS_ONLY;
            md_flags.tracking_policy = event_policy;
            gen_topo_drop_md(me_ptr->topo.gu.log_id,
                             &eos_tracking_payload,
                             MODULE_CMN_MD_ID_EOS,
                             md_flags,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             FALSE,
                             NULL);
         }
         result = cu_gpr_free_pkt(0, packet_ptr);
      }
      else
      {
         if ((DATA_CMD_WR_SH_MEM_EP_DATA_BUFFER_V2 == packet_ptr->opcode))
         {
            result = gen_cntr_free_input_data_buffer_cmd_gpr_client_v2(me_ptr, ext_in_port_ptr, status, is_flush);
         }
         else
         {
            result = cu_gpr_generate_ack(&me_ptr->cu, packet_ptr, status, NULL, 0, 0);
         }
      }
   }

   // set payload to NULL to indicate we are not holding on to any input data msg
   ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;

   return result;
}

/* function to handle the peer port property cmd handling from SPF external Client
 */
static ar_result_t gen_cntr_process_wrep_shmem_peer_client_property_config(gen_cntr_t *  me_ptr,
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
                "processing peer client property configuration command from WREP Client");

   // the payload format aligns with the internal format. re-using the same implementation
   result = gen_cntr_shmem_cmn_process_and_apply_peer_client_property_configuration(&me_ptr->cu,
                                                                                    handle_ptr,
                                                                                    (int8_t *)param_data_ptr,
                                                                                    param_size);

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Failed to process the peer client property configuration from WREP Client");
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "sucessfully processed the peer client property configuration from WREP Client");
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return result;
}

// This function assigns buffer to the internal port and optimizes buffer assigment,
// 1) Checks if NBLC end has a partial buffer that can be borrowed and assigned to wr shm internal input.
// 2) Else checks if Wr shm client buffer itself can be assigned to the internal port.
// 3) Else assigns a topo buf manager's buffer.
ar_result_t gen_cntr_wr_shm_setup_int_port_buf(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // if topo buffer is already assigned return EOK
   if (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return result;
   }

   // Check if buffer can be borrowed from NBLC end input.
   if (in_port_ptr->nblc_end_ptr && in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].data_ptr &&
       gen_topo_is_inplace_nblc_from_ext_input(&me_ptr->topo,
                                               (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                               in_port_ptr))
   {
      gen_topo_input_port_t *nblc_end_ptr = in_port_ptr->nblc_end_ptr;

      // if nblc end is inplace, & it has a buf
      // if (nblc_end_ptr->common.bufs_ptr[0].data_ptr /*&&
      //     (nblc_end_ptr->common.bufs_ptr[0].max_data_len >= in_port_ptr->common.bufs_ptr[0].max_data_len) &&
      //     (0 == nblc_end_ptr->common.bufs_ptr[0].actual_data_len)*/)
      // // not being empty is fine because, in gen_cntr_setup_internal_input_port_and_preprocess, we copy only what
      // // this buf can hold. for the same reason, not of max len is also fine.

      // for Raw compressed/packetized etc or PCM interleaved, we can use the contiguous data as a new buffer with
      // reduced max-length.
      // But for PCM deinterleaved data, nblc_end will have LLL_____|RRR_____. We will need num_ch num of pointers
      // for the empty space. In fwk, we cannot carry so many ptrs.
      if (!(((SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format)) &&
             (TOPO_INTERLEAVED != in_port_ptr->common.media_fmt_ptr->pcm.interleaving)) ||
            ((SPF_IS_PCM_DATA_FORMAT(nblc_end_ptr->common.media_fmt_ptr->data_format)) &&
             (TOPO_INTERLEAVED != nblc_end_ptr->common.media_fmt_ptr->pcm.interleaving))))
      {
         /** nblc end might have some data. point after that data. */
         // in interleaved cases there should be only one buffer
         in_port_ptr->common.bufs_ptr[0].data_ptr =
            nblc_end_ptr->common.bufs_ptr[0].data_ptr + nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
         in_port_ptr->common.bufs_ptr[0].max_data_len =
            nblc_end_ptr->common.bufs_ptr[0].max_data_len - nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
         in_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_BUF_MGR_BORROWED;

         return AR_EOK;
      }
   }

   // 2) Check if ext-in buffer can be borrowed to int-in for the next frame to process,
   //  Checks,s
   //  1. media format is supported - raw/interleaved data/deinterleaved packed.
   //  2. If ext in buffer has atleast threshold amount of data.
   if (ext_in_port_ptr->buf.data_ptr && !((SPF_IS_PCM_DATA_FORMAT(in_port_ptr->common.media_fmt_ptr->data_format)) &&
                                          (TOPO_INTERLEAVED != in_port_ptr->common.media_fmt_ptr->pcm.interleaving)))
   {
      if ((ext_in_port_ptr->buf.actual_data_len >= in_port_ptr->common.max_buf_len_per_buf))
      {
         uint32_t buf_addr = (uint32_t)(ext_in_port_ptr->buf.data_ptr +
                                        (ext_in_port_ptr->buf.max_data_len - ext_in_port_ptr->buf.actual_data_len));

         // if buffer is not 8 byte aligned, skip reusing client buffer.
         if (0 == (buf_addr & 0x7))
         {
            // nblc end might have some data. point after that data
            // in interleaved cases there should be only one buffer
            in_port_ptr->common.bufs_ptr[0].data_ptr     = (int8_t *)buf_addr;
            in_port_ptr->common.bufs_ptr[0].max_data_len = in_port_ptr->common.max_buf_len_per_buf;
            // actual data len will be updated later in this fucntion
            in_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: Port 0x%lx, assigned ext input buffer 0x%p  to internal port, size %lu, "
                         "max_buf_len %lu, buf_origin%u",
                         in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         in_port_ptr->common.bufs_ptr[0].data_ptr,
                         in_port_ptr->common.bufs_ptr[0].max_data_len,
                         in_port_ptr->common.max_buf_len_per_buf,
                         in_port_ptr->common.flags.buf_origin);
#endif

            // since wr shm is inplace assign the buffer to output here itself to optimize assignment for output.
            // If output is an external port, dont propagate ext in buffer since ICB/ext out buffer will be assigned
            if (in_port_ptr->gu.cmn.module_ptr->output_port_list_ptr &&
                in_port_ptr->gu.cmn.module_ptr->output_port_list_ptr->op_port_ptr->conn_in_port_ptr)
            {
               gen_topo_output_port_t *out_port_ptr =
                  (gen_topo_output_port_t *)in_port_ptr->gu.cmn.module_ptr->output_port_list_ptr->op_port_ptr;

               if (NULL == out_port_ptr->common.bufs_ptr[0].data_ptr)
               {
                  out_port_ptr->common.bufs_ptr[0].data_ptr     = in_port_ptr->common.bufs_ptr[0].data_ptr;
                  out_port_ptr->common.bufs_ptr[0].max_data_len = in_port_ptr->common.bufs_ptr[0].max_data_len;
                  out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED;
#ifdef VERBOSE_DEBUGGING
                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_LOW_PRIO,
                               "Module 0x%lX: Port 0x%lx, borrowed ext in buffer 0x%p for the output port, buf_len "
                               "%lu, "
                               "buf_origin%u",
                               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                               out_port_ptr->gu.cmn.id,
                               out_port_ptr->common.bufs_ptr[0].data_ptr,
                               out_port_ptr->common.bufs_ptr[0].max_data_len,
                               out_port_ptr->common.flags.buf_origin);
#endif
               }
            }
            return AR_EOK;
         }
      }
   }

   // note this call can return a fresh buf or a buf already at inplace-nblc-end.
   result = gen_topo_buf_mgr_wrapper_get_buf(&me_ptr->topo, &in_port_ptr->common);

#ifdef BUF_MGMT_DEBUG
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                " Module 0x%lX: Port 0x%lx, got input buffer 0x%p, size %lu, max_buf_len %lu, buf_origin%u",
                in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                in_port_ptr->gu.cmn.id,
                in_port_ptr->common.bufs_ptr[0].data_ptr,
                in_port_ptr->common.bufs_ptr[0].max_data_len,
                in_port_ptr->common.max_buf_len_per_buf,
                in_port_ptr->common.flags.buf_origin);
#endif

   return result;
}
