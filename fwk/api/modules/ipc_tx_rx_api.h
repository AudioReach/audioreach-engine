#ifndef _IPC_TX_RX_API_H_
#define _IPC_TX_RX_API_H_

/*==============================================================================
  @file ipc_tx_rx_api.h
  @brief This file contains IPC Tx and Rx module APIs

 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
// clang-format off
/* =========================================================================
   Edit History

   $Header:

   when       who       what, where, why
   --------   -------   ----------------------------------------------------
 ======================================================================== */
// clang-format on

 /*------------------------------------------------------------------------
 * Include files
 * -----------------------------------------------------------------------*/
#include "module_cmn_api.h"
#include "ipc_module_cmn_api.h"

/** @h2xml_title1           {IPC Tx and Rx API}
     @h2xml_title_agile_rev {IPC Tx and Rx API}
     @h2xml_title_date      {July 28, 2025} */

/*------------------------------------------------------------------------------
   Defines
------------------------------------------------------------------------------*/

// IPC TX related macros
#define IPC_TX_DATA_INPUT_PORT   0x2
#define IPC_TX_DATA_OUTPUT_PORT  0x1

#define IPC_TX_MAX_INPUT_PORTS   0x1
#define IPC_TX_MAX_OUTPUT_PORTS  0x1

#define IPC_TX_STACK_SIZE        4096

// IPC RX related macros
#define IPC_RX_DATA_INPUT_PORT   0x2
#define IPC_RX_DATA_OUTPUT_PORT  0x1

#define IPC_RX_MAX_INPUT_PORTS   0x1
#define IPC_RX_MAX_OUTPUT_PORTS  0x1

#define IPC_RX_STACK_SIZE        4096


/*==============================================================================
   API definitions
==============================================================================*/

/** @ingroup ar_spf_mod_ipc_tx_rx_macros
    ID of the parameter used to set the IPC Tx and Rx. */
#define PARAM_ID_IPC_DATA_LINK_INFO       0x08001B91

/** @h2xmlp_subStruct
    @h2xmlp_description  {Payload for per IPC port configuration.}
    @h2xmlp_toolPolicy   {Calibration} */
#include "spf_begin_pack.h"
struct ipc_data_link_info_per_port_t
{
    uint32_t ipc_port_type;
   /**< @h2xmle_description { Specifies whether the ipc link is connected to current module's external input/output }
        @h2xmle_rangeList   {"INPUT"=0;
                             "OUTPUT"=1}
        @h2xmle_default     {0} */

   uint32_t self_port_id;
   /**< @h2xmle_description { self port ID of the IPC input/output port. }
        @h2xmle_default     {0} */

   uint32_t self_module_iid;
   /**< @h2xmle_description { Self module instance ID.}
        @h2xmle_default     {0x0} */

   uint32_t self_proc_domain_id;
   /**< @h2xmle_description { Self processor domain ID  }
        @h2xmle_default     {0x0} */
   /**< */

   uint32_t peer_port_id;
   /**< @h2xmle_description { Port ID of the peer IPC modules input/output port. For IPC Tx output, peer is IPC Rx input and vice versa.
                              If self ipc port type is input, peer ipc port type is considered output.}
        @h2xmle_default     {0x0} */

   uint32_t peer_module_iid;
   /**< @h2xmle_description { Peer Module instance ID, this is considered as the peer module's GPR address as well.}
        @h2xmle_default     {0x0} */

   uint32_t peer_proc_domain_id;
   /**< @h2xmle_description { Proc domain of the Peer module instance. }
        @h2xmle_default     {0x0} */
}
#include "spf_end_pack.h"
;
/* Structure type def for above payload. */
typedef struct ipc_data_link_info_per_port_t ipc_data_link_info_per_port_t;


