/**
 * \file sgm_cmd_parser_utils.c
 * \brief
 *     This file contains olc functions for command handling.
 *  
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"
#include "olc_driver.h"
/* =======================================================================
Static Function Definitions
========================================================================== */

bool_t olc_get_cmd_hndl_node(spf_list_node_t *      cmd_handle_list_ptr,
                             uint32_t               num_cmd_hndl_list,
                             spgm_cmd_hndl_node_t **cmd_handle_node_ptr,
                             uint32_t               token)
{

   spf_list_node_t *     curr_node_ptr;
   spgm_cmd_hndl_node_t *cmd_hndl_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = cmd_handle_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      cmd_hndl_node_ptr = (spgm_cmd_hndl_node_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == cmd_hndl_node_ptr)
      {
         return FALSE;
      }

      if (token == cmd_hndl_node_ptr->token)
      {
         *cmd_handle_node_ptr = cmd_hndl_node_ptr;
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

bool_t check_if_module_is_in_list(spf_list_node_t *mod_list_ptr, uint32_t num_modules_list, uint32_t module_instance_id)
{

   spf_list_node_t *  curr_node_ptr;
   sgm_module_info_t *module_node_ptr;
   /** Get the pointer to start of the list of module list nodes */
   curr_node_ptr = mod_list_ptr;

   /** Check if the module instance  exists */
   while (curr_node_ptr)
   {
      module_node_ptr = (sgm_module_info_t *)curr_node_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == module_node_ptr)
      {
         return FALSE;
      }

      if (module_instance_id == module_node_ptr->instance_id)
      {
         return TRUE;
      }

      /** Else, keep traversing the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   return FALSE;
}

ar_result_t add_module_list_to_graph_info(spgm_info_t *       spgm_ptr,
                                          apm_modules_list_t *mod_list_ptr,
                                          uint32_t            host_container_id)
{
   ar_result_t       result = AR_EOK;
   uint32_t          arr_idx;
   apm_module_cfg_t *cmd_module_ptr;

   sgm_module_info_t *module_node_ptr = NULL;

   cmd_module_ptr = (apm_module_cfg_t *)(mod_list_ptr + 1);

   for (arr_idx = 0; arr_idx < mod_list_ptr->num_modules; arr_idx++)
   {
      /** ALlocate memory for module node */

      /* TODO VB : Not a really good approach to allocate memory for each module node.
       * Need to check back to optimize this
       */
      if (NULL == (module_node_ptr =
                      (sgm_module_info_t *)posal_memory_malloc(sizeof(sgm_module_info_t), spgm_ptr->cu_ptr->heap_id)))
      {
         AR_MSG(DBG_ERROR_PRIO, "MOD_PARSE: Failed to allocate module node memory");
         return AR_ENOMEMORY;
      }
      module_node_ptr->instance_id            = cmd_module_ptr->instance_id;
      module_node_ptr->module_id              = cmd_module_ptr->module_id;
      module_node_ptr->container_id           = mod_list_ptr->container_id;
      module_node_ptr->sub_graph_id           = mod_list_ptr->sub_graph_id;
      module_node_ptr->is_registered_with_gpr = FALSE;

      if (host_container_id == mod_list_ptr->container_id)
      {
         sgm_util_add_node_to_list(spgm_ptr,
                                   &spgm_ptr->gu_graph_info.olc_module_list_ptr,
                                   module_node_ptr,
                                   &spgm_ptr->gu_graph_info.num_olc_modules);
      }
      else
      {
         sgm_util_add_node_to_list(spgm_ptr,
                                   &spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                   module_node_ptr,
                                   &spgm_ptr->gu_graph_info.num_satellite_modules);
      }
      module_node_ptr = NULL;
      cmd_module_ptr++;
   }

   return result;
}

ar_result_t sgm_client_command_fill_header_cfg(uint8_t *           payload_ptr,
                                               uint32_t *          header_payload_size_ptr,
                                               sgm_shmem_handle_t *shmem_node_ptr,
                                               uint32_t            cmd_payload_size,
                                               bool_t              is_payload_inband)
{
   ar_result_t result = AR_EOK;

   apm_cmd_header_t *cmd_header = (apm_cmd_header_t *)payload_ptr;
   // todo : can we check only if shmem_ptr to determine inband or OOB
   // updated to check both
   if ((TRUE == is_payload_inband) && (NULL == shmem_node_ptr->shm_mem_ptr))
   {
      // Inband case
      cmd_header->payload_address_lsw = 0;
      cmd_header->payload_address_msw = 0;
      cmd_header->mem_map_handle      = 0;
      cmd_header->payload_size        = cmd_payload_size;
   }
   else if ((NULL != shmem_node_ptr) && (FALSE == is_payload_inband))
   {
      // Out of band case. Loaned memory is virtual offset based
      cmd_header->payload_address_lsw = shmem_node_ptr->mem_attr.offset;
      cmd_header->payload_address_msw = 0;
      cmd_header->mem_map_handle      = shmem_node_ptr->mem_attr.sat_handle;
      cmd_header->payload_size        = cmd_payload_size;
   }
   else
   {
      // Invalid case. (out of band, but shm memory was not allocated.
      // This case should not come in general
      *header_payload_size_ptr = 0;
      return AR_EUNEXPECTED;
   }

   *header_payload_size_ptr = sizeof(apm_cmd_header_t);

   return result;
}

ar_result_t sgm_open_get_sub_graph_payload_size(apm_sub_graph_cfg_t **sg_cfg_list_pptr,
                                                uint32_t              num_sub_graphs,
                                                uint32_t *            sub_graph_payload_size_ptr,
                                                spgm_id_info_t *      sgm_id_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    payload_size       = 0;
   uint32_t    size_per_sub_graph = 0;
   uint32_t    arr_indx           = 0;
   uint32_t    prop_arr_indx      = 0;
   uint32_t    offset             = 0;
   uint32_t    num_sub_graph_prop = 0;

   uint8_t *            prop_node_ptr          = NULL;
   apm_sub_graph_cfg_t *sub_graph_cfg_node_ptr = NULL;
   apm_prop_data_t *    sg_prop_node_ptr       = NULL;

   *sub_graph_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_sub_graph_cfg_t);

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_sub_graphs; arr_indx++)
   {
      sub_graph_cfg_node_ptr = sg_cfg_list_pptr[arr_indx];
      if (NULL != sub_graph_cfg_node_ptr)
      {
         size_per_sub_graph = sizeof(apm_sub_graph_cfg_t);
         num_sub_graph_prop = sub_graph_cfg_node_ptr->num_sub_graph_prop;
         sub_graph_cfg_node_ptr++; // ptr offset by size_per_sub_graph
         prop_node_ptr = (uint8_t *)sub_graph_cfg_node_ptr;
         for (prop_arr_indx = 0; prop_arr_indx < num_sub_graph_prop; prop_arr_indx++)
         {
            sg_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            offset           = sizeof(apm_prop_data_t) + sg_prop_node_ptr->prop_size;
            size_per_sub_graph += offset;
            prop_node_ptr += offset;
         }
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get sub_graph_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);
         return AR_EBADPARAM;
      }
      payload_size += size_per_sub_graph;
      size_per_sub_graph = 0;
   }

   *sub_graph_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_container_payload_size(apm_container_cfg_t **cont_cfg_list_pptr,
                                                uint32_t              num_container,
                                                uint32_t *            container_payload_size_ptr,
                                                spgm_id_info_t *      sgm_id_ptr,
                                                uint32_t *            get_sat_pd_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    payload_size       = 0;
   uint32_t    size_per_container = 0;
   uint32_t    arr_indx           = 0;
   uint32_t    prop_arr_indx      = 0;
   uint32_t    offset             = 0;
   uint32_t    num_cont_prop      = 0;
   uint32_t    sat_pd             = 0;

   apm_container_cfg_t *cont_cfg_node_ptr  = NULL;
   apm_prop_data_t *    cont_prop_node_ptr = NULL;
   uint8_t *            prop_node_ptr      = NULL;

   *container_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the container configuration.
   payload_size += sizeof(apm_param_id_container_cfg_t);

   // variable payload size for each of the container
   for (arr_indx = 0; arr_indx < num_container; arr_indx++)
   {
      cont_cfg_node_ptr = cont_cfg_list_pptr[arr_indx];
      if (cont_cfg_node_ptr)
      {
         size_per_container = sizeof(apm_container_cfg_t);
         num_cont_prop      = cont_cfg_node_ptr->num_prop;
         cont_cfg_node_ptr++; // ptr offset by size_per_container
         prop_node_ptr = (uint8_t *)cont_cfg_node_ptr;
         for (prop_arr_indx = 0; prop_arr_indx < num_cont_prop; prop_arr_indx++)
         {
            cont_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            offset             = sizeof(apm_prop_data_t) + cont_prop_node_ptr->prop_size;
            if (!((APM_CONTAINER_PROP_ID_PROC_DOMAIN == cont_prop_node_ptr->prop_id)))
            {
               size_per_container += offset;
            }
            else
            {
               if (APM_CONTAINER_PROP_ID_PROC_DOMAIN == cont_prop_node_ptr->prop_id)
               {
                  if (cont_prop_node_ptr->prop_size >= sizeof(apm_cont_prop_id_proc_domain_t))
                  {
                     apm_cont_prop_id_proc_domain_t *proc_domain_ptr =
                        (apm_cont_prop_id_proc_domain_t *)(cont_prop_node_ptr + 1);

                     if (APM_PROC_DOMAIN_ID_INVALID == sat_pd)
                     {
                        sat_pd = proc_domain_ptr->proc_domain;
                     }
                     else
                     {
                        if (sat_pd != proc_domain_ptr->proc_domain)
                        {
                           OLC_SPGM_MSG(sgm_id_ptr->log_id,
                                        DBG_ERROR_PRIO,
                                        "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX] "
                                        "processing graph open command "
                                        "Invalid proc id in the sat cont config "
                                        "sat cont id[0x%lX] expected pd[0x%lX] configured pd[0x%lX]",
                                        sgm_id_ptr->cont_id,
                                        sgm_id_ptr->sat_pd,
                                        cont_cfg_node_ptr->container_id,
                                        sat_pd,
                                        proc_domain_ptr->proc_domain);
                           return AR_EBADPARAM; // todo : add a message for debug : done // add message for critical
                                                // failures
                        }
                     }
                  }
               }
            }
            prop_node_ptr += offset;
         }
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get container_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);
         return AR_EBADPARAM;
      }
      payload_size += size_per_container;
      size_per_container = 0;
   }

   *container_payload_size_ptr = ALIGN_8_BYTES(payload_size);
   *get_sat_pd_ptr             = sat_pd;

   return result;
}

