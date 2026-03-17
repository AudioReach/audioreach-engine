/**
 * \file sprm.h
 *  
 * \brief
 *  
 *     header file for driver functionality of OLC
 *  
 * 
 * \copyright
 * \brief Header file for driver functionality of OLC
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
#endif // __cplusplus

/* =======================================================================
   OLC Structure Definitions
   ======================================================================= */

/* Maximum number of Input and output ports */
#define SPDM_MAX_IO_PORTS 8

/* Defines the dynamic token start value */
#define DYNAMIC_TOKEN_START_VAL 0x1000000

/**
 * \brief Structure for shared memory handle for every allocation from the MDF loaned memory pool
 */
typedef struct sgm_shmem_handle_t
{
   apm_offload_ret_info_t mem_attr;       /**< Memory attributes: master & satellite mem handle, memory offset */
   uint32_t               shm_alloc_size; /**< Shared memory size */
   void *                 shm_mem_ptr;    /**< Virtual address pointer */
} sgm_shmem_handle_t;

/**
 * \brief Structure for core identify info for the OLC container
 */
typedef struct spgm_id_info_t
{
   uint32_t log_id;    /**< Log ID, shared by the OLC during sgm_init */
   uint32_t cont_id;   /**< Container ID, shared by the OLC during sgm_init */
   uint32_t master_pd; /**< Master process domain */
   uint32_t sat_pd;    /**< Satellite domain with which OLC is communicating */
} spgm_id_info_t;

typedef struct spgm_info_t spgm_info_t;

/* =======================================================================
   SPRM Function Declarations
========================================================================== */

/**--------------------------------- shared memory utils ----------------------------------*/
/**
 * \brief Allocates shared memory
 * \param[in] shmem_size Size of the shared memory
 * \param[in] satellite_proc_domain Satellite process domain
 * \param[out] shmem Pointer to the shared memory handle
 * \return Result of the operation
 */
ar_result_t sgm_shmem_alloc(uint32_t shmem_size, uint32_t satellite_proc_domain, sgm_shmem_handle_t *shmem);

/**
 * \brief Frees shared memory
 * \param[in,out] shmem Pointer to the shared memory handle
 * \return Result of the operation
 */
ar_result_t sgm_shmem_free(sgm_shmem_handle_t *shmem);

/**--------------------------------- IPC communication utils ----------------------------------*/
/**
 * \brief Generates a unique token
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \param[out] unique_token_ptr Pointer to store the unique token
 * \return Result of the operation
 */
ar_result_t sgm_get_unique_token(spgm_info_t *spgm_info, uint32_t *unique_token_ptr);

/**
 * \brief Sends IPC data packet
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \return Result of the operation
 */
ar_result_t sgm_ipc_send_data_pkt(spgm_info_t *spgm_info);

/**
 * \brief Sends IPC command to destination port
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \param[in] dst_port Destination port
 * \return Result of the operation
 */
ar_result_t sgm_ipc_send_command_to_dst(spgm_info_t *spgm_info, uint32_t dst_port);

/**
 * \brief Sends IPC command to destination port with token
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \param[in] dst_port Destination port
 * \param[in] token Token for the command
 * \return Result of the operation
 */
ar_result_t sgm_ipc_send_command_to_dst_with_token(spgm_info_t *spgm_info, uint32_t dst_port, uint32_t token);

/**
 * \brief Sends IPC command
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \return Result of the operation
 */
ar_result_t sgm_ipc_send_command(spgm_info_t *spgm_info);

/**
 * \brief Removes node from list
 * \param[in] spgm_info_ptr Pointer to the SPGM info structure
 * \param[in,out] list_head_pptr Pointer to the list head
 * \param[in] list_node_ptr Pointer to the list node
 * \param[in,out] node_cntr_ptr Pointer to the node counter
 * \return Result of the operation
 */
ar_result_t sgm_util_remove_node_from_list(spgm_info_t *     spgm_info_ptr,
                                           spf_list_node_t **list_head_pptr,
                                           void *            list_node_ptr,
                                           uint32_t *        node_cntr_ptr);

/**
 * \brief Adds node to list
 * \param[in] spgm_info_ptr Pointer to the SPGM info structure
 * \param[in,out] list_head_pptr Pointer to the list head
 * \param[in] list_node_ptr Pointer to the list node
 * \param[in,out] node_cntr_ptr Pointer to the node counter
 * \return Result of the operation
 */
ar_result_t sgm_util_add_node_to_list(spgm_info_t *     spgm_info_ptr,
                                      spf_list_node_t **list_head_pptr,
                                      void *            list_node_ptr,
                                      uint32_t *        node_cntr_ptr);

/**
 * \brief Gets source port ID
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \return Source port ID
 */
uint32_t sgm_get_src_port_id(spgm_info_t *spgm_info);

/**
 * \brief Gets destination port ID
 * \param[in] spgm_info Pointer to the SPGM info structure
 * \return Destination port ID
 */
static inline uint32_t sgm_get_dst_port_id(spgm_info_t *spgm_info)
{
   uint32_t dst_port_id = (uint8_t)APM_MODULE_INSTANCE_ID;
   return dst_port_id;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // SGM_CMN_H
