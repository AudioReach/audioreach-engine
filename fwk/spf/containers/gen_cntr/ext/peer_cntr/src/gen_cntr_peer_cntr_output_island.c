/**
 * \file gen_cntr_peer_cntr_output_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "apm.h"

static ar_result_t gen_cntr_output_buf_set_up_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
static ar_result_t gen_cntr_check_realloc_ext_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr);

const gen_cntr_ext_out_vtable_t peer_cntr_ext_out_vtable = {
   .setup_bufs       = gen_cntr_output_buf_set_up_peer_cntr,
   .write_data       = gen_cntr_write_data_for_peer_cntr,
   .fill_frame_md    = NULL,
   .get_filled_size  = NULL,
   .flush            = gen_cntr_flush_output_data_queue_peer_cntr,
   .recreate_out_buf = gen_cntr_recreate_out_buf_peer_cntr,
   .prop_media_fmt   = gen_cntr_create_send_media_fmt_to_peer_cntr,
   .write_metadata   = NULL,
   .setup_topo_buf   = gen_cntr_peer_output_setup_topo_buffer,
};

ar_result_t gen_cntr_init_peer_cntr_ext_out_port(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   // by default assume 1 frame per buffer
   ext_out_port_ptr->vtbl_ptr              = &peer_cntr_ext_out_vtable;
   ext_out_port_ptr->flags.fill_ext_buf    = FALSE;
   ext_out_port_ptr->max_frames_per_buffer = 1;
   return result;
}

/**
 * ext_port_ptr->out_data_buf_node.buf_ptr may be null when this func returns.
 */
ar_result_t gen_cntr_process_pop_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr)
{
   ar_result_t result = AR_EOK;

   // if a buf is already popped, return
   if ((NULL != ext_port_ptr->cu.out_bufmgr_node.buf_ptr))
   {
      return AR_EOK;
   }

   // poll until we get a buf that doesn't get recreated
   while (posal_channel_poll_inline(me_ptr->cu.channel_ptr, ext_port_ptr->cu.bit_mask))
   {
      // Take next buffer off the q
      if (AR_DID_FAIL(result = posal_queue_pop_front(ext_port_ptr->gu.this_handle.q_ptr,
                                                     (posal_queue_element_t *)&(ext_port_ptr->cu.out_bufmgr_node))))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failure on Out Buf Pop ");
         return result;
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "popped out buffer  0x%p. Current bitmask 0x%x, self bitmask 0x%x",
                   ext_port_ptr->cu.out_bufmgr_node.buf_ptr,
                   me_ptr->cu.curr_chan_mask,
                   ext_port_ptr->cu.bit_mask);
#endif

      result = gen_cntr_check_realloc_ext_buffer(me_ptr, ext_port_ptr);
      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Relloacate failed ");
         return result;
      }

      if (NULL != ext_port_ptr->cu.out_bufmgr_node.buf_ptr)
      {

         break;
      }
      // if buf got recreated, go back to loop.
   }

   return result;
}

static void gen_cntr_assign_v1_ext_out_buf_to_int_buf(gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                      gen_topo_output_port_t  *out_port_ptr)
{
   if (!gen_topo_use_ext_out_buf_for_topo(out_port_ptr))
   {
      return;
   }

   if (out_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return;
   }

   out_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

   uint32_t max_len_per_buf = topo_div_num(ext_out_port_ptr->buf.max_data_len, out_port_ptr->common.sdata.bufs_num);

   // assign rest of the buffer lengths and ptrs
   int8_t *cur_buf_ptr = ext_out_port_ptr->buf.data_ptr;
   if (out_port_ptr->common.flags.is_pcm_unpacked)
   {
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         out_port_ptr->common.bufs_ptr[b].data_ptr = cur_buf_ptr;
         cur_buf_ptr += max_len_per_buf;
      }

      // assign only first ch buffer
      out_port_ptr->common.bufs_ptr[0].actual_data_len = 0;
      out_port_ptr->common.bufs_ptr[0].max_data_len    = max_len_per_buf;
   }
   else
   {
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         out_port_ptr->common.bufs_ptr[b].data_ptr        = cur_buf_ptr;
         out_port_ptr->common.bufs_ptr[b].actual_data_len = 0;
         out_port_ptr->common.bufs_ptr[b].max_data_len    = max_len_per_buf;
         cur_buf_ptr += max_len_per_buf;
      }
   }
}

