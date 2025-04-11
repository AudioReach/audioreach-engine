#ifndef _SPR_BUFFER_API_H_
#define _SPR_BUFFER_API_H_
/**
 * \file spr_api.h
 * \brief
 *    This file contains Public APIs for Splitter-Renderer module.
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ar_defs.h"
#include "module_cmn_api.h"
#include "imcl_fwk_intent_api.h"

/*# @h2xml_title1          {Splitter Renderer Module}
    @h2xml_title_agile_rev {Splitter Renderer Module}
    @h2xml_title_date      {March 19, 2019} */


/*# @h2xmlp_subStruct */

/** @ingroup ar_spf_mod_spr_mod
    Defines the timestamp value.
 */
struct time_us_t
{
   uint32_t value_lsw;
   /**< Lower 32 bits of 64-bit time value in microseconds. */

   /*#< @h2xmle_description {Lower 32 bits of 64-bit time value in
                             microseconds.}
        @h2xmle_range       {0..0xFFFFFFFF} */

   uint32_t value_msw;
   /**< Upper 32 bits of 64-bit time value in microseconds. */

   /*#< @h2xmle_description {Upper 32 bits of 64-bit time value in
                             microseconds.}
        @h2xmle_range       {0..0xFFFFFFFF} */
};
typedef struct time_us_t time_us_t;


/** @ingroup ar_spf_mod_spr_macros
    Indicates that the render start time is immediate. Data is rendered as soon
    as it is available after the render decision. */
#define SPR_RENDER_MODE_IMMEDIATE                         1

/** @ingroup ar_spf_mod_spr_macros
    Indicates that the render start time is at given value of absolute time.
    This value is the 64-bit absolute wall clock time (in microseconds). */
#define SPR_RENDER_MODE_ABSOLUTE_TIME                     2

/** @ingroup ar_spf_mod_spr_macros
    Indicates that the render start time is at a specified offset from the
    current absolute time wall clock time. This value is a 64-bit time for
    render delay (in microseconds). */
#define SPR_RENDER_MODE_DELAYED                           3


/*# @h2xmlp_subStruct */

/** @ingroup ar_spf_mod_spr_mod
    Contains configuration information for the Render mode.
 */
struct config_spr_render_mode_t
{
   uint32_t render_mode;
   /**< Mode that indicates when to start rendering data.

        @valuesbul
        - #SPR_RENDER_MODE_IMMEDIATE (Default)
        - #SPR_RENDER_MODE_ABSOLUTE_TIME
        - #SPR_RENDER_MODE_DELAYED @tablebulletend */

   /*#< @h2xmle_description {Mode that indicates when to start rendering data.}
        @h2xmle_rangeList   {"SPR_RENDER_MODE_IMMEDIATE"=1;
                             "SPR_RENDER_MODE_ABSOLUTE_TIME"=2;
                             "SPR_RENDER_MODE_DELAYED"=3}
        @h2xmle_default     {1} */

   time_us_t render_start_time;
   /**< Render start time in microseconds. This value is based on the value of
        render_mode field. */

   /*#< @h2xmle_description {Render start time in microseconds. This value is
                             based on the value of the render_mode field.} */
};
typedef struct config_spr_render_mode_t config_spr_render_mode_t;


/** @ingroup ar_spf_mod_spr_macros
    MSW of the sign-extended default time (-infinity) for the render window
    start time. */
#define DEFAULT_RENDER_WINDOW_START_MSW                   AR_NON_GUID(0x80000000)

/** @ingroup ar_spf_mod_spr_macros
    LSW of the sign-extended default time (-infinity) for the render window
    start time. */
#define DEFAULT_RENDER_WINDOW_START_LSW                   AR_NON_GUID(0x00000000)

/** @ingroup ar_spf_mod_spr_macros
    MSW of the sign-extended default time (-infinity) for the render window end
    time. */
#define DEFAULT_RENDER_WINDOW_END_MSW                     AR_NON_GUID(0x7FFFFFFF)

/** @ingroup ar_spf_mod_spr_macros
    LSW of the sign extended default time (-infinity) for the render window end
    time. */
#define DEFAULT_RENDER_WINDOW_END_LSW                     AR_NON_GUID(0xFFFFFFFF)


/*# @h2xmlp_subStruct */

/** @ingroup ar_spf_mod_spr_mod
    Contains configuration information for the render window.
 */
