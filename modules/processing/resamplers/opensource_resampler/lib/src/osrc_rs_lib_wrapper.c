/*
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file osrc_rs_lib_wrapper.c
 * @brief Implementation of SPF-compatible wrapper for OSResampler library
 */

#include "osrc_rs_lib_wrapper.h"
#include "osrc_resampler_port.h"
#include "posal_memory.h"
#include "ar_msg.h"

/*
 * g_osrc_rs_heap_id — SPF heap ID used by all OSResampler memory allocations.
 * Declared as extern in osrc_resampler_port.h; defined here as this is the
 * only compiled TU in the opensource_resampler library that references it.
 * Set via osrc_rs_set_heap_id() before every resampleFixedRatioInit() call.
 */
uint32_t g_osrc_rs_heap_id = 0;

/*---------------------------------------------------------------------------
 * Constants
 *--------------------------------------------------------------------------*/

#define OSRC_RS_DEFAULT_NUM_TAPS     48
#define OSRC_RS_DEFAULT_MAX_FILTERS  48
#define OSRC_RS_DEFAULT_FLAGS        (SUBSAMPLE_INTERPOLATE | BLACKMAN_HARRIS)

/* Cycles per multiply-accumulate for OSResampler on Hexagon.
 * Formula: KPPS = (num_taps * output_sample_rate * channels * C) / 1000 + 50.
 * output_sample_rate is the correct cost driver for both upsamples and downsamples,
 * matching the pcm_mf_cnv / dynamic-resampler standard.
 *
 * C=16 is calibrated to be safe (no under-vote) across 11 confirmed SR-conversion
 * cases measured from 170-test sim profiling (parallel_tests_report_170_enriched):
 *
 *   Conversion        Dir  P-phases  Sim KPPS  C_backsolved  Voted(C=16)  Error
 *   44.1K->48K 2ch    up     160      70,560       15.30       73,778      +4.6%  <- dominant
 *   44K->48K   2ch    up      12      55,125       11.95       73,778     +33.8%
 *   32K->48K   2ch    up       3      33,632–47,040  7.3–10.2  73,778  +57–119%
 *   48K->192K  2ch    up       4     134,448        7.29      294,962    +119%
 *   48K->384K  2ch    up       8     285,024        7.73      589,874    +107%
 *   48K->44.1K 2ch    dn     147      50,976       12.03       67,787     +33%
 *   192K->48K  2ch    dn       1      44,160        9.57       73,778     +67%
 *   192K->96K  2ch    dn       1      77,568        8.41      147,506     +90%
 *   384K->48K  2ch    dn       1      54,912       11.91       73,778     +34%
 *   384K->192K 2ch    dn       1     155,136        8.41      294,962     +90%
 *
 * C=12 (prior value) under-voted 44.1K->48K by 21.6% — unsafe for the dominant
 * CD-audio use case.  C=16 targets the high-phase-count worst case (P=160,
 * C_backsolved=15.3) and conservatively over-votes all other cases.
 * Over-votes on large-ratio conversions (48K->192/384K, 192/384K->48K) are accepted
 * as safe resource-reservation margin; the formula cannot be tightened without
 * exposing the polyphase phase count P from the Speex lib. */
#define OSRC_CYCLES_PER_MAC  16

// DM mode values (compatible with hwsw_rs_lib)
#define FIXED_IN  0
#define FIXED_OUT 1

/*---------------------------------------------------------------------------
 * Helper Functions
 *--------------------------------------------------------------------------*/

/**
 * Allocate float conversion buffers
 */
