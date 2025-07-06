#ifndef _APM_MULTI_CLIENT_H_
#define _APM_MULTI_CLIENT_H_

/**
 * \file apm_multi_client.h
 *
 * \brief
 *     This file handles apm multi client
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_i.h"

#define DEBUG_MULTI_CLIENT

#define ONE_CLIENT 1

#ifndef SIM

#define CHECK_CLIENT_AND_DOMAIN(client_ptr, packet_curr_client)   (client_ptr->client_domain_id == packet_curr_client->src_domain_id)

#else

#define CHECK_CLIENT_AND_DOMAIN(client_ptr, packet_curr_client)                                                        \
   ((client_ptr->client_port_id == packet_curr_client->src_port) &&                                                    \
    (client_ptr->client_domain_id == packet_curr_client->src_domain_id))

#endif

/**------------------------------------------------------------------------------
 *  Structure Definition
 *----------------------------------------------------------------------------*/

typedef struct apm_multi_client_vtable
{
   ar_result_t (*apm_multi_client_parse_sg_list_fptr)(apm_sub_graph_t *sub_graph_node_ptr,
                                                      apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr,
                                                      uint8_t        **curr_payload_pptr);

   ar_result_t (*apm_multi_client_add_sg_client_context_fptr)(apm_sub_graph_t *sub_graph_node_ptr,
                                                              apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr);

   ar_result_t (*apm_multi_client_check_sg_state_and_update_send_to_seqncr_fptr)(apm_t           *apm_info_ptr,
                                                                                 apm_sub_graph_t *sub_graph_node_ptr,
                                                                                 uint32_t         cmd_opcode,
                                                                                 bool_t          *seq_entry_reqd);

   ar_result_t (*apm_multi_client_parse_mod_list_fptr)(apm_cmd_ctrl_t     *apm_cmd_ctrl_ptr,
                                                       apm_modules_list_t *module_list_ptr,
                                                       uint8_t           **curr_mod_list_payload_ptr,
                                                       bool_t             *mod_list_parse_req);

   ar_result_t (*apm_multi_client_parse_mod_prop_list_fptr)(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                            apm_module_t   *module_node_ptr,
                                                            bool_t         *mod_prop_list_parse_req);

   ar_result_t (*apm_multi_client_cache_connections_fptr)(apm_t                 *apm_info_ptr,
                                                          apm_module_t          *module_node_ptr_list[2],
                                                          apm_module_conn_cfg_t *curr_conn_cfg_ptr,
                                                          bool_t                *conn_skip_req);

   ar_result_t (*apm_multi_client_parse_mod_conn_list_fptr)(apm_t                 *apm_info_ptr,
                                                            apm_module_conn_cfg_t *curr_conn_cfg_ptr,
                                                            uint8_t              **curr_payload_ptr,
                                                            uint32_t              *num_connections,
                                                            bool_t                *conn_parse_req);

   ar_result_t (*apm_multi_client_clear_graph_mgmt_context_fptr)(apm_t           *apm_info_ptr,
                                                                 apm_sub_graph_t *sub_graph_node_ptr);

   ar_result_t (*apm_multi_client_clear_graph_open_cmd_info_fptr)(apm_t *apm_info_ptr);

   ar_result_t (*apm_multi_client_graph_cmd_rsp_hdlr_fptr)(apm_t     *apm_info_ptr,
                                                           spf_msg_t *rsp_msg_ptr,
                                                           uint32_t   cmd_opcode);

   ar_result_t (*apm_multi_client_parse_mod_ctrl_link_cfg_fptr)(apm_t                      *apm_info_ptr,
                                                                apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr,
                                                                uint8_t                   **curr_payload_ptr,
                                                                uint32_t                   *num_ctrl_links,
                                                                bool_t                     *conn_parse_req);

   ar_result_t (*apm_multi_client_cache_ctrl_links_fptr)(apm_t                      *apm_info_ptr,
                                                         apm_module_t               *module_node_ptr_list[2],
                                                         apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr);

   ar_result_t (*apm_multi_client_update_parsed_sg_list_fptr)(uint32_t sub_graph_id, apm_t *apm_info_ptr);

   ar_result_t (*apm_multi_client_update_sg_calibration_state_fptr)(apm_t *apm_info_ptr);

} apm_multi_client_vtable_t;

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

ar_result_t apm_multi_client_init(apm_t *apm_info_ptr);

ar_result_t apm_multi_client_check_duplicate_client(spf_list_node_t *client_list_ptr, gpr_packet_t *packet_curr_client);

ar_result_t apm_multi_client_check_skip_sub_graph(spf_list_node_t *skip_sg_id_list_ptr, uint32_t sub_graph_id);

