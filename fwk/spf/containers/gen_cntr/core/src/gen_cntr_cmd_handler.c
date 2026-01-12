/**
 * \file gen_cntr_cmd_handler.c
 * \brief
 *   This file contains functions for command hanlder code for GEN_CNTR
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "gen_cntr_utils.h"
#include "apm.h"
#include "pt_cntr.h"

static ar_result_t gen_cntr_set_buf_mgr_mode(gen_cntr_t *me_ptr);

static ar_result_t gen_cntr_call_process_frames(cu_base_t *cu_ptr, void *temp)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;

   if (check_if_pass_thru_container(me_ptr))
   {
      // no need to process frames from the event context, this container is always triggered by a timer/interrupt trigger.
      return AR_EOK;
   }

   // for signal triggered if we call process_frames, STM (EP) module will be called at wrong time.
   if (!me_ptr->topo.flags.is_signal_triggered)
   {
      //  treat as though buf trigger. not calling now may result in hang if there'll be no more IO trigger
      me_ptr->topo.proc_context.curr_trigger = GEN_TOPO_DATA_TRIGGER;

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Processing frames after command handling");

      gen_cntr_data_process_frames(me_ptr);
   }
   // for signal triggered cntr, there could be a data trigger policy module which might have changed trigger policy to
   // listen to dataQ masks.
   // E.g. SPR timer disable on/off. DAM module in Tx port for voice UI.
   else // if (me_ptr->topo.num_data_tpm)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, "Trying to wait for triggers after command handling");
      gen_cntr_wait_for_trigger(me_ptr);
   }
   return AR_EOK;
}

/**
 * handles events after commands.
 * typically required for set-cfg, but also in other scenarios:
 *  - A SG is opened and running and another new one joins. new module raises threshold event.
 *    If we don't handle right after command, then after some data processing, events might be handled causing data
 *       drops (threshold/MF prop can cause data drop).
 *  - start command to a data port may raise trigger policy
 *
 *  This function needs to first ensure that the event flags are reconciled.
 *  This function must also be executed synchronous to the data-path processing.
 */
static ar_result_t gen_cntr_handle_events_after_cmds(gen_cntr_t *me_ptr, bool_t is_ack_cmd, ar_result_t rsp_result)
{
   ar_result_t                 result              = AR_EOK;
   cu_base_t                  *base_ptr            = &me_ptr->cu;
   bool_t                      process_frames      = FALSE;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   // This is an event handler (CAPI and FWK) function
   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   SPF_MANAGE_CRITICAL_SECTION

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   // Need to reconcile the event flags after handling the command.
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, TRUE /*do reconcile*/);
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr, &me_ptr->topo, TRUE /*do reconcile*/);

   /** no need to handle event when handle rest is pending and some events occur when container is not started. */
   if (!cu_is_any_handle_rest_pending(base_ptr) &&
       (fwk_event_flag_ptr->sg_state_change ||
        (me_ptr->cu.flags.is_cntr_started && (fwk_event_flag_ptr->word || capi_event_flag_ptr->word))))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_LOW_PRIO,
                   "Handling events after commands, fwk events 0x%lX, capi events 0x%lX",
                   fwk_event_flag_ptr->word,
                   capi_event_flag_ptr->word);

      // if any module is enabled then we need to check if we can process topo based on that trigger.
      // Example: Enabling DTMF generator module need to enable topo process.

      // fwk_event_flag_ptr->port_state_change:
      // In case if container was waiting on output trigger and stop is propagated
      // then container will stop listening to that as well.
      // later when start is propagated we need to try to process atleast once
      // so that existing tiggers are processed and bitmask is updated.
      // Adding port_flushed check to ensure that the module gets triggered atleast once after the containers are flushed.
      // This flag is set when a container is flushed, and is useful when some container got stuck in data path
      // before flush was done as in that case, process_state flag is not set.

      // capi_event_flag_ptr->media_fmt_event:
      // If a module is raising media format for the first time then it may start generating output.
      if (capi_event_flag_ptr->process_state || capi_event_flag_ptr->media_fmt_event ||
          capi_event_flag_ptr->data_trigger_policy_change || fwk_event_flag_ptr->sg_state_change ||
          fwk_event_flag_ptr->port_state_change || fwk_event_flag_ptr->need_to_handle_dcm_req_for_island_exit ||
          fwk_event_flag_ptr->port_flushed)
      {
         process_frames = TRUE;
      }

      gen_cntr_handle_fwk_events_util_(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);
   }

   // Need to clear this flag in case if event handling is not called because container wasn't started
   fwk_event_flag_ptr->port_state_change = FALSE;
   fwk_event_flag_ptr->port_flushed = FALSE;
   me_ptr->cu.flags.apm_cmd_context = FALSE;

   // send the ack for the command before topo process.
   // this should be done after handling events so that all the votes are in order.
   if (is_ack_cmd)
   {
      spf_msg_ack_msg(&me_ptr->cu.cmd_msg, rsp_result);
   }

   me_ptr->topo.flags.need_to_ignore_signal_miss = TRUE;

#ifdef ENABLE_SIGNAL_MISS_CRASH
   me_ptr->st_module.steady_state_interrupt_counter = 0;
#endif

   /* Check if any pending buffers needs to be destoryed */
   topo_buf_manager_destroy_all_unused_buffers(&me_ptr->topo, FALSE);

   // if currently processing with thin topo, check if anything needs to be updated in thin topo or needs to switch to
   // gen topo
   result |= thin_topo_handle_fwk_events(me_ptr, capi_event_flag_ptr, fwk_event_flag_ptr);

   if (process_frames || me_ptr->topo.flags.process_pending || me_ptr->topo.flags.process_us_gap)
   {
      me_ptr->topo.flags.process_pending = FALSE;
      me_ptr->topo.flags.process_us_gap  = FALSE;

      // calling wait-for-trigger is not sufficient because we might already have an out buf (in case src module
      // disabled itself when we called process instead of at init). E.g. COP packetizer src module.
      if (me_ptr->cu.gu_ptr->data_path_thread_id != posal_thread_get_curr_tid())
      {
         // trigger data processing thread (main container thread) to reevaluate the processing decision.
         me_ptr->cu.handle_rest_fn      = gen_cntr_call_process_frames;
         me_ptr->cu.handle_rest_ctx_ptr = NULL;
      }
      else if (FALSE == check_if_pass_thru_container(me_ptr))
      {
         gen_cntr_call_process_frames(&me_ptr->cu, NULL);
      }
   }

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);

   return result;
}

static ar_result_t gen_cntr_handle_rest_of_graph_close(cu_base_t *base_ptr, bool_t is_handle_cu_cmd_mgmt)
{
   ar_result_t                result      = AR_EOK;
   gen_cntr_t *               me_ptr      = (gen_cntr_t *)base_ptr;
   SPF_MANAGE_CRITICAL_SECTION

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   if (is_handle_cu_cmd_mgmt)
   {
      result = cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_CLOSE, TOPO_SG_STATE_STOPPED);
   }

   /* move closing sg and external ports to the async gu.
    * this also updates the sorted module list.
    * This is done to ensure that the data-path doesn't access any closing SG/module/port in parallel.
    * */
   gu_prepare_async_destroy(me_ptr->cu.gu_ptr, me_ptr->cu.heap_id);

   /* First external ports connected to peer container and not part of subgraph close can be deinited.
    * This will also invalidate the association between external and internal data port, this is to ensure that the
    * internal ports are destroyed during "gu_cleanup_dangling_ports". Ensuring the ports are destroyed from the
    * running subgraph simplifies the nblc assignment.
    * */
   cu_deinit_external_ports(&me_ptr->cu, TRUE /*b_ignore_ports_from_sg_close*/, FALSE /*force_deinit_all_ports*/);

   // cleanup the dangling internal ports.
   // func:"cu_handle_sg_mgmt_cmd" may have invalidated the internal DATA port connections between running and closing
   // SG within this container.
   // func: "cu_deinit_external_ports" may have invalidated the internal DATA port connection
   // between running and closing SG across container.

   // here we are not destroying the control ports as their buf Q must be destroyed first.
   // cu_deinit_internal/external_ctrl_ports will be called asynchronously later which needs to first destroy the buf-q.
   // Later control port will be destroyed in gu_cleanup_danling_control_ports
   gu_cleanup_dangling_data_ports(me_ptr->cu.gu_ptr);

   /* update the nblc chain for non-closing subgraphs.
    * connection (external or internal) between closing and non-closing SGs are already destroyed at this point.
    */
   gen_topo_assign_non_buf_lin_chains(&(me_ptr->topo));

   // reset topo level flags by excluding the subgraphs closing.
   gen_topo_reset_top_level_flags(&me_ptr->topo);

   // check and assign appropriate data process function for signal triggered containers
   gen_cntr_check_and_assign_st_data_process_fn(me_ptr);

   /** NOTES: [PT_CNTR] Ext output close should not alter proc list. Since ext output/SG stop should have already removed the
   inactive due to self/propagated stop modules from the proc list. */
   if (check_if_pass_thru_container(me_ptr))
   {
      spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;

      spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
      if (cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle)
      {
         result |= pt_cntr_update_module_process_list((pt_cntr_t *)me_ptr);
         result |= pt_cntr_assign_port_buffers((pt_cntr_t *)me_ptr);
      }
   }

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   //======== following code must be able to run in parallel to the data processing =============

   // internal control ports have their own outgoing buffer queues. They need to be drained and destroyed when the
   // sg is closed.
   cu_deinit_internal_ctrl_ports(&me_ptr->cu, FALSE /*b_destroy_all_ports*/);

   // module resource can be destroyed outside critical section for Pass Thru container.
   if (check_if_pass_thru_container(me_ptr))
   {
      // destory sdata array pointers for each module
      result |= pt_cntr_destroy_modules_resources((pt_cntr_t *)me_ptr, FALSE);
   }

   // module destroy happens only with SG destroy
   gen_topo_destroy_modules(&me_ptr->topo, FALSE /*b_destroy_all_modules*/);

   // Deinit external ports closing as part of self subgraph close..
   // Also, ** very important** to call gen_topo_destroy_modules before cu_deinit_external_ports.
   // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
   // deinit.
   cu_deinit_external_ports(&me_ptr->cu,
                            FALSE, /*b_ignore_ports_from_sg_close*/
                            FALSE /*force_deinit_all_ports*/);

   gu_finish_async_destroy(me_ptr->cu.gu_ptr);

   // since modules/port got destroyed and there could be Max num ports change, check & recreate scratch memory
   GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&me_ptr->topo, realloc_scratch_mem);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   // dangling data-ports are already destroyed synchronously before, now only need to destroy dangling control ports
   gu_cleanup_danling_control_ports(me_ptr->cu.gu_ptr);

   if (me_ptr->cu.gu_ptr->sg_list_ptr)
   {
      // if some sgs are closed then reevalutate number of parallel paths.
      gen_cntr_allocate_wait_mask_arr(me_ptr);
   }

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   return result;
}
/**
 * For external ports, gen_cntr_operate_on_ext_in_port & gen_cntr_operate_on_ext_out_port is called
 * in 2 contexts:
 * 1. in the context of subgraph command: both ends of the connection belongs to the same SG.
 * 2. in the context of handle list of subgraph: this is an inter-SG connection.
 *
 * the distinction is made in the caller.
 * This is a common utility.
 *
 */
