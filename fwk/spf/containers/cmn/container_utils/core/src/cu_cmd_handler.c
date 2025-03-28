/**
 * \file cu_cmd_handler.c
 * \brief
 *     This file contains container utility functions for command handling.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "cu_i.h"
#include "apm.h"
#include "irm_cntr_if.h"
#include "apm_cntr_debug_if.h"
#include "apm_debug_info.h"

static ar_result_t cu_set_get_cfgs_packed_loop(cu_base_t *         me_ptr,
                                               uint8_t *           data_ptr,
                                               uint32_t            miid,
                                               uint32_t            payload_size,
                                               bool_t              is_set_cfg,
                                               bool_t              is_oob,
                                               bool_t              is_deregister,
                                               spf_cfg_data_type_t cfg_type);

static ar_result_t cu_handle_module_event_reg_dereg(cu_base_t *       me_ptr,
                                                    uint32_t          module_instance_id,
                                                    topo_reg_event_t *reg_event_payload,
                                                    bool_t            is_register);

/* =======================================================================
  Public Function Definitions
========================================================================== */

ar_result_t cu_cntr_rtm_dump_data_port_media_fmt(cu_base_t *base_ptr, int8_t *param_payload_ptr, uint32_t *param_size_ptr)
{
   ar_result_t result = AR_EOK;
   if (*param_size_ptr < sizeof(cntr_port_mf_param_data_cfg_t))
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "cu_cntr_get_data_port_media_fmt();Wrong payload size %lu ",
             *param_size_ptr);
   }

   cntr_port_mf_param_data_cfg_t *cmd_ptr = (cntr_port_mf_param_data_cfg_t *)param_payload_ptr;

   if (base_ptr->topo_vtbl_ptr->rtm_dump_data_port_media_fmt)
   {
      AR_MSG(DBG_HIGH_PRIO,"cmd_ptr->enable is %lu", cmd_ptr->enable);
      base_ptr->topo_vtbl_ptr->rtm_dump_data_port_media_fmt(base_ptr->topo_ptr,
                                                            base_ptr->gu_ptr->container_instance_id,
                                                            cmd_ptr->enable);
   }

   return result;
}

