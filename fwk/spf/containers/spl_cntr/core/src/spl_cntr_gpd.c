/**
 * \file spl_cntr_gpd.c
 * \brief
 *     This file contains spl_cntr logic for the graph processing and postprocessing decisions.
 *
 *  The graph processing decision is broken down into different sub-decisions.
 *  For example, one sub-decision is if an output port is holding a buffer.
 *  Each sub-decision is assigned a bit in a bitmask which is part of the spl_cntr_t
 *  object. At any point in time, when a sub-decision is satisfied (example when
 *  we pop a buffer from an output buffer queue, that sub-decision is satisfied),
 *  we can clear the relevant bitmask. When calling the graph processing decision,
 *  we pass in a gpd_check mask which determines which sub-decisions should be
 *  checked. If all sub-decisions are satisfied (i.e. all relevant bits are
 *  cleared), the gpd can return true.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"

/**
 * Used to remove a bit from the gpd mask. Handles modifying send_to_topo as well.
 */
static inline void spl_cntr_ext_in_port_clear_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   spl_cntr_remove_ext_in_port_from_gpd_mask(me_ptr, ext_in_port_ptr);
   cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_in_port_ptr->cu.bit_mask);

   // Only send started ports to topo.
   if ((TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
       (spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr)))
   {
      ext_in_port_ptr->topo_buf.send_to_topo = TRUE;
   }
   else
   {
      ext_in_port_ptr->topo_buf.send_to_topo = FALSE;
   }
}

/**
 * Used to remove a bit from the gpd mask. Handles modifying send_to_topo as well.
 */
static inline void spl_cntr_ext_out_port_clear_gpd_mask(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   spl_cntr_remove_ext_out_port_from_gpd_mask(me_ptr, ext_out_port_ptr);
   cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_out_port_ptr->cu.bit_mask);

   // Only send only started ports to topo.
   if (TOPO_PORT_STATE_STARTED == out_port_ptr->t_base.common.state)
   {
      ext_out_port_ptr->topo_buf.send_to_topo = TRUE;
   }
   else
   {
      ext_out_port_ptr->topo_buf.send_to_topo = FALSE;
   }
}

static uint32_t spl_cntr_get_all_inputs_gpd_mask(spl_cntr_t *me_ptr)
{
   gu_ext_in_port_list_t *ext_in_port_list_ptr = NULL;
   uint32_t               gpd_mask             = 0;

   for (ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gpd_mask                                = ext_in_port_ptr->cu.bit_mask;
   }
   return gpd_mask;
}

// Used in the gpd: Clear all external input bits from the gpd, but only send ports to the topo
// which have any data. Also stop listening to every external input port. Used if the threshold
// is disabled.
static void spl_cntr_gpd_clear_input_mask_for_threshold_disabled(spl_cntr_t *me_ptr)
{
   gu_ext_in_port_list_t *ext_in_port_list_ptr = NULL;

   for (ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t * in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      if (!in_port_ptr->flags.is_threshold_disabled_prop)
      {
         // skip this external input port if threshold is not disabled here.
         continue;
      }
      ext_in_port_ptr->next_process_samples_valid = FALSE;

      spl_cntr_remove_ext_in_port_from_gpd_mask(me_ptr, ext_in_port_ptr);

      ext_in_port_ptr->topo_buf.send_to_topo = FALSE;

      // Only send ports to the topo which have any data or force process applies (flushing eos without data).
      if ((TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
          spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr) &&
          ((ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len != 0) || ext_in_port_ptr->topo_buf.end_of_frame))
      {
         ext_in_port_ptr->topo_buf.send_to_topo = TRUE;
      }
   }
}

/*
 * Checks if this external input port should satisfy the gpd, even if there is not
 * enough data buffered. TRUE if one of the following holds:
 * 1. There is a pending input media format
 * 2. We are handling flushing eos. For non-flushing EOS, we still expect to received data for the next stream
 *    immediately, therefore we don't have to force process, we can just wait for the next data.
 * 3. Timestamp discontinuity. In this case we will never be able to buffer new data until the current data is
 *    processed, so try to process it.
 * 4. If we have a dfg metadata at the external input port.
 */
static inline bool_t spl_cntr_ext_in_port_force_process(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t                 force_process = FALSE;
   gen_topo_input_port_t *in_port_ptr   = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   bool_t is_mf_changed =
      spl_cntr_media_fmt_is_pending(&(ext_in_port_ptr->pending_media_fmt)) && in_port_ptr->common.flags.is_mf_valid;

   force_process = (is_mf_changed) || ext_in_port_ptr->topo_buf.timestamp_discontinuity ||
                   spl_topo_input_port_has_dfg_or_flushing_eos(in_port_ptr);

   ext_in_port_ptr->topo_buf.end_of_frame = force_process;

   if (ext_in_port_ptr->topo_buf.end_of_frame)
   {
      me_ptr->topo.simpt_event_flags.check_eof = TRUE;

#ifdef SPL_SIPT_DBG
      TOPO_MSG(me_ptr->topo.t_base.gu.log_id,
               DBG_MED_PRIO,
               "Ext in port idx = %ld, miid = 0x%lx eof become %ld, check_eof becomes TRUE.",
               ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
               ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
               ext_in_port_ptr->topo_buf.end_of_frame);
#endif
   }

   return force_process;
}

/*
 * Checks if nblc end of external input port has eos or dfg.
 */
static inline bool_t spl_cntr_ext_in_port_has_pending_eof_at_nblc_end(spl_cntr_t *            me_ptr,
                                                                      spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t                 has_pending_eof = FALSE;
   gen_topo_input_port_t *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   if (in_port_ptr->nblc_end_ptr)
   {
      has_pending_eof |=
         spl_topo_input_port_has_pending_eof(&me_ptr->topo, (spl_topo_input_port_t *)in_port_ptr->nblc_end_ptr);
   }

   return has_pending_eof;
}

/**
 * Framework layer check if the external input port is present. Uses ext_in_port_has_enough_data to
 * check if the local buffer is sufficiently filled.
 */
static bool_t spl_cntr_ext_in_port_is_trigger_present(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   bool_t                 is_trigger_present = FALSE;
   spl_topo_input_port_t *in_port_ptr        = (spl_topo_input_port_t *)(ext_in_port_ptr->gu.int_in_port_ptr);

   if ((GEN_TOPO_DATA_NOT_NEEDED | GEN_TOPO_DATA_BLOCKED) & spl_topo_in_port_needs_data(&me_ptr->topo, in_port_ptr))
   {
      is_trigger_present = TRUE;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Checking for trigger on external input port idx = %ld, miid = 0x%lx: "
                "is_trigger_preset %lu",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                is_trigger_present);
#endif
   return is_trigger_present;
}

/**
 * Checks if the held input data msg has been completely consumed. If so, frees
 * the data msg.
 */
static inline void spl_cntr_check_and_free_held_input_data_msg(spl_cntr_t *            me_ptr,
                                                               spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
   {
      spf_msg_header_t *     header_ptr    = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;
      uint32_t               held_data_msg_consumed_bytes =
         topo_bytes_per_ch_to_bytes(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch,
                                    &ext_in_port_ptr->cu.media_fmt);

      /* Freeing the input data msg if it is fully consumed and setting the gpd input mask*/
      if (input_buf_ptr->actual_size == held_data_msg_consumed_bytes)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "freed input data cmd of size %ld bytes on ext port idx = %ld, miid = 0x%lx bit_mask 0x%lx",
                      input_buf_ptr->actual_size,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->cu.bit_mask);
#endif
         cu_free_input_data_cmd(&me_ptr->cu, &ext_in_port_ptr->gu, AR_EOK);
         ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch = 0;
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "freed input data cmd");
#endif
      }
   }
}

/**
 * Check and free held input data messages from all the external input ports.
 */
static inline void spl_cntr_check_and_free_all_held_input_data_msg(spl_cntr_t *me_ptr)
{
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      spl_cntr_check_and_free_held_input_data_msg(me_ptr, ext_in_port_ptr);
   }
}

/**
 * If there is data buffer in the input Q then pops it.
 */
