/**
 * \file apm_msg_utils.c
 *
 * \brief
 *     This file contains APM messaging utility function definitions.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Innovation Center, Inc. All Rights Reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/****************************************************************************
 * INCLUDE HEADER FILES                                                     *
 ****************************************************************************/

#include "apm_graph_db.h"
#include "apm_graph_utils.h"
#include "apm_msg_utils.h"
#include "apm_data_path_utils.h"
#include "apm_cmd_utils.h"
#include "apm_set_get_cfg_utils.h"
#include "apm_runtime_link_hdlr_utils.h"
#include "apm_cntr_debug_if.h"
#include "apm_debug_info_cfg.h"
#include "proxy_cntr_if.h"

#define APM_DEBUG_MSG_UTILS

/****************************************************************************
 * GLOBALS                                                                  *
 ****************************************************************************/

/****************************************************************************
 * Function Declarations
 ****************************************************************************/

ar_result_t apm_clear_cont_cmd_rsp_ctrl(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr, bool_t release_rsp_msg_buf)
{
   ar_result_t result = AR_EOK;

   cont_cmd_ctrl_ptr->rsp_ctrl.rsp_pending       = FALSE;
   cont_cmd_ctrl_ptr->rsp_ctrl.rsp_result        = AR_EOK;
   cont_cmd_ctrl_ptr->rsp_ctrl.pending_msg_proc  = FALSE;
   cont_cmd_ctrl_ptr->rsp_ctrl.reuse_rsp_msg_buf = FALSE;

   if (cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg.payload_ptr)
   {
      if (release_rsp_msg_buf)
      {
         /** Return message buffer  */
         spf_msg_return_msg(&cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg);

         /** Clear the response message buffer obj */
         memset(&cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg, 0, sizeof(spf_msg_t));
      }
      else /** Reuse current response buffer */
      {
         /** Reuse current response buffer if the caller indicated to
          *  retain it for subsequent container messaging operation */
         cont_cmd_ctrl_ptr->rsp_ctrl.reuse_rsp_msg_buf = TRUE;
      }
   }

   return result;
}

ar_result_t apm_compute_set_get_cfg_msg_payload_size(apm_cont_cached_cfg_t *cont_cached_cfg_ptr,
                                                     uint32_t               msg_opcode,
                                                     uint32_t *             payload_size_ptr,
                                                     uint32_t               container_id)
{
   uint32_t                    result = AR_EOK;
   uint32_t                    num_ptr_objects;
   uint32_t                    msg_payload_size      = 0;
   uint32_t                    cntr_pid_payload_size = 0;
   spf_list_node_t *           curr_node_ptr;
   uint32_t                    param_id;
   apm_cont_set_get_cfg_hdr_t *set_cfg_header_ptr;
   apm_ext_utils_t *           ext_utils_ptr;
   uint32_t cntr_msg_payload_size = 0;

   /** Get the ext utils vtb ptr  */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /** Calculate total number of pointer objects including from
    *  container and module param ID's */
   num_ptr_objects =
      cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg + cont_cached_cfg_ptr->cont_cfg_params.num_cfg_params;

   /** Calculate the overall payload size */
   msg_payload_size = (sizeof(spf_msg_cmd_param_data_cfg_t) + (SIZE_OF_PTR() * num_ptr_objects));

   AR_MSG(DBG_MED_PRIO,
          "SPF_MSG [0x%lX]: cont_id:[0x%lX], num_mod_param[%lu], num_cont_param[%lu]",
          msg_opcode,
          container_id,
          cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg,
          cont_cached_cfg_ptr->cont_cfg_params.num_cfg_params);

   /** Get the pointer to cached container cfg params */
   curr_node_ptr = cont_cached_cfg_ptr->cont_cfg_params.param_data_list_ptr;

   /** Typically for the module config, the calibration is provided
    *  by the client. APM sends the pointer to the memory
    *  (in-band or OOB) holding module calibration to containers.
    *  For container param ID config, APM need to allocate the
    *  param ID payload and send the pointer. OVerall payload
    *  size need to account for container PID payload size as
    *  well */

   /** Iterate over the list of config params */
   while (curr_node_ptr)
   {
      /** Get the pointer to cached config header */
      set_cfg_header_ptr = (apm_cont_set_get_cfg_hdr_t *)curr_node_ptr->obj_ptr;

      /** Get the param ID to be sent to container */
      param_id = set_cfg_header_ptr->param_id;

      /** Update the payload size further based upon the cached
       *  container configuration */
      switch (param_id)
      {
         case CNTR_PARAM_ID_PATH_DELAY_CFG:
         case CNTR_PARAM_ID_CFG_SRC_MOD_DELAY_LIST:
         case CNTR_PARAM_ID_PATH_DESTROY:
         case CNTR_PARAM_ID_DESTROY_SRC_MOD_DELAY_LIST:
         {
            if (ext_utils_ptr->data_path_vtbl_ptr &&
                ext_utils_ptr->data_path_vtbl_ptr->apm_compute_cntr_path_delay_param_payload_size_fptr)
            {
               ext_utils_ptr->data_path_vtbl_ptr
                  ->apm_compute_cntr_path_delay_param_payload_size_fptr(container_id,
                                                                        set_cfg_header_ptr,
                                                                        &cntr_pid_payload_size);
            }

            /** Aggregate the total size */
            msg_payload_size += cntr_pid_payload_size;

            break;
         }
         case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
         {
            if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
                ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_compute_cntr_msg_payload_size_fptr)
            {
               if (AR_EOK != (result = ext_utils_ptr->set_get_cfg_vtbl_ptr
                                          ->apm_compute_cntr_msg_payload_size_fptr(param_id, &cntr_msg_payload_size)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_msg_set_get_cfg(): Failed to populate payload for param ID[0x%lx], "
                         "CONT_ID[0x%lX]",
                         param_id,
                         container_id);

                  return result;
               }
            }
            //msg_payload_size += (sizeof(apm_module_param_data_t) + sizeof(cntr_port_mf_param_data_cfg_t));
            msg_payload_size += cntr_msg_payload_size;
            break;
         }
         case CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO:
         {
            if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
                ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_compute_cntr_msg_payload_size_fptr)
            {
               if (AR_EOK != (result = ext_utils_ptr->set_get_cfg_vtbl_ptr
                                          ->apm_compute_cntr_msg_payload_size_fptr(param_id, &cntr_msg_payload_size)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_msg_set_get_cfg(): Failed to populate payload for param ID[0x%lx], "
                         "CONT_ID[0x%lX]",
                         param_id,
                         container_id);

                  return result;
               }
            }
            msg_payload_size += cntr_msg_payload_size;
            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "Unexpected param id[0x%lX], SPF_MSG[0x%lX]: cont_id:[0x%lX]",
                   param_id,
                   msg_opcode,
                   container_id);

            break;
         }

      } /** End of switch (param_id) */

      /** Advance to next node in the list */
      curr_node_ptr = curr_node_ptr->next_ptr;

   } /**End of while(cont cfg param list) */

   /** Update the return pointer */
   *payload_size_ptr = msg_payload_size;

   return result;
}

ar_result_t apm_compute_msg_payload_size(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                         uint32_t             msg_opcode,
                                         uint32_t *           payload_size_ptr,
                                         uint32_t             container_id)
{
   ar_result_t                   result           = AR_EOK;
   uint32_t                      msg_payload_size = 0;
   uint32_t                      num_sg_obj       = 0;
   uint32_t                      num_ptr_objects  = 0;
   uint32_t                      msg_header_size;
   apm_cont_graph_open_params_t *graph_open_param_ptr;
   apm_cont_cached_cfg_t *       cont_cached_cfg_ptr;
   apm_cmd_ctrl_t *              cmd_ctrl_ptr;
   uint32_t                      num_ip_data_ports, num_op_data_ports, num_ctrl_ports;
   apm_cached_cont_list_t        cached_cont_list_type;

   /** Get the pointer to container's cached config params   */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   /** Get the pointer to APM command control pointer mapped to
    *  current command control object */
   cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   /** Get the pointer to graph open command cached params   */
   graph_open_param_ptr = &cont_cached_cfg_ptr->graph_open_params;

   switch (msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_OPEN:
      {
         num_ptr_objects = graph_open_param_ptr->num_sub_graphs + graph_open_param_ptr->num_module_list_cfg +
                           graph_open_param_ptr->num_module_props + graph_open_param_ptr->num_data_links +
                           graph_open_param_ptr->num_ctrl_links + graph_open_param_ptr->num_mod_param_cfg +
                           graph_open_param_ptr->num_offloaded_imcl_peers +
                           graph_open_param_ptr->sat_graph_open_cfg.num_satellite_containers +
                           graph_open_param_ptr->mxd_heap_id_links_cfg[LINK_TYPE_DATA].num_nodes +
                           graph_open_param_ptr->mxd_heap_id_links_cfg[LINK_TYPE_CTRL].num_nodes;

         msg_header_size = sizeof(spf_msg_cmd_graph_open_t);

         /** Calculate the overall message payload size */
         msg_payload_size = msg_header_size + (SIZE_OF_PTR() * num_ptr_objects);

         AR_MSG(DBG_MED_PRIO,
                "SPF_MSG GRAPH_OPEN: cont_id:[0x%lX], num_sub_graph[%lu] "
                "num_mod_list[%lu], num_mod_prop[%lu], num_data_links[%lu], "
                "num_ctrl_link[%lu], num_offloaded_imcl_peers[%lu] num_mod_param[%lu],"
                "num_satellite_cont[%lu]",
                container_id,
                graph_open_param_ptr->num_sub_graphs,
                graph_open_param_ptr->num_module_list_cfg,
                graph_open_param_ptr->num_module_props,
                graph_open_param_ptr->num_data_links,
                graph_open_param_ptr->num_ctrl_links,
                graph_open_param_ptr->num_offloaded_imcl_peers,
                graph_open_param_ptr->num_mod_param_cfg,
                graph_open_param_ptr->sat_graph_open_cfg.num_satellite_containers);

         AR_MSG(DBG_MED_PRIO,
                "SPF_MSG GRAPH_OPEN: num_mxd_heap_id_data_links [%lu], num_mxd_heap_id_ctrl_links[[%lu]",
                graph_open_param_ptr->mxd_heap_id_links_cfg[LINK_TYPE_DATA].num_nodes,
                graph_open_param_ptr->mxd_heap_id_links_cfg[LINK_TYPE_CTRL].num_nodes);
         break;
      }
      case SPF_MSG_CMD_REGISTER_CFG:
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         num_ptr_objects = cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg;

         msg_payload_size = (sizeof(spf_msg_cmd_param_data_cfg_t) + (SIZE_OF_PTR() * num_ptr_objects));

         AR_MSG(DBG_MED_PRIO,
                "SPF_MSG [0x%lX]: cont_id:[0x%lX], num_mod_param[%lu]",
                msg_opcode,
                container_id,
                cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg);

         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      case SPF_MSG_CMD_SET_CFG:
      {
         result =
            apm_compute_set_get_cfg_msg_payload_size(cont_cached_cfg_ptr, msg_opcode, &msg_payload_size, container_id);

         break;
      }
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_FLUSH:
      case SPF_MSG_CMD_GRAPH_DISCONNECT:
      case SPF_MSG_CMD_GRAPH_CLOSE:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         if (APM_OBJ_TYPE_ACYCLIC == (cached_cont_list_type = apm_get_curr_active_cont_list_type(cmd_ctrl_ptr)))
         {
            /** Number of sub-graph ID's  */
            num_sg_obj = cont_cached_cfg_ptr->graph_mgmt_params.num_sub_graphs;
         }

         num_ip_data_ports =
            cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cached_cont_list_type][PORT_TYPE_DATA_IP].num_nodes;
         num_op_data_ports =
            cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cached_cont_list_type][PORT_TYPE_DATA_OP].num_nodes;
         num_ctrl_ports =
            cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cached_cont_list_type][PORT_TYPE_CTRL_IO].num_nodes;

         /** Number of IO port handles */
         num_ptr_objects = num_ip_data_ports + num_op_data_ports + num_ctrl_ports +
                           cont_cached_cfg_ptr->graph_mgmt_params.num_data_links +
                           cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links;

         msg_payload_size =
            sizeof(spf_msg_cmd_graph_mgmt_t) + (sizeof(uint32_t) * num_sg_obj) + (SIZE_OF_PTR() * num_ptr_objects);

         AR_MSG(DBG_MED_PRIO,
                "GRAPH_MGMT SPF_MSG[0x%lX]: cont_id:[0x%lX], num_sub_graph[%lu], num_ip_data_port[%lu],"
                " num_op_data_port[%lu], num_ctrl_ports[%lu], num_data_links[%lu], num_ctrl_links[%lu]",
                msg_opcode,
                container_id,
                num_sg_obj,
                num_ip_data_ports,
                num_op_data_ports,
                num_ctrl_ports,
                cont_cached_cfg_ptr->graph_mgmt_params.num_data_links,
                cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links);

         break;
      }
      case SPF_MSG_CMD_DESTROY_CONTAINER:
      {
         /** No payload required for destroy command to container */
         msg_payload_size = 0;

         break;
      }
      default:
      {
         /** Should not reach here */

         AR_MSG(DBG_ERROR_PRIO,
                "apm_compute_msg_payload_size(): Invalid msg opcode [0x%lX], cont_id:[0x%lX]",
                msg_opcode,
                container_id);

         return AR_EFAILED;
      }
   } /** End of switch (msg_opcode) */

   AR_MSG(DBG_MED_PRIO,
          "apm_compute_msg_payload_size(): msg opcode[0x%lX]: cont_id:[0x%lX], payload size[%lu]",
          msg_opcode,
          container_id,
          msg_payload_size);

   /** Update the overall payload size including the message
    *  header */
   *payload_size_ptr = GET_SPF_MSG_REQ_SIZE(msg_payload_size);

   return result;
}

ar_result_t apm_release_cont_cached_graph_open_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr)
{
   ar_result_t result = AR_EOK;

   /** Clear the container property pointer  */
   cached_cfg_ptr->graph_open_params.container_prop_ptr = NULL;

   /** Clear the sub-graph ID list */
   if (cached_cfg_ptr->graph_open_params.sub_graph_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.sub_graph_cfg_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.sub_graph_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_sub_graphs         = 0;
   }

   /** Clear Module list */
   if (cached_cfg_ptr->graph_open_params.module_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.module_cfg_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.module_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_module_list_cfg = 0;
   }

   /** Clear Module property list */
   if (cached_cfg_ptr->graph_open_params.module_prop_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.module_prop_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.module_prop_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_module_props     = 0;
   }

   /** Clear Module data link config list */
   if (cached_cfg_ptr->graph_open_params.mod_data_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.mod_data_link_cfg_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.mod_data_link_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_data_links             = 0;
   }

   /** Clear Module control link cfg list */
   if (cached_cfg_ptr->graph_open_params.mod_ctrl_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.mod_ctrl_link_cfg_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.mod_ctrl_link_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_ctrl_links             = 0;
   }

   /** Clear cached imcl peer domain info list */
   if (cached_cfg_ptr->graph_open_params.imcl_peer_domain_info_list_ptr)
   {
      spf_list_delete_list_and_free_objs(&cached_cfg_ptr->graph_open_params.imcl_peer_domain_info_list_ptr,
                                         TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.imcl_peer_domain_info_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_offloaded_imcl_peers       = 0;
   }

   /** Clear Module param id data list */
   if (cached_cfg_ptr->graph_open_params.param_data_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.param_data_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.param_data_list_ptr = NULL;
      cached_cfg_ptr->graph_open_params.num_mod_param_cfg   = 0;
   }

   /** Clear Satellite container configuration list */
   if (cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.sat_cnt_config_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.sat_cnt_config_ptr,
                           TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.sat_cnt_config_ptr       = NULL;
      cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.num_satellite_containers = 0;
   }

   /** Clear cached mixed heap prop data links cfg info list */
   if (cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA].list_ptr)
   {
      spf_list_delete_list_and_free_objs(&cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA]
                                             .list_ptr,
                                         TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA].list_ptr  = NULL;
      cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA].num_nodes = 0;
   }

   /** Clear cached mixed heap prop ctrl links cfg info list */
   if (cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL].list_ptr)
   {
      spf_list_delete_list_and_free_objs(&cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL]
                                             .list_ptr,
                                         TRUE /* pool_used */);

      cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL].list_ptr  = NULL;
      cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL].num_nodes = 0;
   }

   return result;
}

ar_result_t apm_release_cont_cached_graph_mgmt_cfg(apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                                   apm_cont_cached_cfg_t *cached_cfg_ptr)
{
   ar_result_t result = AR_EOK;

   /** Clear the sub-graph ID list */
   if (cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_mgmt_params.num_sub_graphs         = 0;
   }

   for (uint32_t cycle_type = 0; cycle_type < APM_PORT_CYCLE_TYPE_MAX; cycle_type++)
   {
      for (uint32_t dir_type = 0; dir_type < PORT_TYPE_MAX; dir_type++)
      {
         if (cached_cfg_ptr->graph_mgmt_params.cont_ports[cycle_type][dir_type].list_ptr)
         {
            if ((APM_CMD_GRAPH_OPEN == apm_cmd_ctrl_ptr->cmd_opcode) &&
                (APM_OPEN_CMD_OP_ERR_HDLR == apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx) &&
                (APM_OPEN_CMD_STATE_OPEN_FAIL == apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status))
            {
               spf_list_delete_list_and_free_objs(&cached_cfg_ptr->graph_mgmt_params.cont_ports[cycle_type][dir_type]
                                                      .list_ptr,
                                                  TRUE /* pool_used */);
            }
            else
            {
               spf_list_delete_list(&cached_cfg_ptr->graph_mgmt_params.cont_ports[cycle_type][dir_type].list_ptr,
                                    TRUE /* pool_used */);
            }

            memset(&cached_cfg_ptr->graph_mgmt_params.cont_ports[cycle_type][dir_type], 0, sizeof(apm_list_t));
         }

      } /** End of for (cont port direction type) */

   } /** End of for (cont port cycle type) */

   /** Clear data link list */
   if (cached_cfg_ptr->graph_mgmt_params.mod_data_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_mgmt_params.mod_data_link_cfg_list_ptr, TRUE /* pool_used */);
      cached_cfg_ptr->graph_mgmt_params.mod_data_link_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_mgmt_params.num_data_links             = 0;
   }

   /** Clear control link list */
   if (cached_cfg_ptr->graph_mgmt_params.mod_ctrl_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->graph_mgmt_params.mod_ctrl_link_cfg_list_ptr, TRUE /* pool_used */);
      cached_cfg_ptr->graph_mgmt_params.mod_ctrl_link_cfg_list_ptr = NULL;
      cached_cfg_ptr->graph_mgmt_params.num_ctrl_links             = 0;
   }

   return result;
}

static ar_result_t apm_release_cont_cached_spf_set_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr,
                                                       apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;

   switch (apm_cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx)
   {
      case APM_SET_GET_CFG_CMD_OP_SEND_CONT_MSG:
      case APM_SET_GET_CFG_CMD_OP_SEND_PROXY_MGR_MSG:
      {
         break;
      }
      case APM_SET_GET_CFG_CMD_OP_CLOSE_ALL:
      {
         result = apm_release_cont_cached_graph_mgmt_cfg(apm_cmd_ctrl_ptr, cached_cfg_ptr);
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_release_cont_cached_spf_set_cfg(): Un-support set get cmd operation index[%lu]",
                apm_cmd_ctrl_ptr->cmd_seq.set_get_cfg_cmd_seq.op_idx);
      }
   }
   return result;
}

