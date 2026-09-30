/**
 * \file amdb_cmd_handler.c
 * \brief
 *     This file contains GPR command handler for AMDB service
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "amdb_cmd_handler.h"
#include "amdb_api.h"
#include "amdb_static.h"
#include "amdb_autogen_def.h"
#include "amdb_internal.h"
#include "amdb_offload_utils.h"
#include "spf_svc_calib.h"
#include "apm.h" //needed for mem-map handles
#include "apm_offload_pd_info.h"

extern amdb_t *g_amdb_ptr;

#define ALIGN_4_BYTES(a) ((a + 3) & (0xFFFFFFFC))
/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
void amdb_load_handle_holder_free(void *void_ptr, spf_hash_node_t *node)
{
   if (NULL != node)
   {
      amdb_load_handle_t *handle_holder_ptr = STD_RECOVER_REC(amdb_load_handle_t, hn, node);
      AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_unload freeing load handle 0x%X", handle_holder_ptr);
      posal_memory_free(handle_holder_ptr);
      handle_holder_ptr = NULL;
   }
}
/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
void amdb_load_unload_send_rsp(amdb_context_t *context_ptr, bool_t is_load, uint32_t unload_result)
{
   if (NULL == context_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: NULL context ptr sent, this should not happen");
      return;
   }

   gpr_packet_t *packet_ptr = (gpr_packet_t *)context_ptr->gpr_pkt_ptr;
   ar_result_t   result     = AR_EOK;

   if (is_load)
   {
      if (NULL != context_ptr->gpr_rsp_pkt_ptr)
      {
         // This is for in-band case, we send the already allocated response packet, with proper copied payload
         result = __gpr_cmd_async_send((gpr_packet_t *)context_ptr->gpr_rsp_pkt_ptr);
         if (AR_EOK != result)
         {
            AR_MSG(DBG_ERROR_PRIO, "AMDB: Unable to send response via GPR");
            __gpr_cmd_free((gpr_packet_t *)context_ptr->gpr_rsp_pkt_ptr);
         }
      }
      else
      {
         // This is for Out-of-band case, we flush and send the out-of-band payload
         apm_cmd_header_t *cmd_header_ptr = (apm_cmd_header_t *)GPR_PKT_GET_PAYLOAD(void, packet_ptr);
         if (NULL != context_ptr->payload_ptr)
         {
            posal_cache_flush_v2(&context_ptr->payload_ptr, cmd_header_ptr->payload_size);
         }

         if (NULL != packet_ptr)
         {
            gpr_cmd_alloc_send_t args;
            args.src_domain_id = packet_ptr->dst_domain_id;
            args.dst_domain_id = packet_ptr->src_domain_id;
            args.src_port      = packet_ptr->dst_port;
            args.dst_port      = packet_ptr->src_port;
            args.token         = packet_ptr->token;
            args.opcode        = AMDB_CMD_RSP_LOAD_MODULES;
            args.payload       = (void *)context_ptr->payload_ptr;
            args.payload_size  = cmd_header_ptr->payload_size;
            args.client_data   = 0;
            __gpr_cmd_alloc_send(&args);
         }
      }

      if (NULL != packet_ptr)
      {
         // Freeing the gpr packet, this is needed because it is not done in amdb_gpr_cmd_handler
         __gpr_cmd_free(packet_ptr);
      }
   }
   else
   {
      __gpr_cmd_end_command((gpr_packet_t *)packet_ptr, unload_result);
   }
}

/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
void amdb_load_callback(void *cb_ptr)
{
   if (NULL == cb_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load cb, cb_ptr NULL, cannot send gpr rsp");
      return;
   }

   amdb_context_t            *context_ptr             = (amdb_context_t *)cb_ptr;
   spf_list_node_t           *module_handle_info_list = (spf_list_node_t *)(context_ptr + 1);
   amdb_module_load_unload_t *load_unload_ptr         = (amdb_module_load_unload_t *)context_ptr->payload_ptr;
   amdb_load_handle_t        *handle_holder_ptr       = NULL;
   ar_result_t                result                  = AR_EOK;

   if ((NULL != load_unload_ptr) && (0 != load_unload_ptr->num_modules))
   {
      for (uint32_t i = 0; NULL != module_handle_info_list; LIST_ADVANCE(module_handle_info_list), i++)
      {
         amdb_module_handle_info_t *h_info_ptr = (amdb_module_handle_info_t *)(module_handle_info_list->obj_ptr);
         load_unload_ptr->error_code |= h_info_ptr->result;
         amdb_load_unload_info_t *load_unload_info_ptr =
            (amdb_load_unload_info_t *)(((uint8_t *)load_unload_ptr->load_unload_info) +
                                        (i * ALIGN_4_BYTES(sizeof(amdb_load_unload_info_t))));
         load_unload_info_ptr->handle_lsw = 0;
         load_unload_info_ptr->handle_msw = 0;

         if ((AR_EOK == h_info_ptr->result) || (NULL != h_info_ptr->handle_ptr))
         {
            handle_holder_ptr =
               (amdb_load_handle_t *)posal_memory_malloc(sizeof(amdb_load_handle_t), g_amdb_ptr->heap_id);
            if (NULL != handle_holder_ptr)
            {
               handle_holder_ptr->module_handle_ptr = (uint8_t *)(h_info_ptr->handle_ptr);
               handle_holder_ptr->hn.key_ptr        = (const void *)(&handle_holder_ptr->hn.key_ptr);
               handle_holder_ptr->hn.key_size       = sizeof(const void *);

               result = spf_hashtable_insert(&g_amdb_ptr->load_ht, &handle_holder_ptr->hn);
               if (result != AR_EOK)
               {
                  AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load cb, failed to insert handle holder");
                  posal_memory_free(handle_holder_ptr);
                  load_unload_ptr->error_code |= result;

                  // Unloading the module, because, loading succeeded, but steps after that failed
                  spf_list_node_t module_handle_info = { .obj_ptr = h_info_ptr, .next_ptr = NULL, .prev_ptr = NULL };
                  amdb_release_module_handles(&module_handle_info);
               }
               else
               {
                  uint64_t handle                  = (uint64_t)(&handle_holder_ptr->hn.key_ptr);
                  load_unload_info_ptr->handle_lsw = (uint32_t)(handle);
                  load_unload_info_ptr->handle_msw = (uint32_t)(handle >> 32);
               }
            }
            else
            {
               AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load cb, failed to allocate handle holder");
               load_unload_ptr->error_code |= AR_ENOMEMORY;
               // No need to break out of loop, just try to update remaining handles as well

               // Unloading the module, because, loading succeeded, but steps after that failed
               spf_list_node_t module_handle_info = { .obj_ptr = h_info_ptr, .next_ptr = NULL, .prev_ptr = NULL };
               amdb_release_module_handles(&module_handle_info);
            }
         }
         handle_holder_ptr = NULL;
      }
   }

   amdb_load_unload_send_rsp(context_ptr, TRUE /*is_load*/, AR_EOK /*ignore*/);
   posal_memory_free(context_ptr);
   context_ptr = NULL;
   AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_load_callback, done");
}

