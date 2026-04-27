#ifndef ALSA_DEVICE_API_H
#define ALSA_DEVICE_API_H
/**
 * \file alsa_device_api.h
 *
 * \brief alsa_device_api.h: This file contains the Module Id, Param IDs and
 * configuration structures exposed by the ALSA Device Sink and Source Modules.
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hw_intf_cmn_api.h"

 /** @h2xml_title1           {ALSA Device API}
     @h2xml_title_agile_rev  {ALSA Device API}
     @h2xml_title_date       {June 10, 2024} */

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_alsa_device_macros
    Input port ID of ALSA Device module */
#define PORT_ID_ALSA_DEVICE_INPUT   0x2

/** @ingroup ar_spf_alsa_device_macros
    Output port ID of ALSA Device module */
#define PORT_ID_ALSA_DEVICE_OUTPUT  0x1

#define ALSA_DEVICE_STACK_SIZE 2048

#define PARAM_ID_ALSA_DEVICE_INTF_CFG 0x08FFFFF3
/** @h2xmlp_parameter   {"PARAM_ID_ALSA_DEVICE_INTF_CFG", PARAM_ID_ALSA_DEVICE_INTF_CFG}
    @h2xmlp_description {Configures the ALSA Device interface.}
   @h2xmlp_toolPolicy              {Calibration} */

#include "spf_begin_pack.h"
/** Payload for parameter PARAM_ID_ALSA_DEVICE_INTF_CFG */
struct param_id_alsa_device_intf_cfg_t
{
     uint32_t                card_id;
       /**< @h2xmle_description {Card ID for ALSA}
            @h2xmle_range       {0..4294967295}
            @h2xmle_default     {0}
     */
     uint32_t                  device_id;
     /**< @h2xmle_description {Device ID for ALSA}
            @h2xmle_range       {0..4294967295}
            @h2xmle_default     {0}
     */
     uint32_t                  period_count;
     /**< @h2xmle_description {period_count value.}
          @h2xmle_range       {1..4294967295}
          @h2xmle_default     {2}
     */

     uint32_t                  start_threshold;
     /**< @h2xmle_description {start_threshold value.}
          @h2xmle_range       {0..4294967295}
          @h2xmle_default     {0}
     */

     uint32_t                  stop_threshold;
     /**< @h2xmle_description {stop_threshold value.}
          @h2xmle_range       {0..4294967295}
          @h2xmle_default     {0}
     */

     uint32_t                  silence_threshold;
     /**< @h2xmle_description {silence_threshold value.}
          @h2xmle_range       {0..4294967295}
          @h2xmle_default     {0}
     */

}
#include "spf_end_pack.h"
;

/* Structure type def for above payload. */
typedef struct param_id_alsa_device_intf_cfg_t param_id_alsa_device_intf_cfg_t;

/*------------------------------------------------------------------------------
   Module
------------------------------------------------------------------------------*/

#define MODULE_ID_ALSA_DEVICE_SINK 0x18000002

