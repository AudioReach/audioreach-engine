/**
 * \file sgm_cmd_handler_utils.c
 * \brief
 *     This file contains Satellite Graph Management utility functions for command handling.
 *  
 * 
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "sgm_i.h"
#include "sprm.h"
#include "sgm_cmd_parser_utils.h"
#include "offload_path_delay_api.h"

/* =======================================================================
Static Function Definitions
========================================================================== */

/* Utility function to get the command response status */
bool_t sgm_get_cmd_rsp_status(spgm_info_t *spgm_ptr, uint32_t opcode)
{
   bool_t wait_for_rsp = FALSE;
   if ((NULL != spgm_ptr->active_cmd_hndl_ptr) && (opcode == spgm_ptr->active_cmd_hndl_ptr->opcode))
   {
      wait_for_rsp = spgm_ptr->active_cmd_hndl_ptr->wait_for_rsp;
   }
   return wait_for_rsp;
}

/* Utility function to cache the command message in the command node */
ar_result_t sgm_cache_cmd_msg(spgm_info_t *spgm_ptr, uint32_t opcode, spf_msg_t *cmd_msg)
{
   ar_result_t result = AR_EOK;
   if ((NULL != spgm_ptr->active_cmd_hndl_ptr) && (opcode == spgm_ptr->active_cmd_hndl_ptr->opcode))
   {
      spgm_ptr->active_cmd_hndl_ptr->cmd_msg = *cmd_msg;
   }
   else
   {
      result = AR_EUNEXPECTED;
   }
   return result;
}

/* Utility function to get the active command handler given the token */
ar_result_t sgm_get_active_cmd_hndl(spgm_info_t *          spgm_ptr,
                                    uint32_t               opcode,
                                    uint32_t               token,
                                    spgm_cmd_hndl_node_t **cmd_hndl_node_ptr)
{
   ar_result_t result = AR_EOK;

   olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                         spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                         cmd_hndl_node_ptr,
                         token);
   return result;
}

/* Utility function to cache the command message in the command node */
ar_result_t sgm_get_cache_cmd_msg(spgm_info_t *spgm_ptr, uint32_t opcode, uint32_t token, spf_msg_t **cmd_msg)
{
   ar_result_t           result              = AR_EOK;
   spgm_cmd_hndl_node_t *active_cmd_hndl_ptr = NULL;
   int64_t               p_time_us           = 0;

   /*bool_t get_cmd_node = */
   bool_t has_active_handle = olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                                                    spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                                                    &active_cmd_hndl_ptr,
                                                    token);

   if (has_active_handle)
   {
      p_time_us               = (posal_timer_get_time() - active_cmd_hndl_ptr->ipc_cmd_sent_ts_us);
      spgm_ptr->p_cmd_exec_ts = (uint32_t)p_time_us;

      *cmd_msg = &active_cmd_hndl_ptr->cmd_msg;
   }
   return result;
}

/* Utility function to allocate the memory resources for sending the command from
 * Master PD to Satellite PD */
ar_result_t sgm_alloc_cmd_hndl_resources(spgm_info_t *spgm_ptr,
                                         uint32_t     graph_cmd_payload_size, // size of the command payload
                                         bool_t       is_inband,
                                         bool_t       is_persistent)
{
   ar_result_t result               = AR_EOK;
   uint32_t    gpr_msg_payload_size = 0;
   uint32_t    satellite_proc_id    = spgm_ptr->sgm_id.sat_pd;

   sgm_shmem_handle_t *shmem_node_ptr              = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr  = NULL;
   spgm_ptr->active_cmd_hndl_ptr->cmd_payload_size = 0;

   // payload size for inband command
   if (TRUE == is_inband)
   {
      gpr_msg_payload_size        = graph_cmd_payload_size + sizeof(apm_cmd_header_t);
      shmem_node_ptr->shm_mem_ptr = NULL;
   }
   else // payload size for out of band command
   {
      gpr_msg_payload_size = sizeof(apm_cmd_header_t);

      /** allocating the mdf shared memory for the satellite graph management command
       *  to send to the satellite DSP this payload is required to be in
       *  compliance with the APM API's */
      if (!is_persistent)
      {
         // no need to allocate if persistent.
         if (AR_EOK != (result = sgm_shmem_alloc(graph_cmd_payload_size, satellite_proc_id, shmem_node_ptr)))
         {
            OLC_SGM_MSG(OLC_SGM_ID,
                        DBG_ERROR_PRIO,
                        "Failed to allocate MDF shared "
                        "memory for the command opcode [0x%lX]",
                        spgm_ptr->active_cmd_hndl_ptr->opcode);
            return result;
         }
      }
      else
      {
         shmem_node_ptr->shm_mem_ptr = NULL;
      }
   }

   uint8_t *payload_ptr = (uint8_t *)posal_memory_malloc(gpr_msg_payload_size, spgm_ptr->cu_ptr->heap_id);
   if (NULL == payload_ptr)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate memory for gpr"
                  "payload for the command opcode [0x%lX]",
                  spgm_ptr->active_cmd_hndl_ptr->opcode);

      if ((!is_persistent) && (!is_inband))
      {
         sgm_shmem_free(shmem_node_ptr);
      }
      return AR_ENOMEMORY;
   }

   spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr  = payload_ptr;
   spgm_ptr->active_cmd_hndl_ptr->cmd_payload_size = gpr_msg_payload_size;
   return result;
}

/* function to free the memory resources allocated for sending the command
 * The resources would be freed in the response handling */
ar_result_t sgm_free_cmd_hndl_resources(spgm_cmd_hndl_node_t *active_cmd_hndl_ptr, spgm_id_info_t *sgm_id_ptr)
{
   ar_result_t result = AR_EOK;

   sgm_shmem_handle_t *shmem_node_ptr = &active_cmd_hndl_ptr->shm_info;

   // Free the Shared memory allocation for the command
   if (AR_EOK != (result = sgm_shmem_free(shmem_node_ptr)))
   {
      // OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "Failed to free mdf shared memory");
      // do not return from here
   }

   // Free the cached command payload ptr.
   if (NULL != active_cmd_hndl_ptr->cmd_payload_ptr)
   {
      posal_memory_free(active_cmd_hndl_ptr->cmd_payload_ptr);
      active_cmd_hndl_ptr->cmd_payload_ptr = NULL;
   }
   return result;
}

/*  function to create the command handle node
 *  the command handle is created during the start of the command processing
 *  and freed after the response handling for the corresponding command is completed*/
ar_result_t sgm_create_cmd_hndl_node(spgm_info_t *spgm_ptr)
{
   ar_result_t           result       = AR_EOK;
   spgm_cmd_hndl_node_t *cmd_hndl_ptr = NULL;

   cmd_hndl_ptr = (spgm_cmd_hndl_node_t *)posal_memory_malloc(sizeof(spgm_cmd_hndl_node_t), spgm_ptr->cu_ptr->heap_id);
   if (NULL == cmd_hndl_ptr)
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "Failed to allocate memory for the command handler");
      return AR_ENOMEMORY;
   }

   memset(cmd_hndl_ptr, 0, sizeof(spgm_cmd_hndl_node_t));
   spgm_ptr->active_cmd_hndl_ptr = cmd_hndl_ptr;
   return result;
}