/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_reg_custom_modules(amdb_module_registration_t *payload_ptr, uint32_t payload_size)
{
   ar_result_t                  result          = AR_EOK;
   amdb_module_reg_info_t      *reg_info_ptr    = NULL;
   amdb_capi_module_reg_info_t *module_info_ptr = NULL;
   uint32_t                     num_modules     = 0;

   if (payload_size < sizeof(amdb_module_registration_t))
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_register_custom_modules failed, payload size [%d] insufficient", payload_size);
      return AR_EBADPARAM;
   }
   else
   {
      payload_size -= sizeof(amdb_module_registration_t);
   }

   /*Validate the struct version*/
   if (AMDB_MODULE_REG_STRUCT_V1 > payload_ptr->struct_version)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "AMDB: amdb_register_custom_modules failed, struct version %lu not supported",
             payload_ptr->struct_version);
      return AR_EBADPARAM;
   }

   reg_info_ptr = (amdb_module_reg_info_t *)payload_ptr->reg_info;

   num_modules = payload_ptr->num_modules;
   while ((0 < payload_size) && (0 < num_modules))
   {
      if (!((AMDB_INTERFACE_TYPE_CAPI == reg_info_ptr->interface_type) &&
            (AMDB_INTERFACE_VERSION_CAPI_V3 == reg_info_ptr->interface_version)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "AMDB: amdb_register_custom_modules failed, Only CAPI interface Version 3 is supported. Provided "
                "interface "
                "type = %lu and version %lu",
                reg_info_ptr->interface_type,
                reg_info_ptr->interface_version);
         result |= AR_EBADPARAM;
         break;
      }
      module_info_ptr = (amdb_capi_module_reg_info_t *)(reg_info_ptr + 1);

      uint32_t size_of_current_param = sizeof(amdb_module_reg_info_t) + sizeof(amdb_capi_module_reg_info_t) +
                                       module_info_ptr->file_name_len + module_info_ptr->tag_len;

      if (payload_size < size_of_current_param)
      {
         // If there are still modules to be registered but size is not sufficient
         AR_MSG(DBG_ERROR_PRIO,
                "AMDB: amdb_register_custom_modules failed for some modules [%d], payload size [%d] < required [%d]",
                num_modules,
                payload_size,
                size_of_current_param);
         result |= AR_EBADPARAM;
         break;
      }

      // If either file name length or tag length is zero, we skip
      if ((0 != module_info_ptr->file_name_len) && (0 != module_info_ptr->tag_len))
      {

         uint8_t *filename_ptr = (uint8_t *)(module_info_ptr + 1);
         uint8_t *tag_ptr      = filename_ptr + module_info_ptr->file_name_len;

         result |= module_info_ptr->error_code = amdb_register(module_info_ptr->module_type,
                                                               module_info_ptr->module_id,
                                                               NULL,
                                                               NULL,
                                                               module_info_ptr->file_name_len,
                                                               (const char *)filename_ptr,
                                                               module_info_ptr->tag_len,
                                                               (const char *)tag_ptr,
                                                               FALSE);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO,
                "AMDB: amdb_register_custom_modules failed, invalid param, filename_len=%d, tag_len=%d",
                module_info_ptr->file_name_len,
                module_info_ptr->tag_len);
         result |= module_info_ptr->error_code = AR_EBADPARAM;
      }

      reg_info_ptr = (amdb_module_reg_info_t *)(((uint8_t *)reg_info_ptr) + ALIGN_4_BYTES(size_of_current_param));
      if (payload_size > ALIGN_4_BYTES(size_of_current_param))
      {
         payload_size -= ALIGN_4_BYTES(size_of_current_param);
      }
      else
      {
         payload_size = 0;
      }

      num_modules--;
   }

   AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_register_custom_modules done with result %d", result);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_dereg_custom_modules(amdb_module_deregistration_t *payload_ptr, uint32_t payload_size)
{
   ar_result_t result      = AR_EOK;
   uint32_t   *id_ptr      = NULL;
   uint32_t    num_modules = 0;

   if (payload_size < sizeof(amdb_module_deregistration_t))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "AMDB: amdb_deregister_custom_modules failed, payload size [%d] insufficient",
             payload_size);
      return AR_EBADPARAM;
   }
   else
   {
      payload_size -= sizeof(amdb_module_deregistration_t);
   }

   id_ptr      = payload_ptr->module_id;
   num_modules = payload_ptr->num_modules;
   while ((0 < payload_size) && (0 < num_modules))
   {
      uint32_t size_of_current_param = sizeof(uint32_t);

      if (payload_size < size_of_current_param)
      {
         // If there are still modules to be deregistered but size is not sufficient
         AR_MSG(DBG_ERROR_PRIO,
                "AMDB: amdb_deregister_custom_modules failed for some modules [%d], payload size [%d] < required [%d]",
                num_modules,
                payload_size,
                size_of_current_param);
         result |= AR_EBADPARAM;
         break;
      }

      result |= amdb_deregister(*id_ptr);

      id_ptr = (uint32_t *)(((uint8_t *)(id_ptr)) + ALIGN_4_BYTES(size_of_current_param));
      if (payload_size > ALIGN_4_BYTES(size_of_current_param))
      {
         payload_size -= ALIGN_4_BYTES(size_of_current_param);
      }
      else
      {
         payload_size = 0;
      }
      num_modules--;
   }

   return result;
}

