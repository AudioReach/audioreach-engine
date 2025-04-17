/**
 * \file apm_proxy_graph_mgmt.c
 * \brief
 *     This file contains APM proxy graph managment utility functions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "apm_proxy_graph_mgmt.h"
#include "apm_proxy_graph_mgmt_api.h"
#include "spf_list_utils.h"
#include "spf_thread_pool.h"
#include "spf_cmn_if.h"
#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "gpr_api_inline.h"
#include "gpr_ids_domains.h"
#include "gpr_private_api.h"

// Thread priority
#define THREAD_PRIO (posal_thread_get_floor_prio(SPF_THREAD_STAT_CNTR_ID))

// Thread stack size
#define STACK_SIZE (2048)


/****************************************************************************
 * Structure Definition                                                     *
 ****************************************************************************/
//structure for the client handle
typedef struct proxy_client_handle
{
    uint32_t usecase_id;
    posal_signal_t wait_signal_ptr;
    posal_channel_t wait_channel_ptr;
} proxy_client_handle_t;

/** TODO: for overlapping subgraph across usecase IDs
typedef struct apm_proxy_graph_mgmt_sg_info
{
    uint32_t sg_id;
    uint32_t microapp_ref_count; //0: graph in default start state, 1: graph is stopped
    bool_t b_disable_control; //0: graph is controlled by proxy client, 1: graph is controlled by HLOS
    uint16_t num_usecases;
    uint32_t usecase_id_list[1];
} apm_proxy_graph_mgmt_usecase_info_t;

Graph goes into stop state when "take control is set" and "Microapps is not active"
Graph goes into start state when either "relase control is set" or "microapp is active"

 */
//structure to store the use case info.
typedef struct apm_proxy_graph_mgmt_usecase_info
{
    uint32_t usecase_id;
    bool_t b_client_stop; //0: graph in default start state, 1: graph is stopped
    bool_t b_disable_control; //0: graph is controlled by proxy client, 1: graph is controlled by HLOS
    uint16_t num_subgraphs;
    uint32_t sg_id_list[1];
} apm_proxy_graph_mgmt_usecase_info_t;

//global structure to store all the use case info.
typedef struct apm_proxy_graph_mgmt
{
    spf_thread_pool_inst_t *thread_pool_ptr; 
    
    spf_list_node_t *usecase_list_ptr;
} apm_proxy_graph_mgmt_t;

//context pointer for the GPR command
typedef struct tp_job_info
{
    spf_thread_pool_job_t job;
    gpr_packet_t *gpr_pkt_ptr;
} tp_job_info_t;

//command header payload (common for start/stop)
typedef struct apm_proxy_graph_mgmt_cmd_payload
{
   apm_cmd_header_t header;
   apm_module_param_data_t param;
} apm_proxy_graph_mgmt_cmd_payload_t;

//Payload for start and stop (followed by apm_proxy_graph_mgmt_cmd_payload_t)
typedef struct apm_proxy_graph_mgmt_state_payload
{
   apm_param_id_sub_graph_list_t sg_list;
   uint32_t sg_id_array[1];
} apm_proxy_graph_mgmt_state_payload_t;


ar_result_t apm_proxy_graph_mgmt_gpr_init();
ar_result_t apm_proxy_graph_mgmt_gpr_deinit();

ar_result_t apm_proxy_graph_mgmt_send_start_stop(uint32_t num_subgraphs, uint32_t* sg_id_array, bool_t is_start);

ar_result_t apm_proxy_graph_mgmt_register_int(void *context_ptr);
ar_result_t apm_proxy_graph_mgmt_stop_int(void *context_ptr);
ar_result_t apm_proxy_graph_mgmt_start_int(void *context_ptr);
ar_result_t apm_proxy_graph_mgmt_get_state_int(void *context_ptr);


ar_result_t apm_proxy_graph_mgmt_usecaes_register(void *context_ptr);
ar_result_t apm_proxy_graph_mgmt_enable_disable_control(void* context_ptr);
