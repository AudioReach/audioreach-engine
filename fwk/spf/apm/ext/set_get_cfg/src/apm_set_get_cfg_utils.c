/**
 * \file apm_set_get_cfg.c
 *
 * \brief
 *     This file contains APM Set Get Config Utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/

#include "apm_internal.h"
#include "apm_proxy_vcpm_utils.h"
#include "offload_path_delay_api.h"
#include "apm_debug_info.h"
#include "apm_set_get_cfg_utils.h"
#include "apm_cntr_debug_if.h"
#include "proxy_cntr_if.h"
#include "apm_private_api.h"

/**==============================================================================
   Function Declaration
==============================================================================*/

ar_result_t apm_parse_fwk_get_cfg_params(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

ar_result_t apm_parse_fwk_set_cfg_params(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

ar_result_t apm_populate_cntr_param_payload_size(uint32_t                    container_id,
                                                 apm_cont_set_get_cfg_hdr_t *set_get_cfg_hdr_ptr,
                                                 apm_module_param_data_t *   param_ptr);

ar_result_t apm_compute_cntr_msg_payload_size(uint32_t param_id, uint32_t *cntr_msg_payload_size_ptr);

/**==============================================================================
   Global Defines
==============================================================================*/

apm_set_get_cfg_utils_vtable_t set_get_cfg_util_funcs =
   {.apm_parse_fwk_get_cfg_params_fptr = apm_parse_fwk_get_cfg_params,

    .apm_parse_fwk_set_cfg_params_fptr = apm_parse_fwk_set_cfg_params,

    .apm_populate_cntr_param_payload_size_fptr = apm_populate_cntr_param_payload_size,

    .apm_compute_cntr_msg_payload_size_fptr = apm_compute_cntr_msg_payload_size

   };

ar_result_t apm_populate_cntr_param_payload_size(uint32_t                    container_id,
                                                 apm_cont_set_get_cfg_hdr_t *set_get_cfg_hdr_ptr,
                                                 apm_module_param_data_t *   param_ptr)
{
   ar_result_t result = AR_EOK;
   switch (set_get_cfg_hdr_ptr->param_id)
   {
      case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
      {
         result = apm_populate_port_mf_cntr_param_payload_size(container_id, set_get_cfg_hdr_ptr, param_ptr);
         break;
      }
      case CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO:
      {
         apm_offload_voice_session_info_t *cont_cached_cfg_ptr;
         cont_cached_cfg_ptr = (apm_offload_voice_session_info_t *)set_get_cfg_hdr_ptr;
         
         apm_update_cont_param_set_cfg_msg_hdr(container_id,
                                               CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO,
                                               sizeof(cntr_param_id_offload_voice_session_info_t),
                                               param_ptr);
         
         /** populate the payload for container ptr */
         cntr_param_id_offload_voice_session_info_t *param_data_cfg_ptr = (cntr_param_id_offload_voice_session_info_t *)(param_ptr + 1);
         param_data_cfg_ptr->sg_id = cont_cached_cfg_ptr->sg_id;
         param_data_cfg_ptr->kpps_sf                        = cont_cached_cfg_ptr->kpps_sf;
         param_data_cfg_ptr->bw_sf                        = cont_cached_cfg_ptr->bw_sf;
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_populate_cntr_param_payload_size :: WARNING :: Un-supported PID: 0x%lX",
                set_get_cfg_hdr_ptr->param_id);

         result = AR_EUNSUPPORTED;
         break;
      }
   }
   return result;
}

ar_result_t apm_compute_cntr_msg_payload_size(uint32_t param_id , uint32_t *cntr_msg_payload_size_ptr)
{
   ar_result_t      result = AR_EOK;
   switch (param_id)
      {
         case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
         {
            result = apm_compute_port_mf_cntr_msg_payload_size(cntr_msg_payload_size_ptr);
            break;
         }
         case CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO:
         {
           *cntr_msg_payload_size_ptr += (sizeof(apm_module_param_data_t) + sizeof(cntr_param_id_offload_voice_session_info_t));
           break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_compute_cntr_msg_payload_size :: WARNING :: Un-supported PID: 0x%lX",
                   param_id);

            result = AR_EUNSUPPORTED;
            break;
         }
      }
      return result;
}

