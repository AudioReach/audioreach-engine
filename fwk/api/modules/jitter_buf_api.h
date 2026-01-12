#ifndef __JITTER_BUF_H
#define __JITTER_BUF_H
/**
 * \file jitter_buf_api.h
 * \brief
 *     This file contains module published by Jitter Buf CAPI intialization.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "module_cmn_api.h"
#include "imcl_fwk_intent_api.h"

/**
  @h2xml_title1          {Jitter Buffer Module}
  @h2xml_title_agile_rev {Jitter Buffer Modulee}
  @h2xml_title_date      {Jun 3, 2022}
*/

/** @ingroup ar_spf_mod_jitter_buf_macros
    Maximum number of input ports supported by the Jitter Buffer module. */
#define CAPI_JITTER_BUF_MAX_INPUT_PORTS         (1)

/** @ingroup ar_spf_mod_jitter_buf_macros
    Maximum number of output ports supported by the Jitter Buffer module. */
#define CAPI_JITTER_BUF_MAX_OUTPUT_PORTS        (1)

/** @ingroup ar_spf_mod_jitter_buf_macros
    Maximum number of control ports supported by the Jitter Buffer module. */
#define CAPI_JITTER_BUF_MAX_CTRL_PORTS          (1)

/** @ingroup ar_spf_mod_jitter_buf_macros
    Constants for #PARAM_ID_JITTER_BUF_INPUT_BUFFER_MODE. */
#define JBM_BUFFER_INPUT_AT_INPUT_TRIGGER 1

/** @ingroup ar_spf_mod_jitter_buf_macros
    Constants for #PARAM_ID_JITTER_BUF_INPUT_BUFFER_MODE. */
#define JBM_BUFFER_INPUT_AT_OUTPUT_TRIGGER 0

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configuration for Jitter Buffer module. */
#define PARAM_ID_JITTER_BUF_CONFIG              0x080014E3

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configuration for the settling time of the Jitter Buffer module. */
#define PARAM_ID_JITTER_BUF_SETTLING_TIME       0x080014E4

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configuration for the size of the Jitter Buffer. */
#define PARAM_ID_JITTER_BUF_SIZE_CONFIG         0x080014E6

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configuration for the input buffering mode of the Jitter Buf module. */
#define PARAM_ID_JITTER_BUF_INPUT_BUFFER_MODE   0x08001A61

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configuration for the state propagation in input buffer trigger mode of the Jitter Buf module. */
#define PARAM_ID_JITTER_BUF_STATE_PROP_CONFIG   0x08001B93

/*==============================================================================
   Type Definitions
==============================================================================*/


