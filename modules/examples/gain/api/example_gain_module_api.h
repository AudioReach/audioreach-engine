#ifndef _GAIN_MODULE_API_H_
#define _GAIN_MODULE_API_H_

/**
 * \file gain_module_api.h
 *
 * \brief
 *
 *     Example Gain Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*------------------------------------------------------------------------
* Include files
* -----------------------------------------------------------------------*/
#include "module_cmn_api.h"

/** @h2xml_title1          {Gain Module API}
    @h2xml_title_agile_rev {Gain Module API}
    @h2xml_title_date      {Oct 03, 2019} */

/*------------------------------------------------------------------------------
   Defines
------------------------------------------------------------------------------*/
/* Input port ID of Gain module */
#define GAIN_MODULE_DATA_INPUT_PORT 0x2

/* Output port ID of Gain module */
#define GAIN_MODULE_DATA_OUTPUT_PORT 0x1

#define GAIN_MODULE_STACK_SIZE 4096
/*==============================================================================
   Module
==============================================================================*/

/** Module ID for Gain module  */
#define MODULE_ID_EXAMPLE_GAIN_MODULE 0x0700106F
/** @h2xmlm_module       {"MODULE_ID_EXAMPLE_GAIN_MODULE",
                           MODULE_ID_EXAMPLE_GAIN_MODULE}
    @h2xmlm_builtIn     {FALSE}
    @h2xmlm_displayName {"Example - Gain"}
    @h2xmlm_description  {Gain Module \n
                          - Supports following params:
                          - PARAM_ID_GAIN_MODULE_GAIN
                          - \n
                          - Supported Input Media Format:
                          - Data Format          : FIXED_POINT
                          - fmt_id               : Don't care
                          - Sample Rates         : Don't care
                          - Number of channels   : 1 to 32
                          - Channel type         : Don't care
                          - Bits per sample      : 16, 32
                          - Q format             : Don't care
                          - Interleaving         : de-interleaved unpacked
                          - Signed/unsigned      : Signed}
    @h2xmlm_dataMaxInputPorts        { 1 }
    @h2xmlm_dataInputPorts           { IN  = GAIN_MODULE_DATA_INPUT_PORT}
    @h2xmlm_dataOutputPorts          { OUT = GAIN_MODULE_DATA_OUTPUT_PORT}
    @h2xmlm_dataMaxOutputPorts       { 1 }
    @h2xmlm_supportedContTypes      {APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC}
    @h2xmlm_stackSize                {GAIN_MODULE_STACK_SIZE }
    @{                   <-- Start of the Module -->
*/

/*==============================================================================
   API definitions
==============================================================================*/
/* ID of the parameter used to set the gain */
#define PARAM_ID_GAIN_MODULE_GAIN 0x08001175

/** @h2xmlp_parameter   {"PARAM_ID_GAIN_MODULE_GAIN",
                         PARAM_ID_GAIN_MODULE_GAIN}
    @h2xmlp_description {Configures the gain}
    @h2xmlp_toolPolicy  {Calibration; RTC} */

#include "spf_begin_pack.h"
/* Payload for parameter param_id_gain_cfg_t */
struct param_id_module_gain_cfg_t
{
   uint16_t gain;
   /**< @h2xmle_description {Linear gain (in Q13 format)}
        @h2xmle_dataFormat  {Q13}
        @h2xmle_default     {0x2000} */

   uint16_t reserved;
   /**< @h2xmle_description {Clients must set this field to 0.}
        @h2xmle_rangeList   {"0"=0} */
}
#include "spf_end_pack.h"
;
/* Structure type def for above payload. */
typedef struct param_id_module_gain_cfg_t param_id_module_gain_cfg_t;

/**  @}                   <-- End of the Module -->*/

#endif //_GAIN_MODULE_API_H_
