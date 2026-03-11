/*==========================================================================
 * Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * =========================================================================*/

/**
 *   \file capi_spr_avsync_utils.c
 *   \brief
 *        This file contains implementation of the AVSync Utilities used by the SPR module
 *
 */

#include "capi_spr_i.h"

static const uint32_t SPR_TIME_CONV_FACTOR_PER_SEC = 100000000L; //10^8 factor

#ifdef FLOATING_POINT_DEFINED
static const uint32_t SPR_TIME_CONV_FACTOR_PER_US_SEC = 100;
#endif

/*==============================================================================
   Static Functions
==============================================================================*/

static void capi_spr_avsync_check_for_hold_zeroes(capi_spr_t *           me_ptr,
                                                  capi_stream_data_v2_t *input_ptr,
                                                  render_decision_t *    render_decision,
                                                  int64_t                render_delta_us);
static inline uint64_t spr_convert_samples_to_us(uint64_t  samples,
                                                 uint32_t  sample_rate,
                                                 uint64_t *fract_time_ptr);

static void spr_avsync_apply_scale_factor(avsync_t *avsync_ptr, uint64_t in_samples, uint32_t tsm_sf, uint32_t spr_sr);
static int64_t get_reference_session_time(capi_spr_t *me_ptr);
static void spr_calculate_session_start_time(capi_spr_t *me_ptr, capi_stream_data_t *input_strm_ptr);
static void spr_calculate_init_session_time(capi_spr_t *me_ptr, capi_stream_data_t *input_strm_ptr);

/*==============================================================================
   Functions
==============================================================================*/
#ifdef FLOATING_POINT_DEFINED
static uint64_t spr_bytes_to_us_in_floating_point(uint64_t samples, uint32_t sample_rate, uint64_t *fract_time_ptr)
{
   uint64_t time_us        = 0;
   double   scaled_samples = ((double)samples * SPR_TIME_CONV_FACTOR_PER_SEC);
   double   net_time = (scaled_samples / (uint64_t)sample_rate) + ((NULL == fract_time_ptr) ? 0 : *fract_time_ptr);
   time_us           = net_time / SPR_TIME_CONV_FACTOR_PER_US_SEC;
   if (fract_time_ptr)
   {
      *fract_time_ptr = net_time - (time_us * SPR_TIME_CONV_FACTOR_PER_US_SEC);
   }
   return time_us;
}
#endif

/**
 * This utility is used by SPR to convert samples to time to report session time back to
 * the clients (HLOS). Use of samples is especially needed for fractional cases to
 * avoid precision errors.
 *
 * The existing capi utility capi_cmn_bytes_to_us uses a precision of ns. The same cannot
 * be used directly in SPR due to 2 reasons.
 *   1. The existing capi utility uses a 32 bit input argument for num bytes which can
 *      easily overflow in long time playback.
 *   2. Using a precision of ns has a direct impact on time for which we can continuously
 *      run a playback session.
 *
 * To tradeoff between the precision and time for continuous playback, the factor used in this
 * function is 10^8. This ensures that we can run the playback for days together
 *
 *    ~05 days for 384kHz
 *    ~11 days for 192kHz
 *    ~44 days for  48kHz
 *    ~48 days for  44kHz
 *
 *
 * TODO: Long term solution using a modulo approach to allow us to take full advantage of the 64 bit
 * precision.
 */
