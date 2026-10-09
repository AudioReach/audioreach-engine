
/**
@file irm_cmd_handler.cpp

@brief Command handler file for Integrated Resource Monitor (IRM).

================================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause
==============================================================================*/
// clang-format off
// clang-format on

#include "irm_api.h"
#include "irm_i.h"
#include "irm_sysmon_util.h"
#include "spf_list_utils.h"
#include "spf_macros.h"
#include "spf_svc_calib.h"
#include "irm_prev_metric_info.h"
#include "apm_internal_if.h"
#include "irm_cntr_if.h"
#include "posal_mem_prof.h"
#include "apm_offload_pd_info.h"
#include "private_irm_api.h"
#include "irm_offload_utils.h"

/* Constant value of DDR band-width vote used to restrict island entry. Units is bytes per sec */
#define IRM_AUDIO_BW_VOTE_BYTES_PER_SEC (1024 * 1024)

static ar_result_t irm_get_memory_stats(spf_msg_t *msg_ptr, uint32_t rsp_opcode);
static ar_result_t irm_handle_enable_all(irm_t *irm_ptr);
static ar_result_t irm_fill_enable_all_metric_tree(irm_t                 *irm_ptr,
                                                   param_id_enable_all_t *irm_enable_all_params,
                                                   uint32_t               param_size);
static bool_t      irm_is_valid_block_id(uint32_t block_id);

