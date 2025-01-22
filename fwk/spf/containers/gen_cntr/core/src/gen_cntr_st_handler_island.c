/**
 * \file gen_cntr_st_handler_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"

/* Poll for the input buffers for PCM only and only if,
 *  1. Internal input buffer is not filled and data buffers are present on the queue.
 *  2. There is no data discontinuity due to media format change etc.
 *  3. EOS is not received.
 *  4. if media fmt is not known we can poll (as MF comes in data path)
 */
bool_t gen_cntr_st_need_to_poll_for_input_data(gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   if (SPF_UNKNOWN_DATA_FORMAT == ext_in_port_ptr->cu.media_fmt.data_format)
   {
      return TRUE;
   }

   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   /** For compressed formats, we should poll as long as there is a full buffer at ext or int input
    * Should not copy in parts  */
   uint32_t ext_in_total_actual_len = gen_cntr_get_ext_in_total_actual_data_len(ext_in_port_ptr);

   if ((SPF_RAW_COMPRESSED | SPF_DEINTERLEAVED_RAW_COMPRESSED) & ext_in_port_ptr->cu.media_fmt.data_format)
   {
      if ((0 == in_port_ptr->common.bufs_ptr[0].actual_data_len) && (0 == ext_in_total_actual_len))
      {
         return TRUE;
      }
      else
      {
         return FALSE;
      }
   }

   uint32_t max_len_per_buf = in_port_ptr->common.max_buf_len_per_buf;

   // since all bufs are equally filled, it's sufficient to check first one.
   uint32_t max_len =
      in_port_ptr->common.bufs_ptr[0].data_ptr ? in_port_ptr->common.bufs_ptr[0].max_data_len : max_len_per_buf;
   if ((max_len > in_port_ptr->common.bufs_ptr[0].actual_data_len) && (0 == ext_in_total_actual_len) &&
       (FALSE == ext_in_port_ptr->flags.input_discontinuity))
   {
      return TRUE;
   }

   return FALSE;
}

ar_result_t gen_cntr_st_prepare_input_buffers_per_port(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   ar_result_t             result          = AR_EOK;
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // checks if port is blocked/stopped on signal trigger.
   gen_topo_data_need_t rc = gen_topo_in_port_needs_data(&me_ptr->topo, in_port_ptr);
   if (GEN_TOPO_DATA_BLOCKED == rc)
   {
      return result;
   }

#ifdef VERBOSE_DEBUGGING
   uint32_t num_polled_buffers = 0;
#endif
   while (TRUE)
   {
      if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
      {
         // if already holding on to an input buffer [partial buffer], copy data from the buffer.
         result = gen_cntr_setup_internal_input_port_and_preprocess(me_ptr, ext_in_port_ptr, NULL);

         // If the internal topo buffer is filled continue to setup the next input port.
         // here, in_port_ptr->common.buf.data_ptr must be present
         // and hence using max_data_len is ok compared to max_buf_len
         if (in_port_ptr->common.bufs_ptr[0].actual_data_len == in_port_ptr->common.bufs_ptr[0].max_data_len)
         {
            ext_in_port_ptr->flags.ready_to_go      = TRUE;
            in_port_ptr->common.sdata.flags.erasure = FALSE;
            break;
         }
      }

      // Stop popping buffers if,
      //   1. Need to poll returns FALSE or
      //   2. If there is no trigger signal set for the queue.
      if (!gen_cntr_st_need_to_poll_for_input_data(ext_in_port_ptr) ||
          !posal_queue_poll(ext_in_port_ptr->gu.this_handle.q_ptr))
      {
         break;
      }

      /** try to fill buf for Signal trigger */
      ext_in_port_ptr->vtbl_ptr->on_trigger(me_ptr, ext_in_port_ptr);

      if (AR_DID_FAIL(result))
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "process failed for ext input port 0x%lx of Module 0x%lX ",
                      in_port_ptr->gu.cmn.id,
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id);
      }

#ifdef VERBOSE_DEBUGGING
      num_polled_buffers++;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Ext input port 0x%lx of Module 0x%lX. num_polled_buffers %lu ",
                   in_port_ptr->gu.cmn.id,
                   in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                   num_polled_buffers);
#endif
   }

   return result;
}

ar_result_t gen_cntr_st_prepare_input_buffers(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   /* Iterate through all exterior input ports and pop the buffer from the queue */
   for (gu_ext_in_port_list_t *in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)in_port_list_ptr->ext_in_port_ptr;

      result |= gen_cntr_st_prepare_input_buffers_per_port(me_ptr, ext_in_port_ptr);
   }

   return result;
}