static inline uint64_t spr_convert_samples_to_us(uint64_t  samples,
                                                 uint32_t  sample_rate,
                                                 uint64_t *fract_time_ptr)
{
   uint64_t time_us = 0;
   if (sample_rate != 0)
   {
#ifdef FLOATING_POINT_DEFINED
   time_us = spr_bytes_to_us_in_floating_point(samples, sample_rate,fract_time_ptr);
#else
      uint64_t scaled_samples = ((uint64_t)samples * SPR_TIME_CONV_FACTOR_PER_SEC);
      uint64_t net_time       = (scaled_samples / (uint64_t)sample_rate) + ((NULL == fract_time_ptr) ? 0 : *fract_time_ptr);

      time_us = net_time / (SPR_TIME_CONV_FACTOR_PER_SEC / NUM_US_PER_SEC);

      if (fract_time_ptr)
      {
         *fract_time_ptr = net_time - (time_us * (SPR_TIME_CONV_FACTOR_PER_SEC / NUM_US_PER_SEC));
      }
#endif
   }

   return time_us;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_reset_session_clock_for_gapless
  Resets the session clock parameters for the avsync instance during gapless
  usecase. Invoked whenever SPR receives MODULE_CMN_MD_ID_RESET_SESSION_TIME
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_reset_session_clock_for_gapless(avsync_t *avsync_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!is_spr_avsync_enabled(avsync_ptr))
   {
      return CAPI_EOK;
   }

   avsync_ptr->base_timestamp_us         = 0;
   avsync_ptr->expected_session_clock_us = 0;
   avsync_ptr->flags.is_ts_valid         = 0;
   avsync_ptr->proc_timestamp_us         = 0;
   avsync_ptr->session_clock_us          = 0;

   avsync_ptr->elapsed_session_clock_samples          = 0;
   avsync_ptr->tsm_info.tsm_session_clk_samples       = 0;
   avsync_ptr->tsm_info.remainder_tsm_samples         = 0;
   avsync_ptr->elapsed_expected_session_clock_samples = 0;

   SPR_MSG_ISLAND(avsync_ptr->miid, DBG_HIGH_PRIO, "avsync: session time reset received from gapless");

   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_calculate_init_session_time
   Calculates and initializes the session time for the first input buffer
* ------------------------------------------------------------------------------*/
static void spr_calculate_init_session_time(capi_spr_t *me_ptr, capi_stream_data_t *input)
{
   if (NULL == me_ptr || NULL == input)
   {
      // TODO : MSG error
      return;
   }

   avsync_t *avsync_ptr         = me_ptr->avsync_ptr;
   int64_t   input_timestamp_us = input->timestamp;
   bool_t    is_input_ts_valid  = input->flags.is_timestamp_valid;

   if (!is_input_ts_valid || (SPR_RENDER_REFERENCE_WALL_CLOCK == avsync_ptr->client_config.render_reference))
   {
      input_timestamp_us = 0;
   }

   avsync_ptr->base_timestamp_us         = input_timestamp_us;
   avsync_ptr->flags.is_ts_valid         = is_input_ts_valid;
   avsync_ptr->expected_session_clock_us = input_timestamp_us;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Ref Session Time(%ld)", input_timestamp_us);
#endif
   return;
}

/*------------------------------------------------------------------------------
  Function name: spr_avsync_clear_hold_duration
   Clears the hold duration value. This is called from the output process context
   of the module
* ------------------------------------------------------------------------------*/
void spr_avsync_clear_hold_duration(avsync_t *avsync_ptr)
{
   if (!is_spr_avsync_enabled(avsync_ptr))
   {
      return;
   }

   avsync_ptr->hold_zeroes_per_ch = 0;
}


/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_update_session_clock
   Updates the session clock once output is delivered from the primary output port
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_update_session_clock(avsync_t *avsync_ptr, int64_t adj_bytes, uint32_t sr, uint32_t bps)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == avsync_ptr)
   {
#ifdef AVSYNC_DEBUG
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_spr: avsync: Received invalid params for update session clock");
#endif
      return CAPI_EOK;
   }

   if (avsync_ptr->flags.is_dfg)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG_ISLAND(avsync_ptr->miid,
              DBG_MED_PRIO,
              "avsync: dfg is in progress. session time frozen @ %ld",
              avsync_ptr->session_clock_us);
#endif
      return CAPI_EOK;
   }

//#ifdef AVSYNC_DEBUG
   int64_t prev_session_time = avsync_ptr->session_clock_us;
   int64_t prev_session_time_samples = (avsync_ptr->flags.is_timescaled_data)
                                          ? avsync_ptr->tsm_info.tsm_session_clk_samples
                                          : avsync_ptr->elapsed_session_clock_samples;
//#endif

   // Convert adj_bytes to samples
   uint64_t adj_samples = adj_bytes / CAPI_CMN_BITS_TO_BYTES(bps);

   // If TSM MD was received
   if (avsync_ptr->flags.is_timescaled_data)
   {
      uint64_t old_sf_samples = 0, rem_samples = 0;
      uint32_t tsm_sf = 0;

      // Consider the case where new TSM MD is received with non zero offset
      if (avsync_ptr->flags.is_pending_tsm_cfg)
      {
         // Process samples upto offset with previous speed factor
         old_sf_samples = avsync_ptr->cached_tsm_info.offset;
         tsm_sf = avsync_ptr->tsm_info.tsm_speed_factor;

         spr_avsync_apply_scale_factor(avsync_ptr, old_sf_samples, tsm_sf, sr);

         // Apply the new speed factor and clear pending_tsm_cfg
         avsync_ptr->tsm_info.tsm_speed_factor  = avsync_ptr->cached_tsm_info.speed_factor;
         avsync_ptr->cached_tsm_info.offset     = 0;
         avsync_ptr->flags.is_pending_tsm_cfg   = FALSE;
      }

      // Apply the new/current speed factor on remaining/all samples
      tsm_sf = avsync_ptr->tsm_info.tsm_speed_factor;
      rem_samples = adj_samples - old_sf_samples;

      if (rem_samples)
      {
         spr_avsync_apply_scale_factor(avsync_ptr, rem_samples, tsm_sf, sr);
      }

      // Use the scaled TSM samples to update session clock.
      avsync_ptr->session_clock_us =
         spr_convert_samples_to_us(avsync_ptr->tsm_info.tsm_session_clk_samples, sr, NULL /*fract_time_Ptr*/) +
         avsync_ptr->base_timestamp_us;
   }
   else
   {
      // Increment the elasped session clock in samples.
      // Keep updating the timescaled session clock as well to handle the MD on the fly
   avsync_ptr->elapsed_session_clock_samples += adj_samples;
      avsync_ptr->tsm_info.tsm_session_clk_samples += adj_samples;

   avsync_ptr->session_clock_us =
      spr_convert_samples_to_us(avsync_ptr->elapsed_session_clock_samples, sr, NULL /*fract_time_ptr*/) +
      avsync_ptr->base_timestamp_us;
   }