static ar_result_t apm_release_cached_cont_set_get_cfg_params(apm_cont_cached_cfg_t *cached_cfg_ptr)
{
   ar_result_t                 result = AR_EOK;
   spf_list_node_t *           curr_node_ptr;
   apm_cont_set_get_cfg_hdr_t *param_data_hdr_ptr;
   apm_ext_utils_t *           ext_utils_ptr;

   /** Clear the path definition related param ID cached
    *  configuration */
   curr_node_ptr = cached_cfg_ptr->cont_cfg_params.param_data_list_ptr;

   /** Get ext utils vtbl pointer */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   while (curr_node_ptr)
   {
      param_data_hdr_ptr = (apm_cont_set_get_cfg_hdr_t *)curr_node_ptr->obj_ptr;

      switch (param_data_hdr_ptr->param_id)
      {
         case CNTR_PARAM_ID_PATH_DELAY_CFG:
         {
            if (ext_utils_ptr->data_path_vtbl_ptr &&
                ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_clear_cached_cont_cfg_params_fptr)
            {
               ext_utils_ptr->data_path_vtbl_ptr->apm_data_path_clear_cached_cont_cfg_params_fptr(curr_node_ptr);
            }

            break;
         }
         default:
         {
            break;
         }
      }

      /** Delete current node from the list, free up the allocated
       *  memory and advance the list pointer to point to next node
       *  in the list */
      spf_list_delete_node_and_free_obj(&curr_node_ptr, &cached_cfg_ptr->cont_cfg_params.param_data_list_ptr, TRUE);

      cached_cfg_ptr->cont_cfg_params.param_data_list_ptr = NULL;
      cached_cfg_ptr->cont_cfg_params.num_cfg_params      = 0;

   } /** End of while (cont param data list) */

   return result;
}

static ar_result_t apm_release_cached_module_set_get_cfg_params(apm_cont_cached_cfg_t *cached_cfg_ptr)
{
   ar_result_t result = AR_EOK;

   /** Clear Module param id data list */
   if (cached_cfg_ptr->module_cfg_params.param_data_list_ptr)
   {
      spf_list_delete_list(&cached_cfg_ptr->module_cfg_params.param_data_list_ptr, TRUE /* pool_used */);

      cached_cfg_ptr->module_cfg_params.param_data_list_ptr = NULL;
      cached_cfg_ptr->module_cfg_params.num_mod_param_cfg   = 0;
   }

   return result;
}

static ar_result_t apm_cont_release_close_cmd_params(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr)
{
   for (uint32_t cycle_type = 0; cycle_type < APM_PORT_CYCLE_TYPE_MAX; cycle_type++)
   {
      for (uint32_t port_type = 0; port_type < PORT_TYPE_MAX; port_type++)
      {
         if (cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[cycle_type][port_type].list_ptr)
         {
            spf_list_delete_list(&cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[cycle_type][port_type]
                                     .list_ptr,
                                 TRUE /* pool_used */);

            cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[cycle_type][port_type].list_ptr  = NULL;
            cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports[cycle_type][port_type].num_nodes = 0;
         }
      }
   }

   if (cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_data_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_data_link_cfg_list_ptr,
                           TRUE /* pool_used */);

      cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_data_link_cfg_list_ptr = NULL;
      cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_data_links             = 0;
   }

   if (cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_ctrl_link_cfg_list_ptr)
   {
      spf_list_delete_list(&cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_ctrl_link_cfg_list_ptr,
                           TRUE /* pool_used */);

      cont_cmd_ctrl_ptr->cmd_params.link_close_params.mod_ctrl_link_cfg_list_ptr = NULL;
      cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_ctrl_links             = 0;
   }

   return AR_EOK;
}

ar_result_t apm_cont_release_cmd_params(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    cmd_opcode;

   if (!cont_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_cont_release_cmd_params() container cached cfg ptr, is NULL");

      return AR_EFAILED;
   }

   /** Get the current command opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** Clear command parameters stored as part of multi step
    *  command processing */

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      {
         apm_cont_release_close_cmd_params(cont_cmd_ctrl_ptr);
         break;
      }
      default:
      {
         break;
      }

   } /** End of switch (cmd_opcode) */

   return result;
}

static ar_result_t apm_release_cont_cached_graph_open_misc_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr,
                                                               apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_ext_utils_t *ext_util_ptr;

   ext_util_ptr = apm_get_ext_utils_ptr();

   switch (apm_cmd_ctrl_ptr->cmd_seq.graph_open_seq.op_idx)
   {
      case APM_OPEN_CMD_OP_ERR_HDLR:
      {
         if (ext_util_ptr->err_hdlr_vtbl_ptr &&
             ext_util_ptr->err_hdlr_vtbl_ptr->apm_err_hdlr_clear_cont_cached_graph_open_cfg_fptr)
         {
            result =
               ext_util_ptr->err_hdlr_vtbl_ptr->apm_err_hdlr_clear_cont_cached_graph_open_cfg_fptr(cached_cfg_ptr,
                                                                                                   apm_cmd_ctrl_ptr);
         }

         break;
      }
      case APM_OPEN_CMD_OP_HDL_LINK_START:
      {
         result = apm_release_cont_cached_graph_mgmt_cfg(apm_cmd_ctrl_ptr, cached_cfg_ptr);

         break;
      }
      default:
      {
         result = apm_release_cont_cached_graph_open_cfg(cached_cfg_ptr);

         break;
      }
   }

   return result;
}

ar_result_t apm_release_cont_msg_cached_cfg(apm_cont_cached_cfg_t *cached_cfg_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    cmd_opcode;

   if (!cached_cfg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_cont_release_cached_cfg() container cached cfg ptr is NULL");

      return AR_EFAILED;
   }

   /** Get the current command opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** If this a new configuration for this container, clear the
    *  cached config structure */

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_OPEN:
      {
         result = apm_release_cont_cached_graph_open_misc_cfg(cached_cfg_ptr, apm_cmd_ctrl_ptr);

         break;
      }
      case APM_CMD_SET_CFG:
      case APM_CMD_REGISTER_CFG:
      case APM_CMD_DEREGISTER_CFG:
      case APM_CMD_GET_CFG:
      case SPF_EVT_TO_APM_FOR_PATH_DELAY:
      {
         result = apm_release_cached_module_set_get_cfg_params(cached_cfg_ptr);

         break;
      }
      case APM_CMD_GRAPH_PREPARE:
      case APM_CMD_GRAPH_START:
      case APM_CMD_GRAPH_STOP:
      case APM_CMD_GRAPH_FLUSH:
      case APM_CMD_GRAPH_CLOSE:
      case APM_CMD_CLOSE_ALL:
      case APM_CMD_GRAPH_SUSPEND:
      case SPF_MSG_CMD_PROXY_GRAPH_PREPARE:
      case SPF_MSG_CMD_PROXY_GRAPH_START:
      case SPF_MSG_CMD_PROXY_GRAPH_STOP:
      {
         result = apm_release_cont_cached_graph_mgmt_cfg(apm_cmd_ctrl_ptr, cached_cfg_ptr);

         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      {
         /* Close all can be called during SPF_MSG_CMD_SET_CFG in APM satellite side */
         result = apm_release_cont_cached_spf_set_cfg(cached_cfg_ptr, apm_cmd_ctrl_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "apm_cont_release_cached_cfg(): Unexpeted command opcode[0x%lX]", cmd_opcode);

         result = AR_EFAILED;
         break;
      }

   } /** End of switch (cmd_opcode) */

   /** Release container related cached parameters if present */
   result |= apm_release_cached_cont_set_get_cfg_params(cached_cfg_ptr);

   return result;
}

ar_result_t apm_populate_msg_graph_open(apm_cont_cached_cfg_t *cont_cached_cfg_ptr, spf_msg_t *cont_msg_ptr)
{
   spf_msg_header_t *        msg_header_ptr;
   spf_msg_cmd_graph_open_t *msg_graph_open_ptr;
   spf_list_node_t *         curr_node_ptr;
   uint32_t                  arr_idx = 0;
   apm_sub_graph_t *         sub_graph_node_ptr;
   uint8_t *                 msg_data_start_ptr;

   if (!cont_cached_cfg_ptr || !cont_msg_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_populate_msg_graph_open: Container cached config[0x%lX], and/or msg pointer[0x%lX] is NULL",
             cont_cached_cfg_ptr,
             cont_msg_ptr);

      return AR_EFAILED;
   }

   /** Get the pointer to GK message header */
   msg_header_ptr = (spf_msg_header_t *)cont_msg_ptr->payload_ptr;

   /** Get the pointer to start of the GRAPH OPEN message */
   msg_graph_open_ptr = (spf_msg_cmd_graph_open_t *)(&msg_header_ptr->payload_start);

   /** Clear the message payload */
   memset(msg_graph_open_ptr, 0, sizeof(spf_msg_cmd_graph_open_t));

   /** Get the pointer to start of the config payload for graph open message */
   msg_data_start_ptr = ((uint8_t *)msg_graph_open_ptr) + sizeof(spf_msg_cmd_graph_open_t);

   /** Populate all the object counters */
   msg_graph_open_ptr->num_sub_graphs           = cont_cached_cfg_ptr->graph_open_params.num_sub_graphs;
   msg_graph_open_ptr->num_modules_list         = cont_cached_cfg_ptr->graph_open_params.num_module_list_cfg;
   msg_graph_open_ptr->num_module_conn          = cont_cached_cfg_ptr->graph_open_params.num_data_links;
   msg_graph_open_ptr->num_mod_prop_cfg         = cont_cached_cfg_ptr->graph_open_params.num_module_props;
   msg_graph_open_ptr->num_offloaded_imcl_peers = cont_cached_cfg_ptr->graph_open_params.num_offloaded_imcl_peers;
   msg_graph_open_ptr->num_module_ctrl_links    = cont_cached_cfg_ptr->graph_open_params.num_ctrl_links;
   msg_graph_open_ptr->num_param_id_cfg         = cont_cached_cfg_ptr->graph_open_params.num_mod_param_cfg;
   msg_graph_open_ptr->num_satellite_containers =
      cont_cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.num_satellite_containers;

   msg_graph_open_ptr->num_mxd_heap_id_data_links =
      cont_cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA].num_nodes;

   msg_graph_open_ptr->num_mxd_heap_id_ctrl_links =
      cont_cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL].num_nodes;

   /** Populate container property payload pointer */
   msg_graph_open_ptr->container_cfg_ptr = cont_cached_cfg_ptr->graph_open_params.container_prop_ptr;

   /** Get the pointer to sub-graph config list */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.sub_graph_cfg_list_ptr;

   /** Configure the start of array of pointers to store the
    *  Sub-graph config payload pointers with GRAPH OPEN
    *  message payloads */
   msg_graph_open_ptr->sg_cfg_list_pptr = (apm_sub_graph_cfg_t **)msg_data_start_ptr;

   /**## 1. Populate the sub-graph ID list */
   while (curr_node_ptr)
   {
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      msg_graph_open_ptr->sg_cfg_list_pptr[arr_idx++] = sub_graph_node_ptr->cfg_cmd_payload;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 2. Populate the module list */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.module_cfg_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module config
    *  payload within graph open message payload */
   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_sub_graphs);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mod_list_pptr = (apm_modules_list_t **)msg_data_start_ptr;

   /** Populate the module list config pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mod_list_pptr[arr_idx++] = (apm_modules_list_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 3. Populate the module property list */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.module_prop_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module property config
    *  payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_modules_list);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mod_prop_cfg_list_pptr = (apm_module_prop_cfg_t **)msg_data_start_ptr;

   /** Populate the module prop list config pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mod_prop_cfg_list_pptr[arr_idx++] = (apm_module_prop_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 4. Populate the module connection list */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module data link config list
    *  ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.mod_data_link_cfg_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module connection
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_mod_prop_cfg);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mod_conn_list_pptr = (apm_module_conn_cfg_t **)msg_data_start_ptr;

   /** Populate the module connection list config pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mod_conn_list_pptr[arr_idx++] = (apm_module_conn_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 5. Populate the inter-proc ctrl link peer info */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.imcl_peer_domain_info_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_module_conn);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->imcl_peer_domain_info_pptr = (apm_imcl_peer_domain_info_t **)msg_data_start_ptr;

   /** Populate the module control link config list pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->imcl_peer_domain_info_pptr[arr_idx++] = (apm_imcl_peer_domain_info_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 6. Populate the module control link config list */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.mod_ctrl_link_cfg_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_offloaded_imcl_peers);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mod_ctrl_link_cfg_list_pptr = (apm_module_ctrl_link_cfg_t **)msg_data_start_ptr;

   /** Populate the module control link config list pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mod_ctrl_link_cfg_list_pptr[arr_idx++] = (apm_module_ctrl_link_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 7. Populate the module configuration */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.param_data_list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_module_ctrl_links);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->param_data_pptr = (void **)msg_data_start_ptr;

   /** Populate the module connection list config pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->param_data_pptr[arr_idx++] = (apm_module_param_data_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the param data list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 8. Populate the satellite container configuration */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of satellite container list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.sat_graph_open_cfg.sat_cnt_config_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module calibration
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_param_id_cfg);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->sat_cnt_config_pptr = (apm_container_cfg_t **)msg_data_start_ptr;

   /** Populate the module connection list config pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->sat_cnt_config_pptr[arr_idx++] = (apm_container_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the param data list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 9. Populate the non-default to default heap data link info */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of mxd heap link info list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_DATA].list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_satellite_containers);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mxd_heap_id_data_links_cfg_pptr = (apm_mxd_heap_id_link_cfg_t **)msg_data_start_ptr;

   /** Populate the module control link config list pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mxd_heap_id_data_links_cfg_pptr[arr_idx++] =
         (apm_mxd_heap_id_link_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /**## 10. Populate the non-default to default heap control link info */

   /** Reset the array index */
   arr_idx = 0;

   /** Get the pointer to list of mxd heap link info list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->graph_open_params.mxd_heap_id_links_cfg[LINK_TYPE_CTRL].list_ptr;

   /** Increment the msg data start pointer to point to next
    *  array of pointers start to store the module control link
    *  config payload within graph open message payload */

   msg_data_start_ptr += (SIZE_OF_PTR() * msg_graph_open_ptr->num_mxd_heap_id_data_links);

   /** Store the pointer to array of pointers */
   msg_graph_open_ptr->mxd_heap_id_ctrl_links_cfg_pptr = (apm_mxd_heap_id_link_cfg_t **)msg_data_start_ptr;

   /** Populate the module control link config list pointers */
   while (curr_node_ptr)
   {
      msg_graph_open_ptr->mxd_heap_id_ctrl_links_cfg_pptr[arr_idx++] =
         (apm_mxd_heap_id_link_cfg_t *)curr_node_ptr->obj_ptr;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return AR_EOK;
}

ar_result_t apm_update_cached_port_hdl_list(apm_cont_port_connect_info_t *cont_port_conn_info_ptr,
                                            apm_list_t *                  cont_cached_port_hdl_list_ptr)
{
   ar_result_t             result = AR_EOK;
   spf_list_node_t *       curr_port_hdl_list_node_ptr;
   spf_module_port_conn_t *port_conn_obj_ptr;
   bool_t                  is_node_added = FALSE;

   /** Get the pointer to list of container's port connections  */
   curr_port_hdl_list_node_ptr = cont_port_conn_info_ptr->port_conn_list_ptr;

   /** Iterate over the list  */
   while (curr_port_hdl_list_node_ptr)
   {
      /** Reset the node added flag to FALSE */
      is_node_added = FALSE;

      /** Get the pointer to port connection object  */
      port_conn_obj_ptr = (spf_module_port_conn_t *)curr_port_hdl_list_node_ptr->obj_ptr;

      /** Cache the obj */

      /** Check if the node is already in the cached list. If not
       *  added the node. Node presence check is required because
       *  as part of GRAPH CLOSE command, client may send both the
       *  SG ID close and link close, in which case we should cache
       *  the port connection node only once. */
      spf_list_search_and_add_obj(&cont_cached_port_hdl_list_ptr->list_ptr,
                                  port_conn_obj_ptr,
                                  &is_node_added,
                                  APM_INTERNAL_STATIC_HEAP_ID,
                                  TRUE);

      /** If the node is added, increment the counter */
      if (is_node_added)
      {
         cont_cached_port_hdl_list_ptr->num_nodes++;
      }

      /** Advance to next node in the list */
      curr_port_hdl_list_node_ptr = curr_port_hdl_list_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_graph_mgmt_aggregate_cont_port(apm_cmd_ctrl_t * apm_cmd_ctrl_ptr,
                                                      spf_list_node_t *port_conn_list_ptr,
                                                      uint32_t         gm_cmd_curr_sg_id,
                                                      apm_list_t *     cached_port_conn_list_ptr)
{
   ar_result_t                   result = AR_EOK;
   apm_cont_port_connect_info_t *port_conn_info_obj_ptr;
   spf_list_node_t *             curr_port_conn_node_ptr;
   uint32_t                      self_port_sg_id, peer_port_sg_id;
   bool_t                        cache_port_hdl = FALSE;
   apm_sub_graph_state_t         peer_sub_graph_state;

   curr_port_conn_node_ptr = port_conn_list_ptr;

   /** Iterate over list of container port connections for a
    *  given type */
   while (curr_port_conn_node_ptr)
   {
      /** Clear the flag to cache the port handles */
      cache_port_hdl = FALSE;

      /** Set the peer SG ID to invalid */
      peer_port_sg_id = APM_SG_ID_INVALID;

      /** Invalidate the peer SG state */
      peer_sub_graph_state = APM_SG_STATE_INVALID;

      /** Get the pointer to current connection object */
      port_conn_info_obj_ptr = (apm_cont_port_connect_info_t *)curr_port_conn_node_ptr->obj_ptr;

      /** Get the corresponding sub-graph ID's */
      self_port_sg_id = port_conn_info_obj_ptr->self_sg_obj_ptr->sub_graph_id;

      /** Get the peer port SG ID and state  */
      if ((port_conn_info_obj_ptr->peer_sg_obj_ptr) && (port_conn_info_obj_ptr->peer_sg_obj_ptr->sub_graph_id))
      {
         peer_port_sg_id = port_conn_info_obj_ptr->peer_sg_obj_ptr->sub_graph_id;

         peer_sub_graph_state = port_conn_info_obj_ptr->peer_sg_obj_ptr->state;
      }
      /** If the peer SG id in the port connection matches with GM
       *  cmd sub-graph ID, then cache this port connection */
      if (peer_port_sg_id == gm_cmd_curr_sg_id)
      {
         /** If the current sub-graph id needs to be skipped during
          *  suspend handling, then even the port handles where this
          *  sub-graph ID is present is peer should be skipped caching */
         if (apm_gm_cmd_cache_sg_id(apm_cmd_ctrl_ptr))
         {
            cache_port_hdl = TRUE;
         }
      }
      else if ((self_port_sg_id == gm_cmd_curr_sg_id) && (peer_port_sg_id != APM_SG_ID_INVALID) &&
               (peer_sub_graph_state != APM_SG_STATE_INVALID) &&
               (peer_sub_graph_state != port_conn_info_obj_ptr->peer_sg_propagated_state))

      {
         /** If the self SG id in the port connection matches with GM
          *  cmd sub-graph ID, and if the PEER sub-graph ID is valid and
          *  propagated state is not equal to the actual sub-graph state
          *  then cache this handle to be send to the containers \n
          *
          *  This scenario is possible in case if a new sub-graph is
          *  connected to an already PREPARED or STARTED sub-graph, in
          *  which case the new sub-graph needs to be informed explicitly
          *  about the peer port state \n Also, if the sub-graph is
          *  present in the initial list but no command is issued in the
          *  first iteration, but issues from second iteration onwards */

         /** If an open command is executing in parallel to graph
          *  management command, sub-graphs being opened should not be
          *  accounted graph mgmt. The state of these sub-graphs is
          *  init'd at the end of successful completion of GRPAH OPEN
          *  command. The Invalid SG state check in the conditional
          *  above ensure these sub-graphs are skipped. */

         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = TRUE;

         AR_MSG(DBG_MED_PRIO,
                "GRAPH MGMT: sub-graph list mixed state detected, cmd_opcode[0x%08lx]",
                apm_cmd_ctrl_ptr->cmd_opcode);

         cache_port_hdl = TRUE;
      }

      if (cache_port_hdl)
      {
         /** Cache this port connection to be sent to the container */
         apm_update_cached_port_hdl_list(port_conn_info_obj_ptr, cached_port_conn_list_ptr);

#ifdef APM_DEBUG_MSG_UTILS
         if (port_conn_info_obj_ptr->peer_sg_obj_ptr)
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_graph_mgmt_aggregate_cont_port(), GM_CMD_SG_ID[0x%lX], CONN self SG_ID[0x%lX], peer "
                   "SG_ID[0x%lX],num conn[%lu]",
                   gm_cmd_curr_sg_id,
                   port_conn_info_obj_ptr->self_sg_obj_ptr->sub_graph_id,
                   port_conn_info_obj_ptr->peer_sg_obj_ptr->sub_graph_id,
                   port_conn_info_obj_ptr->num_port_conn);
         }
#endif
      }

      /** Advance to next node in the list */
      curr_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;
   }

   return result;
}
ar_result_t apm_graph_mgmt_aggregate_cont_port_all(apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr,
                                                   apm_container_t * container_node_ptr,
                                                   apm_graph_info_t *graph_info_ptr,
                                                   uint32_t          gm_cmd_curr_sg_id)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   spf_list_node_t **     list_pptr          = NULL;
   uint32_t *             num_list_nodes_ptr = 0;
   bool_t                 cache_cont         = FALSE;
   apm_list_t(*cont_port_list_pptr)[PORT_TYPE_MAX];
   apm_list_t(*cont_cached_port_list_pptr)[PORT_TYPE_MAX];

   /** Get the pointer to current container command control
    *  pointer */
   apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to container cached configuration
    *  parameters */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   cont_port_list_pptr        = container_node_ptr->cont_ports_per_sg_pair;
   cont_cached_port_list_pptr = cont_cached_cfg_ptr->graph_mgmt_params.cont_ports;

   /** Get the input connection node list, corresponding to this
    *  sub-graph ID */

   for (uint32_t cont_port_list_type = 0; cont_port_list_type < APM_CONT_LIST_MAX; cont_port_list_type++)
   {
      /** Clear the flag    */
      cache_cont = FALSE;

      for (uint32_t port_type = 0; port_type < PORT_TYPE_MAX; port_type++)
      {
         if (cont_port_list_pptr[cont_port_list_type][port_type].list_ptr)
         {
            if (AR_EOK !=
                (result =
                    apm_graph_mgmt_aggregate_cont_port(apm_cmd_ctrl_ptr,
                                                       cont_port_list_pptr[cont_port_list_type][port_type].list_ptr,
                                                       gm_cmd_curr_sg_id,
                                                       &cont_cached_port_list_pptr[cont_port_list_type][port_type])))
            {
               return result;
            }
         }
      } /** End of for (port type) */

      /** If at least 1 input, output or control port connection is
       *  present, add this container to pending list of containers */
      if ((cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_port_list_type][PORT_TYPE_DATA_IP].num_nodes ||
           cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_port_list_type][PORT_TYPE_DATA_OP].num_nodes ||
           cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_port_list_type][PORT_TYPE_CTRL_IO].num_nodes))
      {
         cache_cont = TRUE;
      }

      if (cache_cont)
      {
         /** Get the pointer to the list of containers cached for the
          *  current graph management command */
         list_pptr =
            &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_port_list_type].list_ptr;

         /** Get the pointer to the number of containers cached for the
          *  current graph management command */
         num_list_nodes_ptr =
            &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_port_list_type].num_nodes;

         /** Cache this container under the graph management command
          *  control object */
         if (AR_EOK == (result = apm_db_search_and_add_node_to_list(list_pptr, container_node_ptr, num_list_nodes_ptr)))
         {
            AR_MSG(DBG_HIGH_PRIO,
                   "apm_graph_mgmt_aggregate_cont_port_all(), Cached CONT_ID[0X%lX] cont_list_idx[%lu], "
                   "GM_CMD_SG_ID[0x%lX], "
                   "cmd_opcode[0x%lX] ",
                   container_node_ptr->container_id,
                   cont_port_list_type,
                   gm_cmd_curr_sg_id,
                   apm_cmd_ctrl_ptr->cmd_opcode);
         }
      }
   } /** End of for (cached cont list type) */

   return result;
}

