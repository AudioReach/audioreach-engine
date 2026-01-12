#ifndef __CAPI_JITTER_BUF_H_I__
#define __CAPI_JITTER_BUF_H_I__

/**
 *   \file capi_jitter_buf_i.h
 *   \brief
 *        This file contains CAPI API's published by Jitter Buf module API.
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi.h"
#include "capi_jitter_buf.h"
#include "capi_cmn.h"
#include "ar_defs.h"
#include "shared_lib_api.h"
#include "capi_cmn_imcl_utils.h"
#include "capi_fwk_extns_multi_port_buffering.h"
#include "capi_fwk_extns_trigger_policy.h"
#include "jitter_buf_api.h"
#include "jitter_buf_driver.h"
#include "imcl_timer_drift_info_api.h"
#include "capi_intf_extn_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Local Defines
==============================================================================*/

/* Debug flag */
//#define DEBUG_JITTER_BUF_DRIVER_DRIFT_ADJ

#ifdef DEBUG_JITTER_BUF_DRIVER
#define DEBUG_JITTER_BUF_DRIVER_DRIFT_ADJ
#endif

/* Stack size requirement from the module */
#define CAPI_JITTER_BUF_MODULE_STACK_SIZE (2048)

/* One control port to connect to Sample slip */
#define JITTER_BUF_MAX_INTENTS_PER_CTRL_PORT 1

/* TODO: Pending - Multiport buffering neededd?*/
/* Trigger policy and multiport buffering */
#define JITTER_BUF_NUM_FRAMEWORK_EXTENSIONS 2

#define JITTER_BUF_MAX_CTRL_PORTS 1

#define JITTER_BUF_MAX_INTENTS_PER_CTRL_PORT 1

/* TODO: Pending - check values of tolerance and max
 * reported if required*/

// Jitter tolerance duration is 1000us due to fractional frame sizes
// from decoder which may not align with jitter buffer container frame sizes.
//#define JITTER_BUF_DRIFT_TOLERANCE_SAMPLES 3
#define JITTER_BUF_DRIFT_TOLERANCE_SAMPLES 48
#define JITTER_BUF_DRIFT_TOLERANCE_DURATION 1000

/* TODO: Pending - check values */
static const uint32_t JITTER_BUF_BW      = 1 * 1024 * 1024;
static const uint32_t JITTER_BUF_BW_16   = 2 * 1024 * 1024;
static const uint32_t JITTER_BUF_BW_32   = 4 * 1024 * 1024;
static const uint32_t JITTER_BUF_KPPS    = 1500;
static const uint32_t JITTER_BUF_KPPS_16 = 2500;
static const uint32_t JITTER_BUF_KPPS_32 = 5000;

/* Control Port Structure of Jitter Buffer */
typedef struct jitter_buf_ctrl_port_info_t
{
   uint32_t          port_id;
   imcl_port_state_t state;
   uint32_t          num_intents;
   uint32_t          intent_list_arr[JITTER_BUF_MAX_INTENTS_PER_CTRL_PORT];
} jitter_buf_ctrl_port_info_t;

/** Drift struct used by jitter_buf module to accumulate drift based on buffer fullness */
typedef struct jitter_buf_drift_info_t
{
   imcl_tdi_hdl_t drift_info_hdl;
   /**< Shared drift info handle */

   imcl_tdi_acc_drift_t acc_drift;
   /**< Current drift info  */

   posal_mutex_t drift_info_mutex;
   /**< Mutex to protect read/write between jitter_buf and rat */

} jitter_buf_drift_info_t;

/* Jitter Buffer Events Config */
typedef struct jitter_buf_events_config_t
{
   uint32_t kpps;
   uint32_t data_bw;
   bool_t   process_state;

} jitter_buf_events_config_t;

