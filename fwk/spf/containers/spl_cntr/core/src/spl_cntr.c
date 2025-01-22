/**
 * \file spl_cntr.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_fwk_extns_sync.h"
#include "capi_fwk_extns_voice_delivery.h"
#include "spl_cntr_i.h"

#include "apm.h"
#include "apm_api.h"
#include "apm_container_api.h"
#include "container_utils.h"
#include "spf_svc_utils.h"
#include "graph_utils.h"
#include "posal_power_mgr.h"
#include "posal_queue.h"

/* =======================================================================
Declarations
========================================================================== */

// Maximum number of commands expected ever in command queue.
static const uint32_t SPL_CNTR_MAX_CMD_Q_ELEMENTS = 128;
static const uint32_t SPL_CNTR_BASE_STACK_SIZE    = 2048;
#ifdef PROD_SPECIFIC_MAX_CH
static const uint32_t SPL_CNTR_PROCESS_STACK_SIZE = 3072;// additional requirement based on profiling
#else
static const uint32_t SPL_CNTR_PROCESS_STACK_SIZE = 2048;
#endif

/* =======================================================================
CU Virtual Table Function Implementations
========================================================================== */
// clang-format off
static const cu_cntr_vtable_t spl_cntr_cntr_funcs = {
   .port_data_thresh_change               = spl_cntr_handle_int_port_data_thresh_change_event,

   .aggregate_kpps_bw                     = spl_cntr_aggregate_kpps_bw,

   .operate_on_subgraph                   = spl_cntr_operate_on_subgraph,
   .operate_on_subgraph_async             = spl_cntr_operate_on_subgraph_async,

   .post_operate_on_subgraph              = spl_cntr_post_operate_on_subgraph,
   .set_get_cfg                           = spl_cntr_set_get_cfg_util,
   .register_events                       = spl_cntr_register_events_utils,

   .init_ext_in_port                      = spl_cntr_init_ext_in_port,
   .deinit_ext_in_port                    = spl_cntr_deinit_ext_in_port,
   .operate_on_ext_in_port                = spl_cntr_operate_on_ext_in_port,
   .post_operate_on_ext_in_port           = spl_cntr_post_operate_on_ext_in_port,
   .post_operate_on_ext_out_port          = NULL,
   .input_media_format_received           = spl_cntr_input_media_format_received,

   .init_ext_out_port                     = spl_cntr_init_ext_out_port,
   .deinit_ext_out_port                   = spl_cntr_deinit_ext_out_port,
   .operate_on_ext_out_port               = spl_cntr_operate_on_ext_out_port,
   .ext_out_port_apply_pending_media_fmt  = spl_cntr_ext_out_port_apply_pending_media_fmt_cmd_path,
   .ext_out_port_recreate_bufs            = spl_cntr_recreate_ext_out_buffers,

   .init_ext_ctrl_port                    = cu_init_ext_ctrl_port,
   .deinit_ext_ctrl_port                  = cu_deinit_ext_ctrl_port,
   .operate_on_ext_ctrl_port              = cu_operate_on_ext_ctrl_port,
   .connect_ext_ctrl_port                 = cu_connect_ext_ctrl_port,

   .apply_downgraded_state_on_output_port = spl_cntr_apply_downgraded_state_on_output_port,
   .apply_downgraded_state_on_input_port  = spl_cntr_apply_downgraded_state_on_input_port,
   .destroy_all_metadata                  = gen_topo_destroy_all_metadata,

   .handle_proc_duration_change           = spl_cntr_handle_proc_duration_change,

   .update_path_delay                     = cu_update_path_delay,

   .aggregate_hw_acc_proc_delay           = spl_cntr_aggregate_hw_acc_proc_delay,
   .vote_against_island                   = NULL,
   .exit_island_temporarily               = NULL,

   .get_additional_ext_in_port_delay_cu_cb   = spl_cntr_get_additional_ext_in_port_delay_cu_cb,
   .get_additional_ext_out_port_delay_cu_cb  = spl_cntr_get_additional_ext_out_port_delay_cu_cb,

   .dcm_topo_set_param 						 = spl_cntr_dcm_topo_set_param,
   .initiate_duty_cycle_island_entry         = spl_cntr_initiate_duty_cycle_island_entry,
   .initiate_duty_cycle_island_exit          = spl_cntr_initiate_duty_cycle_island_exit,

   .handle_cntr_period_change                = spl_cntr_handle_cntr_period_change

};
// clang-format on

/* =======================================================================
Static Function Declarations
========================================================================== */
// clang-format off
static ar_result_t spl_cntr_create_channel_and_queues(spl_cntr_t *me_ptr, POSAL_HEAP_ID peer_cntr_heap_id);
// clang-format on

/* =======================================================================
Function Tables
========================================================================== */

