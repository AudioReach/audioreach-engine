#ifndef _VCPM_API_H_
#define _VCPM_API_H_
/**
 * \file vcpm_api.h

  \brie
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*------------------------------------------------------------------------------
 *  Module ID Definitions
 *----------------------------------------------------------------------------*/

/** @ingroup spf_vcpm_voice_cfg
    Instance identifier for the VCPM static module. */
#define VCPM_MODULE_INSTANCE_ID (0x00000004)

/* Instance identifier for the Voice Services static module. */
#define VOICE_SERVICES_MODULE_INSTANCE_ID (0x00000005)

/* TTY mode values. */

/** @ingroup spf_vcpm_tty
    TTY is Off or disabled. */
#define VOICE_TTY_MODE_OFF 0

/** @ingroup spf_vcpm_tty
    Hearing Carry Over mode. */
#define VOICE_TTY_MODE_HCO 1

/** @ingroup spf_vcpm_tty
    Voice Carry Over mode. */
#define VOICE_TTY_MODE_VCO 2

/** @ingroup spf_vcpm_tty
    HCO mode plus VCO mode. */
#define VOICE_TTY_MODE_FULL 3

/** @ingroup spf_vcpm_tty
    Identifier for the parameter that sets the TTY mode of operation.

    @msgpayload
    vcpm_param_id_tty_mode_t
 */
#define VCPM_PARAM_ID_TTY_MODE 0x080011E4

/*# @h2xmlp_parameter   {"VCPM_PARAM_ID_TTY_MODE", VCPM_PARAM_ID_TTY_MODE}
    @h2xmlp_description {ID for the parameter that sets the TTY mode of
                         operation.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup spf_vcpm_tty
    Payload for #VCPM_PARAM_ID_TTY_MODE.
 */
#include "spf_begin_pack.h"
struct vcpm_param_id_tty_mode_t
{
   uint32_t vsid;
   /**< Voice System identifier for TTY mode. */

   /*#< @h2xmle_description {Voice System ID for TTY mode.}
        @h2xmle_rangeList   {"VSID_SUB1"=0x11C05000,
                             "VSID_SUB2"=0x11DC5000,
                             "VSID_LB_SUB1"=0x12006000,
                             "VSID_LB_SUB2"=0x121C6000} */

   uint32_t mode;
   /**< TTY mode to be set.

        @valuesbul
        - #VOICE_TTY_MODE_OFF
        - #VOICE_TTY_MODE_HCO
        - #VOICE_TTY_MODE_VCO
        - #VOICE_TTY_MODE_FULL @tablebulletend @newpagetable */

   /*#< @h2xmle_description {TTY mode to be set.}
        @h2xmle_rangeList   {"VOICE_TTY_MODE_OFF"=VOICE_TTY_MODE_OFF,
                             "VOICE_TTY_MODE_HCO"=VOICE_TTY_MODE_HCO,
                             "VOICE_TTY_MODE_VCO"=VOICE_TTY_MODE_VCO,
                             "VOICE_TTY_MODE_FULL"=VOICE_TTY_MODE_FULL}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_tty_mode_t vcpm_param_id_tty_mode_t;

#define PARAM_ID_PROXY_VS_CONFIG 0x08001366

struct proxy_vs_config_t
{

   uint32_t vsid;
   /*Voice Service Identifier for vocoder loopback mode*/

   uint32_t payload_size;
   /* Size of the payload that immediately follows this structure */
};
typedef struct proxy_vs_config_t proxy_vs_config_t;

struct proxy_vs_config_payload_t
{

   uint32_t tag_id;
   /**< Identifier for the tag that the VCPM publishes.

        @values supported
        - #VOICE_MOD_TAG_ID_ENCODER
        - #VOICE_MOD_TAG_ID_TX_MAILBOX
        - #VOICE_MOD_TAG_ID_DECODER
        - #VOICE_MOD_TAG_ID_RX_MAILBOX
    */

   uint32_t param_id;
   /* Unique identifier for the module parameter configured */

   uint32_t size;
   /* Size of the parameter data based on the tag_id/param_id
   combination.

           @values > 0 bytes, in multiples of at least 4 bytes */

   /**   regular payload->  Immediately following this structure are param_size
      bytes of calibration data. The structure and size depend on the param_id.
   */
};

typedef struct proxy_vs_config_payload_t proxy_vs_config_payload_t;

/** @ingroup spf_vcpm_voice_cfg
    Identifier for the parameter that contains instance IDs of specific modules
    in a stream subgraph that is part of a voice call use case graph
    (applications processor).

    The HLOS sets this parameter to the VCPM instance ID when the graph is
    opened.

    @msgpayload
    vcpm_param_voice_config_payload_t \n
    @indent{12pt} vcpm_cfg_subgraph_properties_t \n
    @indent{12pt} vcpm_property_config_struct_t
 */
#define VCPM_PARAM_ID_VOICE_CONFIG 0x08001162

/** @ingroup spf_vcpm_tty
    Payload for #VCPM_PARAM_ID_VOICE_CONFIG.

    Immediately following this structure is an array of structures of type
    %vcpm_cfg_subgraph_properties_t. The length of the array is equal to
    num_voice_subgraph_info_objects.
*/
#include "spf_begin_pack.h"
struct vcpm_param_voice_config_payload_t
{
   uint32_t num_sub_graphs;
   /**< Number of %vcpm_cfg_subgraph_properties_t structures and payloads that
        immediately follow this structure. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_voice_config_payload_t vcpm_param_voice_config_payload_t;

/** @ingroup spf_vcpm_voice_cfg
    Payload for #VCPM_PARAM_ID_VOICE_CONFIG. This structure contains subgraph
    properties.
 */
#include "spf_begin_pack.h"
struct vcpm_cfg_subgraph_properties_t
{
   uint32_t sub_graph_id;
   /**< Identifier for the subgraph for which the information is provided. */

   uint32_t num_vcpm_properties;
   /**< Number of %vcpm_property_config_struct_t structures that follow this
        structure. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_cfg_subgraph_properties_t vcpm_cfg_subgraph_properties_t;

/** @ingroup spf_vcpm_voice_cfg
    Payload for #VCPM_PARAM_ID_VOICE_CONFIG. This structure contains property
    configuration objects.
 */
#include "spf_begin_pack.h"
struct vcpm_property_config_struct_t
{
   uint32_t property_id;
   /**< Identifier for the VCPM property. */

   uint32_t property_size;
   /**< Size of the property payload that immediately follows this
        structure. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_property_config_struct_t vcpm_property_config_struct_t;

/** @ingroup spf_vcpm_voice_cfg
    Identifier for the property that contains instance IDs of modules in
    different subgraphs that are part of a voice call use case graph
    (applications processor).

    Set this property to VCPM as part of the #VCPM_PARAM_ID_VOICE_CONFIG
    payload.

    @msgpayload
    vcpm_property_id_tag_info_payload_t \n
    @indent{12pt} vcpm_tag_miid_info_t
*/
#define VCPM_PROPERTY_ID_TAG_INFO 0x080011B2

/** @ingroup spf_vcpm_voice_cfg
    Payload for #VCPM_PROPERTY_ID_TAG_INFO.
 */
#include "spf_begin_pack.h"
struct vcpm_property_id_tag_info_payload_t
{
   uint32_t num_tag_info;
   /**< Number of %vcpm_tag_miid_info_t structures that immediately follow
        this structure.*/
}
#include "spf_end_pack.h"
;
typedef struct vcpm_property_id_tag_info_payload_t vcpm_property_id_tag_info_payload_t;

/** @ingroup spf_vcpm_voice_cfg
    Payload for #VCPM_PROPERTY_ID_TAG_INFO. This structure contains module tag
    information.
 */
#include "spf_begin_pack.h"
struct vcpm_tag_miid_info_t
{
   uint32_t tag_id;
   /**< Identifier for the tag that the VCPM publishes.

        @valuesbul
        - #VOICE_MOD_TAG_ID_ENCODER
        - #VOICE_MOD_TAG_ID_TX_MAILBOX
        - #VOICE_MOD_TAG_ID_TX_STREAM_HPCM
        - #VOICE_MOD_TAG_ID_DECODER
        - #VOICE_MOD_TAG_ID_RX_MAILBOX
        - #VOICE_MOD_TAG_ID_RX_STREAM_HPCM
        - #VOICE_MOD_TAG_ID_CTMTTY_TX
        - #VOICE_MOD_TAG_ID_LTETTY_TX
        - #VOICE_MOD_TAG_ID_CTMTTY_RX
        - #VOICE_MOD_TAG_ID_LTETTY_RX
        - #VOICE_MOD_TAG_ID_1XTTY_RX
        - #VOICE_MOD_TAG_ID_RX_TRM
        - #VOICE_MOD_TAG_ID_TX_SMART_SYNC @tablebulletend */

