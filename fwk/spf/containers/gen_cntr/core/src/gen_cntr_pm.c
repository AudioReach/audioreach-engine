/**
 * \file gen_cntr_pm.c
 * \brief
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_cntr_i.h"

/*
 * Container BW = input BW + output BW.
 * Even though ext buffers may be re-used for internal ports, instead of memcpy, here worst case
 * contribution is considered - which is attributed to memcpy. It's to be noted that even if memcpy doesn't necessarily
 * take
 * place at ext-in (out) port to immediate internal-input(output) port, the copy would happen somewhere deeper inside
 * the graph.
 *
 * MemCopies within the topology are not accounted currently.
 *
 */
ar_result_t gen_cntr_update_cntr_kpps_bw(gen_cntr_t *me_ptr, bool_t force_aggregate)
{
   uint32_t extra_bw_for_sr = 0, extra_for_ch = 0;
   uint32_t bw = 0, kpps = 0;
   bool_t   kpps_bw_scale_factor_change = FALSE;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      gen_cntr_ext_in_port_t *ext_in_port_ptr = (gen_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      topo_port_state_t in_port_state = ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr)->common.state;

      // For force aggregation don't need to check state.
      if (!force_aggregate)
      {
         // dont update container bw unless it is in started state
         if (!(TOPO_PORT_STATE_STARTED == in_port_state))
         {
            continue;
         }
      }

      topo_media_fmt_t *media_fmt_ptr = &ext_in_port_ptr->cu.media_fmt;

      // TODO: voting BW for raw compressed cases, extra BW for higher SR and CH.
      if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
      {
         bw += media_fmt_ptr->pcm.sample_rate * media_fmt_ptr->pcm.num_channels *
               TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample);
         {
            bw += media_fmt_ptr->pcm.sample_rate * TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample) *
                  media_fmt_ptr->pcm.num_channels;

            extra_bw_for_sr = 6 * 1024 * 1024;
            extra_for_ch    = 7 * 1024 * 1024;

            if (media_fmt_ptr->pcm.sample_rate > 48000)
            {
               bw += extra_bw_for_sr;
            }
            if (media_fmt_ptr->pcm.num_channels > 2)
            {
               bw += extra_for_ch;
            }
         }

         kpps += topo_get_memscpy_kpps(media_fmt_ptr->pcm.bits_per_sample,
                                       media_fmt_ptr->pcm.num_channels,
                                       media_fmt_ptr->pcm.sample_rate);
      }
      else
      {
      }
   }

   // Add each external output port's bw to the total bw.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gen_cntr_ext_out_port_t *ext_out_port_ptr = (gen_cntr_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      topo_media_fmt_t *       media_fmt_ptr    = &ext_out_port_ptr->cu.media_fmt;

      topo_port_state_t out_port_state = ((gen_topo_input_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.state;

      // if we are forcing aggregation, even prepared state is allowed
      if (!force_aggregate)
      {
         // dont update container bw unless it is in started state
         if (!(TOPO_PORT_STATE_STARTED == out_port_state))
         {
            continue;
         }
      }

      // TODO: voting BW for raw compressed cases, extra BW for higher SR and CH.
      if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
      {
         bw += media_fmt_ptr->pcm.sample_rate * media_fmt_ptr->pcm.num_channels *
               TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample);
         {
            bw += media_fmt_ptr->pcm.sample_rate * TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample) *
                  media_fmt_ptr->pcm.num_channels;

            extra_bw_for_sr = 6 * 1024 * 1024;
            extra_for_ch    = 7 * 1024 * 1024;

            if (media_fmt_ptr->pcm.sample_rate > 48000)
            {
               bw += extra_bw_for_sr;
            }
            if (media_fmt_ptr->pcm.num_channels > 2)
            {
               bw += extra_for_ch;
            }
         }

         kpps += topo_get_memscpy_kpps(media_fmt_ptr->pcm.bits_per_sample,
                                       media_fmt_ptr->pcm.num_channels,
                                       media_fmt_ptr->pcm.sample_rate);
      }
   }

   if (bw != me_ptr->cu.pm_info.cntr_bw)
   {
      me_ptr->cu.pm_info.cntr_bw  = bw;
      kpps_bw_scale_factor_change = TRUE;
   }

   if (kpps != me_ptr->cu.pm_info.cntr_kpps)
   {
      me_ptr->cu.pm_info.cntr_kpps = kpps;
      kpps_bw_scale_factor_change  = TRUE;
   }

   if (kpps_bw_scale_factor_change)
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, kpps_bw_scale_factor_change);
      GEN_CNTR_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Container KPPS %lu, BW %lu Bps", kpps, bw);
   }

   return AR_EOK;
}

