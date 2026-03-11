/**
 * \file gen_cntr_peer_cntr_input_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "apm.h"
#include "cu_events.h"
#include "media_fmt_extn_api.h"


static ar_result_t gen_cntr_realloc_ext_in_bufs_ptr(gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                    uint32_t                bufs_num,
                                                    POSAL_HEAP_ID           heap_id);

static ar_result_t gen_cntr_input_data_set_up_peer_cntr(gen_cntr_t             *me_ptr,
                                                        gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                        bool_t                  is_data_buf_v2);

static bool_t      gen_cntr_is_input_a_data_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
static ar_result_t gen_cntr_free_input_data_cmd_peer_cntr(gen_cntr_t             *me_ptr,
                                                          gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          ar_result_t             status,
                                                          bool_t                  is_flush);
static ar_result_t gen_cntr_process_eos_md_from_peer_cntr(gen_cntr_t *            me_ptr,
                                                          gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          module_cmn_md_list_t  **md_list_head_pptr);

// clang-format off
const gen_cntr_ext_in_vtable_t peer_cntr_ext_in_vtable = {
   .on_trigger               = gen_cntr_input_dataQ_trigger_peer_cntr,
   .read_data                = gen_cntr_copy_peer_cntr_input_to_int_buf,
   .is_data_buf              = gen_cntr_is_input_a_data_buffer,
   .process_pending_data_cmd = gen_cntr_process_pending_data_cmd_peer_cntr,
   .free_input_buf           = gen_cntr_free_input_data_cmd_peer_cntr,
   .frame_len_change_notif   = NULL,
   .setup_topo_buf            = NULL,
};
// clang-format on

ar_result_t gen_cntr_init_peer_cntr_ext_in_port(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_port_ptr)
{
   ar_result_t result     = AR_EOK;
   ext_port_ptr->vtbl_ptr = &peer_cntr_ext_in_vtable;
   return result;
}

/**
 * returns whether the input buffer being held is an data buffer (not EOS, Media fmt etc)
 */
static bool_t gen_cntr_is_input_a_data_buffer(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t is_data_cmd = FALSE;
   // if no buffer held, then return FALSE
   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return FALSE;
   }

   if ((SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode) ||
       (SPF_MSG_DATA_BUFFER_V2 == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode))
   {
      is_data_cmd = TRUE;
   }

   return is_data_cmd;
}

