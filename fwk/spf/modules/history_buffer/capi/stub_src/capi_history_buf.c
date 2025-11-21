/**
 *   \file capi_history_buffer.c
 *
 *   \brief -
 *    This file contains CAPI Stub implementation for capi History Buffer.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_history_buffer.h"

/*==============================================================================
   Public Function Implementation
==============================================================================*/

/*
  This function is used to query the static properties to create the CAPI.

  param[in] init_set_prop_ptr: Pointer to the initializing property list
  param[in, out] static_prop_ptr: Pointer to the static property list

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_history_buffer_get_static_properties(capi_proplist_t *init_set_prop_ptr, capi_proplist_t *static_prop_ptr)
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
capi_err_t capi_history_buffer_init(capi_t *capi_ptr, capi_proplist_t *init_set_prop_ptr)
{
   return CAPI_EUNSUPPORTED;
}
