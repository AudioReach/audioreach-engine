/**
 * \file capi_history_buffer.h
 *
 * \brief
 *
 *     This file contains CAPI APIs for History Buffer Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*------------------------------------------------------------------------
 * Include files and Macro definitions
 * -----------------------------------------------------------------------*/
#ifndef CAPI_HISTORY_BUFFER_H
#define CAPI_HISTORY_BUFFER_H

#include "capi.h"
#include "ar_defs.h"
#include "imcl_dam_detection_api.h"

/*------------------------------------------------------------------------
* Function declarations
* -----------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

/*
  This function is used to query the static properties to create the history
  buffer module

  param[in] init_set_prop_ptr: Pointer to the initializing property list
  param[in, out] static_prop_ptr: Pointer to the static property list

  return: CAPI_EOK(0) on success else failure error code
 */
capi_err_t capi_history_buffer_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties);


/*
  This is the first function called by the framework after allocating the capi handle.
  Module is expected to initialize the capi_t handle with the init properties set by
  the framework

  param[in] capi_ptr: Pointer to the CAPI lib.
  param[in] init_set_prop_ptr: Pointer to the property list that needs to be
            initialized

  return: CAPI_EOK(0) on success else failure error code
*/
capi_err_t capi_history_buffer_init(capi_t *_pif, capi_proplist_t *init_set_properties);


#ifdef __cplusplus
}
#endif

#endif //CAPI_HISTORY_BUFFER_H