ar_result_t gen_cntr_copy_peer_or_olc_client_input(gen_cntr_t             *me_ptr,
                                                   gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                   uint32_t               *bytes_copied_per_buf_ptr)
{
   ar_result_t result = AR_EOK;
   int8_t     *src_ptr;
   int8_t     *dst_ptr;
   uint32_t    inp_read_index = 0;

   gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   topo_buf_t            *module_bufs_ptr = in_port_ptr->common.bufs_ptr;

   // return w/o doing anything if there is no data left in input buffer only in the non-process context.
   // If it happens in the process context and the trigger mode is Interrupt driven we need to UNDER RUN.
   if (!gen_cntr_is_data_present_in_ext_in_bufs(ext_in_port_ptr))
   {
      *bytes_copied_per_buf_ptr = 0;
      return result;
   }

   if (SPF_IS_PCM_DATA_FORMAT(ext_in_port_ptr->cu.media_fmt.data_format))
   {
      uint32_t bytes_per_sample        = TOPO_BITS_TO_BYTES(ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample);
      uint32_t num_channels            = ext_in_port_ptr->cu.media_fmt.pcm.num_channels;
      uint32_t in_num_bytes            = ext_in_port_ptr->buf.actual_data_len;
      uint32_t in_num_samples_per_chan = topo_div_num(in_num_bytes, (bytes_per_sample * num_channels));

      uint32_t out_num_bytes_per_buf = (module_bufs_ptr[0].max_data_len - module_bufs_ptr[0].actual_data_len);
      out_num_bytes_per_buf          = MIN(*bytes_copied_per_buf_ptr, out_num_bytes_per_buf);

      // out_num_bytes_per_buf is in input buf fmt, not ext-buf-fmt which could differ in interleaving flag
      uint32_t out_num_bytes_per_ch =
         gen_topo_convert_len_per_buf_to_len_per_ch(in_port_ptr->common.media_fmt_ptr, out_num_bytes_per_buf);

      uint32_t out_num_samples_per_chan =
         topo_bytes_per_ch_to_samples(out_num_bytes_per_ch, &ext_in_port_ptr->cu.media_fmt);

      uint32_t samples_to_copy_per_chan = MIN(in_num_samples_per_chan, out_num_samples_per_chan);

      if (0 == samples_to_copy_per_chan)
      {
         *bytes_copied_per_buf_ptr = 0;
         return result;
      }

      out_num_bytes_per_ch   = samples_to_copy_per_chan * bytes_per_sample;
      uint32_t out_num_bytes = out_num_bytes_per_ch * num_channels;

      /* NOTE: this works because, we have assigned max_data_len = actual_data_len in the
       * gen_cntr_input_data_set_up_peer_cntr
       * Hence, the incoming buffer will always be full in the beginning.
       * We don't touch this buffer anywhere else till it becomes empty, so we can keep this indexing logic and copy
       * data in the pre-process till it becomes empty */
      inp_read_index = ext_in_port_ptr->buf.max_data_len - ext_in_port_ptr->buf.actual_data_len;
      src_ptr        = ext_in_port_ptr->buf.data_ptr;
      dst_ptr        = module_bufs_ptr[0].data_ptr;

      if (in_port_ptr->flags.was_eof_set)
      {
         if ((0 == inp_read_index) && (0 == module_bufs_ptr[0].actual_data_len))
         {
            in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;
            in_port_ptr->common.sdata.flags.ts_continue        = in_port_ptr->ts_to_sync.ts_continue;
            in_port_ptr->common.sdata.timestamp                = in_port_ptr->ts_to_sync.ivalue;
         }
      }

      if (TOPO_INTERLEAVED == ext_in_port_ptr->cu.media_fmt.pcm.interleaving)
      {
         src_ptr += inp_read_index;
         dst_ptr += module_bufs_ptr[0].actual_data_len;
         TOPO_MEMSCPY_NO_RET(dst_ptr,
                             out_num_bytes,
                             src_ptr,
                             out_num_bytes,
                             me_ptr->topo.gu.log_id,
                             "E2I: (0x%lX, 0x%lX)",
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         module_bufs_ptr[0].actual_data_len += out_num_bytes;
         *bytes_copied_per_buf_ptr = out_num_bytes;
      }
      else // deinterleaved packed case (between containers there's no deint-unpack)
      {
         /**
          * incoming deinterleaved packed data is copied as deinterleaved unpacked.
          * first module (port of module connected to ext in port) always gets deinterleaved unpacked.
          */

         /* NOTE:
          * In the beginning max_data_len will be equal to actual_data_length (courtesy of
          * gen_cntr_input_data_set_up_peer_cntr)
          * So, in order to keep track of correct location, the spacing,
          * though it is technically (actual_len / num_ch), we can use (max_len / num_ch) */
         uint32_t src_channel_spacing_bytes;
         uint32_t out_num_bytes_per_ch = samples_to_copy_per_chan * bytes_per_sample;

         topo_div_two_nums(ext_in_port_ptr->buf.max_data_len,
                           &src_channel_spacing_bytes,
                           inp_read_index,
                           &inp_read_index,
                           num_channels);

         src_ptr += inp_read_index;

         uint32_t actual_data_len_per_buf = module_bufs_ptr[0].actual_data_len;
         for (uint32_t b = 0; b < in_port_ptr->common.sdata.bufs_num; b++)
         {
            //[-----LLLL___|-----RRRR___]
            dst_ptr = module_bufs_ptr[b].data_ptr + actual_data_len_per_buf;
            TOPO_MEMSCPY_NO_RET(dst_ptr,
                                out_num_bytes_per_buf,
                                src_ptr,
                                out_num_bytes_per_ch,
                                me_ptr->topo.gu.log_id,
                                "E2I: (0x%lX, 0x%lX)",
                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
            src_ptr += src_channel_spacing_bytes;
         }
         module_bufs_ptr[0].actual_data_len += out_num_bytes_per_ch;

         *bytes_copied_per_buf_ptr = out_num_bytes_per_ch;
      }
      // update the remaining sample in data buffer
      ext_in_port_ptr->buf.actual_data_len -= out_num_bytes;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id,
                         "Copied data from external input buffer 0x%lx bytes_left %lu bytes_copied %lu",
                         ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                         ext_in_port_ptr->buf.actual_data_len,
                         out_num_bytes);
#endif
   }
   else if (SPF_DEINTERLEAVED_RAW_COMPRESSED == ext_in_port_ptr->cu.media_fmt.data_format)
   {

      uint32_t total_bytes_to_copy = 0, ext_int_port_actual_len_total = 0, inp_read_index = 0,
               mod_int_port_actual_len_total = 0;
      total_bytes_to_copy                    = module_bufs_ptr[0].max_data_len - module_bufs_ptr[0].actual_data_len;

      /* Evaluate total ext in actual len across all buffers and start index*/
      for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
      {
         ext_int_port_actual_len_total += ext_in_port_ptr->bufs_ptr[b].actual_data_len;
         mod_int_port_actual_len_total += module_bufs_ptr[b].actual_data_len;
         inp_read_index += ext_in_port_ptr->bufs_ptr[b].max_data_len - ext_in_port_ptr->bufs_ptr[b].actual_data_len;

         /* Evaluate total bytes to copy based on module buffer space and ext input available */
         total_bytes_to_copy = MIN(module_bufs_ptr[b].max_data_len - module_bufs_ptr[b].actual_data_len,
                                   total_bytes_to_copy); // empty space
      }

      total_bytes_to_copy = MIN(ext_int_port_actual_len_total, total_bytes_to_copy);

      /* Was EOF Set */
      if (in_port_ptr->flags.was_eof_set)
      {
         if ((0 == inp_read_index) && (0 == mod_int_port_actual_len_total))
         {
            in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;
            in_port_ptr->common.sdata.flags.ts_continue        = in_port_ptr->ts_to_sync.ts_continue;
            in_port_ptr->common.sdata.timestamp                = in_port_ptr->ts_to_sync.ivalue;
         }
      }

      /* If there is no data to copy return*/
      if (0 == total_bytes_to_copy)
      {
         return result;
      }

      uint32_t out_bytes_per_buf = 0;

      /* Copy each buffer data into its module buffer */
      for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
      {
         src_ptr = ext_in_port_ptr->bufs_ptr[b].data_ptr;
         dst_ptr = module_bufs_ptr[b].data_ptr + module_bufs_ptr[b].actual_data_len;

         out_bytes_per_buf = ext_in_port_ptr->bufs_ptr[b].actual_data_len;

         TOPO_MEMSCPY_NO_RET(dst_ptr,
                             out_bytes_per_buf,
                             src_ptr,
                             out_bytes_per_buf,
                             me_ptr->topo.gu.log_id,
                             "E2I: (0x%lX, 0x%lX)",
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

         module_bufs_ptr[b].actual_data_len += out_bytes_per_buf;
         ext_in_port_ptr->bufs_ptr[b].actual_data_len -= out_bytes_per_buf;
      }

      /* update the remaining sample in data buffer - return the bytes copied for last buffer for deint raw case  */
      /* This value returned will be in line with all other places for md offset calcualtions */
      *bytes_copied_per_buf_ptr = out_bytes_per_buf;

#ifdef VERBOSE_DEBUGGING
      for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
      {
         GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id,
                            "gen_cntr_copy_peer_olc_client_input buf %d %lu",
                            b,
                            module_bufs_ptr[b].actual_data_len);
      }
#endif
   }
   else
   {
      uint32_t bytes_to_copy;

      bytes_to_copy = (module_bufs_ptr[0].max_data_len - module_bufs_ptr[0].actual_data_len); // empty space

      bytes_to_copy             = MIN(ext_in_port_ptr->buf.actual_data_len, bytes_to_copy);
      bytes_to_copy             = MIN(*bytes_copied_per_buf_ptr, bytes_to_copy);
      *bytes_copied_per_buf_ptr = bytes_to_copy;

      if (0 == bytes_to_copy)
      {
         return result;
      }

      if (in_port_ptr->flags.was_eof_set)
      {
         if ((0 == inp_read_index) && (0 == module_bufs_ptr[0].actual_data_len))
         {
            in_port_ptr->common.sdata.flags.is_timestamp_valid = in_port_ptr->ts_to_sync.ts_valid;
            in_port_ptr->common.sdata.flags.ts_continue        = in_port_ptr->ts_to_sync.ts_continue;
            in_port_ptr->common.sdata.timestamp                = in_port_ptr->ts_to_sync.ivalue;
         }
      }

      // Set up input and output buffer pointers with correct indices
      src_ptr = ext_in_port_ptr->buf.data_ptr;
      // this still points to first location even though some data is copied already.

      inp_read_index = (ext_in_port_ptr->buf.max_data_len - ext_in_port_ptr->buf.actual_data_len);
      dst_ptr        = module_bufs_ptr[0].data_ptr + module_bufs_ptr[0].actual_data_len;

      src_ptr += inp_read_index;
      TOPO_MEMSCPY_NO_RET(dst_ptr,
                          bytes_to_copy,
                          src_ptr,
                          bytes_to_copy,
                          me_ptr->topo.gu.log_id,
                          "E2I: (0x%lX, 0x%lX)",
                          ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                          ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);

      // update the remaining sample in data buffer
      ext_in_port_ptr->buf.actual_data_len -= bytes_to_copy;
      module_bufs_ptr[0].actual_data_len += bytes_to_copy;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id, "gen_cntr_copy_peer_olc_client_input bytes copied %lu", bytes_to_copy);
#endif
   }

   return result;
}