static void gen_cntr_assign_v2_ext_out_buf_to_int_buf(gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                      gen_topo_output_port_t  *out_port_ptr)
{
   if (!gen_topo_use_ext_out_buf_for_topo(out_port_ptr))
   {
      return;
   }

   if (out_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return;
   }

   out_port_ptr->common.flags.buf_origin = GEN_TOPO_BUF_ORIGIN_EXT_BUF;

   if (out_port_ptr->common.flags.is_pcm_unpacked)
   {
      out_port_ptr->common.bufs_ptr[0].actual_data_len = 0;
      out_port_ptr->common.bufs_ptr[0].max_data_len    = ext_out_port_ptr->bufs_ptr[0].max_data_len;
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         out_port_ptr->common.bufs_ptr[b].data_ptr = ext_out_port_ptr->bufs_ptr[b].data_ptr;
      }
   }
   else
   {
      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         out_port_ptr->common.bufs_ptr[b].data_ptr        = ext_out_port_ptr->bufs_ptr[b].data_ptr;
         out_port_ptr->common.bufs_ptr[b].actual_data_len = 0;
         out_port_ptr->common.bufs_ptr[b].max_data_len    = ext_out_port_ptr->bufs_ptr[b].max_data_len;
      }
   }
}

ar_result_t gen_cntr_init_after_popping_peer_cntr_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   // either the input trigger caused us to reach here, or buf recreate happened.
   if (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
      spf_msg_header_t *header = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);

      if (!gen_cntr_is_ext_out_v2(ext_out_port_ptr))
      {
         spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

         ext_out_port_ptr->buf.data_ptr        = (int8_t *)(&(out_buf_ptr->data_buf));
         ext_out_port_ptr->buf.actual_data_len = 0;
         ext_out_port_ptr->buf.max_data_len    = out_buf_ptr->max_size;

         ext_out_port_ptr->num_frames_in_buf = 0;

         out_buf_ptr->actual_size = 0;
      }
      else
      {
         spf_msg_data_buffer_v2_t *out_buf_ptr = (spf_msg_data_buffer_v2_t *)&header->payload_start;

         spf_msg_single_buf_v2_t *data_buf_ptr = (spf_msg_single_buf_v2_t *)(out_buf_ptr + 1);

         if (data_buf_ptr)
         {
            for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
            {
               ext_out_port_ptr->bufs_ptr[b].data_ptr        = (int8_t *)data_buf_ptr[b].data_ptr;
               ext_out_port_ptr->bufs_ptr[b].actual_data_len = 0;
               ext_out_port_ptr->bufs_ptr[b].max_data_len    = data_buf_ptr[b].max_size;

               data_buf_ptr[b].actual_size = 0;
            }
         }

         ext_out_port_ptr->num_frames_in_buf = 0;
      }
   }

   return result;
}

/**
 * sets up output buffers and calls process on topo.
 *
 * any of the output port can trigger this.
 */
