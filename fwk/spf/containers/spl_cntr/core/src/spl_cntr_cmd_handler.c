/**
 * \file spl_cntr_cmd_handler.c
 * \brief
 *     This file contains spl_cntr functions for command handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/**
 * Choose the frame size based on perf mode of all subgraphs.
 */
static ar_result_t spl_cntr_check_sg_cfg_for_frame_size(spl_cntr_t *me_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   ar_result_t result                            = AR_EOK;
   uint32_t    sg_perf_mode                      = 0;
   uint32_t    direction                         = 0;
   uint32_t    new_configured_frame_size_us      = 0;
   uint32_t    new_configured_frame_size_samples = 0;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; sg_list_ptr; LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;

      /* If 0 -> perf mode not been set before, if it is low power perf mode always pick it
       because it implies higher frame size */
      if ((0 == sg_perf_mode) || (APM_SG_PERF_MODE_LOW_POWER == sg_ptr->perf_mode))
      {
         sg_perf_mode = sg_ptr->perf_mode;
      }

      direction = sg_ptr->direction;
   }

   /* Setting container operating frame size to 20ms for stream PP in voice call use cases,
    * and this will not change after graph open as voice call stream PP would not have any
    * threshold modules. This is to optimize stream PP as voice encoder always require 20ms packet */
   if (APM_CONT_GRAPH_POS_STREAM == me_ptr->cu.position && cu_has_voice_sid(&me_ptr->cu))
   {
      new_configured_frame_size_us = FRAME_LEN_20000_US;

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Setting container frame length to 20ms for this stream PP with voice SID");
   }
   else if (APM_SUB_GRAPH_DIRECTION_RX == direction && cu_has_voice_sid(&me_ptr->cu))
   {
      new_configured_frame_size_us = FRAME_LEN_20000_US;

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Setting container frame length to 20ms for this rx device PP with voice SID");
   }
   else
   {
      if (me_ptr->cu.conf_frame_len.frame_len_us)
      {
         // Container frame len property is configured for frame length in time.
         new_configured_frame_size_us = me_ptr->cu.conf_frame_len.frame_len_us;
      }
      else if (me_ptr->cu.conf_frame_len.frame_len_samples)
      {
         new_configured_frame_size_us      = 0;
         new_configured_frame_size_samples = me_ptr->cu.conf_frame_len.frame_len_samples;
      }
      else
      {
         if (APM_SG_PERF_MODE_LOW_LATENCY == sg_perf_mode)
         {
            new_configured_frame_size_us = FRAME_LEN_1000_US;
         }
         else // (APM_SG_PERF_MODE_LOW_POWER == sg_perf_mode)
         {
            if (APM_CONT_GRAPH_POS_STREAM == me_ptr->cu.position)
            {
               new_configured_frame_size_us = FRAME_LEN_10000_US;
            }
            else
            {
               new_configured_frame_size_us = FRAME_LEN_5000_US;
            }
         }
      }
   }

   // frame size in samples is set from container property so it will be updated only once during first subgraph open.
   if (new_configured_frame_size_samples != 0 &&
       (new_configured_frame_size_samples != me_ptr->threshold_data.configured_frame_size_samples))
   {
      me_ptr->threshold_data.configured_frame_size_samples = new_configured_frame_size_samples;

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "CMD:GRAPH_OPEN: SPL_CNTR configured frame size is now %ld samples.",
                   me_ptr->threshold_data.configured_frame_size_samples);
   }

   // if frame size is not set in samples then check udpate in time
   if (0 == me_ptr->threshold_data.configured_frame_size_samples && new_configured_frame_size_us)
   {
      // If frame size is (0) or (greater AND sync fwk extension isn't present), set it
      // With sync fwk extension, attempt to retain the configured frame size during
      //  subsequent graph opens as well. Any port threshold raised by the modules in the
      //  topology would kick in & be handled by spl_cntr_determine_update_cntr_frame_len_us_from_cfg_and_thresh()
      if ((0 == me_ptr->threshold_data.configured_frame_size_us) ||
          (me_ptr->threshold_data.configured_frame_size_us < new_configured_frame_size_us &&
           !me_ptr->topo.t_base.flags.is_sync_module_present))
      {
         if ((0 != me_ptr->threshold_data.configured_frame_size_us) &&
             (spl_topo_fwk_ext_is_fixed_out_dm_module_present(me_ptr->topo.fwk_extn_info.dm_info)))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "CMD:GRAPH_OPEN:Failure during threshold change with DM module present");
            return AR_EFAILED;
         }

         me_ptr->threshold_data.configured_frame_size_us = new_configured_frame_size_us;

         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "CMD:GRAPH_OPEN: SPL_CNTR configured frame size is now %ld us.",
                      me_ptr->threshold_data.configured_frame_size_us);
      }
   }

   return result;
}

/**
 * Wrapper function to cu_set_get_cfgs_packed which calls set_param_begin/end beforehand for set_cfgs.
 */
static ar_result_t spl_cntr_set_get_cfgs_packed(spl_cntr_t         *me_ptr,
                                                gpr_packet_t       *packet_ptr,
                                                spf_cfg_data_type_t cfg_type)
{
   ar_result_t result     = AR_EOK;
   bool_t      is_set_cfg = FALSE;
   switch (packet_ptr->opcode)
   {
      case APM_CMD_SET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      case APM_CMD_REGISTER_SHARED_CFG:
      case APM_CMD_DEREGISTER_SHARED_CFG:
      {
         is_set_cfg = TRUE;
         break;
      }
   }

   if (is_set_cfg)
   {
      spl_topo_set_param_begin(&me_ptr->topo);
   }

   result = cu_set_get_cfgs_packed(&me_ptr->cu, packet_ptr, cfg_type);

   if (is_set_cfg)
   {
      spl_topo_set_param_end(&me_ptr->topo);
   }

   return result;
}

/**
 * Wrapper function to cu_set_get_cfgs_fragmented which calls set_param_begin/end beforehand for set_cfgs.
 */
static ar_result_t spl_cntr_set_get_cfgs_fragmented(spl_cntr_t               *me_ptr,
                                                    apm_module_param_data_t **param_data_pptr,
                                                    uint32_t                  num_param_id_cfg,
                                                    bool_t                    is_set_cfg,
                                                    bool_t                    is_deregister,
                                                    spf_cfg_data_type_t       data_type)
{
   ar_result_t result = AR_EOK;

   if (is_set_cfg)
   {
      spl_topo_set_param_begin(&me_ptr->topo);
   }

   result =
      cu_set_get_cfgs_fragmented(&me_ptr->cu, param_data_pptr, num_param_id_cfg, is_set_cfg, is_deregister, data_type);

   if (is_set_cfg)
   {
      spl_topo_set_param_end(&me_ptr->topo);
   }

   return result;
}

/**
 * Reset of spl_cntr_t fields. Reset means any new data should be treated as
 * the beginning of data (no continuity between old data and new data).
 */
static ar_result_t spl_cntr_reset(spl_cntr_t *me_ptr)
{
   ar_result_t result = AR_EOK;
#if 0
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "SPL_CNTR reset ");

   uint8_t reset_log_id = me_ptr->topo.t_base.gu.log_id & 0xFF;
   reset_log_id++;
   me_ptr->topo.t_base.gu.log_id = me_ptr->topo.t_base.gu.log_id | (uint32_t)reset_log_id;
#endif
   return result;
}

ar_result_t spl_cntr_allocate_gpd_mask_arr(spl_cntr_t *me_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result                     = AR_EOK;
   uint32_t    curr_num_of_parallel_paths = me_ptr->topo.t_base.gu.num_parallel_paths;
   gu_update_parallel_paths(&(me_ptr->topo.t_base.gu));

   // at least assign one parallel path
   me_ptr->topo.t_base.gu.num_parallel_paths =
      (0 == me_ptr->topo.t_base.gu.num_parallel_paths) ? 1 : me_ptr->topo.t_base.gu.num_parallel_paths;

   if (curr_num_of_parallel_paths != me_ptr->topo.t_base.gu.num_parallel_paths)
   {
      MFREE_REALLOC_MEMSET(me_ptr->gpd_mask_arr,
                           uint32_t,
                           sizeof(uint32_t) * me_ptr->topo.t_base.gu.num_parallel_paths,
                           me_ptr->cu.heap_id,
                           result);
   }

   spl_cntr_update_gpd_mask_for_parallel_paths(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
      me_ptr->topo.t_base.gu.num_parallel_paths = 0;
   }
   return result;
}

/**
 * handles events after commands.
 * typically required for set-cfg, but also in other scenarios:
 *  - A SG is opened and running and another new one joins. new module raises threshold event.
 *    If we don't handle right after command, then after some data processing, events might be handled causing data
 *       drops (threshold/MF prop can cause data drop).
 *  - start command to a data port may raise trigger policy
 */
