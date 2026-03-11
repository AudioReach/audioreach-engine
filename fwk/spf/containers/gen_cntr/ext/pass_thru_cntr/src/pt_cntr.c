/**
 * \file pt_cntr.c
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "pt_cntr_i.h"
#include "irm_cntr_prof_util.h"

// enable static to get accurate savings.
#define PT_CNTR_STATIC static

// #define PT_CNTR_TIME_PROP_ENABLE

// Pass thru container will be enabled only if USES_PASS_THRU_CONTAINER macro is enabled in the chip specific build
// configuration file.
bool_t is_pass_thru_container_supported()
{
   return TRUE;
}

PT_CNTR_STATIC ar_result_t pt_cntr_reset_proc_module_info(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr);

ar_result_t pt_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   pt_cntr_t             *me_ptr          = (pt_cntr_t *)base_ptr;
   pt_cntr_ext_in_port_t *ext_in_port_ptr = (pt_cntr_ext_in_port_t *)gu_ext_port_ptr;

   if (ext_in_port_ptr->topo_in_buf_ptr)
   {
      // return buffer
      topo_buf_manager_return_buf(&me_ptr->gc.topo, ext_in_port_ptr->topo_in_buf_ptr);
      ext_in_port_ptr->topo_in_buf_ptr = NULL;
   }

   return gen_cntr_deinit_ext_in_port(base_ptr, gu_ext_port_ptr);
}

ar_result_t pt_cntr_stm_fwk_extn_handle_enable(pt_cntr_t *me_ptr, gu_module_list_t *stm_mod_list_ptr)
{
   ar_result_t result   = AR_EOK;
   uint32_t    bit_mask = 0;
   INIT_EXCEPTION_HANDLING

   for (; NULL != stm_mod_list_ptr; LIST_ADVANCE(stm_mod_list_ptr))
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)stm_mod_list_ptr->module_ptr;
      // pass interrupt trigger signal only for the last module
      bool_t is_last_module = stm_mod_list_ptr->next_ptr ? FALSE : TRUE;

      // Enable the STM control
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Enabling CAPI signal triggered module 0x%lX, is_last_module:%lu",
                   module_ptr->gc.topo.gu.module_instance_id,
                   is_last_module);

      /* Property structure for stm trigger */
      typedef struct
      {
         capi_custom_property_t  cust_prop;
         capi_prop_stm_trigger_t trigger;
      } stm_trigger_t;

      stm_trigger_t stm_trigger;
      memset(&stm_trigger, 0, sizeof(stm_trigger_t));

      /* Populate the stm trigger */
      stm_trigger.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_STM_TRIGGER;

      /* Enable the stm*/
      typedef struct
      {
         capi_custom_property_t cust_prop;
         capi_prop_stm_ctrl_t   ctrl;
      } stm_ctrl_t;

      stm_ctrl_t stm_ctrl;
      stm_ctrl.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_STM_CTRL;
      stm_ctrl.ctrl.enable                 = TRUE;

      if (is_last_module)
      {
         /* Get the bit mask for stm signal, this signal trigger data processing
               on the container on every DMA interrupt or every time the timer expires */
         bit_mask = GEN_CNTR_TIMER_BIT_MASK;

         /* initialize the signal */
         TRY(result,
             cu_init_signal(&me_ptr->gc.cu,
                            bit_mask,
                            pt_cntr_signal_trigger,
                            &me_ptr->gc.st_module.trigger_signal_ptr));

         /* Initialize interrupt counter */
         me_ptr->gc.st_module.raised_interrupt_counter = 0;

         // set trigger only for the last module in FEF container
         stm_trigger.trigger.signal_ptr              = (void *)me_ptr->gc.st_module.trigger_signal_ptr;
         stm_trigger.trigger.raised_intr_counter_ptr = &me_ptr->gc.st_module.raised_interrupt_counter;
      }

      capi_prop_t set_props[] = {
         { CAPI_CUSTOM_PROPERTY,
           { (int8_t *)(&stm_trigger), sizeof(stm_trigger), sizeof(stm_trigger) },
           { FALSE, FALSE, 0 } },
         { CAPI_CUSTOM_PROPERTY, { (int8_t *)(&stm_ctrl), sizeof(stm_ctrl), sizeof(stm_ctrl) }, { FALSE, FALSE, 0 } }
      };

      capi_proplist_t set_proplist = { SIZE_OF_ARRAY(set_props), set_props };

      if (CAPI_EOK != (result = module_ptr->gc.topo.capi_ptr->vtbl_ptr->set_properties(module_ptr->gc.topo.capi_ptr,
                                                                                       &set_proplist)))
      {
         if (CAPI_EUNSUPPORTED == result)
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_HIGH_PRIO, "Unsupported stm cfg for input port 0x%x", result);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Failed to apply trigger and stm cfg for input port 0x%x",
                         result);
            if (me_ptr->gc.st_module.trigger_signal_ptr)
            {
               GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                            DBG_ERROR_PRIO,
                            "Failed to set the trigger signal on the stm 0x%lx. Destroying trigger signal",
                            result);

               // Stop listening to the mask
               cu_stop_listen_to_mask(&me_ptr->gc.cu, bit_mask);
               posal_signal_destroy(&me_ptr->gc.st_module.trigger_signal_ptr);
               me_ptr->gc.st_module.trigger_signal_ptr       = NULL;
               me_ptr->gc.st_module.raised_interrupt_counter = 0;
            }
            THROW(result, AR_EFAILED);
         }
      }
      else if (is_last_module)
      {
         capi_param_id_stm_latest_trigger_ts_ptr_t cfg = { 0 };

         uint32_t param_payload_size = (uint32_t)sizeof(capi_param_id_stm_latest_trigger_ts_ptr_t);
         result                      = gen_topo_capi_get_param(me_ptr->gc.topo.gu.log_id,
                                          module_ptr->gc.topo.capi_ptr,
                                          FWK_EXTN_PARAM_ID_LATEST_TRIGGER_TIMESTAMP_PTR,
                                          (int8_t *)&cfg,
                                          &param_payload_size);

         if (AR_DID_FAIL(result))
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Failed to get timestamp pointer 0x%x. Ignoring.",
                         result);
         }
         me_ptr->gc.st_module.st_module_ts_ptr   = cfg.ts_ptr;
         me_ptr->gc.st_module.update_stm_ts_fptr = cfg.update_stm_ts_fptr;
         me_ptr->gc.st_module.stm_ts_ctxt_ptr    = cfg.stm_ts_ctxt_ptr;

         if (NULL != cfg.ts_ptr)
         {
            me_ptr->gc.st_module.st_module_ts_ptr->is_valid = FALSE;
         }

         gen_cntr_set_stm_ts_to_module((gen_cntr_t *)me_ptr);
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }

   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_stm_fwk_extn_handle_disable_module(pt_cntr_t        *me_ptr,
                                                                      pt_cntr_module_t *module_ptr,
                                                                      bool_t            disable_timer_signal)
{

   ar_result_t result = AR_EOK;
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "Disabling CAPI signal triggered module 0x%lX",
                module_ptr->gc.topo.gu.module_instance_id);

   /* Disable the end point*/
   typedef struct
   {
      capi_custom_property_t cust_prop;
      capi_prop_stm_ctrl_t   ctrl;
   } stm_ctrl_t;

   stm_ctrl_t stm_ctrl;
   stm_ctrl.cust_prop.secondary_prop_id = FWK_EXTN_PROPERTY_ID_STM_CTRL;
   stm_ctrl.ctrl.enable                 = FALSE;

   capi_prop_t set_props[] = {
      { CAPI_CUSTOM_PROPERTY, { (int8_t *)(&stm_ctrl), sizeof(stm_ctrl), sizeof(stm_ctrl) }, { FALSE, FALSE, 0 } }
   };

   capi_proplist_t set_proplist = { SIZE_OF_ARRAY(set_props), set_props };

   if (CAPI_EOK !=
       (result = module_ptr->gc.topo.capi_ptr->vtbl_ptr->set_properties(module_ptr->gc.topo.capi_ptr, &set_proplist)))
   {
      if (CAPI_EUNSUPPORTED == result)
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_HIGH_PRIO, "Unsupported stm cfg for input port 0x%x", result);
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "Failed to apply trigger and stm cfg for input port 0x%x",
                      result);
         result = AR_EFAILED;
      }
   }

   if (disable_timer_signal)
   {
      if (me_ptr->gc.st_module.trigger_signal_ptr)
      {
         cu_deinit_signal(&me_ptr->gc.cu, &me_ptr->gc.st_module.trigger_signal_ptr);
         me_ptr->gc.st_module.raised_interrupt_counter    = 0;
         me_ptr->gc.st_module.processed_interrupt_counter = 0;
      }

      me_ptr->gc.st_module.st_module_ts_ptr   = NULL;
      me_ptr->gc.st_module.update_stm_ts_fptr = NULL;
      me_ptr->gc.st_module.stm_ts_ctxt_ptr    = NULL;

      gen_cntr_set_stm_ts_to_module((gen_cntr_t *)me_ptr);
   }

   return result;
}

