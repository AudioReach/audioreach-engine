/**
 * \file srm_ipc_shmem_utils.c
 * \brief Utility functions for handling shared memory between processors in Satellite Graph Management
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sprm_i.h"

/* =======================================================================
   Static Function Definitions
   ========================================================================== */

   /**
    * \brief Allocates shared memory for MDF used for data and control communication
    * \param[in] shmem_size Size of the shared memory
    * \param[in] satellite_proc_domain Satellite process domain
    * \param[out] shmem Pointer to the shared memory handle
    * \return Result of the operation
    */
ar_result_t sgm_shmem_alloc(uint32_t shmem_size, uint32_t satellite_proc_domain, sgm_shmem_handle_t *shmem)
{
   ar_result_t            result = AR_EOK;
   apm_offload_ret_info_t ret_info_ptr;

   if (NULL == shmem)
   {
      // Preventive check: Validate shmem pointer
      return AR_EBADPARAM;
   }

   void *shmem_buf_ptr = apm_offload_memory_malloc(satellite_proc_domain, shmem_size, &ret_info_ptr);
   if (NULL == shmem_buf_ptr)
   {
      // Failed to allocate memory; Error message would be printed by the caller
      return AR_ENOMEMORY;
   }
   else
   {
      // Update the information in the shmem handle
      shmem->mem_attr       = ret_info_ptr;
      shmem->shm_alloc_size = shmem_size;
      shmem->shm_mem_ptr    = shmem_buf_ptr;
   }

   return result;
}

/**
 * \brief Frees shared memory
 * \param[in,out] shmem Pointer to the shared memory handle
 * \return Result of the operation
 */
ar_result_t sgm_shmem_free(sgm_shmem_handle_t *shmem)
{
   ar_result_t result = AR_EOK;

   if (NULL == shmem)
   {
      // Preventive check: Validate shmem pointer
      return AR_EBADPARAM;
   }

   // Check if the MDF shared memory is a valid memory
   if ((NULL != shmem->shm_mem_ptr) && (NULL != shmem->mem_attr.ret_ptr))
   {
      apm_offload_memory_free(&shmem->mem_attr);
   }

   // Reset the shared memory handle
   memset(shmem, 0, sizeof(sgm_shmem_handle_t));

   return result;
}
