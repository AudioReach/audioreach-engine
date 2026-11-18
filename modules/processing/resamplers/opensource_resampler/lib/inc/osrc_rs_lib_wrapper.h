// clang-format off
/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file osrc_rs_lib_wrapper.h
 * @brief SPF-compatible wrapper for OSResampler library
 *
 * Key features:
 * - Identical function signatures to hwsw_rs_lib
 * - Automatic type conversion between SPF and OSResampler types
 * - Built-in float conversion for fixed-point data
 * - DM mode support via resampleGetRequiredSamples/GetExpectedOutput
 */

#ifndef OSRC_RS_LIB_WRAPPER_H
#define OSRC_RS_LIB_WRAPPER_H

#include "resampler.h"
#include "ar_defs.h"
#include "ar_error_codes.h"
#include "capi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
 * Type Definitions
 *--------------------------------------------------------------------------*/

/** Media format structure (compatible with hwsw_rs_media_fmt_t) */
typedef struct osrc_rs_media_fmt_t {
   uint32_t inp_sample_rate;
   uint16_t bits_per_sample;
   uint16_t num_channels;
   uint32_t output_sample_rate;
   uint32_t q_factor;
} osrc_rs_media_fmt_t;

/** Events configuration structure */
typedef struct osrc_rs_events_config {
   uint32_t KPPS;
   uint32_t delay_in_us;
   uint32_t bandwidth;
} osrc_rs_events_config_t;

/** Resampler configuration structure */
typedef struct osrc_rs_resampler_config {
   uint32_t use_hw_rs;      // Ignored (OSResampler is SW-only)
   uint16_t dynamic_mode;   // Ignored (OSResampler is always dynamic)
   uint16_t delay_type;     // Ignored (OSResampler delay is fixed)
} osrc_rs_resampler_config_t;

/** OSResampler library instance. */
typedef struct osrc_resampler_lib {
   // OSResampler context
   Resample *rs_cxt;
   
   // Cached media format (needed for calculations)
   osrc_rs_media_fmt_t media_fmt;
   
   // Quality configuration
   uint32_t num_taps;
   uint32_t max_filters;
   int rs_flags;
   
   // DM mode state
   uint32_t dm_mode;
   uint32_t output_fixed_samples;
   
   // Float conversion buffers (per-channel)
   float **float_input_ptrs;
   float **float_output_ptrs;
   uint32_t float_buf_size_per_ch;  // In samples
   
   // Heap ID for memory allocation
   uint32_t heap_id;

   // Cached events
   osrc_rs_events_config_t events_config;

   /* Frame size in samples per channel — set by osrc_rs_lib_set_frame_size()
    * after the container frame duration is known.  Used by the multi-frame
    * processing loop in osrc_rs_lib_process to consume as many complete frames
    * as possible per call, matching the IIR resampler looping behaviour in
    * the original MODULE_ID_MFC. */
   uint32_t in_frame_samples_per_ch;
   uint32_t out_frame_samples_per_ch;

} osrc_resampler_lib_t;

/*---------------------------------------------------------------------------
 * Function Declarations (API-compatible with hwsw_rs_lib)
 *--------------------------------------------------------------------------*/

/**
 * Initialize resampler library structure.
 */
void osrc_rs_lib_init(osrc_resampler_lib_t *osrc_rs_lib);

/**
 * Deinitialize and free resampler resources.
 */