void gen_cntr_handle_failure_at_graph_open(gen_cntr_t *me_ptr, ar_result_t result)
{
   SPF_MANAGE_CRITICAL_SECTION
   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   spf_msg_cmd_graph_open_t *open_cmd_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   gu_finish_async_create(&me_ptr->topo.gu, me_ptr->cu.heap_id);

   gu_prepare_cleanup_for_graph_open_failure(&me_ptr->topo.gu, open_cmd_ptr);

   gen_cntr_handle_rest_of_graph_close(&me_ptr->cu, FALSE /*is_handle_cu_cmd_mgmt*/);

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);

   gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);
}

/**
 * context ctx_ptr is not used.
 *
 * This function is entered in below ways:
 * 1. Directly from gen_cntr_graph_open
 * 2. From thread relaunch originating from gen_cntr_graph_open
 */
static ar_result_t gen_cntr_handle_rest_of_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   spf_msg_cmd_graph_open_t *open_cmd_ptr;
   gen_topo_graph_init_t     graph_init_data = { 0 };

   gen_cntr_t *      me_ptr     = (gen_cntr_t *)base_ptr;
   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   cu_reset_handle_rest(base_ptr);

   /* create modules asynchronously, avoid working on connection to the existing modules */
   graph_init_data.spf_handle_ptr    = &me_ptr->cu.spf_handle;
   graph_init_data.gpr_cb_fn         = cu_gpr_callback;
   graph_init_data.capi_cb =
      check_if_pass_thru_container(me_ptr) ? pt_cntr_capi_event_callback : gen_topo_capi_callback;
   TRY(result, gen_topo_create_modules(&me_ptr->topo, &graph_init_data));

   /* Allocate memory for voice info structure, for voice call use cases*/
   TRY(result, cu_create_voice_info(&me_ptr->cu, open_cmd_ptr));

   TRY(result, cu_init_external_ports(&me_ptr->cu, GEN_CNTR_EXT_CTRL_PORT_Q_OFFSET));

   // NOTES: [PT_CNTR]
   // 1. pass thru container ext ports sdata_ptr is assigned for the newly opened ports in init_ext_in/out_port() Since
   // 2. pass thru supports only one subgraph there is no async subgraph open which can alter module proc order.
   // 3. even in future if multiple SGs are supported, newly opened SG still should not alter the module proc order the
   // SG boundary modules/ext ports opens should also not alter the module proc order. The modules in the started
   // subgraph at the SG boundary will remain inactive due to port being stopped.
   // 4. If the first SG is getting opened, no need to update the module proc order, since there is no started subgraph

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   // merge the new graph with the existing one
   TRY(result, gu_finish_async_create(&me_ptr->topo.gu, me_ptr->cu.heap_id));

   // note: since newly opened modules/ports are not going to change the order of started_sorted_module_list therefore
   // not updating it

   TRY(result, gen_cntr_allocate_wait_mask_arr(me_ptr));

   // initialize the connections to the existing modules
   graph_init_data.skip_scratch_mem_reallocation = TRUE; // already allocated before.
   graph_init_data.propagate_rdf                 = TRUE;
   TRY(result, gen_topo_create_modules(&me_ptr->topo, &graph_init_data));

   // to update the trigger policy related flags.
   gen_topo_reset_top_level_flags(&me_ptr->topo);

   // SYNC and STM can not be supported in same container. There is no need of sync module in STM container.
   VERIFY(result, !(me_ptr->topo.flags.is_signal_triggered && me_ptr->topo.flags.is_sync_module_present));

   // check and assign appropriate data process function for signal triggered containers
   gen_cntr_check_and_assign_st_data_process_fn(me_ptr);

   TRY(result, gen_cntr_set_buf_mgr_mode(me_ptr));

   if (check_if_pass_thru_container(me_ptr))
   {
      result |= pt_cntr_validate_topo_at_open((pt_cntr_t *)me_ptr);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      gen_cntr_handle_failure_at_graph_open(me_ptr, result);

      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
                   me_ptr->cu.curr_chan_mask,
                   result);

      SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
      return result;
   }

   // continue rest of set cfg handling in graph open
   result = gen_cntr_handle_rest_of_set_cfgs_in_graph_open(base_ptr, ctx_ptr);

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   return result;
}

static ar_result_t gen_cntr_relaunch_thread_and_handle_rest_of_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING

   // todo: see if relaunch can be done in the command context itself
   if (ctx_ptr)
   {
      gen_cntr_thread_relaunch_rest_handle_ctx_t *thread_relaunch_open_ctx_ptr =
         (gen_cntr_thread_relaunch_rest_handle_ctx_t *)ctx_ptr;
      bool_t              thread_launched = FALSE;
      posal_thread_prio_t thread_priority = 0;
      char_t              thread_name[POSAL_DEFAULT_NAME_LEN];
      gen_cntr_prepare_to_launch_thread(me_ptr, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN);

      TRY(result,
          cu_check_launch_thread(&me_ptr->cu,
                                 thread_relaunch_open_ctx_ptr->stack_size,
                                 thread_relaunch_open_ctx_ptr->root_stack_size,
                                 thread_priority,
                                 thread_name,
                                 &thread_launched));

      if (thread_launched)
      {
         me_ptr->cu.handle_rest_fn      = gen_cntr_handle_rest_of_graph_open;
         me_ptr->cu.handle_rest_ctx_ptr = NULL;
         return result;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      gen_cntr_handle_failure_at_graph_open(me_ptr, result);
   }

   return result;
}

/**
 * context ctx_ptr is not used.
 *
 * This function is entered in below ways:
 * 1. Directly from gen_cntr_handle_rest_of_graph_open. (Important note: gen_cntr_handle_rest_of_graph_open itself can
 * be
 *    invoked from mulitple contexts. Check comments at gen_cntr_handle_rest_of_graph_open.)
 * 3. From gen_cntr_handle_rest_of_set_cfg_after_real_module_cfg, which in turn originates from thread relaunch due
 *    to set-cfg containing real-module-id of a placeholder module (in graph open).
 */
ar_result_t gen_cntr_handle_rest_of_set_cfgs_in_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gen_cntr_t *              me_ptr = (gen_cntr_t *)base_ptr;
   spf_msg_cmd_graph_open_t *open_cmd_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   /* Handle rest of set config */
   bool_t pm_already_registered   = posal_power_mgr_is_registered(me_ptr->cu.pm_info.pm_handle_ptr);
   bool_t is_duty_cycling_allowed = cu_check_all_subgraphs_duty_cycling_allowed(&me_ptr->cu);
   bool_t dcm_already_registered  = me_ptr->cu.pm_info.flags.register_with_dcm;

   // do not clear handle_rest because below func needs it to know where it left off in the set-cfg payload.
   TRY(result,
       cu_set_get_cfgs_fragmented(base_ptr,
                                  (apm_module_param_data_t **)open_cmd_ptr->param_data_pptr,
                                  open_cmd_ptr->num_param_id_cfg,
                                  TRUE,                        /*is_set_cfg*/
                                  FALSE,                       /*is_deregister (Dont care in this case)*/
                                  SPF_CFG_DATA_TYPE_DEFAULT)); // no module will be in start state during open

   // real-module-id present in the set-cfg payload could recreate thread. after relaunching the thread,
   // gen_cntr_handle_rest_of_set_cfg_after_real_module_cfg calls this func

   if (cu_is_any_handle_rest_pending(base_ptr))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: handle_rest fn set during set-get-cfg of graph open");
      return result;
   }

   // If DCM stubbed, want to fail graph_open on DCM registration attempt
   if ((is_duty_cycling_allowed) && (!dcm_already_registered))
   {
      TRY(result, cu_register_with_dcm(&me_ptr->cu));
   }

   // in success case, ack to APM is sent from this func.
   TRY(result, gu_respond_to_graph_open(me_ptr->cu.gu_ptr, &me_ptr->cu.cmd_msg, me_ptr->cu.heap_id));

   // Register container with pm
   cu_register_with_pm(&me_ptr->cu, is_duty_cycling_allowed);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      if ((is_duty_cycling_allowed) && (dcm_already_registered))
      {
         result = cu_deregister_with_dcm(&me_ptr->cu);
      }

      if (pm_already_registered)
      {
         cu_deregister_with_pm(&me_ptr->cu);
      }

      // handle rest of graph-open is null, hence this handle rest must be set-cfg's
      if (base_ptr->handle_rest_ctx_ptr)
      {
         cu_handle_rest_ctx_for_set_cfg_t *set_get_ptr =
            (cu_handle_rest_ctx_for_set_cfg_t *)base_ptr->handle_rest_ctx_ptr;
         result |= set_get_ptr->overall_result;
      }

      gen_cntr_handle_failure_at_graph_open(me_ptr, result);

      cu_reset_handle_rest(base_ptr);

      return result;
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