/** @h2xmlm_module       {"MODULE_ID_ALSA_DEVICE_SINK",
                           MODULE_ID_ALSA_DEVICE_SINK}
    @h2xmlm_displayName  {"ALSA Device Sink"}
    @h2xmlm_modSearchKeys{hardware}
    @h2xmlm_description  {ALSA_DEVICE Sink Module\n
                        - Supports following params:
                          - PARAM_ID_ALSA_DEVICE_INTF_CFG \n
                          - PARAM_ID_HW_EP_MF_CFG \n
                          - PARAM_ID_HW_EP_FRAME_SIZE_FACTOR \n
                          - \n
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
                          - Interleaving         : ALSA Device sink module needs de-interleaved unpacked or interleaved }
    @h2xmlm_dataInputPorts      {IN = PORT_ID_ALSA_DEVICE_INPUT}
    @h2xmlm_dataMaxInputPorts   {1}
    @h2xmlm_dataMaxOutputPorts  {0}
    @h2xmlm_supportedContTypes { APM_CONTAINER_TYPE_GC }
    @h2xmlm_isOffloadable       {false}
    @h2xmlm_stackSize           { ALSA_DEVICE_STACK_SIZE }
    @{                   <-- Start of the Module -->

    @h2xml_Select     {param_id_hw_ep_mf_t}
    @h2xmlm_InsertParameter
    @h2xml_Select        {param_id_hw_ep_mf_t::num_channels}
    @h2xmle_rangeList    {"ONE"=1;
                         "TWO"=2;
                         "THREE"=3;
                         "FOUR"=4;
                         "FIVE"=5;
                         "SIX"=6;
                         "SEVEN"=7;
                         "EIGHT"=8}
    @h2xmle_default      {1}
    @h2xml_Select        {param_id_hw_ep_mf_t::data_format}
    @h2xmle_rangeList    {"DATA_FORMAT_FIXED_POINT"=1}
    @h2xmle_default      {1}

    @h2xml_Select     {param_id_frame_size_factor_t}
    @h2xmlm_InsertParameter
    @h2xml_Select     {param_id_alsa_device_intf_cfg_t}
    @h2xmlm_InsertParameter
    @h2xml_Select        {param_id_alsa_device_intf_cfg_t::card_id}
    @h2xmle_range       {0..4294967295}
    @h2xmle_default     {0}
    @h2xml_Select        {param_id_alsa_device_intf_cfg_t::device_id}
    @h2xmle_range       {0..4294967295}
    @h2xmle_default     {0}
    @}                   <-- End of the Module -->*/

#define MODULE_ID_ALSA_DEVICE_SOURCE 0x18000003

/** @h2xmlm_module       {"MODULE_ID_ALSA_DEVICE_SOURCE",
                           MODULE_ID_ALSA_DEVICE_SOURCE}
    @h2xmlm_displayName  {"ALSA Device Source"}
    @h2xmlm_modSearchKeys{hardware}
    @h2xmlm_description  {ALSA_DEVICE Source Module\n
                        - Supports following params:
                          - PARAM_ID_ALSA_DEVICE_INTF_CFG \n
                          - PARAM_ID_HW_EP_MF_CFG \n
                          - PARAM_ID_HW_EP_FRAME_SIZE_FACTOR \n
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
                          - Interleaving         : ALSA Device source module needs de-interleaved unpacked or interleaved }
    @h2xmlm_dataOutputPorts      {OUT = PORT_ID_ALSA_DEVICE_OUTPUT}
    @h2xmlm_dataMaxInputPorts   {0}
    @h2xmlm_dataMaxOutputPorts  {1}
    @h2xmlm_supportedContTypes { APM_CONTAINER_TYPE_GC }
    @h2xmlm_isOffloadable       {false}
    @h2xmlm_stackSize           { ALSA_DEVICE_STACK_SIZE }
    @{                   <-- Start of the Module -->

    @h2xml_Select     {param_id_hw_ep_mf_t}
    @h2xmlm_InsertParameter
    @h2xml_Select        {param_id_hw_ep_mf_t::num_channels}
    @h2xmle_rangeList    {"ONE"=1;
                         "TWO"=2;
                         "THREE"=3;
                         "FOUR"=4;
                         "FIVE"=5;
                         "SIX"=6;
                         "SEVEN"=7;
                         "EIGHT"=8}
    @h2xmle_default      {1}
    @h2xml_Select        {param_id_hw_ep_mf_t::data_format}
    @h2xmle_rangeList    {"DATA_FORMAT_FIXED_POINT"=1}
    @h2xmle_default      {1}

    @h2xml_Select     {param_id_frame_size_factor_t}
    @h2xmlm_InsertParameter
    @h2xml_Select     {param_id_alsa_device_intf_cfg_t}
    @h2xmlm_InsertParameter
    @h2xml_Select        {param_id_alsa_device_intf_cfg_t::card_id}
    @h2xmle_range       {0..4294967295}
    @h2xmle_default     {0}
    @h2xml_Select        {param_id_alsa_device_intf_cfg_t::device_id}
    @h2xmle_range       {0..4294967295}
    @h2xmle_default     {0}
    @}                   <-- End of the Module -->*/

#endif /* ALSA_DEVICE_API_H */
