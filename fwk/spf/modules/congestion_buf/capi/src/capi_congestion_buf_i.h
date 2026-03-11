#ifndef __CAPI_CONGESTION_BUF_H_I__
#define __CAPI_CONGESTION_BUF_H_I__

/**
 *   \file capi_congestion_buf_i.h
 *   \brief
 *        This file contains CAPI API's published by Congestion Buffer module API.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi.h"
#include "capi_congestion_buf.h"
#include "capi_cmn.h"
#include "ar_defs.h"
#include "shared_lib_api.h"
#include "capi_cmn_imcl_utils.h"
#include "capi_fwk_extns_multi_port_buffering.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "congestion_buf_api.h"
#include "congestion_buf_driver.h"
#include "imcl_timer_drift_info_api.h"
#include "capi_intf_extn_metadata.h"
#include "other_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Local Defines
==============================================================================*/

/* Stack size requirement from the module */
#define CAPI_CONGESTION_BUF_MODULE_STACK_SIZE (2048)

/* Framework extensions required - trigger policy,
 * multiport buffering */
#define CONGESTION_BUF_NUM_FRAMEWORK_EXTENSIONS 2

/* The three modes available for bit rate which
 * can be specified in set param */
#define CONGESTION_BUF_MODE_UNKNOWN 0

#define CONGESTION_BUF_MODE_AVG 1

#define CONGESTION_BUF_MODE_MAX 2

#define CONGESTION_BUF_FRAME_SIZE_UNSET 0

#define CONGESTION_BUF_FRAME_SIZE_IN_US 1

#define CONGESTION_BUF_FRAME_SIZE_SAMPLES 2

/* Preferred chunk size - this is also the starting
 * base minimum size used for the congestion buffer. */
#define CONGESTION_BUFFER_PREFERRED_CHUNK_SIZE 4096

#define CONGESTION_BUFFER_MAX_MTU_SIZE 1200

#define CONGESTION_BUF_RAW_BUF_MAX_MEMORY (20 * 1024)

/**
 * Sideband ID for the media payload header with number of frames.
 * Part of the information from AVDTP media payload header.
 *
 * Direction: BT to LPASS.
 * Destination: This sideband info is sent to the decoder.
 */
#define COP_SIDEBAND_ID_MEDIA_HEADER_WITH_CP_NUM_FRAMES 0x4

/* TODO: Pending - confirm these values */
static const uint32_t CONGESTION_BUFFER_KPPS = 2000;

static const uint32_t CONGESTION_BUFFER_CODE_BW = 0;

static const uint32_t CONGESTION_BUFFER_DATA_BW = 1 * 1024 * 1024;

/* Congestion Buffer Deint Raw Compr Structure */
typedef struct capi_deint_mf_combined_t
{
   capi_set_get_media_format_t                     main;
   capi_deinterleaved_raw_compressed_data_format_t deint_raw;
   capi_channel_mask_t                             ch_mask[CAPI_MAX_CHANNELS];
} capi_deint_mf_combined_t;

/* CAPI structure  */
typedef struct capi_congestion_buf_t
{
   const capi_vtbl_t *vtbl;
   /* pointer to virtual table */

   POSAL_HEAP_ID heap_id;
   /* Heap id received from framework*/

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   bool_t is_deint;
   /* If true we use mf otherwise bitstream_format */

   uint32_t bitstream_format;
   /* Raw Compressed Bitstream Format  */

   capi_deint_mf_combined_t mf;
   /* Deinterleaved Media format */

   bool_t is_input_mf_received;
   /* Indicates if input media format is received */

   uint32_t raw_data_len;
   /* Raw Frame Len (obtained from MTU - max is 1200) */

   param_id_congestion_buf_config_t cfg_ptr;
   /* configuration that will be used for this module */

   uint32_t congestion_size_bytes_max;
   /* Maximum congestion that will be tolerated calculated
    * based on bit rate. We start the buffer with about 4k and
    * expand as required till we buffer said # of frames or
    * this much data. */

   uint32_t debug_ms;
   /* Value of congestion buf size received in debug param */

   fwk_extn_param_id_trigger_policy_cb_fn_t policy_chg_cb;
   /* Callback for changing trigger policy */

   intf_extn_param_id_metadata_handler_t metadata_handler;
   /* Callback for metadata handler */

   congestion_buf_driver_t driver_hdl;
   /* Congestion Buffer Driver Handle */

} capi_congestion_buf_t;

/*==============================================================================
   Function declarations
==============================================================================*/

capi_err_t congestion_buf_raise_output_media_format_event(capi_congestion_buf_t *me_ptr);

capi_err_t capi_congestion_buf_event_dt_in_st_cntr(capi_congestion_buf_t *me_ptr);

void congestion_buf_change_signal_trigger_policy(capi_congestion_buf_t *me_ptr);

void congestion_buf_change_trigger_policy(capi_congestion_buf_t *me_ptr);

capi_err_t capi_congestion_buf_parse_md_num_frames(capi_congestion_buf_t *me_ptr, capi_stream_data_t *input);

capi_err_t capi_congestion_buf_init_create_buf(capi_congestion_buf_t *me_ptr, bool_t is_debug);

capi_vtbl_t *capi_congestion_buf_get_vtbl();

capi_err_t capi_congestion_buf_end(capi_t *_pif);

capi_err_t capi_congestion_buf_set_param(capi_t *                _pif,
                                          uint32_t                param_id,
                                          const capi_port_info_t *port_info_ptr,
                                          capi_buf_t *            params_ptr);

capi_err_t capi_congestion_buf_get_param(capi_t *                _pif,
                                          uint32_t                param_id,
                                          const capi_port_info_t *port_info_ptr,
                                          capi_buf_t *            params_ptr);

capi_err_t capi_congestion_buf_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_congestion_buf_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_congestion_buf_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__CAPI_CONGESTION_BUF_H_I__ */