   uint32_t module_iid;
   /**< Identifier for the module instance that corresponds to the tag ID. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_tag_miid_info_t vcpm_tag_miid_info_t;

/* Tag IDs for the voice graph modules. */

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for the voice encoder. */
#define VOICE_MOD_TAG_ID_ENCODER 0x08001177

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for a mailbox in the Tx stream. */
#define VOICE_MOD_TAG_ID_TX_MAILBOX 0x08001178

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for Host PCM in the Tx stream. */
#define VOICE_MOD_TAG_ID_TX_STREAM_HPCM 0x08001179

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for the voice decoder. */
#define VOICE_MOD_TAG_ID_DECODER 0x0800117B

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for a mailbox in the Rx stream. */
#define VOICE_MOD_TAG_ID_RX_MAILBOX 0x0800117C

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for HPCM in the Rx stream. */
#define VOICE_MOD_TAG_ID_RX_STREAM_HPCM 0x0800117D

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for the Timed Renderer module in the Rx stream. */
#define VOICE_MOD_TAG_ID_RX_TRM 0x080011AC

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for smart synchronization in the Tx stream. */
#define VOICE_MOD_TAG_ID_TX_SMART_SYNC 0x080011AD

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for LTE TTY in the Tx stream. */
#define VOICE_MOD_TAG_ID_LTETTY_TX 0x0800117A

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for cellular text modem TTY in the Tx stream. */
#define VOICE_MOD_TAG_ID_CTMTTY_TX 0x080011E1

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for LTE TTY in the Rx stream. */
#define VOICE_MOD_TAG_ID_LTETTY_RX 0x0800117E

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for cellular text modem TTY in the Rx stream. */
#define VOICE_MOD_TAG_ID_CTMTTY_RX 0x080011E2

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for 1x TTY in the Rx stream. @newpage */
#define VOICE_MOD_TAG_ID_1XTTY_RX 0x080011E3

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for Soft Volume control in the Rx stream. @newpage */
#define VOICE_MOD_TAG_ID_STREAM_RX_SOFT_VOL 0x08001518

/** @ingroup spf_vcpm_voice_cfg
    Tag ID for Soft Volume control in the Tx stream. @newpage */
#define VOICE_MOD_TAG_ID_STREAM_TX_SOFT_VOL 0x08001519

/** @ingroup spf_vcpm_cal_tbl
    Identifier for the parameter that registers calibration tables per
    subgraph (from the applications processor).

    @msgpayload
    vcpm_param_cal_table_payload_t \n
    @indent{12pt} vcpm_sgid_cal_table_t
*/
#define VCPM_PARAM_ID_CAL_TABLE 0x08001163

/** @ingroup spf_vcpm_cal_tbl
    Payload for #VCPM_PARAM_ID_CAL_TABLE.

    Immediately following this structure is an array of structures of type
    %vcpm_sgid_cal_table_t. The array length is equal to num_sub_graphs.
*/
#include "spf_begin_pack.h"
struct vcpm_param_cal_table_payload_t
{
   uint32_t num_sub_graphs;
   /**< Number of voice subgraphs for which calibration tables are
        provided. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_cal_table_payload_t vcpm_param_cal_table_payload_t;

/** @ingroup spf_vcpm_cal_tbl
    Payload for #VCPM_PARAM_ID_CAL_TABLE. This structure contains calibration
    objects.

    Immediately following this structure is the calibration table of size
    cal_table_size.
*/
#include "spf_begin_pack.h"
struct vcpm_sgid_cal_table_t
{
   uint32_t sub_graph_id;
   /**< Subgraph identifier for the calibration table that follows this
        structure. */

   uint32_t cal_table_size;
   /**< Size of the calibration table that follows this structure.*/

   /*uint8_t cal_table[cal_table_size] */
   /* Immediately following this structure is the calibration table of size
        cal_table_size. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_sgid_cal_table_t vcpm_sgid_cal_table_t;

/** @ingroup spf_vcpm_cal_keys
    Identifier for the calibration key that identifes the vocoder class (from
    the applications processor and modem).

    This key supports the following values:
    - #VCPM_CAL_KEY_VOCODER_CLASS_AMR
    - #VCPM_CAL_KEY_VOCODER_CLASS_EVRC
    - #VCPM_CAL_KEY_VOCODER_CLASS_EVS
 */
#define VCPM_CAL_KEY_ID_VOCODER_CLASS 0x08001164

/** @ingroup spf_vcpm_cal_keys
    Calibration key that identifies the sampling rate.*/
#define VCPM_CAL_KEY_ID_SAMPLING_RATE 0x08001165

/** @ingroup spf_vcpm_cal_keys
    Calibration key that identifies the volume level.*/
#define VCPM_CAL_KEY_ID_VOLUME_LEVEL 0x08001166

/** @ingroup spf_vcpm_cal_keys
    Calibration key that identifies the bandwidth extension mode. */
#define VCPM_CAL_KEY_ID_BWE 0x08001167

/** @ingroup spf_vcpm_cal_keys
    Identifier for the calibration key that identifes the network.

    This key supports the following values:
    - #VCPM_CAL_KEY_NETWORK_CDMA
    - #VCPM_CAL_KEY_NETWORK_GSM
    - #VCPM_CAL_KEY_NETWORK_WCDMA
    - #VCPM_CAL_KEY_NETWORK_LTE
    - #VCPM_CAL_KEY_NETWORK_TDSCDMA
 */
#define VCPM_CAL_KEY_ID_NETWORK 0x080011B5

/** @ingroup spf_vcpm_cal_keys
    Calibration key that identifies the Volume boost mode. */
#define VCPM_CAL_KEY_ID_VOL_BOOST 0x080011B6

/* Values supported for the key ID VCPM_CAL_KEY_ID_VOCODER_CLASS */

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents all the vocoders covered in the AMR class
    for #VCPM_CAL_KEY_ID_VOCODER_CLASS. */
#define VCPM_CAL_KEY_VOCODER_CLASS_AMR 0x08001168

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents all the vocoders covered in the EVRC class
    for #VCPM_CAL_KEY_ID_VOCODER_CLASS. */
#define VCPM_CAL_KEY_VOCODER_CLASS_EVRC 0x08001169

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents all the vocoders covered in the EVS class
    #VCPM_CAL_KEY_ID_VOCODER_CLASS. */
#define VCPM_CAL_KEY_VOCODER_CLASS_EVS 0x0800116A

/* Values supported for the key ID VCPM_CAL_KEY_ID_NETWORK */

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents the CDMA network for
    #VCPM_CAL_KEY_ID_NETWORK. */
#define VCPM_CAL_KEY_NETWORK_CDMA 0x080011B7

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents the GSM network for
    #VCPM_CAL_KEY_ID_NETWORK. */
#define VCPM_CAL_KEY_NETWORK_GSM 0x080011B8

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents the WCDMA network for
    #VCPM_CAL_KEY_ID_NETWORK. */
#define VCPM_CAL_KEY_NETWORK_WCDMA 0x080011B9

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents the LTE network for
    #VCPM_CAL_KEY_ID_NETWORK. */
#define VCPM_CAL_KEY_NETWORK_LTE 0x080011BA

/** @ingroup spf_vcpm_cal_keys
    Calibration key that represents the TD-SCDMA network for
    #VCPM_CAL_KEY_ID_NETWORK. @newpage */
#define VCPM_CAL_KEY_NETWORK_TDSCDMA 0x080011BB

/** @ingroup spf_vcpm_cal_keys
    Identifier for the parameter that sets calibration keys from both the
    applications processor and modem to the VCPM.

    @msgpayload
    vcpm_param_cal_keys_payload_t \n
    @indent{12pt} vcpm_ckv_pair_t
 */
#define VCPM_PARAM_ID_CAL_KEYS 0x0800116B

/** @ingroup spf_vcpm_cal_keys
    Payload for #VCPM_PARAM_ID_CAL_KEYS.

    Immediately following this structure is an array of structures of type
    %vcpm_ckv_pair_t. The array length is equal to num_sub_graphs.
*/
#include "spf_begin_pack.h"
struct vcpm_param_cal_keys_payload_t
{
   uint32_t vsid;
   /**< Voice System identifier for the calibration key. */

