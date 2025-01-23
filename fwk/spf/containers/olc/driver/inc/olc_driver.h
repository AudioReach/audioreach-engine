/**
 * \file olc_driver.h
 * \brief
 *     header file for driver functionality of OLC
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OLC_DRIVER_H
#define OLC_DRIVER_H

#include "olc_cmn_utils.h"
#include "container_utils.h"
#include "sprm.h"
#include "sdm.h"
#include "sgm.h"
#include "gen_topo.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
OLC Structure Definitions
========================================================================== */
/**
function pointer table for the command response hanlder functions
 */
typedef struct sgmc_rsp_h_vtable_t
{
   ar_result_t (*graph_open_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_prepare_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_start_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_suspend_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_stop_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_flush_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_close_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_set_get_cfg_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_set_get_cfg_packed_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info, void *packet_ptr);
   ar_result_t (*graph_set_persistent_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_set_persistent_packed_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
   ar_result_t (*graph_event_reg_rsp_h)(cu_base_t *base_ptr, spgm_cmd_rsp_node_t *rsp_info);
} sgmc_rsp_h_vtable_t;

typedef struct spgm_event_info_t
{
   /*These will be the unique keys to identify the
   client information to route the event to when raised
   by a module on a satellite. All the following fields
   will be validated to determine the source/client that
   requested the event notification.*/
   uint32_t module_iid; // port
   uint32_t client_port_id;
   uint32_t client_domain_id;
   uint32_t client_token;
   uint32_t olc_event_reg_token;
} spgm_event_info_t;

typedef struct spgm_info_t
{
   spf_handle_t          spf_handle;
   cu_base_t *           cu_ptr;
   spgm_id_info_t        sgm_id;
   sgm_graph_info_t      gu_graph_info;
   void *                client_open_payload_ptr;
   spgm_cmd_hndl_list_t  cmd_hndl_list;
   spgm_cmd_hndl_node_t *active_cmd_hndl_ptr;
   spf_msg_t             rsp_msg;
   spf_msg_t             event_msg;
   posal_queue_t *       rsp_q_ptr;
   posal_queue_t *       evnt_q_ptr;
   posal_atomic_word_t   token_instance;
   sgmc_rsp_h_vtable_t * cmd_rsp_vtbl;
   sgmc_rsp_h_vtable_t * servreg_error_notify_cmd_rsp_vtbl;
   spgm_cmd_rsp_node_t   rsp_info;
   sdm_process_info_t    process_info;
   spf_list_node_t *     event_reg_list_ptr; // obj ptr of type (spgm_event_info_t)
   sgm_path_delay_info_t path_delay_list;
} spgm_info_t;

ar_result_t olc_create_graph_open_payload(spgm_info_t *             spgm_ptr,
                                          spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                          spf_msg_cmd_graph_open_t *gmc_olc_open_cmd_ptr);

ar_result_t sgm_init(spgm_info_t *spgm_ptr, cu_base_t *cu_ptr, void *cmd_rsp_h_vtbl_ptr, uint32_t queue_offset);

ar_result_t sgm_deinit(spgm_info_t *spgm_ptr);

ar_result_t spgm_cmd_queue_handler(cu_base_t *cu_ptr, spgm_info_t *spgm_ptr,gpr_packet_t *packet_ptr);

ar_result_t sgm_handle_open(spgm_info_t *             spgm_ptr,
                            spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                            uint32_t                  payload_size);

ar_result_t sgm_handle_prepare(spgm_info_t *             spgm_ptr,
                               spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                               uint32_t                  payload_size);

ar_result_t sgm_handle_suspend(spgm_info_t *             spgm_ptr,
                               spf_msg_cmd_graph_mgmt_t *gmc_apm_suspend_cmd_ptr,
                               uint32_t                  payload_size);

ar_result_t sgm_handle_start(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                             uint32_t                  payload_size);

ar_result_t sgm_handle_stop(spgm_info_t *             spgm_ptr,
                            spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                            uint32_t                  payload_size);

ar_result_t sgm_handle_flush(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                             uint32_t                  payload_size);

ar_result_t sgm_handle_close(spgm_info_t *             spgm_ptr,
                             spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                             uint32_t                  payload_size);

ar_result_t sgm_handle_persistent_cfg(spgm_info_t *                     spgm_ptr,
                                      void *                            param_data_ptr,
                                      uint32_t                          payload_size,
                                      bool_t                            is_inband,
                                      bool_t                            is_deregister,
                                      spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr);

ar_result_t sgm_handle_persistent_cfg_v2(spgm_info_t *                     spgm_ptr,
                                         void **                           param_data_pptr,
                                         uint32_t                          num_config,
                                         uint32_t                          payload_size,
                                         bool_t                            is_inband,
                                         bool_t                            is_deregister,
                                         spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr);

ar_result_t sgm_handle_set_get_cfg(spgm_info_t *                     spgm_ptr,
                                   spf_msg_cmd_param_data_cfg_t *    gmc_apm_param_data_cfg_ptr,
                                   uint32_t                          payload_size,
                                   bool_t                            is_set_cfg_msg,
                                   bool_t                            is_inband,
                                   spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr);

ar_result_t sgm_handle_set_get_cfg_packed(spgm_info_t *spgm_ptr,
                                          uint8_t *    set_cfg_payload_ptr,
                                          uint32_t     set_payload_size,
                                          uint32_t     dst_port,
                                          bool_t       is_inband,
                                          uint32_t     opcode);

ar_result_t sgm_handle_persistent_set_get_cfg_packed(spgm_info_t *     spgm_ptr,
                                                     apm_cmd_header_t *in_apm_cmd_header,
                                                     uint32_t          dst_port,
                                                     uint32_t          opcode);

ar_result_t sgm_handle_register_module_events(spgm_info_t *spgm_info_ptr,
                                              uint8_t *    reg_payload_ptr,
                                              uint32_t     reg_payload_size,
                                              uint32_t     client_token,
                                              uint32_t     miid,
                                              uint32_t     client_port_id,
                                              uint32_t     client_domain_id,
                                              bool_t       is_inband,
                                              uint32_t     opcode);

ar_result_t sgm_set_get_cfg_rsp_update(spgm_info_t *                 spgm_ptr,
                                       spf_msg_cmd_param_data_cfg_t *set_get_cfg_payload_ptr,
                                       uint32_t                      token);

ar_result_t sgm_set_get_packed_cfg_rsp_update(spgm_info_t *spgm_ptr,
                                              uint32_t     token,
                                              uint32_t     opcode,
                                              void *       sat_rsp_packet_ptr);

ar_result_t sgm_cntr_set_cfg_rsp_update(spgm_info_t *spgm_ptr, uint32_t token);

ar_result_t sgm_register_satellite_module_with_gpr(spgm_info_t *spgm_ptr, spf_handle_t *spf_handle);

ar_result_t sgm_register_olc_module_with_gpr(spgm_info_t *spgm_ptr);

bool_t sgm_get_cmd_rsp_status(spgm_info_t *spgm_ptr, uint32_t opcode);

ar_result_t sgm_cache_cmd_msg(spgm_info_t *spgm_ptr, uint32_t opcode, spf_msg_t *cmd_msg);

ar_result_t sgm_get_satellite_cont_id(spgm_info_t *spgm_ptr, uint32_t sat_miid, uint32_t *sat_cont_id_ptr);

void sgm_destroy_cmd_handle(spgm_info_t *spgm_ptr, uint32_t opcode, uint32_t token);

ar_result_t spdm_process_data_release_read_buffer(spgm_info_t *spgm_ptr, uint32_t port_index);

ar_result_t spdm_process_data_write(spgm_info_t *           spgm_ptr,
                                    uint32_t                port_index,
                                    sdm_cnt_ext_data_buf_t *input_data_ptr,
                                    bool_t *                is_data_consumed);

ar_result_t sdm_alloc_ipc_data_buffers(spgm_info_t *       spgm_ptr,
                                       uint32_t            new_buf_size,
                                       uint32_t            port_index,
                                       sdm_ipc_data_type_t data_type);

ar_result_t spdm_handle_input_media_format_update(spgm_info_t *spgm_ptr,
                                                  void *       media_format_payload_ptr,
                                                  uint32_t     port_index,
                                                  bool_t       is_data_path);

ar_result_t sgm_create_rd_data_queue(cu_base_t *            cu_ptr,
                                     spgm_info_t *          spgm_ptr,
                                     uint32_t               ext_inport_bitmask,
                                     uint32_t               rc_client_miid,
                                     void*                  dest,
                                     sdm_data_port_info_t **port_ctrl_cfg_ptr);

ar_result_t sgm_create_wr_data_queue(cu_base_t *            cu_ptr,
                                     spgm_info_t *          spgm_ptr,
                                     uint32_t               ext_inport_bitmask,
                                     uint32_t               wr_client_miid,
                                     void*                  dest,
                                     sdm_data_port_info_t **port_ctrl_cfg_ptr);

ar_result_t sgm_destroy_wr_data_port(spgm_info_t *spgm_ptr, uint32_t port_index);
ar_result_t sgm_destroy_rd_data_port(spgm_info_t *spgm_ptr, uint32_t port_index);

ar_result_t sdm_setup_rd_data_port(spgm_info_t *spgm_ptr, uint32_t port_index);
ar_result_t sdm_setup_wr_data_port(spgm_info_t *spgm_ptr, uint32_t port_index);

ar_result_t sgm_recreate_output_buffers(spgm_info_t *spgm_ptr, uint32_t new_max_size, uint32_t port_index);

ar_result_t sgm_send_n_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index, uint32_t num_buf_to_send);