ar_result_t osrc_rs_lib_deinit(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Create resampler instance based on current configuration.
 */
ar_result_t osrc_rs_lib_check_create_resampler_instance(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t heap_id);

/**
 * Process audio data through resampler.
 */
ar_result_t osrc_rs_lib_process(
    osrc_resampler_lib_t *osrc_rs_ptr,
    capi_buf_t *input_buf_ptr,
    capi_buf_t *output_buf_ptr,
    uint32_t input_num_bufs,
    uint32_t output_num_bufs,
    uint32_t heap_id);

/**
 * Set media format.
 */
void osrc_rs_set_lib_mf(
    osrc_resampler_lib_t *osrc_rs_ptr,
    osrc_rs_media_fmt_t *inp_mf);

/**
 * Set DM mode configuration.
 */
void osrc_rs_lib_set_dm_config(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t dm_mode,
    uint32_t fixed_out_samples);

/**
 * Set per-call frame size in samples per channel.
 * Must be called after the container frame duration is known so that
 * osrc_rs_lib_process can loop over multiple frames per call, exactly
 * mirroring the IIR resampler looping behaviour in MODULE_ID_MFC.
 */
void osrc_rs_lib_set_frame_size(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t in_frame_samples_per_ch,
    uint32_t out_frame_samples_per_ch);

/**
 * Set resampler configuration.
 * Note: use_hwrs, dyn_mode, delay are ignored — OSResampler is SW-only, fixed-delay.
 */
void osrc_rs_lib_set_config(
    uint32_t use_hwrs,
    uint16_t dyn_mode,
    uint16_t delay,
    osrc_resampler_lib_t *osrc_rs_ptr,
    bool_t *update_rs);

/**
 * Get KPPS estimate.
 */
uint32_t osrc_rs_lib_get_kpps(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Get bandwidth estimate.
 */
uint32_t osrc_rs_lib_get_bw(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Get algorithmic delay in microseconds.
 */
uint32_t osrc_rs_lib_get_alg_delay(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Get required input samples to produce a fixed number of output samples.
 * Used for DM fixed-output mode. Wraps resampleGetRequiredSamples().
 *
 * @param osrc_rs_ptr       Resampler library instance.
 * @param req_out_samples   Requested output samples per channel.
 * @param exp_in_samples    [out] Expected input samples per channel.
 * @return AR_EOK on success, AR_EBADPARAM if pointer is NULL.
 */
ar_result_t osrc_rs_lib_get_required_input_samples(osrc_resampler_lib_t *osrc_rs_ptr,
                                                   uint32_t              req_out_samples,
                                                   uint32_t             *exp_in_samples);

/**
 * Get expected output samples for a fixed number of input samples.
 * Used for DM fixed-input mode. Wraps resampleGetExpectedOutput().
 *
 * @param osrc_rs_ptr       Resampler library instance.
 * @param req_in_samples    Available input samples per channel.
 * @return Expected output samples per channel, or 0 if rs_cxt is NULL.
 */
uint32_t osrc_rs_lib_get_in_len(osrc_resampler_lib_t *osrc_rs_ptr, uint32_t req_in_samples);

/**
 * Get process check status.
 */
void osrc_rs_lib_get_process_check(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t *process_check);

/**
 * Reset resampler algorithm state.
 */
ar_result_t osrc_rs_lib_algo_reset(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Check if multi-stage resampling is supported. Always returns FALSE.
 */
bool_t osrc_rs_lib_is_multi_stage_supported(osrc_resampler_lib_t *osrc_rs_ptr);

/**
 * Suspend/resume stub — no-op, OSResampler is SW-only. Always returns AR_EOK.
 */
ar_result_t osrc_rs_lib_set_hwrs_suspend_resume(
    bool_t is_suspend,
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t *create_rs);

/**
 * Get HW process info — stub, no-op for SW-only OSResampler.
 */
void osrc_rs_lib_process_get_hw_process_info(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t input_actual_samples,
    uint32_t output_max_samples,
    uint32_t *input_samples_to_consume_ptr,
    uint32_t *output_samples_to_generate_ptr);

/*---------------------------------------------------------------------------
 * Helper Functions (Internal)
 *--------------------------------------------------------------------------*/

/**
 * Get Q-factor scale for int-to-float conversion
 */
static inline float osrc_rs_get_q_factor_scale(uint32_t q_factor)
{
   switch (q_factor) {
      case 15: return 1.0f / 32768.0f;           // Q15
      case 27: return 1.0f / 134217728.0f;       // Q27
      case 31: return 1.0f / 2147483648.0f;      // Q31
      default: return 1.0f / 32768.0f;           // Default to Q15
   }
}

/**
 * Get Q-factor inverse scale for float-to-int conversion
 */
static inline float osrc_rs_get_q_factor_inverse_scale(uint32_t q_factor)
{
   switch (q_factor) {
      case 15: return 32768.0f;           // Q15
      case 27: return 134217728.0f;       // Q27
      case 31: return 2147483648.0f;      // Q31
      default: return 32768.0f;           // Default to Q15
   }
}

/**
 * Get Q-factor maximum value
 */
static inline int32_t osrc_rs_get_q_factor_max(uint32_t q_factor)
{
   switch (q_factor) {
      case 15: return 32767;              // Q15
      case 27: return 134217727;          // Q27
      case 31: return 2147483647;         // Q31
      default: return 32767;              // Default to Q15
   }
}

#ifdef __cplusplus
}
#endif

#endif /* OSRC_RS_LIB_WRAPPER_H */
// clang-format on