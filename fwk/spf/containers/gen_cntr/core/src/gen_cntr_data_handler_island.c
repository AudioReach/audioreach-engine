/**
 * \file gen_cntr_data_handler_island.c
 * \brief
 *     This file contains functions for data handling of GEN_CNTR service
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "apm.h"

static ar_result_t gen_cntr_move_data_from_topo_to_ext_out_buf(gen_cntr_t *             me_ptr,
                                                               gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                               uint32_t                 total_len,
                                                               uint32_t                 max_empty_space);

ar_result_t gen_cntr_copy_timestamp_from_input(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t        new_ts_valid       = FALSE;
   bool_t        continue_timestamp = FALSE;
   int64_t       new_ts             = 0;
   uint32_t      size               = 0;
   gpr_packet_t *packet_ptr         = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   data_cmd_wr_sh_mem_ep_data_buffer_v2_t *pDataPayload =
      GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);
   spf_msg_header_t *     header  = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
   spf_msg_data_buffer_t *buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

   spf_msg_data_buffer_v2_t *buf_ptr_v2 = (spf_msg_data_buffer_v2_t *)&header->payload_start;

   if (SPF_MSG_CMD_GPR == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      /*gpr_packet_t *packet_ptr = (gpr_packet_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);

      data_cmd_wr_sh_mem_ep_data_buffer_v2_t *pDataPayload =
         GPR_PKT_GET_PAYLOAD(data_cmd_wr_sh_mem_ep_data_buffer_v2_t, packet_ptr);*/
      continue_timestamp =
         cu_get_bits(pDataPayload->flags, WR_SH_MEM_EP_BIT_MASK_TS_CONTINUE_FLAG, WR_SH_MEM_EP_SHIFT_TS_CONTINUE_FLAG);
      new_ts_valid = cu_get_bits(pDataPayload->flags,
                                 WR_SH_MEM_EP_BIT_MASK_TIMESTAMP_VALID_FLAG,
                                 WR_SH_MEM_EP_SHIFT_TIMESTAMP_VALID_FLAG);

      // use int64 such that sign extension happens
      new_ts = ((((int64_t)pDataPayload->timestamp_msw) << 32) | (int64_t)pDataPayload->timestamp_lsw);
      size   = pDataPayload->data_buf_size;
   }
   else if (SPF_MSG_DATA_BUFFER_V2 == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
   {
      new_ts_valid =
         cu_get_bits(buf_ptr_v2->flags, DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK, DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);
      continue_timestamp = cu_get_bits(buf_ptr_v2->flags,
                                       DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_MASK,
                                       DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_SHIFT);
      new_ts             = buf_ptr_v2->timestamp;

      spf_msg_single_buf_v2_t *buf_info = (spf_msg_single_buf_v2_t *)(buf_ptr + 1);

      for (uint32_t b = 0; b < buf_ptr_v2->bufs_num; b++)
      {
         size += buf_info[b].actual_size;
      }
   }
   else
   {
      // spf_msg_header_t *     header  = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      // spf_msg_data_buffer_t *buf_ptr = (spf_msg_data_buffer_t *)&header->payload_start;

      new_ts_valid =
         cu_get_bits(buf_ptr->flags, DATA_BUFFER_FLAG_TIMESTAMP_VALID_MASK, DATA_BUFFER_FLAG_TIMESTAMP_VALID_SHIFT);
      continue_timestamp = cu_get_bits(buf_ptr->flags,
                                       DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_MASK,
                                       DATA_BUFFER_FLAG_TIMESTAMP_CONTINUE_SHIFT);
      new_ts             = buf_ptr->timestamp;
      size               = buf_ptr->actual_size;
   }

   // ignore buffers without data because GK messages without data could be sent for sending metadata;
   // we should use those timestamps as they are probably just previously set value that are not cleared when MD is
   // sent.
   if (size)
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
      bool_t                 discont_ts  = gen_topo_check_copy_incoming_ts(&me_ptr->topo,
                                                          (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                          in_port_ptr,
                                                          new_ts,
                                                          new_ts_valid,
                                                          continue_timestamp);

      if (discont_ts)
      {
         gen_cntr_check_set_input_discontinuity_flag(me_ptr, ext_in_port_ptr, FALSE /* is_mf_pending */);
      }

      // new buffer's timestamp is copied to the input port so mark all the data as previous buffer.
      // test: frontend/tst/record/gen_cntr_priority_sync/cfg/sync_with_mux_demux_2_port_record_9 (with timesamp based
      // synchronization enable in priority sync)
      in_port_ptr->bytes_from_prev_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;

#ifdef VERBOSE_DEBUGGING
      uint32_t flags = 0;
      if (SPF_MSG_CMD_GPR == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
      {
         flags = pDataPayload->flags;
      }
      else if (SPF_MSG_DATA_BUFFER_V2 == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode)
      {
    	  flags = buf_ptr_v2->flags;
      }
      else
      {
         flags = buf_ptr->flags;
      }
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Incoming timestamp 0x%lx%lx (%ld), valid %u, continue %u. size %lu, bytes_from_prev_buf %lu. "
                   "flags0x%08lX",
                   (uint32_t)new_ts,
                   (uint32_t)(new_ts >> 32),
                   (int32_t)new_ts,
                   new_ts_valid,
                   continue_timestamp,
                   size,
                   in_port_ptr->bytes_from_prev_buf,
                   flags);
#endif
   }

   return AR_EOK;
}

ar_result_t gen_cntr_timestamp_set_next_out_buf_ts(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   ext_out_port_ptr->next_out_buf_ts.ts_continue = out_port_ptr->common.sdata.flags.ts_continue;
   ext_out_port_ptr->next_out_buf_ts.valid       = out_port_ptr->common.sdata.flags.is_timestamp_valid;
   ext_out_port_ptr->next_out_buf_ts.value       = out_port_ptr->common.sdata.timestamp;

   return AR_EOK;
}

/**
 * maximum number frames to process across all output ports is
 * determined as follows:
 * across multiple output ports, the min number of frames processed.
 *
 * for most use cases this is one.
 * only for encoding with multiple frames, the number is not unity.
 */
static bool_t gen_cntr_need_to_process_frames(gen_cntr_t *me_ptr,
                                              uint32_t    inner_loop_count,
                                              bool_t *    need_to_exit_outer_loop_ptr,
                                              bool_t *    is_output_ready_ptr)
{
   bool_t                      PROCESS_MORE_FRAMES = TRUE;
   bool_t                      EXIT_PROCESSING     = FALSE;
   gen_topo_process_info_t *   process_info_ptr    = &me_ptr->topo.proc_context.process_info;
   gen_topo_process_context_t *pc_ptr              = &me_ptr->topo.proc_context;

   // need to set need_to_exit_outer_loop_ptr whenever we don't need to evaluate gen_cntr_wait_for_any_ext_trigger and
   // still exit.

   uint32_t ext_out_port_index = 0;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), ext_out_port_index++)
   {
      gen_cntr_ext_out_port_t *temp_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "(MIID, O/p) (0x%lx, 0x%lx) Release buf? max frames %d, num frames in %d, md list ptr %lx",
                   temp_ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   temp_ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                   temp_ext_out_port_ptr->max_frames_per_buffer,
                   temp_ext_out_port_ptr->num_frames_in_buf,
                   temp_ext_out_port_ptr->md_list_ptr);
#endif

      if ((gen_cntr_ext_out_port_has_buffer(&temp_ext_out_port_ptr->gu)) &&
          (temp_ext_out_port_ptr->max_frames_per_buffer == temp_ext_out_port_ptr->num_frames_in_buf))
      {
         *is_output_ready_ptr                                                           = TRUE;
         pc_ptr->ext_out_port_scratch_ptr[ext_out_port_index].flags.release_ext_out_buf = TRUE;
         continue;
      }

      // if there's MD when num_frames_in_buf is zero, release, otherwise wait for num_frames_in_buf to become
      // max_frames_per_buffer
      if (((0 == temp_ext_out_port_ptr->num_frames_in_buf) && temp_ext_out_port_ptr->md_list_ptr) ||
          pc_ptr->ext_out_port_scratch_ptr[ext_out_port_index].flags.release_ext_out_buf)
      {
         *is_output_ready_ptr                                                           = TRUE;
         pc_ptr->ext_out_port_scratch_ptr[ext_out_port_index].flags.release_ext_out_buf = TRUE;
         continue;
      }
   }

   // cannot process, need to release the output buffer first and wait for trigger to process.
   if (*is_output_ready_ptr)
   {
      return EXIT_PROCESSING;
   }

   // if signal triggered loop only once. we cannot exit outer loop when there are any trigger policy modules.
   if (GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger)
   {
      return (0 == inner_loop_count);
   }

   // even if external trigger needs to be waited on, for some module, there may be other modules which might work.
   // hence ultimate condition to break is !anything_changed only. Use ext_trigger_cond_not_met for non-trigger policy
   // cases.
   if ((0 == me_ptr->topo.num_data_tpm) && !gen_cntr_is_any_path_ready_to_process(me_ptr))
   {
      return EXIT_PROCESSING;
   }

   /**
    * TRM & SPR modules can continuously keep dropping data. In the meantime if a signal fires,
    * we should go back and handle it first. Data drop can be done after handling signal.
    * since this is do while, is_entry is always FALSE
    */
   if ((0 == me_ptr->topo.num_data_tpm) && (NULL != me_ptr->st_module.trigger_signal_ptr) &&
       posal_signal_is_set_inline(me_ptr->st_module.trigger_signal_ptr))
   {
      *need_to_exit_outer_loop_ptr = TRUE;
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Signal triggered container with trigger policy module has a signal set during data dropping");
#endif
      return EXIT_PROCESSING;
   }

   // if nothing changed, don't continue looping.
   if (!process_info_ptr->anything_changed)
   {
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Breaking inner loop as nothing changed");
      return EXIT_PROCESSING;
   }

   // if topo is non real time then break the loop to process command Q.
   // this is to avoid delay in processing command Q during NT mode data processing.
   if (!me_ptr->cu.flags.is_real_time &&
       posal_channel_poll_inline(me_ptr->cu.channel_ptr, GEN_CNTR_CMD_BIT_MASK))
   {
      me_ptr->topo.flags.process_pending = TRUE;
      *need_to_exit_outer_loop_ptr       = TRUE;
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Breaking inner loop to process command Q");
      return EXIT_PROCESSING;
   }

   // if there are frames to be processed, but every time we loop, nothing happens, then looping may be happening inf
   // give a margin of 1000 before stopping
   if (inner_loop_count > 1000)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Infinite loop alert: inner_loop_count %lu",
                   inner_loop_count);
      *need_to_exit_outer_loop_ptr = TRUE;
      *is_output_ready_ptr         = FALSE;