ar_result_t sgm_open_get_module_list_payload_size(apm_modules_list_t **module_cfg_list_pptr,
                                                  uint32_t             num_module_list,
                                                  uint32_t *           module_list_payload_size_ptr,
                                                  spgm_id_info_t *     sgm_id_ptr)
{
   ar_result_t result               = AR_EOK;
   uint32_t    payload_size         = 0;
   uint32_t    size_per_module_list = 0;
   uint32_t    arr_indx             = 0;
   uint32_t    num_modules          = 0;

   apm_modules_list_t *module_list_cfg_node_ptr = NULL;

   *module_list_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the module list configuration.
   payload_size += sizeof(apm_param_id_modules_list_t);

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_list; arr_indx++)
   {
      module_list_cfg_node_ptr = module_cfg_list_pptr[arr_indx];
      if (module_list_cfg_node_ptr)
      {
         size_per_module_list = sizeof(apm_modules_list_t);
         num_modules          = module_list_cfg_node_ptr->num_modules;
         size_per_module_list += (num_modules * sizeof(apm_module_cfg_t));
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get module_list_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);
         return AR_EBADPARAM;
      }
      payload_size += size_per_module_list;
      size_per_module_list = 0;
   }

   *module_list_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_module_prop_payload_size(apm_module_prop_cfg_t **module_prop_list_pptr,
                                                  uint32_t                num_module_prop_list,
                                                  uint32_t *              module_prop_payload_size_ptr,
                                                  spgm_id_info_t *        sgm_id_ptr)
{
   ar_result_t result                    = AR_EOK;
   uint32_t    payload_size              = 0;
   uint32_t    size_per_module_prop_list = 0;
   uint32_t    arr_indx                  = 0;
   uint32_t    prop_arr_indx             = 0;
   uint32_t    offset                    = 0;
   uint32_t    num_module_prop           = 0;

   apm_module_prop_cfg_t *module_prop_cfg_node_ptr = NULL;
   apm_prop_data_t *      module_prop_node_ptr     = NULL;
   uint8_t *              prop_node_ptr            = NULL;

   *module_prop_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_module_prop_t);

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_prop_list; arr_indx++)
   {
      module_prop_cfg_node_ptr = module_prop_list_pptr[arr_indx];
      if (module_prop_cfg_node_ptr)
      {
         size_per_module_prop_list = sizeof(apm_module_prop_cfg_t);
         num_module_prop           = module_prop_cfg_node_ptr->num_props;
         module_prop_cfg_node_ptr++;
         prop_node_ptr = (uint8_t *)module_prop_cfg_node_ptr;
         for (prop_arr_indx = 0; prop_arr_indx < num_module_prop; prop_arr_indx++)
         {
            module_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            offset               = sizeof(apm_prop_data_t) + module_prop_node_ptr->prop_size;
            size_per_module_prop_list += offset;
            prop_node_ptr += offset;
         }
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get module_property_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);
         return AR_EBADPARAM;
      }
      payload_size += size_per_module_prop_list;
      size_per_module_prop_list = 0;
   }

   *module_prop_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_module_conn_payload_size(apm_module_conn_cfg_t **module_conn_list_pptr,
                                                  uint32_t                num_module_conn_list,
                                                  uint32_t *              module_conn_payload_size_ptr,
                                                  spgm_id_info_t *        sgm_id_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    payload_size       = 0;
   uint32_t    size_per_conn_list = 0;
   uint32_t    arr_indx           = 0;

   apm_module_conn_cfg_t *module_conn_cfg_node_ptr = NULL;

   *module_conn_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_module_conn_t);

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_conn_list; arr_indx++)
   {
      module_conn_cfg_node_ptr = module_conn_list_pptr[arr_indx];
      if (module_conn_cfg_node_ptr)
      {
         size_per_conn_list += sizeof(apm_module_conn_cfg_t);
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get module_connection_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);
         return AR_EBADPARAM;
      }
      payload_size += size_per_conn_list;
      size_per_conn_list = 0;
   }

   *module_conn_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_imcl_peer_info_payload_size(apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
                                                     uint32_t                      num_offloaded_peers,
                                                     uint32_t *                    imcl_peer_cfg_payload_size_ptr,
                                                     spgm_id_info_t *              sgm_id_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    payload_size = 0;

   *imcl_peer_cfg_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the offload IMCL Peers.
   payload_size += sizeof(apm_param_id_imcl_peer_domain_info_t);

   // size of the payload .
   payload_size += num_offloaded_peers * sizeof(apm_imcl_peer_domain_info_t);

   *imcl_peer_cfg_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_ctrl_link_cfg_payload_size(apm_module_ctrl_link_cfg_t **ctrl_link_cfg_list_pptr,
                                                    uint32_t                     num_ctrl_link_cfg_list,
                                                    uint32_t *                   ctrl_link_cfg_payload_size_ptr,
                                                    spgm_id_info_t *             sgm_id_ptr)
{
   ar_result_t result                 = AR_EOK;
   uint32_t    payload_size           = 0;
   uint32_t    size_per_ctrl_link_cfg = 0;
   uint32_t    arr_indx               = 0;
   uint32_t    prop_arr_indx          = 0;
   uint32_t    offset                 = 0;
   uint32_t    num_ctrl_link_prop     = 0;

   apm_module_ctrl_link_cfg_t *ctrl_link_cfg_node_ptr = NULL;
   uint8_t *                   prop_node_ptr          = NULL;

   *ctrl_link_cfg_payload_size_ptr = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_module_ctrl_link_cfg_t);

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_ctrl_link_cfg_list; arr_indx++)
   {
      ctrl_link_cfg_node_ptr = ctrl_link_cfg_list_pptr[arr_indx];
      if (ctrl_link_cfg_node_ptr)
      {
         num_ctrl_link_prop     = ctrl_link_cfg_node_ptr->num_props;
         prop_node_ptr          = (uint8_t *)(ctrl_link_cfg_node_ptr + 1);
         size_per_ctrl_link_cfg = sizeof(apm_module_ctrl_link_cfg_t);

         for (prop_arr_indx = 0; prop_arr_indx < num_ctrl_link_prop; prop_arr_indx++)
         {
            apm_prop_data_t *prop_ptr = (apm_prop_data_t *)prop_node_ptr;
            offset                    = sizeof(apm_prop_data_t);
            offset += prop_ptr->prop_size;

            size_per_ctrl_link_cfg += offset;
            prop_node_ptr += offset;
         }
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "Failed to get module_cntrl_link_connection_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);

         return AR_EBADPARAM;
      }
      payload_size += size_per_ctrl_link_cfg;
      size_per_ctrl_link_cfg = 0;
   }

   *ctrl_link_cfg_payload_size_ptr = ALIGN_8_BYTES(payload_size);

   return result;
}

