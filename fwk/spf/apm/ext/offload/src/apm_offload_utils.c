/**
 * \file apm_offload_utils.c
 *
 * \brief
 *     This file contains APM Offload processing Utilities
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
   Global Defines
==============================================================================*/

#include "apm_offload_utils.h"
#include "apm_offload_memmap_handler.h"
#include "apm_offload_memmap_utils.h"
#include "apm_offload_utils_i.h"
#include "apm_offload_pd_info.h"
#include "irm_api.h"
#include "amdb_api.h"
#include "apm_proxy_utils.h"
#include "apm_memmap_api.h"
#include "apm_cmd_utils.h"
#include "apm_debug_info.h"

/**==============================================================================
   Function Declarations
==============================================================================*/

bool_t apm_db_get_sat_contaniners_parent_cont_id(spf_list_node_t *sat_cont_info_ptr,
                                                 uint32_t         sat_cont_id,
                                                 uint32_t *       parent_cont_id);

ar_result_t apm_check_and_cache_satellite_container_config(apm_t *              apm_info_ptr,
                                                           apm_container_cfg_t *cont_cfg_ptr,
                                                           bool_t *             is_cont_offloaded);

ar_result_t apm_clear_cont_satellite_cont_list(apm_t *apm_info_ptr, apm_container_t *container_node_ptr);

ar_result_t apm_check_alloc_add_to_peer_domain_ctrl_list(spf_list_node_t **           list_head_pptr,
                                                         apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                         uint32_t *                   node_cntr_ptr);

ar_result_t apm_parse_imcl_peer_domain_info_list(apm_t *  apm_info_ptr,
                                                 uint8_t *mod_pid_payload_ptr,
                                                 uint32_t payload_size);

ar_result_t apm_offload_handle_pd_info(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr);

ar_result_t apm_offload_send_master_pd_info(apm_t *apm_info_ptr, uint32_t sat_proc_domain);

ar_result_t apm_send_close_all_to_sat(apm_t *apm_info_ptr);

bool_t apm_offload_is_master_pid();

ar_result_t apm_send_debug_info_to_sat(apm_t *apm_info_ptr);

ar_result_t apm_offload_basic_rsp_handler(apm_t *         apm_info_ptr,
                                          apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                          gpr_packet_t *  gpr_pkt_ptr);

ar_result_t apm_offload_utils_deinit();

ar_result_t apm_offload_get_ctrl_link_remote_peer_info(apm_module_ctrl_link_cfg_t * curr_ctrl_link_cfg_ptr,
                                                       uint8_t *                    sat_prv_cfg_ptr,
                                                       apm_module_t **              module_node_ptr_list,
                                                       apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                       uint32_t *                   local_peer_idx_ptr);

ar_result_t apm_search_in_sat_prv_peer_cfg_for_miid(uint8_t * sat_prv_cfg_ptr,
                                                    uint32_t  miid,
                                                    uint32_t *peer_domain_id_ptr);

/**==============================================================================
   Global Defines
==============================================================================*/

apm_offload_utils_vtable_t offload_util_funcs =
   {.apm_offload_shmem_cmd_handler_fptr = apm_offload_shmem_cmd_handler,

   .apm_offload_basic_rsp_handler_fptr = apm_offload_basic_rsp_handler,

   .apm_offload_master_memorymap_register_fptr = apm_offload_master_memorymap_register,

   .apm_offload_master_memorymap_check_deregister_fptr = apm_offload_master_memorymap_check_deregister,

   .apm_db_get_sat_contaniners_parent_cont_id_fptr = apm_db_get_sat_contaniners_parent_cont_id,

   .apm_check_and_cache_satellite_container_config_fptr = apm_check_and_cache_satellite_container_config,

   .apm_clear_cont_satellite_cont_list_fptr = apm_clear_cont_satellite_cont_list,

   .apm_check_alloc_add_to_peer_domain_ctrl_list_fptr = apm_check_alloc_add_to_peer_domain_ctrl_list,

   .apm_parse_imcl_peer_domain_info_list_fptr = apm_parse_imcl_peer_domain_info_list,

   .apm_offload_get_ctrl_link_remote_peer_info_fptr = apm_offload_get_ctrl_link_remote_peer_info,

   .apm_search_in_sat_prv_peer_cfg_for_miid_fptr = apm_search_in_sat_prv_peer_cfg_for_miid,

   .apm_offload_handle_pd_info_fptr = apm_offload_handle_pd_info,

   .apm_offload_send_master_pd_info_fptr = apm_offload_send_master_pd_info,

   .apm_send_close_all_to_sat_fptr = apm_send_close_all_to_sat,

   .apm_offload_sat_cleanup_fptr = apm_offload_sat_cleanup,

   .apm_offload_is_master_pid_fptr = apm_offload_is_master_pid,

   .apm_debug_info_cfg_hdlr_fptr = apm_send_debug_info_to_sat,

   .apm_offload_mem_mgr_reset_fptr = apm_offload_mem_mgr_reset

};

static apm_offload_utils_sat_pd_info_t g_apm_sat_pd_info = { 0 };

bool_t apm_db_get_sat_contaniners_parent_cont_id(spf_list_node_t *sat_cont_info_ptr,
                                                 uint32_t         sat_cont_id,
                                                 uint32_t *       parent_cont_id)
{
   spf_list_node_t *               curr_ptr;
   apm_satellite_cont_node_info_t *cont_list_node_ptr = NULL;

   curr_ptr = sat_cont_info_ptr;

   while (curr_ptr != NULL)
   {
      cont_list_node_ptr = (apm_satellite_cont_node_info_t *)curr_ptr->obj_ptr;
      if (NULL != cont_list_node_ptr)
      {
         /** return NULL handle */
         if (cont_list_node_ptr->satellite_cont_id == sat_cont_id)
         {
            *parent_cont_id = cont_list_node_ptr->parent_cont_id;
            return TRUE;
         }

         /** Else, keep traversing the list */
         curr_ptr = curr_ptr->next_ptr;
      }
   }
   return FALSE;
}

static bool_t apm_check_is_container_process_domain_master(uint32_t container_proc_id)
{
   uint32_t host_domain_id = 0;

   __gpr_cmd_get_host_domain_id(&host_domain_id);

   if (host_domain_id != container_proc_id)
   {
      return FALSE;
   }

   return TRUE;
}