#ifdef SIM
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Crashing on SIM");
      spf_svc_crash();
#endif
      return EXIT_PROCESSING;
   }

   return PROCESS_MORE_FRAMES;
}

static ar_result_t gen_cntr_check_output_space_availability(gen_cntr_t *             me_ptr,
                                                            gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                            uint32_t                 ext_out_port_index,
                                                            bool_t *                 buf_available,
                                                            bool_t *                 space_available,
                                                            uint32_t *               total_len_ptr,
                                                            uint32_t *               max_empty_space_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_output_port_t *    out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   gen_topo_process_context_t *pc_ptr       = &me_ptr->topo.proc_context;

   *total_len_ptr = gen_topo_get_total_actual_len(&out_port_ptr->common);

   if (gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      /* For v2 usecase, we need to make sure all buffers are available and have space. So start with both buf available
       * and space available true. If buf is found to not be available make space available also false. If buf is
       * available but space is not then buf_available is true while space_available is false. Space not available is
       * correct when buf is not available so this is not an error case. When buf is available but space is not then
       * this is an error case */
      *space_available = TRUE;
      *buf_available   = TRUE;

      for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
      {
         /* Check if there is space to copy the buffers. If buffer is reused no need to check.
          * Even if one buffer doesn't have space don't copy for the entire set. */
         *max_empty_space_ptr =
            ext_out_port_ptr->bufs_ptr[b].max_data_len - ext_out_port_ptr->bufs_ptr[b].actual_data_len;

         uint32_t actual_len = out_port_ptr->common.flags.is_pcm_unpacked
                                  ? out_port_ptr->common.bufs_ptr[0].actual_data_len
                                  : out_port_ptr->common.bufs_ptr[b].actual_data_len;

         if (ext_out_port_ptr->bufs_ptr[b].data_ptr)
         {
            if (ext_out_port_ptr->bufs_ptr[b].data_ptr != out_port_ptr->common.bufs_ptr[b].data_ptr)
            {
               if (*max_empty_space_ptr < actual_len)
               {
                  pc_ptr->ext_out_port_scratch_ptr[ext_out_port_index].flags.release_ext_out_buf = TRUE;

                  /* Here buf_available is true while space_available is not sufficient - return error */
                  *space_available = FALSE;

                  if (0 == ext_out_port_ptr->num_frames_in_buf)
                  {
                     uint32_t actual_data_len = 0, max_data_len = 0;
                     gen_cntr_get_ext_out_total_actual_max_data_len(ext_out_port_ptr, &actual_data_len, &max_data_len);

                     GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                                         DBG_ERROR_PRIO,
                                         "Output buffer is too small (%lu - %lu) < (%lu) ",
                                         max_data_len,
                                         actual_data_len,
                                         actual_len);
                     // TODO: ext_out_port_ptr->buf.status |= AR_ENEEDMORE; // set the error
                     return AR_EFAILED;
                  }
                  else
                  {
                     return AR_EOK;
                  }
               }
            }
         }
         else
         {
            *buf_available = FALSE;

            /* If buf is not present it is implied that space is not available */
            *space_available = FALSE;
         }
      }
   }
   else
   {

      // ext_out_port_ptr->buf may already have something in case of encoder output
      // ext_out_port_ptr->int_port->buf will be fully copied to ext_out_port_ptr->buf (if different)

      *max_empty_space_ptr = ext_out_port_ptr->buf.max_data_len - ext_out_port_ptr->buf.actual_data_len;

      // Check if there is enough space to the ext out buffer only if,
      //      1. If the external buffer exist. If buffer doesnt exist then space also is not available.
      //      2. And if the ext buffer is not being reused as internal buffer. If its being reused ext buffer
      //         already has the processed data so we don't have to copy.

      *buf_available = (NULL != ext_out_port_ptr->buf.data_ptr);

      /* If buf is available initialize space_available based on this - case of not reusing buffer is computed later */
      *space_available = *buf_available;

      if (*buf_available && (ext_out_port_ptr->buf.data_ptr != out_port_ptr->common.bufs_ptr[0].data_ptr))
      {
         // is there space to write this frame
         *space_available = ((*max_empty_space_ptr >= *total_len_ptr));

         // when out buf is too small to fit even one frame it's an error. otherwise, it's ok
         if (!(*space_available))
         {
            pc_ptr->ext_out_port_scratch_ptr[ext_out_port_index].flags.release_ext_out_buf = TRUE;

            if (0 == ext_out_port_ptr->num_frames_in_buf)
            {
               GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                                   DBG_ERROR_PRIO,
                                   "Output buffer is too small (%lu - %lu) < (%lu) ",
                                   ext_out_port_ptr->buf.max_data_len,
                                   ext_out_port_ptr->buf.actual_data_len,
                                   *total_len_ptr);
               ext_out_port_ptr->buf.status |= AR_ENEEDMORE; // set the error
               return AR_EFAILED;
            }
            else
            {
               return AR_EOK;
            }
         }
      }
   }

   return result;
}

/**
 * for PCM:  removal of initial or trailing zeros
 *
 * Also copies to ext out port buf from module out buf.
 */
ar_result_t gen_cntr_post_process_ext_out_buffer(gen_cntr_t              *me_ptr,
                                                 gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                 uint32_t                 ext_out_port_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   uint32_t max_empty_space = 0, total_len = 0;

   /* If buffer is not available then space not being available is expected - this is overrun case. If buffer is present
    * but space is not available this is error. i.e. there is no case with space available and buffer absent */
   bool_t all_buf_available = FALSE, space_available = FALSE;

   result = gen_cntr_check_output_space_availability(me_ptr,
                                                     ext_out_port_ptr,
                                                     ext_out_port_index,
                                                     &all_buf_available,
                                                     &space_available,
                                                     &total_len,
                                                     &max_empty_space);

   /* Check space availability only if buffer is present - if buffer doesn't exist then no space is expected. If buffer
    * exists but no space is available this is an error */
   if (all_buf_available && !space_available)
   {
      if (result)
      {
         THROW(result, AR_EFAILED);
      }
      else
      {
         return result;
      }
   }

   /* If buffer itself is not available then overrun if ST or return */
   if (!all_buf_available)
   {
      if ((GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger) &&
          gen_topo_is_port_in_realtime_path(&out_port_ptr->common))
      {
         gen_topo_exit_island_temporarily(&me_ptr->topo);
         gen_cntr_handle_st_overrun_at_post_process(me_ptr, ext_out_port_ptr);
      }

      // If the ext buffer is not present, return from post process. Reaches this point in FTRT mode or GEN_CNTR
      // overrun, where process is called even without ext output buffers .

      return AR_EOK;
   }

   // If the external output buffer is being reused as the topo buffer then both the buffer pointers are same.
   // Note that ext input buffer could have been propagated to ext output as well, hence make sure buffer ptr is
   // same as ext buffer
   // In this case we don't need to memcpy,
   //   deint-packed from deint-unpacked conversion if required happens in gen_cntr_send_peer_cntr_out_buffers.
   bool_t skip_copy = FALSE;
   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF == out_port_ptr->common.flags.buf_origin)
   {
      if (gen_cntr_is_ext_out_v2(ext_out_port_ptr))
      {
         if ((ext_out_port_ptr->bufs_ptr[0].data_ptr + ext_out_port_ptr->bufs_ptr[0].actual_data_len) ==
             out_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
            {
               ext_out_port_ptr->bufs_ptr[b].actual_data_len += out_port_ptr->common.flags.is_pcm_unpacked
                                                                   ? out_port_ptr->common.bufs_ptr[0].actual_data_len
                                                                   : out_port_ptr->common.bufs_ptr[b].actual_data_len;
            }
            gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
            skip_copy = TRUE;
         }
      }
      else if ((ext_out_port_ptr->buf.data_ptr + ext_out_port_ptr->buf.actual_data_len) ==
               out_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         ext_out_port_ptr->buf.actual_data_len += total_len;
         gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
         skip_copy = TRUE;
      }
   }

   // copy from topo to ext-out buffer
   if (FALSE == skip_copy)
   {
      gen_cntr_move_data_from_topo_to_ext_out_buf(me_ptr, ext_out_port_ptr, total_len, max_empty_space);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

#ifdef VERBOSE_DEBUGGING
   if (gen_cntr_is_ext_out_v2(ext_out_port_ptr))
   {
      for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
      {
         uint32_t actual_data_len = out_port_ptr->common.flags.is_pcm_unpacked
                                       ? out_port_ptr->common.bufs_ptr[0].actual_data_len
                                       : out_port_ptr->common.bufs_ptr[b].actual_data_len;
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "postprocess:after: %d in actual_data_len = %lu, out actual_data_len = %lu",
                      b,
                      actual_data_len,
                      ext_out_port_ptr->bufs_ptr[b].actual_data_len);
      }
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "postprocess:after: (0x%lX, 0x%lX) in actual_data_len = %lu, out actual_data_len = %lu",
                   out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   total_len,
                   ext_out_port_ptr->buf.actual_data_len);
   }