ar_result_t gen_cntr_gpr_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   gpr_packet_t *packet_ptr = (gpr_packet_t *)me_ptr->cu.cmd_msg.payload_ptr;
   posal_pm_island_vote_t cached_island_vote;
   // set with Exit condition as default
   cached_island_vote.island_vote_type = PM_ISLAND_VOTE_EXIT;

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GPR cmd: Executing GPR command, opcode(%lX) token(%lx), handle_rest %u",
                packet_ptr->opcode,
                packet_ptr->token,
                cu_is_any_handle_rest_pending(base_ptr));

   // fetch current container island voting status
   cached_island_vote.island_vote_type = me_ptr->topo.flags.aggregated_island_vote;

   /*vote against island in command context,
     cntr will vote for island entry later
     in the process context after processing two frames*/
   gen_cntr_vote_against_island((void *)&me_ptr->cu);
   
   switch (packet_ptr->opcode)
   {
      case AR_SPF_MSG_GLOBAL_SH_MEM:
      {
         cu_handle_global_shmem_msg(base_ptr, packet_ptr);
         break;
      }
      case APM_CMD_SET_CFG:
      case APM_CMD_GET_CFG:
      {
         cu_set_get_cfgs_packed(base_ptr, packet_ptr, SPF_CFG_DATA_TYPE_DEFAULT);
         break;
      }

      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      {
         cu_set_get_cfgs_packed(base_ptr, packet_ptr, SPF_CFG_DATA_PERSISTENT);
         break;
      }

      case APM_CMD_REGISTER_SHARED_CFG:
      case APM_CMD_DEREGISTER_SHARED_CFG:
      {
         cu_set_get_cfgs_packed(base_ptr, packet_ptr, SPF_CFG_DATA_SHARED_PERSISTENT);
         break;
      }
      default:
      {
         TRY(result, cu_gpr_cmd(base_ptr));

         me_ptr->topo.flags.need_to_handle_frame_done = cu_is_frame_done_event_registered(base_ptr);
         break;
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if (!cu_is_any_handle_rest_pending(base_ptr) || AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GPR cmd: Done executing GPR command, result=0x%lx, handle_rest %u",
                   result,
                   cu_is_any_handle_rest_pending(base_ptr));

      if (base_ptr->handle_rest_ctx_ptr)
      {
         cu_handle_rest_ctx_for_set_cfg_t *set_get_ptr =
            (cu_handle_rest_ctx_for_set_cfg_t *)base_ptr->handle_rest_ctx_ptr;
         result |= set_get_ptr->overall_result;
      }

      cu_reset_handle_rest(base_ptr);
   }

   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

   gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
   // check if caching was PM_ISLAND_VOTE_DONT_CARE,
   // apply if current state is still PM_ISLAND_VOTE_EXIT.

   if ((PM_ISLAND_VOTE_DONT_CARE == cached_island_vote.island_vote_type) &&
       (PM_ISLAND_VOTE_EXIT == me_ptr->topo.flags.aggregated_island_vote))
   {
      gen_cntr_update_island_vote(me_ptr, cached_island_vote);
   }
   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

   return result;
}

// Buffer manager can operate in low latency mode is the container is signal triggered
// and atleast one SG is LL.
static ar_result_t gen_cntr_set_buf_mgr_mode(gen_cntr_t *me_ptr)
{
   // topo buf mgr will only accept the set mode once, subsequents will be ignored.
   if (TOPO_BUF_MODE_INVALID != me_ptr->topo.buf_mgr.mode)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_MED_PRIO,
                   "Buffer manager mode is already set to 0x%x",
                   me_ptr->topo.buf_mgr.mode);
      return AR_EOK;
   }

   topo_buf_manager_mode_t mode = TOPO_BUF_LOW_POWER;
   if (me_ptr->topo.flags.is_signal_triggered)
   {
      for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
      {
         if (sg_list_ptr->sg_ptr && (sg_list_ptr->sg_ptr->perf_mode == APM_SG_PERF_MODE_LOW_LATENCY))
         {
            mode = TOPO_BUF_LOW_LATENCY;
         }
      }
   }

   bool_t is_duty_cycling_allowed = cu_check_all_subgraphs_duty_cycling_allowed(&me_ptr->cu);

   if (is_duty_cycling_allowed && (TOPO_BUF_LOW_LATENCY == mode))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_ERROR_PRIO,
                   "The buffer manager mode cannot be TOPO_BUF_LOW_LATENCY "
                   "when cntr_duty_cycling_allowed_subgraphs is TRUE");
      /*
       *In low latency mode, buffers are assigned one time and released at the end. This mode is used in signal trigger
       *cases as an optimization. For non-signal trigger cases, this mode is not used. When duty cycling is set to true,
       *SPR can disable timer at run time. Ideally buffering mode has to change. This would need returning all buffers
       *back (gen_topo_mark_buf_mgr_buffers_to_force_return). Since we dont have run time change of buffer mode
       *currently, we fail the graph open.
       */

      return AR_EUNSUPPORTED;
   }

   me_ptr->topo.buf_mgr.mode = mode;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_MED_PRIO, "Setting buffer manager mode to 0x%x", mode);

   return AR_EOK;
}

ar_result_t gen_cntr_graph_open(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   spf_msg_cmd_graph_open_t *open_cmd_ptr    = NULL;
   uint32_t                  stack_size      = 0;
   uint32_t                  root_stack_size = 0;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GRAPH_OPEN: Executing graph open command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_open_t));

   open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   result = gu_validate_graph_open_cmd(&me_ptr->topo.gu, open_cmd_ptr);

   if (AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: validation for graph open failed. current channel mask=0x%x. "
                   "result=0x%lx.",
                   me_ptr->cu.curr_chan_mask,
                   result);
      spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
      return result;
   }

   // container properties can't be changed run time.
   if (0 == me_ptr->topo.gu.num_subgraphs)
   {
      TRY(result, cu_parse_container_cfg(&me_ptr->cu, open_cmd_ptr->container_cfg_ptr));
   }

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   // initiate thread pool
   if (NULL == me_ptr->cu.async_cmd_handle)
   {
      // setup the thread pool to handle asynchronous command
      cu_async_cmd_handle_init(&me_ptr->cu, GEN_CNTR_SYNC_CMD_BIT_MASK);
   }
#endif

   gu_sizes_t sz;
   if (check_if_pass_thru_container(me_ptr))
   {
      sz.ext_ctrl_port_size = GEN_CNTR_EXT_CTRL_PORT_SIZE_W_QS;
      sz.ext_in_port_size   = PT_CNTR_EXT_IN_PORT_SIZE_W_QS;
      sz.ext_out_port_size  = PT_CNTR_EXT_OUT_PORT_SIZE_W_QS;
      sz.in_port_size       = sizeof(pt_cntr_input_port_t);
      sz.out_port_size      = sizeof(pt_cntr_output_port_t);
      sz.ctrl_port_size     = GEN_CNTR_INT_CTRL_PORT_SIZE_W_QS;
      sz.sg_size            = sizeof(gen_topo_sg_t);
      sz.module_size        = sizeof(pt_cntr_module_t);
   }
   else
   {
      sz.ext_ctrl_port_size = GEN_CNTR_EXT_CTRL_PORT_SIZE_W_QS;
      sz.ext_in_port_size   = GEN_CNTR_EXT_IN_PORT_SIZE_W_QS;
      sz.ext_out_port_size  = GEN_CNTR_EXT_OUT_PORT_SIZE_W_QS;
      sz.in_port_size       = sizeof(gen_topo_input_port_t);
      sz.out_port_size      = sizeof(gen_topo_output_port_t);
      sz.ctrl_port_size     = GEN_CNTR_INT_CTRL_PORT_SIZE_W_QS;
      sz.sg_size            = sizeof(gen_topo_sg_t);
      sz.module_size        = sizeof(gen_cntr_module_t);
   }

   // first prepare for asynchronous (parallel to the data path thread) graph open
   // once graph is created gu_finish_async_create will be called to merge the new graph with the existing one.
   TRY(result, gu_prepare_async_create(&me_ptr->topo.gu, me_ptr->cu.heap_id));

   TRY(result, gu_create_graph(&me_ptr->topo.gu, open_cmd_ptr, &sz, me_ptr->cu.heap_id));

   // initialize media format pointer for the new module's ports.
   gen_topo_set_default_media_fmt_at_open(&me_ptr->topo);

   // new modules can change the stack requirement
   TRY(result, gen_cntr_get_thread_stack_size(me_ptr, &stack_size, &root_stack_size));

   if (cu_check_thread_relaunch_required(&me_ptr->cu, stack_size, root_stack_size))
   {
      // thread re-launch should be failed if it is a time critical container.
      // VERIFY(result, (FALSE == gen_cntr_check_if_time_critical(me_ptr))); enable this when actually required.

      /* current execution context can be in the helper thread handling the command processing. So need to notify main
        thread to relaunch*/
      gen_cntr_thread_relaunch_rest_handle_ctx_t *thread_relaunch_open_ctx_ptr = NULL;
      MALLOC_MEMSET(thread_relaunch_open_ctx_ptr,
                    gen_cntr_thread_relaunch_rest_handle_ctx_t,
                    sizeof(gen_cntr_thread_relaunch_rest_handle_ctx_t),
                    me_ptr->cu.heap_id,
                    result);
      thread_relaunch_open_ctx_ptr->stack_size          = stack_size;
      thread_relaunch_open_ctx_ptr->root_stack_size     = root_stack_size;
      thread_relaunch_open_ctx_ptr->handle_rest_ctx_ptr = NULL;

      if (me_ptr->cu.gu_ptr->data_path_thread_id != posal_thread_get_curr_tid())
      {
         // assign function to handle the thread relaunch in the main thread context and continue with open handling
         me_ptr->cu.handle_rest_fn      = gen_cntr_relaunch_thread_and_handle_rest_of_graph_open;
         me_ptr->cu.handle_rest_ctx_ptr = (void *)thread_relaunch_open_ctx_ptr;
      }
      else
      {
         result = gen_cntr_relaunch_thread_and_handle_rest_of_graph_open(base_ptr, thread_relaunch_open_ctx_ptr);
         MFREE_NULLIFY(thread_relaunch_open_ctx_ptr);
      }

      return result;
   }
   else
   {
      return gen_cntr_handle_rest_of_graph_open(&me_ptr->cu, NULL);
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      // Can't revert open on sgs in open_cmd_ptr if open_cmd_ptr is NULL. The NULL case happens when payload size of
      // open is too small.
      if (open_cmd_ptr)
      {
         gen_cntr_handle_failure_at_graph_open(me_ptr, result);
      }
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
                   me_ptr->cu.curr_chan_mask,
                   result);
   }

   return result;
}