static ar_result_t apm_gm_cmd_port_close_aggregate_cont_port(spf_list_node_t *port_conn_list_ptr,
                                                             uint32_t         gm_cmd_curr_sg_id,
                                                             apm_list_t *     cached_port_conn_list_ptr)
{
   ar_result_t             result = AR_EOK;
   spf_list_node_t *       curr_port_conn_node_ptr;
   spf_module_port_conn_t *cont_port_conn_ptr;

   curr_port_conn_node_ptr = port_conn_list_ptr;

   /** Iterate over the list of port connections */
   while (curr_port_conn_node_ptr)
   {
      /** Get the port connection obj pointer */
      cont_port_conn_ptr = (spf_module_port_conn_t *)curr_port_conn_node_ptr->obj_ptr;

      /** If the sub-graph ID matches with PEER SG ID for this,
       *  cache it for closure */
      if (cont_port_conn_ptr->peer_mod_port_hdl.sub_graph_id == gm_cmd_curr_sg_id)
      {
         apm_db_search_and_add_node_to_list(&cached_port_conn_list_ptr->list_ptr,
                                            cont_port_conn_ptr,
                                            &cached_port_conn_list_ptr->num_nodes);
      }

      /** Advance to next node in the list  */
      curr_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_gm_cmd_cache_data_ctrl_link(apm_graph_info_t *graph_info_ptr,
                                                   uint32_t          peer1_mod_iid,
                                                   uint32_t          peer2_mod_iid,
                                                   uint32_t          gm_cmd_curr_sg_id,
                                                   spf_list_node_t **cached_link_list_pptr,
                                                   uint32_t *        num_cached_link_ptr,
                                                   void *            link_to_be_cached_ptr)
{

   ar_result_t   result = AR_EOK;
   apm_module_t *module_node_ptr_list[2];
   bool_t        DANGLING_LINK_NOT_ALLOWED = FALSE;

   enum
   {
      PEER_MOD_1 = 0,
      PEER_MOD_2 = 1
   };

   /** Validate the module instance pair if they exist */
   if (AR_EOK != (result = apm_validate_module_instance_pair(graph_info_ptr,
                                                             peer1_mod_iid,
                                                             peer2_mod_iid,
                                                             module_node_ptr_list,
                                                             DANGLING_LINK_NOT_ALLOWED)))
   {
      AR_MSG(DBG_ERROR_PRIO, "MOD_CTRL_LNK_PARSE: Module IID validation failed");

      return AR_EBADPARAM;
   }

   /** If the sub-graph ID matches with PEER SG ID for this,
    *  cache it for closure */
   if ((module_node_ptr_list[PEER_MOD_1]->host_sub_graph_ptr->sub_graph_id == gm_cmd_curr_sg_id) ||
       (module_node_ptr_list[PEER_MOD_2]->host_sub_graph_ptr->sub_graph_id == gm_cmd_curr_sg_id))
   {
      apm_db_search_and_add_node_to_list(cached_link_list_pptr, link_to_be_cached_ptr, num_cached_link_ptr);
   }

   return result;
}

static ar_result_t apm_gm_cmd_port_close_aggregate_data_links(apm_graph_info_t *graph_info_ptr,
                                                              spf_list_node_t * data_link_list_ptr,
                                                              uint32_t          gm_cmd_curr_sg_id,
                                                              spf_list_node_t **cached_data_link_list_pptr,
                                                              uint32_t *        num_cached_data_link_ptr)
{
   ar_result_t            result = AR_EOK;
   spf_list_node_t *      curr_node_ptr;
   apm_module_conn_cfg_t *data_link_ptr;

   curr_node_ptr = data_link_list_ptr;

   /** Iterate over the list of port connections */
   while (curr_node_ptr)
   {
      /** Get the port connection obj pointer */
      data_link_ptr = (apm_module_conn_cfg_t *)curr_node_ptr->obj_ptr;

#ifdef APM_DEBUG_MSG_UTILS
      AR_MSG(DBG_MED_PRIO,
             "apm_gm_cmd_port_close_aggregate_data_links(), GM_CMD_SG_ID[0x%lX], "
             "src_miid[0x%lX], dst_miid[0x%lX]",
             gm_cmd_curr_sg_id,
             data_link_ptr->src_mod_inst_id,
             data_link_ptr->dst_mod_inst_id);
#endif

      apm_gm_cmd_cache_data_ctrl_link(graph_info_ptr,
                                      data_link_ptr->src_mod_inst_id,
                                      data_link_ptr->dst_mod_inst_id,
                                      gm_cmd_curr_sg_id,
                                      cached_data_link_list_pptr,
                                      num_cached_data_link_ptr,
                                      curr_node_ptr->obj_ptr);

      /** Advance to next node in the list  */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_gm_cmd_port_close_aggregate_ctrl_links(apm_graph_info_t *graph_info_ptr,
                                                              spf_list_node_t * ctrl_link_list_ptr,
                                                              uint32_t          gm_cmd_curr_sg_id,
                                                              spf_list_node_t **cached_ctrl_link_list_pptr,
                                                              uint32_t *        num_cached_data_link_ptr)
{
   ar_result_t                 result = AR_EOK;
   spf_list_node_t *           curr_node_ptr;
   apm_module_ctrl_link_cfg_t *ctrl_link_ptr;

   curr_node_ptr = ctrl_link_list_ptr;

   /** Iterate over the list of port connections */
   while (curr_node_ptr)
   {
      /** Get the port connection obj pointer */
      ctrl_link_ptr = (apm_module_ctrl_link_cfg_t *)curr_node_ptr->obj_ptr;

#ifdef APM_DEBUG_MSG_UTILS
      AR_MSG(DBG_MED_PRIO,
             "apm_gm_cmd_port_close_aggregate_data_links(), GM_CMD_SG_ID[0x%lX], "
             "peer_1_miid[0x%lX], peer_2_miid[0x%lX]",
             gm_cmd_curr_sg_id,
             ctrl_link_ptr->peer_1_mod_iid,
             ctrl_link_ptr->peer_2_mod_iid);
#endif

      apm_gm_cmd_cache_data_ctrl_link(graph_info_ptr,
                                      ctrl_link_ptr->peer_1_mod_iid,
                                      ctrl_link_ptr->peer_2_mod_iid,
                                      gm_cmd_curr_sg_id,
                                      cached_ctrl_link_list_pptr,
                                      num_cached_data_link_ptr,
                                      curr_node_ptr->obj_ptr);

      /** Advance to next node in the list  */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

static ar_result_t apm_check_and_cache_cont_for_port_link_mgmt(apm_cmd_ctrl_t *       apm_cmd_ctrl_ptr,
                                                               apm_container_t *      container_node_ptr,
                                                               apm_cont_cached_cfg_t *cont_cached_cfg_ptr)
{
   ar_result_t       result = AR_EOK;
   spf_list_node_t **list_pptr;
   uint32_t *        num_list_nodes_ptr;
   bool_t            cache_cont = FALSE;

   for (uint32_t cont_list_type = 0; cont_list_type < APM_CONT_LIST_MAX; cont_list_type++)
   {
      /** Reset the flag   */
      cache_cont = FALSE;

      if (cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_IP].num_nodes ||
          cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_OP].num_nodes ||
          cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_CTRL_IO].num_nodes)
      {
         cache_cont = TRUE;
      }

      /** Cache for cyclic link, if present and need to operated
       *  upon */
      if ((APM_OBJ_TYPE_ACYCLIC == cont_list_type) && (cont_cached_cfg_ptr->graph_mgmt_params.num_data_links ||
                                                       cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links))
      {
         cache_cont = TRUE;
      }

      if (cache_cont)
      {
         /** Get the pointer to the list of containers cached for the
          *  current graph management command */
         list_pptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type].list_ptr;

         /** Get the pointer to the number of containers cached for the
          *  current graph management command */
         num_list_nodes_ptr =
            &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type].num_nodes;

         /** Cache this container under the graph management command
          *  control object */
         if (AR_EOK == (result = apm_db_search_and_add_node_to_list(list_pptr, container_node_ptr, num_list_nodes_ptr)))
         {
            AR_MSG(DBG_MED_PRIO,
                   "apm_check_and_cache_cont_for_port_link_mgmt(), Cached CONT_ID[0X%lX] cont_list_idx[%lu], "
                   "cmd_opcode[0x%lX] ",
                   container_node_ptr->container_id,
                   cont_list_type,
                   apm_cmd_ctrl_ptr->cmd_opcode);
         }
      }

      /** Clear the caching flag   */
      cache_cont = FALSE;

   } /** End of for(container list) */

   return result;
}

ar_result_t apm_gm_cmd_port_close_aggr_cont_port_all(apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr,
                                                     apm_container_t * container_node_ptr,
                                                     apm_graph_info_t *graph_info_ptr,
                                                     uint32_t          gm_cmd_curr_sg_id)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   bool_t                 cache_cont = FALSE;
   spf_list_node_t **     list_pptr;
   uint32_t *             num_list_nodes_ptr;
   apm_list_t(*cont_db_port_list_pptr)[PORT_TYPE_MAX];
   apm_list_t(*cont_cached_port_list_pptr)[PORT_TYPE_MAX];

   /** Get the pointer to current container command control
    *  pointer */
   apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to container cached configuration
    *  parameters */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   cont_db_port_list_pptr = cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports;

   cont_cached_port_list_pptr = cont_cached_cfg_ptr->graph_mgmt_params.cont_ports;

   /** Get the input, output and control connection node list, corresponding to this
    *  sub-graph ID */

   for (uint32_t cont_list_type = 0; cont_list_type < APM_CONT_LIST_MAX; cont_list_type++)
   {
      for (uint32_t port_dir_type = 0; port_dir_type < PORT_TYPE_MAX; port_dir_type++)
      {
         if (cont_db_port_list_pptr[cont_list_type][port_dir_type].list_ptr)
         {
            if (AR_EOK !=
                (result =
                    apm_gm_cmd_port_close_aggregate_cont_port(cont_db_port_list_pptr[cont_list_type][port_dir_type]
                                                                 .list_ptr,
                                                              gm_cmd_curr_sg_id,
                                                              &cont_cached_port_list_pptr[cont_list_type]
                                                                                         [port_dir_type])))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to aggr port close params for CONT_ID[0x%lX], "
                      "list_idx[%lu]  "
                      "cmd_opcode[0x%08lx]",
                      container_node_ptr->container_id,
                      cont_list_type,
                      apm_cmd_ctrl_ptr->cmd_opcode);

               return result;
            }

            cache_cont = TRUE;

         } /** End id curr port list non-empty */

         /** If port was found, cache this container for further
          *  processing */
         if (cache_cont)
         {
            list_pptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type].list_ptr;

            num_list_nodes_ptr =
               &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type].num_nodes;

            if (AR_EOK !=
                (result = apm_db_search_and_add_node_to_list(list_pptr, container_node_ptr, num_list_nodes_ptr)))
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to cache CONT_ID[0x%lX] for port/link mgmt, "
                      "cmd_opcode[0x%08lx]",
                      container_node_ptr->container_id,
                      apm_cmd_ctrl_ptr->cmd_opcode);

               return result;
            }
         }
      } /** End of for(port type) */

      /** Clear the flag for caching the container */
      cache_cont = FALSE;

   } /** End of for(cont list type) */

   /** Data and control links are cached only for acyclic links. */

   if (cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_data_links)
   {
      if (AR_EOK !=
          (result = apm_gm_cmd_port_close_aggregate_data_links(graph_info_ptr,
                                                               cont_cmd_ctrl_ptr->cmd_params.link_close_params
                                                                  .mod_data_link_cfg_list_ptr,
                                                               gm_cmd_curr_sg_id,
                                                               &cont_cached_cfg_ptr->graph_mgmt_params
                                                                   .mod_data_link_cfg_list_ptr,
                                                               &cont_cached_cfg_ptr->graph_mgmt_params.num_data_links)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to aggr data link close params for CONT_ID[0x%lX] "
                "cmd_opcode[0x%08lx]",
                container_node_ptr->container_id,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      cache_cont = TRUE;
   }

   if (cont_cmd_ctrl_ptr->cmd_params.link_close_params.num_ctrl_links)
   {
      if (AR_EOK !=
          (result = apm_gm_cmd_port_close_aggregate_ctrl_links(graph_info_ptr,
                                                               cont_cmd_ctrl_ptr->cmd_params.link_close_params
                                                                  .mod_ctrl_link_cfg_list_ptr,
                                                               gm_cmd_curr_sg_id,
                                                               &cont_cached_cfg_ptr->graph_mgmt_params
                                                                   .mod_ctrl_link_cfg_list_ptr,
                                                               &cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to aggr ctrl link close params for CONT_ID[0x%lX] "
                "cmd_opcode[0x%08lx]",
                container_node_ptr->container_id,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      cache_cont = TRUE;
   }

   if (cache_cont)
   {
      /** Data and control links are always acyclic from APM's
       *  perspective for close command even if they are cyclic within
       *  the container */

      list_pptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_OBJ_TYPE_ACYCLIC].list_ptr;

      num_list_nodes_ptr =
         &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_OBJ_TYPE_ACYCLIC].num_nodes;

      if (AR_EOK != (result = apm_db_search_and_add_node_to_list(list_pptr, container_node_ptr, num_list_nodes_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to cache CONT_ID[0x%lX] for port/link mgmt, "
                "cmd_opcode[0x%08lx]",
                container_node_ptr->container_id,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }
   }

#if 0
   /** If at least 1 input, output or control port connection is
    *  present, add this container to pending list of containers */
   if (AR_EOK !=
       (result =
           apm_check_and_cache_cont_for_port_link_mgmt(apm_cmd_ctrl_ptr, container_node_ptr, cont_cached_cfg_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gm_cmd_port_close_aggr_cont_port_all: Failed to cache container for port/link mgmt, "
             "cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);
   }
#endif

   return result;
}

ar_result_t apm_populate_cont_port_list_per_port_type(spf_list_node_t *cached_port_hdl_list_ptr,
                                                      spf_handle_t **  port_handle_list_pptr)
{
   ar_result_t             result  = AR_EOK;
   uint32_t                arr_idx = 0;
   spf_list_node_t *       curr_port_conn_node_ptr;
   spf_module_port_conn_t *module_port_conn_info_ptr;

   /** Get the pointer to start of input port handle list */
   curr_port_conn_node_ptr = cached_port_hdl_list_ptr;

   /** Iterate over input port handle list */
   while (curr_port_conn_node_ptr)
   {
      /** Get the pointer to list node object which is port
       *  connection info container source and destination port
       *  handles */
      module_port_conn_info_ptr = (spf_module_port_conn_t *)curr_port_conn_node_ptr->obj_ptr;

      /** Populate the port handle */
      port_handle_list_pptr[arr_idx++] = module_port_conn_info_ptr->self_mod_port_hdl.port_ctx_hdl;

      /** Get the next node for input port connections */
      curr_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_populate_cont_port_list(apm_cont_cmd_ctrl_t *      cont_cmd_ctrl_ptr,
                                        spf_cntr_port_info_list_t *cont_port_hdl_list_ptr,
                                        uint8_t *                  msg_payload_start_ptr)
{
   ar_result_t            result        = AR_EOK;
   spf_list_node_t *      curr_node_ptr = NULL;
   uint32_t               arr_idx       = 0;
   apm_cached_cont_list_t cont_list_type;
   uint32_t               aggr_num_elem = 0;
   apm_cmd_ctrl_t *       cmd_ctrl_ptr;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_list_t(*cached_cont_list_pptr)[PORT_TYPE_MAX];

   /** Get the pointer to container's cached cmd config params   */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   /** Get the pointer to APM command control object mapped to
    *  container's current cmd ctrl object in process */
   cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   cached_cont_list_pptr = cont_cached_cfg_ptr->graph_mgmt_params.cont_ports;

   cont_list_type = apm_get_curr_active_cont_list_type(cmd_ctrl_ptr);

   /** Populate number of input port handles */
   cont_port_hdl_list_ptr->num_ip_port_handle = cached_cont_list_pptr[cont_list_type][PORT_TYPE_DATA_IP].num_nodes;

   /** Populate number of output port handles */
   cont_port_hdl_list_ptr->num_op_port_handle = cached_cont_list_pptr[cont_list_type][PORT_TYPE_DATA_OP].num_nodes;

   /** Populate number of control port handles */
   cont_port_hdl_list_ptr->num_ctrl_port_handle = cached_cont_list_pptr[cont_list_type][PORT_TYPE_CTRL_IO].num_nodes;

   /** Populate number of data links */
   cont_port_hdl_list_ptr->num_data_links = cont_cached_cfg_ptr->graph_mgmt_params.num_data_links;

   /** Populate number of control links */
   cont_port_hdl_list_ptr->num_ctrl_links = cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links;

   /** Configure the start of array of pointers to store the input port handles */
   cont_port_hdl_list_ptr->ip_port_handle_list_pptr = (spf_handle_t **)msg_payload_start_ptr;

   aggr_num_elem = cont_port_hdl_list_ptr->num_ip_port_handle;

   /** Configure the start of array of pointers to store the output port handles */
   cont_port_hdl_list_ptr->op_port_handle_list_pptr =
      (spf_handle_t **)(msg_payload_start_ptr + (SIZE_OF_PTR() * aggr_num_elem));

   aggr_num_elem += cont_port_hdl_list_ptr->num_op_port_handle;

   /** Configure the start of array of pointers to store the control port handles */
   cont_port_hdl_list_ptr->ctrl_port_handle_list_pptr =
      (spf_handle_t **)(msg_payload_start_ptr + (SIZE_OF_PTR() * (aggr_num_elem)));

   aggr_num_elem += cont_port_hdl_list_ptr->num_ctrl_port_handle;

   /** Configure the start of array of pointers to store the data links */
   cont_port_hdl_list_ptr->data_link_list_pptr =
      (apm_module_conn_cfg_t **)(msg_payload_start_ptr + (SIZE_OF_PTR() * (aggr_num_elem)));

   aggr_num_elem += cont_port_hdl_list_ptr->num_data_links;

   /** Configure the start of array of pointers to store the Control links */
   cont_port_hdl_list_ptr->ctrl_link_list_pptr =
      (apm_module_ctrl_link_cfg_t **)(msg_payload_start_ptr + (SIZE_OF_PTR() * (aggr_num_elem)));

   /** Iterate over the list of port connection list per
    *  sub-graph and populate the input, output and control port handles in message payload */

   /** Populate the input port handles */
   apm_populate_cont_port_list_per_port_type(cached_cont_list_pptr[cont_list_type][PORT_TYPE_DATA_IP].list_ptr,
                                             cont_port_hdl_list_ptr->ip_port_handle_list_pptr);

   /** Populate the output port handles */
   apm_populate_cont_port_list_per_port_type(cached_cont_list_pptr[cont_list_type][PORT_TYPE_DATA_OP].list_ptr,
                                             cont_port_hdl_list_ptr->op_port_handle_list_pptr);

   /** Populate the ctrl port handles */
   apm_populate_cont_port_list_per_port_type(cached_cont_list_pptr[cont_list_type][PORT_TYPE_CTRL_IO].list_ptr,
                                             cont_port_hdl_list_ptr->ctrl_port_handle_list_pptr);

   /** Populate the data link pointers */
   arr_idx       = 0;
   curr_node_ptr = cont_cached_cfg_ptr->graph_mgmt_params.mod_data_link_cfg_list_ptr;
   while (curr_node_ptr)
   {
      cont_port_hdl_list_ptr->data_link_list_pptr[arr_idx++] = (apm_module_conn_cfg_t *)curr_node_ptr->obj_ptr;
      curr_node_ptr                                          = curr_node_ptr->next_ptr;
   }
   msg_payload_start_ptr += (SIZE_OF_PTR() * cont_cached_cfg_ptr->graph_mgmt_params.num_data_links);

   /** Populate the Ctrl link pointers */
   arr_idx       = 0;
   curr_node_ptr = cont_cached_cfg_ptr->graph_mgmt_params.mod_ctrl_link_cfg_list_ptr;
   while (curr_node_ptr)
   {
      cont_port_hdl_list_ptr->ctrl_link_list_pptr[arr_idx++] = (apm_module_ctrl_link_cfg_t *)curr_node_ptr->obj_ptr;
      curr_node_ptr                                          = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_populate_msg_set_get_cfg(apm_cont_cached_cfg_t *cont_cached_cfg_ptr,
                                         spf_msg_t *            cont_msg_ptr,
                                         uint32_t               container_id)
{
   ar_result_t                   result = AR_EOK;
   spf_msg_header_t *            msg_header_ptr;
   spf_list_node_t *             curr_node_ptr;
   uint32_t                      arr_idx = 0;
   apm_module_param_data_t **    param_data_pptr, *param_data_ptr;
   spf_msg_cmd_param_data_cfg_t *msg_set_cfg_ptr;
   uint8_t *                     cfg_data_start_ptr;
   apm_cont_set_get_cfg_hdr_t *  set_get_cfg_hdr_ptr;
   apm_ext_utils_t *             ext_utils_ptr;

   msg_header_ptr = (spf_msg_header_t *)cont_msg_ptr->payload_ptr;

   msg_set_cfg_ptr = (spf_msg_cmd_param_data_cfg_t *)&msg_header_ptr->payload_start;

   /** Get ext utils vtbl ptr  */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /** Populate the total number of parameters being set/get.
    *  Includes the module + container related param ID's */
   msg_set_cfg_ptr->num_param_id_cfg =
      cont_cached_cfg_ptr->module_cfg_params.num_mod_param_cfg + cont_cached_cfg_ptr->cont_cfg_params.num_cfg_params;

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->module_cfg_params.param_data_list_ptr;

   /** Set the pointer to start of the array of pointers for
    *  param ID data payload */
   cfg_data_start_ptr = (uint8_t *)msg_set_cfg_ptr + sizeof(spf_msg_cmd_param_data_cfg_t);

   /** Init the array of pointers in the message buffers */
   msg_set_cfg_ptr->param_data_pptr = (void **)cfg_data_start_ptr;

   /** Get the local pointer to array of pointers in message
    *  buffer */
   param_data_pptr = (apm_module_param_data_t **)msg_set_cfg_ptr->param_data_pptr;

   /** Populate the module connection list config pointers */
   while (curr_node_ptr)
   {
      param_data_ptr = (apm_module_param_data_t *)curr_node_ptr->obj_ptr;

      param_data_pptr[arr_idx++] = param_data_ptr;

      /** Iterate over the param data list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   /** Populate the container related param ID's if, present */

   /** Advance the payload pointer to the end of array of
    *  pointers */
   cfg_data_start_ptr += (msg_set_cfg_ptr->num_param_id_cfg * SIZE_OF_PTR());

   /** Get the pointer to list of module list ptr */
   curr_node_ptr = cont_cached_cfg_ptr->cont_cfg_params.param_data_list_ptr;

   /** Iterate over the list of cached container configuration and
    *  populate them in the allocated payload */
   while (curr_node_ptr)
   {
      set_get_cfg_hdr_ptr = (apm_cont_set_get_cfg_hdr_t *)curr_node_ptr->obj_ptr;

      switch (set_get_cfg_hdr_ptr->param_id)
      {
         case CNTR_PARAM_ID_PATH_DELAY_CFG:
         case CNTR_PARAM_ID_CFG_SRC_MOD_DELAY_LIST:
         case CNTR_PARAM_ID_DESTROY_SRC_MOD_DELAY_LIST:
         case CNTR_PARAM_ID_PATH_DESTROY:
         {
            param_data_ptr = (apm_module_param_data_t *)cfg_data_start_ptr;

            /** Populate the container params */
            if (ext_utils_ptr->data_path_vtbl_ptr &&
                ext_utils_ptr->data_path_vtbl_ptr->apm_populate_cntr_path_delay_params_fptr)
            {
               if (AR_EOK !=
                   (result =
                       ext_utils_ptr->data_path_vtbl_ptr->apm_populate_cntr_path_delay_params_fptr(container_id,
                                                                                                   set_get_cfg_hdr_ptr,
                                                                                                   param_data_ptr)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_msg_set_get_cfg(): Failed to populate payload for param ID[0x%lx], "
                         "CONT_ID[0x%lX]",
                         set_get_cfg_hdr_ptr->param_id,
                         container_id);

                  return result;
               }
            }

            /** Populate the array of pointer  */
            param_data_pptr[arr_idx++] = param_data_ptr;

            /** Increment the message payload pointer to next PID header */
            cfg_data_start_ptr += (sizeof(apm_module_param_data_t) + param_data_ptr->param_size);

            break;
         }
         case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
         {
            param_data_ptr = (apm_module_param_data_t *)cfg_data_start_ptr;

            /** Populate the container params */
            if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
                ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_populate_cntr_param_payload_size_fptr)
            {
               if (AR_EOK !=
                   (result =
                       ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_populate_cntr_param_payload_size_fptr(container_id,
                                                                                                    set_get_cfg_hdr_ptr,
                                                                                                    param_data_ptr)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_msg_set_get_cfg(): Failed to populate payload for param ID[0x%lx], "
                         "CONT_ID[0x%lX]",
                         set_get_cfg_hdr_ptr->param_id,
                         container_id);

                  return result;
               }
            }

            /** Populate the array of pointer  */
            param_data_pptr[arr_idx++] = param_data_ptr;

            /** Increment the message payload pointer to next PID header */
            cfg_data_start_ptr += (sizeof(apm_module_param_data_t) + param_data_ptr->param_size);

            break;
         }
         case CNTR_PARAM_ID_OFFLOAD_VOICE_SESSION_INFO:
         {
            param_data_ptr = (apm_module_param_data_t *)cfg_data_start_ptr;

            /** Populate the container params */
            if (ext_utils_ptr->set_get_cfg_vtbl_ptr &&
                ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_populate_cntr_param_payload_size_fptr)
            {
               if (AR_EOK !=
                   (result =
                       ext_utils_ptr->set_get_cfg_vtbl_ptr->apm_populate_cntr_param_payload_size_fptr(container_id,
                                                                                                    set_get_cfg_hdr_ptr,
                                                                                                    param_data_ptr)))
               {
                  AR_MSG(DBG_ERROR_PRIO,
                         "apm_populate_msg_set_get_cfg(): Failed to populate payload for param ID[0x%lx], "
                         "CONT_ID[0x%lX]",
                         set_get_cfg_hdr_ptr->param_id,
                         container_id);

                  return result;
               }
            }

            /** Populate the array of pointer  */
            param_data_pptr[arr_idx++] = param_data_ptr;

            /** Increment the message payload pointer to next PID header */
            cfg_data_start_ptr += (sizeof(apm_module_param_data_t) + param_data_ptr->param_size);

            break;
         }
         default:
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_populate_msg_set_get_cfg(): Un-supported param ID[0x%lx], CONT_ID[0x%lX]",
                   set_get_cfg_hdr_ptr->param_id,
                   container_id);

            break;
         }

      } /** End of switch(container param ID ) */

      /** Advance to next node in the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_populate_msg_graph_mgmt(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                        spf_msg_t *          cont_msg_ptr,
                                        uint32_t             msg_opcode)
{
   ar_result_t               result = AR_EOK;
   spf_msg_header_t *        msg_header_ptr;
   spf_list_node_t *         curr_node_ptr;
   uint32_t                  arr_idx = 0;
   spf_msg_cmd_graph_mgmt_t *graph_mgmt_msg_ptr;
   uint32_t *                sg_id_list_ptr;
   apm_sub_graph_t *         sub_graph_node_ptr;
   uint8_t *                 msg_payload_start_ptr;
   apm_cached_cont_list_t    cont_list_type;
   apm_cmd_ctrl_t *          cmd_ctrl_ptr;

   /** Get the pointer to container's cached cmd config params   */
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;

   /** Get the pointer to APM command control object mapped to
    *  container's current cmd ctrl object in process */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   msg_header_ptr = (spf_msg_header_t *)cont_msg_ptr->payload_ptr;

   /** Get the start pointer to graph mgmt msg header */
   graph_mgmt_msg_ptr = (spf_msg_cmd_graph_mgmt_t *)&msg_header_ptr->payload_start;

   /** Get the start pointer to graph mgmt msg payload */
   msg_payload_start_ptr = (uint8_t *)(graph_mgmt_msg_ptr + 1);

   cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   cont_list_type = apm_get_curr_active_cont_list_type(cmd_ctrl_ptr);

   /** Clear the msg payload */
   memset(graph_mgmt_msg_ptr, 0, sizeof(spf_msg_cmd_graph_mgmt_t));

   /** If the sub-graph list is available, populate it */
   if ((APM_OBJ_TYPE_ACYCLIC == cont_list_type) && cont_cached_cfg_ptr->graph_mgmt_params.num_sub_graphs)
   {
      graph_mgmt_msg_ptr->sg_id_list.num_sub_graph = cont_cached_cfg_ptr->graph_mgmt_params.num_sub_graphs;

      /** Get the pointer to list of cached sub-graph cfg */
      curr_node_ptr = cont_cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr;

      /** Populate the SG ID list start pointer in the msg payload */
      graph_mgmt_msg_ptr->sg_id_list.sg_id_list_ptr = (uint32_t *)msg_payload_start_ptr;

      sg_id_list_ptr = graph_mgmt_msg_ptr->sg_id_list.sg_id_list_ptr;

      /** Populate the sub-graph ID list */
      while (curr_node_ptr)
      {
         sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

         sg_id_list_ptr[arr_idx++] = sub_graph_node_ptr->sub_graph_id;

         /** Iterate over the param data list */
         curr_node_ptr = curr_node_ptr->next_ptr;
      }

      /** Increment the payload start pointer by number of graphs,
       *  if present  */
      msg_payload_start_ptr += (sizeof(uint32_t) * cont_cached_cfg_ptr->graph_mgmt_params.num_sub_graphs);
   }

   /** If the container IO Port list is available, populate it */
   if (cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_IP].num_nodes ||
       cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_OP].num_nodes ||
       cont_cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_CTRL_IO].num_nodes ||
       cont_cached_cfg_ptr->graph_mgmt_params.num_data_links || cont_cached_cfg_ptr->graph_mgmt_params.num_ctrl_links)
   {
      /** Populate the port handles */
      apm_populate_cont_port_list(cont_cmd_ctrl_ptr, &graph_mgmt_msg_ptr->cntr_port_hdl_list, msg_payload_start_ptr);
   }

   return result;
}

ar_result_t apm_populate_msg_payload(apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                     spf_msg_t *          cont_msg_ptr,
                                     uint32_t             container_id)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;

   /** Get the pointer to container's cached cmd config params   */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   /** Todo: converate into a table */
   switch (cont_msg_ptr->msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_OPEN:
      {
         result = apm_populate_msg_graph_open(cont_cached_cfg_ptr, cont_msg_ptr);
         break;
      }
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_FLUSH:
      case SPF_MSG_CMD_GRAPH_CLOSE:
      case SPF_MSG_CMD_GRAPH_DISCONNECT:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         result = apm_populate_msg_graph_mgmt(cont_cmd_ctrl_ptr, cont_msg_ptr, cont_msg_ptr->msg_opcode);
         break;
      }
      case SPF_MSG_CMD_SET_CFG:
      case SPF_MSG_CMD_GET_CFG:
      case SPF_MSG_CMD_REGISTER_CFG:
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         result = apm_populate_msg_set_get_cfg(cont_cached_cfg_ptr, cont_msg_ptr, container_id);
         break;
      }
      case SPF_MSG_CMD_DESTROY_CONTAINER:
      {
         /** No payload required for destroy */
         break;
      }
      default:
      {
         result = AR_EBADPARAM;
         break;
      }
   }

   return result;
}

static ar_result_t apm_create_container_msg(spf_handle_t *       apm_handle_ptr,
                                            uint32_t             msg_opcode,
                                            apm_container_t *    container_node_ptr,
                                            apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                            spf_msg_t *          cont_msg_ptr)
{
   ar_result_t     result = AR_EOK;
   uint32_t        msg_payload_size;
   uint32_t        cont_id;
   spf_msg_t       cont_msg;
   spf_msg_token_t token;

   /** Get the container ID   */
   cont_id = container_node_ptr->container_id;
   /** Get overall payload message size */
   if (AR_EOK != (result = apm_compute_msg_payload_size(cont_cmd_ctrl_ptr, msg_opcode, &msg_payload_size, cont_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_container_msg(): FAILED to get msg payload size, opcode: 0x%lx, CONT_ID[0x%lX], result: 0x%lx",
             msg_opcode,
             cont_id,
             result);

      return result;
   }

   /** If message payload size is empty, then abort further
    *  processing */
   if (!msg_payload_size)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_container_msg(): Invalid msg payload size, opcode: 0x%lx, CONT_ID[0x%lX]",
             msg_opcode,
             cont_id);

      return AR_EFAILED;
   }

   token.token_ptr = cont_cmd_ctrl_ptr;

   /** Send the init message to container */
   if (AR_EOK != (result = spf_msg_create_msg(&cont_msg,                        /** MSG Ptr */
                                              &msg_payload_size,                /** MSG payload size */
                                              msg_opcode,                       /** MSG opcode */
                                              apm_handle_ptr,                   /** APM response handle */
                                              &token,                           /** MSG Token */
                                              container_node_ptr->cont_hdl_ptr, /** Destination handle */
                                              APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_container_msg(): FAILED to create msg payload, opcode: 0x%lx, CONT_ID[0x%lX], result: 0x%lx",
             msg_opcode,
             cont_id,
             result);

      return result;
   }

   /** Populate message payload */
   if (AR_EOK != (result = apm_populate_msg_payload(cont_cmd_ctrl_ptr, &cont_msg, cont_id)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_create_container_msg(): FAILED to populate msg payload, opcode: 0x%lx, CONT_ID[0x%lX], result: 0x%lx",
             msg_opcode,
             cont_id,
             result);

      spf_msg_return_msg(&cont_msg);
   }

   /** Populate the return pointer  */
   *cont_msg_ptr = cont_msg;

   return result;
}

ar_result_t apm_send_msg_to_containers(spf_handle_t *apm_handle_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   spf_msg_t              cont_msg;
   spf_list_node_t *      curr_node_ptr;
   apm_container_t *      container_node_ptr;
   uint32_t               msg_opcode;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   uint32_t               cont_id;

   /** Get the pointer to container list */
   curr_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Get the pointer to list of container message opcode */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   /** Iterate over the container list */
   while (curr_node_ptr)
   {
      /** Get the pointer to container node */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Get container ID  */
      cont_id = container_node_ptr->container_id;

      /** Get the pointer to current container command control
       *  pointer */
      if (AR_EOK != (result = apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_send_msg_to_containers(): Failed to get container's cmd ctrl obj, CONT_ID[0x%lX], result: %lu",
                cont_id,
                result);

         return result;
      }

      /** Get the container message opcode */
      msg_opcode = cont_msg_opcode_ptr->opcode_list[cont_msg_opcode_ptr->curr_opcode_idx];

      /** Else compose the message for the container */
      if (!cont_cmd_ctrl_ptr->rsp_ctrl.reuse_rsp_msg_buf)
      {
         if (AR_EOK != (result = apm_create_container_msg(apm_handle_ptr,
                                                          msg_opcode,
                                                          container_node_ptr,
                                                          cont_cmd_ctrl_ptr,
                                                          &cont_msg)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_send_msg_to_containers(): Failed to create container msg,  msg_opcode[0x%lX], CONT_ID[0x%lX], "
                   "result: %lu",
                   msg_opcode,
                   cont_id,
                   result);

            return result;
         }
      }
      else /** Reuse current response message buffer */
      {
         cont_msg = cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg;

         /** Update current msg opcode in response payload  */
         cont_msg.msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);
      }

      /** Push the message packet to container command Q */
      if (AR_EOK != (result = spf_msg_send_cmd(&cont_msg, container_node_ptr->cont_hdl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO, "Failed to send msg  to cont cmdQ, CONT_ID[0x%lX], result: 0x%lx", cont_id, result);

         return result;
      }

      /** Set up response control for this container */
      apm_set_cont_cmd_rsp_pending(&cont_cmd_ctrl_ptr->rsp_ctrl, msg_opcode);

      AR_MSG(DBG_MED_PRIO, "Sent msg_opcode[0x%lX] to CONT_ID[0x%lX]", msg_opcode, cont_id);

      /** Increment the number of commands issues */
      apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued++;

      /** If at least 1 command issued to container, set the
       *  command response pending status */
      apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = CMD_RSP_PENDING;

      /** Iterate over the list */
      curr_node_ptr = curr_node_ptr->next_ptr;

   } /** While container list */

   return result;
}

static bool_t apm_gm_cmd_check_process_sg(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_sub_graph_t *sub_graph_node_ptr)
{
   uint32_t              cont_msg_opcode;
   apm_sub_graph_state_t sg_state;
   bool_t                process_sg = FALSE;

   sg_state = sub_graph_node_ptr->state;

   cont_msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);

   switch (cont_msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_PREPARE:
      {
         if (APM_SG_STATE_STOPPED == sg_state)
         {
            process_sg = TRUE;
         }
         break;
      }
      case SPF_MSG_CMD_GRAPH_START:
      {
         if ((APM_SG_STATE_PREPARED == sg_state) || (APM_SG_STATE_SUSPENDED == sg_state))
         {
            process_sg = TRUE;
         }
         break;
      }
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_FLUSH:
      {
         if ((APM_SG_STATE_STARTED == sg_state) || (APM_SG_STATE_PREPARED == sg_state) ||
             (APM_SG_STATE_SUSPENDED == sg_state))
         {
            process_sg = TRUE;
         }
         break;
      }
      case SPF_MSG_CMD_GRAPH_DISCONNECT:
      {
         if ((APM_SG_STATE_STOPPED == sg_state) || (APM_SG_STATE_PREPARED == sg_state))
         {
            process_sg = TRUE;
         }
         break;
      }
      case SPF_MSG_CMD_GRAPH_CLOSE:
      {
         if ((APM_SG_STATE_STOPPED == sg_state) || (APM_SG_STATE_PREPARED == sg_state))
         {
            process_sg = TRUE;
         }
         break;
      }
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         switch (apm_cmd_ctrl_ptr->cmd_opcode)
         {
            case APM_CMD_GRAPH_SUSPEND:
            {
               if (APM_SG_STATE_STARTED == sg_state)
               {
                  process_sg = TRUE;
               }

               break;
            }
            case APM_CMD_GRAPH_PREPARE:
            case APM_CMD_GRAPH_START:
            {
               if (APM_SG_STATE_STOPPED == sg_state)
               {
                  process_sg = TRUE;
               }

               break;
            }
            default:
            {
               break;
            }

         } /** End of switch (apm_cmd_ctrl_ptr->cmd_opcode) */

         break;
      }
      default:
      {
         break;
      }

   } /** End of switch (cont_msg_opcode) */

   return process_sg;
}

static ar_result_t apm_aggregate_sub_graph_list(apm_cmd_ctrl_t * apm_cmd_ctrl_ptr,
                                                apm_sub_graph_t *sub_graph_node_ptr,
                                                apm_container_t *container_node_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   spf_list_node_t **     list_pptr;
   uint32_t *             num_list_nodes_ptr;
   uint32_t               cont_msg_opcode;

   /** Get the current container message opcode being processed */
   cont_msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);

   /** Get the pointer to current container command control
    *  pointer */
   apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to cached configuration for this
    *  container */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   /** Add this sub-graph to container cached config */
   if (AR_EOK != (result = apm_db_add_node_to_list(&cont_cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr,
                                                   sub_graph_node_ptr,
                                                   &cont_cached_cfg_ptr->graph_mgmt_params.num_sub_graphs)))
   {
      return result;
   }

   /** Get the pointer to the list of containers cached for the
    *  current graph management command */
   list_pptr =
      &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_CONT_LIST_WITH_ACYCLIC_LINK].list_ptr;

   /** Get the pointer to the number of containers cached for the
    *  current graph management command */
   num_list_nodes_ptr =
      &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_CONT_LIST_WITH_ACYCLIC_LINK].num_nodes;

   /** Cache this container under the graph management command
    *  control object */
   if (AR_EOK != (result = apm_db_search_and_add_node_to_list(list_pptr, container_node_ptr, num_list_nodes_ptr)))
   {
      return result;
   }

   AR_MSG(DBG_MED_PRIO,
          "apm_aggregate_sub_graph_list(): Cached SG_ID[0x%lX], CONT_ID[0x%lX], cont_msg_opcode[0x%lX] "
          "cmd_opcode[0x%08lx]",
          sub_graph_node_ptr->sub_graph_id,
          container_node_ptr->container_id,
          cont_msg_opcode,
          apm_cmd_ctrl_ptr->cmd_opcode);

   return result;
}