/**
 * force_vote doesn't matter if is_release=TRUE
 *
 * is_release helps in releasing BW even when aggregated BW is nonzero, useful for suspend.
 *
 * force_vote helps in voting BW due to changes in svc & not due to CAPI events.
 */
static ar_result_t gen_cntr_handle_clk_vote_change(gen_cntr_t *      me_ptr,
                                                   cu_pm_vote_type_t vote_type,
                                                   bool_t            only_aggregate,
                                                   uint32_t *        kpps_ptr,
                                                   uint32_t *        bw_ptr)
{
   ar_result_t result = AR_EOK;

   uint32_t      topo_kpps = 0, topo_bw = 0, scaled_kpps_agg_q4 = 0, scaled_bw_agg = 0;
   cu_pm_info_t *pm_info_ptr = &me_ptr->cu.pm_info;

   if ((CU_PM_REQ_KPPS_BW == vote_type) || only_aggregate)
   {
      gen_topo_aggregate_kpps_bandwidth(&me_ptr->topo,
                                        only_aggregate,
                                        &topo_kpps,
                                        &topo_bw,
                                        &scaled_kpps_agg_q4,
                                        &scaled_bw_agg);

      topo_kpps += pm_info_ptr->cntr_kpps;
      scaled_kpps_agg_q4 += (pm_info_ptr->cntr_kpps * UNITY_Q4);
      scaled_bw_agg += pm_info_ptr->cntr_bw;
      // if we don't add then avg scale factor will be wrong in cu_handle_clk_vote_change
      topo_bw += pm_info_ptr->cntr_bw;
   }

   /* Vote the aggregated BW and KPPS using CU utility */
   if (!only_aggregate)
   {
      /** if STM container is started, then PM requests made at run time must be considered nonblocking
       * Generally during cmd handling APM votes covers us.
       * But if direct GPR cmd or run time media format causes vote change, we do best effort to vote.
       * But if we wait for vote to complete, then it may cause signal miss.
       * For non-STM deadline is not so strict. If we process without waiting for PM vote, then we may cause undervoting
       * issues. */
      bool_t has_stm     = gen_cntr_is_signal_triggered(me_ptr);
      bool_t nonblocking = has_stm && me_ptr->cu.flags.is_cntr_started;

      /** force voting: vote only if diff. some SPR tests went signal miss due to run time voting*/
      result = cu_handle_clk_vote_change(&me_ptr->cu,
                                         vote_type,
                                         FALSE /*force_vote*/,
                                         topo_kpps,
                                         scaled_bw_agg,
                                         scaled_kpps_agg_q4,
                                         nonblocking);
   }

   SET_IF_NOT_NULL(kpps_ptr, topo_kpps);
   SET_IF_NOT_NULL(bw_ptr, topo_bw);

   return result;
}