static ar_result_t spl_cntr_handle_events_after_cmds(spl_cntr_t *me_ptr, bool_t is_ack_cmd, ar_result_t rsp_result)
{
   ar_result_t                 result              = AR_EOK;
   cu_base_t                  *base_ptr            = &me_ptr->cu;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   // no need to reconcile in SC
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr,
                                                       &me_ptr->topo.t_base,
                                                       FALSE /*do_reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do_reconcile*/);

   /** no need to handle event when handle rest is pending and some events occur when container is not started. */
   if (me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling ||
       (!cu_is_any_handle_rest_pending(base_ptr) &&
        (fwk_event_flag_ptr->sg_state_change ||
         (me_ptr->cu.flags.is_cntr_started &&
          (fwk_event_flag_ptr->word || capi_event_flag_ptr->word || me_ptr->topo.simpt_event_flags.word)))))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_LOW_PRIO,
                   "Handling events after commands, fwk events 0x%lx, capi events 0x%lx, "
                   "sync_extn_pending_inactive_handling %lu",
                   fwk_event_flag_ptr->word,
                   capi_event_flag_ptr->word,
                   me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling);

      spl_cntr_handle_fwk_events(me_ptr, FALSE /*is_data path*/);
   }

   me_ptr->cu.flags.apm_cmd_context = FALSE;

   // send the ack for the command before topo process.
   // this should be done after handling events so that all the votes are in order.
   if (is_ack_cmd)
   {
      spf_msg_ack_msg(&me_ptr->cu.cmd_msg, rsp_result);
   }

   // if container is started then check and update gpd
   if (me_ptr->cu.flags.is_cntr_started)
   {
      spl_cntr_update_gpd_and_process(me_ptr);
   }

   return result;
}

/**
 * CU vtable implementation of set/get cfg which calls the topology function
 * to call set/get on the proper CAPI module.
 */
ar_result_t spl_cntr_set_get_cfg_util(cu_base_t                         *base_ptr,
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
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)mod_ptr;

   // Ensure module was found.
   VERIFY(result, module_ptr);
   if (module_ptr->capi_ptr == NULL)
   {
      return result = AR_EUNSUPPORTED;
   }

   // fwk handling before calling set/get param.

   if (is_set_cfg)
   {
      // If this is a low power container, we will not inform the module about persistence, so module will treat it as
      // non-persistent set-param
      if (((SPF_CFG_DATA_PERSISTENT == cfg_type) || (SPF_CFG_DATA_SHARED_PERSISTENT == cfg_type)) &&
          (FALSE == POSAL_IS_ISLAND_HEAP_ID(me_ptr->cu.heap_id)))
      {
         gen_topo_capi_set_persistence_prop(me_ptr->topo.t_base.gu.log_id, module_ptr, pid, is_deregister, cfg_type);
      }

      // Don't do set param in the case of deregistering persistence cfg type.
      if (!is_deregister)
      {
         *error_code_ptr = gen_topo_capi_set_param(me_ptr->topo.t_base.gu.log_id,
                                                   module_ptr->capi_ptr,
                                                   pid,
                                                   param_payload_ptr,
                                                   *param_size_ptr);
      }
   }
   else
   {
      *error_code_ptr = gen_topo_capi_get_param(me_ptr->topo.t_base.gu.log_id,
                                                module_ptr->capi_ptr,
                                                pid,
                                                param_payload_ptr,
                                                param_size_ptr);
   }

   result = *error_code_ptr;

   // fwk handling after calling set/get param.

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }
   return result;
}

ar_result_t spl_cntr_register_events_utils(cu_base_t        *base_ptr,
                                           gu_module_t      *gu_module_ptr,
                                           topo_reg_event_t *reg_event_payload_ptr,
                                           bool_t            is_register,
                                           bool_t           *capi_supports_v1_event_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   gen_topo_module_t *module_ptr = (gen_topo_module_t *)gu_module_ptr;

   if (module_ptr->capi_ptr)
   {
      result = gen_topo_set_event_reg_prop_to_capi_modules(me_ptr->topo.t_base.gu.log_id,
                                                           module_ptr->capi_ptr,
                                                           module_ptr,
                                                           reg_event_payload_ptr,
                                                           is_register,
                                                           capi_supports_v1_event_ptr);
   }
   else
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_ERROR_PRIO, "no framework module supported in SPL_CNTR");
      result = AR_EFAILED;
   }

   return result;
}

static ar_result_t spl_cntr_handle_rest_of_graph_close(cu_base_t *base_ptr, bool_t is_handle_cu_cmd_mgmt)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   SPF_MANAGE_CRITICAL_SECTION

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);

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

   // update module flags for remaining modules
   spl_cntr_update_all_modules_info(me_ptr);

   gen_topo_reset_top_level_flags(&(me_ptr->topo.t_base));

   if (me_ptr->cu.gu_ptr->sg_list_ptr)
   {
      /* update the nblc chain for non-closing subgraphs.
       * connection (external or internal) between closing and non-closing SGs are already destroyed at this point.
       */
      gen_topo_assign_non_buf_lin_chains(&(me_ptr->topo.t_base));

      spl_topo_fwk_ext_update_dm_modes(&(me_ptr->topo));

      // propagated threshold state after dm mode update
      gen_topo_fwk_extn_sync_propagate_threshold_state(&me_ptr->topo.t_base);
   }

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);

   //======== following code must be able to run in parallel to the data processing =============

   // internal control ports have their own outgoing buffer queues. They need to be drained and destroyed when the
   // sg is closed.
   cu_deinit_internal_ctrl_ports(&me_ptr->cu, FALSE /*b_destroy_all_ports*/);

   // module destroy happens only with SG destroy
   spl_topo_destroy_modules(&me_ptr->topo, FALSE /*b_destroy_all_modules*/);

   // Deinit external ports closing as part of self subgraph close..
   // Also, ** very important** to call gen_topo_destroy_modules before cu_deinit_external_ports.
   // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
   // deinit.
   cu_deinit_external_ports(&me_ptr->cu,
                            FALSE, /*b_ignore_ports_from_sg_close*/
                            FALSE /*force_deinit_all_ports*/);

   gu_finish_async_destroy(me_ptr->cu.gu_ptr);

   // if module or ports are closed then need to realloc scratch memory.
   GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&me_ptr->topo.t_base, realloc_scratch_mem);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);

   // dangling data-ports are already destroyed synchronously before, now only need to destroy dangling control ports
   gu_cleanup_danling_control_ports(me_ptr->cu.gu_ptr);

   // update the simplified connections.
   spl_topo_update_simp_module_connections(&me_ptr->topo);

   if (me_ptr->cu.gu_ptr->sg_list_ptr)
   {
      spl_cntr_allocate_gpd_mask_arr(me_ptr);
   }

   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);

   return result;
}

/**
 * Error case handling of graph open. Destroys relevant subgraphs and acks back.
 */
static void spl_cntr_handle_failure_at_graph_open(spl_cntr_t *me_ptr, ar_result_t result)
{
   SPF_MANAGE_CRITICAL_SECTION

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);
   spf_msg_cmd_graph_open_t *open_cmd_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   gu_finish_async_create(&me_ptr->topo.t_base.gu, me_ptr->cu.heap_id);

   gu_prepare_cleanup_for_graph_open_failure(&me_ptr->topo.t_base.gu, open_cmd_ptr);

   spl_cntr_handle_rest_of_graph_close(&me_ptr->cu, FALSE /*is_handle_cu_cmd_mgmt*/);

   spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);
}

/**
 * Graph open handling after relaunching the thread. The main reason this is
 * needed (compared to all handling before relaunching the thread) is that
 * calibration might have new stack size requirements.
 */