static ar_result_t osrc_rs_allocate_float_buffers(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t samples_per_ch)
{
   if (!osrc_rs_ptr) {
      return AR_EFAILED;
   }

   uint32_t num_channels = osrc_rs_ptr->media_fmt.num_channels;

   // Free existing buffers if present
   if (osrc_rs_ptr->float_input_ptrs) {
      for (uint32_t ch = 0; ch < num_channels; ch++) {
         if (osrc_rs_ptr->float_input_ptrs[ch]) {
            posal_memory_free(osrc_rs_ptr->float_input_ptrs[ch]);
         }
      }
      posal_memory_free(osrc_rs_ptr->float_input_ptrs);
      osrc_rs_ptr->float_input_ptrs = NULL;
   }

   if (osrc_rs_ptr->float_output_ptrs) {
      for (uint32_t ch = 0; ch < num_channels; ch++) {
         if (osrc_rs_ptr->float_output_ptrs[ch]) {
            posal_memory_free(osrc_rs_ptr->float_output_ptrs[ch]);
         }
      }
      posal_memory_free(osrc_rs_ptr->float_output_ptrs);
      osrc_rs_ptr->float_output_ptrs = NULL;
   }

   // Allocate pointer arrays
   osrc_rs_ptr->float_input_ptrs = (float **)posal_memory_malloc(
       num_channels * sizeof(float *), osrc_rs_ptr->heap_id);
   if (!osrc_rs_ptr->float_input_ptrs) {
      return AR_ENOMEMORY;
   }

   osrc_rs_ptr->float_output_ptrs = (float **)posal_memory_malloc(
       num_channels * sizeof(float *), osrc_rs_ptr->heap_id);
   if (!osrc_rs_ptr->float_output_ptrs) {
      posal_memory_free(osrc_rs_ptr->float_input_ptrs);
      osrc_rs_ptr->float_input_ptrs = NULL;
      return AR_ENOMEMORY;
   }

   // Allocate per-channel buffers
   for (uint32_t ch = 0; ch < num_channels; ch++) {
      osrc_rs_ptr->float_input_ptrs[ch] = (float *)posal_memory_malloc(
          samples_per_ch * sizeof(float), osrc_rs_ptr->heap_id);
      if (!osrc_rs_ptr->float_input_ptrs[ch]) {
         return AR_ENOMEMORY;
      }

      osrc_rs_ptr->float_output_ptrs[ch] = (float *)posal_memory_malloc(
          samples_per_ch * sizeof(float), osrc_rs_ptr->heap_id);
      if (!osrc_rs_ptr->float_output_ptrs[ch]) {
         return AR_ENOMEMORY;
      }
   }

   osrc_rs_ptr->float_buf_size_per_ch = samples_per_ch;
   return AR_EOK;
}

/**
 * Free float conversion buffers
 */
static void osrc_rs_free_float_buffers(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr) {
      return;
   }

   uint32_t num_channels = osrc_rs_ptr->media_fmt.num_channels;

   if (osrc_rs_ptr->float_input_ptrs) {
      for (uint32_t ch = 0; ch < num_channels; ch++) {
         if (osrc_rs_ptr->float_input_ptrs[ch]) {
            posal_memory_free(osrc_rs_ptr->float_input_ptrs[ch]);
         }
      }
      posal_memory_free(osrc_rs_ptr->float_input_ptrs);
      osrc_rs_ptr->float_input_ptrs = NULL;
   }

   if (osrc_rs_ptr->float_output_ptrs) {
      for (uint32_t ch = 0; ch < num_channels; ch++) {
         if (osrc_rs_ptr->float_output_ptrs[ch]) {
            posal_memory_free(osrc_rs_ptr->float_output_ptrs[ch]);
         }
      }
      posal_memory_free(osrc_rs_ptr->float_output_ptrs);
      osrc_rs_ptr->float_output_ptrs = NULL;
   }

   osrc_rs_ptr->float_buf_size_per_ch = 0;
}

/*---------------------------------------------------------------------------
 * Public API Implementation
 *--------------------------------------------------------------------------*/

void osrc_rs_lib_init(osrc_resampler_lib_t *osrc_rs_lib)
{
   if (!osrc_rs_lib) {
      return;
   }

   memset(osrc_rs_lib, 0, sizeof(osrc_resampler_lib_t));

   // Set default quality configuration
   osrc_rs_lib->num_taps = OSRC_RS_DEFAULT_NUM_TAPS;
   osrc_rs_lib->max_filters = OSRC_RS_DEFAULT_MAX_FILTERS;
   osrc_rs_lib->rs_flags = OSRC_RS_DEFAULT_FLAGS;
}

ar_result_t osrc_rs_lib_deinit(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr) {
      return AR_EBADPARAM;
   }

   // Free OSResampler context
   if (osrc_rs_ptr->rs_cxt) {
      resampleFree(osrc_rs_ptr->rs_cxt);
      osrc_rs_ptr->rs_cxt = NULL;
   }

   // Free float conversion buffers
   osrc_rs_free_float_buffers(osrc_rs_ptr);

   // Clear structure
   memset(osrc_rs_ptr, 0, sizeof(osrc_resampler_lib_t));

   return AR_EOK;
}