ar_result_t pt_cntr_stm_fwk_extn_handle_disable(pt_cntr_t *me_ptr, gu_module_list_t *mod_list_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == mod_list_ptr)
   {
      // nothing to do
      return AR_EOK;
   }

   // disable last module first
   gu_module_list_t *last_mod_list_ptr = mod_list_ptr;
   while (last_mod_list_ptr->next_ptr)
   {
      LIST_ADVANCE(last_mod_list_ptr);
   }
   result |=
      pt_cntr_stm_fwk_extn_handle_disable_module(me_ptr, (pt_cntr_module_t *)last_mod_list_ptr->module_ptr, TRUE);

   for (; NULL != mod_list_ptr; LIST_ADVANCE(mod_list_ptr))
   {
      // skip the last module since its already disable
      if (mod_list_ptr->next_ptr)
      {
         result |=
            pt_cntr_stm_fwk_extn_handle_disable_module(me_ptr, (pt_cntr_module_t *)mod_list_ptr->module_ptr, FALSE);
      }
   }

   return result;
}

/**
 * Module is active if it and its ports are started + capi etc are created.
 */
bool_t pt_cntr_is_module_active(gen_topo_module_t *module_ptr, bool_t need_to_ignore_state)
{
   // Firtly check if modules input && output triggers are satisfied.
   //    1. If the module is acting as source input trigger by default satisified, same logic applies for sink as well.
   //    2. If the module is not source/sink, then atleast one output and input trigger must satisfied.

   // if a module cannot have outputs, or if module currently doesnt have any outputs connected and its min outputs are
   // zero.
   bool_t atleast_one_output_trigger_satisfied =
      (0 == module_ptr->gu.max_output_ports) ||
      ((0 == module_ptr->gu.num_output_ports) && (0 == module_ptr->gu.min_output_ports));

   // if module cannot have inputs, or if module currently doesnt have any inputs connected and its min inputs are
   // zero.
   bool_t atleast_one_input_trigger_satisfied =
      (0 == module_ptr->gu.max_input_ports) ||
      ((0 == module_ptr->gu.num_input_ports) && (0 == module_ptr->gu.min_input_ports));

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

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
         // If the input port is not started or doesn't have a buffer (media-fmt prop didn't happen)
         // then process cannot be called on the module.
         if ((TOPO_PORT_STATE_STARTED != in_port_ptr->common.state))
         {
            continue;
         }
         else if (FALSE == in_port_ptr->common.flags.is_mf_valid)
         {
            continue;
         }
         else
         {
            atleast_one_input_trigger_satisfied = TRUE;
            break;
         }
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if (TOPO_PORT_STATE_STARTED != out_port_ptr->common.state)
         {
            continue;
         }
         else if (FALSE == out_port_ptr->common.flags.is_mf_valid)
         {
            continue;
         }
         else
         {
            atleast_one_output_trigger_satisfied = TRUE;
            break;
         }
      }
   }

   bool_t active_as_per_state =
      need_to_ignore_state || (atleast_one_input_trigger_satisfied && atleast_one_output_trigger_satisfied);
   if (active_as_per_state || module_ptr->flags.need_stm_extn)
   {
      if (module_ptr->bypass_ptr || !module_ptr->capi_ptr || !module_ptr->flags.disabled)
      {
         return TRUE;
      }
   }
   return FALSE;
}

