#ifndef CAPI_INTF_EXTN_CALIBRATION_OPS_DONE_H
#define CAPI_INTF_EXTN_CALIBRATION_OPS_DONE_H

/**
 *   \file capi_intf_extn_calibration_ops.h
 *   \brief
 *        intf_extns related to dynamic calibration ops set from proxy static modules like vcpm.
 *
 *    This file defines interface extensions that would allows other proxy static modules
 *    like vcpm to indicate dynamic or runtime calibration settings done event to modules.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------------
 * Include Files
 *----------------------------------------------------------------------------*/
#include "capi_types.h"

/** @addtogroup capi_if_ext_calibration_ops_done
The Calibration Operation interface extension (#INTF_EXTN_CALIBRATION_OPS) 
defines calibration related operations (calibration_done).

Calibration_Done 
This communicates the information to the module that all the relevant calibration has been applied.
Using this marker, the module can apply the latest calibration to the library for all the 
new params together. 
*/

/** @addtogroup capi_if_ext_calibration_ops_done
@{ */

/** Unique identifier for calibration ops done interface extension. */
#define INTF_EXTN_CALIBRATION_OPS_DONE 0x0A00ffee /*TODO - RESERVE GUID*/

/** @} */ /* end_addtogroup capi_if_ext_calibration_ops_done */

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* CAPI_INTF_EXTN_CALIBRATION_OPS_DONE_H*/