struct config_spr_render_window_t
{
   time_us_t render_window_start;
   /**< Start of the tolerance window for incoming timestamps used to make
        rendering decisions. Timestamps behind the current window start are
        held.

        This value is supported only in resolutions of microseconds.

        The 64-bit render window value is treated as a signed value. The client
        who is sending this configuration to the module is responsible for the
        sign extension. */

   /*#< @h2xmle_description {Start of the tolerance window for incoming
                             timestamps used to make rendering decisions.
                             Timestamps behind the current window start are
                             held. \n
                             This value is supported only in resolutions of
                             microseconds. The 64-bit render window value is
                             treated as signed value. The client who is sending
                             this configuration to the module is responsible
                             for the sign extension.} */

   time_us_t render_window_end;
   /**< End of the tolerance window for incoming timestamps used to make
        rendering decisions. Timestamps ahead of current window end are
        dropped.

        This value is supported only in resolutions of microseconds.

        The 64-bit render window value is treated as a signed value. The client
        who is sending this configuration to the module is responsible for the
        sign extension. */

   /*#< @h2xmle_description {End of the tolerance window for incoming
                             timestamps used to make rendering decisions.
                             Timestamps ahead of current window end are
                             dropped. \n
                             This value is supported only in resolutions of
                             microseconds. The 64-bit render window value is
                             treated as signed value. The client who is sending
                             this configuration to the module is responsible
                             for the sign extension.} */
};
typedef struct config_spr_render_window_t config_spr_render_window_t;


/** @ingroup ar_spf_mod_spr_macros
    Indicates that the render decision is based on the rendering rate of the
    primary device (or the session clock mode) served by the SPF module. */
#define SPR_RENDER_REFERENCE_DEFAULT                      1

/** @ingroup ar_spf_mod_spr_macros
    Indicates that the render decision is based on the local wall clock
    reference. */
#define SPR_RENDER_REFERENCE_WALL_CLOCK                   2


/*# @h2xmlp_subStruct */

/** @ingroup ar_spf_mod_spr_mod
    Contains configuration information for the render decision reference.
 */
struct config_spr_render_reference_t
{
   uint32_t render_reference;
   /**< Indicates the choice of render decision reference for incoming data.

        @valuesbul
        - #SPR_RENDER_REFERENCE_DEFAULT
        - #SPR_RENDER_REFERENCE_WALL_CLOCK

        For loopback use cases, we recommend the wall clock value. */

   /*#< @h2xmle_description {Indicates the choice of render decision reference
                             for incoming data. In default mode, the decision
                             is based on the rendering rate of the primary
                             device served by the SPR.
                             In local wall clock mode, the decision is based on
                             the local wall clock. We recommend this mode for
                             loopback usecases.}
        @h2xmle_rangeList   {"SPR_RENDER_REFERENCE_DEFAULT"=1;
                             "SPR_RENDER_REFERENCE_WALL_CLOCK"=2}
        @h2xmle_default     {1} */
};
typedef struct config_spr_render_reference_t config_spr_render_reference_t;


/*# @h2xmlp_subStruct */

/** @ingroup ar_spf_mod_spr_mod
    Defines the duration of the hold buffer when a render decision is to hold
    data in the buffer.
 */
struct config_spr_render_hold_duration_t
{
   time_us_t max_hold_buffer_duration;
   /**< Maximum size (in microseconds) of the hold buffer.

        If the client does not set the max_hold_buffer_duration, the module
        assumes a default size of 150 ms. */

   /*#< @h2xmle_description {Maximum size (in microseconds) of the hold buffer
                             when a render decision is to hold data in the
                             buffer. If the client does not set the
                             max_hold_buffer_duration, the module assumes a
                             default size of 150 ms.}
        @h2xmle_default     {0} */
};
typedef struct config_spr_render_hold_duration_t config_spr_render_hold_duration_t;


/** @ingroup ar_spf_mod_spr_macros
    Definition of the Render mode bitmask. */
#define SPR_RENDER_BIT_MASK_RENDER_MODE                   AR_NON_GUID(0x00000001)

/** @ingroup ar_spf_mod_spr_macros
    Definition of the Render mode shift value. */
#define SPR_RENDER_SHIFT_RENDER_MODE                      0

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render reference bitmask. */
#define SPR_RENDER_BIT_MASK_RENDER_REFERENCE              AR_NON_GUID(0x00000002)

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render reference shift value. */
#define SPR_RENDER_SHIFT_RENDER_REFERENCE                 1

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render window bitmask. */
#define SPR_RENDER_BIT_MASK_RENDER_WINDOW                 AR_NON_GUID(0x00000004)

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render window shift value. */
#define SPR_RENDER_SHIFT_RENDER_WINDOW                    2

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render hold duration. */
#define SPR_RENDER_BIT_MASK_HOLD_DURATION                 AR_NON_GUID(0x00000008)

