#ifndef __SPR_AVSYNC_H
#define __SPR_AVSYNC_H

/**
 *   \file capi_spr_avsync.h
 *   \brief
 *        This file contains utility functions for handling AVSync features in SPR
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "capi_spr_i.h"

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*==============================================================================
   Constants & Defines
==============================================================================*/

#define MALLOC_8_BYTE_ALIGN 8

// Default hold buffer size is 150 ms (from elite)
#define DEFAULT_HOLD_BUF_SIZE_US 150000

typedef struct capi_spr_t capi_spr_t; // forward declaration

// Structure used by the SPR to store the incoming data streams
typedef struct spr_int_buffer_t
{
   bool_t           is_used_for_hold;
   uint64_t         cur_fill_size_us;
   spf_list_node_t *buf_list_ptr;

} spr_int_buffer_t;

typedef enum render_decision_t {
   RENDER  = 0x1, // Default type
   HOLD    = 0x2,
   DROP    = 0x3,
   INVALID = 0x4
} render_decision_t;

/* Structure containing the AV Sync Configuration expected from the client as part of
 * PARAM_ID_AVSYNC_CONFIG */
typedef struct spr_avsync_config_t
{
   bool_t   is_enable;              // Indicates if the client enabled the avsync feature
   uint16_t cfg_mask;               // Indicates if the params have been configured or not.
                                    //   The bits mirror the definition of the PARAM_ID_SPR_AVSYNC_CONFIG
   uint32_t render_reference;       // Indicates the mode of render reference (default/wall clock)
   uint32_t render_mode;            // Indicates the render mode (Immediate/Absolute/Delayed)
   int64_t  start_time_us;          // Value of render start time in microseconds
   int64_t  render_window_start_us; // Value of render window start in microseconds
   int64_t  render_window_end_us;   // Value of render window end in microseconds
   int64_t  hold_buf_duration_us;   // Value of hold buffer duration in microseconds

   bool_t   allow_non_timestamp_honor_mode; // Indicates Non time stamp honor mode can be enabled or not
} spr_avsync_config_t;

typedef struct avsync_flags_t
{
   uint32_t is_ts_valid : 1;            // Indicates if incoming TS is valid or not.
   uint32_t is_dfg : 1;                 // Indicates if DFG has been received at input
   uint32_t is_first_buf_rcvd : 1;      // Indicates the arrival of first valid input sample
   uint32_t is_first_buf_rendered : 1;  // Indicates if the first output from SPR was rendered
   uint32_t is_pending_dfg : 1;         // Indicates if DFG is pending to be applied when md_offset != 0
   uint32_t is_timescaled_data : 1;     // Indicates if SPR has to move to timescaled session time calculation
   uint32_t is_pending_tsm_cfg : 1;     // Indicates if any pending TSM MD to be appplied when md_offset != 0
} avsync_flags_t;

typedef struct avsync_cached_tsm_info_t
{
   uint32_t speed_factor;
   uint32_t offset;

}avsync_tsm_cfg_t;

typedef struct avsync_tsm_info_t
{
	uint64_t tsm_session_clk_samples;
	uint64_t remainder_tsm_samples;
	uint32_t tsm_speed_factor;

}avsync_tsm_info_t;

typedef struct avsync_t
{
   uint64_t            session_clock_us;          // current value of session clock
   uint64_t            expected_session_clock_us; // expected session clock value
   int64_t             base_timestamp_us;         // timestamp of first buffer at input
   int64_t             proc_timestamp_us;         // timestamp last delivered by SPR
   uint64_t            absolute_time_us;          // time at which the proc_timestamp_us is expected to be delivered
   int64_t             calc_start_time_us;        // time at which the session is supposed to start render
   int64_t             curr_wall_clock_us;        // current value of the wall clock tick
   uint64_t            elapsed_session_clock_samples; // count of session clock in samples rendered so far.
   uint64_t            elapsed_expected_session_clock_samples; //count of expected session clock in samples.
   uint64_t            abs_render_delta_us;  // Absolute value of delta calculated during render decision.
   uint32_t            hold_zeroes_per_ch;   // When hold buffer has to be rendered, calculate zeroes to be prepended
   uint32_t            ds_delay_us;          // Downstream delay from the primary output port
   uint32_t            data_held_us;         // Duration of data (us) held in the int_buffers for hold & media format
   uint32_t            miid;                 // module instance id
   avsync_tsm_info_t   tsm_info;             // Timescaled information used for AVSync Updates in SPR
   avsync_tsm_cfg_t    cached_tsm_info;      // Cached speed factor from TSM when md_offset != 0
   render_decision_t   prev_render_decision; // Previous render decision
   spr_avsync_config_t client_config;        // Configuration provided by the client as part of PARAM_ID_AVSYNC_CONFIG
   spr_int_buffer_t    hold_buffer;          // Buffer used by SPR to store the data during a hold scenario
   avsync_flags_t      flags;                // Flags associated with this structure
   param_id_spr_session_time_reset_info_t av_reset_info; // Mode for session time update

} avsync_t;
/*==============================================================================
   Function declarations
==============================================================================*/

capi_err_t capi_spr_avsync_create(avsync_t **avsync_ptr_ptr, POSAL_HEAP_ID heap_id, uint32_t miid);
capi_err_t capi_spr_avsync_destroy(avsync_t **avsync_ptr_ptr, uint32_t miid);
capi_err_t capi_spr_avsync_init(avsync_t *avsync_ptr);
capi_err_t capi_spr_avsync_deinit(avsync_t *avsync_ptr);

