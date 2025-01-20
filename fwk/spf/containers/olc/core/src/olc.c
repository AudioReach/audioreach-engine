/**
 * \file olc.c
 * \brief
 *     This file contains functions for offload processing container.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_i.h"


/*=======================================================================
Declarations
========================================================================== */

// Maximum number of commands expected ever in command queue.
static const uint32_t OLC_MAX_CMD_Q_ELEMENTS = 8;

/* =======================================================================
CU Virtual Table Function Implementations
========================================================================== */

/* =======================================================================
Static Function Declarations
========================================================================== */
static ar_result_t olc_create_channel_and_queues(olc_t *me_ptr, POSAL_HEAP_ID peer_cntr_heap_id);
ar_result_t olc_destroy(olc_t *me_ptr);
/* =======================================================================
Function Tables
========================================================================== */

// most frequent commands be on top
const cu_msg_handler_t olc_cmd_handler_table[] = {
   { SPF_MSG_CMD_GPR, olc_gpr_cmd },
   { SPF_MSG_CMD_SET_CFG, olc_set_get_cfg },
   { SPF_MSG_CMD_GET_CFG, olc_set_get_cfg },
   { SPF_MSG_CMD_INFORM_ICB_INFO, olc_icb_info_from_downstream },
   { SPF_MSG_CMD_CTRL_PORT_TRIGGER_MSG, cu_handle_ctrl_port_trigger_cmd },
   { SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE, olc_handle_peer_port_property_update_cmd },
   { SPF_MSG_CMD_UPSTREAM_STOPPED_ACK, olc_handle_upstream_stop_cmd },
   { SPF_MSG_CMD_GRAPH_OPEN, olc_graph_open },
   { SPF_MSG_CMD_GRAPH_PREPARE, olc_graph_prepare },
   { SPF_MSG_CMD_GRAPH_START, olc_graph_start },
   { SPF_MSG_CMD_GRAPH_SUSPEND, olc_graph_suspend },
   { SPF_MSG_CMD_GRAPH_STOP, olc_graph_stop },
   { SPF_MSG_CMD_GRAPH_FLUSH, olc_graph_flush },
   { SPF_MSG_CMD_GRAPH_CLOSE, olc_graph_close },
   { SPF_MSG_CMD_GRAPH_CONNECT, olc_graph_connect },
   { SPF_MSG_CMD_GRAPH_DISCONNECT, olc_graph_disconnect },
   { SPF_MSG_CMD_DESTROY_CONTAINER, olc_destroy_container },
   { SPF_MSG_CMD_MEDIA_FORMAT, olc_ctrl_path_media_fmt },
   { SPF_MSG_CMD_REGISTER_CFG, olc_set_get_cfg },
   { SPF_MSG_CMD_DEREGISTER_CFG, olc_set_get_cfg },
   { SPF_MSG_CMD_SATELLITE_GPR, olc_sgm_gpr_cmd },
};