/** @ingroup ar_spf_mod_spr_macros
    Definition of the render hold duration shift value. */
#define SPR_RENDER_SHIFT_HOLD_DURATION                    3

/** Indicates Non-time stamp honor mode can be enabled or not. */
#define SPR_RENDER_BIT_MASK_ALLOW_NON_TIMESTAMP_HONOR_MODE AR_NON_GUID(0x00000010)

/** Definition of Non-time stamp honor mode shift value. */
#define SPR_RENDER_SHIFT_ALLOW_NONTIMESTAMP_HONOR_MODE  4

/** @ingroup ar_spf_mod_spr_mod
    ID of the parameter that defines end of the delay path.

    The SPR module requires a path delay to perform accurate AV synchronization
    or return the session time to the client. A path is defined as a series of
    connections from one module to another through which data flows. Path delay
    is the sum of the various delays suffered by the data as it flows through
    the path. Contributors to path delay are algorithmic delays, buffering
    delays, and so on.

    For the SPR module, the required path delay is the aggregation of delay
    from the SPR to the endpoint module. The SPR must be told  about the
    endpoint through this API.

    For the delay to be accurate, it must be automatically calculated, which is
    possible with this API.
    - The SPR might have multiple output ports.
    - The SPF searches for and finds the appropriate output port that leads to
      the given endpoint.
    - If there are multiple paths that lead to the same endpoint (due to splits
      and merges), the first path is chosen (which can be arbitrary).

    For multiple endpoints that are fed from the SPR module, the same parameter
    is sent multiple times. The SPR will store delay per output port by
    auto-detecting the output port that corresponds to the endpoint. If two
    different endpoints are fed from same output port (due to a splitter that
    is downstream of the SPR), the SPR uses the latest endpoint.

    @msgpayload
    param_id_spr_delay_path_end_t @newpage
  */
#define PARAM_ID_SPR_DELAY_PATH_END 0x080010C4

/*# @h2xmlp_parameter   {"PARAM_ID_SPR_DELAY_PATH_END",
                          PARAM_ID_SPR_DELAY_PATH_END}
    @h2xmlp_description {ID for the parameter that defines end of the delay
                         path. For more details, see AudioReach Signal Processing
                         Framework (SPF) API Reference.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #PARAM_ID_SPR_DELAY_PATH_END parameter.
 */
#include "spf_begin_pack.h"
struct param_id_spr_delay_path_end_t
{
   uint32_t module_instance_id;
   /**< Identifier for the module instance ID at the end of the path used for
        delay calculations. */

   /*#< @h2xmle_description {ID for the module instance at the end of the path
                             used for delay calculations.}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_spr_delay_path_end_t param_id_spr_delay_path_end_t;

/*==============================================================================
   Constants
==============================================================================*/

/** @ingroup ar_spf_mod_spr_macros
    Bitmask for the Timestamp Valid flag in param_id_session_time_t. */
#define PARAM_ID_SESSION_TIME_BIT_MASK_IS_TIMESTAMP_VALID AR_NON_GUID(0x80000000UL)

/** @ingroup ar_spf_mod_spr_macros
    Shift value for the Timestamp Valid flag in param_id_session_time_t. */
#define PARAM_ID_SESSION_TIME_SHIFT_IS_TIMESTAMP_VALID    31

/*==============================================================================
   API definitions
==============================================================================*/

/** @ingroup ar_spf_mod_spr_mod
    Identifier for the Get parameter used to query the session time. This
    parameter provides information related to the current session time.

    The session time value must be interpreted by clients as a signed 64-bit
    value. A negative session time can be returned when the client is to start
    running a session in the future, but first queries for the session
    clock. The timestamp field is populated using the last processed timestamp.

    This field follows certain rules:
    - For an uninitialized or invalid timestamp, the value reads 0, and the
      flag that follows this timestamp field is set to 0 to indicate
      Uninitialized/Invalid.
    - During steady state rendering, the session time reflects the last
      processed timestamp.
    - Clients must interpret the timestamp value in the command response as an
      unsigned 64-bit value. The absolute time can be in the past or future.
    - If the client does not enable the AV synchronization feature via
      #PARAM_ID_SPR_AVSYNC_CONFIG, these values are returned as 0.

    @msgpayload
    param_id_spr_session_time_t @newpage
*/
#define PARAM_ID_SPR_SESSION_TIME                         0x0800113D

/*==============================================================================
   Type definitions
==============================================================================*/

/*# @h2xmlp_parameter   {"PARAM_ID_SPR_SESSION_TIME",
                          PARAM_ID_SPR_SESSION_TIME}
    @h2xmlp_description {Identifier for the Get parameter used to query the
                         session time. This parameter provides information
                         related to the current session time. For more
                         details, see AudioReach Signal Processing Framework
                         SPF) API Reference.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #PARAM_ID_SPR_SESSION_TIME parameter.
 */
#include "spf_begin_pack.h"
struct param_id_spr_session_time_t
{
   time_us_t session_time;
   /**< Value of the current session time in microseconds. */