static inline void spl_cntr_check_and_get_input_buffer(spl_cntr_t *            me_ptr,
                                                       spl_cntr_ext_in_port_t *ext_in_port_ptr,
                                                       uint32_t *              data_needed_in_bytes_per_ch_ptr)
{
   // If there is no held buffer and there is a trigger in Q then go ahead.
   if ((NULL == ext_in_port_ptr->cu.input_data_q_msg.payload_ptr) &&
       posal_channel_poll_inline(me_ptr->cu.channel_ptr, ext_in_port_ptr->cu.bit_mask))
   {
      /** If we need data then it means we need to buffer more data on that port so we don't need to
       * unnecessarily check the below conditions.
       * But if data needed is zero then it means we have check if we are allowed to buffer more data.
       */
      if (0 == *data_needed_in_bytes_per_ch_ptr)
      {
         // if input is blocked then don't pull from Q.
         if (!((me_ptr->gpd_mask | me_ptr->gpd_optional_mask) & ext_in_port_ptr->cu.bit_mask))
         {
            return;
         }

         *data_needed_in_bytes_per_ch_ptr = spl_cntr_ext_in_get_free_space(me_ptr, ext_in_port_ptr);

         // if there is not enough space then don't pull from Q.
         if (0 == *data_needed_in_bytes_per_ch_ptr)
         {
            return;
         }
      }

      bool_t found_data_msg = FALSE;

      // Try to get the next data message.
      while ((FALSE == found_data_msg) &&
             (posal_channel_poll_inline(me_ptr->cu.channel_ptr, ext_in_port_ptr->cu.bit_mask)))
      {
         spf_msg_t *q_msg_pp = NULL;

         // Peek the message opcode, proceed only if it is data buffer.
         posal_queue_peek_front(ext_in_port_ptr->gu.this_handle.q_ptr, (posal_queue_element_t **)&q_msg_pp);
         if (SPF_MSG_DATA_BUFFER == q_msg_pp->msg_opcode)
         {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "Popping a new buffer on ext input port idx = %ld, miid = 0x%lx!",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
            // Pop the input data message.
            cu_get_input_data_msg(&me_ptr->cu, &ext_in_port_ptr->gu);

            /* Drop data messages which are < 1ms in VPTX.
             * - Upstream messages are expected to be >= 1ms.
             * - IIR resampler within MFC requires operation at fixed frame size. For threshold-disabled scenario, we
             * use
             *   1ms frame size.
             * - We want to avoid scenarios in VPTX where multiple data msgs get buffered in a single trigger to avoid
             * corner-case
             *   timing scenarios.
             */
            if (spl_cntr_fwk_extn_voice_delivery_need_drop_data_msg(me_ptr, ext_in_port_ptr))
            {
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_HIGH_PRIO,
                            "Dropping data from buffer on ext input port idx = %ld, miid = 0x%lx for voice delivery "
                            "cntr "
                            "due to data length < 1ms.",
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
               spl_cntr_drop_input_data(me_ptr, ext_in_port_ptr);
            }

            if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
            {
               if (TOPO_DATA_FLOW_STATE_FLOWING != ext_in_port_ptr->topo_buf.data_flow_state)
               {

                  /* this function can call the graph processing decision function, which will invalidate the
                   * "data_needed_in_bytes_per_ch_ptr" for this context. therefore setting it to zero here so that it
                   * gets updated later.
                   */
                  spl_cntr_ext_in_port_handle_data_flow_begin(me_ptr, ext_in_port_ptr);

                  *data_needed_in_bytes_per_ch_ptr = 0;
               }

               //if prebuffer then hold it in the prebuffer Q
               if (ext_in_port_ptr->cu.preserve_prebuffer)
               {
                  cu_ext_in_handle_prebuffer(&me_ptr->cu,
                                             &ext_in_port_ptr->gu,
                                             (me_ptr->fwk_extn_info.resync_signal_ptr) ? 1 : 0);  //for VpTx, minimum buffer to hold is 1
               }
            }

            // check the ptr again. it is possible that the buffer is pushed into prebuffer-q
            if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
            {
               found_data_msg = TRUE;
            }
         }
         // If next message isn't a data message, then break.
         else
         {
            break;
         }
      }
   }
}

/**
 * Decide whether to call topo process or not.
 * First pass of algorithm, assume one external input port, one external
 * output port. Check if we are holding both input and ouput, if so return
 * true. If we return false, start listening for anything we are waiting for.
 * first_iter is TRUE the first time the gpd is called from the outer loop.
 */
static bool_t spl_cntr_graph_processing_decision(spl_cntr_t *me_ptr, uint32_t check_mask, bool_t first_iter)
{
   bool_t                  decision                    = FALSE;
   gu_ext_in_port_list_t * ext_in_port_list_ptr        = NULL;
   gu_ext_out_port_list_t *ext_out_port_list_ptr       = NULL;
   bool_t                  is_none_of_the_port_started = TRUE;
   bool_t                  any_condition_satisfied     = FALSE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   // Only check an input port if it triggered us.
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "GPD begin, check mask 0x%lX", check_mask);
#endif

   for (ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gen_topo_input_port_t * in_port_ptr =
         (gen_topo_input_port_t *)ext_in_port_list_ptr->ext_in_port_ptr->int_in_port_ptr;

      if (!(check_mask & ext_in_port_ptr->cu.bit_mask))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: not checking ext input idx = %ld, miid = 0x%lx bit_mask 0x%lx!",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->cu.bit_mask);
#endif

         continue;
      }

      // Don't check stopped ports -> make sure the gpd bit mask is cleared.
      if (TOPO_PORT_STATE_STARTED != in_port_ptr->common.state)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: clearing bit mask on not started port ext input idx = %ld, miid = 0x%lx bit_mask "
                      "0x%lx, "
                      "state 0x%lx!",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->cu.bit_mask,
                      in_port_ptr->common.state);
#endif
         spl_cntr_ext_in_port_clear_gpd_mask(me_ptr, ext_in_port_ptr);
         continue;
      }

      // at least one checked port is started.
      is_none_of_the_port_started = FALSE;

      // If we are force processing or eos/dfg stuck at nblc end
      // then we should block new input until processing is complete.
      // So mark input as satisfied before buffering new input.
      if (spl_cntr_ext_in_port_force_process(me_ptr, ext_in_port_ptr) ||
          spl_cntr_ext_in_port_has_pending_eof_at_nblc_end(me_ptr, ext_in_port_ptr))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: forcing process on input idx = %ld miid = 0x%lx bit_mask 0x%lx. clearing gpd bit.",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->cu.bit_mask);
#endif
         any_condition_satisfied = TRUE;
         spl_cntr_ext_in_port_clear_gpd_mask(me_ptr, ext_in_port_ptr);
         continue;
      }

      /* This variable is used to cache the amount of buffering space available on external input port.
       * Since this is in stack therefore we have to make sure that the gpd function is not called recursively from this
       * function.
       */
      uint32_t data_needed_bytes    = 0;
      bool_t   is_any_data_buffered = FALSE;

      do
      {
         if (first_iter)
         {
            // pull the data buffer if it is available in the Q and topo buffer has space.
            // this is done only for first iteration. this is to make sure that we go back to the work-loop context
            // after processing one frame. this is to ensure that we are not stuck in busy processing loop.

            // we don't want to pull the data in threshold disabled case as well, consider a case where sync module
            // needs only 1ms data to meet threshold but we pull 2 buffers then 1 buffer will remain un-consumed at sync
            // input.
            spl_cntr_check_and_get_input_buffer(me_ptr, ext_in_port_ptr, &data_needed_bytes);
         }

         // if there is not data buffer then there is nothing to held in topo buffer.
         if (NULL == ext_in_port_ptr->cu.input_data_q_msg.payload_ptr)
         {
            break;
         }

         // Buffer the input data in the input port buffer
         spl_cntr_buffer_held_input_data(me_ptr, ext_in_port_ptr, &data_needed_bytes);

         is_any_data_buffered = TRUE;

         // For RT use cases, free the input data msg before process.
         if (ext_in_port_ptr->is_realtime_usecase)
         {
            spl_cntr_check_and_free_held_input_data_msg(me_ptr, ext_in_port_ptr);
         }

         if (spl_cntr_ext_in_port_force_process(me_ptr, ext_in_port_ptr))
         {
            data_needed_bytes = 0;
            break;
         }

         /* If threshold is disabled and Q has more data buffers then
          * try to fill the topo buffer before the topo process.
          * Benefits:-
          * 1. if data buffer is small compared to container frame size then
          * we can avoid multiple topo-process for each smaller data buffer.
          * 2. if there are two external input ports and one has threshold amount of data in topo buffer
          * but second one has only few samples but it also has sufficient data in Q.
          * Now since threshold is disabled, topo process will be called even if second port doesn't meet threshold and
          * sync module will unnecessary push zeros to complete the threshold. Amount of zeros which sync module push
          * can be reduced if we pull all the data from Q into the topo buffer.
          */
         /* Try to fill input buffer. Optimization to avoid multiple trigger.
          */
      } while ((NULL == ext_in_port_ptr->cu.input_data_q_msg.payload_ptr) && (0 < data_needed_bytes));

      // if nothing is buffered then don't need to check anything, gpd bit will remain set.
      if (is_any_data_buffered)
      {
         // If data is not needed then trigger is already satisfied and don't need to check again.
         if ((0 == data_needed_bytes) || (spl_cntr_ext_in_port_is_trigger_present(me_ptr, ext_in_port_ptr)))
         {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "gpd: input port idx = %ld miid = 0x%lx bit_mask 0x%lx is filled and ready to process.",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                         ext_in_port_ptr->cu.bit_mask);
#endif

            any_condition_satisfied = TRUE;
            spl_cntr_ext_in_port_clear_gpd_mask(me_ptr, ext_in_port_ptr);
         }
         else
         {
            if (TOPO_DATA_FLOW_STATE_FLOWING == ext_in_port_ptr->topo_buf.data_flow_state)
            {
               /* If trigger is not present then set the gpd bit so that this port doesn't trigger the topo_process.
                */
               ext_in_port_ptr->topo_buf.send_to_topo = FALSE;
               spl_cntr_add_ext_in_port_from_gpd_mask(me_ptr, ext_in_port_ptr);
            }
         }
      }
   }

   // This check is to avoid processing if we are waiting for voice proc tick,
   if (VOICE_PROC_TRIGGER_MODE == me_ptr->trigger_policy)
   {
      return FALSE;
   }

   for (ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      spl_topo_output_port_t * out_port_ptr     = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
      topo_port_state_t        state            = out_port_ptr->t_base.common.state;

      if (!(check_mask & ext_out_port_ptr->cu.bit_mask))
      {
// check the output port that triggered us and the optional ports.
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: output port idx = %ld miid = 0x%lx bit_mask 0x%lx not checked, check mask not set.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_out_port_ptr->cu.bit_mask);
#endif
         continue;
      }

      // Don't check stopped ports -> make sure the gpd bit mask is cleared.
      if (TOPO_PORT_STATE_STARTED != state)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: clearing bit mask on not started port ext output port idx = %ld miid = 0x%lx bit_mask "
                      "0x%lx, "
                      "state 0x%lx!",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_out_port_ptr->cu.bit_mask,
                      state);