static ar_result_t apm_parse_container_get_process_domain(apm_container_cfg_t *container_cfg_ptr,
                                                          uint32_t *           container_proc_id_ptr)
{
   ar_result_t result = AR_EOK;

   /** Validate the payload pointer */
   if (!container_cfg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CNTR_PARSE: container config ptr is NULL");
      return AR_EFAILED;
   }

   apm_prop_data_t *cntr_prop_ptr = (apm_prop_data_t *)(container_cfg_ptr + 1);
   *container_proc_id_ptr         = APM_PROC_DOMAIN_ID_INVALID; // assign to invalid process domain (=0)

   for (uint32_t i = 0; i < container_cfg_ptr->num_prop; i++)
   {
      switch (cntr_prop_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_PROC_DOMAIN:
         {
            if (cntr_prop_ptr->prop_size < sizeof(apm_cont_prop_id_proc_domain_t))
            {
               return AR_ENORESOURCE;
            }
            apm_cont_prop_id_proc_domain_t *cont_proc_id_ptr = (apm_cont_prop_id_proc_domain_t *)(cntr_prop_ptr + 1);
            *container_proc_id_ptr                           = cont_proc_id_ptr->proc_domain;
            return result; // return once proc domain is updated
         }
         break;
         default:
            break;
      }

      cntr_prop_ptr =
         (apm_prop_data_t *)((uint8_t *)cntr_prop_ptr + cntr_prop_ptr->prop_size + sizeof(apm_prop_data_t));
   }

   return result;
}

static ar_result_t apm_parse_container_get_parent_id(apm_container_cfg_t *container_cfg_ptr,
                                                     uint32_t *           parent_container_id)
{
   ar_result_t result = AR_ENEEDMORE;
   /** Validate the payload pointer */
   if (!container_cfg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "CNTR_PARSE: container config ptr is NULL");
      return AR_EFAILED;
   }

   apm_prop_data_t *cntr_prop_ptr = (apm_prop_data_t *)(container_cfg_ptr + 1);
   *parent_container_id           = 0; /// assign to invalid value

   for (uint32_t i = 0; i < container_cfg_ptr->num_prop; i++)
   {
      switch (cntr_prop_ptr->prop_id)
      {
         case APM_CONTAINER_PROP_ID_PARENT_CONTAINER_ID:
         {
            if (cntr_prop_ptr->prop_size < sizeof(apm_cont_prop_id_parent_container_t))
            {
               return AR_ENORESOURCE;
            }
            apm_cont_prop_id_parent_container_t *cont_pc_cgf_ptr =
               (apm_cont_prop_id_parent_container_t *)(cntr_prop_ptr + 1);
            *parent_container_id = cont_pc_cgf_ptr->parent_container_id;
            return AR_EOK;
         }
         break;
         default:
            break;
      }

      cntr_prop_ptr =
         (apm_prop_data_t *)((uint8_t *)cntr_prop_ptr + cntr_prop_ptr->prop_size + sizeof(apm_prop_data_t));
   }

   return result;
}

/*  Function to process the satellite container configuration
 *  satellite DSP or Master DSP. Assume Master DSP if the process domain is not specified.
 *  If the payload corresponds to satellite DSP, then process the configuration further
 *  to cache the satellite container configuration and associate it with its parent container.
 */
