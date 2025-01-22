/**
 * \file gen_cntr_trigger_policy_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"

bool_t gen_cntr_ext_in_port_has_flushing_eos_dfg(gen_cntr_ext_in_port_t *ext_in_port_ptr)
{
   /**
    * When EOS is stuck inside algos, marker_eos is set even though md_list doesn't have eos.
    * When ext_in_port_ptr->md_list_ptr has EOS, marker_eos may not be set yet.
    */
   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // If md lib is compiled in nlpi, no need to exit island before calling gen_topo_md_list_has_flushing_eos_or_dfg
   // since we should be in nlpi by this point, due to exiting before propagating metadata
   return (
      in_port_ptr->common.sdata.flags.marker_eos ||
      ((ext_in_port_ptr->md_list_ptr) && (gen_topo_md_list_has_flushing_eos_or_dfg(ext_in_port_ptr->md_list_ptr))));
}

gen_topo_data_need_t gen_cntr_ext_in_port_needs_data_buffer(gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   gen_topo_data_need_t rc = GEN_TOPO_DATA_NOT_NEEDED;

   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)gu_ext_in_port_ptr;

   gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
   gen_topo_t *           topo_ptr    = ((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr)->topo_ptr;

   // for taking care of TRM optimization: moved this to top. when TRM blocks we will stop calling topo-process
   // whether or not ext-in buf is available.
   rc = gen_topo_in_port_needs_data(topo_ptr, in_port_ptr);
   if (GEN_TOPO_DATA_BLOCKED == rc)
   {
      return rc;
   }

   // When media format is held, we shouldn't wait for input queue, hence checking input_data_q_msg.payload_ptr
   if (gen_cntr_ext_in_port_has_data_buffer(gu_ext_in_port_ptr) || (ext_in_port_ptr->cu.input_data_q_msg.payload_ptr))
   {
      return GEN_TOPO_DATA_NOT_NEEDED;
   }

   // if there's flushing EOS, then data is needed optionally. If more input comes, then flushing EOS can be changed to
   // non-flushing.
   if (gen_cntr_ext_in_port_has_flushing_eos_dfg(ext_in_port_ptr))
   {
      return GEN_TOPO_DATA_NEEDED_OPTIONALLY;
   }

   return rc;
}

static bool_t gen_cntr_check_if_any_connected_src_module_path_drained(gen_topo_output_port_t *out_port_ptr)
{
   /**
    * if a source is connected to ext out port and is disabled, and also if all the nblcs between src and ext out are
    * at_gap, then we shouldn't wait for output. E.g. DTMF gen module usecase. note that if EOS is stuck between dtmf
    * and ext out then data flow will not be at gap in the intermediate nblc chain and we return False.
    *
    * Doing so causes us to hold output and later on when module gets enabled, there's no trigger.
    * If src is in the middle (not directly connected) to ext porwt, then this check not necessary.
    * If both ext-in and source are feeding to output, anyway nblc wouldn't lead beyond the MIMO & processing would
    * be possible for ext-in.
    *
    */
   if (TOPO_DATA_FLOW_STATE_FLOWING == out_port_ptr->common.data_flow_state)
   {
#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(0,
                   DBG_LOW_PRIO,
                   "Module 0x%x output data flow state is Flowing. Returning False. ",
                   out_port_ptr->gu.cmn.module_ptr->module_instance_id);
#endif
      return FALSE;
   }

   if (out_port_ptr->nblc_start_ptr)
   {
      gen_topo_module_t *nblc_start_module_ptr = (gen_topo_module_t *)out_port_ptr->nblc_start_ptr->gu.cmn.module_ptr;
      if (nblc_start_module_ptr->gu.flags.is_source && nblc_start_module_ptr->flags.disabled)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(0,
                      DBG_LOW_PRIO,
                      "Source Module 0x%x is disabled. Returning True.",
                      nblc_start_module_ptr->gu.module_instance_id);
#endif
         return TRUE;
      }
      else
      {
         // if the nblc is single input, then go to prev nblc and see if it originates in a source module.
         if ((nblc_start_module_ptr->gu.max_input_ports == 1) && (nblc_start_module_ptr->gu.num_input_ports == 1))
         {
            gen_topo_output_port_t *prev_nblc_out_ptr =
               (gen_topo_output_port_t *)nblc_start_module_ptr->gu.input_port_list_ptr->ip_port_ptr->conn_out_port_ptr;

            if (prev_nblc_out_ptr)
            {
               // recursively search upstream if there are is a disabled src module.
               return gen_cntr_check_if_any_connected_src_module_path_drained(prev_nblc_out_ptr);
            }
         }
      }
   }
   return FALSE;
}

