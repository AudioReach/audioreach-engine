#ifndef _AUDIO_DAM_BUFFER_API_H_
#define _AUDIO_DAM_BUFFER_API_H_
/**
 * \file audio_dam_buffer_api.h
 * \brief 
 *  	 This file contains Public APIs for Audio Dam Buffer module.
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "apm_graph_properties.h"
#include "imcl_fwk_intent_api.h"
#include "module_cmn_api.h"

#define AUDIO_DAM_STACK_SIZE_REQUIREMENT       2048

/*==============================================================================
   Constants
==============================================================================*/
/* Global unique Module ID definition
   Module library is independent of this number, it defined here for static
   loading purpose only */
#define MODULE_ID_AUDIO_DAM_BUFFER                       0x07001020
#define AUDIO_DAM_MAX_CHANNELS_PER_STREAM 	    16
/*==============================================================================
   Constants
==============================================================================*/
/** 
    @h2xml_title1          {Audio Dam buffer Module API}
    @h2xml_title_agile_rev {Audio Dam buffer Module API}
    @h2xml_title_date      {April 4, 2019} */

/**
    @h2xmlm_module            	 { "MODULE_ID_AUDIO_DAM_BUFFER", MODULE_ID_AUDIO_DAM_BUFFER}
    @h2xmlm_displayName       	 { "Audio Dam Buffer" }
	@h2xmlm_modSearchKeys        {VoiceUI}
    @h2xmlm_description       	 { Supports the multi port stream buffering }
    @h2xmlm_supportedContTypes  { APM_CONTAINER_TYPE_GC }
    @h2xmlm_isOffloadable        {false}
    @h2xmlm_stackSize            { AUDIO_DAM_STACK_SIZE_REQUIREMENT }
    @h2xmlm_toolPolicy           { Calibration }
    @h2xmlm_dataMaxInputPorts    { INFINITE }
	@h2xmlm_dataMaxOutputPorts   { INFINITE }
	  @h2xmlm_ctrlDynamicPortIntent  { "DAM-DE Control" = INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL,
								   maxPorts= INFINITE}

    @{                  		 <-- Start of the Module -->
 */

/*==============================================================================
   Constants
==============================================================================*/
/* ID of the parameter used to input port configuration of the module. */
#define PARAM_ID_AUDIO_DAM_INPUT_PORTS_CFG               0x08001058

/*==============================================================================
   Type definitions
==============================================================================*/

/* Sub struct forward deceleration. */
typedef struct audio_dam_input_port_cfg_t audio_dam_input_port_cfg_t;

