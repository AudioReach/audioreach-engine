/**
 * \file gen_topo_trigger_policy_island.c
 *
 * \brief
 *
 *     trigger policy related implementation
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"

static bool_t gen_topo_is_signal_trigger_satisfied_for_default_policy_module(gen_topo_module_t *module_ptr);

static bool_t gen_topo_is_signal_trigger_satisfied_for_trigger_policy_module(gen_topo_module_t *module_ptr);

static bool_t gen_topo_is_data_trigger_satisfied_for_default_policy_module(gen_topo_module_t *module_ptr,
                                                                           bool_t *is_ext_trigger_not_satisfied_ptr,
                                                                           gen_topo_process_context_t *proc_ctxt_ptr);

static bool_t gen_topo_is_data_trigger_satisfied_for_trigger_policy_module(gen_topo_module_t *module_ptr,
                                                                           bool_t *is_ext_trigger_not_satisfied_ptr,
                                                                           gen_topo_process_context_t *proc_ctxt_ptr);

static bool_t gen_topo_output_has_empty_buffer(gen_topo_output_port_t *out_port_ptr)
{
   return (NULL != out_port_ptr->common.bufs_ptr[0].data_ptr) &&
          (0 == out_port_ptr->common.bufs_ptr[0].actual_data_len);
}

/**
 * Returns wheter the module's subgraph is started
 *
 * difference between gen_topo_is_module_sg_stopped & gen_topo_is_module_sg_started:
 * 1. if sg is not stopped/suspended, then it can be started/prepared.
 * 2. if sg is not started, then it can be stopped/prepared/suspended.
 * 3. in capiv2 callbacks, !is_module_sg_stopped is more relevant as events need to be handled
 *    as long as sg is in started/prepared states
 * 4. some set-cfg can be done only in !started states. for those scenarios !is_module_sg_started is relevant.
 * 5. process can be called only if module is in started states.
 */
bool_t gen_topo_is_module_sg_started(gen_topo_module_t *module_ptr)
{
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)module_ptr->gu.sg_ptr;
   return (TOPO_SG_STATE_STARTED == sg_ptr->state);
}

/**
 * Returns whether the module's subgraph is stopped
 * This can be used during events to defer handling for stopped subgraphs to a later point (prepare/run)
 *
 * see comments above gen_topo_is_module_sg_stopped
 */

bool_t gen_topo_is_module_sg_stopped(gen_topo_module_t *module_ptr)
{
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)module_ptr->gu.sg_ptr;
   return (TOPO_SG_STATE_STOPPED == sg_ptr->state);
}

/**
 * Returns whether the module's subgraph is stopped/suspended
 * This can be used during events to defer handling for stopped/suspended subgraphs to a later point (prepare/run)
 *
 * see comments above gen_topo_is_module_sg_stopped_or_suspended
 */

bool_t gen_topo_is_module_sg_stopped_or_suspended(gen_topo_module_t *module_ptr)
{
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)module_ptr->gu.sg_ptr;
   return ((TOPO_SG_STATE_STOPPED == sg_ptr->state) || (TOPO_SG_STATE_SUSPENDED == sg_ptr->state));
}

/*
 * Returns True if module has an active data trigger policy.
 * in ST containers, data trigger is active only if module also raised FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR.
 */
bool_t gen_topo_is_module_data_trigger_policy_active(gen_topo_module_t *module_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;
   bool_t      has_module_raised_trigger_policy =
      (NULL != module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_DATA_TRIGGER)]);

   if (has_module_raised_trigger_policy)
   {
      if (/*topo_ptr->flags.is_signal_triggered && */ topo_ptr->flags.is_signal_triggered_active)
      {
         // Data triggers are not allowed in Signal Triggered container unless module raises
         // #FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR event or the event is propagated to this module from another
         // module in container.
         return gen_topo_is_module_need_data_trigger_in_st_cntr(module_ptr);
      }
      else
      {
         return TRUE;
      }
   }

   return FALSE;
}

/**
 * checks for threshold also for requires_data_buf modules.
 */
static bool_t gen_topo_input_has_sufficient_data(gen_topo_input_port_t *in_port_ptr)
{
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

#ifdef VERBOSE_DEBUGGING
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;
#endif

   // For signal trigger policy, the input port criteria is handled via
   // gen_topo_is_module_signal_trigger_condition_satisfied

   // For metadata alone cases, gen_topo_check_copy_between_modules or gen_cntr_setup_internal_input_port_and_preprocess
   // ensures modules have data_ptr as not NULL
   if (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      return FALSE;
   }

   bool_t has_sufficient_data = TRUE;

   /* For SYNC module.
    * When threshold is enabled then full buffer (container frame len) is trigger-satisfied.
    * When threshold is disabled then any data is trigger satisfied.
    */
   bool_t is_threshold_required   = (in_port_ptr->common.flags.port_has_threshold || module_ptr->flags.need_sync_extn);
   bool_t is_fixed_frame_required = gen_topo_does_module_requires_fixed_frame_len(module_ptr);

   if (is_fixed_frame_required && is_threshold_required)
   {
      /** call process for ports that don't require data buffering but have threshold, only if thresh is met.
       * if any thresh is not met, then don't call process */
      if (in_port_ptr->common.bufs_ptr[0].actual_data_len != in_port_ptr->common.bufs_ptr[0].max_data_len)
      {
         // threshold_not_met flag is applicable to only non-buffering modules & non-HW-EP modules.
         // Also this flag is not to be used during force-process (MF/EOS/EOF/discontinuity etc)
         //  At EOS, call process and let the module decide whether it needs to drop data or not.
         //  Data output by the module at EoS is used by the fmwk even if the thresh was not met.
         //  marker EOS can be set, if eos is stuck inside module. In that case call the module process
         // until module propagates eos.
         //
         // Modules can process erasure buffers without data, in that case inputs need not have threshold
         // amount of data.
         if (!in_port_ptr->common.sdata.flags.end_of_frame && !in_port_ptr->common.sdata.flags.erasure &&
             !in_port_ptr->common.sdata.flags.marker_eos)
         {
            has_sufficient_data = FALSE;
            // for fixed threshold modules, when module is not called bytes_from_prev_buf is not updated in
            // topo_process. rve_rx_tx_noise.
            in_port_ptr->bytes_from_prev_buf   = in_port_ptr->common.bufs_ptr[0].actual_data_len;
            in_port_ptr->flags.need_more_input = TRUE;
#ifdef VERBOSE_DEBUGGING
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     " Module 0x%lX: Input port_id 0x%lx, threshold not met (%lu of %lu), bytes_from_prev_buf %lu",
                     module_ptr->gu.module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     in_port_ptr->common.bufs_ptr[0].actual_data_len,
                     in_port_ptr->common.bufs_ptr[0].max_data_len,
                     in_port_ptr->bytes_from_prev_buf);
#endif
            // if at least one port has thresh not met, then call don't process
         }
      }
   }

   return has_sufficient_data;
}

