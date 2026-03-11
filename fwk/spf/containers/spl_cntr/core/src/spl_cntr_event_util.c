/**
 * \file spl_cntr_event_util.c
 * \brief
 *     This file contains spl_cntr utility functions for handling/managing events. (input and output).
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spl_cntr_i.h"

ar_result_t spl_cntr_handle_algo_delay_change_event(gen_topo_module_t *module_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, module_ptr->topo_ptr);

   if (me_ptr->cu.voice_info_ptr && me_ptr->cu.flags.is_cntr_started)
   {
      me_ptr->cu.voice_info_ptr->event_flags.did_algo_delay_change = TRUE;
   }

   return AR_EOK;
}

/*
 * Handler for all capi v2 data_from_dsp_service to.
 */
ar_result_t spl_cntr_handle_event_get_data_from_dsp_service(gen_topo_module_t *module_ptr,
                                                            capi_event_info_t *event_info_ptr)
{
   spl_cntr_t *                            me_ptr  = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, module_ptr->topo_ptr);
   ar_result_t                             result  = AR_EOK;
   capi_buf_t *                            payload = &event_info_ptr->payload;
   capi_event_get_data_from_dsp_service_t *dsp_event_ptr =
      (capi_event_get_data_from_dsp_service_t *)(payload->data_ptr);

   switch (dsp_event_ptr->param_id)
   {
      default:
      {
         return cu_handle_event_from_dsp_service_topo_cb(&me_ptr->cu, &module_ptr->gu, event_info_ptr);
      }
   }

   return result;
}

ar_result_t spl_cntr_handle_capi_event(gen_topo_module_t *module_ptr,
                                       capi_event_id_t    event_id,
                                       capi_event_info_t *event_info_ptr)
{
   spl_cntr_t *me_ptr = (spl_cntr_t *)GET_BASE_PTR(spl_cntr_t, topo, module_ptr->topo_ptr);
   ar_result_t result = AR_EOK;

   result = cu_handle_capi_event(&me_ptr->cu, &module_ptr->gu, event_id, event_info_ptr);

   return result;
}

/*
 * Handler for all capi v2 data_to_dsp_service events.
 */
ar_result_t spl_cntr_handle_event_data_to_dsp_service(gen_topo_module_t *context_ptr, capi_event_info_t *event_info_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t        result      = AR_EOK;
   gen_topo_module_t *module_ptr  = (gen_topo_module_t *)(context_ptr);
   gen_topo_t *       topo_ptr    = module_ptr->topo_ptr;
   uint32_t           topo_offset = offsetof(spl_cntr_t, topo);
   spl_cntr_t *       me_ptr      = (spl_cntr_t *)(((uint8_t *)topo_ptr) - topo_offset);

   capi_buf_t *                      payload       = &event_info_ptr->payload;
   capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

   switch (dsp_event_ptr->param_id)
   {
      case FWK_EXTN_EVENT_ID_SOFT_TIMER_START:
      {
         if (dsp_event_ptr->payload.actual_data_len < sizeof(fwk_extn_event_id_soft_timer_start_t))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error in callback function. The actual size %lu is less than the required size "
                         "%lu for "
                         "id %lu.",
                         payload->actual_data_len,
                         sizeof(fwk_extn_event_id_soft_timer_start_t),
                         (uint32_t)(dsp_event_ptr->param_id));
            return AR_EBADPARAM;
         }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Handling soft timer start event");
#endif

         fwk_extn_event_id_soft_timer_start_t *data_ptr =
            (fwk_extn_event_id_soft_timer_start_t *)(dsp_event_ptr->payload.data_ptr);
         int64_t duration_us = data_ptr->duration_ms * 1000;

         if (TRUE == module_ptr->flags.need_soft_timer_extn)
         {
            result = cu_fwk_extn_soft_timer_start(&me_ptr->cu, &module_ptr->gu, data_ptr->timer_id, duration_us);
         }
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module %lx doesnt support timer ext",
                         module_ptr->gu.module_instance_id);
         }
         break;
      }
      case FWK_EXTN_EVENT_ID_SOFT_TIMER_DISABLE:
      {
         if (dsp_event_ptr->payload.actual_data_len < sizeof(fwk_extn_event_id_soft_timer_disable_t))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Error in callback function. The actual size %lu is less than the required size "
                         "%lu for "
                         "id %lu.",
                         payload->actual_data_len,
                         sizeof(fwk_extn_event_id_soft_timer_disable_t),
                         (uint32_t)(dsp_event_ptr->param_id));
            return AR_EBADPARAM;
         }

