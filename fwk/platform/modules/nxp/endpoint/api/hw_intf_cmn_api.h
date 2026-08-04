#ifndef HW_INTF_CMN_API
#define HW_INTF_CMN_API
/**
 * \file hw_intf_cmn_api.h
 * \brief
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "module_cmn_api.h"
#include "imcl_fwk_intent_api.h"

/** @addtogroup ar_spf_mod_hwep_common_mods
    These APIs are used by all hardware endpoint modules such as I2S and PCM
    TDM.
*/

/*------------------------------------------------------------------------------
   Parameters
------------------------------------------------------------------------------*/
/*==============================================================================
  Constants
==============================================================================*/

/** @ingroup ar_spf_mod_hwep_common_macros
    Frame size in milliseconds (inclusive). */
#define FRAME_SIZE_FACTOR_1                                 1

/** @ingroup ar_spf_mod_hwep_common_macros
    Minimum size of the frame in milliseconds. */
#define FRAME_SIZE_MIN_MS                                   1

/** @ingroup ar_spf_mod_hwep_common_macros
    Maximum size of the frame in milliseconds. */
#define FRAME_SIZE_MAX_MS                                   40

/** @ingroup ar_spf_mod_hwep_common_mods
    Valid types of Low-Power Audio Interfaces (LPAIFs).
*/
typedef enum /** @cond */ lpaif_type_t /** @endcond */
{
  LPAIF             = 0,          /**<  */
  LPAIF_RXTX        = 1,          /**<  */
  LPAIF_WSA         = 2,          /**<  */
  LPAIF_VA          = 3,          /**<  */
  LPAIF_AXI         = 4,          /**<  */
  LPAIF_AUD         = 5,          /**<  */
  LPAIF_SDR         = 6,          /**<  */
  LPAIF_WSA2        = 7,          /**<  */
  MAX_NUM_LPAIF_TYPES,            /**<  */
  LPAIF_BLK_INVALID = 0x7FFFFFFF  /**<  */
} lpaif_type_t ;

/*==============================================================================
   Type definitions
==============================================================================*/

/** @ingroup ar_spf_mod_hwep_common_mods
    ID of the parameter used to configure the hardware endpoint media format. */