#endif
         spl_cntr_ext_out_port_clear_gpd_mask(me_ptr, ext_out_port_ptr);
         continue;
      }

      // at least one port is started.
      is_none_of_the_port_started = FALSE;

      // if buffer is not present then try to pop it.
      if (!ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
      {
         spl_cntr_get_output_buffer(me_ptr, ext_out_port_ptr);
      }

      if (spl_cntr_ext_out_port_has_buffer(&ext_out_port_ptr->gu))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "gpd: output port idx = %ld miid = 0x%lx bit_mask 0x%lx available to process.",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_out_port_ptr->cu.bit_mask);
#endif
         any_condition_satisfied = TRUE;
         spl_cntr_ext_out_port_clear_gpd_mask(me_ptr, ext_out_port_ptr);
      }
      else
      {
         /* If trigger is not present then set the gpd bit so that this port doesn't trigger the topo_process.
          */
         ext_out_port_ptr->topo_buf.send_to_topo = FALSE;
         spl_cntr_add_ext_out_port_from_gpd_mask(me_ptr, ext_out_port_ptr);
      }
   }

   if (is_none_of_the_port_started)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Overruling gpd decision. no external port is started.");
#endif
      decision = FALSE;
      return decision;
   }

   if (me_ptr->topo.simpt1_flags.threshold_disabled)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "threshold disabled case: clearing all input gpd bits and sending all data into topo.");
#endif

      // In threshold-disabled mode, all input bits get cleared if there is any data on any input port.
      any_condition_satisfied = TRUE;
      spl_cntr_gpd_clear_input_mask_for_threshold_disabled(me_ptr);
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "checking gpd. gpd_mask: 0x%lx, optional mask: 0x%lx.",
                me_ptr->gpd_mask,
                me_ptr->gpd_optional_mask);
#endif

   // Make decision.
   // If there's any trigger policy, then we process on when any single trigger is satisfied. The input trigger policy
   // is only satisfied once enough data is buffered on the local buffer.
   // When checking on next iterations through the loop, the decision is always TRUE. We will break from the
   // loop when nothing happens.
   if (me_ptr->topo.t_base.num_data_tpm)
   {
      if ((first_iter && any_condition_satisfied) || !first_iter)
      {
         // clear gpd mask so that topo-process is triggered for all modules.
         me_ptr->gpd_mask = 0;
         memset(me_ptr->gpd_mask_arr, 0, sizeof(uint32_t) * me_ptr->topo.t_base.gu.num_parallel_paths);
      }
   }

   // if gpd mask is cleared for any parallel path then trigger topo process
   decision = (0 == me_ptr->gpd_mask) ? TRUE : spl_cntr_is_any_parallel_path_gpd_satisfied(me_ptr);
   return decision;
}

/**
 * Called to see if more data needed on an external input port or not.
 */
static gen_topo_data_need_t spl_cntr_ext_in_port_needs_data(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   // If there's still a held data in Q, don't listen for more input
   if (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr &&
       (SPF_MSG_DATA_BUFFER == ext_in_port_ptr->cu.input_data_q_msg.msg_opcode))
   {
      spf_msg_header_t *     header_ptr    = (spf_msg_header_t *)(ext_in_port_ptr->cu.input_data_q_msg.payload_ptr);
      spf_msg_data_buffer_t *input_buf_ptr = (spf_msg_data_buffer_t *)&header_ptr->payload_start;
      uint32_t               held_data_msg_consumed_bytes =
         topo_bytes_per_ch_to_bytes(ext_in_port_ptr->held_data_msg_consumed_bytes_per_ch,
                                    &ext_in_port_ptr->cu.media_fmt);

      if (input_buf_ptr->actual_size > held_data_msg_consumed_bytes)
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "spl_cntr_ext_in_port_needs_data: data not needed"
                      " for ext_in_port id = 0x%lx, miid = 0x%lx, bit_mask 0x%lx."
                      " Already holding a data in Q.",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->cu.bit_mask);
#endif
         return GEN_TOPO_DATA_NOT_NEEDED;
      }
   }

   gen_topo_data_need_t data_needed =
      spl_topo_in_port_needs_data(&me_ptr->topo, (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);

   // there can be a module at external input which can't consume one sample so to avoid hang situation we should
   // allow more buffering if possible.
   if ((GEN_TOPO_DATA_NOT_NEEDED == data_needed) && (0 != spl_cntr_ext_in_get_free_space(me_ptr, ext_in_port_ptr)))
   {
      data_needed = GEN_TOPO_DATA_NEEDED_OPTIONALLY;
   }

   return data_needed;
}

/**
 * Called to see if we buffer is needed on an external output port or not.
 */
static gen_topo_port_trigger_need_t spl_cntr_ext_out_port_needs_trigger(spl_cntr_t *             me_ptr,
                                                                        spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   // If there's still a held buffer, don't listen for output
   if (ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "spl_cntr_ext_out_port_needs_trigger: buffer not needed"
                   " for ext_out_port id = 0x%lx, miid = 0x%lx, bit_mask 0x%lx."
                   " Already holding a buffer from Q.",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->cu.bit_mask);
#endif
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   return spl_topo_out_port_needs_trigger(&me_ptr->topo,
                                          (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr);
}
/**
 * Called to see if we need to listen to the external input port's data queue or not
 * update the gpd mask accordingly.
 */
void spl_cntr_ext_in_port_update_gpd_bit(spl_cntr_t *me_ptr, spl_cntr_ext_in_port_t *ext_in_port_ptr)
{
   gen_topo_data_need_t data_need = GEN_TOPO_DATA_NOT_NEEDED;

   data_need = spl_cntr_ext_in_port_needs_data(me_ptr, ext_in_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "spl_cntr_ext_in_port_update_gpd_bit: data_need %d (1:non-needed, 2:needed, 4:optional, 8:blocked)"
                " for ext_in_port id = 0x%lx, miid = 0x%lx, bit_mask 0x%lx",
                data_need,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                ext_in_port_ptr->cu.bit_mask);
#endif

   if ((GEN_TOPO_DATA_NOT_NEEDED | GEN_TOPO_DATA_BLOCKED) & data_need)
   {
      spl_cntr_ext_in_port_clear_gpd_mask(me_ptr, ext_in_port_ptr);
   }
   else if (GEN_TOPO_DATA_NEEDED == data_need)
   {
      cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_in_port_ptr->cu.bit_mask);
      spl_cntr_add_ext_in_port_from_gpd_mask(me_ptr, ext_in_port_ptr);
   }
   else
   {
      cu_set_bits_in_x(&me_ptr->gpd_optional_mask, ext_in_port_ptr->cu.bit_mask);
      spl_cntr_remove_ext_in_port_from_gpd_mask(me_ptr, ext_in_port_ptr);
   }
}

/**
 * Called to see if we need to listen to the external output port's buffer queue or not
 * update the gpd mask accordingly.
 */
static void spl_cntr_ext_out_port_update_gpd_bit(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   gen_topo_port_trigger_need_t trigger_need = GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;

   trigger_need = spl_cntr_ext_out_port_needs_trigger(me_ptr, ext_out_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "spl_cntr_ext_out_port_update_gpd_bit: trigger_need %d (1:needed, 2:not-needed, 4:optional)"
                " for ext_out_port id = 0x%lx, miid = 0x%lx, bit_mask 0x%lx",
                trigger_need,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                ext_out_port_ptr->cu.bit_mask);
#endif

   if (GEN_TOPO_PORT_TRIGGER_NOT_NEEDED == trigger_need)
   {
      spl_cntr_ext_out_port_clear_gpd_mask(me_ptr, ext_out_port_ptr);
   }
   else if (GEN_TOPO_PORT_TRIGGER_NEEDED == trigger_need)
   {
      cu_clear_bits_in_x(&me_ptr->gpd_optional_mask, ext_out_port_ptr->cu.bit_mask);
      spl_cntr_add_ext_out_port_from_gpd_mask(me_ptr, ext_out_port_ptr);
   }
   else
   {
      spl_cntr_remove_ext_out_port_from_gpd_mask(me_ptr, ext_out_port_ptr);
      cu_set_bits_in_x(&me_ptr->gpd_optional_mask, ext_out_port_ptr->cu.bit_mask);
   }
}

/**
 * For all external ports, checks whether to start listening to them
 * (in the gpd mask and in started state), and starts listening.
 */
