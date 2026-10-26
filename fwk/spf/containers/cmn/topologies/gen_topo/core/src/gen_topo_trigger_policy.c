/**
 * \file gen_topo_trigger_policy.c
 *
 * \brief
 *
 *     trigger policy related implementation
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "gen_topo.h"

// Trigger policy absent not supported so moved this function from island file to here.
bool_t gen_topo_input_port_is_trigger_absent(void *ctx_topo_ptr, void *ctx_in_port_ptr)
{
   gen_topo_input_port_t *in_port_ptr       = (gen_topo_input_port_t *)ctx_in_port_ptr;
   bool_t                 trigger_is_absent = TRUE;

   // Buffer is absent only if there's no data_ptr.
   if (NULL != in_port_ptr->common.bufs_ptr[0].data_ptr)
   {
      trigger_is_absent = FALSE;
   }

   return trigger_is_absent;
}


void gen_topo_populate_trigger_policy_extn_vtable(gen_topo_module_t *                       module_ptr,
                                                  fwk_extn_param_id_trigger_policy_cb_fn_t *handler_ptr)
{
   memset(handler_ptr, 0, sizeof(*handler_ptr));

   handler_ptr->version                            = 1;
   handler_ptr->context_ptr                        = (void *)module_ptr;
   handler_ptr->change_data_trigger_policy_cb_fn   = gen_topo_change_data_trigger_policy_cb_fn;
   handler_ptr->change_signal_trigger_policy_cb_fn = gen_topo_change_signal_trigger_policy_cb_fn;
}

static bool_t gen_topo_trigger_policy_is_data_trigger_in_st_cntr_active(gen_topo_t *topo_ptr)
{
   if (!topo_ptr->flags.is_signal_triggered) // not a ST container
   {
      return FALSE;
   }
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr))
         {
            return TRUE;
         }
      }
   }

   return FALSE;
}

ar_result_t gen_topo_trigger_policy_event_data_trigger_in_st_cntr(gen_topo_module_t *module_ptr,
                                                                  capi_buf_t *       payload_ptr)
{
   gen_topo_t *topo_ptr = module_ptr->topo_ptr;

   //when module raises this extn, check if it's at its initial value. It shouldn't have any
   // propagated value since we don't allow multiple data trigger in ST modules in a path.
   if (BLOCKED_DATA_TRIGGER_IN_ST != module_ptr->flags.need_data_trigger_in_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: Trigger Policy: Module 0x%08lX, already has need_data_trigger_in_st = %d: Another "
               "module with data trigger in ST exists in path. Now this module is trying to allow data trigger. Not "
               "allowed",
               module_ptr->gu.module_instance_id,
               module_ptr->flags.need_data_trigger_in_st);
   }

   if (sizeof(fwk_extn_event_id_data_trigger_in_st_cntr_t) > payload_ptr->actual_data_len)
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Insufficient trigger policy event payload size.");
      return AR_EBADPARAM;
   }
   bool_t need_data_trigger_in_st =
      (((fwk_extn_event_id_data_trigger_in_st_cntr_t *)payload_ptr->data_ptr)->is_enable) ? TRUE : FALSE;

   bool_t prev_need_data_trigger_over_st = gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr);

   if (need_data_trigger_in_st && (module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_SIGNAL_TRIGGER)]))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Trigger Policy: Module 0x%08lX, currently a module that uses signal trigger policy in signal "
               "triggered "
               "cntr cannot also need data trigger policy",
               module_ptr->gu.module_instance_id);

      return AR_EBADPARAM;
   }

   if (need_data_trigger_in_st && gen_topo_trigger_policy_is_data_trigger_in_st_cntr_active(topo_ptr))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: Trigger Policy: Module 0x%08lX, currently another module has active data_trigger in signal "
               "trigger",
               module_ptr->gu.module_instance_id);
      // This is not an error case since we are allowed to have multiple data_trig_in_st modules as long as
      // their paths don't intersect. They can be in parallel paths (Ex - 2 dams for Raw + Voice Activation concurrency case)
   }

   if (need_data_trigger_in_st && !prev_need_data_trigger_over_st)
   {
      // if trigger policy is already active for the module then determine the "any_trigger_policy" flag for the
      // container.
      if (module_ptr->tp_ptr[GEN_TOPO_INDEX_OF_TRIGGER(GEN_TOPO_DATA_TRIGGER)])
      {
         gen_topo_update_data_tpm_count(topo_ptr);
      }

      module_ptr->flags.needs_input_data_trigger_in_st =
         (((fwk_extn_event_id_data_trigger_in_st_cntr_t *)payload_ptr->data_ptr)->needs_input_triggers) ? TRUE : FALSE;

      module_ptr->flags.needs_output_data_trigger_in_st =
         (((fwk_extn_event_id_data_trigger_in_st_cntr_t *)payload_ptr->data_ptr)->needs_output_triggers) ? TRUE : FALSE;

      module_ptr->flags.need_data_trigger_in_st = ALLOW_DATA_TRIGGER_IN_ST_EVENT;

      gen_topo_propagate_data_trigger_in_st_cntr_event(module_ptr);
      GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, data_trigger_policy_change);
   }
   else if (!need_data_trigger_in_st && prev_need_data_trigger_over_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Disabling data trigger in ST is not allowed.");
      // changed from enable to disable then determine the "any_trigger_flag" from other modules in container.
      // gen_topo_reset_top_level_flags(topo_ptr);
      return AR_EBADPARAM;
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_HIGH_PRIO,
            "Trigger Policy: Module 0x%08lX. Data trigger in ST enabled %lu, num_data_tpm %lu",
            module_ptr->gu.module_instance_id,
            need_data_trigger_in_st,
            topo_ptr->num_data_tpm);

   return AR_EOK;
}

ar_result_t gen_topo_destroy_trigger_policy(gen_topo_module_t *module_ptr)
{
   for (uint32_t i = 0; i < SIZE_OF_ARRAY(module_ptr->tp_ptr); i++)
   {
      MFREE_NULLIFY(module_ptr->tp_ptr[i]);
   }
   return AR_EOK;
}

static void gen_topo_propagate_data_trigger_in_st_cntr_event_forward(gen_topo_t *       topo_ptr,
                                                                     gen_topo_module_t *module_ptr)
{
   if (!module_ptr)
   {
      return;
   }

   // need_data_trigger is only propagated through SISO or SIMO module.
   if (module_ptr && (module_ptr->gu.num_input_ports > 1))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Trigger Policy: Module 0x%08lX. Breaking Data trigger in ST propagation",
               module_ptr->gu.module_instance_id);
      return;
   }

   // propagation happens from a module that needs data trigger in ST
   // If another such module is found in this path, break propagation.
   if (ALLOW_DATA_TRIGGER_IN_ST_EVENT == module_ptr->flags.need_data_trigger_in_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Trigger Policy: Module 0x%08lX. Wants data trigger in ST propagation, breaking propagation. This may "
               "not work",
               module_ptr->gu.module_instance_id);
      return;
   }
   else if (ALLOW_DATA_TRIGGER_IN_ST_PROPAGATION == module_ptr->flags.need_data_trigger_in_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Trigger Policy: Module 0x%08lX. Wants data trigger in ST propagation, another such module already "
               "exists in the path. This may not work",
               module_ptr->gu.module_instance_id);
   }
   else
   {
      // propagation
      module_ptr->flags.need_data_trigger_in_st = ALLOW_DATA_TRIGGER_IN_ST_PROPAGATION;
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Trigger Policy: Module 0x%08lX. Data trigger in ST enabled via propagation",
               module_ptr->gu.module_instance_id);
   }

   for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
        LIST_ADVANCE(out_port_list_ptr))
   {
      if (out_port_list_ptr->op_port_ptr->conn_in_port_ptr)
      {
         gen_topo_module_t *next_module_ptr =
            (gen_topo_module_t *)out_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.module_ptr;
         gen_topo_propagate_data_trigger_in_st_cntr_event_forward(topo_ptr, next_module_ptr);
      }
   }
}

static void gen_topo_propagate_data_trigger_in_st_cntr_event_backward(gen_topo_t *       topo_ptr,
                                                                      gen_topo_module_t *module_ptr)
{
   if (!module_ptr)
   {
      return;
   }
   // need_data_trigger is only propagated through SISO or MISO module.
   if (module_ptr && (module_ptr->gu.num_output_ports > 1))
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Trigger Policy: Module 0x%08lX. Breaking Data trigger in ST propagation",
               module_ptr->gu.module_instance_id);
      return;
   }

   // If another such module is found in this path, break propagation.
   if (ALLOW_DATA_TRIGGER_IN_ST_EVENT == module_ptr->flags.need_data_trigger_in_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Trigger Policy: Module 0x%08lX. Wants data trigger in ST propagation, breaking propagation. This may "
               "not work",
               module_ptr->gu.module_instance_id);
      return;
   }
   else if (ALLOW_DATA_TRIGGER_IN_ST_PROPAGATION == module_ptr->flags.need_data_trigger_in_st)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Trigger Policy: Module 0x%08lX. Wants data trigger in ST propagation, another such module already "
               "exists in the path. This may not work",
               module_ptr->gu.module_instance_id);
   }
   else
   {
      module_ptr->flags.need_data_trigger_in_st = ALLOW_DATA_TRIGGER_IN_ST_PROPAGATION;

      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_HIGH_PRIO,
               "Trigger Policy: Module 0x%08lX. Data trigger in ST enabled via propagation",
               module_ptr->gu.module_instance_id);
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
        LIST_ADVANCE(in_port_list_ptr))
   {
      if (in_port_list_ptr->ip_port_ptr->conn_out_port_ptr)
      {
         gen_topo_module_t *prev_module_ptr =
            (gen_topo_module_t *)in_port_list_ptr->ip_port_ptr->conn_out_port_ptr->cmn.module_ptr;
         gen_topo_propagate_data_trigger_in_st_cntr_event_backward(topo_ptr, prev_module_ptr);
      }
   }
}

/**
 * Propagates the need_data_trigger_in_ST flag from the trigger policy module to both in forward and backward direction.
 * In forward direction through SISO and SIMO modules
 * In backward direction through SISO and MISO modules.
 */