static ar_result_t gen_cntr_output_buf_set_up_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // Buffer should have been returned to either Q  in previous iteration.
   if (NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
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

   TRY(result, gen_cntr_process_pop_out_buf(me_ptr, ext_out_port_ptr));

   TRY(result, gen_cntr_init_after_popping_peer_cntr_out_buf(me_ptr, ext_out_port_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      // return all the buffer to their respective Q if anything failed.
      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         gen_cntr_ext_out_port_t *temp_ext_out_port_ptr =
            (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         gen_cntr_return_back_out_buf(me_ptr, temp_ext_out_port_ptr);
      }
   }
   return result;
}

static ar_result_t gen_cntr_populate_peer_cntr_out_buf(gen_cntr_t *             me_ptr,
                                                       gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                       bool_t *                 out_buf_has_flushing_eos_ptr)
{
   ar_result_t result = AR_EOK;

   int64_t *              outbuf_timestamp_ptr = NULL;
   uint32_t *             outbuf_flag_ptr;
   module_cmn_md_list_t **outbuf_md_list_pptr;

   spf_msg_header_t *header = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
   header->rsp_result       = 0;

   if (!gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      out_buf_ptr->actual_size = ext_out_port_ptr->buf.actual_data_len;
      out_buf_ptr->max_size    = ext_out_port_ptr->cu.buf_max_size;

      outbuf_timestamp_ptr = &out_buf_ptr->timestamp;
      outbuf_flag_ptr      = &out_buf_ptr->flags;
      outbuf_md_list_pptr  = &(out_buf_ptr->metadata_list_ptr);
   }
   else
   {
      spf_msg_data_buffer_v2_t *out_buf_ptr  = (spf_msg_data_buffer_v2_t *)&header->payload_start;
      spf_msg_single_buf_v2_t * data_buf_ptr = (spf_msg_single_buf_v2_t *)(out_buf_ptr + 1);

      for (uint32_t b = 0; b < out_buf_ptr->bufs_num; b++)
      {
         data_buf_ptr[b].actual_size = ext_out_port_ptr->bufs_ptr[b].actual_data_len;
         data_buf_ptr[b].max_size    = ext_out_port_ptr->bufs_ptr[b].max_data_len;
      }

      // out_buf_ptr->max_size          = ext_out_port_ptr->bufs_ptr[0].max_data_len;
      out_buf_ptr->metadata_list_ptr = NULL;

      outbuf_timestamp_ptr = &out_buf_ptr->timestamp;
      outbuf_flag_ptr      = &out_buf_ptr->flags;
      outbuf_md_list_pptr  = &(out_buf_ptr->metadata_list_ptr);
   }

   *outbuf_flag_ptr     = 0;
   *outbuf_md_list_pptr = NULL;

   gen_topo_timestamp_t ts = ext_out_port_ptr->next_out_buf_ts;

   topo_media_fmt_t *module_med_fmt_ptr =
      ((gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.media_fmt_ptr;

   // if TS is not valid and this is STM container get latest signal trigger TS
   // from the STM module and populate
   if (SPF_IS_RAW_COMPR_DATA_FMT(module_med_fmt_ptr->data_format))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
      bool_t                  end_of_frame = out_port_ptr->common.sdata.flags.end_of_frame;
      // Populate the timestamp
      cu_set_bits(outbuf_flag_ptr, end_of_frame, DATA_BUFFER_FLAG_EOF_MASK, DATA_BUFFER_FLAG_EOF_SHIFT);
   }

   // Populate the timestamp
   cu_set_bits(outbuf_flag_ptr,
               ts.valid,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK,
               DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);

   // Populate the timestamp continue
   cu_set_bits(outbuf_flag_ptr,
               ts.ts_continue,
               DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_MASK,
               DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_SHIFT);

   *outbuf_timestamp_ptr = ts.value;

   if (NULL != ext_out_port_ptr->md_list_ptr)
   {
      // When md lib is in nlpi, we dont need to exit here since by this point we should already be in nlpi
      // due to voting against island when propagating md
      gen_topo_check_realloc_md_list_in_peer_heap_id(me_ptr->topo.gu.log_id,
                                                     &(ext_out_port_ptr->gu),
                                                     &(ext_out_port_ptr->md_list_ptr));

      gen_topo_populate_metadata_for_peer_cntr(&(me_ptr->topo),
                                               &(ext_out_port_ptr->gu),
                                               &(ext_out_port_ptr->md_list_ptr),
                                               outbuf_md_list_pptr,
                                               out_buf_has_flushing_eos_ptr);
   }

#ifdef VERBOSE_DEBUGGING
   if (!gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Outgoing timestamp: %lu (0x%lx%lx), flag=0x%lx, size=%lu",
                   (uint32_t)out_buf_ptr->timestamp,
                   (uint32_t)(out_buf_ptr->timestamp >> 32),
                   (uint32_t)out_buf_ptr->timestamp,
                   out_buf_ptr->flags,
                   out_buf_ptr->actual_size);
   }
   else
   {
      spf_msg_data_buffer_v2_t *out_buf_ptr  = (spf_msg_data_buffer_v2_t *)&header->payload_start;
      spf_msg_single_buf_v2_t * data_buf_ptr = (spf_msg_single_buf_v2_t *)(out_buf_ptr + 1);

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Outgoing timestamp: %lu (0x%lx%lx), flag=0x%lx",
                   (uint32_t)out_buf_ptr->timestamp,
                   (uint32_t)(out_buf_ptr->timestamp >> 32),
                   (uint32_t)out_buf_ptr->timestamp,
                   out_buf_ptr->flags);

      for (uint32_t b = 0; b < out_buf_ptr->bufs_num; b++)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Outgoing size %lu : %lu ", b, data_buf_ptr[b].actual_size);
      }
   }