//#ifdef AVSYNC_DEBUG
   // Note: This message has dependency with the variable prev_session_time (declared above)
   int64_t curr_sess_time_samples = (avsync_ptr->flags.is_timescaled_data)
                                       ? avsync_ptr->tsm_info.tsm_session_clk_samples
                                       : avsync_ptr->elapsed_session_clock_samples;
   SPR_MSG_ISLAND(avsync_ptr->miid,
           DBG_LOW_PRIO,
           "avsync: update session time: curr session clock %ld prev session clock %ld "
           "curr_session_time_samples %ld prev_session_time_samples %ld ",
           avsync_ptr->session_clock_us,
           prev_session_time,
           curr_sess_time_samples,
           prev_session_time_samples);
//#endif

   // Note: As of today, expected session time is not scaled in accordance to TSM as it needs more changes
   // for render decision/TS handling across fwk and SPR. This will be taken up in the future based on requirements if any.
   // Hence the expected session time may be out of sync with the current session clock based on the TSM MD being sent or not.
   result |= capi_spr_avsync_update_absolute_time(avsync_ptr, avsync_ptr->ds_delay_us);
   capi_spr_avsync_update_expected_session_time(avsync_ptr, adj_bytes, sr, bps);

   return result;
}

/*---------------------------------------------------------------------------------------------
   Function : spr_calculate_session_start_time
   This function is used to calculate the expected start time of the session based on
   the configuration provided by the client

   The start time parameter is used to specify the time to render the origin of the session
   i.e (timestamp = 0). The following are the supported modes

   -- Immediate Run Mode
       Rendering begins as soon as samples are available to the renderer.
       Session time or clock is initialized to the timestamp of the first sample or
       0 if there?s no valid timestamp.

   -- Absolute Run Mode
       Session clock starts from zero at the given absolute time and then increments in
       step with rendering of the session samples. This absolute time can be in the past/future
       depending on the position of the playback & timestamps of the buffers.

   -- Run with Delay Mode
       This is similar to the absolute mode where the start time is provided as an relative offset
       to the wall clock from which the session has to render
* ----------------------------------------------------------------------------------------------*/
static void spr_calculate_session_start_time(capi_spr_t *me_ptr, capi_stream_data_t *input)
{
   // The caller has already checked if the avsync feature is enabled. So proceed directly
   avsync_t *avsync_ptr = me_ptr->avsync_ptr;

   int64_t input_timestamp   = input->timestamp;
   bool_t  is_input_ts_valid = input->flags.is_timestamp_valid;
   int64_t path_delay_us     = avsync_ptr->ds_delay_us;

   int64_t session_start_time_us = 0, curr_wall_clock_us = 0, client_cfg_time_us = 0;

   // Value set by the client as part of SPR_PARAM_ID_RENDER_CONFIG.
   // Defaults are managed by SPR as part of the init feature for now.
   client_cfg_time_us = (avsync_ptr->client_config.start_time_us);

   if (!is_input_ts_valid)
   {
      input_timestamp = 0;
   }

   curr_wall_clock_us = avsync_ptr->curr_wall_clock_us;

   switch (avsync_ptr->client_config.render_mode)
   {
      // Since SPR is not the last module in the chain, account for the path delay encountered when considering
      // the session_start_time
      case SPR_RENDER_MODE_ABSOLUTE_TIME:
      {
         session_start_time_us = client_cfg_time_us - path_delay_us;
         break;
      }

      // In this case the client provides the value relative to the curr wall clock. Account for path delay
      // for this case as well.
      case SPR_RENDER_MODE_DELAYED:
      {
         session_start_time_us = client_cfg_time_us - path_delay_us;
         break;
      }

      case SPR_RENDER_MODE_IMMEDIATE:
      {
         // If RUN immediate is used with wall clock reference, the session start time is back-calculated
         // so as to render the first buffer immediately. This is to honor the render mode & render reference configs
         if (SPR_RENDER_REFERENCE_WALL_CLOCK == avsync_ptr->client_config.render_reference)
         {
            session_start_time_us = curr_wall_clock_us + path_delay_us - input_timestamp;
            SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Configured for immediate run with Wall Clock Reference");
         }
         else
         {
            session_start_time_us = curr_wall_clock_us;
         }
         break;
      }
   }

   if (session_start_time_us < 0)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Warning! session_start_time_us is negative. Making it zero");
      session_start_time_us = 0;
   }

   avsync_ptr->calc_start_time_us = session_start_time_us;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Session start time (%llu)us, current wall clock (%llu)",
           avsync_ptr->calc_start_time_us,
           curr_wall_clock_us);
#endif

   return;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_update_output_info
   Updates the avsync instance with the information regarding the output
   generated by the module. This involves updates to session time based on
   timestamp validity & underrun scenarios
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_update_output_info(capi_spr_t *        me_ptr,
                                              bool_t              is_erasure,
                                              capi_stream_data_t *pri_out_strm_ptr,
                                              uint32_t            num_bytes_filled)
{
   capi_err_t result = CAPI_EOK;

   if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      return result;
   }

   bool_t is_ts_valid = me_ptr->avsync_ptr->flags.is_ts_valid;

   // Update session time for primary output port under the following conditions :-
   //   If the erasure flag is set, then session clock must not be incremented when time stamp is not valid
   //   If there is no capi input stream, but the spr_stream_read could fill output, update the session clock.

   bool_t update_session_clk = FALSE;

   // If SPR was able to generate valid output
   if (num_bytes_filled)
   {
      // If there is no erasure flag set in the input, valid data flow case
      if (!is_erasure)
      {
         update_session_clk = TRUE;
      }
      else
      {
         // If erasure is set (i.e. underrun), then session clock is
         // updated only if the timestamps are valid.
         if (is_ts_valid)
         {
            update_session_clk = TRUE;
         }
      }
   }