ar_result_t spl_cntr_update_cu_bit_mask(spl_cntr_t *me_ptr)
{
   ar_result_t             result                = AR_EOK;
   uint32_t                input_listen_mask     = 0;
   uint32_t                output_listen_mask    = 0;
   uint32_t                stop_mask             = 0;
   uint32_t                optional_mask         = 0;
   gu_ext_out_port_list_t *ext_out_port_list_ptr = NULL;
   gu_ext_in_port_list_t * ext_in_port_list_ptr  = NULL;

   // during voice proc trigger, stop waiting on any external ports.
   if (VOICE_PROC_TRIGGER_MODE == me_ptr->trigger_policy)
   {
      gu_ext_in_port_list_t * ext_in_port_list_ptr  = NULL;
      gu_ext_out_port_list_t *ext_out_port_list_ptr = NULL;

      for (ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         stop_mask |= ext_in_port_ptr->cu.bit_mask;
      }

      for (ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         stop_mask |= ext_out_port_ptr->cu.bit_mask;
      }
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "End of Post gpd: not listening to input/output ports, waiting on voice timer trigger");
#endif
   }
   else
   {
      uint32_t is_threshold_disabled_or_tgp_active = FALSE;

      // if container is not running on any_trigger_policy then first wait on all the inputs for this path
      // and once all the inputs are triggered then wait on all the outputs.
      // if threshold is disabled then listen to output port irrespective of input triggers. This is
      // to make sure inputs are not blocked because of output.
      if (((0 != me_ptr->topo.t_base.num_data_tpm)) || (me_ptr->topo.simpt1_flags.threshold_disabled))
      {
         is_threshold_disabled_or_tgp_active = TRUE;
      }

      for (uint16_t path_index = 0; path_index < me_ptr->cu.gu_ptr->num_parallel_paths; path_index++)
      {
         uint32_t input_listen_mask_for_this_path  = 0;
         uint32_t output_listen_mask_for_this_path = 0;

         for (ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
              LIST_ADVANCE(ext_in_port_list_ptr))
         {
            spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

            if (path_index != ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index)
            {
               continue;
            }

            bool_t add_to_stop_mask = TRUE;

            if ((me_ptr->gpd_mask & ext_in_port_ptr->cu.bit_mask))
            {
               input_listen_mask_for_this_path |= ext_in_port_ptr->cu.bit_mask;
               add_to_stop_mask = FALSE;
            }
            else if ((me_ptr->gpd_optional_mask & ext_in_port_ptr->cu.bit_mask))
            {
               optional_mask |= ext_in_port_ptr->cu.bit_mask;
               add_to_stop_mask = FALSE;
            }

            if (add_to_stop_mask)
            {
               stop_mask |= ext_in_port_ptr->cu.bit_mask;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "Not listening to external input port idx = %ld, miid = 0x%lx",
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
            }
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
            else
            {
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "Start listening to external input port idx = %ld, miid = 0x%lx",
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                            ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
            }
#endif
         }

         for (ext_out_port_list_ptr = me_ptr->cu.gu_ptr->ext_out_port_list_ptr; ext_out_port_list_ptr;
              LIST_ADVANCE(ext_out_port_list_ptr))
         {
            spl_cntr_ext_out_port_t *ext_out_port_ptr =
               (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

            if (path_index != ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index)
            {
               continue;
            }

            bool_t add_to_stop_mask = TRUE;

            if ((me_ptr->gpd_mask & ext_out_port_ptr->cu.bit_mask))
            {
               output_listen_mask_for_this_path |= ext_out_port_ptr->cu.bit_mask;
               add_to_stop_mask = FALSE;
            }
            else if ((me_ptr->gpd_optional_mask & ext_out_port_ptr->cu.bit_mask))
            {
               optional_mask |= ext_out_port_ptr->cu.bit_mask;
               add_to_stop_mask = FALSE;
            }

            if (add_to_stop_mask)
            {
               stop_mask |= ext_out_port_ptr->cu.bit_mask;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "Not listening to external output port idx = %ld, miid = 0x%lx",
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif
            }
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
            else
            {
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "Start listening to external output port idx = %ld, miid = 0x%lx",
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
            }
#endif
         }

         /*For voice containers, wait for input trigger first and then start to listen on input*/
         if (cu_has_voice_sid(&me_ptr->cu))
         {
            // listen to input if input mask is set.
            input_listen_mask |= input_listen_mask_for_this_path;

            // listen to output if input mask is zero (input trigger satisfied) or
            // if threshold is disabled or
            // if there is any tgp module in topology.
            if (is_threshold_disabled_or_tgp_active || (0 == input_listen_mask_for_this_path))
            {
               output_listen_mask |= output_listen_mask_for_this_path;
            }
            else
            {
               // otherwise stop listening to output mask
               stop_mask |= output_listen_mask_for_this_path;
            }
         }

         /*For other usecase always wait on output first and then switch to input, this is to avoid unnecessary
          * buffering at external input port*/
         else
         {
            // listen to output if output mask is set.
            output_listen_mask |= output_listen_mask_for_this_path;

            // listen to input if output mask is zero (output trigger satisfied) or
            // if threshold is disabled or
            // if there is any tgp module in topology.
            if (is_threshold_disabled_or_tgp_active || (0 == output_listen_mask_for_this_path))
            {
               input_listen_mask |= input_listen_mask_for_this_path;
            }
            else
            {
               // otherwise stop listening to input mask
               stop_mask |= input_listen_mask_for_this_path;
            }
         }
      }
   }

   cu_start_listen_to_mask(&me_ptr->cu, (input_listen_mask | output_listen_mask | optional_mask));
   cu_stop_listen_to_mask(&me_ptr->cu, stop_mask);

   return result;
}

/**
 * checks whether we need to add each external port
 * gpd bit back to the gpd mask or not. If we do, adds the gpd bit mask back to either the optional or
 * required gpd bit mask.
 */
ar_result_t spl_cntr_update_gpd_and_cu_bit_mask(spl_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      ext_in_port_ptr->topo_buf.send_to_topo = FALSE;

      spl_cntr_ext_in_port_update_gpd_bit(me_ptr, ext_in_port_ptr);
   }

   // Check to set bits back in the gpd.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      ext_out_port_ptr->topo_buf.send_to_topo = FALSE;

      spl_cntr_ext_out_port_update_gpd_bit(me_ptr, ext_out_port_ptr);
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "gpd=0x%x optional gpd=0x%x",
                me_ptr->gpd_mask,
                me_ptr->gpd_optional_mask);
#endif

   // Start waiting on anything set in the gpd mask.
   spl_cntr_update_cu_bit_mask(me_ptr);

   return result;
}

#if 0
/**
 *  Function to drop all the metadata except flushing eos, dfg and stream associate.
 *  Should be called when dropping external output temp buffer data.
 */
static void spl_cntr_ext_out_drop_non_eos_dfg_metadata(spl_cntr_t *me_ptr, spl_cntr_ext_out_port_t *ext_out_port_ptr)
{
   spl_topo_output_port_t *int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   spf_list_node_t *       new_md_list_ptr  = NULL;
   module_cmn_md_list_t *  md_list_ptr      = int_out_port_ptr->md_list_ptr;

   // move dfg and flushing eos metadata to new_md_list_ptr
   while (md_list_ptr)
   {
      module_cmn_md_t *     md_ptr           = md_list_ptr->obj_ptr;
      module_cmn_md_list_t *next_md_list_ptr = md_list_ptr->next_ptr;

      if ((MODULE_CMN_MD_ID_DFG == md_ptr->metadata_id) ||
          ((MODULE_CMN_MD_ID_EOS == md_ptr->metadata_id) && gen_topo_is_flushing_eos(md_ptr)))
      {
         spf_list_move_node_to_another_list(&new_md_list_ptr,
                                            (spf_list_node_t *)md_list_ptr,
                                            (spf_list_node_t **)&int_out_port_ptr->md_list_ptr);
      }
      md_list_ptr = next_md_list_ptr;
   }

   gen_topo_destroy_all_metadata(me_ptr->topo.t_base.gu.log_id,
                                 (void *)ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr,
                                 &int_out_port_ptr->md_list_ptr,
                                 TRUE /*is_dropped*/);

   spf_list_merge_lists((spf_list_node_t **)&int_out_port_ptr->md_list_ptr, &new_md_list_ptr);
   return;
}
#endif

/**
 * Decide whether we should deliver the output buffer or not.
 */
static bool_t spl_cntr_should_deliver_output_buffer(spl_cntr_t *             me_ptr,
                                                    spl_cntr_ext_out_port_t *ext_out_port_ptr,
                                                    uint32_t                 out_port_index)
{
   spl_topo_output_port_t *int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

   if (!ext_out_port_ptr->topo_buf.buf_ptr)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Warning, checking to deliver an output buffer that doesn't exist on port idx = %ld, miid = 0x%lx!",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif
      return FALSE;
   }

   uint32_t output_buffer_size =
      ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len * ext_out_port_ptr->topo_buf.num_bufs;

   // The force delivery flag supersedes other logic.
   if (TRUE == me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "force deliver output buffer on port idx = %ld, miid = 0x%lx",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif
      return TRUE;
   }

   // If there is no data/metadata to deliver, we should not deliver. If the force delivery flag is
   // set, we would return deliver - but while delivering, we'll push it back to our output bufQ
   if ((0 == output_buffer_size) && (!int_out_port_ptr->md_list_ptr))
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "no output or metadata in output buffer, don't deliver it. port idx = %ld, miid = 0x%lx",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
#endif

      return FALSE;
   }

   uint32_t deliver_size = spl_cntr_get_ext_out_buf_deliver_size(me_ptr, ext_out_port_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "should deliver? actual len = %ld, deliver size = %ld!",
                output_buffer_size,
                deliver_size);
#endif

   return output_buffer_size >= deliver_size;
}

/**
 * Called after topo process to determine:
 * - what to listen for
 * - whether or not to deliver the output buffer
 */
