#ifndef _PULL_PUSH_MODE_API_H_
#define _PULL_PUSH_MODE_API_H_
/**
 * \file sh_mem_pull_push_mode_api.h
 * \brief
 *    This file contains CAPI Pull mode and Push mode module APIs
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*------------------------------------------------------------------------
  Include files
 -----------------------------------------------------------------------*/
#include "ar_defs.h"
#include "apm_container_api.h"
#include "module_cmn_api.h"
#include "media_fmt_api.h"

/*# @h2xml_title1          {Shared Memory Pull and Push Modules}
    @h2xml_title_agile_rev {Shared Memory Pull and Push Modules}
    @h2xml_title_date      {March 29, 2019} */


/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the maximum number of input ports for Pull mode. */
#define SH_MEM_PULL_MODE_MAX_INPUT_PORTS                  0x0

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the maximum number of output ports for Pull mode. */
#define SH_MEM_PULL_MODE_MAX_OUTPUT_PORTS                 0x1

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the stack size of the module for Pull mode. */
#define SH_MEM_PULL_MODE_STACK_SIZE_REQUIREMENT           4096

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the maximum number of input ports for Push mode. */
#define SH_MEM_PUSH_MODE_MAX_INPUT_PORTS                  0x1

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the maximum number of output ports for Push mode. */
#define SH_MEM_PUSH_MODE_MAX_OUTPUT_PORTS                 0x0

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Enumerates the stack size of the module for Push mode. @newpage */
#define SH_MEM_PUSH_MODE_STACK_SIZE_REQUIREMENT           4096

/*# @h2xmlx_xmlNumberFormat {int} */

/*==============================================================================
   Event ID
==============================================================================*/

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier of the watermark event from the #MODULE_ID_SH_MEM_PULL_MODE and
    #MODULE_ID_SH_MEM_PUSH_MODE modules to the client.

    @msgpayload
    Payload of the configuration that the client is to register with the module
    through APM_CMD_REGISTER_MODULE_EVENTS . \n
    event_cfg_sh_mem_pull_push_mode_watermark_t
    @par
    @indent{12pt} Payload of the event raised by the module to the client
                  through \n
    @indent{12pt} APM_CMD_REGISTER_MODULE_EVENTS. \n
    @indent{12pt} event_sh_mem_pull_push_mode_watermark_level_t
    @par
    @note1hang For information about the APM events, see the AudioReach SPF
    API Reference (80-VN500-5).
 */
#define EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK 0x0800101C

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Payload of the #EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK event.
*/
#include "spf_begin_pack.h"
struct event_cfg_sh_mem_pull_push_mode_watermark_level_t
{
   uint32_t watermark_level_bytes;
   /**< Watermark level in the circular buffer, in bytes.

        When data in the buffer reaches this level, the module informs the
        client by raising a watermark event. */

