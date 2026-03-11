/**
 * \file sgm.h
 *  
 * \brief
 *  
 *     header file for driver functionality of OLC
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SGM_H
#define SGM_H

#include "sprm.h"
#include "olc_cmn_utils.h"
#include "container_utils.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* Defines the set_get_calibration mode */
typedef enum {
   CFG_FOR_SATELLITE_ONLY          = 0, // Indicates that the configuration is for Satellite modules only
   CFG_FOR_CONTAINER_ONLY          = 1, // Indicates that the configuration is for OLC only
   CFG_FOR_SATELLITE_AND_CONTAINER = 2, // Indicates that the configuration is for both satellite and container
} sgm_cfg_destn_type_t;

/* =======================================================================
OLC Structure Definitions
========================================================================== */

/**< specifies the mapping information of the master path ID and the satellite path ID */
typedef struct sgm_path_id_mapping_info_t
{
   uint32_t master_path_id;

   uint32_t satellite_path_id;
} sgm_path_id_mapping_info_t;

/**< specifies if the satellite container is registered with the delay event */
typedef struct sgm_sat_cont_delay_event_reg_t
{
   uint32_t sat_cont_id;

   uint32_t is_event_registered; // if the value is zero (FALSE), it means registration is pending

   uint32_t num_path_ref;

   spf_list_node_t *master_path_id_list_ptr;
   /**< specifies the list of the path id's where this satellite container is involved
    *   type : int32_t */

} sgm_sat_cont_delay_event_reg_t;

typedef struct sgm_path_delay_info_t
{
   uint32_t num_delay_paths;

   spf_list_node_t *path_id_mapping_info_ptr;
   /**< specifies the mapping information of the master path ID and the satellite path ID
    *   type : sgm_path_id_mapping_info_t*/

   uint32_t num_sat_cont_registered;

   spf_list_node_t *sat_cont_delay_event_reg_ptr;
   /**< specifies if the satellite container is registered with the delay event
    *   type : sgm_sat_cont_delay_event_reg_t*/

} sgm_path_delay_info_t;

typedef struct spgm_cmd_extn_info_t
{
   void *extn_payload_ptr;
} spgm_cmd_extn_info_t;

typedef struct spgm_cmd_rsp_node_t
{
   uint32_t             rsp_result;
   uint32_t             opcode;
   uint32_t             token;
   spf_msg_t *          cmd_msg;
   spgm_cmd_extn_info_t cmd_extn_info;
   uint32_t             sec_opcode;
} spgm_cmd_rsp_node_t;

typedef struct sgm_module_info_t
{
   uint32_t sub_graph_id;
   /**< Number of sub-graphs being configured */

   uint32_t container_id;
   /**< Number of containers being configured */

   uint32_t module_id;
   /**< Valid ID of the module.

        @values  */

   uint32_t instance_id;
   /**< Instance ID for this module */

   bool_t is_registered_with_gpr;
   /* indicates if the module is registered with GPR */
} sgm_module_info_t;

typedef struct sgm_graph_info_t
{
   uint32_t num_olc_modules;
   /* Number of OLC modules in the container
    * Basically corresponds to the WR and RD client modules
    */

   spf_list_node_t *olc_module_list_ptr;
   /**< OLC container module list
    *   type : sgm_module_info_t*/

   uint32_t num_satellite_modules;
   /* Number of modules in the satellite containers
    */

   spf_list_node_t *satellite_module_list_ptr;
   /**< Satellite Graph module list
    *   type : sgm_module_info_t*/
} sgm_graph_info_t;

typedef struct spgm_set_get_cfg_cmd_extn_info_t
{
   bool_t                        cmd_ack_done;
   uint32_t                      pending_resp_counter;
   uint32_t                      accu_result;
   sgm_cfg_destn_type_t          cfg_destn_type;
   spf_msg_cmd_param_data_cfg_t *sat_cfg_cmd_ptr;
   spf_msg_cmd_param_data_cfg_t *cntr_cfg_cmd_ptr;
} spgm_set_get_cfg_cmd_extn_info_t;

typedef struct spgm_cmd_hndl_node_t
{
   uint32_t             opcode;
   uint32_t             sec_opcode;
   uint32_t             token;
   uint32_t             wait_for_rsp;
   uint16_t             multi_max_resp_cnt;
   uint16_t             multi_rsp_cnt;
   uint32_t             cmd_payload_size;
   uint32_t             rsp_payload_size;
   uint8_t *            rsp_payload_ptr;
   uint8_t *            cmd_payload_ptr;
   bool_t               is_inband;
   bool_t               is_sec_opcode_valid;
   bool_t               is_apm_cmd_rsp;
   spf_msg_t            cmd_msg;
   sgm_shmem_handle_t   shm_info;
   spgm_cmd_extn_info_t cmd_extn_info;
} spgm_cmd_hndl_node_t;

typedef struct spgm_cmd_hndl_list_t
{
   uint32_t num_active_cmd_hndls;

   spf_list_node_t *cmd_hndl_list_ptr;
} spgm_cmd_hndl_list_t;

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef SGM_H