#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_MED_PRIO,
           "avsync: sess_clk_debug update_session_clk = %d num_bytes_filled = %d is_ts_valid = %d "
           "input_strm_ptr->flags.erasure = "
           "%d",
           update_session_clk,
           num_bytes_filled,
           is_ts_valid,
           is_erasure);
#endif

   if (update_session_clk)
   {

#ifdef AVSYNC_DEBUG
      SPR_MSG_ISLAND(me_ptr->miid, DBG_MED_PRIO, "avsync: sess_clk_debug num_bytes_filled = %d", num_bytes_filled);
#endif

      result |= capi_spr_avsync_update_session_clock(me_ptr->avsync_ptr,
                                                     num_bytes_filled,
                                                     me_ptr->operating_mf.format.sampling_rate,
                                                     me_ptr->operating_mf.format.bits_per_sample);

      if (me_ptr->avsync_ptr->flags.is_pending_dfg)
      {
         spr_avsync_set_dfg_flag(me_ptr->avsync_ptr, me_ptr->avsync_ptr->flags.is_pending_dfg);
         me_ptr->avsync_ptr->flags.is_pending_dfg = FALSE;
      }
   }
   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_avsync_is_input_strm_hold_buf_head
   Check & compare if the incoming stream is the head of the hold buffer list
* ------------------------------------------------------------------------------*/
bool_t spr_avsync_is_input_strm_hold_buf_head(avsync_t *avsync_ptr, void *input_strm_ptr)
{
   // Validate if input stream exists and is the head node of the hold buffer list
   if ((input_strm_ptr) && (spr_avsync_does_hold_buf_exist(avsync_ptr)) &&
       (input_strm_ptr == spr_avsync_get_hold_buf_head_obj_ptr(avsync_ptr)))
   {
      return TRUE;
   }

   return FALSE;
}


/*------------------------------------------------------------------------------
  Function name: spr_avsync_get_hold_buf_head_obj_ptr
    Return the head input stream of the hold buffer list
* ------------------------------------------------------------------------------*/
void *spr_avsync_get_hold_buf_head_obj_ptr(avsync_t *avsync_ptr)
{
   if (spr_avsync_does_hold_buf_exist(avsync_ptr))
   {
      return capi_spr_get_list_head_obj_ptr(avsync_ptr->hold_buffer.buf_list_ptr);
   }
   return NULL;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_setup_first_input
   Handles avsync for the first input stream for the module.
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_setup_first_input(capi_spr_t *me_ptr, capi_stream_data_t *input)
{

   // If avsync itself is not enabled, return
   if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      return CAPI_EOK;
   }

   avsync_t *avsync_ptr = me_ptr->avsync_ptr;

   // use this to enable data flow again if required.
   if (!input->flags.erasure)
   {
      bool_t DATA_FLOW_RESUME = FALSE;
      spr_avsync_set_dfg_flag(avsync_ptr, DATA_FLOW_RESUME);
   }

   if (UMAX_32 == me_ptr->primary_output_arr_idx)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "avsync, avsync functions called without setting up primary out port.");
   }
   else
   {
      uint32_t pri_arr_idx = me_ptr->primary_output_arr_idx;
      // Update the path delay for each iteration of SPR
      (void)spr_aggregate_path_delay(me_ptr, &me_ptr->out_port_info_arr[pri_arr_idx]);
   }

   avsync_ptr->curr_wall_clock_us = posal_timer_get_time();

   // If the first buffer has already been received on the input, continue
   if (avsync_ptr->flags.is_first_buf_rcvd)
   {
      return CAPI_EOK;
   }

   capi_err_t result = CAPI_EOK;

   avsync_ptr->flags.is_first_buf_rcvd = TRUE;
   avsync_ptr->av_reset_info.mode = me_ptr->reset_sess_time_info.mode;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Received first buffer");
#endif

   // if timestamp is not valid, set the base timestamp as zero
   //int64_t  base_ts            = input->flags.is_timestamp_valid ? input->timestamp : 0;
   uint32_t primary_output_idx = me_ptr->primary_output_arr_idx;

   // if this is the first sample, make sure that the primary output port has aggregated delay
   if (UMAX_32 == primary_output_idx)
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Warning! Process is called but primary output port is not set!");
   }
   else
   {
      spr_output_port_info_t *out_port_info_ptr = &me_ptr->out_port_info_arr[primary_output_idx];
      if (!out_port_info_ptr->path_delay.delay_us_pptr)
      {
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_ERROR_PRIO,
                 "avsync: Process is called but primary output port idx %d delay is not set! Check sequence",
                 primary_output_idx);
      }
   }

   if ((SPR_RENDER_MODE_DELAYED == avsync_ptr->client_config.render_mode) ||
       (SPR_RENDER_MODE_ABSOLUTE_TIME == avsync_ptr->client_config.render_mode))
   {
      //base_ts = 0;
   }

   spr_calculate_session_start_time(me_ptr, input);
   spr_calculate_init_session_time(me_ptr, input);

   return result;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_add_input_to_hold_list
    Add the input buffer to the hold buffer
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_add_input_to_hold_list(capi_spr_t *me_ptr, capi_stream_data_v2_t *input_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!input_ptr)
   {
      return result;
   }

   result = capi_spr_create_int_buf_node(me_ptr,
                                         &me_ptr->avsync_ptr->hold_buffer,
                                         input_ptr,
                                         (POSAL_HEAP_ID)me_ptr->heap_id,
                                         &me_ptr->operating_mf);

   if (CAPI_FAILED(result))
   {
      SPR_MSG_ISLAND(me_ptr->miid, DBG_ERROR_PRIO, "avsync: Failed to allocate hold buf node with result %d", result);
      return result;
   }

   return result;
}