ar_result_t amdb_get_capi_module_version(amdb_node_t *node_ptr, capi_module_version_info_t *version_payload_ptr)
{
   ar_result_t     result = AR_EOK;
   capi_proplist_t proplist;
   capi_prop_t     version_property;

   version_property.id                      = CAPI_MODULE_VERSION_INFO;
   version_property.port_info.is_valid      = FALSE;
   version_property.payload.data_ptr        = (int8_t *)version_payload_ptr;
   version_property.payload.actual_data_len = sizeof(*version_payload_ptr);
   version_property.payload.max_data_len    = sizeof(*version_payload_ptr);

   proplist.props_num = 1;
   proplist.prop_ptr  = &version_property;

   result = amdb_capi_get_static_properties_f((void *)node_ptr, NULL, &proplist);
   return result;
}
/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_load_unload_modules(amdb_module_load_unload_t *payload_ptr,
                                     uint8_t                   *gpr_rsp_pkt_ptr,
                                     uint8_t                   *gpr_pkt_ptr,
                                     uint32_t                   payload_size,
                                     bool_t                     is_load)
{
   ar_result_t                result               = AR_EOK;
   uint32_t                   req_payload_size     = 0;
   uint32_t                   alloc_size           = 0;
   uint32_t                   h_info_size          = 0;
   amdb_load_unload_info_t   *load_unload_info_ptr = NULL;
   spf_list_node_t           *h_info_list          = NULL;
   amdb_module_handle_info_t *h_info_ptr           = NULL;
   amdb_context_t            *context_ptr          = NULL;

   amdb_context_t temp_context = { .payload_ptr     = (uint8_t *)payload_ptr,
                                   .gpr_rsp_pkt_ptr = gpr_rsp_pkt_ptr,
                                   .gpr_pkt_ptr     = gpr_pkt_ptr };

   if (payload_size < sizeof(amdb_module_load_unload_t))
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load_unload_modules failed, payload size [%d] insufficient", payload_size);
      result |= AR_EBADPARAM;
      goto rsp_clean_up;
   }
   else
   {
      payload_size -= sizeof(amdb_module_load_unload_t);
   }

   req_payload_size = payload_ptr->num_modules * ALIGN_4_BYTES(sizeof(amdb_load_unload_info_t));
   if (0 == req_payload_size)
   {
      AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_load_unload_modules, WARNING: no modules to be loaded");
      payload_ptr->error_code = (result |= AR_EOK);
      goto rsp_clean_up;
   }

   if (payload_size < req_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "AMDB: amdb_load_unload_modules failed, payload size [%d] < required size [%d]",
             payload_size,
             req_payload_size);
      payload_ptr->error_code = (result |= AR_EBADPARAM);
      goto rsp_clean_up;
   }

   load_unload_info_ptr = payload_ptr->load_unload_info;
   h_info_size          = (payload_ptr->num_modules) * (sizeof(spf_list_node_t) + sizeof(amdb_module_handle_info_t));
   alloc_size           = sizeof(amdb_context_t) + h_info_size;

   context_ptr = (amdb_context_t *)posal_memory_malloc(alloc_size, g_amdb_ptr->heap_id);
   if (NULL == context_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load_unload_modules, failed to allocate memory");
      payload_ptr->error_code = (result |= AR_ENOMEMORY);
      goto rsp_clean_up;
   }

   memset(context_ptr, 0, alloc_size);
   *context_ptr = temp_context;

   h_info_list = (spf_list_node_t *)(context_ptr + 1);
   h_info_ptr  = (amdb_module_handle_info_t *)(h_info_list + payload_ptr->num_modules);

   for (uint32_t i = 0; i < payload_ptr->num_modules; i++)
   {
      h_info_ptr[i].module_id = load_unload_info_ptr->module_id;
      h_info_list[i].prev_ptr = (0 == i) ? 0 : &h_info_list[i - 1];
      h_info_list[i].obj_ptr  = &h_info_ptr[i];
      h_info_list[i].next_ptr = ((payload_ptr->num_modules - 1) == i) ? 0 : &h_info_list[i + 1];

      if (!is_load)
      {
         h_info_ptr[i].handle_ptr = NULL;
         uint64_t addr_64_bit = ((uint64_t)load_unload_info_ptr->handle_msw << 32) | (load_unload_info_ptr->handle_lsw);
         uint8_t *key         = (uint8_t *)addr_64_bit;
         AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_load_unload_modules, unload handle 0x%X", key);
         spf_hash_node_t *phn = spf_hashtable_find(&g_amdb_ptr->load_ht, (const void *)&key, sizeof(const void *));
         if (NULL != phn)
         {
            amdb_load_handle_t *handle_holder_ptr = STD_RECOVER_REC(amdb_load_handle_t, hn, phn);
            h_info_ptr[i].handle_ptr              = (void *)handle_holder_ptr->module_handle_ptr;
            spf_hashtable_remove(&g_amdb_ptr->load_ht, (const void *)&key, sizeof(const void *), phn);
            load_unload_info_ptr->handle_lsw = 0;
            load_unload_info_ptr->handle_msw = 0;
         }
         else
         {
            AR_MSG(DBG_ERROR_PRIO, "AMDB: amdb_load_unload_modules, load handle not found 0x%X", key);
            result |= AR_EFAILED;
         }
      }
      load_unload_info_ptr = (amdb_load_unload_info_t *)((uint8_t *)(load_unload_info_ptr) +
                                                         ALIGN_4_BYTES(sizeof(amdb_load_unload_info_t)));
   }

   if (is_load)
   {
      amdb_request_module_handles(h_info_list, amdb_load_callback, context_ptr);
      AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_load_unload_modules, load modules command done");
      return result;
   }
   else
   {
      amdb_release_module_handles(h_info_list);
      if (NULL != context_ptr)
      {
         posal_memory_free(context_ptr);
      }
      AR_MSG(DBG_HIGH_PRIO, "AMDB: amdb_load_unload_modules, unload modules command done");
   }