// most frequent commands be on top
const cu_msg_handler_t spl_cntr_cmd_handler_table[] = {
   { SPF_MSG_CMD_GPR, spl_cntr_gpr_cmd },
   { SPF_MSG_CMD_SET_CFG, spl_cntr_set_get_cfg },
   { SPF_MSG_CMD_GET_CFG, spl_cntr_set_get_cfg },
   { SPF_MSG_CMD_INFORM_ICB_INFO, spl_cntr_icb_info_from_downstream },
   { SPF_MSG_CMD_CTRL_PORT_TRIGGER_MSG, spl_cntr_handle_ctrl_port_trigger_cmd },
   { SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE, spl_cntr_handle_peer_port_property_update_cmd },
   { SPF_MSG_CMD_UPSTREAM_STOPPED_ACK, spl_cntr_handle_upstream_stop_cmd },
   { SPF_MSG_CMD_GRAPH_OPEN, spl_cntr_graph_open },
   { SPF_MSG_CMD_GRAPH_PREPARE, spl_cntr_graph_prepare },
   { SPF_MSG_CMD_GRAPH_START, spl_cntr_graph_start },
   { SPF_MSG_CMD_GRAPH_SUSPEND, spl_cntr_graph_suspend },
   { SPF_MSG_CMD_GRAPH_STOP, spl_cntr_graph_stop },
   { SPF_MSG_CMD_GRAPH_FLUSH, spl_cntr_graph_flush },
   { SPF_MSG_CMD_GRAPH_CLOSE, spl_cntr_graph_close },
   { SPF_MSG_CMD_GRAPH_CONNECT, spl_cntr_graph_connect },
   { SPF_MSG_CMD_GRAPH_DISCONNECT, spl_cntr_graph_disconnect },
   { SPF_MSG_CMD_DESTROY_CONTAINER, spl_cntr_destroy_container },
   { SPF_MSG_CMD_MEDIA_FORMAT, spl_cntr_ctrl_path_media_fmt },
   { SPF_MSG_CMD_REGISTER_CFG, spl_cntr_set_get_cfg },
   { SPF_MSG_CMD_DEREGISTER_CFG, spl_cntr_set_get_cfg },
};

// clang-format off
static const topo_to_cntr_vtable_t topo_to_spl_cntr_vtable = {
   .clear_eos                                   = spl_cntr_handle_clear_eos_topo_cb,
   .raise_data_to_dsp_service_event             = spl_cntr_handle_event_data_to_dsp_service,
   .raise_data_from_dsp_service_event           = spl_cntr_handle_event_get_data_from_dsp_service,
   .handle_capi_event                           = spl_cntr_handle_capi_event,
   .set_pending_out_media_fmt                   = spl_cntr_set_pending_out_media_fmt,
   .check_apply_ext_out_ports_pending_media_fmt = spl_cntr_check_apply_ext_out_ports_pending_media_fmt,
   .set_ext_in_port_prev_actual_data_len        = spl_cntr_set_ext_in_port_prev_actual_data_len,
   .update_input_port_max_samples               = spl_cntr_update_input_port_max_samples,
   .ext_in_port_get_buf_len                     = spl_cntr_ext_in_port_get_buf_len,
   .clear_topo_req_samples                       = spl_cntr_clear_topo_req_samples,
   .update_icb_info                             = spl_cntr_update_icb_info,
   .set_propagated_prop_on_ext_output           = spl_cntr_set_propagated_prop_on_ext_output,
   .set_propagated_prop_on_ext_input            = spl_cntr_set_propagated_prop_on_ext_input,
   .ext_out_port_has_buffer                     = spl_cntr_ext_out_port_has_buffer,
   .ext_in_port_has_data_buffer                 = NULL,
   .algo_delay_change_event                     = spl_cntr_handle_algo_delay_change_event,
   .ext_in_port_has_enough_data                 = spl_cntr_ext_in_port_has_enough_data,
   .ext_out_port_get_ext_buf                    = spl_cntr_ext_out_port_get_ext_buf,
   .ext_in_port_dfg_eos_left_port               = spl_cntr_ext_in_port_dfg_eos_left_port,
   .ext_in_port_clear_timestamp_discontinuity   = spl_cntr_ext_in_port_clear_timestamp_discontinuity,

   .create_module                               = spl_cntr_create_module,
   .destroy_module                              = spl_cntr_destroy_module,
   .check_insert_missing_eos_on_next_module     = spl_cntr_check_insert_missing_eos_on_next_module,

   .aggregate_ext_in_port_delay                 = spl_cntr_aggregate_ext_in_port_delay_topo_cb,
   .aggregate_ext_out_port_delay                = spl_cntr_aggregate_ext_out_port_delay_topo_cb,

   .notify_ts_disc_evt                             = NULL,
   .module_buffer_access_event                     = NULL,
   .check_if_any_ext_in_has_to_preserve_prebuffer  = NULL,
};
// clang-format on

/* =======================================================================
Static Function Definitions
========================================================================== */

/**
 * input: Stack size decided by CAPIs.
 * output: Stack size decided based on comparing client given size, capi given
 *         size, etc.
 */