#endif
   return result;
}

ar_result_t gen_cntr_return_back_out_buf(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_port_ptr->gu.int_out_port_ptr;

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   {
      if (out_port_ptr->common.bufs_ptr[0].actual_data_len != 0)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Module 0x%lX: Warning: returning buffer 0x%p when it has valid data %lu",
                      out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      out_port_ptr->common.bufs_ptr[0].data_ptr,
                      out_port_ptr->common.bufs_ptr[0].actual_data_len);
      }

      // usually ext buffer gets cleared during the post process. but if the mf was raised, post process will not be
      // called, and old buffer needs to be released here. Also if internal ports in nblc are borrowing ext buffer free
      // it here.
      gen_cntr_clear_borrowed_ext_out_buffer_from_int_ports(me_ptr, ext_port_ptr, out_port_ptr);
   }

   spf_msg_header_t *header = (spf_msg_header_t *)(ext_port_ptr->cu.out_bufmgr_node.buf_ptr);
   if (NULL != header)
   {
      if (ext_port_ptr->cu.out_bufmgr_node.return_q_ptr)
      {
         spf_msg_t msg = { .payload_ptr = ext_port_ptr->cu.out_bufmgr_node.buf_ptr, .msg_opcode = 0 };
         spf_msg_return_msg(&msg);
      }
      ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
   }

   gen_cntr_clear_ext_out_bufs(ext_port_ptr, TRUE /*clear_max*/);

   return AR_EOK;
}

static ar_result_t gen_cntr_send_data_to_downstream_peer_cntr(gen_cntr_t *             me_ptr,
                                                              gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                              spf_msg_header_t *       out_buf_header)
{
   ar_result_t result = AR_EOK;

   // overrun in GEN_CNTR
   if (!out_buf_header)
   {
      return result;
   }
#ifdef VERBOSE_DEBUGGING

   spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&out_buf_header->payload_start;
   gen_topo_module_t *    module_ptr  = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "gen_cntr_send_pcm_to_downstream_peer_cntr: (0x%lx, 0x%lx) timestamp: %lu (0x%lx%lx). size %lu",
                module_ptr->gu.module_instance_id,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->timestamp >> 32),
                (uint32_t)out_buf_ptr->timestamp,
                (uint32_t)(out_buf_ptr->actual_size));
#endif

   posal_bufmgr_node_t out_buf_node = ext_out_port_ptr->cu.out_bufmgr_node;
   spf_msg_t *         data_msg;

   uint32_t msg_opcode = SPF_MSG_DATA_BUFFER;
   spf_msg_token_t token = {0};

   // upper nibble of MSB is used to indicate v2 message - rest 28 bits
   // are available - token is used instead of flag since this is currently
   // only defined and applicable for GCs
   if (gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      msg_opcode = SPF_MSG_DATA_BUFFER_V2;
      gen_cntr_set_data_msg_out_buf_token(&token);
   }

   {
      data_msg =
         spf_msg_convt_buf_node_to_msg(&out_buf_node, msg_opcode, NULL, &token, 0, &ext_out_port_ptr->gu.this_handle);
   }

   if (ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr)
   {
#ifdef PROC_DELAY_DEBUG
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr;
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "PROC_DELAY_DEBUG: GC Module 0x%lX: Ext output data sent from port 0x%lX",
                   module_ptr->gu.module_instance_id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);
#endif

      result = posal_queue_push_back(ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr,
                                     (posal_queue_element_t *)data_msg);
      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to deliver buffer dowstream. Dropping");
         gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "pushed buffer downstream 0x%p. Current bit mask 0x%x",
                   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr,
                   me_ptr->cu.curr_chan_mask);
#endif
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "downstream not connected. dropping buffer");
      gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
   }

   ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
   return result;
}

/*
 * sends media fmt, output buf, and EoS.
 * if output buffer size is > 0,
 * deliver it to downstream, otherwise return the buffer to bufQ
 */