   /*#< @h2xmle_description {Value of the current session time in
                             microseconds.}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   time_us_t absolute_time;
   /**< Value of the absolute time in microseconds when the sample that
        corresponds to the session time is rendered at the hardware.

        This time can be slightly in the future or past depending on when this
        parameter is queried. */

   /*#< @h2xmle_description {Value of the absolute time in microseconds when
                             the sample that corresponds to the session time is
                             rendered at the hardware. This time can be
                             slightly in the future or past depending on when
                             this parameter is queried.}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   time_us_t timestamp;
   /**< Value of the last processed timestamp in microseconds. The 64-bit
        number is treated as signed. */

   /*#< @h2xmle_description {Value of the last processed timestamp in
                             microseconds. The 64-bit number is treated as
                             signed.}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_default     {0} */

   uint32_t flags;
   /**< Configures additional properties associated with this payload.

        @valuesbul{for bit 31}
        - 0 -- Value of last processed timestamp is invalid/uninitialized
        - 1 -- Value of last processed timestamp is valid/initialized

        All other bits are reserved and are set to 0. */

   /*#< @h2xmle_description {Configures additional properties associated with
                             this payload. Bit 31 indicates whether the
                             timestamp is valid (1) or not (0). All other bits
                             are reserved and are set to 0.}
        @h2xmle_rangeList   {"TS_INVALID"=0x00000000;
                             "TS_VALID"=0x80000000}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_spr_session_time_t param_id_spr_session_time_t;

/** @ingroup ar_spf_mod_spr_macros
    Indicates that the SPR operates in the Default Mode where session_time is
    reset when
        1) Flush command is issued
        2) Gapless stream switch occurs */
#define SPR_SESSION_TIME_RESET_MODE_DEFAULT                      0

/** @ingroup ar_spf_mod_spr_macros
    Indicates that the SPR operates in the mode where session_time reset is
    skipped when Gapless stream switch occurs */
#define SPR_SESSION_TIME_SKIP_RESET_GAPLESS_SWITCH                         1

/** @ingroup ar_spf_mod_spr_mod
    Identifier for the parameter used to configure the SPR module behavior
    with respect to resetting session time.

    Guidelines for using this param are as below :-

      - This parameter has to be sent by the client explicitly when there
        is a need to change the default behavior of the SPR module.

      - Until the subgraph hosting the SPR is started, the client can set
        this parameter multiple times. The last configured value is taken
        into consideration.

        If a subgraph hosting SPR is stopped and started, the functionality
        proceeds with the previous configuration (if any). The client can
        configure this parameter before restarting the session to ensure the
        desired behavior behavior.

      - If no AVSync functionality is enabled via PARAM_ID_SPR_AVSYNC_CONFIG,
        then this configuration does not take effect.

      - If client is interested in obtaining the value of session time before reset,
        refer to #EVENT_ID_SPR_SESSION_TIME_RESET

    The mode field in this payload supports 2 values currently :-

      #SPR_SESSION_TIME_RESET_MODE_DEFAULT
         - This indicates that session time will be reset whenever there is
           a flush command or gapless stream switch.
         - The SPR will choose to operate in this default mode.

      #SPR_SESSION_TIME_SKIP_RESET_GAPLESS_SWITCH
         - This indicates that the session time will not be reset when
           gapless stream switch occurs.

    @msgpayload
    param_id_spr_session_time_t @newpage
*/
#define PARAM_ID_SPR_SESSION_TIME_RESET_INFO                         0x08001B07

/*==============================================================================
   Type definitions
==============================================================================*/

/*# @h2xmlp_parameter   {"PARAM_ID_SPR_SESSION_TIME_RESET_INFO",
                          PARAM_ID_SPR_SESSION_TIME_RESET_INFO}
    @h2xmlp_description {Identifier for the parameter used to configure the
                         SPR module behavior with respect to resetting session
                         time.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #PARAM_ID_SPR_SESSION_TIME_RESET_INFO parameter.
 */
#include "spf_begin_pack.h"
struct param_id_spr_session_time_reset_info_t
{
   uint32_t mode;
   /**< Indicates the configuration for handling session time reset.

        @valuesbul
          - #SPR_SESSION_TIME_RESET_MODE_DEFAULT
          - #SPR_SESSION_TIME_SKIP_RESET_GAPLESS_SWITCH  */