// here check only for buffer, buffer itself is conditionally assigned at other places.
bool_t gen_topo_input_port_is_trigger_present(void *  ctx_topo_ptr,
                                              void *  ctx_in_port_ptr,
                                              bool_t *is_ext_trigger_not_satisfied_ptr)
{
   gen_topo_t *           topo_ptr           = (gen_topo_t *)ctx_topo_ptr;
   gen_topo_input_port_t *in_port_ptr        = (gen_topo_input_port_t *)ctx_in_port_ptr;
   bool_t                 trigger_is_present = TRUE;

   // here check only for buffer, buffer itself is conditionally assigned at other places.
   if (!gen_topo_input_has_sufficient_data(in_port_ptr))
   {
      trigger_is_present = FALSE;

      if (is_ext_trigger_not_satisfied_ptr)
      {
         *is_ext_trigger_not_satisfied_ptr =
            (in_port_ptr->gu.ext_in_port_ptr &&
             !topo_ptr->topo_to_cntr_vtable_ptr->ext_in_port_has_data_buffer(in_port_ptr->gu.ext_in_port_ptr));
      }
   }

   return trigger_is_present;
}

// here check only for buffer, buffer itself is conditionally assigned at other places.
// Trigger is not present if a stale output is present
bool_t gen_topo_output_port_is_trigger_present(void *  ctx_topo_ptr,
                                               void *  ctx_out_port_ptr,
                                               bool_t *is_ext_trigger_not_satisfied_ptr)
{
   gen_topo_t *            topo_ptr           = (gen_topo_t *)ctx_topo_ptr;
   gen_topo_output_port_t *out_port_ptr       = (gen_topo_output_port_t *)ctx_out_port_ptr;
   bool_t                  trigger_is_present = TRUE;

   if (!gen_topo_output_has_empty_buffer(out_port_ptr))
   {
      trigger_is_present = FALSE;

      if (is_ext_trigger_not_satisfied_ptr)
      {
         *is_ext_trigger_not_satisfied_ptr =
            (out_port_ptr->gu.ext_out_port_ptr &&
             !topo_ptr->topo_to_cntr_vtable_ptr->ext_out_port_has_buffer(out_port_ptr->gu.ext_out_port_ptr));
      }
   }
   return trigger_is_present;
}

bool_t gen_topo_output_port_is_trigger_absent(void *ctx_topo_ptr, void *ctx_out_port_ptr)
{
   gen_topo_output_port_t *out_port_ptr      = (gen_topo_output_port_t *)ctx_out_port_ptr;
   bool_t                  trigger_is_absent = TRUE;

   if (gen_topo_output_has_empty_buffer(out_port_ptr))
   {
      trigger_is_absent = FALSE;
   }

   return trigger_is_absent;
}

fwk_extn_port_trigger_policy_t gen_topo_get_port_trigger_policy(gen_topo_module_t *module_ptr,
                                                                gen_topo_trigger_t curr_trigger)
{
   gen_topo_trigger_policy_t *tp_ptr = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];
   if (tp_ptr)
   {
      return tp_ptr->port_trigger_policy;
   }

   return FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY;
}

static fwk_extn_port_trigger_affinity_t gen_topo_get_port_affinity_to_group_for_input(
   gen_topo_input_port_t *in_port_ptr,
   uint32_t               group_num,
   gen_topo_trigger_t     curr_trigger)
{
   gen_topo_module_t *        module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
   gen_topo_trigger_policy_t *tp_ptr     = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];

   if (tp_ptr)
   {
      return tp_ptr->trigger_groups_ptr[group_num].in_port_grp_affinity_ptr[in_port_ptr->gu.cmn.index];
   }
   // Assume PRESENT by default (if module doesn't specify)
   return FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
}

static fwk_extn_port_trigger_affinity_t gen_topo_get_port_affinity_to_group_for_output(
   gen_topo_output_port_t *out_port_ptr,
   uint32_t                group_num,
   gen_topo_trigger_t      curr_trigger)
{
   gen_topo_module_t *        module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   gen_topo_trigger_policy_t *tp_ptr     = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];
   if (tp_ptr)
   {
      return tp_ptr->trigger_groups_ptr[group_num].out_port_grp_affinity_ptr[out_port_ptr->gu.cmn.index];
   }
   // Assume PRESENT by default (if module doesn't specify)
   return FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT;
}

static fwk_extn_port_nontrigger_policy_t gen_topo_get_default_nontrigger_policy(gen_topo_module_t *module_ptr,
                                                                                gen_topo_trigger_t curr_trigger)
{
   if ( module_ptr->flags.need_stm_extn && (GEN_TOPO_DATA_TRIGGER == curr_trigger) &&
       (FALSE == gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr)))
   {
      // data ports are marked blocked by default for signal triggered  modules.
      // module can always overwrite this using trigger policy callback.
      return FWK_EXTN_PORT_NON_TRIGGER_BLOCKED;
   }
   else
   {
      return FWK_EXTN_PORT_NON_TRIGGER_INVALID;
   }
}

fwk_extn_port_nontrigger_policy_t gen_topo_get_nontrigger_policy_for_output(gen_topo_output_port_t *out_port_ptr,
                                                                            gen_topo_trigger_t      curr_trigger)
{
   gen_topo_module_t *        module_ptr = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   gen_topo_trigger_policy_t *tp_ptr     = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];

   if (tp_ptr && tp_ptr->nontrigger_policy.out_port_grp_policy_ptr)
   {
      return tp_ptr->nontrigger_policy.out_port_grp_policy_ptr[out_port_ptr->gu.cmn.index];
   }

   return gen_topo_get_default_nontrigger_policy(module_ptr, curr_trigger);
}

fwk_extn_port_nontrigger_policy_t gen_topo_get_nontrigger_policy_for_input(gen_topo_input_port_t *in_port_ptr,
                                                                           gen_topo_trigger_t     curr_trigger)
{
   gen_topo_module_t *        module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;
   gen_topo_trigger_policy_t *tp_ptr     = module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(curr_trigger)];

   if (tp_ptr && tp_ptr->nontrigger_policy.in_port_grp_policy_ptr)
   {
      return tp_ptr->nontrigger_policy.in_port_grp_policy_ptr[in_port_ptr->gu.cmn.index];
   }

   return gen_topo_get_default_nontrigger_policy(module_ptr, curr_trigger);
}