/* CU call back functions for container specific handling */
// clang-format off
static const cu_cntr_vtable_t olc_cntr_funcs = {
   .port_data_thresh_change                 = olc_handle_port_data_thresh_change_event,

   .aggregate_kpps_bw                       = olc_aggregate_kpps_bw,

   .operate_on_subgraph                     = olc_operate_on_subgraph,
   .post_operate_on_subgraph                = olc_post_operate_on_subgraph,
   .set_get_cfg                             = NULL,
   .register_events                         = NULL,

   .init_ext_in_port                        = olc_init_ext_in_port,
   .deinit_ext_in_port                      = olc_deinit_ext_in_port,
   .operate_on_ext_in_port                  = olc_operate_on_ext_in_port,
   .post_operate_on_ext_in_port             = olc_post_operate_on_ext_in_port,
   .post_operate_on_ext_out_port            = NULL,
   .input_media_format_received             = olc_input_media_format_received,

   .init_ext_out_port                       = olc_init_ext_out_port,
   .deinit_ext_out_port                     = olc_deinit_ext_out_port,
   .operate_on_ext_out_port                 = olc_operate_on_ext_out_port,
   .ext_out_port_apply_pending_media_fmt    = olc_ext_out_port_apply_pending_media_fmt_cmd_path,
   .ext_out_port_recreate_bufs              = olc_recreate_ext_out_buffers,

   .init_ext_ctrl_port                      = cu_init_ext_ctrl_port,
   .deinit_ext_ctrl_port                    = cu_deinit_ext_ctrl_port,
   .operate_on_ext_ctrl_port                = cu_operate_on_ext_ctrl_port,
   .connect_ext_ctrl_port                   = cu_connect_ext_ctrl_port,

   .apply_downgraded_state_on_output_port   = olc_apply_downgraded_state_on_output_port,
   .apply_downgraded_state_on_input_port    = olc_apply_downgraded_state_on_input_port,
   .destroy_all_metadata                    = gen_topo_destroy_all_metadata,

   .handle_proc_duration_change             = NULL,
   .update_path_delay                       = olc_cu_update_path_delay,
   .aggregate_hw_acc_proc_delay             = olc_aggregate_hw_acc_proc_delay,
   .vote_against_island                     = NULL,
   .exit_island_temporarily                 = NULL,
   .get_additional_ext_in_port_delay_cu_cb  = olc_get_additional_ext_in_port_delay_cu_cb,
   .get_additional_ext_out_port_delay_cu_cb = olc_get_additional_ext_out_port_delay_cu_cb,

   .check_bump_up_thread_priority           = NULL,
   .dcm_topo_set_param 						= NULL,
   .rtm_dump_data_port_media_fmt     = gen_topo_rtm_dump_data_port_mf_for_all_ports,

};

static const topo_to_cntr_vtable_t topo_to_olc_vtable = {
   .clear_eos                                   = olc_clear_eos,
   .raise_data_to_dsp_service_event             = NULL,  // unsupported
   .raise_data_from_dsp_service_event           = NULL,  // unsupported
   .raise_data_to_dsp_client_v2                 = NULL,
   .handle_capi_event                           = NULL,  // unsupported
   .set_pending_out_media_fmt                   = NULL,  // unsupported
   .check_apply_ext_out_ports_pending_media_fmt = NULL,  // unsupported
   .set_ext_in_port_prev_actual_data_len        = NULL,  // unsupported
   .update_input_port_max_samples               = NULL,  // unsupported
   .ext_in_port_get_buf_len                     = NULL,  // unsupported
   .clear_topo_req_samples                      = NULL,  // unsupported
   .set_propagated_prop_on_ext_output           = olc_set_propagated_prop_on_ext_output,
   .set_propagated_prop_on_ext_input            = olc_set_propagated_prop_on_ext_input,
   .ext_out_port_has_buffer                     = NULL,  // unsupported
   .ext_in_port_has_data_buffer                 = NULL,  // unsupported
   .algo_delay_change_event                     = NULL,  // unsupported
   .handle_frame_done                           = NULL,  // unsupported
   .ext_in_port_has_enough_data                 = NULL,  // unsupported
   .ext_out_port_get_ext_buf                    = NULL,  // unsupported
   .ext_in_port_dfg_eos_left_port               = NULL,  // unsupported
   .ext_in_port_clear_timestamp_discontinuity   = NULL,  // unsupported
   .create_module                               = olc_create_module,
   .destroy_module                              = olc_destroy_module,
   .check_insert_missing_eos_on_next_module     = NULL,  // todo : check
   .update_icb_info                             = NULL,  // unsupported
   .vote_against_island                         = NULL,


   .aggregate_ext_in_port_delay                 = olc_aggregate_ext_in_port_delay_topo_cb,
   .aggregate_ext_out_port_delay                = olc_aggregate_ext_out_port_delay_topo_cb,
   .check_for_error_print                       = NULL,

   .notify_ts_disc_evt                          = NULL,
   .module_buffer_access_event                  = NULL,
};

