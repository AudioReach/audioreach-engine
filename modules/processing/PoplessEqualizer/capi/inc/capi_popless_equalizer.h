/* =========================================================================
 Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 * @file capi_popless_equalizer.h
 *
 * Common Audio Processor Interface for Popless Equalizer.
 */

#ifndef CAPI_POPLESS_EQUALIZER_H
#define CAPI_POPLESS_EQUALIZER_H

#include "capi.h"
#include "ar_defs.h"
#include "imcl_p_eq_vol_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get static properties of popless equalizer module such as
 * memory, stack requirements etc.
 * See Elite_CAPI.h for more details.
 */
capi_err_t capi_p_eq_get_static_properties(
        capi_proplist_t* init_set_properties,
        capi_proplist_t* static_properties);


/**
 * Instantiates(and allocates) the module memory.
 * See Elite_CAPI.h for more details.
 */
capi_err_t capi_p_eq_init(
        capi_t*          _pif,
        capi_proplist_t* init_set_properties);

#ifdef __cplusplus
}
#endif

#endif /* CAPI_POPLESS_EQUALIZER_H */