static ar_result_t apm_gm_cmd_cache_sg_id_params(apm_container_t * container_node_ptr,
                                                 apm_graph_info_t *graph_info_ptr,
                                                 apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr)
{
   ar_result_t                result = AR_EOK;
   spf_list_node_t *          curr_cont_sg_node_ptr;
   spf_list_node_t *          curr_gm_cmd_sg_node_ptr;
   apm_sub_graph_t *          cont_sg_obj_ptr;
   apm_sub_graph_t *          gm_cmd_sg_obj_ptr;
   apm_graph_mgmt_cmd_ctrl_t *gm_cmd_ctrl_ptr;

   /** Get the pointer APM graph management cmd control */
   gm_cmd_ctrl_ptr = &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl;
   /** Get the pointer to list of sub-graphs within this
    *  container */
   curr_cont_sg_node_ptr = container_node_ptr->sub_graph_list_ptr;

   /** Iterate over the list of sub-graphs for this container */
   while (curr_cont_sg_node_ptr)
   {
      /** Get the pointer to list of sub-graphs to process as part
       *  of GM command */
      curr_gm_cmd_sg_node_ptr = gm_cmd_ctrl_ptr->sg_list.reg_sg_list_ptr;

      /** Iterate over the graph mgmt command sub-graph list to
       *  process  */
      while (curr_gm_cmd_sg_node_ptr)
      {
         /** Get the sub-graph node from the GM command list  */
         gm_cmd_sg_obj_ptr = (apm_sub_graph_t *)curr_gm_cmd_sg_node_ptr->obj_ptr;

         /** Check if the current sub-graph is in the right state to
          *  be processed for the current container message opcode. If
          *  not, skip this sub-graph and move on to next sub-graph in
          *  the list */

         if (!apm_gm_cmd_check_process_sg(apm_cmd_ctrl_ptr, gm_cmd_sg_obj_ptr))
         {
            AR_MSG(DBG_MED_PRIO,
                   "apm_gm_cmd_cache_sg_id_params(): CONT_ID[0x%lX]: SG_ID[0x%lX] skipped for  "
                   "cont_msg_opcode[0x%lX], "
                   "cmd_opcode[0x%08lx]",
                   container_node_ptr->container_id,
                   gm_cmd_sg_obj_ptr->sub_graph_id,
                   apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr),
                   apm_cmd_ctrl_ptr->cmd_opcode);

            /** Advance to next sg node in the gm command */
            curr_gm_cmd_sg_node_ptr = curr_gm_cmd_sg_node_ptr->next_ptr;

            continue;
         }

         /** Get the sub-graph node from the current container's
          *  sub-graph list */
         cont_sg_obj_ptr = (apm_sub_graph_t *)curr_cont_sg_node_ptr->obj_ptr;

         /** If the sub-graph ID matches, cache it for further
          *  processing */

         if (apm_gm_cmd_cache_sg_id(apm_cmd_ctrl_ptr) &&
             (gm_cmd_sg_obj_ptr->sub_graph_id == cont_sg_obj_ptr->sub_graph_id))
         {
            /** If the sub-graph ID matches, then cache it for sending it
             *  to containers */
            if (AR_EOK !=
                (result = apm_aggregate_sub_graph_list(apm_cmd_ctrl_ptr, gm_cmd_sg_obj_ptr, container_node_ptr)))
            {
               return result;
            }

            /** Add this sub-graph to the list of sub-graphs being
             *  processed for the current iteration of the multi-step GM
             *  command  */
            if (AR_EOK !=
                (result = apm_db_search_and_add_node_to_list(&gm_cmd_ctrl_ptr->sg_proc_info.processed_sg_list_ptr,
                                                             gm_cmd_sg_obj_ptr,
                                                             &gm_cmd_ctrl_ptr->sg_proc_info.num_processed_sg)))
            {
               return result;
            }
         }

         /** Populate container port connection list, where the current
          *  graph management sub-graph ID is present as the peer SG
          *  ID */
         if (AR_EOK != (result = apm_graph_mgmt_aggregate_cont_port_all(apm_cmd_ctrl_ptr,
                                                                        container_node_ptr,
                                                                        graph_info_ptr,
                                                                        gm_cmd_sg_obj_ptr->sub_graph_id)))
         {
            return result;
         }

         /** Advance to next sg node in the gm command */
         curr_gm_cmd_sg_node_ptr = curr_gm_cmd_sg_node_ptr->next_ptr;

      } /** End of while (gm command sg id list) */

      /** Advance to next sg node for the current container */
      curr_cont_sg_node_ptr = curr_cont_sg_node_ptr->next_ptr;

   } /** End of while(cont sg list)*/

   return result;
}