ar_result_t spl_cntr_get_thread_stack_size(spl_cntr_t *me_ptr, uint32_t *stack_size)
{
   *stack_size = 0;

   gen_topo_get_aggregated_capi_stack_size(&me_ptr->topo.t_base, stack_size);

   *stack_size = MAX(me_ptr->cu.configured_stack_size, *stack_size);
   *stack_size = MAX(SPL_CNTR_BASE_STACK_SIZE, *stack_size);
   *stack_size += SPL_CNTR_PROCESS_STACK_SIZE;

   // Check this after adding the SPL_CNTR_PROCESS_STACK_SIZE to the stack_size
   // to prevent multiple addition during relaunch
   *stack_size = MAX(me_ptr->cu.actual_stack_size, *stack_size);

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "Stack sizes: Configured %lu, actual %lu, final %lu",
                me_ptr->cu.configured_stack_size,
                me_ptr->cu.actual_stack_size,
                *stack_size);

   return AR_EOK;
}

/**
 * Get the thread priority that the spl_cntr should be running at. Thread priority is primarily
 * based on the operating frame size since this determines the deadline period while processing
 * data. Note that it is discouraged to put parallel graphs within one container since it can only
 * process a single graph at once. Also even if parallel graphs exist, it gets even messier if they
 * run at different frame sizes since that implies the parallel graphs would have different thread
 * priorities.
 */
static ar_result_t spl_cntr_get_thread_priority(spl_cntr_t *me_ptr, posal_thread_prio_t *priority_ptr)
{
   /**
    * If container prio is configured, then it is used independent of whether container is started, or
    * running commands during data processing or if it's FTRT or if its frame size is not known or
    * if a module changes container priority.
    */
   posal_thread_prio_t temp;
   // If no subgraphs are running, we should use a low thread priority.
   if (!me_ptr->cu.flags.is_cntr_started)
   {
      *priority_ptr = posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID);

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_HIGH_PRIO,
                   "Since no sgs are started, returning floor thread priority.");
#endif
   }
   else
   {
      // Normal vote logic goes here.
      prio_query_t query_tbl;
      // vdec, vprx takes < 1ms to process & vdec is stm, but we don't want it to get higher prio compared to HW-EP as
      // margins are higher.
      query_tbl.frame_duration_us =
         me_ptr->cu.voice_info_ptr ? me_ptr->cu.voice_info_ptr->safety_margin_us : me_ptr->cu.cntr_proc_duration;
      query_tbl.static_req_id     = SPF_THREAD_DYN_ID;
      query_tbl.is_interrupt_trig = FALSE;

      posal_thread_calc_prio(&query_tbl, priority_ptr);
      //*priority_ptr = MAX(*priority_ptr, me_ptr->cu.configured_thread_prio); configured prio may be lower.
   }

   temp = *priority_ptr;

   if (APM_CONT_PRIO_IGNORE != me_ptr->cu.configured_thread_prio)
   {
      *priority_ptr = me_ptr->cu.configured_thread_prio;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                SPF_LOG_PREFIX
                "SPL_CNTR thread priority %d (larger is higher prio), period_us %lu us, frame len %lu us, proc "
                "duration %lu us, is real time %u, has_stm %u, cntr started %u",
                *priority_ptr,
                me_ptr->cu.period_us,
                me_ptr->cu.cntr_frame_len.frame_len_us,
                me_ptr->cu.cntr_proc_duration,
                TRUE,
                FALSE,
                me_ptr->cu.flags.is_cntr_started);

   if (temp != *priority_ptr)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Warning: thread priority: configured %d prio overrides internal logic %d", me_ptr->cu.configured_thread_prio, temp);
   }

   return AR_EOK;
}

void spl_cntr_set_thread_priority(spl_cntr_t *me_ptr)
{
   posal_thread_prio_t priority = 0;
   spl_cntr_get_thread_priority(me_ptr, &priority);
   posal_thread_set_prio(priority);
}

ar_result_t spl_cntr_handle_proc_duration_change(cu_base_t *base_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;

   spl_cntr_handle_fwk_events(me_ptr, FALSE /*is_data_path*/);

   return AR_EOK;
}

ar_result_t spl_cntr_handle_cntr_period_change(cu_base_t *base_ptr)
{

   spl_cntr_t *me_ptr   = (spl_cntr_t *)base_ptr;
   capi_err_t  err_code = CAPI_EOK;

   for (gu_sg_list_t *sg_list_ptr = me_ptr->cu.gu_ptr->sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))

   {
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))

      {
         spl_topo_module_t *module_ptr = (spl_topo_module_t *)module_list_ptr->module_ptr;

         if (FALSE == module_ptr->t_base.flags.supports_period)

         {
            continue;
         }

         intf_extn_param_id_period_t param;
         param.period_us = me_ptr->cu.period_us;

         err_code = gen_topo_capi_set_param(me_ptr->topo.t_base.gu.log_id,
                                            module_ptr->t_base.capi_ptr,
                                            INTF_EXTN_PARAM_ID_PERIOD,
                                            (int8_t *)&param,
                                            sizeof(param));

         if ((err_code != CAPI_EOK) && (err_code != CAPI_EUNSUPPORTED))

         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: setting container period failed",
                         module_ptr->t_base.gu.module_instance_id);
            return capi_err_to_ar_result(err_code);
         }
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_LOW_PRIO,
                         "Module 0x%lX: setting container period of %lu",
                         module_ptr->t_base.gu.module_instance_id,
                         param.period_us);
         }
      }
   }
   return AR_EOK;
}