static gen_topo_port_trigger_need_t gen_cntr_ext_out_needs_to_be_waited_on(gen_cntr_t *             me_ptr,
                                                                           gen_cntr_ext_out_port_t *ext_out_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
   gen_topo_module_t *     module_ptr   = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   topo_port_state_t       ds_downgraded_state =
      cu_get_external_output_ds_downgraded_port_state(&ext_out_port_ptr->cu);
   /**
    * First check all the blocked conditions. which are as followings.
    * 1. do not wait for the external ports which are not started.
    * 2. do not wait if buffer is already present
    * 3. do not wait if non-trigger policy is blocked for the external port or for the nblc port.
    * 4. do not wait if connected source module is disabled.
    *
    * Now check for optional/mandaotry trigger.
    * 1. if nblc start port is marked non-trigger optional then return optional.
    * 2. if self port is marked non-trigger invalid then return mandatory otherwise optional.
    *
    */

   // we have to check both port-state and downstream-downgraded state.
   // These can be different in case if module at the external output port is blocking the state propagation.
   if ((TOPO_PORT_STATE_STARTED != out_port_ptr->common.state) || (TOPO_PORT_STATE_STARTED != ds_downgraded_state))
   {
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   // Data is not needed if data trigger is not allowed.
   if (!gen_topo_is_module_data_trigger_allowed(module_ptr))
   {
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   /**
    * if we already have a buffer or if port is non-triggerable blocked, then don't wait
    */
   fwk_extn_port_nontrigger_policy_t ntp =
      gen_topo_get_nontrigger_policy_for_output(out_port_ptr, GEN_TOPO_DATA_TRIGGER);

   if ((gen_cntr_ext_out_port_has_buffer(&ext_out_port_ptr->gu)) ||
       (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == ntp))
   {
      return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
   }

   if (out_port_ptr->nblc_start_ptr)
   {
      gen_topo_module_t *nblc_module_ptr = (gen_topo_module_t *)out_port_ptr->nblc_start_ptr->gu.cmn.module_ptr;

      // Data is not needed if data trigger is not allowed.
      if (!gen_topo_is_module_data_trigger_allowed(nblc_module_ptr))
      {
         return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
      }

      fwk_extn_port_nontrigger_policy_t nblc_start_ntp =
         gen_topo_get_nontrigger_policy_for_output(out_port_ptr->nblc_start_ptr, GEN_TOPO_DATA_TRIGGER);

      if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == nblc_start_ntp)
      {
         return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
      }

      /**
       * we shouldn't wait for output,
       *   1. if a source is connected to ext out port and is disabled
       *   2. if all the ports are at_gap in between the disabled src and ext output,
       *  E.g. DTMF gen module when disabled.
       * Doing so causes us to hold output and later on when module gets enabled, there's no trigger.
       * If src is in the middle (not directly connected) to ext port, then this check not necessary.
       * If both ext-in and source are feeding to output, anyway nblc wouldn't lead beyond the MIMO & processing would
       * be possible for ext-in.
       *
       */
      if (me_ptr->topo.flags.is_src_module_present &&
          gen_cntr_check_if_any_connected_src_module_path_drained(out_port_ptr))
      {
         return GEN_TOPO_PORT_TRIGGER_NOT_NEEDED;
      }

      if (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == nblc_start_ntp)
      {
         return GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY;
      }
   }

   return (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp) ? GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY
                                                      : GEN_TOPO_PORT_TRIGGER_NEEDED;
}

/**
 * func to be called from cmd processing sequence.
 */
void gen_cntr_wait_for_trigger(gen_cntr_t *me_ptr)
{
   gen_cntr_wait_for_any_ext_trigger(me_ptr, FALSE /*not process context*/, FALSE /*is_entry*/);
}

bool_t gen_cntr_is_any_path_ready_to_process_util_(gen_cntr_t *me_ptr)
{
   bool_t is_satisfied = FALSE;
   for (uint32_t i = 0; i < me_ptr->cu.gu_ptr->num_parallel_paths; i++)
   {
      if (0 == me_ptr->wait_mask_arr[i])
      {
         is_satisfied = TRUE;
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "processing conditions satisfied for path index %hu", i);
#else
         break;
#endif
      }
   }
   return is_satisfied;
}