#endif

   return AR_EOK;
}

static ar_result_t gen_cntr_move_data_from_topo_to_ext_out_buf(gen_cntr_t *             me_ptr,
                                                               gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                               uint32_t                 total_len,
                                                               uint32_t                 max_empty_space)
{
   ar_result_t             result                   = AR_EOK;
   gen_topo_output_port_t *out_port_ptr             = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   topo_media_fmt_t *      module_out_media_fmt_ptr = out_port_ptr->common.media_fmt_ptr;

   // can be peer-svc or ext client case; in peer svc this is deemed suboptimal. this can happen during Signal
   // trigger overrun.
   // client buf cannot be used for module as it may not be of right size.
   if (gen_topo_is_ext_peer_container_connected(out_port_ptr))
   {
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_ERROR_PRIO,
                          "Warning: postprocess: using different buf for ext 0x%p and int ports 0x%p. this is "
                          "unexpected",
                          ext_out_port_ptr->buf.data_ptr,
                          out_port_ptr->common.bufs_ptr[0].data_ptr);
   }

   uint32_t            num_channels   = module_out_media_fmt_ptr->pcm.num_channels;
   uint32_t            src_actual_len = total_len;
   int8_t *            src_ptr        = out_port_ptr->common.bufs_ptr[0].data_ptr;
   int8_t *            dst_ptr        = ext_out_port_ptr->buf.data_ptr;
   uint32_t            num_out_bytes  = MIN(src_actual_len, max_empty_space);

   // always assume data starts from beginning in src_ptr. when ext out buf is not large enough to hold,
   // what's left of input has to be memmoved to beginning.

   if (out_port_ptr->common.flags.is_pcm_unpacked)
   {
      uint32_t num_out_bytes_per_ch      = capi_cmn_divide(num_out_bytes, num_channels);
      uint32_t dst_channel_spacing_bytes = capi_cmn_divide(ext_out_port_ptr->buf.max_data_len, num_channels);
      uint32_t data_fill_offset          = capi_cmn_divide(ext_out_port_ptr->buf.actual_data_len, num_channels);

      GEN_CNTR_SIM_DEBUG(me_ptr->topo.gu.log_id, "Warning: postprocess: deint-unpacked handling. this is suboptimal");

      // conversion to deint-packed is done in gen_cntr_send_peer_cntr_out_buffers in case of peer-svc
      // in DSP client case, data is sent in the same way as last module output (usually interleaved)
      uint32_t remaining            = out_port_ptr->common.bufs_ptr[0].actual_data_len - num_out_bytes_per_ch;
      uint32_t max_data_len_per_buf = out_port_ptr->common.bufs_ptr[0].max_data_len;

      for (uint32_t b = 0; b < out_port_ptr->common.sdata.bufs_num; b++)
      {
         TOPO_MEMSCPY_NO_RET(dst_ptr + b * dst_channel_spacing_bytes + data_fill_offset,
                             num_out_bytes_per_ch,
                             out_port_ptr->common.bufs_ptr[b].data_ptr,
                             num_out_bytes_per_ch,
                             me_ptr->topo.gu.log_id,
                             "POST: (0x%lX, 0x%lX)",
                             out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                             out_port_ptr->gu.cmn.id);
         if (remaining)
         {
            TOPO_MEMSMOV_NO_RET(out_port_ptr->common.bufs_ptr[b].data_ptr,
                                max_data_len_per_buf,
                                out_port_ptr->common.bufs_ptr[b].data_ptr + num_out_bytes_per_ch,
                                remaining,
                                me_ptr->topo.gu.log_id,
                                "POST: (0x%lX, 0x%lX)",
                                out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                                out_port_ptr->gu.cmn.id);
         }
      }

      out_port_ptr->common.bufs_ptr[0].actual_data_len = remaining;
      (void)data_fill_offset;
      (void)dst_channel_spacing_bytes;
      ext_out_port_ptr->buf.actual_data_len += num_out_bytes;
   }
   else // raw and de-intlv packed cases
   {
      gen_topo_exit_island_temporarily(&me_ptr->topo);
      result = gen_cntr_mv_data_from_topo_to_ext_out_buf_npli_(me_ptr, ext_out_port_ptr, total_len, max_empty_space);
   }

   (void)src_ptr;
   (void)dst_ptr;

   return result;
}

ar_result_t gen_cntr_handle_process_events_and_flags(gen_cntr_t *             me_ptr,
                                                     gen_topo_process_info_t *process_info_ptr,
                                                     bool_t *                 mf_th_ps_event_ptr,
                                                     gu_module_list_t *       mf_start_module_list_ptr)
{
   ar_result_t result                 = AR_EOK;
   bool_t      prio_bumped_up_locally = FALSE;
   posal_thread_prio_t original_prio          = 0;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr    = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr     = NULL;

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   // this event handler is in process context and must not include handling for the events set in the command context.
   // Therefore avoid reconciliation of the event flags.
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do reconcile*/);

   if ((capi_event_flag_ptr->word || fwk_event_flag_ptr->word))
   {
      gen_topo_exit_island_temporarily(&me_ptr->topo);

      // Need to save the original prio (includes modules vote for thread prio) before bump up
      original_prio = posal_thread_prio_get2(me_ptr->cu.cmd_handle.thread_id);

      // Prio will either bump up or remain as original
      prio_bumped_up_locally = gen_cntr_check_bump_up_thread_priority(&me_ptr->cu, TRUE /* is bump up*/, original_prio);
   }

   if (capi_event_flag_ptr->media_fmt_event)
   {
      // propagating here will be successful only if downstream is empty. Otherwise, propagation in copy_prev_to_next
      // will help. also propagating here helps in not handling MF/thresh events multiple times.
      gen_topo_propagate_media_fmt_from_module(&me_ptr->topo, TRUE /* is_data_path*/, mf_start_module_list_ptr);
	  *mf_th_ps_event_ptr = TRUE;
   }

   // Handle data port threshold change.
   if (capi_event_flag_ptr->port_thresh)
   {
      /**
       * if intermediate (non-last) module raises port-thresh change, then,
       * subsequent module won't have data & hence process_frames loop
       * will make sure process is called again for all modules.
       *
       * if port thresh is raised, then process wouldn't have produced any output. but with media fmt, data would've
       * gotten produced. this is why process_info_ptr->port_thresh_event is tracked. process is called on the
       * modules before reading more input.
       */
      process_info_ptr->port_thresh_event = capi_event_flag_ptr->port_thresh;

      /** see comment next to process_info_ptr->port_thresh_event*/
      process_info_ptr->anything_changed |= process_info_ptr->port_thresh_event;

      me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(me_ptr);
      *mf_th_ps_event_ptr = TRUE;
   }

   // Loop back if there is process state update
   if (capi_event_flag_ptr->process_state)
   {
      *mf_th_ps_event_ptr = TRUE;
   }

   gen_cntr_handle_fwk_events_in_data_path(me_ptr);

   if (me_ptr->cu.voice_info_ptr && me_ptr->cu.event_list_ptr)
   {
      bool_t   hw_acc_proc_delay_event = me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change;
      uint32_t topo_hw_accl_proc_delay = 0;

      if (me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change)
      {
         gen_topo_aggregate_hw_acc_proc_delay(&me_ptr->topo, FALSE, &topo_hw_accl_proc_delay);
         me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change = FALSE;
      }

      cu_check_and_raise_voice_proc_param_update_event(&me_ptr->cu,
                                                       me_ptr->topo.gu.log_id,
                                                       topo_hw_accl_proc_delay,
                                                       hw_acc_proc_delay_event);
   }

   if (prio_bumped_up_locally)
   {
      gen_cntr_check_bump_up_thread_priority(&me_ptr->cu, FALSE /* is bump up*/, original_prio);
   }

   return result;
}

/**
 * after input and output buffers are set-up, process is called repeatedly
 * under frames_per_buffer worth of frames are processed or until either input or output is exhausted.
 */