static ar_result_t spl_cntr_handle_rest_of_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION
   spl_cntr_t               *me_ptr = (spl_cntr_t *)base_ptr;
   spf_msg_cmd_graph_open_t *open_cmd_ptr;
   bool_t                    pm_already_registered   = posal_power_mgr_is_registered(me_ptr->cu.pm_info.pm_handle_ptr);
   bool_t                    is_duty_cycling_allowed = FALSE;
   bool_t                    dcm_already_registered  = me_ptr->cu.pm_info.flags.register_with_dcm;

   gen_topo_graph_init_t   graph_init_data       = { 0 };
   gu_ext_in_port_list_t  *ext_in_port_list_ptr  = NULL;
   gu_ext_out_port_list_t *ext_out_port_list_ptr = NULL;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   open_cmd_ptr                 = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   cu_reset_handle_rest(base_ptr);

   /* create modules asynchronously, avoid working on connection to the existing modules */
   graph_init_data.spf_handle_ptr                = &me_ptr->cu.spf_handle;
   graph_init_data.gpr_cb_fn                     = cu_gpr_callback;
   graph_init_data.capi_cb                       = spl_topo_capi_callback;
   graph_init_data.input_bufs_ptr_arr_not_needed = TRUE;
   TRY(result, gen_topo_create_modules(&me_ptr->topo.t_base, &graph_init_data));

   /* Allocate memory for voice info structure, for voice call use cases*/
   TRY(result, cu_create_voice_info(&me_ptr->cu, open_cmd_ptr));

   TRY(result, cu_init_external_ports(&me_ptr->cu, SPL_CNTR_EXT_CTRL_PORT_Q_OFFSET));

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);

   // merge the new graph with the existing one
   TRY(result, gu_finish_async_create(&me_ptr->topo.t_base.gu, me_ptr->cu.heap_id));

   // note: since newly opened modules/ports are not going to change the order of started_sorted_module_list therefore
   // not updating it

   spl_cntr_allocate_gpd_mask_arr(me_ptr);

   // initialize the connections to the existing modules
   graph_init_data.skip_scratch_mem_reallocation = TRUE; // already allocated before.
   TRY(result, gen_topo_create_modules(&me_ptr->topo.t_base, &graph_init_data));

   // to update the trigger policy related flags.
   gen_topo_reset_top_level_flags(&(me_ptr->topo.t_base));

   if (open_cmd_ptr->num_module_conn)
   {
      // update nblc.
      gen_topo_assign_non_buf_lin_chains(&me_ptr->topo.t_base);

      TRY(result, spl_topo_fwk_ext_update_dm_modes(&me_ptr->topo));

      gen_topo_fwk_extn_sync_propagate_threshold_state(&me_ptr->topo.t_base);
   }

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   // Check sg properties and derive frame size. We can skip this if there aren't any sgs in the
   // open command.
   if (open_cmd_ptr->num_sub_graphs)
   {

      apm_sub_graph_cfg_t *sg_cmd_ptr = open_cmd_ptr->sg_cfg_list_pptr[0];
      gu_sg_t             *sg_ptr     = gu_find_subgraph(&me_ptr->topo.t_base.gu, sg_cmd_ptr->sub_graph_id);
      VERIFY(result, sg_ptr);

      spl_cntr_check_sg_cfg_for_frame_size(me_ptr, open_cmd_ptr);
   }

   me_ptr->topo.flags.is_real_time_topo = FALSE;

   // Check if RT and set frame size
   for (ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      gu_sg_t                *sg_ptr          = ext_in_port_ptr->gu.sg_ptr;

      ext_in_port_ptr->is_realtime_usecase = FALSE;

      //marking real time for voice containers so that all buffers from the queue can be pulled before going through process.
      //when upstream is smaller frame size compared to the downstream then container may have to wait for multiple upstream buffers.
      if (((APM_CONT_GRAPH_POS_STREAM != me_ptr->cu.position) && (APM_SUB_GRAPH_DIRECTION_TX == sg_ptr->direction)) || cu_has_voice_sid(&me_ptr->cu))
      {
         ext_in_port_ptr->is_realtime_usecase        = TRUE;
         me_ptr->topo.flags.is_real_time_topo = TRUE;
      }

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "External input port idx = %ld miid = 0x%lx is_realtime_usecase set to %ld",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_in_port_ptr->is_realtime_usecase);
   }

   for (ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr; ext_out_port_list_ptr;
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      gu_sg_t                 *sg_ptr           = ext_out_port_ptr->gu.sg_ptr;

      ext_out_port_ptr->is_realtime_usecase = FALSE;

      //marking real time for voice containers so that all buffers from the queue can be pulled before going through process.
      //when upstream is smaller frame size compared to the downstream then container may have to wait for multiple upstream buffers.
      if (((APM_CONT_GRAPH_POS_STREAM != me_ptr->cu.position) && (APM_SUB_GRAPH_DIRECTION_TX == sg_ptr->direction)) || cu_has_voice_sid(&me_ptr->cu))
      {
         ext_out_port_ptr->is_realtime_usecase       = TRUE;
         me_ptr->topo.flags.is_real_time_topo = TRUE;
      }

      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "External output port idx = %ld miid = 0x%lx is_realtime_usecase set to %ld",
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.index,
                   ext_out_port_ptr->gu.int_out_port_ptr->cmn.module_ptr->module_instance_id,
                   ext_out_port_ptr->is_realtime_usecase);
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1
   gu_print_graph(&me_ptr->topo.t_base.gu);
#endif

   // Handle Set config payload
   TRY(result,
       spl_cntr_set_get_cfgs_fragmented(me_ptr,
                                        (apm_module_param_data_t **)open_cmd_ptr->param_data_pptr,
                                        open_cmd_ptr->num_param_id_cfg,
                                        TRUE /* is_set_cfg */,
                                        FALSE,                       /*is_deregister (Don't care here)*/
                                        SPF_CFG_DATA_TYPE_DEFAULT)); // no module will be in start state during open

   if (cu_is_any_handle_rest_pending(base_ptr))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: handle rest set during set-get-cfg of graph open");
      return result;
   }

   is_duty_cycling_allowed = cu_check_all_subgraphs_duty_cycling_allowed(&me_ptr->cu);

   // If DCM stubbed, want to fail graph_open on DCM registration attempt
   if ((is_duty_cycling_allowed) && (!dcm_already_registered))
   {
      TRY(result, cu_register_with_dcm(&me_ptr->cu));
   }

   // in success case, ack to APM is sent from this func.
   TRY(result, gu_respond_to_graph_open(&me_ptr->topo.t_base.gu, &me_ptr->cu.cmd_msg, me_ptr->cu.heap_id));

   // Register container with pm
   cu_register_with_pm(&me_ptr->cu, is_duty_cycling_allowed);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
      if (pm_already_registered)
      {
         cu_deregister_with_pm(&me_ptr->cu);
      }

      if ((is_duty_cycling_allowed) && (dcm_already_registered))
      {
         result = cu_deregister_with_dcm(&me_ptr->cu);
      }

      spl_cntr_handle_failure_at_graph_open(me_ptr, result);
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);

   spl_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