#if SPL_CNTR_DEBUG_LEVEL >= SPL_CNTR_DEBUG_LEVEL_4
         SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id, DBG_MED_PRIO, "Handling soft timer disable event");
#endif

         fwk_extn_event_id_soft_timer_disable_t *data_ptr =
            (fwk_extn_event_id_soft_timer_disable_t *)(dsp_event_ptr->payload.data_ptr);

         if (TRUE == module_ptr->flags.need_soft_timer_extn)
         {
            result = cu_fwk_extn_soft_timer_disable(&me_ptr->cu, &module_ptr->gu, data_ptr->timer_id);
         }
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module %lx doesnt support timer ext",
                         module_ptr->gu.module_instance_id);
         }
         break;
      }
      case FWK_EXTN_SYNC_EVENT_ID_ENABLE_THRESHOLD_BUFFERING:
      {
         TRY(result,
             spl_cntr_fwk_extn_sync_handle_toggle_threshold_buffering_event(me_ptr,
                                                                            (spl_topo_module_t *)module_ptr,
                                                                            payload,
                                                                            dsp_event_ptr));
         break;
      }
      case FWK_EXTN_VOICE_DELIVERY_EVENT_ID_CHANGE_CONTAINER_TRIGGER_POLICY:
      {
         if (dsp_event_ptr->payload.actual_data_len < sizeof(capi_event_change_container_trigger_policy_t))
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_ERROR_PRIO,
                         "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                         "%lu for id %lu.",
                         module_ptr->gu.module_instance_id,
                         payload->actual_data_len,
                         sizeof(capi_event_change_container_trigger_policy_t),
                         (uint32_t)(dsp_event_ptr->param_id));
            return AR_ENEEDMORE;
         }

         capi_event_change_container_trigger_policy_t *container_trigger_policy_ptr =
            (capi_event_change_container_trigger_policy_t *)(dsp_event_ptr->payload.data_ptr);

         if (OUTPUT_BUFFER_TRIGGER == container_trigger_policy_ptr->container_trigger_policy)
         {
            me_ptr->trigger_policy = OUTPUT_BUFFER_TRIGGER_MODE;
         }
         else if (VOICE_TIMER_TRIGGER == container_trigger_policy_ptr->container_trigger_policy)
         {
            me_ptr->trigger_policy = VOICE_PROC_TRIGGER_MODE;
         }
         else
         {
            SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                         DBG_HIGH_PRIO,
                         "Invalid trigger, trigger policy: &d",
                         container_trigger_policy_ptr->container_trigger_policy);
         }

         // Start waiting on anything set in the gpd mask.
         spl_cntr_update_cu_bit_mask(me_ptr);

         break;
      }
      default:
      {
         return cu_handle_event_to_dsp_service_topo_cb(&me_ptr->cu, &module_ptr->gu, event_info_ptr);
      }
   }

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return result;
}

/**
 * Check whether frame done needs to be raised. If so, raise and mark as raised.
 */
ar_result_t spl_cntr_handle_frame_done(spl_cntr_t *me_ptr, uint8_t path_index)
{
   ar_result_t result = AR_EOK;

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = me_ptr->cu.gu_ptr->ext_in_port_list_ptr; ext_in_port_list_ptr;
        LIST_ADVANCE(ext_in_port_list_ptr))
   {
      spl_cntr_ext_in_port_t *ext_in_port_ptr = (spl_cntr_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

      // if external port is in the same path where frame is delivered and threshold is enabled
      if (ext_in_port_ptr->cu.preserve_prebuffer &&
          (path_index == ext_in_port_ptr->gu.int_in_port_ptr->cmn.module_ptr->path_index) &&
          (FALSE == ((gen_topo_input_port_t *)ext_in_port_ptr->gu.int_in_port_ptr)->flags.is_threshold_disabled_prop))
      {
         // requeue prebuffers
         cu_ext_in_requeue_prebuffers(&me_ptr->cu, &ext_in_port_ptr->gu);
         ext_in_port_ptr->cu.preserve_prebuffer = FALSE;
      }
   }

   if (me_ptr->topo.t_base.flags.need_to_handle_frame_done)
   {
      // Raise the frame delivery done event.
      cu_raise_frame_done_event(&me_ptr->cu, me_ptr->topo.t_base.gu.log_id);
   }
   return result;
}