// function table for response handling.
// Shared with SGM driver to call when the response from the satellite is received
static sgmc_rsp_h_vtable_t sgm_cmd_rsp_hp =
{
   .graph_open_rsp_h                  = olc_graph_open_rsp_h,
   .graph_prepare_rsp_h               = olc_graph_prepare_rsp_h,
   .graph_start_rsp_h                 = olc_graph_start_rsp_h,
   .graph_suspend_rsp_h               = olc_graph_suspend_rsp_h,
   .graph_stop_rsp_h                  = olc_graph_stop_rsp_h,
   .graph_flush_rsp_h                 = olc_graph_flush_rsp_h,
   .graph_close_rsp_h                 = olc_graph_close_rsp_h,
   .graph_set_get_cfg_rsp_h           = olc_graph_set_get_cfg_rsp_h,
   .graph_set_get_cfg_packed_rsp_h    = olc_graph_set_get_packed_cfg_rsp_h,
   .graph_set_persistent_rsp_h        = olc_graph_set_persistent_cfg_rsp_h, //gpr cmd
   .graph_set_persistent_packed_rsp_h = olc_graph_set_persistent_packed_rsp_h, //gkmsg from apm
   .graph_event_reg_rsp_h             = olc_graph_event_reg_rsp_h
};

// clang-format on
/* =======================================================================
Static Function Definitions
========================================================================== */

void olc_handle_frame_length_n_state_change(olc_t *me_ptr, uint32_t period_us)
{
   bool_t is_at_least_one_sg_started = me_ptr->cu.flags.is_cntr_started;

   olc_vote_pm_conditionally(me_ptr, period_us, is_at_least_one_sg_started);

   if (is_at_least_one_sg_started)
   {
      if (0 != period_us)
      {
         olc_set_thread_priority(me_ptr, period_us);
      }

      olc_cu_update_path_delay(&me_ptr->cu, CU_PATH_ID_ALL_PATHS);

      OLC_MSG(me_ptr->topo.gu.log_id,
              DBG_HIGH_PRIO,
              "Setting proc delay, weighted scale factor in q4 0x%lx from frame length handling",
              me_ptr->cu.pm_info.weighted_kpps_scale_factor_q4);
   }
}

static ar_result_t olc_create_channel_and_queues(olc_t *me_ptr, POSAL_HEAP_ID peer_cntr_heap_id)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   // Queue names.
   char_t cmdQ_name[POSAL_DEFAULT_NAME_LEN];

   snprintf(cmdQ_name, POSAL_DEFAULT_NAME_LEN, "%s%8lX", "COLC", me_ptr->topo.gu.log_id);

   if (AR_DID_FAIL(result = posal_channel_create(&me_ptr->cu.channel_ptr, peer_cntr_heap_id)))
   {
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "OLC: Unable to create channel, result: %lu", result);
      return result;
   }

   TRY(result,
       cu_init_queue(&me_ptr->cu,
                     cmdQ_name,
                     OLC_MAX_CMD_Q_ELEMENTS,
                     OLC_CMD_BIT_MASK,
                     cu_handle_cmd_queue,
                     me_ptr->cu.channel_ptr,
                     &me_ptr->cu.cmd_handle.cmd_q_ptr,
                     CU_PTR_PUT_OFFSET(me_ptr, OLC_CMDQ_OFFSET),
                     me_ptr->cu.heap_id));

   // Assign command handle pointer to point to the correct memory.
   me_ptr->cu.spf_handle.cmd_handle_ptr = &me_ptr->cu.cmd_handle;

   // Clear masks from available bitmasks.
   me_ptr->cu.available_bit_mask &= (~(OLC_CMD_BIT_MASK));

   /* Intialize control channel and mask*/
   if (AR_DID_FAIL(result = posal_channel_create(&me_ptr->cu.ctrl_channel_ptr, peer_cntr_heap_id)))
   {
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "OLC: Unable to create ctrl channel, result: %lu", result);
      posal_channel_destroy(&me_ptr->cu.channel_ptr);
      return result;
   }

   /* Intialize the available ctrl chan mask*/
   me_ptr->cu.available_ctrl_chan_mask = 0xFFFFFFFF;

   OLC_MSG(me_ptr->topo.gu.log_id, DBG_HIGH_PRIO, "olc_create_channel and_queues done result = %d", result);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
      posal_channel_destroy(&me_ptr->cu.channel_ptr);
      posal_channel_destroy(&me_ptr->cu.ctrl_channel_ptr);
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_ERROR_PRIO, "olc_create_channel and_queues failed result = %d", result);
   }

   return result;
}