static ar_result_t apm_cache_satellite_container_config(apm_t *              apm_info_ptr,
                                                        apm_container_cfg_t *cont_cfg_ptr,
                                                        uint32_t             sat_cont_proc_id)
{
   ar_result_t                     result                    = AR_EOK;
   uint32_t                        parent_container_id       = 0;
   uint8_t *                       sat_cont_config_ptr       = NULL;
   apm_container_t *               parent_container_node_ptr = NULL;
   apm_satellite_cont_node_info_t *sat_container_node_ptr    = NULL;
   apm_cont_cmd_ctrl_t *           cont_cmd_ctrl_ptr;
   spf_list_node_t **              list_pptr;
   uint32_t *                      num_list_nodes_ptr;

   if (AR_EOK != (result = apm_parse_container_get_parent_id(cont_cfg_ptr, &parent_container_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CONT_PARSE: Failed to get the parent container configuration: CONT_ID[0x%lX]",
             cont_cfg_ptr->container_id);

      return AR_EBADPARAM;
   }

   if (AR_EOK != (result = apm_db_get_container_node(&apm_info_ptr->graph_info,
                                                     parent_container_id,
                                                     &parent_container_node_ptr,
                                                     APM_DB_OBJ_QUERY)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CONT_PARSE: Failed to get the parent container node: CONT_ID[0x%lX]",
             parent_container_id);

      return AR_EFAILED;
   }

   if (NULL == parent_container_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CONT_PARSE: Failed to get valid container node for parent container: CONT_ID[0x%lX]",
             parent_container_id);

      return AR_EFAILED;
   }

   if (parent_container_node_ptr)
   {

      /** Allocate memory for satellite container node information for parent container */
      if (NULL == (sat_container_node_ptr =
                      (apm_satellite_cont_node_info_t *)posal_memory_malloc(sizeof(apm_satellite_cont_node_info_t),
                                                                            APM_INTERNAL_STATIC_HEAP_ID)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "CONT_PARSE: Failed to allocate satellite cont node mem CONT_ID[0x%lX]",
                cont_cfg_ptr->container_id);

         result = AR_ENOMEMORY;
         return result;
      }

      /** Clear the allocated struct */
      memset(sat_container_node_ptr, 0, sizeof(apm_satellite_cont_node_info_t));
      sat_container_node_ptr->satellite_cont_id = cont_cfg_ptr->container_id;
      sat_container_node_ptr->parent_cont_id    = parent_container_id;

      if (AR_EOK != (result = apm_db_add_node_to_list(&apm_info_ptr->graph_info.sat_container_list_ptr,
                                                      sat_container_node_ptr,
                                                      &apm_info_ptr->graph_info.num_satellite_container)))
      {
         return result;
      }

      if (AR_EOK != (result = apm_db_add_node_to_list(&parent_container_node_ptr->sat_cont_list.satellite_cnts_list_ptr,
                                                      sat_container_node_ptr,
                                                      &parent_container_node_ptr->sat_cont_list.num_of_satellite_cnts)))
      {
         return result;
      }

      apm_get_cont_cmd_ctrl_obj(parent_container_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      sat_cont_config_ptr = (uint8_t *)cont_cfg_ptr;

      /** Pointer to this container's running list of module
       *  property configuration to be sent to containers as part
       *  of GRAPH OPEN command */
      list_pptr = &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.sat_graph_open_cfg.sat_cnt_config_ptr;

      /** Total number of module properties configured for the host
       *  container */
      num_list_nodes_ptr =
         &cont_cmd_ctrl_ptr->cached_cfg_params.graph_open_params.sat_graph_open_cfg.num_satellite_containers;

      /** Add the property configuration structure to the runnning
       *  list of module properties to be sent to containers */
      if (AR_EOK != (result = apm_db_add_node_to_list(list_pptr, sat_cont_config_ptr, num_list_nodes_ptr)))
      {
         return result;
      }
      // Setting the OLC container's proc domain field to the satellite proc domain
      parent_container_node_ptr->prop.proc_domain = sat_cont_proc_id;
   }

   return result;
}

/*  The function checks if the container configuration corresponds to
 *  satellite process domain or Master process domain. Assume Master process
 *  domain if the process domain is not specified.
 *  If the payload corresponds to satellite DSP, then process the configuration further
 *  to cache the satellite container configuration and associate it with its parent container.
 */
ar_result_t apm_check_and_cache_satellite_container_config(apm_t *              apm_info_ptr,
                                                           apm_container_cfg_t *cont_cfg_ptr,
                                                           bool_t *             is_cont_offloaded)
{
   ar_result_t result = AR_EOK;

   uint32_t container_proc_domain = APM_PROC_DOMAIN_ID_INVALID;
   *is_cont_offloaded             = FALSE;

   AR_MSG(DBG_LOW_PRIO, "CONT_PARSE: Container ID[0x%lX] check if it is Offloaded", cont_cfg_ptr->container_id);

   if (AR_EOK != (result = apm_parse_container_get_process_domain(cont_cfg_ptr, &container_proc_domain)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "CONT_PARSE: Container ID [0x%lX] failed to parse the process domain. result %lu",
             cont_cfg_ptr->container_id,
             result);
      return result;
   }

   if ((APM_PROC_DOMAIN_ID_INVALID != container_proc_domain) && (APM_PROP_ID_DONT_CARE != container_proc_domain))
   {
      if (!apm_check_is_container_process_domain_master(container_proc_domain))
      {
         AR_MSG(DBG_MED_PRIO,
                "CONT_PARSE: Container ID[0x%lX] satellite process domain %lu",
                cont_cfg_ptr->container_id,
                container_proc_domain);

         // todo : VB Will need to be updated to get the master domain in generic way
         if (AR_EOK !=
             (result = apm_cache_satellite_container_config(apm_info_ptr, cont_cfg_ptr, container_proc_domain)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "CONT_PARSE: Container ID[0x%lX] failed to cache the satellite container config. result %lu",
                   cont_cfg_ptr->container_id,
                   result);
            return result;
         }
         *is_cont_offloaded = TRUE;
      }
   }
   return result;
}

ar_result_t apm_clear_cont_satellite_cont_list(apm_t *apm_info_ptr, apm_container_t *container_node_ptr)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *               curr_ptr               = NULL;
   apm_satellite_cont_node_info_t *sat_container_node_ptr = NULL;

   if (0 < container_node_ptr->sat_cont_list.num_of_satellite_cnts)
   {

      AR_MSG(DBG_HIGH_PRIO,
             "apm_clear_cont_satellite_cont_list: parent container ID [0x%lX] "
             "number of satellite containers %lu",
             container_node_ptr->container_id,
             container_node_ptr->sat_cont_list.num_of_satellite_cnts);

      curr_ptr = container_node_ptr->sat_cont_list.satellite_cnts_list_ptr;
      while (curr_ptr != NULL)
      {
         sat_container_node_ptr = (apm_satellite_cont_node_info_t *)curr_ptr->obj_ptr;
         if (NULL != sat_container_node_ptr)
         {
            if (AR_EOK != (result = apm_db_remove_node_from_list(&apm_info_ptr->graph_info.sat_container_list_ptr,
                                                                 sat_container_node_ptr,
                                                                 &apm_info_ptr->graph_info.num_satellite_container)))
            {
               return result;
            }

            /** Else, keep traversing the list */
            curr_ptr = curr_ptr->next_ptr;
         }
      }

      spf_list_delete_list_and_free_objs(&container_node_ptr->sat_cont_list.satellite_cnts_list_ptr, TRUE);

      AR_MSG(DBG_HIGH_PRIO,
             "apm_clear_cont_satellite_cont_list: "
             "satellite container list deleted for parent container ID [0x%lX]",
             container_node_ptr->container_id);
   }
   return result;
}

ar_result_t apm_search_in_sat_prv_peer_cfg_for_miid(uint8_t * sat_prv_cfg_ptr,
                                                    uint32_t  miid,
                                                    uint32_t *peer_domain_id_ptr)
{
   ar_result_t                           result        = AR_EOK;
   apm_param_id_imcl_peer_domain_info_t *peer_info_ptr = (apm_param_id_imcl_peer_domain_info_t *)sat_prv_cfg_ptr;
   *peer_domain_id_ptr                                 = 0;
   apm_imcl_peer_domain_info_t *payload_ptr            = (apm_imcl_peer_domain_info_t *)(peer_info_ptr + 1);

   for (uint32_t i = 0; i < peer_info_ptr->num_imcl_peer_cfg; i++)
   {
      if (miid == payload_ptr->module_iid)
      {
         *peer_domain_id_ptr = payload_ptr->domain_id;
      }
      payload_ptr++;
   }
   return result;
}