/*----------------------------------------------------------------------------------------------------------------------
 Handles APM command sent to IRM static module
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_cmdq_apm_cmd_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t       result            = AR_EOK;
   spf_msg_header_t *msg_header_ptr    = (spf_msg_header_t *)msg_ptr->payload_ptr;
   bool_t            is_offload_needed = FALSE;
   uint32_t          dest_domain_id;
   irm_cmd_ctrl_t   *curr_cmd_ctrl_ptr = NULL;

   if (msg_header_ptr->payload_size < sizeof(spf_msg_cmd_param_data_cfg_t))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "IRM: irm_cmdq_apm_cmd_handler(): Invalid payload size %d Minimum size expected %d",
             msg_header_ptr->payload_size,
             sizeof(spf_msg_cmd_param_data_cfg_t));
      result = spf_msg_ack_msg(msg_ptr, AR_EBADPARAM);
      return result;
   }

   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   apm_module_param_data_t     **param_data_pptr    = (apm_module_param_data_t **)param_data_cfg_ptr->param_data_pptr;

   for (uint32_t i = 0; i < param_data_cfg_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)param_data_pptr[i];

      if (SPF_MSG_CMD_SET_CFG == msg_ptr->msg_opcode)
      {
         result |= irm_set_cfg(irm_ptr, param_data_ptr, &is_offload_needed, &dest_domain_id);
         if (is_offload_needed)
         {
            result = irm_route_apm_cmd_to_satellite(irm_ptr, msg_ptr, dest_domain_id);
            return result;
         }
      }
      else if (SPF_MSG_CMD_GET_CFG == msg_ptr->msg_opcode)
      {
         result |= irm_get_cfg(irm_ptr, param_data_ptr, msg_ptr, &curr_cmd_ctrl_ptr);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO, "IRM: irm_process_cmd_q(): Wrong CMD recieved 0x%lx", msg_ptr->msg_opcode);
      }
   }

   if (!irm_offload_any_cmd_pending(irm_ptr))
   {
      result = spf_msg_ack_msg(msg_ptr, result);
   }
   return result;
}
/*----------------------------------------------------------------------------------------------------------------------
 Handles GPR command sent to IRM static module
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_cmdq_gpr_cmd_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      cmd_opcode;
   bool_t        free_gpr_pkt_flag = FALSE;
   bool_t        end_gpr_pkt_flag  = TRUE;

   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "irm_cmdq_gpr_cmd_handler(): msg_ptr is NULL");
      return AR_EBADPARAM;
   }

   // Get the pointer to GPR command
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   if (NULL == gpr_pkt_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "irm_cmdq_gpr_cmd_handler(): Received NULL payload ptr");
      return AR_EBADPARAM;
   }

   // Get the GPR command opcode
   cmd_opcode = gpr_pkt_ptr->opcode;

   switch (cmd_opcode)
   {
      case APM_CMD_SET_CFG:
      case APM_CMD_GET_CFG:
      {
         apm_module_param_data_t *module_data_ptr   = NULL;
         apm_cmd_header_t        *cmd_header_ptr    = NULL;
         uint8_t                 *cmd_payload_ptr   = NULL;
         gpr_packet_t            *gpr_rsp_pkt_ptr   = NULL;
         uint32_t                 alignment_size    = 0;
         bool_t                   is_offload_needed = FALSE;
         bool_t                   is_oob            = FALSE;
         bool_t                   IS_SET            = (cmd_opcode == APM_CMD_SET_CFG);
         uint32_t                 dest_domain_id;
         // Get command header pointer
         cmd_header_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, gpr_pkt_ptr);
         is_oob         = (0 != cmd_header_ptr->mem_map_handle);
         TRY(result,
             spf_svc_get_cmd_payload_addr(IRM_MODULE_INSTANCE_ID,
                                          gpr_pkt_ptr,
                                          &gpr_rsp_pkt_ptr,
                                          (uint8_t **)&cmd_payload_ptr,
                                          &alignment_size,
                                          NULL,
                                          apm_get_mem_map_client()));

         // validate in-band mode
         // VERIFY(result, 0 == cmd_header_ptr->mem_map_handle);

         // Validate if the payload is present
         VERIFY(result, 0 != cmd_header_ptr->payload_size);
         VERIFY(result, NULL != cmd_payload_ptr);

         // Get the module param data pointer
         module_data_ptr = (apm_module_param_data_t *)cmd_payload_ptr;

         // validate module instance ID
         VERIFY(result, module_data_ptr->module_instance_id == IRM_MODULE_INSTANCE_ID);

         result = irm_set_get_cfg_handler(irm_ptr,
                                          cmd_payload_ptr,
                                          cmd_header_ptr->payload_size,
                                          IS_SET,
                                          &is_offload_needed,
                                          &dest_domain_id,
                                          msg_ptr);

         if (is_offload_needed)
         {
            TRY(result, irm_route_cmd_to_satellite(irm_ptr, msg_ptr, dest_domain_id, cmd_payload_ptr));
            if (AR_EOK != result)
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "IRM: Failed to send CMD Opcode 0x%lx to satellite domain ID %lu with %lu",
                      gpr_pkt_ptr->opcode,
                      dest_domain_id,
                      result);
               return __gpr_cmd_end_command(gpr_pkt_ptr, result); // failure
            }
            return result;
         }

         if (!IS_SET && !irm_offload_any_cmd_pending(irm_ptr))
         {

            result            = irm_send_get_cfg_gpr_resp(gpr_rsp_pkt_ptr,
                                           gpr_pkt_ptr,
                                           cmd_header_ptr->payload_size,
                                           cmd_payload_ptr,
                                           is_oob);
            free_gpr_pkt_flag = TRUE;
            end_gpr_pkt_flag  = FALSE;
         }
         else
         {
            if (is_oob)
            {
               posal_cache_flush_v2((posal_mem_addr_t)cmd_payload_ptr, cmd_header_ptr->payload_size);
            }
         }

         break;
      }
      case APM_CMD_REGISTER_MODULE_EVENTS:
      {
         irm_process_register_module_event(irm_ptr, gpr_pkt_ptr);
         break;
      }
      case IRM_CMD_GET_MEMORY_STATS:
      {
         result           = irm_get_memory_stats(msg_ptr, IRM_CMD_RSP_GET_MEMORY_STATS);
         end_gpr_pkt_flag = FALSE;
         break;
      }
      case IRM_CMD_RESET_PEAK_HEAP_USE:
      {
         posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].peak_heap = 0;
         posal_globalstate.non_avs_stats.peak_heap                       = 0;
         break;
      }

      default:
      {
         result = AR_EUNSUPPORTED;
         AR_MSG(DBG_HIGH_PRIO, "irm_cmdq_gpr_cmd_handler(): Unsupported cmd/msg opcode: 0x%8lX", cmd_opcode);
         break;
      }
   }

   CATCH(result, IRM_MSG_PREFIX)
   {
   }
   // End the GPR command when result is not AR_EOK
   // When result is AR_EOK, original packet is freed, and response packet is sent
   if (free_gpr_pkt_flag)
   {
      // Just free the gpr packet, in case of get param and OOB, separate packet will be sent as response
      __gpr_cmd_free(gpr_pkt_ptr);
   }
   if (end_gpr_pkt_flag && !irm_offload_any_cmd_pending(irm_ptr))
   {
      __gpr_cmd_end_command(gpr_pkt_ptr, result);
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles GPR command response sent to IRM static module
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_rspq_gpr_rsp_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      rsp_opcode;

   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Invalid param");
      return AR_EFAILED;
   }

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   rsp_opcode = gpr_pkt_ptr->opcode;

   switch (rsp_opcode)
   {
      case GPR_IBASIC_RSP_RESULT:
      {
         // handle basic responses from the Satellite - should get here only on the master
         result = irm_route_basic_rsp_to_client(irm_ptr, msg_ptr);
         if (AR_EOK != result)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to send basic response to client with %lu", result);
         }
         break;
      }
      case APM_CMD_RSP_GET_CFG:
      {
         result = irm_route_get_cfg_rsp_to_client(irm_ptr, msg_ptr);
         if (AR_EOK != result)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to send get-cfg response to client with %lu", result);
         }
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "IRM: Unsupported response opcode 0x%lx", rsp_opcode);
         result = AR_EUNSUPPORTED;
         __gpr_cmd_end_command(msg_ptr->payload_ptr, AR_EUNSUPPORTED);
      }
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles SPF command response sent to IRM static module
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_rspq_spf_rsp_handler(irm_t *irm_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t result = AR_EOK;

   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Invalid param");
      return AR_EFAILED;
   }

   spf_msg_header_t *msg_header_ptr = (spf_msg_header_t *)msg_ptr->payload_ptr;
   if (msg_header_ptr->payload_size < sizeof(spf_msg_cmd_param_data_cfg_t))
   {
      return AR_EFAILED;
   }
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   apm_module_param_data_t     **param_data_pptr    = (apm_module_param_data_t **)param_data_cfg_ptr->param_data_pptr;

   for (uint32_t i = 0; i < param_data_cfg_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)param_data_pptr[i];
      switch (param_data_ptr->param_id)
      {
         case APM_PARAM_ID_GET_CNTR_HANDLES:
         case APM_PARAM_ID_GET_ALL_CNTR_HANDLES:
         {
            result |= irm_parse_instance_handle_rsp(irm_ptr, param_data_ptr);
            // If enable all is set, handle this when the container handles are set on graph open/when instance handles
            // are recieved
            if (irm_ptr->enable_all)
            {
               result = irm_handle_enable_all(irm_ptr);
            }
            break;
         }
         case CNTR_PARAM_ID_GET_PROF_INFO:
         {
            result |= irm_parse_cntr_rsp(irm_ptr, param_data_ptr);
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "irm_rspq_spf_rsp_handler(), Rcvd un-supported param id: 0x%8lX",
                   param_data_ptr->param_id);
            break;
         }
      }
   }

   result |= spf_msg_return_msg(msg_ptr);
   return result;
}
/*----------------------------------------------------------------------------------------------------------------------
 Handles timer tick event
 1. Get the relevant data to the report payload
 2. Creates log buffer and copies report payload data to the log buffer
 3. Commits the log data to diag
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_timer_tick_handler(irm_t *irm_ptr)
{
   ar_result_t result                     = AR_EOK;
   uint64_t    current_time_ms            = posal_timer_get_time_in_msec();
   uint32_t    frame_size_ms              = (uint32_t)(current_time_ms - irm_ptr->core.previous_timer_tick_time);
   irm_ptr->core.previous_timer_tick_time = current_time_ms;

   if (frame_size_ms != irm_ptr->core.profiling_period_us / 1000)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "IRM: WARNING: Jitter detected in IRM thread, configured period ms = %lu, current period ms = %lu",
             irm_ptr->core.profiling_period_us,
             frame_size_ms);
   }

   result                           = irm_collect_and_fill_info(irm_ptr, frame_size_ms);
   uint64_t post_collection_time_ms = posal_timer_get_time_in_msec();

   if (0 != (post_collection_time_ms - current_time_ms))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "IRM: WARNING: IRM taking more than 1ms to collect statistics, time in ms, before = %lu, after = %lu",
             current_time_ms,
             post_collection_time_ms);
   }

   irm_ptr->core.timer_tick_counter++;
   if (irm_ptr->core.timer_tick_counter == irm_ptr->core.num_profiles_per_report)
   {
      if (AR_EOK != irm_send_rtm_packet(irm_ptr))
      {
         AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to send RTM PACKET");
      }
      irm_reset_report_payload(irm_ptr);
      irm_ptr->core.timer_tick_counter = 0;
   }
   AR_MSG(DBG_HIGH_PRIO, "IRM: Report payload timer tick, timer_tick_counter = %ld", irm_ptr->core.timer_tick_counter);

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles set and get config command sent to IRM module
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_set_get_cfg_handler(irm_t     *irm_ptr,
                                    uint8_t   *data_ptr,
                                    uint32_t   payload_size,
                                    bool_t     is_set,
                                    bool_t    *is_offload_needed_ptr,
                                    uint32_t  *dest_domain_id_ptr,
                                    spf_msg_t *msg_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t     result            = AR_EOK;
   uint32_t        num_param_counter = 0;
   irm_cmd_ctrl_t *curr_cmd_ctrl_ptr = NULL;

   while (0 < payload_size)
   {
      uint32_t one_param_size    = 0;
      uint32_t min_param_size    = 0;
      uint32_t param_header_size = 0;

      apm_module_param_data_t *module_param_data_ptr = (apm_module_param_data_t *)data_ptr;
      param_header_size                              = sizeof(apm_module_param_data_t);
      if (payload_size < param_header_size)
      {
         break;
      }

      min_param_size = param_header_size + module_param_data_ptr->param_size;
      one_param_size = param_header_size + ALIGN_8_BYTES(module_param_data_ptr->param_size);

      if (payload_size >= min_param_size)
      {
         VERIFY(result, module_param_data_ptr->module_instance_id == IRM_MODULE_INSTANCE_ID);

         int8_t *param_data_ptr = (int8_t *)(data_ptr + param_header_size);
         AR_MSG(DBG_HIGH_PRIO, "IRM: irm_set_get_cfg_handler: set/get received, is_set = %lu", is_set);
         if (is_set)
         {
            result |= irm_set_cfg(irm_ptr, module_param_data_ptr, is_offload_needed_ptr, dest_domain_id_ptr);
            if (*is_offload_needed_ptr)
            {
               // if offload is needed, we return here and handle it outside
               // set params for multi-pd in a single cmd is not supported,
               // so all the current params will belong to a different pd

               return result;
            }
         }
         else
         {
            result |= irm_get_cfg(irm_ptr, module_param_data_ptr, msg_ptr, &curr_cmd_ctrl_ptr);
         }

         AR_MSG(DBG_HIGH_PRIO,
                "IRM: param_data_ptr = 0x%X, param_id = 0x%X, param_size = %lu, error_code = %lu",
                param_data_ptr,
                module_param_data_ptr->param_id,
                module_param_data_ptr->param_size,
                module_param_data_ptr->error_code);
      }
      else
      {
         break;
      }

      if (min_param_size <= one_param_size)
      {
         num_param_counter++;
         data_ptr = (uint8_t *)(data_ptr + one_param_size);
         if (one_param_size <= payload_size)
         {
            payload_size = payload_size - one_param_size;
         }
         else
         {
            break;
         }
      }
      else
      {
         break;
      }
   }

   if (!num_param_counter)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Warning: entering set/get cfg with zero set/get cfgs applied.");
   }

   CATCH(result, IRM_MSG_PREFIX)
   {
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles get param
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_get_cfg(irm_t                   *irm_ptr,
                        apm_module_param_data_t *module_param_data_ptr,
                        spf_msg_t               *msg_ptr,
                        irm_cmd_ctrl_t         **curr_cmd_ctrl_pptr)
{
   ar_result_t result         = AR_EOK;
   uint8_t    *param_data_ptr = (uint8_t *)(module_param_data_ptr + 1);
   uint32_t    param_id       = module_param_data_ptr->param_id;
   uint32_t   *param_size_ptr = &module_param_data_ptr->param_size;
   uint32_t   *error_code_ptr = &module_param_data_ptr->error_code;

   AR_MSG(DBG_HIGH_PRIO, "IRM: get received, param_id = 0x%X", param_id);
   uint32_t host_domain_id = 0xFFFFFFFF;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = NULL;
   result = apm_offload_utils_get_sat_proc_domain_list(&apm_sat_pd_info_ptr);

   uint32_t num_sat_pd = 0;

   if (AR_SUCCEEDED(result) && (apm_sat_pd_info_ptr))
   {
      num_sat_pd = apm_sat_pd_info_ptr->num_proc_domain_ids;
   }
   else
   {
      AR_MSG(DBG_MED_PRIO, "IRM: warning: could not fetch satellite pd info from APM");
   }

   result = AR_EOK;

   switch (param_id)
   {
      case PARAM_ID_IRM_VERSION:
      {
         if (*param_size_ptr < sizeof(param_id_irm_version_t))
         {
            *param_size_ptr = sizeof(param_id_irm_version_t);
            *error_code_ptr = AR_ENEEDMORE;
            break;
         }
         param_id_irm_version_t *base_param_ptr = (param_id_irm_version_t *)(param_data_ptr);
         base_param_ptr->version                = IRM_MINOR_VERSION;
         break;
      }
      case PARAM_ID_IRM_SYSTEM_CAPABILITIES:
      {

         uint32_t payload_reqd_size =
            sizeof(param_id_irm_system_capabilities_t) + ((num_sat_pd + 1) * sizeof(irm_system_capabilities_t));

         if (*param_size_ptr < payload_reqd_size)
         {
            *param_size_ptr = payload_reqd_size;
            *error_code_ptr = AR_ENEEDMORE;
            break;
         }
         param_id_irm_system_capabilities_t *base_param_ptr = (param_id_irm_system_capabilities_t *)(param_data_ptr);
         base_param_ptr->num_proc_domain                    = num_sat_pd + 1;

         // Fill the host domain capabilities
         irm_system_capabilities_t *cmn_capabilities_ptr    = (irm_system_capabilities_t *)(base_param_ptr + 1);
         cmn_capabilities_ptr->proc_domain                  = host_domain_id;
         cmn_capabilities_ptr->processor_type               = g_irm_cmn_capabilities.processor_type;
         cmn_capabilities_ptr->min_profiling_period_us      = g_irm_cmn_capabilities.min_profiling_period_us;
         cmn_capabilities_ptr->min_profile_per_report       = g_irm_cmn_capabilities.min_profile_per_report;
         cmn_capabilities_ptr->max_num_containers_supported = g_irm_cmn_capabilities.max_num_containers_supported;
         cmn_capabilities_ptr->max_module_supported         = g_irm_cmn_capabilities.max_module_supported;
         cmn_capabilities_ptr++;

         // If there are 0 sat pds, then we are a sat pd and don't have to query any other pd
         if (0 == num_sat_pd)
         {
            // Since we are in a satellite pd we don't need to query satellites about system capabilities
            *param_size_ptr = sizeof(param_id_irm_system_capabilities_t) +
                              (base_param_ptr->num_proc_domain * sizeof(irm_system_capabilities_t));
            *error_code_ptr = AR_EOK;
            break;
         }

         uint32_t *sat_pd_domain_ptr = (apm_sat_pd_info_ptr) ? apm_sat_pd_info_ptr->proc_domain_list : NULL;
         if (NULL == *curr_cmd_ctrl_pptr)
         {
            if (AR_EOK != (result = irm_set_cmd_ctrl(irm_ptr,
                                                     msg_ptr,
                                                     (void *)module_param_data_ptr,
                                                     TRUE, // We will always send our set-cfg out of band
                                                     curr_cmd_ctrl_pptr,
                                                     host_domain_id))) // Dst domain id not used here as this cmd ctrl
                                                                       // obj is shared among all get cfgs
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: CMD Handler Offload: Setting cmd ctrl failed.");
               return result;
            }
         }

         // param_ptr stores the new payload that will be copied to the gpr packet for sat pds. We don't just use
         // cmd_capabilities_ptr so that we can inject the param_id_irm_system_capabilities_t
         uint8_t *param_ptr =
            posal_memory_malloc(sizeof(param_id_irm_system_capabilities_t) + sizeof(irm_system_capabilities_t),
                                irm_ptr->heap_id);

         if (NULL == param_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: CMD Handler Offload: failed to malloc param ptr memory.");
            return AR_EFAILED;
         }

         ((param_id_irm_system_capabilities_t *)param_ptr)->num_proc_domain = 1;

         for (uint32_t num_proc_domain = 0; ((num_proc_domain < num_sat_pd) && (sat_pd_domain_ptr)); num_proc_domain++)
         {

            memscpy(param_ptr + sizeof(param_id_irm_system_capabilities_t),
                    sizeof(irm_system_capabilities_t),
                    cmn_capabilities_ptr,
                    sizeof(irm_system_capabilities_t));

            ar_result_t send_result = irm_send_get_cfg_cmd_to_satellite(irm_ptr,
                                                                 APM_CMD_GET_CFG,
                                                                 PARAM_ID_IRM_SYSTEM_CAPABILITIES,
                                                                 host_domain_id,
                                                                 sat_pd_domain_ptr[num_proc_domain],
                                                                 sizeof(param_id_irm_system_capabilities_t) +
                                                                    sizeof(irm_system_capabilities_t),
                                                                 (uint8_t *)cmn_capabilities_ptr,
                                                                 param_ptr,
                                                                 *curr_cmd_ctrl_pptr);
            *error_code_ptr |= send_result;

            // If the first call fails and frees curr_cmd_ctrl_ptr, break to avoid double free
            if (AR_DID_FAIL(send_result))
            {
               *curr_cmd_ctrl_pptr = NULL; // Mark as freed to prevent further use
               break;
            }
            // TODO: Need to handle when we have multiple satellite domains and more than one sat pd get cfg is failed.
            cmn_capabilities_ptr++;
         }

         posal_memory_free(param_ptr);

         *param_size_ptr = sizeof(param_id_irm_system_capabilities_t) +
                           (base_param_ptr->num_proc_domain * sizeof(irm_system_capabilities_t));
         *error_code_ptr = AR_EOK;
         break;
      }
      case PARAM_ID_IRM_METRIC_CAPABILITIES:
      {
         uint32_t total_size = sizeof(param_id_irm_metric_capabilities_t);

         // Make sure we at least have enough for a param_id_irm_metric_capabilities_t
         if (total_size > *param_size_ptr)
         {
            *param_size_ptr = total_size;
            *error_code_ptr = AR_ENEEDMORE;
            break;
         }

         param_id_irm_metric_capabilities_t *base_param_ptr = (param_id_irm_metric_capabilities_t *)param_data_ptr;

         base_param_ptr->num_proc_domain = num_sat_pd + 1;

         for (uint32_t num_proc_domain = 0; num_proc_domain < (base_param_ptr->num_proc_domain); num_proc_domain++)
         {
            total_size += sizeof(irm_metric_capabilities_t);

            // Calculate total size required for the get param
            for (uint32_t block_num = 0; block_num < g_num_capability_blocks; block_num++)
            {
               total_size += sizeof(irm_metric_capability_block_t);
               total_size += g_capability_list_ptr[block_num].num_metrics * sizeof(uint32_t);
            }
         }

         // Check if the size given is enough
         if (total_size > *param_size_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "IRM: CMD Handler: Get CFG payload size %d is smaller than calculated required size %d.",
                   *param_size_ptr,
                   total_size);
            *param_size_ptr = total_size;
            *error_code_ptr = AR_ENEEDMORE;
            break;
         }

         // Fill the host domain capabilities list
         irm_metric_capabilities_t *capabilities_ptr = (irm_metric_capabilities_t *)(base_param_ptr + 1);
         capabilities_ptr->proc_domain               = host_domain_id;
         capabilities_ptr->num_blocks                = g_num_capability_blocks;
         irm_metric_capability_block_t *block_ptr    = (irm_metric_capability_block_t *)(capabilities_ptr + 1);

         // Copy the capabilities from the dev cfg file
         for (uint32_t block_num = 0; block_num < capabilities_ptr->num_blocks; block_num++)
         {
            block_ptr->block_id       = g_capability_list_ptr[block_num].block_id;
            block_ptr->num_metric_ids = g_capability_list_ptr[block_num].num_metrics;
            uint32_t payload_size     = block_ptr->num_metric_ids * sizeof(uint32_t);

            uint8_t *block_payload_ptr = (uint8_t *)(block_ptr + 1);

            uint32_t memcpy_size = memscpy((void *)block_payload_ptr,
                                           payload_size,
                                           (void *)g_capability_list_ptr[block_num].capability_ptr,
                                           payload_size);

            block_ptr = (irm_metric_capability_block_t *)(block_payload_ptr + memcpy_size);
         }

         if (0 == num_sat_pd)
         {
            // Since we are in a satellite pd we don't need to query satellites about metric capabilities
            *param_size_ptr = total_size;
            *error_code_ptr = AR_EOK;
            break;
         }

         // Fill Satellite information
         uint32_t *sat_pd_domain_ptr = (apm_sat_pd_info_ptr) ? apm_sat_pd_info_ptr->proc_domain_list : NULL;

         if (NULL == *curr_cmd_ctrl_pptr)
         {
            if (AR_EOK != (result = irm_set_cmd_ctrl(irm_ptr,
                                                     msg_ptr,
                                                     (void *)module_param_data_ptr,
                                                     TRUE, // We will always send our set-cfg out of band
                                                     curr_cmd_ctrl_pptr,
                                                     host_domain_id))) // Dst domain id not used here as this cmd ctrl
                                                                       // obj is shared among all get cfgs
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: CMD Handler Offload: Setting cmd ctrl failed.");
               return result;
            }
         }

         AR_MSG(DBG_ERROR_PRIO, "IRM: CMD Handler Offload: Sending get metric capabilies to sat pds.");

         uint32_t param_size = sizeof(param_id_irm_metric_capabilities_t) + sizeof(irm_metric_capability_block_t) +
                               block_ptr->num_metric_ids * sizeof(uint32_t);

         // param_ptr stores the new payload that will be copied to the gpr packet for sat pds. We don't just use
         // cmd_capabilities_ptr so that we can inject the param_id_irm_metric_capabilities_t
         uint8_t *param_ptr = posal_memory_malloc(param_size, irm_ptr->heap_id);

         if (NULL == param_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: CMD Handler Offload: failed to malloc param ptr memory.");
            return AR_EFAILED;
         }
         ((param_id_irm_metric_capabilities_t *)param_ptr)->num_proc_domain = 1;

         for (uint32_t num_proc_domain = 0; ((num_proc_domain < num_sat_pd) && (sat_pd_domain_ptr)); num_proc_domain++)
         {
            // Use bytes written for the pointer
            capabilities_ptr = (irm_metric_capabilities_t *)block_ptr;
            *error_code_ptr |= irm_send_get_cfg_cmd_to_satellite(irm_ptr,
                                                                 APM_CMD_GET_CFG,
                                                                 PARAM_ID_IRM_METRIC_CAPABILITIES,
                                                                 host_domain_id,
                                                                 sat_pd_domain_ptr[num_proc_domain],
                                                                 total_size,
                                                                 (uint8_t *)capabilities_ptr,
                                                                 param_ptr,
                                                                 *curr_cmd_ctrl_pptr);

         }

         *param_size_ptr = total_size;
         *error_code_ptr = AR_EOK;
         break;
      }
      case PARAM_ID_IRM_REPORT_METRICS:
      {
         param_id_report_metrics_t *base_param_ptr = (param_id_report_metrics_t *)param_data_ptr;
         if (*param_size_ptr < irm_ptr->core.report_payload_size)
         {
            *param_size_ptr = irm_ptr->core.report_payload_size;
            *error_code_ptr = AR_ENEEDMORE;
            break;
         }
         /*
           sysmon_audio_query_t query;
           sysmon_audio_query(&query);

           AR_MSG(DBG_HIGH_PRIO,
                  "IRM: query.sysclocktick %llu, query.pcycles %llu, query.busVoteinAb %llu, query.busVoteinIb %llu,
           "
                  "query.coreClkVoteinkHz %lu, query.avsheap_total_bytes %lu, query.avsheap_available_total ",
                  query.sysclocktick,
                  query.pcycles,
                  query.busVoteinAb,
                  query.busVoteinIb,
                  query.coreClkVoteinkHz,
                  query.avsheap_total_bytes);

           AR_MSG(DBG_HIGH_PRIO,
                  "%lu,query.avsheap_available_max %lu, query.pktcnt %lu, query.axi_rd_cnt %lu, query.axi_wr_cnt
           %lu",
                  query.avsheap_available_total,
                  query.avsheap_available_max,
                  query.pktcnt,
                  query.axi_rd_cnt,
                  query.axi_wr_cnt);
              */
         // Throw away code for testing
         uint32_t         count          = 0x1000;
         spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
         for (; NULL != block_node_ptr;)
         {
            irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
            if (NULL != block_obj_ptr)
            {
               spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;
               for (; NULL != instance_node_ptr;)
               {
                  irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
                  if (NULL != instance_obj_ptr)
                  {
                     spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;
                     for (; NULL != metric_node_ptr;)
                     {
                        irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
                        if (NULL != metric_obj_ptr)
                        {
                           irm_report_metric_t *report_metric_ptr = metric_obj_ptr->metric_info.metric_payload_ptr;
                           irm_report_metric_payload_t *report_metric_payload_ptr =
                              (irm_report_metric_payload_t *)(report_metric_ptr + 1);
                           for (int32_t i = 0; i < irm_ptr->core.num_profiles_per_report; i++)
                           {
                              uint32_t *metric_payload_ptr = (uint32_t *)(report_metric_payload_ptr + 1);
                              *metric_payload_ptr          = count++;
                              report_metric_payload_ptr =
                                 (irm_report_metric_payload_t *)(((uint8_t *)metric_payload_ptr) +
                                                                 irm_get_metric_payload_size(metric_obj_ptr->id));
                           }
                        }
                        LIST_ADVANCE(metric_node_ptr);
                     }
                  }
                  LIST_ADVANCE(instance_node_ptr);
               }
            }
            LIST_ADVANCE(block_node_ptr);
         }

         *param_size_ptr = memscpy((void *)base_param_ptr,
                                   *param_size_ptr,
                                   (void *)irm_ptr->core.report_payload_ptr,
                                   irm_ptr->core.report_payload_size);

         *error_code_ptr = AR_EOK;
         break;
      }
      default:
      {
         break;
      }
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles set config
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_set_cfg(irm_t                   *irm_ptr,
                        apm_module_param_data_t *module_param_data_ptr,
                        bool_t                  *is_offload_needed_ptr,
                        uint32_t                *dest_domain_id_ptr)
{
   ar_result_t result         = AR_EOK;
   uint32_t    host_domain_id = 0xFFFFFFFF;
   uint8_t    *param_data_ptr = (uint8_t *)(module_param_data_ptr + 1);
   uint32_t    param_id       = module_param_data_ptr->param_id;
   uint32_t   *param_size_ptr = &module_param_data_ptr->param_size;
   uint32_t   *error_code_ptr = &module_param_data_ptr->error_code;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   AR_MSG(DBG_HIGH_PRIO, "IRM:set received, param_id = 0x%X", param_id);
   switch (param_id)
   {
      case PARAM_ID_IRM_PROFILING_PARAMS:
      {
         if (*param_size_ptr < sizeof(param_id_irm_profiling_params_t))
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: payload size not enough, param_id = 0x%X", param_id);
            result = AR_EBADPARAM;
            break;
         }
         param_id_irm_profiling_params_t *irm_cmn_params_ptr = (param_id_irm_profiling_params_t *)(param_data_ptr);
         if (host_domain_id != irm_cmn_params_ptr->proc_domain)
         {
            *is_offload_needed_ptr = TRUE;
            *dest_domain_id_ptr    = irm_cmn_params_ptr->proc_domain;
            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: Offload param detected, param_id = 0x%X, host proc_domain =%lu, given proc_domain = %lu",
                   param_id,
                   host_domain_id,
                   irm_cmn_params_ptr->proc_domain);
            return AR_EOK;
         }
         if ((irm_ptr->core.profiling_period_us != irm_cmn_params_ptr->profiling_period_us) ||
             (irm_ptr->core.num_profiles_per_report != irm_cmn_params_ptr->num_profiles_per_report))
         {
            if (irm_ptr->core.is_profiling_started)
            {
               irm_ptr->core.profiling_period_us     = irm_cmn_params_ptr->profiling_period_us;
               irm_ptr->core.num_profiles_per_report = irm_cmn_params_ptr->num_profiles_per_report;
               result                                = irm_handle_new_config(irm_ptr);
               result |= irm_collect_and_fill_info(irm_ptr, 0 /*frame_size_ms*/);
            }
            else
            {
               irm_ptr->core.profiling_period_us     = irm_cmn_params_ptr->profiling_period_us;
               irm_ptr->core.num_profiles_per_report = irm_cmn_params_ptr->num_profiles_per_report;
            }
         }

         break;
      }
      case PARAM_ID_IRM_ENABLE_DISABLE_PROFILING:
      {
         // Disable and clean up enable-all mode
         irm_ptr->enable_all = FALSE;
         irm_clean_up_enable_all(irm_ptr);

         param_id_enable_disable_metrics_t *base_param_ptr = (param_id_enable_disable_metrics_t *)(param_data_ptr);
         uint32_t                           req_size       = sizeof(param_id_enable_disable_metrics_t);
         if (*param_size_ptr < req_size)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: payload size not enough, param_id = 0x%X", param_id);
            result = AR_EBADPARAM;
            break;
         }

         if (host_domain_id != base_param_ptr->proc_domain)
         {
            *is_offload_needed_ptr = TRUE;
            *dest_domain_id_ptr    = base_param_ptr->proc_domain;
            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: Offload param detected, param_id = 0x%X, host proc_domain =%lu, given proc_domain = "
                   "%lu",
                   param_id,
                   host_domain_id,
                   base_param_ptr->proc_domain);
            return AR_EOK;
         }

         irm_enable_disable_block_t *set_enable_disable_ptr = (irm_enable_disable_block_t *)(base_param_ptr + 1);

         for (uint32_t block = 0; block < base_param_ptr->num_blocks; block++)
         {
            req_size += sizeof(irm_enable_disable_block_t);
            if ((*param_size_ptr < req_size) ||
                (*param_size_ptr < (req_size += (sizeof(uint32_t) * set_enable_disable_ptr->num_metric_ids))))
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: payload size not enough, param_id = 0x%X", param_id);
               result = AR_EBADPARAM;
               break;
            }

            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: block_id = 0x%X, instance_id = 0x%X, num_metric_ids = 0x%X",
                   set_enable_disable_ptr->block_id,
                   set_enable_disable_ptr->instance_id,
                   set_enable_disable_ptr->num_metric_ids);

            uint32_t *metric_ids_ptr = (uint32_t *)(set_enable_disable_ptr + 1);
            for (uint32_t metric_ids = 0; metric_ids < set_enable_disable_ptr->num_metric_ids; metric_ids++)
            {
               AR_MSG(DBG_HIGH_PRIO, "IRM: metric_id = 0x%X", *metric_ids_ptr);
               metric_ids_ptr++;
            }

            set_enable_disable_ptr =
               (irm_enable_disable_block_t *)(((uint8_t *)set_enable_disable_ptr) + sizeof(irm_enable_disable_block_t) +
                                              (sizeof(uint32_t) * set_enable_disable_ptr->num_metric_ids));
         }
         if (AR_EOK == result)
         {
            if (base_param_ptr->is_enable)
            {
               result = irm_handle_set_enable(irm_ptr, base_param_ptr);
            }
            else
            {
               result = irm_handle_set_disable(irm_ptr, base_param_ptr);
            }
         }
         *error_code_ptr = result;
         break;
      }
      case APM_PARAM_ID_SET_CNTR_HANDLES:
      {
         result = irm_parse_instance_handle_rsp(irm_ptr, module_param_data_ptr);
         // If enable all is set, handle this when the container handles are set on graph open/when instance handles are
         // recieved
         if (irm_ptr->enable_all)
         {
            result = irm_handle_enable_all(irm_ptr);
         }
         break;
      }
      case APM_PARAM_ID_RESET_CNTR_HANDLES:
      {
         result = irm_parse_instance_handle_rsp(irm_ptr, module_param_data_ptr);
         break;
      }
      case IRM_PARAM_ID_ENABLE_ALL_PROFILING:
      {
         if (*param_size_ptr < sizeof(param_id_enable_all_t))
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: payload size not enough, param_id = 0x%X", param_id);
            result = AR_EBADPARAM;
            break;
         }
         param_id_enable_all_t *irm_enable_all_params = (param_id_enable_all_t *)(param_data_ptr);
         if (host_domain_id != irm_enable_all_params->proc_domain)
         {
            *is_offload_needed_ptr = TRUE;
            *dest_domain_id_ptr    = irm_enable_all_params->proc_domain;
            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: Offload param detected, param_id = 0x%X, host proc_domain =%lu, given proc_domain = %lu",
                   param_id,
                   host_domain_id,
                   irm_enable_all_params->proc_domain);
            return AR_EOK;
         }

         result = irm_fill_enable_all_metric_tree(irm_ptr, irm_enable_all_params, module_param_data_ptr->param_size);

         // If we failed to fill out the tree clean up the mess and do not send garbage to APM
         if (AR_DID_FAIL(result))
         {
            irm_clean_up_enable_all(irm_ptr);
         }
         else
         {
            irm_ptr->enable_all = TRUE;
            // Request for instance handles
            irm_request_all_instance_handles(irm_ptr);
         }
         break;
      }
      default:
      {
         break;
      }
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles PM votes
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_vote_for_ddr_bw(irm_t *irm_ptr, bool_t is_request_bw)
{
   ar_result_t result = AR_EOK;
   if (!posal_power_mgr_is_registered(irm_ptr->pm_info.pm_handle_ptr))
   {
      return AR_EOK;
   }
   // Release DDR BW vote to allow island entry
   if (!is_request_bw)
   {
      posal_pm_release_info_t release;
      memset(&release, 0, sizeof(posal_pm_release_info_t));

      release.pm_handle_ptr   = irm_ptr->pm_info.pm_handle_ptr;
      release.client_log_id   = IRM_MODULE_INSTANCE_ID;
      release.wait_signal_ptr = irm_ptr->pm_info.pm_signal;
      // Populate BW vote
      release.resources.bw.is_valid = TRUE;
      release.resources.bw.value    = IRM_AUDIO_BW_VOTE_BYTES_PER_SEC;
      result                        = posal_power_mgr_release(&release);
   }
   else // request DDR BW vote to restrict island entry
   {
      posal_pm_request_info_t request;
      memset(&request, 0, sizeof(posal_pm_request_info_t));

      request.pm_handle_ptr   = irm_ptr->pm_info.pm_handle_ptr;
      request.client_log_id   = IRM_MODULE_INSTANCE_ID;
      request.wait_signal_ptr = irm_ptr->pm_info.pm_signal;
      // Populate BW vote
      request.resources.bw.is_valid = TRUE;
      request.resources.bw.value    = IRM_AUDIO_BW_VOTE_BYTES_PER_SEC;
      result                        = posal_power_mgr_request(&request);
   }

   if (AR_EOK == result)
   {
      AR_MSG(DBG_HIGH_PRIO, "Success: Voted for DDR BW, is_request_bw = %u", is_request_bw);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed: To vote for DDR BW, new_vote = %u", is_request_bw);
   }
   return result;
}
/*----------------------------------------------------------------------------------------------------------------------
 Handles enable set param
 1. Finds a block node with given block ID or creates a block node and inserts it to the list (ascending order)
 2. Finds the instance node from block obj or creates a instance node and inserts it to the list (ascending order)
 3. Creates separate metric list with given metric IDs
 4. If no errors occurred, moves the previous metric IDs to the new list and deletes the previous list.
 5. Cleans up stray nodes (nodes with no obj ptr, or no head_ptr) - handles partial success cases
 6. Recreates the the report payload based on new information in the nodes
 7. Populates the report payload with proper Block ID, Instance ID, metric ID, size information
 9. For blocks where this is required, request for the instance handles
 8. This payload can be later used to fill just the statistics information from the underlying profiler
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_handle_set_enable(irm_t *irm_ptr, param_id_enable_disable_metrics_t *param_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                 result         = AR_EOK;
   irm_enable_disable_block_t *set_enable_ptr = (irm_enable_disable_block_t *)(param_ptr + 1);
   uint32_t                    num_blocks     = param_ptr->num_blocks;

   for (uint32_t block_count = 0; block_count < num_blocks; block_count++)
   {
      irm_node_obj_t *block_obj_ptr    = NULL;
      irm_node_obj_t *instance_obj_ptr = NULL;

      // find/insert the block node and block obj
      block_obj_ptr = irm_check_insert_node(irm_ptr, &irm_ptr->core.block_head_node_ptr, set_enable_ptr->block_id);
      if (NULL != block_obj_ptr)
      {
         // Find/insert the instance node and instance obj
         instance_obj_ptr = irm_check_insert_node(irm_ptr, &block_obj_ptr->head_node_ptr, set_enable_ptr->instance_id);
         if (NULL != instance_obj_ptr)
         {
            // Used as fall-back option, if anything fails
            spf_list_node_t *previous_metric_head_ptr = instance_obj_ptr->head_node_ptr;
            instance_obj_ptr->head_node_ptr           = NULL;
            ar_result_t local_result                  = AR_EOK;

            // create new metric list - if any error happens, we will be abe to fall back ot previous state
            uint32_t *metric_ids_ptr = (uint32_t *)(set_enable_ptr + 1);
            for (uint32_t metric_count = 0; metric_count < set_enable_ptr->num_metric_ids; metric_count++)
            {
               irm_node_obj_t *metric_obj_ptr = NULL;
               if (!irm_is_supported_metric(block_obj_ptr->id, *metric_ids_ptr))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "IRM: metric_id = 0x%X not supported for block id 0x%X, ignoring",
                         *metric_ids_ptr,
                         block_obj_ptr->id);
                  continue;
               }
               // Insert the metric node and obj
               metric_obj_ptr = irm_check_insert_node(irm_ptr, &instance_obj_ptr->head_node_ptr, (*metric_ids_ptr));
               if (NULL == metric_obj_ptr)
               {
                  AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to add new metric_id = 0x%X node", *metric_ids_ptr);
                  local_result |= AR_EFAILED;
                  break;
               }
               metric_obj_ptr->is_first_time = TRUE;
               AR_MSG(DBG_HIGH_PRIO, "IRM: Enabled metric_id = 0x%X", *metric_ids_ptr);
               metric_ids_ptr++;
            }

            // Add the existing metric nodes to the new list, if any error happens, fall back to the previous list
            spf_list_node_t *temp_metric_node_ptr = previous_metric_head_ptr;
            while (local_result && temp_metric_node_ptr && temp_metric_node_ptr->obj_ptr)
            {
               irm_node_obj_t *temp_metric_obj_ptr = (irm_node_obj_t *)temp_metric_node_ptr->obj_ptr;
               irm_node_obj_t *new_metric_obj_ptr =
                  irm_check_insert_node(irm_ptr, &instance_obj_ptr->head_node_ptr, temp_metric_obj_ptr->id);
               if (NULL == new_metric_obj_ptr)
               {
                  AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to add existing metric_id = 0x%X node", temp_metric_obj_ptr->id);
                  local_result |= AR_EFAILED;
                  break;
               }
               temp_metric_node_ptr = temp_metric_node_ptr->next_ptr;
            }

            if (AR_EOK != local_result)
            {
               irm_delete_list(&instance_obj_ptr->head_node_ptr);
               instance_obj_ptr->head_node_ptr = previous_metric_head_ptr;
            }
            else
            {
               irm_delete_list(&previous_metric_head_ptr);
            }
            result |= local_result;
         }
         else
         {
            result |= AR_EFAILED;
         }
      }
      else
      {
         result |= AR_EFAILED;
      }

      set_enable_ptr = (irm_enable_disable_block_t *)(((uint8_t *)set_enable_ptr) + sizeof(irm_enable_disable_block_t) +
                                                      (sizeof(uint32_t) * set_enable_ptr->num_metric_ids));
   }

   // remove any unwanted or partially filled nodes
   irm_cleanup_stray_nodes(irm_ptr);
   bool_t IS_ENABLE_TRUE = TRUE;
   result = irm_handle_send_cntr_module_enable_disable_info(irm_ptr, (uint8_t *)param_ptr, IS_ENABLE_TRUE);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to send enable metric info to containers, continuing..");
   }

   TRY(result, irm_handle_new_config(irm_ptr));

   bool_t is_mem_prof_enabled         = FALSE;
   bool_t is_mod_process_prof_enabled = FALSE;
   irm_update_cntr_mod_prof_enable_flag(irm_ptr, &is_mem_prof_enabled, &is_mod_process_prof_enabled);

   if (TRUE == is_mem_prof_enabled)
   {
      TRY(result, posal_mem_prof_start());
   }

   if (is_mod_process_prof_enabled)
   {
      /* This DDR BW vote ensures to prevent island entry */
      bool_t request_bw = TRUE;
      irm_vote_for_ddr_bw(irm_ptr, request_bw);
   }

   TRY(result, irm_request_instance_handles_payload(irm_ptr));

   // In case there are any static modules being enabled we should grab the handles for them using the api
   TRY(result, irm_fill_static_instance_info(irm_ptr));

   // Collect the the running counters so it can be used when the timer tick happens
   // Frame size will be 0 for the first time
   result |= irm_collect_and_fill_info(irm_ptr, 0 /*frame_size_ms*/);

   CATCH(result, IRM_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: enable failed");
   }
   return result;
}