rsp_clean_up:
   amdb_load_unload_send_rsp((amdb_context_t *)&temp_context, is_load, result);
   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_handle_get_config(apm_module_param_data_t *param_paylaod)
{
   ar_result_t result = AR_EOK;

   switch (param_paylaod->param_id)
   {
      case AMDB_PARAM_ID_MODULE_VERSION_INFO:
      {
         if (param_paylaod->param_size < sizeof(amdb_param_id_module_version_info_t))
         {
            result = AR_ENEEDMORE;
            break;
         }
         amdb_param_id_module_version_info_t *module_version_header_ptr =
            (amdb_param_id_module_version_info_t *)(param_paylaod + 1);
         uint32_t actual_req_param_size =
            sizeof(amdb_param_id_module_version_info_t) +
            module_version_header_ptr->num_modules * sizeof(amdb_module_version_info_payload_t);
         if (param_paylaod->param_size < actual_req_param_size)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "AMDB: amdb_handle_get_config: param size %d is not enough for %d module(s), need param size of %d",
                   param_paylaod->param_size,
                   module_version_header_ptr->num_modules,
                   actual_req_param_size);
            result = AR_ENEEDMORE;
            break;
         }
         else if (param_paylaod->param_size > actual_req_param_size)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "AMDB: amdb_handle_get_config: Warning: param size %d more than what is needed for %d module(s), "
                   "exact needed param isze is %d. Continuing anyway",
                   param_paylaod->param_size,
                   module_version_header_ptr->num_modules,
                   actual_req_param_size);
         }

         amdb_module_version_info_payload_t *module_version_ptr =
            (amdb_module_version_info_payload_t *)(module_version_header_ptr + 1);
         for (uint32_t i = 0; i < module_version_header_ptr->num_modules; ++i)
         {
            uint32_t module_id = module_version_ptr[i].module_id;
            AR_MSG(DBG_MED_PRIO, "Fetching details for module id: 0x%x", module_version_ptr[i].module_id);
            amdb_node_t *node_ptr;
            memset(&module_version_ptr[i], 0, sizeof(module_version_ptr[i]));
            module_version_ptr[i].module_id  = module_id;
            module_version_ptr[i].is_present = amdb_get_details_from_mid(module_version_ptr[i].module_id, &node_ptr);
            if (module_version_ptr[i].is_present)
            {
               if (!node_ptr->flags.is_static)
               {
                  result = amdb_dyn_module_version(node_ptr, &module_version_ptr[i]);
               }
               else
               {
                  capi_module_version_info_t module_version_info;
                  result = amdb_get_capi_module_version(node_ptr, &module_version_info);
                  module_version_ptr->module_version_major = module_version_info.version_major;
                  module_version_ptr->module_version_minor = module_version_info.version_minor;
               }
            }
         }

         break;
      }
      default:
      {
         result                    = AR_EBADPARAM;
         param_paylaod->error_code = AR_EBADPARAM;
         AR_MSG(DBG_ERROR_PRIO,
                "AMDB: amdb_handle_get_config: invalid get cfg param id: 0x%x",
                param_paylaod->param_id);
      }
   }

   return result;
}

