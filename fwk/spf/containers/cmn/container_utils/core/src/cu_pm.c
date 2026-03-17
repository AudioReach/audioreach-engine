/**
 * \file cu_pm.c
 * \brief
 *     This file contains container common power manager functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"

#define CU_PCPPx2_DEFAULT 7 /* PCPP value of 3.5, multiplied by 2 */

/* =======================================================================
Public Functions
========================================================================== */

ar_result_t cu_handle_island_vote(cu_base_t *me_ptr, posal_pm_island_vote_t island_vote)
{
   ar_result_t result = AR_EOK;

   if (FALSE == cu_is_island_container(me_ptr))
   {
      return AR_EOK;
   }

   if (!posal_power_mgr_is_registered(me_ptr->pm_info.pm_handle_ptr))
   {
      return AR_ENOTREADY;
   }

   /*
    * With blocking call, PM SERVER may not respond immediately, since
    * it may servicing another blocking call request, due to which
    * responding to current request may gets delayed. Due to this wait,
    * signal misses are possible. So, making island votes as non-blocking,
    * since, this request will wakeup PM SERVER which is non-island thread,
    * until PM SERVER goes to wait (even PM SERVER gets pre-empted, HW
    * threads won't be idle due to thread which pre-empted PM SERVER is active),
    * so, device won't enters island. So, no need to wait for the response.
    * */

   if (PM_ISLAND_VOTE_DONT_CARE == island_vote.island_vote_type) // Votes for don't care, we are releasing island.
   {
      posal_pm_release_info_t release;
      memset(&release, 0, sizeof(posal_pm_release_info_t));

      release.pm_handle_ptr   = me_ptr->pm_info.pm_handle_ptr;
      release.client_log_id   = me_ptr->gu_ptr->log_id;
      release.wait_signal_ptr = NULL;
      // Populate island vote
      release.resources.island_vote.is_valid         = TRUE;
      release.resources.island_vote.island_vote_type = PM_ISLAND_VOTE_DONT_CARE;
      result                                         = posal_power_mgr_release(&release);
   }
   else // Else votes for either exit island (restrict entry) or entry island (allow entry), we are requesting a
        // particular state.
   {
      posal_pm_request_info_t request;
      memset(&request, 0, sizeof(posal_pm_request_info_t));

      request.pm_handle_ptr   = me_ptr->pm_info.pm_handle_ptr;
      request.client_log_id   = me_ptr->gu_ptr->log_id;
      request.wait_signal_ptr = NULL;
      // Populate island vote
      request.resources.island_vote.is_valid         = TRUE;
      request.resources.island_vote.island_vote_type = island_vote.island_vote_type;
      request.resources.island_vote.island_type      = island_vote.island_type;
      result                                         = posal_power_mgr_request(&request);
   }

   if (AR_EOK == result)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Success: Island Vote = %u : 0 -> ISLAND_VOTE_ENTRY, 1 -> ISLAND_VOTE_EXIT, "
             "2 -> ISLAND_VOTE_DONT_CARE",
             island_vote.island_vote_type);

      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "Success: Island Type = %u : 0 -> PM_ISLAND_TYPE_DEFAULT, 1 -> PM_ISLAND_TYPE_LOW_POWER_2",
             island_vote.island_type);
   }
   else
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Failed: Island Vote = %u : 0 -> ISLAND_VOTE_ENTRY, 1 -> ISLAND_VOTE_EXIT, 2 -> ISLAND_VOTE_DONT_CARE",
             island_vote.island_vote_type);
   }

   return result;
}

