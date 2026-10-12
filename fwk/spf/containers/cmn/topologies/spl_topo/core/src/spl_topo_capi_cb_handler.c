/**
 * \file spl_topo_capi_cb_handler.c
 *
 * \brief
 *
 *     Implementation of the callback function from the capi to the topo layer.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spl_topo_i.h"
/* =======================================================================
Static Function Declarations
========================================================================== */

static capi_err_t spl_topo_handle_process_state_event(spl_topo_t *       topo_ptr,
                                                      spl_topo_module_t *module_ptr,
                                                      capi_event_info_t *event_info_ptr);

/* =======================================================================
Function Definitions
========================================================================== */

/**
 * Handler for the capi output media fmt event callback. If there is any data
 * in any of the module's output buffers, we can't accept the media format event
 * because that data would not be in the new output media format. CAPIs need to
 * handle this failure by exiting process, and then trying to raise output media
 * format again on the next process() call.
 */
capi_err_t spl_topo_handle_output_media_format_event(void *             ctx_ptr,
                                                     void *             module_ctxt_ptr,
                                                     capi_event_info_t *event_info_ptr,
                                                     bool_t             is_std_fmt_v2,
                                                     bool_t             is_pending_data_valid)
{
   spl_topo_t *            topo_ptr   = (spl_topo_t *)ctx_ptr;
   spl_topo_module_t *     module_ptr = (spl_topo_module_t *)module_ctxt_ptr;
   capi_err_t              result     = CAPI_EOK;
   uint32_t                port_ind   = event_info_ptr->port_info.port_index;
   spl_topo_output_port_t *event_out_port_ptr =
      (spl_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->t_base.gu, port_ind);
   bool_t module_disabled = spl_topo_is_module_disabled(module_ptr);

   if (NULL == event_out_port_ptr)
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: Module 0x%lX doesnt have output port, but module raised output media format",
               module_ptr->t_base.gu.module_instance_id);
      return result;
   }

   // If we are in topo_process, we need to fail if we get this event if there is data in
   // the output buffer (note - that data will be at the old media fmt) because the capi
   // might be about to write data in the output buffer in the new media fmt. If we come from
   // any other place, the capi will not be writing any data and thus we won't run into a situation
   // where there is some data at both media formats in one buffer.
   if (TOPO_CMD_STATE_PROCESS == topo_ptr->cmd_state)
   {
      if (module_disabled)
      {
         TOPO_MSG(topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Error Module 0x%lX: Module is disabled but raised output "
                  "media format event from process (How was module process "
                  "called?)");
         return CAPI_EFAILED;
      }

      for (gu_output_port_list_t *out_port_list_ptr = module_ptr->t_base.gu.output_port_list_ptr; out_port_list_ptr;
           LIST_ADVANCE(out_port_list_ptr))
      {
         spl_topo_output_port_t *out_port_ptr = (spl_topo_output_port_t *)out_port_list_ptr->op_port_ptr;
         uint32_t                out_data_len = spl_topo_get_out_port_actual_data_len(topo_ptr, out_port_ptr);
         if (0 != out_data_len)
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_HIGH_PRIO,
                     "Warning Module 0x%lX: Output media format event raised in process context while there's data in "
                     "output port id %lx, actual data len %ld. This is fine as long as the module doesn't output data "
                     "on this process call",
                     module_ptr->t_base.gu.module_instance_id,
                     out_port_ptr->t_base.gu.cmn.id,
                     out_data_len);
         }
      }

      // Need to set this here in case module raised MF same as before: this could prevent data from being processed
      // but the flag will get cleared during common output media format handling. TRUE allows us to retry processing
      // so the module can consume data.
      topo_ptr->proc_info.state_changed_flags.mf_moved = TRUE;
   }

   result = gen_topo_handle_output_media_format_event(&topo_ptr->t_base,
                                                      &module_ptr->t_base,
                                                      event_info_ptr,
                                                      is_std_fmt_v2,
                                                      is_pending_data_valid);
   if (CAPI_FAILED(result))
   {
      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Output media format event failed",
               module_ptr->t_base.gu.module_instance_id);

      return result;
   }

   spl_topo_update_check_valid_mf_event_flag(topo_ptr,
                                              &event_out_port_ptr->t_base.gu.cmn,
                                              event_out_port_ptr->t_base.common.flags.is_mf_valid);

   topo_ptr->simpt_event_flags.check_pending_mf = TRUE;
   topo_ptr->simpt1_flags.backwards_kick       = TRUE;

   spl_topo_update_max_buf_len_for_single_module(topo_ptr, module_ptr);

   if (!module_disabled)
   {
      // if the media format was raised by a dm module, query the max samples since it may not be set yet
      // or may have changed. if the dm mode is not set yet, the query will happen when the dm mode gets set.
      if (spl_topo_fwk_ext_is_dm_enabled(module_ptr) && (GEN_TOPO_DM_FIXED_OUTPUT_MODE == module_ptr->t_base.flags.dm_mode))
      {
         bool_t IS_MAX_TRUE = TRUE;
         if (AR_DID_FAIL(spl_topo_get_required_input_samples(topo_ptr, IS_MAX_TRUE)))
         {
            TOPO_MSG(topo_ptr->t_base.gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Output media format event failed due to DM max samples query failure",
                     module_ptr->t_base.gu.module_instance_id);

            return CAPI_EFAILED;
         }
      }
   }

   return CAPI_EOK;
}