void sgm_destroy_cmd_handle(spgm_info_t *spgm_ptr, uint32_t opcode, uint32_t token)
{
   spgm_cmd_hndl_node_t *active_cmd_hndl_ptr = NULL;

   /*bool_t get_cmd_node = */
   olc_get_cmd_hndl_node(spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                         spgm_ptr->cmd_hndl_list.num_active_cmd_hndls,
                         &active_cmd_hndl_ptr,
                         token);

   sgm_util_remove_node_from_list(spgm_ptr,
                                  &spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                                  active_cmd_hndl_ptr,
                                  &spgm_ptr->cmd_hndl_list.num_active_cmd_hndls);

   sgm_free_cmd_hndl_resources(active_cmd_hndl_ptr, &spgm_ptr->sgm_id);
   if (active_cmd_hndl_ptr->token == spgm_ptr->active_cmd_hndl_ptr->token)
   {
      posal_memory_free(spgm_ptr->active_cmd_hndl_ptr);
      spgm_ptr->active_cmd_hndl_ptr = NULL;
   }
   else
   {
      posal_memory_free(active_cmd_hndl_ptr);
   }
}

ar_result_t sgm_cmd_preprocessing(spgm_info_t *spgm_ptr, uint32_t cmd_opcode, bool_t inband)
{

   ar_result_t result = AR_EOK;
   // special handling for force close - it can come anytime
   if ((APM_CMD_GRAPH_CLOSE != cmd_opcode) && (NULL != spgm_ptr->active_cmd_hndl_ptr))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "rcvd cmd_opcode [0x%lX], while waiting for previous cmd rsp",
                  cmd_opcode);

      return AR_EUNEXPECTED;
   }
   else
   {
      OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "start processing cmd_opcode [0x%lX]", cmd_opcode);
   }

   if (AR_EOK != (result = sgm_create_cmd_hndl_node(spgm_ptr)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "Failed to allocate cmd handle node memory for cmd opcode[0x%lX]",
                  cmd_opcode);
      return result;
   }
   spgm_ptr->active_cmd_hndl_ptr->opcode    = cmd_opcode;
   spgm_ptr->active_cmd_hndl_ptr->is_inband = inband;

   return result;
}

void sgm_cmd_postprocessing(spgm_info_t *spgm_ptr)
{

   // ar_result_t result = AR_EOK;
   spgm_ptr->active_cmd_hndl_ptr->wait_for_rsp = TRUE;

   sgm_util_add_node_to_list(spgm_ptr,
                             &spgm_ptr->cmd_hndl_list.cmd_hndl_list_ptr,
                             spgm_ptr->active_cmd_hndl_ptr,
                             &spgm_ptr->cmd_hndl_list.num_active_cmd_hndls);

   OLC_SGM_MSG(OLC_SGM_ID,
               DBG_HIGH_PRIO,
               "completed processing of command opcode [0x%lX]. wait for response",
               spgm_ptr->active_cmd_hndl_ptr->opcode);
}

void sgm_cmd_handling_bail_out(spgm_info_t *spgm_ptr)
{
   (void)sgm_free_cmd_hndl_resources(spgm_ptr->active_cmd_hndl_ptr, &spgm_ptr->sgm_id);
   if (NULL != spgm_ptr->active_cmd_hndl_ptr)
   {
      posal_memory_free(spgm_ptr->active_cmd_hndl_ptr);
      spgm_ptr->active_cmd_hndl_ptr = NULL;
   }
}

/* function to populate the object counters in the output message cmd from the
 * input message
 */
static void sgm_populate_gmc_obj_counters(spf_msg_cmd_graph_open_t *gmc_in_cmd_ptr,
                                          spf_msg_cmd_graph_open_t *gmc_out_cmd_ptr)
{

   memset((void *)gmc_out_cmd_ptr, 0, sizeof(spf_msg_cmd_graph_open_t));
   /** Populate all the object counters */
   gmc_out_cmd_ptr->num_sub_graphs             = gmc_in_cmd_ptr->num_sub_graphs;
   gmc_out_cmd_ptr->num_modules_list           = gmc_in_cmd_ptr->num_modules_list;
   gmc_out_cmd_ptr->num_module_conn            = gmc_in_cmd_ptr->num_module_conn;
   gmc_out_cmd_ptr->num_mod_prop_cfg           = gmc_in_cmd_ptr->num_mod_prop_cfg;
   gmc_out_cmd_ptr->num_module_ctrl_links      = gmc_in_cmd_ptr->num_module_ctrl_links;
   gmc_out_cmd_ptr->num_param_id_cfg           = gmc_in_cmd_ptr->num_param_id_cfg;
   gmc_out_cmd_ptr->num_satellite_containers   = gmc_in_cmd_ptr->num_satellite_containers;
   gmc_out_cmd_ptr->num_offloaded_imcl_peers   = gmc_in_cmd_ptr->num_offloaded_imcl_peers;
   gmc_out_cmd_ptr->num_mxd_heap_id_data_links = gmc_in_cmd_ptr->num_mxd_heap_id_data_links;
   gmc_out_cmd_ptr->num_mxd_heap_id_ctrl_links = gmc_in_cmd_ptr->num_mxd_heap_id_ctrl_links;
}