/** Update the list of modules that needs to called in the process context. The function also reassign any topo buffers
 * if required.*/
ar_result_t pt_cntr_update_module_process_list(pt_cntr_t *me_ptr)
{
   ar_result_t result   = AR_EOK;
   gen_topo_t *topo_ptr = &me_ptr->gc.topo;

   INIT_EXCEPTION_HANDLING

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Updating module process list %lu, %lu, %lu",
                (NULL != me_ptr->module_proc_list_ptr),
                (NULL != me_ptr->src_module_list_ptr),
                (NULL != me_ptr->sink_module_list_ptr));

   // delete the exisiting list
   TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->module_proc_list_ptr, TRUE));
   TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->src_module_list_ptr, TRUE));
   TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->sink_module_list_ptr, TRUE));

   // todo: for background thread need to use sorted_module_list_ptr/started_sorted_module_list_ptr depending on
   // context. check with Harsh

   if (!me_ptr->gc.topo.gu.sorted_module_list_ptr)
   {
      // nothing to do if modules are not sorted
      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "Warning! GU Sorted module list is not set. proc list cannot be updated.");
      return AR_EOK;
   }

   // iterate through sorted module list
   for (gu_module_list_t *sorted_module_list_ptr = me_ptr->gc.topo.gu.sorted_module_list_ptr;
        NULL != sorted_module_list_ptr;
        LIST_ADVANCE(sorted_module_list_ptr))
   {
      pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)sorted_module_list_ptr->module_ptr;

      GEN_CNTR_MSG(topo_ptr->gu.log_id,
                   DBG_HIGH_PRIO,
                   "module 0x%lx is being sorted",
                   module_ptr->gc.topo.gu.module_instance_id);

      pt_cntr_reset_proc_module_info(me_ptr, module_ptr);

      // check modules default trigger policy
      if (FALSE == pt_cntr_is_module_active((gen_topo_module_t *)module_ptr, FALSE /** need to ignore state */))
      {
         // dangling source/sink module cannot be added to the list
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! Module 0x%lx is not active, hence not adding to the process list",
                      module_ptr->gc.topo.gu.module_instance_id);
         continue;
      }

      if (module_ptr->gc.topo.gu.flags.is_sink)
      {
         // add to list of sink modules
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&me_ptr->sink_module_list_ptr,
                                  module_ptr,
                                  me_ptr->gc.cu.heap_id,
                                  TRUE));

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "module 0x%lx added to the sink module process list",
                      module_ptr->gc.topo.gu.module_instance_id);
      }
      else if (module_ptr->gc.topo.gu.flags.is_source)
      {
         // add to list of src modules
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&me_ptr->src_module_list_ptr,
                                  module_ptr,
                                  me_ptr->gc.cu.heap_id,
                                  TRUE));

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "module 0x%lx added to the source module process list",
                      module_ptr->gc.topo.gu.module_instance_id);
      }
      else
      {
         TRY(result,
             spf_list_insert_tail((spf_list_node_t **)&me_ptr->module_proc_list_ptr,
                                  module_ptr,
                                  me_ptr->gc.cu.heap_id,
                                  TRUE));

         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "module 0x%lx added to the process list",
                      module_ptr->gc.topo.gu.module_instance_id);
      }
   }

   if ((NULL == me_ptr->module_proc_list_ptr) && (NULL == me_ptr->sink_module_list_ptr) &&
       (NULL == me_ptr->src_module_list_ptr))
   {
      GEN_CNTR_MSG(topo_ptr->gu.log_id, DBG_HIGH_PRIO, "Warning! Module process list is empty.");
   }

   GEN_CNTR_MSG(topo_ptr->gu.log_id,
                DBG_HIGH_PRIO,
                "Updated sorted module list 0x%lx, 0x%lx, 0x%lx",
                (me_ptr->module_proc_list_ptr),
                (me_ptr->src_module_list_ptr),
                (me_ptr->sink_module_list_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, topo_ptr->gu.log_id)
   {
      TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->module_proc_list_ptr, TRUE));
      TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->src_module_list_ptr, TRUE));
      TRY(result, spf_list_delete_list((spf_list_node_t **)&me_ptr->sink_module_list_ptr, TRUE));
   }

   return result;
}