static ar_result_t cu_set_cntr_params(cu_base_t *base_ptr,
                                      uint32_t   miid,
                                      uint32_t   pid,
                                      int8_t *   param_payload_ptr,
                                      uint32_t * param_size_ptr,
                                      uint32_t * error_code_ptr)
{
   ar_result_t result = AR_EOK;
   switch (pid)
   {
      case CNTR_PARAM_ID_PATH_DELAY_CFG:
      {
         result          = cu_path_delay_cfg(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_PATH_DESTROY:
      {
         result          = cu_destroy_delay_path_cfg(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_CFG_SRC_MOD_DELAY_LIST:
      {
         result          = cu_cfg_src_mod_delay_list(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_DESTROY_SRC_MOD_DELAY_LIST:
      {
         result          = cu_destroy_src_mod_delay_list(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_VOICE_SESSION_INFO:
      {
         result          = cu_voice_session_cfg(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_PROC_DURATION:
      {
         if (*param_size_ptr < sizeof(cntr_param_id_proc_duration_t))
         {
            CU_MSG(base_ptr->gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "Wrong payload size %lu for PID 0x%lx; Min expected size == %lu",
                   *param_size_ptr,
                   pid,
                   sizeof(cntr_param_id_proc_duration_t));
            result = AR_EFAILED;
            break;
         }
         cntr_param_id_proc_duration_t *proc_dur_ptr = (cntr_param_id_proc_duration_t *)param_payload_ptr;

         if (0 == proc_dur_ptr->proc_duration_us)
         {
            CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Proc Duration cannot be 0. Failing.");
            result = AR_EFAILED;
            break;
         }

         base_ptr->cntr_proc_duration                 = proc_dur_ptr->proc_duration_us;
         base_ptr->flags.is_cntr_proc_dur_set_paramed = TRUE;
         CU_SET_ONE_FWK_EVENT_FLAG(base_ptr, proc_dur_change);

         if (base_ptr->voice_info_ptr)
         {
            base_ptr->voice_info_ptr->safety_margin_us =
               MAX(proc_dur_ptr->safety_margin_us, proc_dur_ptr->proc_duration_us);
         }

         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "Container Proc Duration %lu us set on the container, safety margin %lu us (max is chosen). voice info "
                "0x%p",
                base_ptr->cntr_proc_duration,
                proc_dur_ptr->safety_margin_us,
                base_ptr->voice_info_ptr);

         result = base_ptr->cntr_vtbl_ptr->handle_proc_duration_change(base_ptr);

         break;
      }
      case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
      {
         result          = cu_cntr_rtm_dump_data_port_media_fmt(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      default:
      {
         result = cu_dcm_island_entry_exit_handler(base_ptr, param_payload_ptr, param_size_ptr, pid);
         if (AR_EOK != result)
         {
            CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Unexpected param-id 0x%lX", pid);
            result = AR_EUNEXPECTED;
         }
         break;
      }
   }
   return result;
}

static ar_result_t cu_get_cntr_params(cu_base_t *base_ptr,
                                      uint32_t   miid,
                                      uint32_t   pid,
                                      int8_t *   param_payload_ptr,
                                      uint32_t * param_size_ptr,
                                      uint32_t * error_code_ptr)
{
   ar_result_t result = AR_EOK;
   switch (pid)
   {
      case CNTR_PARAM_ID_PATH_DELAY_CFG:
      {
         result          = cu_path_delay_cfg(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_PATH_DESTROY:
      {
         result          = cu_destroy_delay_path_cfg(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_CFG_SRC_MOD_DELAY_LIST:
      {
         result          = cu_cfg_src_mod_delay_list(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_DESTROY_SRC_MOD_DELAY_LIST:
      {
         result          = cu_destroy_src_mod_delay_list(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_CONTAINER_PROC_PARAMS_INFO:
      {
         result          = cu_cntr_proc_params_query(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      case CNTR_PARAM_ID_GET_PROF_INFO:
      {
         result = cu_cntr_get_prof_info(base_ptr, param_payload_ptr, param_size_ptr);
         *error_code_ptr = result;
         break;
      }
      default:
      {
         CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Unexpected param-id 0x%lX", pid);
         result = AR_EUNEXPECTED;
         break;
      }
   }
   return result;
}

ar_result_t cu_set_get_cfg_wrapper(cu_base_t                         *base_ptr,
                                   uint32_t                           miid,
                                   uint32_t                           pid,
                                   int8_t                            *param_payload_ptr,
                                   uint32_t                          *param_size_ptr,
                                   uint32_t                          *error_code_ptr,
                                   bool_t                             is_set_cfg,
                                   bool_t                             is_deregister,
                                   spf_cfg_data_type_t                cfg_type,
                                   cu_handle_rest_ctx_for_set_cfg_t **pending_set_cfg_ctx_pptr)
{
   ar_result_t result = AR_EOK;

   if (miid ==  base_ptr->gu_ptr->container_instance_id)
   {
      // handle (g)set-cfg to containers
      if (is_set_cfg)
      {
         result = cu_set_cntr_params(base_ptr, miid, pid, param_payload_ptr, param_size_ptr, error_code_ptr);
      }
      else
      {
         result = cu_get_cntr_params(base_ptr, miid, pid, param_payload_ptr, param_size_ptr, error_code_ptr);
      }
   }
   else
   {
      gu_module_t *module_ptr = (gu_module_t *)gu_find_module(base_ptr->gu_ptr, miid);
      if (NULL == module_ptr)
      {
         CU_MSG(base_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Module 0x%08lX not found", miid);
         result = AR_EFAILED;
      }
      else
      {
         result = base_ptr->cntr_vtbl_ptr->set_get_cfg(base_ptr,
                                                       (void *)module_ptr,
                                                       pid,
                                                       param_payload_ptr,
                                                       param_size_ptr,
                                                       error_code_ptr,
                                                       is_set_cfg,
                                                       is_deregister,
                                                       cfg_type,
                                                       pending_set_cfg_ctx_pptr);
      }
   }

   return result;
}

ar_result_t cu_set_get_cfgs_packed(cu_base_t *me_ptr, gpr_packet_t *packet_ptr, spf_cfg_data_type_t cfg_type)
{
   ar_result_t result            = AR_EOK;
   bool_t      free_gpr_pkt_flag = FALSE;
   INIT_EXCEPTION_HANDLING

   apm_cmd_header_t *in_apm_cmd_header = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, packet_ptr);
   bool_t            is_out_of_band    = (0 != in_apm_cmd_header->mem_map_handle);
   bool_t            is_deregister     = FALSE; /*for setting persistence prop*/
   uint8_t *         param_data_ptr    = NULL;
   uint32_t          alignment_size    = 0;

   if (!is_out_of_band &&
       ((packet_ptr->opcode == APM_CMD_REGISTER_CFG) || (packet_ptr->opcode == APM_CMD_DEREGISTER_CFG) ||
        (packet_ptr->opcode == APM_CMD_REGISTER_SHARED_CFG) || (packet_ptr->opcode == APM_CMD_DEREGISTER_SHARED_CFG)))
   {
      result = AR_EFAILED;
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Expected out-of-band params, got in-band params, opcode 0x%X",
             packet_ptr->opcode);
      __gpr_cmd_end_command(packet_ptr, result);
      return result;
   }

   switch (packet_ptr->opcode)
   {
      case APM_CMD_SET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      case APM_CMD_REGISTER_SHARED_CFG:
      case APM_CMD_DEREGISTER_SHARED_CFG:
      {
         //CU_MSG(me_ptr->gu_ptr->log_id, DBG_MED_PRIO, "Set cfg received from GPR opcode 0x%lX", packet_ptr->opcode);
         if (is_out_of_band)
         {
            result = spf_svc_get_cmd_payload_addr(me_ptr->gu_ptr->log_id,
                                                  packet_ptr,
                                                  NULL,
                                                  (uint8_t **)&param_data_ptr,
                                                  &alignment_size,
                                                  NULL,
                                                  apm_get_mem_map_client());

            if (AR_EOK != result)
            {
               CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to get payload ptr");
               THROW(result, AR_EFAILED);
            }
         }
         else
         {
            param_data_ptr = (uint8_t *)(in_apm_cmd_header + 1);
         }

         if ((APM_CMD_DEREGISTER_CFG == packet_ptr->opcode) || (APM_CMD_DEREGISTER_SHARED_CFG == packet_ptr->opcode))
         {
            is_deregister = TRUE;
         }

         result = cu_set_get_cfgs_packed_loop(me_ptr,
                                              param_data_ptr,
                                              packet_ptr->dst_port,
                                              in_apm_cmd_header->payload_size,
                                              TRUE /* is_set_cfg */,
                                              is_out_of_band,
                                              is_deregister,
                                              cfg_type);

         if (!cu_is_any_handle_rest_pending(me_ptr))
         {
            if (is_out_of_band)
            {
               posal_cache_flush_v2(&param_data_ptr, in_apm_cmd_header->payload_size);
            }
         }
         break;
      }
      case APM_CMD_GET_CFG:
      {
         free_gpr_pkt_flag = TRUE;

         apm_cmd_rsp_get_cfg_t *cmd_get_cfg_rsp_ptr = NULL;
         gpr_packet_t *         gpr_rsp_pkt_ptr     = NULL;

         result = spf_svc_get_cmd_payload_addr(me_ptr->gu_ptr->log_id,
                                               packet_ptr,
                                               &gpr_rsp_pkt_ptr,
                                               (uint8_t **)&param_data_ptr,
                                               &alignment_size,
                                               NULL,
                                               apm_get_mem_map_client());
         if (AR_EOK != result)
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to get payload ptr");
            /* Send IBASIC with error */
            free_gpr_pkt_flag = FALSE;
            THROW(result, AR_EFAILED);
         }

         result = cu_set_get_cfgs_packed_loop(me_ptr,
                                              param_data_ptr,
                                              packet_ptr->dst_port,
                                              in_apm_cmd_header->payload_size,
                                              FALSE,          // is_set_param
                                              is_out_of_band, // is_oob
                                              is_deregister,
                                              cfg_type);

         // doesn't support handle-rest

         if (!is_out_of_band)
         {
            cmd_get_cfg_rsp_ptr         = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, gpr_rsp_pkt_ptr);
            cmd_get_cfg_rsp_ptr->status = result;
            result = __gpr_cmd_async_send(gpr_rsp_pkt_ptr);
            if (AR_EOK != result)
            {
               __gpr_cmd_free(gpr_rsp_pkt_ptr);
               THROW(result, AR_EFAILED);
            }
         }
         else
         {
            posal_cache_flush_v2(&param_data_ptr, in_apm_cmd_header->payload_size);
            apm_cmd_rsp_get_cfg_t cmd_get_cfg_rsp = { 0 };
            cmd_get_cfg_rsp.status                = result;

            gpr_cmd_alloc_send_t args;
            args.src_domain_id = packet_ptr->dst_domain_id;
            args.dst_domain_id = packet_ptr->src_domain_id;
            args.src_port      = packet_ptr->dst_port;
            args.dst_port      = packet_ptr->src_port;
            args.token         = packet_ptr->token;
            args.opcode        = APM_CMD_RSP_GET_CFG;
            args.payload       = &cmd_get_cfg_rsp;
            args.payload_size  = sizeof(apm_cmd_rsp_get_cfg_t);
            args.client_data   = 0;
            TRY(result, __gpr_cmd_alloc_send(&args));
         }
         break;
      }
      default:
      {
         break;
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   if (!cu_is_any_handle_rest_pending(me_ptr))
   {
      if (TRUE == free_gpr_pkt_flag)
      {
         __gpr_cmd_free(packet_ptr);
      }
      else
      {
         __gpr_cmd_end_command(packet_ptr, result);
      }
   }

   return result;
}

/**
 * Parse through multiple set or get configs which are packed in memory, and
 * call set_get_cfg() individually.
 */
static ar_result_t cu_set_get_cfgs_packed_loop(cu_base_t *         me_ptr,
                                               uint8_t *           data_ptr,
                                               uint32_t            miid,
                                               uint32_t            payload_size,
                                               bool_t              is_set_cfg,
                                               bool_t              is_oob,
                                               bool_t              is_deregister,
                                               spf_cfg_data_type_t cfg_type)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result            = AR_EOK;
   uint32_t    num_param_counter = 0;

   void *                 prev_handle_rest_ctx_ptr = me_ptr->handle_rest_ctx_ptr;
   cu_handle_rest_of_fn_t prev_handle_rest_fn      = me_ptr->handle_rest_fn;
   me_ptr->handle_rest_ctx_ptr                     = NULL;
   me_ptr->handle_rest_fn                          = NULL;

   VERIFY(result, me_ptr->cntr_vtbl_ptr && me_ptr->cntr_vtbl_ptr->set_get_cfg);

   while (0 < payload_size)
   {
      uint32_t one_param_size           = 0;
      uint32_t min_param_size           = 0;
      uint32_t param_module_instance_id = 0;
      uint32_t param_id                 = 0;
      uint32_t param_size               = 0;
      uint32_t param_header_size        = 0;
      uint32_t error_code               = 0;

      if (SPF_CFG_DATA_SHARED_PERSISTENT == cfg_type)
      {
         apm_module_param_shared_data_t *param_shared_data_ptr = (apm_module_param_shared_data_t *)data_ptr;
         param_header_size                                     = sizeof(apm_module_param_shared_data_t);
         if (payload_size < param_header_size)
         {
            break;
         }
         param_module_instance_id = miid;
         param_id                 = param_shared_data_ptr->param_id;
         param_size               = param_shared_data_ptr->param_size;
         min_param_size           = param_header_size + param_shared_data_ptr->param_size;
         one_param_size           = param_header_size + ALIGN_8_BYTES(param_shared_data_ptr->param_size);
      }
      else
      {
         apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)data_ptr;
         param_header_size                       = sizeof(apm_module_param_data_t);
         if (payload_size < param_header_size)
         {
            break;
         }
         if (cfg_type == SPF_CFG_DATA_PERSISTENT)
         {
            param_module_instance_id = miid;
         }
         else
         {
            param_module_instance_id = param_data_ptr->module_instance_id;
         }
         param_id   = param_data_ptr->param_id;
         param_size = param_data_ptr->param_size;
         error_code = param_data_ptr->error_code;

         min_param_size = param_header_size + param_data_ptr->param_size;
         one_param_size = param_header_size + ALIGN_8_BYTES(param_data_ptr->param_size);
      }

      bool_t skip_set   = FALSE;
      bool_t break_loop = FALSE;

      if (payload_size >= min_param_size)
      {
         VERIFY(result, param_module_instance_id == miid);

         int8_t *param_data_ptr = (int8_t *)(data_ptr + param_header_size);

         cu_handle_rest_ctx_for_set_cfg_t *pending_set_get_ctx_ptr = NULL;

         if (prev_handle_rest_ctx_ptr && prev_handle_rest_fn)
         {
            cu_handle_rest_ctx_for_set_cfg_t *set_get_ptr =
               (cu_handle_rest_ctx_for_set_cfg_t *)prev_handle_rest_ctx_ptr;

            if (set_get_ptr->param_payload_ptr == (int8_t *)param_data_ptr)
            {
               result |= set_get_ptr->overall_result;
               MFREE_NULLIFY(prev_handle_rest_ctx_ptr);
               prev_handle_rest_fn = NULL;
            }
            skip_set = TRUE;
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_MED_PRIO, "Warning: skipping set param");
         }

         if (!skip_set)
         {
            result |= cu_set_get_cfg_wrapper(me_ptr,
                                             miid,
                                             param_id,
                                             param_data_ptr,
                                             &param_size,
                                             &error_code,
                                             is_set_cfg,
                                             is_deregister,
                                             cfg_type,
                                             &pending_set_get_ctx_ptr);

            if (SPF_CFG_DATA_SHARED_PERSISTENT != cfg_type)
            {
               apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)data_ptr;
               param_data_ptr->param_size              = param_size;
               param_data_ptr->error_code              = error_code;
            }

            // after set-cfg if any handle rest if pending, break
            if (cu_is_any_handle_rest_pending(me_ptr))
            {
               if (pending_set_get_ctx_ptr)
               {
                  pending_set_get_ctx_ptr->overall_result = result;
               }
               break_loop = TRUE;
            }
         }
      }
      else
      {
         break_loop = TRUE;
      }

      if (min_param_size > one_param_size)
      {
         break;
      }
      else
      {
         num_param_counter++;
         data_ptr = (uint8_t *)(data_ptr + one_param_size);
         if (one_param_size > payload_size)
         {
            break;
         }
         else
         {
            payload_size = payload_size - one_param_size;
         }
      }

      if (break_loop)
      {
         break;
      }
   }

   if (!num_param_counter)
   {
      CU_MSG(me_ptr->gu_ptr->log_id, DBG_MED_PRIO, "Warning: entering set/get cfg with zero set/get cfgs applied.");
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   if (prev_handle_rest_fn)
   {
      MFREE_NULLIFY(prev_handle_rest_ctx_ptr);
      prev_handle_rest_fn = NULL;
   }

   return result;
}

/**
 * Parse through multiple set or get configs which are not packed in memory, and
 * call set_get_cfg() individually.
 */
ar_result_t cu_set_get_cfgs_fragmented(cu_base_t *               me_ptr,
                                       apm_module_param_data_t **param_data_pptr,
                                       uint32_t                  num_param_id_cfg,
                                       bool_t                    is_set_cfg,
                                       bool_t                    is_deregister,
                                       spf_cfg_data_type_t       cfg_type)
{
   ar_result_t result = AR_EOK;

   void *                 prev_handle_rest_ctx_ptr = me_ptr->handle_rest_ctx_ptr;
   cu_handle_rest_of_fn_t prev_handle_rest_fn      = me_ptr->handle_rest_fn;
   me_ptr->handle_rest_ctx_ptr                     = NULL;
   me_ptr->handle_rest_fn                          = NULL;

   INIT_EXCEPTION_HANDLING
   VERIFY(result, me_ptr->cntr_vtbl_ptr && me_ptr->cntr_vtbl_ptr->set_get_cfg);

   for (uint32_t i = 0; i < num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)param_data_pptr[i];

      int8_t *param_payload_ptr = (int8_t *)(param_data_ptr + 1);

      cu_handle_rest_ctx_for_set_cfg_t *pending_set_get_ctx_ptr = NULL;
      /**
       * Some params like PARAM_ID_REAL_MODULE_ID involve thread re-launch.
       * When thread re-launch is needed, we cannot continue set-cfg as subsequent params may need higher stack.
       * Due to this reason, we let the thread re-launch, come back for the rest of the cfg params.
       * We need to skip all earlier params. When handling rest, we need to skip until (& including) we reach the
       * place where we left off
       *
       * this handle_rest happens only for set-cfg (not get, as get doesn't need thread relaunch)
       */
      if (prev_handle_rest_ctx_ptr && prev_handle_rest_fn)
      {
         cu_handle_rest_ctx_for_set_cfg_t *set_get_ptr = (cu_handle_rest_ctx_for_set_cfg_t *)prev_handle_rest_ctx_ptr;
         if (set_get_ptr->param_payload_ptr == (int8_t *)param_payload_ptr)
         {
            result |= set_get_ptr->overall_result;
            MFREE_NULLIFY(prev_handle_rest_ctx_ptr);
            prev_handle_rest_fn = NULL;
         }
         continue;
      }

      result |= cu_set_get_cfg_wrapper(me_ptr,
                                       param_data_ptr->module_instance_id,
                                       param_data_ptr->param_id,
                                       param_payload_ptr,
                                       &param_data_ptr->param_size,
                                       &param_data_ptr->error_code,
                                       is_set_cfg,
                                       is_deregister,
                                       cfg_type,
                                       &pending_set_get_ctx_ptr);

      // after set-cfg if any new handle rest if pending, break
      if (cu_is_any_handle_rest_pending(me_ptr))
      {
         if (pending_set_get_ctx_ptr)
         {
            pending_set_get_ctx_ptr->overall_result = result;
         }
         break;
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   if (prev_handle_rest_fn)
   {
      MFREE_NULLIFY(prev_handle_rest_ctx_ptr);
      prev_handle_rest_fn = NULL;
   }

   return result;
}

/** Handles APM_CMD_REGISTER_MODULE_EVENTS gpr command */
ar_result_t cu_register_module_events(cu_base_t *me_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result     = AR_EOK;
   uint32_t    sent_count = 0;
   INIT_EXCEPTION_HANDLING

   apm_cmd_header_t *in_apm_cmd_header = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, packet_ptr);
   uint8_t *         payload           = NULL;
   uint32_t          alignment_size    = 0;
   uint32_t          payload_size      = in_apm_cmd_header->payload_size;
   gpr_packet_t *    temp_gpr_pkt_ptr  = NULL;

   VERIFY(result, me_ptr->cntr_vtbl_ptr && me_ptr->cntr_vtbl_ptr->register_events);

   result = spf_svc_get_cmd_payload_addr(me_ptr->gu_ptr->log_id,
                                         packet_ptr,
                                         &temp_gpr_pkt_ptr,
                                         (uint8_t **)&payload,
                                         &alignment_size,
                                         NULL,
                                         apm_get_mem_map_client());

   if (NULL != payload)
   {
      while (payload_size > 0)
      {
         apm_module_register_events_t *current_payload = (apm_module_register_events_t *)payload;
         if (sizeof(apm_module_register_events_t) > payload_size)
         {
            break;
         }

         uint32_t one_event_size = sizeof(apm_module_register_events_t) + current_payload->event_config_payload_size;

         if (payload_size >= one_event_size)
         {
            topo_reg_event_t reg_event_payload;
            memset(&reg_event_payload, 0 , sizeof(reg_event_payload));

            reg_event_payload.src_port                  = packet_ptr->src_port;
            reg_event_payload.src_domain_id             = packet_ptr->src_domain_id;
            reg_event_payload.dest_domain_id            = packet_ptr->dst_domain_id;
            reg_event_payload.event_id                  = current_payload->event_id;
            reg_event_payload.token                     = packet_ptr->token;
            reg_event_payload.event_cfg.actual_data_len = current_payload->event_config_payload_size;
            reg_event_payload.event_cfg.data_ptr        = (int8_t *)(current_payload + 1);

            if (current_payload->module_instance_id == me_ptr->gu_ptr->container_instance_id)
            {
               // events to the container
               return cu_handle_cntr_events_reg_dereg(me_ptr, &reg_event_payload, current_payload->is_register);
            }
            else
            {
               result |= cu_handle_module_event_reg_dereg(me_ptr,
                                                          current_payload->module_instance_id,
                                                          &reg_event_payload,
                                                          current_payload->is_register);
            }
         }
         payload += ALIGN_8_BYTES(one_event_size);
         payload_size -= one_event_size;
         sent_count++;
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Register events, payload_size: %d", payload_size);
      }
   }

   if (!sent_count)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Register events, 0 events registered, payload too small, size = %lu",
             payload_size);
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

gpr_heap_index_t cu_convert_apm_heap_id_to_gpr_index(uint32_t apm_heap_id)
{
   switch(apm_heap_id)
   {
      case APM_CONT_HEAP_LOW_POWER:
         return GPR_HEAP_INDEX_1;
      case APM_CONT_HEAP_DEFAULT:
         return GPR_HEAP_INDEX_DEFAULT;
      default:
         break;
   }
   return GPR_HEAP_INDEX_DEFAULT;
}

/** Handles APM_CMD_REGISTER_MODULE_EVENTS_V2 gpr command */
ar_result_t cu_register_module_events_v2(cu_base_t *me_ptr, gpr_packet_t *packet_ptr)
{
   ar_result_t result     = AR_EOK;
   uint32_t    sent_count = 0;
   INIT_EXCEPTION_HANDLING

   apm_cmd_header_t *in_apm_cmd_header = GPR_PKT_GET_PAYLOAD(apm_cmd_header_t, packet_ptr);
   uint8_t *         payload           = NULL;
   uint32_t          alignment_size    = 0;
   uint32_t          payload_size      = in_apm_cmd_header->payload_size;
   gpr_packet_t *    temp_gpr_pkt_ptr  = NULL;

   VERIFY(result, me_ptr->cntr_vtbl_ptr && me_ptr->cntr_vtbl_ptr->register_events);

   result = spf_svc_get_cmd_payload_addr(me_ptr->gu_ptr->log_id,
                                         packet_ptr,
                                         &temp_gpr_pkt_ptr,
                                         (uint8_t **)&payload,
                                         &alignment_size,
                                         NULL,
                                         apm_get_mem_map_client());

   if (NULL != payload)
   {
      while (payload_size > 0)
      {
         apm_module_register_events_v2_t *current_payload = (apm_module_register_events_v2_t *)payload;
         if (sizeof(apm_module_register_events_v2_t) > payload_size)
         {
            break;
         }

         uint32_t one_event_size = sizeof(apm_module_register_events_v2_t) + current_payload->event_config_payload_size;

         if (payload_size >= one_event_size)
         {
            topo_reg_event_t reg_event_payload;
            memset(&reg_event_payload, 0 , sizeof(reg_event_payload));

            // get the client gpr addr, heap id and token from the V2 apm_module_register_events_v2_t payload.
            reg_event_payload.gpr_heap_index            = cu_convert_apm_heap_id_to_gpr_index(current_payload->heap_id);
            reg_event_payload.src_port                  = current_payload->dst_port;
            reg_event_payload.src_domain_id             = current_payload->dst_domain_id;
            reg_event_payload.token                     = current_payload->client_token;

            reg_event_payload.dest_domain_id            = packet_ptr->dst_domain_id;
            reg_event_payload.event_id                  = current_payload->event_id;
            reg_event_payload.event_cfg.actual_data_len = current_payload->event_config_payload_size;
            reg_event_payload.event_cfg.data_ptr        = (int8_t *)(current_payload + 1);

            if (current_payload->module_instance_id == me_ptr->gu_ptr->container_instance_id)
            {
               // events to the container
               return cu_handle_cntr_events_reg_dereg(me_ptr, &reg_event_payload, current_payload->is_register);
            }
            else
            {
               result |= cu_handle_module_event_reg_dereg(me_ptr,
                                                          current_payload->module_instance_id,
                                                          &reg_event_payload,
                                                          current_payload->is_register);
            }
         }
         payload += ALIGN_8_BYTES(one_event_size);
         payload_size -= one_event_size;
         sent_count++;
         CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Register events, payload_size: %d", payload_size);
      }
   }

   if (!sent_count)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "Register events, 0 events registered, payload too small, size = %lu",
             payload_size);
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/** Common utiliity to handle module reg/de-reg. Sets event register/de-register command on the capi module.
 * Also stores the client info in the contianer, if the module support capi v1 event. */
static ar_result_t cu_handle_module_event_reg_dereg(cu_base_t *       me_ptr,
                                                    uint32_t          module_instance_id,
                                                    topo_reg_event_t *reg_event_ptr,
                                                    bool_t            is_register)
{
   ar_result_t result = AR_EOK;

   gu_module_t *module_ptr = gu_find_module(me_ptr->gu_ptr, module_instance_id);
   if (NULL == module_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_ERROR_PRIO,
             "register events: Module 0x%lx not found",
             module_instance_id);
      return result;
   }

   cu_module_t *cu_module_ptr          = (cu_module_t *)((uint8_t *)module_ptr + me_ptr->module_cu_offset);
   bool_t       capi_supports_v1_event = FALSE;
   result |=
      me_ptr->cntr_vtbl_ptr->register_events(me_ptr, module_ptr, reg_event_ptr, is_register, &capi_supports_v1_event);

   if (capi_supports_v1_event)
   {
      cu_client_info_t client_info;
      client_info.src_port       = reg_event_ptr->src_port;
      client_info.src_domain_id  = reg_event_ptr->src_domain_id;
      client_info.dest_domain_id = reg_event_ptr->dest_domain_id;

      if (is_register)
      {
         result = cu_event_add_client(me_ptr->gu_ptr->log_id,
                                      reg_event_ptr->event_id,
                                      &client_info,
                                      &cu_module_ptr->event_list_ptr,
                                      me_ptr->heap_id);
         if (AR_EOK != result)
         {
            CU_MSG(me_ptr->gu_ptr->log_id,
                   DBG_ERROR_PRIO,
                   "Add client to module event list failed, result: %d",
                   result);
         }
      }
      else
      {
         result = cu_event_delete_client(me_ptr->gu_ptr->log_id,
                                         reg_event_ptr->event_id,
                                         &client_info,
                                         &cu_module_ptr->event_list_ptr);
         if (AR_EOK != result)
         {
            TOPO_MSG(me_ptr->gu_ptr->log_id,
                     DBG_ERROR_PRIO,
                     "Delete client from module event list failed, result: %d",
                     result);
         }
      }
   }

   return result;
}

/**
 * Common handling of register/deregister events addressed to the container instance ID.
 */
ar_result_t cu_handle_cntr_events_reg_dereg(cu_base_t *me_ptr, topo_reg_event_t *reg_event_ptr, bool_t is_register)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   int8_t *event_cfg_ptr = NULL;

   CU_MSG(me_ptr->gu_ptr->log_id, DBG_MED_PRIO, "Container received event reg/deregistration");

   cu_client_info_t client_info;
   client_info.src_port                  = reg_event_ptr->src_port;
   client_info.src_domain_id             = reg_event_ptr->src_domain_id;
   client_info.dest_domain_id            = reg_event_ptr->dest_domain_id;
   client_info.token                     = reg_event_ptr->token;
   client_info.event_cfg.actual_data_len = reg_event_ptr->event_cfg.actual_data_len;
   client_info.event_cfg.data_ptr        = NULL;

   if (0 != client_info.event_cfg.actual_data_len)
   {
      event_cfg_ptr = (int8_t *)posal_memory_malloc(client_info.event_cfg.actual_data_len, me_ptr->heap_id);

      VERIFY(result, NULL != event_cfg_ptr);

      client_info.event_cfg.data_ptr = event_cfg_ptr;
      memscpy(client_info.event_cfg.data_ptr,
              client_info.event_cfg.actual_data_len,
              (reg_event_ptr->event_cfg.data_ptr),
              client_info.event_cfg.actual_data_len);
   }

   if (is_register)
   {
      result = cu_event_add_client(me_ptr->gu_ptr->log_id,
                                   reg_event_ptr->event_id,
                                   &client_info,
                                   &me_ptr->event_list_ptr,
                                   me_ptr->heap_id);
      if (AR_EOK != result)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Add client to container event list failed, result: %d",
                result);
      }
   }
   else
   {
      result =
         cu_event_delete_client(me_ptr->gu_ptr->log_id, reg_event_ptr->event_id, &client_info, &me_ptr->event_list_ptr);
      if (AR_EOK != result)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Delete client from container event list failed, result: %d",
                result);
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

ar_result_t cu_raise_container_events_to_clients(cu_base_t *me_ptr,
                                                 uint32_t   event_id,
                                                 int8_t *   payload_ptr,
                                                 uint32_t   payload_size)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *client_list_ptr;
   if (AR_EOK !=
       (result = cu_find_client_info(me_ptr->gu_ptr->log_id, event_id, me_ptr->event_list_ptr, &client_list_ptr)))
   {
      // Avoiding the error msg for this event to avoid spamming of logs.
      if (event_id != CNTR_EVENT_ID_CONTAINER_FRAME_DELIVERY_DONE)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "Failed to get client list for event id: 0x%lx, result: %d",
                event_id,
                result);
      }
      return result;
   }

   if (NULL == client_list_ptr)
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "CNTR 0x%lX: client did not register for the event id 0x%lX, failed to raise event",
             me_ptr->gu_ptr->container_instance_id,
             event_id);
      return result; // not returning any failure here
   }
   gpr_packet_t *      event_packet_ptr = NULL;
   apm_module_event_t *event_payload;

   for (cu_client_info_t *client_info_ptr = (cu_client_info_t *)(client_list_ptr->obj_ptr); (NULL != client_list_ptr);
        LIST_ADVANCE(client_list_ptr))
   {
      event_packet_ptr = NULL;
      gpr_cmd_alloc_ext_t args;
      args.src_domain_id = client_info_ptr->dest_domain_id;
      args.dst_domain_id = client_info_ptr->src_domain_id;
      args.src_port      = me_ptr->gu_ptr->container_instance_id;
      args.dst_port      = client_info_ptr->src_port;
      args.token         = client_info_ptr->token;
      args.opcode        = APM_EVENT_MODULE_TO_CLIENT;
      args.payload_size  = sizeof(apm_module_event_t) + payload_size;
      args.client_data   = 0;
      args.ret_packet    = &event_packet_ptr;
      result             = __gpr_cmd_alloc_ext(&args);
      if (NULL == event_packet_ptr)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CNTR 0x%lX: event 0x%lX NOT sent to client with error code %lu",
                me_ptr->gu_ptr->container_instance_id,
                event_id,
                result);
         return AR_EFAILED;
      }

      event_payload = GPR_PKT_GET_PAYLOAD(apm_module_event_t, event_packet_ptr);

      event_payload->event_id           = event_id;
      event_payload->event_payload_size = payload_size;

      memscpy(event_payload + 1, event_payload->event_payload_size, payload_ptr, payload_size);

      result = __gpr_cmd_async_send(event_packet_ptr);

      if (AR_EOK != result)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                "CNTR 0x%lX: Unable to send event  0x%lXto client with result %lu "
                "destn port: 0x%lX, destn domain ID: 0x%lX, src domain ID: 0x%lX ",
                me_ptr->gu_ptr->container_instance_id,
                event_id,
                result,
                client_info_ptr->src_port,
                client_info_ptr->src_domain_id,
                client_info_ptr->dest_domain_id);
         result = __gpr_cmd_free(event_packet_ptr);
      }
      else
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                "CNTR 0x%lX: event 0x%lX sent to client with result %lu "
                "destn port: 0x%lX, destn domain ID: 0x%lX, src domain ID: 0x%lX ",
                me_ptr->gu_ptr->container_instance_id,
                event_id,
                result,
                client_info_ptr->src_port,
                client_info_ptr->src_domain_id,
                client_info_ptr->dest_domain_id);
      }
   } // for loop over all clients

   return result;
}

/**
 * Common handling of graph connect. Parses through each connection, sanity checks
 * that the connection is new, and then assigns external port upstream/downstream
 * handles.
 */
ar_result_t cu_graph_connect(cu_base_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   spf_cntr_port_connect_info_t *connect_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_cntr_port_connect_info_t));

   connect_ptr = (spf_cntr_port_connect_info_t *)&header_ptr->payload_start;

   for (uint32_t i = 0; i < connect_ptr->num_ip_data_port_conn; i++)
   {
      spf_module_port_conn_t *ip_conn_ptr     = &connect_ptr->ip_data_port_conn_list_ptr[i];
      gu_ext_in_port_t *      ext_in_port_ptr = (gu_ext_in_port_t *)ip_conn_ptr->self_mod_port_hdl.port_ctx_hdl;
      if (ext_in_port_ptr->upstream_handle.spf_handle_ptr)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                SPF_LOG_PREFIX
                "Error! Ext in already connected. (mod-inst-id, port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx)",
                ip_conn_ptr->peer_mod_port_hdl.module_inst_id,
                ip_conn_ptr->peer_mod_port_hdl.module_port_id,
                ip_conn_ptr->self_mod_port_hdl.module_inst_id,
                ip_conn_ptr->self_mod_port_hdl.module_port_id);
         result |= AR_ENOTREADY;
      }
      else
      {
         ext_in_port_ptr->upstream_handle.spf_handle_ptr = ip_conn_ptr->peer_mod_port_hdl.port_ctx_hdl;
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "US Connection (mod-inst-id, port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx)",
                ip_conn_ptr->peer_mod_port_hdl.module_inst_id,
                ip_conn_ptr->peer_mod_port_hdl.module_port_id,
                ip_conn_ptr->self_mod_port_hdl.module_inst_id,
                ip_conn_ptr->self_mod_port_hdl.module_port_id);
      }
   }

   for (uint32_t i = 0; i < connect_ptr->num_op_data_port_conn; i++)
   {
      spf_module_port_conn_t *op_conn_ptr      = &connect_ptr->op_data_port_conn_list_ptr[i];
      gu_ext_out_port_t *     ext_out_port_ptr = (gu_ext_out_port_t *)op_conn_ptr->self_mod_port_hdl.port_ctx_hdl;
      if (ext_out_port_ptr->downstream_handle.spf_handle_ptr)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                SPF_LOG_PREFIX
                "Error! Ext out already connected (mod-inst-id, port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx)",
                op_conn_ptr->self_mod_port_hdl.module_inst_id,
                op_conn_ptr->self_mod_port_hdl.module_port_id,
                op_conn_ptr->peer_mod_port_hdl.module_inst_id,
                op_conn_ptr->peer_mod_port_hdl.module_port_id);

         result |= AR_ENOTREADY;
      }
      else
      {
         ext_out_port_ptr->downstream_handle.spf_handle_ptr = op_conn_ptr->peer_mod_port_hdl.port_ctx_hdl;
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "DS Connection (mod-inst-id, port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx)",
                op_conn_ptr->self_mod_port_hdl.module_inst_id,
                op_conn_ptr->self_mod_port_hdl.module_port_id,
                op_conn_ptr->peer_mod_port_hdl.module_inst_id,
                op_conn_ptr->peer_mod_port_hdl.module_port_id);
      }
   }

   for (uint32_t i = 0; i < connect_ptr->num_ctrl_port_conn; i++)
   {
      spf_module_port_conn_t *ctrl_conn_ptr     = &connect_ptr->ctrl_port_conn_list_ptr[i];
      gu_ext_ctrl_port_t *    ext_ctrl_port_ptr = (gu_ext_ctrl_port_t *)ctrl_conn_ptr->self_mod_port_hdl.port_ctx_hdl;
      if (ext_ctrl_port_ptr->peer_handle.spf_handle_ptr)
      {
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                SPF_LOG_PREFIX
                "Error! Control port already connected (mod-inst-id, ctrl-port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx) ",
                ctrl_conn_ptr->self_mod_port_hdl.module_inst_id,
                ctrl_conn_ptr->self_mod_port_hdl.module_port_id,
                ctrl_conn_ptr->peer_mod_port_hdl.module_inst_id,
                ctrl_conn_ptr->peer_mod_port_hdl.module_port_id);

         result |= AR_ENOTREADY;
      }
      else
      {
         ext_ctrl_port_ptr->peer_handle.spf_handle_ptr = ctrl_conn_ptr->peer_mod_port_hdl.port_ctx_hdl;

#ifdef VERBOSE_DEBUGGING
         uint32_t host_domain_id;
         __gpr_cmd_get_host_domain_id(&host_domain_id);
         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_MED_PRIO,
                "Control conn: host_domain_id = %lu, ctrl_conn_ptr->self_mod_port_hdl.domain_id = %lu, "
                "(ctrl_conn_ptr->peer_mod_port_hdl.domain_id = %lu, "
                "ext_ctrl_port_ptr->peer_domain_id %lu, "
                "ext_ctrl_port_ptr->peer_handle.spf_handle_ptr = 0x%lx",
                host_domain_id,
                ctrl_conn_ptr->self_mod_port_hdl.domain_id,
                ctrl_conn_ptr->peer_mod_port_hdl.domain_id,
                ext_ctrl_port_ptr->peer_domain_id,
                ext_ctrl_port_ptr->peer_handle.spf_handle_ptr);