static ar_result_t sgm_create_satellite_graph_open_client_payload(spgm_info_t *             spgm_ptr,
                                                                  spf_msg_cmd_graph_open_t *olc_satellite_open_cmd_ptr,
                                                                  uint8_t *                 open_payload_ptr,
                                                                  uint32_t                  open_payload_size)
{
   ar_result_t result      = AR_EOK;
   uint32_t    offset      = 0;
   uint8_t *   payload_ptr = open_payload_ptr;
   /** Populate all the object counters */
   uint32_t num_sub_graphs           = olc_satellite_open_cmd_ptr->num_sub_graphs;
   uint32_t num_containers           = olc_satellite_open_cmd_ptr->num_satellite_containers;
   uint32_t num_modules_list         = olc_satellite_open_cmd_ptr->num_modules_list;
   uint32_t num_mod_prop_cfg         = olc_satellite_open_cmd_ptr->num_mod_prop_cfg;
   uint32_t num_module_conn          = olc_satellite_open_cmd_ptr->num_module_conn;
   uint32_t num_offloaded_imcl_peers = olc_satellite_open_cmd_ptr->num_offloaded_imcl_peers;
   uint32_t num_module_ctrl_links    = olc_satellite_open_cmd_ptr->num_module_ctrl_links;
   uint32_t num_param_cfg            = olc_satellite_open_cmd_ptr->num_param_id_cfg;

   if (num_sub_graphs > 0)
   {
      /* update the sub graph information to the payload*/
      result = sgm_open_fill_sub_graph_cfg(olc_satellite_open_cmd_ptr->sg_cfg_list_pptr,
                                           num_sub_graphs,
                                           payload_ptr,
                                           &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_containers > 0)
   {
      /* update the container information to the payload*/
      result = sgm_open_fill_container_cfg(olc_satellite_open_cmd_ptr->sat_cnt_config_pptr,
                                           num_containers,
                                           payload_ptr,
                                           &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_modules_list > 0)
   {
      /* update the module list information to the payload*/
      result = sgm_open_fill_module_list_cfg(olc_satellite_open_cmd_ptr->mod_list_pptr,
                                             num_modules_list,
                                             payload_ptr,
                                             &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_mod_prop_cfg > 0)
   {
      /* update the module property information to the payload*/
      result = sgm_open_fill_module_prop_cfg(olc_satellite_open_cmd_ptr->mod_prop_cfg_list_pptr,
                                             num_mod_prop_cfg,
                                             payload_ptr,
                                             &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_module_conn > 0)
   {
      /* update the module connection information to the payload*/
      result = sgm_open_fill_module_conn_cfg(olc_satellite_open_cmd_ptr->mod_conn_list_pptr,
                                             num_module_conn,
                                             payload_ptr,
                                             &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_offloaded_imcl_peers)
   {
      /* update the module connection information to the payload*/
      result = sgm_open_fill_imcl_peer_cfg(olc_satellite_open_cmd_ptr->imcl_peer_domain_info_pptr,
                                           num_offloaded_imcl_peers,
                                           payload_ptr,
                                           &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_module_ctrl_links > 0)
   {
      /* update the control link information to the payload*/
      result = sgm_open_fill_ctrl_link_cfg(olc_satellite_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr,
                                           num_module_ctrl_links,
                                           payload_ptr,
                                           &offset);
      payload_ptr += offset;
      offset = 0;
   }

   if (num_param_cfg > 0)
   {
      /* update the param data information to the payload*/
      result = sgm_open_fill_param_data_cfg(spgm_ptr,
                                            olc_satellite_open_cmd_ptr->param_data_pptr,
                                            num_param_cfg,
                                            payload_ptr,
                                            &offset);
      payload_ptr += offset;
      offset = 0;
   }

   return result;
}

static ar_result_t sgm_get_graph_open_client_payload_size(spgm_info_t *             spgm_ptr,
                                                          spf_msg_cmd_graph_open_t *gmc_sat_open_cmd_ptr,
                                                          uint32_t *                open_payload_size_ptr)
{
   ar_result_t result       = AR_EOK;
   uint32_t    payload_size = 0;
   uint32_t    temp_size    = 0;
   uint32_t    get_sat_pd   = 0;
   /** Populate all the object counters */
   uint32_t num_sub_graphs           = gmc_sat_open_cmd_ptr->num_sub_graphs;
   uint32_t num_modules_list         = gmc_sat_open_cmd_ptr->num_modules_list;
   uint32_t num_module_conn          = gmc_sat_open_cmd_ptr->num_module_conn;
   uint32_t num_mod_prop_cfg         = gmc_sat_open_cmd_ptr->num_mod_prop_cfg;
   uint32_t num_offloaded_imcl_peers = gmc_sat_open_cmd_ptr->num_offloaded_imcl_peers;
   uint32_t num_module_ctrl_links    = gmc_sat_open_cmd_ptr->num_module_ctrl_links;
   uint32_t num_param_id_cfg         = gmc_sat_open_cmd_ptr->num_param_id_cfg;
   uint32_t num_containers           = gmc_sat_open_cmd_ptr->num_satellite_containers;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_MED_PRIO, "num_offloaded_imcl_peers = %lu", num_offloaded_imcl_peers);

   if (num_sub_graphs > 0)
   {
      /* get the size of the sub graph payload size*/
      if (AR_EOK != (result = sgm_open_get_sub_graph_payload_size(gmc_sat_open_cmd_ptr->sg_cfg_list_pptr,
                                                                  num_sub_graphs,
                                                                  &temp_size,
                                                                  &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_containers > 0)
   {
      /* get the size of the container payload size*/
      if (AR_EOK != (result = sgm_open_get_container_payload_size(gmc_sat_open_cmd_ptr->sat_cnt_config_pptr,
                                                                  num_containers,
                                                                  &temp_size,
                                                                  &spgm_ptr->sgm_id,
                                                                  &get_sat_pd)))
      {
         return result;
      }
      spgm_ptr->sgm_id.sat_pd = get_sat_pd;
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_modules_list > 0)
   {
      /* get the size of the module list payload size*/
      if (AR_EOK != (result = sgm_open_get_module_list_payload_size(gmc_sat_open_cmd_ptr->mod_list_pptr,
                                                                    num_modules_list,
                                                                    &temp_size,
                                                                    &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_mod_prop_cfg > 0)
   {
      /* get the size of the module prop configuration payload size*/
      if (AR_EOK != (result = sgm_open_get_module_prop_payload_size(gmc_sat_open_cmd_ptr->mod_prop_cfg_list_pptr,
                                                                    num_mod_prop_cfg,
                                                                    &temp_size,
                                                                    &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_module_conn > 0)
   {
      /* get the size of the module connection configuration payload size*/
      if (AR_EOK != (result = sgm_open_get_module_conn_payload_size(gmc_sat_open_cmd_ptr->mod_conn_list_pptr,
                                                                    num_module_conn,
                                                                    &temp_size,
                                                                    &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_offloaded_imcl_peers > 0)
   {
      /* get the size of the imcl peer info payload size*/
      if (AR_EOK != (result = sgm_open_get_imcl_peer_info_payload_size(gmc_sat_open_cmd_ptr->imcl_peer_domain_info_pptr,
                                                                       num_offloaded_imcl_peers,
                                                                       &temp_size,
                                                                       &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_module_ctrl_links > 0)
   {
      /* get the size of the module control link configuration payload size*/
      if (AR_EOK != (result = sgm_open_get_ctrl_link_cfg_payload_size(gmc_sat_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr,
                                                                      num_module_ctrl_links,
                                                                      &temp_size,
                                                                      &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   if (num_param_id_cfg > 0)
   {
      /* get the size of the module param data payload size*/
      if (AR_EOK != (result = sgm_open_get_param_data_payload_size(gmc_sat_open_cmd_ptr->param_data_pptr,
                                                                   num_param_id_cfg,
                                                                   &temp_size,
                                                                   &spgm_ptr->sgm_id)))
      {
         return result;
      }
      payload_size += temp_size;
      temp_size = 0;
   }

   *open_payload_size_ptr = payload_size;

   OLC_SGM_MSG(OLC_SGM_ID, DBG_HIGH_PRIO, "GRAPH_OPEN: satellite_graph_open payload config size %lu", payload_size);

   return result;
}

static ar_result_t sgm_gmc_open_validate_payload_size(spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                                      uint32_t                  max_payload_size,
                                                      spgm_info_t *             spgm_ptr)

{

   ar_result_t result                = AR_EOK;
   uint32_t    required_payload_size = 0;

   // Update this when ever open payload is updated with additional fields
   required_payload_size += sizeof(spf_msg_cmd_graph_open_t);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_sub_graphs);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_modules_list);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_mod_prop_cfg);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_module_conn);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_module_ctrl_links);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_param_id_cfg);
   required_payload_size += (SIZE_OF_PTR() * gmc_apm_open_cmd_ptr->num_satellite_containers);

   if (required_payload_size > max_payload_size)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_HIGH_PRIO,
                  "GRAPH_OPEN: OLC create sat gmc_open payload: buffer size insufficient"
                  " required_size %lu, actual payload_size %lu ",
                  required_payload_size,
                  max_payload_size);

      return AR_EBADPARAM;
   }
   return result;
}

static ar_result_t olc_create_satellite_graph_open_payload(spgm_info_t *             spgm_ptr,
                                                           spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                                           spf_msg_cmd_graph_open_t *gmc_sat_open_cmd_ptr,
                                                           uint32_t                  max_payload_size)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id                    = 0;
   uint8_t *msg_data_start_ptr        = NULL;
   uint32_t arr_idx                   = 0;
   uint32_t temp_arr_idx              = 0;
   uint32_t olc_container_id          = 0;
   bool_t   is_module_in_satellite[2] = { FALSE, FALSE };

   VERIFY(result, (NULL != spgm_ptr));
   VERIFY(result, (NULL != gmc_apm_open_cmd_ptr));
   VERIFY(result, (NULL != gmc_sat_open_cmd_ptr));
   log_id = spgm_ptr->sgm_id.log_id;

   if (0 == spgm_ptr->cu_ptr->gu_ptr->container_instance_id)
   {
      VERIFY(result, (NULL != gmc_apm_open_cmd_ptr->container_cfg_ptr));
      olc_container_id = gmc_apm_open_cmd_ptr->container_cfg_ptr->container_id;
   }
   else
   {
      if (NULL != gmc_apm_open_cmd_ptr->container_cfg_ptr)
      {
         VERIFY(result, (spgm_ptr->cu_ptr->gu_ptr->container_instance_id == gmc_apm_open_cmd_ptr->container_cfg_ptr->container_id));
         olc_container_id = spgm_ptr->cu_ptr->gu_ptr->container_instance_id;
      }
   }

   /** validate the payload buffer size created */
   if (AR_EOK != (result = sgm_gmc_open_validate_payload_size(gmc_apm_open_cmd_ptr, max_payload_size, spgm_ptr)))
   {
      return result;
   }

   /** Get the pointer to start of the configuration payload for graph open message */
   msg_data_start_ptr = ((uint8_t *)gmc_sat_open_cmd_ptr) + sizeof(spf_msg_cmd_graph_open_t);

   /** Populate all the object counters */
   sgm_populate_gmc_obj_counters(gmc_apm_open_cmd_ptr, gmc_sat_open_cmd_ptr);

   /** Populate container property payload pointer */
   gmc_sat_open_cmd_ptr->container_cfg_ptr = NULL;

   /** Configure the start of array of pointers to store the sub-graph configuration
    *  payload pointers with GRAPH OPEN message payload */
   gmc_sat_open_cmd_ptr->sg_cfg_list_pptr = (apm_sub_graph_cfg_t **)msg_data_start_ptr;

   /**## 1. Populate the sub-graph ID list */
   /* all the sub graphs in Satellite Graph would be added to the OLC */

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_sub_graphs; arr_idx++)
   {
      gmc_sat_open_cmd_ptr->sg_cfg_list_pptr[arr_idx] = gmc_apm_open_cmd_ptr->sg_cfg_list_pptr[arr_idx];
   }

   temp_arr_idx = 0;

   /**## 2. Populate the module list */
   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module config
    *  payload within graph open message payload */
   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_sub_graphs);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->mod_list_pptr = (apm_modules_list_t **)msg_data_start_ptr;

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_modules_list; arr_idx++)
   {
      apm_modules_list_t *module_list_ptr = gmc_apm_open_cmd_ptr->mod_list_pptr[arr_idx];
      if (module_list_ptr->container_id != olc_container_id)
      {
         gmc_sat_open_cmd_ptr->mod_list_pptr[temp_arr_idx++] = gmc_apm_open_cmd_ptr->mod_list_pptr[arr_idx];
      }
   }

   gmc_sat_open_cmd_ptr->num_modules_list = temp_arr_idx;

   /** Reset the array index */
   temp_arr_idx = 0;

   /**## 3. Populate the module property configuration */
   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module property config
    *  payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_modules_list);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->mod_prop_cfg_list_pptr = (apm_module_prop_cfg_t **)msg_data_start_ptr;

   /** Populate the module prop list config pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_mod_prop_cfg; arr_idx++)
   {
      apm_module_prop_cfg_t *module_prop_list_ptr = gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];
      if (check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                     spgm_ptr->gu_graph_info.num_satellite_modules,
                                     module_prop_list_ptr->instance_id))
      {
         gmc_sat_open_cmd_ptr->mod_prop_cfg_list_pptr[temp_arr_idx++] =
            gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];
      }
   }

   gmc_sat_open_cmd_ptr->num_mod_prop_cfg = temp_arr_idx;

   /**## 4. Populate the module connection list */
   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module connection
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_mod_prop_cfg);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->mod_conn_list_pptr = (apm_module_conn_cfg_t **)msg_data_start_ptr;

   /** Populate the module connection list config pointers */

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_module_conn; arr_idx++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = gmc_apm_open_cmd_ptr->mod_conn_list_pptr[arr_idx];

      is_module_in_satellite[0] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_conn_ptr->src_mod_inst_id);

      is_module_in_satellite[1] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_conn_ptr->dst_mod_inst_id);
      // todo : VB -->  we will have more cases
      if ((is_module_in_satellite[0] && is_module_in_satellite[1]))
      {
         gmc_sat_open_cmd_ptr->mod_conn_list_pptr[temp_arr_idx++] = gmc_apm_open_cmd_ptr->mod_conn_list_pptr[arr_idx];
      }
   }
   gmc_sat_open_cmd_ptr->num_module_conn = temp_arr_idx;

   /////////////////////////////////////////////////////
   /**## 5. Populate the remote IMCL peer info list */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the IMCL offloaded peer info
       within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_module_conn);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->imcl_peer_domain_info_pptr = (apm_imcl_peer_domain_info_t **)msg_data_start_ptr;

   /** Populate the module prop list config pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_offloaded_imcl_peers; arr_idx++)
   {
      gmc_sat_open_cmd_ptr->imcl_peer_domain_info_pptr[temp_arr_idx++] =
         gmc_apm_open_cmd_ptr->imcl_peer_domain_info_pptr[arr_idx];
   }

   gmc_sat_open_cmd_ptr->num_offloaded_imcl_peers = temp_arr_idx;
   ///////////////////////////////////////////////////////
   /**## 6. Populate the module control link config list */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_offloaded_imcl_peers);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr = (apm_module_ctrl_link_cfg_t **)msg_data_start_ptr;

   /** for now there are no control links between the processor. based on this assumption,
    *  there are no control links for OLC modules
    *  TODO : VB will need to update once controls link concept is clear for offload
    */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_module_ctrl_links; arr_idx++)
   {
      apm_module_ctrl_link_cfg_t *cmd_ctrl_conn_ptr = gmc_apm_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[arr_idx];

      is_module_in_satellite[0] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_ctrl_conn_ptr->peer_1_mod_iid);

      is_module_in_satellite[1] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_ctrl_conn_ptr->peer_2_mod_iid);

      if (!(is_module_in_satellite[0] || is_module_in_satellite[1])) // if neither is in satellite, it's unexpected
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Unexpected, Neither IMCL peer is a part of the satellite module list. At lease one should be! MIIDS: "
                "0x%lx, 0x%lx",
                cmd_ctrl_conn_ptr->peer_1_mod_iid,
                cmd_ctrl_conn_ptr->peer_2_mod_iid);
         break;
      }

      gmc_sat_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[temp_arr_idx++] =
         gmc_apm_open_cmd_ptr->mod_ctrl_link_cfg_list_pptr[arr_idx];
   }
   gmc_sat_open_cmd_ptr->num_module_ctrl_links = temp_arr_idx;
   ///////////////////////////////////////////////////////////

   /**## 7. Populate the module configuration */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_module_ctrl_links);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->param_data_pptr = (void **)msg_data_start_ptr;

   /** Populate the module configuration list config pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_param_id_cfg; arr_idx++)
   {
      apm_module_param_data_t *module_prop_list_ptr =
         (apm_module_param_data_t *)gmc_apm_open_cmd_ptr->param_data_pptr[arr_idx];

      if (check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                     spgm_ptr->gu_graph_info.num_satellite_modules,
                                     module_prop_list_ptr->module_instance_id))
      {
         gmc_sat_open_cmd_ptr->mod_prop_cfg_list_pptr[temp_arr_idx++] =
            gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];
      }
   }
   gmc_sat_open_cmd_ptr->num_param_id_cfg = temp_arr_idx;

   /**## 7. Populate the satellite container configuration */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_sat_open_cmd_ptr->num_param_id_cfg);

   /** Store the pointer to array of pointers */
   gmc_sat_open_cmd_ptr->sat_cnt_config_pptr = (apm_container_cfg_t **)msg_data_start_ptr;

   /** Populate the satellite container configuration pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_satellite_containers; arr_idx++)
   {
      gmc_sat_open_cmd_ptr->sat_cnt_config_pptr[arr_idx] = gmc_apm_open_cmd_ptr->sat_cnt_config_pptr[arr_idx];
   }

   /** Num of satellite container during the olc open*/
   gmc_sat_open_cmd_ptr->num_satellite_containers = arr_idx;

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t olc_create_graph_open_payload(spgm_info_t *             spgm_ptr,
                                          spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                          spf_msg_cmd_graph_open_t *gmc_olc_open_cmd_ptr)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING

   uint32_t log_id                    = 0;
   uint32_t arr_idx                   = 0;
   uint32_t temp_arr_idx              = 0;
   uint32_t conn_arr_indx             = 0;
   uint32_t olc_container_id          = 0;
   bool_t   is_module_in_satellite[2] = { FALSE, FALSE };
   bool_t   is_module_in_olc[2]       = { FALSE, FALSE };

   uint8_t *msg_data_start_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   VERIFY(result, (NULL != gmc_apm_open_cmd_ptr));
   VERIFY(result, (NULL != gmc_olc_open_cmd_ptr));

   log_id = spgm_ptr->sgm_id.log_id;

   if (0 == spgm_ptr->cu_ptr->gu_ptr->container_instance_id)
   {
      VERIFY(result, (NULL != gmc_apm_open_cmd_ptr->container_cfg_ptr));
      olc_container_id = gmc_apm_open_cmd_ptr->container_cfg_ptr->container_id;
   }
   else
   {
      if (NULL != gmc_apm_open_cmd_ptr->container_cfg_ptr)
      {
         VERIFY(result, (spgm_ptr->cu_ptr->gu_ptr->container_instance_id == gmc_apm_open_cmd_ptr->container_cfg_ptr->container_id));
         olc_container_id = spgm_ptr->cu_ptr->gu_ptr->container_instance_id;
      }
   }

   /** Get the pointer to start of the config payload for graph open message */
   msg_data_start_ptr = ((uint8_t *)gmc_olc_open_cmd_ptr) + sizeof(spf_msg_cmd_graph_open_t);

   /** Populate all the object counters */
   sgm_populate_gmc_obj_counters(gmc_apm_open_cmd_ptr, gmc_olc_open_cmd_ptr);
   /** make the satellite container to zero explicitly */
   gmc_olc_open_cmd_ptr->num_satellite_containers = 0;

   /** Populate container property payload pointer */
   gmc_olc_open_cmd_ptr->container_cfg_ptr = gmc_apm_open_cmd_ptr->container_cfg_ptr;

   /** Configure the start of array of pointers to store the
    *  Sub-graph config payload pointers with GRAPH OPEN
    *  message payloads */
   gmc_olc_open_cmd_ptr->sg_cfg_list_pptr = (apm_sub_graph_cfg_t **)msg_data_start_ptr;

   /**## 1. Populate the sub-graph ID list */
   /* all the sub graphs in Satellite Graph would be added to the OLC */

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_sub_graphs; arr_idx++)
   {
      gmc_olc_open_cmd_ptr->sg_cfg_list_pptr[arr_idx] = gmc_apm_open_cmd_ptr->sg_cfg_list_pptr[arr_idx];
   }

   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module config
    *  payload within graph open message payload */
   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_sub_graphs);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->mod_list_pptr = (apm_modules_list_t **)msg_data_start_ptr;

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_modules_list; arr_idx++)
   {
      apm_modules_list_t *module_list_ptr = gmc_apm_open_cmd_ptr->mod_list_pptr[arr_idx];
      TRY(result, add_module_list_to_graph_info(spgm_ptr, module_list_ptr, olc_container_id));
      if (module_list_ptr->container_id == olc_container_id)
      {
         gmc_olc_open_cmd_ptr->mod_list_pptr[temp_arr_idx++] = gmc_apm_open_cmd_ptr->mod_list_pptr[arr_idx];
      }
   }

   gmc_olc_open_cmd_ptr->num_modules_list = temp_arr_idx;

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module property config
    *  payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_modules_list);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->mod_prop_cfg_list_pptr = (apm_module_prop_cfg_t **)msg_data_start_ptr;

   /** Populate the module prop list config pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_mod_prop_cfg; arr_idx++)
   {
      apm_module_prop_cfg_t *module_prop_list_ptr = gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];

      if (check_if_module_is_in_list(spgm_ptr->gu_graph_info.olc_module_list_ptr,
                                     spgm_ptr->gu_graph_info.num_olc_modules,
                                     module_prop_list_ptr->instance_id))
      {
         gmc_olc_open_cmd_ptr->mod_prop_cfg_list_pptr[temp_arr_idx++] =
            gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];
      }
   }

   gmc_olc_open_cmd_ptr->num_mod_prop_cfg = temp_arr_idx;

   /**## 4. Populate the module connection list */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module connection
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_mod_prop_cfg);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->mod_conn_list_pptr = (apm_module_conn_cfg_t **)msg_data_start_ptr;

   /** Populate the module connection list config pointers */

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_module_conn; arr_idx++)
   {
      apm_module_conn_cfg_t *cmd_conn_ptr = gmc_apm_open_cmd_ptr->mod_conn_list_pptr[arr_idx];

      is_module_in_olc[0] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.olc_module_list_ptr,
                                                       spgm_ptr->gu_graph_info.num_olc_modules,
                                                       cmd_conn_ptr->src_mod_inst_id);

      is_module_in_satellite[0] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_conn_ptr->src_mod_inst_id);

      is_module_in_olc[1] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.olc_module_list_ptr,
                                                       spgm_ptr->gu_graph_info.num_olc_modules,
                                                       cmd_conn_ptr->dst_mod_inst_id);

      is_module_in_satellite[1] = check_if_module_is_in_list(spgm_ptr->gu_graph_info.satellite_module_list_ptr,
                                                             spgm_ptr->gu_graph_info.num_satellite_modules,
                                                             cmd_conn_ptr->dst_mod_inst_id);

      // is source module not present in OLC and Satellite graph
      bool_t is_source_extn_mod = (!(is_module_in_olc[0] || is_module_in_satellite[0]));
      // is destn module not present in OLC and Satellite graph
      bool_t is_dest_extn_mod = (!(is_module_in_olc[1] || is_module_in_satellite[1]));

      // The source module is in another container and destination is module in OLC
      bool_t is_conn_extn_input = (is_source_extn_mod && is_module_in_olc[1]);

      // The source module is in OLC and destination is module in another container
      bool_t is_conn_extn_output = (is_dest_extn_mod && is_module_in_olc[0]);

      //
      if ((is_conn_extn_input || is_conn_extn_output))
      {
         gmc_olc_open_cmd_ptr->mod_conn_list_pptr[temp_arr_idx++] = gmc_apm_open_cmd_ptr->mod_conn_list_pptr[arr_idx];
      }
      else
      {
         // if the source is in OLC and the destination is satellite, add it to write IPC connection.
         if (is_module_in_olc[0] && is_module_in_satellite[1])
         {
            conn_arr_indx = 0;
            while (conn_arr_indx < SPDM_MAX_IO_PORTS)
            {
               if (FALSE ==
                   spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[OLC_IPC_WRITE_CLIENT_CONN][conn_arr_indx]
                      .is_node_used)
               {
                  conn_node_info_t *data_conn_ptr =
                     &spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[OLC_IPC_WRITE_CLIENT_CONN][conn_arr_indx];
                  data_conn_ptr->conn_type       = OLC_IPC_WRITE_CLIENT_CONN;
                  data_conn_ptr->dynamic_conn_id = conn_arr_indx;
                  data_conn_ptr->src_mod_inst_id = cmd_conn_ptr->src_mod_inst_id;
                  data_conn_ptr->dst_mod_inst_id = cmd_conn_ptr->dst_mod_inst_id;
                  data_conn_ptr->is_node_used    = TRUE;
                  spgm_ptr->process_info.data_conn_info.num_data_wr_conn++;
                  break;
               }
               conn_arr_indx++;
            }
         }
         // if the source is in satellite Graph and the destination is in OLC, add it to Read IPC connection.
         if (is_module_in_olc[1] && is_module_in_satellite[0])
         {
            conn_arr_indx = 0;
            while (conn_arr_indx < SPDM_MAX_IO_PORTS)
            {
               if (FALSE ==
                   spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[OLC_IPC_READ_CLIENT_CONN][conn_arr_indx]
                      .is_node_used)
               {
                  conn_node_info_t *data_conn_ptr =
                     &spgm_ptr->process_info.data_conn_info.ipc_rw_conn_list[OLC_IPC_READ_CLIENT_CONN][conn_arr_indx];
                  data_conn_ptr->conn_type       = OLC_IPC_READ_CLIENT_CONN;
                  data_conn_ptr->dynamic_conn_id = conn_arr_indx;
                  data_conn_ptr->src_mod_inst_id = cmd_conn_ptr->src_mod_inst_id;
                  data_conn_ptr->dst_mod_inst_id = cmd_conn_ptr->dst_mod_inst_id;
                  data_conn_ptr->is_node_used    = TRUE;
                  spgm_ptr->process_info.data_conn_info.num_data_rd_conn++;
                  break;
               }
               conn_arr_indx++;
            }
         }
      }
   }
   gmc_olc_open_cmd_ptr->num_module_conn = temp_arr_idx;

   /**## 5. Populate the imcl peer info config list */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_module_conn);

   /**AKR: There will be no control links IN the OLC
    */
   gmc_olc_open_cmd_ptr->num_offloaded_imcl_peers = 0;

   /**## 5. Populate the module control link config list */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_offloaded_imcl_peers);

   /**AKR: There will be no control links IN the OLC
    */
   gmc_olc_open_cmd_ptr->num_module_ctrl_links = 0;

   /**## 6. Populate the module configuration */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_module_ctrl_links);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->param_data_pptr = (void **)msg_data_start_ptr;

   /** Populate the module configuration list config pointers */
   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_param_id_cfg; arr_idx++)
   {
      apm_module_param_data_t *module_prop_list_ptr =
         (apm_module_param_data_t *)gmc_apm_open_cmd_ptr->param_data_pptr[arr_idx];

      if (check_if_module_is_in_list(spgm_ptr->gu_graph_info.olc_module_list_ptr,
                                     spgm_ptr->gu_graph_info.num_olc_modules,
                                     module_prop_list_ptr->module_instance_id))
      {
         gmc_olc_open_cmd_ptr->mod_prop_cfg_list_pptr[temp_arr_idx++] =
            gmc_apm_open_cmd_ptr->mod_prop_cfg_list_pptr[arr_idx];
      }
   }
   gmc_olc_open_cmd_ptr->num_param_id_cfg = temp_arr_idx;

   /**## 7. Populate the satellite container configuration */

   /** Reset the array index */
   temp_arr_idx = 0;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_param_id_cfg);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->sat_cnt_config_pptr = (apm_container_cfg_t **)msg_data_start_ptr;

   /** Num of satellite container during the olc open is zero */
   gmc_olc_open_cmd_ptr->num_satellite_containers = 0;

   msg_data_start_ptr += (SIZE_OF_PTR() * gmc_olc_open_cmd_ptr->num_satellite_containers);

   /** Store the pointer to array of pointers */
   gmc_olc_open_cmd_ptr->mxd_heap_id_data_links_cfg_pptr = (apm_mxd_heap_id_link_cfg_t **)msg_data_start_ptr;

   for (arr_idx = 0; arr_idx < gmc_apm_open_cmd_ptr->num_mxd_heap_id_data_links; arr_idx++)
   {
      gmc_olc_open_cmd_ptr->mxd_heap_id_data_links_cfg_pptr[arr_idx] =
         gmc_apm_open_cmd_ptr->mxd_heap_id_data_links_cfg_pptr[arr_idx];
   }

   gmc_olc_open_cmd_ptr->num_mxd_heap_id_data_links = gmc_apm_open_cmd_ptr->num_mxd_heap_id_data_links;

   gmc_olc_open_cmd_ptr->num_mxd_heap_id_ctrl_links = 0;

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