/* CAPI structure  */
typedef struct capi_jitter_buf_t
{
   const capi_vtbl_t *vtbl;
   /* pointer to virtual table */

   POSAL_HEAP_ID heap_id;
   /* Heap id received from framework*/

   capi_event_callback_info_t event_cb_info;
   /* Event Call back info received from Framework*/

   fwk_extn_param_id_trigger_policy_cb_fn_t policy_chg_cb;
   /* Callback for changing trigger policy */

   intf_extn_param_id_metadata_handler_t metadata_handler;
   /* Callback for metadata handler utility */

   jitter_buf_events_config_t event_config;
   /* Storing the events raised */

   capi_media_fmt_v2_t operating_mf;
   /* Media format of the module. */

   uint8_t configured_buffer_mode;
   /* buffer mode set by the calibration.
   input trigger is triggerable optional or non-triggerable optional. */

   uint8_t buffer_mode;
   /* buffer mode set dynamically based on drift detection logic. */

   bool_t is_input_mf_received;
   /* Indicates if valid media format is received. */

   bool_t settlement_time_done;
   /* Indicates whether the jitter should have settled by now*/

   bool_t steady_state_value_done;
   /* Indicates we have reached steady state */

   bool_t first_frame_written;
   /* Indicate if the first write is done. This helps to note
    * the ts_first_data after which settling time is given. */

   bool_t is_disabled_by_failure;
   /* Indicates whether module got disabled by fatal failures like malloc/buffer allocation failure.
    * Module won't raise process state event but also it won't process data */

   uint32_t jitter_allowance_in_ms;
   /* Size of the jitter buffer - This is also the
    * latency of the module */

   uint32_t jiter_bytes;
   /* Calculated based on jitter_allowance_in_ms and used to send zeros
    * whenever buffer is drainer */

   uint64_t ts_first_data;
   /* Timestamp of the first data written into the buffer. We
    * start waiting from settlement time after this */

   jitter_buf_ctrl_port_info_t ctrl_port_info;
   /* Control port to report drift to Sample Slip Module */

   uint32_t drift_settlement_in_ms;
   /* Drift adjustment calculations begin only after
    * settlement amount of time for the incoming data */

   jitter_buf_drift_info_t drift_info;
   /* Drift correction that is sent to SS */

   uint32_t steady_state_buffer_fullness_bytes;
   /* Steady State buffer fullness that we try to maintain
    * by toggling trigger policy to pull / stop pull */

   uint32_t upper_threshold_drift_bytes;
   /* Upper drift limit for jitter buffer */

   uint32_t lower_threshold_drift_bytes;
   /* Lower drift limit for jitter buffer */

   int32_t step_drift_us;
   /* Drift step for 1 sample correction in us */

   uint32_t frame_duration_in_us;
   /* Decoder frame duration in us obtained from first input from decoder */

   jitter_buf_driver_t driver_hdl;
   /* Jitter Buf driver handle. */

   uint32_t debug_size_ms;
   /* Debug size of jitter buffer. Debug size takes precedence */

   uint32_t debug_settlement_ms;
   /* Debug settlement time for jitter buffer */
   
   uint32_t allow_state_propagation;
   /* allowing output state propagation  */

} capi_jitter_buf_t;

/*==============================================================================
   Function declarations
==============================================================================*/

capi_err_t capi_jitter_buf_vaildate_and_cache_input_mf(capi_jitter_buf_t *me_ptr, capi_buf_t *media_fmt_ptr);

capi_err_t capi_jitter_buf_raise_output_mf_event(capi_jitter_buf_t *me_ptr, capi_media_fmt_v2_t *mf_info_ptr);

capi_err_t capi_jitter_buf_raise_kpps_bw_event(capi_jitter_buf_t *me_ptr);

capi_err_t capi_jitter_buf_set_size(capi_jitter_buf_t *me_ptr);

capi_err_t capi_jitter_buf_change_trigger_policy(capi_jitter_buf_t *me_ptr);

capi_err_t capi_jitter_buf_propagate_metadata(capi_jitter_buf_t *me_ptr, capi_stream_data_t *input);

capi_err_t capi_jitter_buf_destroy_md_list(capi_jitter_buf_t *me_ptr, module_cmn_md_list_t **md_list_pptr);

/*==============================================================================
   IMCL utility functions
==============================================================================*/

capi_err_t capi_jitter_buf_handle_imcl_port_operation(capi_jitter_buf_t *me_ptr, capi_buf_t *params_ptr);

capi_err_t capi_jitter_buf_send_drift_info_hdl_to_rat(capi_jitter_buf_t *me_ptr);

capi_err_t capi_jitter_buf_init_out_drift_info(uint32_t heap_id, jitter_buf_drift_info_t *     drift_info_ptr,
                                               imcl_tdi_get_acc_drift_fn_t get_drift_fn_ptr);

capi_err_t capi_jitter_buf_deinit_out_drift_info(jitter_buf_drift_info_t *drift_info_ptr);

ar_result_t jitter_buf_imcl_read_acc_out_drift(imcl_tdi_hdl_t *      drift_info_hdl_ptr,
                                               imcl_tdi_acc_drift_t *acc_drift_out_ptr);

capi_vtbl_t *capi_jitter_buf_get_vtbl();

capi_err_t capi_jitter_buf_end(capi_t *_pif);

capi_err_t capi_jitter_buf_set_param(capi_t *                _pif,
                                          uint32_t                param_id,
                                          const capi_port_info_t *port_info_ptr,
                                          capi_buf_t *            params_ptr);

capi_err_t capi_jitter_buf_get_param(capi_t *                _pif,
                                          uint32_t                param_id,
                                          const capi_port_info_t *port_info_ptr,
                                          capi_buf_t *            params_ptr);

capi_err_t capi_jitter_buf_set_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_jitter_buf_get_properties(capi_t *_pif, capi_proplist_t *props_ptr);

capi_err_t capi_jitter_buf_process(capi_t *_pif, capi_stream_data_t *input[], capi_stream_data_t *output[]);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__CAPI_JITTER_BUF_H_I__ */
