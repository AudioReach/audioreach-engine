/**
 * \file pt_cntr_cmd_handler.c
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "pt_cntr_i.h"
#include "irm_cntr_prof_util.h"

// enable static to get accurate savings.
#define PT_CNTR_STATIC static

/* CU call back functions for container specific handling */
// clang-format off
const cu_cntr_vtable_t pt_cntr_cntr_funcs = {
   .port_data_thresh_change              = gen_cntr_handle_port_data_thresh_change,
   .aggregate_kpps_bw                    = gen_cntr_aggregate_kpps_bw,

   .operate_on_subgraph                  = gen_cntr_operate_on_subgraph,
   .operate_on_subgraph_async            = gen_cntr_operate_on_subgraph_async,

   .post_operate_on_subgraph             = gen_cntr_post_operate_on_subgraph,

   .set_get_cfg                          = gen_cntr_set_get_cfg_util,
   .register_events                      = gen_cntr_register_events_utils,

   .init_ext_in_port                     = pt_cntr_init_ext_in_port,
   .deinit_ext_in_port                   = pt_cntr_deinit_ext_in_port,
   .operate_on_ext_in_port               = gen_cntr_operate_on_ext_in_port,
   .post_operate_on_ext_in_port          = pt_cntr_post_operate_on_ext_in_port,
   .post_operate_on_ext_out_port         = pt_cntr_post_operate_on_ext_out_port,
   .input_media_format_received          = gen_cntr_input_media_format_received,

   .init_ext_out_port                    = pt_cntr_init_ext_out_port,
   .deinit_ext_out_port                  = gen_cntr_deinit_ext_out_port,
   .operate_on_ext_out_port              = gen_cntr_operate_on_ext_out_port,
   .ext_out_port_apply_pending_media_fmt = gen_cntr_ext_out_port_apply_pending_media_fmt,
   .ext_out_port_recreate_bufs           = gen_cntr_ext_out_port_recreate_bufs,

   .init_ext_ctrl_port                     = cu_init_ext_ctrl_port,
   .deinit_ext_ctrl_port                   = cu_deinit_ext_ctrl_port,
   .operate_on_ext_ctrl_port               = cu_operate_on_ext_ctrl_port,
   .connect_ext_ctrl_port                  = cu_connect_ext_ctrl_port,

   .apply_downgraded_state_on_output_port    = gen_cntr_apply_downgraded_state_on_output_port,
   .apply_downgraded_state_on_input_port     = gen_cntr_apply_downgraded_state_on_input_port,

   .destroy_all_metadata                     = gen_topo_destroy_all_metadata,

   .handle_proc_duration_change              = gen_cntr_handle_proc_duration_change,
   .update_path_delay                        = cu_update_path_delay,
   .aggregate_hw_acc_proc_delay              = gen_cntr_aggregate_hw_acc_proc_delay,
   .vote_against_island                      = gen_cntr_vote_against_island,
   .exit_island_temporarily                  = gen_ctr_exit_island_temporarily,

   .get_additional_ext_in_port_delay_cu_cb   = gen_cntr_get_additional_ext_in_port_delay_cu_cb,
   .get_additional_ext_out_port_delay_cu_cb  = gen_cntr_get_additional_ext_out_port_delay_cu_cb,

   .check_bump_up_thread_priority            = gen_cntr_check_bump_up_thread_priority,

   .dcm_topo_set_param 						   = gen_cntr_dcm_topo_set_param,
   .handle_cntr_period_change 				= gen_cntr_handle_cntr_period_change,

   .initiate_duty_cycle_island_entry         = gen_cntr_initiate_duty_cycle_island_entry,
   .initiate_duty_cycle_island_exit          = gen_cntr_initiate_duty_cycle_island_exit,
};