/* Function to create the client open payload for the satellite sub graph
 * Step 1:
 * This function takes the open payload (spf_msg) for the OLC container
 * and extracts the payload for the satellite graph (in gk msg format).
 * Step 2:
 * The satellite graph information is then packed in compliance with
 * the APM OPEN API specification for the Satellite APM to process the command
 */
ar_result_t sgm_create_graph_open_client_payload(spgm_info_t *             spgm_ptr,
                                                 spf_msg_cmd_graph_open_t *gmc_apm_open_cmd_ptr,
                                                 uint32_t                  apm_gmc_cmd_payload_size)
{
   ar_result_t               result                   = AR_EOK;
   bool_t                    is_inband                = TRUE;
   uint32_t                  sg_open_cmd_payload_size = 0;
   uint32_t                  offset                   = 0;
   uint8_t *                 payload_ptr              = NULL;
   sgm_shmem_handle_t *      shmem_node_ptr           = NULL;
   spf_msg_cmd_graph_open_t *gmc_sat_open_ptr         = NULL;

   /** allocating the memory for the satellite graph open information */
   gmc_sat_open_ptr = (spf_msg_cmd_graph_open_t *)posal_memory_malloc(apm_gmc_cmd_payload_size, spgm_ptr->cu_ptr->heap_id);
   if (NULL == gmc_sat_open_ptr)
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_OPEN: failed to allocate memory for satellite_graph_open(spf_msg)");
      return AR_ENOMEMORY;
   }

   /** extract the satellite graph details from the OLC open command */
   if (AR_EOK != (result = olc_create_satellite_graph_open_payload(spgm_ptr,
                                                                   gmc_apm_open_cmd_ptr,
                                                                   gmc_sat_open_ptr,
                                                                   apm_gmc_cmd_payload_size)))

   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_OPEN: failed to create the satellite_graph_open(spf_msg) from olc graph open msg");
      MFREE_NULLIFY(gmc_sat_open_ptr);
      return result;
   }

   /** determine the payload size for the satellite graph open client command */
   sg_open_cmd_payload_size = 0;
   if (AR_EOK !=
       (result = sgm_get_graph_open_client_payload_size(spgm_ptr, gmc_sat_open_ptr, &sg_open_cmd_payload_size)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_OPEN: failed to determine the client payload size for satellite_graph_open command");
      MFREE_NULLIFY(gmc_sat_open_ptr);
      return result;
   }

   if (0 == sg_open_cmd_payload_size)
   {
      MFREE_NULLIFY(gmc_sat_open_ptr);
      return AR_ECONTINUE;
   }

   if (OLC_IPC_MAX_IN_BAND_PAYLOAD_SIZE < sg_open_cmd_payload_size)
   {
      spgm_ptr->active_cmd_hndl_ptr->is_inband = FALSE;
      is_inband                                = spgm_ptr->active_cmd_hndl_ptr->is_inband;
   }

   if (AR_EOK != (result = sgm_alloc_cmd_hndl_resources(spgm_ptr,
                                                        sg_open_cmd_payload_size,
                                                        spgm_ptr->active_cmd_hndl_ptr->is_inband,
                                                        FALSE /*is_persistent*/)))
   {
      MFREE_NULLIFY(gmc_sat_open_ptr);
      return result;
   }

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK != (result = sgm_client_command_fill_header_cfg(payload_ptr,
                                                              &offset,
                                                              shmem_node_ptr,
                                                              sg_open_cmd_payload_size,
                                                              is_inband)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_OPEN: creating client payload for satellite_graph_mgmt command, failed to fill the header");

      MFREE_NULLIFY(gmc_sat_open_ptr);
      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   if (AR_EOK != (result = sgm_create_satellite_graph_open_client_payload(spgm_ptr,
                                                                          gmc_sat_open_ptr,
                                                                          payload_ptr,
                                                                          sg_open_cmd_payload_size)))
   {
      OLC_SGM_MSG(OLC_SGM_ID,
                  DBG_ERROR_PRIO,
                  "GRAPH_OPEN: Failed to create the client payload size for satellite_graph_open command");

      MFREE_NULLIFY(gmc_sat_open_ptr);
      return result;
   }

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK != (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, sg_open_cmd_payload_size)))
      {
         OLC_SGM_MSG(OLC_SGM_ID, DBG_ERROR_PRIO, "GRAPH_OPEN: failed to cache flush the graph open command data");
         return (AR_EPANIC | result);
      }
   }

   MFREE_NULLIFY(gmc_sat_open_ptr);
   return result;
}

