#ifndef _APM_OFFLOAD_UTILS_H_
#define _APM_OFFLOAD_UTILS_H_

/**
 * \file apm_offload_utils.h
 *
 * \brief
 *     This file declares utility functions to manage shared memory between
 *     the processors in the Multi DSP Framework, including
 *     physical to virtual address mapping, etc.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/

#include "apm_i.h"
#include "apm_offload_memmap_utils.h"
#include "offload_apm_api.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#define APM_OFFLOAD_MEM_MAP_DBG

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/
typedef struct apm_offload_utils_vtable
{
   ar_result_t (*apm_offload_shmem_cmd_handler_fptr)(apm_t *apm_info_ptr, spf_msg_t *msg_ptr);

   ar_result_t (*apm_offload_basic_rsp_handler_fptr)(apm_t *         apm_info_ptr,
                                                     apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                     gpr_packet_t *  gpr_pkt_ptr);

   ar_result_t (*apm_offload_master_memorymap_register_fptr)(uint32_t mem_map_client,
                                                             uint32_t master_handle,
                                                             uint32_t mem_size,
                                                             uint32_t heap_mgr_type);

   ar_result_t (*apm_offload_master_memorymap_check_deregister_fptr)(uint32_t master_handle);

   bool_t (*apm_db_get_sat_contaniners_parent_cont_id_fptr)(spf_list_node_t *cont_info_ptr,
                                                            uint32_t         sat_cont_id,
                                                            uint32_t *       parent_cont_id);

   ar_result_t (*apm_check_and_cache_satellite_container_config_fptr)(apm_t *              apm_info_ptr,
                                                                      apm_container_cfg_t *curr_cont_cfg_ptr,
                                                                      bool_t *             is_cont_offloaded);

   ar_result_t (*apm_clear_cont_satellite_cont_list_fptr)(apm_t *apm_info_ptr, apm_container_t *container_node_ptr);

   ar_result_t (*apm_check_alloc_add_to_peer_domain_ctrl_list_fptr)(spf_list_node_t **           list_head_pptr,
                                                                    apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                                    uint32_t *                   node_cntr_ptr);

   ar_result_t (*apm_parse_imcl_peer_domain_info_list_fptr)(apm_t *  apm_info_ptr,
                                                            uint8_t *mod_pid_payload_ptr,
                                                            uint32_t payload_size);

   ar_result_t (*apm_offload_get_ctrl_link_remote_peer_info_fptr)(apm_module_ctrl_link_cfg_t * curr_ctrl_link_cfg_ptr,
                                                                  uint8_t *                    sat_prv_cfg_ptr,
                                                                  apm_module_t **              module_node_ptr_list,
                                                                  apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                                  uint32_t *                   local_peer_idx_ptr);

   ar_result_t (*apm_search_in_sat_prv_peer_cfg_for_miid_fptr)(uint8_t * sat_prv_cfg_ptr,
                                                               uint32_t  miid,
                                                               uint32_t *peer_domain_id_ptr);

   ar_result_t (*apm_offload_handle_pd_info_fptr)(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

   ar_result_t (*apm_offload_send_master_pd_info_fptr)(apm_t *apm_info_ptr, uint32_t sat_proc_domain);

   ar_result_t (*apm_send_close_all_to_sat_fptr)(apm_t *apm_info_ptr);

   ar_result_t (*apm_offload_sat_cleanup_fptr)(uint32_t sat_domain_id);

   bool_t (*apm_offload_is_master_pid_fptr)();

   ar_result_t (*apm_debug_info_cfg_hdlr_fptr)(apm_t *apm_info_ptr);

   ar_result_t (*apm_offload_mem_mgr_reset_fptr)(void);

   ar_result_t (*apm_offload_memorymap_register_loaned_memory_fptr)(uint32_t mem_map_client,
                                                                    uint32_t unique_shm_id,
                                                                    uint32_t mem_size,
                                                                    posal_heap_t heap_mgr_type);

   ar_result_t (*apm_offload_memorymap_check_deregister_loaned_memory_fptr)(uint32_t unique_shm_id);

   ar_result_t (*apm_offload_memorymap_update_peer_domains_access_info_fptr)(uint32_t unique_shm_id,
                                                                             uint32_t num_peer_proc_domains,
                                                                             uint32_t peer_proc_domain_list[]);

  ar_result_t (*apm_offload_handle_scale_factor_info_fptr)(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

} apm_offload_utils_vtable_t;

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Function Declarations and Documentation
 * -------------------------------------------------------------------------*/
ar_result_t apm_offload_utils_init(apm_t *apm_info_ptr);

ar_result_t apm_offload_utils_deinit();

ar_result_t apm_offload_utils_handle_svc_status(apm_t *apm_info_ptr, void *status_ptr);

uint32_t apm_offload_get_sat_domain_list_index(uint32_t sat_domain_id);

ar_result_t apm_offload_handle_scale_factor_info(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _APM_OFFLOAD_UTILS_H_