ar_result_t gen_cntr_st_prepare_output_buffers_per_ext_out_port(gen_cntr_t              *me_ptr,
                                                                gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   ar_result_t result = AR_EOK;

   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   // if a buffer exists, use it.
   if ((NULL != ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr) ||
       (NULL != ext_out_port_ptr->out_buf_gpr_client.payload_ptr))
   {
      return result;
   }

   // Pop the buffer only if the port has started.
   if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
   {
      return result;
   }

   // Do not setup external outputs if nblc start is blocked.
   fwk_extn_port_nontrigger_policy_t ntp;
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
       (ntp = gen_topo_get_nontrigger_policy_for_output(out_port_ptr, GEN_TOPO_SIGNAL_TRIGGER)))
   {
      return result;
   }
   else if (out_port_ptr->nblc_start_ptr && (out_port_ptr->nblc_start_ptr != out_port_ptr) &&
            (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
             (ntp = gen_topo_get_nontrigger_policy_for_output(out_port_ptr->nblc_start_ptr, GEN_TOPO_SIGNAL_TRIGGER))))
   {
      return result;
   }

   /** poll and see if queue has any buf. if not mark overrun & move to next port. */
   if (!posal_channel_poll_inline(me_ptr->cu.channel_ptr, ext_out_port_ptr->cu.bit_mask))
   {
      // print overrun only for RT paths
      if (FWK_EXTN_PORT_NON_TRIGGER_INVALID == ntp && gen_topo_is_port_in_realtime_path(&out_port_ptr->common))
      {
         gen_topo_exit_island_temporarily(&me_ptr->topo);
         gen_cntr_st_check_print_overrun(me_ptr, ext_out_port_ptr);
      }
      return result;
   }

   result = ext_out_port_ptr->vtbl_ptr->setup_bufs(me_ptr, ext_out_port_ptr);

   // assigns ext buffer to topo buffer if possible, else topo buffer will be assigned during module process context
   if (ext_out_port_ptr->vtbl_ptr->setup_topo_buf)
   {
      result = ext_out_port_ptr->vtbl_ptr->setup_topo_buf(me_ptr, ext_out_port_ptr);
   }

   return result;
}

ar_result_t gen_cntr_st_prepare_output_buffers(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   /* Iterate through all exterior out ports and pop the buffer from the queue */
   for (gu_ext_out_port_list_t *out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)out_port_list_ptr->ext_out_port_ptr;
      gen_cntr_st_prepare_output_buffers_per_ext_out_port(me_ptr, ext_out_port_ptr);
   }

   return result;
}

#ifdef USES_THIN_TOPO
/* This function must be called only from the gen topo context, to check if its possible to switch to thin topo.*/
GEN_CNTR_STATIC ar_result_t gen_cntr_check_and_switch_to_thin_topo(gen_cntr_t *me_ptr)
{
   // return if cannot enter thin topo
   if (check_if_needed_to_exit_thin_topo(&me_ptr->topo))
   {
#if defined(VERBOSE_DEBUGGING)
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_HIGH_PRIO,
                          "Cannot switch to Thin Topo exit_flags:0x%lx",
                          me_ptr->topo.exit_flags.word);
#endif
      return AR_EOK;
   }

   // if already in thin topo its unexpected.
   if (check_if_cntr_is_processing_with_thin_topo(&me_ptr->topo))
   {
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Unexpected! cannot be in thin topo at this point");
      return AR_EOK;
   }

   gen_topo_exit_island_temporarily(&me_ptr->topo);
   return gen_cntr_switch_to_thin_topo_util_(me_ptr);
}
#endif

ar_result_t gen_cntr_signal_trigger(cu_base_t *cu_ptr, uint32_t channel_bit_index)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "gen_cntr_trigger: Received signal trigger");
#endif

   // Increment in process context
   me_ptr->st_module.processed_interrupt_counter++;

#ifdef ENABLE_SIGNAL_MISS_CRASH
   me_ptr->st_module.steady_state_interrupt_counter++;
