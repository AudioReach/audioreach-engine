#ifndef GEN_CNTR_UTIL_H
#define GEN_CNTR_UTIL_H
/**
 * \file gen_cntr_utils.h
 * \brief
 *     This file contains utility functions for GEN_CNTR
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr.h"
#include "gen_cntr_cmn_utils.h"
#include "apm_container_api.h"
#include "gen_topo.h"
#include "gen_cntr_wr_sh_mem_ep.h"
#include "gen_cntr_rd_sh_mem_ep.h"
#include "gen_cntr_placeholder.h"
#include "gen_cntr_offload_utils.h"
#include "gen_cntr_err_check.h"
#include "gen_cntr_bt_codec_fwk_ext.h"
#include "gen_cntr_metadata.h"
#include "gen_cntr_sync_fwk_ext.h"
#include "gen_cntr_pure_st.h"
#include "gen_cntr_peer_cntr.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef struct gen_cntr_t               gen_cntr_t;
typedef struct gen_cntr_timestamp_t     gen_cntr_timestamp_t;
typedef struct gen_cntr_ext_out_port_t  gen_cntr_ext_out_port_t;
typedef struct gen_cntr_ext_in_port_t   gen_cntr_ext_in_port_t;
typedef struct gen_cntr_ext_ctrl_port_t gen_cntr_ext_ctrl_port_t;
typedef struct gen_cntr_circ_buf_list_t gen_cntr_circ_buf_list_t;
typedef struct gen_cntr_module_t        gen_cntr_module_t;

/** ------------------------------------------- util --------------------------------------------------------------*/
ar_result_t gen_cntr_get_thread_stack_size(gen_cntr_t *me_ptr, uint32_t *stack_size_ptr, uint32_t *root_stack_size_ptr);
bool_t      gen_cntr_check_if_time_critical(gen_cntr_t *me_ptr);
bool_t gen_cntr_check_bump_up_thread_priority(cu_base_t *cu_ptr, bool_t is_bump_up, posal_thread_prio_t original_prio);
ar_result_t gen_cntr_get_set_thread_priority(gen_cntr_t *        me_ptr,
                                             int32_t *           priority_ptr,
                                             bool_t              should_set,
                                             uint16_t            bump_up_factor,
                                             posal_thread_prio_t original_prio);
