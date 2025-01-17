/**
 * \file icb.c
 *
 * \brief
 *
 *     inter-container-buffering
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "icb.h"

#define ICB_MSG_PREFIX "ICB :%08X: "
#define ICB_MSG(ID, xx_ss_mask, xx_fmt, ...) AR_MSG(xx_ss_mask, ICB_MSG_PREFIX xx_fmt, ID, ##__VA_ARGS__)

/** x must be larger than y & both must be fixed-point */
static inline bool_t icb_is_multiple(uint32_t x, uint32_t y)
{
   if ((0 != y) && (0 != x))
   {
      return ((((x) / (y)) * (y)) == (x));
   }
   return FALSE;
}

ar_result_t icb_determine_buffering(icb_upstream_info_t *  us_ptr,
                                    icb_downstream_info_t *ds_ptr,
                                    icb_calc_output_t *    result_ptr)
{
   ar_result_t result = AR_EOK;

   uint32_t us_frame_len       = 0;
   uint32_t ds_frame_len       = 0;
   uint32_t us_frame_len_us    = 0;
   uint32_t ds_frame_len_us    = 0;
   bool_t   is_unit_samples    = FALSE;
   bool_t   is_multiple        = FALSE;
   bool_t   disable_double_buf = FALSE, disable_otp = FALSE, disable_reg_prebuf = FALSE;
   bool_t   is_us_voice_sid   = IS_VOICE_SCENARIO_ID(us_ptr->sid);
   bool_t   is_ds_voice_sid   = IS_VOICE_SCENARIO_ID(ds_ptr->sid);


   /* If period is provided for downstream, then use period as frame length for downstream
    * else if both sample rates are provided, then use in samples, else convert the other to us. */
   if (ds_ptr->period_us)
   {
      ds_frame_len_us = ds_ptr->period_us;
      if (us_ptr->len.sample_rate)
      {
         us_frame_len_us = icb_samples_to_us(us_ptr->len.frame_len_samples, us_ptr->len.sample_rate);
      }
      else
      {
         us_frame_len_us = us_ptr->len.frame_len_us;
      }
   }
   else if (us_ptr->len.sample_rate && ds_ptr->len.sample_rate)
   {
      if (us_ptr->len.sample_rate == ds_ptr->len.sample_rate)
      {
         us_frame_len    = us_ptr->len.frame_len_samples;
         ds_frame_len    = ds_ptr->len.frame_len_samples;
         is_unit_samples = TRUE;
         us_frame_len_us = icb_samples_to_us(us_ptr->len.frame_len_samples, us_ptr->len.sample_rate);
         ds_frame_len_us = icb_samples_to_us(ds_ptr->len.frame_len_samples, ds_ptr->len.sample_rate);
      }
      else
      {
         us_frame_len_us = icb_samples_to_us(us_ptr->len.frame_len_samples, us_ptr->len.sample_rate);
         ds_frame_len_us = icb_samples_to_us(ds_ptr->len.frame_len_samples, ds_ptr->len.sample_rate);
      }
   }
   else if (us_ptr->len.sample_rate)
   {
      us_frame_len_us = icb_samples_to_us(us_ptr->len.frame_len_samples, us_ptr->len.sample_rate);
      ds_frame_len_us = ds_ptr->len.frame_len_us;
   }
   else if (ds_ptr->len.sample_rate)
   {
      us_frame_len_us = us_ptr->len.frame_len_us;
      ds_frame_len_us = icb_samples_to_us(ds_ptr->len.frame_len_samples, ds_ptr->len.sample_rate);
   }
   else
   {
      us_frame_len_us = us_ptr->len.frame_len_us;
      ds_frame_len_us = ds_ptr->len.frame_len_us;
   }

   if (!is_unit_samples)
   {
      us_frame_len = us_frame_len_us;
      ds_frame_len = ds_frame_len_us;
   }

   if (is_us_voice_sid)
   {
      if (is_ds_voice_sid)
      {
         // We don't need extra buffering between voice containers.
         disable_double_buf = TRUE;
      }
      else if (!ds_ptr->flags.is_real_time)
      {
         // If downstream is non-real time then we can assume that it will return the buffer immediately to the upstream
         // so double buffer are not needed.
         // this case is to handle VpTx <-> TRM
         disable_double_buf = TRUE;
      }
      else
      {
         // If downstream is real time then we need double buffers.
         disable_double_buf = FALSE;
      }
   }

   /**
    * disable double buffer: for voice, OR when DS indicates and US frame size <= DS.
    *    If the upstream of satellite EP container has higher frame-size, we would need double buffering
    *    but if the upstream frame size is small compared to downstream, we don't need double buffering in satellite
    */
   disable_double_buf = ((disable_double_buf) || us_ptr->flags.is_default_single_buffering_mode ||
                         ((us_frame_len_us <= ds_frame_len_us) && (ds_ptr->flags.is_default_single_buffering_mode)));

   disable_otp = us_ptr->disable_otp || (is_us_voice_sid) || (is_ds_voice_sid);

   disable_reg_prebuf = is_us_voice_sid;

   if (us_frame_len_us >= ds_frame_len_us)
   {
      /** If ping pong mode is needed two regular buffers of upstream len, else one regular buffer of upstream len */
      if (!disable_double_buf)
      {
         result_ptr->num_reg_bufs = 2;
      }
      else
      {
         result_ptr->num_reg_bufs = 1;
      }
   }
   else
   {
      is_multiple = icb_is_multiple(ds_frame_len, us_frame_len);
      if (is_multiple)
      {
         result_ptr->num_reg_bufs = (ds_frame_len / us_frame_len);
         /** ds/us regular buffers of upstream len */
      }
      else
      {
         result_ptr->num_reg_bufs = (ds_frame_len / us_frame_len) + 1;
         /** (ds/us+1) regular buffers of upstream len */
      }
   }

   /* Need to allocate sufficient buffers to process at least one upstream period len samples.
    *
    * Upstream: VPRx-Device period is 40ms, frame len 20ms
    *	Downstream: GEN_CNTR, period is 1 ms
    *	We need at least two buffers between VPRx and GEN_CNTR
    */
   if (us_ptr->period_us > (us_frame_len_us * result_ptr->num_reg_bufs))
   {
      is_multiple = icb_is_multiple(us_ptr->period_us, us_frame_len_us);
      if (is_multiple)
      {
         result_ptr->num_reg_bufs = (us_ptr->period_us / us_frame_len_us);
      }
      else
      {
         result_ptr->num_reg_bufs = (us_ptr->period_us / us_frame_len_us) + 1;
      }
   }

   if (!disable_otp)
   {
      /* If jitter buffer is required for real time use case, one time pre buffer of upstream frame length size
       * this helps mask any processing jitter */
      if ((us_ptr->flags.is_real_time) && (ds_ptr->flags.is_real_time))
      {
         result_ptr->num_reg_prebufs = 1;
      }
   }

   if (!disable_reg_prebuf)
   {
      /* If output of upstream is variable or input of downstream is variable,
       * 1 regular pre-buffer of upstream length to account for sample slipping
       * If the ds_frame_len is not a multiple of us_frame_len, add a prebuffer.
       * The pre-buffer is needed only if downstream is realtime*/
      if ((ds_ptr->flags.is_real_time) &&
          ((us_ptr->flags.variable_output) || (ds_ptr->flags.variable_input) ||
           ((ds_frame_len > us_frame_len) && (!icb_is_multiple(ds_frame_len, us_frame_len)))))
      {
         result_ptr->num_reg_prebufs = 1;
      }
   }

   if (!disable_double_buf)
   {
      /**
       * e.g. RAT->RAT, or Slimbus->Dec->RAT. Extra empty buffer to cover for any jitter.
       */
      if (us_ptr->flags.is_real_time && ds_ptr->flags.is_real_time && (us_frame_len_us < ds_frame_len_us) &&
          (!is_us_voice_sid))
      {
         result_ptr->num_reg_bufs = result_ptr->num_reg_bufs + 1;
      }

      /*Post-buffer is required. i.e. one empty buffer is required for the sample stuffing cases.
       *Consider downstream is fast and sample stuffing happens. Assume 48k, 1 ms = 48 samples. While stuffing 1 sample
       *is stuffed, 47 samples are consumed from input. This means the buffer is held between upstream and downstream.
       *A real-time upstream will overrun at this point. Having an extra output buffer helps prevent overrun.
       */
      /** If (ds_frame_len <= us_frame_len) then we are already allocating one extra buffer, so no need for one more
       * additional buffer.
       */
      /* Since this buffer is created in real time path and it is a regular buffer therefore it will not account in
       * extra delay.
       */
#if 0 // above jitter buffer will cover the post-buffer requirement
      if (us_ptr->flags.is_real_time && ds_ptr->flags.is_real_time &&
          ((us_ptr->flags.variable_output) || (ds_ptr->flags.variable_input)) && (ds_frame_len > us_frame_len))
      {
         result_ptr->num_reg_bufs++;
      }
#endif
   }

   ICB_MSG(us_ptr->log_id,
           DBG_HIGH_PRIO,
           "ICB: US ( frame len = %lu samples, sample rate = %lu kHz, frame len = %lu us, period = %lu us)",
           us_ptr->len.frame_len_samples,
           us_ptr->len.sample_rate,
           us_ptr->len.frame_len_us,
           us_ptr->period_us);

   ICB_MSG(us_ptr->log_id,
           DBG_HIGH_PRIO,
           "ICB: DS ( frame len = %lu samples, sample rate = %lu kHz, frame len = %lu us, period = %lu us)",
           ds_ptr->len.frame_len_samples,
           ds_ptr->len.sample_rate,
           ds_ptr->len.frame_len_us,
           ds_ptr->period_us);

   ICB_MSG(us_ptr->log_id,
           DBG_HIGH_PRIO,
           "ICB: (is_real_time, variable size) US (%u, %u) DS (%u, %u). US SID %lu",
           us_ptr->flags.is_real_time,
           us_ptr->flags.variable_output,
           ds_ptr->flags.is_real_time,
           ds_ptr->flags.variable_input,
           us_ptr->sid);

   ICB_MSG(us_ptr->log_id,
           DBG_HIGH_PRIO,
           "ICB: regular bufs %lu, regular prebuffers %lu, OTP size %lu us",
           result_ptr->num_reg_bufs,
           result_ptr->num_reg_prebufs,
           result_ptr->otp.frame_len_us);

   return result;
}