#endif // VERBOSE_DEBUGGING

         if (NULL == ext_ctrl_port_ptr->peer_handle.spf_handle_ptr)
         {
            // could be offload case. Need to check if the self domain ID != peer domain ID
            if ((ctrl_conn_ptr->peer_mod_port_hdl.domain_id == ext_ctrl_port_ptr->peer_domain_id) &&
                (gu_is_domain_id_remote(ext_ctrl_port_ptr->peer_domain_id)))
            {
               CU_MSG(me_ptr->gu_ptr->log_id,
                      DBG_MED_PRIO,
                      "Connected an inter-proc control link from proc id %lu and proc id %lu",
                      ctrl_conn_ptr->self_mod_port_hdl.domain_id,
                      ctrl_conn_ptr->peer_mod_port_hdl.domain_id);
            }
         }
         me_ptr->cntr_vtbl_ptr->connect_ext_ctrl_port(me_ptr, ext_ctrl_port_ptr);

         CU_MSG(me_ptr->gu_ptr->log_id,
                DBG_HIGH_PRIO,
                SPF_LOG_PREFIX "Control Connection (mod-inst-id, ctrl-port-id): (0x%lx, 0x%lx) -> (0x%lx, 0x%lx) ",
                ctrl_conn_ptr->self_mod_port_hdl.module_inst_id,
                ctrl_conn_ptr->self_mod_port_hdl.module_port_id,
                ctrl_conn_ptr->peer_mod_port_hdl.module_inst_id,
                ctrl_conn_ptr->peer_mod_port_hdl.module_port_id);
      }
   }

   gu_attach_pending_elementary_modules(me_ptr->gu_ptr, me_ptr->heap_id);

   // sorted list may have changed after attaching the elementary modules.
   TRY(result, me_ptr->topo_vtbl_ptr->check_update_started_sorted_module_list(me_ptr->topo_ptr, FALSE));

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * Handling for the PREPARE control path command.
 */
