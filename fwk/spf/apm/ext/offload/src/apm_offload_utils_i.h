#ifndef _APM_OFFLOAD_UTILS_I_H__
#define _APM_OFFLOAD_UTILS_I_H__

/**
 * \file apm_offload_utils_i.h
 *
 * \brief
 *     This file contains function declaration for APM utilities for offload handling
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/
/*------------------------------------------------------------------------------
 *  Constants/Macros
 *----------------------------------------------------------------------------*/

/****************************************************************************
 * Structure Definitions                                                    *
 ****************************************************************************/

/** Satellite container information for offload container */
typedef struct apm_satellite_cont_node_info_t apm_satellite_cont_node_info_t;

struct apm_satellite_cont_node_info_t
{
   uint32_t satellite_cont_id;
   /**< Satellite Container ID */

   uint32_t parent_cont_id;
   /**< OLC Container ID in master process domain*/
};

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

/**
  Creates the Heap Manager and maintains a record of the
  Master Handle with the associated heap details.

  @datatypes

  @param[in] mem_map_client   Client who has registered to memorymap.
  @param[in] master_handle    Memory Map Handle of the Master DSP.
  @param[in] mem_size         Size of the memory loaned to the master.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_master_memorymap_register(uint32_t mem_map_client,
                                                  uint32_t master_handle,
                                                  uint32_t mem_size,
                                                  uint32_t heap_mgr_type);

/**
  Deletes the Heap Manager and destroys the record of the
  Master Handle and the associated heap.

  @datatypes

  @param[in] master_handle    Memory Map Handle of the Master DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_master_memorymap_check_deregister(uint32_t master_handle);

/**
  Registers the loaned shared memory region info in the offload memory manager driver.
  Mainly creates a heap for the loaned memory and caches the heap ID for offloading
  use case purposes.

  @datatypes

  @param[in] mem_map_client   Client who has registered to memorymap.
  @param[in] unique_shm_id    Unqiue shared memory ID set by the client for the mapped memory.
  @param[in] mem_map_handle   Memory Map Handle of the mapped memory.
  @param[in] mem_size         Size of the memory loaned to the master.
  @param[in] heap_mgr_type    Default/Safe heap memory type.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the memory should be mapped.
*/
ar_result_t apm_offload_memorymap_register_loaned_memory(uint32_t     mem_map_client,
                                                         uint32_t     unique_shm_id,
                                                         uint32_t     mem_size,
                                                         posal_heap_t heap_mgr_type);

/**
  Deregisters info related to the loaned shared memory. And also destroys the heap
  created for the loaned region. If the shared memory region was not mapped as not
  loaned, this function return AR_EOK.

  @datatypes

  @param[in] unique_shm_id    Unqiue shared memory ID set by the client for the mapped memory.
  @param[in] mem_map_client   Client who has registered to memorymap.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the memory should be mapped.
*/
ar_result_t apm_offload_memorymap_check_deregister_loaned_memory(uint32_t unique_shm_id);

/**
  Sets the list of peer proc domains that have access to the given loaned shared memory region. If the shared memory
  region was not mapped as not loaned, this function return AR_EOK.

  @datatypes

  @param[in] unique_shm_id          Unqiue shared memory ID set by the client for the mapped memory.
  @param[in] num_peer_proc_domains  Num peer proc domains that are mapped to this region.
  @param[in] peer_proc_domain_list  List of the peer proc domain IDs that have mapping to this region.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the memory should be mapped.
*/
ar_result_t apm_offload_memorymap_update_peer_domains_access_info(uint32_t unique_shm_id,
                                                                  uint32_t num_peer_proc_domains,
                                                                  uint32_t peer_proc_domain_list[]);

#ifdef __cplusplus
}
#endif /*__cplusplus*/
#endif /* _APM_OFFLOAD_UTILS_I_H__ */