static ar_result_t spl_cntr_graph_postprocessing_decision(spl_cntr_t *me_ptr, bool_t processed_audio)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result       = AR_EOK;
   uint32_t    port_index   = 0;
   bool_t      is_data_path = TRUE;

   DBG_VERIFY(result, me_ptr->cu.topo_vtbl_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Entering post gpd, processed_audio = %ld",
                processed_audio);
#endif

   if (processed_audio)
   {
      // Handling when we do process audio.

      // Check each external output port to see if it is ready to be delivered.
      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
           ext_out_port_list_ptr;
           LIST_ADVANCE(ext_out_port_list_ptr), port_index++)
      {
         spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

         if (spl_cntr_should_deliver_output_buffer(me_ptr, ext_out_port_ptr, port_index))
         {
#if 0
            if ((!(ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)) && ext_out_port_ptr->temp_out_buf_ptr)
            {
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "post gpd: dropping data and metadata in temporary buffer on output on port idx = %ld, "
                            "miid = "
                            "0x%lx "
                            "bit_mask 0x%lx!",
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                            ext_out_port_ptr->cu.bit_mask);

               // drop all the metadata except flushing eos, dfg and stream associated.
               // Remaining metadata will be propagated when output buffer triggers next time and data flow state will
               // be updated.
               spl_cntr_ext_out_drop_non_eos_dfg_metadata(me_ptr, ext_out_port_ptr);

               // adjust the offset of remaining metadata
               spl_topo_output_port_t *int_out_port_ptr =
                  (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
               bool_t SUBTRACT_FALSE = FALSE;
               gen_topo_metadata_adj_offset(&(me_ptr->topo.t_base),
                                            &int_out_port_ptr->t_base.common.media_fmt,
                                            int_out_port_ptr->md_list_ptr,
                                            (ext_out_port_ptr->topo_buf.buf_ptr->actual_data_len *
                                             ext_out_port_ptr->topo_buf.num_bufs),
                                            SUBTRACT_FALSE);
            }
            else
#endif
            {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "post gpd: delivering output on port idx = %ld, miid = 0x%lx bit_mask 0x%lx, "
                            "actual_data_len per ch %ld!",
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                            ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                            ext_out_port_ptr->cu.bit_mask,
                            (ext_out_port_ptr->topo_buf.buf_ptr ? ext_out_port_ptr->topo_buf.buf_ptr[0].actual_data_len
                                                                : 0));
#endif
            }

            // Deliver output buffer.
            TRY(result, spl_cntr_deliver_output_buffer(me_ptr, ext_out_port_ptr));
         }

         // This flag is used to force output buffer deliver. Explicitly set it to false here so any time
         // it gets set to true, that only applies to the current gpd loop.
         me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[port_index].flags.release_ext_out_buf = FALSE;

         // If this output port has a pending media fmt or if the output's frame length has changed,
         // then at this point all old data has been delivered and it's time to change the media format
         // on the external port.
         if ((spl_cntr_media_fmt_is_pending(&ext_out_port_ptr->pending_media_fmt) ||
              (TRUE == ext_out_port_ptr->cu.flags.upstream_frame_len_changed)) &&
             (!spl_cntr_ext_out_port_has_unconsumed_data(me_ptr, ext_out_port_ptr)))
         {
            TRY(result, spl_cntr_ext_out_port_apply_pending_media_fmt(me_ptr, ext_out_port_ptr, is_data_path));
         }
      }

      // For FTRT use cases, we return an exhausted input data msg after processing data.
      spl_cntr_check_and_free_all_held_input_data_msg(me_ptr);

      // Query for next iteration sample requirements from DM modules.
      // this should be done after delivering the output buffer, in case if query starts from external output port.
      TRY(result, spl_cntr_query_topo_req_samples(me_ptr));

      // Check which bits to add back to the GPD. Then we will start listening to all inputs and outputs whose bits
      // are set in the GPD.
      spl_cntr_update_gpd_and_cu_bit_mask(me_ptr);
   }
   else
   {
      // For FTRT use cases, we return an exhausted input data msg after processing data.
      spl_cntr_check_and_free_all_held_input_data_msg(me_ptr);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "End of Post gpd: gpd=0x%x optional gpd=0x%x",
                   me_ptr->gpd_mask,
                   me_ptr->gpd_optional_mask);
#endif

      // Start waiting on anything set in the gpd mask.
      spl_cntr_update_cu_bit_mask(me_ptr);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "End of Post gpd: current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);
#endif
   return result;
}

/**
 * Called in only one location, in spl_cntr_topo_process(), to setup the topo layer's external input
 * ports to point to the fwk layer's external ports.
 */
static ar_result_t spl_cntr_setup_ext_in_port_bufs(spl_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   // Setup topo external input port structs.

   uint32_t in_port_index = 0;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr), in_port_index++)
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      // Only setup ports that we are sending to the topo. Otherwise send a NULL ptr to ext_in_buf_ptr.
      if (ext_in_port_ptr->topo_buf.send_to_topo)
      {
         // Assign the internal input port's topo_buf to the one stored in the spl_cntr_ext_port.
         spl_cntr_input_port_t *in_port_ptr = (spl_cntr_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
         in_port_ptr->topo.ext_in_buf_ptr   = &ext_in_port_ptr->topo_buf;

         spl_cntr_ext_in_port_assign_timestamp(me_ptr, ext_in_port_ptr);

         // We never call process with any consumed data on the external input beforehand.
         ext_in_port_ptr->topo_buf.bytes_consumed_per_ch = 0;

         me_ptr->topo.t_base.proc_context.ext_in_port_scratch_ptr[in_port_index].prev_actual_data_len =
            ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len;

         in_port_ptr->topo.t_base.common.sdata.flags.end_of_frame = ext_in_port_ptr->topo_buf.end_of_frame;

#if SAFE_MODE
         /*
          * KA: can setup topo buf length here to required samples instead of current actual length. If all
          * samples are consumed as expected, actual data length will remain same, and buffer should be
          * appropriately adjusted.
          * For 44.1k use cases (and other all ports are fractional), sends only 44 (nominal) samples through.
          */

         if (!ext_in_port_ptr->topo_buf.end_of_frame)
         {
            // Find next process bytes based on DM logic if exists, otherwise based on nominal samples.
            uint32_t next_process_bytes = 0;
            if (ext_in_port_ptr->next_process_samples_valid)
            {
               next_process_bytes =
                  ext_in_port_ptr->next_process_samples * (ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);
            }
            else
            {
               next_process_bytes =
                  ext_in_port_ptr->nominal_samples * (ext_in_port_ptr->cu.media_fmt.pcm.bits_per_sample >> 3);
            }

#if (SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1) || defined(SPL_CNTR_DEBUG_DM)
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Entering next process samples valid next process bytes %d",
                         next_process_bytes);
#endif

            if (ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len < next_process_bytes)
            {
               // This means less than expected data was provided. For DM use cases this is unexpected behavior so print
               // a warning.
               if (ext_in_port_ptr->next_process_samples_valid)
               {
                  SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                               DBG_HIGH_PRIO,
                               "Warning: external input port idx %ld miid 0x%lx Required samples %lu, had only %lu "
                               "bytes",
                               ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                               ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                               next_process_bytes,
                               ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len);
               }
            }
            else if (ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len != next_process_bytes)
            {
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
               // set the samples the port needs for all channels
               for (uint32_t index = 0; index < ext_in_port_ptr->topo_buf.num_bufs; index++)
               {
                  ext_in_port_ptr->topo_buf.buf_ptr[index].actual_data_len = next_process_bytes;
               }
#else
               ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = next_process_bytes;
#endif
            }
         }
#endif
      }
      else
      {
         // Don't really need to do anything if send_to_topo is false, the topo will read that field
         // of the topo_buf and won't do setup for that port.
         spl_cntr_input_port_t *in_port_ptr = (spl_cntr_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
         in_port_ptr->topo.ext_in_buf_ptr   = &ext_in_port_ptr->topo_buf;
      }
   }
   return result;
}

/**
 * Called in only one location, in spl_cntr_topo_process(), to setup the topo layer's external output
 * ports to point to the fwk layer's external ports.
 *
 * In RT scenarios, we could come here without an output buffer. In that case, we need to try again
 * to pop an output buffer. If that fails, we still need to process audio, for example so capis
 * maintain proper buffering state, so we setup a temporary buffer to write to which we will later
 * drop.
 */