static ar_result_t gen_cntr_data_process_one_frame(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                    out_port_index   = 0;
   gen_topo_process_info_t *   process_info_ptr = &me_ptr->topo.proc_context.process_info;
   gen_topo_process_context_t *pc_ptr           = &me_ptr->topo.proc_context;

   // Flag which is set if there is a media format, threshold or process state change
   bool_t mf_th_ps_event = FALSE;

   if (process_info_ptr->port_thresh_event)
   {
      // see comment next to port_thresh_event
      process_info_ptr->anything_changed  = TRUE;
      process_info_ptr->port_thresh_event = FALSE;
   }
   else
   {
      // by this time, input and output buffers must be popped & assigned.
      for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
           (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         gen_cntr_ext_in_port_t *temp_ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         // don't check port state & data flow state here as it's checked already inside gen_topo_in_port_needs_data

         if (!temp_ext_in_port_ptr->flags.ready_to_go)
         {
            TRY(result,
                gen_cntr_setup_internal_input_port_and_preprocess(me_ptr, temp_ext_in_port_ptr, process_info_ptr));
         }
         else
         {
            temp_ext_in_port_ptr->flags.ready_to_go = FALSE;
         }
      }
   }

   out_port_index = 0;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      uint32_t actual_data_len = 0;
      gen_cntr_get_ext_out_total_actual_max_data_len(ext_out_port_ptr, &actual_data_len, NULL);

      pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len = actual_data_len;

      // assigns ext buffer to topo buffer if possible, else topo buffer will be assigned during module process context
      // Only peer cntr ext out and Rd shm ep has valid cb. Offload Rd shm ep will not reuse client buffer.
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
      if (ext_out_port_ptr->vtbl_ptr->setup_topo_buf && (TOPO_PORT_STATE_STARTED == out_port_ptr->common.state))
      {
         TRY(result, ext_out_port_ptr->vtbl_ptr->setup_topo_buf(me_ptr, ext_out_port_ptr));
      }
   }

   for (uint8_t i = 0; i < me_ptr->cu.gu_ptr->num_parallel_paths; i++)
   {
      gu_module_list_t *start_module_list_ptr = me_ptr->topo.started_sorted_module_list_ptr;
      if (0 == me_ptr->wait_mask_arr[i])
      {
         while (TRUE)
         {
            // Flag to indicate Media format, threshold or process state change event
            mf_th_ps_event = FALSE;
            result      = gen_topo_topo_process(&me_ptr->topo, &start_module_list_ptr, &i);

            // here MF must propagate from next module, starting from current module overwrites any data that module
            // might have outputed in this call.
            gen_cntr_handle_process_events_and_flags(me_ptr,
                                                     process_info_ptr,
                                                     &mf_th_ps_event,
                                                     (start_module_list_ptr ? start_module_list_ptr->next_ptr : NULL));

            if (mf_th_ps_event && (NULL != start_module_list_ptr))
            {
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_LOW_PRIO,
                            "Looping back to topo process from module 0x%08lX ",
                            start_module_list_ptr->module_ptr->module_instance_id);
            }
            else
            {
               break;
            }
         }
      }
   }

   /**  For each external out port, postprocess data, send media fmt and data down. */
   out_port_index = 0;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      gen_cntr_ext_out_port_t *temp_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      /** Prepares external output buffer, sends pending media format as well. Buffer will be delivered in a different
       * context. refer gen_cntr_deliver_output() */
      result |=
         gen_cntr_post_process_ext_output_port(me_ptr, process_info_ptr, pc_ptr, temp_ext_out_port_ptr, out_port_index);
   }

   // free ext input if it was propagated from the external input, clear after ext output port post process because
   // ext input buffer could have been propagated to internal output connected to the ext output. (Eg: cntr with only Wr shm module)
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      // check if a borrowed buffer is stuck in the NBLC path and relace with a topo buffer.
      result |= gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports(me_ptr, ext_in_port_ptr, in_port_ptr, FALSE);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * Post process needs to called after topo process for each external ouptut port.
 * Handles any pending media format and moves data from topo to ext buffer if required.
 * Note this function doesnt update the flag is external ready to go.
 */
ar_result_t gen_cntr_post_process_ext_output_port(gen_cntr_t                 *me_ptr,
                                                  gen_topo_process_info_t    *process_info_ptr,
                                                  gen_topo_process_context_t *pc_ptr,
                                                  gen_cntr_ext_out_port_t    *ext_out_port_ptr,
                                                  uint32_t                    out_port_index)
{
   ar_result_t             result       = AR_EOK;
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   // Skip postprocessing on ports which are not started.
   if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
   {
      return AR_EOK;
   }

   uint32_t bytes_produced_by_topo = out_port_ptr->common.bufs_ptr[0].actual_data_len;
   uint32_t bytes_produced_by_pp   = bytes_produced_by_topo;

   // if marker EOS is set this it's flushing EOS
   if (out_port_ptr->common.sdata.flags.marker_eos)
   {
      out_port_ptr->common.sdata.flags.marker_eos = FALSE;
   }

   /**
    * copy MF to ext-out even if bytes_produced_by_topo is zero.
    * in gapless use cases, if EOS comes before initial silence is removed, then we send EOS before any data is sent.
    * before sending EOS, we need to send MF (true for any MD)
    */
   if (out_port_ptr->common.flags.media_fmt_event)
   {
      /** if we are holding on to old data while media fmt changed, then we need to first release old data
       */
      if (ext_out_port_ptr->vtbl_ptr->get_filled_size &&
          ext_out_port_ptr->vtbl_ptr->get_filled_size(me_ptr, ext_out_port_ptr))
      {
         pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = TRUE;
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "postprocess:media format changed while holding on to old data at output");
#endif
         return AR_EOK;
      }

      // Don't clear the out_port_ptr->common.flags.media_fmt_event if old data is left.
      gen_cntr_get_output_port_media_format_from_topo(ext_out_port_ptr, TRUE /* updated to unchanged */);
   }

   if (bytes_produced_by_topo)
   {
      result |= gen_cntr_post_process_ext_out_buffer(me_ptr, ext_out_port_ptr, out_port_index);

      if (gen_cntr_is_ext_out_v2(ext_out_port_ptr))
      {
         bytes_produced_by_pp = 0;
         for (uint32_t b = 0; b < ext_out_port_ptr->bufs_num; b++)
         {
            bytes_produced_by_pp += ext_out_port_ptr->bufs_ptr[b].actual_data_len;
         }
         bytes_produced_by_pp -= pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len;
      }
      else
      {
         bytes_produced_by_pp = ext_out_port_ptr->buf.actual_data_len -
                                pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len;
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "postprocess output: (0x%lx, 0x%lx) module (%lu of %lu), ext out (%lu of %lu), "
                   "bytes_produced_by_pp %lu",
                   out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   out_port_ptr->gu.cmn.id,
                   out_port_ptr->common.bufs_ptr[0].actual_data_len,
                   out_port_ptr->common.bufs_ptr[0].max_data_len,
                   ext_out_port_ptr->buf.actual_data_len,
                   ext_out_port_ptr->buf.max_data_len,
                   bytes_produced_by_pp);
#endif
   }

   // merge the metadata if required.
   spf_list_merge_lists((spf_list_node_t **)&ext_out_port_ptr->md_list_ptr,
                        (spf_list_node_t **)&out_port_ptr->common.sdata.metadata_list_ptr);

   if (bytes_produced_by_pp) // in this frame
   {
      process_info_ptr->anything_changed = TRUE;
      if (0 == ext_out_port_ptr->num_frames_in_buf)
      {
         // set next_out_buf_ts here, because when out buf is popped we don't know if inp has already arrived.
         gen_cntr_timestamp_set_next_out_buf_ts(me_ptr, ext_out_port_ptr);
      }

      if (ext_out_port_ptr->vtbl_ptr->fill_frame_md)
      {
         uint32_t frame_offset_in_ext_buf = 0;
         gen_topo_do_md_offset_math(me_ptr->topo.gu.log_id,
                                    &frame_offset_in_ext_buf,
                                    pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len,
                                    out_port_ptr->common.media_fmt_ptr,
                                    TRUE /* need to add */);

         bool_t release_out_buf = pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf;
         ext_out_port_ptr->vtbl_ptr->fill_frame_md(me_ptr,
                                                   ext_out_port_ptr,
                                                   bytes_produced_by_pp,
                                                   frame_offset_in_ext_buf,
                                                   &release_out_buf);

         pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = release_out_buf;
      }

      ext_out_port_ptr->num_frames_in_buf++;

      if (me_ptr->topo.flags.need_to_handle_frame_done)
      {
         gen_cntr_handle_frame_done(&me_ptr->topo, out_port_ptr->gu.cmn.module_ptr->path_index);
      }

      if (out_port_ptr->common.sdata.flags.is_timestamp_valid && out_port_ptr->common.flags.is_mf_valid &&
          SPF_IS_PACKETIZED_OR_PCM(out_port_ptr->common.media_fmt_ptr->data_format))
      {
         out_port_ptr->common.sdata.timestamp = (out_port_ptr->common.sdata.timestamp +
                                                 (int64_t)topo_bytes_to_us(bytes_produced_by_pp,
                                                                           out_port_ptr->common.media_fmt_ptr,
                                                                           &out_port_ptr->common.fract_timestamp_pns));
      }
   }
   else if (ext_out_port_ptr->vtbl_ptr->write_metadata)
   {
      // Fill all external metadata into the client buffer even if there are no data frames
      // Needed for c2 usecases where start/end md is sent out even if data frame is dropped
      bool_t release_out_buf = pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf;

      uint32_t frame_offset_in_ext_buf = 0;
      gen_topo_do_md_offset_math(me_ptr->topo.gu.log_id,
                                 &frame_offset_in_ext_buf,
                                 pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len,
                                 out_port_ptr->common.media_fmt_ptr,
                                 TRUE /* need to add */);

      ext_out_port_ptr->vtbl_ptr->write_metadata(me_ptr, ext_out_port_ptr, frame_offset_in_ext_buf, &release_out_buf);

      pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = release_out_buf;
   }

   // Mark output buffer for force return
   // Generally we use the external output buffer for the last output port.
   // In case we use the internal buffer because ext out buffers were not available, every time we'll have to
   // copy data from topo out to ext out buffer - this is not optimal
   // So we mark the output buffer as force return so we can try and use the external out buffer for the
   // last module output in subsequent process calls
   out_port_ptr->common.flags.force_return_buf = TRUE;

   // Return the internal output buffer only if its not using the external output buffer (check inside).
   gen_topo_output_port_return_buf_mgr_buf(&me_ptr->topo, out_port_ptr);

   // clear external buffer ptr from the internal ports
   result |= gen_cntr_clear_borrowed_ext_out_buffer_from_int_ports(me_ptr, ext_out_port_ptr, out_port_ptr);

   // when EOF propagated from the input due to discontinuity, reaches output port, we can release output buffer.
   if (out_port_ptr->common.sdata.flags.end_of_frame)
   {
      if (ext_out_port_ptr->vtbl_ptr->get_filled_size &&
          ext_out_port_ptr->vtbl_ptr->get_filled_size(me_ptr, ext_out_port_ptr))
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Release buf due to EoF");
         pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = TRUE;
      }

      if (!SPF_IS_RAW_COMPR_DATA_FMT(out_port_ptr->common.media_fmt_ptr->data_format))
      {
         out_port_ptr->common.sdata.flags.end_of_frame = FALSE;
      }
   }
   return result;
}