static ar_result_t spl_cntr_create_channel_and_queues(spl_cntr_t *me_ptr, POSAL_HEAP_ID peer_cntr_heap_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // Queue names.
   char_t cmdQ_name[POSAL_DEFAULT_NAME_LEN];

   snprintf(cmdQ_name, POSAL_DEFAULT_NAME_LEN, "%s%8lX", "CPP", me_ptr->topo.t_base.gu.log_id);

   TRY(result, posal_channel_create(&me_ptr->cu.channel_ptr, peer_cntr_heap_id));

   TRY(result, posal_channel_create(&me_ptr->cu.ctrl_channel_ptr, peer_cntr_heap_id)); // imcl

   TRY(result,
       cu_init_queue(&me_ptr->cu,
                     cmdQ_name,
                     SPL_CNTR_MAX_CMD_Q_ELEMENTS,
                     SPL_CNTR_CMD_BIT_MASK,
                     cu_handle_cmd_queue,
                     me_ptr->cu.channel_ptr,
                     &me_ptr->cu.cmd_handle.cmd_q_ptr,
                     SPL_CNTR_GET_CMDQ_ADDR(me_ptr),
                     me_ptr->cu.heap_id));

   // Assign command handle pointer to point to the correct memory.
   me_ptr->cu.spf_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;

   // Clear masks from available bitmasks.
   me_ptr->cu.available_bit_mask &= (~(SPL_CNTR_CMD_BIT_MASK));

   /* Intialize the available ctrl chan mask*/
   me_ptr->cu.available_ctrl_chan_mask = 0xFFFFFFFF;

   /*Intra container IMCL ctrl queue*/
   cu_create_cmn_int_ctrl_port_queue(&me_ptr->cu, SPL_CNTR_CTRL_PORT_OFFSET);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
      posal_channel_destroy(&me_ptr->cu.channel_ptr);
      posal_channel_destroy(&me_ptr->cu.ctrl_channel_ptr);
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "spl_cntr_create_channel_and_queues failed result = %d",
                   result);
   }

   return result;
}