/**
 * timer/signal trigger is always being waited-on.
 *
 * is_first_start - for the first time start on the container, we can wait only for input.
 */
bool_t gen_cntr_wait_for_any_ext_trigger(gen_cntr_t *me_ptr, bool_t called_from_process_context, bool_t is_entry)
{
   uint32_t in_wait_mask       = 0;
   uint32_t out_wait_mask      = 0;
   uint32_t stop_mask          = 0;
   uint32_t wait_mask          = 0;
   uint32_t optional_wait_mask = 0;
   /** If EOS that came to an ext in port has not departed container yet, then in-port is optional.
    *  In case input starts quickly (e.g. after data flow gap), then this helps in listening to input quicker
    *  (as opposed to listening after EOS goes out). */
   bool_t WAIT_FOR_TRIGGER    = TRUE;
   bool_t CONTINUE_PROCESSING = FALSE;

   gen_topo_process_info_t *process_info_ptr  = &me_ptr->topo.proc_context.process_info;
   process_info_ptr->probing_for_tpm_activity = FALSE;

   //reset the wait mask array for each parallel path in topo.
   memset(me_ptr->wait_mask_arr, 0 , sizeof(uint32_t)*me_ptr->cu.gu_ptr->num_parallel_paths);

   /**
    * if no trigger policy module, then wait for both input and output before proceeding ahead (except if signal
    * triggered). But if there's trigger policy then we should continue with trigger and not wait for all triggers.
    * start/set-cfg etc have is_entry=FALSE, because we need to wait for some signals at the start.
    */
   if (me_ptr->topo.num_data_tpm)
   {
      if(is_entry)
      {
         return CONTINUE_PROCESSING;
      }
   }
   else // if not data triggered
   {
      /**
       * if there's no trigger policy module and container is signal triggered then don't wait at entry, but wait at exit.
      */
      if(/*me_ptr->topo.flags.is_signal_triggered && */ me_ptr->topo.flags.is_signal_triggered_active)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                     DBG_LOW_PRIO,
                     "Signal triggered container with no data trigger policy module: waiting for signal? %u",
                     !is_entry);
#endif
         if (is_entry)
         {
            return CONTINUE_PROCESSING;
         }
         else
         {
            // When transitioning from is_signal_trigger_active=False to True, we might have set some wait-masks. We
            // need to clear them now.
            // this check is only for optimization purpose.
            if (GEN_TOPO_SIGNAL_TRIGGER != me_ptr->topo.proc_context.curr_trigger)
            {
               stop_mask |= gen_cntr_get_all_output_port_mask(me_ptr);
               stop_mask |= gen_cntr_get_all_input_port_mask(me_ptr);
               cu_stop_listen_to_mask(&me_ptr->cu, stop_mask); // stop listening to output
            }

#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG_ISLAND(me_ptr->topo.gu.log_id,
                                DBG_HIGH_PRIO,
                                "stop_mask %d curr_chan_mask:%x ",
                                stop_mask,
                                me_ptr->cu.curr_chan_mask);
#endif
            return WAIT_FOR_TRIGGER; // listen to only signal trigger
         }
      }
      else if (me_ptr->topo.flags.is_sync_module_present)
      {
         // first ask SYNC module to update the external input port trigger condition ("ready_to_go").
         gen_cntr_fwk_ext_sync_update_ext_in_tgp(me_ptr);
      }
   }

   /**
    * go through topo again to see which ext inputs are needed.
    * module like SPR uses this to drop data.
    *
    * If nothing has changed, no need to probe further.
    *
    * if we don't set trigger as GEN_TOPO_BUF_TRIGGER, then underrun will be introduced incorrectly.
    *
    * probing_for_tpm_activity - is only an optimization to avoid extra looping due to anything changed==true.
    * this flag is used to check if the data tpm modules at the NBLC ends of the external ports are ready to process or
    * not.
    *
    * If there are parallel paths,  "probing_for_tpm_activity" optimization doesnt work, so avoid all probing activity
    * implementation. There is a possibility that tpm module is in one path but in second path there is no tpm module.
    * We should not block the processing on second path if first path's tpm is not ready.
    */
   if (!is_entry && me_ptr->topo.num_data_tpm && process_info_ptr->anything_changed)
   {
#ifdef VERBOSE_DEBUGGING
      // curr_trigger needs to be set b4 calling gen_cntr_ext_in_port_needs_data_buffer
      if (GEN_TOPO_SIGNAL_TRIGGER == me_ptr->topo.proc_context.curr_trigger)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "After processing of signal trigger, changed trigger to data trigger");
      }