void gen_cntr_reset_process_info(gen_cntr_t *me_ptr)
{
   gen_topo_process_context_t *pc_ptr     = &me_ptr->topo.proc_context;
   pc_ptr->process_info.port_thresh_event = FALSE;
   pc_ptr->process_info.anything_changed  = FALSE;
   // pc_ptr->process_info.probing_for_tpm_activity = FALSE;
   pc_ptr->process_info.num_data_tpm_done = 0;

   for (uint32_t out_port_index = 0; out_port_index < pc_ptr->num_ext_out_ports; out_port_index++)
   {
      pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = FALSE;
   }
}

static ar_result_t gen_cntr_deliver_output(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   bool_t   out_media_fmt_changed = FALSE;
   bool_t   FORCE_AGGREGATE_FALSE = FALSE;
   uint32_t out_port_index        = 0;

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      out_media_fmt_changed |= ext_out_port_ptr->flags.out_media_fmt_changed;

      if (ext_out_port_ptr->flags.out_media_fmt_changed ||
          me_ptr->topo.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf)
      {
         ext_out_port_ptr->vtbl_ptr->write_data(me_ptr, ext_out_port_ptr);

         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

         out_port_ptr->common.sdata.flags.end_of_frame = FALSE;

         me_ptr->topo.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = FALSE;
      }
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Gen cntr deliver output buffer");
#endif

   if (out_media_fmt_changed)
   {
      gen_topo_exit_island_temporarily(&me_ptr->topo);

      // Container KPPS, BW depend on ext port media fmt
      TRY(result, gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * Algorithm to process frames
 *  - loop until
 *    - any input or output buf is available, otherwise go back and wait in workloop
 *    - if any input or other is available, then
 *       - process until we have enough number of frames.
 *          exit conditions are
 *             - need to wait for input (buffer emptied) or output (buf recreation)
 *             - out buf has to be released due to media fmt change, ts discontinuity,
 *                output too small to hold next frame etc
 *             - need to send EoS
 */
ar_result_t gen_cntr_data_process_frames(gen_cntr_t *me_ptr)
{
   ar_result_t                 result                  = AR_EOK;
   uint32_t                    inner_loop_count        = 0;
   bool_t                      need_to_exit_outer_loop = FALSE;
   gen_topo_process_info_t *   process_info_ptr        = &me_ptr->topo.proc_context.process_info;
   gen_topo_process_context_t *pc_ptr                  = &me_ptr->topo.proc_context;
   INIT_EXCEPTION_HANDLING

   gen_cntr_reset_process_info(me_ptr);
   pc_ptr->process_info.probing_for_tpm_activity = FALSE;

   if (gen_cntr_wait_for_any_ext_trigger(me_ptr, TRUE /* process context */, TRUE /*is_entry */))
   {
      return AR_EOK;
   }

   /**
    * purpose of for loop:
    * when release_out_buf is TRUE, input may not have been released. in cases like push mode,
    * input cannot be queued back or dropped. Also there's no output trigger to wake back up.
    * so in general, loop until out & in bufs are returned back or pushed down.
    */
   for (uint32_t loop_count = 0;; loop_count++)
   {
      /** process as long as we have not produced required number of frames per buffer.
       *  in case output recreates or media format changes or input is not sufficient break
       *
       *  when first module returns need-more, we come back here and loop again as we
       *  wouldn't have met num_frames requirement.
       */
      need_to_exit_outer_loop = FALSE;
      inner_loop_count        = 0;
      bool_t is_output_ready  = FALSE;

      do
      {
         process_info_ptr->anything_changed  = FALSE;
         process_info_ptr->num_data_tpm_done = 0;

         /** Process one frame: even if there's error continue with rest of processing */
         result = gen_cntr_data_process_one_frame(me_ptr);

         // Poll control channel and check for incoming ctrl msgs.
         // If any present, do set param and return the msgs.
         /*poll control port msg after data processing. In case of signal triggered cases it helps in having module
          * process called with same periodicity every time. Control port msgs usually occur once in a while and cause
          * jitter in the periodicity, so process them after data.*/
         cu_poll_and_process_ctrl_msgs(&me_ptr->cu);

         /*Increment the processed frame count */
         inner_loop_count++;

      } while (gen_cntr_need_to_process_frames(me_ptr, inner_loop_count, &need_to_exit_outer_loop, &is_output_ready));

      /** deliver output under following conditions
       *  - if output is marked ready even though num frames condition is not satisfied OR
       *  - if num frames conditions is met.
       * */
      if (is_output_ready)
      {
         TRY(result, gen_cntr_deliver_output(me_ptr));
      }

      gen_cntr_check_and_vote_for_island_in_data_path(me_ptr);

      if (me_ptr->flags.is_any_ext_in_mf_pending)
      {
         /** if input discontinuity was introduced by input data cmd (media fmt) & its handling is done now,
          *  then continue to process that command */
         gen_cntr_process_pending_input_data_cmd(me_ptr);
      }

      gen_cntr_handle_fwk_events_in_data_path(me_ptr);

      if (need_to_exit_outer_loop)
      {
         break;
      }

      if (gen_cntr_wait_for_any_ext_trigger(me_ptr, TRUE /* process context*/, FALSE /* is_entry */))
      {
         break;
      }

      if (loop_count >= 100)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Infinite loop alert. Listening to only commands.");
         gen_cntr_listen_to_controls(me_ptr);
#ifdef SIM
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Crashing on SIM");
         spf_svc_crash();
#endif
         break;
      }

      /**important: be careful before adding, any continue statements*/
      gen_cntr_reset_process_info(me_ptr);
   }

   me_ptr->prev_err_print_time_ms = me_ptr->topo.proc_context.err_print_time_in_this_process_ms;

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/* Setup the internal input port buffers.
 *    1. Copies data from the external buffer to internal buffer.
 *    2. In buffer driven (CD) mode, if internal buffer is not filled and external buffer is empty, then sets
 *       the input_wait_mask for that input port.
 *    3. In Interrupt driven (EP), if ext buffer is empty and internal buffer is not filled, then UNDER RUNS.
 */
ar_result_t gen_cntr_setup_internal_input_port_and_preprocess(gen_cntr_t *             me_ptr,
                                                              gen_cntr_ext_in_port_t * ext_in_port_ptr,
                                                              gen_topo_process_info_t *process_info_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_input_port_t *in_port_ptr          = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   uint32_t               bytes_copied_per_buf = 0, bytes_to_copy_per_buf = 0, bytes_available_per_buf = 0;
   uint32_t               bytes_in_int_inp_md_prop = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);
   bool_t                 sufficient_bytes_copied  = FALSE;

   uint32_t bytes_in_ext_in_port = gen_cntr_get_bytes_in_ext_in_for_md(ext_in_port_ptr);

   // if EOF is at nblc end also we dont need to just continue to process without going back to waiting for more in
   bool_t is_input_discontinuity =
      ext_in_port_ptr->flags.input_discontinuity || in_port_ptr->common.sdata.flags.end_of_frame ||
      (in_port_ptr->nblc_end_ptr && in_port_ptr->nblc_end_ptr->common.sdata.flags.end_of_frame);

   // set discontinuity related EOF only if we didn't previously handle EOF for this port
   if (!in_port_ptr->flags.was_eof_set)
   {
      in_port_ptr->common.sdata.flags.end_of_frame |= ext_in_port_ptr->flags.input_discontinuity;
   }

   // force process is from the perspective of first module (not nblc). force process if there's metadata
   bool_t force_process = in_port_ptr->common.sdata.flags.end_of_frame || in_port_ptr->common.sdata.flags.marker_eos ||
                          ext_in_port_ptr->md_list_ptr || in_port_ptr->common.sdata.metadata_list_ptr;

#ifdef VERBOSE_DEBUGGING
   bool_t dbg_got_buf          = FALSE;
   bool_t dbg_inp_insufficient = FALSE;
#endif

   /** to call module process we need input buf, hence even at eos we get a buf*/
   gen_topo_data_need_t needs_inp_data = gen_topo_in_port_needs_data(&me_ptr->topo, in_port_ptr);

   /**
    * if we allocate buffers when downstream (nblc end) doesn't need data, then we will
    * unnecessarily pile up data and consume memory/cause delay (Memory optimization). Hence get-in-buf only if
    * nblc end needs inp data or if it's a force-process case.
    * we also don't need to copy more than what nblc end's buffer can hold. Copying more data leads to data pile up.
    */
   if ((((GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger) ||
         gen_cntr_ext_in_port_has_data_buffer(&ext_in_port_ptr->gu)) &&
        (!((GEN_TOPO_DATA_BLOCKED | GEN_TOPO_DATA_NOT_NEEDED) & needs_inp_data))) ||
       force_process)
   {

      TRY(result, gen_cntr_ext_input_setup_int_port_buf(me_ptr, ext_in_port_ptr, in_port_ptr));

      if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         // nothing can be done if there's no buffer, we still have to continue processing as Signal trigger module
         // needs to be called for sending zeros to HW.
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "preprocess input: module (%lu of %lu), ext in (%lu of %lu), "
                      "No buffer (OK for Signal trigger)",
                      in_port_ptr->common.bufs_ptr[0].actual_data_len,
                      in_port_ptr->common.bufs_ptr[0].max_data_len,
                      ext_in_port_ptr->buf.actual_data_len,
                      ext_in_port_ptr->buf.max_data_len);
         return AR_EOK;
      }
#ifdef VERBOSE_DEBUGGING
      dbg_got_buf = TRUE;
#endif

      // if there's input discontinuity, at this ext input port or internal port or nblc end, don't read more data.
      if (!is_input_discontinuity)
      {
         // don't read more input that what nblc end needs if it exists for raw-compressed.
         // do not check for in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].data_ptr for covering both requires_data_buf
         // (decoder) and !requires_data_buf (encoder)
         bool_t is_fixed_frame_required = FALSE;
         if (in_port_ptr->nblc_end_ptr && (in_port_ptr->nblc_end_ptr != in_port_ptr))
         {
            is_fixed_frame_required = gen_topo_does_module_requires_fixed_frame_len(
               (gen_topo_module_t *)in_port_ptr->nblc_end_ptr->gu.cmn.module_ptr);

            bytes_available_per_buf        = in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
            uint32_t total_bytes_available = gen_topo_get_total_actual_len(&in_port_ptr->nblc_end_ptr->common);

            if (total_bytes_available &&
                SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->nblc_end_ptr->common.media_fmt_ptr->data_format) &&
                (SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->common.media_fmt_ptr->data_format)))
            {
               // since rescaling involves channels, we need to pass total count.
               total_bytes_available =
                  topo_rescale_byte_count_with_media_fmt(total_bytes_available,
                                                         in_port_ptr->common.media_fmt_ptr,
                                                         in_port_ptr->nblc_end_ptr->common.media_fmt_ptr);

               bytes_available_per_buf = topo_div_num(total_bytes_available, in_port_ptr->common.sdata.bufs_num);
            }
         }
         else
         {
            is_fixed_frame_required =
               gen_topo_does_module_requires_fixed_frame_len((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr);
         }

         bytes_available_per_buf += in_port_ptr->common.bufs_ptr[0].actual_data_len;

         bytes_to_copy_per_buf = in_port_ptr->common.max_buf_len_per_buf - bytes_available_per_buf;
         // max_buf_len is rescaled version of nblc_end's max_buf_len

         bytes_copied_per_buf = bytes_to_copy_per_buf;
         // If there is more data to be copied from client buffer.
         // Even if client data is not present, process has to be called to flush any remaining input data (esp. @ EoS)
         // Special note: gpr client EOS is popped and read directly in read_data.
         //    So even if there was no EOS at ext-in-port before calling this function, we might end up with one now.
         result = ext_in_port_ptr->vtbl_ptr->read_data(me_ptr, ext_in_port_ptr, &bytes_copied_per_buf);

         gen_topo_handle_eof_history(in_port_ptr, (bytes_copied_per_buf > 0));

         sufficient_bytes_copied = (!is_fixed_frame_required && bytes_copied_per_buf) ||
                                   (is_fixed_frame_required && (bytes_copied_per_buf == bytes_to_copy_per_buf));

         // if sufficient bytes are copied then reset need_more_input
         if (sufficient_bytes_copied)
         {
            in_port_ptr->flags.need_more_input = FALSE;
         }
         else if (bytes_copied_per_buf) // if bytes are not copied then don't update the bytes_from_prev_buf
         {
            // if sufficient bytes are not present then new buffer (along with new TS) will be copied, so mark the
            // current data as prev-data.
            // need_more_true then do this always.
            in_port_ptr->bytes_from_prev_buf = in_port_ptr->common.bufs_ptr[0].actual_data_len;
         }
      }
   }

   // Handle only in the process context.
   if (process_info_ptr)
   {
      // reset erasure flag. It will be set if context is signal trigger and underflow.
      // It will be always false external inputs during data trigger.
      in_port_ptr->common.sdata.flags.erasure = FALSE;

      /* UNDER RUN: If there is not input buffer available on the input port then push zeros in to the module buffer.
         Do not underrun for blocked/optional signal triggered ports. **/
      if (GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger && (needs_inp_data & GEN_TOPO_DATA_NEEDED))
      {
         // Check the trigger mode and fill zeros if the buffer is not filled.
         result = gen_cntr_st_check_input_and_underrun(me_ptr, ext_in_port_ptr);
      }
      else
      {
         // MPPS optimization for no-trigger policy: this check helps us go back to waiting for input instead of
         // extra call to topo_process.
         // 1. for input discontinuity and force process do one round of topo_process before going for waiting
         // 2. for requires_data_buf=False: if input has less than what nblc end needs, wait for input.
         //       call to gen_topo_in_port_needs_data, in above lines will use need_more_input. call from
         //       gen_cntr_ext_in_port_needs_data_buffer will use threshold based calc (in steady state, not first time)
         // 3. for requires_data_buf=True: if no bytes were copied, wait for input
         // Following contexts: a) after ext-in buf triggers: if sufficient data is not available, go wait for one more
         // b) after processing some data, for remaining input.
         // 4. If a sync module is present at the ext input's nblc end, and it does not have sufficient data at its
         // input only then set the flag. Else consider that sync input trigger is satisfied.
         if ((0 == me_ptr->topo.num_data_tpm) && !sufficient_bytes_copied && !is_input_discontinuity &&
             !force_process && (GEN_TOPO_DATA_NEEDED == gen_cntr_ext_in_port_needs_data_buffer(&ext_in_port_ptr->gu)) &&
             (!me_ptr->topo.flags.is_sync_module_present || gen_cntr_fwk_ext_sync_requires_data(me_ptr, in_port_ptr)))
         {
#ifdef VERBOSE_DEBUGGING
            dbg_inp_insufficient = TRUE;
#endif
            // Wait for the input buffers only if the EOS has not arrived and not in input discontinuity state
            // and its not interrupt driven mode. In interrupt mode, we never wait for input/outputs.

            // if module has requires_data_buf = FALSE, then we check if input buf is full. if not, wait for
            // input.

            // for is_input_discontinuity like input MF, we need to process first without going for waiting for more
            // input.

            // update wait mask, so that this path trigger can be avoided.
            me_ptr->wait_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index] |=
               ext_in_port_ptr->cu.bit_mask;
         }
      }
   }

   if (!gen_cntr_is_data_present_in_ext_in_bufs(ext_in_port_ptr))
   {
      // DSP Client EOF: If EoF was set in the input buffer, then last copy to decoder internal buffer ensures
      // integral number of frames in the decoder internal buffer.
      // OR it, instead of assigning because previous EOF may not be cleared yet.
      if (ext_in_port_ptr->flags.eof)
      {
         in_port_ptr->common.sdata.flags.end_of_frame |= ext_in_port_ptr->flags.eof;
         ext_in_port_ptr->flags.eof = FALSE;
      }
   }

   /* TODO: Bytes copied per buf for v2 currently returned is based on the first buffer size  */
   uint32_t bytes_copied_total = 0;
   if (!gen_cntr_is_ext_in_v2(ext_in_port_ptr))
   {
      bytes_copied_total = bytes_copied_per_buf * in_port_ptr->common.sdata.bufs_num;
   }
   else
   {
      bytes_copied_total = bytes_copied_per_buf;
   }

   gen_topo_move_md_from_ext_in_to_int_in(in_port_ptr,
                                          me_ptr->topo.gu.log_id,
                                          &ext_in_port_ptr->md_list_ptr,
                                          bytes_in_int_inp_md_prop,
                                          bytes_in_ext_in_port,
                                          bytes_copied_total,
                                          in_port_ptr->common.media_fmt_ptr);

   if (ext_in_port_ptr->flags.input_discontinuity)
   {
      ext_in_port_ptr->flags.input_discontinuity = FALSE;
      gen_topo_handle_eof_history(in_port_ptr, FALSE);
   }

   // return buf if its empty (checked inside) in non force process cases.
   if (!force_process)
   {
      gen_topo_input_port_return_buf_mgr_buf(&me_ptr->topo, in_port_ptr);
   }