/* =======================================================================
Public Function Definitions
========================================================================== */
ar_result_t spl_cntr_create(cntr_cmn_init_params_t *init_params_ptr, spf_handle_t **handle, uint32_t cntr_type)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spl_cntr_t *me_ptr                   = NULL;
   *handle                              = NULL;
   uint32_t             stack_size      = 0;
   posal_thread_prio_t  thread_priority = 0;
   char_t               thread_name[POSAL_DEFAULT_NAME_LEN];
   bool_t               thread_launched = FALSE;
   gen_topo_init_data_t init_data;
   POSAL_HEAP_ID        my_heap_id, peer_heap_id;
   uint32_t             log_id = 0;

   if (AR_EOK != (result = cu_set_cntr_type_bits_in_log_id(cntr_type, &log_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "SPL_CNTR: Failed to set bits for container log id");
      return AR_EFAILED;
   }

   cu_set_bits(&log_id, (uint32_t)init_params_ptr->log_seq_id, LOG_ID_SEQUENCE_MASK, LOG_ID_SEQUENCE_SHIFT);

   // retrieve heap_ids from the container properties
   if (AR_EOK != cu_parse_get_self_and_peer_heap_ids(init_params_ptr->container_cfg_ptr, &my_heap_id, &peer_heap_id))
   {
      AR_MSG(DBG_ERROR_PRIO, "SPL_CNTR: Error getting the heap ID in create");
      return AR_EFAILED;
   }

   my_heap_id   = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, my_heap_id);
   peer_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, peer_heap_id);

   // Allocate instance struct. // needs to be aligned
   MALLOC_MEMSET(me_ptr, spl_cntr_t, SPL_CNTR_SIZE_W_2Q, my_heap_id, result);

   me_ptr->topo.t_base.gu.log_id = log_id;
   me_ptr->cu.cntr_type          = APM_CONTAINER_TYPE_ID_SC;
   me_ptr->cu.topo_ptr           = (void *)&me_ptr->topo;
   me_ptr->cu.gu_ptr             = &me_ptr->topo.t_base.gu;
   me_ptr->cu.configured_thread_prio                = APM_CONT_PRIO_IGNORE; // Assume configured priority, to be updated by tools
   me_ptr->cu.configured_sched_policy               = APM_CONT_SCHED_POLICY_IGNORE;
   me_ptr->cu.configured_core_affinity              = APM_CONT_CORE_AFFINITY_IGNORE;

   TRY(result, spl_cntr_parse_container_cfg(me_ptr, init_params_ptr->container_cfg_ptr));

   // Assign heap ID.
   me_ptr->cu.heap_id = my_heap_id;
   // Assign handler tables.
   me_ptr->cu.cmd_handler_table_ptr = spl_cntr_cmd_handler_table;

   me_ptr->cu.cmd_handler_table_size = SIZE_OF_ARRAY(spl_cntr_cmd_handler_table);

   me_ptr->cu.pm_info.weighted_kpps_scale_factor_q4 = UNITY_Q4;

   // Init the topo and setup cu pointers to topo and gu fields.
   memset(&init_data, 0, sizeof(init_data));
   init_data.topo_to_cntr_vtble_ptr = &topo_to_spl_cntr_vtable;

   TRY(result, spl_topo_init_topo(&me_ptr->topo, &init_data, me_ptr->cu.heap_id));

   me_ptr->cu.cntr_vtbl_ptr           = &spl_cntr_cntr_funcs;
   me_ptr->cu.topo_vtbl_ptr           = init_data.topo_cu_vtbl_ptr;
   me_ptr->cu.topo_island_vtbl_ptr    = init_data.topo_cu_island_vtbl_ptr;
   me_ptr->cu.ext_in_port_cu_offset   = offsetof(spl_cntr_ext_in_port_t, cu);
   me_ptr->cu.ext_out_port_cu_offset  = offsetof(spl_cntr_ext_out_port_t, cu);
   me_ptr->cu.ext_ctrl_port_cu_offset = offsetof(spl_cntr_ext_ctrl_port_t, cu);
   me_ptr->cu.int_ctrl_port_cu_offset = offsetof(spl_cntr_int_ctrl_port_t, cu);
   me_ptr->cu.module_cu_offset        = offsetof(spl_cntr_module_t, cu);

   // Initially all bit masks are available.
   me_ptr->cu.available_bit_mask = 0xFFFFFFFF;

   // Originally we don't have any requirements to process audio. We add them
   // as we add external ports.
   me_ptr->cu.gu_ptr->num_parallel_paths = 0;
   me_ptr->gpd_mask                      = 0;
   me_ptr->gpd_optional_mask             = 0;

   // Clear masks from resync and voice timer bitmasks.
   me_ptr->cu.available_bit_mask &= (~(SPL_CNTR_VOICE_RESYNC_BIT_MASK | SPL_CNTR_VOICE_TIMER_TICK_BIT_MASK));

   TRY(result, spl_cntr_create_channel_and_queues(me_ptr, peer_heap_id));

   me_ptr->cu.curr_chan_mask = SPL_CNTR_CMD_BIT_MASK;

   TRY(result, cu_init(&me_ptr->cu));

   TRY(result, spl_cntr_allocate_gpd_mask_arr(me_ptr));

   spl_cntr_get_thread_stack_size(me_ptr, &stack_size);

   TRY(result,
       spl_cntr_prepare_to_launch_thread(me_ptr, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN));

   TRY(result, cu_check_launch_thread(&me_ptr->cu, stack_size, 0, thread_priority, thread_name, &thread_launched));

   // Creation was successful. Return the container handle.
   *handle = (spf_handle_t *)&me_ptr->cu.spf_handle;

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "Created SPL_CNTR with container ID 0x%08lX, my heap_id = %lu, peer_heap_id = %lu",
                me_ptr->topo.t_base.gu.container_instance_id,
                (uint32_t)my_heap_id,
                (uint32_t)peer_heap_id);

   /** Register with GPR */
   if (AR_EOK != (result = __gpr_cmd_register( me_ptr->topo.t_base.gu.container_instance_id, cu_gpr_callback, &me_ptr->cu.spf_handle)))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_ERROR_PRIO,
                   "SPL_CNTR Create: Failed to register with GPR, result: 0x%8x",
                   result);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, (me_ptr ? me_ptr->topo.t_base.gu.log_id : 0))
   {
      SPL_CNTR_MSG(log_id, DBG_HIGH_PRIO, "Create failed");
      spl_cntr_destroy((cu_base_t*)me_ptr, NULL);
   }

   return result;
}