ar_result_t spl_cntr_handle_fwk_events(spl_cntr_t *me_ptr, bool_t is_data_path)
{
   ar_result_t                 result              = AR_EOK;
   gen_topo_capi_event_flag_t *capi_event_flag_ptr = NULL;
   cu_event_flags_t           *fwk_event_flag_ptr  = NULL;

   INIT_EXCEPTION_HANDLING

   GEN_TOPO_CAPI_EVENT_HANDLER_CONTEXT
   CU_FWK_EVENT_HANDLER_CONTEXT

   // no need to reconcile the flags in SC
   GEN_TOPO_GET_CAPI_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(capi_event_flag_ptr,
                                                       &me_ptr->topo.t_base,
                                                       FALSE /*do_reconcile*/);
   CU_GET_FWK_EVENT_FLAG_PTR_FOR_EVENT_HANDLING(fwk_event_flag_ptr, &me_ptr->cu, FALSE /*do_reconcile*/);

   if ((!spl_topo_needs_update_simp_topo_L2_flags(&me_ptr->topo)) && (0 == fwk_event_flag_ptr->word) &&
       (0 == capi_event_flag_ptr->word) && (!me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling))
   {
      return AR_EOK;
   }

   SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                DBG_LOW_PRIO,
                "Handling fwk events: fwk events 0x%lX, capi events 0x%lX, sync pending inactive port flag = %lu",
                fwk_event_flag_ptr->word,
                capi_event_flag_ptr->word,
                me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling);

   bool_t is_sync_inactive_port_event_handled = TRUE;
   if (me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling)
   {
      TRY(result, spl_topo_sync_handle_inactive_port_events(&(me_ptr->topo), &is_sync_inactive_port_event_handled));
   }

   // propagate the media format and update the fractional use case.
   if (capi_event_flag_ptr->media_fmt_event)
   {
      TRY(result, me_ptr->cu.topo_vtbl_ptr->propagate_media_fmt(&me_ptr->topo, is_data_path));
   }

   // some of the event flag might have set in media format propagation and sync inactive port handling.
   if (spl_topo_needs_update_simp_topo_L2_flags(&me_ptr->topo))
   {
      SPL_CNTR_MSG(me_ptr->topo.t_base.gu.log_id,
                   DBG_LOW_PRIO,
                   "Handling simp topo L2 flags events: L1 Flag 0x%lX, L2 Flag 0x%lX, event = 0x%lx",
                   me_ptr->topo.simpt1_flags.word,
                   me_ptr->topo.simpt2_flags.word,
                   me_ptr->topo.simpt_event_flags.word);

      spl_topo_update_simp_topo_L2_flags(&me_ptr->topo);
   }

   /* update threshold
    * 1. media format changed
    * 		to update the threshold samples based on the new media format.
    * 2. threshold changed
    * 3. sg state changed
    * 		to update threshold only from the modules which are in started state.
    */
   if (capi_event_flag_ptr->media_fmt_event || capi_event_flag_ptr->port_thresh || fwk_event_flag_ptr->sg_state_change)
   {
      TRY(result, me_ptr->cu.cntr_vtbl_ptr->port_data_thresh_change(&me_ptr->cu));
   }

   /* If container is started then update the sample requirement based on the new media format, threshold,
    * if container starts or there is a port state change.
    * Since dm query happens only on the ports which are in started(downgraded) state,
    * sg_state_change and port_state_change flags are considered.
    *
    * Port state change should be considered in BT cyclic graph use cases because next container(which is cyclic)
    * is starting AFTER the self container; backward port state propagation happens after the self sg start */
   if ((me_ptr->cu.flags.is_cntr_started) &&
       (fwk_event_flag_ptr->port_state_change || capi_event_flag_ptr->media_fmt_event ||
        capi_event_flag_ptr->port_thresh || fwk_event_flag_ptr->sg_state_change))
   {
      /* Query for next iteration sample requirements from DM modules*/
      TRY(result, spl_cntr_query_topo_req_samples(me_ptr));
   }

   if (capi_event_flag_ptr->realloc_scratch_mem)
   {
      TRY(result, gen_topo_check_n_realloc_scratch_memory(&me_ptr->topo.t_base, FALSE /*is_open_context*/));
   }

   if (capi_event_flag_ptr->port_prop_is_up_strm_rt_change)
   {
      me_ptr->cu.topo_vtbl_ptr->propagate_port_property(&me_ptr->topo.t_base, PORT_PROPERTY_IS_UPSTREAM_RT);
      TRY(result, cu_inform_downstream_about_upstream_property(&me_ptr->cu));
      fwk_event_flag_ptr->rt_ftrt_change = TRUE;
   }

   if (capi_event_flag_ptr->port_prop_is_down_strm_rt_change)
   {
      me_ptr->cu.topo_vtbl_ptr->propagate_port_property(&me_ptr->topo.t_base, PORT_PROPERTY_IS_DOWNSTREAM_RT);
      TRY(result, cu_inform_upstream_about_downstream_property(&me_ptr->cu));
      fwk_event_flag_ptr->rt_ftrt_change = TRUE;
   }

   if (fwk_event_flag_ptr->rt_ftrt_change)
   {
      cu_is_realtime(&me_ptr->cu);
   }

   /* update votes
    * 1. if module has updated votes
    * 2. sg and port state change
    * 		to exclude module from stopped/suspended SGs
    * 3. media format is changes
    * 		to update container votes.
    * */

   if (capi_event_flag_ptr->bw || capi_event_flag_ptr->kpps || fwk_event_flag_ptr->port_state_change ||
       fwk_event_flag_ptr->sg_state_change || capi_event_flag_ptr->media_fmt_event ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_island_exit)
   {
      cu_pm_vote_type_t vote_type;
      bool_t            is_release = (!me_ptr->cu.flags.is_cntr_started);
      if (fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry)
      {
         vote_type = CU_PM_REL_KPPS_ONLY;
      }
      else
      {
         vote_type = is_release ? CU_PM_REL_KPPS_BW : CU_PM_REQ_KPPS_BW;
      }

      spl_cntr_update_cntr_kpps_bw(me_ptr, TRUE /*force_aggregate*/);
      spl_cntr_handle_clk_vote_change(me_ptr, vote_type, FALSE, FALSE /*force_aggregate*/, NULL, NULL);
   }

   if (me_ptr->cu.flags.is_cntr_started)
   {
      // Ext inputs reports additional delay depending upon the US frame leng and US RT,
      // hence path delay is updated if one of them changes.
      if (fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->proc_dur_change ||
          capi_event_flag_ptr->algo_delay_event || fwk_event_flag_ptr->upstream_frame_len_change ||
          fwk_event_flag_ptr->rt_ftrt_change)
      {
         cu_update_path_delay(&me_ptr->cu, CU_PATH_ID_ALL_PATHS);
      }

      // the voice flags are only to track changes on the steady state
      if (me_ptr->cu.voice_info_ptr)
      {
         me_ptr->cu.voice_info_ptr->event_flags.did_frame_size_change = fwk_event_flag_ptr->frame_len_change;
      }
   }

   // update thread priority if proc duration changes for container run state is changed.
   if (fwk_event_flag_ptr->frame_len_change || fwk_event_flag_ptr->proc_dur_change ||
       fwk_event_flag_ptr->cntr_run_state_change || fwk_event_flag_ptr->rt_ftrt_change ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry ||
       fwk_event_flag_ptr->need_to_handle_dcm_req_for_island_exit)
   {
      bool_t is_at_least_one_sg_started = !me_ptr->cu.flags.is_cntr_started;
      if (fwk_event_flag_ptr->need_to_handle_dcm_req_for_unblock_island_entry)
      {
         is_at_least_one_sg_started = TRUE;
      }

      spl_cntr_set_thread_priority(me_ptr);
      cu_vote_latency(&me_ptr->cu, is_at_least_one_sg_started, me_ptr->cu.flags.is_real_time);
   }

   // send updated proc duration to the modules.
   if (fwk_event_flag_ptr->proc_dur_change)
   {
      spl_cntr_fwk_extn_set_cntr_proc_duration(me_ptr, me_ptr->cu.cntr_proc_duration);
   }

   if (me_ptr->cu.voice_info_ptr && capi_event_flag_ptr->hw_acc_proc_delay_event)
   {
      me_ptr->cu.voice_info_ptr->event_flags.did_hw_acc_proc_delay_change =
         capi_event_flag_ptr->hw_acc_proc_delay_event;
   }

   // process_state: disabled modules can be removed from the simplified topo
   // algo delay & media_fmt: mimo modules which have suggested disabled can be removed if they have same media format
   // on input-output port and zero algo delay.
   if (capi_event_flag_ptr->process_state || me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling ||
       capi_event_flag_ptr->algo_delay_event || capi_event_flag_ptr->media_fmt_event)
   {
      spl_topo_update_simp_module_connections(&me_ptr->topo);
   }

   capi_event_flag_ptr->word                                      = 0;
   fwk_event_flag_ptr->word                                       = 0;
   me_ptr->topo.fwk_extn_info.sync_extn_pending_inactive_handling = !is_sync_inactive_port_event_handled;

   CATCH(result, SPL_CNTR_MSG_PREFIX, me_ptr->topo.t_base.gu.log_id)
   {
   }

   return AR_EOK;
}