/**
 * Prerequisite tasks for thread launching. Must call this function before
 * launching/relaunching thread.
 *
 * stack_size [out]: returns the new thread's stack size.
 * priority [out]: returns the new thread's priority.
 */
ar_result_t olc_prepare_to_launch_thread(olc_t *              me_ptr,
                                         uint32_t *           stack_size,
                                         posal_thread_prio_t *priority,
                                         char *               thread_name,
                                         uint32_t             name_length)
{

   snprintf(thread_name, name_length, "OLC_%lX", me_ptr->topo.gu.container_instance_id);

   olc_get_thread_stack_size(me_ptr, stack_size);
   olc_get_set_thread_priority(me_ptr, priority, FALSE /*should_set*/);

   return AR_EOK;
}

/* =======================================================================
Public Function Definitions
========================================================================== */

/*Function to create and initialize an Offload container instance */
ar_result_t olc_create(cntr_cmn_init_params_t *init_params_ptr, spf_handle_t **handle, uint32_t cntr_type)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   olc_t *me_ptr                        = NULL;
   *handle                              = NULL;
   uint32_t             stack_size      = 0;
   posal_thread_prio_t  thread_priority = 0;
   char_t               thread_name[POSAL_DEFAULT_NAME_LEN];
   bool_t               thread_launched = FALSE;
   gen_topo_init_data_t init_data;
   POSAL_HEAP_ID        my_heap_id, peer_heap_id;

   uint32_t host_domain_id = 0;
   uint32_t log_id         = 0;

   if (AR_EOK != (result = cu_set_cntr_type_bits_in_log_id(cntr_type, &log_id)))
   {
      AR_MSG(DBG_ERROR_PRIO, "OLC: Failed to set cntr log id bits");
      return AR_EFAILED;
   }

   cu_set_bits(&log_id, (uint32_t)init_params_ptr->log_seq_id, LOG_ID_SEQUENCE_MASK, LOG_ID_SEQUENCE_SHIFT);

   // retrieve heap_ids from the container properties
   if (AR_EOK != cu_parse_get_self_and_peer_heap_ids(init_params_ptr->container_cfg_ptr, &my_heap_id, &peer_heap_id))
   {
      AR_MSG(DBG_ERROR_PRIO, "OLC:: Error getting the heap ID in create");
      return AR_EFAILED;
   }

   my_heap_id   = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, my_heap_id);
   peer_heap_id = MODIFY_HEAP_ID_FOR_MEM_TRACKING(log_id, peer_heap_id);

   OLC_MSG(log_id, DBG_HIGH_PRIO, "start Offload container Create ");

   // retrieve heap_id from the container properties
   if (AR_EOK != cu_parse_get_self_and_peer_heap_ids(init_params_ptr->container_cfg_ptr, &my_heap_id, &peer_heap_id))
   {
      OLC_MSG(log_id, DBG_ERROR_PRIO, "Error getting the heap ID in create");
      return AR_EFAILED;
   }
   // Allocate instance struct.
   me_ptr = (olc_t *)posal_memory_malloc(OLC_SIZE, my_heap_id);
   //                                                    OLC q, sgm event + resp q
   if (NULL == me_ptr)
   {
      OLC_MSG(log_id,
              DBG_ERROR_PRIO,
              "OLC: Malloc failed from heap ID %u. Required Size %lu",
              (uint32_t)my_heap_id,
              OLC_SIZE);
      return AR_ENOMEMORY;
   }
   memset(me_ptr, 0, OLC_SIZE);

   /*Initialize gu_ptr, such that if olc_destroy is called due to some failure, we don't crash for this being null */
   me_ptr->cu.topo_ptr    = (void *)&me_ptr->topo;
   me_ptr->cu.gu_ptr      = &me_ptr->topo.gu;
   me_ptr->topo.gu.log_id = log_id;
   me_ptr->cu.cntr_type   = APM_CONTAINER_TYPE_ID_OLC;

   // Get the Host domain ID
   __gpr_cmd_get_host_domain_id(&host_domain_id);
   me_ptr->host_domain_id = host_domain_id;
   me_ptr->cu.configured_thread_prio                = APM_CONT_PRIO_IGNORE; // Assume configured priority, to be updated by tools
   me_ptr->cu.configured_sched_policy               = APM_CONT_SCHED_POLICY_IGNORE;
   me_ptr->cu.configured_core_affinity              = APM_CONT_CORE_AFFINITY_IGNORE;

   // Parse the container configuration
   TRY(result, olc_parse_container_cfg(me_ptr, init_params_ptr->container_cfg_ptr));

   // Assign heap ID.
   me_ptr->cu.heap_id = my_heap_id;

   // Assign handler tables.
   me_ptr->cu.cmd_handler_table_ptr  = olc_cmd_handler_table;
   me_ptr->cu.cmd_handler_table_size = SIZE_OF_ARRAY(olc_cmd_handler_table);

   // Init to default scale factor in q4 format
   me_ptr->cu.pm_info.weighted_kpps_scale_factor_q4 = UNITY_Q4;

   // Init the topo and setup cu pointers to topo and gu fields.
   memset(&init_data, 0, sizeof(init_data));
   init_data.topo_to_cntr_vtble_ptr = &topo_to_olc_vtable;

   TRY(result, gen_topo_init_topo(&me_ptr->topo, &init_data, me_ptr->cu.heap_id));

   me_ptr->cu.cntr_vtbl_ptr           = &olc_cntr_funcs;
   me_ptr->cu.topo_vtbl_ptr           = init_data.topo_cu_vtbl_ptr;

   me_ptr->cu.ext_in_port_cu_offset   = offsetof(olc_ext_in_port_t, cu);
   me_ptr->cu.ext_out_port_cu_offset  = offsetof(olc_ext_out_port_t, cu);
   me_ptr->cu.ext_ctrl_port_cu_offset = offsetof(olc_ext_ctrl_port_t, cu);
   // me_ptr->cu.int_ctrl_port_cu_offset = offsetof(olc_int_ctrl_port_t, cu);  //DFGC
   me_ptr->cu.module_cu_offset = offsetof(olc_module_t, cu);

   // Initially all bit masks are available.
   me_ptr->cu.available_bit_mask = 0xFFFFFFFF & (~OLC_SYSTEM_Q_BIT_MASK);

   /* Intialize the available ctrl chan mask*/
   me_ptr->cu.available_ctrl_chan_mask = (0xFFFFFFFF);

   // Create the channels and Queues
   TRY(result, olc_create_channel_and_queues(me_ptr, peer_heap_id));

   // Initialize the satellite graph management driver
   TRY(result, sgm_init(&me_ptr->spgm_info, &me_ptr->cu, (void *)&sgm_cmd_rsp_hp, OLC_SGM_Q_OFFSET));

   // Initialize the satellite graph management driver
   TRY(result, olc_serv_reg_notify_init(me_ptr));

   // Intialize the wait mask only with the CMD & system queue handler mask
   me_ptr->cu.curr_chan_mask |= (OLC_SYSTEM_Q_BIT_MASK | OLC_CMD_BIT_MASK);

   // Initialize the CU
   TRY(result, cu_init(&me_ptr->cu));

   // Prepare to Launch the OLC container thread.
   TRY(result,
       olc_prepare_to_launch_thread(me_ptr, &stack_size, &thread_priority, thread_name, POSAL_DEFAULT_NAME_LEN));

   // Launch the OLC container thread.
   TRY(result, cu_check_launch_thread(&me_ptr->cu, stack_size, 0, thread_priority, thread_name, &thread_launched));

   // Creation was successful. Return the container handle.
   *handle = (spf_handle_t *)&me_ptr->cu.spf_handle;

   OLC_MSG(log_id,
           DBG_HIGH_PRIO,
           SPF_LOG_PREFIX "Created OLC with container ID 0x%08lX, from heap_id %u , peer_heap_id = %lu",
           me_ptr->cu.gu_ptr->container_instance_id,
           (uint32_t)my_heap_id,
           (uint32_t)peer_heap_id);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
      OLC_MSG(log_id, DBG_ERROR_PRIO, "OLC create failed");
      olc_destroy(me_ptr);
   }

   return result;
}