static ar_result_t gen_cntr_send_peer_cntr_out_buffers(gen_cntr_t *             me_ptr,
                                                       gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                       spf_msg_t *              media_fmt_msg_ptr)
{
   ar_result_t result                   = AR_EOK;
   bool_t      out_buf_has_flushing_eos = FALSE;

   if (media_fmt_msg_ptr && media_fmt_msg_ptr->payload_ptr)
   {
      result |= cu_send_media_fmt_update_to_downstream(&me_ptr->cu,
                                                       &ext_out_port_ptr->gu,
                                                       media_fmt_msg_ptr,
                                                       ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr->q_ptr);

      if (AR_EOK == result)
      {
         ext_out_port_ptr->flags.out_media_fmt_changed         = FALSE;
         ext_out_port_ptr->cu.flags.upstream_frame_len_changed = FALSE;
      }
   }

   bool_t is_data_present = gen_cntr_is_data_present_in_ext_out_bufs(ext_out_port_ptr);

   if (ext_out_port_ptr->md_list_ptr || is_data_present)
   {
      spf_msg_header_t *     header      = (spf_msg_header_t *)(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr);
      spf_msg_data_buffer_t *out_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;
      topo_port_state_t      ds_downgraded_state =
         cu_get_external_output_ds_downgraded_port_state(&me_ptr->cu, &ext_out_port_ptr->gu);

      // can happen during overrun in GEN_CNTR or if we force process at DFG or if any downstream is stopped.
      if ((NULL == header) || (TOPO_PORT_STATE_STARTED != ds_downgraded_state))
      {
         if ((/*me_ptr->topo.flags.is_signal_triggered && */ me_ptr->topo.flags.is_signal_triggered_active) ||
             ext_out_port_ptr->cu.prop_info.is_us_rt)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         " Dropping data %lu and/or metadata 0x%p as there's no external buf",
                         ext_out_port_ptr->buf.actual_data_len,
                         ext_out_port_ptr->md_list_ptr);

            gen_cntr_clear_ext_out_bufs(ext_out_port_ptr, FALSE /* Clear max */);

            if (ext_out_port_ptr->md_list_ptr)
            {
               // Since this involves mem free operation, exit island only temporarily
               gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(&me_ptr->topo);
               gen_topo_destroy_all_metadata(me_ptr->topo.gu.log_id,
                                             (void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                             &ext_out_port_ptr->md_list_ptr,
                                             TRUE /*is_dropped*/);
            }
         }
         else
         {
            uint32_t actual_data_len = 0;
            gen_cntr_get_ext_out_total_actual_max_data_len(ext_out_port_ptr, &actual_data_len, NULL);

            if (ext_out_port_ptr->md_list_ptr || (actual_data_len))
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_LOW_PRIO,
                            " No output buffer at the end of process frames, keeping data %lu and/or metadata 0x%p",
                            actual_data_len,
                            ext_out_port_ptr->md_list_ptr);
            }
         }

         gen_cntr_return_back_out_buf(me_ptr, ext_out_port_ptr);
         return result;
      }

      gen_cntr_populate_peer_cntr_out_buf(me_ptr, ext_out_port_ptr, &out_buf_has_flushing_eos);

      // if module output unpacked, container needs to convert to packed.
      // note that internal output port may have new media format that hasn't been propagated to ext
      // output at this point (which can be intereleaved), but the pending data in ext output buffer may be
      // of old media format i.e deintereleaved unpacked. Hence we need to maintain the ext output specific flag to
      // check if data is PCM unpacked.
      if (ext_out_port_ptr->cu.flags.is_pcm_unpacked)
      {
         // if actual=max len, then already packed.
         if (ext_out_port_ptr->buf.actual_data_len < ext_out_port_ptr->buf.max_data_len)
         {
            uint32_t num_ch           = ext_out_port_ptr->cu.media_fmt.pcm.num_channels;
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

      if (!ext_out_port_ptr->cu.icb_info.is_prebuffer_sent)
      {
         // Exit and handle prebuffers only if port has requirement
         if (cu_check_if_port_requires_prebuffers(&ext_out_port_ptr->cu))
         {
            gen_topo_exit_island_temporarily(&me_ptr->topo);
            cu_handle_prebuffer(&me_ptr->cu,
                                &ext_out_port_ptr->gu,
                                out_buf_ptr,
                                ext_out_port_ptr->cu.buf_max_size -
                                   (ext_out_port_ptr->cu.media_fmt.pcm.num_channels * gen_topo_compute_if_output_needs_addtional_bytes_for_dm(&(me_ptr->topo),
                                                                                            (gen_topo_output_port_t *)
                                                                                               ext_out_port_ptr->gu
                                                                                                  .int_out_port_ptr)));
         }
         else
         {
            // if port didnt have prebuf requirement at data flow start, then we can mark sent= TRUE,
            // If prebuf requriement changes after data flow start, no need to insert prebuffer since its going cause
            // a glitch, if we need to handle scenario there we need to consider if media format changed
            ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = TRUE;
         }
      }

      result = gen_cntr_send_data_to_downstream_peer_cntr(me_ptr, ext_out_port_ptr, header);
   }
   else
   {
      // no need to return the buffer if it doesnt have any data
      // Note: Returning buffer can cause infinite loop for some graphs with data TGP modules. If there is data TGP
      // module thats not in the NBLC of ext out port,
      // and tgp module's output is "blocked". Container keeps getting triggerd to process since ext output is marked
      // "mandatory".
      //
      // Example: SVA+AAD+FFNS usecase. we have AAD -> DAM-1 (output blocked)-> FFNS -> Splitter (ext out) -> (exit
      // in)SVA.
      //                                                                                         (int out) -> DAM-2
      //          The ext out of splitter can continuously wake up cntr if buffer is returned to the queue. DAM-1's
      //          blocked output is not visible to Ext out, since its
      //          not NBLC
      return result;
   }

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   {
      // it's enough to reset first buf, as only this is checked against.
      out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
      out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
      out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
      gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
   }

   gen_cntr_clear_ext_out_bufs(ext_out_port_ptr, TRUE /*clear_max*/);

   // if flushing EOS was sent out, reset the ext out port
   if (out_buf_has_flushing_eos)
   {
      gen_cntr_ext_out_port_basic_reset(me_ptr, ext_out_port_ptr);
      if (FALSE == me_ptr->topo.flags.defer_voting_on_dfs_change)
      {
         CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, dfs_change);
      }

      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_HIGH_PRIO,
                          "Output of miid 0x%lx defer_voting_on_dfs_change:%lu",
                          ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                          me_ptr->topo.flags.defer_voting_on_dfs_change);
   }

   return result;
}

