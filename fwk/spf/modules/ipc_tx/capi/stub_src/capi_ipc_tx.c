/**
 *   \file capi_ipc_tx.c
 *   \brief This file contains CAPI Stub implementation for capi ipc TX module.
 */

/*===========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
===========================================================================*/


#include "capi_ipc_tx.h"

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/*
  This function is used to query the static properties to create the CAPI.

  param[in] init_set_prop_ptr: Pointer to the initializing property list
  param[in, out] static_prop_ptr: Pointer to the static property list

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_ipc_tx_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
{
   return CAPI_EUNSUPPORTED;
}

/*
  This function is used init the CAPI lib.

  param[in] capi_ptr: Pointer to the CAPI lib.
  param[in] init_set_prop_ptr: Pointer to the property list that needs to be
            initialized

  return: CAPI_EOK(0) on success else failure error code
*/
capi_err_t capi_ipc_tx_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   return CAPI_EUNSUPPORTED;
}