/**------------------------------- capi_spr_avsync_utils ------------------------*/
capi_err_t capi_spr_avsync_update_session_clock(avsync_t *avsync_ptr, int64_t adj_bytes, uint32_t sr, uint32_t bps);
capi_err_t capi_spr_avsync_update_absolute_time(avsync_t *avsync_ptr, int64_t adj_us);
capi_err_t capi_spr_avsync_reset_session_clock(avsync_t *avsync_ptr);
capi_err_t capi_spr_avsync_reset_session_clock_params(avsync_t *avsync_ptr);
capi_err_t capi_spr_avsync_reset_session_clock_for_gapless(avsync_t *avsync_ptr);
capi_err_t capi_spr_avsync_update_expected_session_time(avsync_t *avsync_ptr, int64_t update_bytes, uint32_t sr, uint32_t bps);

capi_err_t capi_spr_process_avsync_config_param(capi_spr_t *me_ptr, param_id_spr_avsync_config_t *params_ptr);
capi_err_t capi_spr_process_session_time_reset_param(capi_spr_t *me_ptr, param_id_spr_session_time_reset_info_t *params_ptr);
void capi_spr_avsync_check_update_primary_output_port(capi_spr_t *me_ptr);

capi_err_t capi_spr_add_input_to_hold_list(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_ptr);
void capi_spr_destroy_hold_buf_list(capi_spr_t *me_ptr);

capi_err_t capi_spr_avsync_update_input_info(capi_spr_t *me_ptr, capi_stream_data_t *input);
capi_err_t capi_spr_avsync_update_output_info(capi_spr_t *        me_ptr,
                                              bool_t              is_erasure,
                                              capi_stream_data_t *pri_out_strm_ptr,
                                              uint32_t            num_bytes_filled);

capi_err_t capi_spr_avsync_setup_first_input(capi_spr_t *me_ptr, capi_stream_data_t *input);
capi_err_t capi_spr_avsync_make_render_decision(capi_spr_t *        me_ptr,
                                                capi_stream_data_t *input,
                                                render_decision_t * render_decision);
void *spr_avsync_get_hold_buf_head_obj_ptr(avsync_t *avsync_ptr);
bool_t spr_avsync_is_input_strm_hold_buf_head(avsync_t *avsync_ptr, void *input_strm_ptr);
void spr_avsync_clear_hold_duration(avsync_t *avsync_ptr);
capi_err_t capi_spr_avsync_check_fill_hold_zeroes(capi_spr_t *           me_ptr,
                                                  capi_stream_data_v2_t *out_strm_ptr,
                                                  int64_t *              timestamp_ptr);
void capi_spr_avsync_set_miid(avsync_t *avsync_ptr, uint32_t miid);
void spr_avsync_set_ds_delay(avsync_t *avsync_ptr, uint32_t delay_us);

/*==============================================================================
   Static inline declarations
==============================================================================*/

// Updates the state of DFG on the SPR input port. Used to handle avsync updates
static inline void spr_avsync_set_dfg_flag(avsync_t *avsync_ptr, bool_t is_dfg)
{
   if (!avsync_ptr)
   {
      return;
   }

   avsync_ptr->flags.is_dfg = is_dfg;
   // Do not force sync to next buffer input timestamp. Happens with flush.
}

// Updates the state of pending DFG on the avsync library. This happens if
// dfg came along with valid data i.e without erasure
static inline void spr_avsync_set_dfg_pending_flag(avsync_t *avsync_ptr, bool_t is_dfg)
{
   if (!avsync_ptr)
   {
      return;
   }

   avsync_ptr->flags.is_pending_dfg = is_dfg;
}

// Checks if the avsync feature is enabled or not
static inline bool_t is_spr_avsync_enabled(avsync_t *avsync_ptr)
{
   bool_t is_enabled = (avsync_ptr) ? avsync_ptr->client_config.is_enable : FALSE;
   return is_enabled;
}

// Generate the tick value from the lsw & msw values
static inline int64_t process_time_us_type(time_us_t time)
{
   uint64_t ret_val_us = (uint64_t)time.value_msw << 32;
   ret_val_us |= (uint64_t)time.value_lsw;

   return (int64_t)ret_val_us;
}

// Check if the hold buffer for avsync exists
static inline bool_t spr_avsync_does_hold_buf_exist(avsync_t *avsync_ptr)
{
   if (avsync_ptr && avsync_ptr->hold_buffer.buf_list_ptr)
   {
      return TRUE;
   }
   return FALSE;
}

// Check if the hold buffer for avsync exists
static inline bool_t spr_avsync_is_hold_buf_configured(avsync_t *avsync_ptr)
{
   if (avsync_ptr && avsync_ptr->client_config.hold_buf_duration_us > 0)
   {
      return TRUE;
   }
   return FALSE;
}

// Updates the render decision made in the current process call
static inline void spr_avsync_set_render_decision(avsync_t *avsync_ptr, render_decision_t decision)
{
   if (!avsync_ptr)
   {
      return;
   }

   avsync_ptr->prev_render_decision = decision;
}

// Returns the render decision set in the current process call
static inline render_decision_t spr_avsync_get_render_decision(avsync_t *avsync_ptr)
{
   if (!avsync_ptr)
   {
      return RENDER;
   }

   return avsync_ptr->prev_render_decision;
}

// Returns the render mode used by the module
static inline uint32_t spr_avsync_get_render_mode(avsync_t *avsync_ptr)
{
   return avsync_ptr ? avsync_ptr->client_config.render_mode : SPR_RENDER_MODE_IMMEDIATE;
}

static inline uint32_t spr_avsync_get_reset_session_time_mode(avsync_t *avsync_ptr)
{
   return avsync_ptr? avsync_ptr->av_reset_info.mode : SPR_SESSION_TIME_RESET_MODE_DEFAULT;
}

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /*__SPR_AVSYNC_H*/