static spf_list_node_t *get_enable_all_metric_list(irm_t *irm_ptr, uint32_t block_id)
{
   spf_list_node_t *enable_block_list_ptr = irm_ptr->tmp_metric_payload_list;

   while (enable_block_list_ptr)
   {
      irm_node_obj_t *enable_block_node_ptr = enable_block_list_ptr->obj_ptr;
      if (NULL == enable_block_node_ptr)
      {
         return NULL;
      }
      if (block_id == enable_block_node_ptr->id)
      {
         return enable_block_node_ptr->head_node_ptr;
      }
      enable_block_list_ptr = enable_block_list_ptr->next_ptr;
   }
   return NULL;
}

/*
   Fill out the metric tree tmp_metric_payload_list to enter enable all mode. This will cache the enabled metrics for
   whenever we get new cntr handles, etc.
*/
static ar_result_t irm_fill_enable_all_metric_tree(irm_t                 *irm_ptr,
                                                   param_id_enable_all_t *irm_enable_all_params,
                                                   uint32_t               param_size)
{
   ar_result_t result                     = AR_EOK;
   uint32_t    actual_required_param_size = sizeof(param_id_enable_all_t);
   // If num blocks is 0, enable everything
   if (irm_enable_all_params->num_blocks == 0)
   {
      AR_MSG(DBG_HIGH_PRIO, "IRM: Enabling all metrics for all blocks.");
      // Since we are for sure enabling for container and module, set the flag so APM will send the handles
      posal_mutex_lock(irm_ptr->core.cntr_mod_prof_enable_mutex);
      irm_ptr->core.is_cntr_or_mod_prof_enabled = TRUE;
      posal_mutex_unlock(irm_ptr->core.cntr_mod_prof_enable_mutex);

      for (uint32_t block_idx = 0; block_idx < g_num_capability_blocks; block_idx++)
      {
         // For each block in the compatibility list, insert all the metrics into the cache
         irm_node_obj_t *block_node = irm_check_insert_node(irm_ptr,
                                                            &irm_ptr->tmp_metric_payload_list,
                                                            g_capability_list_ptr[block_idx].block_id);

         if (NULL == block_node)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert block node");
            // Return early and don't request for module handles
            return AR_EFAILED;
         }

         // based on which metrics are allowed for this block/node we enable all allowed metrics
         uint32_t *supported_metrics_ptr = NULL;
         uint32_t  num_supported_metrics = 0;

         irm_get_supported_metric_arr(block_node->id, &supported_metrics_ptr, &num_supported_metrics);

         AR_MSG(DBG_HIGH_PRIO,
                "IRM: Enabling %d supported metrics for block id %d.",
                num_supported_metrics,
                block_node->id);

         for (uint32_t i = 0; i < num_supported_metrics; ++i)
         {
            irm_node_obj_t *metric_obj_ptr =
               irm_check_insert_node(irm_ptr, &block_node->head_node_ptr, supported_metrics_ptr[i]);
            if (NULL == metric_obj_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to add new metric_id = 0x%X node", supported_metrics_ptr[i]);
               result = AR_EFAILED;
               break;
            }
         }
      }
   }
   else
   {
      irm_enable_all_block_t *enable_all_block_ptr = (irm_enable_all_block_t *)(irm_enable_all_params + 1);

      for (uint32_t block = 0; block < irm_enable_all_params->num_blocks; block++)
      {
         actual_required_param_size +=
            sizeof(irm_enable_all_block_t) + enable_all_block_ptr->num_metric_ids * sizeof(uint32_t);

         if (actual_required_param_size > param_size)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "IRM: recieved param size: %d Is smaller than calculated required param size: %d",
                   param_size,
                   actual_required_param_size);
            return AR_ENEEDMORE;
         }
         if (!irm_is_valid_block_id(enable_all_block_ptr->block_id))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "IRM: Parsed block ID: %d is not vaild. Bailing out.",
                   enable_all_block_ptr->block_id);
            // Return early and don't request for module handles
            return AR_EFAILED;
         }
         irm_node_obj_t *block_node =
            irm_check_insert_node(irm_ptr, &irm_ptr->tmp_metric_payload_list, enable_all_block_ptr->block_id);

         if (NULL == block_node)
         {
            AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert block node");
            // Return early and don't request for module handles
            return AR_EFAILED;
         }

         if (0 == enable_all_block_ptr->num_metric_ids)
         {
            // based on which metrics are allowed for this block/node we enable all allowed metrics
            uint32_t *supported_metrics_ptr = NULL;
            uint32_t  num_supported_metrics = 0;

            irm_get_supported_metric_arr(block_node->id, &supported_metrics_ptr, &num_supported_metrics);

            AR_MSG(DBG_HIGH_PRIO,
                   "IRM: Enabling %d supported metrics for block id %d.",
                   num_supported_metrics,
                   block_node->id);

            for (uint32_t i = 0; i < num_supported_metrics; ++i)
            {
               irm_node_obj_t *metric_obj_ptr =
                  irm_check_insert_node(irm_ptr, &block_node->head_node_ptr, supported_metrics_ptr[i]);
               if (NULL == metric_obj_ptr)
               {
                  AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to add new metric_id = 0x%X node", supported_metrics_ptr[i]);
                  result = AR_EFAILED;
                  break;
               }
            }
         }
         else
         {
            uint32_t *metrics_enable_ptr = (uint32_t *)(enable_all_block_ptr + 1);
            for (uint32_t metric_idx = 0; metric_idx < enable_all_block_ptr->num_metric_ids; metric_idx++)
            {

               irm_node_obj_t *metric_obj_ptr = NULL;
               if (!irm_is_supported_metric(block_node->id, metrics_enable_ptr[metric_idx]))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "IRM: metric_id = 0x%X not supported for block id 0x%X, ignoring",
                         metrics_enable_ptr[metric_idx],
                         block_node->id);
                  continue;
               }
               // Insert the metric node and obj
               metric_obj_ptr =
                  irm_check_insert_node(irm_ptr, &block_node->head_node_ptr, (metrics_enable_ptr[metric_idx]));
               if (NULL == metric_obj_ptr)
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "IRM: Failed to add new metric_id = 0x%X node",
                         metrics_enable_ptr[metric_idx]);
                  result = AR_EFAILED;
                  break;
               }
               AR_MSG(DBG_HIGH_PRIO,
                      "IRM: enable all metric_id = 0x%X cached for block id %d",
                      metrics_enable_ptr[metric_idx],
                      block_node->id);
            }
            if (block_node->id == IRM_BLOCK_ID_CONTAINER || block_node->id == IRM_BLOCK_ID_MODULE)
            {
               posal_mutex_lock(irm_ptr->core.cntr_mod_prof_enable_mutex);
               irm_ptr->core.is_cntr_or_mod_prof_enabled = TRUE;
               posal_mutex_unlock(irm_ptr->core.cntr_mod_prof_enable_mutex);
            }
         }

         enable_all_block_ptr =
            (irm_enable_all_block_t *)(((uint32_t *)(enable_all_block_ptr + 1)) + enable_all_block_ptr->num_metric_ids);
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles enable enable all when we don't have a set param. This should happen after graph open if we have the enable all
 flag set to TRUE.

 1. Assume that all nodes EXCEPT for static module nodes and the processor node have been inserted into the irm