/*===========================================================================

Copies the contents of input buffer from peer service into the first module
internal input buffer after appropriate format conversion for PCM.
outputs in interleaved format. Q31 or Q15. little endian.

At input, bytes_copied_ptr contains max inputs that we can copy.

 ===========================================================================*/
ar_result_t gen_cntr_copy_peer_cntr_input_to_int_buf(gen_cntr_t *            me_ptr,
                                                            gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                            uint32_t               *bytes_copied_per_buf_ptr)
{
   ar_result_t            result      = AR_EOK;

   result |= gen_cntr_copy_peer_or_olc_client_input(me_ptr, ext_in_port_ptr, bytes_copied_per_buf_ptr);
   // input logging is done as soon as buf is popped because otherwise deinterleaved data cannot be handled.

   // if we have copied all the data from 'data' (not EOS) buffer, release it
   // PCM decoder use case doesn't come here.
   if (gen_cntr_is_input_a_data_buffer(me_ptr, ext_in_port_ptr) && (0 == ext_in_port_ptr->buf.actual_data_len))
   {
      gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
   }

   return result;
}

static ar_result_t gen_cntr_realloc_ext_in_bufs_ptr(gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                    uint32_t                bufs_num,
                                                    POSAL_HEAP_ID           heap_id)
{
   ar_result_t result = AR_EOK;

   ext_in_port_ptr->bufs_num = bufs_num;

   if (ext_in_port_ptr->bufs_ptr)
   {
      posal_memory_free(ext_in_port_ptr->bufs_ptr);
   }

   gen_cntr_buf_t *bufs_ptr =
      (gen_cntr_buf_t *)posal_memory_malloc(ext_in_port_ptr->bufs_num * sizeof(gen_cntr_buf_t), heap_id);

   if (!bufs_ptr)
   {
      return AR_ENOMEMORY;
   }

   memset(bufs_ptr, 0, ext_in_port_ptr->bufs_num * sizeof(gen_cntr_buf_t));

   ext_in_port_ptr->bufs_ptr = bufs_ptr;

   return result;
}

