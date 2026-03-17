/**
 * \file gen_topo_trigger_policy_change_island.c
 *
 * \brief
 *
 *     trigger policy related implementation
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"
#include "thin_topo_inline.h"

static ar_result_t gen_topo_validate_trigger_non_trigger(uint32_t                          log_id,
                                                         fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                         uint32_t                          num_groups,
                                                         fwk_extn_port_trigger_group_t *   triggerable_groups_ptr,
                                                         bool_t                            is_input,
                                                         uint32_t                          port_index)
{
   ar_result_t result = AR_EOK;
   // Validate: A port cannot belong to both triggerable and non-triggerable groups.
   for (uint32_t g = 0; g < num_groups; g++)
   {
      fwk_extn_port_trigger_affinity_t *port_grp_affinity_ptr =
         is_input ? triggerable_groups_ptr[g].in_port_grp_affinity_ptr
                  : triggerable_groups_ptr[g].out_port_grp_affinity_ptr;

      fwk_extn_port_nontrigger_policy_t *non_trigger_policy_ptr =
         is_input ? nontriggerable_ports_ptr->in_port_grp_policy_ptr
                  : nontriggerable_ports_ptr->out_port_grp_policy_ptr;

      if (port_grp_affinity_ptr && (FWK_EXTN_PORT_TRIGGER_AFFINITY_ABSENT == port_grp_affinity_ptr[port_index]))
      {
         // Even though for data triggers we support ABSENT policy, for signal triggers we don't; Further, even for data
         // triggers, ABSENT policy is not tested as it's not required for any use cases.
         TOPO_MSG_ISLAND(log_id,
                  DBG_ERROR_PRIO,
                  "For port (is_input, index) (%u, %lu) trigger policy %u given in "
                  "group %lu is not supported",
                  is_input,
                  port_index,
                  port_grp_affinity_ptr[port_index],
                  g);
         return AR_EFAILED;
      }

#ifdef TRIGGER_DEBUG
      TOPO_MSG_ISLAND(log_id,
               DBG_LOW_PRIO,
               "For port (is_input, index) (%u, %lu) trigger policy %u (none0,present1,absent2), nontrigger policy %u "
               "(invalid0,optional1,blocked2) given in group %lu",
               is_input,
               port_index,
               port_grp_affinity_ptr ? port_grp_affinity_ptr[port_index] : 0,
               non_trigger_policy_ptr ? non_trigger_policy_ptr[port_index] : 0,
               g);
#endif
      if (non_trigger_policy_ptr && port_grp_affinity_ptr)
      {
         if ((FWK_EXTN_PORT_TRIGGER_AFFINITY_NONE != port_grp_affinity_ptr[port_index]) &&
             (FWK_EXTN_PORT_NON_TRIGGER_INVALID != non_trigger_policy_ptr[port_index]))
         {
            TOPO_MSG_ISLAND(log_id,
                     DBG_ERROR_PRIO,
                     "For port (is_input, index) (%u, %lu) trigger policy %u as well as nontrigger policy %u given in "
                     "group %lu",
                     is_input,
                     port_index,
                     port_grp_affinity_ptr[port_index],
                     non_trigger_policy_ptr[port_index],
                     g);
            return AR_EFAILED;
         }
      }
   }
   return result;
}


capi_err_t gen_topo_change_data_trigger_policy_cb_fn(void *                            context_ptr,
                                                            fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                            fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                            uint32_t                          num_groups,
                                                            fwk_extn_port_trigger_group_t *   triggerable_groups_ptr)
{
   capi_err_t result = gen_topo_change_trigger_policy_cb_fn(GEN_TOPO_DATA_TRIGGER,
                                                            context_ptr,
                                                            nontriggerable_ports_ptr,
                                                            port_trigger_policy,
                                                            num_groups,
                                                            triggerable_groups_ptr);
   return result;
}

capi_err_t gen_topo_change_signal_trigger_policy_cb_fn( void *                            context_ptr,
                                                               fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                               fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                               uint32_t                          num_groups,
                                                               fwk_extn_port_trigger_group_t *   triggerable_groups_ptr)
{
   return gen_topo_change_trigger_policy_cb_fn(GEN_TOPO_SIGNAL_TRIGGER,
                                               context_ptr,
                                               nontriggerable_ports_ptr,
                                               port_trigger_policy,
                                               num_groups,
                                               triggerable_groups_ptr);
}

capi_err_t gen_topo_change_trigger_policy_cb_fn(gen_topo_trigger_t                trigger_type,
                                                void                             *context_ptr,
                                                fwk_extn_port_nontrigger_group_t *nontriggerable_ports_ptr,
                                                fwk_extn_port_trigger_policy_t    port_trigger_policy,
                                                uint32_t                          num_groups,
                                                fwk_extn_port_trigger_group_t    *triggerable_groups_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_topo_module_t *     module_ptr   = (gen_topo_module_t *)context_ptr;
   gen_topo_t *            topo_ptr     = module_ptr->topo_ptr;
   gen_topo_input_port_t * in_port_ptr  = NULL;
   gen_topo_output_port_t *out_port_ptr = NULL;
   uint32_t                size = 0, size_first_part = 0;
   int8_t *                ptr                          = NULL;
   uint32_t                all_groups_policy_size       = 0;
   uint32_t                one_in_affinity_size         = 0;
   uint32_t                all_in_affinity_size         = 0;
   uint32_t                one_out_affinity_size        = 0;
   bool_t                  any_change                   = FALSE;
   bool_t                  ip_nontrigger_policy_present = FALSE, op_nontrigger_policy_present = FALSE;

   if ((GEN_TOPO_DATA_TRIGGER != trigger_type) && (GEN_TOPO_SIGNAL_TRIGGER != trigger_type))
   {
      return CAPI_EOK;
   }

   uint32_t                    index_of_trigger = GEN_TOPO_INDEX_OF_TRIGGER(trigger_type);
   gen_topo_trigger_policy_t **tp_pptr          = &(module_ptr->tp_ptr[index_of_trigger]);

   // check if there if TGP is getting enable to disable OR from disable to enable.
   // IF the TGP activeness is changing fwk events must be handled. If only
   bool_t is_tgp_activeness_change = TRUE;
   if(*tp_pptr)
   {
      if (num_groups)
      {
         // TGP is already disabled.
         TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                         DBG_HIGH_PRIO,
                         "Trigger Policy: Module 0x%08lX, TGP is already enabled, so not setting the event",
                         module_ptr->gu.module_instance_id);
         is_tgp_activeness_change = FALSE;
      }
   }
   else if (0 == num_groups)
   {
      // TGP is already disabled.
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Trigger Policy: Module 0x%08lX, TGP is already disabled.",
                      module_ptr->gu.module_instance_id);
      return AR_EOK;
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Trigger Policy: Module 0x%08lX, trigger_type%u (1-data,2-signal). prev trigger groups = %lu, prev "
            "port_trigger_policy%u ",
            module_ptr->gu.module_instance_id,
            trigger_type,
            gen_topo_get_num_trigger_groups(module_ptr, trigger_type),
            gen_topo_get_port_trigger_policy(module_ptr, trigger_type));
#endif

   // due to complexity with caching trigger policy when bypassed.
   if (module_ptr->bypass_ptr)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      "Trigger Policy: Module 0x%08lX, cannot change trigger policy when module is bypassed.",
                      module_ptr->gu.module_instance_id);

      return CAPI_EFAILED;
   }

   size_first_part = sizeof(gen_topo_trigger_policy_t);

   if (NULL != nontriggerable_ports_ptr)
   {
      if (nontriggerable_ports_ptr->in_port_grp_policy_ptr)
      {
         ip_nontrigger_policy_present = TRUE;
         size_first_part += (sizeof(fwk_extn_port_nontrigger_policy_t) * module_ptr->gu.max_input_ports);

         for (gu_input_port_list_t *gu_in_port_list_ptr = module_ptr->gu.input_port_list_ptr;
              (NULL != gu_in_port_list_ptr);
              LIST_ADVANCE(gu_in_port_list_ptr))
         {
            in_port_ptr = (gen_topo_input_port_t *)gu_in_port_list_ptr->ip_port_ptr;

            TRY(result,
                gen_topo_validate_trigger_non_trigger(topo_ptr->gu.log_id,
                                                      nontriggerable_ports_ptr,
                                                      num_groups,
                                                      triggerable_groups_ptr,
                                                      TRUE /* is_input */,
                                                      in_port_ptr->gu.cmn.index));
         }
      }

      if (nontriggerable_ports_ptr->out_port_grp_policy_ptr)
      {
         op_nontrigger_policy_present = TRUE;
         size_first_part += sizeof(fwk_extn_port_nontrigger_policy_t) * module_ptr->gu.max_output_ports;
         for (gu_output_port_list_t *gu_out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != gu_out_port_list_ptr);
              LIST_ADVANCE(gu_out_port_list_ptr))
         {
            out_port_ptr = (gen_topo_output_port_t *)gu_out_port_list_ptr->op_port_ptr;

            TRY(result,
                gen_topo_validate_trigger_non_trigger(topo_ptr->gu.log_id,
                                                      nontriggerable_ports_ptr,
                                                      num_groups,
                                                      triggerable_groups_ptr,
                                                      FALSE /* is_input */,
                                                      out_port_ptr->gu.cmn.index));
         }
      }
   }

   size_first_part = ALIGN_8_BYTES(size_first_part);

   size = size_first_part + num_groups * (sizeof(fwk_extn_port_trigger_group_t) +
                                          sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_input_ports +
                                          sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_output_ports);

   /* If non-trigger policy for input or output changes i.e:
      - present earlier but not raised now
      - not present earlier but raised now we need to free and recreate the memory
      to store the new info*/
   if (*tp_pptr)
   {
      bool_t nontrigger_policy_present =
         ((NULL != (*tp_pptr)->nontrigger_policy.in_port_grp_policy_ptr) ^ ip_nontrigger_policy_present) ||
         ((NULL != (*tp_pptr)->nontrigger_policy.out_port_grp_policy_ptr) ^ op_nontrigger_policy_present);

      if (((*tp_pptr)->num_trigger_groups != num_groups) || nontrigger_policy_present)
      {
         gen_topo_exit_island_temporarily(topo_ptr);
         MFREE_NULLIFY(*tp_pptr);
         any_change = TRUE;
      }
   }

   // If module removed both non-trigger and trigger policy then switch to fwk's default trigger policy.
   if ((NULL == nontriggerable_ports_ptr) && (NULL == triggerable_groups_ptr))
   {
      any_change = TRUE;
   }
   else if (!(*tp_pptr))
   {
      /**
       * this pointer: {gen_topo_trigger_policy_t,
       *                [conditional:max-input-ports for nontrigger policy on input],
       *                [conditional:max-out-ports for nontrigger policy on output],
       *                array of [fwk_extn_port_trigger_group_t],
       *                array of [fwk_extn_port_trigger_affinity_t for each input,
       *                 fwk_extn_port_trigger_affinity_t for each out]
       *                }
       */
      gen_topo_exit_island_temporarily(topo_ptr);
      MALLOC_MEMSET((*tp_pptr), gen_topo_trigger_policy_t, size, topo_ptr->heap_id, result);
   }

   if ((*tp_pptr))
   {
      ptr = (int8_t *)(*tp_pptr);
      ptr += sizeof(gen_topo_trigger_policy_t);

      if (NULL != nontriggerable_ports_ptr)
      {
         if (nontriggerable_ports_ptr->in_port_grp_policy_ptr)
         {
            (*tp_pptr)->nontrigger_policy.in_port_grp_policy_ptr = (fwk_extn_port_nontrigger_policy_t *)ptr;

            ptr += (sizeof(fwk_extn_port_nontrigger_policy_t) * module_ptr->gu.max_input_ports);

            for (gu_input_port_list_t *gu_in_port_list_ptr = module_ptr->gu.input_port_list_ptr;
                 (NULL != gu_in_port_list_ptr);
                 LIST_ADVANCE(gu_in_port_list_ptr))
            {
               in_port_ptr    = (gen_topo_input_port_t *)gu_in_port_list_ptr->ip_port_ptr;
               uint32_t index = in_port_ptr->gu.cmn.index;
               if ((*tp_pptr)->nontrigger_policy.in_port_grp_policy_ptr[index] !=
                   nontriggerable_ports_ptr->in_port_grp_policy_ptr[index])
               {
                  any_change = TRUE;
                  (*tp_pptr)->nontrigger_policy.in_port_grp_policy_ptr[index] =
                     nontriggerable_ports_ptr->in_port_grp_policy_ptr[index];
               }
            }
         }

         if (nontriggerable_ports_ptr->out_port_grp_policy_ptr)
         {
            (*tp_pptr)->nontrigger_policy.out_port_grp_policy_ptr = (fwk_extn_port_nontrigger_policy_t *)ptr;
            ptr += (sizeof(fwk_extn_port_nontrigger_policy_t) * module_ptr->gu.max_output_ports);

            for (gu_output_port_list_t *gu_out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
                 (NULL != gu_out_port_list_ptr);
                 LIST_ADVANCE(gu_out_port_list_ptr))
            {
               out_port_ptr   = (gen_topo_output_port_t *)gu_out_port_list_ptr->op_port_ptr;
               uint32_t index = out_port_ptr->gu.cmn.index;
               if ((*tp_pptr)->nontrigger_policy.out_port_grp_policy_ptr[index] !=
                   nontriggerable_ports_ptr->out_port_grp_policy_ptr[index])
               {
                  any_change = TRUE;
                  (*tp_pptr)->nontrigger_policy.out_port_grp_policy_ptr[index] =
                     nontriggerable_ports_ptr->out_port_grp_policy_ptr[index];
               }
            }
         }
      }

      ptr = (int8_t *)(*tp_pptr) + size_first_part;

      (*tp_pptr)->num_trigger_groups = num_groups;
      (*tp_pptr)->trigger_groups_ptr = (fwk_extn_port_trigger_group_t *)ptr;

      all_groups_policy_size = sizeof(fwk_extn_port_trigger_group_t) * num_groups;
      one_in_affinity_size   = sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_input_ports;
      all_in_affinity_size   = one_in_affinity_size * num_groups;
      one_out_affinity_size  = sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_output_ports;

      for (uint32_t g = 0; (g < num_groups) && triggerable_groups_ptr; g++)
      {
         fwk_extn_port_trigger_affinity_t *cur_in_port_grp_affinity_ptr =
            (fwk_extn_port_trigger_affinity_t *)(ptr + all_groups_policy_size + (g * one_in_affinity_size));
         fwk_extn_port_trigger_affinity_t *cur_out_grp_affinity_ptr =
            (fwk_extn_port_trigger_affinity_t *)(ptr + all_groups_policy_size + all_in_affinity_size +
                                                 (g * one_out_affinity_size));

         (*tp_pptr)->trigger_groups_ptr[g].in_port_grp_affinity_ptr  = cur_in_port_grp_affinity_ptr;
         (*tp_pptr)->trigger_groups_ptr[g].out_port_grp_affinity_ptr = cur_out_grp_affinity_ptr;

         size = sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_input_ports;

         if (any_change || memcmp((*tp_pptr)->trigger_groups_ptr[g].in_port_grp_affinity_ptr,
                                  triggerable_groups_ptr[g].in_port_grp_affinity_ptr,
                                  size))
         {
            any_change = TRUE;
            memscpy((*tp_pptr)->trigger_groups_ptr[g].in_port_grp_affinity_ptr,
                    size,
                    triggerable_groups_ptr[g].in_port_grp_affinity_ptr,
                    size);
         }

         size = sizeof(fwk_extn_port_trigger_affinity_t) * module_ptr->gu.max_output_ports;

         if (any_change || memcmp((*tp_pptr)->trigger_groups_ptr[g].out_port_grp_affinity_ptr,
                                  triggerable_groups_ptr[g].out_port_grp_affinity_ptr,
                                  size))
         {
            any_change = TRUE;

            memscpy((*tp_pptr)->trigger_groups_ptr[g].out_port_grp_affinity_ptr,
                    size,
                    triggerable_groups_ptr[g].out_port_grp_affinity_ptr,
                    size);
         }
      }

      if ((*tp_pptr)->port_trigger_policy != port_trigger_policy)
      {
         any_change                      = TRUE;
         (*tp_pptr)->port_trigger_policy = port_trigger_policy;
      }
   }

