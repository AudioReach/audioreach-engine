/**
 * \file gen_topo_pm.c
 * \brief
 *     This file contains topo common power manager functions.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gen_topo.h"

/* =======================================================================
Static Functions
========================================================================== */

/**
 * total BW = sum of CAPI BW + svc contribution.
 *
 * bytes per sec
 */

static uint32_t gen_topo_aggregate_bandwidth(gen_topo_t *topo_ptr, bool_t only_aggregate, uint32_t *scaled_bw_agg_ptr)
{
   uint32_t aggregated_bw        = 0;
   bool_t   need_to_ignore_state = only_aggregate;

   *scaled_bw_agg_ptr = 0;

   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      uint64_t sg_scaled_bw_agg = 0;
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         uint32_t           bw         = 0;
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;
         // Why not checking data flow state? see gen_cntr_pm gen_cntr_vote_pm_conditionally
         if (gen_topo_is_module_active(module_ptr, need_to_ignore_state))
         {
            if (module_ptr->capi_ptr)
            {
               bw = module_ptr->data_bw + module_ptr->code_bw;
            }
            else
            {
               bw = 0; // framework modules are assumed to have zero BW for now.
            }
         }
         sg_scaled_bw_agg += bw;
         aggregated_bw += bw;
      }
      sg_scaled_bw_agg = sg_scaled_bw_agg * sg_list_ptr->sg_ptr->bus_scale_factor_q4;
      if (sg_list_ptr->sg_ptr->bw_scale_factor > 0 )
      {
         sg_scaled_bw_agg = ( sg_list_ptr->sg_ptr->bw_scale_factor * sg_scaled_bw_agg )/10;
      }

      *scaled_bw_agg_ptr += (sg_scaled_bw_agg >> 4);
   }

   return aggregated_bw;
}

/** gives for all CAPIs. but not per IO streams. */
static uint32_t gen_topo_aggregate_kpps(gen_topo_t *topo_ptr, bool_t only_aggregate, uint32_t *scaled_kpps_agg_q4_ptr)
{
   uint32_t aggregate_kpps       = 0;
   uint32_t sg_kpps       = 0;
   bool_t   need_to_ignore_state = only_aggregate;

   *scaled_kpps_agg_q4_ptr = 0;
   for (gu_sg_list_t *sg_list_ptr = topo_ptr->gu.sg_list_ptr; (NULL != sg_list_ptr); LIST_ADVANCE(sg_list_ptr))
   {
      uint64_t sg_scaled_kpps_agg_q12_ptr = 0;
      for (gu_module_list_t *module_list_ptr = sg_list_ptr->sg_ptr->module_list_ptr; (NULL != module_list_ptr);
           LIST_ADVANCE(module_list_ptr))
      {
         gen_topo_module_t *module_ptr = (gen_topo_module_t *)module_list_ptr->module_ptr;

         // Why not checking data flow state? see gen_cntr_pm gen_cntr_vote_pm_conditionally
         if (gen_topo_is_module_active(module_ptr, need_to_ignore_state))
         {
            uint32_t kpps = 0, scaled_kpps_q4 = 0;

            kpps           = module_ptr->kpps;
            scaled_kpps_q4 = (module_ptr->kpps_scale_factor_q4 * kpps);

            sg_scaled_kpps_agg_q12_ptr += scaled_kpps_q4;
            sg_kpps += kpps;
         }
      }
      if (sg_list_ptr->sg_ptr->duty_cycling_mode_enabled)
      {
         sg_scaled_kpps_agg_q12_ptr = sg_scaled_kpps_agg_q12_ptr *
                                      sg_list_ptr->sg_ptr->duty_cycling_clock_scale_factor_q4 *
                                      sg_list_ptr->sg_ptr->clock_scale_factor_q4;
      }
      else
      {
         sg_scaled_kpps_agg_q12_ptr =
            sg_scaled_kpps_agg_q12_ptr * UNITY_Q4 * sg_list_ptr->sg_ptr->clock_scale_factor_q4;
      }
      if (sg_list_ptr->sg_ptr->kpps_scale_factor > 0 )
      {
         aggregate_kpps += ( sg_list_ptr->sg_ptr->kpps_scale_factor * sg_kpps )/10;
      }
      else
      {
         aggregate_kpps += sg_kpps;
      }
      sg_kpps = 0;
      *scaled_kpps_agg_q4_ptr += (sg_scaled_kpps_agg_q12_ptr >> 8);
   }

   return aggregate_kpps;
}

/* =======================================================================
Public Functions

BW is in bytes per sec
========================================================================== */

ar_result_t gen_topo_aggregate_kpps_bandwidth(gen_topo_t *topo_ptr,
                                              bool_t      only_aggregate,
                                              uint32_t   *aggregate_kpps_ptr,
                                              uint32_t   *aggregate_bw_ptr,
                                              uint32_t   *scaled_kpps_q4_agg_ptr,
                                              uint32_t   *scaled_bw_agg_ptr)
{
   ar_result_t result = AR_EOK;

   // Aggregate kpps/bw, then send to common processing.
   {
      *aggregate_kpps_ptr = gen_topo_aggregate_kpps(topo_ptr, only_aggregate, scaled_kpps_q4_agg_ptr);
   }

   {
      *aggregate_bw_ptr = gen_topo_aggregate_bandwidth(topo_ptr, only_aggregate, scaled_bw_agg_ptr);
   }

   return result;
}