static ar_result_t apm_gm_cmd_cache_port_close_params(apm_container_t * container_node_ptr,
                                                      apm_graph_info_t *graph_info_ptr,
                                                      apm_cmd_ctrl_t *  apm_cmd_ctrl_ptr)
{
   ar_result_t          result = AR_EOK;
   spf_list_node_t *    curr_sg_node_ptr;
   apm_sub_graph_t *    sg_obj_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;
   uint32_t             cont_msg_opcode;

   /** Get the current container message opcode being processed */
   cont_msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);

   /** Get container's cmd ctrl obj for current APM command in
    *  process */
   apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to list of sub-graphs impacted as part of
    *  the data/control link close for current container */
   curr_sg_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;

   while (curr_sg_node_ptr)
   {
      sg_obj_ptr = (apm_sub_graph_t *)curr_sg_node_ptr->obj_ptr;

      /** Check if the current sub-graph is in the right state to
       *  be processed for the current container message opcode. If
       *  not, skip this sub-graph and move on to next sub-graph in
       *  the list */
      if (!apm_gm_cmd_check_process_sg(apm_cmd_ctrl_ptr, sg_obj_ptr) && (SPF_MSG_CMD_GRAPH_STOP == cont_msg_opcode))
      {
         AR_MSG(DBG_MED_PRIO,
                "apm_gm_cmd_cache_port_close_params(): SG_ID[0x%lX], skipped for  cont_msg_opcode[0x%lX], "
                "cmd_opcode[0x%08lx]",
                sg_obj_ptr->sub_graph_id,
                cont_msg_opcode,
                apm_cmd_ctrl_ptr->cmd_opcode);

         /** Advance to next sg node in the gm command */
         curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

         continue;
      }

      /** Populate container port connection list, where the current
       *  graph management sub-graph ID is present as the peer SG
       *  ID */
      if (AR_EOK != (result = apm_gm_cmd_port_close_aggr_cont_port_all(apm_cmd_ctrl_ptr,
                                                                       container_node_ptr,
                                                                       graph_info_ptr,
                                                                       sg_obj_ptr->sub_graph_id)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_cache_port_close_params(): Failed to aggr port close params CONT_ID[0x%lX], SG_ID[0x%lX], "
                "cont_msg_opcode[0x%lX], "
                "cmd_opcode[0x%08lx]",
                container_node_ptr->container_id,
                sg_obj_ptr->sub_graph_id,
                cont_msg_opcode,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      /** Advance to next node in the list */
      curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

   } /** End of while (cont port) */

   return result;
}

ar_result_t apm_cache_cont_sg_and_port_list(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   spf_list_node_t *      curr_cont_node_ptr;
   uint32_t               curr_cont_msg_opcode;
   apm_container_t *      container_node_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;

   /** Get the current container message opcode being processed */
   curr_cont_msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);
   /** Get the current container node being processed */
   if (NULL == (curr_cont_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_cache_cont_sg_and_port_list(), Curr container ptr is NULL, cont_msg_opcode[0x%lX], "
             "cmd_opcode[0x%08lx]",
             curr_cont_msg_opcode,
             apm_cmd_ctrl_ptr->cmd_opcode);

      return result;
   }

   /** Get the container object */
   container_node_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

   /** Iterate over the list of all the disjoint container
    *  graphs and for each container cache the list of sub-graphs
    *  and port handles to be sent to container that are impacted
    *  as part of the graph management command */

   do
   {
      /** Aggregate the container parameters affected due to
       *  sub-graph list received directly as part of the graph
       *  mgmt command */
      if (AR_EOK != (result = apm_gm_cmd_cache_sg_id_params(container_node_ptr, graph_info_ptr, apm_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cache_cont_sg_and_port_list(), Failed to cache sg_id and port hdl, cont_msg_opcode[0x%lX], "
                "cmd_opcode[0x%08lx]",
                curr_cont_msg_opcode,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      if (APM_CMD_GRAPH_CLOSE == apm_cmd_ctrl_ptr->cmd_opcode)
      {
         /** Aggregate the container port handles to be closed
          *  received as part of the GRAPH CLOSE command */
         if (AR_EOK !=
             (result = apm_gm_cmd_cache_port_close_params(container_node_ptr, graph_info_ptr, apm_cmd_ctrl_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_cache_cont_sg_and_port_list(), Failed to cache port hdl for close, cont_msg_opcode[0x%lX], "
                   "cmd_opcode[0x%08lx]",
                   curr_cont_msg_opcode,
                   apm_cmd_ctrl_ptr->cmd_opcode);

            return result;
         }
      }
      else if (APM_CMD_GRAPH_OPEN == apm_cmd_ctrl_ptr->cmd_opcode)
      {
         if (AR_EOK != (result = apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr)))
         {
            return result;
         }

         /** Get the pointer to cached cfg params obj  */
         cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

         /** If at least 1 input, output or control port connection is
          *  present, add this container to pending list of containers */
         if (AR_EOK != (result = apm_check_and_cache_cont_for_port_link_mgmt(apm_cmd_ctrl_ptr,
                                                                             container_node_ptr,
                                                                             cont_cached_cfg_ptr)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_cache_cont_sg_and_port_list: Failed to cache container for port/link mgmt, "
                   "cmd_opcode[0x%08lx]",
                   apm_cmd_ctrl_ptr->cmd_opcode);
         }
      }

      /** Get next container in sorted list of containers */
      apm_gm_cmd_get_next_cont_in_sorted_list(apm_cmd_ctrl_ptr, &container_node_ptr);

      /** Container node is NULL if reached the end of sorted
       *  container list */
      if (!container_node_ptr)
      {
         AR_MSG(DBG_MED_PRIO,
                "Reached end of sorted container list, cont_msg_opcode[0x%lX], "
                "cmd_opcode[0x%08lx],"
                "cached cont list idx[%lu]",
                curr_cont_msg_opcode,
                apm_cmd_ctrl_ptr->cmd_opcode,
                apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr));
      }

   } while (container_node_ptr);

   return result;
}

ar_result_t apm_gm_cmd_reset_sg_cont_list_proc_info(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t result                  = AR_EOK;
   bool_t      cont_cmd_params_release = FALSE;

   /** Clear the list of sub-graphs processed in the previous
    *  iteration */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.processed_sg_list_ptr)
   {
      spf_list_delete_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.processed_sg_list_ptr, TRUE);
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.num_processed_sg = 0;
   }

   /** Clear the mixed state flag */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = FALSE;

   /** Clear cached container list */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_CONT_LIST_WITH_ACYCLIC_LINK]
          .list_ptr ||
       apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[APM_CONT_LIST_WITH_CYCLIC_LINK].list_ptr)
   {
      /** Check if the regular sub-graph processing is done,
       *  released the cached container command params */
      if (apm_check_cont_msg_seq_done(apm_cmd_ctrl_ptr))
      {
         /** Clear the sub-graph list state */
         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.curr_sg_list_state = APM_SG_STATE_INVALID;

         cont_cmd_params_release = TRUE;
      }

      for (uint32_t list_idx = 0; list_idx < APM_CONT_LIST_MAX; list_idx++)
      {
         if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[list_idx].list_ptr)
         {
            /** Release any cached command params or message params to be
             *  sent to containers */
            apm_clear_container_list(apm_cmd_ctrl_ptr,
                                     &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[list_idx]
                                         .list_ptr,
                                     &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[list_idx]
                                         .num_nodes,
                                     APM_CONT_CACHED_CFG_RELEASE,
                                     cont_cmd_params_release,
                                     APM_CACHED_CONT_LIST);
         }
      }
   }

   /** Clear the container processing info struct */
   memset(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info, 0, sizeof(apm_gm_cmd_cont_proc_info_t));

   AR_MSG(DBG_MED_PRIO,
          "apm_gm_cmd_reset_sg_cont_list_proc_info(): Reset sg-cont proc info, cmd_opcode[0x%lX]",
          apm_cmd_ctrl_ptr->cmd_opcode);

   return result;
}

static ar_result_t apm_gm_cmd_is_sg_list_mixed_state(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t           result = AR_EOK;
   spf_list_node_t *     curr_node_ptr;
   apm_sub_graph_t *     curr_sub_graph_node_ptr;
   uint32_t              total_sg_to_process;
   spf_list_node_t *     sg_list[2]            = { NULL };
   uint32_t              list_idx              = 0;
   uint32_t              num_active_lists      = 0;
   apm_sub_graph_state_t overall_sg_list_state = APM_SG_STATE_INVALID;
   bool_t                mixed_state_detected  = FALSE;

   if (!apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs &&
       !apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cont_port_hdl_sg)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_gm_cmd_is_sg_list_mixed_state(): GM cmd and Link Close cmd sg lists are empty, cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);

      return AR_EFAILED;
   }

   /** Check if the mixed state detection is already set */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state)
   {
      AR_MSG(DBG_MED_PRIO,
             "GRAPH MGMT: sub-graph list mixed state already set, cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);

      return AR_EOK;
   }

   /** Get the total number of sub-graphs, direct and indirect
    *  that needs to be processed */
   total_sg_to_process = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_reg_sub_graphs +
                         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.num_cont_port_hdl_sg;

   /** If there is only 1 sub-graph then no need to reset the
    *  container cached config  */
   if (1 == total_sg_to_process)
   {
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = FALSE;

      return AR_EOK;
   }

   /** If the list of sub-graph directly received as part of
    *  graph management command store in the temp list */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr)
   {
      sg_list[list_idx] = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;

      /** Increment the list index */
      list_idx++;

      /** Increment the number of active lists */
      num_active_lists++;
   }

   /** If the list of sub-graph in-directly received as part of
    *  data/control link close command, store in the temp list */
   if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr)
   {
      sg_list[list_idx] = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.cont_port_hdl_sg_list_ptr;

      /** Increment the number of active lists */
      num_active_lists++;
   }

   /** Reset the link index again for looping below */
   list_idx = 0;

   /** Execution falls through if more than 1 sub-graphs need to
    *  be processed as part of the graph management command */

   do
   {
      /** Get the start of the list node */
      curr_node_ptr = sg_list[list_idx];

      /** Iterate over the list of sub-graphs for the current list */
      while (curr_node_ptr)
      {
         /** Get the pointer to the sub-graph object */
         curr_sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

         /** If the overall state is invalid, init with valid value */
         if (APM_SG_STATE_INVALID == overall_sg_list_state)
         {
            overall_sg_list_state = curr_sub_graph_node_ptr->state;
         }
         else if (overall_sg_list_state != curr_sub_graph_node_ptr->state)
         {
            /** If the state of the current SG does not match with the
             *  overall state, set the flag and break the loop */
            mixed_state_detected = TRUE;
            break;
         }

         /** Advance to next sub-graph in the list */
         curr_node_ptr = curr_node_ptr->next_ptr;
      }

      /** If mixed state detected, break from the external do-while
       *  loop */
      if (mixed_state_detected)
      {
         break;
      }

      /** Increment the list index */
      list_idx++;

      /** Decrement the number of active lists */
      num_active_lists--;

   } while (num_active_lists);

   /** Update the state variable */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.sg_list_mixed_state = mixed_state_detected;

   /** Print the message in case of mixed state detection */
   if (mixed_state_detected)
   {
      AR_MSG(DBG_MED_PRIO,
             "GRAPH MGMT: sub-graph list mixed state detected, cmd_opcode[0x%08lx]",
             apm_cmd_ctrl_ptr->cmd_opcode);
   }

   return result;
}