static ar_result_t gen_cntr_input_data_set_up_peer_cntr(gen_cntr_t             *me_ptr,
                                                        gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                        bool_t                  is_data_buf_v2)
{
   ar_result_t            result      = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   uint32_t              *flags_ptr           = NULL;
   module_cmn_md_list_t **inpbuf_md_list_pptr = NULL;

#ifdef PROC_DELAY_DEBUG
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

   GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                       DBG_HIGH_PRIO,
                       "PROC_DELAY_DEBUG: GC Module 0x%lX: Ext Input data received on port 0x%lX",
                       module_ptr->gu.module_instance_id,
                       in_port_ptr->gu.cmn.id);
#endif

   if (is_data_buf_v2)
   {
      spf_msg_header_t         *header        = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      spf_msg_data_buffer_v2_t *input_buf_ptr = (spf_msg_data_buffer_v2_t *)&header->payload_start;
      inpbuf_md_list_pptr                     = &input_buf_ptr->metadata_list_ptr;
      spf_msg_single_buf_v2_t *data_buf_ptr   = (spf_msg_single_buf_v2_t *)(input_buf_ptr + 1);

      if (ext_in_port_ptr->bufs_num != input_buf_ptr->bufs_num)
      {
         gen_cntr_realloc_ext_in_bufs_ptr(ext_in_port_ptr, input_buf_ptr->bufs_num, me_ptr->cu.heap_id);
      }

      for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
      {
         ext_in_port_ptr->bufs_ptr[b].data_ptr        = data_buf_ptr[b].data_ptr;
         ext_in_port_ptr->bufs_ptr[b].actual_data_len = data_buf_ptr[b].actual_size;
         ext_in_port_ptr->bufs_ptr[b].max_data_len    = data_buf_ptr[b].max_size;
      }

      flags_ptr = &input_buf_ptr->flags;
   }
   else
   {
      /*prebuffers should be held in the prebuffer Q during first frame processing.*/
      if (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state)
      {
         // set flag so that prebuffers can be held into the prebuffer Q during first frame processing
         ext_in_port_ptr->cu.preserve_prebuffer       = TRUE;
         me_ptr->topo.flags.need_to_handle_frame_done = TRUE;
      }

      if (ext_in_port_ptr->cu.preserve_prebuffer)
      {
         gen_topo_exit_island_temporarily(&me_ptr->topo);
         cu_ext_in_handle_prebuffer(&me_ptr->cu, &ext_in_port_ptr->gu, 0);

         THIN_TOPO_SET_EXIT_FLAG((&me_ptr->topo), has_to_preserve_prebuffer, TRUE);

         // if prebuffer is pushed into the queue then return as no buffer available to process now
         if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
         {
            return AR_EOK;
         }
      }

      spf_msg_header_t      *header        = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;
      inpbuf_md_list_pptr                  = &input_buf_ptr->metadata_list_ptr;

      if (inpbuf_md_list_pptr)
      {
         // incremement md count by number of nodes received
         thin_topo_count_and_incr_active_md_nodes(&me_ptr->topo, input_buf_ptr->metadata_list_ptr);
      }

// if input buffer do not contain integer PCM samples per channel, return it immediately with error code.
// Under safe mode to reduce MPPS for HW-EP containers (1 ms)
#ifdef SAFE_MODE
      if (SPF_FIXED_POINT == ext_in_port_ptr->cu.media_fmt.data_format)
      {
         uint32_t unit_size = ext_in_port_ptr->cu.media_fmt.pcm.num_channels *
                              TOPO_BITS_TO_BYTES(ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample);

         if (input_buf_ptr->actual_size % unit_size)
         {
            GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                                DBG_ERROR_PRIO,
                                "Returning an input buffer that do not contain the same PCM samples on all channel!");
            return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EBADPARAM, FALSE);
         }
      }
