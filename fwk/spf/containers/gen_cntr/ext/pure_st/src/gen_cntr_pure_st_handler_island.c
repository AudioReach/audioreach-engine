/**
 * \file gen_cntr_pure_st_topo_handler_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"

/* Check if ST topo is compiled in lpi or NLPI, and exit island if necessary*/
#ifdef PURE_ST_LIB_IN_ISLAND

/* If st topo lib is compiled in island, we simply return without exiting island */
void gen_cntr_vote_against_lpi_if_pure_st_topo_lib_in_nlpi(gen_cntr_t *me_ptr)
{
   return;
}

/* If st topo lib is compiled in island, we simply return without exiting island */
void gen_cntr_exit_lpi_temporarily_if_pure_st_topo_lib_in_nlpi(gen_cntr_t *me_ptr)
{
   return;
}

#else  // !PURE_ST_LIB_IN_ISLAND

void gen_cntr_vote_against_lpi_if_pure_st_topo_lib_in_nlpi(gen_cntr_t *me_ptr)
{
   if (me_ptr->topo.topo_to_cntr_vtable_ptr->vote_against_island)
   {
      me_ptr->topo.topo_to_cntr_vtable_ptr->vote_against_island(&me_ptr->topo);
   }
   return;
}

void gen_cntr_exit_lpi_temporarily_if_pure_st_topo_lib_in_nlpi(gen_cntr_t *me_ptr)
{
   gen_topo_exit_island_temporarily(&me_ptr->topo);
   return;
}
#endif // PURE_ST_LIB_IN_ISLAND

/* Setup the internal input port buffers on signal trigger.
 *  1. Copies data from the external buffer to internal buffer.
 *  2. In the process context, if there is not enough data input UNDERRUNS. */