bool_t gen_topo_aggregate_across_groups_and_ports(gen_topo_module_t *module_ptr,
                                                  gen_topo_trigger_t curr_trigger,
                                                  bool_t             in_ports_satisfied,
                                                  bool_t             out_ports_satisfied,
                                                  bool_t             is_in_port_dictating_policy,
                                                  bool_t             is_out_port_dictating_policy,
                                                  bool_t *           accross_group_satisfied_ptr)
{
   bool_t BREAK_LOOP      = TRUE;
   bool_t DONT_BREAK_LOOP = FALSE;

   if (FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY == gen_topo_get_port_trigger_policy(module_ptr, curr_trigger))
   {
      // across group it's OR policy. Even if one group satisfies we can break
      // (opposite of port-trigger-policy)

      // for sink module, only input side triggers need to be satisfied.
      if (module_ptr->gu.flags.is_sink && (/*is_in_port_dictating_policy && */ in_ports_satisfied))
      {
         *accross_group_satisfied_ptr = TRUE;
         return BREAK_LOOP;
      }

      // for source module, only output side triggers need to be satisfied.
      if (module_ptr->gu.flags.is_source && (/*is_out_port_dictating_policy && */ out_ports_satisfied))
      {
         *accross_group_satisfied_ptr = TRUE;
         return BREAK_LOOP;
      }

      // port level mandatory
      if ((!is_in_port_dictating_policy && out_ports_satisfied) ||
          (!is_out_port_dictating_policy && in_ports_satisfied) ||
          (is_in_port_dictating_policy && is_out_port_dictating_policy && in_ports_satisfied && out_ports_satisfied))
      {
         *accross_group_satisfied_ptr = TRUE;
         return BREAK_LOOP;
      }
   }
   else
   {
      // across-groups it's AND policy. So we can break if even one group is not satisfied.

      // for sink module, if input side is not satisfied, we can break
      if (module_ptr->gu.flags.is_sink)
      {
         if (is_in_port_dictating_policy && !in_ports_satisfied)
         {
            *accross_group_satisfied_ptr = FALSE;
            return BREAK_LOOP;
         }
      }

      // for source module, if output side is not satisfied we can break
      else if (module_ptr->gu.flags.is_source)
      {
         if (is_out_port_dictating_policy && !out_ports_satisfied)
         {
            *accross_group_satisfied_ptr = FALSE;
            return BREAK_LOOP;
         }
      }
      else
      {
         // port level optional
         if (is_in_port_dictating_policy && is_out_port_dictating_policy)
         {
            // if both in and out are dictating policy, then if both are not satisfied,
            // whole group is not satisfied. Since we use group-level-AND,
            // we can exit even if one group is not satisfied.
            if (!in_ports_satisfied && !out_ports_satisfied)
            {
               *accross_group_satisfied_ptr = FALSE;
               return BREAK_LOOP;
            }
         }
         else if (is_in_port_dictating_policy)
         {
            if (!in_ports_satisfied)
            {
               *accross_group_satisfied_ptr = FALSE;
               return BREAK_LOOP;
            }
         }
         else if (is_out_port_dictating_policy)
         {
            if (!out_ports_satisfied)
            {
               *accross_group_satisfied_ptr = FALSE;
               return BREAK_LOOP;
            }
         }
      }

      // if we didn't break, then this group condition satisfied, check other groups.
      if (is_in_port_dictating_policy || is_out_port_dictating_policy)
      {
         *accross_group_satisfied_ptr = TRUE;
      }
   }

   return DONT_BREAK_LOOP;
}

static bool_t gen_topo_is_data_trigger_satisfied_for_default_policy_module(gen_topo_module_t *module_ptr,
                                                                           bool_t *is_ext_trigger_not_satisfied_ptr,
                                                                           gen_topo_process_context_t *proc_ctxt_ptr)
{
   gen_topo_t *topo_ptr                    = module_ptr->topo_ptr;
   bool_t      any_ext_port_needs_data_buf = FALSE;
   bool_t      module_trigger_satisfied    = FALSE;

   bool_t in_ports_satisfied = TRUE, out_ports_satisfied = TRUE;

   // src modules must get triggered as long as output is present.
   bool_t atleast_one_ip_started = (0 == module_ptr->gu.max_input_ports) ||
                                   ((0 == module_ptr->gu.min_input_ports) && (0 == module_ptr->gu.num_input_ports));

   // sink modules must get triggered as long as input is present.
   bool_t atleast_one_op_started = (0 == module_ptr->gu.max_output_ports) ||
                                   ((0 == module_ptr->gu.min_output_ports) && (0 == module_ptr->gu.num_output_ports));

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      /**
       * For modules with no trigger policy, ignore port if it is not started or if data-flow has not started.
       * For modules with trigger policy, the module must change trigger policy.
       * This helps in SAL/Splitter (which don't implement trigger policy) cases where one in or one out is
       * stopped/EOSed, others (if any) can run; at least one input and one out still required.
       * i.e., is_in_port_dictating_policy is TRUE by default, but ports may not be satisfied.
       */
      if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state) ||
          (TOPO_DATA_FLOW_STATE_FLOWING != in_port_ptr->common.data_flow_state))
      {
         continue;
      }

      atleast_one_ip_started = TRUE;

      // start state is already checked to get buf
      bool_t port_satisfied = /*(TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) && */
         in_port_ptr->common.flags.is_mf_valid;

      bool_t ext_trigger_not_satisfied = FALSE;
      bool_t trigger_present           = topo_ptr->gen_topo_vtable_ptr->input_port_is_trigger_present(topo_ptr,
                                                                                            in_port_ptr,
                                                                                            &ext_trigger_not_satisfied);
      if (!trigger_present)
      {
         port_satisfied = FALSE;
         any_ext_port_needs_data_buf |= ext_trigger_not_satisfied;

         proc_ctxt_ptr->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;
      }
      else
      {
         proc_ctxt_ptr->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
      }

#ifdef TRIGGER_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Input port_id:0x%lx, curr_trigger1 (1-D,2-S), port_satisfied:%lu, "
               "any_ext_port_needs_data_buf:%lu",
               module_ptr->gu.module_instance_id,
               in_port_ptr->gu.cmn.id,
               port_satisfied,
               any_ext_port_needs_data_buf);
#endif

      in_ports_satisfied = in_ports_satisfied & port_satisfied;
   }

   in_ports_satisfied = in_ports_satisfied & atleast_one_ip_started;

   // if inputs are not satisfied, early return if possible
   if (FALSE == in_ports_satisfied)
   {
      if (NULL == is_ext_trigger_not_satisfied_ptr)
      {
         // if ext port check is not required, early return since all port are mandatory
         return FALSE;
      }
      else if (any_ext_port_needs_data_buf)
      {
         // if atleast one input ext port is not satisfied, then return early
         *is_ext_trigger_not_satisfied_ptr = any_ext_port_needs_data_buf;
         return FALSE;
      }
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      /** see comment in in_port
       *
       * Data flow state cannot be used. If it's used, then output will never move out of data flow stop.
       * output port size check is used. Consider splitter for which new output is added. the new output must be
       * ignored until
       * MF flows through it or threshold is assigned (decoder case). */
      bool_t size_is_known = topo_ptr->gen_topo_vtable_ptr->output_port_is_size_known(topo_ptr, out_port_ptr);
      if ((TOPO_PORT_STATE_STARTED != out_port_ptr->common.state) || (!size_is_known))
      {
         continue;
      }

      atleast_one_op_started = TRUE;

      // start state is already checked to get buf
      bool_t port_satisfied            = TRUE;
      bool_t ext_trigger_not_satisfied = FALSE;
      bool_t trigger_present =
         topo_ptr->gen_topo_vtable_ptr->output_port_is_trigger_present(topo_ptr,
                                                                       out_port_ptr,
                                                                       &ext_trigger_not_satisfied);
      if (!trigger_present)
      {
         port_satisfied = FALSE;
         any_ext_port_needs_data_buf |= ext_trigger_not_satisfied;

         proc_ctxt_ptr->out_port_scratch_ptr[out_port_ptr->gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;
      }
      else
      {
         proc_ctxt_ptr->out_port_scratch_ptr[out_port_ptr->gu.cmn.index].flags.is_trigger_present =
            TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
      }

#ifdef TRIGGER_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Output port_id:0x%lx, curr_trigger1 (1-D,2-S), port_satisfied:%lu, "
               "any_ext_port_needs_data_buf:%lu",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id,
               port_satisfied,
               any_ext_port_needs_data_buf);
#endif

      out_ports_satisfied = out_ports_satisfied && port_satisfied;
   }

   out_ports_satisfied = out_ports_satisfied & atleast_one_op_started;

   module_trigger_satisfied = out_ports_satisfied && in_ports_satisfied;
   if (!module_trigger_satisfied)
   {
      if (is_ext_trigger_not_satisfied_ptr)
      {
         *is_ext_trigger_not_satisfied_ptr = any_ext_port_needs_data_buf;
      }
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: curr_trigger1 (1-D,2-S), is_module_trigger_condition_satisfied%u, "
            "any_ext_port_needs_data_buf%u",
            module_ptr->gu.module_instance_id,
            module_trigger_satisfied,
            any_ext_port_needs_data_buf);