   uint32_t num_ckv_pairs;
   /**< Number of %vcpm_ckv_pair_t structures that immediately follow this
        structure. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_cal_keys_payload_t vcpm_param_cal_keys_payload_t;

/** @ingroup spf_vcpm_cal_keys
    Payload for #VCPM_PARAM_ID_CAL_KEYS and #VCPM_PARAM_ID_ACTIVE_CAL_KEYS.
    This structure contains the calibration key and value pair objects.
 */
#include "spf_begin_pack.h"
struct vcpm_ckv_pair_t
{
   uint32_t cal_key_id;
   /**< Unique identifier for the calibration key.

        @valuesbul{from modem processor}
        - #VCPM_CAL_KEY_ID_VOCODER_CLASS
        - #VCPM_CAL_KEY_ID_SAMPLING_RATE
        - #VCPM_CAL_KEY_ID_NETWORK

        @valuesbul{from applications processor}
        - #VCPM_CAL_KEY_ID_VOLUME_LEVEL
        - #VCPM_CAL_KEY_ID_BWE
        - #VCPM_CAL_KEY_ID_VOL_BOOST @tablebulletend */

   uint32_t value;
   /**< Values of the following calibration keys from the modem processor.

        @valuesbul{for VCPM\_CAL\_KEY\_ID\_VOCODER\_CLASS}
        - #VCPM_CAL_KEY_VOCODER_CLASS_AMR
        - #VCPM_CAL_KEY_VOCODER_CLASS_EVRC
        - #VCPM_CAL_KEY_VOCODER_CLASS_EVS

        @valuesbul{for VCPM\_CAL\_KEY\_ID\_NETWORK}
        - #VCPM_CAL_KEY_NETWORK_CDMA
        - #VCPM_CAL_KEY_NETWORK_GSM
        - #VCPM_CAL_KEY_NETWORK_WCDMA
        - #VCPM_CAL_KEY_NETWORK_LTE
        - #VCPM_CAL_KEY_NETWORK_TDSCDMA @tablebulletend @newpagetable */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_ckv_pair_t vcpm_ckv_pair_t;

/** @ingroup spf_vcpm_cal_keys
    Identifier for the parameter the applications processor uses to query
    active calibration keys for each voice call subgraph.

    This parameter supports only an #APM_CMD_GET_CFG command from the client.
    It does not support an APM_CMD_SET_CFG command.

    @msgpayload
    vcpm_param_active_cal_keys_payload_t \n
    @indent{12pt} vcpm_ckv_pair_t
*/
#define VCPM_PARAM_ID_ACTIVE_CAL_KEYS 0x080011C1

/** @ingroup spf_vcpm_cal_keys
    Payload for #VCPM_PARAM_ID_ACTIVE_CAL_KEYS.

    Immediately following this structure is an array of structures of type
    %vcpm_ckv_pair_t. The array length is equal to num_ckv_pairs.
*/
#include "spf_begin_pack.h"
struct vcpm_param_active_cal_keys_payload_t
{
   uint32_t sg_id;
   /**< Unique identifier for the subgraph for which the parameter payload is
        provided. */

   uint32_t num_max_ckv_pairs;
   /**< Maximum number of ckv_pairs of the given subgraph ID.

        The client fills this value when #APM_CMD_GET_CFG is called for this
        parameter ID. */

   uint32_t num_ckv_pairs;
   /**< Number of %vcpm_ckv_pair_t structures that immediately follow this
        structure. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_active_cal_keys_payload_t vcpm_param_active_cal_keys_payload_t;

/** @ingroup spf_vcpm_cal_keys
    Identifier for the parameter that sets the number of volume steps from the
    applications processor to the VCPM.

    @msgpayload
    vcpm_param_num_vol_steps_payload_t @newpage
*/
#define VCPM_PARAM_ID_NUM_VOL_STEPS 0x0800116C

/** @ingroup spf_vcpm_cal_keys
    Payload for #VCPM_PARAM_ID_NUM_VOL_STEPS.
 */
#include "spf_begin_pack.h"
struct vcpm_param_num_vol_steps_payload_t
{
   uint32_t vsid;
   /**< Voice System identifier for the volume step. */

   uint32_t num_vol_steps;
   /**< Total number of volume steps for the VSID session. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_num_vol_steps_payload_t vcpm_param_num_vol_steps_payload_t;

/** @ingroup spf_vcpm_clk_ctrl
    Identifier for the parameter that sets the clock control parameters.

    @msgpayload
    vcpm_param_clk_ctrl_payload_t
*/
#define VCPM_PARAM_ID_CLK_CTRL 0x080011AE

/** @ingroup spf_vcpm_clk_ctrl
    Payload for #VCPM_PARAM_ID_CLK_CTRL.
 */
#include "spf_begin_pack.h"
struct vcpm_param_clk_ctrl_payload_t
{
   uint32_t kpps_scale_factor;
   /**< Scale factor to apply to the aggregated kpps of the subgraph. KPPS SF is defined as follows:
    * 0: Don't care -- applies the default scale factor
    * 1-50: final kpps = 10 * kpps_scale_factor * initial kpps
    * So, a scale factor of 10 translates to unity kpps scaling. A scale factor of 50 translates to
    * kpps scaling of 5x.
    */

   uint32_t bus_bw_scale_factor;
   /**< Scale factor to apply to the aggregated bus bandwidth of the
        subgraph. BW SF is defined as follows:
    * 0: Don't care -- applies the default scale factor
    * 1-50: final bus bw = 10 * bus_bw_scale_factor * initial bus bw
    * So, a scale factor of 10 translates to unity bus bw scaling. A scale factor of 50 translates to
    * bus bw scaling of 5x.
    */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_clk_ctrl_payload_t vcpm_param_clk_ctrl_payload_t;

/** @ingroup spf_vcpm_events
    ID for the event that indicates a voice call graph is opened.

    @msgpayload
    vcpm_event_graph_open_payload_t \n
    @indent{12pt} vcpm_vs_tag_miid_info_t
*/
#define VCPM_EVENT_ID_GRAPH_OPEN 0x080011AF

/** @ingroup spf_vcpm_events
    Payload for #VCPM_EVENT_ID_GRAPH_OPEN.
 */
#include "spf_begin_pack.h"
struct vcpm_event_graph_open_payload_t
{
   uint32_t vsid;
   /**< Voice System identifier for the voice call graph. */

   uint32_t num_tag_info;
   /**< Number of %vcpm_vs_tag_miid_info_t structures that immediately follow
        this structure. @newpagetable */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_event_graph_open_payload_t vcpm_event_graph_open_payload_t;

/** @ingroup spf_vcpm_events
    Payload for #VCPM_EVENT_ID_GRAPH_OPEN. This structure contains tag and MIID
    mapping information for each subgraph. The information is sent to the Voice
    System.
*/
#include "spf_begin_pack.h"
struct vcpm_vs_tag_miid_info_t
{
   uint32_t sg_id;
   /**< ID for the sub-graph that corresponds to the module instance ID. */

   uint32_t tag_id;
   /**< Tag IDs published by the VCPM.

        @valuesbul
        - #VOICE_MOD_TAG_ID_ENCODER
        - #VOICE_MOD_TAG_ID_TX_MAILBOX
        - #VOICE_MOD_TAG_ID_TX_STREAM_HPCM
        - #VOICE_MOD_TAG_ID_DECODER
        - #VOICE_MOD_TAG_ID_RX_MAILBOX
        - #VOICE_MOD_TAG_ID_RX_STREAM_HPCM
        - #VOICE_MOD_TAG_ID_CTMTTY_TX
        - #VOICE_MOD_TAG_ID_LTETTY_TX
        - #VOICE_MOD_TAG_ID_CTMTTY_RX
        - #VOICE_MOD_TAG_ID_LTETTY_RX
        - #VOICE_MOD_TAG_ID_1XTTY_RX
        - #VOICE_MOD_TAG_ID_RX_TRM
        - #VOICE_MOD_TAG_ID_TX_SMART_SYNC @tablebulletend */