static ar_result_t gen_cntr_pure_st_setup_internal_input_port_and_preprocess(gen_cntr_t *             me_ptr,
                                                                             gen_cntr_ext_in_port_t * ext_in_port_ptr,
                                                                             gen_topo_process_info_t *process_info_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_input_port_t *in_port_ptr          = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   uint32_t               bytes_copied_per_buf = 0, bytes_to_copy_per_buf = 0, bytes_available_per_buf = 0;
   uint32_t               bytes_in_int_inp_md_prop = gen_topo_get_actual_len_for_md_prop(&in_port_ptr->common);
   uint32_t               bytes_in_ext_in_port     = gen_cntr_get_bytes_in_ext_in_for_md(ext_in_port_ptr);

   // TODO: check if is_input_discontinuity is required for Signal triggered topos
   // is EOF possible at ext in/nblc end input in EP ? since ts disc is ignored in EPs it may not be applicable check.

#ifdef VERBOSE_DEBUGGING
   bool_t sufficient_bytes_copied = TRUE; /*always true for ST cntrs*/
   bool_t is_input_discontinuity =
      in_port_ptr->common.sdata.flags.end_of_frame ||
      (in_port_ptr->nblc_end_ptr && in_port_ptr->nblc_end_ptr->common.sdata.flags.end_of_frame);
   bool_t dbg_got_buf          = FALSE;
   bool_t dbg_inp_insufficient = FALSE;
#endif

   // note this call can return a fresh buf or a buf already at inplace-nblc-end.
   // returns a failure if buffer couldn't be assigned.
   TRY(result, gen_cntr_ext_input_setup_int_port_buf(me_ptr, ext_in_port_ptr, in_port_ptr));

#ifdef VERBOSE_DEBUGGING
   dbg_got_buf = TRUE;
#endif

   // todo: check if we can move it to static function and make it common betwene GT and ST.

   // if there's input discontinuity, at this ext input port or internal port or nblc end, don't read more data.
   // don't read more input that what nblc end needs if it exists for raw-compressed.
   // do not check for in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].data_ptr for covering both requires_data_buf
   // (decoder) and !requires_data_buf (encoder)
   if (in_port_ptr->nblc_end_ptr && (in_port_ptr->nblc_end_ptr != in_port_ptr))
   {
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

   bytes_available_per_buf += in_port_ptr->common.bufs_ptr[0].actual_data_len;

   bytes_to_copy_per_buf = in_port_ptr->common.max_buf_len_per_buf - bytes_available_per_buf;
   // max_buf_len is rescaled version of nblc_end's max_buf_len

   bytes_copied_per_buf = bytes_to_copy_per_buf;
   // If there is more data to be copied from client buffer.
   // Even if client data is not present, process has to be called to flush any remaining input data (esp. @ EoS)
   // Special note: gpr client EOS is popped and read directly in read_data.
   //    So even if there was no EOS at ext-in-port before calling this function, we might end up with one now.
   result = ext_in_port_ptr->vtbl_ptr->read_data(me_ptr, ext_in_port_ptr, &bytes_copied_per_buf);

   // By default need_more_input = TRUE when input is reset, set it to false.
   // TODO: can we avoid setting this to TRUE by default in the first place
   in_port_ptr->flags.need_more_input = FALSE;

   // Handle only in the process context.
   if (process_info_ptr)
   {
      // reset erasure flag. It will be set if context is signal trigger and underflow.
      // It will be always false external inputs during data trigger.
      in_port_ptr->common.sdata.flags.erasure = FALSE;

      /* UNDER RUN: If there is not input buffer available on the input port then push zeros in to the module buffer */
      // Check the trigger mode and fill zeros if the buffer is not filled.
      result = gen_cntr_st_check_input_and_underrun(me_ptr, ext_in_port_ptr);
   }

   /* Bytes copied per buf for v2 currently returned is based on the first buffer size  */
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

#ifdef VERBOSE_DEBUGGING
   {
      uint32_t force_process    = 1;
      uint32_t needs_inp_data   = 0;
      bool_t   ext_cond_not_met = TRUE;
      bool_t   buf_present      = (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr);
      uint32_t flags            = (sufficient_bytes_copied << 7) | (buf_present << 6) | (ext_cond_not_met << 5) |
                       (in_port_ptr->common.sdata.flags.marker_eos << 4) | (is_input_discontinuity << 3) |
                       (force_process << 2) | (dbg_got_buf << 1) | dbg_inp_insufficient;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "preprocess input: module (%lu of %lu) per buf, ext in (%lu of %lu), "
                   "flags %08lX, curr_trigger%u, needs_inp_data%u, inport flags0x%lX",
                   in_port_ptr->common.bufs_ptr[0].actual_data_len,
                   in_port_ptr->common.bufs_ptr[0].max_data_len,
                   ext_in_port_ptr->buf.actual_data_len,
                   ext_in_port_ptr->buf.max_data_len,
                   flags,
                   me_ptr->topo.proc_context.curr_trigger,
                   needs_inp_data,
                   in_port_ptr->flags.word);
   }
#endif

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
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
   }

   return result;
}

/**
 * after input and output buffers are set-up, topo process is called once on signal triggers.
 * 1. If sufficient input is not present, ext in will underrun at this point.
 * 2. If ext output is ready, it ready_to_deliver_output flag will be set and buffer will be released by the caller.
 * 3. If output is ready, delivers the buffers to downstream containers.
 */
static ar_result_t gen_cntr_pure_st_data_process_one_frame(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t                    out_port_index              = 0;
   gen_topo_process_info_t *   process_info_ptr            = &me_ptr->topo.proc_context.process_info;
   gen_topo_process_context_t *pc_ptr                      = &me_ptr->topo.proc_context;
   bool_t                      mf_th_ps_event              = FALSE;
   bool_t                      ready_to_deliver_ext_output = FALSE;
   bool_t                      ext_out_media_fmt_changed   = FALSE;
   gu_module_list_t           *start_module_list_ptr       = me_ptr->topo.started_sorted_module_list_ptr;

   /**
    * Poll ext input ports and buffer as much as possible. If input couldn't be filled completely underrun.
    * Port_thresh_event is handled immediately after process in ST path, so no need to check pending thresh event
    * for every process frame call.
    */
   uint32_t in_port_index = 0;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr), in_port_index++)
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr       = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t * in_port_ptr           = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
      bool_t                  is_ext_in_ready_to_go = FALSE;

      // Pop the input buffers only if the port state is started.
      if (TOPO_PORT_STATE_STARTED != in_port_ptr->common.state)
      {
         continue;
      }

