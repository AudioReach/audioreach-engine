/**
 * \file cu_voice_util.c
 * \brief
 *     This file contains container utility functions for voice path handling in CU
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"
#include "apm.h"
#include "other_metadata.h"

/**
 * Returns TRUE if at least one subgraph in the container has the SID APM_SUB_GRAPH_SID_VOICE_CALL.
 */
bool_t cu_has_voice_sid(cu_base_t *base_ptr)
{
   gu_sg_list_t *sg_list_ptr = base_ptr->gu_ptr->sg_list_ptr;
   while (sg_list_ptr)
   {
      if (sg_list_ptr->sg_ptr)
      {
         gu_sg_t *sg_ptr = sg_list_ptr->sg_ptr;
         if (IS_VOICE_SCENARIO_ID(sg_ptr->sid))
         {
            return TRUE;
         }
      }
      LIST_ADVANCE(sg_list_ptr);
   }

   return FALSE;
}

void cu_destroy_voice_info(cu_base_t *base_ptr)
{
   MFREE_NULLIFY(base_ptr->voice_info_ptr);
}

ar_result_t cu_create_voice_info(cu_base_t *base_ptr, spf_msg_cmd_graph_open_t *open_cmd_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (0 == open_cmd_ptr->num_sub_graphs)
   {
      return result;
   }

   apm_sub_graph_cfg_t **sg_cfg_list_pptr = open_cmd_ptr->sg_cfg_list_pptr;
   /* Checking for only one sub graph. Assuming all sub graphs mapped to a container would have same scenario ID
    * (atleast for voice call use case) */
   apm_sub_graph_cfg_t *sg_cmd_ptr = sg_cfg_list_pptr[0];
   gu_t                *gu_ptr     = get_gu_ptr_for_current_command_context(base_ptr->gu_ptr);
   gu_sg_t *sg_ptr = gu_find_subgraph(gu_ptr, sg_cmd_ptr->sub_graph_id);
   VERIFY(result, sg_ptr);

   if (IS_VOICE_SCENARIO_ID(sg_ptr->sid))
   {
      MALLOC_MEMSET(base_ptr->voice_info_ptr, cu_voice_info_t, sizeof(cu_voice_info_t), base_ptr->heap_id, result);
      CU_MSG(base_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Allocated memory for voice info");
      if (APM_SUB_GRAPH_SID_SAT_VOICE_CALL == sg_ptr->sid)
      {
         base_ptr->voice_info_ptr->is_satellite_voice_sid = TRUE;
      }
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t cu_voice_session_cfg(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cntr_param_id_voice_session_info_t *cmd_ptr = NULL;
   VERIFY(result, (NULL != base_ptr->voice_info_ptr));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_CFG: Executing voice session cfg SET-param. current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   VERIFY(result, *param_size_ptr >= sizeof(cntr_param_id_voice_session_info_t));
   cmd_ptr = (cntr_param_id_voice_session_info_t *)param_payload_ptr;

   VERIFY(result, (NULL != cmd_ptr));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_CFG: period_us = %lu, vsid = %lu, vfr_mode = %lu",
          cmd_ptr->period_us,
          cmd_ptr->vsid,
          cmd_ptr->vfr_mode);

   base_ptr->flags.is_cntr_period_set_paramed = FALSE; // set FALSE to get past check in below func
   /* Send ICB info to upstream since container period is updated */
   TRY(result, cu_handle_frame_len_change(base_ptr, &base_ptr->cntr_frame_len, cmd_ptr->period_us));
   base_ptr->flags.is_cntr_period_set_paramed = TRUE;

   base_ptr->cntr_vtbl_ptr->handle_cntr_period_change(base_ptr);

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_CFG: Done Executing voice session cfg cmd, current channel mask=0x%x. result=%lu",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t cu_set_calibration_ops_done(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   VERIFY(result, (NULL != base_ptr->voice_info_ptr));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:CALIBRATION_OPS: Executing dynamic calibration ops done SET-param. current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   if(NULL != base_ptr->cntr_vtbl_ptr->handle_cntr_set_calibration_ops_done)
   {
      result |= base_ptr->cntr_vtbl_ptr->handle_cntr_set_calibration_ops_done(base_ptr);
   }
   else
   {
      result = AR_EUNSUPPORTED;
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_CALIBRATION_OPS: Done Executing voice set calibration ops, current channel mask=0x%x. result=%lu",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t cu_offload_voice_session_cfg(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   cntr_param_id_offload_voice_session_info_t *voice_session_info_ptr = NULL;
   VERIFY(result, (NULL != base_ptr->voice_info_ptr));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:OFFLOAD_VOICE_SESSION_CFG: Executing offload voice session cfg SET-param. current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   VERIFY(result, *param_size_ptr >= sizeof(cntr_param_id_offload_voice_session_info_t));
   voice_session_info_ptr = (cntr_param_id_offload_voice_session_info_t *)param_payload_ptr;

   VERIFY(result, (NULL != voice_session_info_ptr));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:OFFLOAD_VOICE_SESSION_CFG: sg_id = 0x%lX, kpps_sf = %d, bw_sf = %d",
          voice_session_info_ptr->sg_id,
          voice_session_info_ptr->kpps_sf,
          voice_session_info_ptr->bw_sf);

   base_ptr->cntr_vtbl_ptr->handle_cntr_set_offload_voice_session_info(base_ptr, voice_session_info_ptr);

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:OFFLOAD_VOICE_SESSION_CFG: Done Executing offload voice session cfg cmd, current channel mask=0x%x. result=%lu",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t cu_cntr_proc_params_query(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   cntr_param_id_container_proc_params_info_t *cmd_ptr = NULL;
   bool_t                                      kpps_query, bw_query, frame_size_query, hw_acc_proc_delay_query = FALSE;

   VERIFY(result, (NULL != base_ptr->voice_info_ptr));
   VERIFY(result, (NULL != base_ptr->cntr_vtbl_ptr));
   VERIFY(result, (NULL != base_ptr->cntr_vtbl_ptr->aggregate_kpps_bw));

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_QUERY: Executing voice session cfg GET-Param. current channel mask=0x%x",
          base_ptr->curr_chan_mask);

   VERIFY(result, *param_size_ptr >= sizeof(cntr_param_id_container_proc_params_info_t));
   cmd_ptr = (cntr_param_id_container_proc_params_info_t *)param_payload_ptr;

   VERIFY(result, (NULL != cmd_ptr));

   // update the container contribution to bw and kpps
   uint32_t kpps = 0, bw = 0, hw_acc_proc_delay = 0;
   result |= base_ptr->cntr_vtbl_ptr->aggregate_kpps_bw(base_ptr, &kpps, &bw);

   result |= base_ptr->cntr_vtbl_ptr->aggregate_hw_acc_proc_delay(base_ptr, &hw_acc_proc_delay);

   kpps_query              = cmd_ptr->event_flags.kpps_query;
   frame_size_query        = cmd_ptr->event_flags.frame_size_query;
   bw_query                = cmd_ptr->event_flags.bw_query;
   hw_acc_proc_delay_query = cmd_ptr->event_flags.hw_acc_proc_delay_query;

   // initialize to 0
   cmd_ptr->kpps                 = 0;
   cmd_ptr->frame_size_us        = 0;
   cmd_ptr->bw                   = 0;
   cmd_ptr->hw_acc_proc_delay_us = 0;

   if (kpps_query)
   {
      cmd_ptr->kpps = kpps;
   }

   if (frame_size_query)
   {
      cmd_ptr->frame_size_us = base_ptr->cntr_frame_len.frame_len_us;
   }

   if (bw_query)
   {
      cmd_ptr->bw = bw;
   }

   if (hw_acc_proc_delay_query)
   {
      cmd_ptr->hw_acc_proc_delay_us = hw_acc_proc_delay;
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_QUERY: Parameters Queried (1 -yes, 0- no): kpps (%u) = %lu, frame_size (%u) = %lu ms, bw "
          "(%u) = %lu Bps, HW accelerator proc delay (%u) = %lu us",
          kpps_query,
          cmd_ptr->kpps,
          frame_size_query,
          cmd_ptr->frame_size_us,
          bw_query,
          cmd_ptr->bw,
          hw_acc_proc_delay_query,
          cmd_ptr->hw_acc_proc_delay_us);

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_SESSION_QUERY: Done Executing voice session query, current channel mask=0x%x. result=%lu",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

static ar_result_t cu_voice_raise_proc_params_update_event_to_clients(
   cu_base_t *                                   base_ptr,
   cntr_event_id_container_perf_params_update_t *payload_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_PROC_PARAMS_EVENT: Raising the proc params updated event, kpps = %lu, bw = %lu bps, operating "
          "frame_size = %lu, hw_acc_proc_delay_us = %lu us, did_hw_acc_proc_delay_change = %u did_algo_delay_change = "
          "%u",
          payload_ptr->kpps,
          payload_ptr->bw,
          payload_ptr->frame_size_us,
          payload_ptr->hw_acc_proc_delay_us,
          payload_ptr->did_hw_acc_proc_delay_change,
          payload_ptr->did_algo_delay_change);

   TRY(result,
       cu_raise_container_events_to_clients(base_ptr,
                                            CNTR_EVENT_ID_CONTAINER_PERF_PARAMS_UPDATE,
                                            (int8_t *)payload_ptr,
                                            sizeof(cntr_event_id_container_perf_params_update_t)));

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:VOICE_PROC_PARAMS_EVENT: Done raising the proc params updated event, current channel mask=0x%x. "
          "result=%lu",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t cu_check_and_raise_voice_proc_param_update_event(cu_base_t *base_ptr,
                                                             uint32_t   log_id,
                                                             uint32_t   hw_acc_proc_delay,
                                                             bool_t     hw_acc_proc_delay_event)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   if (base_ptr->voice_info_ptr->event_flags.did_kpps_change || base_ptr->voice_info_ptr->event_flags.did_bw_change ||
       base_ptr->voice_info_ptr->event_flags.did_algo_delay_change ||
       base_ptr->voice_info_ptr->event_flags.did_frame_size_change || hw_acc_proc_delay_event)
   {
      cntr_event_id_container_perf_params_update_t evt_payload;
      evt_payload.kpps                         = base_ptr->pm_info.prev_kpps_vote;
      evt_payload.bw                           = base_ptr->pm_info.prev_bw_vote;
      evt_payload.frame_size_us                = base_ptr->cntr_frame_len.frame_len_us;
      evt_payload.did_algo_delay_change        = base_ptr->voice_info_ptr->event_flags.did_algo_delay_change;
      evt_payload.did_hw_acc_proc_delay_change = hw_acc_proc_delay_event;
      evt_payload.hw_acc_proc_delay_us         = hw_acc_proc_delay;

      TRY(result, cu_voice_raise_proc_params_update_event_to_clients(base_ptr, &evt_payload));
      base_ptr->voice_info_ptr->event_flags.did_kpps_change       = FALSE;
      base_ptr->voice_info_ptr->event_flags.did_bw_change         = FALSE;
      base_ptr->voice_info_ptr->event_flags.did_algo_delay_change = FALSE;
      base_ptr->voice_info_ptr->event_flags.did_frame_size_change = FALSE;

      CU_MSG(log_id, DBG_MED_PRIO, "Raised voice params update event with result %lu", result);
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}
