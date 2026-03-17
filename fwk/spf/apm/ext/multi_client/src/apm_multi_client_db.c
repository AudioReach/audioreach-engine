/**
 * \file apm_multi_client_db.c
 *
 * \brief
 *
 *     This file handles multi client db related requests.
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**==============================================================================
 * INCLUDE HEADER FILES
 ==============================================================================*/
#include "apm_multi_client.h"

ar_result_t apm_db_get_mod_connection(apm_graph_info_t             *graph_info_ptr,
                                      apm_module_conn_cfg_t        *conn_cfg_ptr,
                                      apm_module_data_port_conn_t **conn_cfg_pptr,
                                      apm_db_query_t                query_type)
{
   ar_result_t                  result = AR_EOK;
   spf_list_node_t             *curr_ptr;
   apm_module_data_port_conn_t *conn_list_node_ptr;

   /** Validate input arguments */
   if (!graph_info_ptr || !conn_cfg_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Graph info ptr [0x%lX] and/or conn pptr [0x%lX] is/are NULL",
             graph_info_ptr,
             conn_cfg_pptr);

      return AR_EFAILED;
   }

   /** Set the pointer to conn node to NULL */
   *conn_cfg_pptr = NULL;

   curr_ptr = graph_info_ptr->conn_cfg_list_ptr;

   /** Check if the connection instance already exists */
   while (NULL != curr_ptr)
   {
      conn_list_node_ptr = (apm_module_data_port_conn_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == conn_list_node_ptr)
      {
         /** return NULL handle */
         *conn_cfg_pptr = NULL;

         return AR_EFAILED;
      }

      /** Check if the connection matches exactly to any existing connections */
      if ((conn_list_node_ptr->src_mod_inst_id == conn_cfg_ptr->src_mod_inst_id) &&
          (conn_list_node_ptr->src_mod_op_port_id == conn_cfg_ptr->src_mod_op_port_id) &&
          (conn_list_node_ptr->dst_mod_inst_id == conn_cfg_ptr->dst_mod_inst_id) &&
          (conn_list_node_ptr->dst_mod_ip_port_id == conn_cfg_ptr->dst_mod_ip_port_id))
      {
         /** connection  found */
         *conn_cfg_pptr = conn_list_node_ptr;

         return result;
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   if (APM_DB_OBJ_QUERY == query_type)
   {
      /** The execution falls through here when the subgraph  is not
       *  found and if the query is to check the APM database if the
       *  sub graph ID is present or not in the database
       * it returns error if Sub graph handle is not found in the database*/

      AR_MSG(DBG_HIGH_PRIO,
             "GRAPH_MGMT:[multi-client] connection: [0x%lX][%d]->[0x%lX][%d] does not exist",
             conn_cfg_ptr->src_mod_inst_id,
             conn_cfg_ptr->src_mod_op_port_id,
             conn_cfg_ptr->dst_mod_inst_id,
             conn_cfg_ptr->dst_mod_ip_port_id);

      result = AR_EFAILED;
   }

   return result;
}

ar_result_t apm_db_get_mod_ctrl_link(apm_graph_info_t             *graph_info_ptr,
                                     apm_module_ctrl_link_cfg_t   *ctrl_link_cfg_ptr,
                                     apm_module_ctrl_port_conn_t **ctrl_link_cfg_pptr,
                                     apm_db_query_t                query_type)
{
   ar_result_t                  result = AR_EOK;
   spf_list_node_t             *curr_ptr;
   apm_module_ctrl_port_conn_t *ctrl_link_list_node_ptr;

   /** Validate input arguments */
   if (!graph_info_ptr || !ctrl_link_cfg_pptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "Graph info ptr [0x%lX] and/or conn pptr [0x%lX] is/are NULL",
             graph_info_ptr,
             ctrl_link_cfg_pptr);

      return AR_EFAILED;
   }

   /** Set the pointer to conn node to NULL */
   *ctrl_link_cfg_pptr = NULL;

   curr_ptr = graph_info_ptr->ctrl_link_cfg_list_ptr;

   /** Check if the connection instance already exists */
   while (NULL != curr_ptr)
   {
      ctrl_link_list_node_ptr = (apm_module_ctrl_port_conn_t *)curr_ptr->obj_ptr;

      /** validate the instance pointer */
      if (NULL == ctrl_link_list_node_ptr)
      {
         /** return NULL handle */
         *ctrl_link_cfg_pptr = NULL;

         return AR_EFAILED;
      }
      /** Check if the connection matches exactly to any existing connections */
      if ((ctrl_link_list_node_ptr->peer_1_mod_iid == ctrl_link_cfg_ptr->peer_1_mod_iid) &&
			(ctrl_link_list_node_ptr->peer_1_mod_ctrl_port_id == ctrl_link_cfg_ptr->peer_1_mod_ctrl_port_id) &&
			(ctrl_link_list_node_ptr->peer_2_mod_iid == ctrl_link_cfg_ptr->peer_2_mod_iid) &&
			(ctrl_link_list_node_ptr->peer_2_mod_ctrl_port_id == ctrl_link_cfg_ptr->peer_2_mod_ctrl_port_id)/* &&
			(ctrl_link_list_node_ptr->num_props == ctrl_link_cfg_ptr->num_props)*/)
      {
         /** connection  found */
         *ctrl_link_cfg_pptr = ctrl_link_list_node_ptr;
         return result;
      }

      /** Else, keep traversing the list */
      curr_ptr = curr_ptr->next_ptr;
   }

   if (APM_DB_OBJ_QUERY == query_type)
   {
      /** The execution falls through here when the subgraph  is not
       *  found and if the query is to check the APM database if the
       *  sub graph ID is present or not in the database
       * it returns error if Sub graph handle is not found in the database*/

      AR_MSG(DBG_HIGH_PRIO,
             "GRAPH_MGMT:[multi-client] ctrl link: [0x%lX][%d]->[0x%lX][%d] does not exist",
             ctrl_link_cfg_ptr->peer_1_mod_iid,
             ctrl_link_cfg_ptr->peer_1_mod_ctrl_port_id,
             ctrl_link_cfg_ptr->peer_2_mod_iid,
             ctrl_link_cfg_ptr->peer_2_mod_ctrl_port_id);

      result = AR_EFAILED;
   }

   return result;
}