   /*#< @h2xmle_description {Mode of operation for session time reset}
        @h2xmle_rangeList   {"SPR_SESSION_TIME_RESET_MODE_DEFAULT"=0;
                             "SPR_SESSION_TIME_SKIP_RESET_GAPLESS_SWITCH"=1}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_spr_session_time_reset_info_t param_id_spr_session_time_reset_info_t;

/** @ingroup ar_spf_mod_spr_mod
    Identifier for the parameter that configures information required for
    AV synchronization functionalities in the SPR module, including a data
    render decision.

    The SPR module makes render decisions on incoming data. A render decision
    involves rendering (allowing data to output), and holding or dropping the
    incoming data.

    SPR module must know if the AVSync feature is to be enabled. If so, the
    following additional information is required:
    - When to render
    - What to gate or compare against
    - Render tolerance window

    Because the SPR performs gating of input to output, a Graph Start on the
    subgraph hosting the SPR module does not guarantee immediate data flow in
    the subgraph if the render decision parameter is not configured to render
    immediately.

    This parameter is expected to be set before the SPR module is moved to the
    Start state. If the parameter is not set by then, no AVSync functionality
    is enabled for the current run.

    If this parameter is set with enable=1 AND the client is to use the
    default values for all the other render parameters for this run, the
    expected payload values are enable=1 AND render_mask=0. The individual
    parameter  descriptions contain the default values.

    If the client is to configure fields specifically, it can set enable=1 and
    set up the render_mask to reflect the values that are to be updated by the
    SPR. For the fields that are not configured, the SPR chooses the default
    values.

    Once the subgraph hosting the SPR is moved to the Start state, changes in
    this configuration are not allowed except in the following case: If the
    client enabled AVSync before the Start state transition, only the render
    window is allowed to be updated at run time. Any other changes are rejected
    and result in a failure for the Set parameter command.

    Until the subgraph hosting the SPR is started, the client can set this
    parameter multiple times. The last configured value is taken into
    consideration.

    If a subgraph hosting SPR is stopped and started or paused and resumed, the
    AVSync functionality proceeds with the previous configuration (if any) and
    uses the first buffer to perform the first sample rendering. The client can
    configure this parameter before restarting the session to ensure the
    specified behavior.

    @note1hang This parameter supports only setting of the configuration, not
               getting the configuration.

    @msgpayload
    param_id_spr_avsync_config_t @newpage
*/
#define PARAM_ID_SPR_AVSYNC_CONFIG                        0x0800115B

/*# @h2xmlp_parameter   {"PARAM_ID_SPR_AVSYNC_CONFIG",
                          PARAM_ID_SPR_AVSYNC_CONFIG}
    @h2xmlp_description {ID for the parameter that configures information
                         required for the AVSync functionalities for the SPR
                         module, including a data render decision. For more
                         details, see AudioReach Signal Processing Framework
                         SPF) API Reference.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #PARAM_ID_SPR_AVSYNC_CONFIG parameter.
 */
#include "spf_begin_pack.h"
struct param_id_spr_avsync_config_t
{
   uint16_t enable;
   /**< Indicates if the AVSync functionality is to be enabled.

        @valuesbul
        - 0 -- Disable
        - 1 -- Enable

        This parameter can be enabled or disabled until the subgraph hosting
        the SPR container is started. The value of the variable most recently
        configured is used to decide whether the AVSync feature is to be
        enabled.

        If this field is set to 0, the rest of the values are ignored by the
        SPR and any configured fields are reset. */

   /*#< @h2xmle_description {Indicates if the AVSync functionality is to be
                             enabled (1) or not (0). \n
                             This parameter can be enabled or disabled until
                             the subgraph hosting the SPR container is started.
                             The value of the variable most recently configured
                             is used to decide whether the AVSync feature is to
                             be enabled. \n
                             If this field is set to 0, the rest of the values
                             are ignored by the SPR and any configured fields
                             are reset.}
        @h2xmle_rangeList   {"DISABLE"=0,
                             "ENABLE"=1}
        @h2xmle_default     {0} */