#endif
      // At the end of Signal trigger processing, we must be transition to data trigger before
      // continuing to update mask/continue processing data tpm triggers. Else we can end up in infinite loop,
      // because calling topo process with curr_trigger=SIGNAL_TRIGGER can cause underruns at ext inputs.
      // And anything_changed will remain TRUE due to unintended underruns.
      me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_DATA_TRIGGER;

      if (1 == me_ptr->cu.gu_ptr->num_parallel_paths)
      {
         process_info_ptr->probing_for_tpm_activity = TRUE;
      }
   }

   bool_t   atleast_one_inp_data_tpm_present = FALSE;
   uint32_t num_ext_in_tpm_ready_to_process  = 0; // see below for explanation
   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      bool_t                  is_input_data_tpm     = FALSE;

      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      // probe ext input only if input triggered tpm module is present
      if (process_info_ptr->probing_for_tpm_activity)
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;
         if (in_port_ptr->nblc_end_ptr)
         {
            gen_topo_module_t *nblc_end_module_ptr = (gen_topo_module_t *)in_port_ptr->nblc_end_ptr->gu.cmn.module_ptr;
            is_input_data_tpm = gen_topo_is_module_data_trigger_policy_active(nblc_end_module_ptr) &&
                                (!me_ptr->topo.flags.is_signal_triggered_active ||
                                 (me_ptr->topo.flags.is_signal_triggered_active &&
                                  nblc_end_module_ptr->flags.needs_input_data_trigger_in_st));
            if (is_input_data_tpm)
            {
               atleast_one_inp_data_tpm_present = TRUE;
            }
         }
      }

      /**
       * need to wait for only started ports
       * also only if non-trigger policy is not optional or blocked.
       * only if they don't already have a buf & port needs data. Also don't wait if there's EOS.
       *
       * payload_ptr is checked because when MF is there at input we shouldn't wait for more input until MF is first
       * handled.
       *
       * in below call data_trigger is used.
       */
      gen_topo_data_need_t data_need = (ext_in_port_ptr->flags.ready_to_go)
                                          ? GEN_TOPO_DATA_NOT_NEEDED
                                          : gen_cntr_ext_in_port_needs_data_buffer(&ext_in_port_ptr->gu);

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "wait_for_trig: Ext in (0x%lx, 0x%lx) bit_mask: 0x%08lx needs_trigger:%lu is_input_data_tpm:%lu",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                   ext_in_port_ptr->cu.bit_mask,
                   data_need,
                   is_input_data_tpm);
#endif

      if (GEN_TOPO_DATA_NOT_NEEDED == data_need)
      {
         stop_mask |= ext_in_port_ptr->cu.bit_mask;
         if (is_input_data_tpm)
         {
            num_ext_in_tpm_ready_to_process++;
         }
      }
      else if (GEN_TOPO_DATA_BLOCKED == data_need)
      {
         stop_mask |= ext_in_port_ptr->cu.bit_mask;
      }
      else if (GEN_TOPO_DATA_NEEDED == data_need)
      {
         in_wait_mask |= ext_in_port_ptr->cu.bit_mask;

         //add the external input port's wait mask to the wait-mask-array.
         me_ptr->wait_mask_arr[ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index] |=
            ext_in_port_ptr->cu.bit_mask;
      }
      else
      {
         optional_wait_mask |= ext_in_port_ptr->cu.bit_mask;
         if (is_input_data_tpm)
         {
            num_ext_in_tpm_ready_to_process++;
         }
      }
   }

   /**
    * If all data trigger policy modules that we have are connected to ext-in and they all need data, then no point
    * continuing processing. Further, if all of them are blocked (TRM) also, no need of processing.
    * Otherwise, we must probe and see. maybe tpm module wants to output something.
    * Applicable for signal triggered container.
    */
   bool_t force_wait_for_trigger = FALSE;
   // probe ext input only if input triggered tpm module is present
   if (atleast_one_inp_data_tpm_present && process_info_ptr->probing_for_tpm_activity)
   {
      if (0 == num_ext_in_tpm_ready_to_process)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "All data trigger policy modules that we have are "
                      "connected to ext-in and either need data to continue processing or are blocked. Waiting for "
                      "trigger.");