#ifdef VERBOSE_DEBUGGING
      uint32_t num_polled_buffers = 0;
#endif
      while (TRUE)
      {
         if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
         {
            // if already holding on to an input buffer [partial buffer], copy data from the buffer.
            result = gen_cntr_pure_st_setup_internal_input_port_and_preprocess(me_ptr, ext_in_port_ptr, NULL);

            // If the internal topo buffer is filled continue to setup the next input port.
            // here, in_port_ptr->common.buf.data_ptr must be present
            // and hence using max_data_len is ok compared to max_buf_len
            if (in_port_ptr->common.bufs_ptr[0].actual_data_len == in_port_ptr->common.bufs_ptr[0].max_data_len)
            {
               is_ext_in_ready_to_go                   = TRUE;
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

      // underrun using the process context if necessary
      if (!is_ext_in_ready_to_go)
      {
         TRY(result,
             gen_cntr_pure_st_setup_internal_input_port_and_preprocess(me_ptr, ext_in_port_ptr, process_info_ptr));
      }
   }

   out_port_index = 0;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      gen_cntr_ext_out_port_t *temp_ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      /* Prepare output for the external outputs*/
      gen_cntr_st_prepare_output_buffers_per_ext_out_port(me_ptr, temp_ext_out_port_ptr);

      uint32_t actual_data_len = 0;
      gen_cntr_get_ext_out_total_actual_max_data_len(temp_ext_out_port_ptr, &actual_data_len, NULL);

      pc_ptr->ext_out_port_scratch_ptr[out_port_index].prev_actual_data_len = actual_data_len;
   }

   while (TRUE)
   {
      if (gen_cntr_is_pure_signal_triggered(me_ptr))
      {
         result = st_topo_process(&me_ptr->topo, &start_module_list_ptr);
      }
      else
      {
         /** Switches from Pure ST to Gen Topo dynamically in the following scenarios:

         Case 1: After setting up ext input and output container may not support pure ST, due to ext input threshold/media
           format propagation exit and continue processing gen topo. For example, change in threshold leading to
           (num_proc_loops != 1) needs a switch to gen topo.

         Case 2: Module event like MF/threshold changes can lead Pure ST disablement dynamically, hence need to switch
         to gen topo and continue. For example, change in threshold leading to (num_proc_loops != 1) needs a switch to
         gen topo. Note that its not possible to switch to gen_cntr_data_process_frames() at this point, it can lead to
         issues since signal trigger module capi process is already called, and can lead to unintended
         underruns/overruns **/
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! Switching from Pure ST to gen topo from module 0x%08lX.",
                      start_module_list_ptr->module_ptr->module_instance_id);

         result = gen_topo_topo_process(&me_ptr->topo, &start_module_list_ptr, NULL);
      }

      {
         // check and handle if any event is set during topo process
         GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
         CU_FWK_EVENT_HANDLER_CONTEXT

         gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
         cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

         GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do_reconcile*/)
         CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do reconcile*/);
         // here MF must propagate from next module, starting from current module overwrites any data that module might
         // have outputed in this call.
         if (fwk_event_flag_ptr->word || capi_event_flag_ptr->word)
         {
            // Flag to indicate Media format, threshold or process state change event
            mf_th_ps_event = FALSE;
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
         else
         {
            break;
         }
      }
   }

   // free ext input if it was borrowed by the external input
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      // check if a borrowed buffer is stuck in the NBLC path and copy to a topo buffer so
      result |= gen_cntr_clear_borrowed_ext_in_buffer_from_int_ports(me_ptr, ext_in_port_ptr, in_port_ptr, FALSE);
   }

   /**  For each external out port, postprocess data, send media fmt and data down. */
   out_port_index = 0;
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t  *out_port_ptr     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      /** Prepares external output buffer, sends any pending media format.*/
      result |=
         gen_cntr_post_process_ext_output_port(me_ptr, process_info_ptr, pc_ptr, ext_out_port_ptr, out_port_index);

      /** check if output if ready to deliver */
      if ((gen_cntr_ext_out_port_has_buffer(&ext_out_port_ptr->gu)) &&
          (ext_out_port_ptr->max_frames_per_buffer == ext_out_port_ptr->num_frames_in_buf))
      {
         /** deliver the buffer if  its filled. */
         ready_to_deliver_ext_output = TRUE;
      }
      else if (((0 == ext_out_port_ptr->num_frames_in_buf) && ext_out_port_ptr->md_list_ptr) ||
               pc_ptr->ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf)
      {
         // if there's MD when num_frames_in_buf is zero, release, otherwise wait for num_frames_in_buf to become
         // max_frames_per_buffer
         ready_to_deliver_ext_output = TRUE;
      }
      else if (ext_out_port_ptr->flags.out_media_fmt_changed)
      {
         /** deliver buffer if media format has changed even if the buffer is not filled completely.   */
         ready_to_deliver_ext_output = TRUE;
      }

      if (ready_to_deliver_ext_output)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "Gen cntr deliver output buffer");