ar_result_t cu_handle_prepare(cu_base_t *base_ptr, spf_msg_cmd_graph_mgmt_t *cmd_gmgmt_ptr)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;

   VERIFY(result, base_ptr->topo_vtbl_ptr && base_ptr->topo_vtbl_ptr->propagate_media_fmt);

   VERIFY(result,
          base_ptr->cntr_vtbl_ptr && base_ptr->cntr_vtbl_ptr->port_data_thresh_change &&
             base_ptr->cntr_vtbl_ptr->ext_out_port_apply_pending_media_fmt &&
             base_ptr->cntr_vtbl_ptr->update_path_delay);

   /**
    * If media fmt came before prepare, then it would be sitting at input
    *
    * When prepare is given for downstream SG, upstream ext ports also get prepare.
    *  Ideally, upstream shouldn't go through media fmt through all modules, but only start from ext out port.
    *  However, calling propagate_media_fmt shouldn't be an issue as port state checks are made appropriately in
    *  propagate_media_fmt. Similarly for multi-SG container cases (where one of the SG receives prepare).
    *  It's not possible to go through only connected list of modules as we don't know how they are connected.
    *  We have to go through sorted module list.
    */
   TRY(result, base_ptr->topo_vtbl_ptr->propagate_media_fmt(base_ptr->topo_ptr, FALSE /* is_data_path*/));

   // this also takes care of icb
   base_ptr->cntr_vtbl_ptr->port_data_thresh_change(base_ptr);

   for (gu_ext_in_port_list_t *ext_in_port_list_ptr = base_ptr->gu_ptr->ext_in_port_list_ptr;
           (NULL != ext_in_port_list_ptr);
           LIST_ADVANCE(ext_in_port_list_ptr))
   {
         gu_ext_in_port_t *ext_in_port_ptr = (gu_ext_in_port_t *)ext_in_port_list_ptr->ext_in_port_ptr;

         //If ICB info is sent when downstream container is not in start/prepare state, then it will get rejected.
         //did_inform_us_of_frame_len_and_var_ip FLAG must be set to FALSE.
         //So that when prepare is received on external input port it will enter cu_create_send_icb_info_msg_to_upstreams() and sends ICB info to create external buffers

         if (gu_is_port_handle_found_in_spf_array(cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle,
                 cmd_gmgmt_ptr->cntr_port_hdl_list.ip_port_handle_list_pptr,
                 &ext_in_port_ptr->this_handle))
         {
             cu_ext_in_port_t *gu_ext_in_port_ptr =
            (cu_ext_in_port_t *)(((uint8_t *)ext_in_port_ptr + base_ptr->ext_in_port_cu_offset));

             gu_ext_in_port_ptr->prop_info.did_inform_us_of_frame_len_and_var_ip = FALSE;

         }
   }

   //handle_frame_len_change ensures that the ICB info is sent to upstream.
   //if frame-len is evaluated during threshold propagation then it will be sent inside "port_data_thresh_change"
   //if frame-len is not evaluated due to the absence of media format then need to send RT/voice-SId ICB info here.
   cu_handle_frame_len_change(base_ptr, &base_ptr->cntr_frame_len, base_ptr->period_us);

   // media format msg has even container threshold information which is requried for downstream containers.
   // hence propagate ctrl path media fmt to peer container only after media format and threshold propagation.
   for (gu_ext_out_port_list_t *ext_out_port_list_ptr = base_ptr->gu_ptr->ext_out_port_list_ptr;
        (NULL != ext_out_port_list_ptr);
        LIST_ADVANCE(ext_out_port_list_ptr))
   {
      gu_ext_out_port_t *ext_out_port_ptr = (gu_ext_out_port_t *)ext_out_port_list_ptr->ext_out_port_ptr;

      if (gu_is_sg_id_found_in_spf_array(&cmd_gmgmt_ptr->sg_id_list, ext_out_port_ptr->sg_ptr->id) ||
          (gu_is_port_handle_found_in_spf_array(cmd_gmgmt_ptr->cntr_port_hdl_list.num_op_port_handle,
                                                cmd_gmgmt_ptr->cntr_port_hdl_list.op_port_handle_list_pptr,
                                                &ext_out_port_ptr->this_handle)))
      {

         // The containers are expected to check for the downstream handle and correspondingly apply the the media
         // format to the downstream. If the downstream is not available and the last module in the container is RD_EP
         // the container would raise the media format event to the registered clients

         // Send the media format event regardless of if it is changed or not. Even if we have sent the media format before
         // we can not guarantee that downstream has pulled it from the Q, downstream may have flushed the Q during upstream stop. (rapid stop->start->stop scenario)
         // Therefore we must always send it again at prepare.
         // Downstream container are responsible to handle it only if it is changed.
         base_ptr->cntr_vtbl_ptr->ext_out_port_apply_pending_media_fmt((void *)base_ptr, ext_out_port_ptr);
      }
   }

   base_ptr->cntr_vtbl_ptr->update_path_delay(base_ptr, CU_PATH_ID_ALL_PATHS);

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * Handling for a control path media format command. Check that upstream is
 * connected and that we didn't receive this command while running. Then,
 * converts the media format to the internal struct and calls the
 * container vtable function to handle a new input media format.
 */