const topo_to_cntr_vtable_t topo_to_pt_cntr_vtable = {
   .clear_eos                                   = gen_cntr_clear_eos,
   .raise_data_to_dsp_service_event             = gen_cntr_raise_data_to_dsp_service_event,
   .raise_data_from_dsp_service_event           = gen_cntr_raise_data_from_dsp_service_event,
   .raise_data_to_dsp_client_v2                 = gen_cntr_event_data_to_dsp_client_v2_topo_cb,
   .handle_capi_event                           = gen_cntr_handle_capi_event,
   .set_pending_out_media_fmt                   = NULL,
   .check_apply_ext_out_ports_pending_media_fmt = NULL,
   .set_ext_in_port_prev_actual_data_len        = NULL,
   .update_input_port_max_samples               = NULL,
   .ext_in_port_get_buf_len                     = NULL,
   .set_propagated_prop_on_ext_output           = gen_cntr_set_propagated_prop_on_ext_output,
   .set_propagated_prop_on_ext_input            = gen_cntr_set_propagated_prop_on_ext_input,
   .ext_out_port_has_buffer                     = gen_cntr_ext_out_port_has_buffer,
   .ext_in_port_has_data_buffer                 = gen_cntr_ext_in_port_has_data_buffer,
   .algo_delay_change_event                     = gen_cntr_handle_algo_delay_change_event,
   .handle_frame_done                           = gen_cntr_handle_frame_done,
   .ext_in_port_has_enough_data                 = NULL,
   .ext_out_port_get_ext_buf                    = NULL,
   .ext_in_port_dfg_eos_left_port               = NULL,
   .ext_in_port_clear_timestamp_discontinuity   = NULL,
   .create_module                               = pt_cntr_create_module,
   .destroy_module                              = gen_cntr_destroy_module,
   .check_insert_missing_eos_on_next_module     = pt_cntr_check_insert_missing_eos_on_next_module,
   .update_icb_info                             = gen_cntr_update_icb_info,
   .vote_against_island                         = gen_cntr_vote_against_island_topo,

   .aggregate_ext_in_port_delay                 = gen_cntr_aggregate_ext_in_port_delay_topo_cb,
   .aggregate_ext_out_port_delay                = gen_cntr_aggregate_ext_out_port_delay_topo_cb,
   .check_for_error_print                       = gen_cntr_check_for_err_print,

   .notify_ts_disc_evt                          = gen_cntr_notify_timestamp_discontinuity_event_cb,
   .module_buffer_access_event                  = pt_cntr_handle_module_buffer_access_event,

   .check_if_any_ext_in_has_to_preserve_prebuffer = NULL,
};
// clang-format on

// callback handler for peer port state operation on external input
ar_result_t pt_cntr_post_operate_on_ext_in_port(void                      *base_ptr,
                                                uint32_t                   sg_ops,
                                                gu_ext_in_port_t         **ext_in_port_pptr,
                                                spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)*ext_in_port_pptr;
   gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   if (TOPO_SG_OP_STOP & sg_ops)
   {
      // todo: check if all ports are stopped, in that case push zeros from the next modules input
   }
   else if (TOPO_SG_OP_START & sg_ops)
   {
      // todo: check if the module's first input is getting started, then stop pushing zeros from the next module's
      // input
   }

   /* If ext input port receives both a self and peer stop/flush from upstream (any order)
    * eos need not be set since next DS container will anyway get the eos because of this self stop
    *
    * EOS needs to be inserted only if this port is started and it not already at gap
    */
   if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
       TOPO_PORT_STATE_STARTED == in_port_ptr->common.state)
   {
      if ((TOPO_SG_OP_STOP & sg_ops)) // || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))
      {
         // no need to insert EOS since ext input underruns as long as port is started
      }
      else if (TOPO_SG_OP_SUSPEND & sg_ops)
      {
         // Currently gen cntr doesnt push zeros when US is suspended, need to check SUSPEND state before pushing zeros
         // in process context else it will create data discontinuity (low priority for now)
      }
   }

   return result;
}