ar_result_t spl_cntr_destroy(cu_base_t *base_ptr, void *temp)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;
   spf_msg_t cmd_msg = { 0 };

   VERIFY(result, NULL != me_ptr);

   cmd_msg = me_ptr->cu.cmd_msg;

   log_id = me_ptr->topo.t_base.gu.log_id;

   bool_t is_duty_cycling_allowed = cu_check_all_subgraphs_duty_cycling_allowed(&me_ptr->cu);
   bool_t dcm_already_registered  = me_ptr->cu.pm_info.flags.register_with_dcm;

   /** De-register with  GPR    */
   if (AR_EOK != (result = __gpr_cmd_deregister( me_ptr->topo.t_base.gu.container_instance_id)))
   {
      SPL_CNTR_MSG(log_id, DBG_ERROR_PRIO, "SPL_CNTR: Failed to de-register with GPR, result: %lu", result);
   }

   cu_destroy_voice_info(&me_ptr->cu);

   cu_destroy_offload_info(&me_ptr->cu);

   cu_delete_all_event_nodes(&me_ptr->cu.event_list_ptr);

   if ((is_duty_cycling_allowed) && (dcm_already_registered))
   {
      (void)cu_deregister_with_dcm(&me_ptr->cu);
   }

   (void)cu_deregister_with_pm(&me_ptr->cu);

   TRY(result, cu_deinit(&me_ptr->cu));

   spl_topo_destroy_modules(&me_ptr->topo, TRUE /*b_destroy_all_modules*/);
   spl_topo_destroy_topo(&me_ptr->topo);

   gu_destroy_graph(&me_ptr->topo.t_base.gu, TRUE /*b_destroy_everything*/);
   // Don't need to update graph depth since the whole graph is being destroyed.

   /*Destroy the intra container IMCL Queue */
   cu_destroy_cmn_int_port_queue(&me_ptr->cu);

   // Destroy resp_q.
   if (me_ptr->cu.spf_handle.q_ptr)
   {
      posal_queue_deinit(me_ptr->cu.spf_handle.q_ptr);
      me_ptr->cu.spf_handle.q_ptr = NULL;
   }

   // Destroy cmd_q.
   if (me_ptr->cu.cmd_handle.cmd_q_ptr)
   {
      spf_svc_deinit_cmd_queue(me_ptr->cu.cmd_handle.cmd_q_ptr);
      me_ptr->cu.cmd_handle.cmd_q_ptr = NULL;

      // shouldn't be made NULL as cntr_cmn_destroy uses this to get handle->thread_id
      // me_ptr->cu.spf_handle.cmd_handle_ptr = NULL;

      // Channel is init'ed only if cmd_q exists.
      posal_channel_destroy(&me_ptr->cu.channel_ptr);

      /** Destroy the control port channel */
      posal_channel_destroy(&me_ptr->cu.ctrl_channel_ptr);
   }

   MFREE_NULLIFY(me_ptr->gpd_mask_arr);

   // If the thread is not launched, free up the me ptr as
   // APM cannot send destroy messsage without a thread context
   if (!me_ptr->cu.cmd_handle.thread_id)
   {
      MFREE_NULLIFY(me_ptr);
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, log_id)
   {
      SPL_CNTR_MSG(log_id, DBG_HIGH_PRIO, "destroy failed");
   }

   if (cmd_msg.payload_ptr)
   {
      spf_msg_ack_msg(&cmd_msg, AR_ETERMINATED);
   }

   SPL_CNTR_MSG(log_id, DBG_HIGH_PRIO, "CMD:DESTROY:Done");

   return AR_ETERMINATED;
}

/**
 * Prerequisite tasks for thread launching. Must call this function before
 * launching/relaunching thread.
 *
 * stack_size [out]: returns the new thread's stack size.
 * priority [out]: returns the new thread's priority.
 */
ar_result_t spl_cntr_prepare_to_launch_thread(spl_cntr_t *         me_ptr,
                                              posal_thread_prio_t *priority_ptr,
                                              char *               thread_name,
                                              uint32_t             name_length)
{

   snprintf(thread_name, name_length, "SC_%lX",  me_ptr->topo.t_base.gu.container_instance_id);

   spl_cntr_get_thread_priority(me_ptr, priority_ptr);

   return AR_EOK;
}

ar_result_t spl_cntr_parse_container_cfg(spl_cntr_t *me_ptr, apm_container_cfg_t *container_cfg_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   TRY(result, cu_parse_container_cfg(&me_ptr->cu, container_cfg_ptr));

   // SC can't be used as GLOBAL_DEV.
   VERIFY(result, APM_CONT_GRAPH_POS_GLOBAL_DEV != me_ptr->cu.position);

   // SC can't be an island container
   VERIFY(result, PM_MODE_DEFAULT == me_ptr->cu.pm_info.register_info.mode);

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/* Set container frame length in topo variable and ICB variable.
 * Both variables are needed because CU is not accessible in topo and topo is not
 * accessible by CU.
 */
ar_result_t spl_cntr_set_cntr_frame_len_us(spl_cntr_t *me_ptr, icb_frame_length_t *fm_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result           = AR_EOK;
   uint32_t    old_frame_len_us = me_ptr->cu.cntr_frame_len.frame_len_us;

   // inform modules about frame len change.
   // this should be done before handling the cu_handle_frame_len_change because setting frame duration to the modules can trigger DM update.
   // and DM update can cause change in variable input/output configuration.
   if ((0 != fm_ptr->frame_len_us) && (old_frame_len_us != fm_ptr->frame_len_us))
   {
      // Inform sync of threshold change.
      TRY(result, gen_topo_fwk_ext_set_cntr_frame_dur(&me_ptr->topo.t_base, fm_ptr->frame_len_us));
   }

   if (0 != fm_ptr->frame_len_us)
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_MED_PRIO,
                   "ICB: cntr frame length is %lu. old %lu",
                   fm_ptr->frame_len_us,
                   old_frame_len_us);

      TRY(result, cu_handle_frame_len_change(&me_ptr->cu, fm_ptr, fm_ptr->frame_len_us));
      // Above function takes care of the following:

      me_ptr->topo.cntr_frame_len.frame_len_us      = fm_ptr->frame_len_us;
      me_ptr->topo.cntr_frame_len.frame_len_samples = fm_ptr->frame_len_samples;
      me_ptr->topo.cntr_frame_len.sample_rate       = fm_ptr->sample_rate;
   }
   else
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "ICB: cntr frame length is 0.");
   }

   // update max buf len for all the ports in the topo
   if (old_frame_len_us != me_ptr->cu.cntr_frame_len.frame_len_us)
   {
      TRY(result, spl_topo_update_max_buf_len_for_all_modules(&me_ptr->topo));
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}