ar_result_t apm_offload_get_ctrl_link_remote_peer_info(apm_module_ctrl_link_cfg_t * curr_ctrl_link_cfg_ptr,
                                                       uint8_t *                    sat_prv_cfg_ptr,
                                                       apm_module_t **              module_node_ptr_list,
                                                       apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                       uint32_t *                   local_peer_idx_ptr)
{
   ar_result_t result = AR_EOK;

   enum
   {
      PEER_1_MODULE = 0,
      PEER_2_MODULE = 1
   };

   /** Clear the remote peer info */
   memset(remote_peer_info_ptr, 0, sizeof(apm_imcl_peer_domain_info_t));

   /** On SAT APM: IF inter_proc link:
    *  1 peer will be a part of the module list in the OPEN payload - sat peer.
    *  The other peer - let's check if the miid is present in the
    *  prv peer list. if so, that domain id and sat_peer's
    *  domain ID - if different, then it is  inter_proc_sat_ctrl_link */

   if (module_node_ptr_list[PEER_2_MODULE])
   {
      *local_peer_idx_ptr              = PEER_2_MODULE;
      remote_peer_info_ptr->module_iid = curr_ctrl_link_cfg_ptr->peer_1_mod_iid;
   }
   else if (module_node_ptr_list[PEER_1_MODULE])
   {
      *local_peer_idx_ptr              = PEER_1_MODULE;
      remote_peer_info_ptr->module_iid = curr_ctrl_link_cfg_ptr->peer_2_mod_iid;
   }

   apm_search_in_sat_prv_peer_cfg_for_miid(sat_prv_cfg_ptr,
                                           remote_peer_info_ptr->module_iid,
                                           &remote_peer_info_ptr->domain_id);

   if (remote_peer_info_ptr->domain_id)
   {
      AR_MSG(DBG_HIGH_PRIO,
             "SAT APM: Peer with MIID 0x%lx was found in the peer_domain_cfg_list, in domain %lu",
             remote_peer_info_ptr->module_iid,
             remote_peer_info_ptr->domain_id);
   }
   else
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SAT APM Warning: This is a dangling ctrl link connection between peers 0x%lx and 0x%lx",
             curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
             curr_ctrl_link_cfg_ptr->peer_2_mod_iid);

      result = AR_ECONTINUE;
   }

   return result;
}

ar_result_t apm_parse_imcl_peer_domain_info_list(apm_t *  apm_info_ptr,
                                                 uint8_t *mod_pid_payload_ptr,
                                                 uint32_t payload_size)
{
   ar_result_t     result     = AR_EOK;
   uint32_t        cmd_opcode = 0;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;

   /** Validate the payload pointer */
   if (!mod_pid_payload_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "IMCL_PEER_CFG_PARSE: PID payload pointer is NULL");
      return AR_EFAILED;
   }

   /** Get the opcode for current command being executed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /** Validate if this function is called only for Graph OPEN
    *  or CLOSE, else return failure */
   if ((APM_CMD_GRAPH_OPEN != cmd_opcode) && (APM_CMD_GRAPH_CLOSE != cmd_opcode))
   {
      AR_MSG(DBG_ERROR_PRIO, "IMCL_PEER_CFG_PARSE: Unexpected command opcode, cmd_opcode[0x%lX]", cmd_opcode);

      return AR_EFAILED;
   }

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the pointer to imcl peer config payload
    *  start and cache in the cmd ctrl */
   apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sat_prv_cfg_ptr = mod_pid_payload_ptr;
   AR_MSG(DBG_HIGH_PRIO, "Satellite APM IMCL_PEER_CFG_PARSE: SAT APM received private param for ctrl link peer info");
   return result;
}

ar_result_t apm_check_alloc_add_to_peer_domain_ctrl_list(spf_list_node_t **           list_head_pptr,
                                                         apm_imcl_peer_domain_info_t *remote_peer_info_ptr,
                                                         uint32_t *                   node_cntr_ptr)
{
   bool_t                       is_first_node = FALSE;
   apm_imcl_peer_domain_info_t *data_ptr      = NULL;
   void *                       obj_ptr       = NULL;

   if (*node_cntr_ptr < 1)
   {
      is_first_node = TRUE;
   }

   if (!is_first_node)
   {
      // search
      spf_list_node_t *curr_ptr = *(list_head_pptr);

      while (curr_ptr)
      {
         data_ptr = (apm_imcl_peer_domain_info_t *)curr_ptr->obj_ptr;
         if (data_ptr->module_iid == remote_peer_info_ptr->module_iid)
         {
            // duplicate found, so we can just return success
            return AR_EOK;
         }
         curr_ptr = curr_ptr->next_ptr;
      }
   }

   // if we get here, it's either the first node, or we didn't find a duplicate
   obj_ptr = (void *)posal_memory_malloc(sizeof(apm_imcl_peer_domain_info_t), APM_INTERNAL_STATIC_HEAP_ID);

   if (NULL == obj_ptr)
   {
      return AR_ENOMEMORY;
   }

   memscpy(obj_ptr, sizeof(apm_imcl_peer_domain_info_t), remote_peer_info_ptr, sizeof(apm_imcl_peer_domain_info_t));

   AR_MSG(DBG_LOW_PRIO,
          "APM: apm_check_alloc_add_to_peer_domain_ctrl_list : Adding remote_peer_info to DB. Domain ID = %lu, IID = "
          "0x%lx",
          remote_peer_info_ptr->domain_id,
          remote_peer_info_ptr->module_iid);

   // add it to db_list
   return apm_db_add_node_to_list(list_head_pptr, obj_ptr, node_cntr_ptr);
}

static ar_result_t apm_offload_allocate_proxy_and_payload(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t              result            = AR_EOK;
   uint32_t                 size_of_param     = 0;
   bool_t                   USE_SYS_Q_TRUE    = TRUE;
   apm_module_param_data_t *irm_mod_data_ptr  = NULL;
   apm_module_param_data_t *amdb_mod_data_ptr = NULL;

   size_of_param = sizeof(apm_module_param_data_t) + ALIGN_8_BYTES(mod_data_ptr->param_size);

   /** Allocate payload of same size for IRM (to be broadcasted) */
   /** payloads for both IRM and AMDB cannot be allocated together
    * because we will lose the context of allocated ptr at the rsp
    * even after checking if the num rsp required are zero*/
   irm_mod_data_ptr = (apm_module_param_data_t *)posal_memory_malloc(size_of_param, APM_INTERNAL_STATIC_HEAP_ID);
   if (NULL == irm_mod_data_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to allocated memory for IRM sat pd info param");
      goto __bailout_allocate_proxy_and_payload_3;
   }

   /** Allocate payload of same size for AMDB (to be broadcasted) */
   amdb_mod_data_ptr = (apm_module_param_data_t *)posal_memory_malloc(size_of_param, APM_INTERNAL_STATIC_HEAP_ID);
   if (NULL == amdb_mod_data_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to allocated memory for AMDB sat pd info param");
      goto __bailout_allocate_proxy_and_payload_3;
   }

   /** Copy the contents of original payload to the newly allocated payloads */
   memscpy((void *)irm_mod_data_ptr, size_of_param, (void *)mod_data_ptr, size_of_param);
   memscpy((void *)amdb_mod_data_ptr, size_of_param, (void *)mod_data_ptr, size_of_param);

   /** Update the instance IDs of the newly allocated payloads since
    * the original payload will have APM instance id */
   irm_mod_data_ptr->module_instance_id  = IRM_MODULE_INSTANCE_ID;
   amdb_mod_data_ptr->module_instance_id = AMDB_MODULE_INSTANCE_ID;

   /** Allocate proxy manager for IRM, and use new payload */
   result = apm_handle_proxy_mgr_cfg_params(apm_info_ptr, irm_mod_data_ptr, USE_SYS_Q_TRUE);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to proxy manager");
      goto __bailout_allocate_proxy_and_payload_2;
   }

   /** Allocate proxy manager for AMDB, and use new payload */
   result = apm_handle_proxy_mgr_cfg_params(apm_info_ptr, amdb_mod_data_ptr, USE_SYS_Q_TRUE);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to proxy manager");
      goto __bailout_allocate_proxy_and_payload_1;
   }

   return result;