   uint16_t render_mask;
   /**< Render mask with bitfields to selectively configure fields.

        @tblsubhd{Bit 0}
        Configures the render mode for the SPR. This value is effective only
        if enable=1. @vertspace{-3}
        - 0 -- IGNORE
        - 1 -- CONFIGURE

        @tblsubhd{Bit 1}
        Configures the render reference for the SPR. This value is effective
        only if enable=1. @vertspace{-3}
        - 0 -- IGNORE
        - 1 -- CONFIGURE

       @tblsubhd{Bit 2}
        Configures the render window for the SPR. This value is effective only
        if enable=1. @vertspace{-3}
        - 0 -- IGNORE
        - 1 -- CONFIGURE

        @tblsubhd{Bit 3}
        Configures the maximum hold duration that is expected for the SPR. This
        value is effective only if enable=1. @vertspace{-3}
        - 0 -- IGNORE
        - 1 -- CONFIGURE

        @tblsubhd{Bits 15 to 4}
        Reserved bits that are set to 0.

        All other bits are reserved; clients must set them to 0. If any of
        these bits are set to IGNORE, the last configured value or the default
        value is used as long as AVSync enable=1. */

   /*#< @h2xmle_description {Render mask with bitfields to selectively
                             configure fields. All other bits are reserved;
                             clients must set them to 0. \n
                             If any of these bits are set to ignore, the last
                             configured value or the default value is used as
                             long as AVSync enable=1.}
        @h2xmle_range       {0x0000..0xFFFF}

        @h2xmle_bitField    {0x0001}
        @h2xmle_bitName     {Bit_0_Render_Mode_Config}
        @h2xmle_description {Configures the render mode for the SPR. This
                             value is effective only if enable=1.}
        @h2xmle_rangeList   {"IGNORE"=0,
                             "CONFIGURE"=1}
        @h2xmle_bitFieldEnd

        @h2xmle_bitField    {0x0002}
        @h2xmle_bitName     {Bit_1_Render_Reference_Config}
        @h2xmle_description {Configures the render reference for the SPR. This
                             value is effective only if enable=1. }
        @h2xmle_rangeList   {"IGNORE"=0,
                             "CONFIGURE"=1}
        @h2xmle_bitFieldEnd

        @h2xmle_bitField    {0x0004}
        @h2xmle_bitName     {Bit_2_Render_Window_Config}
        @h2xmle_description {Configures the render window for the SPR. This
                             value is effective only if enable=1.}
        @h2xmle_rangeList   {"IGNORE"=0,
                             "CONFIGURE"=1}
        @h2xmle_bitFieldEnd

        @h2xmle_bitField    {0x0008}
        @h2xmle_bitName     {Bit_3_Hold_Duration_Config}
        @h2xmle_description {Configures the maximum hold duration that is
                             expected for the SPR. This value is effective only
                             if enable=1.}
        @h2xmle_rangeList   {"IGNORE"=0,
                             "CONFIGURE"=1}
        @h2xmle_bitFieldEnd
        @h2xmle_bitFieldEnd

        @h2xmle_bitField      {0x0010}
          @h2xmle_bitName     {Bit_4_Allow_Non_TimeStamp_Honor_Mode}
          @h2xmle_description {Configures the non-timestamp honor mode}
          @h2xmle_rangeList   {"DISABLE"= 0,
                               "ENABLE" = 1}
        @h2xmle_bitFieldEnd

        @h2xmle_bitfield    {0xFFF0}
        @h2xmle_bitName     {Bit_15_to_4_RESERVED}
        @h2xmle_description {Reserved bits [15:4].}
        @h2xmle_rangeList   {"RESERVED"=0}
        @h2xmle_visibility  {hide}
        @h2xmle_bitfieldEnd */

   config_spr_render_mode_t render_mode_config;
   /**< Configuration parameter for the Render mode and start time. */

   /*#< @h2xmle_description {Configuration parameter for the Render mode and
                             start time.} */

   config_spr_render_reference_t render_reference_config;
   /**< Configuration parameter for the render reference. */

   /*#< @h2xmle_description {Configuration parameter for the render
                             reference.} */

   config_spr_render_window_t render_window;
   /**< Configuration parameter for the render window. */

   /*#< @h2xmle_description {Configuration parameter for the render window.} */

   config_spr_render_hold_duration_t render_hold_buffer_size;
   /**< Configuration parameter for the size of the render hold buffer. */

