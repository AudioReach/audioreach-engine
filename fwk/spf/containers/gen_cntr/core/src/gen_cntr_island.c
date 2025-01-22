/**
 * \file gen_cntr_island.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"
#include "spf_svc_utils.h"

#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

// most frequent commands be on top
const cu_msg_handler_t gen_cntr_cmd_handler_table[] = {
   { SPF_MSG_CMD_GPR, gen_cntr_gpr_cmd },
   { SPF_MSG_CMD_SET_CFG, gen_cntr_set_get_cfg },
   { SPF_MSG_CMD_GET_CFG, gen_cntr_set_get_cfg },
   { SPF_MSG_CMD_INFORM_ICB_INFO, gen_cntr_cmd_icb_info_from_downstream },
   { SPF_MSG_CMD_CTRL_PORT_TRIGGER_MSG, gen_cntr_handle_ctrl_port_trigger_cmd },
   { SPF_MSG_CMD_PEER_PORT_PROPERTY_UPDATE, gen_cntr_handle_peer_port_property_update_cmd },
   { SPF_MSG_CMD_UPSTREAM_STOPPED_ACK, gen_cntr_handle_upstream_stop_cmd },
   { SPF_MSG_CMD_GRAPH_OPEN, gen_cntr_graph_open },
   { SPF_MSG_CMD_GRAPH_PREPARE, gen_cntr_graph_prepare },
   { SPF_MSG_CMD_GRAPH_START, gen_cntr_graph_start },
   { SPF_MSG_CMD_GRAPH_SUSPEND, gen_cntr_graph_suspend },
   { SPF_MSG_CMD_GRAPH_STOP, gen_cntr_graph_stop },
   { SPF_MSG_CMD_GRAPH_FLUSH, gen_cntr_graph_flush },
   { SPF_MSG_CMD_GRAPH_CLOSE, gen_cntr_graph_close },
   { SPF_MSG_CMD_GRAPH_CONNECT, gen_cntr_graph_connect },
   { SPF_MSG_CMD_GRAPH_DISCONNECT, gen_cntr_graph_disconnect },
   { SPF_MSG_CMD_DESTROY_CONTAINER, gen_cntr_destroy_container },
   { SPF_MSG_CMD_MEDIA_FORMAT, gen_cntr_ctrl_path_media_fmt_cmd },
   { SPF_MSG_CMD_REGISTER_CFG, gen_cntr_set_get_cfg },
   { SPF_MSG_CMD_DEREGISTER_CFG, gen_cntr_set_get_cfg },
};

/* CU call back functions for container specific handling */
// clang-format off
const cu_cntr_vtable_t gen_cntr_cntr_funcs = {
   .port_data_thresh_change              = gen_cntr_handle_port_data_thresh_change,
   .aggregate_kpps_bw                    = gen_cntr_aggregate_kpps_bw,

   .operate_on_subgraph                  = gen_cntr_operate_on_subgraph,
   .operate_on_subgraph_async            = gen_cntr_operate_on_subgraph_async,

   .post_operate_on_subgraph             = gen_cntr_post_operate_on_subgraph,

   .set_get_cfg                          = gen_cntr_set_get_cfg_util,
   .register_events                      = gen_cntr_register_events_utils,

   .init_ext_in_port                     = gen_cntr_init_ext_in_port,
   .deinit_ext_in_port                   = gen_cntr_deinit_ext_in_port,
   .operate_on_ext_in_port               = gen_cntr_operate_on_ext_in_port,
   .post_operate_on_ext_in_port          = gen_cntr_post_operate_on_ext_in_port,
   .post_operate_on_ext_out_port         = NULL,
   .input_media_format_received          = gen_cntr_input_media_format_received,

   .init_ext_out_port                    = gen_cntr_init_ext_out_port,
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

   .dcm_topo_set_param 						 = gen_cntr_dcm_topo_set_param,


   .handle_cntr_period_change 				 = gen_cntr_handle_cntr_period_change,

   .initiate_duty_cycle_island_entry         = gen_cntr_initiate_duty_cycle_island_entry,
   .initiate_duty_cycle_island_exit          = gen_cntr_initiate_duty_cycle_island_exit,
};

const topo_to_cntr_vtable_t topo_to_gen_cntr_vtable = {
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
   .create_module                               = gen_cntr_create_module,
   .destroy_module                              = gen_cntr_destroy_module,
   .check_insert_missing_eos_on_next_module     = gen_cntr_check_insert_missing_eos_on_next_module,
   .update_icb_info                             = gen_cntr_update_icb_info,
   .vote_against_island                         = gen_cntr_vote_against_island_topo,

   .aggregate_ext_in_port_delay                 = gen_cntr_aggregate_ext_in_port_delay_topo_cb,
   .aggregate_ext_out_port_delay                = gen_cntr_aggregate_ext_out_port_delay_topo_cb,
   .check_for_error_print                       = gen_cntr_check_for_err_print,

   .notify_ts_disc_evt                          = gen_cntr_notify_timestamp_discontinuity_event_cb,
   .module_buffer_access_event                  = NULL,

   .check_if_any_ext_in_has_to_preserve_prebuffer = gen_cntr_check_if_any_ext_in_has_to_preserve_prebuffer,
};

// clang-format on
const uint32_t g_sizeof_gen_cntr_cmd_handler_table = (SIZE_OF_ARRAY(gen_cntr_cmd_handler_table));
