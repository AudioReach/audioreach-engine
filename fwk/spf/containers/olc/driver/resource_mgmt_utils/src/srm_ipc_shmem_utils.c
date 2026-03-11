/**
 * \file srm_ipc_shmem_utils.c
 * \brief
 *     This file contains Satellite Graph Management utility functions for handling the shared memory between the processors
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sprm_i.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/* Function to allocate the shared memory for MDF used for data and control communication */
ar_result_t sgm_shmem_alloc(uint32_t shmem_size, uint32_t satellite_proc_domain, sgm_shmem_handle_t *shmem)
{
   ar_result_t            result = AR_EOK;
   apm_offload_ret_info_t ret_info_ptr;

   if (NULL == shmem)
   {
      return AR_EBADPARAM;
   }

   void *shmem_buf_ptr = apm_offload_memory_malloc(satellite_proc_domain, shmem_size, &ret_info_ptr);
   if (NULL == shmem_buf_ptr)
   {
      // Failed to allocate Memory ; Error Message would be printed by the caller
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

/* function to free the shared memory */
ar_result_t sgm_shmem_free(sgm_shmem_handle_t *shmem)

{
   ar_result_t result = AR_EOK;
   if (NULL == shmem)
   {
      // check if the shmem handle is valid
      return AR_EBADPARAM;
   }

   // check if the mdf shared memory is a valid memory
   if ((NULL != shmem->shm_mem_ptr) && (NULL != shmem->mem_attr.ret_ptr))
   {
      apm_offload_memory_free(&shmem->mem_attr);
   }

   // reset the shared memory handle
   memset(shmem, 0, sizeof(sgm_shmem_handle_t));

   return result;
}