__bailout_allocate_proxy_and_payload_1:
   result |= apm_free_proxy_mgr_from_id(apm_info_ptr, AMDB_MODULE_INSTANCE_ID);
__bailout_allocate_proxy_and_payload_2:
   result |= apm_free_proxy_mgr_from_id(apm_info_ptr, IRM_MODULE_INSTANCE_ID);
__bailout_allocate_proxy_and_payload_3:
   if (NULL != amdb_mod_data_ptr)
   {
      posal_memory_free(amdb_mod_data_ptr);
   }
   if (NULL != irm_mod_data_ptr)
   {
      posal_memory_free(irm_mod_data_ptr);
   }

   return result;
}

static ar_result_t apm_offload_handle_sat_pd_info(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t                       result                  = AR_EOK;
   uint32_t                          required_payload        = 0;
   uint32_t                          host_domain_id          = 0;
   apm_offload_utils_sat_pd_info_t * apm_sat_pd_info_ptr     = &g_apm_sat_pd_info;
   apm_param_id_satellite_pd_info_t *payload_sat_pd_info_ptr = NULL;

   required_payload = sizeof(apm_param_id_satellite_pd_info_t);
   if (mod_data_ptr->param_size < required_payload)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "APM: apm_offload_handle_sat_pd_info: insufficient payload %lu, required %lu",
             mod_data_ptr->param_size,
             required_payload);
   }

   payload_sat_pd_info_ptr = (apm_param_id_satellite_pd_info_t *)(mod_data_ptr + 1);
   required_payload += ((sizeof(uint32_t) * payload_sat_pd_info_ptr->num_proc_domain_ids));
   if (mod_data_ptr->param_size < required_payload)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "APM: apm_offload_handle_sat_pd_info: insufficient payload %lu, required %lu",
             mod_data_ptr->param_size,
             required_payload);
   }

   __gpr_cmd_get_host_domain_id(&host_domain_id);

   /** If APM receives SAT pd info set param, it can consider itself as master APM */
   apm_sat_pd_info_ptr->master_proc_domain = host_domain_id;

   /** If there is a sat pd info already present - free it
    * This case should not happen, although we can use this behavior to free up
    * allocated memory by sending 0 sat proc domain id */
   if (NULL != apm_sat_pd_info_ptr->proc_domain_list)
   {
      posal_memory_free(apm_sat_pd_info_ptr->proc_domain_list);
      apm_sat_pd_info_ptr->proc_domain_list    = NULL;
      apm_sat_pd_info_ptr->num_proc_domain_ids = 0;
   }

   /** Store the sat pd info in the apm structure*/
   if (0 != payload_sat_pd_info_ptr->num_proc_domain_ids)
   {
      uint32_t size = payload_sat_pd_info_ptr->num_proc_domain_ids * sizeof(uint32_t);

      apm_sat_pd_info_ptr->proc_domain_list = (uint32_t *)posal_memory_malloc(size, APM_INTERNAL_STATIC_HEAP_ID);
      if (NULL == apm_sat_pd_info_ptr->proc_domain_list)
      {
         AR_MSG(DBG_ERROR_PRIO, "APM: apm_offload_handle_sat_pd_info: Failed to allocated memory sat pd info");
         goto __bailout_handle_sat_pd_info;
      }

      apm_sat_pd_info_ptr->num_proc_domain_ids = payload_sat_pd_info_ptr->num_proc_domain_ids;
      memscpy(apm_sat_pd_info_ptr->proc_domain_list, size, (payload_sat_pd_info_ptr + 1), size);
   }
   else
   {
      // If no sat pds are registered by APM with the sys util and num_proc_domain_ids is 0, we do not send cmds
      // to IRM or AMDB, since they would not have registered to sys util as well. Sending 0 num_proc_domain_ids
      // basically de-registers if any register is done
      apm_sys_util_vtable_t *sys_util_vtbl_ptr = apm_info_ptr->ext_utils.sys_util_vtbl_ptr;
      if (sys_util_vtbl_ptr && sys_util_vtbl_ptr->apm_sys_util_is_pd_info_available_fptr)
      {
         if (FALSE == sys_util_vtbl_ptr->apm_sys_util_is_pd_info_available_fptr())
         {
            goto __bailout_handle_sat_pd_info_1;
         }
      }
   }

   /** Register for satellite SSR up/down notification */
   apm_sys_util_vtable_t *sys_util_vtbl_ptr = apm_info_ptr->ext_utils.sys_util_vtbl_ptr;
   if (sys_util_vtbl_ptr && sys_util_vtbl_ptr->apm_sys_util_register_fptr)
   {
      result = sys_util_vtbl_ptr->apm_sys_util_register_fptr(apm_sat_pd_info_ptr->num_proc_domain_ids,
                                                             apm_sat_pd_info_ptr->proc_domain_list);
      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO, "APM: apm_offload_handle_sat_pd_info: Failed to register for SSR notification");
         goto __bailout_handle_sat_pd_info;
      }
   }

   /** Allocate the proxy managers and broadcast payloads for irm and amdb */
   result = apm_offload_allocate_proxy_and_payload(apm_info_ptr, mod_data_ptr);
   if (AR_EOK != result)
   {
      AR_MSG(DBG_ERROR_PRIO, "APM: apm_offload_handle_sat_pd_info: Failed to allocate proxy mgr or broadcast payload");
   }

   return result;

