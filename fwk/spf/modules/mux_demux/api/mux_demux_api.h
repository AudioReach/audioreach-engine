#ifndef MUX_DEMUX_H_
#define MUX_DEMUX_H_
/**
 * \file mux_demux_api.h
 * \brief
 *    This file contains APIs for mux_demux module.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*# @h2xml_title1          {Muxing and Demuxing Module}
    @h2xml_title_agile_rev {Muxing and Demuxing Module}
    @h2xml_title_date      {October, 2019} */

/*==============================================================================
   Include Files
==============================================================================*/
#include "module_cmn_api.h"

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_mod_mux_demux_mod
    Stack size of the module. */
#define MUX_DEMUX_STACK_SIZE         2048

/*==============================================================================
   Param ID
==============================================================================*/

#ifdef SIM
/** Param ID to Raise Trigger policy for MUX DEMUX module on SIM*/
#define PARAM_ID_MUX_DEMUX_TP_CFG 0x08001368
#endif

/** @ingroup ar_spf_mod_mux_demux_mod
    Identifier for the parameter that configures mux and demux at the channel
    level across input-to-output streams.

    @msgpayload
    param_id_mux_demux_config_t \n
    @indent{12pt} mux_demux_connection_config_t
*/
#define PARAM_ID_MUX_DEMUX_CONFIG 0x080011BD

/*==============================================================================
   Param structure definitions
==============================================================================*/

/*# @h2xmlp_subStruct
    @h2xmlp_description {Defines a connection from an input port to an output
                         port.} */

/** @ingroup ar_spf_mod_mux_demux_mod
    Payload for #PARAM_ID_MUX_DEMUX_CONFIG. This structure defines a connection
    from an input port to an output port.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct mux_demux_connection_config_t
{
   uint32_t input_port_id;
   /**< Unique identifier for the input port. */

   /*#< @h2xmle_description {Unique ID of the input port.} */

   uint32_t input_channel_index;
   /**< Unique identifier for the input channel.

        @values 0 through 127 */

   /*#< @h2xmle_description {Unique ID of the input channel.}
        @h2xmle_range       {0..127} */

   uint32_t output_port_id;
   /**< Unique identifier for the output port. */

   /*#< @h2xmle_description {Unique ID of the output port.} */

   uint32_t output_channel_index;
   /**< Unique identifier for the output channel. @newpagetable

        @values 0 through 127 */

   /*#< @h2xmle_description {Unique ID of the output channel.}
        @h2xmle_range       {0..127} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct mux_demux_connection_config_t mux_demux_connection_config_t;

/*# @h2xmlp_parameter   {"PARAM_ID_MUX_DEMUX_CONFIG",PARAM_ID_MUX_DEMUX_CONFIG}
    @h2xmlp_description {ID for the parameter that configures mux and demux at
                         the channel level across input-to-output streams.}
    @h2xmlp_toolPolicy  {Calibration; RTC} */

/** @ingroup ar_spf_mod_mux_demux_mod
    Payload for #PARAM_ID_MUX_DEMUX_CONFIG. This structure is an array of
    connections.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_mux_demux_config_t
{
   uint32_t num_of_connections;
   /**< Number of connections from input to output across all streams. */

   /*#< @h2xmle_description {Number of connections from input to output across
                             all streams.} */

#ifdef __H2XML__
   mux_demux_connection_config_t connection_arr[0];
   /**< Array of connections of size num_of_connections. */

   /*#< @h2xmle_description       {Array of connections of size
                                   num_of_connections.}
        @h2xmlx_expandStructs     {true}
        @h2xmle_variableArraySize {num_of_connections} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct param_id_mux_demux_config_t param_id_mux_demux_config_t;

/*==============================================================================
   Param ID
==============================================================================*/