ar_result_t cu_vote_latency(cu_base_t *me_ptr, bool_t is_release, bool_t is_realtime_usecase)
{
   uint32_t    new_vote = 0;
   ar_result_t result   = AR_EOK;

   // if container is an island container, sleep latency voting is driven by the end point module,
   // container should not vote.
   if (cu_is_island_container(me_ptr))
   {
      return AR_EOK;
   }

   if ((me_ptr->voice_info_ptr) && (FALSE == me_ptr->voice_info_ptr->is_satellite_voice_sid))
   {
      return AR_EOK;
   }

   // also release votes if container proc duration is not yet known.
   is_release = is_release || (0 == me_ptr->cntr_proc_duration);

   bool_t previously_released =
      ((0 == me_ptr->pm_info.prev_latency_vote) || (LATENCY_VOTE_MAX == me_ptr->pm_info.prev_latency_vote));

   // if we didn't vote even once, and this call is for release, then return
   if (is_release && previously_released)
   {
      return AR_EOK;
   }

   if (!posal_power_mgr_is_registered(me_ptr->pm_info.pm_handle_ptr))
   {
      return AR_EOK;
   }

   // If no sub graphs are started, is_release will be false -> we need to release, voting value doesn't matter
   if (!is_release)
   {
      // The latency vote of a container is computed as a function of its frame length & nature of usecase
      // For RT/NRT use cases, the tolerance factor for latency is 3%/70% respectively
      // Note : This assumes that frame length is the processing time of the container
      uint32_t tolerance_factor = (is_realtime_usecase) ? LATENCY_VOTE_RT_FACTOR : LATENCY_VOTE_NRT_FACTOR;

      // Example: For 1 ms frame_len_us latency votes for RT/NRT are 30us and 700us respectively
      new_vote = (tolerance_factor * me_ptr->cntr_proc_duration) / 100;

      // for sub millisecond frame durations (like 83us) the sleep latency tolerance is very low, hence voting 10us if
      // frame duration is less than 500us to prevent DSP entry into any sleep mode.
      if (me_ptr->cntr_proc_duration <= 500)
      {
         new_vote = LATENCY_VOTE_MIN; // voting 10us to prevent entering all sleep modes.
      }
      else if (new_vote < LATENCY_VOTE_LOW)  // floor min latency to 40us if proc duration is > 500us
      {
         new_vote = LATENCY_VOTE_LOW; // Hack to make latency vote 40us
      }
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "New latency vote calculated= %u actual= %lu",
             ((tolerance_factor * me_ptr->cntr_proc_duration) / 100),
             new_vote);
   }
   else // release
   {
      new_vote = LATENCY_VOTE_MAX;
   }

   if (me_ptr->pm_info.prev_latency_vote != new_vote)
   {
      if (is_release)
      {
         posal_pm_release_info_t release;
         memset(&release, 0, sizeof(posal_pm_release_info_t));

         release.pm_handle_ptr                    = me_ptr->pm_info.pm_handle_ptr;
         release.client_log_id                    = me_ptr->gu_ptr->log_id;
         release.wait_signal_ptr                  = NULL;
         release.resources.sleep_latency.is_valid = TRUE;
         release.resources.sleep_latency.value    = new_vote;
         result                                   = posal_power_mgr_release(&release);
      }
      else
      {
         posal_pm_request_info_t request;
         memset(&request, 0, sizeof(posal_pm_request_info_t));

         request.pm_handle_ptr                    = me_ptr->pm_info.pm_handle_ptr;
         request.client_log_id                    = me_ptr->gu_ptr->log_id;
         request.wait_signal_ptr                  = (me_ptr->flags.apm_cmd_context) ? NULL : me_ptr->gp_signal_ptr;
         request.resources.sleep_latency.is_valid = TRUE;
         request.resources.sleep_latency.value    = new_vote;
         result                                   = posal_power_mgr_request(&request);
      }

      if (AR_EOK == result)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_LOW_PRIO,
                SPF_LOG_PREFIX "Success: Latency Vote, is_release = %d, new_vote = %d, previous vote = %d",
                is_release,
                new_vote,
                me_ptr->pm_info.prev_latency_vote);
         me_ptr->pm_info.prev_latency_vote = new_vote;
      }
      else
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed: Latency Vote, is_release = %d, new_vote = %d, previous vote = %d",
                is_release,
                new_vote,
                me_ptr->pm_info.prev_latency_vote);
      }
   }
   return result;
}

/**
 * also updates container prod duration
 */
