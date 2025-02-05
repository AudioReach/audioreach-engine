#ifndef _APM_OFFLOAD_MEM_H_
#define _APM_OFFLOAD_MEM_H_

/**
 * \file apm_offload_mem.h
 *
 * \brief
 *     This file declares utility functions for malloc/free
 *     operations for offload case.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/

#include "posal.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#define APM_OFFLOAD_INVALID_VAL 0xFFFFFFFF

#define APM_OFFLOAD_MEMORY_DBG

/*----------------------------------------------------------------------------
 * Public Type Declarations
 * -------------------------------------------------------------------------*/

struct apm_offload_ret_info_t
{
   uint32_t master_handle;

   uint32_t sat_handle;

   uint32_t offset; /*will be the same for both master and slave*/

   uint32_t heap_id; /* specifies the heap_id */

   void *ret_ptr;  /* memory pointer allocator */
};

typedef struct apm_offload_ret_info_t apm_offload_ret_info_t;

/* =======================================================================
   Memory Function Declarations
========================================================================== */
/**
  Allocates memory and returns mapping information.

  @datatypes

  @param[in] size                    Number of bytes to allocate.
  @param[in] sat_domain_id           Satellite Domain ID for whose pupose the memory is meant.
                                     This will decide the Heap ID from which to allocate memory.
  @param[in] apm_offload_ret_info_t *  Pointer to the struct that will be populated by this function.

  @return
  void*                              Pointer to the allocated block(Master), or NULL if the request failed.
  apm_offload_ret_info_t *ret_info_ptr  -> the offset of the allocated block from the heap start,
                                      -> the master_mem_handle
                                      -> the satellite_mem_handle


  @dependencies
  Before calling this function, the memories should be mapped to the master and satellite DSPs.
*/
void *apm_offload_memory_malloc(uint32_t sat_domain_id, uint32_t size, apm_offload_ret_info_t *ret_info_ptr);

/**
  Frees the memory pointed to, and returns it to the correct heap.

  @datatypes
`
  @param[in] ptr                     Pointer to the memory which has to be freed back to its heap.

  @dependencies
  Before calling this function, the memories should be mapped to the master and satellite DSPs.
*/

void apm_offload_memory_free(apm_offload_ret_info_t *shmem_info_ptr);

/**
  Get mem map handle for persistent configuration.

  @datatypes

  @param[in] master_handle           Memory map handle of master proc.
  @param[in] sat_domain_id           Satellite Domain ID for whose pupose the memory is meant.
  @return
  uint32                             Memory map handle for persistent mem


  @dependencies
  Before calling this function, the memories should be mapped to the master and satellite DSPs.
*/
uint32_t apm_offload_get_persistent_sat_handle(uint32_t sat_domain_id, uint32_t master_handle);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _APM_OFFLOAD_MEM_H_