ar_result_t apm_multi_client_check_sg_state_and_update_send_to_seqncr(apm_t           *apm_info_ptr,
                                                                      apm_sub_graph_t *sub_graph_node_ptr,
                                                                      uint32_t         cmd_opcode,
                                                                      bool_t          *seq_entry_reqd);

ar_result_t apm_multi_client_get_client_node(apm_sub_graph_t       *sub_graph_node_ptr,
                                             gpr_packet_t          *packet_curr_client,
                                             apm_client_context_t **client_node_pptr);

ar_result_t apm_multi_client_aggregate_state(spf_list_node_t       *client_list_ptr,
                                             apm_client_context_t  *client_node_ptr,
                                             apm_sub_graph_state_t *aggregated_state);

ar_result_t apm_multi_client_get_translated_apm_sg_state(uint32_t cmd_opcode, apm_sub_graph_state_t *state_ptr);

ar_result_t apm_multi_client_parse_sg_list(apm_sub_graph_t *sub_graph_node_ptr,
                                           apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr,
                                           uint8_t        **curr_payload_pptr);

ar_result_t apm_multi_client_add_sg_client_context(apm_sub_graph_t *sub_graph_node_ptr,
                                                   apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr);

ar_result_t apm_multi_client_parse_mod_list(apm_cmd_ctrl_t     *apm_cmd_ctrl_ptr,
                                            apm_modules_list_t *module_list_ptr,
                                            uint8_t           **curr_mod_list_payload_ptr,
                                            bool_t             *mod_list_parse_req_ptr);

ar_result_t apm_multi_client_parse_mod_prop_list(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                 apm_module_t   *module_node_ptr,
                                                 bool_t         *mod_prop_list_parse_req);

ar_result_t apm_multi_client_parse_mod_conn_list(apm_t                 *apm_info_ptr,
                                                 apm_module_conn_cfg_t *curr_ctrl_link_cfg_ptr,
                                                 uint8_t              **curr_payload_ptr,
                                                 uint32_t              *num_ctrl_links,
                                                 bool_t                *conn_parse_req);

ar_result_t apm_multi_client_cache_connections(apm_t                 *apm_info_ptr,
                                               apm_module_t          *module_node_ptr_list[2],
                                               apm_module_conn_cfg_t *curr_ctrl_link_cfg_ptr,
                                               bool_t                *conn_skip_req);

ar_result_t apm_multi_client_add_conn_client_context(apm_t *apm_info_ptr, apm_module_data_port_conn_t *conn_cfg_ptr);

ar_result_t apm_multi_client_parse_mod_ctrl_link_cfg(apm_t                      *apm_info_ptr,
                                                     apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr,
                                                     uint8_t                   **curr_payload_ptr,
                                                     uint32_t                   *num_ctrl_links,
                                                     bool_t                     *conn_parse_req);

ar_result_t apm_multi_client_cache_ctrl_links(apm_t                      *apm_info_ptr,
                                              apm_module_t               *module_node_ptr_list[2],
                                              apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr);

ar_result_t apm_multi_client_add_ctrl_link_client_context(apm_t                       *apm_info_ptr,
                                                          apm_module_ctrl_port_conn_t *conn_cfg_ptr);

ar_result_t apm_multi_client_free_sub_graph_client_context(apm_sub_graph_t *sub_graph_node_ptr,
                                                           gpr_packet_t    *packet_curr_client);

ar_result_t apm_multi_client_free_conn(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr);

ar_result_t apm_multi_client_free_conn_client_context(apm_module_data_port_conn_t *conn_cfg_ptr,
                                                      gpr_packet_t                *packet_curr_client);

ar_result_t apm_multi_client_free_ctrl_link(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr);

ar_result_t apm_multi_client_free_ctrl_link_client_context(apm_module_ctrl_port_conn_t *conn_cfg_ptr,
                                                           gpr_packet_t                *packet_curr_client);

ar_result_t apm_multi_client_clear_graph_mgmt_context(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr);

ar_result_t apm_multi_client_clear_graph_open_cmd_info(apm_t *apm_info_ptr);

ar_result_t apm_multi_client_free_all_conn_client_context(apm_t           *apm_info_ptr,
                                                          apm_sub_graph_t *sub_graph_node_ptr,
                                                          gpr_packet_t    *packet_curr_client);

ar_result_t apm_multi_client_free_all_ctrl_link_client_context(apm_t           *apm_info_ptr,
                                                               apm_sub_graph_t *sub_graph_node_ptr,
                                                               gpr_packet_t    *packet_curr_client);

ar_result_t apm_multi_client_graph_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr, uint32_t cmd_opcode);

bool_t apm_check_is_multi_client();

ar_result_t apm_multi_client_update_parsed_sg_list(uint32_t sub_graph_id, apm_t *apm_info_ptr);

ar_result_t apm_multi_client_update_sg_calibration_state(apm_t *apm_info_ptr);

#endif /* _APM_MULTI_CLIENT_H_ */