static ar_result_t spl_cntr_setup_ext_out_port_bufs(spl_cntr_t *me_ptr)
{
   //   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   // Setup topo external output port structs.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      spl_topo_output_port_t * out_port_ptr     = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      // If the buffer size isn't known yet, there's no need to setup this output buffer (send_to_topo could be true for
      // realtime optional output ports since ports begin as optional).
      if (ext_out_port_ptr->topo_buf.send_to_topo &&
          (0 != spl_topo_get_max_buf_len(&me_ptr->topo, &out_port_ptr->t_base.common)))
      {
         // If topo_buf buf_ptr is null, allocate temporary buffer locally and free it when output buffer node is
         // available.
         //         if (ext_out_port_ptr->is_realtime_usecase)
         //         {
         //            bool_t need_temp_buf = FALSE;
         //            if (!ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
         //            {
         //               // If we don't have an output buffer, try and pop it. If we can't, then allocate the
         //               // temporary buffer and push to there.
         //               result = spl_cntr_get_output_buffer(me_ptr, ext_out_port_ptr);
         //               if (AR_EOK != result && AR_ENEEDMORE != result)
         //               {
         //                  THROW(result, result);
         //               }
         //
         //               if (!ext_out_port_ptr->cu.out_bufmgr_node.buf_ptr)
         //               {
         //                  need_temp_buf = TRUE;
         //                  TRY(result, spl_cntr_alloc_temp_out_buf(me_ptr, ext_out_port_ptr));
         //                  TRY(result, spl_cntr_init_after_getting_out_buf(me_ptr, ext_out_port_ptr));
         //               }
         //
         //               // We got an output buffer for this external output port, so clear the bit.
         //               spl_cntr_ext_out_port_clear_gpd_mask(me_ptr, ext_out_port_ptr);
         //            }
         //
         //            if (!need_temp_buf)
         //            {
         //// We didn't need the temporary buffer. If one exists, we can deallocate it to reduce steady
         //// state memory footprint.
         //#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         //               if (ext_out_port_ptr->temp_out_buf_ptr)
         //               {
         //                  SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
         //                          DBG_HIGH_PRIO,
         //                          "Freeing unused temporary buffer on ext out port idx = %ld, miid = 0x%lx",
         //                          ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
         //                          ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id);
         //               }
         //#endif
         //               MFREE_NULLIFY(ext_out_port_ptr->temp_out_buf_ptr);
         //            }
         //         }
      }
   }

   //   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   //   {
   //   }

   return result;
}

/**
 * Remove all consumed data (this amount is known in ext_in_port_ptr->topo_buf.bytes_consumed_per_ch)
 * from the external input port local buffer by moving the start of unconsumed data to the top of the
 * buffer.
 */
static ar_result_t spl_cntr_move_ext_in_buffer_data(spl_cntr_t *                      me_ptr,
                                                    spl_cntr_ext_in_port_t *          ext_in_port_ptr,
                                                    gen_topo_ext_port_scratch_data_t *ext_in_port_scratch_ptr)
{
   ar_result_t            result          = AR_EOK;
   bool_t                 SUBTRACT_FALSE  = FALSE;
   spl_topo_input_port_t *int_in_port_ptr = (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "external input buf prev actual data length: %d",
                ext_in_port_scratch_ptr->prev_actual_data_len);
#endif

   // If all data was consumed, then we can remove it all from the local buffer by
   // setting the actual length to 0. At this point we are ready to apply any pending
   // media format since all data in the old media format was consumed.
   if (ext_in_port_ptr->topo_buf.bytes_consumed_per_ch == ext_in_port_scratch_ptr->prev_actual_data_len)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "external input buf all data was consumed.");
#endif

#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
      for (uint32_t i = 0; i < ext_in_port_ptr->topo_buf.num_bufs; i++)
      {
         ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len = 0;
      }
#else
      ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = 0;
#endif

   }
   // Not all data was consumed.
   else if (ext_in_port_ptr->topo_buf.bytes_consumed_per_ch < ext_in_port_scratch_ptr->prev_actual_data_len)
   {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "external input data consumed(per channel): %d",
                   ext_in_port_ptr->topo_buf.bytes_consumed_per_ch);
#endif
      // If some data was consumed, we need to remove that data from the local buffer by
      // shifting the remaining data to the top of the buffer and updating the actual data
      // length.
      if (ext_in_port_ptr->topo_buf.bytes_consumed_per_ch)
      {
         uint32_t actual_data_len      = 0;
         uint32_t max_data_len_per_buf = ext_in_port_ptr->topo_buf.buf_ptr[0].max_data_len;
         for (uint32_t i = 0; i < ext_in_port_ptr->topo_buf.num_bufs; i++)
         {
            // Moving the the data not consumed to the top of the buffer.
            actual_data_len = memsmove(ext_in_port_ptr->topo_buf.buf_ptr[i].data_ptr,
                                       max_data_len_per_buf,
                                       (uint8_t *)(ext_in_port_ptr->topo_buf.buf_ptr[i].data_ptr) +
                                          ext_in_port_ptr->topo_buf.bytes_consumed_per_ch,
                                       (ext_in_port_scratch_ptr->prev_actual_data_len -
                                        ext_in_port_ptr->topo_buf.bytes_consumed_per_ch));
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
            ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len = actual_data_len;
         }
#else
         }
         // optimization: update only first buffer lengths outside the for loop
         ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = actual_data_len;
#endif

         // Offsets have to move as well.
         gen_topo_metadata_adj_offset(&(me_ptr->topo.t_base),
                                      int_in_port_ptr->t_base.common.media_fmt_ptr,
                                      int_in_port_ptr->t_base.common.sdata.metadata_list_ptr,
                                      ext_in_port_ptr->topo_buf.bytes_consumed_per_ch *
                                         ext_in_port_ptr->topo_buf.num_bufs,
                                      SUBTRACT_FALSE);
      }
      // Nothing to do if no data was consumed.
      else
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Did not consume any external input data");
#endif
         // If nothing was consumed, then dm handling may have overwritten the actual_len with required samples
         // so it needs to be set to the actual amount of data that was present
#ifdef SAFE_MODE_SDATA_BUF_LENGTHS
         for (uint32_t i = 0; i < ext_in_port_ptr->topo_buf.num_bufs; i++)
         {
            ext_in_port_ptr->topo_buf.buf_ptr[i].actual_data_len = ext_in_port_scratch_ptr->prev_actual_data_len;
         }
#else
         ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len = ext_in_port_scratch_ptr->prev_actual_data_len;
#endif
      }
   }
   else
   {
      // prev actual data length < bytes_consumed_per_channel. This should never happen.
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Error: consumed more external input data (%ld bytes) than was available (%ld bytes).",
                   ext_in_port_ptr->topo_buf.bytes_consumed_per_ch,
                   ext_in_port_scratch_ptr->prev_actual_data_len);
      return AR_EFAILED;
   }

   return result;
}

/**
 * Move data in the buffer up. IF all data was consumed, send a pending media format. When moving data, also
 * update timestamps.
 */
static ar_result_t spl_cntr_adjust_ext_in_port_bufs(spl_cntr_t *me_ptr)
{
   ar_result_t result  = AR_EOK;
   bool_t is_data_path = TRUE; // This function is only called after process_audio(), so it comes from the data path.

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "checking and updating external input bufs");
#endif

   uint32_t in_port_index = 0;
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr), in_port_index++)
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      // Skip over ports we didn't send to the topo.
      if (!ext_in_port_ptr->topo_buf.send_to_topo)
      {
         continue;
      }

      // Remove the consumed data (which is of size bytes_consumed_per_ch).
      spl_cntr_move_ext_in_buffer_data(me_ptr,
                                       ext_in_port_ptr,
                                       &me_ptr->topo.t_base.proc_context.ext_in_port_scratch_ptr[in_port_index]);

      // Only adjust timestamps if data was consumed and there are valid timestamps to adjust.
      if ((0 != ext_in_port_ptr->topo_buf.bytes_consumed_per_ch) &&
          (ext_in_port_ptr->topo_buf.buf_timestamp_info.is_valid ||
           ext_in_port_ptr->topo_buf.newest_timestamp_info.is_valid))
      {
         // Adjust timestamps.
         spl_cntr_ext_in_port_adjust_timestamps(me_ptr, ext_in_port_ptr);
      }

      ext_in_port_ptr->topo_buf.bytes_consumed_per_ch = 0;

      // If all data was consumed (buffer is now empty):
      // - apply any pending media format
      // - clear timestamp discontinuity
      if (!ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len)
      {
         if (spl_cntr_media_fmt_is_pending(&ext_in_port_ptr->pending_media_fmt))
         {
            spl_cntr_ext_in_port_apply_pending_media_fmt(me_ptr, ext_in_port_ptr, is_data_path);
         }

         if (ext_in_port_ptr->topo_buf.timestamp_discontinuity)
         {
            /* Considered as data did move if process was called due to
          timestamp discontinuity even when external input buffer was empty.*/
            me_ptr->topo.proc_info.state_changed_flags.data_moved = TRUE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_MED_PRIO,
                         "Clearing timestamp discontinuity since external input port idx %ld miid %ld was emptied.",
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                         ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
#endif
            spl_cntr_ext_in_port_clear_timestamp_discontinuity(&(me_ptr->topo.t_base), &(ext_in_port_ptr->gu));
         }
      }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "external input buf actual data length after memsmove(per channel): %d",
                   ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len);
#endif
   }

   return result;
}

/*
 * Called a single time, in spl_cntr_topo_process() after calling topo_process(). Checks
 * if any output port media format events need to be handled.
 */
