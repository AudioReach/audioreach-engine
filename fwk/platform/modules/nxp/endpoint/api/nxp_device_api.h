#ifndef NXP_DEVICE_API_H
#define NXP_DEVICE_API_H
/**
 * \file nxp_device_api.h
 *
 * \brief nxp_device_api.h: This file contains the Module Id, Param IDs and
 * configuration structures exposed by the NXP Device Sink and Source Modules.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "hw_intf_cmn_api.h"

 /** @h2xml_title1           {NXP Device API}
     @h2xml_title_agile_rev  {NXP Device API}
     @h2xml_title_date       {June 10, 2026} */

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_NXP_device_macros
    Input port ID of NXP Device module */
#define PORT_ID_NXP_DEVICE_INPUT   0x2

/** @ingroup ar_spf_NXP_device_macros
    Output port ID of NXP Device module */
#define PORT_ID_NXP_DEVICE_OUTPUT  0x1

#define NXP_DEVICE_STACK_SIZE 2048

/*------------------------------------------------------------------------------
   Module
------------------------------------------------------------------------------*/

#define MODULE_ID_NXP_DEVICE_SINK 0x0700109C

/** @h2xmlm_module       {"MODULE_ID_NXP_DEVICE_SINK",
                           MODULE_ID_NXP_DEVICE_SINK}
    @h2xmlm_displayName  {"NXP Device"}
    @h2xmlm_modSearchKeys{hardware}
    @h2xmlm_description  {NXP_DEVICE Sink Module\n
                        - Supports following params:
                          - PARAM_ID_HW_EP_MF_CFG \n
                          - PARAM_ID_HW_EP_FRAME_SIZE_FACTOR \n
                          - Supported Input Media Format: \n
                          - Data Format          : FIXED_POINT \n
                          - fmt_id               : Don't care \n
                          - Sample Rates         : 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48, \n
                          -                        88.2, 96, 176.4, 192, 352.8, 384 kHz \n
                          - Number of channels   : 1 to 8 \n
                          - Channel type         : Don't care \n
                          - Bit Width            : 16 (bits per sample 16 and Q15), \n
                          -                      : 24 (bits per sample 24 and Q27) \n
                          -                      : 32 (bits per sample 32 and Q31)  \n
                          - Q format             : Q15, Q27, Q31 \n
                          - Interleaving         : ? }
    @h2xmlm_dataInputPorts      {IN = PORT_ID_NXP_DEVICE_INPUT}
    @h2xmlm_dataMaxInputPorts   {1}
    @h2xmlm_dataMaxOutputPorts  {0}
    @h2xmlm_supportedContTypes { APM_CONTAINER_TYPE_GC }
    @h2xmlm_isOffloadable       {false}
    @h2xmlm_stackSize           { NXP_DEVICE_STACK_SIZE }
    @{                   <-- Start of the Module -->
    @}                   <-- End of the Module -->*/

#endif /* NXP_DEVICE_API_H */