ar_result_t gen_cntr_set_get_cfg(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   spf_msg_cmd_param_data_cfg_t *cfg_cmd_ptr;
   bool_t                        is_set_cfg_msg;
   bool_t                        is_deregister;
   bool_t                        is_ack_cmd = FALSE;
   spf_cfg_data_type_t           data_type;

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:SET_GET_CFG: Executing command opcode=0x%lX, current channel mask=0x%x, handle_rest %u",
                me_ptr->cu.cmd_msg.msg_opcode,
                me_ptr->cu.curr_chan_mask,
                cu_is_any_handle_rest_pending(base_ptr));

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_param_data_cfg_t));

   cfg_cmd_ptr = (spf_msg_cmd_param_data_cfg_t *)&header_ptr->payload_start;

   switch (me_ptr->cu.cmd_msg.msg_opcode)
   {
      case SPF_MSG_CMD_REGISTER_CFG:
      {
         data_type      = SPF_CFG_DATA_PERSISTENT;
         is_deregister  = FALSE;
         is_set_cfg_msg = TRUE;
         break;
      }
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         data_type      = SPF_CFG_DATA_PERSISTENT;
         is_deregister  = TRUE;
         is_set_cfg_msg = FALSE;
         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      {
         data_type      = SPF_CFG_DATA_TYPE_DEFAULT;
         is_deregister  = FALSE;
         is_set_cfg_msg = TRUE;
         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      {
         data_type      = SPF_CFG_DATA_TYPE_DEFAULT;
         is_deregister  = FALSE;
         is_set_cfg_msg = FALSE;
         break;
      }
      default:
      {
         THROW(result, AR_EUNSUPPORTED);
         break;
      }
   }

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);

   TRY(result,
       cu_set_get_cfgs_fragmented(base_ptr,
                                  (apm_module_param_data_t **)cfg_cmd_ptr->param_data_pptr,
                                  cfg_cmd_ptr->num_param_id_cfg,
                                  is_set_cfg_msg,
                                  is_deregister,
                                  data_type));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   if (!cu_is_any_handle_rest_pending(base_ptr) || AR_DID_FAIL(result))
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:SET_GET_CFG: Done executing command opcode=0x%lX, current channel mask=0x%x. result=0x%lx, "
                   "handle_rest %u",
                   me_ptr->cu.cmd_msg.msg_opcode,
                   me_ptr->cu.curr_chan_mask,
                   result,
                   cu_is_any_handle_rest_pending(base_ptr));

      if (base_ptr->handle_rest_ctx_ptr)
      {
         cu_handle_rest_ctx_for_set_cfg_t *set_get_ptr =
            (cu_handle_rest_ctx_for_set_cfg_t *)base_ptr->handle_rest_ctx_ptr;
         result |= set_get_ptr->overall_result;
      }

      cu_reset_handle_rest(base_ptr);

      //check to avoid double ack. in some case (error) ack may been raised already.
      is_ack_cmd = (me_ptr->cu.cmd_msg.payload_ptr)? TRUE: FALSE;
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

   gen_cntr_handle_events_after_cmds(me_ptr, is_ack_cmd, result);

   return result;
}

ar_result_t gen_cntr_graph_prepare(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "CMD:Prepare Graph: Entering prepare graph");
   spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr;
   spf_msg_header_t *        header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   cmd_gmgmt_ptr                        = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   // loop over the modules, if placeholder, check if enabled
   // if so, error out if real module id is not received
   TRY(result, gen_cntr_placeholder_check_if_real_id_rcvd_at_prepare(base_ptr, cmd_gmgmt_ptr));

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_PREPARE, TOPO_SG_STATE_PREPARED));

#ifdef CONTAINER_ASYNC_CMD_HANDLING
   if (!base_ptr->flags.is_cntr_started && (NULL == me_ptr->cu.async_cmd_handle))
   {
      // setup the thread pool to handle asynchronous command if container is not started yet.
      cu_async_cmd_handle_init(&me_ptr->cu, GEN_CNTR_SYNC_CMD_BIT_MASK);
   }
#endif

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);
   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:Prepare Graph: Done executing prepare graph command., current channel mask=0x%x. result=0x%lx",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

ar_result_t gen_cntr_graph_start(cu_base_t *base_ptr)
{
   ar_result_t result                = AR_EOK;
   gen_cntr_t *me_ptr                = (gen_cntr_t *)base_ptr;
   bool_t      FORCE_AGGREGATE_FALSE = FALSE;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:START:Executing start command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_START, TOPO_SG_STATE_STARTED));

   gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);

   /** NOTES: [PT_CNTR]
    *  Module active flag and proc list not updated it seems unsafe to free the lock here check with Harsh once.
    * Possible corner case if the ext output gets started and data thread is processing with old sorted order list.
    */

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:START:Done executing start Command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

ar_result_t gen_cntr_graph_suspend(cu_base_t *base_ptr)
{
   ar_result_t result                       = AR_EOK;
   gen_cntr_t *me_ptr                       = (gen_cntr_t *)base_ptr;
   bool_t      FORCE_AGGREGATE_FALSE        = FALSE;
   bool_t      stm_module_is_started_before = FALSE, stm_module_is_started_after = FALSE;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:SUSPEND: Executing suspend Command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   stm_module_is_started_before = (NULL != me_ptr->st_module.trigger_signal_ptr) ? TRUE : FALSE;

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_SUSPEND, TOPO_SG_STATE_SUSPENDED));

   gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);

   stm_module_is_started_after = (NULL != me_ptr->st_module.trigger_signal_ptr) ? TRUE : FALSE;

   /** Check if STM is stopped due to the current suspend */
   if (stm_module_is_started_before && !stm_module_is_started_after)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "CMD:SUSPEND:STM module stopped");
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   TRY(result, cu_handle_sg_mgmt_cmd_async(base_ptr, TOPO_SG_OP_SUSPEND, TOPO_SG_STATE_SUSPENDED));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
   }

   // handle fwk events like releasing BW, MIPS and latency votes if all sgs are suspended.
   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:SUSPEND:Done Executing suspend command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

ar_result_t gen_cntr_graph_stop(cu_base_t *base_ptr)
{
   ar_result_t result                       = AR_EOK;
   gen_cntr_t *me_ptr                       = (gen_cntr_t *)base_ptr;
   bool_t      FORCE_AGGREGATE_FALSE        = FALSE;
   bool_t      stm_module_is_started_before = FALSE, stm_module_is_started_after = FALSE;

   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:STOP: Executing stop Command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

   stm_module_is_started_before = (NULL != me_ptr->st_module.trigger_signal_ptr) ? TRUE : FALSE;

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_STOP, TOPO_SG_STATE_STOPPED));

   gen_cntr_update_cntr_kpps_bw(me_ptr, FORCE_AGGREGATE_FALSE);

   stm_module_is_started_after = (NULL != me_ptr->st_module.trigger_signal_ptr) ? TRUE : FALSE;

   /** if signal triggered module is stopped, and there are modules belonging to other subgraphs
    * in the container, we need to reset modules downstream of STM and send EOS from external out ports.
    * Not doing so will result in pending data which will be discontinuous if STM is restarted. */
   if (stm_module_is_started_before && !stm_module_is_started_after)
   {
      gen_topo_module_t *stm_module_ptr = gen_cntr_get_stm_module(me_ptr);
      if (stm_module_ptr)
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_HIGH_PRIO,
                      "CMD:STOP:STM module 0x%lX stopped",
                      stm_module_ptr->gu.module_instance_id);

         // no need to process frames as we forcefully reset
         me_ptr->topo.flags.process_us_gap = FALSE;

         uint32_t recurse_depth = 0;
         gen_cntr_reset_downstream_of_stm_upon_stop_and_send_eos(me_ptr,
                                                                 stm_module_ptr,
                                                                 stm_module_ptr,
                                                                 &recurse_depth);
      }
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);

   TRY(result, cu_handle_sg_mgmt_cmd_async(base_ptr, TOPO_SG_OP_STOP, TOPO_SG_STATE_STOPPED));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
   }

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:STOP:Done Executing stop command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

ar_result_t gen_cntr_graph_flush(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:FLUSH:Executing flush Command, current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_FLUSH, TOPO_SG_STATE_INVALID));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   me_ptr->cu.flags.apm_cmd_context = TRUE;

   // THis is a corner case scenario. Container may be hung in the data path before flush.
   // Event flag is set to ensure the topo processing is triggered at least once after Flush and it may resume the data-path processing.
   CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu,port_flushed);

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);
   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:FLUSH:Done executing flush Command, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

/**
 * APM follows these steps for close: stop, disconnect, close.
 * Close comes for external port disconnect (E.g. AFE client going away).
 */