ar_result_t apm_gm_cmd_pre_processing(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_graph_info_t *graph_info_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    cmd_opcode;

   /** Get the current command opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** Reset the processing info for the sub-graph and container
    *  list processed as part of current iteration of command */
   if (!apm_is_cont_first_msg_opcode(apm_cmd_ctrl_ptr))
   {
      apm_gm_cmd_reset_sg_cont_list_proc_info(apm_cmd_ctrl_ptr);
   }

   /** Init the book keeping for traversing the sorted container
    *  list */
   if (AR_EOK != (result = apm_set_up_graph_list_traversal(graph_info_ptr, apm_cmd_ctrl_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_set_up_cont_msg_proc: Failed to set up sg list traversal, cmd_opcode[0x%08lx]",
             cmd_opcode);

      return result;
   }

   /** Process sub-graph list for this container */
   if (AR_EOK != (result = apm_cache_cont_sg_and_port_list(graph_info_ptr, apm_cmd_ctrl_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_set_up_cont_msg_proc: Failed to process cont sub-graph ID list, cmd_opcode[0x%08lx]",
             cmd_opcode);

      return result;
   }

   /** Check and update if the current list of sub-graphs are of
    *  same or different states */
   result = apm_gm_cmd_is_sg_list_mixed_state(apm_cmd_ctrl_ptr);

   return result;
}

ar_result_t apm_get_allocated_cont_cmd_ctrl_obj(apm_container_t *     container_node_ptr,
                                                apm_cmd_ctrl_t *      apm_cmd_ctrl_ptr,
                                                apm_cont_cmd_ctrl_t **cont_cmd_ctrl_pptr)
{
   ar_result_t          result = AR_EFAILED;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   if (!container_node_ptr || !cont_cmd_ctrl_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_get_allocated_cont_cmd_ctrl_obj: received null arguments ");
      return AR_EFAILED;
   }

   /** Get the pointer to this container's command control
    *  object list */
   cont_cmd_ctrl_ptr = container_node_ptr->cmd_list.cmd_ctrl_list;

   /** Init the return pointer */
   *cont_cmd_ctrl_pptr = NULL;

   /** Check if any of the cmd obj is already allocated for current
    *  APM command */
   for (uint32_t idx = 0; idx < APM_NUM_MAX_PARALLEL_CMD; idx++)
   {
      /** If the APM cmd ctrl pointer matches with the in container
       *  cmd obj */
      if ((void *)apm_cmd_ctrl_ptr == cont_cmd_ctrl_ptr[idx].apm_cmd_ctrl_ptr)
      {
         /** Match found, return */
         *cont_cmd_ctrl_pptr = &cont_cmd_ctrl_ptr[idx];

         return AR_EOK;
      }

   } /** End of for (cont cmd obj list) */

   /** If no allocated object has been found, print a message.
    *  Caller will decide if this is an error */
   if (!(*cont_cmd_ctrl_pptr))
   {
      AR_MSG(DBG_MED_PRIO,
             "apm_get_allocated_cont_cmd_ctrl_obj: Allocated cmd ctrl obj for CONT_ID[0x%lX] not found, "
             "cmd_opcode[0x%lX]",
             container_node_ptr->container_id,
             apm_cmd_ctrl_ptr->cmd_opcode);

      result = AR_EFAILED;
   }

   return result;
}

ar_result_t apm_get_cont_cmd_ctrl_obj(apm_container_t *     container_node_ptr,
                                      apm_cmd_ctrl_t *      apm_cmd_ctrl_ptr,
                                      apm_cont_cmd_ctrl_t **cont_cmd_ctrl_pptr)
{
   ar_result_t          result = AR_EOK;
   uint32_t             cmd_slot_idx;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr = NULL;

   /** First check if any command object is already allocated
    *  corresponding to current command */
   apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** If allocated object found, return */
   if (cont_cmd_ctrl_ptr)
   {
      *cont_cmd_ctrl_pptr = cont_cmd_ctrl_ptr;

      return result;
   }

   /** Execution falls through if a new object needs to be
    *  allocated now */

   /** Check if all the slots in the command obj list are
    *  occupied.
    *  This condition should not hit as the APM cmd Q is removed
    *  from the wait mask once all the cmd obj slots are
    *  occupied. */
   if (APM_CMD_LIST_FULL_MASK == container_node_ptr->cmd_list.active_cmd_mask)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_get_cont_cmd_ctrl_obj(), cmd obj list is full, CONT_ID[0x%lX]",
             container_node_ptr->container_id);

      return AR_EFAILED;
   }

   /** Find the next available slot in the command list */
   cmd_slot_idx = s32_ct1_s32(container_node_ptr->cmd_list.active_cmd_mask);

   /** Set this bit in the active command mask */
   APM_SET_BIT(&container_node_ptr->cmd_list.active_cmd_mask, cmd_slot_idx);

   /** Get the pointer to command control object corresponding to
    *  available slot */
   cont_cmd_ctrl_ptr = &container_node_ptr->cmd_list.cmd_ctrl_list[cmd_slot_idx];

   cont_cmd_ctrl_ptr->msg_token = APM_CMD_TOKEN_CONTAINER_CTRL_TYPE;

   /** Save the list index in cmd obj */
   cont_cmd_ctrl_ptr->list_idx = cmd_slot_idx;

   /** Save the APM command control pointer in this object */
   cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr = (void *)apm_cmd_ctrl_ptr;

   /** Save the host container pointer in this object */
   cont_cmd_ctrl_ptr->host_container_ptr = (void *)container_node_ptr;

   /** Populate the return pointer */
   *cont_cmd_ctrl_pptr = cont_cmd_ctrl_ptr;

   AR_MSG(DBG_MED_PRIO,
          "apm_get_cont_cmd_ctrl_obj(), Assigned list_idx[0x%lX], CONT_ID[0x%lX], cmd_opcode[0x%lX]",
          cont_cmd_ctrl_ptr->list_idx,
          container_node_ptr->container_id,
          apm_cmd_ctrl_ptr->cmd_opcode);

   return result;
}

ar_result_t apm_release_cont_cmd_ctrl_obj(apm_container_t *container_node_ptr, apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    list_idx;
   uint32_t    cmd_opcode;

   if (!container_node_ptr || !cont_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_release_cont_cmd_ctrl_obj(): Cont ptr(0x%lX) and/or Cont cmd ctrl ptr(0x%lX) is NULL",
             container_node_ptr,
             cont_cmd_ctrl_ptr);

      return AR_EFAILED;
   }

   /** Cached the list index being freed */
   list_idx = cont_cmd_ctrl_ptr->list_idx;

   /** Get the corresponding APM command opcode */
   cmd_opcode = ((apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr)->cmd_opcode;

   /** Clear this bit in the active command mask */
   APM_CLR_BIT(&container_node_ptr->cmd_list.active_cmd_mask, cont_cmd_ctrl_ptr->list_idx);

   /** Release the graph open cached configuration (due to
    *  linked list usage) */

   /** Clear the command control */
   memset(cont_cmd_ctrl_ptr, 0, sizeof(apm_cont_cmd_ctrl_t));

   AR_MSG(DBG_MED_PRIO,
          "apm_get_cont_cmd_ctrl_obj(), Release list_idx[0x%lX], CONT_ID[0x%lX], cmd_opcode[0x%lX]",
          list_idx,
          container_node_ptr->container_id,
          cmd_opcode);

   return result;
}

ar_result_t apm_add_cont_to_pending_msg_send_list(apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                                  apm_container_t *    container_node_ptr,
                                                  apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr)
{
   ar_result_t         result = AR_EOK;
   apm_cmd_rsp_ctrl_t *cmd_rsp_ctrl_ptr;

   /** Get the pointer to response control object corresponding to
    *  current command control object */
   cmd_rsp_ctrl_ptr = &apm_cmd_ctrl_ptr->rsp_ctrl;

   /** Add this container node to the list of container pending
    *  send message */
   if (!cont_cmd_ctrl_ptr->rsp_ctrl.pending_msg_proc)
   {
      if (AR_EOK != (result = apm_db_add_node_to_list(&cmd_rsp_ctrl_ptr->pending_cont_list_ptr,
                                                      (void *)container_node_ptr,
                                                      &cmd_rsp_ctrl_ptr->num_pending_container)))
      {
         return result;
      }

      /** Set the pending flag  */
      cont_cmd_ctrl_ptr->rsp_ctrl.pending_msg_proc = TRUE;
   }

   return result;
}

ar_result_t apm_gm_cmd_get_next_cont_to_process(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, apm_container_t **container_node_pptr)
{
   ar_result_t            result = AR_EOK;
   spf_list_node_t *      curr_cont_node_ptr, *next_cont_node_ptr;
   uint32_t               cmd_opcode;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   uint32_t               curr_cont_msg_opcode;
   apm_cached_cont_list_t curr_list_type;

   /** Get the current GPR command being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   curr_list_type = apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr);

   /** Check if the next node container node available */
   if (!apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.next_cont_node_ptr)
   {
      AR_MSG(DBG_MED_PRIO,
             "apm_gm_cmd_get_next_cont_to_process(): Reached end of cached container list[%lu], cmd_opcode[0x%8lX]",
             curr_list_type,
             cmd_opcode);

      /** Init the return pointer */
      *container_node_pptr = NULL;

      /** Clear the current container node being processed */
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = NULL;

      return AR_EOK;
   }

   /** Init the return pointer with next container to be
    *  processed */
   curr_cont_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.next_cont_node_ptr;

   /** Save the current container being processed */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = curr_cont_node_ptr;

   /** Save the container node return pointer */
   *container_node_pptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

   /** Update the next container to be processed */

   /** Get the pointer to list of container msg opcodes */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   /** Get the current message opcode being processed */
   curr_cont_msg_opcode = cont_msg_opcode_ptr->opcode_list[cont_msg_opcode_ptr->curr_opcode_idx];

   /** Get current container in the graph being processed */
   curr_cont_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr;

   /** For GRAPH START command, sorted container list is
    *  travered in reverse order from sink to source */
   if (SPF_MSG_CMD_GRAPH_START == curr_cont_msg_opcode)
   {
      /** Traverse back the container node ptr */
      next_cont_node_ptr = curr_cont_node_ptr->prev_ptr;
   }
   else
   {
      /** Advance the container node ptr */
      next_cont_node_ptr = curr_cont_node_ptr->next_ptr;
   }

   /** Save the next node to be processed */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.next_cont_node_ptr = next_cont_node_ptr;

   /** Check if the list traversal is done */
   if (!next_cont_node_ptr)
   {
      /** FLag to indicate if the container list traversal is done  */
      apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[curr_list_type] =
         APM_CONT_LIST_TRAV_DONE;
   }

   return result;
}

ar_result_t apm_clear_container_list(apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                     spf_list_node_t **   cached_cont_list_pptr,
                                     uint32_t *           num_container_ptr,
                                     bool_t               clear_cont_cached_cfg,
                                     bool_t               clear_cont_cmd_params,
                                     apm_cont_list_type_t list_type)
{
   ar_result_t          result = AR_EOK;
   spf_list_node_t *    curr_node_ptr, *next_node_ptr;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the pointer to list of containers pending send
    *  message */
   curr_node_ptr = *cached_cont_list_pptr;

   /** Iterate over the container list */
   while (curr_node_ptr)
   {
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Init next node  */
      next_node_ptr = curr_node_ptr->next_ptr;

      /** Clear newly created flag */
      if (APM_CMD_GRAPH_OPEN == apm_cmd_ctrl_ptr->cmd_opcode)
      {
         container_node_ptr->newly_created = FALSE;
      }

      /** Get the container's command control object corresponding
       *  to current APM command control object */
      apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      if (!cont_cmd_ctrl_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "Failed to get cont cmd ctrl obj for CONT_ID[0x%lX], cmd_opcode[0x%lX], result: 0x%lx",
                container_node_ptr->container_id,
                apm_cmd_ctrl_ptr->cmd_opcode,
                result);

         /** Remove this node from the pending list and continue to
          *  next iteration */
         spf_list_delete_node_update_head(&curr_node_ptr, cached_cont_list_pptr, TRUE /*pool_used */);

         /** Move to next node in the list   */
         curr_node_ptr = next_node_ptr;

         continue;
      }

      /** If container cached configuration needs to be cleared */
      if (clear_cont_cached_cfg)
      {
         /** Release the container cached configuration corresponding to
          *  this container */
         result = apm_release_cont_msg_cached_cfg(&cont_cmd_ctrl_ptr->cached_cfg_params, apm_cmd_ctrl_ptr);

         AR_MSG(DBG_MED_PRIO,
                "apm_clear_container_list(): Cleared cached msg config for CONT_ID[0x%lX]",
                container_node_ptr->container_id);
      }

      /** If the container command params need to be released */
      if (clear_cont_cmd_params)
      {
         /** Release all the params cached as part of the current
          *  command processed */
         apm_cont_release_cmd_params(cont_cmd_ctrl_ptr, apm_cmd_ctrl_ptr);

         AR_MSG(DBG_MED_PRIO,
                "apm_clear_container_list(): Cleared cached cmd params for CONT_ID[0x%lX]",
                container_node_ptr->container_id);
      }

      if (APM_PENDING_CONT_LIST == list_type)
      {
         /** Clear the container command control */
         apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, TRUE /** Release rsp msg buf */);

         /** Do the below steps only if the this routine is called for
          *  the last message in the list of container message
          *  sequence */
         if (apm_check_cont_msg_seq_done(apm_cmd_ctrl_ptr))
         {
            /** Release the container command control object */
            apm_release_cont_cmd_ctrl_obj(container_node_ptr, cont_cmd_ctrl_ptr);
         }
      }

      /** Free up the list node memory and
         advance to next node in the list */
      spf_list_delete_node_update_head(&curr_node_ptr, cached_cont_list_pptr, TRUE /*pool_used */);

      /** Move to next node in the list   */
      curr_node_ptr = next_node_ptr;
   }

   /** Update the return container list pointer */
   *cached_cont_list_pptr = curr_node_ptr;

   /** Clear the number of containers */
   *num_container_ptr = 0;

   return result;
}

ar_result_t apm_set_get_cfg_msg_rsp_clear_cont_list(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t          result = AR_EOK;
   spf_list_node_t *    curr_node_ptr, *next_node_ptr;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the pointer to list of containers pending send
    *  message */
   curr_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate over the container list */
   while (curr_node_ptr)
   {
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Get the next node pointer   */
      next_node_ptr = curr_node_ptr->next_ptr;

      /** Get the container's command control object corresponding
       *  to current APM command control object */
      apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

      if (cont_cmd_ctrl_ptr)
      {
         /** If there are no container related parameters been
          *  configured, release the response control */
         if (!cont_cmd_ctrl_ptr->cached_cfg_params.cont_cfg_params.num_cfg_params)
         {
            /** Clear response control for this container */
            apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, TRUE /** Release response buffer */);

            /** Remove this node from the pending list of containers */
            apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                                         container_node_ptr,
                                         &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container);
         }

         /** Release the container cached configuration corresponding to
          *  this container */
         apm_release_cont_msg_cached_cfg(&cont_cmd_ctrl_ptr->cached_cfg_params, apm_cmd_ctrl_ptr);
      }
      else
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_set_get_cfg_msg_rsp_clear_cont_list(): Failed to get cont cmd ctrl obj for CONT_ID[0x%lX]",
                container_node_ptr->container_id);
      }

      /** Advance to next node in the list */
      curr_node_ptr = next_node_ptr;
   }

   return result;
}

static ar_result_t apm_gm_cmd_check_if_cont_to_process(apm_cmd_ctrl_t * apm_cmd_ctrl_ptr,
                                                       apm_container_t *container_node_ptr,
                                                       bool_t *         cont_to_process_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;
   apm_cont_cached_cfg_t *cached_cfg_ptr;
   apm_cont_msg_opcode_t *cont_msg_opcode_list_ptr;
   uint32_t               cont_msg_opcode;
   apm_cached_cont_list_t cont_list_type;

   *cont_to_process_ptr = FALSE;

   /** Get the pointer to list of container message opcodes
    *  corresponding to current APM command being processed */
   cont_msg_opcode_list_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   /** Get the container command control object corresponding to
    *  current APM command */
   apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

   /** Get the pointer to container cached configuration
    *  corresponding to current command */
   cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   /** Get the current message opcode being processed */
   cont_msg_opcode = cont_msg_opcode_list_ptr->opcode_list[cont_msg_opcode_list_ptr->curr_opcode_idx];

   cont_list_type = apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr);

   /** If the current message opcode is disconnect and
    *  containers has no port handles, then skip this step.
    *  Separate check is required because in some cases, APM
    *  caches the sub-graph ID in lieu of port handles to be
    *  disconnected so this check cannot be performed on the
    *  cached port handles alone. Also sub-graph ID may have
    *  been cached because the sub-graph state is also changing  */
   if ((SPF_MSG_CMD_GRAPH_DISCONNECT == cont_msg_opcode) && !apm_cont_has_port_hdl(container_node_ptr, cont_list_type))
   {
      return AR_EOK;
   }

   /** Check if the container has at least 1 sub-graph, 1 external
    *  data and or control port connections or 1 control / data
    *  link to be processed */
   if (cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_IP].num_nodes ||
       cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_DATA_OP].num_nodes ||
       cached_cfg_ptr->graph_mgmt_params.cont_ports[cont_list_type][PORT_TYPE_CTRL_IO].num_nodes)
   {
      *cont_to_process_ptr = TRUE;
   }

   if ((APM_OBJ_TYPE_ACYCLIC == cont_list_type) &&
       (cached_cfg_ptr->graph_mgmt_params.num_sub_graphs || cached_cfg_ptr->graph_mgmt_params.num_data_links ||
        cached_cfg_ptr->graph_mgmt_params.num_ctrl_links))
   {
      *cont_to_process_ptr = TRUE;
   }

   return result;
}

ar_result_t apm_gm_cmd_populate_cont_list_to_process(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t          result = AR_EOK;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;
   bool_t               cont_to_process;

   do
   {
      /** Get next container from the cached container list based
       *  upon current message opcode under process */
      result = apm_gm_cmd_get_next_cont_to_process(apm_cmd_ctrl_ptr, &container_node_ptr);

      if (!container_node_ptr)
      {
         /** Reached end of cached container list, MSG() is printed in
          *  the previous function context */
         return result;
      }

      /** Clear the flag to check if the current container needs to
       *  be processed */
      cont_to_process = FALSE;

      /** Check if this container node needs to be processed */
      apm_gm_cmd_check_if_cont_to_process(apm_cmd_ctrl_ptr, container_node_ptr, &cont_to_process);

      /** If this container can be processed, add it to list of
       *  container pending sending message */
      if (cont_to_process)
      {
         /** Get the container command control object corresponding to
          *  current APM command */
         apm_get_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr);

         /** Add this container to list of container pending send
          *  message */
         apm_add_cont_to_pending_msg_send_list(apm_cmd_ctrl_ptr, container_node_ptr, cont_cmd_ctrl_ptr);

         /** If the current graph management command is sequential,
          *  then break the loop and return */
         if (!apm_graph_mgmt_cmd_iter_is_broadcast(apm_cmd_ctrl_ptr))
         {
            break;
         }

         /** Execution falls through in case of broadcast, where the
          *  current GK msg opcode needs to be broadcasted to all the
          *  impacted containers */
      }

   } while (container_node_ptr);

   return result;
}