ar_result_t cu_ctrl_path_media_fmt_cmd(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;

   INIT_EXCEPTION_HANDLING
   gu_ext_in_port_t *gu_ext_in_port_ptr;
   cu_ext_in_port_t *ext_in_port_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;

   VERIFY(result,
          base_ptr->cntr_vtbl_ptr && base_ptr->cntr_vtbl_ptr->input_media_format_received &&
             base_ptr->topo_vtbl_ptr->get_port_property);
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_media_format_t));

   // Media format comes for external input port only
   gu_ext_in_port_ptr = (gu_ext_in_port_t *)header_ptr->dst_handle_ptr;
   ext_in_port_ptr    = (cu_ext_in_port_t *)((uint8_t *)gu_ext_in_port_ptr + base_ptr->ext_in_port_cu_offset);

   topo_port_state_t port_state;
   base_ptr->topo_vtbl_ptr->get_port_property(base_ptr->topo_ptr,
                                              TOPO_DATA_INPUT_PORT_TYPE,
                                              PORT_PROPERTY_TOPO_STATE,
                                              (void *)gu_ext_in_port_ptr->int_in_port_ptr,
                                              (uint32_t *)&port_state);

   // For all commands addressed to ports, check if connection exists
   // (E.g. during disconnect cntr to cntr commands may be still sent. Such cmd must be dropped).
   if (NULL == gu_ext_in_port_ptr->upstream_handle.spf_handle_ptr)
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "CMD:CTRL_PATH_MEDIA_FMT: received when no connection is present. "
             "Dropping.");
   }
   else
   {
      /* if downgraded state of the port is 'STARTED' then ctrl path media fmt is dropped.
       * The SG of this port may be started though. Port state is already downgraded when
       * updating state at the end of command handling.
       * need to check both as we don't downgrade port_state for input ports during port op (US to DS, only data path)*/
      if ((TOPO_PORT_STATE_STARTED == ext_in_port_ptr->connected_port_state) && (TOPO_PORT_STATE_STARTED == port_state))
      {
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_ERROR_PRIO,
                " input media format control cmd received in start state. Module 0x%lX, port 0x%lx",
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id);
         result = AR_EUNSUPPORTED;
      }
      else
      {
         // get media format update cmd payload
         spf_msg_media_format_t *media_fmt_ptr = (spf_msg_media_format_t *)&header_ptr->payload_start;
         topo_media_fmt_t                       local_media_fmt;
         cu_ext_in_port_upstream_frame_length_t local_up_frame_len;
         CU_MSG(base_ptr->gu_ptr->log_id,
                DBG_MED_PRIO,
                "processing input media format control cmd, data_format = %lu. Module 0x%lX, port 0x%lx, "
                "upstream_max_frame_len_bytes bytes 0x%lx",
                media_fmt_ptr->df,
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.module_ptr->module_instance_id,
                gu_ext_in_port_ptr->int_in_port_ptr->cmn.id,
                media_fmt_ptr->upstream_frame_len.frame_len_bytes);

         // Transfer frame len info to downstream container when we receive ctrl path media format
         local_up_frame_len.frame_len_bytes =
            media_fmt_ptr->upstream_frame_len.frame_len_bytes;
         local_up_frame_len.frame_len_samples =
            media_fmt_ptr->upstream_frame_len.frame_len_samples;
         local_up_frame_len.frame_len_us = media_fmt_ptr->upstream_frame_len.frame_len_us;
         local_up_frame_len.sample_rate  = media_fmt_ptr->upstream_frame_len.sample_rate;

         TRY(result,
             tu_convert_media_fmt_spf_msg_to_topo(base_ptr->gu_ptr->log_id,
                                                  media_fmt_ptr,
                                                  &local_media_fmt,
                                                  base_ptr->heap_id));
         TRY(result,
             base_ptr->cntr_vtbl_ptr->input_media_format_received(base_ptr,
                                                                  gu_ext_in_port_ptr,
                                                                  &local_media_fmt,
																  &local_up_frame_len,
                                                                  FALSE /* is_data_path */));
      }
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "CMD:CTRL_PATH_MEDIA_FMT:Done excuting media format cmd, current channel mask=0x%x. result=0x%lx.",
          base_ptr->curr_chan_mask,
          result);

   return result;
}