PT_CNTR_STATIC ar_result_t pt_cntr_destroy_module(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr)
{
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "Destroying module resources 0x%lx ",
                module_ptr->gc.topo.gu.module_instance_id);

   // destroy sdata array pointers
   // single memory blob is used to allocate and assign "in_port_sdata_pptr" and "out_port_sdata_pptr"
   // hence free on in_port_sdata_pptr works for SISO/source modules, for sink modules in_port_sdata_pptr will be NULL
   // hence free "out_port_sdata_pptr"
   if (module_ptr->in_port_sdata_pptr)
   {
      MFREE_NULLIFY(module_ptr->in_port_sdata_pptr);
   }
   else if (module_ptr->out_port_sdata_pptr)
   {
      MFREE_NULLIFY(module_ptr->out_port_sdata_pptr);
   }

   return AR_EOK;
}

/**
 * if flag "b_destroy_all_modules" is set then all modules are destroyed.
 * if flag "b_destroy_all_modules" is unset then modules within subgraphs marked for close are destroyed.
 */
ar_result_t pt_cntr_destroy_modules_resources(pt_cntr_t *me_ptr, bool_t b_destroy_all_modules)
{
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_LOW_PRIO, "Destroying module resources %lu", b_destroy_all_modules);
   gu_sg_list_t *sg_list_ptr = get_gu_ptr_for_current_command_context(&me_ptr->gc.topo.gu)->sg_list_ptr;
   for (; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      // If sg_list is not providing then close all SGs.
      if (b_destroy_all_modules || GU_STATUS_CLOSING == sg_list_ptr->sg_ptr->gu_status)
      {
         for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
              LIST_ADVANCE(module_list_ptr))
         {
            pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;
            pt_cntr_destroy_module(me_ptr, module_ptr);
         }
      }
   }

   return AR_EOK;
}