/*----------------------------------------------------------------------------------------------------------------------
 * DESCRIPTION:
 *
 *--------------------------------------------------------------------------------------------------------------------*/
ar_result_t amdb_cmdq_gpr_cmd_handler(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "Invalid param");
      return AR_EFAILED;
   }

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;
   /** Get command header pointer */
   apm_cmd_header_t *cmd_header_ptr   = (apm_cmd_header_t *)GPR_PKT_GET_PAYLOAD(void, gpr_pkt_ptr);
   bool_t            is_out_of_band   = (0 != cmd_header_ptr->mem_map_handle);
   uint8_t          *payload_ptr      = NULL;
   uint32_t          alignment_size   = 0;
   gpr_packet_t     *gpr_rsp_pkt_ptr  = NULL;
   uint8_t         **cmd_payload_pptr = (uint8_t **)&payload_ptr;
   *cmd_payload_pptr                  = NULL;
   /*Get the domain ID of the host proc*/
   uint32_t host_domain_id, dest_domain_id = 0xFFFFFFFF;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   /* For OOB payloads, payload_ptr will be filled with correct dereferenced oob ptr
    * For in-band which needs response, this will allocate, copy and assign the payload to payload_ptr
    * For in-band payload which doesn't need response, payload_ptr will be filled from apm header */
   result = spf_svc_get_cmd_payload_addr(AMDB_MODULE_INSTANCE_ID,
                                         gpr_pkt_ptr,
                                         &gpr_rsp_pkt_ptr,
                                         cmd_payload_pptr,
                                         &alignment_size,
                                         NULL,
                                         apm_get_mem_map_client());
   if (NULL == payload_ptr)
   {
      result = AR_EFAILED;
      __gpr_cmd_end_command(gpr_pkt_ptr, result);
      return result;
   }

   // Switch 1: just to extract the destination domain ID
   switch (gpr_pkt_ptr->opcode)
   {
      case AMDB_CMD_REGISTER_MODULES:
      {
         amdb_module_registration_t *reg_ptr = (amdb_module_registration_t *)payload_ptr;
         dest_domain_id                      = reg_ptr->proc_domain;
         break;
      }

      case AMDB_CMD_DEREGISTER_MODULES:
      {
         amdb_module_deregistration_t *dereg_ptr = (amdb_module_deregistration_t *)payload_ptr;
         dest_domain_id                          = dereg_ptr->proc_domain;
         break;
      }

      case AMDB_CMD_LOAD_MODULES:
      case AMDB_CMD_UNLOAD_MODULES:
      {
         amdb_module_load_unload_t *load_unload_ptr = (amdb_module_load_unload_t *)payload_ptr;
         dest_domain_id                             = load_unload_ptr->proc_domain;
         break;
      }
      case APM_CMD_GET_CFG:
      {
         apm_module_param_data_t             *param_paylaod = (apm_module_param_data_t *)payload_ptr;
         amdb_param_id_module_version_info_t *version_info_ptr =
            (amdb_param_id_module_version_info_t *)(param_paylaod + 1);
         dest_domain_id = version_info_ptr->proc_domain;
         break;
      }

      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Invalid opcode 0x%X", gpr_pkt_ptr->opcode);
         break;
      }
   }

   // Routing
   if (host_domain_id != dest_domain_id)
   {
      // we're on the master, and this is an OFFLOAD case.
      AR_MSG(DBG_HIGH_PRIO,
             "AMDB CMD: OFFLOAD: Opcode 0x%lx received for domain ID %lu. Host Domain is %lu",
             gpr_pkt_ptr->opcode,
             dest_domain_id,
             host_domain_id);

      if (FALSE == apm_offload_utils_is_valid_sat_pd(dest_domain_id))
      {
         AR_MSG(DBG_ERROR_PRIO, "OFFLOAD: AMDB: Invalid satellite domain ID %lu", dest_domain_id);
         result = AR_EBADPARAM;
         __gpr_cmd_end_command(gpr_pkt_ptr, result);
         return result;
      }

      result = amdb_route_cmd_to_satellite(amdb_info_ptr, msg_ptr, dest_domain_id, payload_ptr);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "OFFLOAD: AMDB: Failed to send CMD Opcode 0x%lx to satellite domain ID %lu with %lu",
                gpr_pkt_ptr->opcode,
                dest_domain_id,
                result);
         return __gpr_cmd_end_command(gpr_pkt_ptr, result); // failure
      }
      return result;
   }
   // else
   // Switch 2: Handling at destination proc
   switch (gpr_pkt_ptr->opcode)
   {
      case AMDB_CMD_REGISTER_MODULES:
      {
         // If proc domain is not host domain, add to cmd ctrl from here
         amdb_module_registration_t *reg_ptr = (amdb_module_registration_t *)payload_ptr;

         result = amdb_reg_custom_modules(reg_ptr, cmd_header_ptr->payload_size);

         // We need to flush the payload if it is out of band, in case any modification is done
         if (is_out_of_band)
         {
            posal_cache_flush_v2(&payload_ptr, cmd_header_ptr->payload_size);
         }

         // amdb_register_custom_modules call is blocking, so we can free the msg pkt here
         __gpr_cmd_end_command(gpr_pkt_ptr, result);

         break;
      }

      case AMDB_CMD_DEREGISTER_MODULES:
      {
         amdb_module_deregistration_t *dereg_ptr = (amdb_module_deregistration_t *)payload_ptr;

         result = amdb_dereg_custom_modules(dereg_ptr, cmd_header_ptr->payload_size);

         // We need to flush the payload if it is out of band, in case any modification is done
         if (is_out_of_band)
         {
            posal_cache_flush_v2(&payload_ptr, cmd_header_ptr->payload_size);
         }

         // amdb_register_custom_modules call is blocking, so we can free the msg pkt here
         __gpr_cmd_end_command(gpr_pkt_ptr, result);

         break;
      }

      case AMDB_CMD_LOAD_MODULES:
      {
         if (!is_out_of_band)
         {
            if (AR_EOK != (result = spf_svc_alloc_rsp_payload(AMDB_MODULE_INSTANCE_ID,
                                                              gpr_pkt_ptr,
                                                              &gpr_rsp_pkt_ptr,
                                                              cmd_header_ptr->payload_size,
                                                              cmd_payload_pptr,
                                                              NULL)))
            {
               AR_MSG(DBG_ERROR_PRIO, "Failed to allocate RSP packet for in-band AMDB LOAD.");
               return result;
            }
         }

         result = amdb_load_unload_modules((amdb_module_load_unload_t *)(*cmd_payload_pptr),
                                           (uint8_t *)gpr_rsp_pkt_ptr,
                                           (uint8_t *)gpr_pkt_ptr,
                                           cmd_header_ptr->payload_size,
                                           TRUE /*is_load*/);
         // Since amdb_load_unload_modules is not blocking, gpr-packet-free is handled internally
         break;
      }

      case AMDB_CMD_UNLOAD_MODULES:
      {
         amdb_module_load_unload_t *unload_ptr = (amdb_module_load_unload_t *)payload_ptr;

         result = amdb_load_unload_modules(unload_ptr,
                                           (uint8_t *)gpr_rsp_pkt_ptr,
                                           (uint8_t *)gpr_pkt_ptr,
                                           cmd_header_ptr->payload_size,
                                           FALSE /*is_load*/);
         // Since amdb_load_unload_modules is not blocking, gpr-packet-free is handled internally

         break;
      }
      case APM_CMD_GET_CFG:
      {
         apm_module_param_data_t *param_paylaod = (apm_module_param_data_t *)payload_ptr;

         result = amdb_handle_get_config(param_paylaod);

         if (!is_out_of_band)
         {
            apm_cmd_rsp_get_cfg_t *cmd_get_cfg_rsp_ptr = GPR_PKT_GET_PAYLOAD(apm_cmd_rsp_get_cfg_t, gpr_rsp_pkt_ptr);
            cmd_get_cfg_rsp_ptr->status                = result;
            result                                     = __gpr_cmd_async_send(gpr_rsp_pkt_ptr);
            if (AR_EOK != result)
            {
               __gpr_cmd_free(gpr_rsp_pkt_ptr);
               result = AR_EFAILED;
            }

            __gpr_cmd_free(gpr_pkt_ptr);
         }
         else
         {
            posal_cache_flush_v2(&payload_ptr, cmd_header_ptr->payload_size);
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
            __gpr_cmd_free(gpr_pkt_ptr);
         }

         break;
      }

      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "Invalid opcode 0x%X", gpr_pkt_ptr->opcode);
         break;
      }
   }
   return result;
}