structure. When APM sets container handles during graph open we automatically insert them into the IRM structure. At
this point we will also enable all static modules
 2. We enable all of the metrics that are allowed for all nodes.
 3. If no errors occurred, moves the previous metric IDs to the new list and deletes the previous list.
 4. Cleans up stray nodes (nodes with no obj ptr, or no head_ptr) - handles partial success cases
 5. Recreates the the report payload based on new information in the nodes
 6. Populates the report payload with proper Block ID, Instance ID, metric ID, size information
 7. This payload can be later used to fill just the statistics information from the underlying profiler
----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t irm_handle_enable_all(irm_t *irm_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   AR_MSG(DBG_HIGH_PRIO, "IRM: Enabling ALL");

   irm_node_obj_t *processor_node_ptr =
      irm_check_insert_node(irm_ptr, &irm_ptr->core.block_head_node_ptr, IRM_BLOCK_ID_PROCESSOR);

   if (NULL == processor_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert processor block node");
      // Return early and don't request for module handles
      return AR_EFAILED;
   }

   irm_node_obj_t *processor_instance_ptr = irm_check_insert_node(irm_ptr, &processor_node_ptr->head_node_ptr, 0);
   if (NULL == processor_instance_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert processor instance node");
      // Return early and don't request for module handles
      return AR_EFAILED;
   }

   // if there are any static modules insert them now
   irm_node_obj_t *sm_node_ptr =
      irm_check_insert_node(irm_ptr, &irm_ptr->core.block_head_node_ptr, IRM_BLOCK_ID_STATIC_MODULE);
   if (NULL == sm_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert static module block node");
      // Return early and don't request for module handles
      return AR_EFAILED;
   }
   irm_insert_all_static_modules(irm_ptr);

   // Insert the pool block
   irm_node_obj_t *pool_node_ptr =
      irm_check_insert_node(irm_ptr, &irm_ptr->core.block_head_node_ptr, IRM_BLOCK_ID_POOL);

   if (NULL == pool_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert pool block node");
      // Return early and don't request for module handles
      return AR_EFAILED;
   }

   irm_node_obj_t *pool_instance_ptr = irm_check_insert_node(irm_ptr, &pool_node_ptr->head_node_ptr, IRM_POOL_ID_LIST);
   if (NULL == pool_instance_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: failed to insert processor instance node");
      // Return early and don't request for module handles
      return AR_EFAILED;
   }

   spf_list_node_t *block_list_ptr = irm_ptr->core.block_head_node_ptr;

   while (NULL != block_list_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_list_ptr->obj_ptr;
      if (NULL == block_obj_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO, "Block Obj ptr is null");
         result = AR_EFAILED;
         return result;
      }
      spf_list_node_t *instance_list_ptr = block_obj_ptr->head_node_ptr;

      spf_list_node_t *metric_list_head_ptr = get_enable_all_metric_list(irm_ptr, block_obj_ptr->id);

      while (NULL != instance_list_ptr)
      {
         irm_node_obj_t *instance_obj_ptr = instance_list_ptr->obj_ptr;

         if (NULL == instance_obj_ptr)
         {
            AR_MSG(DBG_ERROR_PRIO, "Instance Obj ptr is null");
            result = AR_EFAILED;
            return result;
         }
         AR_MSG(DBG_MED_PRIO, "IRM: Enabling metrics for instance id = 0x%X", instance_obj_ptr->id);

         spf_list_node_t *metric_list_ptr = metric_list_head_ptr;

         while (metric_list_ptr)
         {
            irm_node_obj_t *cached_tmp_metric = metric_list_ptr->obj_ptr;

            irm_node_obj_t *metric_obj_ptr =
               irm_check_insert_node(irm_ptr, &instance_obj_ptr->head_node_ptr, cached_tmp_metric->id);
            if (NULL == metric_obj_ptr)
            {
               AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to add new metric_id = 0x%X node", cached_tmp_metric->id);
               result |= AR_EFAILED;
               break;
            }
            metric_obj_ptr->is_first_time = TRUE;
            AR_MSG(DBG_HIGH_PRIO, "IRM: Enabled metric_id = 0x%X", metric_obj_ptr->id);

            metric_list_ptr = metric_list_ptr->next_ptr;
         }

         instance_list_ptr = instance_list_ptr->next_ptr;
      }

      block_list_ptr = block_list_ptr->next_ptr;
   }

   // remove any unwanted or partially filled nodes
   irm_cleanup_stray_nodes(irm_ptr);

   TRY(result, irm_handle_new_config(irm_ptr));

   // Even if we don't see any modules yet, start profiling memory so we are ready at the beginning of the use case
   TRY(result, posal_mem_prof_start());

   /* This DDR BW vote ensures to prevent island entry */
   bool_t request_bw = TRUE;
   irm_vote_for_ddr_bw(irm_ptr, request_bw);

   // Collect the the running counters so it can be used when the timer tick happens
   // Frame size will be 0 for the first time
   result |= irm_collect_and_fill_info(irm_ptr, 0 /*frame_size_ms*/);

   CATCH(result, IRM_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: enable all failed");
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Handles disable set param
 If num_blocks = 0; Deletes everything, easy way to reset things
 If num_metric_ids = 0; Deletes all the metric ID nodes present in that instance id.(same reason as above)
 At the end, clean up is done to remove stray nodes, this helps in simplifying the deletion code and moving
 clean - up code to the end.
 1. Remove every node, if num_blocks = 0
 2. else Finds the block node and obj
 3. Finds the instance node and obj from the block obj
 4. Remove all metric ids in the insance obj if num_metric_ids = 0
 5. Else Removes the given metric IDs from the instance obj
 6. Cleans up any stray nodes (nodes without obj_ptr, or head_ptr)
 7. Recreates the report payload from the new information
 8. Fills the report payload with BID, IID, MID and size information
 9. This report payload can be later used to fill the metric specific statistics
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_handle_set_disable(irm_t *irm_ptr, param_id_enable_disable_metrics_t *param_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t                 result          = AR_EOK;
   bool_t                      IS_ENABLE_FALSE = FALSE;
   irm_enable_disable_block_t *set_disable_ptr = (irm_enable_disable_block_t *)(param_ptr + 1);
   irm_ptr->core.sessoin_counter++;

   // If the number of block id provided is 0, delete everything
   // Easy way to reset instead of providing all the metric IDs when you have disable everything
   if (0 == param_ptr->num_blocks)
   {
      AR_MSG(DBG_MED_PRIO, "IRM: Disabling Entire the metrics across all blocks");
      irm_clean_up_all_nodes(irm_ptr);
   }

   result = irm_handle_send_cntr_module_enable_disable_info(irm_ptr, (uint8_t *)param_ptr, IS_ENABLE_FALSE);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: Failed to send disable metric info to containers, continuing..");
   }

   for (uint32_t block_count = 0; block_count < param_ptr->num_blocks; block_count++)
   {
      spf_list_node_t *block_node_ptr    = NULL;
      irm_node_obj_t  *block_obj_ptr     = NULL;
      spf_list_node_t *instance_node_ptr = NULL;
      irm_node_obj_t  *instance_obj_ptr  = NULL;

      AR_MSG(DBG_HIGH_PRIO,
             "IRM: Disable, block_count = %lu, num_blocks = %lu, block_id = 0x%X, instance_id = 0x%X",
             block_count,
             param_ptr->num_blocks,
             set_disable_ptr->block_id,
             set_disable_ptr->instance_id);

      block_node_ptr = irm_find_node_from_id(irm_ptr->core.block_head_node_ptr, set_disable_ptr->block_id);
      if ((NULL == block_node_ptr) || (NULL == block_node_ptr->obj_ptr))
      {
         set_disable_ptr =
            (irm_enable_disable_block_t *)(((uint8_t *)set_disable_ptr) + sizeof(irm_enable_disable_block_t) +
                                           (sizeof(uint32_t) * set_disable_ptr->num_metric_ids));
         continue;
      }
      block_obj_ptr = block_node_ptr->obj_ptr;

      instance_node_ptr = irm_find_node_from_id(block_obj_ptr->head_node_ptr, set_disable_ptr->instance_id);
      if ((NULL == instance_node_ptr) || (NULL == instance_node_ptr->obj_ptr))
      {
         set_disable_ptr =
            (irm_enable_disable_block_t *)(((uint8_t *)set_disable_ptr) + sizeof(irm_enable_disable_block_t) +
                                           (sizeof(uint32_t) * set_disable_ptr->num_metric_ids));
         continue;
      }
      instance_obj_ptr = instance_node_ptr->obj_ptr;

      // If the number of metric id provided is 0, delete everything belonging to that instance/block id
      // Easy way to reset instead of providing all the metric IDs when you have disable everything
      if (0 == set_disable_ptr->num_metric_ids)
      {
         AR_MSG(DBG_MED_PRIO,
                "IRM: Disabled block id = %lu, instance id = 0x%X, disabling all the metrics for this instance",
                set_disable_ptr->block_id,
                set_disable_ptr->instance_id);
         // Just the delete list is good enough here
         // since the cleanup stray nodes does a proper clean up of unwanted nodes
         irm_delete_list(&instance_obj_ptr->head_node_ptr);
      }

      // Find and delete the node using metric ID if present
      uint32_t *metric_ids_ptr = (uint32_t *)(set_disable_ptr + 1);
      for (uint32_t metric_count = 0; metric_count < set_disable_ptr->num_metric_ids; metric_count++)
      {
         spf_list_node_t *metric_node_ptr = irm_find_node_from_id(instance_obj_ptr->head_node_ptr, (*metric_ids_ptr));
         if (NULL != metric_node_ptr)
         {
            // Returns the obj to the pool as well
            irm_delete_node(&instance_obj_ptr->head_node_ptr, &metric_node_ptr);
         }

         AR_MSG(DBG_HIGH_PRIO, "IRM: Disabled metric_id = 0x%X", *metric_ids_ptr);
         metric_ids_ptr++;
      }

      set_disable_ptr =
         (irm_enable_disable_block_t *)(((uint8_t *)set_disable_ptr) + sizeof(irm_enable_disable_block_t) +
                                        (sizeof(uint32_t) * set_disable_ptr->num_metric_ids));
   }

   // remove any unwanted or partially filled nodes
   irm_cleanup_stray_nodes(irm_ptr);

   TRY(result, irm_handle_new_config(irm_ptr));

   bool_t is_mem_prof_enabled         = FALSE;
   bool_t is_mod_process_prof_enabled = FALSE;
   irm_update_cntr_mod_prof_enable_flag(irm_ptr, &is_mem_prof_enabled, &is_mod_process_prof_enabled);

   if (FALSE == is_mem_prof_enabled)
   {
      TRY(result, posal_mem_prof_stop());
   }

   if (!is_mod_process_prof_enabled)
   {
      /*This devote for DDR BW ensures to allow island entry */
      bool_t release_bw = FALSE;
      irm_vote_for_ddr_bw(irm_ptr, release_bw);
   }

   // Collect the the running counters so it can be used when the timer tick happens
   // Frame size will be 0 for the first time
   result |= irm_collect_and_fill_info(irm_ptr, 0 /*frame_size_ms*/);

   CATCH(result, IRM_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: disable failed");
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Recreates the report payload based on the newly updated IRM data structure.
 1. Calculates the size required for the report payload.
 2. Free and allocate new payload
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_recreate_report_payload(irm_t *irm_ptr)
{
   ar_result_t result                         = AR_EOK;
   uint32_t    total_size                     = 0;
   uint32_t    total_prev_metric_payload_size = 0;

   // Calculate the size required for the new report payload
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
   for (; NULL != block_node_ptr;)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;
         for (; NULL != instance_node_ptr;)
         {
            irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
            if (NULL != instance_obj_ptr)
            {
               total_size += sizeof(irm_report_metrics_block_t);

               spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;
               for (; NULL != metric_node_ptr;)
               {
                  irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
                  if (NULL != metric_obj_ptr)
                  {
                     total_size +=
                        (sizeof(irm_report_metric_t) +
                         (irm_ptr->core.num_profiles_per_report *
                          (sizeof(irm_report_metric_payload_t) + irm_get_metric_payload_size(metric_obj_ptr->id))));
                     total_prev_metric_payload_size += irm_get_prev_metric_payload_size(metric_obj_ptr->id);
                  }
                  LIST_ADVANCE(metric_node_ptr);
               }
            }
            LIST_ADVANCE(instance_node_ptr);
         }
      }
      LIST_ADVANCE(block_node_ptr);
   }

   if (NULL != irm_ptr->core.report_payload_ptr)
   {
      posal_memory_free(irm_ptr->core.report_payload_ptr);
      irm_ptr->core.report_payload_ptr = NULL;
      AR_MSG(DBG_HIGH_PRIO, "IRM: report payload old report payload freed");
   }

   if (0 != total_size)
   {
      total_size += sizeof(irm_rtm_header_t);
      total_size += sizeof(param_id_report_metrics_t);

      // Allocate payload for report payload + memory for storing prev statistics.
      irm_ptr->core.report_payload_ptr =
         (uint8_t *)posal_memory_malloc(ALIGN_8_BYTES(total_size) + total_prev_metric_payload_size, irm_ptr->heap_id);
      if (NULL == irm_ptr->core.report_payload_ptr)
      {
         result = AR_ENOMEMORY;
         return result;
      }
      memset((void *)irm_ptr->core.report_payload_ptr, 0, total_size);
   }
   irm_ptr->core.report_payload_size = total_size;
   AR_MSG(DBG_HIGH_PRIO,
          "IRM: report payload created report_payload_ptr = 0x%X, size = %lu, metric payload size = %lu",
          irm_ptr->core.report_payload_ptr,
          irm_ptr->core.report_payload_size,
          total_prev_metric_payload_size);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Populated the BID, IID, MID and payload size information in the report payload
 1. Loop through the Block nodes and instance node to fill BID and IID information in the report payload
 2. Loop through the metric list per block per instance to fill the MID, size information
 3. Update other params like num blocks, proc domain etc
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_populate_report_payload_info(irm_t *irm_ptr)
{
   ar_result_t      result         = AR_EOK;
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
   uint32_t         num_blocks     = 0;
   uint8_t         *payload_ptr    = irm_ptr->core.report_payload_ptr;
   uint8_t *prev_statistics_ptr = irm_ptr->core.report_payload_ptr + ALIGN_8_BYTES(irm_ptr->core.report_payload_size);
   uint32_t host_domain_id      = 0xFFFFFFFF;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   if (NULL == payload_ptr)
   {
      return result;
   }
   irm_rtm_header_t *rtm_header_ptr              = (irm_rtm_header_t *)(payload_ptr);
   rtm_header_ptr->version                       = 0;
   rtm_header_ptr->header_size                   = 0;
   rtm_header_ptr->rtm_header.module_instance_id = IRM_MODULE_INSTANCE_ID;
   rtm_header_ptr->rtm_header.param_id           = PARAM_ID_IRM_REPORT_METRICS;
   rtm_header_ptr->rtm_header.param_size         = irm_ptr->core.report_payload_size - sizeof(irm_rtm_header_t);

   param_id_report_metrics_t *param_id_report_metric_ptr = (param_id_report_metrics_t *)(rtm_header_ptr + 1);

   irm_report_metrics_block_t *report_metrics_block_ptr =
      (irm_report_metrics_block_t *)(param_id_report_metric_ptr + 1);

   for (; NULL != block_node_ptr; block_node_ptr = block_node_ptr->next_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;
         for (; NULL != instance_node_ptr; instance_node_ptr = instance_node_ptr->next_ptr)
         {
            irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
            if (NULL != instance_obj_ptr)
            {
               uint32_t             num_metrics              = 0;
               uint32_t             all_metrics_payload_size = 0;
               spf_list_node_t     *metric_node_ptr          = instance_obj_ptr->head_node_ptr;
               irm_report_metric_t *report_metric_ptr        = (irm_report_metric_t *)(report_metrics_block_ptr + 1);
               for (; NULL != metric_node_ptr; metric_node_ptr = metric_node_ptr->next_ptr)
               {
                  irm_node_obj_t *metric_obj_ptr            = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
                  uint32_t        total_metric_payload_size = 0;
                  if (NULL != metric_obj_ptr)
                  {
                     report_metric_ptr->metric_id = metric_obj_ptr->id;
                     // Store the location of the payload in the Metric node
                     // This is later used to fill the statistics information directly to the IRM payload
                     metric_obj_ptr->metric_info.metric_payload_ptr = (irm_report_metric_t *)report_metric_ptr;
                     metric_obj_ptr->metric_info.prev_statistic_ptr = (void *)prev_statistics_ptr;

                     uint32_t metric_payload_size = irm_get_metric_payload_size(report_metric_ptr->metric_id);
                     irm_report_metric_payload_t *report_metric_payload =
                        (irm_report_metric_payload_t *)(report_metric_ptr + 1);

                     report_metric_payload->payload_size = metric_payload_size;

                     AR_MSG(DBG_HIGH_PRIO,
                            "IRM: Enabled metric during recreate payload, block id = %lu, instance id = 0x%X, "
                            "metric id = 0x%X, payload ptr = 0x%X, prev_statistic_ptr = 0x%X",
                            block_obj_ptr->id,
                            instance_obj_ptr->id,
                            metric_obj_ptr->id,
                            metric_obj_ptr->metric_info.metric_payload_ptr,
                            metric_obj_ptr->metric_info.prev_statistic_ptr);

                     total_metric_payload_size =
                        (sizeof(irm_report_metric_t) + (irm_ptr->core.num_profiles_per_report *
                                                        (sizeof(irm_report_metric_payload_t) + metric_payload_size)));
                     all_metrics_payload_size += total_metric_payload_size;
                     report_metric_ptr =
                        (irm_report_metric_t *)(((uint8_t *)report_metric_ptr) + total_metric_payload_size);
                     prev_statistics_ptr += irm_get_prev_metric_payload_size(metric_obj_ptr->id);
                     num_metrics++;
                  }
               }

               report_metrics_block_ptr->block_id       = block_obj_ptr->id;
               report_metrics_block_ptr->instance_id    = instance_obj_ptr->id;
               report_metrics_block_ptr->num_metric_ids = num_metrics;
               num_blocks++;
               report_metrics_block_ptr = (irm_report_metrics_block_t *)(report_metrics_block_ptr + 1);
               report_metrics_block_ptr =
                  (irm_report_metrics_block_t *)(((uint8_t *)report_metrics_block_ptr) + all_metrics_payload_size);
            }
         }
      }
   }

   param_id_report_metric_ptr->num_blocks  = num_blocks;
   param_id_report_metric_ptr->proc_domain = host_domain_id;
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 Does the clean up of IRM data base, removes all nodes
----------------------------------------------------------------------------------------------------------------------*/
void irm_clean_up_all_nodes(irm_t *irm_ptr)
{
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
   for (; NULL != block_node_ptr; block_node_ptr = block_node_ptr->next_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;
         for (; NULL != instance_node_ptr; instance_node_ptr = instance_node_ptr->next_ptr)
         {
            irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
            if (NULL != instance_obj_ptr)
            {
               irm_delete_list(&instance_obj_ptr->head_node_ptr);
            }
         }
         irm_delete_list(&block_obj_ptr->head_node_ptr);
      }
   }
   irm_delete_list(&irm_ptr->core.block_head_node_ptr);
}