ar_result_t cu_unsupported_cmd(cu_base_t *me_ptr)
{
   CU_MSG(me_ptr->gu_ptr->log_id, DBG_HIGH_PRIO, "Unsupported command with opcode 0x%lx!!", me_ptr->cmd_msg.msg_opcode);
   spf_msg_ack_msg(&me_ptr->cmd_msg, AR_EUNSUPPORTED);
   return AR_EUNSUPPORTED;
}

ar_result_t cu_cmd_icb_info_from_downstream(cu_base_t *base_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_msg_header_t              *header_ptr          = (spf_msg_header_t *)base_ptr->cmd_msg.payload_ptr;
   spf_msg_cmd_inform_icb_info_t *ds_icb_info_ptr     = (spf_msg_cmd_inform_icb_info_t *)&header_ptr->payload_start;
   gu_ext_out_port_t             *gu_ext_out_port_ptr = (gu_ext_out_port_t *)header_ptr->dst_handle_ptr;
   cu_ext_out_port_t             *ext_out_port_ptr =
      (cu_ext_out_port_t *)(((uint8_t *)gu_ext_out_port_ptr + base_ptr->ext_out_port_cu_offset));

   topo_port_state_t out_port_sg_state =
      topo_sg_state_to_port_state(base_ptr->topo_vtbl_ptr->get_sg_state(gu_ext_out_port_ptr->sg_ptr));

   ext_out_port_ptr->icb_info.ds_frame_len.frame_len_samples = ds_icb_info_ptr->downstream_frame_len_samples;
   ext_out_port_ptr->icb_info.ds_frame_len.frame_len_us      = ds_icb_info_ptr->downstream_frame_len_us;
   ext_out_port_ptr->icb_info.ds_frame_len.sample_rate       = ds_icb_info_ptr->downstream_sample_rate;
   ext_out_port_ptr->icb_info.ds_period_us                   = ds_icb_info_ptr->downstream_period_us;
   ext_out_port_ptr->icb_info.ds_flags.variable_input        = ds_icb_info_ptr->downstream_consumes_variable_input;
   ext_out_port_ptr->icb_info.ds_flags.is_real_time          = ds_icb_info_ptr->downstream_is_self_real_time;
   ext_out_port_ptr->icb_info.ds_sid                         = ds_icb_info_ptr->downstream_sid;
   ext_out_port_ptr->icb_info.ds_flags.is_default_single_buffering_mode =
      (ds_icb_info_ptr->downstream_set_single_buffer_mode) ? TRUE : FALSE;

   CU_MSG(base_ptr->gu_ptr->log_id,
          DBG_HIGH_PRIO,
          "ICB: Received ICB info from downstream of Module (0x%lX, %lX) - "
          "frame length (%lu, %lu, %lu), period in us (%lu), variable input (%u), real time (%u)",
          gu_ext_out_port_ptr->int_out_port_ptr->cmn.module_ptr->module_instance_id,
          gu_ext_out_port_ptr->int_out_port_ptr->cmn.id,
          ext_out_port_ptr->icb_info.ds_frame_len.frame_len_samples,
          ext_out_port_ptr->icb_info.ds_frame_len.sample_rate,
          ext_out_port_ptr->icb_info.ds_frame_len.frame_len_us,
          ext_out_port_ptr->icb_info.ds_period_us,
          ext_out_port_ptr->icb_info.ds_flags.variable_input,
          ext_out_port_ptr->icb_info.ds_flags.is_real_time);

   /** handle only in start state. in other states, prepare will take care of this.*/
   if ((TOPO_PORT_STATE_PREPARED == out_port_sg_state) || (TOPO_PORT_STATE_STARTED == out_port_sg_state))
   {
      TRY(result, base_ptr->cntr_vtbl_ptr->ext_out_port_recreate_bufs((void *)base_ptr, gu_ext_out_port_ptr));
   }
   else
   {
      CU_MSG(base_ptr->gu_ptr->log_id,
             DBG_HIGH_PRIO,
             "ICB: not in start/prepared state. postponing handling of frame length info cmd");
   }

   CATCH(result, CU_MSG_PREFIX, base_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

bool_t cu_is_frame_done_event_registered(cu_base_t *me_ptr)
{
   ar_result_t      result = AR_EOK;
   spf_list_node_t *temp;
   bool_t           need_to_handle_frame_done = FALSE;
   if (AR_EOK == (result = cu_find_client_info(me_ptr->gu_ptr->log_id,
                                               CNTR_EVENT_ID_CONTAINER_FRAME_DELIVERY_DONE,
                                               me_ptr->event_list_ptr,
                                               &temp)))
   {
      CU_MSG(me_ptr->gu_ptr->log_id,
             DBG_LOW_PRIO,
             "Found a client registered to CNTR_EVENT_ID_CONTAINER_FRAME_DELIVERY_DONE event result: %d",
             result);
      need_to_handle_frame_done = TRUE;
   }
   return need_to_handle_frame_done;
}

ar_result_t cu_gpr_cmd(cu_base_t *me_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   gpr_packet_t *packet_ptr = (gpr_packet_t *)me_ptr->cmd_msg.payload_ptr;

   // this handles only cmn handling b/w gen_cntr/spl_cntr.
   switch (packet_ptr->opcode)
   {
      case APM_CMD_REGISTER_MODULE_EVENTS:
      {
         result = cu_register_module_events(me_ptr, packet_ptr);
         if (AR_EOK != result)
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to register events with modules");
         }
         __gpr_cmd_end_command(packet_ptr, result);
         break;
      }
      case APM_CMD_REGISTER_MODULE_EVENTS_V2:
      {
         result = cu_register_module_events_v2(me_ptr, packet_ptr);
         if (AR_EOK != result)
         {
            CU_MSG(me_ptr->gu_ptr->log_id, DBG_ERROR_PRIO, "Failed to register events with modules");
         }
         __gpr_cmd_end_command(packet_ptr, result);
         break;
      }
      default:
      {
         bool_t switch_case_found = FALSE;
         result                   = cu_offload_handle_gpr_cmd(me_ptr, &switch_case_found);

         if (!switch_case_found)
         {
            gu_module_t *module_ptr = gu_find_module(me_ptr->gu_ptr, packet_ptr->dst_port);
            VERIFY(result, NULL != module_ptr);

            result = AR_EUNSUPPORTED;
            __gpr_cmd_end_command(packet_ptr, result);
            break;
         }
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }

   return result;
}

/**
 * This function should only be called while a subgraph management command for CLOSE is being handled. It parsers
 * The subgraph management command to see if an input port is currently getting closed.
 */
ar_result_t cu_is_in_port_closing(cu_base_t *  me_ptr, gu_input_port_t *in_port_ptr, bool_t *is_closing_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_cntr_sub_graph_list_t *sg_list_ptr;
   spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;

   VERIFY(result, is_closing_ptr);
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   *is_closing_ptr = FALSE;

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;

   if (in_port_ptr->ext_in_port_ptr)
   {
      // Check for external input ports closed.
      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_ip_port_handle; i++)
      {
         gu_ext_in_port_t *cmd_ext_in_port_ptr =
            (gu_ext_in_port_t *)cmd_gmgmt_ptr->cntr_port_hdl_list.ip_port_handle_list_pptr[i];

         if (in_port_ptr->ext_in_port_ptr == cmd_ext_in_port_ptr)
         {
            *is_closing_ptr = TRUE;
            break;
         }
      }
   }
   else if (in_port_ptr->conn_out_port_ptr)
   {
      bool_t close_connection_found = FALSE;

      // Check for internal input ports closed.
      for (uint32_t i = 0; i < cmd_gmgmt_ptr->cntr_port_hdl_list.num_data_links; i++)
      {
         apm_module_conn_cfg_t *data_link_ptr = cmd_gmgmt_ptr->cntr_port_hdl_list.data_link_list_pptr[i];

         gu_output_port_t *src_out_port_ptr = NULL;
         gu_input_port_t * dst_in_port_ptr  = NULL;
         if (AR_EOK == gu_parse_data_link(me_ptr->gu_ptr, data_link_ptr, &src_out_port_ptr, &dst_in_port_ptr))
         {
            if (in_port_ptr == dst_in_port_ptr)
            {
               close_connection_found = TRUE;
               break;
            }
         }
      }

      if (!close_connection_found)
      {
         for (uint32_t i = 0; i < sg_list_ptr->num_sub_graph; i++)
         {
            uint32_t *sg_id_base_ptr = sg_list_ptr->sg_id_list_ptr;
            uint32_t  cmd_sg_id      = *(sg_id_base_ptr + i);

            // If the connected port's subgraph is getting closed, then this input
            // port will be closed.
            if (cmd_sg_id == in_port_ptr->conn_out_port_ptr->cmn.module_ptr->sg_ptr->id)
            {
               close_connection_found = TRUE;
               break;
            }
         }
      }

      if (close_connection_found)
      {
         *is_closing_ptr = TRUE;
      }
   }

   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }
   return result;
}