__bailout_handle_sat_pd_info:

   /** Update the error code of the param if anything when wrong before broadcast */
   mod_data_ptr->error_code = result;
   /** Free if non-null */
   if (apm_sat_pd_info_ptr->proc_domain_list)
   {
      posal_memory_free(apm_sat_pd_info_ptr->proc_domain_list);
      apm_sat_pd_info_ptr->proc_domain_list    = NULL;
      apm_sat_pd_info_ptr->num_proc_domain_ids = 0;
   }
__bailout_handle_sat_pd_info_1:
   return result;
}

static ar_result_t apm_offload_handle_master_pd_info(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t                      result              = AR_EOK;
   uint32_t                         required_payload    = 0;
   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = &g_apm_sat_pd_info;

   required_payload = sizeof(apm_param_id_master_pd_info_t);
   if (mod_data_ptr->param_size < required_payload)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "APM: apm_offload_handle_master_pd_info: insufficient payload %lu, required %lu",
             mod_data_ptr->param_size,
             required_payload);
   }

   apm_param_id_master_pd_info_t *master_pd_info_ptr = (apm_param_id_master_pd_info_t *)(mod_data_ptr + 1);

   /** Store the master proc domain so we can have different
       behavior when the based on if it is master or satellite */
   apm_sat_pd_info_ptr->master_proc_domain = master_pd_info_ptr->proc_domain;

   /** Register for master status notification */
   apm_sys_util_vtable_t *sys_util_vtbl_ptr = apm_info_ptr->ext_utils.sys_util_vtbl_ptr;
   if (sys_util_vtbl_ptr && sys_util_vtbl_ptr->apm_sys_util_register_fptr)
   {
      uint32_t num_proc_domain_id = 1;
      result = sys_util_vtbl_ptr->apm_sys_util_register_fptr(num_proc_domain_id, &master_pd_info_ptr->proc_domain);
   }

   return result;
}

ar_result_t apm_offload_handle_pd_info(apm_t *apm_info_ptr, apm_module_param_data_t *mod_data_ptr)
{
   ar_result_t result = AR_EOK;

   switch (mod_data_ptr->param_id)
   {
      case APM_PARAM_ID_SATELLITE_PD_INFO:
      {
         result = apm_offload_handle_sat_pd_info(apm_info_ptr, mod_data_ptr);
         break;
      }
      case APM_PARAM_ID_MASTER_PD_INFO:
      {
         result = apm_offload_handle_master_pd_info(apm_info_ptr, mod_data_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "APM: apm_offload_handle_pd_info: Unsupported pid 0x%X", mod_data_ptr->param_id);
         result = AR_EUNSUPPORTED;
      }
   }
   return result;
}
ar_result_t apm_send_debug_info_to_sat(apm_t *apm_info_ptr)
{
   ar_result_t                      result              = AR_EOK;
   uint32_t                         host_domain_id      = 0;
   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = &g_apm_sat_pd_info;
   uint32_t                         size                = 0;
   uint8_t *                        payload             = NULL;
   apm_cmd_header_t *               cmd_header          = NULL;
   apm_cmd_ctrl_t *                 apm_cmd_ctrl_ptr    = apm_info_ptr->curr_cmd_ctrl_ptr;

   if (!apm_offload_is_master_pid())
   {
      return result;
   }

   // If message is not pending to sat apm, no need to send the debug info.
   if (!apm_cmd_ctrl_ptr->set_cfg_cmd_ctrl.debug_info.is_sattelite_debug_info_send_pending)
   {
      return result;
   }

   apm_cmd_ctrl_ptr->set_cfg_cmd_ctrl.debug_info.is_sattelite_debug_info_send_pending = FALSE;

   size = sizeof(apm_cmd_header_t) + sizeof(apm_module_param_data_t) +
          sizeof(apm_param_id_port_media_fmt_report_cfg_enable_t);

   __gpr_cmd_get_host_domain_id(&host_domain_id);

   if ((apm_offload_is_master_pid()) && (0 != apm_sat_pd_info_ptr->num_proc_domain_ids))
   {
      for (uint32_t sat_pd_idx = 0; sat_pd_idx < apm_sat_pd_info_ptr->num_proc_domain_ids; sat_pd_idx++)
      {
         ar_result_t   local_result    = AR_EOK;
         gpr_packet_t *gpr_pkt_ptr     = NULL;
         uint32_t      sat_proc_domain = apm_sat_pd_info_ptr->proc_domain_list[sat_pd_idx];

         gpr_cmd_alloc_ext_t args;
         args.src_domain_id = host_domain_id; // Master ID
         args.src_port      = APM_MODULE_INSTANCE_ID;
         args.dst_domain_id = sat_proc_domain;
         args.dst_port      = APM_MODULE_INSTANCE_ID;
         args.client_data   = 0;
         args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;
         args.opcode        = APM_CMD_SET_CFG;
         args.payload_size  = size;
         args.ret_packet    = &gpr_pkt_ptr;

         local_result = __gpr_cmd_alloc_ext(&args);
         if (AR_DID_FAIL(result) || NULL == gpr_pkt_ptr)
         {
            result |= local_result;
            AR_MSG(DBG_ERROR_PRIO, "APM: Failed to allocate gpr packet %lu", local_result);
            continue;
         }

         payload = GPR_PKT_GET_PAYLOAD(uint8_t, gpr_pkt_ptr);

         /** Fill apm header information, only in band is used here */
         cmd_header                      = (apm_cmd_header_t *)payload;
         cmd_header->payload_size        = args.payload_size - sizeof(apm_cmd_header_t);
         cmd_header->payload_address_lsw = 0;
         cmd_header->payload_address_msw = 0;
         cmd_header->mem_map_handle      = 0;

         /** Fill module param data */
         apm_module_param_data_t *param_data = (apm_module_param_data_t *)(payload + sizeof(apm_cmd_header_t));
         param_data->module_instance_id      = APM_MODULE_INSTANCE_ID;
         param_data->param_id                = APM_PARAM_ID_PORT_MEDIA_FMT_REPORT_CFG;
         param_data->param_size              = sizeof(apm_param_id_port_media_fmt_report_cfg_enable_t);

         apm_param_id_port_media_fmt_report_cfg_enable_t *master_enable_info_ptr =
            (apm_param_id_port_media_fmt_report_cfg_enable_t *)(param_data + 1);
         master_enable_info_ptr->is_port_media_fmt_report_cfg_enabled =
            apm_info_ptr->curr_cmd_ctrl_ptr->set_cfg_cmd_ctrl.debug_info.is_port_media_fmt_enable;

         if (AR_EOK != (local_result = __gpr_cmd_async_send(gpr_pkt_ptr)))
         {
            result |= local_result;
            __gpr_cmd_free(gpr_pkt_ptr);
            AR_MSG(DBG_ERROR_PRIO,
                   "APM: sending sending debug info to sat %lu failed with %lu",
                   apm_info_ptr->curr_cmd_ctrl_ptr->set_cfg_cmd_ctrl.debug_info.is_port_media_fmt_enable,
                   local_result);
            continue;
         }

         /** Increment the number of commands issues */
         apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

         /** If at least 1 command issued to container, set the
          *  command response pending status */
         apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;
         AR_MSG(DBG_HIGH_PRIO, "APM: sending debug info to sat successful for %lu", sat_proc_domain);
      }
   }
   return result;
}