/**
 * The spl_topo handling of process state checks if process state disabled the module.
 * If so, then we propagate media format downstream starting from the module after
 * the current module in the sorted list. We have to do this because the current module
 * might have changed the media format, and now that it is disabled downstream modules
 * need to use the upstream media format.
 */
static capi_err_t spl_topo_handle_process_state_event(spl_topo_t *       topo_ptr,
                                                      spl_topo_module_t *module_ptr,
                                                      capi_event_info_t *event_info_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                 ar_result    = AR_EOK;
   capi_buf_t *                payload      = &event_info_ptr->payload;
   capi_event_process_state_t *data_ptr     = (capi_event_process_state_t *)payload->data_ptr;
   bool_t                      new_disabled = FALSE;

   if (TOPO_CMD_STATE_PROCESS == topo_ptr->cmd_state)
   {
      topo_ptr->proc_info.state_changed_flags.event_raised = TRUE;
   }

   if (module_ptr->t_base.flags.disabled != (!data_ptr->is_enabled))
   {
      module_ptr->t_base.flags.disabled = !data_ptr->is_enabled;

      GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&topo_ptr->t_base, process_state);
      topo_ptr->simpt1_flags.backwards_kick = TRUE;

      TOPO_MSG(topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               SPF_LOG_PREFIX "Module 0x%lX: Module process state set to %lu",
               module_ptr->t_base.gu.module_instance_id,
               (uint32_t)(data_ptr->is_enabled));
   }

   new_disabled = spl_topo_is_module_disabled(module_ptr);

   TRY(ar_result, spl_topo_check_update_bypass_module(topo_ptr, module_ptr, new_disabled));

   CATCH(ar_result, TOPO_MSG_PREFIX, topo_ptr->t_base.gu.log_id)
   {
   }

   return ar_result_to_capi_err(ar_result);
}

/**
 * Send the threshold change event to the framework for processing.
 */