   /*#< @h2xmle_description {Configuration parameter for the size of the render
                             hold buffer.} */
}
#include "spf_end_pack.h"
;
typedef struct param_id_spr_avsync_config_t param_id_spr_avsync_config_t;


/*# @h2xmlp_subStruct
    @h2xmlp_description {Defines a control port-to-output data port mapping.
                         Each output port ID must be linked to only one control
                         port ID.} */

/** @ingroup ar_spf_mod_spr_mod
    Defines a control port-to-output data port mapping. Each output port ID
    must be linked to only one control port ID.
*/
#include "spf_begin_pack.h"
struct spr_port_binding_t
{
   uint32_t                control_port_id;
   /**< Identifier for the control port of the module. */

   /*#< @h2xmle_description {Control port ID of the module.}
        @h2xmle_default     {0} */

   uint32_t                output_port_id;
   /**< Identifier for the output port to which a control port is linked. */

   /*#< @h2xmle_description {Output port ID to which a control port is linked.}
        @h2xmle_default     {0} */
}
#include "spf_end_pack.h"
;
typedef struct spr_port_binding_t spr_port_binding_t;


/** @ingroup ar_spf_mod_spr_mod
    Identifier for the parameter that defines the control port-to-data port
    mapping.

    SPR control ports that are connected to the endpoint must be coupled to
    output port IDs. Each control port can be coupled to only one output port
    ID; thus, the number of bindings is same as the number of output ports.

    @msgpayload
    param_id_spr_ctrl_to_data_port_map_t
*/
#define PARAM_ID_SPR_CTRL_TO_DATA_PORT_MAP 0x0800113A

/*# @h2xmlp_parameter   {"PARAM_ID_SPR_CTRL_TO_DATA_PORT_MAP",
                          PARAM_ID_SPR_CTRL_TO_DATA_PORT_MAP}
    @h2xmlp_description {ID for the parameter that defines the control
                         port-to-data port mapping. \n
                         SPR control ports that are connected to the endpoint
                         must be coupled to output port IDs. Each control port
                         can be coupled to only one output port ID; thus, the
                         number of bindings is same as the number of output
                         ports.}
    @h2xmlp_toolPolicy  {Calibration} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #PARAM_ID_SPR_CTRL_TO_DATA_PORT_MAP parameter.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_spr_ctrl_to_data_port_map_t
{
   uint32_t               num_ctrl_ports;
   /**< Number of control port IDs attached to the module. */

   /*#< @h2xmle_description {Number of control port IDs attached to the
                             module.}
        @h2xmle_default     {0} */

#if defined(__H2XML__)
   spr_port_binding_t        ctrl_to_outport_map[0];
   /**< Array of control port-to-data port mappings of size num_ctrl_ports. */