static ar_result_t spl_cntr_relaunch_thread_and_handle_rest_of_graph_open(cu_base_t *base_ptr, void *ctx_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t         result          = AR_EOK;
   posal_thread_prio_t thread_priority = 0;
   char_t              thread_name[POSAL_DEFAULT_NAME_LEN];
   bool_t              thread_launched = FALSE;

   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   if (ctx_ptr)
   {
      uint32_t stack_size = *((uint32_t *)ctx_ptr);

      // Do set-cfg after launch thread - needed because higher stack size might be required for the set-cfg itself.
      TRY(result, spl_cntr_prepare_to_launch_thread(me_ptr, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN));

      TRY(result, cu_check_launch_thread(&me_ptr->cu, stack_size, 0, thread_priority, thread_name, &thread_launched));

      if (thread_launched)
      {
         me_ptr->cu.handle_rest_fn      = spl_cntr_handle_rest_of_graph_open;
         me_ptr->cu.handle_rest_ctx_ptr = NULL;
         return AR_EOK;
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
      spl_cntr_handle_failure_at_graph_open(me_ptr, result);
   }

   return result;
}
/* =======================================================================
Public Function Definitions
========================================================================== */

/**
 * Complete an operation (start, stop, flush, reset, etc) on a subgraph. To
 * operate on a subgraph, operate on all of its ports (internal and external).
 * If operations are successful, updates the current sg's state.
 */
ar_result_t spl_cntr_operate_on_subgraph(void                      *base_ptr,
                                         uint32_t                   sg_ops,
                                         topo_sg_state_t            sg_state,
                                         gu_sg_t                   *gu_sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t    result = AR_EOK;
   cu_base_t     *cu_ptr = (cu_base_t *)base_ptr;
   spl_cntr_t    *me_ptr = (spl_cntr_t *)base_ptr;
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
                cu_operate_on_ext_ctrl_port(base_ptr,
                                            sg_ops,
                                            &ctrl_port_ptr->ext_ctrl_port_ptr,
                                            TRUE /* is_self_sg */));
         }
         else
         {
            // flush the control buf queue in case of STOP and CLOSE
            TRY(result, cu_operate_on_int_ctrl_port(cu_ptr, sg_ops, &ctrl_port_ptr, TRUE /* is_self_sg */));

            // invalidate the connection in case of CLOSE
            TRY(result,
                gen_topo_operate_on_int_ctrl_port(cu_ptr->topo_ptr, ctrl_port_ptr, spf_sg_list_ptr, sg_ops, FALSE));
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

         if (out_port_list_ptr->op_port_ptr->ext_out_port_ptr)
         {
            // Handles the case where both ends of this connection belong to the same SG.
            TRY(result,
                spl_cntr_operate_on_ext_out_port(base_ptr,
                                                 sg_ops,
                                                 &(out_port_list_ptr->op_port_ptr->ext_out_port_ptr),
                                                 TRUE));

            // if (ext_out_port_ptr->base.downstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
         else if (operate_on_all_internal_ports || // all internal ports for FLUSH
                  (operate_on_sg_boundary_ports &&
                   gen_topo_is_output_port_at_sg_boundary(out_port_ptr))) // all boundary ports for STOP/SUSPEND/CLOSE
         {
            bool_t SET_PORT_OP_FALSE = FALSE;
            TRY(result,
                spl_topo_operate_on_int_out_port(cu_ptr->topo_ptr,
                                                 out_port_list_ptr->op_port_ptr,
                                                 spf_sg_list_ptr,
                                                 sg_ops,
                                                 SET_PORT_OP_FALSE));
         }
      }

      if (sg_ops & TOPO_SG_OP_FLUSH)
      {
         // if internal metadata list has nodes, then destroy them before destroying any nodes in ports.
         //  this is to maintain order of metadata: output, internal list, input
         gen_topo_reset_module(&me_ptr->topo.t_base, module_ptr);
      }

      for (gu_input_port_list_t *in_port_list_ptr = module_ptr->gu.input_port_list_ptr; (NULL != in_port_list_ptr);
           LIST_ADVANCE(in_port_list_ptr))
      {
         gen_topo_input_port_t *in_port_ptr = (gen_topo_input_port_t *)in_port_list_ptr->ip_port_ptr;

         if (in_port_list_ptr->ip_port_ptr->ext_in_port_ptr)
         {
            // Handles the case where both ends of the connection from this ext in port belong to the same SG.
            TRY(result,
                spl_cntr_operate_on_ext_in_port(base_ptr,
                                                sg_ops,
                                                &(in_port_list_ptr->ip_port_ptr->ext_in_port_ptr),
                                                TRUE));

            // if (ext_in_port_ptr->base.upstream_handle_ptr) : any ext connection is issued appropriate cmd by APM.
            // no container to container messaging necessary.
         }
         else if (operate_on_all_internal_ports || // all internal ports for FLUSH
                  (operate_on_sg_boundary_ports &&
                   gen_topo_is_input_port_at_sg_boundary(in_port_ptr))) // all boundary ports for STOP/SUSPEND/CLOSE
         {
            bool_t SET_PORT_OP_FALSE = FALSE;
            TRY(result,
                spl_topo_operate_on_int_in_port(cu_ptr->topo_ptr,
                                                in_port_list_ptr->ip_port_ptr,
                                                spf_sg_list_ptr,
                                                sg_ops,
                                                SET_PORT_OP_FALSE));
         }
      }
   }

   // Operation was successful (did not go to CATCH). Apply new state.
   if (TOPO_SG_STATE_INVALID != sg_state)
   {
      sg_ptr->state = sg_state;
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      sg_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

ar_result_t spl_cntr_operate_on_subgraph_async(void                      *base_ptr,
                                               uint32_t                   sg_ops,
                                               topo_sg_state_t            sg_state,
                                               gu_sg_t                   *gu_sg_ptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{

   INIT_EXCEPTION_HANDLING
   ar_result_t    result = AR_EOK;
   spl_cntr_t    *me_ptr = (spl_cntr_t *)base_ptr;
   gen_topo_sg_t *sg_ptr = (gen_topo_sg_t *)gu_sg_ptr;

   SPF_MANAGE_CRITICAL_SECTION

   // for STOP and SUSPEND, operation on modules can be done now.
   if (sg_ops & (TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND))
   {
      TRY(result,
          spl_topo_operate_on_modules((void *)&me_ptr->topo, sg_ops, sg_ptr->gu.module_list_ptr, spf_sg_list_ptr));
   }

   if (TOPO_SG_OP_STOP & sg_ops)
   {
      for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.t_base.gu.ext_in_port_list_ptr;
           (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
      {
         spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
         if (ext_in_port_ptr->gu.sg_ptr == gu_sg_ptr)
         {
            SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);
            spl_topo_reset_input_port(&me_ptr->topo, (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);

            spl_cntr_flush_input_data_queue(me_ptr,
                                            ext_in_port_ptr,
                                            TRUE /*keep_data_msg*/,
                                            FALSE /*buffer_data_locally*/);

            spl_cntr_ext_in_port_handle_data_flow_end(me_ptr,
                                                      ext_in_port_ptr,
                                                      FALSE /*create_eos_md*/,
                                                      TRUE /*is_flushing*/);
            SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);
         }
      }

      for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
           (NULL != ext_out_port_list_ptr);
           LIST_ADVANCE(ext_out_port_list_ptr))
      {
         spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
         if (ext_out_port_ptr->gu.sg_ptr == gu_sg_ptr)
         {
            SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);
            spl_topo_reset_output_port(&me_ptr->topo, (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr);
            spl_cntr_return_held_out_buf(me_ptr, ext_out_port_ptr);
            ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = FALSE;
            ext_out_port_ptr->sent_media_fmt                = FALSE;
            SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);
         }
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Operates on a subgraph after state/RT propagation occurs.
 */
ar_result_t spl_cntr_post_operate_on_subgraph(void                      *base_ptr,
                                              uint32_t                   sg_ops,
                                              topo_sg_state_t            sg_state,
                                              gu_sg_t                   *gu_sg_ptr,
                                              spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   ar_result_t    result = AR_EOK;
   spl_cntr_t    *me_ptr = (spl_cntr_t *)base_ptr;
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
            spl_cntr_post_operate_on_connected_input(me_ptr, out_port_ptr, spf_sg_list_ptr, sg_ops);
         }
      }
   }

   return result;
}

/**
 * Operates on downstream connected input ports. Used to set internal EOS on subgraph boundaries internal to the
 * container when the upstream subgraph is stopped.
 */
ar_result_t spl_cntr_post_operate_on_connected_input(spl_cntr_t                *me_ptr,
                                                     gen_topo_output_port_t    *out_port_ptr,
                                                     spf_cntr_sub_graph_list_t *spf_sg_list_ptr,
                                                     uint32_t                   sg_ops)
{
   ar_result_t            result           = AR_EOK;
   gen_topo_module_t     *module_ptr       = (gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr;
   gen_topo_input_port_t *conn_in_port_ptr = (gen_topo_input_port_t *)out_port_ptr->gu.conn_in_port_ptr;
   if (conn_in_port_ptr)
   {
      gen_topo_sg_t *other_sg_ptr = (gen_topo_sg_t *)conn_in_port_ptr->gu.cmn.module_ptr->sg_ptr;
      if (!gu_is_sg_id_found_in_spf_array(spf_sg_list_ptr, other_sg_ptr->gu.id))
      {
         // Apply data flow gap at internal input ports on the downstream boundary of the operating subgraph.
         if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
             (TOPO_PORT_STATE_STARTED == conn_in_port_ptr->common.state) &&
             (TOPO_DATA_FLOW_STATE_AT_GAP != conn_in_port_ptr->common.data_flow_state))
         {
            spl_topo_input_port_t *spl_topo_conn_in_port_ptr = (spl_topo_input_port_t *)conn_in_port_ptr;

            uint32_t is_upstream_realtime = FALSE;
            gen_topo_get_port_property(&me_ptr->topo,
                                       TOPO_DATA_OUTPUT_PORT_TYPE,
                                       PORT_PROPERTY_IS_UPSTREAM_RT,
                                       out_port_ptr,
                                       &is_upstream_realtime);

            /* If upstream is real-time we can avoid sending eos downstream for FLUSH command since this implies
            upstream is in STARTED state and will be pumping data in real time. If we flush while upstream is in
            STARTED state, it might lead to an infinite loop since data is dropped while new data is coming in.
            In current cases, when we seek, upstream sg is flushed and spr will be pumping 0's downstream which flushes
            out the device leg while ensuring there are no endpoint underruns. Upstream rt check will not work in cases
            where timestamp is valid since it would lead to ts discontinuities due to dropping of data at ext inp of
            device leg.

            We still need eos to be sent when upstream is STOPPED, to flush out any data stuck in the device pipeline.
             */
            if ((TOPO_SG_OP_STOP & sg_ops) || ((TOPO_SG_OP_FLUSH & sg_ops) && (is_upstream_realtime == FALSE)))
            {
               uint32_t                  INPUT_PORT_ID_NONE = 0; // Internal input port's don't have unique id's, use 0.
               bool_t                    IS_INPUT_TRUE      = TRUE;
               bool_t                    NEW_MARKER_EOS_TRUE = TRUE;
               module_cmn_md_eos_flags_t eos_md_flag         = { .word = 0 };
               eos_md_flag.is_flushing_eos                   = TRUE;
               eos_md_flag.is_internal_eos                   = TRUE;

               result =
                  gen_topo_create_eos_for_cntr(&me_ptr->topo.t_base,
                                               &(spl_topo_conn_in_port_ptr->t_base),
                                               INPUT_PORT_ID_NONE,
                                               me_ptr->cu.heap_id,
                                               &(spl_topo_conn_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                               NULL,         /* md_flag_ptr */
                                               NULL,         /*tracking_payload_ptr*/
                                               &eos_md_flag, /* eos_payload_flags */
                                               spl_topo_get_in_port_actual_data_len(&(me_ptr->topo),
                                                                                    spl_topo_conn_in_port_ptr),
                                               spl_topo_conn_in_port_ptr->t_base.common.media_fmt_ptr);

               // Assign marker eos.
               spl_topo_assign_marker_eos_on_port(&(me_ptr->topo),
                                                  (void *)conn_in_port_ptr,
                                                  IS_INPUT_TRUE,
                                                  NEW_MARKER_EOS_TRUE);

               SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                            DBG_MED_PRIO,
                            "MD_DBG: EOS set at SG boundary within container (0x%lX, 0x%lx) to (0x%lX, 0x%lx), result "
                            "%ld",
                            module_ptr->gu.module_instance_id,
                            out_port_ptr->gu.cmn.id,
                            conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                            conn_in_port_ptr->gu.cmn.id,
                            result);
            }
            // Create DFG in suspend (avoids flush).
            else if (TOPO_SG_OP_SUSPEND & sg_ops)
            {

               module_cmn_md_t *out_md_ptr = NULL;
               uint32_t         buf_actual_data_len =
                  spl_topo_get_in_port_actual_data_len(&(me_ptr->topo), spl_topo_conn_in_port_ptr);

               result =
                  gen_topo_create_dfg_metadata(me_ptr->topo.t_base.gu.log_id,
                                               &(spl_topo_conn_in_port_ptr->t_base.common.sdata.metadata_list_ptr),
                                               me_ptr->cu.heap_id,
                                               &out_md_ptr,
                                               buf_actual_data_len,
                                               spl_topo_conn_in_port_ptr->t_base.common.media_fmt_ptr);
               if (out_md_ptr)
               {
                  SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                               DBG_HIGH_PRIO,
                               "MD_DBG: Inserted DFG at in port (0x%lX, 0x%lx) at offset %lu, result %ld",
                               conn_in_port_ptr->gu.cmn.module_ptr->module_instance_id,
                               conn_in_port_ptr->gu.cmn.id,
                               out_md_ptr->offset,
                               result);

                  spl_topo_conn_in_port_ptr->t_base.common.sdata.flags.end_of_frame = TRUE;

                  me_ptr->topo.simpt_event_flags.check_eof = TRUE;

#ifdef SPL_SIPT_DBG
                  TOPO_MSG(me_ptr->topo.t_base.gu.log_id,
                           DBG_MED_PRIO,
                           "Input port idx = %ld, miid = 0x%lx eof set, check_eof becomes TRUE.",
                           spl_topo_conn_in_port_ptr->t_base.gu.cmn.index,
                           spl_topo_conn_in_port_ptr->t_base.gu.cmn.module_ptr->module_instance_id);
#endif
               }
            }
         }
      }
   }

   return result;
}

