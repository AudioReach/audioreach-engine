/**
 * \file sprm_i.h
 * \brief Internal definitions and declarations for the OLC Satellite Graph Management
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OLC_SGM_I_H
#define OLC_SGM_I_H

#include "olc_cmn_utils.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "container_utils.h"
#include "posal_power_mgr.h"
#include "posal_memorymap.h"
#include "olc_driver.h"

/* =======================================================================
   OLC SGM Macros
   ======================================================================= */

/* =======================================================================
   OLC SGM Structure Definitions
   ======================================================================= */

/* =======================================================================
OLC SGM Function Declarations
========================================================================== */
/**--------------------------- olc_sgm_cmd_parser_utilities --------------------*/

/**
 * \brief Callback function for GPR packet
 * \param[in] packet Pointer to the GPR packet
 * \param[in] callback_data Pointer to the callback data
 * \return Result of the operation
 */
uint32_t sgm_gpr_callback(gpr_packet_t *packet, void *callback_data);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // #ifndef OLC_SGM_I_H
