/**
 * \file ipc_module_cmn_api.h
 * \brief
 *    This file contains the API definitions for the commands common to the IPC modules
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _IPC_MODULE_CMN_API_H_
#define _IPC_MODULE_CMN_API_H_

#include "ar_defs.h"
#include "media_fmt_api_basic.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/** @ingroup ar_spf_mod_media_fmt_ids
    Identifier for the parameter used to set the media format on IPC modules

    This parameter ID is set via APM_CMD_SET_CFG  It is accepted only when the subgraph is
    in the Stop or Prepare state.

    @msgpayload
    media_format_v2_t
 */
#define PARAM_ID_IPC_MEDIA_FORMAT_V2       0x08001BC0

/** @h2xmlp_subStruct
    @h2xmlp_description {For PCM, frame_len_us and frame_len_bytes can be used,
                         For raw compr and deint raw compr data frame_len_bytes is used} */

typedef struct ipc_frame_length_t
{
   uint32_t frame_len_us;
   /**< Frame length in microseconds. */

   uint32_t frame_len_bytes;
   /**< Frame length in bytes.
    * For raw compr and deint raw compr data,
    * this represents the total size of the buffer- including all channels */
} ipc_frame_length_t;

/*# @h2xmlp_parameter   {"PARAM_ID_IPC_MEDIA_FORMAT_V2", PARAM_ID_IPC_MEDIA_FORMAT_V2}
    @h2xmlp_toolPolicy  {Calibration}
    @h2xmlp_description {ID for the parameter used to set the media format on
                         IPC TX and RX modules.} */

/** @ingroup ar_spf_mod_media_fmt_ids
    Payload structure used by the following opcodes:
    - #PARAM_ID_IPC_MEDIA_FORMAT_V2
    - #DATA_CMD_IPC_MEDIA_FORMAT_V2

    Immediately following this structure is a payload_size number of bytes that
    represent the actual media format block.

    Following is the overall payload structure:

    @code
    media_format_v2_t mf;
    uint8_t payload[payload_size];
    @endcode @vertspace{6}
 */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct media_format_v2_t
{
   uint32_t data_format;
   /**< Format of the data.

        @valuesbul
        - #INVALID_VALUE (Default)
        - #DATA_FORMAT_FIXED_POINT
        - #DATA_FORMAT_IEC61937_PACKETIZED
        - #DATA_FORMAT_IEC60958_PACKETIZED
        - #DATA_FORMAT_DSD_OVER_PCM
        - #DATA_FORMAT_GENERIC_COMPRESSED
        - #DATA_FORMAT_RAW_COMPRESSED @tablebulletend */

   /*#< @h2xmle_description {Format of the data.}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {"INVALID_VALUE"=0,
                             "DATA_FORMAT_FIXED_POINT"=1,
                             "DATA_FORMAT_IEC61937_PACKETIZED"=2,
                             "DATA_FORMAT_IEC60958_PACKETIZED"=3,
                             "DATA_FORMAT_DSD_OVER_PCM"=4,
                             "DATA_FORMAT_GENERIC_COMPRESSED"=5,
                             "DATA_FORMAT_RAW_COMPRESSED"=6}
        @h2xmle_policy      {Basic} */

   uint32_t fmt_id;
   /**< Media format ID of the data stream.

        @valuesbul
        - #INVALID_VALUE (Default)
        - #MEDIA_FMT_ID_PCM @tablebulletend */

   /*#< @h2xmle_description {Media format ID of the data stream.}
        @h2xmle_default     {0}
        @h2xmle_rangeList   {"INVALID_VALUE"=0,
                             "Media format ID of PCM"=MEDIA_FMT_ID_PCM}
        @h2xmle_policy      {Basic} */

   ipc_frame_length_t peer_frame_len;
   /**< frame length of container.

        @h2xmle_description {frame length of container, can be in us or samples}
        @h2xmle_default     {0}
        @h2xmle_policy      {Basic} */

   uint32_t payload_size;
   /**< Size of the payload that immediately follows this structure.

        This size does not include bytes added for 32-bit alignment. */

   /*#< @h2xmle_description {Size of the payload that immediately follows this
                             structure. This size does not include bytes added
                             for 32-bit alignment.}
        @h2xmle_default     {0}
        @h2xmle_range       {0..0xFFFFFFFF}
        @h2xmle_policy      {Basic} */

#if defined(__H2XML__)
   uint8_t  payload[0];
   /**< Payload for media format configurations.

        The payload structure varies depending on the combination of the
        data_format and fmt_id fields and is of size payload_size. For example,
        a PCM fixed point (payload_media_fmt_pcm_t) and floating point might
        have different payloads. @newpagetable */

   /*#< @h2xmle_description       {Payload for media format configurations.
                                   The payload structure varies depending on
                                   the combination of the data_format and
                                   fmt_id fields and is of size payload_size.
                                   For example, if fmt_id=PARAM_ID_PCM_OUTPUT_FORMAT_CFG
                                   and data_format=DATA_FORMAT_FIXED_POINT, the
                                   payload is of type payload_media_fmt_pcm_t.
                                   The floating point might have a different
                                   payload.}
        @h2xmle_policy            {Basic} */

#endif
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct media_format_v2_t media_format_v2_t;

/** @ingroup ar_spf_mod_media_fmt_ids
    Identifier for the parameter used to send the frame length of the container.

    This parameter ID is set via PARAM_ID_DOWNSTREAM_FRAME_LENGTH command

    @msgpayload
    media_format_v2_t
 */
#define PARAM_ID_DOWNSTREAM_FRAME_LENGTH       0x01005BAD
/*# @h2xmlp_parameter   {"PARAM_ID_DOWNSTREAM_FRAME_LENGTH", PARAM_ID_DOWNSTREAM_FRAME_LENGTH}
    @h2xmlp_toolPolicy  {Calibration}
    @h2xmlp_description {ID for the parameter used to set the media format on
                         IPC TX and RX modules.} */

/** @ingroup ar_spf_mod_media_fmt_ids
    Payload structure used by the following opcodes:
    - #PARAM_ID_DOWNSTREAM_FRAME_LENGTH

    Immediately following this structure is a payload_size number of bytes that
    represent the actual media format block.

    Following is the overall payload structure:

    @code

    @endcode @vertspace{6}
 */

#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
struct param_id_downstream_frame_length_t
{
   ipc_frame_length_t peer_frame_len;
   /**< frame length of container.

        @h2xmle_description {frame length of container, can be in us or samples}
        @h2xmle_default     {0}
        @h2xmle_policy      {Basic} */

}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
typedef struct param_id_downstream_frame_length_t param_id_downstream_frame_length_t;

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* _IPC_MODULE_CMN_API_H_ */