/**
 * Complete an operation (stop, flush, reset, etc) on an external input port.
 * For external ports, spl_cntr_operate_on_ext_in_port & spl_cntr_operate_on_ext_out_port is called
 * in 2 contexts:
 * 1. in the context of subgraph command: both ends of the connection belongs to the same SG.
 * 2. in the context of handle list of subgraph: this is an inter-SG connection.
 *
 * CLOSE, FLUSH, STOP are handled in spl_cntr_operate_on_ext_in_port context.
 *
 * START is handled based on downgraded state in spl_cntr_apply_downgraded_state_on_input_port.
 *
 * the distinction is made in the caller.
 * This is a common utility.
 *
 */
ar_result_t spl_cntr_operate_on_ext_in_port(void              *base_ptr,
                                            uint32_t           sg_ops,
                                            gu_ext_in_port_t **ext_in_port_pptr,
                                            bool_t             is_self_sg)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result          = AR_EOK;
   spl_cntr_t             *me_ptr          = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)*ext_in_port_pptr;

   // stop listening to the external input port for STOP/SUSPEND/CLOSE ops
   // This can be done even for peer stop due to DFG/EOS causing force process.
   if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
   }

   // Nothing else to do for START/PREPARE
   // for self SG STOP, flush/reset will be done in operate_on_subgraph_async
   // Peer stop, flush/reset will be done in post_operate_on_ext_in_port.
   if ((TOPO_SG_OP_START | TOPO_SG_OP_PREPARE | TOPO_SG_OP_STOP) & sg_ops)
   {
      return AR_EOK;
   }

   bool_t is_disconnect_needed = ((TOPO_SG_OP_DISCONNECT & sg_ops) &&
                                  (cu_is_disconnect_ext_in_port_needed((cu_base_t *)base_ptr, &ext_in_port_ptr->gu)));

   // Drop all queued data for close/flush/disconnect.
   if (is_disconnect_needed || ((TOPO_SG_OP_FLUSH | TOPO_SG_OP_CLOSE) & sg_ops))
   {
      // Drop data in external input ports. Set port to at_gap.
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "ext in port idx = %ld, miid = 0x%lx flushing input data queue",
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                   ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id);
      bool_t keep_data_msg = (TOPO_SG_OP_FLUSH & sg_ops) ? TRUE : FALSE;
      spl_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, keep_data_msg, FALSE /*buffer_data_locally*/);
   }

   if (is_disconnect_needed)
   {
      ext_in_port_ptr->gu.upstream_handle.spf_handle_ptr = NULL;
   }

   // For self-sg external input ports.
   if (is_self_sg)
   {
      if (TOPO_SG_OP_FLUSH & sg_ops)
      {
         // Self sg sends STOP port op to each module, so EOS is not needed.
         bool_t CREATE_EOS_MD_FALSE = FALSE;
         bool_t IS_FLUSHING_TRUE    = TRUE;

         // Set data flow gap at the external input port.
         TRY(result,
             spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, CREATE_EOS_MD_FALSE, IS_FLUSHING_TRUE));

         spl_topo_reset_input_port(&me_ptr->topo, (spl_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);
      }
      else if (TOPO_SG_OP_SUSPEND & sg_ops)
      {
         bool_t CREATE_EOS_MD_FALSE = FALSE;
         bool_t IS_FLUSHING_FALSE   = FALSE;

         // Set data flow gap at the external input port.
         TRY(result,
             spl_cntr_ext_in_port_handle_data_flow_end(me_ptr,
                                                       ext_in_port_ptr,
                                                       CREATE_EOS_MD_FALSE,
                                                       IS_FLUSHING_FALSE));
      }
   }
   // For peer-sg external input ports, change the connected state of a port
   else
   {
      // inform module (for self-sg case, operate_on_sg will take care or at inter-sg,intra-container level)
      if (TOPO_SG_OP_CLOSE & sg_ops)
      {
         gen_topo_module_t *module_ptr = ((gen_topo_module_t *)ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr);
         if (module_ptr->capi_ptr)
         {
            gen_topo_input_port_t *in_port_ptr = ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);
            result                             = gen_topo_capi_set_data_port_op_from_sg_ops(module_ptr,
                                                                sg_ops,
                                                                &in_port_ptr->common.last_issued_opcode,
                                                                TRUE,
                                                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.index,
                                                                ext_in_port_ptr->gu.int_in_port_ptr->cmn.id);
         }

         // Insert EOS to the next module if all upstream peer subgraphs are destroyed.
         if (me_ptr->topo.t_base.topo_to_cntr_vtable_ptr->check_insert_missing_eos_on_next_module)
         {
            me_ptr->topo.t_base.topo_to_cntr_vtable_ptr
               ->check_insert_missing_eos_on_next_module(&(me_ptr->topo.t_base),
                                                         (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr);
         }
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   if (TOPO_SG_OP_CLOSE == sg_ops)
   {
      ext_in_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   return result;
}

/**
 * Operate on external input ports after state/RT propagation is completed.
 */
ar_result_t spl_cntr_post_operate_on_ext_in_port(void                      *base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_in_port_t         **ext_in_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result          = AR_EOK;
   spl_cntr_t             *me_ptr          = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)*ext_in_port_pptr;
   gen_topo_input_port_t  *in_port_ptr     = (gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr;

   /* If ext input port receives both a self and peer stop/flush from upstream (any order)
    * eos need not be set since next DS container will anyway get the eos because of this self stop
    *
    * EOS needs to be inserted only if this port is started and it not already at gap
    */
   if (((TOPO_SG_OP_FLUSH | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops) &&
       (TOPO_PORT_STATE_STARTED == in_port_ptr->common.state) &&
       (TOPO_DATA_FLOW_STATE_AT_GAP != ext_in_port_ptr->topo_buf.data_flow_state))
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
         bool_t IS_FLUSHING_TRUE = TRUE;
         bool_t create_eos_md    = TRUE;

         /* if ec module is present in the topology
          * buffer partial data and force process with EOS. EC Sync module will pad zeroes for reference path if
          * EOS metadata is found and will drop the data for primary path */
         bool_t buffer_data_locally = is_module_with_ecns_extn_found(me_ptr);

         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                      DBG_MED_PRIO,
                      "external input (MIID,port_id)=(0x%lx,0x%lx) peer sg stop/flush received, flushing input data "
                      "queue. Buffering data locally for EC %lu",
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->module_instance_id,
                      ext_in_port_ptr->gu.int_in_port_ptr->cmn.id,
                      buffer_data_locally);

         TRY(result,
             spl_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /*keep_data_msg*/, buffer_data_locally));

         // Set data flow gap at the external input port.
         TRY(result,
             spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, create_eos_md, IS_FLUSHING_TRUE));
      }
      else if (TOPO_SG_OP_SUSPEND & sg_ops)
      {
         bool_t IS_FLUSHING_FALSE = FALSE;
         bool_t create_dfg_md     = TRUE;

         // Set data flow gap at the external input port.
         TRY(result,
             spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, create_dfg_md, IS_FLUSHING_FALSE));
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Complete an operation (stop, flush, reset, etc) on an external output port.
 *
 * CLOSE, FLUSH, RESET, STOP are handled in spl_cntr_operate_on_ext_in_port context.
 */