/** @ingroup ar_spf_mod_mux_demux_mod
    Identifier for the parameter that configures the media format of an output
    port.

    @msgpayload
    param_id_mux_demux_out_format_t \n
    @indent{12pt} mux_demux_out_format_t
*/
#define PARAM_ID_MUX_DEMUX_OUT_FORMAT 0x080011BE

/*==============================================================================
   Param structure defintions
==============================================================================*/
/*# @h2xmlp_subStruct
    @h2xmlp_description {Sets the media format of an output port.} */

/** @ingroup ar_spf_mod_mux_demux_mod
    Parameter for #PARAM_ID_MUX_DEMUX_OUT_FORMAT. This structure sets the media
    format of an output port.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct mux_demux_out_format_t
{
   uint32_t output_port_id;
   /**< Unique identifier for the output port. */

   /*#< @h2xmle_description {Unique ID of the output port.} */

   uint16_t bits_per_sample;
   /**< Bits per sample at the output port.

        @values 16, 32 (Default = 16) */

   /*#< @h2xmle_description {Bits per sample at the output port.}
        @h2xmle_default     {16}
        @h2xmle_rangelist   {"16"=16;
                             "32"=32} */

   uint16_t q_factor;
   /**< Indicates the Q factor.

        @valuesbul
        - Q15 for 16 bits per sample (Default)
        - Q27 or Q31 for 32 bits per sample @tablebulletend */

   /*#< @h2xmle_description {Indicates the Q factor: Q15 for 16 bits per
                             sample, and Q27 or Q31 for 32 bits per sample.}
        @h2xmle_default     {15}
        @h2xmle_rangeList   {"Q15"=15;
                             "Q27"=27;
                             "Q31"=31} */

   uint32_t num_channels;
   /**< Number of channels in the array.

        @values 1 through 128 (Default = 2) */

   /*#< @h2xmle_description {Number of channels in the array.}
        @h2xmle_default     {2}
        @h2xmle_range       {1..MODULE_CMN_MAX_CHANNEL} */

#ifdef __H2XML__
   uint16_t channel_type[0];
   /**< Array of channel types of size num_channels. The size is even for
        alignment purpose. */

   /*#< @h2xmle_description       {Array of channel types of size num_channels.
                                   The size is even for alignment purpose.}
        @h2xmlx_expandStructs     {true}
        @h2xmle_variableArraySize {num_channels}
        @h2xmle_rangeEnum         {pcm_channel_map}
        @h2xmle_default           {1} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct mux_demux_out_format_t mux_demux_out_format_t;

/*# @h2xmlp_parameter   {"PARAM_ID_MUX_DEMUX_OUT_FORMAT",
                          PARAM_ID_MUX_DEMUX_OUT_FORMAT}
    @h2xmlp_description {ID for the parameter that configures the media format
                         of an output port.}
    @h2xmlp_toolPolicy  {Calibration; RTC} */

/** @ingroup ar_spf_mod_mux_demux_mod
    Payload for #PARAM_ID_MUX_DEMUX_OUT_FORMAT. This structure lists the media
    formats for the output port.
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_mux_demux_out_format_t
{
   uint32_t num_config;
   /**< Number of media formats in the array. */

   /*#< @h2xmle_description {Number of of media format configuration parameters
                             for the output port.} */

#ifdef __H2XML__
   mux_demux_out_format_t out_fmt_arr[0];
   /**< Array of output port media formats of size num_config. */

   /*#< @h2xmle_description       {Array of output port media formats of size
                                   num_config.}
        @h2xmlx_expandStructs     {false}
        @h2xmle_variableArraySize {num_config} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct param_id_mux_demux_out_format_t param_id_mux_demux_out_format_t;

/* ID of the TS Propagation parameter used by MODULE_ID_MUX_DEMUX. */
#define PARAM_ID_MUX_DEMUX_TS_PROPAGATION 0x08001AE4

