/**
 * \file spf_sys_util.c
 * \brief
 *     This file contains utilities to be used by typical services.
 *
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "spf_sys_util.h"
#include "gpr_proc_info_api.h"
#include "ar_msg.h"
#include "spf_macros.h"
#include "ar_osal_servreg.h"
#include "spf_utils.h"
#include "gpr_api_inline.h"
#include "apm_cntr_if.h"

static const uint32_t sys_cmd_done_mask = 0x00000001UL;
#define SVC_REG_MSG_PREFIX "SVC_REG:"

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/

static ar_result_t spf_sys_util_push_close_all(spf_sys_util_handle_t *handle_ptr,
                                               POSAL_HEAP_ID          heap_id,
                                               bool_t                 set_done_signal,
                                               bool_t                 is_flush_needed,
                                               bool_t                 is_reset_needed);

static spf_sys_util_status_t spf_svc_reg_get_state(ar_osal_service_state_type status)
{
   if (AR_OSAL_SERVICE_STATE_DOWN == status)
   {
      return SPF_SYS_UTIL_SSR_STATUS_DOWN;
   }
   else if (AR_OSAL_SERVICE_STATE_UP == status)
   {
      return SPF_SYS_UTIL_SSR_STATUS_UP;
   }
   else
   {
      return SPF_SSYS_UTIL_SSR_STATUS_UNINIT;
   }
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t spf_svc_get_domain_and_service_string(ar_osal_servreg_entry_type *domain_ptr,
                                                         ar_osal_servreg_entry_type *service_ptr,
                                                         uint32_t                    proc_domain_id)
{
   ar_result_t result = AR_EOK;
   result             = gpr_get_pd_str_from_id(proc_domain_id, domain_ptr->name, AR_OSAL_SERVREG_NAME_LENGTH_MAX);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to get pd string from id = %lu, result = %lu", proc_domain_id, result);
      return result;
   }
   result = gpr_get_audio_service_name(service_ptr->name, AR_OSAL_SERVREG_NAME_LENGTH_MAX);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to get audio service name, result = %lu", result);
      return result;
   }
   return result;
}
/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
void spf_svc_reg_state_change_callback(ar_osal_servreg_t             servreg_handle,
                                       ar_osal_servreg_cb_event_type event_id,
                                       void *                        cb_context,
                                       void *                        payload,
                                       uint32_t                      payload_size)
{
   ar_result_t                                result        = AR_EOK;
   ar_osal_servreg_state_notify_payload_type *serv_ntfy_ptr = (ar_osal_servreg_state_notify_payload_type *)payload;
   spf_sys_util_handle_t *                    handle_ptr    = NULL;
   spf_msg_t                                  msg;
   param_id_sys_util_svc_status_t *           svc_status_ptr = NULL;
   AR_MSG(DBG_HIGH_PRIO,
          "SYS_UTIL: Call back rcved with state %lu, domain name %s, service name = %s",
          serv_ntfy_ptr->service_state,
          serv_ntfy_ptr->domain.name,
          serv_ntfy_ptr->service.name);

   if (NULL == cb_context)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: NULL cb context");
      return;
   }
   handle_ptr = (spf_sys_util_handle_t *)cb_context;

   uint32_t buf_size = sizeof(spf_msg_cmd_param_data_cfg_t) + (sizeof(void *)) + sizeof(apm_module_param_data_t) +
                       sizeof(param_id_sys_util_svc_status_t);

   buf_size = GET_SPF_MSG_REQ_SIZE(buf_size);

   result = spf_msg_create_msg(&msg, &buf_size, SPF_MSG_CMD_SET_CFG, NULL, 0, NULL, handle_ptr->heap_id);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to allocate the msg");
      return;
   }

   spf_msg_header_t *            msg_header_ptr     = (spf_msg_header_t *)msg.payload_ptr;
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   param_data_cfg_ptr->num_param_id_cfg             = 1;
   param_data_cfg_ptr->param_data_pptr              = (void **)(param_data_cfg_ptr + 1);
   apm_module_param_data_t *param_data_ptr =
      (apm_module_param_data_t *)((uint8_t *)param_data_cfg_ptr->param_data_pptr + sizeof(void *));
   *(param_data_cfg_ptr->param_data_pptr) = param_data_ptr;
   param_data_ptr->module_instance_id     = 0; // Ignore
   param_data_ptr->param_id               = PARAM_ID_SYS_UTIL_SVC_STATUS;
   param_data_ptr->param_size             = sizeof(param_id_sys_util_svc_status_t);
   param_data_ptr->error_code             = AR_EOK;

   svc_status_ptr = (param_id_sys_util_svc_status_t *)(param_data_ptr + 1);

   if (AR_DID_FAIL(result = gpr_get_pd_id_from_str(&svc_status_ptr->proc_domain_id,
                                                   serv_ntfy_ptr->domain.name,
                                                   AR_OSAL_SERVREG_NAME_LENGTH_MAX)))
   {
      AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Failed to get pd id from domain string, %s", serv_ntfy_ptr->domain.name);
      spf_msg_return_msg(&msg);
      return;
   }
   svc_status_ptr->status = spf_svc_reg_get_state(serv_ntfy_ptr->service_state);

   if (AR_DID_FAIL(result = posal_queue_push_back(handle_ptr->sys_queue_ptr, (posal_queue_element_t *)&msg)))
   {
      AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Failed to push gpr msg to svr reg system queue");
      spf_msg_return_msg(&msg);
   }

   return;
}
#if APM_SSR_TEST_CODE
void spf_svc_test_cb(spf_sys_util_handle_t *handle_ptr, bool_t status_up)
{
   ar_osal_servreg_state_notify_payload_type payload;

   spf_svc_get_domain_and_service_string(&payload.domain, &payload.service, 1);

   if (status_up)
   {
      payload.service_state = AR_OSAL_SERVICE_STATE_UP;
   }
   else
   {
      payload.service_state = AR_OSAL_SERVICE_STATE_DOWN;
   }

   spf_svc_reg_state_change_callback(handle_ptr->reg_info_list->sev_reg_handle_ptr,
                                     AR_OSAL_SERVICE_STATE_NOTIFY,
                                     handle_ptr,
                                     &payload,
                                     sizeof(payload));
}
#endif
/*----------------------------------------------------------------------------------------------------------------------
 Create sys queue and related components. Returns the handle to be used by the caller
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t spf_sys_util_get_handle(spf_sys_util_handle_t **handle_pptr,
                                    spf_sys_util_vtable *   sys_util_vtable_ptr,
                                    posal_channel_t         channel,
                                    char_t *                q_name_ptr,
                                    uint32_t                q_bit_mask,
                                    POSAL_HEAP_ID           heap_id,
                                    uint32_t                num_max_sys_q_elements,
                                    uint32_t                num_max_prealloc_sys_q_elem)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t            result     = AR_EOK;
   spf_sys_util_handle_t *handle_ptr = NULL;
   if (NULL == handle_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: NULL handle pptr");
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Get handle called");
   (*handle_pptr) = (spf_sys_util_handle_t *)posal_memory_malloc(sizeof(spf_sys_util_handle_t), heap_id);
   if (NULL == *handle_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to allocate handle");
      return AR_ENOMEMORY;
   }

   handle_ptr = (spf_sys_util_handle_t *)(*handle_pptr);
   memset(handle_ptr, 0, sizeof(spf_sys_util_handle_t));

   handle_ptr->heap_id = heap_id;

   TRY(result, posal_channel_create(&handle_ptr->sys_cmd_done_channel, heap_id));

   TRY(result, posal_signal_create(&handle_ptr->sys_cmd_done_signal, heap_id));

   TRY(result,
       posal_channel_add_signal(handle_ptr->sys_cmd_done_channel, handle_ptr->sys_cmd_done_signal, sys_cmd_done_mask));

   /** Create service reg service queue */
   posal_queue_init_attr_t q_attr;
   TRY(result,
       posal_queue_set_attributes(&q_attr, heap_id, num_max_sys_q_elements, num_max_prealloc_sys_q_elem, q_name_ptr));

   TRY(result, posal_queue_create_v1(&(handle_ptr->sys_queue_ptr), &q_attr));

   TRY(result, posal_channel_addq(channel, handle_ptr->sys_queue_ptr, q_bit_mask))

   handle_ptr->sys_queue_mask    = q_bit_mask;
   handle_ptr->sys_cmd_done_mask = sys_cmd_done_mask;
   if (NULL != sys_util_vtable_ptr)
   {
      handle_ptr->sys_util_vtable = *sys_util_vtable_ptr;
   }

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Get handle success, handle 0x%X", *handle_pptr);
   return result;

   CATCH(result, SVC_REG_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Get handle failed, releasing...");
   }
   result |= spf_sys_util_release_handle(handle_pptr);

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
  Releases/frees the handle created during get handle
  De-registers any pid registered for service reg notification
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t spf_sys_util_release_handle(spf_sys_util_handle_t **handle_pptr)
{
   ar_result_t result = AR_EOK;
   if (NULL == handle_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: NULL handle pptr");
      return AR_EFAILED;
   }
   spf_sys_util_handle_t *handle_ptr = (spf_sys_util_handle_t *)(*handle_pptr);

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Releasing handle 0x%X", *handle_pptr);

   if (NULL == handle_ptr)
   {
      return result;
   }

   if (handle_ptr->sys_queue_ptr)
   {
      /** De-init the service queue */
      posal_queue_destroy((posal_queue_t *)handle_ptr->sys_queue_ptr);
   }

   /** Destroy the sys q cmd done signal */
   if (0 != handle_ptr->sys_cmd_done_signal)
   {
      posal_signal_destroy(&handle_ptr->sys_cmd_done_signal);
   }

   /** Destroy the sys q cmd done channel */
   if (0 != handle_ptr->sys_cmd_done_channel)
   {
      posal_channel_destroy(&handle_ptr->sys_cmd_done_channel);
   }

   result |= spf_sys_util_ssr_deregister(handle_ptr);

   if (NULL != *handle_pptr)
   {
      posal_memory_free((void *)*handle_pptr);
      *handle_pptr = NULL;
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
Registers the given proc domain ids with service registry for up/down notification
If there were some PIDs already registered, it de-registers them first and then registers the new pids
If there is any error during registration, it de-registers all the registered pids and returns error
----------------------------------------------------------------------------------------------------------------------*/
ar_result_t spf_sys_util_ssr_register(spf_sys_util_handle_t *handle_ptr,
                                      uint32_t               num_proc_domain,
                                      uint32_t *             proc_domain_id_list)
{
   INIT_EXCEPTION_HANDLING
   ar_result_t result = AR_EOK;
   uint32_t    size   = 0;
   if (NULL == handle_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: NULL handle ptr");
      return AR_EFAILED;
   }

   if (AR_EOK != (result = spf_sys_util_ssr_deregister(handle_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to deregister before registering");
      return result;
   }

   if (0 == num_proc_domain)
   {
      AR_MSG(DBG_LOW_PRIO, "SYS_UTIL: WARNING: num_proc_domain is 0, ignoring ");
      return result;
   }

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: SSR registration called, num proc domain = %lu", num_proc_domain);

   size = sizeof(spf_sys_util_ssr_info_t) * num_proc_domain;

   handle_ptr->reg_info_list = (spf_sys_util_ssr_info_t *)posal_memory_malloc(size, handle_ptr->heap_id);
   VERIFY(result, NULL != handle_ptr->reg_info_list);

   memset(handle_ptr->reg_info_list, 0, size);

   for (uint32_t i = 0; i < num_proc_domain; i++)
   {
      spf_sys_util_ssr_info_t *cur_reg_info_ptr = &handle_ptr->reg_info_list[i];

      cur_reg_info_ptr->proc_domain_id = proc_domain_id_list[i];

      ar_osal_servreg_entry_type domain;
      ar_osal_servreg_entry_type service;
      // get the sev reg obj from id
      spf_svc_get_domain_and_service_string(&domain, &service, cur_reg_info_ptr->proc_domain_id);

#ifndef SIM
      // Register for the service needed to listen along with its domain.
      cur_reg_info_ptr->sev_reg_handle_ptr = ar_osal_servreg_register(AR_OSAL_CLIENT_LISTENER,
                                                                      spf_svc_reg_state_change_callback,
                                                                      (void *)handle_ptr,
                                                                      &domain,
                                                                      &service);
      VERIFY(result, NULL != cur_reg_info_ptr->sev_reg_handle_ptr);
#endif
   }
   handle_ptr->num_proc_domain_ids = num_proc_domain;
   return result;

   CATCH(result, SVC_REG_MSG_PREFIX)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Register failed, de-registering");
      result |= spf_sys_util_ssr_deregister(handle_ptr);
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t spf_sys_util_ssr_deregister(spf_sys_util_handle_t *handle_ptr)
{
   ar_result_t result = AR_EOK;

   if (NULL == handle_ptr)
   {
      return result;
   }

   for (uint32_t i = 0; i < handle_ptr->num_proc_domain_ids; i++)
   {
      spf_sys_util_ssr_info_t *cur_reg_info_list = &handle_ptr->reg_info_list[i];
      if (NULL != cur_reg_info_list->sev_reg_handle_ptr)
      {
#ifndef SIM
         result |= ar_osal_servreg_deregister(cur_reg_info_list->sev_reg_handle_ptr);
#endif
         cur_reg_info_list->sev_reg_handle_ptr = NULL;
      }
   }

   if (NULL != handle_ptr->reg_info_list)
   {
      posal_memory_free(handle_ptr->reg_info_list);
      handle_ptr->reg_info_list = NULL;
   }
   handle_ptr->num_proc_domain_ids = 0;
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/

// use sync and async instead of blocking
ar_result_t spf_sys_util_sync_push_close_all(spf_sys_util_handle_t *handle_ptr,
                                             POSAL_HEAP_ID          heap_id,
                                             bool_t                 is_flush_needed,
                                             bool_t                 is_reset_needed)
{
   ar_result_t result = spf_sys_util_push_close_all(handle_ptr, heap_id, TRUE, is_flush_needed, is_reset_needed);

   // does a blocking wait for the done signal
   posal_channel_wait(handle_ptr->sys_cmd_done_channel, handle_ptr->sys_cmd_done_mask);
   posal_signal_clear(handle_ptr->sys_cmd_done_signal);

   return result;
}

ar_result_t spf_sys_util_async_push_close_all(spf_sys_util_handle_t *handle_ptr,
                                              POSAL_HEAP_ID          heap_id,
                                              bool_t                 is_flush_needed,
                                              bool_t                 is_reset_needed)
{
   return spf_sys_util_push_close_all(handle_ptr, heap_id, FALSE, is_flush_needed, is_reset_needed);
}

static ar_result_t spf_sys_util_push_close_all(spf_sys_util_handle_t *handle_ptr,
                                               POSAL_HEAP_ID          heap_id,
                                               bool_t                 set_done_signal,
                                               bool_t                 is_flush_needed,
                                               bool_t                 is_reset_needed)
{
   ar_result_t                    result = AR_EOK;
   spf_msg_t                      msg;
   param_id_sys_util_close_all_t *close_all_ptr = NULL;
   AR_MSG(DBG_HIGH_PRIO, "spf_svc_util: Close all called");

   uint32_t buf_size = sizeof(spf_msg_cmd_param_data_cfg_t) + (sizeof(void *)) + sizeof(apm_module_param_data_t) +
                       sizeof(param_id_sys_util_close_all_t);
   buf_size = GET_SPF_MSG_REQ_SIZE(buf_size);

   result = spf_msg_create_msg(&msg, &buf_size, SPF_MSG_CMD_SET_CFG, NULL, 0, NULL, handle_ptr->heap_id);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to allocate the msg");
      return result;
   }

   spf_msg_header_t *            msg_header_ptr     = (spf_msg_header_t *)msg.payload_ptr;
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   param_data_cfg_ptr->num_param_id_cfg             = 1;
   param_data_cfg_ptr->param_data_pptr              = (void **)(param_data_cfg_ptr + 1);
   apm_module_param_data_t *param_data_ptr =
      (apm_module_param_data_t *)((uint8_t *)param_data_cfg_ptr->param_data_pptr + sizeof(void *));
   *(param_data_cfg_ptr->param_data_pptr) = param_data_ptr;

   param_data_ptr->module_instance_id = 0; // Ignore
   param_data_ptr->param_id           = PARAM_ID_SYS_UTIL_CLOSE_ALL;
   param_data_ptr->param_size         = sizeof(param_id_sys_util_close_all_t);
   param_data_ptr->error_code         = AR_EOK;

   close_all_ptr = (param_id_sys_util_close_all_t *)(param_data_ptr + 1);

   close_all_ptr->set_done_signal = set_done_signal;
   close_all_ptr->is_flush_needed = is_flush_needed;
   close_all_ptr->is_reset_needed = is_reset_needed;

   if (AR_DID_FAIL(result = posal_queue_push_back(handle_ptr->sys_queue_ptr, (posal_queue_element_t *)&msg)))
   {
      AR_MSG(DBG_HIGH_PRIO, "spf_svc_util: Failed to push gpr msg to svr reg system queue");
      spf_msg_return_msg(&msg);
      return result;
   }
   return result;
}

ar_result_t spf_sys_util_push_kill_cmd(spf_sys_util_handle_t *handle_ptr, POSAL_HEAP_ID heap_id)
{
   ar_result_t                    result = AR_EOK;
   spf_msg_t                      msg;
   AR_MSG(DBG_HIGH_PRIO, "spf_svc_util: Kill command called");

   uint32_t buf_size = sizeof(spf_msg_cmd_param_data_cfg_t) + (sizeof(void *)) + sizeof(apm_module_param_data_t);
   buf_size = GET_SPF_MSG_REQ_SIZE(buf_size);

   result = spf_msg_create_msg(&msg, &buf_size, SPF_MSG_CMD_SET_CFG, NULL, 0, NULL, handle_ptr->heap_id);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to allocate the msg");
      return result;
   }

   spf_msg_header_t *            msg_header_ptr     = (spf_msg_header_t *)msg.payload_ptr;
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;
   param_data_cfg_ptr->num_param_id_cfg             = 1;
   param_data_cfg_ptr->param_data_pptr              = (void **)(param_data_cfg_ptr + 1);
   apm_module_param_data_t *param_data_ptr =
      (apm_module_param_data_t *)((uint8_t *)param_data_cfg_ptr->param_data_pptr + sizeof(void *));
   *(param_data_cfg_ptr->param_data_pptr) = param_data_ptr;

   param_data_ptr->module_instance_id = 0; // Ignore
   param_data_ptr->param_id           = PARAM_ID_SYS_UTIL_KILL;
   param_data_ptr->param_size         = 0;
   param_data_ptr->error_code         = AR_EOK;
   if (AR_DID_FAIL(result = posal_queue_push_back(handle_ptr->sys_queue_ptr, (posal_queue_element_t *)&msg)))
   {
      AR_MSG(DBG_HIGH_PRIO, "spf_svc_util: Failed to push gpr msg to svr reg system queue");
      spf_msg_return_msg(&msg);
      return result;
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
static ar_result_t spf_sys_util_handle_set_cfg(spf_sys_util_handle_t *       handle_ptr,
                                               spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr)
{
   ar_result_t               result                 = AR_EOK;
   apm_module_param_data_t **module_param_data_pptr = NULL;

   module_param_data_pptr = (apm_module_param_data_t **)param_data_cfg_ptr->param_data_pptr;
   for (uint32_t i = 0; i < param_data_cfg_ptr->num_param_id_cfg; i++)
   {
      apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)module_param_data_pptr[i];

      switch (param_data_ptr->param_id)
      {
         case PARAM_ID_SYS_UTIL_SVC_STATUS:
         {
            if (NULL != handle_ptr->sys_util_vtable.spf_sys_util_handle_svc_status)
            {
               param_id_sys_util_svc_status_t *payload_ptr = (param_id_sys_util_svc_status_t *)(param_data_ptr + 1);
               result = handle_ptr->sys_util_vtable.spf_sys_util_handle_svc_status((void *)payload_ptr);
            }
            break;
         }
         case PARAM_ID_SYS_UTIL_CLOSE_ALL:
         {
            param_id_sys_util_close_all_t *payload_ptr = (param_id_sys_util_close_all_t *)(param_data_ptr + 1);
            if (NULL != handle_ptr->sys_util_vtable.spf_sys_util_handle_close_all)
            {
               result = handle_ptr->sys_util_vtable.spf_sys_util_handle_close_all((void *)payload_ptr);
            }

            if(payload_ptr->set_done_signal)
            {
               posal_signal_send(handle_ptr->sys_cmd_done_signal);
            }
            break;
         }
         case PARAM_ID_SYS_UTIL_KILL:
         {
            if (NULL != handle_ptr->sys_util_vtable.spf_sys_util_handle_kill)
            {
               result = handle_ptr->sys_util_vtable.spf_sys_util_handle_kill();
            }
            break;
         }
         case APM_PARAM_ID_SATELLITE_PD_INFO:
         {
            if (NULL != handle_ptr->sys_util_vtable.spf_sys_util_handle_sat_pd_info)
            {
               apm_param_id_satellite_pd_info_t *payload_ptr = (apm_param_id_satellite_pd_info_t *)(param_data_ptr + 1);
               result = handle_ptr->sys_util_vtable.spf_sys_util_handle_sat_pd_info(payload_ptr->num_proc_domain_ids,
                                                                                    payload_ptr->proc_domain_id_list);
            }
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Unsupported param id 0x%X", param_data_ptr->param_id);
         }
      }
   }
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------

----------------------------------------------------------------------------------------------------------------------*/
ar_result_t spf_sys_util_handle_cmd(spf_sys_util_handle_t *handle_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_msg_t                     msg;
   spf_msg_t *                   msg_ptr            = NULL;
   spf_msg_header_t *            msg_header_ptr     = NULL;
   spf_msg_cmd_param_data_cfg_t *param_data_cfg_ptr = NULL;

   if (NULL == handle_ptr)
   {
      return result;
   }

   /** Pop the message buffer from the cmd queue */
   if (AR_EOK != (result = posal_queue_pop_front(handle_ptr->sys_queue_ptr, (posal_queue_element_t *)&msg)))
   {
      AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Failed to pop buf from svc cmd_q, result: 0x%lx", result);
      return result;
   }

   msg_ptr        = &msg;
   msg_header_ptr = (spf_msg_header_t *)msg_ptr->payload_ptr;
   if (msg_header_ptr->payload_size < sizeof(spf_msg_cmd_param_data_cfg_t))
   {
      spf_msg_ack_msg(msg_ptr, AR_EBADPARAM);
      return AR_EBADPARAM;
   }

   param_data_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Handling command, opcode 0x%X", msg_ptr->msg_opcode);

   switch (msg_ptr->msg_opcode)
   {
      case SPF_MSG_CMD_SET_CFG:
      {
         result = spf_sys_util_handle_set_cfg(handle_ptr, param_data_cfg_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "SYS_UTIL: Unsupported CMD recieved 0x%lx", msg_ptr->msg_opcode);
         result = AR_EBADPARAM;
         break;
      }
   }

   AR_MSG(DBG_HIGH_PRIO, "SYS_UTIL: Handling command done, opcode 0x%X, result %lu", msg_ptr->msg_opcode, result);
   spf_msg_ack_msg(msg_ptr, result);
   return result;
}