#define PARAM_ID_HW_EP_MF_CFG                               0x08001017

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_MF_CFG", PARAM_ID_HW_EP_MF_CFG}
    @h2xmlp_description {ID for the parameter used to configure the hardware
                         endpoint media format.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_MF_CFG parameter.
*/
#include "spf_begin_pack.h"
struct param_id_hw_ep_mf_t
{
   uint32_t sample_rate;
   /**< Defines the sampling rate of the interface.

        @values 8, 11.025, 12, 16, 22.05, 24,32, 44.1, 48, 88.2, 96, 176.4,
                192, 352.8, 384 kHz */

   /*#< @h2xmle_description {Defines the sampling rate of the interface.}
        @h2xmle_rangeList   {"8 kHz"=8000;
                             "11.025 kHz"=11025;
                             "12 kHz"=12000;
                             "16 kHz"=16000;
                             "22.05 kHz"=22050;
                             "24 kHz"=24000;
                             "32 kHz"=32000;
                             "44.1 kHz"=44100;
                             "48 kHz"=48000;
                             "88.2 kHz"=88200;
                             "96 kHz"=96000;
                             "176.4 kHz"=176400;
                             "192 kHz"=192000;
                             "352.8 kHz"=352800;
                             "384 kHz"=384000}
        @h2xmle_default     {48000} */

    uint16_t bit_width;
   /**< Bit width in bits per sample.

        @valuesbul
        - #BIT_WIDTH_16
        - #BIT_WIDTH_24
        - #BIT_WIDTH_32 @tablebulletend*/

   /*#< @h2xmle_description {Bit width in bits per sample.}
        @h2xmle_rangeList   {"BIT_WIDTH_16"=16;
                             "BIT_WIDTH_24"=24;
                             "BIT_WIDTH_32"=32}
        @h2xmle_default     {16} */

   uint16_t num_channels;
   /**< Number of channels.

        @values 1 through 32 (Default = 1) */

   /*#< @h2xmle_description {Number of channels.}
        @h2xmle_range       {1..32}
        @h2xmle_default     {1} */

   uint32_t data_format;
   /**< Format of the data.

        @valuesbul
        - #DATA_FORMAT_FIXED_POINT (Default)
        - #DATA_FORMAT_IEC61937_PACKETIZED
        - #DATA_FORMAT_IEC60958_PACKETIZED
        - #DATA_FORMAT_IEC60958_PACKETIZED_NON_LINEAR
        - #DATA_FORMAT_DSD_OVER_PCM
        - #DATA_FORMAT_GENERIC_COMPRESSED
        - #DATA_FORMAT_RAW_COMPRESSED
        - #DATA_FORMAT_COMPR_OVER_PCM_PACKETIZED @tablebulletend */

   /*#< @h2xmle_description {Format of the data.}
        @h2xmle_rangeList   {"DATA_FORMAT_FIXED_POINT"=1;
                             "DATA_FORMAT_IEC61937_PACKETIZED"=2;
                             "DATA_FORMAT_IEC60958_PACKETIZED"=3;
                             "DATA_FORMAT_IEC60958_PACKETIZED_NON_LINEAR"= 8;
                             "DATA_FORMAT_DSD_OVER_PCM"=4;
                             "DATA_FORMAT_GENERIC_COMPRESSED"=5;
                             "DATA_FORMAT_RAW_COMPRESSED"=6;
                             "DATA_FORMAT_COMPR_OVER_PCM_PACKETIZED"= 7}
        @h2xmle_default     {1} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_mf_t param_id_hw_ep_mf_t;


/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that configures the operating frame size.

    @msgpayload
    param_id_frame_size_factor_t
*/
#define PARAM_ID_HW_EP_FRAME_SIZE_FACTOR                    0x08001018

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_FRAME_SIZE_FACTOR",
                          PARAM_ID_HW_EP_FRAME_SIZE_FACTOR}
    @h2xmlp_description {ID for the parameter that configures the operating
                         frame size.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_FRAME_SIZE_FACTOR parameter.
 */
#include "spf_begin_pack.h"
struct param_id_frame_size_factor_t
{
   uint32_t frame_size_factor;
   /**< Frame size factor used to derive the operating frame size.

        @values 1 through 40 samples per channel (Default = 1) */

   /*#< @h2xmle_description {Frame size factor used to derive the operating
                             frame size (in number of samples per channel).}
        @h2xmle_range       {1..40}
        @h2xmle_default     {1} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_frame_size_factor_t param_id_frame_size_factor_t;


/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that configures the power mode (XO shutdown).

    @msgpayload
    param_id_hw_ep_power_mode_cfg_t
*/
#define PARAM_ID_HW_EP_POWER_MODE_CFG                       0x08001176

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_POWER_MODE_CFG",
                          PARAM_ID_HW_EP_POWER_MODE_CFG}
    @h2xmlp_description {Configures the power mode (XO shutdown).}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_macros
    Default mode where the endpoint is not in the low-power island (LPI). */
#define POWER_MODE_0             0

/** @ingroup ar_spf_mod_hwep_common_macros
    XO shutdown (deep sleep) is allowed, all LPM's allowed. */
#define POWER_MODE_1             1

/** @ingroup ar_spf_mod_hwep_common_macros
    XO shutdown not allowed, LPM+ modes disabled */
#define POWER_MODE_2             2

/** @ingroup ar_spf_mod_hwep_common_macros
    XO shutdown is allowed, LPM+ modes disabled */
#define POWER_MODE_3             3

/** @ingroup ar_spf_mod_hwep_common_macros
    Maximum number of power modes. */
#define HW_EP_POWER_MODE_MAX     AR_NON_GUID(POWER_MODE_3)

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_POWER_MODE_CFG parameter.
 */
#include "spf_begin_pack.h"
struct param_id_hw_ep_power_mode_cfg_t
{
   uint32_t power_mode;
   /**< Indicates the sleep latency XO mode vote in a low-power scenario.

        @valuesbul
        - #POWER_MODE_0 (Default)
        - #POWER_MODE_1
        - #POWER_MODE_2
        - #POWER_MODE_3 @tablebulletend */

   /*#< @h2xmle_description {Indicates the sleep latency XO mode vote in a
                             low-power scenario.}
        @h2xmle_default     {POWER_MODE_0}
        @h2xmle_rangeList   {"POWER_MODE_0"=POWER_MODE_0;
                             "POWER_MODE_1"=POWER_MODE_1;
                             "POWER_MODE_2"=POWER_MODE_2;
                             "POWER_MODE_3"=POWER_MODE_3} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_power_mode_cfg_t param_id_hw_ep_power_mode_cfg_t;

/*********************************** HW EP Timestamp Propagation enable ******************************/
/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that enable/disable the timestamp propagation.

    @msgpayload
    param_id_hw_ep_timestamp_propagation_t
*/
#define PARAM_ID_HW_EP_TIMESTAMP_PROPAGATION                0x08001A7E

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_TIMESTAMP_PROPAGATION",
                          PARAM_ID_HW_EP_TIMESTAMP_PROPAGATION}
    @h2xmlp_description {Identifier for the parameter that enable/disable
                        the timestamp propagation.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_macros
    Mode where the timstamp propagation is disabled. */
#define TIMESTAMP_PROPAGATION_DISABLE                           1

/** @ingroup ar_spf_mod_hwep_common_macros
    Default mode where the timstamp propagation is enabled. */
#define TIMESTAMP_PROPAGATION_ENABLE                            0
/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_TIMESTAMP_PROPAGATION parameter.  */

#include "spf_begin_pack.h"
struct param_id_hw_ep_timestamp_propagation_t
{
    uint32_t is_timestamp_disabled;
    /**< Indicates that the timestamp propagation is disabled or not.

        @valuesbul
        - #TIMESTAMP_PROPAGATION_DISABLE
        - #TIMESTAMP_PROPAGATION_ENABLE (Default) @tablebulletend */

   /*#< @h2xmle_description {Indicates the timestamp propagation is disabled or not.}
        @h2xmle_default     {TIMESTAMP_PROPAGATION_ENABLE}
        @h2xmle_rangeList   {"TIMESTAMP_PROPAGATION_DISABLE"=TIMESTAMP_PROPAGATION_DISABLE;
                             "TIMESTAMP_PROPAGATION_ENABLE"=TIMESTAMP_PROPAGATION_ENABLE}  */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_timestamp_propagation_t param_id_hw_ep_timestamp_propagation_t;


/*********************************** HW EP Interleaving ******************************/

/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that configures data interleaving for
    the HW EP Source interface.

    @msgpayload
    param_id_hw_ep_data_interleaving_t
*/
#define PARAM_ID_HW_EP_SRC_OUT_DATA_INTERLEAVING                0x08001A7F

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_SRC_OUT_DATA_INTERLEAVING",
                          PARAM_ID_HW_EP_SRC_OUT_DATA_INTERLEAVING}
    @h2xmlp_description {ID for the parameter that configures the data
                         interleaving for the HW EP Module.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_macros
    Mode to configure the data interleaving as interleaved. */
#define DATA_INTERLEAVED                                       0

/** @ingroup ar_spf_mod_hwep_common_macros
    Mode to configure the data interleaving as deinterleaved packed. */
#define DATA_DEINTERLEAVED_PACKED                              1

/** @ingroup ar_spf_mod_hwep_common_macros
    Default mode to configure the data interleaving as deinterleaved unpacked. */
#define DATA_DEINTERLEAVED_UNPACKED                            2

/** @ingroup ar_spf_mod_hwep_common_macros
    Mode to configure the data interleaving as invalid. */
#define DATA_INVALID_INTERLEAVING                              3

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_SRC_OUT_DATA_INTERLEAVING parameter. */

#include "spf_begin_pack.h"
struct param_id_hw_ep_data_interleaving_t
{
    uint32_t data_interleaving;
   /**< Indicates the data interleaving supported by the HW Endpoint Module.

        @valuesbul
        - #DATA_INTERLEAVED
        - #DATA_DEINTERLEAVED_PACKED
        - #DATA_DEINTERLEAVED_UNPACKED (Default)
        - #DATA_INVALID_INTERLEAVING @tablebulletend */

   /*#< @h2xmle_description {Indicates the data interleaving supported by the
                             HW EP module.}
        @h2xmle_default     {DATA_DEINTERLEAVED_UNPACKED}
        @h2xmle_rangeList   {"DATA_INTERLEAVED"=DATA_INTERLEAVED;
                             "DATA_DEINTERLEAVED_PACKED"=DATA_DEINTERLEAVED_PACKED;
                             "DATA_DEINTERLEAVED_UNPACKED"=DATA_DEINTERLEAVED_UNPACKED;
                             "DATA_INVALID_INTERLEAVING"=DATA_INVALID_INTERLEAVING}  */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_data_interleaving_t param_id_hw_ep_data_interleaving_t;

/*********************************** HW EP Source Custom Channel Map ******************************/

/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that configures custom channel maps for the
    hardware endpoint source modules.

    @msgpayload
    param_id_hw_ep_src_channel_map_t
 */
#define PARAM_ID_HW_EP_SRC_CHANNEL_MAP                 0x0800108A

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_SRC_CHANNEL_MAP",
                          PARAM_ID_HW_EP_SRC_CHANNEL_MAP}
    @h2xmlp_description {ID for the parameter that configures custom channel
                         maps for the hardware EP source.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_SRC_CHANNEL_MAP parameter.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_hw_ep_src_channel_map_t
{
   uint16_t num_channels;
   /**< Number of channels in the array.

        @values 0 through 16 (Default = 0) */

   /*#< @h2xmle_description {Number of channels}
        @h2xmle_default     {0}
        @h2xmle_range       {0..16}
        @h2xmle_policy      {Basic} */

#if defined(__H2XML__)
   uint16_t channel_mapping[0];
   /**< Array of channel mappings of size num_channels.

        @values PCM_CUSTOM_CHANNEL_MAP_1 (Default) through
                PCM_CUSTOM_CHANNEL_MAP_16 (see #pcm_channel_map)

        Each element i of the array describes channel i in the buffer, where i
        is less than num_channels. An unused channel is set to 0.

        Each channel that is used should be between the values of 48 and 63.
        @newpagetable */

   /*#< @h2xmle_description       {Channel mapping array of size num_channels. Each
                                   element i of the array describes channel i
                                   in the buffer, where i is less than
                                   num_channels. An unused channel is set to
                                   0. Each channel that is used should be
                                   between the values of 48 and 63.}
        @h2xmle_rangeList         {"PCM_CUSTOM_CHANNEL_MAP_1"=PCM_CUSTOM_CHANNEL_MAP_1,
                                   "PCM_CUSTOM_CHANNEL_MAP_2"=PCM_CUSTOM_CHANNEL_MAP_2,
                                   "PCM_CUSTOM_CHANNEL_MAP_3"=PCM_CUSTOM_CHANNEL_MAP_3,
                                   "PCM_CUSTOM_CHANNEL_MAP_4"=PCM_CUSTOM_CHANNEL_MAP_4,
                                   "PCM_CUSTOM_CHANNEL_MAP_5"=PCM_CUSTOM_CHANNEL_MAP_5,
                                   "PCM_CUSTOM_CHANNEL_MAP_6"=PCM_CUSTOM_CHANNEL_MAP_6,
                                   "PCM_CUSTOM_CHANNEL_MAP_7"=PCM_CUSTOM_CHANNEL_MAP_7,
                                   "PCM_CUSTOM_CHANNEL_MAP_8"=PCM_CUSTOM_CHANNEL_MAP_8,
                                   "PCM_CUSTOM_CHANNEL_MAP_9"=PCM_CUSTOM_CHANNEL_MAP_9,
                                   "PCM_CUSTOM_CHANNEL_MAP_10"=PCM_CUSTOM_CHANNEL_MAP_10,
                                   "PCM_CUSTOM_CHANNEL_MAP_11"=PCM_CUSTOM_CHANNEL_MAP_11,
                                   "PCM_CUSTOM_CHANNEL_MAP_12"=PCM_CUSTOM_CHANNEL_MAP_12,
                                   "PCM_CUSTOM_CHANNEL_MAP_13"=PCM_CUSTOM_CHANNEL_MAP_13,
                                   "PCM_CUSTOM_CHANNEL_MAP_14"=PCM_CUSTOM_CHANNEL_MAP_14,
                                   "PCM_CUSTOM_CHANNEL_MAP_15"=PCM_CUSTOM_CHANNEL_MAP_15,
                                   "PCM_CUSTOM_CHANNEL_MAP_16"=PCM_CUSTOM_CHANNEL_MAP_16}
        @h2xmle_default           {PCM_CUSTOM_CHANNEL_MAP_1}
        @h2xmle_variableArraySize {"num_channels"}
        @h2xmle_policy            {Basic} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_src_channel_map_t param_id_hw_ep_src_channel_map_t;


/** @ingroup ar_spf_mod_hwep_common_mods
    Identifier for the parameter that configures DMA data alignment for the
    Codec DMA interface.

    @msgpayload
    param_id_hw_ep_dma_data_align_t
*/
#define PARAM_ID_HW_EP_DMA_DATA_ALIGN                          0x08001233

/*# @h2xmlp_parameter   {"PARAM_ID_HW_EP_DMA_DATA_ALIGN",
                          PARAM_ID_HW_EP_DMA_DATA_ALIGN}
    @h2xmlp_description {ID for the parameter that configures DMA data
                         alignment for the Codec DMA interface.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_macros
    DMA data is aligned to the most significant bit (MSB). */
#define DMA_DATA_ALIGN_MSB                                     0

/** @ingroup ar_spf_mod_hwep_common_macros
    DMA data is aligned to the least significant bit (LSB). */
#define DMA_DATA_ALIGN_LSB                                     1

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_EP_DMA_DATA_ALIGN parameter.
 */
#include "spf_begin_pack.h"
struct param_id_hw_ep_dma_data_align_t
{
   uint32_t                dma_data_align;
   /**< Indicates the data alignment in the DMA buffer.

        @valuesbul
        - #DMA_DATA_ALIGN_MSB (Default)
        - #DMA_DATA_ALIGN_LSB @tablebulletend */

   /*#< @h2xmle_description {Indicates the data alignment in the DMA buffer.}
        @h2xmle_rangeList   {"DMA_DATA_ALIGN_MSB"=0;
                             "DMA_DATA_ALIGN_LSB"=1}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_ep_dma_data_align_t param_id_hw_ep_dma_data_align_t;

/** @ingroup ar_spf_mod_hwep_common_mods
 * Identifier for the parameter that configures H.W delay
 * for hardware interfaces.

    @msgpayload
    param_id_hw_delay_t
*/
#define PARAM_ID_HW_DELAY 0x080013D5

/*# @h2xmlp_parameter   {"PARAM_ID_HW_DELAY",
                          PARAM_ID_HW_DELAY}
    @h2xmlp_description {ID for the parameter that configures H.W delay
                         for hardware interfaces.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_hwep_common_mods
    Payload of the #PARAM_ID_HW_DELAY parameter.
 */
#include "spf_begin_pack.h"
struct param_id_hw_delay_t
{
   uint32_t hw_delay_us;
   /**< Indicates the H.W delay in micro seconds. */

   /*#< @h2xmle_description {Indicates the H.W delay in micro seconds.}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_hw_delay_t param_id_hw_delay_t;

#endif /* HW_INTF_CMN_API */
