#ifndef APM_OFFLOAD_MEMMAP_UTILS_H
#define APM_OFFLOAD_MEMMAP_UTILS_H

/**
 * \file apm_offload_memmap_utils.h
 * \brief
 *     This file contains utilities for memory mapping and unmapping of shared
 *     memory, in the Multi-DSP-Framwork (MDF).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "posal.h"
#include "apm_offload_mem.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/* =======================================================================
   Memory Map Function Declarations
========================================================================== */

/**
  Initializes the global structure and creates the mutex
  @param[in] heap_id   Heap id used for malloc.

  @datatypes

  @return
  ar_result_t to indicate success or failure.

*/
ar_result_t apm_offload_global_mem_mgr_init_v1(POSAL_HEAP_ID heap_id);

/**
  Initializes the global structure and destroys the mutex

  @datatypes

  @return
  ar_result_t to indicate success or failure.

*/
ar_result_t apm_offload_global_mem_mgr_deinit_v1();

/**
  Records the satellite Memory Map Handle along with the association to
  the corresponding master record and the satellite domain ID.

  @datatypes

  @param[in] master_handle    Memory Map Handle of the Master DSP.
  @param[in] sat_handle       Memory Map Handle of the Satellite DSP.
  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_satellite_memorymap_register(uint32_t master_handle,
                                                     uint32_t sat_handle,
                                                     uint32_t sat_domain_id);

/**
  Deletes the satellite Memory Map Handle along with the association to
  the corresponding master record and the satellite domain ID.

  @datatypes

  @param[in] unmap_master_handle  Memory Map Handle on the Master DSP.
  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_satellite_memorymap_check_deregister(uint32_t unmap_master_handle, uint32_t sat_domain_id);

/**
  Query function for the satellite handle given the master handle and the satellite domain ID.

  @datatypes

  @param[in] master_handle    Memory Map Handle of the Master DSP.
  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  uint32_t sat_handle

  @dependencies
  Before calling this function, the global must be initialized.
*/
uint32_t apm_offload_get_sat_handle_from_master_handle(uint32_t master_handle, uint32_t sat_domain_id);
/**
  Registers the satellite Memory Map Handle along with the association to
  the corresponding master Handle and the satellite domain ID for unloaned memory.

  @datatypes

  @param[in] master_handle    Memory Map Handle of the Master DSP.
  @param[in] sat_handle       Memory Map Handle of the Satellite DSP.
  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_unloaned_mem_register(uint32_t sat_domain_id, uint32_t master_handle, uint32_t sat_handle);

/**
  Deletes the satellite Memory Map Handle along with the association to
  the corresponding master record and the satellite domain ID for the unloaned memory.

  @datatypes

  @param[in] sat_handle       Memory Map Handle of the Satellite DSP.
  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_unloaned_mem_deregister(uint32_t sat_domain_id, uint32_t sat_handle);

/**
 Deletes satellite memory map bookkeeping for the given satellite process domain id.

  @datatypes

  @param[in] sat_domain_id    Domain ID of the Satellite DSP.

  @return
  ar_result_t to indicate success or failure

  @dependencies
  Before calling this function, the global must be initialized.
*/
ar_result_t apm_offload_sat_cleanup(uint32_t sat_domain_id);

/* =======================================================================
   Memory Utility Function Declarations
========================================================================== */
uint32_t apm_offload_get_va_offset_from_sat_handle(uint32_t sat_domain_id, uint32_t sat_handle, void *address);

/* =======================================================================
  Utility function to reset the memory manager
========================================================================== */
ar_result_t apm_offload_mem_mgr_reset(void);


ar_result_t apm_offload_mem_mgr_reset_v1(void);

ar_result_t apm_offload_mem_mgr_reset_v2(void);

void *apm_offload_memory_malloc_v1(uint32_t sat_domain_id, uint32_t req_size, apm_offload_ret_info_t *ret_info_ptr);

void *apm_offload_memory_malloc_v2(uint32_t                peer_proc_domain_id,
                                   uint32_t                req_size,
                                   apm_offload_ret_info_t *ret_info_ptr);

void apm_offload_memory_free_v1(apm_offload_ret_info_t *shmem_info_ptr);

void apm_offload_memory_free_v2(apm_offload_ret_info_t *ret_info_ptr);

uint32_t apm_offload_get_persistent_sat_handle_v1(uint32_t sat_domain_id, uint32_t master_handle);

uint32_t apm_offload_get_persistent_sat_handle_v2(uint32_t sat_domain_id, uint32_t master_handle);

ar_result_t apm_offload_global_mem_mgr_init_v2(POSAL_HEAP_ID heap_id);

ar_result_t apm_offload_global_mem_mgr_deinit_v2();


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // APM_OFFLOAD_MEMMAP_UTILS_H
