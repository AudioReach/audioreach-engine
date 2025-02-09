/**
 * \file sgm_i.h
 * \brief
 *     This file contains internal definitions and declarations for the OLC Satellite Graph Management.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OLC_SGM_I_H
#define OLC_SGM_I_H

#include "olc_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus//

// VB:TODO :: includes are taken from PP. We can check if everything is needed

#include "apm_api.h"
#include "apm_sub_graph_api.h"
#include "apm_container_api.h"
#include "apm_module_api.h"
#include "apm_container_api.h"

#include "container_utils.h"
#include "graph_utils.h"
#include "posal_power_mgr.h"
#include "posal_memorymap.h"

#include "spf_macros.h"

#include "olc_driver.h"
#include "sgm_cmd_parser_utils.h"
#include "offload_path_delay_api.h"

/* =======================================================================
OLC SGM Macros
========================================================================== */
#define SIZE_OF_PTR() (sizeof(void *))
#define ALIGN_8_BYTES(a) ((a + 7) & (0xFFFFFFF8))

// size of Queues
#define SGM_MAX_RSP_Q_ELEMENTS 128
#define SGM_MAX_EVNT_Q_ELEMENTS 128
#define SGM_MAX_DATA_Q_ELEMENTS 32

// bit mask of command response and event Q
#define SGM_RSPQ_BIT_MASK 0x10000000
#define SGM_EVENTQ_BIT_MASK 0x20000000

#define OLC_IPC_MAX_IN_BAND_PAYLOAD_SIZE 8 // Set to value low to make everything out of band

/* =======================================================================
OLC SGM Structure Definitions
========================================================================== */
typedef struct sgm_param_id_offload_graph_path_delay_t
{
   uint32_t                                num_paths;
   apm_offload_graph_path_defn_for_delay_t paths;
} sgm_param_id_offload_graph_path_delay_t;

/* =======================================================================
OLC SGM Function Declarations
========================================================================== */
/**--------------------------- general_utilities --------------------*/
ar_result_t sgm_util_add_node_to_list(spgm_info_t *     spgm_ptr,
                                      spf_list_node_t **list_head_pptr,
                                      void *            list_node_ptr,
                                      uint32_t *        node_cntr_ptr);
ar_result_t sgm_util_remove_node_from_list(spgm_info_t *     spgm_info_ptr,
                                           spf_list_node_t **list_head_pptr,
                                           void *            list_node_ptr,
                                           uint32_t *        node_cntr_ptr);
bool_t check_if_module_is_in_list(spf_list_node_t *mod_list_ptr,
                                  uint32_t         num_modules_list,
                                  uint32_t         module_instance_id);
ar_result_t add_module_list_to_graph_info(spgm_info_t *       spgm_ptr,
                                          apm_modules_list_t *mod_list_ptr,
                                          uint32_t            host_container_id);
bool_t olc_get_cmd_hndl_node(spf_list_node_t *      cmd_hndl_list_ptr,
                             uint32_t               num_cmd_hndl_list,
                             spgm_cmd_hndl_node_t **cmd_hndl_node_pptr,
                             uint32_t               token);

/**--------------------------- command handling utilities --------------------*/

ar_result_t sgm_free_cmd_resources(spgm_info_t *spgm_ptr);
ar_result_t sgm_alloc_cmd_hndl_resources(spgm_info_t *spgm_ptr,
                                         uint32_t     graph_cmd_payload_size,
                                         bool_t       is_inband,
                                         bool_t       is_persistent);
ar_result_t sgm_create_cmd_hndl_node(spgm_info_t *spgm_ptr);
void sgm_destroy_cmd_handle(spgm_info_t *spgm_ptr, uint32_t opcode, uint32_t token);
ar_result_t sgm_cmd_preprocessing(spgm_info_t *spgm_ptr, uint32_t cmd_opcode, bool_t inband);
void sgm_cmd_postprocessing(spgm_info_t *spgm_ptr);
void sgm_cmd_handling_bail_out(spgm_info_t *spgm_ptr);

ar_result_t sgm_create_graph_mgmt_client_payload(spgm_info_t *             spgm_ptr,
                                                 spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr);
ar_result_t sgm_create_graph_close_client_payload(spgm_info_t *             spgm_ptr,
                                                  spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr);

ar_result_t sgm_create_graph_open_client_payload(spgm_info_t *             spgm_ptr,
                                                 spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                                 uint32_t                  apm_gmc_cmd_payload_size);

ar_result_t sgm_create_set_get_cfg_client_payload(spgm_info_t *                 spgm_ptr,
                                                  spf_msg_cmd_param_data_cfg_t *gmc_apm_param_data_cfg_ptr,
                                                  bool_t                        is_inband);

ar_result_t sgm_create_set_get_cfg_packed_client_payload(spgm_info_t *spgm_ptr,
                                                         uint8_t *    set_cfg_ptr,
                                                         uint32_t     cfg_payload_size,
                                                         bool_t       is_inband);
ar_result_t sgm_create_reg_event_payload(spgm_info_t *spgm_info_ptr,
                                         uint8_t *    reg_payload_ptr,
                                         uint32_t     reg_payload_size,
                                         bool_t       is_inband);
/**--------------------------- response handling utilities --------------------*/

ar_result_t sgm_cmd_rsp_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index);
ar_result_t sgm_get_cache_cmd_msg(spgm_info_t *spgm_ptr,
                                  uint32_t     opcode,
                                  uint32_t     token,
                                  spf_msg_t ** cmd_msg);
ar_result_t sgm_get_active_cmd_hndl(spgm_info_t *          spgm_ptr,
                                    uint32_t               opcode,
                                    uint32_t               token,
                                    spgm_cmd_hndl_node_t **cmd_hndl_node_ptr);

/**--------------------------- event handling utilities --------------------*/

ar_result_t sgm_event_queue_handler(cu_base_t *cu_ptr, uint32_t channel_bit_index);
uint32_t sgm_gpr_callback(gpr_packet_t *packet_ptr, void *callback_data_ptr);

/**--------------------------- path delay event utilities --------------------*/
ar_result_t sgm_update_path_delay_list(spgm_info_t *spgm_ptr,
                                       uint32_t     m_path_id,
                                       uint32_t     sat_path_id,
                                       bool_t       add_to_list);
ar_result_t sgm_register_path_delay_event(spgm_info_t *spgm_ptr, bool_t is_register);
// ar_result_t sgm_add_cont_id_delay_event_reg_list(spgm_info_t *spgm_ptr, uint32_t sat_cont_id);
ar_result_t sgm_create_get_path_delay_client_payload(spgm_info_t *spgm_ptr,
                                                     uint8_t *    apm_path_defn_for_delay_ptr,
                                                     bool_t       is_inband);
ar_result_t spgm_handle_event_get_container_delay(spgm_info_t *spgm_ptr, gpr_packet_t *packet_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_SGM_I_H