#ifdef VERBOSE_DEBUGGING
   {
      bool_t ext_cond_not_met =
         (me_ptr->wait_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index] &
          ext_in_port_ptr->cu.bit_mask)
            ? TRUE
            : FALSE;
      bool_t   buf_present = (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr);
      uint32_t flags       = (sufficient_bytes_copied << 7) | (buf_present << 6) | (ext_cond_not_met << 5) |
                       (in_port_ptr->common.sdata.flags.marker_eos << 4) | (is_input_discontinuity << 3) |
                       (force_process << 2) | (dbg_got_buf << 1) | dbg_inp_insufficient;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "preprocess input: module (0x%lx,0x%lx) (%lu of %lu) per buf, ext in (%lu of %lu) ",
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   in_port_ptr->gu.cmn.id,
                   in_port_ptr->common.bufs_ptr[0].actual_data_len,
                   in_port_ptr->common.bufs_ptr[0].max_data_len,
                   ext_in_port_ptr->buf.actual_data_len,
                   ext_in_port_ptr->buf.max_data_len);

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "preprocess input: module (0x%lx,0x%lx) flags %08lX, curr_trigger%u, needs_inp_data%u, inport "
                   "flags0x%lX",
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   in_port_ptr->gu.cmn.id,
                   flags,
                   me_ptr->topo.proc_context.curr_trigger,
                   needs_inp_data,
                   in_port_ptr->flags.word);
   }