static ar_result_t spl_cntr_adjust_ext_out_port_bufs(spl_cntr_t *me_ptr)
{
   ar_result_t result         = AR_EOK;
   uint32_t    out_port_index = 0;

   // Check if there are any pending media formats in external output ports.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr), out_port_index++)
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      spl_topo_output_port_t * int_out_port_ptr = (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      // Skip over ports we didn't send to the topo.
      if (!ext_out_port_ptr->topo_buf.send_to_topo)
      {
         continue;
      }

      if (spl_cntr_media_fmt_is_pending(&ext_out_port_ptr->pending_media_fmt))
      {
         // Force buffer delivery in case media format changed. If media format didn't change, pending media format
         // can be ignored and is therefore invalidated.
         if (tu_has_media_format_changed(&ext_out_port_ptr->cu.media_fmt, &ext_out_port_ptr->pending_media_fmt))
         {
            me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf = TRUE;
         }
         else
         {
            spl_cntr_invalidate_pending_media_fmt(&ext_out_port_ptr->pending_media_fmt);
         }
      }

      // Force buffer delivery if flushing eos reached the external output port.
      me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf |=
         spl_topo_port_has_flushing_eos(&(me_ptr->topo), &(int_out_port_ptr->t_base.common));

      // Force buffer delivery if there is a pending data flow state.
      me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf |=
         gen_topo_md_list_has_dfg(int_out_port_ptr->md_list_ptr);

      // Force buffer delivery if there is a timestamp discontinuity.
      me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf |=
         ext_out_port_ptr->topo_buf.timestamp_discontinuity;

      me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf |=
         ext_out_port_ptr->topo_buf.end_of_frame;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
      if (me_ptr->topo.t_base.proc_context.ext_out_port_scratch_ptr[out_port_index].flags.release_ext_out_buf)
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "Setting force delivery on external output port idx %ld miid 0x%lx: flushing eos %ld, dfg: %ld, "
                      "ts_disc: %ld, eof: %ld",
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                      ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                      spl_topo_port_has_flushing_eos(&(me_ptr->topo), &(int_out_port_ptr->t_base.common)),
                      gen_topo_md_list_has_dfg(int_out_port_ptr->md_list_ptr),
                      ext_out_port_ptr->topo_buf.timestamp_discontinuity,
                      ext_out_port_ptr->topo_buf.end_of_frame);
      }
#endif

      // Clearing for next process call.
      ext_out_port_ptr->topo_buf.end_of_frame = FALSE;
   }

   return result;
}

ar_result_t spl_cntr_handle_process_events_and_flags(spl_cntr_t *me_ptr)
{
   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   INIT_EXCEPTION_HANDLING

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   // no need to reconcile in SC
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr,
                                                       &me_ptr->topo.t_base,
                                                       FALSE /*do_reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do_reconcile*/);

   if ((0 != fwk_event_flag_ptr->word) || (0 != capi_event_flag_ptr->word) || (me_ptr->topo.simpt_event_flags.word) ||
       (me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling))
   {
      TRY(result, spl_cntr_handle_fwk_events(me_ptr, TRUE /*is_data_path*/));
   }

   // if voice scenario
   if (me_ptr->cu.voice_info_ptr && me_ptr->cu.event_list_ptr)
   {
      bool_t   hw_acc_proc_delay_event = me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change;
      uint32_t topo_hw_accl_proc_delay = 0;

      if (me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change)
      {
         spl_topo_aggregate_hw_acc_proc_delay(&me_ptr->topo, FALSE, &topo_hw_accl_proc_delay);
         me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change = FALSE;
      }

      TRY(result,
          cu_check_and_raise_voice_proc_param_update_event(&me_ptr->cu,
                                                           me_ptr->topo.t_base.gu.log_id,
                                                           topo_hw_accl_proc_delay,
                                                           hw_acc_proc_delay_event));
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/*
 * Invokes the topo layer for processing modules. Sets up topo layer external buffers before invoking, and 'adjusts'
 * external buffers (moves away unconsumed data, other post-processing handling) after invocation.
 */
static ar_result_t spl_cntr_topo_process(spl_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "spl_cntr_topo_process start.");
#endif

   // if there is any data stuck in topo then some buffer will be occupied.
   // if data is stuck then trigger backward kick.
   me_ptr->topo.simpt1_flags.backwards_kick =
      ((me_ptr->topo.t_base.buf_mgr.num_used_buffers > 0) || me_ptr->topo.simpt_event_flags.check_pending_mf) ? TRUE
                                                                                                              : FALSE;

   TRY(result, spl_cntr_setup_ext_in_port_bufs(me_ptr));
   TRY(result, spl_cntr_setup_ext_out_port_bufs(me_ptr));

   if (me_ptr->is_voice_delivery_cntr)
   {
      spl_cntr_fwk_extn_voice_delivery_handle_topo_proc_notif(me_ptr);
   }

   if (spl_topo_can_use_simp_topo(&me_ptr->topo))
   {
#ifdef SPL_SIPT_DBG
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Using simp topo.");
#endif

      if (me_ptr->topo.flags.is_bypass_container)
      {
         TRY(result, simp_topo_bypass(&me_ptr->topo));
      }
      else
      {
         for (uint8_t path_index = 0; path_index < me_ptr->topo.t_base.gu.num_parallel_paths; path_index++)
         {
            if (0 == me_ptr->gpd_mask_arr[path_index])
            {
               TRY(result, simp_topo_process(&me_ptr->topo, path_index));
            }
         }
      }

      /* If backward_kick is enabled in simplified topo then it means that there is some data/md stuck in the topo,
       * we have to first move that data out of the topo before we go back to the container. If we don't handle the
       * stuck data immediately then container will insert more data from external input which will keep the
       * backward_kick true all the time.
       * Test Case: pb_sync_spl_cntr_sal_1_staggered_close_2*/
      if (me_ptr->topo.simpt1_flags.backwards_kick)
      {
         // DM modules need their expected_out_samples updated at the end of every backwards kick.
         bool_t IS_MAX_FALSE = FALSE;
         TRY(result, spl_topo_get_required_input_samples(&me_ptr->topo, IS_MAX_FALSE));

#ifdef SPL_SIPT_DBG
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Using spl topo.");
#endif
         TRY(result, spl_topo_process(&me_ptr->topo, SPL_TOPO_INVALID_PATH_INDEX));
      }
   }
   else
   {
#ifdef SPL_SIPT_DBG
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "Using spl topo. simp1 flag 0x%x, simp2 flag 0x%x, simp event flag 0x%x",
                   me_ptr->topo.simpt1_flags.word,
                   me_ptr->topo.simpt2_flags.word,
                   me_ptr->topo.simpt_event_flags.word);
#endif

      for (uint8_t path_index = 0; path_index < me_ptr->topo.t_base.gu.num_parallel_paths; path_index++)
      {
         if (0 == me_ptr->gpd_mask_arr[path_index])
         {
            TRY(result, spl_topo_process(&me_ptr->topo, path_index));
         }
      }
   }

   TRY(result, spl_cntr_adjust_ext_in_port_bufs(me_ptr));
   TRY(result, spl_cntr_adjust_ext_out_port_bufs(me_ptr));

   // Process incoming control messages
   cu_poll_and_process_ctrl_msgs(&me_ptr->cu);

   TRY(result, spl_cntr_handle_process_events_and_flags(me_ptr));

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

/**
 * Assumes that the GPD bits are already updated.
 * Checks the graph processing decision. If TRUE, then calls topo process and
 * then does post-processing tasks.
 * Post processing tasks:
 * - Check and deliver output.
 *    - If delivered, start listening to output queue.
 * - Start listening to input.
 */
ar_result_t spl_cntr_check_and_process_audio(spl_cntr_t *me_ptr, uint32_t gpd_check_mask)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result          = AR_EOK;
   bool_t      processed_audio = FALSE;
   bool_t      first_iter      = TRUE;

   // Can only come here during STARTED, so same behavior as msg from data path.
   bool_t is_data_path = TRUE;

   bool_t need_to_update_gpd_post_process = FALSE;

   bool_t is_mf_or_state_changed = FALSE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "check and process audio begin.");
#endif

   // Don't process if there are no external inputs and outputs. This could happen if the container hosts floating
   // modules.
   if ((0 == spl_cntr_num_ext_in_ports(me_ptr)) && (0 == spl_cntr_num_ext_out_ports(me_ptr)))
   {
      return FALSE;
   }

   // If there are any pending media formats at external output ports, we need to
   // send them now.
   spl_cntr_check_apply_ext_out_ports_pending_media_fmt(&(me_ptr->topo.t_base), is_data_path);

   /** Check the port
    * 1. if it is triggered.
    * 2. optional input ports.
    * 	  GPD can be true even without a trigger at optional port.
    * 	  Therefore we should give a chance for optional ports to pull the data from Q if it is available.
    * 3. optional output ports.
    * 	  GPD can be true even without a trigger at optional port.
    * 	  Therefore we should give a chance for optional ports to pull the buffer from Q if it is available
    */
   gpd_check_mask |= me_ptr->gpd_optional_mask;


   /* If trigger policy is enabled then topo-process will be called if any trigger is satisfied.
    * So before calling topo-process, we should give chance to other ports to buffer sufficient data as well if it is
    * already available but not get triggered just because it has a lower bit-mask in GPD.
    */
   if (me_ptr->topo.t_base.num_data_tpm)
   {
      gpd_check_mask |= me_ptr->gpd_mask;
   }


   //if we broke the processing loop earlier then we have to check all the gpd bits.
   if (me_ptr->topo.t_base.flags.process_pending)
   {
      gpd_check_mask                            = 0xFFFFFFFF;
      me_ptr->topo.t_base.flags.process_pending = FALSE;
   }

   uint32_t loop_count = 0;

   // Check to process audio. We might need to process audio multiple times
   // per trigger, for example if the topology frame size is smaller than the
   // framework frame size.

   /* https://orbit/CR/3004315: Virtualizer disable to enabled for mono clip playback was causing mute.
    *  Added "is_mf_or_state_changed" for while check.
    *  Virtualizer updates the media format during process because of that we disables the external input
    *  port check mask and make sure that all the stuck data is moved out before we buffer new data from external input.
    *  We have to continue the loop untill mf is moving in the topo, once it stops moving then we have to make suare
    * that we buffer data from the external input port Q.
    */
   while (spl_cntr_graph_processing_decision(me_ptr, gpd_check_mask, first_iter) || is_mf_or_state_changed)
   {
      first_iter      = FALSE;
      processed_audio = TRUE;

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "processing audio!");
#endif

      // Reset state changed flags.
      me_ptr->topo.proc_info.state_changed_flags.data_moved   = FALSE;
      me_ptr->topo.proc_info.state_changed_flags.mf_moved     = FALSE;
      me_ptr->topo.proc_info.state_changed_flags.event_raised = FALSE;

      // Process entire container graph.
      TRY(result, spl_cntr_topo_process(me_ptr));

      spl_cntr_graph_postprocessing_decision(me_ptr, processed_audio);

      // gpd is already evaluated in "spl_cntr_graph_postprocessing_decision" function
      need_to_update_gpd_post_process = FALSE;

      // Processing potentially could have changed any of the bits, so we need
      // to check them all.
      gpd_check_mask = 0xFFFFFFFF;

      // If nothing happened during this call, we can't expect it to move again during the next call.
      // We should break to avoid infinite loops.
      //
      // If the graph isn't connected, this will cause us to wait on graph_open, which itself needs to trigger
      // check_and_process audio.
      //
      // if mf or process state was changed in the previous iteration then we have to run one more iteration to consume
      // data from external input ports.
      if (!(is_mf_or_state_changed || me_ptr->topo.proc_info.state_changed_flags.data_moved ||
            me_ptr->topo.proc_info.state_changed_flags.mf_moved ||
            me_ptr->topo.proc_info.state_changed_flags.event_raised))
      {
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_2
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "Done processing audio since none of the following happened: data moved %ld, mf moved %ld, event "
                      "raised %ld",
                      me_ptr->topo.proc_info.state_changed_flags.data_moved,
                      me_ptr->topo.proc_info.state_changed_flags.mf_moved,
                      me_ptr->topo.proc_info.state_changed_flags.event_raised);