/**
 * this is callback from topo
 */
ar_result_t spl_cntr_create_module(gen_topo_t *           topo_ptr,
                                   gen_topo_module_t *    module_ptr,
                                   gen_topo_graph_init_t *graph_init_ptr)
{
   ar_result_t result = AR_EOK;
   // spl_cntr_t *       me_ptr         = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)topo_ptr);
   // spl_cntr_module_t *spl_cntr_module_ptr = (spl_cntr_module_t *)module_ptr;

   if (AMDB_MODULE_TYPE_FRAMEWORK == module_ptr->gu.module_type)
   {
      SPL_CNTR_MSG(topo_ptr->gu.log_id, DBG_ERROR_PRIO, "fwk module not supported");
   }

   spl_cntr_handle_fwk_extn_at_init(topo_ptr, module_ptr);

   return result;
}

/**
 * this is callback from topo
 */
ar_result_t spl_cntr_destroy_module(gen_topo_t *       topo_ptr,
                                    gen_topo_module_t *module_ptr,
                                    bool_t             reset_capi_dependent_dont_destroy)
{
   // spl_cntr_t *       me_ptr         = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, (spl_topo_t *)topo_ptr);
   spl_cntr_module_t *spl_cntr_module_ptr = (spl_cntr_module_t *)module_ptr;

   spl_cntr_handle_fwk_extn_at_deinit(topo_ptr, module_ptr);
   cu_delete_all_event_nodes(&spl_cntr_module_ptr->cu.event_list_ptr);

   return AR_EOK;
}

ar_result_t spl_cntr_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_proc_delay_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)cu_ptr;

   return spl_topo_aggregate_hw_acc_proc_delay(&me_ptr->topo, TRUE /* only aggregate */, hw_acc_proc_delay_ptr);
}

void spl_cntr_update_all_modules_info(spl_cntr_t *me_ptr)
{
     // mark all the l2 flags so that they can be re-evaluated.
     // this is to make sure that l2 flags are updated after graph mgmt commands
     spl_topo_simp_topo_set_all_event_flag(&me_ptr->topo);

     spl_topo_update_all_modules_info(&me_ptr->topo);
}

#ifdef USES_DEBUG_DEV_ENV
void spl_cntr_print_mem_req()
{
#if 0
   // not covered; path delay, trigger policy, metadata, temp mallocs for port operations,
   // sdata bufs is estimated worst case (as worst case ch count is used, not actual ch count per port).

   // queue elements is based on min elements 8 (CU_MIN_QUEUE_ELEMENTS_TO_PRE_ALLOC).
   uint32_t size_for_q = posal_queue_get_size() + 8 * sizeof(posal_queue_element_list_t) + 16 /*sizeof(posal_qurt_mutex_t)*/;
   uint32_t size_for_channel = 24;
   printf(  "\nSpecialized Container State Struct (topo, buf mgr)    %lu"
            "\nPer Subgraph                                          %lu"
            "\nPer Module                                            %lu"
            "\nPer input port                                        %lu"
            "\nPer ext input port (peer, WR)                         %lu"
            "\nPer output port                                       %lu"
            "\nPer ext output port (peer, RD)                        %lu"
            "\nPer control port                                      %lu"
            "\nPer ext control port                                  %lu"
            "\nPer max input ports                                   %lu"
            "\nPer max output ports                                  %lu"
            "\nPer unique media format per container                 %lu"
            "\nPer deinter buf per port (at least one per port)      %lu"
            "\nSize and number of topo buffers (TBD)                    "
            "\nSize and number of external buffers (TBD)                "
            "\n",
         sizeof(spl_cntr_t) + 40 /*topo_buf_manager_t*/ + size_for_channel  + size_for_q /*cmdq */ +
            size_for_channel /* ctrl */ + size_for_q /*intra cntr q*/ +
            28 /* gp signal etc */  + 32 /* GPR? */ + 1024 /* min stack size */,
         sizeof(gen_topo_sg_t) + sizeof(spf_list_node_t),
         sizeof(spl_cntr_module_t)+ 2 * sizeof(spf_list_node_t) /* sorted module, sg module */,
         sizeof(spl_cntr_input_port_t) + sizeof(spf_list_node_t),
         sizeof(spl_cntr_ext_in_port_t) + sizeof(gen_topo_ext_port_scratch_data_t) + size_for_q + sizeof(spf_list_node_t),
         sizeof(spl_cntr_output_port_t) + sizeof(spf_list_node_t),
         sizeof(spl_cntr_ext_out_port_t) + sizeof(gen_topo_ext_port_scratch_data_t) + size_for_q + sizeof(spf_list_node_t),
         sizeof(spl_cntr_int_ctrl_port_t) + sizeof(spf_list_node_t),
         sizeof(spl_cntr_ext_ctrl_port_t) + 2 * size_for_q /* incoming, outgoing */ + sizeof(spf_list_node_t),
         sizeof(gen_topo_port_scratch_data_t) + sizeof(capi_stream_data_v2_t *),
         sizeof(gen_topo_port_scratch_data_t) + sizeof(capi_stream_data_v2_t *),
         sizeof(topo_media_fmt_t) + sizeof(spf_list_node_t),
         sizeof(topo_buf_t)
         );
#endif // if 0
}
#endif

