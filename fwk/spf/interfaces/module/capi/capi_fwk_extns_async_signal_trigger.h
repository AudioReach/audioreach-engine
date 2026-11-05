#ifndef _CAPI_FWK_EXTNS_EXTERNAL_IO_CTRL_H_
#define _CAPI_FWK_EXTNS_EXTERNAL_IO_CTRL_H_

/**
 *   \file capi_fwk_extns_external_io_ctrl.h
 *   \brief
 *        This file contains Async Signal Trigger Module's Extension.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "capi_types.h"

/** @addtogroup capi_fwk_extns_external_io_ctrl
@{ */

/*==============================================================================
     Constants
==============================================================================*/

/** Unique identifier of the framework extension for the Async signal trigger.

    This extension supports the following property and parameter IDs:
    - #FWK_EXTN_PROPERTY_ID_ASYNC_SIGNAL_CTRL
    - #FWK_EXTN_PROPERTY_ID_ASYNC_SIGNAL_CALLBACK_INFO
*/
#define FWK_EXTN_ASYNC_SIGNAL_TRIGGER 0x0A001024

/*==============================================================================
   Constants
==============================================================================*/
/** ID of the custom property used to share the async signal handle with the module.

    When the module sets this signal,
      1. container will be triggered with a command signal.
      2. container issues a module callback to inform that the signal is set.
      3. Container calls process after the callback is done, similar to other commands.

   Async signal must not be used for periodic data trigger signals like the STM extension.
   Modules need this extension, to indicate if module have dependency on a service which needs to
   trigger the module asynchronously.

   For example, if the module is waiting on a control interrupt from hardware, then module can
   register an ISR to set the async signal. Which intrun wakeups the  container and provides context
   for the module to process the async signal.

    @msgpayload{capi_prop_async_signal_ctrl_t}
    @table{weak__capi__prop__async__signal__ctrl__t}
*/
#define FWK_EXTN_PROPERTY_ID_ASYNC_SIGNAL_CTRL 0x0A001047

/*==============================================================================
   Type definitions
==============================================================================*/

typedef struct capi_prop_async_signal_ctrl_t capi_prop_async_signal_ctrl_t;

/** @weakgroup capi_prop_async_signal_ctrl_t
@{ */
struct capi_prop_async_signal_ctrl_t
{
   bool_t enable;
   /**< Specifies whether to enable the STM.

        @valuesbul
        - 0 -- FALSE (disable)
        - 1 -- TRUE (enable) @tablebulletend */

   void *async_signal_ptr;
   /**< Pointer to the signal from the framework.
    *    Its valid only if enable=1
    */
};
/** @} */ /* end_weakgroup capi_prop_async_signal_ctrl_t */

/*==============================================================================
   Constants
==============================================================================*/
/** ID of the parameter used to get the callback info from the module. framework
 *  issues the callback when the async signal triggers the container.

   The callback must be island safe, and module is responsible to exit island from
   the callback if necessary.

    @msgpayload{capi_prop_async_signal_callback_info_t}
    @table{weak__capi__prop__stm__latest__trigger__ts__ptr__t}
    */
#define FWK_EXTN_PROPERTY_ID_ASYNC_SIGNAL_CALLBACK_INFO 0x0A00105A

/*==============================================================================
   Type definitions
==============================================================================*/

/* Structure for above property */
typedef struct capi_prop_async_signal_callback_info_t capi_prop_async_signal_callback_info_t;

typedef ar_result_t (*fwk_extn_async_signal_callback_fn_ptr_t)(void *module_context_ptr);
/**< Pointer to the function that updates the timestamp. */

/** @weakgroup capi_prop_async_signal_callback_info_t
@{ */
struct capi_prop_async_signal_callback_info_t
{
   fwk_extn_async_signal_callback_fn_ptr_t module_callback_fptr;
   /**< Callback function ptr, which fwk calls when the async signal is set. The callback must be island safe,
    * and module is responsible to exit island from the callback if necessary. */

   void                          *module_context_ptr;
   /**< This module handle will be passed to the callback as an argument */
};
/** @} */ /* end_weakgroup capi_prop_async_signal_callback_info_t */

/** @} */ /* end_addtogroup capi_fwk_ext_async_signal_trigger */

#endif /* _CAPI_FWK_EXTNS_EXTERNAL_IO_CTRL_H_ */