// callback handler for peer port state operation on external output
ar_result_t pt_cntr_post_operate_on_ext_out_port(void                      *base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_out_port_t        **ext_out_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t              result           = AR_EOK;
   pt_cntr_t               *me_ptr           = (pt_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)*ext_out_port_pptr;

   // if SG/external output is starting immediately update the proc list
   // todo: add explanation why only ops/sg are checked   // if SG/external output is stopped immediately update the
   // proc list, because it can alter the module proc list and update the module buffer assignment as well todo: need to
   // update the proc list when the peer port state proapgation command is recieved as well.
   if ((TOPO_SG_OP_START | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      CU_MSG(me_ptr->gc.cu.gu_ptr->log_id,
             DBG_LOW_PRIO,
             "cu_handle_sg_mgmt_cmd. sg_op %lu hanlding post operate on external output id 0x%lx",
             sg_ops,
             ext_out_port_ptr->gu.int_out_port_ptr->cmn.id);

      result |= pt_cntr_update_module_process_list(me_ptr);
      result |= pt_cntr_assign_port_buffers(me_ptr);
   }

   return result;
}

/**
 * If internal eos is still at an input port (not at gap) of a module while that input port is closed, we need to ensure
 * that eos gets propagated to the next module, otherwise data flow gap is never communicated downstream.
 */
ar_result_t pt_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr)
{
   ar_result_t        result     = AR_EOK;
   gen_cntr_t        *me_ptr     = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr;

#if 0
   // if module owns the md propagation then it should raise eos after port op close.
   /* but if all the input ports are closed for the first module,
    * process will not be called and module won't be able to put eos on output buffer. */
   if (!gen_topo_fwk_owns_md_prop(module_ptr))
   {
      return result;
   }
#endif

   // Only move eof/dfg if all of a module's input ports are either closing or already in at-gap state.
   // If there are some port remained in data-flow state then module is responsible for pushing out eos
   // on the next process call based on close port_operation handling.

   // todo: a mimo module which has one of the output port as source will break with EOS insertion.

   // don't need to manually insert eos here if module itself is closing.
   bool_t is_module_closing = FALSE;
   cu_is_module_closing(&(me_ptr->cu), &(module_ptr->gu), &is_module_closing);
   if (is_module_closing)
   {
      return result;
   }

   for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; in_port_list_ptr;
        LIST_ADVANCE(in_port_list_ptr))
   {
      gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;
      if (TOPO_DATA_FLOW_STATE_FLOWING == in_port_ptr->common.data_flow_state)
      {
         bool_t is_port_closing = FALSE;
         cu_is_in_port_closing(&(me_ptr->cu), &(in_port_ptr->gu), &is_port_closing);
         if (!is_port_closing)
         { // if port is not closing then can't insert eos. module should handle from process call.
            return result;
         }
      }
   }

   // reset the module to clear flushing eos or internal metadata.
   gen_topo_reset_module(topo_ptr, module_ptr);

   // do not insert any EOS in pass thru container
   // front container has a different mechanism to continue to underrn on the connected input port

   return result;
}

static ar_result_t pt_cntr_check_n_validate_module_static_properties(pt_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t num_stm_modules                    = 0;
   uint32_t num_modules_not_supporting_md_extn = 0;
   for (gu_sg_list_t *sg_list_ptr = me_ptr->gc.topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->gc.topo.flags.need_stm_extn)
         {
            num_stm_modules++;
         }

         // todo: add new API for MFC to disable requires data buffering dynamcially which will be implemented only by
         // Pass thru container. other containers always treat MFC as requires_data_buf modules.
         if (module_ptr->gc.topo.flags.requires_data_buf)
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Warning! Module 0x%lx requires data buffering may not work in Pass Thru container.",
                         module_ptr->gc.topo.gu.module_instance_id);
         }

         // check and disable trigger policy extension if it was enabled at init.
         if (module_ptr->gc.topo.flags.need_trigger_policy_extn)
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Warning! Pass Thru container trigger policy extn not supported, disabling the extn for "
                         "Module 0x%lx",
                         module_ptr->gc.topo.gu.module_instance_id);

            // reset extn callback info
            fwk_extn_param_id_trigger_policy_cb_fn_t handler;
            memset(&handler, 0, sizeof(fwk_extn_param_id_trigger_policy_cb_fn_t));

            gen_topo_populate_trigger_policy_extn_vtable((gen_topo_module_t *)module_ptr, &handler);
            result |= gen_topo_capi_set_param(me_ptr->gc.topo.gu.log_id,
                                              module_ptr->gc.topo.capi_ptr,
                                              FWK_EXTN_PARAM_ID_TRIGGER_POLICY_CB_FN,
                                              (int8_t *)&handler,
                                              sizeof(handler));

            // destory if trigger policy was already raised.
            gen_topo_destroy_trigger_policy((gen_topo_module_t *)module_ptr);

            // disable the trigger policy extension
            module_ptr->gc.topo.flags.need_trigger_policy_extn = FALSE;
         }

         if (FALSE == module_ptr->gc.topo.flags.supports_metadata)
         {
            num_modules_not_supporting_md_extn++;
         }

         if (module_ptr->gc.topo.flags.supports_module_allow_duty_cycling ||
             module_ptr->gc.topo.flags.need_soft_timer_extn || module_ptr->gc.topo.flags.need_sync_extn)
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Warning! Module 0x%lx requires unsupported extns duty_cycling:%lu soft_timer:%lu "
                         "sync_extn:%lu ",
                         module_ptr->gc.topo.gu.module_instance_id,
                         module_ptr->gc.topo.flags.supports_module_allow_duty_cycling,
                         module_ptr->gc.topo.flags.need_soft_timer_extn,
                         module_ptr->gc.topo.flags.need_sync_extn);
            THROW(result, AR_EFAILED);
         }
      }
   }

   me_ptr->flags.supports_md_propagation = num_modules_not_supporting_md_extn > 1 ? FALSE : TRUE;
   if (FALSE == me_ptr->flags.supports_md_propagation)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Warning! Pass Thru cntr cannot propagate MD since it has num modules %lu that doesnt support.",
                   num_modules_not_supporting_md_extn);
   }

   if (num_stm_modules > 1)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_HIGH_PRIO, "Warning! Num STM modules %lu > 1", num_stm_modules);
   }
   else if (0 == num_stm_modules)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id, DBG_ERROR_PRIO, "STM module not found, failing...");
      THROW(result, AR_EFAILED);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t pt_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr)
{
   ar_result_t            result          = AR_EOK;
   pt_cntr_ext_in_port_t *ext_in_port_ptr = (pt_cntr_ext_in_port_t *)gu_ext_port_ptr;
   pt_cntr_input_port_t  *in_port_ptr     = (pt_cntr_input_port_t *)ext_in_port_ptr->gc.gu.int_in_port_ptr;
   pt_cntr_module_t      *module_ptr      = (pt_cntr_module_t *)in_port_ptr->gc.gu.cmn.module_ptr;

   result = gen_cntr_init_ext_in_port(base_ptr, gu_ext_port_ptr);

   in_port_ptr->sdata_ptr                                       = &in_port_ptr->gc.common.sdata;
   module_ptr->in_port_sdata_pptr[in_port_ptr->gc.gu.cmn.index] = &in_port_ptr->gc.common.sdata;

#ifdef VERBOSE_DEBUGGING
   pt_cntr_t             *me_ptr          = (pt_cntr_t *)base_ptr;
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "Assigned self sdata ptr 0x%lx to module 0x%lx in_port_id 0x%x",
                in_port_ptr->sdata_ptr,
                module_ptr->gc.topo.gu.module_instance_id,
                in_port_ptr->gc.gu.cmn.id);