/** @h2xmlp_parameter   {"PARAM_ID_IPC_DATA_LINK_INFO",
                          PARAM_ID_IPC_DATA_LINK_INFO}
    @h2xmlp_description { Parameter used to configure self and peer information for the given IPC Tx/Rx module. }
    @h2xmlp_toolPolicy  {Calibration} */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_ipc_data_link_info_t
{
    uint32_t num_ipc_ports;
   /**< @h2xmle_description { Specifies number of IPC ports does the IPC tx/rx module has. }
        @h2xmle_default     {1} */

#if defined(__H2XML__)
   ipc_data_link_info_per_port_t   ipc_port_info[0];
   /**< @h2xmle_description { self port ID of the IPC input/output port.}
        @h2xmle_variableArraySize  { "num_ipc_ports" } */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/* Structure type def for above payload. */
typedef struct param_id_ipc_data_link_info_t param_id_ipc_data_link_info_t;

/*==============================================================================
   Module
==============================================================================*/

/** @ingroup ar_spf_mod_ipc_tx_rx_macros
    ipc_tx module.

    @subhead4{Supported parameter IDs}
    - PARAM_ID_IPC_TX

    @subhead4{Supported input media format ID}
    - Data Format          : FIXED_POINT @lstsp1
    - fmt_id               : Don't care @lstsp1
    - Sample Rates         : Don't care @lstsp1
    - Number of channels   : 1 to 128 (for certain products this module supports only 32 channels) @lstsp1
    - Channel type         : Don't care @lstsp1
    - Bits per sample      : 16, 32 @lstsp1
    - Q format             : Don't care @lstsp1
    - Interleaving         : De-interleaved unpacked @lstsp1
    - Signed/unsigned      : Signed @lstsp1
 */
#define MODULE_ID_IPC_TX 			0x07001184
/** @h2xmlm_module       {"MODULE_ID_IPC_TX",
                           MODULE_ID_IPC_TX}
    @h2xmlm_displayName  {"ipc_tx"}
    @h2xmlm_modSearchKeys{ ipc_tx, Audio}
	@h2xmlm_description  { ipc_tx module \n
                          - Supports following params:
                          - PARAM_ID_IPC_DATA_LINK_INFO \n
                          - Supported Input Media Format:
                          - Data Format          : FIXED_POINT
                          - fmt_id               : Don't care
                          - Sample Rates         : Don't care
                          - Number of channels   : 1 to 128 (for certain products this module supports only 32 channels)
                          - Channel type         : Don't care
                          - Bits per sample      : 16, 32
                          - Q format             : Don't care
                          - Interleaving         : de-interleaved unpacked
                          - Signed/unsigned      : Signed}
    @h2xmlm_dataMaxInputPorts        { IPC_TX_MAX_INPUT_PORTS }
    @h2xmlm_dataMaxOutputPorts       { IPC_TX_MAX_OUTPUT_PORTS }
	@h2xmlm_supportedContTypes       { APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC, APM_CONTAINER_TYPE_PTC}
    @h2xmlm_isOffloadable            { true}
	@h2xmlm_stackSize                { IPC_TX_STACK_SIZE }
    @{                   <-- Start of the Module -->

    @h2xml_Select        {"param_id_ipc_data_link_info_t"}
    @h2xmlm_InsertParameter

    @h2xml_Select           {param_id_ipc_data_link_info_t::ipc_port_info::ipc_port_type}
    @h2xmle_defaultList     {1}

    @h2xml_Select           {ipc_data_link_info_per_port_t}
    @h2xmlm_InsertStructure



    @}                   <-- End of the Module -->
*/


/*==============================================================================
   Module
==============================================================================*/

/** @ingroup ar_spf_mod_ipc_tx_rx_macros
    ipc_rx module.

    @subhead4{Supported parameter IDs}
    - PARAM_ID_IPC_RX

    @subhead4{Supported input media format ID}
    - Data Format          : FIXED_POINT @lstsp1
    - fmt_id               : Don't care @lstsp1
    - Sample Rates         : Don't care @lstsp1
    - Number of channels   : 1 to 128 @lstsp1
    - Channel type         : Don't care @lstsp1
    - Bits per sample      : 16, 32 @lstsp1
    - Q format             : Don't care @lstsp1
    - Interleaving         : De-interleaved unpacked @lstsp1
    - Signed/unsigned      : Signed @lstsp1
 */
#define MODULE_ID_IPC_RX 		0x07001185
/** @h2xmlm_module       {"MODULE_ID_IPC_RX",
                           MODULE_ID_IPC_RX}
    @h2xmlm_displayName  {"ipc_rx"}
    @h2xmlm_modSearchKeys{ipc_rx, Audio}
	@h2xmlm_description  {ipc_rx Module \n
                          - Supports following params:
                          - PARAM_ID_IPC_DATA_LINK_INFO \n
                          - Supported Input Media Format:
                          - Data Format          : FIXED_POINT
                          - fmt_id               : Don't care
                          - Sample Rates         : Don't care
                          - Number of channels   : 1 to 128 (for certain products this module supports only 32 channels)
                          - Channel type         : Don't care
                          - Bits per sample      : 16, 32
                          - Q format             : Don't care
                          - Interleaving         : any
                          - Signed/unsigned      : Signed }
    @h2xmlm_dataMaxInputPorts        { IPC_RX_MAX_INPUT_PORTS }
    @h2xmlm_dataInputPorts           { IN  = IPC_RX_DATA_INPUT_PORT}
    @h2xmlm_dataMaxOutputPorts       { IPC_RX_MAX_OUTPUT_PORTS }
    @h2xmlm_dataOutputPorts          { OUT = IPC_RX_DATA_OUTPUT_PORT}
	@h2xmlm_supportedContTypes       { APM_CONTAINER_TYPE_GC, APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_PTC}
    @h2xmlm_isOffloadable            {true}
	@h2xmlm_stackSize                { IPC_RX_STACK_SIZE }
    @{                     <-- Start of the Module -->


    @h2xml_Select        {"param_id_ipc_data_link_info_t"}
    @h2xmlm_InsertParameter

    @h2xml_Select           {param_id_ipc_data_link_info_t::ipc_port_info::ipc_port_type}
    @h2xmle_defaultList     {0}

    @h2xml_Select           {ipc_data_link_info_per_port_t}
    @h2xmlm_InsertStructure

    @}                   <-- End of the Module -->*/

#endif //_IPC_TX_RX_API_H_