#endif

   /* Check for signal miss, if raised_interrupt_counter is greater than process counter,
    * one or more interrupts have not been serviced.
    * This check will be skipped for timer modules eg:spr, rat since raised_interrupt_counter is always 0
    * This checks kind of signal miss because of the container thread being busy and one or more interrupts have not
    * been serviced. */
   if (me_ptr->st_module.raised_interrupt_counter > me_ptr->st_module.processed_interrupt_counter)
   {
      // signal miss cannot be handled in island, even if signal miss is to be ignored, we will exit island. This
      // reduces island footprint.
      gen_topo_exit_island_temporarily(&me_ptr->topo);
      bool_t continue_processing = TRUE;
      gen_cntr_check_handle_signal_miss(me_ptr, FALSE /*is_after_process*/, &continue_processing);
      if (!continue_processing)
      {
         return result;
      }
   }

   /*clear the trigger signal */
   posal_signal_clear_inline(me_ptr->st_module.trigger_signal_ptr);

#ifdef VERBOSE_DEBUGGING
   if (!me_ptr->topo.gu.ext_out_port_list_ptr && !me_ptr->topo.gu.ext_in_port_list_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "gen_cntr_trigger: No external input or output ports");
   }
   else if (me_ptr->topo.gu.ext_out_port_list_ptr && me_ptr->topo.gu.ext_in_port_list_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "gen_cntr_trigger: Both external input and output ports present.");
   }
   else if (me_ptr->topo.gu.ext_in_port_list_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "gen_cntr_trigger: only external inputs present");
   }
   else if (me_ptr->topo.gu.ext_out_port_list_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "gen_cntr_trigger: only external outputs present");
   }
#endif

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_SIGNAL_TRIGGER;

   // querying the signal triggered timestamp before invoking the topo
   if (me_ptr->st_module.update_stm_ts_fptr && me_ptr->st_module.st_module_ts_ptr)
   {
      me_ptr->st_module.update_stm_ts_fptr(me_ptr->st_module.stm_ts_ctxt_ptr,
                                           &me_ptr->st_module.st_module_ts_ptr->timestamp);
      me_ptr->st_module.st_module_ts_ptr->is_valid = TRUE;
   }

   if (me_ptr->cu.cmd_msg.payload_ptr)
   {
      // if async command processing is going on then check for any pending event handling
      gen_cntr_handle_fwk_events_in_data_path(me_ptr);
   }

   if (gen_cntr_is_pure_signal_triggered(me_ptr))
   {
      // vote against lpi if the Pure ST topology is compiled in non-island
      gen_cntr_vote_against_lpi_if_pure_st_topo_lib_in_nlpi(me_ptr);

      result = gen_cntr_pure_st_process_frames(me_ptr);
   }
   else
   {
      /*Prepare input buffer and output buffers*/
      /* If RX path,
       *    1. Check if input ports have input buffers available and prepare the buffers for processing.
       *    2. If input buffers are not available then insert zero buffers.*/
      gen_cntr_st_prepare_input_buffers(me_ptr);

      /* If TX path,
       *    1. Check if out ports have input buffers available and prepare the buffers for processing.
       *    2. If input buffers are not available then insert zero buffers.*/
      gen_cntr_st_prepare_output_buffers(me_ptr);

      /* Trigger process once all the ports are prepared with inputs/outputs */
      result = gen_cntr_data_process_frames(me_ptr);
   }

#ifdef USES_THIN_TOPO
   /** checks if the conditions are met to switch to thin topo, and updates the mask. */
   result = gen_cntr_check_and_switch_to_thin_topo(me_ptr);
#endif

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failure in processing data on signal trigger");
   }

   me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_INVALID_TRIGGER;

   /* Check for signal miss, can detect if process took too long and raised_interrupt_counter was incremented
    *  This checks signal miss because of the process being delayed and one or more interrupts have not been serviced.
    */
   if (me_ptr->st_module.raised_interrupt_counter > me_ptr->st_module.processed_interrupt_counter)
   {
      gen_topo_exit_island_temporarily(&me_ptr->topo);
      bool_t continue_processing = TRUE; // dummy
      gen_cntr_check_handle_signal_miss(me_ptr, TRUE /*is_after_process*/, &continue_processing);
   }

   me_ptr->topo.flags.need_to_ignore_signal_miss = FALSE;

   me_ptr->prev_err_print_time_ms = me_ptr->topo.proc_context.err_print_time_in_this_process_ms;

   return result;
}