#endif
   return result;
}

ar_result_t pt_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr)
{
   ar_result_t             result           = AR_EOK;
   pt_cntr_ext_out_port_t *ext_out_port_ptr = (pt_cntr_ext_out_port_t *)gu_ext_port_ptr;
   pt_cntr_output_port_t  *out_port_ptr     = (pt_cntr_output_port_t *)ext_out_port_ptr->gc.gu.int_out_port_ptr;
   pt_cntr_module_t       *module_ptr       = (pt_cntr_module_t *)out_port_ptr->gc.gu.cmn.module_ptr;

   result = gen_cntr_init_ext_out_port(base_ptr, gu_ext_port_ptr);

   out_port_ptr->sdata_ptr                                        = &out_port_ptr->gc.common.sdata;
   module_ptr->out_port_sdata_pptr[out_port_ptr->gc.gu.cmn.index] = &out_port_ptr->gc.common.sdata;

#ifdef VERBOSE_DEBUGGING
   pt_cntr_t              *me_ptr           = (pt_cntr_t *)base_ptr;
   GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                DBG_LOW_PRIO,
                "Assigned self sdata ptr 0x%lx to module 0x%lx out_port_id 0x%x",
                out_port_ptr->sdata_ptr,
                module_ptr->gc.topo.gu.module_instance_id,
                out_port_ptr->gc.gu.cmn.id);
#endif
   return result;
}

/** Validate graph shape at Graph open command.*/
ar_result_t pt_cntr_validate_topo_at_open(pt_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // check if number of Subgraphs is "1"
   // todo: use num subgraphs
   if (!me_ptr->gc.topo.gu.sg_list_ptr || me_ptr->gc.topo.gu.sg_list_ptr->next_ptr)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Number of Subgraphs must be 1, its more than 1/0, %lu",
                   me_ptr->gc.topo.gu.sg_list_ptr ? 2 : 0);
      THROW(result, AR_EFAILED);
   }

   // Fail if voice SG type
   if (APM_SUB_GRAPH_SID_VOICE_CALL == me_ptr->gc.topo.gu.sg_list_ptr->sg_ptr->sid)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "SID 0x%lx not supported",
                   me_ptr->gc.topo.gu.sg_list_ptr->sg_ptr->sid);
      THROW(result, AR_EFAILED);
   }

   // check number of parallel paths in the sorted order list
   if (me_ptr->gc.topo.gu.num_parallel_paths > 1)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Number of parallel paths %lu > 1 is not supported",
                   me_ptr->gc.topo.gu.num_parallel_paths);
      THROW(result, AR_EFAILED);
   }

   // check if atleast one STM is present
   if (FALSE == me_ptr->gc.topo.flags.is_signal_triggered)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Error Signal triggered module not found! Pass Thru cntr needs atleast one STM module. ");
      THROW(result, AR_EFAILED);
   }

   if (TRUE == me_ptr->gc.topo.flags.is_sync_module_present)
   {
      GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "Error sync module found, Pass thru container doesnt support.");
      THROW(result, AR_EFAILED);
   }

   // TODO: check if all the modules support MD extension or not support MD extension.
   TRY(result, pt_cntr_check_n_validate_module_static_properties(me_ptr));

   // todo: option 1) Print warning in PTC, and add the limitation in the documentation.
   // todo: Option 2) Future work: requires_data_buf is not supported. for MFC by default keep rqdb=TRUE -> add new API
   // event to disable/enable requires_data_buf dynamically.

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->gc.topo.gu.log_id)
   {
   }

   return result;
}