ar_result_t apm_parse_fwk_get_cfg_params(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_ext_utils_t *ext_utils_ptr;

   /** Validate input arguments */
   if (!apm_info_ptr || !mod_data_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "APM_GET_CFG: APM info[0x%lX] or module data ptr[0x%lX] is/are NULL",
             apm_info_ptr,
             mod_data_ptr);

      return AR_EFAILED;
   }

   /** Get the pointer to APM ext utils vtbl ptr  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   switch (mod_data_ptr->param_id)
   {
      case APM_PARAM_ID_PATH_DELAY:
      case APM_PARAM_ID_OFFLOAD_GRAPH_PATH_DELAY:
      {
         if (ext_utils_ptr->data_path_vtbl_ptr &&
             ext_utils_ptr->data_path_vtbl_ptr->apm_process_get_cfg_path_delay_fptr)
         {
            result = ext_utils_ptr->data_path_vtbl_ptr->apm_process_get_cfg_path_delay_fptr(apm_info_ptr, mod_data_ptr);
         }

         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_parse_fwk_get_cfg_params :: WARNING :: Un-supported PID: 0x%lX",
                mod_data_ptr->param_id);

         result = AR_EUNSUPPORTED;
         break;
      }
   }

   /** If no errors, cache this PID payload under current
    *  command control context. For get config commands, the APM
    *  params could be stacked together along with other module
    *  params. Client may populate the PID size for each of the
    *  params greater than what is required. Modules update the
    *  actual size after executing get config. For APM to update
    *  the param structure for it's param(based upon response
    *  received from containers), it needs to store the offset
    *  pointer for all those params as APM will not be able to
    *  parse the Get Config payload once param sizes are all
    *  updated by the modules. */
   if (AR_EOK == result)
   {
      apm_db_add_node_to_list(&apm_info_ptr->curr_cmd_ctrl_ptr->get_cfg_cmd_ctrl.pid_payload_list_ptr,
                              mod_data_ptr,
                              &apm_info_ptr->curr_cmd_ctrl_ptr->get_cfg_cmd_ctrl.num_pid_payloads);
   }

   return result;
}

ar_result_t apm_parse_fwk_set_cfg_params(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t result = AR_EOK;
   /** Validate input arguments */
   if (!apm_info_ptr || !mod_data_ptr)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "APM_SET_CFG: APM info[0x%lX] or module data ptr[0x%lX] is/are NULL",
             apm_info_ptr,
             mod_data_ptr);

      return AR_EFAILED;
   }

   switch (mod_data_ptr->param_id)
   {
      case APM_PARAM_ID_SATELLITE_PD_INFO:
      case APM_PARAM_ID_MASTER_PD_INFO:
      {
         /** Get the pointer to ext utils vtbl   */
         apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;

         if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_offload_handle_pd_info_fptr)
         {
            result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_handle_pd_info_fptr(apm_info_ptr, mod_data_ptr);
         }

         break;
      }
      case APM_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO:
      {
         /** Get the pointer to ext utils vtbl   */
         apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;

         if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_offload_handle_pd_info_fptr)
         {
            result = ext_utils_ptr->offload_vtbl_ptr->apm_offload_handle_scale_factor_info_fptr(apm_info_ptr, mod_data_ptr);
         }
      }
      break;
      case APM_PARAM_ID_PORT_MEDIA_FMT_REPORT_CFG:
      case APM_PARAM_ID_SG_STATE_REPORT_CFG:
      {
         apm_ext_utils_t *ext_utils_ptr = &apm_info_ptr->ext_utils;
         if (ext_utils_ptr->debug_info_utils_vtable_ptr &&
             ext_utils_ptr->debug_info_utils_vtable_ptr->apm_parse_debug_info_fptr)
         {
            result = ext_utils_ptr->debug_info_utils_vtable_ptr->apm_parse_debug_info_fptr(apm_info_ptr, mod_data_ptr);
         }
         break;
      }

      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_parse_fwk_set_cfg_params :: WARNING :: Un-supported PID: 0x%lX",
                mod_data_ptr->param_id);

         result = AR_EUNSUPPORTED;
         break;
      }
   }

   return result;
}

ar_result_t apm_set_get_cfg_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_info_ptr->ext_utils.set_get_cfg_vtbl_ptr = &set_get_cfg_util_funcs;

   return result;
}