   uint32_t module_iid;
   /**< ID for the module instance that corresponds to the tag ID. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_vs_tag_miid_info_t vcpm_vs_tag_miid_info_t;

/** @ingroup spf_vcpm_events
    Identifier for the event that indicates a voice call graph is closed.

    @msgpayload
    vcpm_event_graph_close_payload_t
*/
#define VCPM_EVENT_ID_GRAPH_CLOSE 0x080011B0

/** @ingroup spf_vcpm_events
    Payload for #VCPM_EVENT_ID_GRAPH_CLOSE.
 */
#include "spf_begin_pack.h"
struct vcpm_event_graph_close_payload_t
{
   uint32_t vsid;
   /**< Voice System identifier for the voice call graph. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_event_graph_close_payload_t vcpm_event_graph_close_payload_t;

/** @ingroup spf_vcpm_events
    ID for event to indicate client that Resync cmd is processed.

    @msgpayload
    vcpm_event_resync_done_t

*/
#define VCPM_EVENT_ID_RESYNC_DONE 0x0800135C

/** @ingroup spf_vcpm_events
    Payload for #VCPM_EVENT_ID_RESYNC_DONE.
 */
#include "spf_begin_pack.h"

struct vcpm_event_resync_done_t
{
   uint32_t status;
   /* Status of the resync cmd. */

   uint32_t token;
   /* Token capturing the client info. */
}

#include "spf_end_pack.h"
;

typedef struct vcpm_event_resync_done_t vcpm_event_resync_done_t;

/** @ingroup spf_vcpm_events
    ID for event to report Tx/Rx path delays to the VS.

    @msgpayload
    vcpm_event_path_delay_payload_t

*/
#define VCPM_EVENT_ID_PATH_DELAY 0x0800135D

/** @ingroup spf_vpcm_events
    Payload for #VCPM_EVENT_ID_PATH_DELAY.
 */
#include "spf_begin_pack.h"
struct vcpm_event_path_delay_payload_t
{
   uint32_t tx_path_delay_us; /**< Tx voice path delay in microseconds. */
   uint32_t rx_path_delay_us; /**< Rx voice path delay in microseconds. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_event_path_delay_payload_t vcpm_event_path_delay_payload_t;

/** @ingroup spf_vcpm_timing_cfg
    Identifier for the parameter that sets timing parameters from the modem to
    the VCPM.

    @msgpayload
    vcpm_param_timing_params_t
 */
#define VCPM_PARAM_ID_TIMING_PARAMS 0x0800116E

/*# @h2xmlp_parameter   {"VCPM_PARAM_ID_TIMING_PARAMS",
                          VCPM_PARAM_ID_TIMING_PARAMS}
    @h2xmlp_description {ID for the parameter that sets timing parameters from
                         the modem to the VCPM.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup spf_vcpm_timing_cfg
    Payload for #VCPM_PARAM_ID_TIMING_PARAMS.
 */
#include "spf_begin_pack.h"
struct vcpm_param_timing_params_t
{
   uint32_t vfr_mode;
   /**< Frame synchronization mode for the vocoder. */

   /*#< @h2xmle_description {Frame synchronization mode for the vocoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0, 1}
        @h2xmle_policy      {Basic} */

   uint32_t vsid;
   /**< Voice System identifier for the vocoder. */

   /*#< @h2xmle_description {Voice System ID for the vocoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0x11C05000, 0x11DC5000}
        @h2xmle_policy      {Basic} */

   uint32_t tx_delivery_offset_us;
   /**< Offset (in microseconds) from the VFR to deliver a Tx vocoder
        packet. */

   /*#< @h2xmle_description {Offset (in microseconds) from the VFR to deliver a
                             Tx vocoder packet.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t rx_start_offset_us;
   /**< Offset (in microseconds) from the VFR to indicate the deadline to
        receive an Rx vocoder packet. */

   /*#< @h2xmle_description {Offset (in microseconds) from the VFR to indicate
                             the deadline to receive an Rx vocoder packet.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t vfr_cycle_us;
   /**< VFR cycle duration for the voice call. @newpagetable */

   /*#< @h2xmle_description {VFR cycle duration for the voice call.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_timing_params_t vcpm_param_timing_params_t;

/** @ingroup spf_vcpm_timing_cfg
    Identifier for the parameter that sets timing references from the modem to
    the VCPM.

    @msgpayload
    vcpm_param_timing_references_t
 */
#define VCPM_PARAM_ID_TIMING_REFERENCES 0x080010A5

/*# @h2xmlp_parameter   {"VCPM_PARAM_ID_TIMING_REFERENCES",
                          VCPM_PARAM_ID_TIMING_REFERENCES}
    @h2xmlp_description {ID for the parameter that sets timing references from
                         the modem to the VCPM.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup spf_vcpm_timing_cfg
    Payload for #VCPM_PARAM_ID_TIMING_REFERENCES.
 */
#include "spf_begin_pack.h"
struct vcpm_param_timing_references_t
{
   uint32_t vfr_mode;
   /**< Frame synchronization mode for the vocoder. */

   /*#< @h2xmle_description {Frame synchronization mode for the vocoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0, 1}
        @h2xmle_policy      {Basic} */

   uint32_t vsid;
   /**< Voice System identifier for the encoder. */

   /*#< @h2xmle_description {Voice System ID for the encoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0x11C05000, 0x11DC5000}
        @h2xmle_policy      {Basic} */

   uint64_t tx_ref_timstamp_us;
   /**< Time reference (in microseconds) used to align the encoder's timeline
        to the client's timeline at the beginning of the vocoder's packet
        exchange operation.

        Use the QTimer as the clock source for interpreting the timestamp. */

   /*#< @h2xmle_description {Time reference (in microseconds) used to align
                             the encoder's timeline to the client's timeline at
                             the beginning of the vocoder's packet exchange
                             operation. \n
                             Use the QTimer as the clock source for
                             interpreting the timestamp.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint64_t rx_ref_timstamp_us;
   /**< Time reference(in microseconds) used to align the decoder's timeline to
        the client's timeline at the beginning of the vocoder's packet exchange
        operation.

        Use the QTimer as the clock source for interpreting the timestamp. */

   /*#< @h2xmle_description {Time reference(in microseconds) used to align the
                             decoder's timeline to the client's timeline at the
                             beginning of the vocoder's packet exchange
                             operation. \n
                             Use the QTimer as the clock source for
                             interpreting the timestamp.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t vfr_cycle_us;
   /**< VFR cycle duration for the voice call. */

   /*#< @h2xmle_description {VFR cycle duration for the voice call.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_timing_references_t vcpm_param_timing_references_t;

/** @ingroup spf_vcpm_timing_cfg
    Identifier for the parameter that sets version 2 of the timing references
    from the modem to the VCPM.

    @msgpayload
    vcpm_param_timing_references_v2_t
 */
#define VCPM_PARAM_ID_TIMING_REFERENCES_V2 0x08001256

/*# @h2xmlp_parameter   {"VCPM_PARAM_ID_TIMING_REFERENCES_V2",
                          VCPM_PARAM_ID_TIMING_REFERENCES_V2}
    @h2xmlp_description {Identifier for the parameter that sets version 2 of
                         the timing references from the modem to the VCPM.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup spf_vcpm_timing_cfg
    Payload for #VCPM_PARAM_ID_TIMING_REFERENCES_V2.
 */
#include "spf_begin_pack.h"
struct vcpm_param_timing_references_v2_t
{
   uint32_t vfr_mode;
   /**< Frame synchronization mode for the vocoder. */

   /*#< @h2xmle_description {Frame synchronization mode for the vocoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0, 1}
        @h2xmle_policy      {Basic} */

   uint32_t vsid;
   /**< Voice System identifier 2 for the vocoder. */

   /*#< @h2xmle_description {Voice System ID 2 for the vocoder.}
        @h2xmle_default     {0}
        @h2xmle_range       {0x11C05000, 0x11DC5000}
        @h2xmle_policy      {Basic} */