PT_CNTR_STATIC ar_result_t pt_cntr_reset_proc_module_info(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr)
{
   // store process function in the module strcuture.
   if (module_ptr->gc.topo.bypass_ptr)
   {
      module_ptr->process = pt_cntr_bypass_module_process;
   }
   else
   {
      module_ptr->process = module_ptr->gc.topo.capi_ptr->vtbl_ptr->process;
   }

   // update the flags related to attached module, port states. These flags are used to skip output post process
   // in the module process context.
   bool_t has_attached_module            = FALSE;
   bool_t has_stopped_port               = FALSE;
   for (gu_output_port_list_t *op_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr; NULL != op_list_ptr;
        LIST_ADVANCE(op_list_ptr))
   {
      pt_cntr_output_port_t *out_port_ptr = (pt_cntr_output_port_t *)op_list_ptr->op_port_ptr;

      has_attached_module = out_port_ptr->gc.gu.attached_module_ptr ? TRUE : has_attached_module;
      has_stopped_port    = (TOPO_PORT_STATE_STOPPED == out_port_ptr->gc.common.state) ? TRUE : has_stopped_port;
   }

   /** Reset output port related flags */
   module_ptr->flags.has_attached_module = has_attached_module;
   module_ptr->flags.has_stopped_port    = has_stopped_port;

#ifdef VERBOSE_DEBUGGING
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_HIGH_PRIO,
                "Module 0x%lX has_attached_module:%lu has_stopped_port:%lu ",
                module_ptr->gc.topo.gu.module_instance_id,
                (TRUE == module_ptr->flags.has_attached_module),
                (TRUE == module_ptr->flags.has_stopped_port));
#endif
   return AR_EOK;
}

ar_result_t pt_cntr_realloc_scratch_sdata_arr(pt_cntr_t *me_ptr, pt_cntr_module_t *module_ptr)
{
   if (module_ptr->in_port_sdata_pptr)
   {
      MFREE_NULLIFY(module_ptr->in_port_sdata_pptr);
   }

   if (module_ptr->out_port_sdata_pptr)
   {
      MFREE_NULLIFY(module_ptr->out_port_sdata_pptr);
   }

   uint32_t total_malloc_size = (module_ptr->gc.topo.gu.max_input_ports + module_ptr->gc.topo.gu.max_output_ports) *
                                sizeof(capi_stream_data_v2_t *);

   // if num inputs and outputs are zero no need to malloc
   if(0 == total_malloc_size)
   {
      return AR_EOK;
   }

   int8_t *blob_ptr = (int8_t *)posal_memory_malloc(total_malloc_size, me_ptr->gc.cu.heap_id);
   if (NULL == blob_ptr)
   {
      return AR_EFAILED;
   }
   memset(blob_ptr, 0, total_malloc_size);

   if (module_ptr->gc.topo.gu.max_input_ports)
   {
      module_ptr->in_port_sdata_pptr = (capi_stream_data_v2_t **)blob_ptr;
      blob_ptr += (module_ptr->gc.topo.gu.max_input_ports * sizeof(capi_stream_data_v2_t *));
   }

   if (module_ptr->gc.topo.gu.max_output_ports)
   {
      module_ptr->out_port_sdata_pptr = (capi_stream_data_v2_t **)blob_ptr;
   }
   return AR_EOK;
}