/*---------------------------------------------------------------------------------------------------------
 Function : capi_spr_avsync_make_render_decision

   This function is used to make the render decision for the input buffer. Based on the
  render reference against which the input timestamp must be compared, this function determines
  if the sample can be played in time at the HW EP. The margin for tolerance is determined by the
  render window ranges set by the client.


  The following render reference modes are currently supported

  -- Render Reference Default
       The render decision is based on the default rate (session clock-based rendering; device driven).
       This is expected to be inherently driven by the timing of the HW EP. After the initial decision
       is made (at the first buffer after a start command), subsequent data rendering decisions are made
       with respect to the rate at which the device is rendering.

  -- Render Reference Wall Clock
       The render decision is based on the comparison of the incoming buffer timestamp with the wall
       clock value.

  FIRST SAMPLE RENDER
  Depending on the render run mode selected by the client, the first buffer is used to honor the
  start time configured by the client. As a result, the following comes into play :-

  -- Render Reference Default with Render Mode Immediate
       First buffer is always rendered & is used as a reference for the subsequent decisions

  -- Render Reference Default with Render Mode Absolute/Delayed
       Though the render reference is default, to honor the start time programmed by the client, the first
       buffer has to be compared against the wall clock (i.e Render Reference Wall Clock equation). After
       this the default render reference comparison is honored

  -- Render Reference Wall Clock with Render Mode Immediate
       When the first buffer is received, to ensure that it is always rendered (to honor run immediate),
       the start time for this session is derived based on back calculation. After this, if there is any
       jitter, the render decisions might result in holds/drops. This mode is not recommended.

  -- Render Reference Wall Clock with Render Mode Absolute/Delayed
       All the buffers including the first one are compared against the wall clock before being
       rendered. (Preferred choice for loopback/broadcast scenarios with Presentation Timestamps (PTS))
*------------------------------------------------------------------------------------------------------- */
capi_err_t capi_spr_avsync_make_render_decision(capi_spr_t *        me_ptr,
                                                capi_stream_data_t *input,
                                                render_decision_t * render_decision)
{
   if (NULL == me_ptr || NULL == render_decision || NULL == input)
   {
      // TODO : MSG error
      return CAPI_EFAILED;
   }

   // If client did not enable avsync, do not do anything. Simply return render
   if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      *render_decision = RENDER;
      return CAPI_EOK;
   }

   int64_t   input_timestamp = input->timestamp;
   bool_t    is_ts_valid     = input->flags.is_timestamp_valid;
   avsync_t *avsync_ptr      = me_ptr->avsync_ptr;

   // if timestamp is not valid or dfg is set, always render after first buffer
   if ((!is_ts_valid || avsync_ptr->flags.is_dfg) && (avsync_ptr->flags.is_first_buf_rendered))
   {
      *render_decision                = RENDER;
      avsync_ptr->abs_render_delta_us = 0;
      return CAPI_EOK;
   }

   // even if timestamps are not valid, always honor the start time by setting timestamp zero
   if (!is_ts_valid)
   {
      input_timestamp = 0;
   }

   // data with erasure flag set is considered for render decision only if it has metadata (see
   // spr_setup_input_to_process)
   // for this case, always return buffer as render so that metadata is handled.
   if (input->flags.erasure)
   {
      *render_decision                = RENDER;
      avsync_ptr->abs_render_delta_us = 0;
      return CAPI_EOK;
   }

   int64_t ref_session_time_us = 0, start_time_us = 0, path_delay_us = 0, render_delta_us = 0;

   uint32_t render_reference = avsync_ptr->client_config.render_reference;
   uint32_t render_mode      = avsync_ptr->client_config.render_mode;

   ref_session_time_us = get_reference_session_time(me_ptr);

   // If default render reference was selected with render modes absolute/delayed, the first buffer decision
   // has to be based on the wall clock. After this, the configured render reference is selected.
   if ((!avsync_ptr->flags.is_first_buf_rendered) && (SPR_RENDER_REFERENCE_DEFAULT == render_reference) &&
       ((SPR_RENDER_MODE_ABSOLUTE_TIME == render_mode) || (SPR_RENDER_MODE_DELAYED == render_mode)))
   {
      render_reference    = SPR_RENDER_REFERENCE_WALL_CLOCK;
      ref_session_time_us = posal_timer_get_time();
   }

   start_time_us = avsync_ptr->calc_start_time_us;
   path_delay_us = avsync_ptr->ds_delay_us;

   if (SPR_RENDER_REFERENCE_WALL_CLOCK == render_reference)
   {
      render_delta_us = ref_session_time_us + path_delay_us - input_timestamp - start_time_us;
   }
   else // gate based on expected session clock value. do not care about path delay
   {
      render_delta_us = ref_session_time_us - input_timestamp;
   }

   //#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: make_render_decision: Session time (%ld), Input timestamp(%ld), Start time(%ld), "
           "Path Delay (%lu), Render Delta (%ld)",
           ref_session_time_us,
           input_timestamp,
           start_time_us,
           (uint32_t)path_delay_us,
           render_delta_us);

   //#endif

   avsync_ptr->abs_render_delta_us = (render_delta_us < 0) ? -render_delta_us : render_delta_us;

   // Gate the render_time w.r.t the render windows & take a decision
   if (render_delta_us <= avsync_ptr->client_config.render_window_start_us)
   {
      *render_decision = HOLD;
   }
   else if (render_delta_us >= avsync_ptr->client_config.render_window_end_us)
   {
      *render_decision = DROP;

      // First buffer has ts invalid, then consider as RENDER
      // This means that the start time was in the past.
      if (!is_ts_valid)
      {
         *render_decision = RENDER;
      }
   }
   else
   {
      *render_decision = RENDER;

      capi_spr_avsync_check_for_hold_zeroes(me_ptr, (capi_stream_data_v2_t *)input, render_decision, render_delta_us);
   }

   if (RENDER == *render_decision && !avsync_ptr->flags.is_first_buf_rendered)
   {
      avsync_ptr->flags.is_first_buf_rendered = TRUE;
   }

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid, DBG_HIGH_PRIO, "avsync: Render decision %lu (render1,hold2,drop3)", *render_decision);
#endif

   return CAPI_EOK;
}