ar_result_t gen_cntr_graph_close(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.gu.log_id;
   INIT_EXCEPTION_HANDLING

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CLOSE:Executing close Command, current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   result = gen_cntr_handle_rest_of_graph_close(base_ptr, TRUE /*is_handle_cu_cmd_mgmt*/);

   // Catch here so that we don't catch on AR_ETERMINATED.
   CATCH(result, GEN_CNTR_MSG_PREFIX, log_id)
   {
   }

   if (me_ptr->topo.gu.num_subgraphs != 0)
   {
      gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

      GEN_CNTR_MSG(log_id,
                   DBG_HIGH_PRIO,
                   "CMD:CLOSE:Done executing close command, current channel mask=0x%x. result=0x%lx.",
                   me_ptr->cu.curr_chan_mask,
                   result);
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                   DBG_HIGH_PRIO,
                   SPF_LOG_PREFIX "As number of subgraphs is zero, destroying this container");

      return gen_cntr_destroy_container(base_ptr);
   }

   return result;
}

/*
 * Any external output ports getting connected whose media format on the external port doesn't match internal port, try
 * to resend media
 * format to the external port. This is needed when there is an external output elementary module - after device
 * switch the external output port gets transferred to the previous module which already has sent media format. We
 * need to retrigger handling to copy media format from internal to external output port structures.
 */
static ar_result_t gen_cntr_check_prop_ext_outport_mf_on_connect(gen_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;

   cu_base_t *                   base_ptr    = (cu_base_t *)me_ptr;
   spf_msg_header_t *            header_ptr  = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;
   spf_cntr_port_connect_info_t *connect_ptr = (spf_cntr_port_connect_info_t *)&header_ptr->payload_start;

   for (uint32_t i = 0; i < connect_ptr->num_op_data_port_conn; i++)
   {
      spf_module_port_conn_t * op_conn_ptr = &connect_ptr->op_data_port_conn_list_ptr[i];
      gen_cntr_ext_out_port_t *ext_out_port_ptr =
         (gen_cntr_ext_out_port_t *)op_conn_ptr->self_mod_port_hdl.port_ctx_hdl;
      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;

      if ((out_port_ptr->common.flags.is_mf_valid) &&
          tu_has_media_format_changed(&ext_out_port_ptr->cu.media_fmt, out_port_ptr->common.media_fmt_ptr))
      {
         out_port_ptr->common.flags.media_fmt_event = TRUE;

         // Set overall mf flag to true to force assign_non_buf_lin_chains(), which should be handled at connect since
         // graph shape may change to attach tap point modules.
         GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&me_ptr->topo, media_fmt_event);
      }
   }

   return result;
}

ar_result_t gen_cntr_graph_connect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CONNECT:Executing connect command, current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   TRY(result, cu_graph_connect(base_ptr));

   /*
    * Any external output ports whose media format on the external port doesn't match internal port, try to resend media
    * format to the external port. This is needed when there is an external output elementary module - after device
    * switch the external output port gets transferred to the previous module which already has sent media format. We
    * need to retrigger handling to copy media format from internal to external output port structures.
    */
   gen_cntr_check_prop_ext_outport_mf_on_connect(me_ptr);

   /*
    * Connect is a substep of graph-open. For creating ext ports buf, downstream queue info is needed
    *  (to populate in dst handle of gk handle)
    * This means only after connect we can handle threshold (which handles ext ports buf).
    */

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CONNECT:Done executing connect command, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;

   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/**
 * As part of graph close command, APM sends disconnect first
 * this helps all containers stop accessing peer ports.
 */
ar_result_t gen_cntr_graph_disconnect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:DISCONNECT: Executing disconnect. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(me_ptr->cu.gu_ptr);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_DISCONNECT, TOPO_SG_STATE_INVALID));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   SPF_CRITICAL_SECTION_END(me_ptr->cu.gu_ptr);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:DISCONNECT:Done executing disconnect, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);
   return result;
}

/**
 * this is no longer used.
 */
ar_result_t gen_cntr_destroy_container(cu_base_t *base_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.gu.log_id;

   GEN_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:DESTROY:destroy received. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   if (me_ptr->cu.gu_ptr->data_path_thread_id != posal_thread_get_curr_tid())
   {
      // destroy will be done in main thread context and not in helper thread context
      base_ptr->handle_rest_fn      = gen_cntr_destroy;
      base_ptr->handle_rest_ctx_ptr = NULL;
      return AR_ETERMINATED; // to terminate from the thread pool
   }
   else
   {
      return gen_cntr_destroy(base_ptr, NULL);
   }
}

ar_result_t gen_cntr_ctrl_path_media_fmt_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PATH_MEDIA_FMT: Executing media format cmd. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_ctrl_path_media_fmt_cmd(base_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PATH_MEDIA_FMT:Done excuting media format cmd, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

ar_result_t gen_cntr_cmd_icb_info_from_downstream(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:FRAME_LEN_DS: ICB: Executing ICB from DS. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);
   TRY(result, cu_cmd_icb_info_from_downstream(base_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }
   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:FRAME_LEN_DS: ICB: Done excuting ICB info from DS, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

ar_result_t gen_cntr_handle_ctrl_port_trigger_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "CMD:CTRL_PORT_TRIGGER: triggerable control link msg. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_handle_ctrl_port_trigger_cmd(base_ptr));

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                DBG_LOW_PRIO,
                "CMD:CTRL_PORT_TRIGGER: Done triggerable control link msg, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   // if any module is enabled then we need to check if we can process topo based on that trigger.
   // Example: Enabling DTMF generator module need to enable topo process.
   gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   return result; // handle trig already acks this - should NOT do ack/return here
}

/* Handles Peer port property update command in GEN_CNTR contianer.*/
ar_result_t gen_cntr_handle_peer_port_property_update_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   SPF_MANAGE_CRITICAL_SECTION
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   SPF_CRITICAL_SECTION_START(base_ptr->gu_ptr);

   result = cu_handle_peer_port_property_update_cmd(base_ptr);

   SPF_CRITICAL_SECTION_END(base_ptr->gu_ptr);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);


   return result;
}

ar_result_t gen_cntr_handle_upstream_stop_cmd(cu_base_t *base_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   ar_result_t result = AR_EOK;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;

   gu_ext_in_port_t *dst_port_ptr = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;

   CU_MSG(me_ptr->topo.gu.log_id,
          DBG_HIGH_PRIO,
          "CMD:UPSTREAM_STOP_CMD: Executing upstream stop for (0x%lX, 0x%lx). current channel mask=0x%x",
          dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          dst_port_ptr->int_in_port_ptr->cmn.id,
          base_ptr->curr_chan_mask);

   // assuming the port is already in STOP state, this can be handled asynchronously
   gen_cntr_ext_in_port_t *ext_in_port_ptr  = (gen_cntr_ext_in_port_t *)(dst_port_ptr);
   gen_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */);

   CU_MSG(me_ptr->topo.gu.log_id,
          DBG_LOW_PRIO,
          "CMD:UPSTREAM_STOP_CMD: (0x%lX, 0x%lx) handling upstream stop done",
          dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          dst_port_ptr->int_in_port_ptr->cmn.id);

   gen_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

ar_result_t gen_cntr_set_get_cfg_util(cu_base_t                         *base_ptr,
                                      void                              *mod_ptr,
                                      uint32_t                           pid,
                                      int8_t                            *param_payload_ptr,
                                      uint32_t                          *param_size_ptr,
                                      uint32_t                          *error_code_ptr,
                                      bool_t                             is_set_cfg,
                                      bool_t                             is_deregister,
                                      spf_cfg_data_type_t                cfg_type,
                                      cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)mod_ptr;

   if (NULL == module_ptr)
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "gen_cntr_set_cfg_util: Module not found");
      return AR_EUNSUPPORTED;
   }

   // For placeholder module, once a real module is replaced, all params except RESET_PLACEHOLDER go to CAPI
   if (module_ptr->capi_ptr && (pid != PARAM_ID_RESET_PLACEHOLDER_MODULE))
   {
      if (is_set_cfg)
      {
         // If this is a low power container, we will not inform the module about persistence, so module will treat it
         // as non-persistent set-param
         if (((SPF_CFG_DATA_PERSISTENT == cfg_type) || (SPF_CFG_DATA_SHARED_PERSISTENT == cfg_type)) &&
             (FALSE == POSAL_IS_ISLAND_HEAP_ID(me_ptr->cu.heap_id)))
         {
            gen_topo_capi_set_persistence_prop(me_ptr->topo.gu.log_id, module_ptr, pid, is_deregister, cfg_type);
         }

         // when deregistering a persistent payload, we shouldn't call set param
         if (!is_deregister)
         {
            result |= gen_topo_capi_set_param(me_ptr->topo.gu.log_id,
                                              module_ptr->capi_ptr,
                                              pid,
                                              param_payload_ptr,
                                              *param_size_ptr);
         }
      }
      else /* GET_CFG */
      {
         result |= gen_topo_capi_get_param(me_ptr->topo.gu.log_id,
                                           module_ptr->capi_ptr,
                                           pid,
                                           param_payload_ptr,
                                           param_size_ptr);
      }
   }
   else
   {
      if (is_set_cfg)
      {
         gen_cntr_module_t *gen_cntr_module_ptr = (gen_cntr_module_t *)module_ptr;
         if (gen_cntr_module_ptr->fwk_module_ptr && gen_cntr_module_ptr->fwk_module_ptr->vtbl_ptr->set_cfg)
         {
            result = gen_cntr_module_ptr->fwk_module_ptr->vtbl_ptr->set_cfg(me_ptr,
                                                                            gen_cntr_module_ptr,
                                                                            pid,
                                                                            param_payload_ptr,
                                                                            *param_size_ptr,
                                                                            cfg_type,
                                                                            pending_set_cfg_ctx_pptr);
         }
         else
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_ERROR_PRIO,
                         " Unsupported framework module-id 0x%lX",
                         module_ptr->gu.module_id);
            result = AR_EUNSUPPORTED;
         }
      }
      else
      {
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_ERROR_PRIO,
                      "gen_cntr_set_cfg_util: Get-cfg not supported for framework modules");
         result = AR_EFAILED;
      }
   }

   /*Return Error code */
   *error_code_ptr = result;

   return result;
}

