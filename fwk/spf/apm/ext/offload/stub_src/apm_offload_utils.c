/**
 * \file apm_offload_utils.c
 *
 * \brief
 *     This file contains stubbed implementation of APM Offload
 *     processing Utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#include "apm_internal.h"
#include "apm_offload_memmap_utils.h"
#include "apm_offload_pd_info.h"

/**==============================================================================
   Global Defines
==============================================================================*/

ar_result_t apm_offload_utils_init(apm_t *apm_info_ptr)
{
   apm_info_ptr->ext_utils.offload_vtbl_ptr = NULL;

   return AR_EOK;
}

ar_result_t apm_offload_utils_deinit()
{
   return AR_EOK;
}

void *apm_offload_memory_malloc(uint32_t sat_domain_id, uint32_t size, apm_offload_ret_info_t *ret_info_ptr)
{
   return NULL;
}

void apm_offload_memory_free(apm_offload_ret_info_t *shmem_info_ptr)
{
   return;
}

uint32_t apm_offload_get_persistent_sat_handle(uint32_t sat_domain_id, uint32_t master_handle)
{
   return 0;
}

bool_t apm_offload_utils_is_valid_sat_pd(uint32_t sat_proc_domain_id)
{
   return FALSE;
}

ar_result_t apm_offload_utils_get_sat_proc_domain_list(apm_offload_utils_sat_pd_info_t **sat_pd_info_pptr)
{
   if (sat_pd_info_pptr)
   {
      *sat_pd_info_pptr = NULL;
   }
   return AR_EOK;
}