   uint64_t tx_ref_timstamp_us;
   /**< Time reference (in microseconds) used to align the encoder's timeline
        to the client's timeline at the beginning of the vocoder's packet
        exchange operation.

        Use the QTimer as the clock source for interpreting the timestamp. */

   /*#< @h2xmle_description {Time reference (in microseconds) used to align the
                             encoder's timeline to the client's timeline at the
                             beginning of the vocoder's packet exchange
                             operation. \n
                             Use the QTimer as the clock source for
                             interpreting the timestamp.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint64_t rx_ref_timstamp_us;
   /**< Time reference(in microseconds) used to align the decoder's timeline to
        the client's timeline at the beginning of the vocoder's packet exchange
        operation.

        Use the QTimer as the clock source for interpreting the timestamp. */

   /*#< @h2xmle_description {Time reference(in microseconds) used to align the
                             decoder's timeline to the client's timeline at the
                             beginning of the vocoder's packet exchange
                             operation. \n
                             Use the QTimer as the clock source for
                             interpreting the timestamp.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t is_frame_type_event_enabled;
   /**< Enables the event-driven mailbox packet exchange mode in the Tx path
        for NO_DATA packets. */

   /*#< @h2xmle_description {Enables the event-driven mailbox packet exchange
                             mode in the Tx path for NO_DATA packets.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..1}
        @h2xmle_policy      {Basic} */

   uint64_t frame_type_ref_timestamp_us;
   /**< Client's time reference for receiving the frame type of an uplink
        packet (absolute time in microseconds).

        An indication of a NO_DATA frame for the next CDRx cycle must be sent
        before this reference.

        The clock source to use for interpreting the timestamp is
        product-specific. Currently, the QTimer is used as the timing reference
        for all products.

        The frame_type_ref_timestamp_us, read_ref_timestamp_us, and
        enc_packet_ready_margin_us fields are used together to determine the
        uplink packet delivery timeline for speech packets:

        @indent{12pt} Timestamp of speech packet delivery = minimum
        @indent{12pt} ( (frame_type_ref_timestamp_us + enc_proc_time *
        @indent{12pt} (vfr_cycle_duration_us/20000_us)), (read_ref_timestamp_us
        @indent{12pt} - enc_packet_ready_margin_us) )

        A value equal to 0 implies the mailbox enhancements feature is
        disabled.

        Events for the NO_DATA frame type are not sent to the client. */

   /*#< @h2xmle_description {Client's time reference for receiving the frame
                             type of uplink packet (absolute time in
                             microseconds). \n
                             An indication of a NO_DATA frame for the next CDRx
                             cycle must be sent before this reference. \n
                             The clock source to use for interpreting the
                             timestamp is product-specific (currently, the
                             QTimer for all products). \n
                             For more details, see AudioReach Signal Processing Framework (SPF) API Reference.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy       {Basic} */

   uint32_t enc_packet_ready_margin_us;
   /**< Minimum allowed delay margin between the timestamp of the encoder
        packet being ready and read_ref_timestamp_us.

        The encoder packet is ready and available in the mailbox buffer at
        least enc_packet_ready_margin_us ahead of read_ref_timestamp_us.

        The value is specified in microseconds. */

   /*#< @h2xmle_description {Minimum allowed delay margin between the timestamp
                             of the encoder packet being ready and
                             read_ref_timestamp_us. \n
                             The encoder packet is ready and available in the
                             mailbox buffer at least enc_packet_ready_margin_us
                             ahead of read_ref_timestamp_us. \n
                             The value is specified in microseconds.}
       @h2xmle_default     {0}
       @h2xmle_range       {0..0xFFFFFFFF}
       @h2xmle_policy      {Basic} */

   uint32_t vfr_cycle_us;
   /**< Duration of the VFR cycle for the voice call. */

   /*#< @h2xmle_description {Duration of the VFR cycle for the voice call.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_timing_references_v2_t vcpm_param_timing_references_v2_t;

/** @ingroup spf_vcpm_voice_cfg
    Identifier for the parameter that sets the Voice System ID from the
    applications processor to the VCPM.

    @msgpayload
    vcpm_param_vsid_payload_t
*/
#define VCPM_PARAM_ID_VSID 0x080011BC

/** @ingroup spf_vcpm_voice_cfg
    Payload for #VCPM_PARAM_ID_VSID.
 */
#include "spf_begin_pack.h"
struct vcpm_param_vsid_payload_t
{
   uint32_t vsid;
   /**< Voice System identifier for the graph.

        This ID is to be set on the graph that was previously created with a
        <em>don't care</em> value. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_vsid_payload_t vcpm_param_vsid_payload_t;

/** @ingroup spf_vcpm_mailbox_mem
    Parameter identifier for configuring memory on the modem that is shared
    with mailbox Tx and Rx modules present on the aDSP for vocoder packet
    exchange.

    @msgpayload
    vcpm_param_id_mailbox_memory_config_t
 */
#define VCPM_PARAM_ID_MAILBOX_MEMORY_CONFIG 0x080011E7

/** @ingroup spf_vcpm_mailbox_mem
    Payload for #VCPM_PARAM_ID_MAILBOX_MEMORY_CONFIG.
 */
#include "spf_begin_pack.h"
struct vcpm_param_id_mailbox_memory_config_t
{
   uint32_t mailbox_mem_address_adsp_lsw;
   /**< Lower 32 bits of the IO virtual address (understandable to the aDSP) of
        the mailbox memory.

        This memory is carved out from the APQ DDR specifically for mailbox
        packet exchange between the aDSP and modem. */

   uint32_t mailbox_mem_address_adsp_msw;
   /**< Upper 32 bits of the IO virtual address (understandable to aDSP) of the
        mailbox memory.

        This memory is carved out from the APQ DDR specifically for mailbox
        packet exchange between the aDSP and modem. */

   uint32_t mailbox_mem_address_pcie_lsw;
   /**< Lower 32 bits of the IO virtual address (understandable to the PCIe) of
        the mailbox memory.

        This memory is carved out from the APQ DDR specifically for mailbox
        packet exchange between the aDSP and modem. */

   uint32_t mailbox_mem_address_pcie_msw;
   /**< Upper 32 bits of the IO virtual address (understandable to the PCIe) of
        the mailbox memory.

        This memory is carved out from the APQ DDR specifically for mailbox
        packet exchange between the aDSP and modem. */

   uint32_t mem_size;
   /**< Size (in bytes) of the mailbox memory carved out from the APQ DDR. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_mailbox_memory_config_t vcpm_param_id_mailbox_memory_config_t;

/** @ingroup spf_vcpm_mailbox_mem
    Parameter ID the VCPM exposes to the HLOS to set the Tx channel
    information processed by the Tx sub-graph. This sub-graph hosts the ECNS
    module for a given voice call use case.

    The Tx channel information must be set as follows:
    - After the valid VSID configuration is set and before a Prepare command
      is issued to the voice sub-graphs
    - During voice call setup and device switch scenarios

    @msgpayload
    vcpm_param_id_tx_dev_pp_channel_info_t
 */
#define VCPM_PARAM_ID_TX_DEV_PP_CHANNEL_INFO 0x08001310

/** @ingroup spf_vcpm_mailbox_mem
    Payload for #VCPM_PARAM_ID_TX_DEV_PP_CHANNEL_INFO.
*/
#include "spf_begin_pack.h"
struct vcpm_param_id_tx_dev_pp_channel_info_t
{
   uint32_t vsid;
   /**< Voice System identifier for the channel. */