ar_result_t cu_handle_clk_vote_change(cu_base_t *       me_ptr,
                                      cu_pm_vote_type_t vote_type,
                                      bool_t            force_vote,
                                      uint32_t          total_kpps,
                                      uint32_t          total_bw,
                                      uint32_t          scaled_kpps_agg_q4,
                                      bool_t            nonblocking)
{
   ar_result_t result = AR_EOK;

   bool_t is_voice_scenario = (NULL != me_ptr->voice_info_ptr);
   bool_t is_release = (CU_PM_REQ_KPPS_BW != vote_type);

   /* Multiply the scale factor and pCPP to the current KPPS value and vote with new floor clock.
   This scale factor is set when an event is raised by the encoder if it needs to process data
   faster to catch up the real time [refer: FWK_EXTN_BT_CODEC].
   Floor clock  = old_kpps * scale_factor * (CU_PCPPx2_DEFAULT)/2
   Considering PCPP as 3.5, CU_PCPPx2_DEFAULT is 7 for the ease of calculations. */

   /* Scale kpps with integral part, floor clock in hz*/
   uint64_t floor_clock = 0;

   // if scaled kpps is same as topo kpps then all scale factors are unity; no need of voting floor clock.
   if (scaled_kpps_agg_q4 != (total_kpps << 4))
   {
      /*shift right by 4 to convert to q4 format */
      floor_clock = ((uint64_t)scaled_kpps_agg_q4 * CU_PCPPx2_DEFAULT * 1000);

      /* Need to rotate 4 + 1 times to divide by 32, 4 rotations to account for 4 fractional bits in scale
       * factor and one rotation to divide CU_PCPPx2_DEFAULT by 2 to get the actual PCPP. */
      floor_clock = (floor_clock >> 5);

      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "scaled_kpps_agg %lu, agg kpps %lu, floor_clock in hz lsw: %lu msw: %lu",
             (scaled_kpps_agg_q4 >> 4),
             total_kpps,
             (uint32_t)floor_clock,
             (uint32_t)(floor_clock >> 32));
   }

   // If there was no event or no release-call, or no force vote or there was
   // no change, return.
   if (!((me_ptr->pm_info.prev_kpps_vote != total_kpps) || (me_ptr->pm_info.prev_bw_vote != total_bw) ||
         (me_ptr->pm_info.prev_floor_clock_vote != floor_clock) || force_vote))
   {
      return AR_EOK;
   }

   // if we didn't vote even once, and this call is for release, then return
   if (is_release && ((0 == me_ptr->pm_info.prev_kpps_vote) && (0 == me_ptr->pm_info.prev_bw_vote) &&
                      (0 == me_ptr->pm_info.prev_floor_clock_vote)))
   {
      return AR_EOK;
   }

   if (!posal_power_mgr_is_registered(me_ptr->pm_info.pm_handle_ptr))
   {
      return AR_EOK;
   }

   if (me_ptr->flags.apm_cmd_context)
   {
      // APM already has max vote in place and release will be slowed.
      nonblocking = TRUE;
   }

   /*
    * nonblocking -> nonblocking means request is submitted to PM server and container doesn't wait for response.
    * all releases are always nonblocking.
    * some requests like during close/stop (resulting in decrease in votes) can also be done in non-blocking way.
    * All votes must be decreasing or equal
    * */
   if (!nonblocking && !force_vote && (total_kpps <= me_ptr->pm_info.prev_kpps_vote) &&
       (total_bw <= me_ptr->pm_info.prev_bw_vote) && (floor_clock <= me_ptr->pm_info.prev_floor_clock_vote))
   {
      nonblocking = TRUE;
   }

   // whether mips req is made
   bool_t is_mips_req = ((me_ptr->pm_info.prev_kpps_vote != total_kpps) || is_release ||
                         (me_ptr->pm_info.prev_floor_clock_vote != floor_clock));
   // whether bw req is made
   bool_t is_bw_req = ((me_ptr->pm_info.prev_bw_vote != total_bw) || (CU_PM_REL_KPPS_BW == vote_type));

   if (is_mips_req)
   {
      me_ptr->pm_info.prev_kpps_vote        = total_kpps;
      me_ptr->pm_info.prev_floor_clock_vote = floor_clock;
   }

   if (is_bw_req)
   {
      me_ptr->pm_info.prev_bw_vote = total_bw;
   }

   // for voice scenario, voting is taken care by VCPM
   if (!is_voice_scenario)
   {
      if (CU_PM_REQ_KPPS_BW == vote_type)
      {
         posal_pm_request_info_t request;
         memset(&request, 0, sizeof(posal_pm_request_info_t));

         request.pm_handle_ptr            = me_ptr->pm_info.pm_handle_ptr;
         request.client_log_id            = me_ptr->gu_ptr->log_id;
         request.wait_signal_ptr          = (nonblocking ? NULL : me_ptr->gp_signal_ptr);
         request.resources.mpps.is_valid  = is_mips_req;
         request.resources.mpps.floor_clk = floor_clock; // floor_clock will be 0 by default
         request.resources.mpps.value     = total_kpps / 1000;
         request.resources.bw.is_valid    = is_bw_req;
         request.resources.bw.value       = total_bw;
         result                           = posal_power_mgr_request(&request);
      }
      else
      {
         posal_pm_release_info_t release;
         memset(&release, 0, sizeof(posal_pm_release_info_t));

         release.pm_handle_ptr            = me_ptr->pm_info.pm_handle_ptr;
         release.client_log_id            = me_ptr->gu_ptr->log_id;
         release.wait_signal_ptr          = (nonblocking ? NULL : me_ptr->gp_signal_ptr);
         release.resources.mpps.is_valid  = is_mips_req;
         release.resources.mpps.floor_clk = floor_clock; // floor_clock will be 0 by default
         release.resources.mpps.value     = total_kpps / 1000;
         release.resources.bw.is_valid    = (CU_PM_REL_KPPS_BW == vote_type);
         release.resources.bw.value       = total_bw;
         result                           = posal_power_mgr_release(&release);
      }
   }
   else
   {
      if (me_ptr->flags.is_cntr_started)
      {
         // updating voice flag
         me_ptr->voice_info_ptr->event_flags.did_kpps_change = is_mips_req;
         me_ptr->voice_info_ptr->event_flags.did_bw_change   = is_bw_req;
      }
   }

   me_ptr->pm_info.weighted_kpps_scale_factor_q4 = UNITY_Q4;
   // update only upon voting. Otherwise, voting and below value will not be in sync.
   if (scaled_kpps_agg_q4 != (total_kpps << 4))
   {
      uint32_t weighted_kpps_scale_q4 = total_kpps ? (scaled_kpps_agg_q4 / total_kpps) : UNITY_Q4;

      if (weighted_kpps_scale_q4)
      {
         me_ptr->pm_info.weighted_kpps_scale_factor_q4 = weighted_kpps_scale_q4;
      }
   }

   cu_update_cntr_proc_duration(me_ptr);

   CU_MSG(me_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          SPF_LOG_PREFIX "Vote kpps bw: kpps %lu, bw %lu Bps,floor_clock = %lu. Container "
                         "proc duration %lu us, weighted scale "
                         "factor Q4 %lx, is_cntr_proc_dur_set_paramed %u, nonblocking %u, scaled_kpps_agg_q4 = %lu",
          total_kpps,
          total_bw,
          (uint32_t)floor_clock,
          me_ptr->cntr_proc_duration,
          me_ptr->pm_info.weighted_kpps_scale_factor_q4,
          me_ptr->flags.is_cntr_proc_dur_set_paramed,
          nonblocking,
          scaled_kpps_agg_q4);

   return result;
}