#endif

   return module_trigger_satisfied;
}

bool_t gen_topo_is_module_data_trigger_condition_satisfied(gen_topo_module_t *         module_ptr,
                                                           bool_t *                    is_ext_trigger_not_satisfied_ptr,
                                                           gen_topo_process_context_t *proc_ctxt_ptr)
{
   if (gen_topo_is_module_data_trigger_policy_active(module_ptr))
   {
      return gen_topo_is_data_trigger_satisfied_for_trigger_policy_module(module_ptr,
                                                                          is_ext_trigger_not_satisfied_ptr,
                                                                          proc_ctxt_ptr);
   }

   return gen_topo_is_data_trigger_satisfied_for_default_policy_module(module_ptr,
                                                                       is_ext_trigger_not_satisfied_ptr,
                                                                       proc_ctxt_ptr);
}

static bool_t gen_topo_is_data_trigger_satisfied_for_trigger_policy_module(gen_topo_module_t *module_ptr,
                                                                           bool_t *is_ext_trigger_not_satisfied_ptr,
                                                                           gen_topo_process_context_t *proc_ctxt_ptr)
{
   gen_topo_t *       topo_ptr                    = module_ptr->topo_ptr;
   bool_t             accross_group_satisfied     = FALSE;
   bool_t             any_ext_port_needs_data_buf = FALSE;
   gen_topo_trigger_t curr_trigger                = GEN_TOPO_DATA_TRIGGER;

   uint32_t num_groups = gen_topo_get_num_trigger_groups(module_ptr, curr_trigger);

   for (uint32_t g = 0; g < num_groups; g++)
   {
      bool_t in_ports_satisfied = FALSE, out_ports_satisfied = FALSE;
      bool_t is_in_port_dictating_policy  = FALSE;
      bool_t is_out_port_dictating_policy = FALSE;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (GEN_TOPO_IS_NON_TRIGGERABLE_PORT(gen_topo_get_nontrigger_policy_for_input(in_port_ptr, curr_trigger)))
         {
            continue;
         }

         fwk_extn_port_trigger_affinity_t a =
            gen_topo_get_port_affinity_to_group_for_input(in_port_ptr, g, curr_trigger);

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE == a)
         {
            continue;
         }
         is_in_port_dictating_policy = TRUE;

         /**
          * For modules with no trigger policy, ignore port if it is not started or if data-flow has not started.
          * For modules with trigger policy, the module must change trigger policy.
          * This helps in SAL/Splitter (which don't implement trigger policy) cases where one in or one out is
          * stopped/EOSed, others (if any) can run; at least one input and one out still required.
          * i.e., is_in_port_dictating_policy is TRUE by default, but ports may not be satisfied.
          *
          * For trigger policy module: module could be buffering module or non-buffering module.
          * For buffering module if input is at-gap or stopped, we can call with output alone.
          * For non-buffering if input is not there then we might not call the module (both input & output required).
          */

         // start state is already checked to get buf
         bool_t port_satisfied = /*(TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) && */
            in_port_ptr->common.flags.is_mf_valid;

         if (a == FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT)
         {
            bool_t ext_trigger_not_satisfied = FALSE;
            bool_t trigger_present =
               topo_ptr->gen_topo_vtable_ptr->input_port_is_trigger_present(topo_ptr,
                                                                            in_port_ptr,
                                                                            &ext_trigger_not_satisfied);
            if (!trigger_present)
            {
               port_satisfied = FALSE;
               any_ext_port_needs_data_buf |= ext_trigger_not_satisfied;

               proc_ctxt_ptr->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.is_trigger_present =
                  TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;
            }
            else
            {
               proc_ctxt_ptr->in_port_scratch_ptr[in_port_ptr->gu.cmn.index].flags.is_trigger_present =
                  TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT
         {
            bool_t trigger_absent = topo_ptr->gen_topo_vtable_ptr->input_port_is_trigger_absent(topo_ptr, in_port_ptr);
            if (!trigger_absent)
            {
               port_satisfied = FALSE;
            }
         }
#ifdef TRIGGER_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Input port_id:0x%lx, curr_trigger%u (1-D,2-S), group_idx:%lu, port_satisfied:%lu, "
                  "any_ext_port_needs_data_buf:%lu",
                  module_ptr->gu.module_instance_id,
                  in_port_ptr->gu.cmn.id,
                  curr_trigger,
                  g,
                  port_satisfied,
                  any_ext_port_needs_data_buf);
#endif
         if (FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY == gen_topo_get_port_trigger_policy(module_ptr, curr_trigger))
         {
            if (!port_satisfied)
            {
               // even if one port is not satisfied, in-port trigger is not said to be satisfied as within group ports
               // are ANDed.
               in_ports_satisfied = FALSE;
               break;
            }
            else
            {
               in_ports_satisfied = TRUE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL
         {
            // even if one port is satisfied, in-port trigger is said to be satisfied as within groups ports are ORed.
            if (port_satisfied)
            {
               in_ports_satisfied = TRUE;
               break;
            }
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if (GEN_TOPO_IS_NON_TRIGGERABLE_PORT(gen_topo_get_nontrigger_policy_for_output(out_port_ptr, curr_trigger)))
         {
            continue;
         }
         // Assume PRESENT by default (if module doesn't specify)
         fwk_extn_port_trigger_affinity_t a =
            gen_topo_get_port_affinity_to_group_for_output(out_port_ptr, g, curr_trigger);

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE == a)
         {
            continue;
         }
         is_out_port_dictating_policy = TRUE;

         // start state is already checked to get buf
         bool_t port_satisfied = TRUE;
         /*(TOPO_PORT_STATE_STARTED == out_port_ptr->common.state)*/;

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT == a)
         {
            bool_t ext_trigger_not_satisfied = FALSE;
            bool_t trigger_present =
               topo_ptr->gen_topo_vtable_ptr->output_port_is_trigger_present(topo_ptr,
                                                                             out_port_ptr,
                                                                             &ext_trigger_not_satisfied);
            if (!trigger_present)
            {
               port_satisfied = FALSE;
               any_ext_port_needs_data_buf |= ext_trigger_not_satisfied;

               proc_ctxt_ptr->out_port_scratch_ptr[out_port_ptr->gu.cmn.index].flags.is_trigger_present =
                  TOPO_PROC_CONTEXT_TRIGGER_NOT_PRESENT;
            }
            else
            {
               proc_ctxt_ptr->out_port_scratch_ptr[out_port_ptr->gu.cmn.index].flags.is_trigger_present =
                  TOPO_PROC_CONTEXT_TRIGGER_IS_PRESENT;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT
         {
            bool_t trigger_absent = topo_ptr->gen_topo_vtable_ptr->output_port_is_trigger_absent(topo_ptr, out_port_ptr);
            if (!trigger_absent)
            {
               port_satisfied = FALSE;
            }
         }

#ifdef TRIGGER_DEBUG
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Output port_id:0x%lx, curr_trigger%u (1-D,2-S), group_idx:%lu, port_satisfied:%lu, "
                  "any_ext_port_needs_data_buf:%lu",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  curr_trigger,
                  g,
                  port_satisfied,
                  any_ext_port_needs_data_buf);
#endif

         if (FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY == gen_topo_get_port_trigger_policy(module_ptr, curr_trigger))
         {
            if (!port_satisfied)
            {
               // even if one port is not satisfied, in-port trigger is not said to be satisfied as within group
               // ports are ANDed.
               out_ports_satisfied = FALSE;
               break;
            }
            else
            {
               out_ports_satisfied = TRUE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL
         {
            // even if one port is satisfied, in-port trigger is said to be satisfied as within groups ports are ORed.
            if (port_satisfied)
            {
               out_ports_satisfied = TRUE;
               break;
            }
         }
      }

      if (gen_topo_aggregate_across_groups_and_ports(module_ptr,
                                                     curr_trigger,
                                                     in_ports_satisfied,
                                                     out_ports_satisfied,
                                                     is_in_port_dictating_policy,
                                                     is_out_port_dictating_policy,
                                                     &accross_group_satisfied))
      {
         break;
      }
   }

   if (!accross_group_satisfied)
   {
      if (is_ext_trigger_not_satisfied_ptr)
      {
         *is_ext_trigger_not_satisfied_ptr = any_ext_port_needs_data_buf;
      }
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: curr_trigger%u (1-D,2-S), is_module_trigger_condition_satisfied%u, "
            "any_ext_port_needs_data_buf%u",
            module_ptr->gu.module_instance_id,
            curr_trigger,
            accross_group_satisfied,
            any_ext_port_needs_data_buf);
#endif

   return accross_group_satisfied;
}

static bool_t gen_topo_is_module_signal_trigger_condition_satisfied(gen_topo_module_t *module_ptr)
{
   if (gen_topo_is_module_signal_trigger_policy_active(module_ptr))
   {
      return gen_topo_is_signal_trigger_satisfied_for_trigger_policy_module(module_ptr);
   }

   return gen_topo_is_signal_trigger_satisfied_for_default_policy_module(module_ptr);
}

/**
 * For signal trigger, by default, all the started & at flow input/output ports must be satisfied.
 *
 * when a module raises signal trigger policy, these 2 conditions are relaxed (unlike data triggers where we check
 * meeting threshold etc)
 */
static bool_t gen_topo_is_signal_trigger_satisfied_for_default_policy_module(gen_topo_module_t *module_ptr)
{
   /* if signal triger module has no trigger policy defined for signal triggers, then always call process on them.
    * Modules implementing stm extn that don't want process to be called when there's no valid inp media format
    * need to handle it in their own process func. This condition mainly covers HW EP modules. */
   if (module_ptr->flags.need_stm_extn)
   {
#ifdef TRIGGER_DEBUG
      TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: is_module_trigger_condition_satisfied1",
               module_ptr->gu.module_instance_id);
#endif
      return TRUE;
   }

   // src modules must get triggered as long as output is present.
   bool_t atleast_one_in_port_started =
      (0 == module_ptr->gu.max_input_ports) ||
      ((0 == module_ptr->gu.min_input_ports) && (0 == module_ptr->gu.num_input_ports));

   // sink modules must get triggered as long as input is present.
   bool_t atleast_one_out_port_started =
      (0 == module_ptr->gu.max_output_ports) ||
      ((0 == module_ptr->gu.min_output_ports) && (0 == module_ptr->gu.num_output_ports));

   bool_t input_ports_satisfied = TRUE, output_ports_satisfied = TRUE;

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

      // for non-trigger policy MIMO modules, trigger is satisfied if atleast one input is started like SAL , splitter.
      if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state) ||
          (TOPO_DATA_FLOW_STATE_FLOWING != in_port_ptr->common.data_flow_state))
      {
         continue;
      }

      atleast_one_in_port_started = TRUE;

      // start state is already checked to get buf
      bool_t port_satisfied =
         (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr) && in_port_ptr->common.flags.is_mf_valid;

#ifdef TRIGGER_DEBUG
      TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Input port_id:0x%lx, curr_trigger2 (1-D,2-S), port_satisfied:%lu",
               module_ptr->gu.module_instance_id,
               in_port_ptr->gu.cmn.id,
               port_satisfied);