/*----------------------------------------------------------------------------------------------------------------------
 Does the clean up of IRM Cached enable all metrics tree
----------------------------------------------------------------------------------------------------------------------*/
void irm_clean_up_enable_all(irm_t *irm_ptr)
{
   spf_list_node_t *block_node_ptr = irm_ptr->tmp_metric_payload_list;
   for (; NULL != block_node_ptr; block_node_ptr = block_node_ptr->next_ptr)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         // Clean up cached metrics for the block
         irm_delete_list(&block_obj_ptr->head_node_ptr);
      }
   }
   irm_delete_list(&irm_ptr->tmp_metric_payload_list);
}

/*----------------------------------------------------------------------------------------------------------------------
 Does the clean up of IRM data base
  1. Cleans up any list node which doesn't have any object
  2. Cleans up obj and list node if the head pointer is NULL
----------------------------------------------------------------------------------------------------------------------*/
void irm_cleanup_stray_nodes(irm_t *irm_ptr)
{
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;

   for (; NULL != block_node_ptr;)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL == block_obj_ptr)
      {
         irm_delete_node(&irm_ptr->core.block_head_node_ptr, &block_node_ptr);
         continue;
      }
      spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;

      for (; NULL != instance_node_ptr;)
      {
         irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
         if (NULL == instance_obj_ptr)
         {
            irm_delete_node(&block_obj_ptr->head_node_ptr, &instance_node_ptr);
            continue;
         }

         spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;
         for (; NULL != metric_node_ptr;)
         {
            irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
            if (NULL == metric_obj_ptr)
            {
               irm_delete_node(&instance_obj_ptr->head_node_ptr, &metric_node_ptr);
               continue;
            }
            LIST_ADVANCE(metric_node_ptr);
         }

         if (NULL == instance_obj_ptr->head_node_ptr)
         {
            irm_delete_node(&block_obj_ptr->head_node_ptr, &instance_node_ptr);
            continue;
         }
         LIST_ADVANCE(instance_node_ptr);
      }

      if (NULL == block_obj_ptr->head_node_ptr)
      {
         irm_delete_node(&irm_ptr->core.block_head_node_ptr, &block_node_ptr);
         continue;
      }
      LIST_ADVANCE(block_node_ptr);
   }
}