ar_result_t olc_destroy(olc_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id           = 0;
   uint32_t cntr_instance_id = 0;

   VERIFY(result, NULL != me_ptr);

   log_id           = me_ptr->topo.gu.log_id;
   cntr_instance_id = me_ptr->topo.gu.container_instance_id;

   cu_delete_all_event_nodes(&me_ptr->cu.event_list_ptr);

   (void)cu_deregister_with_pm(&me_ptr->cu);

   sgm_deinit(&me_ptr->spgm_info);

   cu_destroy_voice_info(&me_ptr->cu);

   TRY(result, cu_deinit(&me_ptr->cu));

   gen_topo_destroy_modules(&me_ptr->topo, TRUE /*b_destroy_all_modules*/);

   // if this container has a port connected to another SG, then the port alone might be destroyed in this cmd.
   // Also, very important to call gen_topo_destroy_modules before cu_deinit_external_ports.
   // This ensures gpr dereg happens before ext port is deinit'ed. If not, crash may occur if HLOS pushes buf when we
   // deinit.
   cu_deinit_external_ports(&me_ptr->cu, FALSE /*b_ignore_ports_from_sg_close*/, TRUE /*force_deinit_all_ports*/);

   gen_topo_destroy_topo(&me_ptr->topo);

   gu_destroy_graph(me_ptr->cu.gu_ptr, TRUE /*b_destroy_everything*/);

   // Destroy sys_q.
   olc_serv_reg_notify_deinit(me_ptr);

   // deinit resp_q.
   if (me_ptr->cu.spf_handle.q_ptr)
   {
      posal_queue_deinit(me_ptr->cu.spf_handle.q_ptr);
      me_ptr->cu.spf_handle.q_ptr = NULL;
   }

   // deinit cmd_q.
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

   // If the thread is not launched, free up the me ptr as
   // APM cannot send destroy messsage without a thread context
   if (!me_ptr->cu.cmd_handle.thread_id)
   {
      MFREE_NULLIFY(me_ptr);
   }

   OLC_MSG(log_id, DBG_HIGH_PRIO, "Destroyed with container ID 0x%08lX", cntr_instance_id);

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
      OLC_MSG(log_id,
              DBG_ERROR_PRIO,
              "Destroy OLC with "
              "container ID 0x%08lX failed",
              cntr_instance_id);
   }

   return AR_ETERMINATED;
}

/* Function to parse the container configuration and validate the properties */
ar_result_t olc_parse_container_cfg(olc_t *me_ptr, apm_container_cfg_t *container_cfg_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   TRY(result, cu_parse_container_cfg(&me_ptr->cu, container_cfg_ptr));

   // Processor domain should not change.
   VERIFY(result, me_ptr->host_domain_id == me_ptr->cu.proc_domain);

   VERIFY(result, PM_MODE_DEFAULT == me_ptr->cu.pm_info.register_info.mode);

   CATCH(result, OLC_MSG_PREFIX, me_ptr->topo.gu.log_id)
   {
   }

   return result;
}