#endif

      if (!port_satisfied)
      {
         return FALSE;
      }

      // if port is not satisfied we are returining early so no need to aggregate port satisfied result.
      // input_ports_satisfied = input_ports_satisfied && port_satisfied;
   }

   // if any input was not satisfied it would have returned early, so no need to && with input_ports_satisfied
   input_ports_satisfied = atleast_one_in_port_started;

   if (!input_ports_satisfied)
   {
      return FALSE;
   }

   // if port is not satisfied we are returining early so no need to aggregate port satisfied result.
   // input_ports_satisifed = input_ports_satisifed && port_satisfied;

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

      // for non-trigger policy MIMO modules, trigger is satisfied if atleast one input is started like SAL , splitter.
      if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
      {
         continue;
      }

      atleast_one_out_port_started = TRUE;

      // no need to check port threshold since buffer presence implies valid port threshold.
      bool_t port_satisfied = gen_topo_output_has_empty_buffer(out_port_ptr) && out_port_ptr->common.flags.is_mf_valid;

#ifdef TRIGGER_DEBUG
      TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Output port_id:0x%lx, curr_trigger2 (1-D,2-S), port_satisfied:%lu",
               module_ptr->gu.module_instance_id,
               out_port_ptr->gu.cmn.id,
               port_satisfied);