ar_result_t sgm_send_all_read_buffers(spgm_info_t *spgm_ptr, uint32_t port_index);

ar_result_t sgm_flush_write_data_port(spgm_info_t *spgm_ptr,
                                      uint32_t     port_index,
                                      bool_t       is_flush,
                                      bool_t       is_flush_post_processing);

ar_result_t sgm_deregister_satellite_module_with_gpr(spgm_info_t *spgm_ptr, uint32_t sub_graph_id);

ar_result_t sgm_deregister_olc_module_with_gpr(spgm_info_t *spgm_ptr, uint32_t sub_graph_id);

ar_result_t sgm_flush_read_data_port(spgm_info_t *spgm_ptr,
                                     uint32_t     port_index,
                                     bool_t       is_flush,
                                     bool_t       is_flush_post_processing);

ar_result_t spdm_process_send_wr_eos(spgm_info_t *spgm_ptr, module_cmn_md_eos_flags_t *flags, uint32_t port_index);

ar_result_t spgm_handle_event_opfs(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

ar_result_t sdm_handle_peer_port_property_update_cmd(spgm_info_t *spgm_ptr, uint32_t port_index, void *property_ptr);

ar_result_t spdm_process_upstream_stopped(spgm_info_t *spgm_ptr, uint32_t port_index);

ar_result_t spgm_handle_event_upstream_state(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

ar_result_t spgm_handle_event_upstream_peer_port_property(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

ar_result_t spgm_handle_event_downstream_peer_port_property(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

ar_result_t sgm_handle_set_get_path_delay_cfg(spgm_info_t *                     spgm_ptr,
                                              uint8_t *                         apm_path_defn_for_delay_ptr,
                                              uint8_t *                         rsp_payload_ptr,
                                              uint32_t                          rsp_payload_size,
                                              uint32_t                          sec_op_code,
                                              spgm_set_get_cfg_cmd_extn_info_t *cmd_extn_ptr);

ar_result_t sgm_add_cont_id_delay_event_reg_list(spgm_info_t *spgm_ptr, uint32_t sat_cont_id, uint32_t master_path_id);
ar_result_t sgm_destroy_path(spgm_info_t *spgm_ptr, uint32_t master_path_id);
ar_result_t sgm_path_delay_list_destroy(spgm_info_t *spgm_ptr, bool_t deregister_sat_cont);

ar_result_t sgm_servreg_notify_event_handler(cu_base_t *cu_ptr, spgm_info_t *spgm_ptr);
ar_result_t sgm_set_servreg_error_notify_cmd_rsp_fn_handler(spgm_info_t *spgm_ptr,
                                                            void *       servreg_error_notify_rsp_vtbl_ptr);

ar_result_t spgm_handle_event_clone_md(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);
ar_result_t spgm_handle_tracking_md_event(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_DRIVER_H
