/**
 * \file apm_ext_cmn.c
 *  
 * \brief
 *  
 *     This file contains stubbed function definition for APM
 *     extended functionalites.
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
#include "apm_ext_cmn.h"

ar_result_t apm_ext_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   memset(&apm_info_ptr->ext_utils, 0, sizeof(apm_ext_utils_t));

   return result ;
}

ar_result_t apm_ext_utils_deinit(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;
   return result;
}