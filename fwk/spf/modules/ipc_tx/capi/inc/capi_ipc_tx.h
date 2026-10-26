/* ======================================================================== */
/**
@file capi_ipc_tx.h


*/

/* =========================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
  ========================================================================== */

/* =========================================================================
 * Edit History:
 * when         who         what, where, why
 * ----------   -------     -------------------------------------

   ========================================================================= */

/*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#ifndef CAPI_IPC_TX_H
#define CAPI_IPC_TX_H

#include "capi.h"
#include "ar_defs.h"

#ifdef __cplusplus
extern "C"{
#endif /*__cplusplus*/

/**
* Get static properties of ipc_tx module such as
* memory, stack requirements etc.
*/
capi_err_t capi_ipc_tx_get_static_properties(
      capi_proplist_t *init_set_properties,
      capi_proplist_t *static_properties);


/**
* Instantiates(and allocates) the module memory.
*/
capi_err_t capi_ipc_tx_init(
      capi_t          *_pif,
      capi_proplist_t *init_set_properties);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif //CAPI_IPC_TX_H