capi_err_t pt_cntr_capi_event_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr)
{
   capi_err_t         result     = CAPI_EOK;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)(context_ptr);
   gen_topo_t        *topo_ptr   = module_ptr->topo_ptr;

   switch (id)
   {
      /** Events ignored by Pass thru container */
      case CAPI_EVENT_METADATA_AVAILABLE:
      case CAPI_EVENT_DYNAMIC_INPLACE_CHANGE:
      case CAPI_EVENT_ISLAND_VOTE:
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_HIGH_PRIO,
                      "Warning! Pass thru container ignored event 0x%lx raised by module 0x%lX",
                      id,
                      module_ptr->gu.module_instance_id);
         return CAPI_EOK;
      }
      case CAPI_EVENT_GET_DLINFO:
      case CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE:
      case CAPI_EVENT_KPPS:
      case CAPI_EVENT_BANDWIDTH:
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2:
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED:
      case CAPI_EVENT_PORT_DATA_THRESHOLD_CHANGE:
      case CAPI_EVENT_GET_LIBRARY_INSTANCE:
      case CAPI_EVENT_ALGORITHMIC_DELAY:
      case CAPI_EVENT_HW_ACCL_PROC_DELAY:
      case CAPI_EVENT_DEINTERLEAVED_UNPACKED_V2_SUPPORTED:
      case CAPI_EVENT_PROCESS_STATE:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT_V2:
      {
         return gen_topo_capi_callback(context_ptr, id, event_info_ptr);
      }
      case CAPI_EVENT_DATA_TO_DSP_SERVICE:
      {
         capi_buf_t *payload = &event_info_ptr->payload;
         if (payload->actual_data_len < sizeof(capi_event_data_to_dsp_service_t))
         {
            TOPO_MSG_ISLAND(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX: Error in callback function. The actual size %lu is less than the required "
                            "size "
                            "%lu for id %lu.",
                            module_ptr->gu.module_instance_id,
                            payload->actual_data_len,
                            sizeof(capi_event_data_to_dsp_service_t),
                            (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }
         // Some events can be handled within topo itself
         capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

         switch (dsp_event_ptr->param_id)
         {
            case FWK_EXTN_EVENT_ID_ISLAND_EXIT:
            case FWK_EXTN_SYNC_EVENT_ID_DATA_PORT_ACTIVITY_STATE: // GC ignores this event anyways
            {
               GEN_CNTR_MSG(topo_ptr->gu.log_id,
                            DBG_HIGH_PRIO,
                            "Warning! Module 0x%lX: Pass thru container ignored CAPI_EVENT_DATA_TO_DSP_SERVICE event "
                            "param id 0x%lx ",
                            module_ptr->gu.module_instance_id,
                            dsp_event_ptr->param_id);
               return CAPI_EOK;
            }
            case INTF_EXTN_EVENT_ID_IMCL_RECURRING_BUF_INFO:
            case INTF_EXTN_EVENT_ID_IMCL_OUTGOING_DATA:
            case INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP:
            case INTF_EXTN_EVENT_ID_PORT_DS_STATE:
            case INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY:
            case INTF_EXTN_EVENT_ID_MODULE_BUFFER_ACCESS_ENABLE:
            case FWK_EXTN_DM_EVENT_ID_DISABLE_DM:
            {
               return gen_topo_capi_callback(context_ptr, id, event_info_ptr);
            }
            default:
            {
               GEN_CNTR_MSG(topo_ptr->gu.log_id,
                            DBG_ERROR_PRIO,
                            "Module 0x%lX: Raised unsupported CAPI_EVENT_DATA_TO_DSP_SERVICE event param id 0x%lx ",
                            module_ptr->gu.module_instance_id,
                            dsp_event_ptr->param_id);
               return CAPI_EFAILED;
            }
         }

         return CAPI_EOK;
      }
      default:
      {
         GEN_CNTR_MSG(topo_ptr->gu.log_id,
                      DBG_ERROR_PRIO,
                      "Module 0x%lX: Raised unsupported event id %lu ",
                      module_ptr->gu.module_instance_id,
                      id);
         return CAPI_EFAILED;
      }
   }

   return result;
}

/*** Error check function - end */