ar_result_t gen_cntr_aggregate_kpps_bw(void *cu_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr)
{
   gen_cntr_t *me_ptr = (gen_cntr_t *)cu_ptr;
   gen_cntr_update_cntr_kpps_bw(me_ptr, TRUE /*force_aggregate*/);
   return gen_cntr_handle_clk_vote_change(me_ptr,
                                          CU_PM_REQ_KPPS_BW /* vote type */,
                                          TRUE /* only aggregate */,
                                          kpps_ptr,
                                          bw_ptr);
}

/**
 * conditionally votes
 * - core and bus clocks
 * - latency vote
 * - thread priority
 *
 * core & bus clocks:
 *    - Module events KPPS/BW (Process state) KPPS Scale factor change
 *       - at least one SG start + data flow for FTRT graphs
 *    - Container input or output media format change
 *    -
 * Latency vote:
 *    - KPPS Scale factor change causing container proc duration change
 *    - Threshold change resulting in container frame duration change
 *    - RT/FTRT
 *    - At least one sub-graph started
 * Thread priority
 *    - same as latency
 *
 * KPPS/BW/latency Need to be voted only if at least one SG is started && in FTRT graphs, data flow should've begun.
 * Removing votes: All SG are stopped (data flow not checked).
 * Thread priority would be lower for FTRT cases.
 *
 * Data flow state based voting helps in voice UI use case, where graph after DAM module doesn't
 * contribute to vote after internal EOS is sent from DAM module. Also in NT mode, voting is done only
 * after data flow begins.
 *
 * In case of EOS stuck inside the topo, ext-in may be at gap, but internal ports may have still have EOSes or data
 * buffers. The caller is responsible for ensuring that when ext-in is at gap, no EOSes are stuck. See
 *   gen_cntr_clear_eos.
 *
 * In vote_core_n_bus_clock, where we iterate over all ports, we need to, ideally, consider KPPS/BW val only
 * for those ports for which data flow has begun. However, such checks would not only be complicated, but also
 * suffer from an issue. Unless data flow happens, port state cannot be moved to data-flow-started state, however,
 * to have the data flow, power voting is necessary. In order to avoid such circular dependency, simple route is taken:
 * All started ports contribute to power voting independent of data flow state. Data flow state is checked only
 * at the ext-in level and as long as one has data flow, all ports contribute to voting.

 */