ar_result_t gen_topo_propagate_data_trigger_in_st_cntr_event(gen_topo_module_t *module_ptr)
{
   gen_topo_t *topo_ptr                      = module_ptr->topo_ptr;

   if (!topo_ptr->flags.is_signal_triggered)
   {
      return AR_EOK;
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_HIGH_PRIO,
            "Trigger Policy: Module 0x%08lX. Data trigger in ST enabled input prop %ld, output prop %ld",
            module_ptr->gu.module_instance_id,
            module_ptr->flags.needs_input_data_trigger_in_st,
            module_ptr->flags.needs_output_data_trigger_in_st);

   if (module_ptr->flags.needs_input_data_trigger_in_st)
   {
      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         if (in_port_list_ptr->ip_port_ptr->conn_out_port_ptr)
         {
            gen_topo_module_t *prev_module_ptr =
               (gen_topo_module_t *)in_port_list_ptr->ip_port_ptr->conn_out_port_ptr->cmn.module_ptr;

            gen_topo_propagate_data_trigger_in_st_cntr_event_backward(topo_ptr, prev_module_ptr);
         }
      }
   }

   if (module_ptr->flags.needs_output_data_trigger_in_st)
   {
      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         if (out_port_list_ptr->op_port_ptr->conn_in_port_ptr)
         {
            gen_topo_module_t *next_module_ptr =
               (gen_topo_module_t *)out_port_list_ptr->op_port_ptr->conn_in_port_ptr->cmn.module_ptr;

            gen_topo_propagate_data_trigger_in_st_cntr_event_forward(topo_ptr, next_module_ptr);
         }
      }
   }

   return AR_EOK;
}
/**
 * Module is active if it and its ports are started + capi etc are created.
 */