ar_result_t gen_cntr_handle_proc_duration_change(cu_base_t *base_ptr);
ar_result_t gen_cntr_handle_cntr_period_change(cu_base_t *base_ptr);
ar_result_t gen_cntr_handle_frame_done(gen_topo_t *gen_topo_ptr, uint8_t path_index);
ar_result_t gen_cntr_ext_out_port_reset(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_ext_out_port_basic_reset(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
ar_result_t gen_cntr_ext_in_port_reset(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_flush_input_data_queue(gen_cntr_t *            me_ptr,
                                            gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                            bool_t                  keep_data_msg);
ar_result_t gen_cntr_flush_output_data_queue(gen_cntr_t *             me_ptr,
                                             gen_cntr_ext_out_port_t *ext_out_port_ptr,
                                             bool_t                   is_client_cmd);
ar_result_t gen_cntr_allocate_wait_mask_arr(gen_cntr_t *me_ptr);

/** ------------------------------------------- (ADSP)PM- ---------------------------------------------------------*/
ar_result_t gen_cntr_update_cntr_kpps_bw(gen_cntr_t *me_ptr, bool_t force_aggregate);
ar_result_t gen_cntr_aggregate_kpps_bw(void *cu_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr);
ar_result_t gen_cntr_perf_vote(gen_cntr_t                 *me_ptr,
                               posal_thread_prio_t         original_prio,
                               gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                               cu_event_flags_t           *fwk_event_flag_ptr);
ar_result_t gen_cntr_initiate_duty_cycle_island_entry(cu_base_t *me_ptr);
ar_result_t gen_cntr_initiate_duty_cycle_island_exit(cu_base_t *me_ptr);
ar_result_t gen_cntr_dcm_topo_set_param(void *cu_ptr);
/** ------------------------------------------- data msg -----------------------------------------------------------*/
ar_result_t gen_cntr_input_media_format_received(void *                                  ctx_ptr,
                                                 gu_ext_in_port_t *                      gu_ext_in_port_ptr,
                                                 topo_media_fmt_t *                      media_fmt_ptr,
                                                 cu_ext_in_port_upstream_frame_length_t *upstream_frame_len_ptr,
                                                 bool_t                                  is_data_path);
ar_result_t gen_cntr_process_pending_input_data_cmd(gen_cntr_t *me_ptr);
/** ------------------------------------------- topo events ------------------------------------------------------*/
ar_result_t gen_cntr_raise_data_to_dsp_service_event(gen_topo_module_t *module_context_ptr,
                                                     capi_event_info_t *event_info_ptr);
ar_result_t gen_cntr_raise_data_to_dsp_service_event_within_island(gen_topo_module_t *module_context_ptr,
                                                                   capi_event_info_t *event_info_ptr,
                                                                   bool_t *           handled_event_within_island);

ar_result_t gen_cntr_raise_data_to_dsp_service_event_non_island(gen_topo_module_t *module_context_ptr,
                                                                capi_event_info_t *event_info_ptr);

ar_result_t gen_cntr_raise_data_from_dsp_service_event(gen_topo_module_t *module_ptr,
                                                       capi_event_info_t *event_info_ptr);
ar_result_t gen_cntr_event_data_to_dsp_client_v2_topo_cb(gen_topo_module_t *module_ptr,
                                                         capi_event_info_t *event_info_ptr);
ar_result_t gen_cntr_handle_capi_event(gen_topo_module_t *module_ptr,
                                       capi_event_id_t    event_id,
                                       capi_event_info_t *event_info_ptr);

ar_result_t gen_cntr_handle_algo_delay_change_event(gen_topo_module_t *module_ptr);
/** ------------------------------------------- data         -----------------------------------------------------*/
void gen_cntr_wait_for_trigger(gen_cntr_t *me_ptr);
bool_t gen_cntr_wait_for_any_ext_trigger(gen_cntr_t *me_ptr, bool_t called_from_process_context, bool_t is_entry);
ar_result_t gen_cntr_input_dataQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t gen_cntr_output_bufQ_trigger(cu_base_t *base_ptr, uint32_t channel_bit_index);
ar_result_t gen_cntr_data_process_frames(gen_cntr_t *me_ptr);
ar_result_t gen_cntr_setup_internal_input_port_and_preprocess(gen_cntr_t *             me_ptr,
                                                              gen_cntr_ext_in_port_t * ext_in_port_ptr,
                                                              gen_topo_process_info_t *process_info_ptr);
ar_result_t gen_cntr_get_input_data_cmd(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_free_input_data_cmd(gen_cntr_t *            me_ptr,
                                         gen_cntr_ext_in_port_t *ext_in_port_ptr,
                                         ar_result_t             status,
                                         bool_t                  is_flush);
bool_t gen_cntr_ext_in_port_has_flushing_eos_dfg(gen_cntr_ext_in_port_t *ext_in_port_ptr);
gen_topo_data_need_t gen_cntr_ext_in_port_needs_data_buffer(gu_ext_in_port_t *gu_ext_in_port_ptr);
ar_result_t gen_cntr_check_process_input_media_fmt_data_cmd(gen_cntr_t *            me_ptr,
                                                            gen_cntr_ext_in_port_t *ext_in_port_ptr);
/** ------------------------------------------- Signal Trigger -----------------------------------------------------*/
ar_result_t gen_cntr_signal_trigger(cu_base_t *cu_ptr, uint32_t channel_bit_index);
ar_result_t gen_cntr_st_prepare_output_buffers_per_ext_out_port(gen_cntr_t *             me_ptr,
                                                                gen_cntr_ext_out_port_t *ext_out_port_ptr);
void gen_cntr_st_check_print_overrun(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_out_port_ptr);
void gen_cntr_st_underrun(gen_cntr_t *            me_ptr,
                          gen_cntr_ext_in_port_t *ext_in_port_ptr,
                          uint32_t                bytes_required_per_buf);
bool_t gen_cntr_st_need_to_poll_for_input_data(gen_cntr_ext_in_port_t *ext_in_port_ptr);

/** ------------------------------------------- cmd          -----------------------------------------------------*/
ar_result_t gen_cntr_gpr_cmd(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_open(cu_base_t *me_ptr);
ar_result_t gen_cntr_set_get_cfg(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_prepare(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_start(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_suspend(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_stop(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_flush(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_close(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_connect(cu_base_t *me_ptr);
ar_result_t gen_cntr_graph_disconnect(cu_base_t *me_ptr);
ar_result_t gen_cntr_destroy(cu_base_t *base_ptr, void *temp);
ar_result_t gen_cntr_destroy_container(cu_base_t *me_ptr);
ar_result_t gen_cntr_ctrl_path_media_fmt_cmd(cu_base_t *me_ptr);
ar_result_t gen_cntr_cmd_icb_info_from_downstream(cu_base_t *base_ptr);
ar_result_t gen_cntr_handle_ctrl_port_trigger_cmd(cu_base_t *base_ptr);
ar_result_t gen_cntr_handle_rest_of_set_cfgs_in_graph_open(cu_base_t *base_ptr, void *ctx_ptr);
void        gen_cntr_handle_failure_at_graph_open(gen_cntr_t *me_ptr, ar_result_t result);
ar_result_t gen_cntr_handle_peer_port_property_update_cmd(cu_base_t *me_ptr);
ar_result_t gen_cntr_handle_upstream_stop_cmd(cu_base_t *base_ptr);
ar_result_t gen_cntr_prepare_to_launch_thread(gen_cntr_t *         me_ptr,
                                              posal_thread_prio_t *priority,
                                              char *               thread_name,
                                              uint32_t             name_length);
ar_result_t gen_cntr_operate_on_ext_out_port(void *              base_ptr,
                                             uint32_t            sg_ops,
                                             gu_ext_out_port_t **ext_out_port_pptr,
                                             bool_t              is_self_sg);
ar_result_t gen_cntr_operate_on_ext_in_port(void *             base_ptr,
                                            uint32_t           sg_ops,
                                            gu_ext_in_port_t **ext_in_port_pptr,
                                            bool_t             is_self_sg);
ar_result_t gen_cntr_post_operate_on_ext_in_port(void *                     base_ptr,
                                                 uint32_t                   sg_ops,
                                                 gu_ext_in_port_t **        ext_in_port_pptr,
                                                 spf_cntr_sub_graph_list_t *spf_sg_list_ptr);
ar_result_t gen_cntr_operate_on_subgraph(void *                     base_ptr,
                                         uint32_t                   sg_ops,
                                         topo_sg_state_t            sg_state,
                                         gu_sg_t *                  sg_ptr,
                                         spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t gen_cntr_operate_on_subgraph_async(void                      *base_ptr,
                                               uint32_t                   sg_ops,
                                               topo_sg_state_t            sg_state,
                                               gu_sg_t                   *gu_sg_ptr,
                                               spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t gen_cntr_post_operate_on_subgraph(void *                     base_ptr,
                                              uint32_t                   sg_ops,
                                              topo_sg_state_t            sg_state,
                                              gu_sg_t *                  gu_sg_ptr,
                                              spf_cntr_sub_graph_list_t *spf_sg_list_ptr);

ar_result_t gen_cntr_set_get_cfg_util(cu_base_t                         *base_ptr,
                                      void                              *mod_ptr,
                                      uint32_t                           pid,
                                      int8_t                            *param_payload_ptr,
                                      uint32_t                          *param_size_ptr,
                                      uint32_t                          *error_code_ptr,
                                      bool_t                             is_set_cfg,
                                      bool_t                             is_deregister,
                                      spf_cfg_data_type_t                cfg_type,
                                      cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr);

ar_result_t gen_cntr_register_events_utils(cu_base_t *       base_ptr,
                                           gu_module_t *     gu_module_ptr,
                                           topo_reg_event_t *reg_event_payload_ptr,
                                           bool_t            is_register,
                                           bool_t *          capi_supports_v1_event_ptr);
ar_result_t gen_cntr_cache_set_event_prop(gen_cntr_t *       me_ptr,
                                          gen_cntr_module_t *module_ptr,
                                          topo_reg_event_t * event_cfg_payload_ptr,
                                          bool_t             is_register);

bool_t gen_cntr_is_signal_triggered(gen_cntr_t *me_ptr);

ar_result_t gen_cntr_apply_downgraded_state_on_output_port(cu_base_t *       cu_ptr,
                                                           gu_output_port_t *out_port_ptr,
                                                           topo_port_state_t downgraded_state);
ar_result_t gen_cntr_apply_downgraded_state_on_input_port(cu_base_t *       cu_ptr,
                                                          gu_input_port_t * in_port_ptr,
                                                          topo_port_state_t downgraded_state);

ar_result_t gen_cntr_set_propagated_prop_on_ext_output(gen_topo_t *              topo_ptr,
                                                       gu_ext_out_port_t *       ext_out_port_ptr,
                                                       topo_port_property_type_t prop_type,
                                                       void *                    payload_ptr);

ar_result_t gen_cntr_set_propagated_prop_on_ext_input(gen_topo_t *              topo_ptr,
                                                      gu_ext_in_port_t *        gu_in_port_ptr,
                                                      topo_port_property_type_t prop_type,
                                                      void *                    payload_ptr);
ar_result_t gen_cntr_reset_downstream_of_stm_upon_stop_and_send_eos(gen_cntr_t *       me_ptr,
                                                                    gen_topo_module_t *stm_module_ptr,
                                                                    gen_topo_module_t *curr_module_ptr,
                                                                    uint32_t *         recurse_depth_ptr);

ar_result_t gen_cntr_check_insert_missing_eos_on_next_module(gen_topo_t *topo_ptr, gen_topo_input_port_t *in_port_ptr);
/** ------------------------------------------- buffer utils    -----------------------------------------------------*/
ar_result_t gen_cntr_handle_port_data_thresh_change(void *me_ptr); // also handles create buf
ar_result_t gen_cntr_ext_out_port_recreate_bufs(void *base_ptr, gu_ext_out_port_t *gu_out_port_ptr);
ar_result_t gen_cntr_create_ext_out_bufs(gen_cntr_t *             me_ptr,
                                         gen_cntr_ext_out_port_t *ext_port_ptr,
                                         uint32_t                 num_out_bufs);
void gen_cntr_destroy_ext_buffers(gen_cntr_t *me_ptr, gen_cntr_ext_out_port_t *ext_port_ptr, uint32_t num_bufs_to_keep);

ar_result_t gen_cntr_init_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);

ar_result_t gen_cntr_deinit_ext_in_port(void *base_ptr, gu_ext_in_port_t *gu_ext_port_ptr);

ar_result_t gen_cntr_init_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);

ar_result_t gen_cntr_deinit_ext_out_port(void *base_ptr, gu_ext_out_port_t *gu_ext_port_ptr);

ar_result_t gen_cntr_update_icb_info(gen_topo_t *topo_ptr);
/** ------------------------------------------- timestamp    ---------------------------------------------------------*/
ar_result_t gen_cntr_copy_timestamp_from_input(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);

/* ------------------------------------------ FWK Extns --------------------------------------------------------------*/
ar_result_t gen_cntr_handle_fwk_extn_pre_subgraph_op(gen_cntr_t *      me_ptr,
                                                     uint32_t          sg_ops,
                                                     gu_module_list_t *module_list_ptr);
ar_result_t gen_cntr_handle_fwk_extn_post_subgraph_op(gen_cntr_t *      me_ptr,
                                                      uint32_t          sg_ops,
                                                      gu_module_list_t *module_list_ptr);
gen_topo_module_t *gen_cntr_get_stm_module(gen_cntr_t *me_ptr);
ar_result_t gen_cntr_create_module(gen_topo_t *           topo_ptr,
                                   gen_topo_module_t *    module_ptr,
                                   gen_topo_graph_init_t *graph_init_data_ptr);
ar_result_t gen_cntr_destroy_module(gen_topo_t *       topo_ptr,
                                    gen_topo_module_t *module_ptr,
                                    bool_t             reset_capi_dependent_dont_destroy);
ar_result_t gen_cntr_capi_set_fwk_extn_proc_dur(gen_cntr_t *me_ptr, uint32_t cont_proc_dur_us);

ar_result_t gen_cntr_fwk_extn_handle_at_stop(gen_cntr_t *me_ptr, gu_module_list_t *module_list_ptr);
ar_result_t gen_cntr_fwk_extn_handle_at_start(gen_cntr_t *me_ptr, gu_module_list_t *module_list_ptr);

/** ------------------------------------------- data flow state -----------------------------------------------------*/
ar_result_t gen_cntr_handle_ext_in_data_flow_begin(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_handle_ext_in_data_flow_end(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);
ar_result_t gen_cntr_handle_ext_in_data_flow_preflow(gen_cntr_t *me_ptr, gen_cntr_ext_in_port_t *ext_in_port_ptr);

ar_result_t gen_cntr_aggregate_hw_acc_proc_delay(void *cu_ptr, uint32_t *hw_acc_delay_ptr);

void gen_ctr_exit_island_temporarily(void *cu_ptr);

ar_result_t gen_cntr_vote_against_island_topo(gen_topo_t *topo_ptr);
ar_result_t gen_cntr_vote_against_island(void *cu_ptr);
ar_result_t gen_cntr_update_island_vote(gen_cntr_t *me_ptr, posal_pm_island_vote_t fwk_island_vote);
ar_result_t gen_cntr_check_and_vote_for_island_in_data_path_(gen_cntr_t *me_ptr);

/** ------------------ **/
uint32_t gen_cntr_assign_ext_in_buf_for_int_port(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t gen_cntr_aggregate_ext_in_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t gen_cntr_aggregate_ext_out_port_delay_topo_cb(gen_topo_t *topo_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);
uint32_t gen_cntr_get_additional_ext_in_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_in_port_t *gu_ext_in_port_ptr);
uint32_t gen_cntr_get_additional_ext_out_port_delay_cu_cb(cu_base_t *base_ptr, gu_ext_out_port_t *gu_ext_out_port_ptr);

ar_result_t gen_cntr_notify_timestamp_discontinuity_event_cb(gen_topo_t *topo_ptr,
                                                             bool_t      ts_valid,
                                                             int64_t     timestamp_disc_us,
                                                             uint32_t    path_index);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef GEN_CNTR_UTIL_H