ar_result_t gen_cntr_write_data_for_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   spf_msg_t media_fmt_msg = { 0 };

   // This will take care of run time media format or frame len change
   if ((TRUE == ext_out_port_ptr->flags.out_media_fmt_changed) ||
       (TRUE == ext_out_port_ptr->cu.flags.upstream_frame_len_changed))
   {

      TRY(result,
          cu_create_media_fmt_msg_for_downstream(&me_ptr->cu,
                                                 &ext_out_port_ptr->gu,
                                                 &media_fmt_msg,
                                                 SPF_MSG_DATA_MEDIA_FORMAT));

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "Sent container output media format from module 0x%x, port 0x%x, flags: media fmt %d frame len %d",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                   ext_out_port_ptr->flags.out_media_fmt_changed,
                   ext_out_port_ptr->cu.flags.upstream_frame_len_changed);
#endif
      // note: flags are cleared after sending the mf to DS cntr
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id,
                      "gen_cntr_write_data_for_peer_cntr output[module 0x%lx port 0x%lx] bytes %lu",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                      ext_out_port_ptr->buf.actual_data_len);
#endif

   TRY(result, gen_cntr_send_peer_cntr_out_buffers(me_ptr, ext_out_port_ptr, &media_fmt_msg));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/* if a buffer is recreated, then
 * ext_port_ptr->out_data_buf_node.pBuffer is NULL
 * and the caller must go back to work loop.
 */