   /*#< @h2xmle_description       {Array of control port-to-data port mappings
                                   of size num_ctrl_ports.}
        @h2xmle_variableArraySize {num_ctrl_ports} */
#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct param_id_spr_ctrl_to_data_port_map_t param_id_spr_ctrl_to_data_port_map_t;


/** @ingroup ar_spf_mod_spr_macros
    Indicates an underrun occurred because input is not available. */
#define UNDERRUN_STATUS_INPUT_NOT_AVAILABLE      0x1

/** @ingroup ar_spf_mod_spr_macros
    Indicates an underrun occurred because the input is held after the render
    decision (AVSync).
*/
#define UNDERRUN_STATUS_INPUT_HOLD               0x2

/** @ingroup ar_spf_mod_spr_macros
    Indicates an underrun occurred because input is dropped after the render
    decision (AVSync).
*/
#define UNDERRUN_STATUS_INPUT_DROP               0x3


/** @ingroup ar_spf_mod_spr_mod
    Identifier for the underrun event raised by the SPR module. A client can
    register to listen to this event.

    @msgpayload
    event_id_spr_underrun_t
*/
#define EVENT_ID_SPR_UNDERRUN                   0x080011E6

/*# @h2xmlp_parameter   {"EVENT_ID_SPR_UNDERRUN", EVENT_ID_SPR_UNDERRUN}
    @h2xmlp_description {ID for the underrun event raised by the SPR module.
                         A client can register to listen to this event.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup ar_spf_mod_spr_mod
    Payload of the #EVENT_ID_SPR_UNDERRUN event.
 */
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct event_id_spr_underrun_t
{
   uint32_t    status;
   /**< Status of the underrun event being raised by the SPR module.

        @valuesbul
        - #UNDERRUN_STATUS_INPUT_NOT_AVAILABLE
        - #UNDERRUN_STATUS_INPUT_HOLD
        - #UNDERRUN_STATUS_INPUT_DROP @tablebulletend */

   /*#< @h2xmle_description {Status of the underrun event being raised by the
                             SPR module.}
        @h2xmle_range       {1..3} */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct event_id_spr_underrun_t event_id_spr_underrun_t;

/** @ingroup ar_spf_mod_spr_mod
    Identifier for the session time reset event raised by SPR module.
    The event provides the current session time at SPR before resetting the
    session time to 0. Upto 1 client can register for this event with the
    SPR module.

    If the client does not enable the AV synchronization feature via
    #PARAM_ID_SPR_AVSYNC_CONFIG, these values are returned as 0.

    @msgpayload
    param_id_session_time_t
*/

#define EVENT_ID_SPR_SESSION_TIME_RESET                  0x08001B60

/*# @h2xmlp_parameter   {"EVENT_ID_SPR_SESSION_TIME_RESET", EVENT_ID_SPR_SESSION_TIME_RESET}
    @h2xmlp_description {ID for the session time reset event raised by SPR module.}
    @h2xmlp_toolPolicy  {NO_SUPPORT} */

/** @ingroup ar_spf_mod_spr_mod
    Identifier for the Splitter Renderer module, which is used to split an
    audio stream into multiple devices.

    This module is used for AV Synchronization as well as decision rendering.
    The module is only to be used in an audio playback use case. It is
    supported only in a container whose position is APM_CONT_GRAPH_POS_STREAM.
    (For information about APM_CONT_GRAPH_POS_STREAM, see AudioReach Signal
    Processing Framework (SPF) API Reference.

    @subhead4{Supported input media format ID}
     - Data format       : #DATA_FORMAT_FIXED_POINT @lstsp1
     - fmt_id            : #MEDIA_FMT_ID_PCM @lstsp1
     - Sample rates      : Any @lstsp1
     - Number of channels: Any @lstsp1
     - Bit width         : Any @lstsp1
     - Interleaving      : De-interleaved unpacked @lstsp1
     - Endianness        : little

    @par Output media format
    Until the input media format is available, the output media format is
    arbitrarily decided. @newpage
 */
#define MODULE_ID_SPR 0x07001032

/*# @h2xmlm_module                {"MODULE_ID_SPR", MODULE_ID_SPR}
    @h2xmlm_displayName           {"Splitter Renderer"}
    @h2xmlm_modSearchKeys         {Audio}
    @h2xmlm_description           {ID for the Splitter Renderer module, which
                                   is used to split an audio stream into
                                   multiple devices. For more details about
                                   this module, see AudioReach Signal Processing
                                   Framework (SPF) API Reference.}
    @h2xmlm_dataMaxInputPorts     {1}
    @h2xmlm_dataMaxOutputPorts    {-1}
    @h2xmlm_toolPolicy            {Calibration}
    @h2xmlm_ctrlDynamicPortIntent {"Drift Info IMCL"=INTENT_ID_TIMER_DRIFT_INFO,
                                    maxPorts= -1}
    @h2xmlm_supportedContTypes    {APM_CONTAINER_TYPE_GC}
    @h2xmlm_isOffloadable         {false}
    @h2xmlm_stackSize             {1024}

    @{                      <-- Start of the Module -->
    @h2xml_Select           {param_id_spr_delay_path_end_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_spr_session_time_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {param_id_spr_avsync_config_t}
    @h2xmlm_InsertParameter

    @h2xml_Select           {param_id_spr_avsync_config_t::render_window::render_window_start}
    @h2xmle_defaultList     {0x00000000,0x80000000}

    @h2xml_Select           {param_id_spr_avsync_config_t::render_window::render_window_end}
   @h2xmle_defaultList     {0xFFFFFFFF,0x7FFFFFFF}

    @h2xml_Select           {param_id_spr_ctrl_to_data_port_map_t}
    @h2xmlm_InsertParameter
    @h2xml_Select           {time_us_t}
    @h2xmlm_InsertStructure
    @h2xml_Select           {config_spr_render_mode_t}
    @h2xmlm_InsertStructure
    @h2xml_Select           {config_spr_render_window_t}
    @h2xmlm_InsertStructure
    @h2xml_Select           {config_spr_render_reference_t}
    @h2xmlm_InsertStructure
    @h2xml_Select           {config_spr_render_hold_duration_t}
    @h2xmlm_InsertStructure
    @h2xml_Select           {spr_port_binding_t}
    @h2xmlm_InsertStructure
    @}                      <-- End of the Module --> */


#endif /* _SPR_BUFFER_API_H_ */
