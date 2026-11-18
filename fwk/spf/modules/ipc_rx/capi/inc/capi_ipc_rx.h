/*==============================================================================
@file capi_ipc_rx.h
@brief This file contains CAPI API for IPC RX module.

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
// clang-format off
// No Edit History for automated refactoring.
// clang-format on

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#ifndef CAPI_IPC_RX_H
#define CAPI_IPC_RX_H

/**------------------------------------------------------------------------
 * @brief Includes
 * -----------------------------------------------------------------------*/
#include "capi.h"   /* For capi_t and capi_proplist_t */
#include "ar_defs.h" /* For common audio return codes (AR_EOK, etc.) */

#ifdef __cplusplus
extern "C"{
#endif /*__cplusplus*/

/**------------------------------------------------------------------------
 * @brief Function Declarations
 * -----------------------------------------------------------------------*/

/**
 * \brief Gets the static properties of the IPC_RX CAPI module.
 *
 * This function is designed to query the immutable, static characteristics of
 * the IPC_RX module, such as its memory footprint, stack requirements, and
 * supported configurations, without requiring an instantiated module object.
 * It is typically called during module discovery or initialization planning.
 *
 * \param[in] init_set_properties Optional pointer to a property list containing
 *                                initial properties that might influence the static
 *                                properties returned. Can be NULL if no such properties are applicable.
 * \param[out] static_properties   Pointer to a `capi_proplist_t` structure where the
 *                                 module's static properties will be populated. This
 *                                 list must be pre-allocated by the caller.
 *
 * \return CAPI_EOK on successful retrieval of static properties.
 *         An appropriate CAPI error code if parameters are invalid or if an
 *         internal error occurs during property retrieval.
 */
capi_err_t capi_ipc_rx_get_static_properties(capi_proplist_t *init_set_properties, capi_proplist_t *static_properties);

/**
 * \brief Initializes a new instance of the IPC_RX CAPI module.
 *
 * This function creates and configures a new IPC_RX module instance. It involves
 * allocating memory for the module's internal state, setting up its operational
 * parameters, and preparing it for data processing.
 *
 * \param[in,out] _pif               Pointer to the `capi_t` instance structure that
 *                                   will be initialized as the IPC_RX module. The caller
 *                                   is responsible for allocating memory for this structure.
 * \param[in] init_set_properties  Optional pointer to a `capi_proplist_t` structure
 *                                   containing properties that define the initial
 *                                   configuration of the module (e.g., heap ID, callbacks).
 *                                   Can be NULL if default initialization is desired.
 *
 * \return CAPI_EOK on successful initialization of the module instance.
 *         An appropriate CAPI error code if initialization fails, for example,
 *         due to invalid parameters, insufficient memory, or unsupported configurations.
 */
capi_err_t capi_ipc_rx_init(capi_t *_pif, capi_proplist_t *init_set_properties);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif //CAPI_IPC_RX_H