#endif

      if (!port_satisfied)
      {
         return FALSE;
      }

      // if port is not satisfied we are returining early so no need to aggregate port satisfied result.
      // output_ports_satisifed = output_ports_satisifed && port_satisfied;
   }

   // if any output was not satisfied it would have returned early, so no need to && with output_ports_satisifed
   output_ports_satisfied = atleast_one_out_port_started;

   if (!output_ports_satisfied)
   {
      return FALSE;
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: curr_trigger2 (1-D,2-S), is_module_trigger_condition_satisfied%u",
            module_ptr->gu.module_instance_id,
            output_ports_satisfied && input_ports_satisfied);
#endif

   return output_ports_satisfied && input_ports_satisfied;
}

static bool_t gen_topo_is_signal_trigger_satisfied_for_trigger_policy_module(gen_topo_module_t *module_ptr)
{
   bool_t             accross_group_satisfied = FALSE;
   gen_topo_trigger_t curr_trigger            = GEN_TOPO_SIGNAL_TRIGGER;

   uint32_t num_groups = gen_topo_get_num_trigger_groups(module_ptr, curr_trigger);

   for (uint32_t g = 0; g < num_groups; g++)
   {
      bool_t in_ports_satisfied = FALSE, out_ports_satisfied = FALSE;
      bool_t is_in_port_dictating_policy  = FALSE;
      bool_t is_out_port_dictating_policy = FALSE;

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (GEN_TOPO_IS_NON_TRIGGERABLE_PORT(gen_topo_get_nontrigger_policy_for_input(in_port_ptr, curr_trigger)))
         {
            continue;
         }

         fwk_extn_port_trigger_affinity_t a =
            gen_topo_get_port_affinity_to_group_for_input(in_port_ptr, g, curr_trigger);

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE == a)
         {
            continue;
         }
         is_in_port_dictating_policy = TRUE;

         // start state is already checked to get buf
         bool_t port_satisfied = /*(TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) && */
            in_port_ptr->common.flags.is_mf_valid;

         if (a == FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT)
         {
            bool_t trigger_present = (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr);
            if (!trigger_present)
            {
               port_satisfied = FALSE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT
         {
            // trigger is absent if we have no buffer. otherwise, trigger may be present.
            bool_t trigger_absent = (NULL == in_port_ptr->common.bufs_ptr[0].data_ptr);
            if (!trigger_absent)
            {
               port_satisfied = FALSE;
            }
         }
#ifdef TRIGGER_DEBUG
         TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Input port_id:0x%lx, curr_trigger%u (1-D,2-S), group_idx:%lu, port_satisfied:%lu",
                  module_ptr->gu.module_instance_id,
                  in_port_ptr->gu.cmn.id,
                  curr_trigger,
                  g,
                  port_satisfied);
#endif
         if (FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY == gen_topo_get_port_trigger_policy(module_ptr, curr_trigger))
         {
            if (!port_satisfied)
            {
               // even if one port is not satisfied, in-port trigger is not said to be satisfied as within group ports
               // are ANDed.
               in_ports_satisfied = FALSE;
               break;
            }
            else
            {
               in_ports_satisfied = TRUE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL
         {
            // even if one port is satisfied, in-port trigger is said to be satisfied as within groups ports are ORed.
            if (port_satisfied)
            {
               in_ports_satisfied = TRUE;
               break;
            }
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if (GEN_TOPO_IS_NON_TRIGGERABLE_PORT(gen_topo_get_nontrigger_policy_for_output(out_port_ptr, curr_trigger)))
         {
            continue;
         }

         // Assume PRESENT by default (if module doesn't specify)
         fwk_extn_port_trigger_affinity_t a =
            gen_topo_get_port_affinity_to_group_for_output(out_port_ptr, g, curr_trigger);

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE == a)
         {
            continue;
         }
         is_out_port_dictating_policy = TRUE;

         // start state is already checked to get buf
         bool_t port_satisfied = TRUE;
         /*(TOPO_PORT_STATE_STARTED == out_port_ptr->common.state)*/;

         if (FWK_EXTN_PORT_TRIGGER_AFFINITY_PRESENT == a)
         {
            bool_t trigger_present = gen_topo_output_has_empty_buffer(out_port_ptr);
            if (!trigger_present)
            {
               port_satisfied = FALSE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT
         {
            // if there's an empty buf, then trigger maybe present.
            bool_t trigger_absent = !gen_topo_output_has_empty_buffer(out_port_ptr);
            if (!trigger_absent)
            {
               port_satisfied = FALSE;
            }
         }

#ifdef TRIGGER_DEBUG
         TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Output port_id:0x%lx, curr_trigger%u (1-D,2-S), group_idx:%lu, port_satisfied:%lu",
                  module_ptr->gu.module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  curr_trigger,
                  g,
                  port_satisfied);
#endif

         if (FWK_EXTN_PORT_TRIGGER_POLICY_MANDATORY == gen_topo_get_port_trigger_policy(module_ptr, curr_trigger))
         {
            if (!port_satisfied)
            {
               // even if one port is not satisfied, in-port trigger is not said to be satisfied as within group
               // ports are ANDed.
               out_ports_satisfied = FALSE;
               break;
            }
            else
            {
               out_ports_satisfied = TRUE;
            }
         }
         else // FWK_EXTN_PORT_TRIGGER_POLICY_OPTIONAL
         {
            // even if one port is satisfied, in-port trigger is said to be satisfied as within groups ports are ORed.
            if (port_satisfied)
            {
               out_ports_satisfied = TRUE;
               break;
            }
         }
      }

      if (gen_topo_aggregate_across_groups_and_ports(module_ptr,
                                                     curr_trigger,
                                                     in_ports_satisfied,
                                                     out_ports_satisfied,
                                                     is_in_port_dictating_policy,
                                                     is_out_port_dictating_policy,
                                                     &accross_group_satisfied))
      {
         break;
      }
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            " Module 0x%lX: curr_trigger%u (1-D,2-S), is_module_trigger_condition_satisfied%u",
            module_ptr->gu.module_instance_id,
            curr_trigger,
            accross_group_satisfied);
#endif

   return accross_group_satisfied;
}

/**
 * decides if trigger condition is satisfied
 */
bool_t gen_topo_is_module_trigger_condition_satisfied(gen_topo_module_t *module_ptr,
                                                      bool_t *           is_ext_trigger_not_satisfied_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;

   if (GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger)
   {
      return gen_topo_is_module_signal_trigger_condition_satisfied(module_ptr);
   }

   return gen_topo_is_module_data_trigger_condition_satisfied(module_ptr,
                                                              is_ext_trigger_not_satisfied_ptr,
                                                              &topo_ptr->proc_context);
}

/**
 * a module must be given output buffer only if there's a trigger
 * When it's possible to determine presence of a trigger, we do check
 * When it's not possible, then we just assume there's a trigger and give a buf. the side effect is inefficient
 process calls
 * but it's better than not calling a module and getting stuck.
 *
 * 1. If stale output is present, trigger is not present.
 * 2. If output_port->nblc_end_ptr exists and if the nblc end is an external port, check if ext port has a buf if so
 trigger is present, else trigger not present.
   3. If output_port->nblc_end_ptr doesn't exist or if its not external port
      a. Check output_port -> connected input port -> nblc_end_ptr has need-more, if so trigger is present, else
 trigger not present.
   4. Else
      Assume trigger is present and give topo buf.

   If port is non-triggerable optional and we give buffer here, it's ok as unless module trigger condition is not
 satisfied
   we wont call module.
 */
bool_t gen_topo_out_port_has_trigger(gen_topo_t *topo_ptr, gen_topo_output_port_t *out_port_ptr)
{
   /*Ex: if each leg of the splitter output is a separate subgraph, we need to check if each
    output port state is started before we allocate buffers for splitter's outputs.
    If downstream of any output port is not started, we continue.*/
   if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
   {
      return FALSE;
   }

   // Drop partial data in signal trigered container only if the data is not in DM module's variable nblc path
   if (GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger)
   {
      if (gen_topo_output_has_data(out_port_ptr) && gen_topo_is_port_in_realtime_path(&out_port_ptr->common) &&
          (!topo_ptr->flags.is_dm_enabled || !gen_topo_is_out_port_in_dm_variable_nblc(topo_ptr, out_port_ptr)))
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: out port id 0x%lx, Dropping stale data %lu bytes for signal trigger",
                  out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  out_port_ptr->common.bufs_ptr[0].actual_data_len);

         if (out_port_ptr->common.sdata.metadata_list_ptr)
         {
            // Dont need to exit island even if md lib is compiled in non island since we already
            // exited island when copying md from prev to next
         gen_topo_drop_all_metadata_within_range(topo_ptr->gu.log_id,
                                                 (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr,
                                                 &out_port_ptr->common,
                                                 gen_topo_get_actual_len_for_md_prop(&out_port_ptr->common),
                                                 FALSE /*keep_eos_and_ba_md*/);
         }
         gen_topo_set_all_bufs_len_to_zero(&out_port_ptr->common);
      }

      // Check if the current output/nblc external output is blocked
      fwk_extn_port_nontrigger_policy_t ntp =
         gen_topo_get_nontrigger_policy_for_output(out_port_ptr, GEN_TOPO_SIGNAL_TRIGGER);
      if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == ntp)
      {
         return FALSE;
      }
      else if (out_port_ptr->gu.ext_out_port_ptr &&
               !topo_ptr->topo_to_cntr_vtable_ptr->ext_out_port_has_buffer(out_port_ptr->gu.ext_out_port_ptr) &&
               FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp)
      {
         return FALSE;
      }
      else if (out_port_ptr->nblc_end_ptr && (out_port_ptr->nblc_end_ptr != out_port_ptr))
      {
         return gen_topo_out_port_has_trigger(topo_ptr, out_port_ptr->nblc_end_ptr);
      }

      return TRUE;
   }

   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
       gen_topo_get_nontrigger_policy_for_output(out_port_ptr, topo_ptr->proc_context.curr_trigger))
   {
      return FALSE;
   }

   // If the output has stale data, then trigger is not present.
   if (gen_topo_output_has_data(out_port_ptr))
   {
#ifdef TRIGGER_DEBUG
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: output buf has stale %lu bytes.",
               out_port_ptr->gu.cmn.module_ptr->module_instance_id,
               out_port_ptr->common.bufs_ptr[0].actual_data_len);