#endif

      if (SPF_DEINTERLEAVED_RAW_COMPRESSED == ext_in_port_ptr->cu.media_fmt.data_format)
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_HIGH_PRIO,
                             "Dropping / returning input v1 message for SPF_DEINTERLEAVED_RAW_COMPRESSED - not "
                             "supported!");
         return gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }

      flags_ptr = &input_buf_ptr->flags;

      ext_in_port_ptr->buf.data_ptr        = (int8_t *)&input_buf_ptr->data_buf;
      ext_in_port_ptr->buf.actual_data_len = input_buf_ptr->actual_size;
      ext_in_port_ptr->buf.max_data_len    = input_buf_ptr->actual_size; // deinterleaving is wrt this size.
   }

   ext_in_port_ptr->buf.mem_map_handle = 0; // mem map handle is not used for inter-container buffers.
   bool_t end_of_frame                 = cu_get_bits(*flags_ptr, DATA_BUFFER_FLAG_EOF_MASK, DATA_BUFFER_FLAG_EOF_SHIFT);
   ext_in_port_ptr->flags.eof          = end_of_frame;
   in_port_ptr->common.sdata.flags.end_of_frame = FALSE;
   in_port_ptr->common.sdata.flags.marker_eos   = FALSE;

   gen_cntr_process_eos_md_from_peer_cntr(me_ptr, ext_in_port_ptr, inpbuf_md_list_pptr);

   // as soon as new data comes while we have flushing EOS in the md_list, the flushing EOS must be converted to
   // nonflushing. This helps in removing any EOS inside ext_in_port_ptr->md_list_ptr, thus helping in
   // gen_cntr_ext_in_port_needs_data_buffer

   if (ext_in_port_ptr->md_list_ptr)
   {
      uint32_t end_offset = 0;
      // deinterleaved raw compressed - take the first buffer len.
      uint32_t bytes_across_all_ch = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);

      // When md lib is compiled in nlpi, need to exit island and stay out of island for 2
      // frames if md is present at ext input, this will avoid multiple entry/exit to lpi
      gen_topo_vote_against_lpi_if_md_lib_in_nlpi(&me_ptr->topo);

      gen_topo_do_md_offset_math(me_ptr->topo.gu.log_id,
                                 &end_offset,
                                 bytes_across_all_ch + gen_cntr_get_bytes_in_ext_in_for_md(ext_in_port_ptr),
                                 in_port_ptr->common.media_fmt_ptr,
                                 TRUE /* need to add */);

      bool_t dst_has_flush_eos = FALSE, dst_has_dfg = FALSE;

      // gen_topo_md_list_modify_md_when_new_data_arrives must happen first compared to spf_list_merge_lists
      // don't clear marker EOS as EOS might be stuck inside module.
      gen_topo_md_list_modify_md_when_new_data_arrives(&me_ptr->topo,
                                                       (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                       &ext_in_port_ptr->md_list_ptr,
                                                       end_offset,
                                                       &dst_has_flush_eos,
                                                       &dst_has_dfg);
   }

   spf_list_merge_lists((spf_list_node_t **)&ext_in_port_ptr->md_list_ptr, (spf_list_node_t **)inpbuf_md_list_pptr);

   gen_cntr_copy_timestamp_from_input(me_ptr, ext_in_port_ptr);

   gen_cntr_handle_ext_in_data_flow_begin(me_ptr, ext_in_port_ptr);

   // try to copy data from the external buffer.
   result = gen_cntr_setup_internal_input_port_and_preprocess(me_ptr, ext_in_port_ptr, NULL);

   // If the internal topo buffer is filled then mark input is ready
   if (in_port_ptr->common.bufs_ptr[0].actual_data_len == in_port_ptr->common.bufs_ptr[0].max_data_len)
   {
      ext_in_port_ptr->flags.ready_to_go      = TRUE;
      in_port_ptr->common.sdata.flags.erasure = FALSE;
   }

   return result;
}

