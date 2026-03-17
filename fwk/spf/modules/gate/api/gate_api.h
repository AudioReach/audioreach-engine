#ifndef GATE_API_H_
#define GATE_API_H_
/**
 * \file gate_api.h
 * \brief 
 *  	 API file for Gate Module
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "module_cmn_api.h"
#include "imcl_fwk_intent_api.h"


/** @h2xml_title1           {GATE API}
    @h2xml_title_agile_rev  {GATE API}
    @h2xml_title_date       {July 7, 2019} */

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_mod_gate_macros
    Input port ID of the Gate module. */
#define GATE_DATA_INPUT_PORT   0x2

/** @ingroup ar_spf_mod_gate_macros
    Output port ID of the Gate module. */
#define GATE_DATA_OUTPUT_PORT  0x1

/** @ingroup ar_spf_mod_gate_macros
    Maximum number of input ports of the Gate module. */
#define GATE_DATA_MAX_INPUT_PORTS 0x1

/** @ingroup ar_spf_mod_gate_macros
    Maximum number of output ports of the Gate module. */
#define GATE_DATA_MAX_OUTPUT_PORTS 0x1

/** @ingroup ar_spf_mod_gate_macros
    Stack size of the Gate module. */
#define GATE_STACK_SIZE 1024

/** @ingroup ar_spf_mod_gate_macros
    Control port for receiving deadline time. */
#define DEADLINE_TIME_INFO_IN AR_NON_GUID(0xC0000001)

/*==============================================================================
   Parameters
==============================================================================*/
 
/**
    ID of the deadline offset parameter, used by the Gate Module
 */
 
#define PARAM_ID_GATE_DEADLINE_OFFSET 0x08001BA9
/** @h2xmlp_parameter   {"PARAM_ID_GATE_DEADLINE_OFFSET", PARAM_ID_GATE_DEADLINE_OFFSET}
    @h2xmlp_description {Structure for PARAM_ID_GATE_DEADLINE_OFFSET parameter.
                         This parameter provides a value which can be used as an offset into the calcualted
                         deadline time value. Can be used to tune this to align packet delivery to BT controller
                         as close as posible to the ISO interval time}
   @h2xmlp_toolPolicy              {Calibration}                         */
#include "spf_begin_pack.h"
/** Payload of the PARAM_ID_GATE_DEADLINE_OFFSET parameter.
 */
struct param_id_gate_deadline_offset_t
{
   int32_t deadline_offset_us;
   /**< @h2xmle_description   {Deadline offset value in micro seconds.
    *                          Positive or negative offset}
         @h2xmle_default      {0}
         @h2xmle_range        {-10000..10000}*/
 
}
#include "spf_end_pack.h"
;
/* Structure for PARAM_ID_GATE_DEADLINE_OFFSET parameter. */
typedef struct param_id_gate_deadline_offset_t param_id_gate_deadline_offset_t;
 
/*==============================================================================
   Module
==============================================================================*/

/** @ingroup ar_spf_mod_gate_macros
    The Gate module gates data flow based on calculated deadline time.
    It does not modify data in any way.

    @subhead4{Supported parameter IDs}
    - PARAM_ID_GATE_EP_TRANSMISSION_DELAY

    @subhead4{Supported input media format ID}
    - Data Format         : FIXED_POINT @lstsp1
    - fmt_id              : Don't care @lstsp1
    - Sample Rates         : 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48, @lstsp1
                             88.2, 96, 176.4, 192, 352.8, 384 kHz @lstsp1
    - Number of channels   : 1 to 128 (for certain products this module supports only 32 channels) @lstsp1
    - Channel type         : Don't care @lstsp1
    - Bit Width            : Don't care @lstsp1
    - Q format             : Don't care @lstsp1
    - Interleaving         : Dont care
 */
#define MODULE_ID_GATE			0x07001042

/**
    @h2xmlm_module              {"Gate", MODULE_ID_GATE}
    @h2xmlm_displayName         {"Gate"}
    @h2xmlm_modSearchKeys       {Bluetooth}
    @h2xmlm_description         {- This module gates data flow based on calculated deadline time\n
                                 - It doesn't modify data in any way.
                                 - Supports following params:\n
                                 -- PARAM_ID_GATE_EP_TRANSMISSION_DELAY\n
                                 -- PARAM_ID_GATE_DEADLINE_OFFSET\n
                                 - Supported Input Media Format:\n
                                 -- Data Format         : FIXED_POINT\n
                                 -- fmt_id              : Don't care\n
                                 - Sample Rates         : 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48, \n
                                 -                        88.2, 96, 176.4, 192, 352.8, 384 kHz \n
                                 - Number of channels   : 1 to 128(for certain products this module supports only 32 channels)\n
                                 - Channel type         : Don't care \n
                                 - Bit Width            : Don't care \n
                                 - Q format             : Don't care \n
                                 - Interleaving         : Dont care}

    @h2xmlm_dataMaxInputPorts   {GATE_DATA_MAX_INPUT_PORTS}
    @h2xmlm_dataMaxOutputPorts  {GATE_DATA_MAX_OUTPUT_PORTS}
    @h2xmlm_dataInputPorts      {IN=GATE_DATA_INPUT_PORT}
    @h2xmlm_dataOutputPorts     {OUT=GATE_DATA_OUTPUT_PORT}
    @h2xmlm_ctrlStaticPort      {"DEADLINE_TIME_INFO_IN" = 0xC0000001,
                                 "Deadline time intent" = INTENT_ID_BT_DEADLINE_TIME}
    @h2xmlm_supportedContTypes {APM_CONTAINER_TYPE_GC}
    @h2xmlm_stackSize           {GATE_STACK_SIZE}
    @h2xmlm_toolPolicy          {Calibration}
    @{                          <-- Start of the Module -->
    @h2xml_Select               {"param_id_gate_deadline_offset_t"}
    @h2xmlm_InsertParameter
    @}                          <-- End of the Module -->
*/

#endif //GATE_API_H_