static capi_err_t spl_topo_handle_port_data_threshold_change_event(spl_topo_t *       spl_topo_ptr,
                                                                   spl_topo_module_t *module_ptr,
                                                                   capi_event_info_t *event_info_ptr)
{
   ar_result_t result = AR_EOK;

   if (!event_info_ptr->port_info.is_valid)
   {
      TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
               DBG_ERROR_PRIO,
               "Module 0x%lX: Error in callback function. port is not valid ",
               module_ptr->t_base.gu.module_instance_id);
      return CAPI_EBADPARAM;
   }

   bool_t                  is_input_port       = event_info_ptr->port_info.is_input_port;
   uint32_t                port_index          = event_info_ptr->port_info.port_index;
   gen_topo_common_port_t *thresh_cmn_port_ptr = NULL;

   capi_buf_t *payload_ptr = &event_info_ptr->payload;

   capi_port_data_threshold_change_t *data_ptr = (capi_port_data_threshold_change_t *)(payload_ptr->data_ptr);
   uint32_t new_threshold = (data_ptr->new_threshold_in_bytes > 1) ? data_ptr->new_threshold_in_bytes : 0;

   // Modules should not raise 0 for threshold.
   if (!(data_ptr->new_threshold_in_bytes))
   {
      TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Warning: port is_input %lu idx %ld miid 0x%lx raised threshold is 0. Ignoring.",
               is_input_port,
               port_index,
               module_ptr->t_base.gu.module_instance_id);
      return AR_EOK;
   }

   if (1 == data_ptr->new_threshold_in_bytes)
   {
      if (module_ptr->threshold_data.is_threshold_module)
      {
         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Port is_input %lu idx %ld miid 0x%lx raised threshold is 1, previously raised threshold > 1. "
                  "Not yet "
                  "suppported.",
                  is_input_port,
                  port_index,
                  module_ptr->t_base.gu.module_instance_id);
         return AR_EFAILED;
      }

      TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
               DBG_HIGH_PRIO,
               "Port is_input %lu idx %ld miid 0x%lx raised threshold is 1, meaning no threshold. Ignoring.",
               is_input_port,
               port_index,
               module_ptr->t_base.gu.module_instance_id);
      return AR_EOK;
   }

   // Send the input or output port to the fwk.
   if (event_info_ptr->port_info.is_input_port)
   {
      gen_topo_input_port_t *in_port_ptr =
         (gen_topo_input_port_t *)gu_find_input_port_by_index(&module_ptr->t_base.gu,
                                                              event_info_ptr->port_info.port_index);

      if (!in_port_ptr)
      {
         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Input port index 0x%lx not present",
                  module_ptr->t_base.gu.module_instance_id,
                  event_info_ptr->port_info.port_index);
         return CAPI_EFAILED;
      }

      if (module_ptr->t_base.bypass_ptr)
      {
         module_ptr->t_base.bypass_ptr->in_thresh_bytes_all_ch = new_threshold;

         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Input port id 0x%lx event with new port-threshold = %lu raised while module is "
                  "bypassed, caching to handle later.",
                  module_ptr->t_base.gu.module_instance_id,
                  in_port_ptr->gu.cmn.id,
                  data_ptr->new_threshold_in_bytes);
      }
      else
      {
         thresh_cmn_port_ptr = &in_port_ptr->common;
      }
   }
   else
   {
      gen_topo_output_port_t *out_port_ptr =
         (gen_topo_output_port_t *)gu_find_output_port_by_index(&module_ptr->t_base.gu,
                                                                event_info_ptr->port_info.port_index);

      if (!out_port_ptr)
      {
         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Output port index %lu not present",
                  module_ptr->t_base.gu.module_instance_id,
                  event_info_ptr->port_info.port_index);
         return CAPI_EFAILED;
      }

      if (module_ptr->t_base.bypass_ptr)
      {
         module_ptr->t_base.bypass_ptr->out_thresh_bytes_all_ch = new_threshold;

         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Module 0x%lX: Output port id 0x%lx event with new port-threshold = %lu raised while module is "
                  "bypassed, caching to handle later.",
                  module_ptr->t_base.gu.module_instance_id,
                  out_port_ptr->gu.cmn.id,
                  data_ptr->new_threshold_in_bytes);
      }
      else
      {
         thresh_cmn_port_ptr = &out_port_ptr->common;
      }
   }

   if (thresh_cmn_port_ptr)
   {
      // The module shouldn't be raising a threshold if the port's media format isn't valid. How would the module
      // know how many bytes to raise?
      if (!thresh_cmn_port_ptr->flags.is_mf_valid)
      {
         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Warning: port is_input %ld idx %ld miid 0x%lx raised threshold when media format is not valid. "
                  "Ignoring.",
                  is_input_port,
                  port_index,
                  module_ptr->t_base.gu.module_instance_id);
         return AR_EOK;
      }

      /** Mark threshold event asap */
      if (thresh_cmn_port_ptr->threshold_raised != new_threshold)
      {
         thresh_cmn_port_ptr->threshold_raised         = new_threshold;
         thresh_cmn_port_ptr->flags.port_has_threshold = (thresh_cmn_port_ptr->threshold_raised) ? TRUE : FALSE;

         if (!gen_topo_is_module_sg_stopped_or_suspended(&module_ptr->t_base))
         {
            GEN_TOPO_SET_ONE_CAPI_EVENT_FLAG(&spl_topo_ptr->t_base, port_thresh);
         }

         TOPO_MSG(spl_topo_ptr->t_base.gu.log_id,
                  DBG_HIGH_PRIO,
                  "Port is_input %lu idx %ld miid 0x%lx raised threshold in_bytes: %lu.",
                  is_input_port,
                  port_index,
                  module_ptr->t_base.gu.module_instance_id,
                  data_ptr->new_threshold_in_bytes);
      }
   }
   return result;
}