ar_result_t gen_cntr_st_check_input_and_underrun(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   topo_buf_t            *module_bufs_ptr = in_port_ptr->common.bufs_ptr;

   // Return if the mode is not interrupt driven or if the buffer is full.
   // if data is not flowing (at-gap), then we don't need to underrun. Due to this reason EOS could be flushing even for
   // signal triggered container. Also before moving to at-gap, if port has flushing-eos, then also we don't need to
   // underrun.
   // Even when EOS has moved out of the md-list to internal list, we dont need to underrun
   // 3 levels: EOS at input, EOS internal to algo, EOS comes out and port is at-gap.
   // here, module_buf_ptr->data_ptr must be present, hence checking max_data_len is ok compared to checking for
   // max_buf_len.

   // Dont need to exit island before calling gen_topo_md_list_has_flushing_eos_or_dfg since
   // if md is present we would already be in nlpi due to exiting in process context to propagate md
   if ((module_bufs_ptr[0].actual_data_len == module_bufs_ptr[0].max_data_len) ||
       (!(/* me_ptr->topo.flags.is_signal_triggered */ me_ptr->topo.flags.is_signal_triggered_active)) ||
       (NULL == module_bufs_ptr[0].data_ptr) || (TOPO_DATA_FLOW_STATE_AT_GAP == in_port_ptr->common.data_flow_state) ||
       gen_cntr_ext_in_port_has_flushing_eos_dfg(ext_in_port_ptr) ||
       ((in_port_ptr->common.sdata.metadata_list_ptr) &&
        (gen_topo_md_list_has_flushing_eos_or_dfg(in_port_ptr->common.sdata.metadata_list_ptr))))
   {
      return AR_EOK;
   }

   uint32_t bytes_required_per_buf = in_port_ptr->common.bufs_ptr[0].max_data_len;

   // No need to underrun if the external input has sufficient data as required at the nblc end
   if (in_port_ptr->nblc_end_ptr && in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len &&
       (SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->nblc_end_ptr->common.media_fmt_ptr->data_format)) &&
       (SPF_IS_PACKETIZED_OR_PCM(in_port_ptr->common.media_fmt_ptr->data_format)))
   {
      // buffer size of nblc end input port (it can be the same port as well)
      uint32_t nblc_end_min_req_bytes_per_buf = in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].data_ptr
                                                   ? in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].max_data_len
                                                   : (in_port_ptr->nblc_end_ptr->common.max_buf_len_per_buf);

      // if port is in variable data path then slight less data can also be processed without underrun
      if (me_ptr->topo.flags.is_dm_enabled && gen_topo_is_in_port_in_dm_variable_nblc(&me_ptr->topo, in_port_ptr))
      {
         // subtract 1ms
         nblc_end_min_req_bytes_per_buf -=
            (gen_topo_compute_if_input_needs_addtional_bytes_for_dm(&me_ptr->topo,
                                                                    (gen_topo_input_port_t *)
                                                                       ext_in_port_ptr->gu.int_in_port_ptr));
      }

      if (in_port_ptr->nblc_end_ptr == in_port_ptr)
      { // nblc end is same input port
         bytes_required_per_buf = nblc_end_min_req_bytes_per_buf;
      }
      else
      { // nblc end is not same input port
         uint32_t bytes_required_at_nblc_end_per_buf = 0;
         uint32_t total_bytes                        = 0;

         // nblc end may already have some data buffered, figure out how much more is needed from external input port to
         // complete the requirement at nblc end
         if (nblc_end_min_req_bytes_per_buf > in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len)
         {
            bytes_required_at_nblc_end_per_buf =
               nblc_end_min_req_bytes_per_buf - in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len;

            total_bytes = bytes_required_at_nblc_end_per_buf * in_port_ptr->nblc_end_ptr->common.sdata.bufs_num;

            total_bytes = topo_rescale_byte_count_with_media_fmt(total_bytes,
                                                                 in_port_ptr->common.media_fmt_ptr,
                                                                 in_port_ptr->nblc_end_ptr->common.media_fmt_ptr);

            bytes_required_per_buf = topo_div_num(total_bytes, in_port_ptr->common.sdata.bufs_num);
         }
         else
         {
            bytes_required_per_buf = 0;
         }
      }
   }

   if (in_port_ptr->common.bufs_ptr[0].actual_data_len >= bytes_required_per_buf)
   {
      return AR_EOK;
   }

   // handle actual underrun outside island, due to LPI mem constraints.
   gen_topo_exit_island_temporarily(&me_ptr->topo);
   gen_cntr_st_underrun(me_ptr, ext_in_port_ptr, bytes_required_per_buf);
   return AR_EOK;
}
