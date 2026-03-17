#ifndef _APM_SHMEM_UTIL_H_
#define _APM_SHMEM_UTIL_H_

/**
 * \file apm_shmem_util.h
 * \brief
 *     This file declares utility functions to manage shared memory between processors,
 *     including physical to virtual address mapping, etc.
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
#include "gpr_api_inline.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

typedef struct apm_shmem_utils_vtable
{
   ar_result_t (*apm_shmem_cmd_handler_fptr)(apm_t *apm_info_ptr, spf_msg_t *msg_ptr);

   bool_t (*apm_shmem_is_supported_mem_pool_fptr)(uint16_t mem_pool_id);

}apm_shmem_utils_vtable_t;

/*----------------------------------------------------------------------------
 * Function Declarations and Documentation
 * -------------------------------------------------------------------------*/

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

ar_result_t apm_shmem_utils_init(apm_t *apm_info_ptr);


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _APM_SHMEM_UTIL_H_