static uint32_t spl_cntr_aggregate_ext_in_port_delay_util(spl_cntr_t *me_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)gu_ext_in_port_ptr;

   // If the US is RT and cntr frame length is more than the upstream, then the input needs to buffer and
   // adds up to an additioanl path delay.
   // Note: SC always buffers up ext input data based on the nominal threshold irrespective of req_data_buf
   // flag.
   uint32_t       delay                    = 0;
   const uint32_t upstrm_frame_duration_us = ext_in_port_ptr->cu.upstream_frame_len.frame_len_us;
   const uint32_t self_frame_duration_us   = me_ptr->cu.cntr_frame_len.frame_len_us;
   if (ext_in_port_ptr->cu.prop_info.is_us_rt && upstrm_frame_duration_us && self_frame_duration_us &&
       self_frame_duration_us > upstrm_frame_duration_us)
   {
      // After first buffer arrives at the ext input, the no of additonal buffers required to start processing
      // ext input will account buffering delay.
      // Ex: cntr A (3ms) -> cntr B (7ms). Lets say at t=0 first 3ms arrives at B. B needs to wait for additional
      // 2*3ms buffers to start processing.
      uint32_t num_additional_buffers_req = TOPO_CEIL(self_frame_duration_us, upstrm_frame_duration_us) - 1;
      delay                               = num_additional_buffers_req * upstrm_frame_duration_us;
   }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_1
   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "Ext-in-port 0x%lx us frame len:%lu self frame len:%lu us_rt:%lu delay %lu",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                upstrm_frame_duration_us,
                self_frame_duration_us,
                ext_in_port_ptr->cu.prop_info.is_us_rt,
                delay);
#endif

   return delay;
}

/**
 * Callback from CU to get additional container delays due to requires data buf etc
 */
uint32_t spl_cntr_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   return spl_cntr_aggregate_ext_in_port_delay_util(me_ptr, gu_ext_in_port_ptr);
}
/**
 * callback from topo layer to get container delays due to requires data buf etc
 */
uint32_t spl_cntr_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo,  (spl_topo_t *)topo_ptr);
   return cu_aggregate_ext_in_port_delay(&me_ptr->cu, gu_ext_in_port_ptr);
}

/**
 * callback from CU to get additional container delays.
 */
uint32_t spl_cntr_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   //spl_cntr_t *me_ptr = (spl_cntr_t *)base_ptr;
   return 0;
}

/**
 * callback from topo to get container delays
 */
uint32_t spl_cntr_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo,  (spl_topo_t *)topo_ptr);
   return cu_aggregate_ext_out_port_delay(&me_ptr->cu, gu_ext_out_port_ptr);
}

ar_result_t spl_cntr_dcm_topo_set_param(void *cu_ptr)
{
   ar_result_t result = AR_EOK;
   spl_cntr_t *me_ptr = (spl_cntr_t *)cu_ptr;
   for (gu_sg_list_t *sg_list_ptr = me_ptr->topo.t_base.gu.sg_list_ptr; (NULL != sg_list_ptr);
        LIST_ADVANCE(sg_list_ptr))
   {
      gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
      for (gu_module_list_t *module_list_ptr = sg_ptr->module_list_ptr; NULL != module_list_ptr;
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         if (TRUE == module_ptr->flags.supports_module_allow_duty_cycling)
         {
            intf_extn_param_id_cntr_duty_cycling_enabled_t p = { .is_cntr_duty_cycling =
                                                                    me_ptr->cu.pm_info.flags.register_with_dcm };
            me_ptr->cu.pm_info.flags.module_disallows_duty_cycling = me_ptr->cu.pm_info.flags.register_with_dcm;
            result = gen_topo_capi_set_param(module_ptr->topo_ptr->gu.log_id,
                                             module_ptr->capi_ptr,
                                             INTF_EXTN_PARAM_ID_CNTR_DUTY_CYCLING_ENABLED,
                                             (int8_t *)&p,
                                             sizeof(p));
            if (AR_DID_FAIL(result))
            {
               TOPO_MSG(module_ptr->topo_ptr->gu.log_id,
                        DBG_ERROR_PRIO,
                        "topo_port_rt_prop:set cntr duty cycling flag on module 0x%x, "
                        "me_ptr->cu.pm_info.register_with_dcm %u "
                        "failed",
                        module_ptr->gu.module_instance_id,
                        me_ptr->cu.pm_info.flags.register_with_dcm);
               return result;
            }
         }
      }
   }
   return result;
}