ar_result_t apm_set_gm_cmd_cont_proc_info(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   spf_list_node_t *      curr_cont_node_ptr;
   uint32_t               cont_msg_opcode;
   apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   apm_cached_cont_list_t cont_list_type = 0;

   /** Get the pointer to list of container message opcode */
   cont_msg_opcode_ptr = &apm_cmd_ctrl_ptr->cont_msg_opcode;

   cont_list_type = apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr);

   /** Get the current container message opcode */
   cont_msg_opcode = cont_msg_opcode_ptr->opcode_list[cont_msg_opcode_ptr->curr_opcode_idx];

   /** For the GRAPH STAT command, sorted container list is
    *  traversed from sink to source container order */
   if (SPF_MSG_CMD_GRAPH_START == cont_msg_opcode)
   {
      /** Get the pointer to tail node in the list of containers
       *  within this graph */
      spf_list_get_tail_node(apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type]
                                .list_ptr,
                             &curr_cont_node_ptr);
   }
   else
   {
      /** Get the pointer to start of the list of containers within
       *  this graph */
      curr_cont_node_ptr =
         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[cont_list_type].list_ptr;
   }

   /** Save the current and next container node being processed
    *  to same initial value */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.next_cont_node_ptr = curr_cont_node_ptr;
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_node_ptr = curr_cont_node_ptr;

   /** Set the flag to indicate list traversal started */
   apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[cont_list_type] =
      APM_CONT_LIST_TRAV_STARTED;

   return result;
}

ar_result_t apm_gm_cmd_prepare_for_next_cont_msg(apm_graph_info_t *graph_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   uint32_t               cont_msg_opcode;
   apm_cached_cont_list_t curr_cont_list_type;

   do
   {
      curr_cont_list_type = apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr);

      /** If container list traversal has not started */
      if (APM_CONT_LIST_TRAV_STOPPED ==
          apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[curr_cont_list_type])
      {
         /** Set up the cached container list traversal info as per
          *  the current GK msg opcode being processed */
         apm_set_gm_cmd_cont_proc_info(graph_info_ptr, apm_cmd_ctrl_ptr);
      }

      /** Populate the list of containers to be processed as per the
       *  current GK message opcode */
      if (AR_EOK != (result = apm_gm_cmd_populate_cont_list_to_process(apm_cmd_ctrl_ptr)))
      {
         return result;
      }

      /** If the list of pending containers is empty, move on to
       *  the next opcode. This is possible e.g. in case of direct
       *  CLOSE command to sub-graphs in STOPPED state. If the
       *  container does not have external connection, then DISCONNECT
       *  needs to be skipped for this container */

      if (!apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container)
      {
         /** Get the current container message opcode being processed */
         cont_msg_opcode = apm_get_curr_cont_msg_opcode(apm_cmd_ctrl_ptr);

         AR_MSG(DBG_MED_PRIO,
                "apm_gm_cmd_process_next_iteration(): Completed processing of cont list idx[%lu], msg_opcode[0x%lX], "
                "cmd_opcode[0x%08lx]",
                apm_get_curr_active_cont_list_type(apm_cmd_ctrl_ptr),
                cont_msg_opcode,
                apm_cmd_ctrl_ptr->cmd_opcode);

         /** Increment the container list being processed   */
         apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_list_type++;

         /** once the regular container list is traversed Set cont proc
          *  info for the list of containers with cyclic
          *  link */
         if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_list_type < APM_CONT_LIST_MAX)
         {
            continue;
         }
         else /** All the links traversed and no container to process */
         {
            AR_MSG(DBG_MED_PRIO,
                   "apm_gm_cmd_process_next_iteration(): No containers to process, msg_opcode[0x%lX], "
                   "cmd_opcode[0x%08lx]",
                   cont_msg_opcode,
                   apm_cmd_ctrl_ptr->cmd_opcode);

            /** Check if the cont msg opcode list is exhuasted */
            if (apm_check_cont_msg_seq_done(apm_cmd_ctrl_ptr))
            {
               /** If the execution reaches here that implies that all the
                *  container message opcodes have been iterated through and
                *  no containers are impacted because of that. This is an
                *  error scenario  */
               result = AR_EFAILED;

               AR_MSG(DBG_ERROR_PRIO,
                      "apm_gm_cmd_process_next_iteration(): Reached end of container list without any processing, "
                      "cmd_opcode[0x%08lx]",
                      apm_cmd_ctrl_ptr->cmd_opcode);
            }

            /** Break the do-while loop */
            break;
         }

      } /** End of if (empty pending container list) */

   } while (!apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container);

   return result;
}

static ar_result_t apm_update_peer_module_port_handle(spf_module_port_conn_t *  peer_conn_list_ptr,
                                                      spf_module_port_handle_t *peer_port_hdl_ptr,
                                                      uint32_t                  num_conn_handles)
{
   ar_result_t result = AR_EOK;

   /** Iterate over the list of input port connections of peer
    *  container */
   for (uint32_t idx = 0; idx < num_conn_handles; idx++)
   {
      /** Check if the module instance ID and port ID matches */
      if ((peer_conn_list_ptr[idx].self_mod_port_hdl.module_inst_id == peer_port_hdl_ptr->module_inst_id) &&
          (peer_conn_list_ptr[idx].self_mod_port_hdl.module_port_id == peer_port_hdl_ptr->module_port_id))
      {
         /** Validate the peer module port handle */
         if (!peer_conn_list_ptr[idx].self_mod_port_hdl.port_ctx_hdl)
         {
            AR_MSG(DBG_ERROR_PRIO, "CONNECT PEER: Dstn module port handle is NULL");

            return AR_EFAILED;
         }

         /** For the self container, update the peer port handle
          *  information */
         peer_port_hdl_ptr->port_ctx_hdl = peer_conn_list_ptr[idx].self_mod_port_hdl.port_ctx_hdl;

         break;
      }
   }

   return result;
}

ar_result_t apm_connect_peer_module_ports(apm_cont_cmd_ctrl_t *     peer_cont_cmd_ctrl_ptr,
                                          spf_module_port_handle_t *self_port_hdl_ptr,
                                          spf_module_port_handle_t *peer_port_hdl_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_msg_header_t *            msg_header_ptr;
   spf_cntr_port_connect_info_t *peer_cont_graph_open_rsp_ptr;

   /** Get the peer container's response msg */
   msg_header_ptr = (spf_msg_header_t *)peer_cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg.payload_ptr;

   /** Get the pointer to graph open response message */
   peer_cont_graph_open_rsp_ptr = (spf_cntr_port_connect_info_t *)&msg_header_ptr->payload_start;

   if (PORT_TYPE_DATA_OP == self_port_hdl_ptr->port_type)
   {
      apm_update_peer_module_port_handle(peer_cont_graph_open_rsp_ptr->ip_data_port_conn_list_ptr,
                                         peer_port_hdl_ptr,
                                         peer_cont_graph_open_rsp_ptr->num_ip_data_port_conn);
   }
   else if (PORT_TYPE_DATA_IP == self_port_hdl_ptr->port_type) /** Input port */
   {
      apm_update_peer_module_port_handle(peer_cont_graph_open_rsp_ptr->op_data_port_conn_list_ptr,
                                         peer_port_hdl_ptr,
                                         peer_cont_graph_open_rsp_ptr->num_op_data_port_conn);
   }
   else /** Control ports */
   {
      apm_update_peer_module_port_handle(peer_cont_graph_open_rsp_ptr->ctrl_port_conn_list_ptr,
                                         peer_port_hdl_ptr,
                                         peer_cont_graph_open_rsp_ptr->num_ctrl_port_conn);
   }

   return result;
}

static ar_result_t apm_add_cont_port_db_info(apm_graph_info_t *      graph_info_ptr,
                                             apm_container_t *       container_node_ptr,
                                             spf_module_port_conn_t *mod_port_conn_ptr,
                                             spf_module_port_type_t  port_type)
{
   ar_result_t                   result                     = AR_EOK;
   apm_cont_port_connect_info_t *port_connect_info_node_ptr = NULL;
   spf_module_port_conn_t *      module_port_info_ptr;
   spf_list_node_t **            list_pptr;
   spf_list_node_t *             list_ptr;
   uint32_t *                    list_node_counter_ptr;
   uint32_t                      self_port_sg_id;
   uint32_t                      peer_port_sg_id;
   apm_sub_graph_t *             sub_graph_node_ptr;
   apm_module_t *                peer_module_obj_ptr = NULL;

   /** Get the sub-graph ID corresponding to this container
    *  input port's owner module instance */

   self_port_sg_id = mod_port_conn_ptr->self_mod_port_hdl.sub_graph_id;
   peer_port_sg_id = mod_port_conn_ptr->peer_mod_port_hdl.sub_graph_id;

   /** Get peer module obj pointer  */
   if (AR_EOK != (result = apm_db_get_module_node(graph_info_ptr,
                                                  mod_port_conn_ptr->peer_mod_port_hdl.module_inst_id,
                                                  &peer_module_obj_ptr,
                                                  APM_DB_OBJ_QUERY)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_add_cont_port_db_info(): Failed to get node for MIID[0x%lX]",
             mod_port_conn_ptr->peer_mod_port_hdl.module_inst_id);

      return result;
   }

   /** Validate return ptr  */
   if (!peer_module_obj_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_add_cont_port_db_info(): Returned mod ptr is null for MIID[0x%lX]",
             mod_port_conn_ptr->peer_mod_port_hdl.module_inst_id);

      return AR_EFAILED;
   }

   /** Get the list of ports */
   list_ptr = container_node_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][port_type].list_ptr;

   /** Get the pointer to list of ports  */
   list_pptr = &container_node_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][port_type].list_ptr;

   /** Get the pointer to num port connection per SG pair */
   list_node_counter_ptr = &container_node_ptr->cont_ports_per_sg_pair[APM_PORT_TYPE_ACYCLIC][port_type].num_nodes;

   /** If any error occurred during the GRAPH open, from current
    *  container's context which returned SUCCESS response, the
    *  source SG ID will be invalid for input ports and dstn SG
    *  ID will be invalid for output ports */
   if (!self_port_sg_id || !peer_port_sg_id)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_add_cont_port_db_info(): Invalid self "
             "SG_ID[0x%lX] or peer SG_ID[0x%lX]",
             self_port_sg_id,
             peer_port_sg_id);

      return AR_EFAILED;
   }

   /** Get the connection node */
   if (AR_EOK !=
       (result =
           apm_db_get_cont_port_info_node(list_ptr, self_port_sg_id, peer_port_sg_id, &port_connect_info_node_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_add_cont_port_db_info(): Cont port connection info query failed, self sg_id[0x%lX], peer "
             "sg_id[0x%lX], self cont_id[0x%lX], peer cont_id[0x%lX]",
             self_port_sg_id,
             peer_port_sg_id,
             container_node_ptr->container_id,
             peer_module_obj_ptr->host_cont_ptr->container_id);

      return result;
   }

   /** If the node does not exist, allocate one */
   if (!port_connect_info_node_ptr)
   {
      port_connect_info_node_ptr =
         (apm_cont_port_connect_info_t *)posal_memory_malloc(sizeof(apm_cont_port_connect_info_t), APM_INTERNAL_STATIC_HEAP_ID);

      if (!port_connect_info_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_add_cont_port_db_info(): Failed to allocate cont port info memory, cont_id[0x%lX]",
                container_node_ptr->container_id);

         return AR_ENOMEMORY;
      }

      /** Clear the allocated structure */
      memset(port_connect_info_node_ptr, 0, sizeof(apm_cont_port_connect_info_t));

      /** Add the connection node to the host container */
      apm_db_add_node_to_list(list_pptr, port_connect_info_node_ptr, list_node_counter_ptr);

      /** If the sub-graph ID's are different, then store this
       *  sub-graph edge in the global graph data base */
      if (self_port_sg_id != peer_port_sg_id)
      {
         apm_db_add_node_to_list(&graph_info_ptr->sub_graph_conn_list_ptr,
                                 port_connect_info_node_ptr,
                                 &graph_info_ptr->num_sub_graph_conn);
      }

      /** Get the  sub-graph node obj corresponding to self
       *  sub-graph ID in the current connection */

      if (AR_EOK !=
          (result = apm_db_get_sub_graph_node(graph_info_ptr, self_port_sg_id, &sub_graph_node_ptr, APM_DB_OBJ_QUERY)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Failed to get sub-graph obj for self SG_ID[0x%lX]",
                self_port_sg_id);

         return result;
      }
      if (!sub_graph_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Failed to get sub-graph obj for self SG_ID[0x%lX], sg_node_ptr is NULL",
                self_port_sg_id);

         return AR_EFAILED;
      }
      /** Cache the sub-graph obj */
      port_connect_info_node_ptr->self_sg_obj_ptr = sub_graph_node_ptr;

      /** Get the  sub-graph node obj corresponding to peer
       *  sub-graph ID in the current connection. */

      if (AR_EOK !=
          (result = apm_db_get_sub_graph_node(graph_info_ptr, peer_port_sg_id, &sub_graph_node_ptr, APM_DB_OBJ_QUERY)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Failed to get sub-graph obj for peer SG_ID[0x%lX]",
                peer_port_sg_id);

         return result;
      }
      if (!sub_graph_node_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Failed to get sub-graph obj for peer SG_ID[0x%lX], sg_node_ptr is NULL",
                peer_port_sg_id);

         return AR_EFAILED;
      }
      /** Cache the sub-graph obj */
      port_connect_info_node_ptr->peer_sg_obj_ptr = sub_graph_node_ptr;

      /** Init the propagated state of the peer sub-graph as
       *  STOPPED */
      port_connect_info_node_ptr->peer_sg_propagated_state = APM_SG_STATE_STOPPED;

   } /** End of if (port connection node does not exist for a sub-graph ID pair) */

   /** Allocate memory for the port connection link */
   module_port_info_ptr =
      (spf_module_port_conn_t *)posal_memory_malloc(sizeof(spf_module_port_conn_t), APM_INTERNAL_STATIC_HEAP_ID);

   if (!module_port_info_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_add_cont_port_db_info(): Failed to allocate module port info memory");

      return AR_ENOMEMORY;
   }

   /** Copy the connection contents */
   memscpy(module_port_info_ptr, sizeof(spf_module_port_conn_t), mod_port_conn_ptr, sizeof(spf_module_port_conn_t));

   /** Add the module port connection to list of input/output/ctrl
    *  port connection */
   apm_db_add_node_to_list(&port_connect_info_node_ptr->port_conn_list_ptr,
                           module_port_info_ptr,
                           &port_connect_info_node_ptr->num_port_conn);

   /** Cache peer container ID.  If there are
    *  multiple connections across 2 containers for same
    *  sub-graph ID pair, cache the peer container only once */
   if (AR_EOK != (result = apm_db_search_and_add_node_to_list(&port_connect_info_node_ptr->peer_cont_list.list_ptr,
                                                              peer_module_obj_ptr->host_cont_ptr,
                                                              &port_connect_info_node_ptr->peer_cont_list.num_nodes)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_add_cont_port_db_info(): Failed to addd peer cont ID[0x%lX] for self cont ID[0x%lX]",
             peer_module_obj_ptr->host_cont_ptr->container_id,
             container_node_ptr->container_id);
   }

   return result;
}

static ar_result_t apm_connect_container_ports(apm_t *apm_info_ptr, spf_module_port_conn_t *mod_port_conn_ptr)
{
   ar_result_t               result = AR_EOK;
   apm_module_t *            peer_module_node_ptr;
   apm_container_t *         peer_cont_node_ptr;
   apm_cont_cmd_ctrl_t *     peer_cont_cmd_ctrl_ptr = NULL;
   spf_module_port_handle_t *self_mod_port_hdl_ptr;
   spf_module_port_handle_t *peer_mod_port_hdl_ptr;
   apm_ext_utils_t *         ext_utils_ptr;

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr  = apm_info_ptr->curr_cmd_ctrl_ptr;
   uint8_t *       sat_prv_param_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sat_prv_cfg_ptr;

   self_mod_port_hdl_ptr = &mod_port_conn_ptr->self_mod_port_hdl;
   peer_mod_port_hdl_ptr = &mod_port_conn_ptr->peer_mod_port_hdl;

   /** For this connection link, get the peer module's db node */
   apm_db_get_module_node(&apm_info_ptr->graph_info,
                          peer_mod_port_hdl_ptr->module_inst_id,
                          &peer_module_node_ptr,
                          APM_DB_OBJ_QUERY);

   /** Get the pointer to ext util vtbl  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** If the peer module node is not present */
   if (!peer_module_node_ptr)
   {
      if (sat_prv_param_ptr)
      {
         /** Check if it's an inter-proc peer */
         uint32_t remote_peer_domain_id = 0;

         if (ext_utils_ptr->offload_vtbl_ptr &&
             ext_utils_ptr->offload_vtbl_ptr->apm_search_in_sat_prv_peer_cfg_for_miid_fptr)
         {
            ext_utils_ptr->offload_vtbl_ptr
               ->apm_search_in_sat_prv_peer_cfg_for_miid_fptr(sat_prv_param_ptr,
                                                              peer_mod_port_hdl_ptr->module_inst_id,
                                                              &remote_peer_domain_id);
         }

         if (remote_peer_domain_id)
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "sat_apm_connect_container_ports() INTER-PROC IMCL: Found peer module M_IID[%lu]",
                   peer_mod_port_hdl_ptr->module_inst_id);
            return AR_EOK;
         }

      } /** End of if (sat_prv_param_ptr) */

      AR_MSG(DBG_ERROR_PRIO,
             "apm_connect_container_ports() : Failed to get peer module M_IID[%lu]",
             peer_mod_port_hdl_ptr->module_inst_id);

      return AR_EFAILED;
   }

   /** Update the peer module's sub-graph ID in the connection
    *  info received from container */
   peer_mod_port_hdl_ptr->sub_graph_id = peer_module_node_ptr->host_sub_graph_ptr->sub_graph_id;

   /** Get the pointer to host container node for the connected
    *  peer module node */
   peer_cont_node_ptr = peer_module_node_ptr->host_cont_ptr;

   /** Get the peer container's command control object
    *  corresponding to current APM command in process */
   apm_get_allocated_cont_cmd_ctrl_obj(peer_cont_node_ptr, apm_info_ptr->curr_cmd_ctrl_ptr, &peer_cont_cmd_ctrl_ptr);

   if (!peer_cont_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_connect_container_ports() : Failed to get APM cmd ctrl obj for peer CONT_ID : [0x%lX]",
             peer_cont_node_ptr->container_id);

      return AR_EFAILED;
   }

   /** Update the peer module port info */
   if (AR_EOK !=
       (result = apm_connect_peer_module_ports(peer_cont_cmd_ctrl_ptr, self_mod_port_hdl_ptr, peer_mod_port_hdl_ptr)))
   {
      return result;
   }
   AR_MSG(DBG_LOW_PRIO,
          "After APM connect peer : self_mod_port_hdl_ptr->domain_id = %lu, peer_mod_port_hdl_ptr->domain_id "
          "= %lu",
          self_mod_port_hdl_ptr->domain_id,
          peer_mod_port_hdl_ptr->domain_id);

   return result;
}

static ar_result_t apm_connect_ports_and_update_db(apm_t *                 apm_info_ptr,
                                                   apm_container_t *       container_node_ptr,
                                                   spf_module_port_conn_t *mod_port_conn_list_ptr,
                                                   uint32_t                num_connections,
                                                   spf_module_port_type_t  port_type,
                                                   ar_result_t             graph_open_aggr_rsp)
{
   ar_result_t             result = AR_EOK;
   spf_module_port_conn_t *curr_mod_port_conn_ptr;

   /** Iterate over the input port handles */
   for (uint32_t idx = 0; idx < num_connections; idx++)
   {
      /** Get the port connect info */
      curr_mod_port_conn_ptr = &mod_port_conn_list_ptr[idx];

      /** Make port connections only if all the containers returned
       *  SUCCESS in response to GK MSG GRAPH OPEN */
      if (AR_EOK == graph_open_aggr_rsp)
      {
         apm_connect_container_ports(apm_info_ptr, curr_mod_port_conn_ptr);
      }

      /** Add container port connection info. In case of any
       *  failures,the port information is  later used for closing
       *  the sub-graph */
      apm_add_cont_port_db_info(&apm_info_ptr->graph_info, container_node_ptr, curr_mod_port_conn_ptr, port_type);

   } /** End of for (rsp_msg_ptr->num_ip_port_conn) */

   return result;
}

