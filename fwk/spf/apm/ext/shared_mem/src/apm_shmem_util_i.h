#ifndef _APM_SHMEM_UTIL_I_H_
#define _APM_SHMEM_UTIL_I_H_

/**
 * \file apm_shmem_util_i.h
 * \brief
 *     This file declares internal utility functions
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

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Function Declarations and Documentation
 * -------------------------------------------------------------------------*/

/**------------------------------------------------------------------------------
 *  Function Declaration
 *----------------------------------------------------------------------------*/

 ar_result_t apm_shmem_cmd_handler_v2(apm_t *apm_info_ptr, spf_msg_t *msg_ptr);


#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _APM_SHMEM_UTIL_I_H_