ar_result_t spl_cntr_operate_on_ext_out_port(void               *base_ptr,
                                             uint32_t            sg_ops,
                                             gu_ext_out_port_t **ext_out_port_pptr,
                                             bool_t              is_self_sg)
{
   ar_result_t              result           = AR_EOK;
   spl_cntr_t              *me_ptr           = (spl_cntr_t *)base_ptr;
   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)*ext_out_port_pptr;

   if ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
   }

   // Nothing else to do for START/PREPARE/SUSPEND
   // for self SG STOP, reset will be done in gen_cntr_operate_on_subgraph_async
   // for peer STOP, reset will be done in apply_downgraded_state.
   if ((TOPO_SG_OP_START | TOPO_SG_OP_PREPARE | TOPO_SG_OP_STOP | TOPO_SG_OP_SUSPEND) & sg_ops)
   {
      return AR_EOK;
   }

   bool_t is_disconnect_needed = (((TOPO_SG_OP_DISCONNECT)&sg_ops) &&
                                  (cu_is_disconnect_ext_out_port_needed((cu_base_t *)base_ptr, &ext_out_port_ptr->gu)));

   if (is_self_sg && (TOPO_SG_OP_FLUSH & sg_ops))
   {
      spl_topo_reset_output_port(&me_ptr->topo, (spl_topo_output_port_t *)ext_out_port_ptr->gu.int_out_port_ptr);
   }

   if (is_disconnect_needed || ((TOPO_SG_OP_CLOSE | TOPO_SG_OP_FLUSH) & sg_ops))
   {
      spl_cntr_return_held_out_buf(me_ptr, ext_out_port_ptr);
      ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = FALSE;
   }

   if (is_disconnect_needed)
   {
      ext_out_port_ptr->gu.downstream_handle.spf_handle_ptr = NULL;
   }

   if (TOPO_SG_OP_CLOSE & sg_ops)
   {
      // inform module (for self-sg case, operate_on_sg will take care at module level or at
      // inter-sg,intra-container level)
      if (!is_self_sg)
      {
         // if there is an attached module at the external output port then detach it.
         // detachment is done now to ensure state is propagated through all ports.
         if (ext_out_port_ptr->gu.int_out_port_ptr->attached_module_ptr)
         {
            gu_detach_module(&me_ptr->topo.t_base.gu,
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
                                                                FALSE,
                                                                out_port_ptr->gu.cmn.index,
                                                                out_port_ptr->gu.cmn.id);
         }
      }

      ext_out_port_ptr->gu.gu_status = GU_STATUS_CLOSING;
   }

   return result;
}

/**
 * Handling of the control path GPR command.
 */
ar_result_t spl_cntr_gpr_cmd(cu_base_t *base_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t   result     = AR_EOK;
   spl_cntr_t   *me_ptr     = (spl_cntr_t *)base_ptr;
   gpr_packet_t *packet_ptr = (gpr_packet_t *)me_ptr->cu.cmd_msg.payload_ptr;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GPR cmd: Executing GPR command, opCode(%lX) token(%lx),  handle_rest %u",
                packet_ptr->opcode,
                packet_ptr->token,
                cu_is_any_handle_rest_pending(base_ptr));

   switch (packet_ptr->opcode)
   {
      case AR_SPF_MSG_GLOBAL_SH_MEM:
      {
         cu_handle_global_shmem_msg(base_ptr, packet_ptr);
         break;
      }
      case APM_CMD_SET_CFG:
      // fall through
      case APM_CMD_GET_CFG:
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "SET/GET cfg received from GPR");

         spl_cntr_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_TYPE_DEFAULT);
         break;
      }

      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "REG/DEREG cfg received from GPR");
         spl_cntr_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_PERSISTENT);
         break;
      }

      case APM_CMD_REGISTER_SHARED_CFG:
      case APM_CMD_DEREGISTER_SHARED_CFG:
      {
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Shared REG/DEREG cfg received from GPR");
         spl_cntr_set_get_cfgs_packed(me_ptr, packet_ptr, SPF_CFG_DATA_SHARED_PERSISTENT);
         break;
      }
      default:
      {
         TRY(result, cu_gpr_cmd(base_ptr));
         me_ptr->topo.t_base.flags.need_to_handle_frame_done = cu_is_frame_done_event_registered(base_ptr);
         break;
      }
   }

   spl_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   if (!cu_is_any_handle_rest_pending(base_ptr) || AR_DID_FAIL(result))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GPR cmd: Done executing GPR command, result=0x%lx, handle_rest %u",
                   result,
                   cu_is_any_handle_rest_pending(base_ptr));

      cu_reset_handle_rest(base_ptr);
   }

   return result;
}

/**
 * Handling of the control path graph open command.
 */
ar_result_t spl_cntr_graph_open(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spl_cntr_t               *me_ptr       = (spl_cntr_t *)base_ptr;
   spf_msg_cmd_graph_open_t *open_cmd_ptr = NULL;
   uint32_t                  stack_size   = 0;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GRAPH_OPEN: Executing graph open command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_open_t));

   open_cmd_ptr = (spf_msg_cmd_graph_open_t *)&header_ptr->payload_start;

   result = gu_validate_graph_open_cmd(&me_ptr->topo.t_base.gu, open_cmd_ptr);

   if (AR_DID_FAIL(result))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:GRAPH_OPEN: Link Connection validation for graph open failed. current channel mask=0x%x. "
                   "result=0x%lx.",
                   me_ptr->cu.curr_chan_mask,
                   result);
      spf_msg_ack_msg(&me_ptr->cu.cmd_msg, result);
      return result;
   }

   // container properties can't be changed run time.
   if (0 == me_ptr->topo.t_base.gu.num_subgraphs)
   {
      TRY(result, spl_cntr_parse_container_cfg(me_ptr, open_cmd_ptr->container_cfg_ptr));
   }

   gu_sizes_t payload_size = { .ext_ctrl_port_size = SPL_CNTR_EXT_CTRL_PORT_SIZE_W_QS,
                               .ext_in_port_size   = SPL_CNTR_EXT_IN_PORT_SIZE_W_QS,
                               .ext_out_port_size  = SPL_CNTR_EXT_OUT_PORT_SIZE_W_QS,
                               .in_port_size       = sizeof(spl_cntr_input_port_t),
                               .out_port_size      = sizeof(spl_cntr_output_port_t),
                               .ctrl_port_size     = SPL_CNTR_INT_CTRL_PORT_SIZE_W_QS,
                               .sg_size            = sizeof(gen_topo_sg_t),
                               .module_size        = sizeof(spl_cntr_module_t) };

   // first prepare for asynchronous (parallel to the data path thread) graph open
   // once graph is created gu_finish_async_create will be called to merge the new graph with the existing one.
   TRY(result, gu_prepare_async_create(&me_ptr->topo.t_base.gu, me_ptr->cu.heap_id));

   TRY(result, gu_create_graph(&me_ptr->topo.t_base.gu, open_cmd_ptr, &payload_size, me_ptr->cu.heap_id));

   // initialize media format pointer for the new ports.
   gen_topo_set_default_media_fmt_at_open(&me_ptr->topo.t_base);

   TRY(result, spl_cntr_get_thread_stack_size(me_ptr, &stack_size));

   if (cu_check_thread_relaunch_required(&me_ptr->cu, stack_size, 0 /*root_stack_size*/))
   {
      if (me_ptr->cu.gu_ptr->data_path_thread_id != posal_thread_get_curr_tid())
      {
         uint32_t *ctx_ptr = NULL;
         MALLOC_MEMSET(ctx_ptr, uint32_t, sizeof(uint32_t), me_ptr->cu.heap_id, result);

         *ctx_ptr = stack_size;

         // assign function to handle the thread relaunch in the main thread context and continue with open handling
         me_ptr->cu.handle_rest_fn      = spl_cntr_relaunch_thread_and_handle_rest_of_graph_open;
         me_ptr->cu.handle_rest_ctx_ptr = (void *)ctx_ptr;
      }
      else
      {
         result = spl_cntr_relaunch_thread_and_handle_rest_of_graph_open(base_ptr, &stack_size);
      }

      return result;
   }

   return spl_cntr_handle_rest_of_graph_open(&me_ptr->cu, NULL /*ctx_ptr*/);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
      // Can't revert open on sgs in open_cmd_ptr if open_cmd_ptr is NULL. The NULL case happens when payload size of
      // open is too small.
      if (open_cmd_ptr)
      {
         spl_cntr_handle_failure_at_graph_open(me_ptr, result);
      }
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:GRAPH_OPEN: Done executing graph open command. current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result;
}

/**
 * Handling of the control path set cfg command and get cfg command.
 */
