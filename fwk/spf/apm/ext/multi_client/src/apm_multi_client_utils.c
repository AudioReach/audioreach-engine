/**
 * \file apm_multi_client_utils.c
 *
 * \brief
 *
 *     This file handles multi client requests.
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
#include "apm_internal.h"

ar_result_t apm_multi_client_check_duplicate_client(spf_list_node_t *client_list_ptr, gpr_packet_t *packet_curr_client)
{
   ar_result_t result = AR_EOK;

   /**Iterate through all client to figure out if current client is again sending the cmd*/
   while (client_list_ptr)
   {
      apm_client_context_t *node_ptr = (apm_client_context_t *)client_list_ptr->obj_ptr;

      if (CHECK_CLIENT_AND_DOMAIN(node_ptr, packet_curr_client))
      {
         return AR_EDUPLICATE;
      }

      LIST_ADVANCE(client_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_check_skip_sub_graph(spf_list_node_t *skip_sg_id_list_ptr, uint32_t sub_graph_id)
{
   ar_result_t result = AR_EOK;

   while (skip_sg_id_list_ptr)
   {
      apm_sub_graph_t *skip_sub_graph_node_ptr = (apm_sub_graph_t *)skip_sg_id_list_ptr->obj_ptr;

      if (sub_graph_id == skip_sub_graph_node_ptr->sub_graph_id)
      {
         return AR_EDUPLICATE;
      }

      LIST_ADVANCE(skip_sg_id_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_get_translated_apm_sg_state(uint32_t cmd_opcode, apm_sub_graph_state_t *state_ptr)
{
   ar_result_t result = AR_EOK;

   *state_ptr = APM_SG_STATE_INVALID;

   switch (cmd_opcode)
   {
      case APM_CMD_GRAPH_PREPARE:
         *state_ptr = APM_SG_STATE_PREPARED;
         break;

      case APM_CMD_GRAPH_STOP:
         *state_ptr = APM_SG_STATE_STOPPED;
         break;

      case APM_CMD_GRAPH_START:
         *state_ptr = APM_SG_STATE_STARTED;
         break;

      case APM_CMD_GRAPH_SUSPEND:
         *state_ptr = APM_SG_STATE_SUSPENDED;
         break;

      case APM_CMD_GRAPH_CLOSE:
         break;
      case APM_CMD_CLOSE_ALL:
         break;
      case APM_CMD_GRAPH_FLUSH:
         break;

      default:
         break;
   }

   return result;
}

ar_result_t apm_multi_client_get_client_node(apm_sub_graph_t       *sub_graph_node_ptr,
                                             gpr_packet_t          *packet_curr_client,
                                             apm_client_context_t **client_node_pptr)
{
   ar_result_t      result          = AR_EOK;
   spf_list_node_t *client_list_ptr = NULL;

   client_list_ptr = sub_graph_node_ptr->client_list_ptr;

   /**Iterate through all client to figure out if current client is again sending the cmd*/
   while (client_list_ptr)
   {
      apm_client_context_t *node_ptr = (apm_client_context_t *)client_list_ptr->obj_ptr;

      if (CHECK_CLIENT_AND_DOMAIN(node_ptr, packet_curr_client))
      {
#ifdef DEBUG_MULTI_CLIENT
         AR_MSG(DBG_HIGH_PRIO,
                "[multi-client] Found client [0x%lX] domain [%d] info",
                node_ptr->client_port_id,
                node_ptr->client_domain_id);
#endif
         *client_node_pptr = node_ptr;
         break;
      }

      LIST_ADVANCE(client_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_aggregate_state(spf_list_node_t       *client_list_ptr,
                                             apm_client_context_t  *client_node_ptr,
                                             apm_sub_graph_state_t *aggregated_state)
{
   ar_result_t result = AR_EOK;

   while (client_list_ptr)
   {
      apm_client_context_t *node_ptr = (apm_client_context_t *)client_list_ptr->obj_ptr;

      if (node_ptr->client_port_id != client_node_ptr->client_port_id) // Except the current client
      {
         *aggregated_state = MAX(*aggregated_state, node_ptr->curr_state);
      }

      LIST_ADVANCE(client_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_free_client_context(spf_list_node_t **client_list_pptr,
                                                 gpr_packet_t     *packet_curr_client,
                                                 uint32_t         *num_client,
                                                 uint32_t         *client_port_id,
                                                 uint32_t         *client_domain_id,
                                                 bool_t           *is_client_freed)
{
   ar_result_t      result          = AR_EOK;
   spf_list_node_t *client_list_ptr = *client_list_pptr;
   *is_client_freed                 = FALSE;

   while (client_list_ptr)
   {
      apm_client_context_t *client_ptr = (apm_client_context_t *)client_list_ptr->obj_ptr;

      /*remove the client context of the current client*/
      if (CHECK_CLIENT_AND_DOMAIN(client_ptr, packet_curr_client))
      {
         // For debugging purpose
         *client_port_id   = client_ptr->client_port_id;
         *client_domain_id = client_ptr->client_domain_id;

         result = apm_db_remove_node_from_list(client_list_pptr, client_ptr, num_client);

         *is_client_freed = TRUE;
         posal_memory_free((void *)client_ptr);
         client_ptr = NULL;
         break;
      }

      LIST_ADVANCE(client_list_ptr);
   }
   return result;
}

ar_result_t apm_multi_client_free_sub_graph_client_context(apm_sub_graph_t *sub_graph_node_ptr,
                                                           gpr_packet_t    *packet_curr_client)
{
   ar_result_t result = AR_EOK;
   uint32_t    client_port_id;
   uint32_t    client_domain_id;
   bool_t      is_client_freed = FALSE;

   if (AR_EOK != (result = apm_multi_client_free_client_context(&sub_graph_node_ptr->client_list_ptr,
                                                                packet_curr_client,
                                                                &sub_graph_node_ptr->num_client,
                                                                &client_port_id,
                                                                &client_domain_id,
                                                                &is_client_freed)))
   {
      return result;
   }

   if (is_client_freed)
   {
      AR_MSG(DBG_LOW_PRIO,
             "[multi-client] SG[0x%lX] client[0x%lX] domain [%d] removed, remaining num client[%d]",
             sub_graph_node_ptr->sub_graph_id,
             client_port_id,
             client_domain_id,
             sub_graph_node_ptr->num_client);
   }

   return result;
}

ar_result_t apm_multi_client_free_conn_client_context(apm_module_data_port_conn_t *conn_cfg_ptr,
                                                      gpr_packet_t                *packet_curr_client)
{
   ar_result_t result = AR_EOK;
   uint32_t    client_port_id;
   uint32_t    client_domain_id;
   bool_t      is_client_freed = FALSE;

   if (AR_EOK != (result = apm_multi_client_free_client_context(&conn_cfg_ptr->client_list_ptr,
                                                                packet_curr_client,
                                                                &conn_cfg_ptr->num_client,
                                                                &client_port_id,
                                                                &client_domain_id,
                                                                &is_client_freed)))
   {
      return result;
   }

   if (is_client_freed)
   {
      AR_MSG(DBG_LOW_PRIO,
             "[multi-client] conn [0x%lX -> 0x%lX] client [0x%lX] domain [%d] removed, remaining num client[%d]",
             conn_cfg_ptr->src_mod_inst_id,
             conn_cfg_ptr->dst_mod_inst_id,
             client_port_id,
             client_domain_id,
             conn_cfg_ptr->num_client);
   }

   return result;
}

ar_result_t apm_multi_client_free_ctrl_link_client_context(apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr,
                                                           gpr_packet_t                *packet_curr_client)
{
   ar_result_t result = AR_EOK;
   uint32_t    client_port_id;
   uint32_t    client_domain_id;
   bool_t      is_client_freed = FALSE;

   if (AR_EOK != (result = apm_multi_client_free_client_context(&ctrl_link_cfg_ptr->client_list_ptr,
                                                                packet_curr_client,
                                                                &ctrl_link_cfg_ptr->num_client,
                                                                &client_port_id,
                                                                &client_domain_id,
                                                                &is_client_freed)))
   {
      return result;
   }

   if (is_client_freed)
   {
      AR_MSG(DBG_LOW_PRIO,
             "[multi-client] ctrl link [0x%lX -> 0x%lX] client [0x%lX] domain [%d] removed, remaining num client[%d]",
             ctrl_link_cfg_ptr->peer_1_mod_iid,
             ctrl_link_cfg_ptr->peer_2_mod_iid,
             client_port_id,
             client_domain_id,
             ctrl_link_cfg_ptr->num_client);
   }

   return result;
}

ar_result_t apm_multi_client_free_conn(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr)
{
   ar_result_t      result            = AR_EOK;
   spf_list_node_t *conn_cfg_list_ptr = NULL;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   conn_cfg_list_ptr = apm_info_ptr->graph_info.conn_cfg_list_ptr;

   while (conn_cfg_list_ptr)
   {
      apm_module_data_port_conn_t *conn_cfg_ptr = (apm_module_data_port_conn_t *)conn_cfg_list_ptr->obj_ptr;

      // All the connection from this SG should be removed either originating or ending
      if (sub_graph_node_ptr->sub_graph_id == conn_cfg_ptr->src_host_sub_graph_id ||
          sub_graph_node_ptr->sub_graph_id == conn_cfg_ptr->dst_host_sub_graph_id)
      {
         /*free client context*/
         apm_multi_client_free_conn_client_context(conn_cfg_ptr, packet_curr_client);

         AR_MSG(DBG_LOW_PRIO,
                "[multi-client] check- conn [0x%lX -> 0x%lX], At this point num_client [%d] must be 0",
                conn_cfg_ptr->src_mod_inst_id,
                conn_cfg_ptr->dst_mod_inst_id,
                conn_cfg_ptr->num_client);

         /*if we don't have any client,*/
         /*remove the connection node*/

         apm_db_remove_node_from_list(&apm_info_ptr->graph_info.conn_cfg_list_ptr,
                                      conn_cfg_list_ptr->obj_ptr,
                                      &apm_info_ptr->graph_info.num_conn);

         AR_MSG(DBG_LOW_PRIO,
                "[multi-client] conn [(0x%lX)0x%lX -> 0x%lX] removed, remaining num conn [%d]",
                conn_cfg_ptr->src_host_sub_graph_id,
                conn_cfg_ptr->src_mod_inst_id,
                conn_cfg_ptr->dst_mod_inst_id,
                apm_info_ptr->graph_info.num_conn);

         posal_memory_free((void *)conn_cfg_ptr);
         conn_cfg_ptr = NULL;
      }

      LIST_ADVANCE(conn_cfg_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_free_ctrl_link(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr)
{
   ar_result_t      result                 = AR_EOK;
   spf_list_node_t *ctrl_link_cfg_list_ptr = NULL;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   ctrl_link_cfg_list_ptr = apm_info_ptr->graph_info.ctrl_link_cfg_list_ptr;

   while (ctrl_link_cfg_list_ptr)
   {
      apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr = (apm_module_ctrl_port_conn_t *)ctrl_link_cfg_list_ptr->obj_ptr;

      // All the connection from this SG should be removed either originating or ending
      if (sub_graph_node_ptr->sub_graph_id == ctrl_link_cfg_ptr->peer_1_sub_graph_id ||
          sub_graph_node_ptr->sub_graph_id == ctrl_link_cfg_ptr->peer_2_sub_graph_id)
      {
         /*free client context*/
         apm_multi_client_free_ctrl_link_client_context(ctrl_link_cfg_ptr, packet_curr_client);

         AR_MSG(DBG_LOW_PRIO,
                "[multi-client] check- ctrl link [0x%lX -> 0x%lX], At this point num_client [%d] must be 0",
                ctrl_link_cfg_ptr->peer_1_mod_iid,
                ctrl_link_cfg_ptr->peer_2_mod_iid,
                ctrl_link_cfg_ptr->num_client);

         /*if we don't have any client,*/
         /*remove the connection node*/

         apm_db_remove_node_from_list(&apm_info_ptr->graph_info.ctrl_link_cfg_list_ptr,
                                      ctrl_link_cfg_list_ptr->obj_ptr,
                                      &apm_info_ptr->graph_info.num_ctrl_link);

         AR_MSG(DBG_LOW_PRIO,
                "[multi-client] ctrl link [(0x%lX)0x%lX -> 0x%lX] removed, remaining num ctrl link [%d]",
                ctrl_link_cfg_ptr->peer_1_sub_graph_id,
                ctrl_link_cfg_ptr->peer_1_mod_iid,
                ctrl_link_cfg_ptr->peer_2_mod_iid,
                apm_info_ptr->graph_info.num_ctrl_link);

         posal_memory_free((void *)ctrl_link_cfg_ptr);
         ctrl_link_cfg_ptr = NULL;
      }

      LIST_ADVANCE(ctrl_link_cfg_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_free_all_conn_client_context(apm_t           *apm_info_ptr,
                                                          apm_sub_graph_t *sub_graph_node_ptr,
                                                          gpr_packet_t    *packet_curr_client)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *conn_cfg_list_ptr = apm_info_ptr->graph_info.conn_cfg_list_ptr;

   while (conn_cfg_list_ptr)
   {
      apm_module_data_port_conn_t *conn_cfg_ptr = (apm_module_data_port_conn_t *)conn_cfg_list_ptr->obj_ptr;

      if (sub_graph_node_ptr->sub_graph_id == conn_cfg_ptr->src_host_sub_graph_id ||
          sub_graph_node_ptr->sub_graph_id == conn_cfg_ptr->dst_host_sub_graph_id)
      {
         apm_multi_client_free_conn_client_context(conn_cfg_ptr, packet_curr_client);
      }

      LIST_ADVANCE(conn_cfg_list_ptr);
   }

   return result;
}

ar_result_t apm_multi_client_free_all_ctrl_link_client_context(apm_t           *apm_info_ptr,
                                                               apm_sub_graph_t *sub_graph_node_ptr,
                                                               gpr_packet_t    *packet_curr_client)
{
   ar_result_t result = AR_EOK;

   spf_list_node_t *conn_cfg_list_ptr = apm_info_ptr->graph_info.ctrl_link_cfg_list_ptr;

   while (conn_cfg_list_ptr)
   {
      apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr = (apm_module_ctrl_port_conn_t *)conn_cfg_list_ptr->obj_ptr;

      if (sub_graph_node_ptr->sub_graph_id == ctrl_link_cfg_ptr->peer_1_sub_graph_id ||
          sub_graph_node_ptr->sub_graph_id == ctrl_link_cfg_ptr->peer_2_sub_graph_id)
      {
         apm_multi_client_free_ctrl_link_client_context(ctrl_link_cfg_ptr, packet_curr_client);
      }

      LIST_ADVANCE(conn_cfg_list_ptr);
   }

   return result;
}

bool_t apm_check_is_multi_client()
{
   return TRUE;
}