#endif

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_output_bufQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

#ifdef VERBOSE_DEBUGGING
   {
      uint32_t mask = 1 << channel_bit_index;
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "gen_cntr_output_bufQ_trigger: channel_bit_mask: 0x%08lX ",
                   mask);
   }
#endif

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_DATA_TRIGGER;

   if (me_ptr->cu.cmd_msg.payload_ptr)
   {
      // if async command processing is going on then check for any pending event handling
      gen_cntr_handle_fwk_events_in_data_path(me_ptr);
   }

   // don't process frames until other simultaneous signals are also looked into
   uint32_t all_ext_out_mask = gen_cntr_get_all_output_port_mask(me_ptr);

   uint32_t channel_status =
      posal_channel_poll_inline(me_ptr->cu.channel_ptr, all_ext_out_mask) & me_ptr->cu.curr_chan_mask;

   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *temp_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      // If Signal is set in output data q then pop from output data q
      if (temp_ext_out_port_ptr->cu.bit_mask & channel_status)
      {
         TRY(result, temp_ext_out_port_ptr->vtbl_ptr->setup_bufs(me_ptr, temp_ext_out_port_ptr));
      }
   }

   TRY(result, gen_cntr_data_process_frames(me_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_INVALID_TRIGGER;

   return result;
}

/* is_flush flag should be used for flushing case.
 * For EOS: for flushing case EOS needs to be dropped
 * This flag can be used for other scenarios where special handling is required when flushing is done
 */
ar_result_t gen_cntr_free_input_data_cmd(gen_cntr_t *            me_ptr,
                                         gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                         ar_result_t             status,
                                         bool_t                  is_flush)
{
   ar_result_t result = AR_EOK;

   if (!ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      return AR_EOK;
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Freed input msg buffer 0x%lx with opcode 0x%x from ext input (miid,port-id) (0x%lX, 0x%lx)",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
#endif

   ext_in_port_ptr->vtbl_ptr->free_input_buf(me_ptr, ext_in_port_ptr, status, is_flush);

   // set payload to NULL to indicate we are not holding on to any input data msg
   // it must be cleared in the .free_input_buf() itself
   //ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;

   return result;
}

ar_result_t gen_cntr_get_input_data_cmd(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t result;

   if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Already holding on to an input data command. "
                   "Cannot get another command until this is freed");
      return AR_EUNEXPECTED;
   }

   result = posal_queue_pop_front(ext_in_port_ptr->gu.this_handle.q_ptr,
                                  (posal_queue_element_t *)&(ext_in_port_ptr->cu.input_data_q_msg));
   if (AR_EOK != result)
   {
      ext_in_port_ptr->cu.input_data_q_msg.payload_ptr = NULL;
      return result;
   }

#ifdef BUF_CNT_DEBUG
   if (ext_in_port_ptr->vtbl_ptr->is_data_buf(me_ptr, ext_in_port_ptr))
   {
      ext_in_port_ptr->cu.buf_recv_cnt++;
   }
#endif

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "Popped an input msg buffer 0x%lx with opcode 0x%x from (miid,port-id) (0x%lX, 0x%lx) queue",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->cu.input_data_q_msg.payload_ptr,
                ext_in_port_ptr->cu.input_data_q_msg.msg_opcode);
#endif

   return result;
}

ar_result_t gen_cntr_input_dataQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

#ifdef VERBOSE_DEBUGGING
   uint32_t mask = 1 << channel_bit_index;
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "gen_cntr_input_dataQ_trigger: channel_bit_mask: 0x%08lX ", mask);
#endif

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_DATA_TRIGGER;

   if (me_ptr->cu.cmd_msg.payload_ptr)
   {
      // if async command processing is going on then check for any pending event handling
      gen_cntr_handle_fwk_events_in_data_path(me_ptr);
   }

   // don't process frames until other simultaneous signals are also looked into
   uint32_t all_ext_in_mask = gen_cntr_get_all_input_port_mask(me_ptr);

   uint32_t channel_status =
      posal_channel_poll_inline(me_ptr->cu.channel_ptr, all_ext_in_mask) & me_ptr->cu.curr_chan_mask;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *temp_ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      // If Signal is set in input data q then pop from in data q
      if (temp_ext_in_port_ptr->cu.bit_mask & channel_status)
      {
         /** do not try to fully fill internal buf for non-Signal trigger. if we try to fill for non-Signal trigger,
          * then timestamp of the
          * buffer will get replaced.Also it could cause higher continuous latency
          * */
         temp_ext_in_port_ptr->vtbl_ptr->on_trigger(me_ptr, temp_ext_in_port_ptr);
      }
   }

   TRY(result, gen_cntr_data_process_frames(me_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_INVALID_TRIGGER;

   return result;
}

static ar_result_t gen_cntr_replace_borrowed_buf_with_topo_buf_util_(gen_cntr_t             *me_ptr,
                                                                     gu_module_t            *module_ptr,
                                                                     gen_topo_common_port_t *cmn_port_ptr)
{
   ar_result_t result = AR_EOK;
   // get a topo buffer and copy data from ext borrowed buffer to int buffer
   // assign topo buffer to int port
   int8_t  *src_ptr       = cmn_port_ptr->bufs_ptr[0].data_ptr;
   uint32_t bytes_to_copy = cmn_port_ptr->bufs_ptr[0].actual_data_len;

   cmn_port_ptr->bufs_ptr[0].data_ptr = NULL;

   result = gen_topo_buf_mgr_wrapper_get_buf(&me_ptr->topo, cmn_port_ptr);

   TOPO_MEMSCPY_NO_RET(cmn_port_ptr->bufs_ptr[0].data_ptr,
                       cmn_port_ptr->bufs_ptr[0].max_data_len,
                       src_ptr,
                       bytes_to_copy,
                       me_ptr->topo.gu.log_id,
                       "miid 0x%lX",
                       module_ptr->module_instance_id);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Module 0x%08lX: Replacing ext buf 0x%lx with topo buf 0x%lx len (%lu of %lu)",
                module_ptr->module_instance_id,
                src_ptr,
                cmn_port_ptr->bufs_ptr[0].data_ptr,
                cmn_port_ptr->bufs_ptr[0].actual_data_len,
                cmn_port_ptr->bufs_ptr[0].max_data_len);
#endif
   return result;
}

ar_result_t gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports(gen_cntr_t             *me_ptr,
                                                                 gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                                                 gen_topo_input_port_t  *in_port_ptr,
                                                                 bool_t                  IS_FORCE_CLEAR)
{
   ar_result_t result = AR_EOK;

   if ((GEN_TOPO_BUF_ORIGIN_EXT_BUF == in_port_ptr->common.flags.buf_origin) &&
       in_port_ptr->common.bufs_ptr[0].data_ptr &&
       ((0 == in_port_ptr->common.bufs_ptr[0].actual_data_len) || IS_FORCE_CLEAR))
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   " Module 0x%lX: Port 0x%lx, Clearing ext input buffer 0x%p from internal port, max_buf_len %lu, "
                   "buf_origin%u",
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   in_port_ptr->gu.cmn.id,
                   in_port_ptr->common.bufs_ptr[0].data_ptr,
                   in_port_ptr->common.bufs_ptr[0].max_data_len,
                   in_port_ptr->common.flags.buf_origin);
#endif

      // free the ext buffer from internal port

      int8_t *brwd_ext_buf_ptr = in_port_ptr->common.bufs_ptr[0].data_ptr;
      if (in_port_ptr->common.bufs_ptr[0].actual_data_len)
      {
         // it would enter with actual data len only if force clear is set
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Module (0x%08lX, 0x%lx): Unexpected! Freeing unprocessed held ext in buffer 0x%lx len (%lu of "
                      "%lu).",
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->common.bufs_ptr[0].data_ptr,
                      in_port_ptr->common.bufs_ptr[0].actual_data_len,
                      in_port_ptr->common.bufs_ptr[0].max_data_len);

         result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                     in_port_ptr->gu.cmn.module_ptr,
                                                                     &in_port_ptr->common);
      }
      else
      {
         in_port_ptr->common.bufs_ptr[0].data_ptr = NULL;
         in_port_ptr->common.flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_INVALID;
         gen_topo_set_all_bufs_len_to_zero(&in_port_ptr->common);
      }

      // Borrowed external buffer (GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED) could be stuck in the NBLC due to any of the
      // following reasons,
      // 1. Downstream SG stopped and data is stalled.
      // 2. NBLC boundary module's trigger policy is blocked and module doesnt consume input.
      //
      // Note that if there is requires data buffering module buffer is not propagated, hence
      gen_topo_input_port_t *nblc_end_ptr    = in_port_ptr->nblc_end_ptr;
      gen_topo_input_port_t *cur_in_port_ptr = in_port_ptr;
      while (nblc_end_ptr != cur_in_port_ptr)
      {
         gen_topo_module_t *cur_module_ptr = (gen_topo_module_t *)cur_in_port_ptr->gu.cmn.module_ptr;

         // check cur mod output
         if (!cur_module_ptr->gu.output_port_list_ptr)
         {
            break;
         }

         gen_topo_output_port_t *curr_out_port_ptr =
            (gen_topo_output_port_t *)cur_module_ptr->gu.output_port_list_ptr->op_port_ptr;
         // replace GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED with topo buffer in the NBLC
         // ideally buf would be cleared if there is no pending data at this point.
         if (brwd_ext_buf_ptr == curr_out_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            if (curr_out_port_ptr->common.bufs_ptr[0].actual_data_len)
            {
               result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                           cur_in_port_ptr->gu.cmn.module_ptr,
                                                                           &curr_out_port_ptr->common);
            }
            else
            {
#ifdef VERBOSE_DEBUGGING
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_LOW_PRIO,
                            " Module 0x%lX: Port 0x%lx, Clearing borrowed ext input buffer 0x%p from internal port, "
                            "max_buf_len %lu, "
                            "buf_origin%u",
                            curr_out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                            curr_out_port_ptr->gu.cmn.id,
                            curr_out_port_ptr->common.bufs_ptr[0].data_ptr,
                            curr_out_port_ptr->common.bufs_ptr[0].max_data_len,
                            curr_out_port_ptr->common.flags.buf_origin);
#endif

               curr_out_port_ptr->common.bufs_ptr[0].data_ptr = NULL;
               curr_out_port_ptr->common.flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_INVALID;
               gen_topo_set_all_bufs_len_to_zero(&curr_out_port_ptr->common);
            }
         }

         gen_topo_input_port_t *next_in_port_ptr = (gen_topo_input_port_t *)curr_out_port_ptr->gu.conn_in_port_ptr;
         if (!next_in_port_ptr)
         {
            break;
         }

         // ideally buf would be cleared if there is no pending data at this point.
         if (brwd_ext_buf_ptr == next_in_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            if (next_in_port_ptr->common.bufs_ptr[0].actual_data_len)
            {
               result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                           next_in_port_ptr->gu.cmn.module_ptr,
                                                                           &next_in_port_ptr->common);
            }
            else
            {
#ifdef VERBOSE_DEBUGGING
               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_LOW_PRIO,
                            " Module 0x%lX: Port 0x%lx, Clearing borrowed ext input buffer 0x%p from internal port, "
                            "max_buf_len %lu, "
                            "buf_origin%u",
                            next_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                            next_in_port_ptr->gu.cmn.id,
                            next_in_port_ptr->common.bufs_ptr[0].data_ptr,
                            next_in_port_ptr->common.bufs_ptr[0].max_data_len,
                            next_in_port_ptr->common.flags.buf_origin);