/*----------------------------------------------------------------------------------------------------------------------
   Returns metric specific payload size based on metric ID
----------------------------------------------------------------------------------------------------------------------*/
uint32_t irm_get_metric_payload_size(uint32_t metric_id)
{
   uint32_t size = 0;
   switch (metric_id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         size = sizeof(irm_metric_id_processor_cycles_t);
         break;
      }
      case IRM_BASIC_METRIC_ID_CURRENT_CLOCK:
      {
         size = sizeof(irm_metric_id_current_clock_t);
         break;
      }
      case IRM_METRIC_ID_HEAP_INFO:
      {
         size = sizeof(irm_metric_id_heap_info_t) + (IRM_MAX_NUM_HEAP_ID * sizeof(irm_per_heap_id_info_payload_t));
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         size = sizeof(irm_metric_id_packet_count_t);
         break;
      }
      case IRM_METRIC_ID_MEM_TRANSACTIONS:
      {
         size = sizeof(irm_metric_id_mem_transactions_t);
         break;
      }
      case IRM_METRIC_ID_Q6_HW_INFO:
      {
         size = sizeof(irm_metric_id_q6_hw_info_t);
         break;
      }
      case IRM_METRIC_ID_TIMESTAMP:
      {
         size = sizeof(irm_metric_id_timestamp_t);
         break;
      }
      case IRM_METRIC_ID_STACK_INFO:
      {
         size = sizeof(irm_metric_id_stack_info_t);
         break;
      }

      default:
      {
         break;
      }
   }
   return ALIGN_4_BYTES(size);
}