ar_result_t sgm_create_graph_mgmt_client_payload(spgm_info_t *spgm_ptr, spf_msg_cmd_graph_mgmt_t *gmc_apm_gmgmt_cmd_ptr)
{

   ar_result_t         result                          = AR_EOK;
   bool_t              is_inband                       = spgm_ptr->active_cmd_hndl_ptr->is_inband;
   uint32_t            sgm_graph_mgmt_cmd_payload_size = 0;
   uint32_t            offset                          = 0;
   uint8_t *           payload_ptr                     = NULL;
   sgm_shmem_handle_t *shmem_node_ptr                  = NULL;

   /** determine the payload size for the satellite graph open client command */
   if (AR_EOK !=
       (result =
           sgm_get_graph_mgmt_client_payload_size(spgm_ptr, gmc_apm_gmgmt_cmd_ptr, &sgm_graph_mgmt_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to determine the client payload size for "
                   "satellite_graph_managment command",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   if (OLC_IPC_MAX_IN_BAND_PAYLOAD_SIZE < sgm_graph_mgmt_cmd_payload_size)
   {
      spgm_ptr->active_cmd_hndl_ptr->is_inband = FALSE;
      is_inband                                = spgm_ptr->active_cmd_hndl_ptr->is_inband;
   }

   if (AR_EOK !=
       (result =
           sgm_alloc_cmd_hndl_resources(spgm_ptr, sgm_graph_mgmt_cmd_payload_size, is_inband, FALSE /*is_persistent*/)))
   {
      return result;
   }

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK != (result = sgm_client_command_fill_header_cfg(payload_ptr,
                                                              &offset,
                                                              shmem_node_ptr,
                                                              sgm_graph_mgmt_cmd_payload_size,
                                                              is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "creating client payload for satellite_graph_mgmt command "
                   "Failed to fill the header",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);

      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   if (AR_EOK != (result = sgm_create_graph_mgmt_command_client_payload(spgm_ptr,
                                                                        gmc_apm_gmgmt_cmd_ptr,
                                                                        payload_ptr,
                                                                        sgm_graph_mgmt_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to create the client "
                   "payload for satellite_graph_mgmt command",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK !=
          (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, sgm_graph_mgmt_cmd_payload_size)))
      {
         OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                      DBG_ERROR_PRIO,
                      "DATA_MGMT:CONT_ID[0x%lX] sat pd [0x%lX]"
                      "failed to cache flush the graph mgmt command data",
                      spgm_ptr->sgm_id.cont_id,
                      spgm_ptr->sgm_id.sat_pd);

         return (AR_EPANIC | result);
      }
   }

   return result;
}

ar_result_t sgm_create_graph_close_client_payload(spgm_info_t *             spgm_ptr,
                                                  spf_msg_cmd_graph_mgmt_t *gmc_apm_close_cmd_ptr)
{

   ar_result_t         result                           = AR_EOK;
   bool_t              is_inband                        = spgm_ptr->active_cmd_hndl_ptr->is_inband;
   uint32_t            sgm_graph_close_cmd_payload_size = 0;
   uint32_t            offset                           = 0;
   uint8_t *           payload_ptr                      = NULL;
   sgm_shmem_handle_t *shmem_node_ptr                   = NULL;

   /** determine the payload size for the satellite graph open client command */
   if (AR_EOK !=
       (result =
           sgm_get_graph_close_client_payload_size(spgm_ptr, gmc_apm_close_cmd_ptr, &sgm_graph_close_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to determine the client payload size for "
                   "close command",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   if (OLC_IPC_MAX_IN_BAND_PAYLOAD_SIZE < sgm_graph_close_cmd_payload_size)
   {
      spgm_ptr->active_cmd_hndl_ptr->is_inband = FALSE;
      is_inband                                = spgm_ptr->active_cmd_hndl_ptr->is_inband;
   }

   if (AR_EOK != (result = sgm_alloc_cmd_hndl_resources(spgm_ptr,
                                                        sgm_graph_close_cmd_payload_size,
                                                        is_inband,
                                                        FALSE /*is_persistent*/)))
   {
      return result;
   }

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK != (result = sgm_client_command_fill_header_cfg(payload_ptr,
                                                              &offset,
                                                              shmem_node_ptr,
                                                              sgm_graph_close_cmd_payload_size,
                                                              is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "creating client payload for satellite_graph_close command "
                   "Failed to fill the header",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);

      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   if (AR_EOK != (result = sgm_create_graph_close_command_client_payload(spgm_ptr,
                                                                         gmc_apm_close_cmd_ptr,
                                                                         payload_ptr,
                                                                         sgm_graph_close_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to create the client "
                   "payload for satellite_graph_close command",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK !=
          (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, sgm_graph_close_cmd_payload_size)))
      {
         OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                      DBG_ERROR_PRIO,
                      "DATA_MGMT:CONT_ID[0x%lX] sat pd [0x%lX]"
                      "failed to cache flush the graph close command data",
                      spgm_ptr->sgm_id.cont_id,
                      spgm_ptr->sgm_id.sat_pd);

         return (AR_EPANIC | result);
      }
   }

   return result;
}

ar_result_t sgm_create_set_get_cfg_client_payload(spgm_info_t *                 spgm_ptr,
                                                  spf_msg_cmd_param_data_cfg_t *gkmc_in_param_data_cfg_ptr,
                                                  bool_t                        is_inband)
{

   ar_result_t         result                 = AR_EOK;
   uint32_t            graph_cmd_payload_size = 0;
   uint32_t            offset                 = 0;
   uint8_t *           payload_ptr            = NULL;
   sgm_shmem_handle_t *shmem_node_ptr         = NULL;

   /** determine the payload size for the satellite graph set get client command */
   if (AR_EOK != (result = sgm_get_graph_set_get_cfg_client_payload_size(spgm_ptr,
                                                                         gkmc_in_param_data_cfg_ptr,
                                                                         &graph_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to determine the client payload"
                   "size for graph_set_get_cfg command",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   // Allocate the shared memory resources for sending the command payload to the satellite graph
   if (AR_EOK !=
       (result = sgm_alloc_cmd_hndl_resources(spgm_ptr, graph_cmd_payload_size, is_inband, FALSE /*is_persistent*/)))
   {
      return result;
   }

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK !=
       (result =
           sgm_client_command_fill_header_cfg(payload_ptr, &offset, shmem_node_ptr, graph_cmd_payload_size, is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "creating client payload for graph_set_get_cfg command "
                   "Failed to fill the header ",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   // Fill the payload for set get configuration command
   if (AR_EOK != (result = sgm_graph_set_get_cfg_fill_client_payload(spgm_ptr,
                                                                     gkmc_in_param_data_cfg_ptr,
                                                                     payload_ptr,
                                                                     graph_cmd_payload_size)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "Failed to create the client payload for graph_set_get_cfg command ",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);

      return result;
   }

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK != (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, graph_cmd_payload_size)))
      {
         OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                      DBG_ERROR_PRIO,
                      "DATA_MGMT:CONT_ID[0x%lX] sat pd [0x%lX]"
                      "failed to cache flush the set_get command",
                      spgm_ptr->sgm_id.cont_id,
                      spgm_ptr->sgm_id.sat_pd);

         return (AR_EPANIC | result);
      }
   }

   return result;
}

ar_result_t sgm_create_set_get_cfg_packed_client_payload(spgm_info_t *spgm_ptr,
                                                         uint8_t *    set_cfg_ptr,
                                                         uint32_t     cfg_payload_size,
                                                         bool_t       is_inband)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t            log_id         = 0;
   uint32_t            offset         = 0;
   uint8_t *           payload_ptr    = NULL;
   sgm_shmem_handle_t *shmem_node_ptr = NULL;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != set_cfg_ptr));

   // Allocate the shared memory resources for sending the command payload to the satellite graph
   TRY(result, sgm_alloc_cmd_hndl_resources(spgm_ptr, cfg_payload_size, is_inband, FALSE /*is_persistent*/));

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK !=
       (result = sgm_client_command_fill_header_cfg(payload_ptr, &offset, shmem_node_ptr, cfg_payload_size, is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX] "
                   "creating client payload "
                   "for graph_set_get_cfg packed command "
                   "Failed to fill the header ",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);

      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   // Fill the payload for set get configuration command
   (void)memscpy(payload_ptr, cfg_payload_size, set_cfg_ptr, cfg_payload_size);

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK != (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, cfg_payload_size)))
      {
         OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                      DBG_ERROR_PRIO,
                      "DATA_MGMT:CONT_ID[0x%lX] sat pd [0x%lX]"
                      "failed to cache flush the set_get_packed command",
                      spgm_ptr->sgm_id.cont_id,
                      spgm_ptr->sgm_id.sat_pd);

         return (AR_EPANIC | result);
      }
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }

   return result;
}