ar_result_t osrc_rs_lib_check_create_resampler_instance(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t heap_id)
{
   if (!osrc_rs_ptr) {
      return AR_EBADPARAM;
   }

   // Validate media format
   if (osrc_rs_ptr->media_fmt.inp_sample_rate == 0 ||
       osrc_rs_ptr->media_fmt.output_sample_rate == 0 ||
       osrc_rs_ptr->media_fmt.num_channels == 0) {
      AR_MSG(DBG_ERROR_PRIO,
             "osrc_rs_lib: Invalid media format for resampler creation");
      return AR_EBADPARAM;
   }

   // Store heap ID
   osrc_rs_ptr->heap_id = heap_id;

   // Free existing context if present
   if (osrc_rs_ptr->rs_cxt) {
      resampleFree(osrc_rs_ptr->rs_cxt);
      osrc_rs_ptr->rs_cxt = NULL;
   }

   // Determine flags
   int flags = osrc_rs_ptr->rs_flags;
   if (osrc_rs_ptr->media_fmt.output_sample_rate <
       osrc_rs_ptr->media_fmt.inp_sample_rate) {
      flags |= INCLUDE_LOWPASS;
   }

   // Set heap ID for OSResampler allocations
   osrc_rs_set_heap_id(heap_id);

   // Create OSResampler context
   osrc_rs_ptr->rs_cxt = resampleFixedRatioInit(
       osrc_rs_ptr->media_fmt.num_channels,
       osrc_rs_ptr->num_taps,
       osrc_rs_ptr->max_filters,
       (double)osrc_rs_ptr->media_fmt.inp_sample_rate,
       (double)osrc_rs_ptr->media_fmt.output_sample_rate,
       0,  // lowpassFreq (auto)
       flags);

   if (!osrc_rs_ptr->rs_cxt) {
      AR_MSG(DBG_ERROR_PRIO, "osrc_rs_lib: resampleFixedRatioInit failed");
      return AR_EFAILED;
   }

   // Calculate and cache events
   osrc_rs_ptr->events_config.KPPS = osrc_rs_lib_get_kpps(osrc_rs_ptr);
   osrc_rs_ptr->events_config.delay_in_us = osrc_rs_lib_get_alg_delay(osrc_rs_ptr);
   osrc_rs_ptr->events_config.bandwidth = osrc_rs_lib_get_bw(osrc_rs_ptr);

   /* Pre-warm the polyphase filter delay line.
    *
    * After resampleFixedRatioInit the filter state is cold. On the first real
    * process call the library can only consume (N-1) input samples instead of N
    * (e.g. 47 of 48 for 48→32 kHz) because the delay line is not yet filled.
    * This leaves a 1-sample (2-byte) residual that cascades across IPC buffer
    * boundaries and eventually deadlocks the write-buffer return path.
    *
    * Fix: drive one silent (zero-filled) frame of exactly `in_samples_per_ch`
    * samples through the filter at init time.  This fills the delay line so
    * the first real data call consumes all available input samples cleanly.
    * The discarded output introduces at most `algo_delay_us` µs of latency
    * offset which is already accounted for in the algo-delay event raised above.
    */
   {
      uint32_t in_sr  = osrc_rs_ptr->media_fmt.inp_sample_rate;
      uint32_t out_sr = osrc_rs_ptr->media_fmt.output_sample_rate;
      uint32_t ch     = osrc_rs_ptr->media_fmt.num_channels;

      /* One 1 ms frame worth of input samples (same as the container frame). */
      uint32_t in_samp  = (in_sr  + 999) / 1000;  /* ceil(sr/1000) */
      uint32_t out_samp = (out_sr + 999) / 1000;

      /* Allocate temporary per-channel float arrays on the stack.
       * Maximum supported: 48000/1000 = 48 samples per channel. */
      float zero_in[48]  = { 0.0f };
      float zero_out[48] = { 0.0f };

      const float *in_ptrs[8];   /* up to 8 channels */
      float       *out_ptrs[8];

      if (ch <= 8 && in_samp <= 48 && out_samp <= 48)
      {
         for (uint32_t c = 0; c < ch; c++)
         {
            in_ptrs[c]  = zero_in;
            out_ptrs[c] = zero_out;
         }

         resampleProcess(osrc_rs_ptr->rs_cxt,
                         (const float *const *)in_ptrs,
                         (int)in_samp,
                         (float *const *)out_ptrs,
                         (int)out_samp,
                         1.0);

         AR_MSG(DBG_HIGH_PRIO,
                "osrc_rs_lib: prewarm done (%u->%u Hz, %u ch, in=%u out_max=%u)",
                in_sr, out_sr, ch, in_samp, out_samp);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO,
                "osrc_rs_lib: prewarm skipped — ch=%u in_samp=%u out_samp=%u exceeds limits",
                ch, in_samp, out_samp);
      }
   }

   /* Pre-allocate float conversion buffers at init time so that the first
    * process() call never triggers a heap allocation.  A heap malloc inside
    * process() causes the container to re-evaluate port thresholds and exit
    * the inner data loop prematurely, deadlocking the write-buffer return
    * path in a non-STM upstream container (e.g. STREAM container with
    * MODULE_ID_MFC_OSRC).
    *
    * Size: max(in_samples_per_1ms, out_samples_per_1ms) per channel.
    * This matches the required_buf_size computed in osrc_rs_lib_process and
    * guarantees the realloc guard (float_buf_size_per_ch < required_buf_size)
    * is FALSE on every steady-state call.
    */
   {
      uint32_t in_sr    = osrc_rs_ptr->media_fmt.inp_sample_rate;
      uint32_t out_sr   = osrc_rs_ptr->media_fmt.output_sample_rate;
      uint32_t in_samp  = (in_sr  + 999) / 1000;
      uint32_t out_samp = (out_sr + 999) / 1000;
      uint32_t buf_size = (in_samp > out_samp) ? in_samp : out_samp;

      ar_result_t alloc_result = osrc_rs_allocate_float_buffers(osrc_rs_ptr, buf_size);
      if (AR_EOK != alloc_result)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "osrc_rs_lib: pre-alloc float buffers failed (size=%u ch=%u) — will retry in process",
                buf_size, osrc_rs_ptr->media_fmt.num_channels);
         /* Non-fatal: process() will allocate on first call, just less safely */
      }
      else
      {
         AR_MSG(DBG_HIGH_PRIO,
                "osrc_rs_lib: pre-alloc float buffers done (size=%u ch=%u)",
                buf_size, osrc_rs_ptr->media_fmt.num_channels);
      }
   }

   AR_MSG(DBG_HIGH_PRIO,
          "osrc_rs_lib: Created resampler %d->%d Hz, %d ch, %d taps, KPPS=%d",
          osrc_rs_ptr->media_fmt.inp_sample_rate,
          osrc_rs_ptr->media_fmt.output_sample_rate,
          osrc_rs_ptr->media_fmt.num_channels,
          osrc_rs_ptr->num_taps,
          osrc_rs_ptr->events_config.KPPS);

   return AR_EOK;
}