#endif
         force_wait_for_trigger = TRUE;
         //return WAIT_FOR_TRIGGER; after setting masks. Even if mask is zero, wait.
      }
      else
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Not all data trigger policy modules that we have are "
                      "connected to ext-in or either they have data to process. Continuing processing");
#endif
       return CONTINUE_PROCESSING;
      }
   }

   bool_t   atleast_one_out_data_tpm_present      = FALSE;
   uint32_t num_ext_out_tpm_ready_to_process_data = 0; // see below for explanation
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gen_topo_output_port_t * out_port_ptr     = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      bool_t is_output_data_tpm = FALSE;
      // probe ext output only if output triggered tpm module is present
      if (process_info_ptr->probing_for_tpm_activity)
      {
         if (out_port_ptr->nblc_start_ptr)
         {
            gen_topo_module_t *nblc_start_module_ptr =
               (gen_topo_module_t *)out_port_ptr->nblc_start_ptr->gu.cmn.module_ptr;
            is_output_data_tpm = gen_topo_is_module_data_trigger_policy_active(nblc_start_module_ptr) &&
                                 (!me_ptr->topo.flags.is_signal_triggered_active ||
                                  (me_ptr->topo.flags.is_signal_triggered_active &&
                                   nblc_start_module_ptr->flags.needs_output_data_trigger_in_st));
            if (is_output_data_tpm)
            {
               atleast_one_out_data_tpm_present = TRUE;
            }
         }
      }

      gen_topo_port_trigger_need_t trigger_need = gen_cntr_ext_out_needs_to_be_waited_on(me_ptr, ext_out_port_ptr);

      if (GEN_TOPO_PORT_TRIGGER_NEEDED == trigger_need)
      {
         out_wait_mask |= ext_out_port_ptr->cu.bit_mask;

         // add the external output port's wait mask to the wait-mask-array.
         me_ptr->wait_mask_arr[ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->path_index] |=
            ext_out_port_ptr->cu.bit_mask;
      }
      else if (GEN_TOPO_PORT_TRIGGER_NEEDED_OPTIONALLY == trigger_need)
      {
         optional_wait_mask |= ext_out_port_ptr->cu.bit_mask;
         if(is_output_data_tpm)
         {
            num_ext_out_tpm_ready_to_process_data++;
         }
      }
      else // GEN_TOPO_PORT_TRIGGER_NOT_NEEDED
      {
         // check if data is not needed since the nblc start port is blocked. If so
         // output is not ready to process
         if (is_output_data_tpm)
         {
            fwk_extn_port_nontrigger_policy_t nblc_start_ntp =
               gen_topo_get_nontrigger_policy_for_output(out_port_ptr->nblc_start_ptr, GEN_TOPO_DATA_TRIGGER);
            if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED != nblc_start_ntp)
            {
               num_ext_out_tpm_ready_to_process_data++;
            }
         }

         stop_mask |= ext_out_port_ptr->cu.bit_mask;
      }

#ifdef VERBOSE_DEBUGGING
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "wait_for_trig: Ext out (0x%lx, 0x%lx) bit_mask 0x%08lx needs_trigger:%lu is_output_data_tpm:%lu num_ext_rdy:%lu",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.id,
                   ext_out_port_ptr->cu.bit_mask,
                   trigger_need,
                   is_output_data_tpm,
                   num_ext_out_tpm_ready_to_process_data);
#endif
   }

   /**
    * If all data trigger policy modules that we have are connected to ext-out and they all need buffers, then no point
    * continuing processing. Further, if all of them are blocked (DAM) also, no need of processing.
    * Otherwise, we must probe and see. maybe tpm module wants to output something.
    * Applicable for signal triggered container.
    */
   if (atleast_one_out_data_tpm_present && process_info_ptr->probing_for_tpm_activity)
   {
      if (0 == num_ext_out_tpm_ready_to_process_data)
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "All data trigger policy modules that we have are "
                      "connected to ext-out either need buffers to continue or are blocked. Waiting for "
                      "trigger.");