/*------------------------------------------------------------------------------
  Function name: get_reference_session_time
    Returns the reference session time for the render decision
* ------------------------------------------------------------------------------*/
static int64_t get_reference_session_time(capi_spr_t *me_ptr)
{
   if (NULL == me_ptr)
   {
      // TODO : MSG error
      return 0;
   }

   int64_t session_time = me_ptr->avsync_ptr->expected_session_clock_us;

   // If render reference is wall clock, snap shot the current timer tick
   if (SPR_RENDER_REFERENCE_WALL_CLOCK == me_ptr->avsync_ptr->client_config.render_reference)
   {
      session_time = me_ptr->avsync_ptr->curr_wall_clock_us;
   }

   return session_time;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_check_for_hold_zeroes
   Checks if hold zeroes have to be filled for this render or not
* ------------------------------------------------------------------------------*/
static void capi_spr_avsync_check_for_hold_zeroes(capi_spr_t *           me_ptr,
                                                  capi_stream_data_v2_t *input,
                                                  render_decision_t *    render_decision,
                                                  int64_t                render_delta_us)
{
   avsync_t *avsync_ptr = me_ptr->avsync_ptr;

   // If the buffer is from the hold list, then zeroes have to be accounted for depending on the delta value
   // Or for the first buffer being rendered, make sure that start time aligns exactly
   if (((spr_avsync_is_input_strm_hold_buf_head(avsync_ptr, (capi_stream_data_v2_t *)input) &&
         (HOLD == avsync_ptr->prev_render_decision)) ||
        (FALSE == avsync_ptr->flags.is_first_buf_rendered)) &&
       (render_delta_us < 0))
   {

      // Compare the render_delta_us with the frame duration of SPR. If render_delta is within the frame duration
      // of SPR, then some zeroes have to be inserted.
      // Consider render windows +/-5000 us for frame duration 1000 us (i.e. low latency)
      // If render_delta_us is say -4999, then treat the input as held till the render_delta_us is
      //   within the frame duration boundary. So zeroes will be inserted for 4000 us & then 999us of
      //   zeroes are rendered
      // TODO: Revisit after migration to generic circular buffer utils

      avsync_ptr->hold_zeroes_per_ch = me_ptr->frame_dur_bytes_per_ch;

      if (-render_delta_us < me_ptr->frame_dur_us)
      {
         // Note the negative sign attached to render_delta_us
         uint32_t calc_zeroes_per_ch = capi_cmn_us_to_bytes(-render_delta_us,
                                                            me_ptr->operating_mf.format.sampling_rate,
                                                            me_ptr->operating_mf.format.bits_per_sample,
                                                            1);

         if (calc_zeroes_per_ch < me_ptr->frame_dur_bytes_per_ch)
         {
            avsync_ptr->hold_zeroes_per_ch = calc_zeroes_per_ch;
         }
         else
         {
            *render_decision = HOLD;
#ifdef AVSYNC_DEBUG
            SPR_MSG_ISLAND(me_ptr->miid,
                    DBG_HIGH_PRIO,
                    "avsync: hold_zeroes_per_ch %ld greater than frame_dur_bytes_per_ch %d. Holding the buffer",
                    calc_zeroes_per_ch,
                    me_ptr->frame_dur_bytes_per_ch);
#endif
         }
      }
      else
      {
         *render_decision = HOLD;
#ifdef AVSYNC_DEBUG
         SPR_MSG_ISLAND(me_ptr->miid,
                 DBG_HIGH_PRIO,
                 "avsync: render_delta_us %ld greater than frame_dur_us %d. Holding the buffer",
                 render_delta_us,
                 me_ptr->frame_dur_us);
#endif
      }
   } // if input stream is head of hold buffer
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_update_input_info
  Updates the avsync instance with the information regarding the input stream
  processed by the module
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_update_input_info(capi_spr_t *me_ptr, capi_stream_data_t *input)
{
   capi_err_t result = CAPI_EOK;

   if (!input)
   {
      return CAPI_EFAILED;
   }

   if (!is_spr_avsync_enabled(me_ptr->avsync_ptr))
   {
      return CAPI_EOK;
   }

   avsync_t *avsync_ptr = me_ptr->avsync_ptr;

   // update the latest processed timestamp always
   // TODO: if there is variable delay across outputs, this needs to be absorbed into the circular buffer & updated
   // from the output context
   avsync_ptr->proc_timestamp_us = input->timestamp;
   avsync_ptr->flags.is_ts_valid = input->flags.is_timestamp_valid;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_LOW_PRIO,
           "avsync: Set proc timestamp to %ld is_ts_valid %d",
           input->timestamp,
           input->flags.is_timestamp_valid);
#endif

   return result;
}

capi_err_t capi_spr_avsync_update_absolute_time(avsync_t *avsync_ptr, int64_t adj_us)
{
   capi_err_t result = CAPI_EOK;

   if (NULL == avsync_ptr)
   {
#ifdef AVSYNC_DEBUG
      AR_MSG_ISLAND(DBG_ERROR_PRIO, "capi_spr: avsync: Received invalid params for update absolute time ");
#endif
      return CAPI_EBADPARAM;
   }

   // Use the current timer tick and incorporate the adjustment provided by the caller.
   // The adjustment is typically a measure of the downstream delay from this point onwards.
   avsync_ptr->absolute_time_us = avsync_ptr->curr_wall_clock_us;
   avsync_ptr->absolute_time_us += adj_us;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(avsync_ptr->miid,
           DBG_LOW_PRIO,
           "avsync: update absolute time %ld adjustment %ld",
           avsync_ptr->absolute_time_us,
           adj_us);
#endif

   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_avsync_set_ds_delay
    Sets the path delay value after aggregation for render decision
* ------------------------------------------------------------------------------*/
void spr_avsync_set_ds_delay(avsync_t *avsync_ptr, uint32_t delay_us)
{
   if (!avsync_ptr)
   {
      return;
   }

   if (delay_us != avsync_ptr->ds_delay_us)
   {
      SPR_MSG_ISLAND(avsync_ptr->miid,
              DBG_HIGH_PRIO,
              "PATH_DELAY: aggregated delay for primary out port changed from %lu us to %lu us",
              avsync_ptr->ds_delay_us,
              delay_us);
   }

   avsync_ptr->ds_delay_us = delay_us;
}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_check_fill_hold_zeroes
   Fills the hold zeroes in the output stream
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_check_fill_hold_zeroes(capi_spr_t *           me_ptr,
                                                  capi_stream_data_v2_t *out_strm_ptr,
                                                  int64_t *              timestamp_ptr)
{
   capi_err_t result     = CAPI_EOK;
   avsync_t * avsync_ptr = me_ptr->avsync_ptr;

   if (!is_spr_avsync_enabled(avsync_ptr))
   {
      return result;
   }

   if (0 == avsync_ptr->hold_zeroes_per_ch)
   {
      return result;
   }

   if (!(out_strm_ptr && out_strm_ptr->buf_ptr))
   {
      return result;
   }

   // Assumes that at least frame_dur_bytes_per_ch space is present in the output
   uint32_t space_in_output = (out_strm_ptr->buf_ptr->max_data_len - out_strm_ptr->buf_ptr->actual_data_len);
   uint32_t num_zeroes_per_ch =
      (avsync_ptr->hold_zeroes_per_ch > space_in_output) ? space_in_output : avsync_ptr->hold_zeroes_per_ch;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(me_ptr->miid,
           DBG_LOW_PRIO,
           "avsync: inserting num_hold_zeroes_per ch = %d calc_hold_zeroes_per_ch = %d, space in output %d",
           num_zeroes_per_ch,
           avsync_ptr->hold_zeroes_per_ch,
           space_in_output);
#endif

   for (uint32_t j = 0; j < me_ptr->operating_mf.format.num_channels; j++)
   {
      int8_t *update_ptr = out_strm_ptr->buf_ptr[j].data_ptr + out_strm_ptr->buf_ptr[j].actual_data_len;
      memset((void *)update_ptr, 0, num_zeroes_per_ch);

      out_strm_ptr->buf_ptr[j].actual_data_len += num_zeroes_per_ch;
   }

   // TODO: Revisit after moving to generic circular buffer
   // If there is a hold, each underrun buffer should have a TS of incoming TS - hold duration.
   // For eg. incoming timestamp is X and hold duration is 5ms and frame duration of SPR is 1ms
   // So buffer timestamps from SPR should be X-5, X-4, X-3, X-2, X-1 during the hold period.
   *timestamp_ptr -= avsync_ptr->abs_render_delta_us;

   return result;
}

/*------------------------------------------------------------------------------
  Function name: spr_avsync_apply_scale_factor
   Applies scale factor to the session clk samples based on MODULE_CMN_MD_ID_SCALE_SESSION_TIME.
   The caller handles the case where the scale factor has to be applied from the middle of an
   input buffer
* ------------------------------------------------------------------------------*/
static void spr_avsync_apply_scale_factor(avsync_t *avsync_ptr, uint64_t in_samples, uint32_t tsm_sf, uint32_t spr_sr)
{
	uint64_t temp_sf_samples = 0;
	uint64_t rem_sf = 0;

	uint64_t scaled_in_samples = (in_samples * tsm_sf);

	temp_sf_samples = (scaled_in_samples)/(TSM_UNITY_SPEED_FACTOR);
	rem_sf = (scaled_in_samples) - (temp_sf_samples * TSM_UNITY_SPEED_FACTOR);

//#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(avsync_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: tsm: in_samples %d, temp_sf_samples %d speed_factor %d remainder_sf %d prev_remainder_samples %d",
           in_samples,
           temp_sf_samples,
           tsm_sf,
           rem_sf,
           avsync_ptr->tsm_info.remainder_tsm_samples);
   //#endif

   // Round to unity in Q24 and add to samples rendered calculation.
   avsync_ptr->tsm_info.remainder_tsm_samples += rem_sf;
   if (avsync_ptr->tsm_info.remainder_tsm_samples >= TSM_UNITY_SPEED_FACTOR)
   {
      avsync_ptr->tsm_info.remainder_tsm_samples -= TSM_UNITY_SPEED_FACTOR;
      rem_sf = 1;
    }
    else
    {
      rem_sf = 0;
    }

   // TODO: Discuss and conclude if only one variable is enough to simplify
	avsync_ptr->tsm_info.tsm_session_clk_samples += temp_sf_samples + rem_sf;
   avsync_ptr->elapsed_session_clock_samples += temp_sf_samples + rem_sf;

//#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(avsync_ptr->miid, DBG_HIGH_PRIO,
         "avsync: tsm: tsm_session_clk_samples [%lu,%lu] elapsed_session_clock_samples [%lu,%lu] rem_sf %d temp_sf_samples %d",
         (uint32_t)(avsync_ptr->tsm_info.tsm_session_clk_samples >> 32),
         (uint32_t)(avsync_ptr->tsm_info.tsm_session_clk_samples),
         (uint32_t)(avsync_ptr->elapsed_session_clock_samples >> 32),
         (uint32_t)(avsync_ptr->elapsed_session_clock_samples),rem_sf, temp_sf_samples);
//#endif

}