ar_result_t spl_cntr_set_get_cfg(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t                   *me_ptr = (spl_cntr_t *)base_ptr;
   spf_msg_cmd_param_data_cfg_t *cfg_cmd_ptr;
   // is_set_cfg is used to decide whether to do set param or get param. We need to do set param
   // if it is reg, dereg, or set cfg.
   bool_t              is_set_cfg = FALSE;
   bool_t              is_ack_cmd = FALSE;
   bool_t              is_deregister;
   spf_cfg_data_type_t data_type;

   CU_MSG(me_ptr->topo.t_base.gu.log_id,
          DBG_HIGH_PRIO,
          "CMD:SET_CFG: Executing set-cfg command. current channel mask=0x%x, handle_rest %u",
          me_ptr->cu.curr_chan_mask,
          cu_is_any_handle_rest_pending(base_ptr));

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_param_data_cfg_t));

   cfg_cmd_ptr = (spf_msg_cmd_param_data_cfg_t *)&header_ptr->payload_start;

   switch (me_ptr->cu.cmd_msg.msg_opcode)
   {
      case SPF_MSG_CMD_REGISTER_CFG:
      {
         data_type     = SPF_CFG_DATA_PERSISTENT;
         is_deregister = FALSE;
         is_set_cfg    = TRUE;
         break;
      }
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         data_type     = SPF_CFG_DATA_PERSISTENT;
         is_deregister = TRUE;
         is_set_cfg    = TRUE;
         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      {
         data_type     = SPF_CFG_DATA_TYPE_DEFAULT;
         is_deregister = FALSE;
         is_set_cfg    = TRUE;
         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      {
         data_type     = SPF_CFG_DATA_TYPE_DEFAULT;
         is_deregister = FALSE;
         is_set_cfg    = FALSE;
         break;
      }
      default:
      {
         THROW(result, AR_EUNSUPPORTED);
         break;
      }
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "is_deregister = %u, is_set_cfg = %u, data_type = %u",
                is_deregister,
                is_set_cfg,
                data_type);
#endif

   TRY(result,
       spl_cntr_set_get_cfgs_fragmented(me_ptr,
                                        (apm_module_param_data_t **)cfg_cmd_ptr->param_data_pptr,
                                        cfg_cmd_ptr->num_param_id_cfg,
                                        is_set_cfg,
                                        is_deregister,
                                        data_type));

   CATCH(result, CU_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   if (!cu_is_any_handle_rest_pending(base_ptr) || AR_DID_FAIL(result))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:SET_PARAM: Done executing set-cfg command, current channel mask=0x%x. result=0x%lx, "
                   "handle_rest %u",
                   me_ptr->cu.curr_chan_mask,
                   result,
                   cu_is_any_handle_rest_pending(base_ptr));

      cu_reset_handle_rest(base_ptr);

      is_ack_cmd = TRUE;
   }

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, is_ack_cmd, result);
   return result;
}

/**
 * Handling of the control path graph prepare command.
 */
ar_result_t spl_cntr_graph_prepare(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id, DBG_HIGH_PRIO, "CMD:Prepare Graph: Executing prepare graph.");

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_PREPARE, TOPO_SG_STATE_PREPARED));

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:Prepare Graph:Done executing prepare graph. current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/**
 * Handling of the control path graph start command.
 */
ar_result_t spl_cntr_graph_start(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:START: Executing start command. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_START, TOPO_SG_STATE_STARTED));

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:START:Done executing start command. current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/**
 * Handling of the control path graph stop command.
 */
ar_result_t spl_cntr_graph_stop(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:STOP: Executing stop command. current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_STOP, TOPO_SG_STATE_STOPPED));

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);

   TRY(result, cu_handle_sg_mgmt_cmd_async(base_ptr, TOPO_SG_OP_STOP, TOPO_SG_STATE_STOPPED));

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
      SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);
   }

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:STOP:Done Executing stop command. current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   return result;
}

/**
 * Handling of the control path graph suspend. Suspend is the same as stop but without flushing data or
 * resetting algorithms.
 */
ar_result_t spl_cntr_graph_suspend(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   INIT_EXCEPTION_HANDLING
   SPF_MANAGE_CRITICAL_SECTION

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:SUSPEND: Executing stop command. current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   SPF_CRITICAL_SECTION_START(&me_ptr->topo.t_base.gu);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_SUSPEND, TOPO_SG_STATE_SUSPENDED));

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);

   TRY(result, cu_handle_sg_mgmt_cmd_async(base_ptr, TOPO_SG_OP_SUSPEND, TOPO_SG_STATE_SUSPENDED));

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
      SPF_CRITICAL_SECTION_END(&me_ptr->topo.t_base.gu);
   }

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:SUSPEND:Done Executing stop command. current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   return result;
}

/**
 * Handling of the control path graph flush command.
 */
ar_result_t spl_cntr_graph_flush(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:FLUSH:Executing flush command, current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_FLUSH, TOPO_SG_STATE_INVALID));

   spl_cntr_reset(me_ptr);

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:FLUSH:Done executing flush command, current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/**
 * Handling of the control path graph close command.
 */
ar_result_t spl_cntr_graph_close(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CLOSE:Executing close Command, current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   result = spl_cntr_handle_rest_of_graph_close(base_ptr, TRUE /*is_handle_cu_cmd_mgmt*/);

   // Catch here so we don't print an error on AR_ETERMINATED.
   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   // check if any subgraph is pending, if not, destroy this container
   if (me_ptr->topo.t_base.gu.num_subgraphs != 0)
   {
      spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "CMD:CLOSE:Done executing close command, current channel mask=0x%x. result=0x%lx.",
                   base_ptr->curr_chan_mask,
                   result);
   }
   else
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   SPF_LOG_PREFIX "As number of subgraphs is zero, destroying this container");

      return spl_cntr_destroy_container(base_ptr);
   }

   return result;
}

/**
 * Handling of the control path graph connect command.
 */
ar_result_t spl_cntr_graph_connect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:CONNECT:Executing connect command, current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   TRY(result, cu_graph_connect(base_ptr));

   // Since tap point modules may have changed the graph shape, we need to reassign NBLC's. We also need to attempt to
   // resend
   // external output port media format updates for tap point modules which are container boundary modules - the
   // external output
   // port may be new but its internal output port could be old and therefore already have valid media format.
   gen_topo_assign_non_buf_lin_chains(&me_ptr->topo.t_base);
   gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.t_base.gu.ext_out_port_list_ptr;
   while (ext_out_port_list_ptr)
   {
      spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      spl_topo_output_port_t  *out_port_ptr =
         (spl_topo_output_port_t *)ext_out_port_list_ptr->ext_out_port_ptr->int_out_port_ptr;

      if ((out_port_ptr->t_base.common.flags.is_mf_valid) &&
          tu_has_media_format_changed(&ext_out_port_ptr->cu.media_fmt, out_port_ptr->t_base.common.media_fmt_ptr))
      {
         spl_cntr_set_pending_out_media_fmt(&me_ptr->topo.t_base,
                                            &out_port_ptr->t_base.common,
                                            ext_out_port_list_ptr->ext_out_port_ptr);
      }

      LIST_ADVANCE(ext_out_port_list_ptr);
   }

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   // Even though frame size could be determined by open cmd, we need to postpone below until connect because to send
   // ICB msg we need connection first

   // In graph-open, cntr_frame_len is populated but upstream is not connected.
   // Call again here to send the ICB info to upstream.
   TRY(result, cu_handle_frame_len_change(base_ptr, &base_ptr->cntr_frame_len, base_ptr->cntr_frame_len.frame_len_us));

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:CONNECT:Done excuting connect command, current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, AR_EOK);
   return result;
}

/**
 * Handling of the control path graph disconnect command.
 */
ar_result_t spl_cntr_graph_disconnect(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cu.cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:DISCONNECT:Executing disconnect command, current channel mask=0x%x",
                base_ptr->curr_chan_mask);

   TRY(result, cu_handle_sg_mgmt_cmd(base_ptr, TOPO_SG_OP_DISCONNECT, TOPO_SG_STATE_INVALID));

   // update module flags
   spl_cntr_update_all_modules_info(me_ptr);

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:DISCONNECT:Done excuting disconnect command, current channel mask=0x%x. result=0x%lx.",
                base_ptr->curr_chan_mask,
                result);

   me_ptr->cu.flags.apm_cmd_context = TRUE;
   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, AR_EOK);

   return result;
}

/**
 * Handling of the control path graph destroy container command.
 */
ar_result_t spl_cntr_destroy_container(cu_base_t *base_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:DESTROY:destroy received. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   if (me_ptr->cu.gu_ptr->data_path_thread_id != posal_thread_get_curr_tid())
   {
      // destroy will be done in main thread context and not in helper thread context
      base_ptr->handle_rest_fn      = spl_cntr_destroy;
      base_ptr->handle_rest_ctx_ptr = NULL;
      return AR_ETERMINATED; // to terminate from the thread pool
   }
   else
   {
      return spl_cntr_destroy(base_ptr, NULL);
   }
}

/**
 * Handling of the control path media format command.
 */