static ar_result_t gen_cntr_check_realloc_ext_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr)
{
   ar_result_t       result     = AR_EOK;
   spf_msg_header_t *buffer_ptr = (spf_msg_header_t *)ext_port_ptr->cu.out_bufmgr_node.buf_ptr;

#ifdef SAFE_MODE
   if (NULL == buffer_ptr)
   {
      return result;
   }
#endif

   uint32_t max_size = 0, num_bufs_needed = 0, bufs_num = 0;

   bool_t is_msg_data_deint_buf = gen_cntr_check_data_msg_out_buf_token_is_v2(buffer_ptr->token);

   if (!gen_cntr_is_ext_out_v2(ext_port_ptr))
   {
      spf_msg_data_buffer_t *data_buf_ptr = (spf_msg_data_buffer_t *)&buffer_ptr->payload_start;
      max_size                            = data_buf_ptr->max_size;
      num_bufs_needed = ext_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_port_ptr->cu.icb_info.icb.num_reg_prebufs;

      // if buf size or count doesn't match then recreate.
      uint32_t num_bufs_to_destroy = num_bufs_needed >= ext_port_ptr->cu.num_buf_allocated
                                        ? 0
                                        : (ext_port_ptr->cu.num_buf_allocated - num_bufs_needed);

      if ((data_buf_ptr->max_size != ext_port_ptr->cu.buf_max_size) || (num_bufs_to_destroy > 0) || is_msg_data_deint_buf)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Destroyed 1 external buffers 0x%p", buffer_ptr);

         // Free the buffer
         posal_memory_free(buffer_ptr);
         ext_port_ptr->cu.num_buf_allocated--;
         ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
      }
   }
   else
   {
      spf_msg_data_buffer_v2_t *data_msg_buf_ptr = (spf_msg_data_buffer_v2_t *)&buffer_ptr->payload_start;
      spf_msg_single_buf_v2_t * data_buf_ptr     = (spf_msg_single_buf_v2_t *)(data_msg_buf_ptr + 1);

      /* Currently all buffers are of same max size */
      max_size = data_buf_ptr[0].max_size;
      bufs_num =  data_msg_buf_ptr->bufs_num;

      num_bufs_needed = ext_port_ptr->cu.icb_info.icb.num_reg_bufs + ext_port_ptr->cu.icb_info.icb.num_reg_prebufs;
      uint32_t num_bufs_to_destroy = num_bufs_needed >= ext_port_ptr->cu.num_buf_allocated
                                        ? 0
                                        : (ext_port_ptr->cu.num_buf_allocated - num_bufs_needed);

      if ((max_size != ext_port_ptr->cu.buf_max_size) || (num_bufs_to_destroy > 0) || (bufs_num != ext_port_ptr->bufs_num) || !is_msg_data_deint_buf)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Destroyed 1 external buffers 0x%p", buffer_ptr);

         // Free the buffer
         posal_memory_free(buffer_ptr);
         ext_port_ptr->cu.num_buf_allocated--;
         ext_port_ptr->cu.out_bufmgr_node.buf_ptr = NULL;
      }
   }

   uint32_t num_bufs_to_create =
      num_bufs_needed > ext_port_ptr->cu.num_buf_allocated ? num_bufs_needed - ext_port_ptr->cu.num_buf_allocated : 0;

   //TODO: Optimize redundant check - buf_max_size and bufs_num already checked during destroy
   if ((max_size != ext_port_ptr->cu.buf_max_size) || (num_bufs_to_create > 0) || (bufs_num != ext_port_ptr->bufs_num))
   {
      // this check prevents unnecessary repeating that come from prints gen_cntr_create_ext_out_bufs
      if (ext_port_ptr->cu.num_buf_allocated < CU_MAX_OUT_BUF_Q_ELEMENTS)
      {
         // one buf was destroyed, so create one more.
         result = gen_cntr_create_ext_out_bufs(me_ptr, ext_port_ptr, ext_port_ptr->cu.num_buf_allocated + 1);
         if (AR_DID_FAIL(result))
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, " Buffer recreate failed %d", result);
         }
      }
   }

   return result;
}

ar_result_t gen_cntr_peer_output_setup_topo_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if ((NULL == ext_out_port_ptr->buf.data_ptr) || (NULL != out_port_ptr->common.bufs_ptr[0].data_ptr))
   {
      // In case of signal triggered containers, if ext buffer is not available, topo buffer will be assigned to
      // internal output during process
      return result;
   }

   if (!gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      gen_cntr_assign_v1_ext_out_buf_to_int_buf(ext_out_port_ptr, out_port_ptr);
   }
   else
   {
      gen_cntr_assign_v2_ext_out_buf_to_int_buf(ext_out_port_ptr, out_port_ptr);
   }

   return result;
}