/** @h2xmlp_subStruct
    @h2xmlp_description  {The input port configuration consists of number of channels expected on this
                         input port stream. The number of channels should match with the input
                         media format. The channel IDs define the explicit channel IDs for
                         each channel.}
    @h2xmlp_toolPolicy   {Calibration} */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct audio_dam_input_port_cfg_t
{
   uint32_t                input_port_id;
   /**< @h2xmle_description { Input port ID on which the following configuration needs to be set. }
        @h2xmle_default     { 0 } */

   uint32_t                num_channels;
   /**< @h2xmle_description { Number of channels in the input port stream.}
        @h2xmle_range       {0.. AUDIO_DAM_MAX_CHANNELS_PER_STREAM }
        @h2xmle_default     {0} */
#if defined(__H2XML__)
   uint32_t                channel_ids[0];
   /**< @h2xmle_description { Defines channel IDs for each of the channel index.
                              Range is defined between 1 to 128 .  Value '0' is considered
                              invalid and must not be set. }
        @h2xmle_range       { 0.. MODULE_CMN_MAX_CHANNEL_TYPE }
        @h2xmle_variableArraySize  { "num_channels" } */
#endif

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/* Payload of the PARAM_ID_AUDIO_DAM_INPUT_PORTS_CFG command. */
typedef struct param_id_audio_dam_input_ports_cfg_t param_id_audio_dam_input_ports_cfg_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_INPUT_PORTS_CFG",
                          PARAM_ID_AUDIO_DAM_INPUT_PORTS_CFG}
    @h2xmlp_description {Sets the input port configuration for all the input ports of the audio dam buffer. 

                         Each input port configuration consists of number of channels expected on this
                         input port stream. The number of channels should match with the input
                         media format. The channel types define the explicit channel types for
                         each channel.} */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_input_ports_cfg_t
{
   uint32_t                     num_input_ports;
   /**< @h2xmle_description { Number of input ports connected to Audio Dam module. }
        @h2xmle_default     { 1 } */
#if defined(__H2XML__)
   audio_dam_input_port_cfg_t   input_port_cfgs[0];
   /**< @h2xmle_description {   Sub structure containing per input port configuration. The payload for
                                each input port is defined by the structure audio_dam_input_port_cfg_t. }
        @h2xmle_range       { 0.. MODULE_CMN_MAX_CHANNEL_TYPE }
        @h2xmle_variableArraySize  { "num_input_ports" } */
#endif

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;


/*==============================================================================
   Constants
==============================================================================*/
/* ID of the parameter used to output port configuration of the module. */
#define PARAM_ID_AUDIO_DAM_OUTPUT_PORTS_CFG               0x08001059

/*==============================================================================
   Type definitions
==============================================================================*/
/* Sub struct forward deceleration. */
typedef struct channel_map_t channel_map_t;

/** @h2xmlp_subStruct
    @h2xmlp_description  {Each channel being buffered in Audio Dam buffer is uniquely
                          identified by a input Channel ID. This structure contains the
                          (input Channel ID -> output Channel ID) pair.

                          The output channel ID defines the channel type that must be
                          propagated downstream for the given input channel ID.}
    @h2xmlp_toolPolicy   {Calibration} */
#include "spf_begin_pack.h"
struct channel_map_t
{
   uint32_t                input_ch_id;
   /**< @h2xmle_description { Channel type as expected from the source module.}
        @h2xmle_range       {1.. MODULE_CMN_MAX_CHANNEL_TYPE }
        @h2xmle_default     { 1} */

   uint32_t                output_ch_id;
   /**< @h2xmle_description { Defines the type of the channel in the output stream.
                              This will be the channel type propagated downstream.}
        @h2xmle_range       {1.. MODULE_CMN_MAX_CHANNEL_TYPE }
        @h2xmle_default     { 1} */
}
#include "spf_end_pack.h"
;

/* Payload of the each output port configuration. */
typedef struct audio_dam_output_port_cfg_t audio_dam_output_port_cfg_t;

/** @h2xmlp_subStruct
    @h2xmlp_description  {The input port configuration consists of number of channels expected on this
                         input port stream. The number of channels should match with the input
                         media format. The channel IDs define the explicit channel IDs for
                         each channel.}
    @h2xmlp_toolPolicy   {Calibration} */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct audio_dam_output_port_cfg_t
{
   uint32_t                 output_port_id;
   /**< @h2xmle_description { Output port ID on which the following configuration needs to be set. }
        @h2xmle_default     { 0 } */

   uint32_t                 num_channels;
   /**< @h2xmle_description { Number of channels in the port stream.}
		@h2xmle_range       {0.. AUDIO_DAM_MAX_CHANNELS_PER_STREAM }
        @h2xmle_default     { 0 } */
#if defined(__H2XML__)
   channel_map_t            output_ch_map[0];
   /**< @h2xmle_description { Output channel map array. Each element in the array selects the output channels source 
                              based on (Input port ID , Input channel type) and output channel type sets the channel 
                              type that must be propagated to the downstream. }
        @h2xmle_variableArraySize  { "num_channels" } */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;


/* Payload of the PARAM_ID_AUDIO_DAM_OUTPUT_PORTS_CFG command. */
typedef struct param_id_audio_dam_output_ports_cfg_t param_id_audio_dam_output_ports_cfg_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_OUTPUT_PORTS_CFG",
                          PARAM_ID_AUDIO_DAM_OUTPUT_PORTS_CFG}
    @h2xmlp_description {Sets the output port configuration of all the output ports connected to Audio Dam module.

                         Each The output channel map defines the list of the channels from input streams to the module.
                         Output channel map is represented as a variable array of (Input channel ID -> output channel ID)
                         vectors. }
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_output_ports_cfg_t
{
   uint32_t                              num_output_ports;
   /**< @h2xmle_description { Number of output ports connected to Audio Dam module.}
        @h2xmle_default     { 0 } */
#if defined(__H2XML__)
   audio_dam_output_port_cfg_t           output_port_cfgs[0];
   /**< @h2xmle_description { Per output port configuration. The payload for each output port is defined by the
                              structure audio_dam_output_port_cfg_t. }
        @h2xmle_variableArraySize  { "num_output_ports" } */
#endif

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;




/*==============================================================================
   Constants
==============================================================================*/

/*  Parameter to set maximum duration required to setup the downstream subgraphs after
    the keyword is detected by the detection engine. This is time taken by the HLOS
    client to setup the downstream after receiving detection. This defines the minimum
    amount of buffering required to avoid data loss, before HLOS sets up downstream sub
    graph and starts reading the buffered data. */
#define PARAM_ID_AUDIO_DAM_DOWNSTREAM_SETUP_DURATION           0x0800105A

/*==============================================================================
   Type Definitions
==============================================================================*/


/* Sub structure to configure per port downstream setup duration. */
typedef struct  audio_dam_downstream_setup_duration_t  audio_dam_downstream_setup_duration_t;

/** @h2xmlp_subStruct
    @h2xmlp_description  {This structure defines per output port downstream setup duration time.

                          This is time taken by the HLOS client to setup the downstream after receiving detection.
                          This defines the minimum amount of buffering required to avoid data loss, before HLOS
                          sets up downstream subgraph and starts reading the buffered data. }
    @h2xmlp_toolPolicy   {Calibration} */
#include "spf_begin_pack.h"
struct audio_dam_downstream_setup_duration_t
{
   uint32_t                output_port_id;
   /**< @h2xmle_description { Output port ID on which the following configuration needs to be set. }
        @h2xmle_default     { 0 } */

   uint32_t                dwnstrm_setup_duration_ms;
   /**< @h2xmle_description {  Maximum duration required to setup the downstream subgraphs after
                               the keyword is detected. }
        @h2xmle_default     { 250 } */
}
#include "spf_end_pack.h"
;

/* Structure to configure downstream setup duration. */
typedef struct  param_id_audio_dam_downstream_setup_duration_t  param_id_audio_dam_downstream_setup_duration_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_DOWNSTREAM_SETUP_DURATION",
                         PARAM_ID_AUDIO_DAM_DOWNSTREAM_SETUP_DURATION}
    @h2xmlp_description {This parameter contains downstream setup duration config for all output ports.

                         Down stream setup duration is the time taken by the HLOS client to setup the downstream
                         subgraph and start reading the data from dam module, upon receiving detection. 

                         This defines the minimum amount of buffering required to avoid data loss, before HLOS 
                         sets up downstream subgraph and starts reading the buffered data. }*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_dam_downstream_setup_duration_t
{
   uint32_t                                 num_output_ports;
   /**< @h2xmle_description { Number of output ports defined for the Audio Dam module.}
        @h2xmle_default     { 0 } */
#if defined(__H2XML__)
   audio_dam_downstream_setup_duration_t    port_cfgs[0];
   /**< @h2xmle_description { Per port downstream setup duration config. }
        @h2xmle_variableArraySize  { "num_output_ports" } */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/*==============================================================================
   Constants
==============================================================================*/

/* Parameter used by the to map the control link ID to the output data port ID. 
*/
#define PARAM_ID_AUDIO_DAM_CTRL_TO_DATA_PORT_MAP          0x0800105B

/*==============================================================================
   Type Definitions
==============================================================================*/

/** Sub structure forward declaration. */
typedef struct audio_dam_port_binding_t audio_dam_port_binding_t;

/** @h2xmlp_subStruct
    @h2xmlp_description  { This structure defines each of the control port to output data port mappings.
                           Each output port ID must be linked to only one control port ID. }
    @h2xmlp_toolPolicy   { Calibration } */
#include "spf_begin_pack.h"
struct audio_dam_port_binding_t
{
   uint32_t                control_port_id;
   /**< @h2xmle_description { Control port ID of the module.}
        @h2xmle_default     { 0} */

   uint32_t                output_port_id;
   /**< @h2xmle_description { The output port ID to which control port is linked to. }
        @h2xmle_default     { 0} */
}
#include "spf_end_pack.h"
;

/* Structure to define the Audio Dam buffer adjust */
typedef struct param_id_audio_ctrl_to_data_port_map_t param_id_audio_ctrl_to_data_port_map_t;

/** @h2xmlp_parameter   {"PARAM_ID_AUDIO_DAM_CTRL_TO_DATA_PORT_MAP",
                          PARAM_ID_AUDIO_DAM_CTRL_TO_DATA_PORT_MAP}
    @h2xmlp_description { This param ID defines the control ID to data port binding. Each control port ID
                          in Dam module must be coupled to an output port ID it needs to control. Each control
                          port can be coupled to only one output port ID, hence number of bindings is same as
                          number of control ports.} */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_audio_ctrl_to_data_port_map_t
{
   uint32_t               num_output_ports;
   /**< @h2xmle_description { Number of control port IDs attached to the module.}
        @h2xmle_default     {0} */
#if defined(__H2XML__)
   audio_dam_port_binding_t        ctrl_to_outport_map[0];
   /**< @h2xmle_description { Control port to Data port map. }
        @h2xmle_variableArraySize  { "num_output_ports" } */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/*==============================================================================
   Constants
==============================================================================*/

 /**
 * PARAM_ID_AUDIO_DAM_HANDLE_BATCH_END_TRACKING_EVENT
 *
 * DAM sends this parameter to downstream as part of the tracking metadata payload.
 * After consuming the batch, downstream raise a GPR tracking event back to inform
 * about batch consumption through this parameter.
 *
 */
#define PARAM_ID_AUDIO_DAM_HANDLE_BATCH_END_TRACKING_EVENT         0x08001BD3


/** @} <-- End of the module -- > */

#endif /* _AUDIO_DAM_BUFFER_API_H_ */