#endif
               next_in_port_ptr->common.bufs_ptr[0].data_ptr = NULL;
               next_in_port_ptr->common.flags.buf_origin     = GEN_TOPO_BUF_ORIGIN_INVALID;
               gen_topo_set_all_bufs_len_to_zero(&next_in_port_ptr->common);
            }
         }

         // replace GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED with topo buffer in the NBLC

         cur_in_port_ptr = next_in_port_ptr;
      }

      // free ext buffer only if it is borrowed by internal ports. If its not borrowed, buffer will be free in the
      // .read_data() context itself.
      if (0 == ext_in_port_ptr->buf.actual_data_len)
      {
         result |= gen_cntr_free_input_data_cmd(me_ptr, ext_in_port_ptr, AR_EOK, FALSE);
      }
   }

   return result;
}

static ar_result_t gen_cntr_ext_out_free_borrowed_buf_in_nblc_util_(gen_cntr_t             *me_ptr,
                                                                    gen_topo_output_port_t *out_port_ptr,
                                                                    int8_t                 *brwd_ext_buf_ptr)
{
   ar_result_t result = AR_EOK;

   // check cur out_port_ptr
   if (brwd_ext_buf_ptr == out_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      if (out_port_ptr->common.bufs_ptr[0].actual_data_len)
      {
         // get a topo buffer and copy data from ext borrowed buffer to int buffer
         // assign topo buffer to int port
         result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                     out_port_ptr->gu.cmn.module_ptr,
                                                                     &out_port_ptr->common);
      }
      else
      {
         out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
         out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
         out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
         gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
      }
   }

   // Borrowed external buffer could be stuck in the NBLC path due to any of the following reasons,
   // 1. upstream SG stopped and data is stalled.
   // 2. NBLC boundary module's trigger policy is blocked and module doesnt generate any output.
   gen_topo_output_port_t *nblc_start_ptr   = out_port_ptr->nblc_start_ptr;
   gen_topo_output_port_t *cur_out_port_ptr = out_port_ptr;
   while (nblc_start_ptr != cur_out_port_ptr)
   {
      gen_topo_module_t *cur_module_ptr = (gen_topo_module_t *)cur_out_port_ptr->gu.cmn.module_ptr;

      // check cur mod input
      if (!cur_module_ptr->gu.input_port_list_ptr)
      {
         break;
      }

      gen_topo_input_port_t *curr_in_port_ptr =
         (gen_topo_input_port_t *)cur_module_ptr->gu.input_port_list_ptr->ip_port_ptr;
      if (brwd_ext_buf_ptr == curr_in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         if (curr_in_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                        curr_in_port_ptr->gu.cmn.module_ptr,
                                                                        &curr_in_port_ptr->common);
         }
         else
         {
            curr_in_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
            curr_in_port_ptr->common.bufs_ptr[0].max_data_len = 0;
            curr_in_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
            gen_topo_set_all_bufs_len_to_zero(&curr_in_port_ptr->common);
         }
      }

      // check the upstream module's output
      gen_topo_output_port_t *prev_out_port_ptr = (gen_topo_output_port_t *)curr_in_port_ptr->gu.conn_out_port_ptr;
      if (NULL == prev_out_port_ptr)
      {
         break;
      }

      if (brwd_ext_buf_ptr == prev_out_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         if (prev_out_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                        prev_out_port_ptr->gu.cmn.module_ptr,
                                                                        &prev_out_port_ptr->common);
         }
         else
         {
            prev_out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
            prev_out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
            prev_out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
            gen_topo_set_all_bufs_len_to_zero(&prev_out_port_ptr->common);
         }
      }
      cur_out_port_ptr = prev_out_port_ptr;
   }
   return result;
}

/** This function takes care of resetting ext buffer references from the internal ports (buf_origin:
 * GEN_TOPO_BUF_ORIGIN_EXT_BUF). This function must be called after ext out data is post processed.
 *
 * This function also makes sure any references to the ext buffer gets cleared from the NBLC path (buf_origin:
 * GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED). If any data is stalled in the borrowed buffer due to module's trigger not
 * being satisfied (like stopped sg boundary), this function replaces the borrowed buffer with a topo buffer, so as to
 * free the ext out buffer. */
ar_result_t gen_cntr_clear_borrowed_ext_out_buffer_from_int_ports(gen_cntr_t              *me_ptr,
                                                                  gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                                                  gen_topo_output_port_t  *out_port_ptr)
{
   ar_result_t result = AR_EOK;

   if (GEN_TOPO_BUF_ORIGIN_EXT_BUF != out_port_ptr->common.flags.buf_origin)
   {
      return result;
   }

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                " Module 0x%lX: Port 0x%lx, Clearing ext output buffer 0x%p from internal port, max_buf_len %lu, "
                "buf_origin%u",
                out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                out_port_ptr->gu.cmn.id,
                out_port_ptr->common.bufs_ptr[0].data_ptr,
                out_port_ptr->common.bufs_ptr[0].max_data_len,
                out_port_ptr->common.flags.buf_origin);
#endif

   // it's enough to reset first buf, as only first buf is checked for validity
   int8_t *brwd_ext_buf_ptr                      = out_port_ptr->common.bufs_ptr[0].data_ptr;
   out_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
   out_port_ptr->common.bufs_ptr[0].max_data_len = 0;
   out_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
   gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);

   // Important: Even though Rd shm is nblc boundary module buffer might be propagated to the NBLC upstream as well.
   // check gen_topo_check_get_out_buf_from_buf_mgr_util_() how
   // rd shm buffer is propagated to nblc beyond rd shm module.
   gen_topo_module_t *cur_module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   if (MODULE_ID_RD_SHARED_MEM_EP == out_port_ptr->gu.cmn.module_ptr->module_id)
   {
      // replace GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED with topo buffer
      gen_topo_input_port_t *curr_in_port_ptr =
         (gen_topo_input_port_t *)cur_module_ptr->gu.input_port_list_ptr->ip_port_ptr;
      if (brwd_ext_buf_ptr == curr_in_port_ptr->common.bufs_ptr[0].data_ptr)
      {
         // get a topo buffer and copy data from ext borrowed buffer to int buffer
         // assign topo buffer to int port
         if (curr_in_port_ptr->common.bufs_ptr[0].actual_data_len)
         {
            result |= gen_cntr_replace_borrowed_buf_with_topo_buf_util_(me_ptr,
                                                                        curr_in_port_ptr->gu.cmn.module_ptr,
                                                                        &curr_in_port_ptr->common);
         }
         else
         {
            curr_in_port_ptr->common.bufs_ptr[0].data_ptr     = NULL;
            curr_in_port_ptr->common.bufs_ptr[0].max_data_len = 0;
            curr_in_port_ptr->common.flags.buf_origin         = GEN_TOPO_BUF_ORIGIN_INVALID;
            gen_topo_set_all_bufs_len_to_zero(&curr_in_port_ptr->common);
         }
      }

      // free GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED buf in the nblc path before Rd shm module
      if (curr_in_port_ptr->gu.conn_out_port_ptr)
      {
         result |= gen_cntr_ext_out_free_borrowed_buf_in_nblc_util_(me_ptr,
                                                                    (gen_topo_output_port_t *)
                                                                       curr_in_port_ptr->gu.conn_out_port_ptr,
                                                                    brwd_ext_buf_ptr);
      }
   }
   else
   {
      // free GEN_TOPO_BUF_ORIGIN_EXT_BUF_BORROWED buf in the nblc path conneced to ext output
      result |= gen_cntr_ext_out_free_borrowed_buf_in_nblc_util_(me_ptr, out_port_ptr, brwd_ext_buf_ptr);
   }

   return result;
}