/**
 * Encode (AEnc, VEnc), split A2DP (encode, decode), decode with ASM loopback, push mode.
 */
ar_result_t gen_cntr_input_dataQ_trigger_peer_cntr(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // Take next msg off the q
   TRY(result, gen_cntr_get_input_data_cmd(me_ptr, ext_in_port_ptr));

   // process messages
   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER_V2:
      {
         TRY(result, gen_cntr_input_data_set_up_peer_cntr(me_ptr, ext_in_port_ptr, TRUE /* is_data_buf_v2*/));
         break;
      }
      case SPF_MSG_DATA_BUFFER:
      {
         TRY(result, gen_cntr_input_data_set_up_peer_cntr(me_ptr, ext_in_port_ptr, FALSE /* is_data_buf_v2 */));
         break;
      }
      case SPF_MSG_DATA_MEDIA_FORMAT:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_MED_PRIO,
                             "GEN_CNTR input media format update cmd from peer service to module, port index  (0x%lX, "
                             "%lu) ",
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.index);

         // Exit island temporarily for handling media format.If there is any event as part of this command
         // we will vote against island in that context
         gen_topo_exit_island_temporarily(&me_ptr->topo);
         gen_cntr_check_process_input_media_fmt_data_cmd(me_ptr, ext_in_port_ptr);
         break;
      }
      default:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "GEN_CNTR received unsupported message 0x%lx",
                             ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
         result = AR_EUNSUPPORTED;
         result |= gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EUNSUPPORTED, FALSE);
         break;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   return (result);
}

