#ifndef __HISTORY_BUFFER_API_H__
#define __HISTORY_BUFFER_API_H__

/**
 * \file history_buffer_api.h
 *
 * \brief
 *
 *     History Buffer API for Voice UI
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "module_cmn_api.h"
#include "detection_cmn_api.h"
#include "imcl_fwk_intent_api.h"
/**
  @h2xml_title1          {Module history buffer API}
  @h2xml_title_agile_rev {Module history buffer API}
  @h2xml_title_date      {May 19, 2025}
*/

/*==============================================================================
   Constants
==============================================================================*/

/* History buffer is a sink module so it has only input port.*/
#define HISTORY_BUFFER_MAX_INPUT_PORTS                   1

/* History buffer is a sink module so it doesn't have an output port */
#define HISTORY_BUFFER_MAX_OUTPUT_PORTS                  0

/* History buffer has a control port which can be connected to other modules, which
   expects the FTRT data info or any other information from History Buffer.
   */
#define HISTORY_BUFFER_MAX_CONTROL_PORTS                  ( 1 )

#define HISTORY_BUFFER_MAX_INTENTS_PER_CTRL_PORT                 ( 1 )

/* Stack size requirement(in bytes) is a static module property. It is the peak stack memory
   requirement of the module. This is accounted only for the module, do not have to include
   framework requirement.*/
#define HISTORY_BUFFER_STACK_SIZE_REQUIREMENT             4096 /* Bytes */

/*==============================================================================
   Param ID
==============================================================================*/

/* Parameter to trigger the detection event */
#define PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER 			0x08001B7E

/**@h2xmlp_emptyParameter {"PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER",
                           PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER}
    @h2xmlp_description   {Sets the History buffer parameter to trigger the
                           new detection event.}
    @h2xmlp_toolPolicy    {NO_SUPPORT} */

/*Parameter to configure the Streaming mode requirement in History Buffer Module*/
#define PARAM_ID_HISTORY_BUFFER_MODE                     0x08001B87

/*==============================================================================
   Type Definitions
==============================================================================*/

#define HISTORY_BUFFER_ON_DEMAND  0
   /* In this mode History buffer sends the data, equal to DAM buffer size to the
    * client. If FTRT buffer drains, data flow switches to RT mode.*/

#define HISTORY_BUFFER_BATCHING  1
   /* In this mode history buffer sends the data to the client in batches till reset
    * i.e. data sent only after batch amount of data is accumulated in the dam.*/

typedef struct param_id_history_buffer_mode_t param_id_history_buffer_mode_t;
/** @h2xmlp_parameter   {"PARAM_ID_HISTORY_BUFFER_MODE",
                          PARAM_ID_HISTORY_BUFFER_MODE}
    @h2xmlp_description { Parameter to set the out data flow mode. History buffer module
                          supports two modes for data flow from DAM to the client
                          On_demand and Batching. }
    @h2xmlp_toolPolicy  { Calibration} */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_history_buffer_mode_t
{
   uint32_t data_flow_mode;
   /**< @h2xmle_description  {0: HISTORY_BUFFER_ON_DEMAND sends the data, equal to DAM buffer.
                              1: HISTORY_BUFFER_BATCHING sends the data to the client in batches.}
        @h2xmle_rangeList    {"HISTORY_BUFFER_ON_DEMAND"=0, "HISTORY_BUFFER_BATCHING"=1}
        @h2xmle_policy       {Basic}
        @h2xmle_default      {0} */

   uint32_t batch_size_ms;
   /**< @h2xmle_description {Size of the batch (in milli secs) for sending the data
                             to client, applicable only for batching mode and don't care
                             for on_demand mode.}
        @h2xmle_policy      {Basic}
        @h2xmle_default     {0} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;

/*------------------------------------------------------------------------------
   Module ID
------------------------------------------------------------------------------*/

/* History Buffer module ID */
#define MODULE_ID_HISTORY_BUFFER									0x07001182

/**
    @h2xmlm_module      {"MODULE_ID_HISTORY_BUFFER", MODULE_ID_HISTORY_BUFFER}
    @h2xmlm_displayName {"HISTORY_BUFFER"}
    @h2xmlm_description { History Buffer Module provides the history/past data
                          from the DAM module to the client on demand.
                          This module supports the following parameter IDs, \n
                        -  PARAM_ID_DETECTION_ENGINE_GENERIC_EVENT_CFG
                        -  PARAM_ID_DETECTION_ENGINE_RESET
                        -  PARAM_ID_DETECTION_ENGINE_BUFFERING_CONFIG
                        -  PARAM_ID_MODULE_ENABLE
                        -  PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER
                        -  PARAM_ID_HISTORY_BUFFER_MODE  \n
                         \n}

    @h2xmlm_dataMaxInputPorts    {HISTORY_BUFFER_MAX_INPUT_PORTS}
    @h2xmlm_dataMaxOutputPorts   {HISTORY_BUFFER_MAX_OUTPUT_PORTS}
    @h2xmlm_ctrlDynamicPortIntent   { "DAM-HB Control" = INTENT_ID_AUDIO_DAM_DETECTION_ENGINE_CTRL, maxPorts= 1 }
    @h2xmlm_supportedContTypes   {APM_CONTAINER_TYPE_GC }
    @h2xmlm_stackSize            {HISTORY_BUFFER_STACK_SIZE_REQUIREMENT }

    @h2xmlm_ToolPolicy           {Calibration}
    @{                   <-- Start of the Module -->

    @h2xml_Select               { param_id_module_enable_t }
    @h2xmlp_description         { This is a common param, shared by other modules.
                                  It is used to enable/disable the module runtime.
                                  This is an optional param, if the module doesn't
                                  need this param it can be removed. }
    @h2xmlm_InsertParameter
    @h2xmlp_toolPolicy          { Calibration, RTC }
    @h2xml_Select               {param_id_module_enable_t::enable}
    @h2xmle_default             {1}

    @h2xml_Select               {param_id_detection_engine_generic_event_cfg_t}
    @h2xmlm_InsertParameter
    @h2xmlp_description         { Configures the payload of the detection events.
                                  History Buffer supports only FTRT event.}


    @h2xml_Select               {param_id_detection_engine_buffering_config_t}
    @h2xmlm_InsertParameter
    @h2xmlp_description         { Set the buffering required by History Buffer module. Resizes
                                  the Audio Dam buffers via IMCL. pre_roll_duration_in_ms is
                                  don't care for history buffer module.}

    @h2xml_Select               {PARAM_ID_DETECTION_ENGINE_RESET}
    @h2xmlm_InsertParameter

    @h2xml_Select               {PARAM_ID_HISTORY_BUFFER_DETECTION_TRIGGER}
    @h2xmlm_InsertParameter

    @h2xml_Select               {param_id_history_buffer_mode_t}
    @h2xmlm_InsertParameter

   @}                   <-- End of the Module -->*/

#endif // #ifndef __HISTORY_BUFFER_API_H__