ar_result_t apm_connect_peer_containers_ext_ports(apm_t *apm_info_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_cntr_port_connect_info_t *port_conn_msg_ptr;
   spf_msg_header_t *            msg_header_ptr;
   apm_container_t *             container_node_ptr;
   spf_list_node_t *             curr_node_ptr;
   spf_list_node_t *             next_node_ptr;
   apm_cmd_ctrl_t *              apm_cmd_ctrl_ptr;
   apm_cont_cmd_ctrl_t *         cont_cmd_ctrl_ptr;
   uint32_t                      graph_open_aggr_rsp;
   bool_t                        RETAIN_CONT_RSP_MSG_BUF = FALSE;

   /** Get the pointer to current APM cmd control */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Get the overall response for the GRAPH OPEN GK msg */
   graph_open_aggr_rsp = apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status;

   /** Get the pointer to list of containers pending message */
   curr_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate over the list of containers to which command has
    *  been sent */
   while (curr_node_ptr)
   {
      /** Get the handle to container in pending list */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** Get the next node pointer */
      next_node_ptr = curr_node_ptr->next_ptr;

      /** Get the pointer to container's command control object */
      if (AR_EOK !=
          (result = apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_prepare_graph_connect_msg(): Failed to get cmd ctrl obj for CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Validate the cmd control pointer */
      if (!cont_cmd_ctrl_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Cont cmd ctrl ptr is NULL, CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Get the pointer to this container's response message */
      msg_header_ptr = (spf_msg_header_t *)cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg.payload_ptr;

      /** Validate the message pointer */
      if (!msg_header_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "GRAPH OPEN RSP HDLR: Cont rsp msg ptr is NULL, CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Get the pointer to start of the GRAPH OPEN response
       *  message payload for this container */
      port_conn_msg_ptr = (spf_cntr_port_connect_info_t *)&msg_header_ptr->payload_start;

      AR_MSG(DBG_MED_PRIO,
             "GRAPH OPEN RSP HDLR: CONT_ID [0x%lX], NUM_IP_HDL "
             "[%lu], NUM_OP_HDL [%lu], NUM_CTRL_HDL [%lu]",
             container_node_ptr->container_id,
             port_conn_msg_ptr->num_ip_data_port_conn,
             port_conn_msg_ptr->num_op_data_port_conn,
             port_conn_msg_ptr->num_ctrl_port_conn);

      /** Update container port connections */

      /** If input port connections are present, then connect them
       *  with the peer output ports and update the APM DB */
      if (port_conn_msg_ptr->num_ip_data_port_conn)
      {
         result = apm_connect_ports_and_update_db(apm_info_ptr,
                                                  container_node_ptr,
                                                  port_conn_msg_ptr->ip_data_port_conn_list_ptr,
                                                  port_conn_msg_ptr->num_ip_data_port_conn,
                                                  PORT_TYPE_DATA_IP,
                                                  graph_open_aggr_rsp);
      }

      /** If output port connections are present, then connect them
       *  with the peer input ports and update the APM DB */
      if (port_conn_msg_ptr->num_op_data_port_conn)
      {
         result = apm_connect_ports_and_update_db(apm_info_ptr,
                                                  container_node_ptr,
                                                  port_conn_msg_ptr->op_data_port_conn_list_ptr,
                                                  port_conn_msg_ptr->num_op_data_port_conn,
                                                  PORT_TYPE_DATA_OP,
                                                  graph_open_aggr_rsp);
      }

      /** If control port connections are present, then connect them
       *  with the peer ports and update the APM DB */
      if (port_conn_msg_ptr->num_ctrl_port_conn)
      {
         result = apm_connect_ports_and_update_db(apm_info_ptr,
                                                  container_node_ptr,
                                                  port_conn_msg_ptr->ctrl_port_conn_list_ptr,
                                                  port_conn_msg_ptr->num_ctrl_port_conn,
                                                  PORT_TYPE_CTRL_IO,
                                                  graph_open_aggr_rsp);
      }

      /** If current container has no external input/output/control
       *  ports, then remove this container from the list of
       *  pending containers for further processing */
      if (!port_conn_msg_ptr->num_ip_data_port_conn && !port_conn_msg_ptr->num_op_data_port_conn &&
          !port_conn_msg_ptr->num_ctrl_port_conn)
      {
         /** Remove this node the list of pending containers */
         apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                                      container_node_ptr,
                                      &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container);

         spf_list_node_t *temp_list_ptr  = NULL;
         uint32_t         temp_node_cntr = 1;

         /** Insert this node in a temporary list for clean up  */
         spf_list_insert_tail(&temp_list_ptr, container_node_ptr, APM_INTERNAL_STATIC_HEAP_ID, TRUE /* use_pool*/);

         /** Clear cached cfg for current container object   */
         apm_clear_container_list(apm_cmd_ctrl_ptr,
                                  &temp_list_ptr,
                                  &temp_node_cntr,
                                  APM_CONT_CACHED_CFG_RELEASE,
                                  APM_CONT_CMD_PARAMS_RELEASE,
                                  APM_PENDING_CONT_LIST);
      }
      else /** Contains atleast 1, IO, control ports */
      {
         /** Clear container response control. Retain the response
          *  buffer as it will be re-used for sub-sequent connect
          *  command to containers */
         apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, RETAIN_CONT_RSP_MSG_BUF);
      }

      /** Advance the pointer to next node in the list of pending
       *  containers  */
      curr_node_ptr = next_node_ptr;

   } /** End of while( container pending rsp msgs ) */

   return result;
}

apm_sub_graph_state_t apm_get_updated_sg_state(uint32_t msg_opcode)
{
   apm_sub_graph_state_t sg_state;

   switch (msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_OPEN:
      case SPF_MSG_CMD_GRAPH_CONNECT:
      case SPF_MSG_CMD_GRAPH_STOP:
      {
         sg_state = APM_SG_STATE_STOPPED;
         break;
      }
      case SPF_MSG_CMD_GRAPH_PREPARE:
      {
         sg_state = APM_SG_STATE_PREPARED;
         break;
      }
      case SPF_MSG_CMD_GRAPH_START:
      {
         sg_state = APM_SG_STATE_STARTED;
         break;
      }
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         sg_state = APM_SG_STATE_SUSPENDED;
         break;
      }
      default:
      {
         sg_state = APM_SG_STATE_INVALID;
         break;
      }
   } /** End of switch() */

   return sg_state;
}

static ar_result_t apm_update_cont_sg_state(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, uint32_t msg_opcode)
{
   ar_result_t           result        = AR_EOK;
   spf_list_node_t *     curr_node_ptr = NULL;
   apm_sub_graph_t *     sub_graph_node_ptr;
   apm_sub_graph_state_t new_sg_state;
   uint32_t              cmd_opcode;
   apm_ext_utils_t *     ext_utils_ptr;
   uint32_t              num_subgraphs     = 0;
   spf_list_node_t *     subgraph_list_ptr = NULL;

   /** Get the ext utils vtb ptr  */
   ext_utils_ptr = apm_get_ext_utils_ptr();

   /** Get the current command opcode being processed */
   cmd_opcode = apm_cmd_ctrl_ptr->cmd_opcode;

   /** Get the pointer to list of sub-grphs being processed as
    *  part of current command */
   switch (msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         curr_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.processed_sg_list_ptr;
         subgraph_list_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.processed_sg_list_ptr;
         num_subgraphs = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_proc_info.num_processed_sg;
         break;
      }
      case SPF_MSG_CMD_GRAPH_CONNECT:
      {
         curr_node_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr;
         subgraph_list_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr;
         num_subgraphs = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.num_sub_graphs;

         break;
      }
      default:
      {
         break;
      }

   } /** End of switch (msg_opcode) */

   /** Get the updated sub-graph state */
   new_sg_state = apm_get_updated_sg_state(msg_opcode);

   while (curr_node_ptr)
   {
      sub_graph_node_ptr = (apm_sub_graph_t *)curr_node_ptr->obj_ptr;

      /** Update this sub-graph's state */
      sub_graph_node_ptr->state = new_sg_state;

      AR_MSG(DBG_MED_PRIO,
             "apm_update_cont_sg_state(): SG_ID[0x%lX] moved to state: 0x%lX, cmd_opcode[0x%08lX]",
             sub_graph_node_ptr->sub_graph_id,
             new_sg_state,
             cmd_opcode);

      /** Advance to next node in the list */
      curr_node_ptr = curr_node_ptr->next_ptr;
   }
   if (ext_utils_ptr->debug_info_utils_vtable_ptr && ext_utils_ptr->debug_info_utils_vtable_ptr->apm_log_sg_state_change_fptr)
   {
      result = ext_utils_ptr->debug_info_utils_vtable_ptr->apm_log_sg_state_change_fptr(subgraph_list_ptr, num_subgraphs);
   }
   if(result != AR_EOK)
   {
      AR_MSG(DBG_HIGH_PRIO,"Change in sub graph states are not populated in payload %lu", result);
   }
   return result;
}

ar_result_t apm_cont_msg_one_iter_completed(apm_cmd_ctrl_t *cmd_ctrl_ptr)
{
   ar_result_t result = AR_EOK;
   uint32_t    cont_msg_opcode;

   /** Get current container message opcode under process */
   cont_msg_opcode = apm_get_curr_cont_msg_opcode(cmd_ctrl_ptr);

   /** Update sub-graph state of all the impacted sub-graphs after
    *  the processing of current GK MSG */

   if (apm_is_sg_state_changing_msg_opcode(cont_msg_opcode))
   {
      apm_update_cont_sg_state(cmd_ctrl_ptr, cont_msg_opcode);
   }

   /** Increment the container msg list opcode index */
   cmd_ctrl_ptr->cont_msg_opcode.curr_opcode_idx++;

   /** Reset the container list being processed   */
   cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.curr_cont_list_type = APM_CONT_LIST_WITH_CYCLIC_LINK;

   for (uint32_t idx = 0; idx < APM_CONT_LIST_MAX; idx++)
   {
      cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cont_list_trav_state[idx] = APM_CONT_LIST_TRAV_STOPPED;
   }

   AR_MSG(DBG_HIGH_PRIO, "GRAPH MGMT RSP: Completed cont msg_opcode[0x%lX] processing", cont_msg_opcode);

   return result;
}

static ar_result_t apm_update_cont_port_hdl_list_state_per_type(apm_cmd_ctrl_t * apm_cmd_ctrl_ptr,
                                                                spf_list_node_t *cached_port_conn_list_ptr,
                                                                spf_list_node_t *port_conn_list_per_sg_pair_ptr,
                                                                uint32_t         msg_opcode)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_cached_port_conn_node_ptr;
   spf_list_node_t *             curr_conn_list_per_sg_pair_node_ptr;
   spf_list_node_t *             curr_port_conn_node_ptr;
   apm_cont_port_connect_info_t *port_conn_list_per_sg_ptr;
   spf_module_port_conn_t *      cached_port_conn_obj_ptr;
   spf_module_port_conn_t *      port_conn_obj_ptr;
   apm_sub_graph_state_t         updated_sg_state            = APM_SG_STATE_INVALID;
   bool_t                        port_hdl_list_state_updated = FALSE;

   /** Get the update sub-graph state as per the current message
    *  opcode */
   updated_sg_state = apm_get_updated_sg_state(msg_opcode);

   /** Get the pointer to cached port connection list */
   curr_cached_port_conn_node_ptr = cached_port_conn_list_ptr;

   /** Iterate over the list of cached container port handles
    *  which are not grouped under particular sub-graph id pair.
    *  Find these handles under the container book keeping of
    *  port handles per sub-graph ID pair and update the
    *  propagated state for the PEER sub-graph */

   while (curr_cached_port_conn_node_ptr)
   {
      /** Set the list state updated flag to FALSE */
      port_hdl_list_state_updated = FALSE;

      /** Get the pointer to cached port handle list */
      cached_port_conn_obj_ptr = (spf_module_port_conn_t *)curr_cached_port_conn_node_ptr->obj_ptr;

      /** Get the pointer to list of port handles per sub-graph ID
       *  pair */
      curr_conn_list_per_sg_pair_node_ptr = port_conn_list_per_sg_pair_ptr;

      /** Iterate over the list of container book keeping struct
       *  for port handle list per sub-graph ID pair */
      while (curr_conn_list_per_sg_pair_node_ptr)
      {
         /** Get the list node object for current port handle list per
          *  sub-graph ID pair */
         port_conn_list_per_sg_ptr = (apm_cont_port_connect_info_t *)curr_conn_list_per_sg_pair_node_ptr->obj_ptr;

         /** If the state is already updated, skip this port handle
          *  list */
         if (port_conn_list_per_sg_ptr->peer_sg_propagated_state == updated_sg_state)
         {
            /** Advance to next node in the list */
            curr_conn_list_per_sg_pair_node_ptr = curr_conn_list_per_sg_pair_node_ptr->next_ptr;

            /** Skip to next node in the list */
            continue;
         }

         /** Get the pointer to corresponding port handle list */
         curr_port_conn_node_ptr = port_conn_list_per_sg_ptr->port_conn_list_ptr;

         /** Iterate over the list of port handles */
         while (curr_port_conn_node_ptr)
         {
            /** Get the pointer to port connection object */
            port_conn_obj_ptr = (spf_module_port_conn_t *)curr_port_conn_node_ptr->obj_ptr;

            /** Check if the cached port handle matches with book keeping
             *  struct, then delete it */
            if (port_conn_obj_ptr == cached_port_conn_obj_ptr)
            {
               /** Update the propagated state */
               port_conn_list_per_sg_ptr->peer_sg_propagated_state = updated_sg_state;

               /** Set the state update flag to TRUE */
               port_hdl_list_state_updated = TRUE;

               /** Break the loop as list state has been updated */
               break;
            }

            /** Else, advance the list pointer */
            curr_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;

         } /** End of while (port hdl list under single sg id pair )*/

         /** If list state has been update, break for this while loop */
         if (port_hdl_list_state_updated)
         {
            break;
         }

         /** Else, keep traversing the list */
         curr_conn_list_per_sg_pair_node_ptr = curr_conn_list_per_sg_pair_node_ptr->next_ptr;

      } /** End of while (container port hdl list per sg id pair )*/

      curr_cached_port_conn_node_ptr = curr_cached_port_conn_node_ptr->next_ptr;

   } /** End of while (container cached port hdl list)*/

   return result;
}

static ar_result_t apm_update_cont_port_hdl_list_state_all(apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr,
                                                           apm_container_t *    container_node_ptr,
                                                           apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                                           uint32_t             msg_opcode)
{
   ar_result_t result = AR_EOK;
   apm_list_t(*cont_db_port_list_pptr)[PORT_TYPE_MAX];
   apm_list_t(*cont_cached_port_list_pptr)[PORT_TYPE_MAX];

   cont_db_port_list_pptr     = container_node_ptr->cont_ports_per_sg_pair;
   cont_cached_port_list_pptr = cont_cmd_ctrl_ptr->cached_cfg_params.graph_mgmt_params.cont_ports;

   /** Destroy individual input,output and control port handles, if present in
    *  cached configuration */

   for (uint32_t cont_list_type = 0; cont_list_type < APM_CONT_LIST_MAX; cont_list_type++)
   {
      for (uint32_t port_type = 0; port_type < PORT_TYPE_MAX; port_type++)
      {
         if (cont_cached_port_list_pptr[cont_list_type][port_type].list_ptr)
         {
            apm_update_cont_port_hdl_list_state_per_type(apm_cmd_ctrl_ptr,
                                                         cont_cached_port_list_pptr[cont_list_type][port_type].list_ptr,
                                                         cont_db_port_list_pptr[cont_list_type][port_type].list_ptr,
                                                         msg_opcode);
         }
      }
   }

   return result;
}

ar_result_t apm_gm_cmd_update_cont_cached_port_hdl_state(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr, uint32_t msg_opcode)
{
   ar_result_t          result = AR_EOK;
   spf_list_node_t *    curr_cont_node_ptr;
   apm_container_t *    container_obj_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the list of containers to which the graph mgmt
    *  command was sent */
   curr_cont_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate over the list of containers */
   while (curr_cont_node_ptr)
   {
      /** Get the pointer to container object pointer */
      container_obj_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      /** Get the pointer to allocated container command control
       *  object corresponding to current APM command under process */
      if (AR_EOK !=
          (result = apm_get_allocated_cont_cmd_ctrl_obj(container_obj_ptr, apm_cmd_ctrl_ptr, &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_gm_cmd_update_cont_cached_port_hdl_state(): Failed to get cmd ctrl obj for CONT_ID[0x%lX] "
                "cmd_opcode[0x%08lx]",
                container_obj_ptr->container_id,
                apm_cmd_ctrl_ptr->cmd_opcode);

         return result;
      }

      /** Update the propagated state of the peer-subgraph within
       *  the port handle list per sub-graph ID pair */
      if (cont_cmd_ctrl_ptr)
      {
         apm_update_cont_port_hdl_list_state_all(apm_cmd_ctrl_ptr, container_obj_ptr, cont_cmd_ctrl_ptr, msg_opcode);
      }

      /** Advance to next node in the list */
      curr_cont_node_ptr = curr_cont_node_ptr->next_ptr;
   }

   return result;
}

ar_result_t apm_search_and_cache_cont_port_hdl(apm_container_t *      container_obj_ptr,
                                               apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr,
                                               uint32_t               module_iid,
                                               uint32_t               port_id,
                                               spf_module_port_type_t port_type,
                                               uint32_t               cmd_opcode)
{
   ar_result_t             result             = AR_EOK;
   spf_module_port_conn_t *port_conn_info_ptr = NULL;
   apm_list_t(*cont_cached_port_list_pptr)[PORT_TYPE_MAX];
   apm_list_t(*cont_db_port_list_pptr)[PORT_TYPE_MAX];

   /** Check if the current command is graph open/close   */
   if ((APM_CMD_GRAPH_OPEN != cmd_opcode) && (APM_CMD_GRAPH_CLOSE != cmd_opcode))
   {
      AR_MSG(DBG_ERROR_PRIO, "apm_search_and_cache_cont_port_hdl(): Unexpected cmd opcode[0x%lX]", cmd_opcode);

      return AR_EFAILED;
   }

   cont_db_port_list_pptr = container_obj_ptr->cont_ports_per_sg_pair;

   if (APM_CMD_GRAPH_OPEN == cmd_opcode)
   {
      cont_cached_port_list_pptr = cont_cmd_ctrl_ptr->cached_cfg_params.graph_mgmt_params.cont_ports;
   }
   else if (APM_CMD_GRAPH_CLOSE == cmd_opcode)
   {
      cont_cached_port_list_pptr = cont_cmd_ctrl_ptr->cmd_params.link_close_params.cont_ports;
   }

   apm_db_query_t query_type = APM_DB_OBJ_NOTEXIST_OK;

   for (uint32_t cont_list_idx = 0; cont_list_idx < APM_CONT_LIST_MAX; cont_list_idx++)
   {
      /** If the loop runs til the last container list, and the
       *  queried object is not found, then for last iteration,
       *  change the query type to mandatory, so that if object is
       *  not found, failure can be returned */
      if (APM_CONT_LIST_END == cont_list_idx)
      {
         /** If the obj is not found until last iteration, make the
          *  final query mandating the object must exist, else its a
          *  failure */
         query_type = APM_DB_OBJ_QUERY;
      }

      if (cont_db_port_list_pptr[cont_list_idx][port_type].list_ptr)
      {
         /** Get the container port handle corresponding to the port
          *  ID */
         if (AR_EOK != (result = apm_db_get_cont_port_conn(cont_db_port_list_pptr[cont_list_idx][port_type].list_ptr,
                                                           module_iid,
                                                           port_id,
                                                           &port_conn_info_ptr,
                                                           query_type)))
         {
            AR_MSG(DBG_ERROR_PRIO,
                   "apm_search_and_cache_cont_port_hdl(): Failed to get port handle,module_iid[0x%lx], port_id[0x%lX]",
                   module_iid,
                   port_id);

            return result;
         }
      }

      /** Cache this port connection node for further processing  */
      if (port_conn_info_ptr)
      {
         /** Cache this port connection node for further processing  */

         result = apm_db_search_and_add_node_to_list(&cont_cached_port_list_pptr[cont_list_idx][port_type].list_ptr,
                                                     port_conn_info_ptr,
                                                     &cont_cached_port_list_pptr[cont_list_idx][port_type].num_nodes);

         break;
      }
   }

   return result;
}