ar_result_t sgm_open_get_param_data_payload_size(void **         param_data_cfg_list_pptr,
                                                 uint32_t        num_param_id_cfg,
                                                 uint32_t *      param_data_payload_size_ptr,
                                                 spgm_id_info_t *sgm_id_ptr)
{
   ar_result_t result                  = AR_EOK;
   uint32_t    payload_size            = 0;
   uint32_t    size_per_param_data_cfg = 0;
   uint32_t    arr_indx                = 0;

   apm_module_param_data_t *param_data_ptr = NULL;
   *param_data_payload_size_ptr            = 0;

   for (arr_indx = 0; arr_indx < num_param_id_cfg; arr_indx++)
   {
      param_data_ptr = (apm_module_param_data_t *)param_data_cfg_list_pptr[arr_indx];
      if (param_data_ptr)
      {
         size_per_param_data_cfg = sizeof(apm_module_param_data_t);
         size_per_param_data_cfg += ALIGN_8_BYTES(param_data_ptr->param_size);
      }
      else
      {
         OLC_SPGM_MSG(sgm_id_ptr->log_id,
                      DBG_ERROR_PRIO,
                      "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                      "processing graph open command"
                      "FFailed to get module_param data_payload_size "
                      "to create the satellite_graph_open_msg",
                      sgm_id_ptr->cont_id,
                      sgm_id_ptr->sat_pd);

         return AR_EBADPARAM;
      }
      payload_size += ALIGN_8_BYTES(size_per_param_data_cfg);
   }

   *param_data_payload_size_ptr = ALIGN_8_BYTES(payload_size);
   return result;
}

