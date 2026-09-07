/* ======================================================================== */
/**
  @file capi_nxp_device.h
  @brief This file contains CAPI API's published by nxp device module.

  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
  SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/

#ifndef CAPI_NXP_DEVICE_H
#define CAPI_NXP_DEVICE_H

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "capi.h"

#ifdef __cplusplus
extern "C"
{
#endif /*__cplusplus*/

/*------------------------------------------------------------------------
 * Function declarations
 * ----------------------------------------------------------------------*/

capi_err_t capi_nxp_device_sink_init(
   capi_t *_pif,
   capi_proplist_t *init_set_properties);

capi_err_t capi_nxp_device_sink_get_static_properties(
   capi_proplist_t *init_set_properties,
   capi_proplist_t *static_properties);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // CAPI_NXP_DEVICE_H