ar_result_t gen_cntr_perf_vote(gen_cntr_t                 *me_ptr,
                               posal_thread_prio_t         original_prio,
                               gen_topo_capi_event_flag_t *capi_event_flag_ptr,
                               cu_event_flags_t           *fwk_event_flag_ptr)
{
   ar_result_t result = AR_EOK;

   /**
    * Vote only if at least one SG is started. individual port states are checked in process_kpps_bandwidth.
    * For FTRT containers don't vote unless data flow begins.
    *
    * basically this func should block call to process_kpps_bandwidth if
    * gen_topo_are_all_ext_in_ftrt_and_data_flow_stopped
    * return TRUE for requests (otherwise, vote will be made as aggregation happens).
    * For releases it cannot block the call (as otherwise, votes won't be released);
    *
    * containers with source modules still vote even if source is disabled.
    * When source modules are disabled, no need to vote for power even if graph is started. Currently only Ext-in
    * flow-gap is considered.
    *
    */
   cu_pm_vote_type_t vote_type;
   bool_t            cntr_activity_stopped = !me_ptr->cu.flags.is_cntr_started;
   bool_t            is_ftrt               = FALSE;
   if (!cntr_activity_stopped) // request
   {
      bool_t ftrt_data_flow_stopped = gen_topo_check_if_all_src_are_ftrt_n_at_gap(&me_ptr->topo, &is_ftrt);
      if (ftrt_data_flow_stopped)
      {
         TOPO_MSG(me_ptr->topo.gu.log_id,
                  DBG_HIGH_PRIO,
                  "The container has all FTRT ports and none have data flow on. Hence releasing kpps/bw vote if any.");
         // but we may have to release if we had voted earlier
         cntr_activity_stopped = TRUE;
      }
   }

   /***
    * DFS+rt_ftrt, cntr_run_state_change -> KPPS, BW, latency, thread priority, latency needed only if SG started + data
    * flow for FTRT
    *
    * sg_state_change - state is used in KPPS/BW aggregation
    *
    * Only for FTRT graphs, DFS change must result in voting.
    * For RT graphs if DFS causes voting, then that can cause signal miss etc as voting takes time
    */

   bool_t cmn_conditions =
      cntr_activity_stopped ||
      (is_ftrt && (fwk_event_flag_ptr->dfs_change || me_ptr->topo.flags.defer_voting_on_dfs_change)) ||
      fwk_event_flag_ptr->rt_ftrt_change || fwk_event_flag_ptr->port_state_change;

   // island entry or don't-care votes are usually blocking so should be done first before kpps/bw/latency which can be
   // non-blocking.
   {
      posal_pm_island_vote_t fwk_island_vote;

      /** if container is not started then vote for island don't care
          if all ports are ftrt and at gap then vote for island don't care
          Buffering module changing it's trigger policies for every island entry/exit, as of now we are not handling
          trigger policy events during island period so container is exiting from island and after 2ms it votes for
          island. But here even though trigger policies are changing container should be in island so introduced these
          two flags For BT A2DP LPI usecase. We should also make sure to not vote for Entry if only TGP events are set,
        island entry is voted from data processing context based on wall clk time (2 frames).
        */
      if ((!me_ptr->cu.flags.is_cntr_started) || cntr_activity_stopped)
      {
         fwk_island_vote.island_vote_type = PM_ISLAND_VOTE_DONT_CARE;
         gen_cntr_update_island_vote(me_ptr, fwk_island_vote);
      }
      else if ((capi_event_flag_ptr->word) != (GT_CAPI_EVENT_SIGNAL_TRIGGER_POLICY_CHANGE_BIT_MASK |
                                               GT_CAPI_EVENT_DATA_TRIGGER_POLICY_CHANGE_BIT_MASK))
      {
         fwk_island_vote.island_vote_type = PM_ISLAND_VOTE_EXIT;
         gen_cntr_update_island_vote(me_ptr, fwk_island_vote);
      }
   }

   if (cmn_conditions || fwk_event_flag_ptr->kpps_bw_scale_factor_change || capi_event_flag_ptr->kpps ||
       capi_event_flag_ptr->process_state || capi_event_flag_ptr->bw || fwk_event_flag_ptr->sg_state_change ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_island_exit)
   {
      if (fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry)
      {
         vote_type = CU_PM_REL_KPPS_ONLY;
      }
      else
      {
         vote_type = cntr_activity_stopped ? CU_PM_REL_KPPS_BW : CU_PM_REQ_KPPS_BW;
      }
      // updates me_ptr->cu.cntr_proc_duration
      gen_cntr_handle_clk_vote_change(me_ptr, vote_type, FALSE /* only aggregate */, NULL, NULL);
   }

   if (cmn_conditions || fwk_event_flag_ptr->proc_dur_change || fwk_event_flag_ptr->cntr_run_state_change ||
       capi_event_flag_ptr->is_signal_triggered_active_change ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_island_exit)
   {
      // if thread prio is already bumped up, then it will go to normal when they bump-down. no need to change now.
      if (!me_ptr->flags.is_thread_prio_bumped_up)
      {
         // Original prio is saved in the context of the caller (handle_fwk_events),
         // so it will go back to original prio during bump down
         gen_cntr_check_bump_up_thread_priority(&me_ptr->cu, FALSE /* is bump up*/, original_prio);
      }
      if (fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry)
      {
         cntr_activity_stopped = TRUE;
      }
      cu_vote_latency(&me_ptr->cu,
                      cntr_activity_stopped,
                      (gen_cntr_is_signal_triggered(me_ptr) || me_ptr->cu.flags.is_real_time));
   }

   return result;
}