#endif
         force_wait_for_trigger = TRUE;
         //return WAIT_FOR_TRIGGER; after setting masks. Even if mask is zero, wait.
      }
      else
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "Some modules connected to ext-out are ready to process. Continuing processing");
#endif
        return CONTINUE_PROCESSING;
      }
   }

   /*
    * Note: for DAM module (Trigger policy module) we need to wait for input and output at the same time.
    */
   if (0 == me_ptr->topo.num_data_tpm)
   {
      wait_mask = 0;
      for (uint8_t i = 0; i < me_ptr->cu.gu_ptr->num_parallel_paths; i++)
      {
         uint32_t in_wait_mask_for_this_path  = in_wait_mask & me_ptr->wait_mask_arr[i];
         uint32_t out_wait_mask_for_this_path = out_wait_mask & me_ptr->wait_mask_arr[i];

         // Earlier we were waiting on the input first, this caused buffering at external input when output
         // in not present and results in additional path delay in playback paths (on measurement). Waiting on
         // outputs first removes this additional input buffering delay.
         wait_mask |= out_wait_mask_for_this_path ? out_wait_mask_for_this_path : in_wait_mask_for_this_path;

#ifdef VERBOSE_DEBUGGING
   // per path triggering debug
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "wait_for_trigger for path[%ld/%d]: wait_mask0x%08lX, in_wait_mask%08lX, out_wait_mask%08lX, ",
                i+1,
                me_ptr->cu.gu_ptr->num_parallel_paths,
                wait_mask,
                in_wait_mask_for_this_path,
                out_wait_mask_for_this_path);
#endif
      }
   }
   else
   {
      wait_mask = in_wait_mask | out_wait_mask;

#ifdef VERBOSE_DEBUGGING
   // per path triggering debug
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "wait_for_trigger overall wait_mask0x%08lX, in_wait_mask%08lX, out_wait_mask%08lX, ",
                wait_mask,
                in_wait_mask,
                out_wait_mask);
#endif
   }

   cu_start_listen_to_mask(&me_ptr->cu, (wait_mask | optional_wait_mask));
   cu_stop_listen_to_mask(&me_ptr->cu, stop_mask);

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "wait_for_trigger is_entry%u, wait_mask0x%08lX, opt_wait_mask0x%08lX, stop_mask0x%08lX, "
                "curr_chan_mask0x%08lX",
                is_entry,
                wait_mask,
                optional_wait_mask,
                stop_mask,
                me_ptr->cu.curr_chan_mask);
#endif

   if (force_wait_for_trigger)
   {
      return WAIT_FOR_TRIGGER;
   }

   /**
    * When called from process context,
    * if nothing changed at exit, exit process loop (this needs to be after cu_stop_listen_to_mask, or otherwise, same
    * trigger occurs multiple times
    */
   if (called_from_process_context && !is_entry)
   {
      if (process_info_ptr->anything_changed)
      {
         // When TPM is present as long as something changes we need to continue processing.
         // E.g. DAM module output stall while input is still not fully consumed.
         // We exit inner loop to deliver output and after this wait_mask is set for output.
         // We cannot exit processing because output may stall and input side will overrun sooner or later.
         if (me_ptr->topo.num_data_tpm)
         {
#ifdef VERBOSE_DEBUGGING
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Continuing processing");
#endif
            // reset the wait mask array for each parallel path in topo so that each module gets a chance to process.
            memset(me_ptr->wait_mask_arr, 0, sizeof(uint32_t) * me_ptr->cu.gu_ptr->num_parallel_paths);
            return CONTINUE_PROCESSING;
         }
      }
      else
      {
#ifdef VERBOSE_DEBUGGING
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Nothing changed. exiting process loop");
#endif
         return WAIT_FOR_TRIGGER; // if we don't return wait-for-trigger, it will cause infinite loop.
      }
   }

   return (0 == wait_mask) ? CONTINUE_PROCESSING
                           : (gen_cntr_is_any_path_ready_to_process(me_ptr) ? CONTINUE_PROCESSING : WAIT_FOR_TRIGGER);
}