// No need to register for voice use cases. Only needed for audio use cases.
ar_result_t cu_register_with_pm(cu_base_t *me_ptr, bool_t is_duty_cycling_allowed)
{

   bool_t pm_already_registered = posal_power_mgr_is_registered(me_ptr->pm_info.pm_handle_ptr);

   CU_MSG(me_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "Duty cycling enabled is : %d (0-FALSE, 1-TRUE), container IID:0x%x", 
          (int)is_duty_cycling_allowed, me_ptr->gu_ptr->container_instance_id);

   /* *
    * pm_already_registered 	is_duty_cycling_allowed		PM_MODE 		Outcome
    * True						True						ISLAND			No registration again
    * True						True						DEFAULT			De-reg and register as PM_MODE_ISLAND_DUTY_CYCLE
    * True						False						NA				No registration again
    * False						True						ISLAND			No change in PM Mode, Continue register
    * False						True						DEFAULT			register as PM_MODE_ISLAND_DUTY_CYCLE
    * False						False						NA				No change in PM Mode, Continue register
    * */

   if (pm_already_registered)
   {
      if (is_duty_cycling_allowed && me_ptr->pm_info.register_info.mode == PM_MODE_DEFAULT)
      {
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "cu_deregister_with_pm since it registered as  PM_MODE_DEFAULT");
         cu_deregister_with_pm(me_ptr);
         me_ptr->pm_info.register_info.mode = PM_MODE_ISLAND_DUTY_CYCLE;
      }
      else
      {
         return AR_EOK;
      }
   }
   else
   {
      if (is_duty_cycling_allowed && me_ptr->pm_info.register_info.mode == PM_MODE_DEFAULT)
      {
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Updating PM Mode to PM_MODE_ISLAND_DUTY_CYCLE");
         me_ptr->pm_info.register_info.mode = PM_MODE_ISLAND_DUTY_CYCLE;
      }
   }

   return posal_power_mgr_register(me_ptr->pm_info.register_info,
                                   &me_ptr->pm_info.pm_handle_ptr,
                                   me_ptr->gp_signal_ptr,
                                   me_ptr->gu_ptr->container_instance_id);
}

ar_result_t cu_deregister_with_pm(cu_base_t *me_ptr)
{
   if (!posal_power_mgr_is_registered(me_ptr->pm_info.pm_handle_ptr))
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "PM deregister failed, client null");
      return AR_EOK;
   }

   return posal_power_mgr_deregister(&me_ptr->pm_info.pm_handle_ptr, me_ptr->gu_ptr->log_id);
}