#endif
         break;
      }
      else if (me_ptr->topo.proc_info.state_changed_flags.mf_moved ||
               me_ptr->topo.proc_info.state_changed_flags.event_raised)
      {
         is_mf_or_state_changed = TRUE;

         // if media format is moved or process state is updated then there can be some data stuck in the topo, this is
         // because a module can not generate output and raise mf/state event in the same process call.

         // to process this stuck data before we consume any new data from external input, we need to assume that the
         // input trigger is satisfied.
         cu_clear_bits_in_x(&me_ptr->gpd_mask, spl_cntr_get_all_inputs_gpd_mask(me_ptr));
         cu_clear_bits_in_x(&gpd_check_mask, spl_cntr_get_all_inputs_gpd_mask(me_ptr));

         spl_cntr_update_gpd_mask_for_parallel_paths(me_ptr);

         // since we cleared the input gpd bits therefore need to evaluate it later.
         need_to_update_gpd_post_process = TRUE;
      }
      else
      {
         is_mf_or_state_changed = FALSE;
      }

      loop_count++;
      if (loop_count > 100)
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "Busy process loop alert. Breaking!");

         // mark the process pending because we are breaking the process loop
         me_ptr->topo.t_base.flags.process_pending = TRUE;
#ifdef SIM
         spf_svc_crash();
#endif
         break;
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
      // if there is a failure then check and update the gpd mask
      need_to_update_gpd_post_process = TRUE;
   }

   // input data msg should be checked for free and mask should be updated irrespective of topo_process result.
   processed_audio = FALSE;
   spl_cntr_graph_postprocessing_decision(me_ptr, processed_audio);

   // if threshold is disabled then need to make sure that the gpd is re-evaluated after processing.
   // this is because spl_cntr_graph_processing_decision clears the gpd masks for input port even if the trigger is not
   // satisfied. We need to make sure that those gpd bits are updated again.
   if (need_to_update_gpd_post_process || me_ptr->topo.simpt1_flags.threshold_disabled)
   {
      spl_cntr_update_gpd_and_cu_bit_mask(me_ptr);
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "check and process audio end.");
#endif

   return result;
}

/* function to update the gpd bit masks and then try to process the audio based on the updated masks*/
ar_result_t spl_cntr_update_gpd_and_process(spl_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result                    = AR_EOK;
   uint32_t    GPD_CHECK_MASK_EVERYTHING = 0xFFFFFFFF;

   // update the gpd masks.
   TRY(result, spl_cntr_update_gpd_and_cu_bit_mask(me_ptr));
   TRY(result, spl_cntr_check_and_process_audio(me_ptr, GPD_CHECK_MASK_EVERYTHING));

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }
   return result;
}

/*
 * Queries topo for samples needed for next process call.
 */
ar_result_t spl_cntr_query_topo_req_samples(spl_cntr_t *me_ptr)
{
   // query topo for sample requirements
   ar_result_t result = AR_EOK;

   bool_t IS_MAX_FALSE = FALSE;
#if (SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3) || defined(SPL_CNTR_DEBUG_DM)
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "DM SPL_CNTR start topo sample query");
#endif
   result = spl_topo_get_required_input_samples(&me_ptr->topo, IS_MAX_FALSE);
   if (AR_DID_FAIL(result))
   {
      // print message
      return result;
   }
   // go over ports and check for what was reported
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      spl_cntr_input_port_t * in_port_ptr     = (spl_cntr_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
      if (TRUE == in_port_ptr->topo.req_samples_info.is_updated)
      {
#if (SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1) || defined(SPL_CNTR_DEBUG_DM)
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "spl_cntr received updated input sample requirement, "
                      "port id 0x%X, module id 0x%x, prev %lu, new %lu",
                      in_port_ptr->topo.t_base.gu.cmn.id,
                      in_port_ptr->topo.t_base.gu.cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->next_process_samples,
                      in_port_ptr->topo.req_samples_info.samples_in);
#endif
         ext_in_port_ptr->next_process_samples         = in_port_ptr->topo.req_samples_info.samples_in;
         ext_in_port_ptr->next_process_samples_valid   = TRUE;
         in_port_ptr->topo.req_samples_info.is_updated = FALSE;
      }
   }
   return result;
}

ar_result_t spl_cntr_clear_topo_req_samples(gen_topo_t *topo_ptr, bool_t is_max)
{
   ar_result_t result      = AR_EOK;
   uint32_t    topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t *me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);

#if (SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1) || defined(SPL_CNTR_DEBUG_DM)
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "SPL_CNTR clearing sample requirements");
#endif

   // go over ports and reset required sample values
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      spl_cntr_input_port_t * in_port_ptr     = (spl_cntr_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

      if (is_max)
      {
         ext_in_port_ptr->max_process_samples = 0;
      }
      else
      {
         ext_in_port_ptr->next_process_samples         = 0;
         ext_in_port_ptr->next_process_samples_valid   = FALSE;
         in_port_ptr->topo.req_samples_info.samples_in = 0;
         in_port_ptr->topo.req_samples_info.is_updated = FALSE;
      }
   }
   return result;
}

// Used in the gpd: returns true if the external input port has enough data to invoke the topology.
// The input buffer should be filled up to nominal unless in case of fixed output mode, where it has to
// contain the required sample count returned by the topo.
bool_t spl_cntr_ext_in_port_has_enough_data(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_in_port_ptr;
   bool_t                  has_enough_data = FALSE;
   spl_cntr_t *            me_ptr          = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)topo_ptr);

   if (!spl_cntr_ext_in_port_local_buf_exists(ext_in_port_ptr))
   {
      return FALSE;
   }

   if (((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr)->flags.is_threshold_disabled_prop)
   {
      // if threshold is disabled for this external input port then any data is enough
      has_enough_data = (0 != ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len);
   }
   else
   {
      has_enough_data = (0 == spl_cntr_ext_in_get_free_space(me_ptr, ext_in_port_ptr));
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_MED_PRIO,
                "Checking for enough data on external input port idx = %ld, miid = 0x%lx: "
                "has enough data %lu, has %lu bytes, already consumed %lu bytes",
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                has_enough_data,
                ext_in_port_ptr->topo_buf.buf_ptr[0].actual_data_len,
                ext_in_port_ptr->topo_buf.bytes_consumed_per_ch);
#endif
   return has_enough_data;
}

void spl_cntr_update_gpd_mask_for_parallel_paths(spl_cntr_t *me_ptr)
{
   // reset gpd mask for each parallel path
   memset(&me_ptr->gpd_mask_arr[0], 0, sizeof(uint32_t) * me_ptr->cu.gu_ptr->num_parallel_paths);

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      if (ext_in_port_ptr->cu.bit_mask & me_ptr->gpd_mask)
      {
         cu_set_bits_in_x(&me_ptr->gpd_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index],
                          ext_in_port_ptr->cu.bit_mask);
      }
   }

   // Check to set bits back in the gpd.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
        ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      if (ext_out_port_ptr->cu.bit_mask & me_ptr->gpd_mask)
      {
         cu_set_bits_in_x(&me_ptr->gpd_mask_arr[ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index],
                          ext_out_port_ptr->cu.bit_mask);
      }
   }
}

bool_t spl_cntr_is_any_parallel_path_gpd_satisfied(spl_cntr_t *me_ptr)
{
   bool_t is_satisfied = FALSE;
   for (uint32_t i = 0; i < me_ptr->cu.gu_ptr->num_parallel_paths; i++)
   {
      if (0 == me_ptr->gpd_mask_arr[i])
      {
         is_satisfied = TRUE;
#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_3
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "GPD satisfied for path index %hu", i);
#endif
      }
   }
   return is_satisfied;
}