/*------------------------------------------------------------------------------
  Function name: capi_spr_avsync_update_expected_session_time
  Update the expected session time after the output is generated
* ------------------------------------------------------------------------------*/
capi_err_t capi_spr_avsync_update_expected_session_time(avsync_t *avsync_ptr, int64_t adj_bytes, uint32_t sr, uint32_t bps)
{

   if (!is_spr_avsync_enabled(avsync_ptr))
   {
      return CAPI_EOK;
   }

   if (!avsync_ptr->flags.is_first_buf_rendered)
   {
#ifdef AVSYNC_DEBUG
      SPR_MSG_ISLAND(avsync_ptr->miid, DBG_HIGH_PRIO, "avsync: First buffer not yet rendered. Skip update");
      return CAPI_EOK;
#endif
   }
   int64_t expected_session_clock_us         = 0;
   //int64_t elapsed_exp_session_clock_samples = avsync_ptr->elapsed_expected_session_clock_samples;

   // Convert adj_bytes to samples and increment the elapsed session clock in samples.
   int64_t adj_samples = adj_bytes / CAPI_CMN_BITS_TO_BYTES(bps);
   avsync_ptr->elapsed_expected_session_clock_samples += adj_samples;

   // This is from the output side. Input should account for the actual number of samples in the
   // internal buffer in case in size != out size
   if (SPR_RENDER_REFERENCE_DEFAULT == avsync_ptr->client_config.render_reference)
   {
      // For default render reference, calculate expected session clock directly based on elapsed session clock in samples.
      expected_session_clock_us =
         spr_convert_samples_to_us(avsync_ptr->elapsed_expected_session_clock_samples, sr, NULL /*fract_time_ptr*/) +
         avsync_ptr->base_timestamp_us;
   }
   else
   {
      // For wall clock reference, expected_session_clock = input buffer TS + samples rendered this iteration
      int64_t update_us = spr_convert_samples_to_us(adj_samples, sr, NULL /*fract_time_ptr*/);
      expected_session_clock_us = avsync_ptr->proc_timestamp_us + update_us;
   }
#ifdef AVSYNC_DEBUG
   int64_t prev_expected_session_clock_us = avsync_ptr->expected_session_clock_us;
#endif

   avsync_ptr->expected_session_clock_us = expected_session_clock_us;

#ifdef AVSYNC_DEBUG
   SPR_MSG_ISLAND(avsync_ptr->miid,
           DBG_HIGH_PRIO,
           "avsync: Expected session clock updated to %ld prev value %ld",
           avsync_ptr->expected_session_clock_us,
           prev_expected_session_clock_us);
#endif
   return CAPI_EOK;
}