ar_result_t apm_offload_send_master_pd_info(apm_t *apm_info_ptr, uint32_t sat_proc_domain)
{
   ar_result_t       result           = AR_EOK;
   apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;
   gpr_packet_t *    gpr_pkt_ptr      = NULL;
   uint8_t *         payload          = NULL;
   apm_cmd_header_t *cmd_header       = NULL;
   uint32_t          size             = 0;
   uint32_t          host_domain_id   = 0;

   __gpr_cmd_get_host_domain_id(&host_domain_id);

   size = sizeof(apm_cmd_header_t) + sizeof(apm_module_param_data_t) + sizeof(apm_param_id_master_pd_info_t);

   /** allocate the GPR packet to Send to satellite DSP */
   gpr_cmd_alloc_ext_t args;
   args.src_domain_id = host_domain_id; // Master ID
   args.src_port      = APM_MODULE_INSTANCE_ID;
   args.dst_domain_id = sat_proc_domain;
   args.dst_port      = APM_MODULE_INSTANCE_ID;
   args.client_data   = 0;
   args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;

   /** Will use this token during response tracking and for book-keeping */
   args.opcode       = APM_CMD_SET_CFG;
   args.payload_size = size;
   args.ret_packet   = &gpr_pkt_ptr;

   result = __gpr_cmd_alloc_ext(&args);
   if (AR_DID_FAIL(result) || NULL == gpr_pkt_ptr)
   {
      result = AR_ENOMEMORY;
      AR_MSG(DBG_ERROR_PRIO, "APM: Failed to allocate gpr packet %lu", result);
      return result;
   }

   payload = GPR_PKT_GET_PAYLOAD(uint8_t, gpr_pkt_ptr);

   /** Fill apm header information, only in band is used here */
   cmd_header                      = (apm_cmd_header_t *)payload;
   cmd_header->payload_size        = args.payload_size - sizeof(apm_cmd_header_t);
   cmd_header->payload_address_lsw = 0;
   cmd_header->payload_address_msw = 0;
   cmd_header->mem_map_handle      = 0;

   /** Fill module param data */
   apm_module_param_data_t *param_data = (apm_module_param_data_t *)(payload + sizeof(apm_cmd_header_t));
   param_data->module_instance_id      = APM_MODULE_INSTANCE_ID;
   param_data->param_id                = APM_PARAM_ID_MASTER_PD_INFO;
   param_data->param_size              = sizeof(apm_param_id_master_pd_info_t);

   apm_param_id_master_pd_info_t *master_pd_info_ptr = (apm_param_id_master_pd_info_t *)(param_data + 1);
   master_pd_info_ptr->proc_domain                   = host_domain_id;

   if (AR_EOK != (result = __gpr_cmd_async_send(gpr_pkt_ptr)))
   {
      result = AR_EFAILED;
      AR_MSG(DBG_ERROR_PRIO, "APM: sending master pd info param failed with %lu", result);
      goto __bailout_send_master_pd;
   }

   /** Increment the number of commands issues */
   apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

   /** If at least 1 command issued to container, set the
    *  command response pending status */
   apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;

   AR_MSG(DBG_HIGH_PRIO, "APM: sending master pd info param successful");

   return result;
__bailout_send_master_pd:
   __gpr_cmd_free(gpr_pkt_ptr);
   return result;
}

ar_result_t apm_send_close_all_to_sat(apm_t *apm_info_ptr)
{
   ar_result_t                      result              = AR_EOK;
   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = &g_apm_sat_pd_info;
   apm_cmd_ctrl_t *                 apm_cmd_ctrl_ptr    = apm_info_ptr->curr_cmd_ctrl_ptr;
   uint32_t                         host_domain_id      = 0;

   /** Only send the close all from master */
   if (!apm_offload_is_master_pid())
   {
      return result;
   }

   __gpr_cmd_get_host_domain_id(&host_domain_id);

   if ((apm_offload_is_master_pid()) && (0 != apm_sat_pd_info_ptr->num_proc_domain_ids))
   {
      for (uint32_t sat_pd_idx = 0; sat_pd_idx < apm_sat_pd_info_ptr->num_proc_domain_ids; sat_pd_idx++)
      {
         ar_result_t   local_result    = AR_EOK;
         gpr_packet_t *gpr_pkt_ptr     = NULL;
         uint32_t      sat_proc_domain = apm_sat_pd_info_ptr->proc_domain_list[sat_pd_idx];

         if (APM_PROC_DOMAIN_ID_CDSP == sat_proc_domain)
         {
            /* Close all is due to HLOS restart. */
            AR_MSG(DBG_HIGH_PRIO, "APM: skipping sending close all to CDSP as dynamic PD restarts when HLOS restarts");
            continue;
         }

         /** allocate the GPR packet to Send to satellite DSP */
         gpr_cmd_alloc_ext_t args;
         args.src_domain_id = host_domain_id; // Master ID
         args.src_port      = APM_MODULE_INSTANCE_ID;
         args.dst_domain_id = sat_proc_domain;
         args.dst_port      = APM_MODULE_INSTANCE_ID;
         args.client_data   = 0;
         args.token         = apm_info_ptr->curr_cmd_ctrl_ptr->list_idx;

         /** Will use this token during response tracking and for book-keeping */
         args.opcode       = APM_CMD_CLOSE_ALL;
         args.payload_size = 0;
         args.ret_packet   = &gpr_pkt_ptr;

         local_result = __gpr_cmd_alloc_ext(&args);
         if (AR_DID_FAIL(result) || NULL == gpr_pkt_ptr)
         {
            result |= local_result;
            AR_MSG(DBG_ERROR_PRIO, "APM: Failed to allocate gpr packet %lu", local_result);
            continue;
         }

         if (AR_EOK != (local_result = __gpr_cmd_async_send(gpr_pkt_ptr)))
         {
            result |= local_result;
            __gpr_cmd_free(gpr_pkt_ptr);
            AR_MSG(DBG_ERROR_PRIO, "APM: sending close all to sat %lu failed with %lu", sat_proc_domain, local_result);
            continue;
         }

         /** Increment the number of commands issues */
         apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

         /** If at least 1 command issued to container, set the
          *  command response pending status */
         apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;
      }
   }

   return result;
}
bool_t apm_offload_is_master_pid()
{
   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = &g_apm_sat_pd_info;
   uint32_t                         host_domain_id      = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   return (host_domain_id == apm_sat_pd_info_ptr->master_proc_domain);
}