   uint32_t num_channels;
   /**< Number of channels processed by the Tx voice processor sub-graph. */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_tx_dev_pp_channel_info_t vcpm_param_id_tx_dev_pp_channel_info_t;

/** @ingroup spf_vcpm_timeline_resync_cfg
    Identifier for the parameter that sets timeline resync parameters from the
   modem to the VCPM.

    @msgpayload
    vcpm_param_id_timeline_resync_t
 */
#define VCPM_PARAM_ID_TIMELINE_RESYNC 0x0800134C

/* None resync type indicates this is not a timeline resync.
 */
/** @ingroup spf_vcpm_timeline_resync_cfg
      Resynce type None. */
#define VCPM_RESYNC_NONE AR_NON_GUID(0x00000000)

/** @ingroup spf_vcpm_timeline_resync_cfg
      Resynce type Tx, indicates this a timeline resync in Tx direction. */
#define VCPM_RESYNC_TX AR_NON_GUID(0x00000001)

/** @ingroup spf_vcpm_timeline_resync_cfg
      Resynce type Rx, indicates this a timeline resync in Rx direction. */
#define VCPM_RESYNC_RX AR_NON_GUID(0x00000002)

/** @ingroup spf_vcpm_timeline_resync_cfg
      Resynce type Tx_Rx, indicates this a timeline resync in Tx and Rx
   direction. */
#define VCPM_RESYNC_TX_RX AR_NON_GUID(VCPM_RESYNC_TX | VCPM_RESYNC_RX)

/** @ingroup spf_vcpm_timeline_resync_cfg
    Payload for #VCPM_PARAM_ID_TIMELINE_RESYNC.
*/
#include "spf_begin_pack.h"

struct vcpm_param_id_timeline_resync_t
{
   uint32_t resync_type;
   /**<  Type of resync that is being performed.
    *  Supported Values:
    *   VCPM_RESYNC_TX
    *   VCPM_RESYNC_RX
    *   VCPM_RESYNC_TX_RX
    */

   uint32_t network_id;
   /**<  Network ID. */

   uint64_t tx_ref_timestamp_us;
   /**< Client's encoder packet exchange time reference (absolute time in
    * microseconds).
    *
    *  The time reference is used to align the encoder's timeline to the
    *  client's timeline.
    *
    *  The clock source to be used for interpreting the timestamp is
    *  product-specific. Currently, the QTimer is used as the timing reference
    *  for all products.
    */

   uint64_t rx_ref_timestamp_us;
   /**< Client's decoder packet exchange time reference (absolute time in
    * microseconds).
    *
    *  The time reference is used to align the encoder's timeline to the
    *  client's timeline.
    *
    *  The clock source to be used for interpreting the timestamp is
    *  product-specific. Currently, the QTimer is used as the timing reference
    *  for all products.
    */

   uint64_t frame_type_ref_timestamp_us;
   /**< Client's time reference to receive the frame type of uplink packet (
    * absolute time in microseconds).
    *
    *  Indication of NO_DATA frame for the next CDRx cycle should
    *  be sent prior to this reference.
    *
    *  The clock source to be used for interpreting the timestamp is
    *  product-specific. Currently, the QTimer is used as the timing reference
    *  for all products.
    *
    *  frame_type_ref_timestamp_us, read_ref_timestamp_us and
    * enc_packet_ready_margin_us will be used together to determine the uplink
    * packet delivery timeline for speech packets.
    *
    *  Timestamp of speech packet delivery = minimum (
    * (frame_type_ref_timestamp_us +
    * enc_proc_time*(vfr_cycle_duration_us/20000_us)), (read_ref_timestamp_us -
    * enc_packet_ready_margin_us))
    *
    *  Value 0 implies mailbox enhancements feature is disabled. Events for
    * NO_DATA frame type will not be sent to client.
    */

   uint32_t is_frame_type_event_enabled;
   /**< Enables the event driven mailbox packet exchange mode in Tx path for
    * NO_DATA packets.
    */

   uint32_t vsid;
   /**< Voice System ID as defined @80-NF774-2,80-NA610-2.}*/

   uint32_t vfr_mode;
   /**< Vocoder frame synchronization mode.
    *
    *  @values
    *   - 0: SOFT_VFR - VFR sourced by vfr_cycle_duration_ms software interupt.
    *   - 1: HARD_VFR - VFR sourced by hardware interupt.
    *     This is not expected to change during runtime reconfig.
    */

   uint32_t vfr_cycle_us;
   /**< Vocoder frame reference duration in microseconds.
    *  This is not expected to change during runtime reconfig.
    *
    *  @values N*20, where N is an integer.
    */

   uint32_t enc_packet_ready_margin_us;
   /**<
    *  Minimum allowed delay margin between the timestamp of encoder packet
    *  being ready and read_ref_timestamp_us.
    *
    *  Encoder packet would be ready and available in the mailbox buffer atleast
    * enc_packet_ready_margin_us ahead of read_ref_timestamp_us. Value is
    * specified in microseconds. Default value is 0us.
    */
}
#include "spf_end_pack.h"
;

typedef struct vcpm_param_id_timeline_resync_t vcpm_param_id_timeline_resync_t;

/** @ingroup spf_vcpm_mailbox_mem
    Identifier for the parameter that sets up the vocoder packet loopback
    delay.

    @msgpayload
    vcpm_param_id_voc_pkt_loopback_delay_t @newpage
 */
#define VCPM_PARAM_ID_VOC_PKT_LOOPBACK_DELAY 0x08001311

/*# @h2xmlp_parameter   {"VCPM_PARAM_ID_VOC_PKT_LOOPBACK_DELAY",
                          VCPM_PARAM_ID_VOC_PKT_LOOPBACK_DELAY}
    @h2xmlp_description {ID for the parameter used to set up the vocoder
                         packet loopback delay in milliseconds.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup spf_vcpm_mailbox_mem
    Payload structure for the #VCPM_PARAM_ID_VOC_PKT_LOOPBACK_DELAY.
*/
#include "spf_begin_pack.h"
struct vcpm_param_id_voc_pkt_loopback_delay_t
{
   uint32_t vsid;
   /**< Voice System identifier for the vocoder packet loopback. */

   /*#< @h2xmle_description {Voice System ID for the vocoder packet loopback.}
        @h2xmle_default     {0}
        @h2xmle_range       {0x12006000, 0x121C6000}
        @h2xmle_policy      {Basic} */

   uint32_t delay_ms;
   /**< Vocoder packet loopback delay in milliseconds.

        Increment the delay in steps of 20 ms. It will be rounded off to the
        next 20 ms multiple. */

   /*#< @h2xmle_description {Vocoder packet loopback delay in milliseconds.
                             Increment the delay in steps of 20 ms. It will be
                             rounded off to the next 20 ms multiple.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..5000}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_voc_pkt_loopback_delay_t vcpm_param_id_voc_pkt_loopback_delay_t;

#define VCPM_PARAM_ID_VOC_PKT_LOOPBACK_START_EXCHANGE 0x08001317

#include "spf_begin_pack.h"
struct vcpm_param_id_voc_pkt_loopback_start_exchange_t
{
   uint32_t vsid;
}
#include "spf_end_pack.h"
;
typedef struct vcpm_param_id_voc_pkt_loopback_start_exchange_t vcpm_param_id_voc_pkt_loopback_start_exchange_t;

/**<  v1 version payload associated with VS_PARAM_VOC_TIMING_INFO paramID .
  */
#include "spf_begin_pack.h"
struct vcpm_param_timing_info_v1_t {

     uint32_t vsid;
     /**< Voice System identifier for the vocoder. */

     uint32_t vfr_mode;
     /**< Frame synchronization mode for the vocoder. */

     uint64_t tx_ref_timstamp_us;
     /**< Client's encoder packet exchange time reference (absolute time in microseconds).
       *
       *  The time reference is used to align the encoder's timeline to the
       *  client's timeline.
       *  For VFR 40ms: this will be considered as Tx buffer processing start TS.
       *  For VFR 20ms: this will be considered as Tx buffer delivery TS.
       *  0.5 ms of margin is considered by Audio to account for any system delay.
       *  The clock source to be used for interpreting the timestamp is
       *  product-specific. Currently, the QTimer is used as the timing reference
       *  for all products.
       */

     uint64_t rx_ref_timstamp_us;
     /**< Client's decoder packet exchange time reference (absolute time in microseconds).
       *
       *  This timing reference is used to derive the start TS of the Rx packet processing by decoder.
       *  0.5 ms of margin is considered by Audio to account for any system delay.
       *  The clock source to be used for interpreting the timestamp is
       *  product-specific. Currently, the QTimer is used as the timing reference
       *  for all products.
       */

     uint32_t vfr_cycle_us;
     /**< Duration of the VFR cycle for the voice call. */