#endif
      return FALSE;
   }

   if (out_port_ptr->nblc_end_ptr && out_port_ptr->nblc_end_ptr->gu.ext_out_port_ptr)
   {
      if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED ==
          gen_topo_get_nontrigger_policy_for_output(out_port_ptr->nblc_end_ptr, topo_ptr->proc_context.curr_trigger))
      {
#ifdef TRIGGER_DEBUG_DEEP
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Output port id:0x%lx, NBLC end blocked",
                  out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  out_port_ptr->gu.cmn.id);
#endif
         return FALSE;
      }

      if (topo_ptr->topo_to_cntr_vtable_ptr->ext_out_port_has_buffer)
      {
         return topo_ptr->topo_to_cntr_vtable_ptr->ext_out_port_has_buffer(
            out_port_ptr->nblc_end_ptr->gu.ext_out_port_ptr);
      }
      else
      {
         return TRUE;
      }
   }
   else
   {
      if (out_port_ptr->gu.conn_in_port_ptr)
      {
         gen_topo_data_need_t rc =
            gen_topo_in_port_needs_data(topo_ptr, (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr);
         if ((GEN_TOPO_DATA_NOT_NEEDED | GEN_TOPO_DATA_BLOCKED) & rc)
         {
#ifdef TRIGGER_DEBUG_DEEP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     "   Module 0x%lX: Output port id:0x%lx, connected input does not need data or blocked",
                     out_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     out_port_ptr->gu.cmn.id);
#endif
            return FALSE;
         }
         else // GEN_TOPO_DATA_NEEDED and GEN_TOPO_DATA_NEEDED_OPTIONALLY cases
         {
            return TRUE;
         }
      }
      else
      {
         return TRUE;
      }
   }
}

/**
 * input port needs more input if nblc end is asking for need-more.
 * if this is not the beginning of the linear chain, or of this is not a linear chain or if processing didn't begin
 * otherwise, need-more only if end of the NBLC says need-more
 *
 * at EoS or input-discontinuity (force_process) also modules need to be called even if they don't have need-more
 * set. (need to be taken care in the caller)
 *
 */

gen_topo_data_need_t gen_topo_in_port_needs_data(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr)
{
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

   gen_topo_data_need_t rc = GEN_TOPO_DATA_NOT_NEEDED;
   /*Ex: if each leg of the SAL input is a separate subgraph, we need to check if this
          input port's state is "started" before we allocate buffers for it.
          If upstream of any input port is not started, we continue.*/
   if (TOPO_PORT_STATE_STARTED != in_port_ptr->common.state)
   {
      return GEN_TOPO_DATA_BLOCKED;
   }

   /**
    * If module requests signal-trigger-policy,
    * Port can be blocked/optional non-triggerable for signal triggers.
    * This makes sure that, blocked/optional ports are not Underrun.
    * FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT policy is not supported yet.
    */
   if (GEN_TOPO_SIGNAL_TRIGGER == topo_ptr->proc_context.curr_trigger)
   {
      fwk_extn_port_nontrigger_policy_t ntp =
         gen_topo_get_nontrigger_policy_for_input(in_port_ptr, GEN_TOPO_SIGNAL_TRIGGER);
      if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == ntp)
      {
         return GEN_TOPO_DATA_BLOCKED;
      }
      else if (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp)
      {
         return GEN_TOPO_DATA_NEEDED_OPTIONALLY;
      }

      // check nblc end
      if(in_port_ptr->nblc_end_ptr && (in_port_ptr->nblc_end_ptr != in_port_ptr))
      {
         return gen_topo_in_port_needs_data(topo_ptr, in_port_ptr->nblc_end_ptr );
      }

      // by default return data needed for signal triggers
      return GEN_TOPO_DATA_NEEDED;
   }

   // Data is not needed if data trigger is not allowed.
   if (!gen_topo_is_module_data_trigger_allowed(module_ptr))
   {
      return GEN_TOPO_DATA_BLOCKED;
   }

   fwk_extn_port_nontrigger_policy_t ntp =
      gen_topo_get_nontrigger_policy_for_input(in_port_ptr, topo_ptr->proc_context.curr_trigger);
   // nontrigger_policy or trigger policy doesn't decide the policy when signal trigger happens
   if (FWK_EXTN_PORT_NON_TRIGGER_BLOCKED == ntp)
   {
      return GEN_TOPO_DATA_BLOCKED;
   }