#endif

         ext_out_media_fmt_changed |= ext_out_port_ptr->flags.out_media_fmt_changed;

         ext_out_port_ptr->vtbl_ptr->write_data(me_ptr, ext_out_port_ptr);

         if (out_port_ptr->common.sdata.flags.end_of_frame)
         {
            out_port_ptr->common.sdata.flags.end_of_frame = FALSE;
         }
      }
   }

   if (ext_out_media_fmt_changed)
   {
      // Container KPPS, BW depend on ext port media fmt
      bool_t FORCE_AGGREGATE_FALSE = FALSE;
      TRY(result, gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE));
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 * Algorithm to process frames
 *  1. Process only one frame, Each signal trigger can only process one frame, st doesnt allow processing multiple
 * frames Handles any ext input MF in the same context by dropping old MF data before first module process.
 *  2. If ext output buffer is ready to deliver, send it downstream
 */
ar_result_t gen_cntr_pure_st_process_frames(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   gen_cntr_reset_process_info(me_ptr);

   /** Process one frame per signal trigger.
    *
    * Important Note: that modules cannot return need more in ST containers. hence we dont need to handle NEED more in
    * the pure signal triggered path.
    */
   result = gen_cntr_pure_st_data_process_one_frame(me_ptr);

   /** Poll control channel and check for incoming ctrl msgs.
    * If any present, do set param and return the msgs. */
   cu_poll_and_process_ctrl_msgs(&me_ptr->cu);

   gen_cntr_check_and_vote_for_island_in_data_path(me_ptr);

   gen_cntr_handle_fwk_events_in_data_path(me_ptr);

#ifdef ST_TOPO_SAFE_MODE
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;
   CU_FWK_EVENT_HANDLER_CONTEXT;
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT;

   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, FALSE /*do_reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /* do_reconcile*/);

   // crash here if any events are not handled, especially MF should have been propagated by now.
   if (me_ptr->flags.is_any_ext_in_mf_pending || fwk_event_flag_ptr->word || capi_event_flag_ptr->word)
   {
      GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                          DBG_ERROR_PRIO,
                          "Events are un-handled is_any_ext_in_mf_pending:%lu fwk_evt_flags:0x%lx "
                          "capi_event_flags:0x%lx",
                          (uint32_t)me_ptr->flags.is_any_ext_in_mf_pending,
                          fwk_event_flag_ptr->word,
                          capi_event_flag_ptr->word);
      spf_svc_crash();
   }
#endif

   /* Transition to generic topology if there is data trigger policy module and its data trigger is satisfied.
    * Else go back to container workloop.*/
   if (me_ptr->topo.num_data_tpm > 0)
   {
      // check if any data trigger is satisfied
      const bool_t CONTINUE_PROCESSING     = FALSE;
      const bool_t IS_PROCESS_CONTEXT_TRUE = TRUE;
      const bool_t IS_ENTRY_FALSE          = FALSE;
      if (CONTINUE_PROCESSING == gen_cntr_wait_for_any_ext_trigger(me_ptr, IS_PROCESS_CONTEXT_TRUE, IS_ENTRY_FALSE))
      {
         GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                             DBG_LOW_PRIO,
                             "Transitioning to data trigger from Signal trigger.");
         if (AR_DID_FAIL(result = gen_cntr_data_process_frames(me_ptr)))
         {
            GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "Failed to process data frames");
         }
      }
   }

   return result;
}