ar_result_t amdb_rspq_gpr_rsp_handler(amdb_thread_t *amdb_info_ptr, spf_msg_t *msg_ptr)
{
   ar_result_t   result = AR_EOK;
   gpr_packet_t *gpr_pkt_ptr;
   uint32_t      rsp_opcode;

   if (msg_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO, "Invalid param");
      return AR_EFAILED;
   }

   /** Get the pointer to GPR command */
   gpr_pkt_ptr = (gpr_packet_t *)msg_ptr->payload_ptr;

   /** Get the GPR command opcode */
   rsp_opcode = gpr_pkt_ptr->opcode;

   if (AMDB_CMD_RSP_LOAD_MODULES == rsp_opcode)
   {
      // here, the apm header is not a part of the inband GPR packet. so we should directly
      // call the handler to route the response to the client. We'll get here only on the MASTER.
      result = amdb_route_load_rsp_to_client(amdb_info_ptr, msg_ptr);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "OFFLOAD: AMDB: Failed to send LOAD RSP to client with %lu", result);
      }
   }
   else if (GPR_IBASIC_RSP_RESULT == rsp_opcode)
   {
      // handle basic responses from the Satellite - should get here only on the master
      /* Cases:
       * response to REGISTER
       * response to DEREGISTER
       * response to UNLOAD
       */
      result = amdb_route_basic_rsp_to_client(amdb_info_ptr, msg_ptr);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "OFFLOAD: AMDB: Failed to send basic response to client with %lu", result);
      }
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO, "OFFLOAD: AMDB: Unsupported response opcode 0x%lx", rsp_opcode);
      result = AR_EUNSUPPORTED;
   }
   return result;
}