bool_t apm_offload_utils_is_valid_sat_pd(uint32_t sat_proc_domain_id)
{
   apm_offload_utils_sat_pd_info_t *apm_sat_pd_info_ptr = &g_apm_sat_pd_info;
   uint32_t *                       cur_proc_domain_id  = apm_sat_pd_info_ptr->proc_domain_list;
   if (0 != apm_sat_pd_info_ptr->num_proc_domain_ids)
   {
      for (uint32_t i = 0; i < apm_sat_pd_info_ptr->num_proc_domain_ids; i++)
      {
         if ((APM_PROC_DOMAIN_ID_INVALID != sat_proc_domain_id) && (cur_proc_domain_id[i] == sat_proc_domain_id))
         {
            return TRUE;
         }
      }
   }
   else
   {
      if ((APM_PROC_DOMAIN_ID_MDSP == sat_proc_domain_id) || (APM_PROC_DOMAIN_ID_ADSP == sat_proc_domain_id) ||
          (APM_PROC_DOMAIN_ID_ADSP_1 == sat_proc_domain_id) || (APM_PROC_DOMAIN_ID_ADSP_2 == sat_proc_domain_id) ||
          (APM_PROC_DOMAIN_ID_SDSP == sat_proc_domain_id) || (APM_PROC_DOMAIN_ID_CDSP == sat_proc_domain_id))
      {
         return TRUE;
      }
   }
   return FALSE;
}

ar_result_t apm_offload_utils_get_sat_proc_domain_list(apm_offload_utils_sat_pd_info_t **sat_pd_info_pptr)
{
   if (sat_pd_info_pptr)
   {
      *sat_pd_info_pptr = &g_apm_sat_pd_info;
      return AR_EOK;
   }
   return AR_EBADPARAM;
}

ar_result_t apm_offload_basic_rsp_handler(apm_t *         apm_info_ptr,
                                          apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                          gpr_packet_t *  gpr_pkt_ptr)
{
   ar_result_t              result               = AR_EOK;
   gpr_ibasic_rsp_result_t *curr_rsp_payload_ptr = NULL;

   AR_MSG(DBG_HIGH_PRIO, "APM: apm_offload_basic_rsp_handler: Received MDF Basic Response");

   /** get basic response payload. This has sat memhandle and its domain*/
   curr_rsp_payload_ptr = GPR_PKT_GET_PAYLOAD(gpr_ibasic_rsp_result_t, gpr_pkt_ptr);

   /** We can get the basic response in two main cases:
       1. A response for the MDF Unmap command, or
       2. Perhaps a failure for the MDF MAP command
       3. Rsp for master pd info sent to satellite */
   switch (curr_rsp_payload_ptr->opcode)
   {
      case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
      case APM_CMD_SHARED_MEM_MAP_REGIONS:
      {
         result = apm_offload_mem_basic_rsp_handler(apm_info_ptr, apm_cmd_ctrl_ptr, gpr_pkt_ptr);
         break;
      }
      case APM_CMD_SET_CFG:
      case APM_CMD_CLOSE_ALL:
      {
         /** Nothing particular to handle, just need to free the cmd ctrl obj
          *  and deallocate the resources */
         if (AR_EOK != curr_rsp_payload_ptr->status)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "APM: apm_offload_basic_rsp_handler: Opcode %lu returned with failed result %lu ",
                   curr_rsp_payload_ptr->opcode,
                   curr_rsp_payload_ptr->status);
         }
         else
         {
            AR_MSG(DBG_LOW_PRIO,
                   "APM: apm_offload_basic_rsp_handler: Opcode %lu returned with success result %lu ",
                   curr_rsp_payload_ptr->opcode,
                   curr_rsp_payload_ptr->status);
         }
         __gpr_cmd_free(gpr_pkt_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "APM: apm_offload_basic_rsp_handler: Unexpected response for cmd opcode 0x%lx",
                curr_rsp_payload_ptr->opcode);
         break;
      }
   }

   return result;
}

ar_result_t apm_offload_utils_init(apm_t *apm_info_ptr)
{
   ar_result_t result         = AR_EOK;
   uint32_t    host_domain_id = 0;
   __gpr_cmd_get_host_domain_id(&host_domain_id);

   // Every APM will consider itself master unless until mentioned
   g_apm_sat_pd_info.master_proc_domain     = host_domain_id;
   apm_info_ptr->ext_utils.offload_vtbl_ptr = &offload_util_funcs;

   apm_offload_global_mem_mgr_init(APM_INTERNAL_STATIC_HEAP_ID);

   return result;
}

ar_result_t apm_offload_utils_deinit()
{
   memset((void *)&g_apm_sat_pd_info, 0, sizeof(g_apm_sat_pd_info));
   apm_offload_global_mem_mgr_deinit();

   return AR_EOK;
}