/*----------------------------------------------------------------------------------------------------------------------
   Returns prev metric specific payload size based on metric ID
----------------------------------------------------------------------------------------------------------------------*/
uint32_t irm_get_prev_metric_payload_size(uint32_t metric_id)
{
   uint32_t size = 0;
   switch (metric_id)
   {
      case IRM_METRIC_ID_PROCESSOR_CYCLES:
      {
         size = sizeof(irm_prev_metric_processor_cycles_t);
         break;
      }
      case IRM_METRIC_ID_PACKET_COUNT:
      {
         size = sizeof(irm_prev_metric_packet_count_t);
         break;
      }
      case IRM_METRIC_ID_MEM_TRANSACTIONS:
      {
         size = sizeof(irm_prev_metric_mem_transactions_t);
         break;
      }
      case IRM_BASIC_METRIC_ID_CURRENT_CLOCK:
      case IRM_METRIC_ID_HEAP_INFO:
      case IRM_METRIC_ID_Q6_HW_INFO:
      case IRM_METRIC_ID_TIMESTAMP:
      case IRM_METRIC_ID_STACK_INFO:
      {
         size = 0;
         break;
      }
      default:
      {
         break;
      }
   }
   return ALIGN_4_BYTES(size);
}
/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_handle_new_config(irm_t *irm_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   // recreate the report payload using the updated information
   TRY(result, irm_recreate_report_payload(irm_ptr));

   // Fill the report payload with BID, IID, MID and size information
   TRY(result, irm_populate_report_payload_info(irm_ptr));

   TRY(result, irm_profiler_init(irm_ptr));

   if (irm_ptr->core.is_profiling_started)
   {
      posal_timer_destroy(&irm_ptr->irm_report_timer);
      irm_ptr->core.is_profiling_started     = FALSE;
      irm_ptr->core.timer_tick_counter       = 0;
      irm_ptr->core.previous_timer_tick_time = 0;
      AR_MSG(DBG_HIGH_PRIO, "IRM: Timer destroyed");
   }

   posal_signal_clear(irm_ptr->timer_signal);
   // No need to restart the timer if there is not metric enabled
   if (irm_ptr->core.block_head_node_ptr)
   {
      TRY(result,
          posal_timer_create(&irm_ptr->irm_report_timer,
                             POSAL_TIMER_PERIODIC,
                             POSAL_TIMER_USER,
                             irm_ptr->timer_signal,
                             irm_ptr->heap_id));
      TRY(result, posal_timer_periodic_start(irm_ptr->irm_report_timer, irm_ptr->core.profiling_period_us));
      irm_ptr->core.is_profiling_started     = TRUE;
      irm_ptr->core.timer_tick_counter       = 0;
      irm_ptr->core.previous_timer_tick_time = posal_timer_get_time_in_msec();
      AR_MSG(DBG_HIGH_PRIO, "IRM: Timer started");
   }
   result = irm_tst_fwk_override_event_send(irm_ptr);

   AR_MSG(DBG_HIGH_PRIO, "IRM: After handle new config block_head_node_ptr = 0x%X", irm_ptr->core.block_head_node_ptr);
   CATCH(result, IRM_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "IRM: irm_handle_new_config failed");
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void irm_reset_report_payload(irm_t *irm_ptr)
{
   if (0 == irm_ptr->core.report_payload_size || NULL == irm_ptr->core.report_payload_ptr)
   {
      return;
   }
   spf_list_node_t *block_node_ptr = irm_ptr->core.block_head_node_ptr;
   for (; NULL != block_node_ptr;)
   {
      irm_node_obj_t *block_obj_ptr = (irm_node_obj_t *)block_node_ptr->obj_ptr;
      if (NULL != block_obj_ptr)
      {
         spf_list_node_t *instance_node_ptr = block_obj_ptr->head_node_ptr;
         for (; NULL != instance_node_ptr;)
         {
            irm_node_obj_t *instance_obj_ptr = (irm_node_obj_t *)instance_node_ptr->obj_ptr;
            if (NULL != instance_obj_ptr)
            {
               spf_list_node_t *metric_node_ptr = instance_obj_ptr->head_node_ptr;
               for (; NULL != metric_node_ptr;)
               {
                  irm_node_obj_t *metric_obj_ptr = (irm_node_obj_t *)metric_node_ptr->obj_ptr;
                  if (NULL != metric_obj_ptr)
                  {
                     irm_report_metric_t         *report_metric_ptr = metric_obj_ptr->metric_info.metric_payload_ptr;
                     irm_report_metric_payload_t *report_metric_payload_ptr =
                        (irm_report_metric_payload_t *)(report_metric_ptr + 1);
                     for (int32_t i = 0; i < irm_ptr->core.num_profiles_per_report; i++)
                     {
                        report_metric_payload_ptr->frame_size_ms = 0;
                        report_metric_payload_ptr->is_valid      = 0;
                        report_metric_payload_ptr =
                           (irm_report_metric_payload_t *)(((uint8_t *)(report_metric_payload_ptr + 1)) +
                                                           irm_get_metric_payload_size(metric_obj_ptr->id));
                     }
                  }
                  LIST_ADVANCE(metric_node_ptr);
               }
            }
            LIST_ADVANCE(instance_node_ptr);
         }
      }
      LIST_ADVANCE(block_node_ptr);
   }
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t irm_send_rtm_packet(irm_t *irm_ptr)
{
   if ((0 != irm_ptr->core.report_payload_size) && (NULL != irm_ptr->core.report_payload_ptr))
   {
      uint64_t              timestamp = irm_ptr->core.previous_timer_tick_time;
      posal_data_log_info_t log_info;
      memset(&log_info, 0, sizeof(posal_data_log_info_t));

      log_info.log_time_stamp = timestamp;
      log_info.log_code       = (IRM_LOG_CODE);
      log_info.session_id     = irm_ptr->core.sessoin_counter;
      log_info.log_tap_id     = 0;
      log_info.buf_size       = irm_ptr->core.report_payload_size;
      log_info.data_fmt       = LOG_DATA_FMT_BITSTREAM;

      void *log_payload_ptr =
         posal_data_log_alloc(irm_ptr->core.report_payload_size, (IRM_LOG_CODE), LOG_DATA_FMT_BITSTREAM);
      if (NULL == log_payload_ptr)
      {
         AR_MSG(DBG_HIGH_PRIO, "IRM: Failed to allocate log packet, dropping the collected data");
         return AR_EOK;
      }
      AR_MSG(DBG_HIGH_PRIO,
             "IRM: copying to RTM packet, log_payload_ptr = 0x%X, report_payload_ptr = 0x%X, size = %lu",
             log_payload_ptr,
             irm_ptr->core.report_payload_ptr,
             irm_ptr->core.report_payload_size);

      memscpy(log_payload_ptr,
              irm_ptr->core.report_payload_size,
              (void *)irm_ptr->core.report_payload_ptr,
              irm_ptr->core.report_payload_size);
      (void)posal_data_log_commit((void *)log_payload_ptr, (posal_data_log_info_t *)&log_info);
   }
   return AR_EOK;
}

ar_result_t irm_send_get_cfg_gpr_resp(gpr_packet_t *gpr_rsp_pkt_ptr,
                                  gpr_packet_t *gpr_pkt_ptr,
                                  uint32_t      payload_size,
                                  uint8_t      *cmd_payload_ptr,
                                  bool_t        is_oob)
{
   ar_result_t            result              = AR_EOK;
   apm_cmd_rsp_get_cfg_t *cmd_get_cfg_rsp_ptr = NULL;

   if (!is_oob)
   {
      cmd_get_cfg_rsp_ptr         = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, gpr_rsp_pkt_ptr);
      cmd_get_cfg_rsp_ptr->status = result;
      result                      = __gpr_cmd_async_send(gpr_rsp_pkt_ptr);
      if (AR_EOK != result)
      {
         __gpr_cmd_free(gpr_rsp_pkt_ptr);
         return AR_EFAILED;
      }
   }
   else
   {
      posal_cache_flush_v2((posal_mem_addr_t)cmd_payload_ptr, payload_size);
      apm_cmd_rsp_get_cfg_t cmd_get_cfg_rsp = { 0 };
      cmd_get_cfg_rsp.status                = result;

      gpr_cmd_alloc_send_t args;
      args.src_domain_id = gpr_pkt_ptr->dst_domain_id;
      args.dst_domain_id = gpr_pkt_ptr->src_domain_id;
      args.src_port      = gpr_pkt_ptr->dst_port;
      args.dst_port      = gpr_pkt_ptr->src_port;
      args.token         = gpr_pkt_ptr->token;
      args.opcode        = APM_CMD_RSP_GET_CFG;
      args.payload       = &cmd_get_cfg_rsp;
      args.payload_size  = sizeof(apm_cmd_rsp_get_cfg_t);
      args.client_data   = 0;
      result             = __gpr_cmd_alloc_send(&args);
   }
   return result;
}

ar_result_t irm_alloc_cmd_rsp_for_mem_stat(spf_msg_t     *msg_ptr,
                                           gpr_packet_t **gpr_rsp_pkt_pptr,
                                           uint32_t       payload_size,
                                           uint32_t       rsp_opcode)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   gpr_packet_t *gpr_rsp_pkt_ptr;

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   gpr_cmd_alloc_ext_t args;

   args.src_domain_id = gpr_pkt_ptr->dst_domain_id;
   args.dst_domain_id = gpr_pkt_ptr->src_domain_id;
   args.src_port      = gpr_pkt_ptr->dst_port;
   args.dst_port      = gpr_pkt_ptr->src_port;
   args.token         = gpr_pkt_ptr->token;
   args.opcode        = rsp_opcode;
   args.payload_size  = payload_size;
   args.ret_packet    = &gpr_rsp_pkt_ptr;
   args.client_data   = 0;

   /** Allocate GPR response packet  */
   if (AR_EOK != (result = __gpr_cmd_alloc_ext(&args)))
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to allocate rsp payload, result: %lu", result);
      return result;
   }

   *gpr_rsp_pkt_pptr = gpr_rsp_pkt_ptr;

   /** Free GPR command packet */
   result = __gpr_cmd_free(gpr_pkt_ptr);

   return result;
}