/** Checks media format and threshold.
      1. Checks if all the modules raised same threshold. If there are multiple threshold modules, returns error.
      2. Check supported media format for each port as well .*/
ar_result_t pt_cntr_validate_media_fmt_thresh(pt_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   for (gu_sg_list_t *sg_list_ptr = me_ptr->gc.topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         pt_cntr_module_t *module_ptr = (pt_cntr_module_t *)module_list_ptr->module_ptr;

         if (module_ptr->gc.topo.flags.need_dm_extn && gen_topo_is_dm_enabled((gen_topo_module_t *)module_ptr))
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error! Module 0x%lx has dm extension enabled %lu will not work and can lead to glitches in "
                         "Pass thru container.",
                         module_ptr->gc.topo.gu.module_instance_id);
            result |= AR_EFAILED;
         }

         // if num proc loops is greater than 1 it means there are two modules are with different thresholds in the
         // container which is not supported in the pass thru container.
         if (module_ptr->gc.topo.num_proc_loops > 1)
         {
            GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error! Module 0x%lx has num_proc_loops %lu > 1 means there are multiple threshold "
                         "modules, which doesnt work for Pass thru container!",
                         module_ptr->gc.topo.gu.module_instance_id,
                         module_ptr->gc.topo.num_proc_loops);
            result |= AR_EFAILED;
         }

         // Module has thresh if any port has threshold.
         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gc.topo.gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            pt_cntr_output_port_t *out_port_ptr     = (pt_cntr_output_port_t *)out_port_list_ptr->op_port_ptr;
            pt_cntr_input_port_t  *next_in_port_ptr = (pt_cntr_input_port_t *)out_port_ptr->gc.gu.conn_in_port_ptr;

            // check if current output and next input supports same version of deinterleaved unpacked
            // thats is either both are not unpacked, both are v1 and both are v2.
            // If one module is v1 and other is v2 it requires conversion in the fwk and will not work
            if (next_in_port_ptr &&
                (out_port_ptr->gc.common.flags.is_pcm_unpacked != next_in_port_ptr->gc.common.flags.is_pcm_unpacked))
            {
               GEN_CNTR_MSG(me_ptr->gc.topo.gu.log_id,
                            DBG_ERROR_PRIO,
                            "Current output (MIID, PID) (0x%lx, 0x%lx) mf is unpacked V%lu but next input (MIID, PID) "
                            "(0x%lx, 0x%lx) is unpacked V%lu",
                            out_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                            out_port_ptr->gc.gu.cmn.id,
                            out_port_ptr->gc.common.flags.is_pcm_unpacked,
                            next_in_port_ptr->gc.gu.cmn.module_ptr->module_instance_id,
                            next_in_port_ptr->gc.gu.cmn.id,
                            next_in_port_ptr->gc.common.flags.is_pcm_unpacked);
               result |= AR_EFAILED;
            }
         }
      }
   }

   return result;
}

ar_result_t pt_cntr_create_module(gen_topo_t            *topo_ptr,
                                  gen_topo_module_t     *module_ptr,
                                  gen_topo_graph_init_t *graph_init_data_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)GET_BASE_PTR(gen_cntr_t, topo, topo_ptr);
   INIT_EXCEPTION_HANDLING

   TRY(result, gen_cntr_create_module(topo_ptr, module_ptr, graph_init_data_ptr));

   TRY(result, pt_cntr_realloc_scratch_sdata_arr((pt_cntr_t *)me_ptr, (pt_cntr_module_t *)module_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}
