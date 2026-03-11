/**
 * \file olc_pm.c
 * \brief
 *     This file contains container OLC power manager functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "olc_i.h"

/* =======================================================================
Public Functions
========================================================================== */

/**
 * TODO: will need update after profiling
 */
static void olc_get_extra_pcm_bw_by_mf_type(olc_t *me_ptr, uint32_t *extra_for_sr, uint32_t *extra_for_ch)
{
   *extra_for_sr = 6 * 1024 * 1024;
   *extra_for_ch = 7 * 1024 * 1024;
}

/*
 * svc BW = input BW + output BW. Input BW is considered only for PCM.
 *
 * Also this BW should ideally be InpStrm, OutStrm property. But for now, its property of olc_t
 */
ar_result_t olc_update_cntr_kpps_bw(olc_t *me_ptr, bool_t force_aggregate)
{
   uint32_t extra_bw_for_sr              = 0;
   uint32_t extra_for_ch                 = 0;
   uint32_t bw                           = 0;
   uint32_t kpps                         = 0;
   bool_t   kpps_bw_scale_factor_changed = FALSE;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->topo.gu.ext_in_port_list_ptr;
        (NULL != ext_in_port_list_ptr);
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      olc_ext_in_port_t *ext_in_port_ptr = (olc_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;
      topo_port_state_t  in_port_state   = ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr)->common.state;

      // if we are forcing aggregation, even prepared state is allowed
      if (force_aggregate)
      {
         // dont update container bw unless it is in started or prepared state
         if ((TOPO_PORT_STATE_STARTED != in_port_state) && (TOPO_PORT_STATE_PREPARED != in_port_state))
         {
            continue;
         }
      }
      else
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
         if (1) // TODO: to vote on behalf of sh mem modules
         {
            bw += media_fmt_ptr->pcm.sample_rate * TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample) *
                  media_fmt_ptr->pcm.num_channels;

            olc_get_extra_pcm_bw_by_mf_type(me_ptr, &extra_bw_for_sr, &extra_for_ch);

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

   // Add each external output port's bw to the total bw.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = me_ptr->topo.gu.ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      olc_ext_out_port_t *ext_out_port_ptr = (olc_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;
      topo_media_fmt_t *  media_fmt_ptr    = &ext_out_port_ptr->cu.media_fmt;

      topo_port_state_t out_port_state = ((gen_topo_input_port_t *)ext_out_port_ptr->gu.int_out_port_ptr)->common.state;

      // if we are forcing aggregation, even prepared state is allowed
      if (force_aggregate)
      {
         // dont update container bw unless it is in started or prepared state
         if ((TOPO_PORT_STATE_STARTED != out_port_state) && (TOPO_PORT_STATE_PREPARED != out_port_state))
         {
            continue;
         }
      }
      else
      {
         // dont update container bw unless it is in started state
         if (!(TOPO_PORT_STATE_STARTED == out_port_state))
         {
            continue;
         }
      }

      // TODO: voting BW for raw compressed cases, extra BW for higher SR and CH
      if (SPF_IS_PACKETIZED_OR_PCM(media_fmt_ptr->data_format))
      {
         bw += media_fmt_ptr->pcm.sample_rate * media_fmt_ptr->pcm.num_channels *
               TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample);
         if (1) // TODO: to vote on behalf of sh mem modules
         {
            bw += media_fmt_ptr->pcm.sample_rate * TOPO_BITS_TO_BYTES(media_fmt_ptr->pcm.bits_per_sample) *
                  media_fmt_ptr->pcm.num_channels;

            olc_get_extra_pcm_bw_by_mf_type(me_ptr, &extra_bw_for_sr, &extra_for_ch);

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
      me_ptr->cu.pm_info.cntr_bw   = bw;
      kpps_bw_scale_factor_changed = TRUE;
   }

   if (kpps != me_ptr->cu.pm_info.cntr_kpps)
   {
      me_ptr->cu.pm_info.cntr_kpps = kpps;
      kpps_bw_scale_factor_changed = TRUE;
   }

   if (kpps_bw_scale_factor_changed)
   {
      CU_SET_ONE_FWK_EVENT_FLAG(&me_ptr->cu, kpps_bw_scale_factor_change);
      OLC_MSG(me_ptr->topo.gu.log_id, DBG_LOW_PRIO, " Container KPPS %lu, BW %lu Bps", kpps, bw);
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
ar_result_t olc_handle_clk_vote_change(olc_t *           me_ptr,
                                       cu_pm_vote_type_t vote_type,
                                       bool_t            force_aggregate,
                                       uint32_t *        kpps_ptr,
                                       uint32_t *        bw_ptr)
{
   ar_result_t result = AR_EOK;
   bool_t force_vote = TRUE; // always force vote as aggregation may happen at any time: dfs, sg_start etc (all of which
                             // are checked in caller).
   uint32_t topo_kpps = 0, topo_bw = 0, scaled_kpps_agg_q4 = 0;

   // todo:There is no topo running in OLC. Need to have a call in sgm to update the vote based on I/O ports

   /* Vote the aggregated BW and KPPS using CU utility */
   result = cu_handle_clk_vote_change(&me_ptr->cu,
                                      vote_type,
                                      force_vote,
                                      topo_kpps,
                                      topo_bw,
                                      scaled_kpps_agg_q4,
                                      FALSE /*nonblocking*/);

   SET_IF_NOT_NULL(kpps_ptr, topo_kpps);
   SET_IF_NOT_NULL(bw_ptr, topo_bw);

   return result;
}

void olc_vote_pm_conditionally(olc_t *me_ptr, uint32_t period_us, bool_t is_at_least_one_sg_started)
{
   bool_t FORCE_AGGREGATE_FALSE = FALSE;

   cu_pm_vote_type_t vote_type;
   bool_t            is_release = !is_at_least_one_sg_started;
   if (!is_release) // request
   {
      bool_t is_ftrt                = FALSE;
      bool_t ftrt_data_flow_stopped = gen_topo_check_if_all_src_are_ftrt_n_at_gap(&me_ptr->topo, &is_ftrt);
      if (ftrt_data_flow_stopped)
      {
         TOPO_MSG(me_ptr->topo.gu.log_id,
                  DBG_HIGH_PRIO,
                  "The container has all FTRT ports and none have data flow "
                  "started yet. Hence releasing kpps/bw vote if any.");
         // but we may have to release if we had voted earlier
         is_release = TRUE;
      }
   }

   if (is_release)
   {
      period_us = 0;
   }
   vote_type = is_release ? CU_PM_REL_KPPS_BW : CU_PM_REQ_KPPS_BW;
   cu_vote_latency(&me_ptr->cu, is_release, (olc_is_stm(&me_ptr->cu) || cu_is_realtime(&me_ptr->cu)));

   olc_handle_clk_vote_change(me_ptr, vote_type, FORCE_AGGREGATE_FALSE, NULL, NULL);
}

ar_result_t olc_aggregate_kpps_bw(void *cu_ptr, uint32_t *kpps_ptr, uint32_t *bw_ptr)
{
   olc_t *me_ptr = (olc_t *)cu_ptr;
   olc_update_cntr_kpps_bw(me_ptr, TRUE /*force_aggregate*/);
   return olc_handle_clk_vote_change(me_ptr, CU_PM_REQ_KPPS_BW, TRUE /* force aggregate */, kpps_ptr, bw_ptr);
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
ar_result_t olc_perf_vote(olc_t                      *me_ptr,
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
   bool_t            is_release = !me_ptr->cu.flags.is_cntr_started;
   if (!is_release) // request
   {
      bool_t is_ftrt                = FALSE;
      bool_t ftrt_data_flow_stopped = gen_topo_check_if_all_src_are_ftrt_n_at_gap(&me_ptr->topo, &is_ftrt);
      if (ftrt_data_flow_stopped)
      {
         TOPO_MSG(me_ptr->topo.gu.log_id,
                  DBG_HIGH_PRIO,
                  "The container has all FTRT ports and none have data flow on. Hence releasing kpps/bw vote if any.");
         // but we may have to release if we had voted earlier
         is_release = TRUE;
      }
   }

   /***
    * DFS+rt_ftrt, cntr_run_state_change -> KPPS, BW, latency, thread priority, latency needed only if SG started + data
    * flow for FTRT
    *
    * sg_state_change - state is used in KPPS/BW aggregation
    */
   bool_t cmn_conditions = is_release || fwk_event_flag_ptr->dfs_change || fwk_event_flag_ptr->rt_ftrt_change;

   if (cmn_conditions || fwk_event_flag_ptr->kpps_bw_scale_factor_change || capi_event_flag_ptr->kpps ||
       capi_event_flag_ptr->bw || fwk_event_flag_ptr->sg_state_change)
   {
      vote_type = is_release ? CU_PM_REL_KPPS_BW : CU_PM_REQ_KPPS_BW;
      // updates me_ptr->cu.cntr_proc_duration
      olc_handle_clk_vote_change(me_ptr, vote_type, FALSE /* force aggregate */, NULL, NULL);
   }

   if (cmn_conditions || fwk_event_flag_ptr->proc_dur_change || fwk_event_flag_ptr->cntr_run_state_change)
   {
      olc_get_set_thread_priority(me_ptr, NULL, TRUE /*should set */);
      cu_vote_latency(&me_ptr->cu, is_release, cu_is_realtime(&me_ptr->cu));
   }

   return result;
}