/** @h2xmlp_parameter   {"PARAM_ID_MUX_DEMUX_TS_PROPAGATION", PARAM_ID_MUX_DEMUX_TS_PROPAGATION}
    @h2xmlp_description {Enable/disable TimeStamp propagation}
    @h2xmlp_toolPolicy  {Calibration}*/

/* Payload of the PARAM_ID_MUX_DEMUX_TS_PROPAGATION parameter used
 by the Mux Demux module */
/* Structure for the TS propagation parameter for Mux-Demux module. */
#include "spf_begin_pack.h"
struct param_id_mux_demux_ts_propagation_t
{
   uint32_t enable_ts_propagation;
   /**< @h2xmle_description  {Enable or disable TS propagation from input to output data buffer.\n
                              The first active input port's Timestamp is propagated to all of its connected output ports.\n
                              Discontinuity in timestamp can happen if connected input port's state changes.\n
                              In case of the first active input port goes into inactive state then then next active
                              connected input port's timestamp is propagated.\n
                              Active port means it is in started state and data is flow is going on \n}

         @h2xmle_rangeList    {"Enable"=1;
                              "Disable"=0}
         @h2xmle_default      {0} */
}
#include "spf_end_pack.h"
;

/* Structure type def for above payload. */
typedef struct param_id_mux_demux_ts_propagation_t param_id_mux_demux_ts_propagation_t;

/*------------------------------------------------------------------------------
   Module
------------------------------------------------------------------------------*/

/** @ingroup ar_spf_mod_mux_demux_mod
    Identifier for the module that provides channel routing functionality from
    multiple input stream to multiple output streams.

    If this module is in a container that does not have a signal-triggered
    module, the Synchronization (SYNC) module is required to be placed upstream
    of this module to synchronize the inputs.

    @subhead4{Supported parameter IDs}
    - #PARAM_ID_MUX_DEMUX_CONFIG @lstsp1
    - #PARAM_ID_MUX_DEMUX_OUT_FORMAT
    - #PARAM_ID_MUX_DEMUX_TS_PROPAGATION

    @subhead4{Supported input media format ID}
    - Data format       : #DATA_FORMAT_FIXED_POINT @lstsp1
    - fmt_id            : #MEDIA_FMT_ID_PCM @lstsp1
    - Sample rates      : 8000..384000 @lstsp1
    - Number of channels: 1 to 128 (for certain products this module supports only 32 channels) @lstsp1
    - Channel type      : Don't care @lstsp1
    - Bits per sample   : 16,32 @lstsp1
    - Q format          : Q15,Q27,Q31 @lstsp1
    - Interleaving      : De-interleaved unpacked @lstsp1
    - Signed/unsigned   : Signed
*/
#define MODULE_ID_MUX_DEMUX 0x07001098

/*# @h2xmlm_module             {"MODULE_ID_MUX_DEMUX", MODULE_ID_MUX_DEMUX}
    @h2xmlm_displayName        {"Muxing and Demuxing"}
    @h2xmlm_modSearchKeys      {Audio, Voice}
    @h2xmlm_description        {ID for the module that provides channel
                                routing functionality from multiple input
                                stream to multiple output streams. For more
                                details, see AudioReach Signal Processing
                                Framework (SPF) API Reference.}
    @h2xmlm_dataMaxInputPorts  {INFINITE}
    @h2xmlm_dataMaxOutputPorts {INFINITE}
    @h2xmlm_supportedContTypes {APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC, APM_CONTAINER_TYPE_PTC}
    @h2xmlm_stackSize          {MUX_DEMUX_STACK_SIZE}
    @h2xmlm_toolPolicy         {Calibration}

    @{                      <-- Start of the Module -->
    @h2xml_Select           {mux_demux_out_format_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {mux_demux_connection_config_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_mux_demux_config_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_mux_demux_out_format_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_mux_demux_ts_propagation_t}
    @h2xmlm_InsertParameter
    @}                      <-- End of the Module --> */

#endif // MUX_DEMUX_H_