ar_result_t pt_cntr_handle_module_buffer_access_event(gen_topo_t        *topo_ptr,
                                                      gen_topo_module_t *mod_ptr,
                                                      capi_event_info_t *event_info_ptr)
{
   capi_buf_t                       *payload       = &event_info_ptr->payload;
   capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);
   pt_cntr_module_t                 *module_ptr    = (pt_cntr_module_t *)mod_ptr;

   if (!event_info_ptr->port_info.is_valid)
   {
      TOPO_MSG(topo_ptr->gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Error in module buffer access event callback function. port info invalid:%lu "
               "is_input:%lu",
               module_ptr->gc.topo.gu.module_instance_id,
               event_info_ptr->port_info.is_valid,
               event_info_ptr->port_info.is_input_port);
      return AR_EFAILED;
   }

   intf_extn_event_id_module_buffer_access_enable_t *cfg_ptr =
      (intf_extn_event_id_module_buffer_access_enable_t *)dsp_event_ptr->payload.data_ptr;
   bool_t is_enable = (TRUE == cfg_ptr->enable);

   if (event_info_ptr->port_info.is_input_port)
   {
      // get output port id by the port index
      gen_topo_input_port_t *input_port_ptr =
         (gen_topo_input_port_t *)gu_find_input_port_by_index((gu_module_t *)module_ptr,
                                                              event_info_ptr->port_info.port_index);
      if (!input_port_ptr)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in module buffer access event callback function. invalid input port index %lu ",
                  module_ptr->gc.topo.gu.module_instance_id,
                  event_info_ptr->port_info.port_index);
         return AR_EFAILED;
      }

      intf_extn_input_buffer_manager_cb_info_t *ip_cb_info_ptr =
         (intf_extn_input_buffer_manager_cb_info_t *)(dsp_event_ptr->payload.data_ptr +
                                                      sizeof(intf_extn_event_id_module_buffer_access_enable_t));

      input_port_ptr->common.flags.supports_buffer_resuse_extn =
         is_enable ? GEN_TOPO_MODULE_INPUT_BUF_ACCESS : GEN_TOPO_MODULE_BUF_ACCESS_INVALID;

      module_ptr->buffer_mgr_cb_handle = (TRUE == is_enable) ? ip_cb_info_ptr->buffer_mgr_cb_handle : (uint32_t)NULL;
      module_ptr->get_input_buf_fn     = (TRUE == is_enable) ? ip_cb_info_ptr->get_input_buf_fn : NULL;
   }
   else
   {
      // get output port id by the port index
      gen_topo_output_port_t *output_port_ptr =
         (gen_topo_output_port_t *)gu_find_output_port_by_index((gu_module_t *)module_ptr,
                                                                event_info_ptr->port_info.port_index);
      if (!output_port_ptr)
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in module buffer access event callback function. invalid output port index %lu ",
                  module_ptr->gc.topo.gu.module_instance_id,
                  event_info_ptr->port_info.port_index);
         return AR_EFAILED;
      }

      intf_extn_output_buffer_manager_cb_info_t *op_cb_info_ptr =
         (intf_extn_output_buffer_manager_cb_info_t *)(dsp_event_ptr->payload.data_ptr +
                                                       sizeof(intf_extn_event_id_module_buffer_access_enable_t));

      output_port_ptr->common.flags.supports_buffer_resuse_extn =
         (TRUE == is_enable) ? GEN_TOPO_MODULE_OUTPUT_BUF_ACCESS : GEN_TOPO_MODULE_BUF_ACCESS_INVALID;

      module_ptr->buffer_mgr_cb_handle = (TRUE == is_enable) ? op_cb_info_ptr->buffer_mgr_cb_handle : (uint32_t)NULL;
      module_ptr->return_output_buf_fn = (TRUE == is_enable) ? op_cb_info_ptr->return_output_buf_fn : NULL;
   }

   TOPO_MSG(topo_ptr->gu.log_id,
            DBG_LOW_PRIO,
            "Module 0x%lX: Module support buffer access:%lu on the port index%lu is_input:%lu callbacks validity:(%lu, "
            "%lu) ",
            module_ptr->gc.topo.gu.module_instance_id,
            (TRUE == is_enable),
            event_info_ptr->port_info.port_index,
            event_info_ptr->port_info.is_input_port,
            (NULL != module_ptr->get_input_buf_fn),
            (NULL != module_ptr->return_output_buf_fn));

   return AR_EOK;
}