   /*#< @h2xmle_description {Watermark level in the circular buffer, in bytes.
                             When data in the buffer reaches this level, the
                             module informs the client by raising a watermark
                             event.}
        @h2xmle_policy      {Basic}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct event_cfg_sh_mem_pull_push_mode_watermark_level_t event_cfg_sh_mem_pull_push_mode_watermark_level_t;


/** @ingroup ar_spf_mod_ep_shmempp_mods
    Immediately following this structure are watermark level structures of type
    sh_mem_pull_push_mode_watermark_level_t.

    Following is the overall structure:

    @code
    {
      event_cfg_sh_mem_pull_push_mode_watermark_t a;
      event_cfg_sh_mem_pull_push_mode_watermark_level_t[num_water_mark_levels];
    }
    @endcode @vertspace{6}
*/
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct event_cfg_sh_mem_pull_push_mode_watermark_t
{
   uint32_t num_water_mark_levels;
   /**< Number of watermark levels in the array.

        If set to zero, no watermark levels are specified, and hence no
        watermark events are supported. */

   /*#< @h2xmle_description {Number of watermark level structures in the array.
                             If set to zero, no watermark levels are specified,
                             and hence no watermark events are supported.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

#if defined(__H2XML__)
   event_cfg_sh_mem_pull_push_mode_watermark_level_t levels[0];
   /**< Array of watermark levels of size num_water_mark_levels. */

   /*#< @h2xmle_description       {Array of watermark levels of size
                                   num_water_mark_levels.}
        @h2xmle_variableArraySize {num_water_mark_levels}
        @h2xmle_policy            {Basic} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct event_cfg_sh_mem_pull_push_mode_watermark_t event_cfg_sh_mem_pull_push_mode_watermark_t;


/** @ingroup ar_spf_mod_ep_shmempp_mods
    Payload of the #EVENT_ID_SH_MEM_PULL_PUSH_MODE_WATERMARK event.
*/
#include "spf_begin_pack.h"
struct event_sh_mem_pull_push_mode_watermark_level_t
{
   uint32_t    watermark_level_bytes;
   /**< Watermark level in the circular buffer in bytes.

        When data in the buffer reaches this level, the module informs the
        client by raising a watermark event. @newpagetable */

   /*#< h2xmle_description {Watermark level in the circular buffer in bytes.
                            When data in the buffer reaches this level, the
                            module informs the client by raising a watermark
                            event.}
        h2xmle_policy      {Basic}
        h2xmle_default     {0}
        h2xmle_range       {0..0xFFFFFFFF}
        h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct event_sh_mem_pull_push_mode_watermark_level_t event_sh_mem_pull_push_mode_watermark_level_t;

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier of the EOS marker event from the #MODULE_ID_SH_MEM_PUSH_MODE modules to the client.
    EOS marker payload has the timestamp info when the Push mode module receives end of the stream, which
    inturn indicates the TS till which client can read the current stream from circular buffer.

    The client must exercise this event with caution and cannot assume has absolutely ended here.
    Event only indicates that the stream momentarily ended at this timestamp. The data flow can always resume
    if the upstream graph of the push mode graph remains started. So there is no synchronization promised with
    respect to the data flow and EOS event.

    @msgpayload
    Client needs to register with the module through APM_CMD_REGISTER_MODULE_EVENTS. No
    payload required for this event's registration.\n

    @par
    @indent{12pt} Payload of the event raised by the module to the client
                  through \n
    @indent{12pt} APM_CMD_REGISTER_MODULE_EVENTS. \n
    @indent{12pt} event_sh_mem_push_mode_eos_marker_t
    @par
    @note1hang For information about the APM events, see the AudioReach SPF
    API Reference (80-VN500-5).
 */
#define EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER    0x080013DD

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Payload of the event EVENT_ID_SH_MEM_PUSH_MODE_EOS_MARKER, contains the timestamp of the EOS marker. */
#include "spf_begin_pack.h"
struct event_sh_mem_push_mode_eos_marker_t
{
    uint32_t index;
   /**< Index of the sample in the circular buffer at which EOS marker is set.

        Range for the index:
        0 @ge index \> shared_circ_buf_size - 1 */

   /*#< @h2xmle_description {Index of the sample in the circular buffer at which EOS marker is set. \n
                             Range for the index: 0 &ge; index &gt; shared_circ_buf_size - 1}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t timestamp_us_lsw;
   /**< Lower 32 bits of Timestamp at which push mode module received EOS. */

   /*#< @h2xmle_description {Lower 32 bits of Timestamp at which push mode module gets EOS. }
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t timestamp_us_msw;
   /**< Higher 32 bits of Timestamp at which push mode module received EOS. */

   /*#< @h2xmle_description {Higher 32 bits of Timestamp at which push mode module gets EOS. }
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct event_sh_mem_push_mode_eos_marker_t event_sh_mem_push_mode_eos_marker_t;

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Common structure definition for the shared circular buffer between the SPF
    and clients. Both Pull mode and Push mode modules support this structure. */
#include "spf_begin_pack.h"
struct sh_mem_pull_push_mode_position_buffer_t
{
   volatile uint32_t frame_counter;
   /**< Counter used to handle interprocessor synchronization issues associated
        with reading write_index, wall_clock_us_lsw, and wall_clock_us_msw.
        These fields are invalid when frame_counter=0.

        Read the frame_counter value both before and after reading these values
        to ensure that the SPF did not update them while the client was reading
        them. */

   /*#< @h2xmle_description {Counter used to handle interprocessor
                             synchronization issues associated with reading
                             write_index, wall_clock_us_lsw, and
                             wall_clock_us_msw. These fields are invalid when
                             frame_counter=0. \n
                             Read the frame_counter value both before and after
                             reading these values to ensure that the SPF did
                             not update them while the client was reading
                             them.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   volatile uint32_t index;
   /**< Index (in bytes) to the buffer where the SPF is writing (Push mode) or
        reading (Pull mode).

        Range for the index:
        0 @ge index \> sh_mem_pull_push_mode_cfg_t::shared_circ_buf_size - 1 */

   /*#< @h2xmle_description {Index (in bytes) to the buffer where the SPF is
                             writing (Push mode) or reading (Pull mode). \n
                             Range for the index: 0 &ge; index &gt;
                             sh_mem_pull_push_mode_cfg_t::
                             shared_circ_buf_size - 1}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   volatile uint32_t timestamp_us_lsw;
   /**< Upper 32 bits of the 64-bit timestamp, in microseconds.

        For Pull mode, the timestamp is the timestamp at which the index was
        updated. For Push mode, the timestamp is the buffer or capture
        timestamp of the sample at the index. */

   /*#< @h2xmle_description {Upper 32 bits of the 64-bit timestamp, in
                             microseconds. \n
                             For Pull mode, the timestamp is the timestamp at
                             which the index was updated. \n
                             For Push mode, the timestamp is the buffer or
                             capture timestamp of the sample at the index.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   volatile uint32_t timestamp_us_msw;
   /**< Upper 32 bits of the 64-bit timestamp, in microseconds.

        For Pull mode, the timestamp is the timestamp at which the index was
        updated. For Push mode, the timestamp is the buffer or the capture
        timestamp of the sample at the index. */

   /*#< @h2xmle_description {Upper 32 bits of the 64-bit timestamp, in
                             microseconds. \n
                             For Pull mode, the timestamp is the timestamp at
                             which the index was updated. \n
                             For Push mode, the timestamp is the buffer or the
                             capture timestamp of the sample at the index.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct sh_mem_pull_push_mode_position_buffer_t sh_mem_pull_push_mode_position_buffer_t;

/*==============================================================================
   Param ID
==============================================================================*/

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier of the parameter that configures shared memory information for
    Pull mode and Push mode.

    Before issuing this Set parameter:
    - APM_CMD_SHARED_MEM_MAP_REGIONS must be issued (see the AudioReach SPF
      API Reference (80-VN500-5))
    - Memory for the position buffer must be mapped as uncached
    - Memory for the shared circular buffer must be mapped as uncached for
      Push mode and cached for Pull mode

    @msgpayload
    sh_mem_pull_push_mode_cfg_t @newpage
 */
#define PARAM_ID_SH_MEM_PULL_PUSH_MODE_CFG 0x0800100A

/*==============================================================================
   Param structure definitions
==============================================================================*/

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Payload of the #PARAM_ID_SH_MEM_PULL_PUSH_MODE_CFG parameter.
*/
#include "spf_begin_pack.h"
struct sh_mem_pull_push_mode_cfg_t
{
   uint32_t shared_circ_buf_addr_lsw;
   /**< Lower 32 bits of the address of the shared circular buffer. */

   /*#< @h2xmle_description {Lower 32 bits of the address of the shared
                             circular buffer.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t shared_circ_buf_addr_msw;
   /**< Upper 32 bits of the address of the shared circular buffer. */

   /*#< @h2xmle_description {Upper 32 bits of the address of the shared
                             circular buffer.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t shared_circ_buf_size;
   /**< Number of bytes in the shared circular buffer.

        The value must be an integral multiple of the number of (bits per
        sample * number of channels). */

   /*#< @h2xmle_description {Number of bytes in the shared circular buffer.
                             The value must be an integral multiple of the
                             number of (bits per sample * number of channels).}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t circ_buf_mem_map_handle;
   /**< Unique identifier for the shared memory address of shared circular
        buffer.

        The SPF returns this handle through APM_CMD_RSP_SHARED_MEM_MAP_REGIONS
        (see the AudioReach SPF CAPI API Reference (80-VN500-6)). */

   /*#< @h2xmle_description {Unique ID of the shared memory address of shared
                             circular buffer. The SPF returns this handle
                             through APM_CMD_RSP_SHARED_MEM_MAP_REGIONS
                             (see the AudioReach SPF CAPI API Reference
                             (80-VN500-6)).}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t shared_pos_buf_addr_lsw;
   /**< Lower 32 bits of the address of the shared position buffer. */

   /*#< @h2xmle_description {Lower 32 bits of the address of the shared
                             position buffer.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t shared_pos_buf_addr_msw;
   /**< Upper 32 bits of the address of the shared position buffer. */

   /*#< @h2xmle_description {Upper 32 bits of the address of the shared
                             position buffer.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

   uint32_t pos_buf_mem_map_handle;
   /**< Unique identifier for the shared memory address of position buffer.

        The SPF returns this handle through APM_CMD_RSP_SHARED_MEM_MAP_REGIONS
        (see the AudioReach SPF CAPI API Reference (80-VN500-6)). */

   /*#< @h2xmle_description {Unique ID of the shared memory address of position
                             buffer. The SPF returns this handle through
                             APM_CMD_RSP_SHARED_MEM_MAP_REGIONS (see the
                             AudioReach SPF CAPI API Reference
                             (80-VN500-6)).}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct sh_mem_pull_push_mode_cfg_t sh_mem_pull_push_mode_cfg_t;

/*==============================================================================
   Param ID
==============================================================================*/

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier of the parameter that configures header information for Push mode.
    This parameter enables timestamp information to be written alongside PCM data
    in the shared circular buffer.

    @msgpayload
    sh_mem_push_mode_header_cfg_t
 */
#define PARAM_ID_SH_MEM_PUSH_MODE_HEADER_CFG 0x08001BDF

/*==============================================================================
   Param structure definitions
==============================================================================*/
#include "spf_begin_pack.h"
struct sh_mem_push_mode_header_cfg_t
{
   uint32_t header_type;
   /**< Bit field to enable/disable additional information to be sent along with
        the audio data in the buffer.

        Bit 0: Include UTC timestamp (0=disabled, 1=enabled)
        Bits 1-31: Reserved for future use*/

   /*#< @h2xmle_description {Bit field to enable/disable additional header information.
                             Bit 0: UTC timestamp
                             Bits 1-31: Reserved}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */
}
#include "spf_end_pack.h"
;
typedef struct sh_mem_push_mode_header_cfg_t sh_mem_push_mode_header_cfg_t;

/*==============================================================================
   Header Structure Definitions
==============================================================================*/

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Sync word for header validation. Value is ASCII "SPFM" (0x5350464D). */
#define SH_MEM_PUSH_MODE_HEADER_SYNC_WORD  0x5350464D

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Batch header structure that appears at the start of each batch in shared memory.
    Contains sync word for validation. */
struct sh_mem_push_mode_batch_header_t
{
   uint32_t sync_word;
   /**< Sync word for validation. Must be 0x5350464D ("SPFM"). */
};

typedef struct sh_mem_push_mode_batch_header_t sh_mem_push_mode_batch_header_t;

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Parameter header type information structure (PARAM_ID + SIZE).
    This header precedes each header type data/payload in the batch header. */
struct sh_mem_push_mode_param_header_t
{
   uint32_t param_id;
   /**< Parameter ID identifying the type of data that follows. */

   uint32_t actual_size;
   /**< Actual size of the payload in bytes */

   uint32_t padding_size;
   /**< Size of the padded bytes in param payload, to make next header SYNC_WORD 4-byte aligned. */
};

typedef struct sh_mem_push_mode_param_header_t sh_mem_push_mode_param_header_t;

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Header Parameter ID for UTC timestamp. */
#define PARAM_ID_HEADER_TYPE_UTC_TIMESTAMP  0x08001BE0

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Timestamp parameter payload structure.
    Can be used for both Propagated timestamp and UTC timestamp parameters. */
struct sh_mem_push_mode_timestamp_payload_t
{
   uint32_t timestamp_us_lsw;
   /**< Lower 32 bits of the 64-bit timestamp, in microseconds*/

   uint32_t timestamp_us_msw;
   /**< Upper 32 bits of the 64-bit timestamp, in microseconds*/
};

typedef struct sh_mem_push_mode_timestamp_payload_t sh_mem_push_mode_timestamp_payload_t;

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Header Parameter ID for PCM data. */
#define PARAM_ID_HEADER_TYPE_PCM_DATA  0x08001BE1

/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier for the Shared Memory Pull mode module, which writes data to the
    SPF from the host through a shared circular buffer.

    This module has only one static output port with ID 1.

    The module does not work unless it receives #PARAM_ID_MEDIA_FORMAT.

    @subhead4{Supported input media format ID}
    - Data format       : #DATA_FORMAT_FIXED_POINT @lstsp1
    - fmt_id            : #MEDIA_FMT_ID_PCM @lstsp1
    - Sample rates      : 1..384 kHz @lstsp1
    - Number of channels: 1 to 128 (for certain products this module supports only 32 channels) @lstsp1
    - Bit width: @lstsep
       - 16 bits per sample in Q15 format @lstsp2
       - 24 bits per sample in Q23 format; 32 bits per sample in Q23, Q27, or
         Q31 format @lstsp2
       - 32 bits per sample in Q31 format @lstsp1
    - Interleaving: @lstsep
       - Interleaved @lstsp2
       - De-interleaved unpacked @lstsp2
       - De-interleaved packed @lstsp1
    - Endianness: little, big
 */
#define MODULE_ID_SH_MEM_PULL_MODE 0x07001006

/*# @h2xmlm_module             {"MODULE_ID_SH_MEM_PULL_MODE",
                                 MODULE_ID_SH_MEM_PULL_MODE}
    @h2xmlm_displayName        {"Shared Memory Pull Mode Endpoint"}
    @h2xmlm_modSearchKeys      {software, source}
    @h2xmlm_description        {ID for the module that writes data to the SPF
                                from the host through a shared circular
                                buffer. \n
                                This module has only one static output port
                                with ID 1. For more details, see the
                                AudioReach Module User Guide
                                (80-VN500-4).}
    @h2xmlm_dataMaxInputPorts  {SH_MEM_PULL_MODE_MAX_INPUT_PORTS}
    @h2xmlm_dataMaxOutputPorts {SH_MEM_PULL_MODE_MAX_OUTPUT_PORTS}
    @h2xmlm_dataOutputPorts    {OUT=1}
    @h2xmlm_supportedContTypes {APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC,APM_CONTAINER_TYPE_PTC}
    @h2xmlm_isOffloadable      {false}
    @h2xmlm_stackSize          {SH_MEM_PULL_MODE_STACK_SIZE_REQUIREMENT}
    @h2xmlm_toolPolicy         {Calibration}

    @{                      <-- Start of the Module -->
    @h2xml_Select           {"sh_mem_pull_push_mode_cfg_t"}
    @h2xmlm_InsertParameter
    @h2xml_Select           {"event_cfg_sh_mem_pull_push_mode_watermark_t"}
    @h2xmlm_InsertParameter
    @h2xml_Select           {"media_format_t"}
    @h2xmlm_InsertParameter
    @h2xml_Select           {"param_id_module_data_interleaving_t"}
    @h2xmlm_InsertParameter
    @}                      <-- End of the Module -->*/


/** @ingroup ar_spf_mod_ep_shmempp_mods
    Identifier for the shared memory Push mode module, which reads data from
    the SPF to the host through a shared circular buffer.

    This module has only one static input port with ID 2.

    @subhead4{Supported input media format ID}
    - Data format       : #DATA_FORMAT_FIXED_POINT @lstsp1
    - fmt_id            : Don't care @lstsp1
    - Sample rates      : 1..384 kHz @lstsp1
    - Number of channels: @lstsep
       - 16 bits per sample in Q15 format @lstsp2
       - 24 bits per sample in Q23 format; 32 bits per sample in Q23, Q27, or
         Q31 format @lstsp2
       - 32 bits per sample in Q31 format @lstsp1
    - Interleaving: @lstsep
       - Interleaved @lstsp2
       - De-interleaved unpacked @lstsp2
       - De-interleaved packed @lstsp1
    - Endianness: little, big @newpage
 */
#define MODULE_ID_SH_MEM_PUSH_MODE 0x07001007

/*# @h2xmlm_module             {"MODULE_ID_SH_MEM_PUSH_MODE",
                                 MODULE_ID_SH_MEM_PUSH_MODE}
    @h2xmlm_displayName        {"Shared Memory Push-Mode Endpoint"}
    @h2xmlm_modSearchKeys      {software}
    @h2xmlm_description        {ID for the shared memory Push mode module,
                                which reads data from the SPF to the host
                                through a shared circular buffer. \n
                                This module has only one static input port
                                with ID 2. For more details, see the
                                AudioReach Module User Guide
                                (80-VN500-4).}
    @h2xmlm_dataMaxInputPorts  {SH_MEM_PUSH_MODE_MAX_INPUT_PORTS}
    @h2xmlm_dataInputPorts     {IN=2}
    @h2xmlm_dataMaxOutputPorts {SH_MEM_PUSH_MODE_MAX_OUTPUT_PORTS}
    @h2xmlm_supportedContTypes {APM_CONTAINER_TYPE_SC, APM_CONTAINER_TYPE_GC,APM_CONTAINER_TYPE_PTC}
    @h2xmlm_stackSize          {SH_MEM_PUSH_MODE_STACK_SIZE_REQUIREMENT}
    @h2xmlm_toolPolicy         {Calibration}

    @{                      <-- Start of the Module -->
    @h2xml_Select           {"sh_mem_pull_push_mode_cfg_t"}
    @h2xmlm_InsertParameter
    @h2xml_Select           {"event_cfg_sh_mem_pull_push_mode_watermark_t"}
    @h2xmlm_InsertParameter
    @h2xml_Select           {"sh_mem_push_mode_header_cfg_t"}
    @h2xmlm_InsertParameter
    @}                      <-- End of the Module --> */


#endif // _PULL_PUSH_MODE_API_H_