ar_result_t sgm_open_fill_sub_graph_cfg(apm_sub_graph_cfg_t **sg_cfg_list_pptr,
                                        uint32_t              num_sub_graphs,
                                        uint8_t *             payload_ptr,
                                        uint32_t *            sub_graph_payload_size_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    payload_size       = 0;
   uint32_t    arr_indx           = 0;
   uint32_t    prop_arr_indx      = 0;
   uint32_t    offset             = 0;
   uint32_t    num_sub_graph_prop = 0;
   uint32_t    fill_size          = 0;

   uint8_t *            prop_node_ptr          = NULL;
   apm_sub_graph_cfg_t *sub_graph_cfg_node_ptr = NULL;
   apm_prop_data_t *    sg_prop_node_ptr       = NULL;

   apm_module_param_data_t      sg_mp_data;
   apm_param_id_sub_graph_cfg_t sg_pid_data;

   *sub_graph_payload_size_ptr = 0;

   sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_CONFIG;
   sg_mp_data.param_size         = 0;
   sg_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   sg_pid_data.num_sub_graphs = num_sub_graphs;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_sub_graph_cfg_t),
                       &sg_pid_data,
                       sizeof(apm_param_id_sub_graph_cfg_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_sub_graphs; arr_indx++)
   {
      sub_graph_cfg_node_ptr = sg_cfg_list_pptr[arr_indx];
      if (sub_graph_cfg_node_ptr)
      {

         fill_size = memscpy(payload_ptr + offset,
                             sizeof(apm_sub_graph_cfg_t),
                             sub_graph_cfg_node_ptr,
                             sizeof(apm_sub_graph_cfg_t));

         offset += fill_size;

         num_sub_graph_prop = sub_graph_cfg_node_ptr->num_sub_graph_prop;
         sub_graph_cfg_node_ptr++; // ptr offset by size_per_sub_graph
         prop_node_ptr = (uint8_t *)sub_graph_cfg_node_ptr;
         for (prop_arr_indx = 0; prop_arr_indx < num_sub_graph_prop; prop_arr_indx++)
         {
            sg_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            payload_size     = sizeof(apm_prop_data_t) + sg_prop_node_ptr->prop_size;

            fill_size = memscpy(payload_ptr + offset, payload_size, sg_prop_node_ptr, payload_size);

            if (APM_SUB_GRAPH_PROP_ID_SCENARIO_ID == sg_prop_node_ptr->prop_id)
            {
               apm_sg_prop_id_scenario_id_t *scenario_id_ptr =
                  (apm_sg_prop_id_scenario_id_t *)(payload_ptr + offset + sizeof(apm_prop_data_t));
               if (APM_SUB_GRAPH_SID_VOICE_CALL == scenario_id_ptr->scenario_id)
               {
                  scenario_id_ptr->scenario_id = APM_SUB_GRAPH_SID_SAT_VOICE_CALL;
               }
            }
            offset += fill_size;
            prop_node_ptr += payload_size;
         }
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *sub_graph_payload_size_ptr = ALIGN_8_BYTES(offset);

   // temp code
   sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_CONFIG;
   sg_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   sg_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_container_cfg(apm_container_cfg_t **cont_cfg_list_pptr,
                                        uint32_t              num_container,
                                        uint8_t *             payload_ptr,
                                        uint32_t *            container_payload_size_ptr)
{
   ar_result_t result        = AR_EOK;
   uint32_t    payload_size  = 0;
   uint32_t    arr_indx      = 0;
   uint32_t    prop_arr_indx = 0;
   uint32_t    offset        = 0;
   uint32_t    num_cont_prop = 0;
   uint32_t    fill_size     = 0;

   apm_container_cfg_t *cont_cfg_node_ptr  = NULL;
   apm_prop_data_t *    cont_prop_node_ptr = NULL;
   uint8_t *            prop_node_ptr      = NULL;

   apm_module_param_data_t      cont_mp_data;
   apm_param_id_container_cfg_t cont_pid_data;

   *container_payload_size_ptr = 0;

   cont_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   cont_mp_data.param_id           = APM_PARAM_ID_CONTAINER_CONFIG;
   cont_mp_data.param_size         = 0;
   cont_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &cont_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   cont_pid_data.num_container = num_container;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_container_cfg_t),
                       &cont_pid_data,
                       sizeof(apm_param_id_container_cfg_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_container; arr_indx++)
   {
      cont_cfg_node_ptr = cont_cfg_list_pptr[arr_indx];
      if (cont_cfg_node_ptr)
      {
         apm_container_cfg_t cont_cfg;
         uint8_t *           cont_cfg_payload_ptr = payload_ptr + offset;
         uint32_t            num_prop_updated     = 0;

         fill_size = memscpy(&cont_cfg, sizeof(apm_container_cfg_t), cont_cfg_node_ptr, sizeof(apm_container_cfg_t));
         offset += fill_size;

         num_cont_prop = cont_cfg_node_ptr->num_prop;
         cont_cfg_node_ptr++; // ptr offset by size_per_container
         prop_node_ptr = (uint8_t *)cont_cfg_node_ptr;

         for (prop_arr_indx = 0; prop_arr_indx < num_cont_prop; prop_arr_indx++)
         {
            cont_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            payload_size       = sizeof(apm_prop_data_t) + cont_prop_node_ptr->prop_size;
            if (!((APM_CONTAINER_PROP_ID_PROC_DOMAIN == cont_prop_node_ptr->prop_id)))
            {
               fill_size = memscpy(payload_ptr + offset, payload_size, cont_prop_node_ptr, payload_size);
               offset += fill_size;
               num_prop_updated++;
            }
            prop_node_ptr += payload_size;
         }
         cont_cfg.num_prop = num_prop_updated;
         fill_size = memscpy(cont_cfg_payload_ptr, sizeof(apm_container_cfg_t), &cont_cfg, sizeof(apm_container_cfg_t));
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *container_payload_size_ptr = ALIGN_8_BYTES(offset);

   cont_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   cont_mp_data.param_id           = APM_PARAM_ID_CONTAINER_CONFIG;
   cont_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   cont_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &cont_mp_data, sizeof(apm_module_param_data_t));
   return result;
}

ar_result_t sgm_open_fill_module_list_cfg(apm_modules_list_t **module_cfg_list_pptr,
                                          uint32_t             num_module_list,
                                          uint8_t *            payload_ptr,
                                          uint32_t *           module_list_payload_size_ptr)
{
   ar_result_t result               = AR_EOK;
   uint32_t    size_per_module_list = 0;
   uint32_t    arr_indx             = 0;
   uint32_t    num_modules          = 0;
   uint32_t    offset               = 0;
   uint32_t    fill_size            = 0;

   apm_modules_list_t *        module_list_cfg_node_ptr = NULL;
   apm_module_param_data_t     ml_mp_data;
   apm_param_id_modules_list_t ml_pid_data;

   *module_list_payload_size_ptr = 0;

   ml_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   ml_mp_data.param_id           = APM_PARAM_ID_MODULES_LIST;
   ml_mp_data.param_size         = 0;
   ml_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &ml_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   ml_pid_data.num_modules_list = num_module_list;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_modules_list_t),
                       &ml_pid_data,
                       sizeof(apm_param_id_modules_list_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_list; arr_indx++)
   {
      module_list_cfg_node_ptr = module_cfg_list_pptr[arr_indx];
      if (module_list_cfg_node_ptr)
      {
         size_per_module_list = sizeof(apm_modules_list_t);
         num_modules          = module_list_cfg_node_ptr->num_modules;
         size_per_module_list += (num_modules * sizeof(apm_module_cfg_t));

         fill_size =
            memscpy(payload_ptr + offset, size_per_module_list, module_list_cfg_node_ptr, size_per_module_list);
         offset += fill_size;
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
      size_per_module_list = 0;
   }

   *module_list_payload_size_ptr = ALIGN_8_BYTES(offset);

   ml_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   ml_mp_data.param_id           = APM_PARAM_ID_MODULES_LIST;
   ml_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   ml_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &ml_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_module_prop_cfg(apm_module_prop_cfg_t **module_prop_list_pptr,
                                          uint32_t                num_module_prop_list,
                                          uint8_t *               payload_ptr,
                                          uint32_t *              module_prop_payload_size_ptr)
{
   ar_result_t result          = AR_EOK;
   uint32_t    payload_size    = 0;
   uint32_t    arr_indx        = 0;
   uint32_t    prop_arr_indx   = 0;
   uint32_t    offset          = 0;
   uint32_t    num_module_prop = 0;
   uint32_t    fill_size       = 0;

   apm_module_prop_cfg_t *    module_prop_cfg_node_ptr = NULL;
   apm_prop_data_t *          module_prop_node_ptr     = NULL;
   uint8_t *                  prop_node_ptr            = NULL;
   apm_module_param_data_t    mod_prop_mp_data;
   apm_param_id_module_prop_t mod_prop_pid_data;

   *module_prop_payload_size_ptr = 0;

   mod_prop_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   mod_prop_mp_data.param_id           = APM_PARAM_ID_MODULE_PROP;
   mod_prop_mp_data.param_size         = 0;
   mod_prop_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_module_param_data_t),
                       &mod_prop_mp_data,
                       sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   mod_prop_pid_data.num_module_prop_cfg = num_module_prop_list;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_modules_list_t),
                       &mod_prop_pid_data,
                       sizeof(apm_param_id_modules_list_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_prop_list; arr_indx++)
   {
      module_prop_cfg_node_ptr = module_prop_list_pptr[arr_indx];
      if (module_prop_cfg_node_ptr)
      {
         fill_size = memscpy(payload_ptr + offset,
                             sizeof(apm_module_prop_cfg_t),
                             module_prop_cfg_node_ptr,
                             sizeof(apm_module_prop_cfg_t));
         offset += fill_size;

         num_module_prop = module_prop_cfg_node_ptr->num_props;
         module_prop_cfg_node_ptr++;

         prop_node_ptr = (uint8_t *)module_prop_cfg_node_ptr;
         for (prop_arr_indx = 0; prop_arr_indx < num_module_prop; prop_arr_indx++)
         {
            module_prop_node_ptr = (apm_prop_data_t *)prop_node_ptr;
            payload_size         = sizeof(apm_prop_data_t) + module_prop_node_ptr->prop_size;

            fill_size = memscpy(payload_ptr + offset, payload_size, module_prop_node_ptr, payload_size);
            offset += fill_size;
            prop_node_ptr += payload_size;
         }
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *module_prop_payload_size_ptr = ALIGN_8_BYTES(offset);

   mod_prop_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   mod_prop_mp_data.param_id           = APM_PARAM_ID_MODULE_PROP;
   mod_prop_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   mod_prop_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr, sizeof(apm_module_param_data_t), &mod_prop_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_module_conn_cfg(apm_module_conn_cfg_t **module_conn_list_pptr,
                                          uint32_t                num_module_conn_list,
                                          uint8_t *               payload_ptr,
                                          uint32_t *              module_conn_payload_size_ptr)
{
   ar_result_t result    = AR_EOK;
   uint32_t    arr_indx  = 0;
   uint32_t    fill_size = 0;
   uint32_t    offset    = 0;

   apm_module_conn_cfg_t *    module_conn_cfg_node_ptr = NULL;
   apm_module_param_data_t    mod_conn_mp_data;
   apm_param_id_module_conn_t mod_conn_pid_data;

   *module_conn_payload_size_ptr = 0;

   mod_conn_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   mod_conn_mp_data.param_id           = APM_PARAM_ID_MODULE_CONN;
   mod_conn_mp_data.param_size         = 0;
   mod_conn_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_module_param_data_t),
                       &mod_conn_mp_data,
                       sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   mod_conn_pid_data.num_connections = num_module_conn_list;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_module_conn_t),
                       &mod_conn_pid_data,
                       sizeof(apm_param_id_module_conn_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_module_conn_list; arr_indx++)
   {
      module_conn_cfg_node_ptr = module_conn_list_pptr[arr_indx];
      if (module_conn_cfg_node_ptr)
      {
         fill_size = memscpy(payload_ptr + offset,
                             sizeof(apm_module_conn_cfg_t),
                             module_conn_cfg_node_ptr,
                             sizeof(apm_module_conn_cfg_t));
         offset += fill_size;
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *module_conn_payload_size_ptr = ALIGN_8_BYTES(offset);

   mod_conn_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   mod_conn_mp_data.param_id           = APM_PARAM_ID_MODULE_CONN;
   mod_conn_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   mod_conn_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr, sizeof(apm_module_param_data_t), &mod_conn_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_imcl_peer_cfg(apm_imcl_peer_domain_info_t **imcl_peer_domain_info_pptr,
                                        uint32_t                      num_offloaded_peers,
                                        uint8_t *                     payload_ptr,
                                        uint32_t *                    imcl_peer_cfg_payload_size_ptr)
{
   ar_result_t result    = AR_EOK;
   uint32_t    arr_indx  = 0;
   uint32_t    offset    = 0;
   uint32_t    fill_size = 0;

   apm_imcl_peer_domain_info_t *        imcl_peer_cfg_node_ptr = NULL;
   apm_module_param_data_t              imcl_peer_mp_data      = { 0 };
   apm_param_id_imcl_peer_domain_info_t imcl_peer_pid_data     = { 0 };

   *imcl_peer_cfg_payload_size_ptr = 0;

   imcl_peer_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   imcl_peer_mp_data.param_id           = APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO;
   imcl_peer_mp_data.param_size         = 0;
   imcl_peer_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_module_param_data_t),
                       &imcl_peer_mp_data,
                       sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   imcl_peer_pid_data.num_imcl_peer_cfg = num_offloaded_peers;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_imcl_peer_domain_info_t),
                       &imcl_peer_pid_data,
                       sizeof(apm_param_id_imcl_peer_domain_info_t));

   offset += fill_size;

   for (arr_indx = 0; arr_indx < num_offloaded_peers; arr_indx++)
   {
      imcl_peer_cfg_node_ptr = imcl_peer_domain_info_pptr[arr_indx];
      if (imcl_peer_cfg_node_ptr)
      {
         fill_size = memscpy(payload_ptr + offset,
                             sizeof(apm_imcl_peer_domain_info_t),
                             imcl_peer_cfg_node_ptr,
                             sizeof(apm_imcl_peer_domain_info_t));
         offset += fill_size;
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *imcl_peer_cfg_payload_size_ptr = ALIGN_8_BYTES(offset);

   imcl_peer_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   imcl_peer_mp_data.param_id           = APM_PARAM_ID_IMCL_PEER_DOMAIN_INFO;
   imcl_peer_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   imcl_peer_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr, sizeof(apm_module_param_data_t), &imcl_peer_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_ctrl_link_cfg(apm_module_ctrl_link_cfg_t **ctrl_link_cfg_list_pptr,
                                        uint32_t                     num_ctrl_link_cfg_list,
                                        uint8_t *                    payload_ptr,
                                        uint32_t *                   ctrl_link_cfg_payload_size_ptr)
{
   ar_result_t result             = AR_EOK;
   uint32_t    payload_size       = 0;
   uint32_t    arr_indx           = 0;
   uint32_t    prop_arr_indx      = 0;
   uint32_t    offset             = 0;
   uint32_t    num_ctrl_link_prop = 0;
   uint32_t    fill_size          = 0;

   apm_module_ctrl_link_cfg_t *        ctrl_link_cfg_node_ptr = NULL;
   uint8_t *                           ctrl_link_prop_ptr     = NULL;
   uint8_t *                           prop_node_ptr          = NULL;
   apm_module_param_data_t             cl_mp_data;
   apm_param_id_module_ctrl_link_cfg_t cl_pid_data;

   *ctrl_link_cfg_payload_size_ptr = 0;

   cl_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   cl_mp_data.param_id           = APM_PARAM_ID_MODULE_CTRL_LINK_CFG;
   cl_mp_data.param_size         = 0;
   cl_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &cl_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;

   cl_pid_data.num_ctrl_link_cfg = num_ctrl_link_cfg_list;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_module_ctrl_link_cfg_t),
                       &cl_pid_data,
                       sizeof(apm_param_id_module_ctrl_link_cfg_t));

   offset += fill_size;

   // variable payload size for each of the sub graph
   for (arr_indx = 0; arr_indx < num_ctrl_link_cfg_list; arr_indx++)
   {
      ctrl_link_cfg_node_ptr = ctrl_link_cfg_list_pptr[arr_indx];
      if (ctrl_link_cfg_node_ptr)
      {
         num_ctrl_link_prop = ctrl_link_cfg_node_ptr->num_props;

         fill_size = memscpy(payload_ptr + offset,
                             sizeof(apm_module_ctrl_link_cfg_t),
                             ctrl_link_cfg_node_ptr,
                             sizeof(apm_module_ctrl_link_cfg_t));
         offset += fill_size;

         prop_node_ptr = (uint8_t *)(ctrl_link_cfg_node_ptr + 1);

         for (prop_arr_indx = 0; prop_arr_indx < num_ctrl_link_prop; prop_arr_indx++)
         {
            apm_prop_data_t *prop_ptr = (apm_prop_data_t *)prop_node_ptr;
            payload_size              = sizeof(apm_prop_data_t);

            fill_size = memscpy(payload_ptr + offset, payload_size, prop_ptr, payload_size);
            offset += fill_size;

            ctrl_link_prop_ptr = (uint8_t *)(prop_ptr + 1);

            fill_size = memscpy(payload_ptr + offset, prop_ptr->prop_size, ctrl_link_prop_ptr, prop_ptr->prop_size);
            offset += fill_size;
            payload_size += prop_ptr->prop_size;
            prop_node_ptr += payload_size;
         }
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *ctrl_link_cfg_payload_size_ptr = ALIGN_8_BYTES(offset);

   cl_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   cl_mp_data.param_id           = APM_PARAM_ID_MODULE_CTRL_LINK_CFG;
   cl_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   cl_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &cl_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_open_fill_param_data_cfg(spgm_info_t *spgm_ptr,
                                         void **      param_data_cfg_list_pptr,
                                         uint32_t     num_param_id_cfg,
                                         uint8_t *    payload_ptr,
                                         uint32_t *   param_data_payload_size_ptr)
{
   ar_result_t              result                  = AR_EOK;
   uint32_t                 size_per_param_data_cfg = 0;
   uint32_t                 arr_indx                = 0;
   uint32_t                 offset                  = 0;
   uint32_t                 fill_size               = 0;
   apm_module_param_data_t *param_data_ptr          = NULL;

   for (arr_indx = 0; arr_indx < num_param_id_cfg; arr_indx++)
   {
      param_data_ptr = (apm_module_param_data_t *)param_data_cfg_list_pptr[arr_indx];
      if (param_data_ptr)
      {
         size_per_param_data_cfg = sizeof(apm_module_param_data_t);
         size_per_param_data_cfg += ALIGN_8_BYTES(param_data_ptr->param_size);
         OLC_SGM_MSG(OLC_SGM_ID,
                     DBG_MED_PRIO,
                     "fill_param_data_cfg: payload size %lu, p_index %lu",
                     size_per_param_data_cfg,
                     arr_indx);

         fill_size = memscpy(payload_ptr + offset, size_per_param_data_cfg, param_data_ptr, size_per_param_data_cfg);
         offset += ALIGN_8_BYTES(fill_size);
      }
      else
      {
         // Error case; add message
         return AR_EBADPARAM;
      }
   }

   *param_data_payload_size_ptr = ALIGN_8_BYTES(offset);
   return result;
}

ar_result_t sgm_get_graph_mgmt_client_payload_size(spgm_info_t *             spgm_ptr,
                                                   spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                                                   uint32_t *                graph_mgmt_cmd_payload_size_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    payload_size = 0;
   uint32_t    temp_size    = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_sub_graph_cfg_t);

   /** Populate all the object counters */
   uint32_t num_sub_graphs = gmc_apm_gmgmt_cmd_ptr->sg_id_list.num_sub_graph;

   /* get the size of the sub graph payload size*/
   temp_size = num_sub_graphs * sizeof(apm_sub_graph_id_t); // todo : take uint32_t // updated
   payload_size += temp_size;
   temp_size = 0;

   *graph_mgmt_cmd_payload_size_ptr = ALIGN_8_BYTES(payload_size);
   return result;
}

ar_result_t sgm_get_graph_close_client_payload_size(spgm_info_t *             spgm_ptr,
                                                    spf_msg_cmd_graph_mgmt_t *gmc_apm_close_cmd_ptr,
                                                    uint32_t *                graph_close_cmd_payload_size_ptr)
{
   ar_result_t result         = AR_EOK;
   uint32_t    payload_size   = 0;
   uint32_t    temp_size      = 0;
   uint32_t    num_sub_graphs = 0;
   uint32_t    num_data_conn  = 0;
   uint32_t    num_ctrl_conn  = 0;

   // size of the module parameter data structure
   payload_size += sizeof(apm_module_param_data_t);

   // size of the parameter ID to configure the sub-graph properties.
   payload_size += sizeof(apm_param_id_sub_graph_cfg_t);

   /** Populate all the object counters */
   num_sub_graphs = gmc_apm_close_cmd_ptr->sg_id_list.num_sub_graph;
   num_data_conn  = gmc_apm_close_cmd_ptr->cntr_port_hdl_list.num_data_links;
   num_ctrl_conn  = gmc_apm_close_cmd_ptr->cntr_port_hdl_list.num_ctrl_links;

   /* get the size of the sub graph payload size*/
   temp_size = num_sub_graphs * sizeof(gmc_apm_close_cmd_ptr->sg_id_list.sg_id_list_ptr); // todo : take uint32_t
   payload_size += temp_size;
   temp_size = 0;

   if (num_data_conn > 0)
   {
      /* get the size of the module connection configuration payload size*/
      if (AR_EOK !=
          (result = sgm_open_get_module_conn_payload_size(gmc_apm_close_cmd_ptr->cntr_port_hdl_list.data_link_list_pptr,
                                                          num_data_conn,
                                                          &temp_size,
                                                          &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_ctrl_conn > 0)
   {
      /* get the size of the module control link configuration payload size*/
      if (AR_EOK !=
          (result =
              sgm_open_get_ctrl_link_cfg_payload_size(gmc_apm_close_cmd_ptr->cntr_port_hdl_list.ctrl_link_list_pptr,
                                                      num_ctrl_conn,
                                                      &temp_size,
                                                      &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   *graph_close_cmd_payload_size_ptr = ALIGN_8_BYTES(payload_size);
   return result;
}

ar_result_t sgm_gmgmt_fill_sub_graph_list_info(uint32_t *sg_id_list_info,
                                               uint32_t  num_sub_graphs,
                                               uint8_t * payload_ptr,
                                               uint32_t *sub_graph_payload_size_ptr)
{
   ar_result_t result                   = AR_EOK;
   uint32_t    arr_indx                 = 0;
   uint32_t    offset                   = 0;
   *sub_graph_payload_size_ptr          = 0;
   apm_sub_graph_id_t *sub_graph_id_ptr = NULL;

   for (arr_indx = 0; arr_indx < num_sub_graphs; arr_indx++)
   {
      sub_graph_id_ptr               = (apm_sub_graph_id_t *)(payload_ptr + offset);
      sub_graph_id_ptr->sub_graph_id = sg_id_list_info[arr_indx];

      offset += sizeof(apm_sub_graph_id_t);
   }

   *sub_graph_payload_size_ptr = ALIGN_8_BYTES(offset);

   return result;
}

ar_result_t sgm_create_graph_mgmt_command_client_payload(spgm_info_t *             spgm_ptr,
                                                         spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr,
                                                         uint8_t *                 client_graph_mgmt_payload_ptr,
                                                         uint32_t                  graph_mgmt_cmd_payload_size)
{
   ar_result_t result      = AR_EOK;
   uint32_t    offset      = 0;
   uint8_t *   payload_ptr = client_graph_mgmt_payload_ptr;
   uint32_t    fill_size   = 0;

   apm_module_param_data_t       sg_mp_data;
   apm_param_id_sub_graph_list_t sg_pid_data;

   /** Populate all the object counters */
   uint32_t num_sub_graphs = gmc_apm_gmgmt_cmd_ptr->sg_id_list.num_sub_graph;

   sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_LIST;
   sg_mp_data.param_size         = 0;
   sg_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;
   fill_size = 0;

   sg_pid_data.num_sub_graphs = num_sub_graphs;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_sub_graph_list_t),
                       &sg_pid_data,
                       sizeof(apm_param_id_sub_graph_list_t));

   offset += fill_size;
   fill_size = 0;

   /* update the sub graph information to the payload*/
   result = sgm_gmgmt_fill_sub_graph_list_info(gmc_apm_gmgmt_cmd_ptr->sg_id_list.sg_id_list_ptr,
                                               num_sub_graphs,
                                               payload_ptr + offset,
                                               &fill_size);
   offset += fill_size;
   fill_size = 0;

   // temp code
   sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_LIST;
   sg_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
   sg_mp_data.error_code         = AR_EOK;

   fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));

   return result;
}

ar_result_t sgm_create_graph_close_command_client_payload(spgm_info_t *             spgm_ptr,
                                                          spf_msg_cmd_graph_mgmt_t *gmc_apm_close_cmd_ptr,
                                                          uint8_t *                 client_graph_mgmt_payload_ptr,
                                                          uint32_t                  graph_close_cmd_payload_size)
{
   ar_result_t result         = AR_EOK;
   uint32_t    offset         = 0;
   uint8_t *   payload_ptr    = client_graph_mgmt_payload_ptr;
   uint32_t    fill_size      = 0;
   uint32_t    num_sub_graphs = 0;
   uint32_t    num_data_conn  = 0;
   uint32_t    num_ctrl_conn  = 0;

   apm_module_param_data_t       sg_mp_data;
   apm_param_id_sub_graph_list_t sg_pid_data;

   /** Populate all the object counters */
   num_sub_graphs = gmc_apm_close_cmd_ptr->sg_id_list.num_sub_graph;
   num_data_conn  = gmc_apm_close_cmd_ptr->cntr_port_hdl_list.num_data_links;
   num_ctrl_conn  = gmc_apm_close_cmd_ptr->cntr_port_hdl_list.num_ctrl_links;

   sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
   sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_LIST;
   sg_mp_data.param_size         = 0;
   sg_mp_data.error_code         = AR_EOK;

   fill_size =
      memscpy(payload_ptr + offset, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));
   // size of the module parameter data structure
   offset += fill_size;
   fill_size = 0;

   sg_pid_data.num_sub_graphs = num_sub_graphs;

   fill_size = memscpy(payload_ptr + offset,
                       sizeof(apm_param_id_sub_graph_list_t),
                       &sg_pid_data,
                       sizeof(apm_param_id_sub_graph_list_t));

   offset += fill_size;
   fill_size = 0;

   if (0 < num_sub_graphs)
   {

      /* update the sub graph information to the payload*/
      result = sgm_gmgmt_fill_sub_graph_list_info(gmc_apm_close_cmd_ptr->sg_id_list.sg_id_list_ptr,
                                                  num_sub_graphs,
                                                  payload_ptr + offset,
                                                  &fill_size);
      offset += fill_size;
      fill_size = 0;

      // temp code
      sg_mp_data.module_instance_id = APM_MODULE_INSTANCE_ID;
      sg_mp_data.param_id           = APM_PARAM_ID_SUB_GRAPH_LIST;
      sg_mp_data.param_size         = offset - sizeof(apm_module_param_data_t);
      sg_mp_data.error_code         = AR_EOK;

      fill_size = memscpy(payload_ptr, sizeof(apm_module_param_data_t), &sg_mp_data, sizeof(apm_module_param_data_t));
   }

   if (num_data_conn > 0)
   {
      /* update the module connection information to the payload*/
      result = sgm_open_fill_module_conn_cfg(gmc_apm_close_cmd_ptr->cntr_port_hdl_list.data_link_list_pptr,
                                             num_data_conn,
                                             payload_ptr + offset,
                                             &fill_size);
      offset += fill_size;
      offset = 0;
   }

   if (num_ctrl_conn > 0)
   {
      /* update the control link information to the payload*/
      result = sgm_open_fill_ctrl_link_cfg(gmc_apm_close_cmd_ptr->cntr_port_hdl_list.ctrl_link_list_pptr,
                                           num_ctrl_conn,
                                           payload_ptr + offset,
                                           &fill_size);
      offset += fill_size;
      offset = 0;
   }

   return result;
}

ar_result_t sgm_get_graph_set_get_cfg_client_payload_size(spgm_info_t *                 spgm_ptr,
                                                          spf_msg_cmd_param_data_cfg_t *gmc_set_get_cfg_ptr,
                                                          uint32_t *                    get_apm_cmd_payload_size_ptr)

{
   ar_result_t result = AR_EOK;

   result = sgm_open_get_param_data_payload_size(gmc_set_get_cfg_ptr->param_data_pptr,
                                                 gmc_set_get_cfg_ptr->num_param_id_cfg,
                                                 get_apm_cmd_payload_size_ptr,
                                                 &spgm_ptr->sgm_id);

   return result;
}

ar_result_t sgm_graph_set_get_cfg_fill_client_payload(spgm_info_t *                 spgm_ptr,
                                                      spf_msg_cmd_param_data_cfg_t *gmc_apm_gmgmt_set_get_cfg_ptr,
                                                      uint8_t *                     client_set_get_cfg_payload_ptr,
                                                      uint32_t                      gm_set_get_cfg_payload_size)
{
   ar_result_t result       = AR_EOK;
   uint8_t *   payload_ptr  = client_set_get_cfg_payload_ptr;
   uint32_t    payload_size = 0;

   result = sgm_open_fill_param_data_cfg(spgm_ptr,
                                         gmc_apm_gmgmt_set_get_cfg_ptr->param_data_pptr,
                                         gmc_apm_gmgmt_set_get_cfg_ptr->num_param_id_cfg,
                                         payload_ptr,
                                         &payload_size);
   return result;
}
