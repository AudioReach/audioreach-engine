/**
 * \file sprm.h
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

#ifndef SGM_CMN_H
#define SGM_CMN_H

#include "olc_cmn_utils.h"
#include "container_utils.h"
#include "olc_driver.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
OLC Structure Definitions
========================================================================== */
/* Maximum number of Input and output ports */
#define SPDM_MAX_IO_PORTS 8

// defines the dynamic tken start value
#define DYNAMIC_TOKEN_START_VAL 0x1000000

// Structure for shared memory handle for every allocation from the mdf loaned memory pool
typedef struct sgm_shmem_handle_t
{
   apm_offload_ret_info_t mem_attr;
   /*  memory attributes : master & satellite mem handle, memory offset */
   uint32_t shm_alloc_size;
   /* shared memory size */
   void *shm_mem_ptr;
   /* virtual address pointer */
} sgm_shmem_handle_t;

/* Structure for core identify info for the OLC container */
typedef struct spgm_id_info_t
{
   uint32_t log_id;
   /* log id, shared by the OLC during sgm_init */
   uint32_t cont_id;
   /* container id, shared by the OLC during sgm_init */
   uint32_t master_pd;
   /* master process domain */
   uint32_t sat_pd;
   /* Satellite domain with which OLC is communicating */
} spgm_id_info_t;

typedef struct spgm_info_t spgm_info_t;

/* =======================================================================
   SPRM Function Declarations
========================================================================== */

/**--------------------------------- shared memory utils ----------------------------------*/
ar_result_t sgm_shmem_alloc(uint32_t shmem_size, uint32_t satellite_proc_domain, sgm_shmem_handle_t *shmem);
ar_result_t sgm_shmem_free(sgm_shmem_handle_t *shmem);

/**--------------------------------- IPC communication utils ----------------------------------*/
ar_result_t sgm_get_unique_token(spgm_info_t *spgm_info, uint32_t *unique_token_ptr);
ar_result_t sgm_ipc_send_data_pkt(spgm_info_t *spgm_info);
ar_result_t sgm_ipc_send_command_to_dst(spgm_info_t *spgm_info, uint32_t dst_port);
ar_result_t sgm_ipc_send_command_to_dst_with_token(spgm_info_t *spgm_info, uint32_t dst_port, uint32_t token);
ar_result_t sgm_ipc_send_command(spgm_info_t *spgm_info);
ar_result_t sgm_util_remove_node_from_list(spgm_info_t *     spgm_info_ptr,
                                           spf_list_node_t **list_head_pptr,
                                           void *            list_node_ptr,
                                           uint32_t *        node_cntr_ptr);
ar_result_t sgm_util_add_node_to_list(spgm_info_t *     spgm_info_ptr,
                                      spf_list_node_t **list_head_pptr,
                                      void *            list_node_ptr,
                                      uint32_t *        node_cntr_ptr);

uint32_t sgm_get_src_port_id(spgm_info_t *spgm_info);

// todo:VB:make generic
static inline uint32_t sgm_get_dst_port_id(spgm_info_t *spgm_info)
{
   uint32_t dst_port_id = (uint8_t)APM_MODULE_INSTANCE_ID;
   return dst_port_id;
}

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_DRIVER_H
