/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     -------------------------------------
 * 7/15/26   vasharma    Added file for opensource resampler based MFC
   ========================================================================= */

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#ifndef CAPI_MFC_OSRC_H
#define CAPI_MFC_OSRC_H

#include "capi.h"
#include "ar_defs.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

capi_err_t capi_mfc_osrc_get_static_properties(capi_proplist_t *init_set_properties,
                                                capi_proplist_t *static_properties);
capi_err_t capi_mfc_osrc_init(capi_t *_pif, capi_proplist_t *init_set_properties);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // CAPI_MFC_OSRC_H