static ar_result_t gen_cntr_process_eos_md_from_peer_cntr(gen_cntr_t             *me_ptr,
                                                          gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          module_cmn_md_list_t  **md_list_head_pptr)
{
   ar_result_t           result      = AR_EOK;
   module_cmn_md_list_t *md_list_ptr = *md_list_head_pptr;
   module_cmn_md_list_t *node_ptr    = md_list_ptr;
   module_cmn_md_list_t *next_ptr    = NULL;

   while (node_ptr)
   {
      next_ptr                = node_ptr->next_ptr;
      module_cmn_md_t *md_ptr = node_ptr->obj_ptr;

      if (MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id)
      {
         // Need to exit island temporarily to process eos from peer cntr (create container ref)
         gen_cntr_process_eos_md_from_peer_cntr_util_(me_ptr, ext_in_port_ptr, md_list_head_pptr, node_ptr);
      }
      node_ptr = next_ptr;
   }

   // this list will be merged later to the ext_in_port's mdlist.

   return result;
}

static ar_result_t gen_cntr_free_input_data_cmd_peer_cntr(gen_cntr_t             *me_ptr,
                                                          gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                          ar_result_t             status,
                                                          bool_t                  is_flush)
{
   ar_result_t result = AR_EOK;

   switch (ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      case SPF_MSG_DATA_BUFFER:
      {
         spf_msg_header_t      *header      = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
         spf_msg_data_buffer_t *buf_ptr     = (spf_msg_data_buffer_t *)&header->payload_start;
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

         // drop any metadata that came with the buffer
         if (buf_ptr->metadata_list_ptr)
         {
            // When md lib is compiled in nlpi, need to exit island temporarily to destroy metadata
            gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(&me_ptr->topo);
            gen_topo_destroy_all_metadata(me_ptr->topo.gu.log_id,
                                          (void *)in_port_ptr->gu.cmn.module_ptr,
                                          &buf_ptr->metadata_list_ptr,
                                          TRUE /* is dropped*/);
         }

         buf_ptr->actual_size = 0;

         if (NULL != ext_in_port_ptr->buf.data_ptr) // for data commands
         {
            // reset input buffer params
            memset(&ext_in_port_ptr->buf, 0, sizeof(ext_in_port_ptr->buf));
         }
         break;
      }
      case SPF_MSG_DATA_BUFFER_V2:
      {

         spf_msg_header_t         *header  = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
         spf_msg_data_buffer_v2_t *buf_ptr = (spf_msg_data_buffer_v2_t *)&header->payload_start;
         spf_msg_single_buf_v2_t  *data_buf_ptr = (spf_msg_single_buf_v2_t *)(buf_ptr + 1);

         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

         // drop any metadata that came with the buffer
         if (buf_ptr->metadata_list_ptr)
         {
            // When md lib is compiled in nlpi, need to exit island temporarily to destroy metadata
            gen_topo_exit_lpi_temporarily_if_md_lib_in_nlpi(&me_ptr->topo);
            gen_topo_destroy_all_metadata(me_ptr->topo.gu.log_id,
                                          (void *)in_port_ptr->gu.cmn.module_ptr,
                                          &buf_ptr->metadata_list_ptr,
                                          TRUE /* is dropped*/);
         }

         for (uint32_t b = 0; b < ext_in_port_ptr->bufs_num; b++)
         {
            data_buf_ptr[b].actual_size = 0;

            if (NULL != ext_in_port_ptr->bufs_ptr[b].data_ptr)
            {
               memset(&ext_in_port_ptr->bufs_ptr[b], 0, sizeof(ext_in_port_ptr->buf));
            }
         }

         break;
      }
      default:
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_ERROR_PRIO,
                             "GEN_CNTR Received message with opcode 0x%lX",
                             ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
         break;
      }
   }
   spf_msg_ack_msg(&ext_in_port_ptr->cu.input_data_q_msg, status);

   // set payload to NULL to indicate we are not holding on to any input data msg
   ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;

   return result;
}