/**
 * This function should only be called while a subgraph management command for CLOSE is being handled. It parsers
 * The subgraph management command to see if a module is getting closed.
 */
ar_result_t cu_is_module_closing(cu_base_t *  me_ptr, gu_module_t *module_ptr, bool_t *is_closing_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   spf_cntr_sub_graph_list_t *sg_list_ptr;
   spf_msg_cmd_graph_mgmt_t * cmd_gmgmt_ptr;

   spf_msg_header_t *header_ptr = (spf_msg_header_t *)me_ptr->cmd_msg.payload_ptr;

   VERIFY(result, is_closing_ptr);
   VERIFY(result, header_ptr->payload_size >= sizeof(spf_msg_cmd_graph_mgmt_t));

   *is_closing_ptr = FALSE;

   cmd_gmgmt_ptr = (spf_msg_cmd_graph_mgmt_t *)&header_ptr->payload_start;
   sg_list_ptr   = &cmd_gmgmt_ptr->sg_id_list;

   for (uint32_t i = 0; i < sg_list_ptr->num_sub_graph; i++)
   {
      uint32_t *sg_id_base_ptr = sg_list_ptr->sg_id_list_ptr;
      uint32_t  cmd_sg_id      = *(sg_id_base_ptr + i);

      if (cmd_sg_id == module_ptr->sg_ptr->id)
      {
         *is_closing_ptr = TRUE;
         break;
      }
   }


   CATCH(result, CU_MSG_PREFIX, me_ptr->gu_ptr->log_id)
   {
   }
   return result;
}

