/**
 * \file apm_multi_client_stub.c
 *
 * \brief
 *
 *     This file handles stubs multi client.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
 * INCLUDE HEADER FILES
 ==============================================================================*/
#include "apm_multi_client.h"
#include "apm_internal.h"

ar_result_t apm_multi_client_init(apm_t *apm_info_ptr)
{
   apm_info_ptr->ext_utils.multi_client_vtbl_ptr = NULL;

   return AR_EOK;
}
