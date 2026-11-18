/**
 * \file spl_topo_capi.c
 *
 * \brief
 *
 *     Implementation of the capi v2 helpers.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "spl_topo_i.h"
#include "capi_fwk_extns_sync.h"
#include "capi_fwk_extns_ecns.h"
#include "capi_fwk_extns_dm.h"
#include "capi_fwk_extns_voice_delivery.h"

/* =======================================================================
Function Definitions
========================================================================== */

/**
 * Querry the module for required fwk extensions. Call to container layer code to
 * store any necessary data, as well as to error out if anything is not handled
 * by the container.
 */
ar_result_t spl_topo_capi_get_required_fmwk_extensions(void            *topo_ctx_ptr,
                                                       void            *module_ctx_ptr,
                                                       void            *amdb_handle,
                                                       capi_proplist_t *init_proplist_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   capi_err_t                     err_code                = CAPI_EOK;
   capi_framework_extension_id_t *needed_fmwk_xtn_ids_arr = NULL;
   spl_topo_t                    *topo_ptr                = (spl_topo_t *)topo_ctx_ptr;
   spl_topo_module_t             *module_ptr              = (spl_topo_module_t *)module_ctx_ptr;

   uint32_t module_instance_id = module_ptr->t_base.gu.module_instance_id;
   uint32_t log_id             = topo_ptr->t_base.gu.log_id;

   // Get the number of needed framework extensions.
   capi_proplist_t props_list;
   capi_prop_t     prop[1]; // query only one property at once.

   capi_num_needed_framework_extensions_t num_fmwk_extns = { 0 };

   uint32_t i;
   i                               = 0;
   prop[i].id                      = CAPI_NUM_NEEDED_FRAMEWORK_EXTENSIONS;
   prop[i].payload.actual_data_len = 0;
   prop[i].payload.max_data_len    = sizeof(num_fmwk_extns);
   prop[i].payload.data_ptr        = (int8_t *)&num_fmwk_extns;
   prop[i].port_info.is_valid      = FALSE;
   i++;

   props_list.prop_ptr  = prop;
   props_list.props_num = i;

   err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);
   // ignore any error since params are options.

#if SPL_TOPO_DEBUG_LEVEL >= SPL_TOPO_DEBUG_LEVEL_4
   TOPO_MSG(topo_ptr->t_base.gu.log_id,
            DBG_MED_PRIO,
            "Topo2 checking required fwk extensions mid = 0x%lx, num_extensions = %ld",
            module_instance_id,
            num_fmwk_extns.num_extensions);
#endif

   if ((num_fmwk_extns.num_extensions != 0) && (CAPI_SUCCEEDED(err_code)))
   {
      // Get the needed framework extensions.
      needed_fmwk_xtn_ids_arr =
         (capi_framework_extension_id_t *)posal_memory_malloc(sizeof(capi_framework_extension_id_t) *
                                                                 num_fmwk_extns.num_extensions,
                                                              topo_ptr->t_base.heap_id);

      VERIFY(result, (NULL != needed_fmwk_xtn_ids_arr));

      i                               = 0;
      prop[i].id                      = CAPI_NEEDED_FRAMEWORK_EXTENSIONS;
      prop[i].payload.actual_data_len = 0;
      prop[i].payload.max_data_len    = sizeof(capi_framework_extension_id_t) * num_fmwk_extns.num_extensions;
      prop[i].payload.data_ptr        = (int8_t *)needed_fmwk_xtn_ids_arr;
      prop[i].port_info.is_valid      = FALSE;
      i++;

      props_list.prop_ptr  = prop;
      props_list.props_num = i;

      err_code = amdb_capi_get_static_properties_f((void *)amdb_handle, init_proplist_ptr, &props_list);

      // Check if any framework extensions are not supported.
      bool_t any_extn_not_supported = FALSE;
      for (i = 0; i < num_fmwk_extns.num_extensions; i++)
      {
         bool_t extn_supported = TRUE;
         switch (needed_fmwk_xtn_ids_arr[i].id)
         {
            case FWK_EXTN_GLOBAL_SHMEM_MSG:
            {
               module_ptr->t_base.flags.need_global_shmem_extn = TRUE;
               break;
            }
            case FWK_EXTN_SOFT_TIMER:
            {
               module_ptr->t_base.flags.need_soft_timer_extn = TRUE;
               break;
            }
            case FWK_EXTN_SYNC:
            {
               module_ptr->t_base.flags.need_sync_extn = TRUE;
               break;
            }
            case FWK_EXTN_ECNS:
            {
               module_ptr->flags.need_ecns_extn = TRUE;
               break;
            }
            case FWK_EXTN_PCM:
            {
               module_ptr->t_base.flags.need_pcm_extn = TRUE;
               break;
            }
            case FWK_EXTN_DM:
            {
               module_ptr->t_base.flags.need_dm_extn = TRUE;
               break;
            }
            case FWK_EXTN_VOICE_DELIVERY:
            {
               module_ptr->flags.need_voice_delivery_extn = TRUE;
               break;
            }
            case FWK_EXTN_CONTAINER_PROC_DURATION:
            {
               module_ptr->t_base.flags.need_proc_dur_extn = TRUE;
               break;
            }
            case FWK_EXTN_CONTAINER_FRAME_DURATION:
            {
               module_ptr->t_base.flags.need_cntr_frame_dur_extn = TRUE;
               break;
            }
            case FWK_EXTN_THRESHOLD_CONFIGURATION:
            {
               module_ptr->t_base.flags.need_thresh_cfg_extn = TRUE;
               break;
            }
            case FWK_EXTN_TRIGGER_POLICY:
            {
               module_ptr->t_base.flags.need_trigger_policy_extn = TRUE;
               break;
            }
            case FWK_EXTN_IPC_PORT_HANDLER:
            {
               module_ptr->t_base.flags.need_ipc_port_extn = TRUE;
               break;
            }
            default:
            {
               extn_supported = FALSE;
               break;
            }
         }

         if (!extn_supported)
         {
            TOPO_MSG(log_id,
                     DBG_ERROR_PRIO,
                     "Module 0x%lX: fmwk extensions not supported are 0x%lx. Module may not work correctly",
                     module_instance_id,
                     needed_fmwk_xtn_ids_arr[i].id);
            any_extn_not_supported = TRUE;
         }
         else
         {
            TOPO_MSG(log_id,
                     DBG_HIGH_PRIO,
                     "Module 0x%lX: fmwk extensions needed & supported 0x%lX",
                     module_instance_id,
                     needed_fmwk_xtn_ids_arr[i].id);
         }
      }

      if (any_extn_not_supported)
      {
         TOPO_MSG(log_id,
                  DBG_ERROR_PRIO,
                  "Module 0x%lX: Failing since the fmwk doesnot support some extensions",
                  module_instance_id);
         THROW(result, AR_EFAILED);
      }
   }
   else
   {
      err_code = CAPI_EOK;
   }

   CATCH(result, TOPO_MSG_PREFIX, log_id)
   {
      result |= capi_err_to_ar_result(err_code);
   }

   if (needed_fmwk_xtn_ids_arr)
   {
      posal_memory_free(needed_fmwk_xtn_ids_arr);
   }
   return result;
}