ar_result_t osrc_rs_lib_process(
    osrc_resampler_lib_t *osrc_rs_ptr,
    capi_buf_t *input_buf_ptr,
    capi_buf_t *output_buf_ptr,
    uint32_t input_num_bufs,
    uint32_t output_num_bufs,
    uint32_t heap_id)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt || !input_buf_ptr || !output_buf_ptr) {
      AR_MSG(DBG_ERROR_PRIO,
             "osrc_rs_lib: process NULL ptr — rs_ptr=%p rs_cxt=%p in=%p out=%p",
             osrc_rs_ptr,
             osrc_rs_ptr ? osrc_rs_ptr->rs_cxt : NULL,
             input_buf_ptr, output_buf_ptr);
      return AR_EBADPARAM;
   }

   uint32_t num_channels = osrc_rs_ptr->media_fmt.num_channels;
   uint32_t bytes_per_sample = osrc_rs_ptr->media_fmt.bits_per_sample / 8;
   uint32_t q_factor = osrc_rs_ptr->media_fmt.q_factor;

   // Calculate sample counts from byte lengths
   int num_input_frames = input_buf_ptr[0].actual_data_len / bytes_per_sample;
   int num_output_frames = output_buf_ptr[0].max_data_len / bytes_per_sample;

   #ifdef OSRC_DEBUG
   AR_MSG(DBG_LOW_PRIO,
          "osrc_rs_lib: process ENTRY in_actual=%lu in_max=%lu out_max=%lu"
          " in_samp=%d out_max_samp=%d ch=%lu bps=%lu q=%lu",
          input_buf_ptr[0].actual_data_len,
          input_buf_ptr[0].max_data_len,
          output_buf_ptr[0].max_data_len,
          num_input_frames,
          num_output_frames,
          num_channels,
          bytes_per_sample,
          q_factor);
   AR_MSG(DBG_LOW_PRIO,
          "osrc_rs_lib: process ENTRY in_sr=%lu out_sr=%lu dm_mode=%lu fix_out_samp=%lu",
          osrc_rs_ptr->media_fmt.inp_sample_rate,
          osrc_rs_ptr->media_fmt.output_sample_rate,
          osrc_rs_ptr->dm_mode,
          osrc_rs_ptr->output_fixed_samples);
   #endif

   // Allocate/reallocate float buffers if needed
   uint32_t required_buf_size = (num_input_frames > num_output_frames) ?
                                 num_input_frames : num_output_frames;
   if (osrc_rs_ptr->float_buf_size_per_ch < required_buf_size) {
      AR_MSG(DBG_LOW_PRIO,
             "osrc_rs_lib: float buf realloc old=%lu new=%lu ch=%lu",
             osrc_rs_ptr->float_buf_size_per_ch, required_buf_size, num_channels);
      ar_result_t result = osrc_rs_allocate_float_buffers(osrc_rs_ptr, required_buf_size);
      if (AR_EOK != result) {
         AR_MSG(DBG_ERROR_PRIO,
                "osrc_rs_lib: Failed to allocate float buffers (%lu samp x %lu ch) — DATA LOSS",
                required_buf_size, num_channels);
         return result;
      }
   }

   // Convert fixed-point input to float
   float scale_to_float = osrc_rs_get_q_factor_scale(q_factor);
   for (uint32_t ch = 0; ch < num_channels; ch++) {
      if (bytes_per_sample == 2) {
         // 16-bit Q15
         int16_t *src = (int16_t *)input_buf_ptr[ch].data_ptr;
         float *dst = osrc_rs_ptr->float_input_ptrs[ch];
         for (int i = 0; i < num_input_frames; i++) {
            dst[i] = (float)src[i] * scale_to_float;
         }
      } else {
         // 24-bit or 32-bit (Q27 or Q31)
         int32_t *src = (int32_t *)input_buf_ptr[ch].data_ptr;
         float *dst = osrc_rs_ptr->float_input_ptrs[ch];
         for (int i = 0; i < num_input_frames; i++) {
            dst[i] = (float)src[i] * scale_to_float;
         }
      }
   }

   #ifdef OSRC_DEBUG
   AR_MSG(DBG_LOW_PRIO,
          "osrc_rs_lib: calling resampleProcess num_input_frames=%d num_output_frames=%d",
          num_input_frames, num_output_frames);
   #endif

   /* Multi-frame loop — mirrors pc_iir_resampler_process in the original MFC lib.
    *
    * When in_frame_samples_per_ch > 0 (set via osrc_rs_lib_set_frame_size after the
    * container frame duration is known), compute proc_multiplier = min(in_frames_avail,
    * out_frames_avail) and call resampleProcess once per frame, advancing the floating-
    * point buffer pointers.  This drains the entire input buffer in a single capi
    * process() call instead of 1-frame-per-call, matching the IIR resampler looping
    * behaviour that allows a 4096-byte write buffer to be consumed without IPC buffer
    * starvation.
    *
    * When in_frame_samples_per_ch == 0 (not yet set), fall back to the original
    * single-call behaviour for safety.
    */
   uint32_t total_input_used     = 0;
   uint32_t total_output_generated = 0;

   if (osrc_rs_ptr->in_frame_samples_per_ch > 0 &&
       osrc_rs_ptr->out_frame_samples_per_ch > 0)
   {
      uint32_t in_fs  = osrc_rs_ptr->in_frame_samples_per_ch;
      uint32_t out_fs = osrc_rs_ptr->out_frame_samples_per_ch;

      uint32_t in_proc_mult  = (uint32_t)num_input_frames  / in_fs;
      uint32_t out_proc_mult = (uint32_t)num_output_frames / out_fs;
      uint32_t proc_mult     = (in_proc_mult < out_proc_mult) ? in_proc_mult : out_proc_mult;

      /* partial_frame: either side has fewer samples than one full frame.
       * Run the loop exactly once, but pass the actual available sample counts
       * instead of the nominal frame sizes so resampleProcess does not read
       * past the converted float buffer (which only holds num_input_frames
       * samples) and does not write past the output buffer. */
      bool_t partial_frame = (proc_mult == 0);
      if (partial_frame)
      {
         proc_mult = 1;
      }

      #ifdef OSRC_DEBUG
      AR_MSG(DBG_LOW_PRIO,
             "osrc_rs_lib: multi-frame loop in_fs=%u out_fs=%u in_mult=%u out_mult=%u proc_mult=%u partial=%u",
             in_fs, out_fs, in_proc_mult, out_proc_mult, proc_mult, (uint32_t)partial_frame);
      #endif
      
      for (uint32_t f = 0; f < proc_mult; f++)
      {
         /* Build per-frame pointer arrays pointing into the pre-converted float bufs */
         const float *frame_in[CAPI_MAX_CHANNELS_V2];
         float       *frame_out[CAPI_MAX_CHANNELS_V2];
         for (uint32_t c = 0; c < num_channels; c++)
         {
            frame_in[c]  = osrc_rs_ptr->float_input_ptrs[c]  + total_input_used;
            frame_out[c] = osrc_rs_ptr->float_output_ptrs[c] + total_output_generated;
         }

         /* For a partial frame use the actual available counts; for full frames
          * use the nominal frame sizes so the resampler's internal state machine
          * advances by exactly one frame per call (as it was designed for). */
         int call_in_samp  = partial_frame ? num_input_frames : (int)in_fs;
         int call_out_samp = partial_frame ? num_output_frames : (int)out_fs;

         ResampleResult r = resampleProcess(
             osrc_rs_ptr->rs_cxt,
             (const float *const *)frame_in,
             call_in_samp,
             (float *const *)frame_out,
             call_out_samp,
             1.0);

         total_input_used      += (uint32_t)r.input_used;
         total_output_generated += (uint32_t)r.output_generated;

         if (r.input_used == 0)
         {
            AR_MSG(DBG_LOW_PRIO,
                   "osrc_rs_lib: multi-frame loop stalled at frame %u — stopping", f);
            break;
         }
      }
   }
   else
   {
      /* Frame size not yet set — single call (startup / DM path). */
      ResampleResult r = resampleProcess(
          osrc_rs_ptr->rs_cxt,
          (const float *const *)osrc_rs_ptr->float_input_ptrs,
          num_input_frames,
          (float *const *)osrc_rs_ptr->float_output_ptrs,
          num_output_frames,
          1.0);
      total_input_used       = (uint32_t)r.input_used;
      total_output_generated = (uint32_t)r.output_generated;
   }

   #ifdef OSRC_DEBUG
   AR_MSG(DBG_LOW_PRIO,
          "osrc_rs_lib: resampleProcess done input_used=%d output_generated=%d"
          " (avail_in=%d max_out=%d)",
          total_input_used, total_output_generated,
          num_input_frames, num_output_frames);
   #endif

   if (total_input_used == 0 && num_input_frames > 0)
   {
      AR_MSG(DBG_LOW_PRIO,
             "osrc_rs_lib: WARNING 0 of %d input samples consumed — startup fill or stall",
             num_input_frames);
   }

   // Convert float output back to fixed-point
   float scale_to_fixed = osrc_rs_get_q_factor_inverse_scale(q_factor);
   int32_t max_val = osrc_rs_get_q_factor_max(q_factor);
   int32_t min_val = -max_val - 1;

   for (uint32_t ch = 0; ch < num_channels; ch++) {
      float *src = osrc_rs_ptr->float_output_ptrs[ch];

      if (bytes_per_sample == 2) {
         // 16-bit Q15
         int16_t *dst = (int16_t *)output_buf_ptr[ch].data_ptr;
         for (uint32_t i = 0; i < total_output_generated; i++) {
            float val = src[i] * scale_to_fixed;
            if (val > max_val) val = max_val;
            if (val < min_val) val = min_val;
            dst[i] = (int16_t)val;
         }
      } else {
         // 24-bit or 32-bit (Q27 or Q31)
         int32_t *dst = (int32_t *)output_buf_ptr[ch].data_ptr;
         for (uint32_t i = 0; i < total_output_generated; i++) {
            float val = src[i] * scale_to_fixed;
            if (val > max_val) val = max_val;
            if (val < min_val) val = min_val;
            dst[i] = (int32_t)val;
         }
      }

      output_buf_ptr[ch].actual_data_len = total_output_generated * bytes_per_sample;
   }

   // Update input consumed on buf[0] only.
   // For DEINTERLEAVED_UNPACKED_V2: buf[1..N-1].actual_data_len is 0 by contract
   //   (only buf[0] is authoritative). Subtracting from ch[1] would always underflow.
   // For DEINTERLEAVED_UNPACKED (V1): the pre-call sync in pc_osrc_resampler_process
   //   already made all channels equal; the post-call fixup propagates the updated
   //   buf[0] back to buf[1..N-1] after we return, so updating buf[0] here is enough.
   // For INTERLEAVED / PACKED: bufs_num==1, only buf[0] exists.
   uint32_t consumed_bytes = (uint32_t)(total_input_used * bytes_per_sample);
   if (consumed_bytes > input_buf_ptr[0].actual_data_len)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "osrc_rs_lib: consumed_bytes %lu > in_actual[0] %lu — dropping",
             consumed_bytes, input_buf_ptr[0].actual_data_len);
      //input_buf_ptr[0].actual_data_len = 0;
   }
   else if(consumed_bytes < input_buf_ptr[0].actual_data_len)
   {
	   input_buf_ptr[0].actual_data_len = consumed_bytes;
   }
   else
   {
      //input_buf_ptr[0].actual_data_len -= consumed_bytes;
	  // Do Nothing
   }

   #ifdef OSRC_DEBUG
   AR_MSG(DBG_LOW_PRIO,
          "osrc_rs_lib: process EXIT in_consumed=%lu in_remaining=%lu out_produced=%lu",
          consumed_bytes,
          input_buf_ptr[0].actual_data_len,
          output_buf_ptr[0].actual_data_len);
   #endif

   return AR_EOK;
}