ar_result_t gen_cntr_set_event_prop_to_fwk_module(gen_cntr_t *       me_ptr,
                                                  gen_cntr_module_t *module_ptr,
                                                  topo_reg_event_t * event_cfg_payload_ptr,
                                                  bool_t             is_register)
{
   ar_result_t result = AR_EOK;

   if (module_ptr->fwk_module_ptr)
   {
      module_ptr->fwk_module_ptr->vtbl_ptr->reg_evt(me_ptr, module_ptr, event_cfg_payload_ptr, is_register);
   }
   else
   {
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "fwk module not present");
   }
   return result;
}

ar_result_t gen_cntr_register_events_utils(cu_base_t *       base_ptr,
                                           gu_module_t *     gu_module_ptr,
                                           topo_reg_event_t *reg_event_payload_ptr,
                                           bool_t            is_register,
                                           bool_t *          capi_supports_v1_event_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gu_module_ptr;

   if (module_ptr->capi_ptr)
   {
      result = gen_topo_set_event_reg_prop_to_capi_modules(me_ptr->topo.gu.log_id,
                                                           module_ptr->capi_ptr,
                                                           module_ptr,
                                                           reg_event_payload_ptr,
                                                           is_register,
                                                           capi_supports_v1_event_ptr);
   }
   else
   {
      result = gen_cntr_set_event_prop_to_fwk_module(me_ptr,
                                                     (gen_cntr_module_t *)module_ptr,
                                                     reg_event_payload_ptr,
                                                     is_register);
   }

   return result;
}

/**
 * For external ports, gen_cntr_operate_on_ext_in_port & gen_cntr_operate_on_ext_out_port is called
 * in 2 contexts:
 * 1. in the context of subgraph command: both ends of the connection belongs to the same SG.
 * 2. in the context of handle list of subgraph: this is an inter-SG connection.
 *
 * CLOSE, FLUSH, RESET, STOP are handled in this context.
 * START is not handled here, its handled based on downgraded state in gen_cntr_set_downgraded_state_on_output_port.
 */

ar_result_t gen_cntr_operate_on_ext_out_port(void *              base_ptr,
                                             uint32_t            sg_ops,
                                             gu_ext_out_port_t **ext_out_port_pptr,
                                             bool_t              is_self_sg)
{
   ar_result_t              result           = AR_EOK;
   gen_cntr_t              *me_ptr           = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)*ext_out_port_pptr;

   if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
   }
   // Nothing else to do for START/PREPARE/SUSPEND
   if ((TOPO_SG_OP_START | TOPO_SG_OP_PREPARE | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      return AR_EOK;
   }

   // for self SG STOP, flush/reset will be done in gen_cntr_operate_on_subgraph_async
   // for peer STOP, flush/reset will be done now (although it is repeating in apply_downgraded_state as well)
   if (is_self_sg && (TOPO_SG_OP_STOP & sg_ops))
   {
      return AR_EOK;
   }

   // inform module for ext conn (for self-sg case, operate_on_sg will take care at module level or at
   // inter-sg,intra-container level)
   if (!is_self_sg && (TOPO_SG_OP_CLOSE & sg_ops))
   {
      // if there is an attached module at the external output port then detach it.
      // detachment is done now to ensure state is propagated through all ports.
      if (ext_out_port_ptr->gu.int_out_port_ptr->attached_module_ptr)
      {
         gu_detach_module(&me_ptr->topo.gu,
                          ext_out_port_ptr->gu.int_out_port_ptr->attached_module_ptr,
                          me_ptr->cu.heap_id);
      }

      gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
      gen_topo_module_t      *module_ptr   = (gen_topo_module_t *)(out_port_ptr->gu.cmn.module_ptr);

      if (module_ptr->capi_ptr)
      {
         result = gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                             sg_ops,
                                                             &out_port_ptr->common.last_issued_opcode,
                                                             FALSE, /* is_input */
                                                             out_port_ptr->gu.cmn.index,
                                                             out_port_ptr->gu.cmn.id);
      }
      else
      {
         // framework module handling if any
      }
   }

   // flush and reset will be done in gen_cntr_deinit_ext_out_port
   if (TOPO_SG_OP_CLOSE & sg_ops)
   {
      if (CLIENT_ID_OLC == ext_out_port_ptr->client_config.cid.client_id)
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr;
         if (ext_out_port_ptr->cached_md_list_ptr)
         {
            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "MD_DBG: Destroy the cached metadata list, RD_EP miid 0x%lx",
                         out_port_ptr->gu.cmn.module_ptr->module_instance_id);
            // Metadata would be cache in Satellite RD_EP output port
            // Destroy the metadata if module is closed before ending the metadata
            gen_topo_destroy_all_metadata(me_ptr->cu.gu_ptr->log_id,
                                          (void *)out_port_ptr->gu.cmn.module_ptr,
                                          &ext_out_port_ptr->cached_md_list_ptr,
                                          TRUE /*is_dropped*/);
         }
      }
      ext_out_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
      return AR_EOK;
   }

   /* if disconnect came for the self SG then must handle the flush queue.
    * Case of Read shared memory end point in remote DSP.
    * 	APM is not going to send the DISCONNECT/CLOSE to the external output port connected to the ADSP.
    * 	It is possible that some read buffers are in Queue and those must be flushed at SG DISCONNECT
    * */
   bool_t is_disconnect_needed =
      ((TOPO_SG_OP_DISCONNECT & sg_ops) &&
       (is_self_sg || cu_is_disconnect_ext_out_port_needed((cu_base_t *)base_ptr, &ext_out_port_ptr->gu)));

   if (((TOPO_SG_OP_STOP | TOPO_SG_OP_FLUSH) & sg_ops) || (is_disconnect_needed))
   {
      gen_cntr_flush_output_data_queue(me_ptr, ext_out_port_ptr, TRUE);
      (void)gen_cntr_ext_out_port_reset(me_ptr, ext_out_port_ptr);
   }

   if (is_disconnect_needed)
   {
      ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr = NULL;
   }

   return result;
}

/* CLOSE, FLUSH, STOP are handled in gen_cntr_operate_on_ext_in_port context.
 * START, PREPARE and SUSPEND is not handled here, its handled based on downgraded state in
 * gen_cntr_set_downgraded_state_on_input_port
 */
ar_result_t gen_cntr_operate_on_ext_in_port(void *             base_ptr,
                                            uint32_t           sg_ops,
                                            gu_ext_in_port_t **ext_in_port_pptr,
                                            bool_t             is_self_sg)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_t             *me_ptr          = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)*ext_in_port_pptr;
   gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   // stop listening to the external input port for STOP/SUSPEND/CLOSE ops
   if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
   }

   // Nothing else to do for START/PREPARE/SUSPEND
   if ((TOPO_SG_OP_START | TOPO_SG_OP_PREPARE | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      return AR_EOK;
   }

   // for self SG STOP, flush/reset will be done in operate_on_subgraph_async
   // for peer STOP, Q flush must be done now synchronously.
   if (is_self_sg && (TOPO_SG_OP_STOP & sg_ops))
   {
      return AR_EOK;
   }

   if (!is_self_sg && (TOPO_SG_OP_CLOSE & sg_ops))
   {
      gen_topo_module_t *module_ptr = ((gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr);

      // inform module that the external input port is closing.
      if (module_ptr->capi_ptr)
      {
         result = gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                             sg_ops,
                                                             &in_port_ptr->common.last_issued_opcode,
                                                             TRUE /* is_input */,
                                                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                                                             ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
      }

      // Insert EOS to the next module if all upstream peer subgraphs are destroyed.
      if (me_ptr->topo.topo_to_cntr_vtable_ptr->check_insert_missing_eos_on_next_module)
      {
         me_ptr->topo.topo_to_cntr_vtable_ptr
            ->check_insert_missing_eos_on_next_module(&(me_ptr->topo),
                                                      (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);
      }
   }

   // flush and reset will be done in gen_cntr_deinit_ext_in_port
   if (TOPO_SG_OP_CLOSE & sg_ops)
   {
      ext_in_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
      return AR_EOK;
   }

   // check the comment on gen_cntr_operate_on_ext_out_port
   bool_t is_disconnect_needed =
      ((TOPO_SG_OP_DISCONNECT & sg_ops) &&
       (is_self_sg || cu_is_disconnect_ext_in_port_needed((cu_base_t *)base_ptr, &ext_in_port_ptr->gu)));

   // 1. Flush input Q if peer STOP is received
   // 2. Flush input Q if peer or self STOP is received
   // 2. Flush input Q if disconnecting from peer-US
   //
   // Note that if US is stopped and self is started we are flushing. We can ideally process the input buffers at
   // since self is started. But if we don't flush immediately, US CLOSE can be potentially delayed because it will
   // wait for the buffers.
   if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) || (is_disconnect_needed))
   {
      // For stop and flush operation, we need to preserve the MF (which is like stream associated MD)
      gen_cntr_flush_input_data_queue(me_ptr,
                                      ext_in_port_ptr,
                                      ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP) & sg_ops) /* keep data msg */);
   }

   // we cannot reset port except in self flush cases, as resetting puts the port in data flow state = at-gap.
   // we can reset only after EOS is propagated ( upstream to downstream)
   if ((is_self_sg && (TOPO_SG_OP_FLUSH & sg_ops)) || (is_disconnect_needed))
   {
      gen_cntr_ext_in_port_reset(me_ptr, ext_in_port_ptr);
   }

   if (is_disconnect_needed)
   {
      ext_in_port_ptr->gu.upstream_handle.spf_handle_ptr = NULL;
   }

   return result;
}