ar_result_t sgm_create_reg_event_payload(spgm_info_t *spgm_ptr,
                                         uint8_t *    reg_payload_ptr,
                                         uint32_t     reg_payload_size,
                                         bool_t       is_inband)
{
   ar_result_t result = AR_EOK;
   INIT_EXCEPTION_HANDLING
   uint32_t log_id = 0;

   VERIFY(result, (NULL != spgm_ptr));
   log_id = spgm_ptr->sgm_id.log_id;
   VERIFY(result, (NULL != reg_payload_ptr));

   if (AR_EOK !=
       (result = sgm_create_set_get_cfg_packed_client_payload(spgm_ptr, reg_payload_ptr, reg_payload_size, is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX] "
                   "Failed to send event registration command to satellite graph ",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);

      return result;
   }

   CATCH(result, OLC_MSG_PREFIX, log_id)
   {
   }
   return result;
}

/* Function to fill the payload for the path delay command to be sent to satellite graph*/
ar_result_t sgm_create_get_path_delay_client_payload(spgm_info_t *spgm_ptr,
                                                     uint8_t *    apm_path_defn_for_delay_ptr,
                                                     bool_t       is_inband)
{

   ar_result_t         result                 = AR_EOK;
   uint32_t            graph_cmd_payload_size = 0;
   uint32_t            offset                 = 0;
   uint8_t *           payload_ptr            = NULL;
   sgm_shmem_handle_t *shmem_node_ptr         = NULL;

   /** determine the payload size for the satellite graph for get path delay client command */
   graph_cmd_payload_size =
      ALIGN_8_BYTES(sizeof(apm_module_param_data_t) + sizeof(sgm_param_id_offload_graph_path_delay_t));

   // Allocate the shared memory resources for sending the command payload to the satellite graph
   if (AR_EOK !=
       (result = sgm_alloc_cmd_hndl_resources(spgm_ptr, graph_cmd_payload_size, is_inband, FALSE /*is_persistent*/)))
   {
      return result;
   }

   shmem_node_ptr = &spgm_ptr->active_cmd_hndl_ptr->shm_info;
   payload_ptr    = spgm_ptr->active_cmd_hndl_ptr->cmd_payload_ptr;

   // Fill the apm cmd header information
   if (AR_EOK !=
       (result =
           sgm_client_command_fill_header_cfg(payload_ptr, &offset, shmem_node_ptr, graph_cmd_payload_size, is_inband)))
   {
      OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                   DBG_ERROR_PRIO,
                   "GRAPH_MGMT: CONT_ID[0x%lX] sat pd [0x%lX]"
                   "creating client payload for graph_set_get_cfg command "
                   "Failed to fill the header ",
                   spgm_ptr->sgm_id.cont_id,
                   spgm_ptr->sgm_id.sat_pd);
      return result;
   }

   if (TRUE == is_inband)
   {
      payload_ptr += offset;
   }
   else
   {
      payload_ptr = (uint8_t *)shmem_node_ptr->shm_mem_ptr;
   }

   // Fill the payload for set get configuration command
   apm_module_param_data_t *param_data_ptr = (apm_module_param_data_t *)payload_ptr;

   param_data_ptr->module_instance_id = APM_MODULE_INSTANCE_ID;
   param_data_ptr->param_id           = APM_PARAM_ID_OFFLOAD_GRAPH_PATH_DELAY;
   param_data_ptr->error_code         = 0;
   param_data_ptr->param_size         = sizeof(sgm_param_id_offload_graph_path_delay_t);

   sgm_param_id_offload_graph_path_delay_t *param_payload_ptr =
      (sgm_param_id_offload_graph_path_delay_t *)(param_data_ptr + 1);

   param_payload_ptr->num_paths = 1;
   memscpy((void *)&param_payload_ptr->paths,
           sizeof(apm_offload_graph_path_defn_for_delay_t),
           (void *)apm_path_defn_for_delay_ptr,
           sizeof(apm_offload_graph_path_defn_for_delay_t));

   // Flush the data.
   if (FALSE == is_inband)
   {
      if (AR_EOK != (result = posal_cache_flush((uint32_t)shmem_node_ptr->shm_mem_ptr, graph_cmd_payload_size)))
      {
         OLC_SPGM_MSG(spgm_ptr->sgm_id.log_id,
                      DBG_ERROR_PRIO,
                      "DATA_MGMT:CONT_ID[0x%lX] sat pd [0x%lX]"
                      "failed to cache flush the get path delay command",
                      spgm_ptr->sgm_id.cont_id,
                      spgm_ptr->sgm_id.sat_pd);

         return (AR_EPANIC | result);
      }
   }

   return result;
}