ar_result_t spl_cntr_ctrl_path_media_fmt(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PATH_MEDIA_FMT: Executing media format cmd. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_ctrl_path_media_fmt_cmd(base_ptr));

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PATH_MEDIA_FMT:Done excuting media format cmd, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/**
 * Handling of the frame len information from downstream.
 */
ar_result_t spl_cntr_icb_info_from_downstream(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   uint32_t    log_id = me_ptr->topo.t_base.gu.log_id;

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:FRAME_LEN_DS: ICB: Executing ICB info from DS. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_cmd_icb_info_from_downstream(base_ptr));

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
   }

   SPL_CNTR_MSG(log_id,
                DBG_HIGH_PRIO,
                "CMD:FRAME_LEN_DS: ICB: Done executing ICB info from DS, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

/*
 * Handles downgraded port START and update state in topo layer.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t spl_cntr_apply_downgraded_state_on_output_port(cu_base_t        *cu_ptr,
                                                           gu_output_port_t *gu_out_port_ptr,
                                                           topo_port_state_t downgraded_state)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)cu_ptr;

   VERIFY(result, gu_out_port_ptr && cu_ptr);

   spl_cntr_ext_out_port_t *ext_out_port_ptr = (spl_cntr_ext_out_port_t *)gu_out_port_ptr->ext_out_port_ptr;
   gen_topo_output_port_t  *out_port_ptr     = NULL;

   // If its an external port apply the downgraded state on ext output port.
   if (ext_out_port_ptr)
   {
      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state)
      {
         spl_cntr_return_held_out_buf(me_ptr, ext_out_port_ptr);
         cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
         ext_out_port_ptr->cu.icb_info.is_prebuffer_sent = FALSE;
         ext_out_port_ptr->sent_media_fmt                = FALSE;
      }
      else if ((TOPO_PORT_STATE_SUSPENDED) == downgraded_state)
      {
         cu_stop_listen_to_mask(&me_ptr->cu, ext_out_port_ptr->cu.bit_mask);
      }
   } // end of ext port handling

   // Apply port state on the internal port.
   out_port_ptr = (gen_topo_output_port_t *)gu_out_port_ptr;

   if (TOPO_PORT_STATE_STOPPED == downgraded_state)
   {
      spl_topo_reset_output_port(&me_ptr->topo, (void *)gu_out_port_ptr);
   }
   else if (TOPO_PORT_STATE_STARTED == downgraded_state)
   {
      // Once output port is in start state, it comes out of reset state.
      out_port_ptr->common.flags.port_is_not_reset = TRUE;
   }
   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)out_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &out_port_ptr->common.last_issued_opcode,
                                                      FALSE, // is_input
                                                      out_port_ptr->gu.cmn.index,
                                                      out_port_ptr->gu.cmn.id);

   CATCH(result, SPL_CNTR_MSG_PREFIX, (me_ptr ? me_ptr->cu.gu_ptr->log_id : 0))
   {
   }

   return result;
}

/*
 * Handles downgraded port START and update state in topo layer.
 *
 * This function is called from cu_update_all_port_state.
 */
ar_result_t spl_cntr_apply_downgraded_state_on_input_port(cu_base_t        *cu_ptr,
                                                          gu_input_port_t  *gu_in_port_ptr,
                                                          topo_port_state_t downgraded_state)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t             result          = AR_EOK;
   spl_cntr_t             *me_ptr          = (spl_cntr_t *)cu_ptr;
   spl_cntr_ext_in_port_t *ext_in_port_ptr = NULL;
   gen_topo_input_port_t  *in_port_ptr     = NULL;

   VERIFY(result, (gu_in_port_ptr) && (cu_ptr));

   ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_in_port_ptr->ext_in_port_ptr;

   // If its an external port apply the downgraded state on ext port.
   if (ext_in_port_ptr)
   {
      if ((TOPO_PORT_STATE_STOPPED) == downgraded_state)
      {
         bool_t IS_FLUSHING_TRUE    = TRUE;
         bool_t CREATE_EOS_MD_FALSE = FALSE;
         spl_cntr_flush_input_data_queue(me_ptr,
                                         ext_in_port_ptr,
                                         TRUE /*keep_data_msg*/,
                                         FALSE /*buffer_data_locally*/);
         cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
         spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, CREATE_EOS_MD_FALSE, IS_FLUSHING_TRUE);
      }
      else if ((TOPO_PORT_STATE_SUSPENDED) == downgraded_state)
      {
         bool_t IS_FLUSHING_FALSE   = FALSE;
         bool_t CREATE_EOS_MD_FALSE = FALSE;
         cu_stop_listen_to_mask(&me_ptr->cu, ext_in_port_ptr->cu.bit_mask);
         spl_cntr_ext_in_port_handle_data_flow_end(me_ptr, ext_in_port_ptr, CREATE_EOS_MD_FALSE, IS_FLUSHING_FALSE);
      }
   }

   // Reset input port, if stopped
   if (TOPO_PORT_STATE_STOPPED == downgraded_state)
   {
      spl_topo_reset_input_port(&me_ptr->topo, (void *)gu_in_port_ptr);
   }

   // Apply port state on the internal port.
   in_port_ptr = (gen_topo_input_port_t *)gu_in_port_ptr;

   // set data port state on the module.
   result = gen_topo_capi_set_data_port_op_from_state((gen_topo_module_t *)in_port_ptr->gu.cmn.module_ptr,
                                                      downgraded_state,
                                                      &in_port_ptr->common.last_issued_opcode,
                                                      TRUE, // is_input
                                                      in_port_ptr->gu.cmn.index,
                                                      in_port_ptr->gu.cmn.id);

   CATCH(result, SPL_CNTR_MSG_PREFIX, (me_ptr ? me_ptr->cu.gu_ptr->log_id : 0))
   {
   }

   return result;
}

/* Handles control port trigger.*/
ar_result_t spl_cntr_handle_ctrl_port_trigger_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   INIT_EXCEPTION_HANDLING
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PORT_TRIGGER: triggerable control link msg. current channel mask=0x%x",
                me_ptr->cu.curr_chan_mask);

   TRY(result, cu_handle_ctrl_port_trigger_cmd(base_ptr));

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->cu.gu_ptr->log_id)
   {
   }

   // if any module is enabled then we need to check if we can process topo based on that trigger.
   // Example: Enabling DTMF generator module need to enable topo process.
   spl_cntr_handle_events_after_cmds(me_ptr, FALSE, result);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                "CMD:CTRL_PORT_TRIGGER: Done triggerable control link msg, current channel mask=0x%x. result=0x%lx.",
                me_ptr->cu.curr_chan_mask,
                result);

   return result; // handle trig already acks this - should NOT do ack/return here
}

/* Handles peer port property update command.*/
ar_result_t spl_cntr_handle_peer_port_property_update_cmd(cu_base_t *base_ptr)
{
   ar_result_t       result             = AR_EOK;
   spl_cntr_t       *me_ptr             = (spl_cntr_t *)base_ptr;
   cu_event_flags_t *fwk_event_flag_ptr = NULL;

   result = cu_handle_peer_port_property_update_cmd(base_ptr);

   // no need to reconcile the event flag in SC.
   CU_FWK_EVENT_HANDLER_CONTEXT
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, base_ptr, FALSE /*do_reconcile*/);

   if (fwk_event_flag_ptr->port_state_change)
   {
      spl_topo_update_all_modules_info(&me_ptr->topo);
   }

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

ar_result_t spl_cntr_handle_upstream_stop_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;

   gu_ext_in_port_t *dst_port_ptr = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;

   CU_MSG(me_ptr->topo.t_base.gu.log_id,
          DBG_HIGH_PRIO,
          "CMD:UPSTREAM_STOP_CMD: Executing upstream stop for (0x%lX, 0x%lx). current channel mask=0x%x",
          dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          dst_port_ptr->int_in_port_ptr->cmn.id,
          base_ptr->curr_chan_mask);

   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)(dst_port_ptr);
   spl_cntr_flush_input_data_queue(me_ptr, ext_in_port_ptr, TRUE /*keep_data_msg*/, FALSE /*buffer_data_locally*/);

   CU_MSG(me_ptr->topo.t_base.gu.log_id,
          DBG_LOW_PRIO,
          "CMD:UPSTREAM_STOP_CMD: (0x%lX, 0x%lx) handling upstream stop done.",
          dst_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
          dst_port_ptr->int_in_port_ptr->cmn.id);

   spl_cntr_handle_events_after_cmds(me_ptr, TRUE, result);

   return result;
}

ar_result_t spl_cntr_initiate_duty_cycle_island_entry(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   if (!me_ptr->cu.pm_info.flags.module_disallows_duty_cycling)
   {
      spl_cntr_listen_to_controls(me_ptr);

      spl_cntr_handle_events_after_cmds(me_ptr, FALSE, result);
      cu_send_island_entry_ack_to_dcm(&me_ptr->cu);
   }
   return result;
}

ar_result_t spl_cntr_initiate_duty_cycle_island_exit(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   if (!me_ptr->cu.pm_info.flags.module_disallows_duty_cycling)
   {
      spl_cntr_handle_events_after_cmds(me_ptr, FALSE, result);
   }
   return result;
}