void osrc_rs_set_lib_mf(
    osrc_resampler_lib_t *osrc_rs_ptr,
    osrc_rs_media_fmt_t *inp_mf)
{
   if (!osrc_rs_ptr || !inp_mf) {
      return;
   }

   // Copy media format
   osrc_rs_ptr->media_fmt = *inp_mf;
}

void osrc_rs_lib_set_dm_config(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t dm_mode,
    uint32_t fixed_out_samples)
{
   if (!osrc_rs_ptr) {
      return;
   }

   osrc_rs_ptr->dm_mode = dm_mode;
   osrc_rs_ptr->output_fixed_samples = fixed_out_samples;
}

void osrc_rs_lib_set_frame_size(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t in_frame_samples_per_ch,
    uint32_t out_frame_samples_per_ch)
{
   if (!osrc_rs_ptr) {
      return;
   }
   osrc_rs_ptr->in_frame_samples_per_ch  = in_frame_samples_per_ch;
   osrc_rs_ptr->out_frame_samples_per_ch = out_frame_samples_per_ch;
}

void osrc_rs_lib_set_config(
    uint32_t use_hwrs,
    uint16_t dyn_mode,
    uint16_t delay,
    osrc_resampler_lib_t *osrc_rs_ptr,
    bool_t *update_rs)
{
   if (!osrc_rs_ptr) {
      return;
   }

   // Log warnings for unsupported features
   if (use_hwrs) {
      AR_MSG(DBG_HIGH_PRIO,
             "osrc_rs_lib: HW resampler not supported, using SW resampler");
   }

   // OSResampler is always dynamic (ASRC capable), so dyn_mode is ignored
   // OSResampler delay is always numTaps/2, so delay type is ignored

   // Always indicate update needed (conservative approach)
   if (update_rs) {
      *update_rs = TRUE;
   }
}