     uint32_t is_frame_type_event_enabled;
     /**< Enables the event-driven mailbox packet exchange mode in the Tx path
          for NO_DATA packets. */

     uint64_t frame_type_ref_timestamp_us;
     /**< Client's time reference to receive the frame type of uplink packet ( absolute time in microseconds).
       *
       *  Indication of NO_DATA frame for the next CDRx cycle should
       *  be sent prior to this reference.
       *
       *  The clock source to be used for interpreting the timestamp is
       *  product-specific. Currently, the QTimer is used as the timing reference
       *  for all products.
       *
       *  Value 0 implies mailbox enhancements feature is disabled. Events for NO_DATA frame type will
       *  not be sent to client.
       */

     uint64_t modem_sleep_start_ref_timestamp_us;
     /**<
       *  Modem sleep start Ref TS.
       *  For VFR 20ms: this will be ignored.
       *  For VFR 40ms: this will be considered as Tx buffer delivery TS.
       *  Value is specified in microseconds.
       */

}
#include "spf_end_pack.h"
;

typedef struct vcpm_param_timing_info_v1_t vcpm_param_timing_info_v1_t;

/**<Version field to identify the timing info payload sructure and definition.
  */
#define VCPM_TIMING_INFO_PAYLOAD_VERSION_V1 1

/** @ingroup spf_vcpm_timing_cfg
    Identifier for the parameter that sets the timing info
    from the modem to the VCPM.

    @msgpayload
    vcpm_param_timing_info_t
 */

#define VCPM_PARAM_ID_TIMING_INFO ( 0x0800135F )

#include "spf_begin_pack.h"
struct vcpm_param_timing_info_t {

  uint64_t version;
  /**< Version field to identify the timing info payload sructure and definition.
    *  Version VCPM_TIMING_INFO_PAYLOAD_VERSION_V1: - Power optimized resync param with start and end timing ref markers.
    */
  union
  {
    /**< payload for V1 version.
     */
     vcpm_param_timing_info_v1_t payload_v1;
  };
}
#include "spf_end_pack.h"
;

typedef struct vcpm_param_timing_info_t vcpm_param_timing_info_t;

/**<  v1 version payload associated with VS_PARAM_VOC_TIMELINE_RESYNC_INFO paramID .
  */
#include "spf_begin_pack.h"
struct vcpm_param_timeline_resync_info_v1_t {

   uint32_t resync_type;
  /**<  Type of resync that is being performed.
    *  Supported Values:
    *   VCPM_RESYNC_TX
    *   VCPM_RESYNC_RX
    *   VCPM_RESYNC_TX_RX
    */

   uint32_t network_id;
  /**<  Network ID. */

   uint64_t tx_ref_timestamp_us;
    /**< Client's encoder packet exchange time reference (absolute time in microseconds).
      *
      *  The time reference is used to align the encoder's timeline to the
      *  client's timeline.
      *  For VFR 40ms: this will be considered as Tx buffer processing start TS.
      *  For VFR 20ms: this will be considered as Tx buffer delivery TS.
      *  0.5 ms of margin is considered by Audio to account for any system delay.
      *  The clock source to be used for interpreting the timestamp is
      *  product-specific. Currently, the QTimer is used as the timing reference
      *  for all products.
      */


  uint64_t rx_ref_timestamp_us;
    /**< Client's decoder packet exchange time reference (absolute time in microseconds).
      *
      *  This timing reference is used to derive the start TS of the Rx packet processing by decoder.
      *  0.5 ms of margin is considered by Audio to account for any system delay.
      *  The clock source to be used for interpreting the timestamp is
      *  product-specific. Currently, the QTimer is used as the timing reference
      *  for all products.
      */

  uint64_t frame_type_ref_timestamp_us;
    /**< Client's time reference to receive the frame type of uplink packet ( absolute time in microseconds).
      *
      *  Indication of NO_DATA frame for the next CDRx cycle should
      *  be sent prior to this reference.
      *
      *  The clock source to be used for interpreting the timestamp is
      *  product-specific. Currently, the QTimer is used as the timing reference
      *  for all products.
      *
      *  Value 0 implies mailbox enhancements feature is disabled. Events for NO_DATA frame type will
      *  not be sent to client.
      */

  uint32_t is_frame_type_event_enabled;
    /**< Enables the event driven mailbox packet exchange mode in Tx path for NO_DATA packets.
      */

  uint32_t vsid;
  /**< Voice System ID as defined @80-NF774-2,80-NA610-2.}*/

  uint32_t vfr_mode;
    /**< Vocoder frame synchronization mode.
      *
      *  @values
      *   - 0: SOFT_VFR - VFR sourced by vfr_cycle_duration_ms software interupt.
      *   - 1: HARD_VFR - VFR sourced by hardware interupt.
      *     This is not expected to change during runtime reconfig.
      */

  uint32_t vfr_cycle_us;
    /**< Vocoder frame reference duration in microseconds.
      *  This is not expected to change during runtime reconfig.
      *
      *  @values N*20, where N is an integer.
      */

  uint64_t modem_sleep_start_ref_timestamp_us;
    /**<
      *  Modem sleep start Ref TS.
      *  For VFR 20ms: this will be ignored.
      *  For VFR 40ms: this will be considered as Tx buffer delivery TS.
      *  Value is specified in microseconds.
      */
}
#include "spf_end_pack.h"
;


/** @ingroup spf_vcpm_timing_cfg
    Identifier for the parameter that sets the timing info
    from the modem to the VCPM.

    @msgpayload
    vcpm_param_timing_info_t
 */

/* this param's behavior and payload are exactly similar to VCPM_PARAM_ID_TIMING_INFO
   But to check whether client support frame type event or not, we are using this new param
*/
#define VCPM_PARAM_ID_TIMING_INFO_V2 ( 0x08001B01 )

typedef struct vcpm_param_timeline_resync_info_v1_t vcpm_param_timeline_resync_info_v1_t;

/** @ingroup spf_vcpm_timeline_resync_cfg
    Identifier for the parameter that sets timeline resync parameters from the modem to
    the VCPM.

    @msgpayload
    vcpm_param_id_timeline_resync_info_t
 */

#define VCPM_PARAM_ID_TIMELINE_RESYNC_INFO ( 0x0800136C )

#include "spf_begin_pack.h"

struct vcpm_param_timeline_resync_info_t {

  uint64_t version;
  /**< Version field to identify the timing info payload sructure and definition.
    *  Version VCPM_TIMING_INFO_PAYLOAD_VERSION_V1: - Power optimized resync param with start and end timing ref markers.
    */
  union
  {
    /**< payload for V1 version.
     */
     vcpm_param_timeline_resync_info_v1_t payload_v1;
  };
}
#include "spf_end_pack.h"
;

typedef struct vcpm_param_timeline_resync_info_t vcpm_param_timeline_resync_info_t;


/**
 * List of opcodes
 *
 * The client uses APM_CMD_GET_CFG command to query for the version info.
 */
typedef struct vcpm_param_opcode_list_t {

  uint32_t opcode;
    /**< opcode being queried
      *  This is an input param.
      *  @values:
      *  - 0x0 to 0xFFFFFFFF
      */

  uint32_t is_supported;
  /**< Field indicating if the opcode is supported.
    *  This is an output param.
    *  @values:
    *  - 0x0 - unsupported
    *    0x1 --Supported
    */
}vcpm_param_opcode_list_t;

/** @ingroup spf_vcpm_timeline_resync_cfg
    Identifier for the parameter used for querying the VCPM API capability.
    This paramID only supports APM_CMD_GET_CFG operation.

    @msgpayload
    vcpm_param_api_capability_t
 */

#define VCPM_PARAM_ID_API_CAPABILITY ( 0x0800151F )
/**< Immediately following this structure is an array of structures of type
     vcpm_param_opcode_list_t. The number of such arrays is equal to num_opcodes.*/
#include "spf_begin_pack.h"

struct vcpm_param_api_capability_t {