ar_result_t gen_cntr_operate_on_subgraph_async(void                      *base_ptr,
                                               uint32_t                   sg_ops,
                                               topo_sg_state_t            sg_state,
                                               gu_sg_t                   *gu_sg_ptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{

   INIT_EXCEPTION_HANDLING
   ar_result_t    result = AR_EOK;
   gen_cntr_t    *me_ptr = (gen_cntr_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   SPF_MANAGE_CRITICAL_SECTION

   // for STOP and SUSPEND, operation on modules can be done now.
   if (sg_ops & (TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND))
   {
      TRY(result,
          gen_topo_operate_on_modules((void *)&me_ptr->topo, sg_ops, sg_ptr->gu.module_list_ptr, spf_sg_list_ptr));
   }

   if (TOPO_SG_OP_STOP & sg_ops)
   {
      for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
           (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         if (ext_in_port_ptr->gu.sg_ptr == gu_sg_ptr)
         {
            SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

            // For stop operation, we need to preserve the MF (which is like stream associated MD)
            gen_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /* keep data msg */);
            (void)gen_cntr_ext_in_port_reset(me_ptr, ext_in_port_ptr);
            ext_in_port_ptr->flags.is_not_reset = FALSE;

            SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
         }
      }

      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         if (ext_out_port_ptr->gu.sg_ptr == gu_sg_ptr)
         {
            SPF_CRITICAL_SECTION_START(&me_ptr->topo.gu);

            gen_cntr_flush_output_data_queue(me_ptr, ext_out_port_ptr, TRUE /*is_client_cmd*/);
            (void)gen_cntr_ext_out_port_reset(me_ptr, ext_out_port_ptr);
            ext_out_port_ptr->flags.is_not_reset = FALSE;

            SPF_CRITICAL_SECTION_END(&me_ptr->topo.gu);
         }
      }
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

/**
 *    1. Operate on all the modules in the current subgraph.
 *    2. Operate on all the internal and external ports belonging to the subgraph.
 *    3. update SG state.
 * Note: This function must be called synchronously.
 */
ar_result_t gen_cntr_operate_on_subgraph(void                      *base_ptr,
                                         uint32_t                   sg_ops,
                                         topo_sg_state_t            sg_state,
                                         gu_sg_t                   *gu_sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t    result = AR_EOK;
   gen_cntr_t    *me_ptr = (gen_cntr_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   // operate on subgraph is a synchronous call so need to keep the processing only to the necessary ports.
   //  1. for STOP and SUSPEND; internal SG data ports and modules are handled in gen_cntr_operate_on_subgraph_async
   //  only external and SG boundary ports are handled here..
   //  2. for CLOSE; there is nothing to handle on modules and internal SG data ports, external and SG boundary ports
   //  are handled here..
   //  3. for START and PREPARE; only control port handling is needed which is done here.
   //  4. for DISCONNECT; there is nothing to handle on module and any internal data ports (even between two SG),
   //  external ports are handled here.
   //  5. for FLUSH; need to handle modules and all data ports synchronously here.
   bool_t dont_operate_on_any_data_port = (sg_ops & (TOPO_SG_OP_START | TOPO_SG_OP_PREPARE));
   bool_t operate_on_sg_boundary_ports  = (sg_ops & (TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND | TOPO_SG_OP_CLOSE));
   bool_t operate_on_all_internal_ports = (sg_ops & TOPO_SG_OP_FLUSH);

   // Handles frame work extension on the modules on before operating on the subgraph
   gen_cntr_handle_fwk_extn_pre_subgraph_op(me_ptr, sg_ops, sg_ptr->gu.module_list_ptr);

   for (gu_module_list_t *module_list_ptr = sg_ptr->gu.module_list_ptr; (NULL != module_list_ptr);
        LIST_ADVANCE(module_list_ptr))
   {
      gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

      for (gu_ctrl_port_list_t *ctrl_port_list_ptr = module_ptr->gu.ctrl_port_list_ptr; (NULL != ctrl_port_list_ptr);
           LIST_ADVANCE(ctrl_port_list_ptr))
      {
         gu_ctrl_port_t *ctrl_port_ptr = (gu_ctrl_port_t *)ctrl_port_list_ptr->ctrl_port_ptr;

         if (ctrl_port_ptr->ext_ctrl_port_ptr)
         {
            // Handles the case where both ends of the ctrl connection from this ext in port belong to the same SG.
            TRY(result,
                cu_operate_on_ext_ctrl_port(&me_ptr->cu,
                                            sg_ops,
                                            &ctrl_port_ptr->ext_ctrl_port_ptr,
                                            TRUE /* is_self_sg */));
         }
         else
         {
            // flush the control buf queue in case of STOP and CLOSE
            TRY(result, cu_operate_on_int_ctrl_port(&me_ptr->cu, sg_ops, &ctrl_port_ptr, TRUE /* is_self_sg */));

            // invalidate the connection in case of CLOSE
            TRY(result,
                gen_topo_operate_on_int_ctrl_port(&me_ptr->topo, ctrl_port_ptr, spf_sg_list_ptr, sg_ops, FALSE));
         }
      }

      if (dont_operate_on_any_data_port ||
          (!operate_on_all_internal_ports && // skip if there is no data-port to another SG or CNTR
           !(module_ptr->gu.flags.is_ds_at_sg_or_cntr_boundary || module_ptr->gu.flags.is_us_at_sg_or_cntr_boundary)))
      {
         continue;
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr; (NULL != out_port_list_ptr);
           LIST_ADVANCE(out_port_list_ptr))
      {
         gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

         if (out_port_ptr->gu.ext_out_port_ptr)
         {
            // Handles the case where both ends of this connection belong to the same SG.
            TRY(result,
                gen_cntr_operate_on_ext_out_port(&me_ptr->cu,
                                                 sg_ops,
                                                 &out_port_ptr->gu.ext_out_port_ptr,
                                                 TRUE /* is_self_sg */));

            // if (ext_out_port_ptr->gu.downstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
         else if (operate_on_all_internal_ports || // all internal ports for FLUSH
                  (operate_on_sg_boundary_ports &&
                   gen_topo_is_output_port_at_sg_boundary(out_port_ptr))) // all boundary ports for STOP/SUSPEND/CLOSE
         {
            bool_t SET_PORT_OP_FALSE = FALSE;
            TRY(result,
                gen_topo_operate_on_int_out_port(&me_ptr->topo,
                                                 &out_port_ptr->gu,
                                                 spf_sg_list_ptr,
                                                 sg_ops,
                                                 SET_PORT_OP_FALSE));
         }
      }

      if (sg_ops & TOPO_SG_OP_FLUSH)
      {
         // if internal metadata list has nodes, then destroy them before destroying any nodes in ports.
         //  this is to maintain order of metadata: output, internal list, input
         gen_topo_reset_module(&me_ptr->topo, module_ptr);
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (in_port_ptr->gu.ext_in_port_ptr)
         {
            // Handles the case where both ends of the connection from this ext in port belong to the same SG.
            TRY(result,
                gen_cntr_operate_on_ext_in_port(&me_ptr->cu,
                                                sg_ops,
                                                &in_port_ptr->gu.ext_in_port_ptr,
                                                TRUE /* is_self_sg */));

            // if (ext_in_port_ptr->gu.upstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
         else if (operate_on_all_internal_ports || // all internal ports for FLUSH
                  (operate_on_sg_boundary_ports &&
                   gen_topo_is_input_port_at_sg_boundary(in_port_ptr))) // all boundary ports for STOP/SUSPEND/CLOSE
         {
            bool_t SET_PORT_OP_FALSE = FALSE;
            TRY(result,
                gen_topo_operate_on_int_in_port(&me_ptr->topo,
                                                &in_port_ptr->gu,
                                                spf_sg_list_ptr,
                                                sg_ops,
                                                SET_PORT_OP_FALSE));
         }
      }
   }

   TRY(result, gen_cntr_handle_fwk_extn_post_subgraph_op(me_ptr, sg_ops, sg_ptr->gu.module_list_ptr));

   // Operation was successful (did not go to CATCH). Apply new state.
   if (TOPO_SG_STATE_INVALID != sg_state)
   {
      sg_ptr->state = sg_state;
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      sg_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   CATCH(result, GEN_CNTR_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}

ar_result_t gen_cntr_post_operate_on_ext_in_port(void *                     base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_in_port_t **        ext_in_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t             result          = AR_EOK;
   gen_cntr_t             *me_ptr          = (gen_cntr_t *)base_ptr;
   gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)*ext_in_port_pptr;
   gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   /* If ext input port receives both a self and peer stop/flush from upstream (any order)
    * eos need not be set since next DS container will anyway get the eos because of this self stop
    *
    * EOS needs to be inserted only if this port is started and it not already at gap
    */
   if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
       (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
       (TOPO_DATA_FLOW_STATE_AT_GAP != in_port_ptr->common.data_flow_state))
   {
      uint32_t is_upstream_realtime = FALSE;
      gen_topo_get_port_property(&me_ptr->topo,
                                 TOPO_DATA_INPUT_PORT_TYPE,
                                 PORT_PROPERTY_IS_UPSTREAM_RT,
                                 in_port_ptr,
                                 &is_upstream_realtime);

      /* If upstream is real-time we can avoid sending eos downstream for FLUSH command since this implies upstream
      is in STARTED state and will be pumping data in real time. If we flush while upstream is in STARTED state,
      it might lead to an infinite loop since data is dropped while new data is coming in.
      In current cases, when we seek, upstream sg is flushed and spr will be pumping 0's downstream which flushes out
      the device leg while ensuring there are no endpoint underruns. Upstream rt check will not work in cases where
      timestamp is valid since it would lead to ts discontinuities due to dropping of data at ext inp of device leg.

      We still need eos to be sent when upstream is STOPPED, to flush out any data stuck in the device pipeline.
    */
      if ((TOPO_SG_OP_STOP & sg_ops) || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))
      {
         module_cmn_md_eos_flags_t eos_md_flag = { .word = 0 };
         eos_md_flag.is_flushing_eos           = TRUE;
         eos_md_flag.is_internal_eos           = TRUE;

         result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                               (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr,
                                               ext_in_port_ptr->cu.id,
                                               me_ptr->cu.heap_id,
                                               &ext_in_port_ptr->md_list_ptr,
                                               NULL,         /* md_flag_ptr */
                                               NULL,         /*tracking_payload_ptr*/
                                               &eos_md_flag, /* eos_payload_flags */
                                               gen_cntr_get_bytes_in_ext_in_for_md(ext_in_port_ptr),
                                               &ext_in_port_ptr->cu.media_fmt);

         if (AR_SUCCEEDED(result))
         {
            me_ptr->topo.flags.process_us_gap = TRUE;
         }
         GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                      DBG_LOW_PRIO,
                      "MD_DBG: Created EOS for ext in port (0x%0lX, 0x%lx) with result 0x%lx",
                      in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                      in_port_ptr->gu.cmn.id,
                      result);
      }
      else if (TOPO_SG_OP_SUSPEND & sg_ops)
      {
         module_cmn_md_t *out_md_ptr = NULL;

         result = gen_topo_create_dfg_metadata(&me_ptr->topo,
                                               &ext_in_port_ptr->md_list_ptr,
                                               me_ptr->cu.heap_id,
                                               &out_md_ptr,
                                               ext_in_port_ptr->buf.actual_data_len,
                                               &ext_in_port_ptr->cu.media_fmt);
         if (out_md_ptr)
         {
            // set the flag to call process frames and propagate DFG out
            me_ptr->topo.flags.process_us_gap = TRUE;

            GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                         DBG_HIGH_PRIO,
                         "MD_DBG: Inserted DFG at ext in port (0x%lX, 0x%lx) at offset %lu, result %ld",
                         in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                         in_port_ptr->gu.cmn.id,
                         out_md_ptr->offset,
                         result);
         }
      }
   }

   return result;
}

static ar_result_t gen_cntr_post_operate_on_connected_input(gen_cntr_t *               me_ptr,
                                                            gen_topo_output_port_t *   out_port_ptr,
                                                            spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                                            uint32_t                   sg_ops)
{
   ar_result_t            result           = AR_EOK;
   gen_topo_module_t *    module_ptr       = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   gen_topo_input_port_t *conn_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
   if (conn_in_port_ptr)
   {
      gen_topo_sg_t *other_sg_ptr = (gen_topo_sg_t *)conn_in_port_ptr->gu.cmn.module_ptr->sg_ptr;
      if (!gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, other_sg_ptr->gu.id))
      {
         // see notes in gen_cntr_post_operate_on_ext_in_port
         if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
             (TOPO_PORT_STATE_STARTED == conn_in_port_ptr->common.state) &&
             (TOPO_DATA_FLOW_STATE_AT_GAP != conn_in_port_ptr->common.data_flow_state))
         {
            // If sg op is,
            // STOP/FLUSH - insert internal flushing eos
            // SUSPEND - insert DFG
            uint32_t is_upstream_realtime = FALSE;
            gen_topo_get_port_property(&me_ptr->topo,
                                       TOPO_DATA_OUTPUT_PORT_TYPE,
                                       PORT_PROPERTY_IS_UPSTREAM_RT,
                                       out_port_ptr,
                                       &is_upstream_realtime);

            /* If upstream is real-time we can avoid sending eos downstream for FLUSH command since this implies
               upstream is in STARTED state and will be pumping data in real time. If we flush while upstream is in
               STARTED state, it might lead to an infinite loop since data is dropped while new data is coming in.
               In current cases, when we seek, upstream sg is flushed and spr will be pumping 0's downstream which
               flushes out the device leg while ensuring there are no endpoint underruns. Upstream rt check will not
               work in cases where timestamp is valid since it would lead to ts discontinuities due to dropping of
               data at ext inp of device leg.

               We still need eos to be sent when upstream is STOPPED, to flush out any data stuck in the device
               pipeline.
            */
            if ((TOPO_SG_OP_STOP & sg_ops) || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))
            {
               bool_t INPUT_PORT_ID_NONE             = 0; // Internal input ports don't have unique ids. Use 0 instead.
               module_cmn_md_eos_flags_t eos_md_flag = { .word = 0 };
               eos_md_flag.is_flushing_eos           = TRUE;
               eos_md_flag.is_internal_eos           = TRUE;

               uint32_t bytes_across_ch = gen_topo_get_actual_len_for_md_prop(&conn_in_port_ptr->common);

               result = gen_topo_create_eos_for_cntr(&me_ptr->topo,
                                                     (gen_topo_input_port_t *)conn_in_port_ptr,
                                                     INPUT_PORT_ID_NONE,
                                                     me_ptr->cu.heap_id,
                                                     &conn_in_port_ptr->common.sdata.metadata_list_ptr,
                                                     NULL,         /* md_flag_ptr */
                                                     NULL,         /*tracking_payload_ptr*/
                                                     &eos_md_flag, /* eos_payload_flags */
                                                     bytes_across_ch,
                                                     conn_in_port_ptr->common.media_fmt_ptr);

               if (AR_SUCCEEDED(result))
               {
                  me_ptr->topo.flags.process_us_gap               = TRUE;
                  conn_in_port_ptr->common.sdata.flags.marker_eos = TRUE;
               }

               GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                            DBG_MED_PRIO,
                            "MD_DBG: EOS set at SG boundary within container (0x%lX, 0x%lx) to (0x%lX, 0x%lx), "
                            "result "
                            "%ld",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                            conn_in_port_ptr->gu.cmn.id,
                            result);

               // for internal ports, flushing EOS needs zeros, which must be assigned here.
               gen_topo_set_pending_zeros((gen_topo_module_t *)conn_in_port_ptr->gu.cmn.module_ptr, conn_in_port_ptr);
            }
            else if (TOPO_SG_OP_SUSPEND & sg_ops) // insert DFG
            {
               module_cmn_md_t *out_md_ptr = NULL;

               uint32_t bytes_across_all_ch = gen_topo_get_actual_len_for_md_prop(&conn_in_port_ptr->common);
               result                       = gen_topo_create_dfg_metadata(&me_ptr->topo,
                                                     &conn_in_port_ptr->common.sdata.metadata_list_ptr,
                                                     me_ptr->cu.heap_id,
                                                     &out_md_ptr,
                                                     bytes_across_all_ch,
                                                     conn_in_port_ptr->common.media_fmt_ptr);
               if (out_md_ptr)
               {
                  // set the flag to call process frames and propagate DFG out
                  me_ptr->topo.flags.process_us_gap = TRUE;

                  GEN_CNTR_MSG(me_ptr->topo.gu.log_id,
                               DBG_HIGH_PRIO,
                               "MD_DBG: DFG set at SG boundary within container (0x%lX, 0x%lx) to (0x%lX, 0x%lx) at "
                               "offset %lu, result "
                               "%ld",
                               module_ptr->gu.module_instance_id,
                               out_port_ptr->gu.cmn.id,
                               conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                               conn_in_port_ptr->gu.cmn.id,
                               out_md_ptr->offset,
                               result);
               }
            }
         }
      }
   }
   return result;
}