uint32_t osrc_rs_lib_get_kpps(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt) {
      return 0;
   }

   /* KPPS = (num_taps * output_sample_rate * channels * OSRC_CYCLES_PER_MAC) / 1000 + 50.
    * output_sample_rate is the cost driver for both upsamples (polyphase interpolation
    * runs at output rate) and downsamples (filter decimation rate = output rate).
    * This matches the pcm_mf_cnv / dynamic-resampler KPPS convention.
    * OSRC_CYCLES_PER_MAC=16: see constant definition for full calibration table. */
   uint32_t kpps = (osrc_rs_ptr->num_taps *
                    osrc_rs_ptr->media_fmt.output_sample_rate *
                    osrc_rs_ptr->media_fmt.num_channels * OSRC_CYCLES_PER_MAC) / 1000;
   //TODO: incorporate the kpps table here
   //TODO: check how many memcpy are done with capi and fwk and accordingly put the bw

   kpps += 50;

   return kpps;
}

uint32_t osrc_rs_lib_get_bw(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr) {
      return 0;
   }

   uint32_t bytes_per_sample = osrc_rs_ptr->media_fmt.bits_per_sample / 8;

   // Input bandwidth
   uint32_t input_bw = osrc_rs_ptr->media_fmt.inp_sample_rate *
                       osrc_rs_ptr->media_fmt.num_channels *
                       bytes_per_sample;

   // Output bandwidth
   uint32_t output_bw = osrc_rs_ptr->media_fmt.output_sample_rate *
                        osrc_rs_ptr->media_fmt.num_channels *
                        bytes_per_sample;

   return input_bw + output_bw;
}