// Process the command queue: call the proper handler function depending on
// the message op code.
ar_result_t cu_process_cmd_queue(cu_base_t *me_ptr)
{
   ar_result_t         result        = AR_EOK;
   posal_thread_prio_t original_prio = 0;

   // bump-up priority for signal triggered containers when procesing cmd.
   bool_t prio_bumped_locally = FALSE;
   if (me_ptr->cntr_vtbl_ptr->check_bump_up_thread_priority)
   {
      // Save original prio which includes module vote
      original_prio = posal_thread_prio_get2(me_ptr->cmd_handle.thread_id);
      prio_bumped_locally =
         me_ptr->cntr_vtbl_ptr->check_bump_up_thread_priority(me_ptr, TRUE /* bump up */, original_prio);
   }

   bool_t handler_found = FALSE;
   for (uint32_t i = 0; i < me_ptr->cmd_handler_table_size; i++)
   {
      if (me_ptr->cmd_msg.msg_opcode == me_ptr->cmd_handler_table_ptr[i].opcode)
      {
         result        = me_ptr->cmd_handler_table_ptr[i].fn(me_ptr);
         handler_found = TRUE;
         break;
      }
   }

   if (!handler_found)
   {
      result = cu_unsupported_cmd(me_ptr);
   }

   if (prio_bumped_locally)
   {
      // Fall back to max of calc prio and original prio
      me_ptr->cntr_vtbl_ptr->check_bump_up_thread_priority(me_ptr, FALSE /* bump up */, original_prio);
   }

   // reset the flag
   me_ptr->flags.apm_cmd_context = FALSE;
   return result;
}