#ifdef TRIGGER_DEBUG
   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "Trigger Policy: Module 0x%08lX, trigger_type%u (1-data,2-signal). new trigger groups = %lu, new "
                   "port_trigger_policy%u, any_change%u",
                   module_ptr->gu.module_instance_id,
                   trigger_type,
                   gen_topo_get_num_trigger_groups(module_ptr, trigger_type),
                   gen_topo_get_port_trigger_policy(module_ptr, trigger_type),
                   any_change);
#endif

   /**
    * Need to handle trigger policy in fwk - to check which external ports to listen to since the policy changed.
    */
   if (any_change)
   {
      // Currently TGP change event is set for two reasons -
         //    1. Need to handle trigger policy in fwk - to check which external ports to listen to since the policy changed.
         //    2. If module enables TGP for first time or reverts to default TGP, result in change in number of data/signal TPM modules.
         //       And fwk may need to update module active states/check if pure ST can be used with this event.
         //
         // Event must be set to handle these scenarios -
         // 1. If not signal triggered cntr, we should handle the TGP change immediately. Else wait mask may never get updated and process frames never get triggered.
         //    Example: A src module changes ext out port tgp from blocked to optional, event is set to update output wait mask in cmd cntxt.
         //
         // 2. If signal triggered, module changes data TGP in command cntxt we need to handle event. Else cntr will delay start processing data triggers to next signal trigger.
         //    Example: Dam opens gate in ctrl trigger context, if ext output mask is not updated immediately data draining will be delayed until next interrrupt.
         //    Can result in increased KW history read latency issues.
         //
         // 3. If TGP activeness change, i.e module enables TGP or reverts to default TGP. Need to update module active/pure ST enablement.
         //
         // Event can be avoided to be set in the following case -
         // 1. In signal trigger cntr and process cntxt, and if there is a port TGP change. For example port changes from blocked to optional, viceversa. We do
         //    not have to set the TGP event. Since ch mask gets updated at the end of process. Eg: This is required for Acoustic Activity Detection usecase, when Acoustic Activity Detection opens dam gate, we
         //    can avoid setting event and exiting island.
      bool_t is_event_raised_in_cmd_context = (GEN_TOPO_INVALID_TRIGGER == topo_ptr->proc_context.curr_trigger);
      bool_t need_to_set_fwk_tgp_change_event = is_tgp_activeness_change || !topo_ptr->flags.is_signal_triggered ||
                                                is_event_raised_in_cmd_context;

      /* "trigger_policy_active" is a module level flag which is used while evaluating the trigger condition for
       * individual modules.
       *
       * -In non signal triggered container, whenever there is an active trigger policy module the container moves to
       * "any_trigger_policy".
       *  with this, topo process is called for any external input or output trigger and every time the module's trigger
       * conditions are checked.
       *
       * -In signal trigger container, by default topo process is called only when signal triggers even if there is an
       * active trigger policy module.
       *  this is needed to avoid processing (over running) the STM modules with just data trigger.
       *  But if the trigger policy module works on incoming timestamp, supports dropping the data and can make sure
       *  that it doesn't overrun the STM module then
       *  it can set the  "need_data_trigger_in_st" flag. With this ST topo triggers with both data and signal.
       */
      if (GEN_TOPO_DATA_TRIGGER == trigger_type)
      {
         if (!topo_ptr->flags.is_signal_triggered || (gen_topo_has_module_allowed_data_trigger_in_st_cntr(module_ptr)))
         {
            gen_topo_update_data_tpm_count(topo_ptr);
         }

         if (need_to_set_fwk_tgp_change_event)
         {
            GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, data_trigger_policy_change);
         }
      }
      else
      {
         if (need_to_set_fwk_tgp_change_event)
         {
            GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(topo_ptr, signal_trigger_policy_change);

            if(check_if_thin_topo_manager_is_initialized(topo_ptr))
            {
               thin_topo_reset_signal_tgp_flag(topo_ptr);
            }
         }
      }
   }

   CATCH(result, TOPO_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      TOPO_MSG_ISLAND(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "Trigger policy callback func failed");
   }

   return ar_result_to_capi_err(result);
}

void gen_topo_update_data_tpm_count(gen_topo_t *topo_ptr)
{
   topo_ptr->num_data_tpm = 0;
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         if (gen_topo_is_module_data_trigger_policy_active(module_ptr))
         {
            topo_ptr->num_data_tpm++;
         }
      }
   }

   TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                   DBG_LOW_PRIO,
                   "Number of active data trigger modules in this topo num_data_tpm %lu",
                   topo_ptr->num_data_tpm);

   THIN_TOPO_SET_EXIT_FLAG(topo_ptr, has_active_data_tgp_extn, (topo_ptr->num_data_tpm ? TRUE : FALSE));
}