// first time: Only effect of not treating first time as special case is that initially input will be optional.
// output will be mandatory. hence we will wake up, see output, and go back to waiting for optional input. Since this is
// one time hit
// it's ok. Container ref of EOS is avoided with this.
#if 0
   if (FALSE == in_port_ptr->flags.processing_began)
   {
      rc = (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == in_port_ptr->common.nontrigger_policy)
              ? GEN_TOPO_DATA_NEEDED_OPTIONALLY
              : GEN_TOPO_DATA_NEEDED;
   }
   else
#endif
   {
      // if there's a flushing EOS or DFG stuck, input is optional
      /* Fix for DFG:
         Ideally checking just for end_of_frame should be sufficient for DFG.
         Since end_of_frame has wider scope threfore to limit the fix, DFG MD is looked up.*/
      if (in_port_ptr->common.sdata.flags.marker_eos || 
             (in_port_ptr->common.sdata.flags.end_of_frame && gen_topo_md_list_has_dfg(in_port_ptr->common.sdata.metadata_list_ptr)))
      {
#ifdef TRIGGER_DEBUG_DEEP
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: Input port id:0x%lx, port has EOS or DFG. need data optionally",
                  in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                  in_port_ptr->gu.cmn.id);
#endif
         return GEN_TOPO_DATA_NEEDED_OPTIONALLY;
      }

      bool_t inbuf_full = GEN_TOPO_IS_IN_BUF_FULL(in_port_ptr);

      // if port doesn't require data buffering, and buffer is not full, then also consider as
      // need-more.
      if ((NULL == in_port_ptr->nblc_end_ptr) || (in_port_ptr->nblc_end_ptr == in_port_ptr))
      {
         /** SYNC module.
          * if threshold is enabled then data is needed if buffer is not full (container frame len data is not present)
          * if threshold is disabled then data is needed if need_more_input is set.
          */
         bool_t is_fixed_frame_required = gen_topo_does_module_requires_fixed_frame_len(module_ptr);

         // if inbuf is not full, check if corresponding nblc has sufficient data
         if ((is_fixed_frame_required && !inbuf_full) || in_port_ptr->flags.need_more_input)
         {
            rc = GEN_TOPO_DATA_NEEDED;
         }
         else
         {
// port_has_threshold flag must not be checked here. test: pb_pause_13_dfg.
#ifdef TRIGGER_DEBUG_DEEP
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_LOW_PRIO,
                     " Module 0x%lX: Input port id:0x%lx, need_more%u, inbuf_full%u, rdf%u",
                     in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                     in_port_ptr->gu.cmn.id,
                     in_port_ptr->flags.need_more_input,
                     inbuf_full,
                     module_ptr->flags.requires_data_buf);
#endif

            /* Justification of adding GEN_TOPO_DATA_NEEDED_OPTIONALLY in case if buffer is not full.
             * Two external input ports to SYNC module via MFC (DM/requires data buffering true) at input.
             * Container frame size is 5ms. One input upstream is 5ms and another input upstream is 1ms.
             * One MFC input has 1ms data, which is sufficient and need_more_input is cleared.
             * Container can't trigger until trigger is received from other input port. Need to make sure that more
             * buffering happens on first input port until container gets trigger on other ports.
             * If we don't make it optional then we will stop buffering on the 1ms path and will only unblock it when
             * buffer is received on 5ms path. This can result in some jitter issues on 1ms path.
             */
            // if input buffer is not full then can buffer optionally
            // doing this only for RT path to avoid overruns on upstream.
            rc = (in_port_ptr->common.flags.is_upstream_realtime && !inbuf_full) ? GEN_TOPO_DATA_NEEDED_OPTIONALLY
                                                                                 : GEN_TOPO_DATA_NOT_NEEDED;
         }
      }
      else
      {

         // can't copy more data if buffer is already full.
         rc = inbuf_full ? GEN_TOPO_DATA_NOT_NEEDED : gen_topo_in_port_needs_data(topo_ptr, in_port_ptr->nblc_end_ptr);

         // if nblc-end needs data and this port has some data then evaluates the data-requirement.
         if (GEN_TOPO_DATA_NEEDED == rc && in_port_ptr->common.bufs_ptr[0].data_ptr)
         {
            bool_t is_fixed_frame_required = gen_topo_does_module_requires_fixed_frame_len(
               (gen_topo_module_t *)in_port_ptr->nblc_end_ptr->gu.cmn.module_ptr);

            if (!is_fixed_frame_required)
            {
               // if nblc doesn't requires fixed-frame then trigger is optional if there is some data on this input
               // port.
               rc = (in_port_ptr->common.bufs_ptr[0].actual_data_len) ? GEN_TOPO_DATA_NEEDED_OPTIONALLY
                                                                      : GEN_TOPO_DATA_NEEDED;
            }
            else
            {
               uint32_t bytes_available_per_buf = in_port_ptr->nblc_end_ptr->common.bufs_ptr[0].actual_data_len;
               uint32_t total_bytes_available   = gen_topo_get_total_actual_len(&in_port_ptr->nblc_end_ptr->common);
               uint32_t bytes_needed_per_buf    = 0;

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

               bytes_needed_per_buf = in_port_ptr->common.max_buf_len_per_buf - bytes_available_per_buf;

               //if this input already has sufficient data then return not-needed
               if (in_port_ptr->common.bufs_ptr[0].actual_data_len >= bytes_needed_per_buf)
               {
                  rc = GEN_TOPO_DATA_NOT_NEEDED;
               }
            }
         }
      }

      bool_t is_steady_state = (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->common.data_flow_state);

      // at flushing EOS or at MF, data flow state (at-gap), in-port becomes optional.
      // consider ext-in port from which EOS has moved inside. Earlier Ports may not have EOS at this time.
      // only when data flow is happening, input is mandatory.
      // dont consider data flow state first time (otherwise, we will never move out of at-gap).
      if (!is_steady_state || (FWK_EXTN_PORT_NON_TRIGGER_OPTIONAL == ntp))
      {
         rc = ((GEN_TOPO_DATA_NEEDED == rc) ? GEN_TOPO_DATA_NEEDED_OPTIONALLY : rc);
      }
#ifdef TRIGGER_DEBUG_DEEP
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_LOW_PRIO,
               " Module 0x%lX: Input port id:0x%lx, in_port_needs_data%u",
               in_port_ptr->gu.cmn.module_ptr->module_instance_id,
               in_port_ptr->gu.cmn.id,
               rc);
#endif
   }

   return rc;
}