  uint32_t num_opcodes;
    /**< num of opcodes being queried
      *
      *  @values:
      *  - 0x0 to 0xFFFFFFFF
      */
}
#include "spf_end_pack.h"
;

typedef struct vcpm_param_api_capability_t vcpm_param_api_capability_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS // to end of file
/*------------------------------------------------------------------------------
 *  H2XML annotations
 *----------------------------------------------------------------------------*/
enum platforms_spf
{
   PLATFORM_SPF = 0xFFFFFFFE /**< @h2xmle_name {SPF} */
};

/** @h2xml_platforms{PLATFORM_SPF} */

enum vcpm_key_ids
{
   voice_vocoder_class       = VCPM_CAL_KEY_ID_VOCODER_CLASS,
   voice_sample_rate         = VCPM_CAL_KEY_ID_SAMPLING_RATE,
   voice_vol_level           = VCPM_CAL_KEY_ID_VOLUME_LEVEL,
   voice_bandwidth_extension = VCPM_CAL_KEY_ID_BWE,
   voice_network             = VCPM_CAL_KEY_ID_NETWORK,
   voice_vol_boost           = VCPM_CAL_KEY_ID_VOL_BOOST
};

/*# @h2xmlk_key         {voice_vocoder_class}
    @h2xmlk_isVoice     {true}
    @h2xmlk_description {Calibration key for the vocoder class.} */

enum vcpm_key_vocoder_class
{
   voc_cal_key_amr = VCPM_CAL_KEY_VOCODER_CLASS_AMR,
   /*#< @h2xmle_description {AMR vocoder class.} */

   voc_cal_key_evrc = VCPM_CAL_KEY_VOCODER_CLASS_EVRC,
   /*#< @h2xmle_description {EVRC vocoder class.} */

   voc_cal_key_evs = VCPM_CAL_KEY_VOCODER_CLASS_EVS
   /*#< @h2xmle_description {EVS vocoder class.} */
};

/*# @h2xmlk_key         {voice_sample_rate}
    @h2xmlk_sampleRate
    @h2xmlk_isVoice     {true}
    @h2xmlk_description {Calibration key for the sampling rate.} */

enum vcpm_key_sample_rate
{
   SR_NB = 0x1F40,
   /*#< @h2xmle_sampleRate  {8000}
        @h2xmle_description {Narrowband sampling rate.} */

   SR_WB = 0x3E80,
   /*#< @h2xmle_sampleRate  {16000}
        @h2xmle_description {Wideband sampling rate.} */

   SR_SWB = 0x7D00,
   /*#< @h2xmle_sampleRate  {32000}
        @h2xmle_description {Superwideband sampling rate.} */

   SR_FB = 0xBB80,
   /*#< @h2xmle_sampleRate  {48000}
        @h2xmle_description {Fullband sampling rate.} */
};

/*# @h2xmlk_key         {voice_vol_level}
    @h2xmlk_isVoice     {true}
    @h2xmlk_isDynamic   {true}
    @h2xmlk_description {Calibration key for the volume level.} */

enum vcpm_key_volume_level
{
   vol_level_0 = 0x00000000, /*#< @h2xmle_description {Volume level 0.} */
   vol_level_1 = 0x00000001, /*#< @h2xmle_description {Volume level 1.} */
   vol_level_2 = 0x00000002, /*#< @h2xmle_description {Volume level 2.} */
   vol_level_3 = 0x00000003, /*#< @h2xmle_description {Volume level 3.} */
   vol_level_4 = 0x00000004, /*#< @h2xmle_description {Volume level 4.} */
   vol_level_5 = 0x00000005, /*#< @h2xmle_description {Volume level 5.} */
};

/*# @h2xmlk_key         {voice_bandwidth_extension}
    @h2xmlk_isVoice     {true}
    @h2xmlk_description {Calibration key for BWE.} */

enum vcpm_key_bandwidth_extension
{
   enable  = 0x00000001, /*#< @h2xmle_description {Enable BWE.} */
   disable = 0           /*#< @h2xmle_description {Disable BWE.} */
};

/*# @h2xmlk_key         {voice_network}
    @h2xmlk_isVoice     {true}
    @h2xmlk_description {Calibration key for the vocoder network.} */

enum vcpm_key_vocoder_network
{
   voc_cal_key_cdma = VCPM_CAL_KEY_NETWORK_CDMA,
   /*#< @h2xmle_description {CDMA for the vocoder network.} */

   voc_cal_key_gsm = VCPM_CAL_KEY_NETWORK_GSM,
   /*#< @h2xmle_description {GSM for the vocoder network.} */

   voc_cal_key_wcdma = VCPM_CAL_KEY_NETWORK_WCDMA,
   /*#< @h2xmle_description {WCDMA for the vocoder network.} */

   voc_cal_key_lte = VCPM_CAL_KEY_NETWORK_LTE,
   /*#< @h2xmle_description {LTE for the vocoder network.} */

   voc_cal_key_TDSCDMA = VCPM_CAL_KEY_NETWORK_TDSCDMA
   /*#< @h2xmle_description {TDS CDMA for the vocoder network.} */
};

/*# @h2xmlk_key         {voice_vol_boost}
    @h2xmlk_isVoice     {true}
    @h2xmlk_isDynamic   {true}
    @h2xmlk_description {Calibration key for volume boost.} */

enum vcpm_key_vol_boost
{
   vol_boost_enable = 0x00000001,
   /*#< @h2xmle_description {Enable volume boost.} */

   vol_boost_disable = 0
   /*#< @h2xmle_description {Disable volume boost.} */
};

/*# @h2xmlk_ckeys
    @h2xmlk_isVoice     {true}
    @h2xmlk_description {Calibration keys for a voice call use case.} */

enum vcpm_cal_Keys
{
   ck_vocoder_class = voice_vocoder_class,
   ck_sampling_rate = voice_sample_rate,
   ck_volume_level  = voice_vol_level,
   ck_WV2           = voice_bandwidth_extension,
   ck_voice_network = voice_network,
   ck_vol_boost     = voice_vol_boost
};

/*# @h2xmlk_modTagList
    @h2xmlk_description {Module tag information.} */

typedef enum {
   voice_mod_tag_id_encoder = VOICE_MOD_TAG_ID_ENCODER,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Voice encoder.} */

   voice_mod_tag_id_tx_mailbox = VOICE_MOD_TAG_ID_TX_MAILBOX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Mailbox in the Tx Stream.} */

   voice_mod_tag_id_tx_stream_hpcm = VOICE_MOD_TAG_ID_TX_STREAM_HPCM,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Host PCM in the Tx Stream.} */

   voice_mod_tag_id_tx_smart_sync = VOICE_MOD_TAG_ID_TX_SMART_SYNC,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Smart synchronization in the Tx stream.} */

   voice_mod_tag_id_decoder = VOICE_MOD_TAG_ID_DECODER,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Voice decoder.} */

   voice_mod_tag_id_rx_mailbox = VOICE_MOD_TAG_ID_RX_MAILBOX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Mailbox in the Rx stream.} */

   voice_mod_tag_id_rx_stream_hpcm = VOICE_MOD_TAG_ID_RX_STREAM_HPCM,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Host PCM in the Rx stream.} */

   voice_mod_tag_id_1x_rx_tty = VOICE_MOD_TAG_ID_1XTTY_RX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {1x TTY in the Rx stream.} */

   voice_mod_tag_id_ctm_rx_tty = VOICE_MOD_TAG_ID_CTMTTY_RX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {CTM TTY in the Rx stream.} */

   voice_mod_tag_id_ctm_tx_tty = VOICE_MOD_TAG_ID_CTMTTY_TX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {CTM TTY in the Tx stream.} */

   voice_mod_tag_id_lte_rx_tty = VOICE_MOD_TAG_ID_LTETTY_RX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {LTE TTY in the Rx stream.} */

   voice_mod_tag_id_lte_tx_tty = VOICE_MOD_TAG_ID_LTETTY_TX,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {LTE TTY in the Tx stream.} */

   voice_mod_tag_id_stream_rx_soft_vol = VOICE_MOD_TAG_ID_STREAM_RX_SOFT_VOL,
   /*#< @h2xmle_isVoice     {true}
        @h2xmle_description {Volume control in the Rx stream.} */

   voice_mod_tag_id_stream_tx_soft_vol = VOICE_MOD_TAG_ID_STREAM_TX_SOFT_VOL,
  /*#< @h2xmle_isVoice     {true}
       @h2xmle_description {Volume control in the Tx stream.} */

} vcpm_graph_tag_Keys;

#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* _VCPM_API_H_ */