/**
 * Callback for handling capi v2 events. Delegates handling to event-specific
 * handler functions.
 */
capi_err_t spl_topo_capi_callback(void *context_ptr, capi_event_id_t id, capi_event_info_t *event_info_ptr)
{
   capi_err_t result = CAPI_EOK;

   if (!context_ptr || !event_info_ptr)
   {
      return CAPI_EFAILED;
   }

   capi_buf_t *       payload    = &event_info_ptr->payload;
   gen_topo_module_t *module_ptr = (gen_topo_module_t *)(context_ptr);
   gen_topo_t *       topo_ptr   = module_ptr->topo_ptr;

   spl_topo_t *       spl_topo_ptr        = (spl_topo_t *)topo_ptr;
   spl_topo_module_t *spl_topo_module_ptr = (spl_topo_module_t *)module_ptr;

   switch (id)
   {
      case CAPI_EVENT_KPPS:
      case CAPI_EVENT_BANDWIDTH:
      case CAPI_EVENT_METADATA_AVAILABLE:
      case CAPI_EVENT_GET_LIBRARY_INSTANCE:
      case CAPI_EVENT_GET_DLINFO:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT:
      case CAPI_EVENT_DATA_TO_DSP_CLIENT_V2:
      case CAPI_EVENT_ALGORITHMIC_DELAY:
      case CAPI_EVENT_GET_DATA_FROM_DSP_SERVICE:
      case CAPI_EVENT_HW_ACCL_PROC_DELAY:
      case CAPI_EVENT_DEINTERLEAVED_UNPACKED_V2_SUPPORTED:
      {
         result = gen_topo_capi_callback_base(module_ptr, id, event_info_ptr);
         if (CAPI_FAILED(result))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. Error in common callback "
                     "handling, err code: 0x%lx",
                     module_ptr->gu.module_instance_id,
                     result);
         }
         return result;
      }
      case CAPI_EVENT_PROCESS_STATE:
      {
         if (payload->actual_data_len < sizeof(capi_event_process_state_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for "
                     "id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_process_state_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }
         return spl_topo_handle_process_state_event(spl_topo_ptr, spl_topo_module_ptr, event_info_ptr);
      }
      case CAPI_EVENT_DATA_TO_DSP_SERVICE:
      {
         if (payload->actual_data_len < sizeof(capi_event_data_to_dsp_service_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%lu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_event_data_to_dsp_service_t),
                     (uint32_t)(id));
            return AR_ENEEDMORE;
         }
         // Some events can be handled within topo itself
         capi_buf_t *                      payload       = &event_info_ptr->payload;
         capi_event_data_to_dsp_service_t *dsp_event_ptr = (capi_event_data_to_dsp_service_t *)(payload->data_ptr);

         switch (dsp_event_ptr->param_id)
         {
            case FWK_EXTN_DM_EVENT_ID_REPORT_SAMPLES:
            case FWK_EXTN_DM_EVENT_ID_REPORT_MAX_SAMPLES:
            {
               bool_t is_max = (FWK_EXTN_DM_EVENT_ID_REPORT_SAMPLES == dsp_event_ptr->param_id) ? FALSE : TRUE;
               result        = spl_topo_fwk_ext_handle_dm_report_samples_event(spl_topo_ptr,
                                                                        spl_topo_module_ptr,
                                                                        dsp_event_ptr,
                                                                        is_max);
               break;
            }
            case FWK_EXTN_DM_EVENT_ID_DISABLE_DM:
            {
               result = spl_topo_fwk_ext_handle_dm_disable_event(spl_topo_ptr, spl_topo_module_ptr, dsp_event_ptr);
               break;
            }
            case FWK_EXTN_EVENT_ID_DATA_TRIGGER_IN_ST_CNTR:
            {
               result = gen_topo_trigger_policy_event_data_trigger_in_st_cntr(&spl_topo_module_ptr->t_base,
                                                                              &dsp_event_ptr->payload);
               break;
            }
            case INTF_EXTN_EVENT_ID_MIMO_MODULE_PROCESS_STATE:
            {
               result = spl_topo_intf_extn_mimo_module_process_state_handle_event(spl_topo_ptr,
                                                                                  spl_topo_module_ptr,
                                                                                  dsp_event_ptr);
               break;
            }
            case INTF_EXTN_EVENT_ID_BLOCK_PORT_DS_STATE_PROP:
            case INTF_EXTN_EVENT_ID_PORT_DS_STATE:
            case INTF_EXTN_EVENT_ID_IS_RT_PORT_PROPERTY:
            {
               result = gen_topo_handle_port_propagated_capi_event(topo_ptr, module_ptr, event_info_ptr);
               break;
            }
            case FWK_EXTN_SYNC_EVENT_ID_DATA_PORT_ACTIVITY_STATE:
            {
               result = spl_topo_handle_data_port_activity_sync_event_cb(spl_topo_ptr, spl_topo_module_ptr, &dsp_event_ptr->payload);
               break;
            }
            case FWK_EXTN_EVENT_ID_IPC_DATA_LINK_INFO:
            {
               if (topo_ptr->topo_to_cntr_vtable_ptr->handle_ipc_data_link_info_event)
               {
                  result = topo_ptr->topo_to_cntr_vtable_ptr->handle_ipc_data_link_info_event(topo_ptr,
                                                                                              module_ptr,
                                                                                              event_info_ptr);
               }
               else
               {
                  return CAPI_EUNSUPPORTED;
               }
               break;
            }
            default:
            {
               result = spl_topo_ptr->t_base.topo_to_cntr_vtable_ptr->raise_data_to_dsp_service_event(module_ptr,
                                                                                                      event_info_ptr);
            }
         }

         result = ar_result_to_capi_err(result);
         break;
      }
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED:
      {
         if (payload->actual_data_len < sizeof(capi_set_get_media_format_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for "
                     "id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_set_get_media_format_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         return spl_topo_handle_output_media_format_event(spl_topo_ptr,
                                                          spl_topo_module_ptr,
                                                          event_info_ptr,
                                                          FALSE,
                                                          TRUE);
      }
      case CAPI_EVENT_OUTPUT_MEDIA_FORMAT_UPDATED_V2:
      {
         if (payload->actual_data_len < sizeof(capi_set_get_media_format_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for "
                     "id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_set_get_media_format_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         return spl_topo_handle_output_media_format_event(spl_topo_ptr,
                                                          spl_topo_module_ptr,
                                                          event_info_ptr,
                                                          TRUE,
                                                          TRUE);
      }
      case CAPI_EVENT_PORT_DATA_THRESHOLD_CHANGE:
      {
         if (payload->actual_data_len < sizeof(capi_port_data_threshold_change_t))
         {
            TOPO_MSG(topo_ptr->gu.log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: Error in callback function. The actual size %lu is less than the required size "
                     "%zu for id %lu.",
                     module_ptr->gu.module_instance_id,
                     payload->actual_data_len,
                     sizeof(capi_port_data_threshold_change_t),
                     (uint32_t)(id));
            return CAPI_ENEEDMORE;
         }

         result = spl_topo_handle_port_data_threshold_change_event(spl_topo_ptr, spl_topo_module_ptr, event_info_ptr);

         break;
      }
      case CAPI_EVENT_ISLAND_VOTE:
      case CAPI_EVENT_DYNAMIC_INPLACE_CHANGE:
      {
         // These events are only supported by GC, but we don't need to print any error message from SC.
         return CAPI_EUNSUPPORTED;
      }
      default:
      {
         TOPO_MSG(topo_ptr->gu.log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Error in callback function. ID %lu not supported.",
                  module_ptr->gu.module_instance_id,
                  (uint32_t)(id));
         return CAPI_EUNSUPPORTED;
      }
   }

   return result;
}