/** @h2xmlp_parameter   {"PARAM_ID_JITTER_BUF_CONFIG",
                          PARAM_ID_JITTER_BUF_CONFIG}
    @h2xmlp_description { This param ID is used to configure Jitter Buf module. }
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configure the Jitter Buffer module. */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_jitter_buf_config_t
{
   uint32_t jitter_allowance_in_ms;
   /**< It defines the range of jitter for client buffers.
        0 disables the module. */

   /**< @h2xmle_description { It defines the range of jitter for client buffers.
                              If 0 then used to disable module. }
        @h2xmle_default     { 200 }
        @h2xmle_range       {0..300}*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/* Structure to define the Jitter Buf configuration.*/
typedef struct param_id_jitter_buf_config_t param_id_jitter_buf_config_t;

/** @h2xmlp_parameter   {"PARAM_ID_JITTER_BUF_SETTLING_TIME",
                          PARAM_ID_JITTER_BUF_SETTLING_TIME}
    @h2xmlp_description { This param ID is used to configure settling time before accurate drift can be reported. }
    @h2xmlp_toolPolicy  {CALIBRATION} */

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configure settling time before accurate drift can be reported. */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_jitter_buf_settling_time_t
{
   uint32_t drift_settlement_in_ms;
   /**< It defines the maximum time usually taken to settle.
        in steady state to start reporting drift. */

   /**< @h2xmle_description { It defines the max time usually taken to settle.
                              in steady state to start reporting drift. }
        @h2xmle_default     { 0 }
        @h2xmle_range       {0..10000} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/* Structure to define the Jitter Buf configuration.*/
typedef struct param_id_jitter_buf_settling_time_t param_id_jitter_buf_settling_time_t;

/** @h2xmlp_parameter   {"PARAM_ID_JITTER_BUF_SIZE_CONFIG",
                          PARAM_ID_JITTER_BUF_SIZE_CONFIG}
    @h2xmlp_description { This param ID is used to configure Jitter Buf module size from QACT for debug purposes. }
    @h2xmlp_toolPolicy  {CALIBRATION} */

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configure Jitter Buffer module size from QACT for debug purposes. */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_jitter_buf_size_config_t
{
   uint32_t jitter_allowance_in_ms;
   /**< It defines the range of jitter for client buffers.
        0 disables the module. */

   /**< @h2xmle_description { It defines the range of jitter for client buffers.
                              If 0 then used to disable module. }
        @h2xmle_default     { 200 }
        @h2xmle_range       {0..300}*/

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct param_id_jitter_buf_size_config_t param_id_jitter_buf_size_config_t;

/** @h2xmlp_parameter   {"PARAM_ID_JITTER_BUF_INPUT_BUFFER_MODE",
                          PARAM_ID_JITTER_BUF_INPUT_BUFFER_MODE}
    @h2xmlp_description { This param ID is used to configure the input buffering mode. }
    @h2xmlp_toolPolicy  {CALIBRATION} */

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configure the input buffering mode. */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_jitter_buf_input_buffer_mode_t
{
   uint32_t input_buffer_mode;
   /**< Parameter to configure if module should buffer the input as long as input is available,
      this is recommended for real time input or module should try to buffer the input when output is requested, this is
      recommended for non-real time input and real time output. Drift reporting is disabled for
      #JBM_BUFFER_INPUT_AT_INPUT_TRIGGER. */

   /**< @h2xmle_description { parameter to configure if module should buffer the input as long as input is available,
                              this is recommended for real time input or module should try to buffer the input when output is requested, this  is
                              recommended for non-real time input and real time output. Drift reporting is disabled for
                              #JBM_BUFFER_INPUT_AT_INPUT_TRIGGER. }
        @h2xmle_default     {JBM_BUFFER_INPUT_AT_OUTPUT_TRIGGER}
        @h2xmle_rangeList   {"buffer_at_input_trigger" = JBM_BUFFER_INPUT_AT_INPUT_TRIGGER,
                             "buffer_at_output_trigger" = JBM_BUFFER_INPUT_AT_OUTPUT_TRIGGER}*/
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct param_id_jitter_buf_input_buffer_mode_t param_id_jitter_buf_input_buffer_mode_t;


/** @h2xmlp_parameter   {"PARAM_ID_JITTER_BUF_STATE_PROP_CONFIG",
                          PARAM_ID_JITTER_BUF_STATE_PROP_CONFIG}
    @h2xmlp_description { This param ID is used to configure the state propagation in input buffer trigger mode. }
    @h2xmlp_toolPolicy  {CALIBRATION} */

/** @ingroup ar_spf_mod_jitter_buf_macros
    Configure the state propagation in input buffer trigger mode. */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_jitter_buf_state_prop_config_t
{
   uint32_t allow_state_propagation;
   /**< Parameter to is used to configure the output state propagation in input buffer trigger mode 
        by default state propagation will be blocked for input buffer trigger mode
    */

   /**< @h2xmle_description { Parameter to is used to configure the output state propagation in input buffer trigger mode 
                              by default state propagation will be blocked for input buffer trigger mode }
        @h2xmle_default     {FALSE}
        @h2xmle_rangeList   {"FALSE"=0, "TRUE"=1}
        @h2xmle_policy      {Advanced} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

typedef struct param_id_jitter_buf_state_prop_config_t param_id_jitter_buf_state_prop_config_t;

/*------------------------------------------------------------------------------
   Module
------------------------------------------------------------------------------*/

/**  @ingroup ar_spf_mod_jitter_buf_macros
     The Jitter Buffer module acts as a delay buffer between BT decoder and sink.

    @subhead4{Supported parameter IDs}
    - #PARAM_ID_JITTER_BUF_CONFIG @lstsp1
    - #PARAM_ID_JITTER_BUF_SETTLING_TIME @lstsp1
    - #PARAM_ID_JITTER_BUF_SIZE_CONFIG

    @subhead4{Supported input media format ID}
    - Data Format          : FIXED_POINT @lstsp1
    - fmt_id               : Don't care @lstsp1
    - Sample Rates         : 44100, 48000 @lstsp1
    - Number of channels   : 1 and 2 @lstsp1
    - Channel type         : 1 to 63 @lstsp1
    - Bits per sample      : 16, 24 @lstsp1
    - Q format             : 15 for bps = 16 and 27 for bps = 24 supported bps @lstsp1
    - Interleaving         : de-interleaved unpacked @lstsp1
    - Signed/unsigned      : Signed.
*/
#define MODULE_ID_JITTER_BUFFER                     0x07010005

/**
    @h2xmlm_module         {"MODULE_ID_JITTER_BUFFER", MODULE_ID_JITTER_BUFFER}
    @h2xmlm_displayName    {"Jitter Buffer Module"}

    @h2xmlm_description    { This module acts a delay buffer between BT decoder and sink.\n

    \n This module supports the following parameter IDs:\n
     - PARAM_ID_JITTER_BUF_CONFIG\n
     - PARAM_ID_JITTER_BUF_SETTLING_TIME\n
     - PARAM_ID_JITTER_BUF_SIZE_CONFIG\n

   \n Supported Input Media Format:\n
*  - Data Format          : FIXED_POINT\n
*  - fmt_id               : Don't care\n
*  - Sample Rates         : 44100, 48000\n
*  - Number of channels   : 1 and 2\n
*  - Channel type         : 1 to 63\n
*  - Bits per sample      : 16, 24  \n
*  - Q format             : 15 for bps = 16 and 27 for bps = 24\n
supported bps
*  - Interleaving         : de-interleaved unpacked\n
*  - Signed/unsigned      : Signed }

    @h2xmlm_dataMaxInputPorts    { 1 }
    @h2xmlm_dataMaxOutputPorts   { 1 }
    @h2xmlm_ctrlDynamicPortIntent  { "Jitter Buf - SS" = INTENT_ID_TIMER_DRIFT_INFO,
                                 maxPorts= 1 }
    @h2xmlm_supportedContTypes  { APM_CONTAINER_TYPE_GC }
    @h2xmlm_stackSize            { 1024 }
    @{                     <-- Start of the Module -->

    @h2xml_Select      {param_id_jitter_buf_config_t}
    @h2xmlm_InsertParameter

    @h2xml_Select      {param_id_jitter_buf_settling_time_t}
    @h2xmlm_InsertParameter

    @h2xml_Select      {param_id_jitter_buf_size_config_t}
    @h2xmlm_InsertParameter

    @h2xml_Select      {param_id_jitter_buf_input_buffer_mode_t}
    @h2xmlm_InsertParameter

 @}                     <-- End of the Module --> */

#endif /*__JITTER_BUF_H*/