uint32_t osrc_rs_lib_get_alg_delay(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt) {
      return 0;
   }

   // Delay = (numTaps / 2) * 1,000,000 / input_sample_rate
   uint32_t delay_samples = osrc_rs_ptr->num_taps / 2;
   uint32_t delay_us = (delay_samples * 1000000) /
                       osrc_rs_ptr->media_fmt.inp_sample_rate;

   return delay_us;
}

ar_result_t osrc_rs_lib_get_required_input_samples(osrc_resampler_lib_t *osrc_rs_ptr,
                                                   uint32_t              req_out_samples,
                                                   uint32_t             *exp_in_samples)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt || !exp_in_samples) {
      return AR_EBADPARAM;
   }

   *exp_in_samples = (uint32_t)resampleGetRequiredSamples(osrc_rs_ptr->rs_cxt,
                                                          (int)req_out_samples,
                                                          1.0 /* ratio ignored for fixed-ratio */);
   return AR_EOK;
}

uint32_t osrc_rs_lib_get_in_len(osrc_resampler_lib_t *osrc_rs_ptr, uint32_t req_in_samples)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt) {
      return 0;
   }

   return (uint32_t)resampleGetExpectedOutput(osrc_rs_ptr->rs_cxt,
                                              (int)req_in_samples,
                                              1.0 /* ratio ignored for fixed-ratio */);
}

