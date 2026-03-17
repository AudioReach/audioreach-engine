/**
 * \file capi_history_buffer_i.h
 *
 * \brief
 *
 *     This file contains CAPI APIs for History Buffer Module
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef CAPI_HISTORY_BUFFER_I_H
#define CAPI_HISTORY_BUFFER_I_H

#ifdef CAPI_STANDALONE

#include "capi_util.h"
#include "ar_error_codes.h"
#else

#include "shared_lib_api.h"
#include "posal.h"
#include "capi_types.h"

#endif /* CAPI_STANDALONE */

// include files
#include "history_buffer_api.h"
#include "capi_history_buffer.h"
#include "capi_fwk_extns_ecns.h"
#include "capi_intf_extn_data_port_operation.h"
#include "capi_intf_extn_imcl.h"
#include "capi_intf_extn_metadata.h"
#include "capi_history_buffer_imcl_utils.h"


/* Debug log flags */
#define HISTORY_BUFFER_DEBUG

#define INVALID ((int32_t)(-1))

/* This module doesn't require any framework extensions. */
#define HISTORY_BUFFER_NUM_FWK_EXTENSIONS   (0)

/* Update Frame size in msec*/
#define HISTORY_BUFFER_PROCESS_FRAME_SIZE_MS (10)

// Bandwidth numbers
/* TODO: These numbers must be profiled and assigned.*/
#define HISTORY_BUFFER_CODE_BW (512)
#define HISTORY_BUFFER_DATA_BW (512)

/* TODO: update appropriate KPPS value here */
#define HISTORY_BUFFER_KPPS_REQUIREMENT 1024

/* TODO: update delay in usec*/
#define HISTORY_BUFFER_DEFAULT_DELAY_IN_US (0)

/* Update no of history buffer client depending upon the usecase */
#define MAX_TRIGGERED_EVENT_CLIENTS (1)

/*TODO: recurring buffers info */
#define HISTORY_BUFFER_RECURRING_BUF_SIZE   64
#define HISTORY_BUFFER_RECURRING_BUFS_NUMS  2

/* History Buffer module debug message */
#define DE_MSG_PREFIX "capi_history_buffer[0x%X]: "
#define DE_DBG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, DE_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

/*Event mode bit mask */
#define EVENT_INFO_CONFIDENNCE_LEVELS     0x00000001
#define EVENT_INFO_KEYWORD_INDICES        0x00000002
#define EVENT_INFO_DETECTION_TIMESTAMP    0x00000004
#define EVENT_INFO_FTRT_LENGTH            0x00000008

/* Capi media format event payload structure */
typedef struct capi_hist_buf_media_fmt_v2_t
{
   capi_set_get_media_format_t    header;
   capi_standard_data_format_v2_t format;
   uint16_t                       channel_type[CAPI_MAX_CHANNELS_V2];
} capi_hist_buf_media_fmt_v2_t;

/* Capi input port state structure */
typedef struct hist_buf_port_info_t
{
   uint32_t                             port_index;
   bool_t                               is_media_fmt_received;
   capi_hist_buf_media_fmt_v2_t media_fmt;
} hist_buf_port_info_t;

/* Structure to hold the triggered detection info */
typedef struct detection_trigger_info_t
{
   bool_t                     is_kw_detected;
   bool_t                     waiting_for_ftrt_len;
   ftrt_data_info_t           ftrt_info;
} detection_trigger_info_t;

/* Module capi handle structure */
typedef struct capi_history_buffer_t
{
   const capi_vtbl_t *        vtbl_ptr;
   uint32_t                   miid;
   capi_event_callback_info_t cb_info;
   POSAL_HEAP_ID              heap_id;
   capi_port_num_info_t       num_port_info;
   bool_t                     is_enabled;
   bool_t                     is_trigger_detected;
   bool_t                     is_event_inprogess;

   uint32_t                   kpps;
   uint32_t                   delay_us;

   uint32_t                   max_dam_buffer_size_us;
   uint32_t                   hist_buffer_duration_msec;
   detection_trigger_info_t       latest_trigger_info;

   uint32_t                                  event_mode;
   uint32_t                                  event_payload_size;
   event_id_detection_engine_generic_info_t *event_payload_ptr;

   hist_buf_port_info_t in_port_info[HISTORY_BUFFER_MAX_INPUT_PORTS];
   /* Input port info structure, currently applicable to one input port. */

   capi_register_event_to_dsp_client_v2_t reg_clients_info[MAX_TRIGGERED_EVENT_CLIENTS];
   /* detection event client registration info. has destination address and token for the events */

   uint32_t                   num_opened_ctrl_ports;
   history_buffer_ctrl_port_info_t ctrl_port_info[HISTORY_BUFFER_MAX_CONTROL_PORTS];
   /* Control port info structure, currently applicable to one input port. */

   param_id_history_buffer_mode_t mode;

} capi_history_buffer_t;

/********** Capi helper functions **************/
capi_err_t capi_history_buffer_handle_get_properties(capi_history_buffer_t *me, capi_proplist_t *proplist_ptr);
capi_err_t capi_history_buffer_handle_set_properties(capi_history_buffer_t *me, capi_proplist_t *proplist_ptr);
capi_err_t capi_populate_trigger_info_and_raise_event(capi_history_buffer_t *me_ptr);
bool_t capi_history_buffer_media_fmt_equal(capi_hist_buf_media_fmt_v2_t *media_fmt_1_ptr,
                                      capi_hist_buf_media_fmt_v2_t *media_fmt_2_ptr);
capi_err_t capi_history_buffer_init_util(capi_t *_pif, capi_proplist_t *init_set_properties, bool_t is_second_stage);

#endif /* CAPI_HISTORY_BUFFER_I_H */