static ar_result_t irm_get_memory_stats(spf_msg_t *msg_ptr, uint32_t rsp_opcode)
{
   gpr_packet_t *gpr_rsp_pkt_ptr;
   uint32_t      payload_size;
   ar_result_t   result = AR_EOK;

   /** Allocate the response packet*/
   payload_size = sizeof(irm_cmd_rsp_get_memory_stats_t);
   result       = irm_alloc_cmd_rsp_for_mem_stat(msg_ptr, &gpr_rsp_pkt_ptr, payload_size, rsp_opcode);
   if (result != AR_EOK)
   {
      AR_MSG(DBG_ERROR_PRIO, "Failed to allocate rsp_payload, result: %lu", result);
      /** Get the pointer to GPR command */
      gpr_packet_t *gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;
      /** End the GPR command */
      __gpr_cmd_end_command(gpr_pkt_ptr, result);
      return result;
   }

   /** Fill in response packet */
   irm_cmd_rsp_get_memory_stats_t *cmd_rsp_payload_ptr =
      GPR_PKT_GET_PAYLOAD(irm_cmd_rsp_get_memory_stats_t, gpr_rsp_pkt_ptr);
   // uint32_t list_start_idx = spf_list_get_num_created_lists();
   // AR_MSG(DBG_HIGH_PRIO, "Allocated %lu lists", list_start_idx);
   spf_list_buf_pool_reset(POSAL_HEAP_DEFAULT);
   posal_queue_pool_reset(POSAL_HEAP_DEFAULT);

   spf_list_buf_pool_reset(posal_get_island_heap_id());
   posal_queue_pool_reset(posal_get_island_heap_id());

   irm_buf_pool_reset();
   cmd_rsp_payload_ptr->num_mallocs                 = posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].num_mallocs;
   cmd_rsp_payload_ptr->num_frees                   = posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].num_frees;
   cmd_rsp_payload_ptr->current_heap_use            = posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].curr_heap;
   cmd_rsp_payload_ptr->peak_heap_use               = posal_globalstate.avs_stats[POSAL_DEFAULT_HEAP_INDEX].peak_heap;
   cmd_rsp_payload_ptr->num_non_mallocs             = posal_globalstate.non_avs_stats.num_mallocs;
   cmd_rsp_payload_ptr->num_non_frees               = posal_globalstate.non_avs_stats.num_frees;
   cmd_rsp_payload_ptr->current_non_heap_use        = posal_globalstate.non_avs_stats.curr_heap;
   cmd_rsp_payload_ptr->peak_non_heap_use           = posal_globalstate.non_avs_stats.peak_heap;
   cmd_rsp_payload_ptr->num_nondefault_mallocs      = 0;
   cmd_rsp_payload_ptr->num_nondefault_frees        = 0;
   cmd_rsp_payload_ptr->current_nondefault_heap_use = 0;
   cmd_rsp_payload_ptr->peak_nondefault_heap_use    = 0;
   for (uint32_t num_heap = 1; num_heap <= POSAL_HEAP_MGR_MAX_NUM_HEAPS; num_heap++)
   {
      cmd_rsp_payload_ptr->num_nondefault_mallocs += posal_globalstate.avs_stats[num_heap].num_mallocs;
      cmd_rsp_payload_ptr->num_nondefault_frees += posal_globalstate.avs_stats[num_heap].num_frees;
      cmd_rsp_payload_ptr->current_nondefault_heap_use += posal_globalstate.avs_stats[num_heap].curr_heap;
      cmd_rsp_payload_ptr->peak_nondefault_heap_use += posal_globalstate.avs_stats[num_heap].peak_heap;
   }

   /** Send response packet */
   result = __gpr_cmd_async_send(gpr_rsp_pkt_ptr);
   if (AR_EOK != result)
   {
      __gpr_cmd_free(gpr_rsp_pkt_ptr);
   }
   return result;
}

static bool_t irm_is_valid_block_id(uint32_t block_id)
{
   switch (block_id)
   {
      case IRM_BLOCK_ID_PROCESSOR:
      case IRM_BLOCK_ID_CONTAINER:
      case IRM_BLOCK_ID_MODULE:
      case IRM_BLOCK_ID_POOL:
      case IRM_BLOCK_ID_STATIC_MODULE:
      {
         return true;
      }
      default:
         return false;
   }
}