void osrc_rs_lib_get_process_check(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t *process_check)
{
   if (!osrc_rs_ptr || !process_check) {
      return;
   }

   // Return TRUE if resampler is active
   *process_check = (osrc_rs_ptr->rs_cxt != NULL) ? 1 : 0;
}

ar_result_t osrc_rs_lib_algo_reset(osrc_resampler_lib_t *osrc_rs_ptr)
{
   if (!osrc_rs_ptr || !osrc_rs_ptr->rs_cxt) {
      return AR_EBADPARAM;
   }

   resampleReset(osrc_rs_ptr->rs_cxt);
   return AR_EOK;
}

bool_t osrc_rs_lib_is_multi_stage_supported(osrc_resampler_lib_t *osrc_rs_ptr)
{
   // OSResampler does not support multi-stage resampling
   return FALSE;
}

ar_result_t osrc_rs_lib_set_hwrs_suspend_resume(
    bool_t is_suspend,
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t *create_rs)
{
   // No-op for SW-only resampler
   if (create_rs) {
      *create_rs = 0;
   }
   return AR_EOK;
}

void osrc_rs_lib_process_get_hw_process_info(
    osrc_resampler_lib_t *osrc_rs_ptr,
    uint32_t input_actual_samples,
    uint32_t output_max_samples,
    uint32_t *input_samples_to_consume_ptr,
    uint32_t *output_samples_to_generate_ptr)
{
   // No-op for SW-only resampler
   // For DM mode support, use resampleGetRequiredSamples/GetExpectedOutput instead
   if (input_samples_to_consume_ptr) {
      *input_samples_to_consume_ptr = input_actual_samples;
   }
   if (output_samples_to_generate_ptr) {
      *output_samples_to_generate_ptr = output_max_samples;
   }
}