ar_result_t gen_cntr_post_operate_on_subgraph(void *                     base_ptr,
                                              uint32_t                   sg_ops,
                                              topo_sg_state_t            sg_state,
                                              gu_sg_t *                  gu_sg_ptr,
                                              spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t    result = AR_EOK;
   gen_cntr_t    *me_ptr = (gen_cntr_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   if ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      for (gu_module_list_t *module_list_ptr = sg_ptr->gu.module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         if (!module_ptr->gu.flags.is_ds_at_sg_or_cntr_boundary)
         {
            continue;
         }

         for (gu_output_port_list_t *out_port_list_ptr = module_ptr->gu.output_port_list_ptr;
              (NULL != out_port_list_ptr);
              LIST_ADVANCE(out_port_list_ptr))
         {
            gen_topo_output_port_t *out_port_ptr = (gen_topo_output_port_t *)out_port_list_ptr->op_port_ptr;

            // Check if connected port is not in a subgraph being operated on.
            gen_cntr_post_operate_on_connected_input(me_ptr, out_port_ptr, spf_sg_list_ptr, sg_ops);
         }
      }
   }

   if (check_if_pass_thru_container(me_ptr))
   {
      // if SG/external output is starting immediately update the proc list
      // todo: add explanation why only ops/sg are checked   // if SG/external output is stopped immediately update the
      // proc list, because it can alter the module proc list and update the module buffer assignment as well todo: need
      // to update the proc list when the peer port state proapgation command is recieved as well.
      if ((TOPO_SG_OP_START | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
      {
         GEN_CNTR_MSG(me_ptr->cu.gu_ptr->log_id,
                      DBG_LOW_PRIO,
                      "cu_handle_sg_mgmt_cmd. sg_op %lu hanlding post operate on pass thru container sub graph 0x%lx",
                      sg_ops,
                      sg_ptr->gu.id);

         result |= pt_cntr_update_module_process_list((pt_cntr_t *)me_ptr);
         result |= pt_cntr_assign_port_buffers((pt_cntr_t *)me_ptr);
      }
   }

   return result;
}

ar_result_t gen_cntr_initiate_duty_cycle_island_entry(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;

   if (!me_ptr->cu.pm_info.flags.module_disallows_duty_cycling)
   {
      gen_cntr_listen_to_controls(me_ptr);
      gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);
      cu_send_island_entry_ack_to_dcm(&me_ptr->cu);
   }
   return result;
}

ar_result_t gen_cntr_initiate_duty_cycle_island_exit(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   gen_cntr_t *me_ptr = (gen_cntr_t *)base_ptr;
   if (!me_ptr->cu.pm_info.flags.module_disallows_duty_cycling)
   {
      gen_cntr_handle_events_after_cmds(me_ptr, FALSE, result);
   }
   return result;
}