bool_t gen_topo_is_module_active(gen_topo_module_t *module_ptr, bool_t need_to_ignore_state)
{
   bool_t atleast_one_active_output = FALSE;
   bool_t atleast_one_active_input  = FALSE;

   if (!need_to_ignore_state)
   {
      /**
       * If module belongs to a SG that's not started, then module is not active
       * But if SG is started, doesn't mean port is started (need to check down-graded state)
       */
      if (!gen_topo_is_module_sg_started(module_ptr))
      {
         return FALSE;
      }

      gu_input_port_list_t *in_port_lists[] = { module_ptr->gu.input_port_list_ptr,
                                                module_ptr->gu.ipc_input_port_list_ptr };
      for (uint32_t i = 0; i < SIZE_OF_ARRAY(in_port_lists); i++)
      {
         for (gu_input_port_list_t *in_port_list_ptr = in_port_lists[i]; in_port_list_ptr != NULL;
              LIST_ADVANCE(in_port_list_ptr))
         {
            gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
            // If the input port is not started or doesn't have a buffer (media-fmt prop didn't happen)
            // then process cannot be called on the module.
            if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state))
            {
               continue;
            }
            else
            {
               atleast_one_active_input = TRUE;
               break;
            }
         }
      }

      gu_output_port_list_t *out_port_lists[] = { module_ptr->gu.output_port_list_ptr,
                                                  module_ptr->gu.ipc_output_port_list_ptr };
      for (uint32_t i = 0; i < SIZE_OF_ARRAY(out_port_lists); i++)
      {
         for (gu_output_port_list_t *out_port_list_ptr = out_port_lists[i]; (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
            {
               continue;
            }
            else
            {
               atleast_one_active_output = TRUE;
               break;
            }
         }
      }
   }

   /**
    * at least one input or output must be active or module must support STM ext (=HW-EP mainly, in which case even if
    * input or output is not present we can call the module)
    *
    * If trigger policy then without only input or output also module may be called.
    *
    * AND if (disabled &) bypassed or if fmwk module (which always take bypass path) or if not disabled
    *     [don't call module_process if disabled and bypass not created except for framework modules]
    */
   bool_t active_as_per_state = need_to_ignore_state || (atleast_one_active_output || atleast_one_active_input);
   if (active_as_per_state || module_ptr->flags.need_stm_extn ||
       gen_topo_is_module_data_trigger_policy_active(module_ptr) ||
       gen_topo_is_module_signal_trigger_policy_active(module_ptr))
   {
      if (module_ptr->bypass_ptr || !module_ptr->capi_ptr || !module_ptr->flags.disabled)
      {
         return TRUE;
      }
   }
   return FALSE;
}

void gen_topo_set_module_active_flag_for_all_modules(gen_topo_t *topo_ptr)
{
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         module_ptr->flags.active      = gen_topo_is_module_active(module_ptr, FALSE /* need_to_ignore_state */);

#ifdef VERBOSE_DEBUGGING
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_LOW_PRIO,
                  " Module 0x%lX: active_flag %lu",
                  module_ptr->gu.module_instance_id,
                  module_ptr->flags.active);
#endif
      }
   }
}
