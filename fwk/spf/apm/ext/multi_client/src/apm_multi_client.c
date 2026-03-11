/**
 * \file apm_multi_client.c
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
#include "apm_gpr_cmd_parser.h"

apm_multi_client_vtable_t multi_client_funcs = {
   .apm_multi_client_parse_sg_list_fptr = apm_multi_client_parse_sg_list,

   .apm_multi_client_add_sg_client_context_fptr = apm_multi_client_add_sg_client_context,

   .apm_multi_client_check_sg_state_and_update_send_to_seqncr_fptr =
      apm_multi_client_check_sg_state_and_update_send_to_seqncr,

   .apm_multi_client_parse_mod_list_fptr = apm_multi_client_parse_mod_list,

   .apm_multi_client_parse_mod_prop_list_fptr = apm_multi_client_parse_mod_prop_list,

   .apm_multi_client_cache_connections_fptr = apm_multi_client_cache_connections,

   .apm_multi_client_parse_mod_conn_list_fptr = apm_multi_client_parse_mod_conn_list,

   .apm_multi_client_clear_graph_mgmt_context_fptr = apm_multi_client_clear_graph_mgmt_context,

   .apm_multi_client_clear_graph_open_cmd_info_fptr = apm_multi_client_clear_graph_open_cmd_info,

   .apm_multi_client_graph_cmd_rsp_hdlr_fptr = apm_multi_client_graph_cmd_rsp_hdlr,

   .apm_multi_client_parse_mod_ctrl_link_cfg_fptr = apm_multi_client_parse_mod_ctrl_link_cfg,

   .apm_multi_client_cache_ctrl_links_fptr = apm_multi_client_cache_ctrl_links,

   .apm_multi_client_update_parsed_sg_list_fptr = apm_multi_client_update_parsed_sg_list,

   .apm_multi_client_update_sg_calibration_state_fptr = apm_multi_client_update_sg_calibration_state

};

ar_result_t apm_multi_client_init(apm_t *apm_info_ptr)
{
   ar_result_t result = AR_EOK;

   apm_info_ptr->ext_utils.multi_client_vtbl_ptr = &multi_client_funcs;

   return result;
}
/**
 * Checks client if already exists for the SG.
 * If same exists returns error.
 * If a new client for this SG adds SG to skip list and adds client context
 */

ar_result_t apm_multi_client_parse_sg_list(apm_sub_graph_t *sub_graph_node_ptr,
                                           apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr,
                                           uint8_t        **curr_payload_pptr)
{
   ar_result_t      result          = AR_EOK;
   spf_list_node_t *client_list_ptr = NULL;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   client_list_ptr = sub_graph_node_ptr->client_list_ptr;

   if (AR_EOK != (result = apm_multi_client_check_duplicate_client(client_list_ptr, packet_curr_client)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE:[multi-client] Duplicate SG_ID: [0x%lX] for client port: [0x%lX], domain [%d]",
             sub_graph_node_ptr->sub_graph_id,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);
      return result;
   }
   /** Cache this sub-graph ID in the APM graph open command
    *  control in skip list if client is different*/
   if (AR_EOK != (result = apm_db_add_node_to_list(&apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.skip_sg_id_list_ptr,
                                                   sub_graph_node_ptr,
                                                   &apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.num_skipped_sg)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "[multi-client] Failed to add client/port 0x%x, domain [%d] to SG (0x%X)skip list",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id,
             sub_graph_node_ptr->sub_graph_id);
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "SG_PARSE:[multi-client]SG node added to skip list for SG_ID: [0x%lX] for client port [0x%lX], domain [%d]",
          sub_graph_node_ptr->sub_graph_id,
          packet_curr_client->src_port,
          packet_curr_client->src_domain_id);

   /** Cache this client node in the SG client list ptr  */
   if (AR_EOK != (result = apm_multi_client_add_sg_client_context(sub_graph_node_ptr, apm_cmd_ctrl_ptr)))
   {
      return AR_EFAILED;
   }

   /** Get the pointer to start of property list struct */
   *curr_payload_pptr += sizeof(apm_sub_graph_cfg_t);
   /** Advance the pointer to point to next sub-graph config */
   *curr_payload_pptr += sub_graph_node_ptr->prop.payload_size;

   return result;
}

/**
 * Adds client context to SG node
 */

ar_result_t apm_multi_client_add_sg_client_context(apm_sub_graph_t *sub_graph_node_ptr,
                                                   apm_cmd_ctrl_t  *apm_cmd_ctrl_ptr)
{
   ar_result_t           result          = AR_EOK;
   apm_client_context_t *client_node_ptr = NULL;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** ALlocate memory for client node for sub graph client list */
   if (NULL == (client_node_ptr = (apm_client_context_t *)posal_memory_malloc(sizeof(apm_client_context_t),
                                                                              APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE:[multi-client] Failed to allocate client node mem, client/port: [0x%lX], domain [%d]",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);

      return AR_ENOMEMORY;
   }

   /** Clear the allocated struct */
   memset(client_node_ptr, 0, sizeof(apm_client_context_t));

   /**
    * caching the client info for which the subgraph is validated and added to the list for 1st time
    */
   client_node_ptr->client_domain_id = packet_curr_client->src_domain_id;
   client_node_ptr->client_port_id   = packet_curr_client->src_port;

   /**Assign the default state of SG for the client */
   client_node_ptr->curr_state = APM_SG_STATE_STOPPED;
   client_node_ptr->prev_state = APM_SG_STATE_INVALID;

   /** Cache this client node in the SG client list ptr  */
   if (AR_EOK != (result = apm_db_add_node_to_list(&sub_graph_node_ptr->client_list_ptr,
                                                   client_node_ptr,
                                                   &sub_graph_node_ptr->num_client)))
   {
      /** Free up the memory for allocated client node */
      if (client_node_ptr)
      {
         posal_memory_free(client_node_ptr);
      }

      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "SG_PARSE:[multi-client]For SG_ID: [0x%lX] client port [0x%lX], domain [%d] added to client list, ref count "
          "[%d]",
          sub_graph_node_ptr->sub_graph_id,
          packet_curr_client->src_port,
          packet_curr_client->src_domain_id,
          sub_graph_node_ptr->num_client);

   return result;
}

/**
 * Checks if the module belongs to the skip SG and can be skipped parsing.
 */

ar_result_t apm_multi_client_parse_mod_list(apm_cmd_ctrl_t     *apm_cmd_ctrl_ptr,
                                            apm_modules_list_t *module_list_ptr,
                                            uint8_t           **curr_mod_list_payload_ptr,
                                            bool_t             *mod_list_parse_req_ptr)
{
   ar_result_t result      = AR_EOK;
   *mod_list_parse_req_ptr = TRUE;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   spf_list_node_t *skip_sg_id_list_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.skip_sg_id_list_ptr;

   if (AR_EDUPLICATE == apm_multi_client_check_skip_sub_graph(skip_sg_id_list_ptr, module_list_ptr->sub_graph_id))
   {
      *mod_list_parse_req_ptr = FALSE;

      *curr_mod_list_payload_ptr +=
         (sizeof(apm_modules_list_t) + (module_list_ptr->num_modules * sizeof(apm_module_cfg_t)));

      AR_MSG(DBG_HIGH_PRIO,
             "MOD_PARSE:[multi-client] skipping module list for SG_ID:[0x%lX] for client port:[0x%lX], domain [%d]",
             module_list_ptr->sub_graph_id,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);
   }

   return result;
}

/**
 * checks if the module prop is the skip SG list and can be skipped
 */

ar_result_t apm_multi_client_parse_mod_prop_list(apm_cmd_ctrl_t *apm_cmd_ctrl_ptr,
                                                 apm_module_t   *module_node_ptr,
                                                 bool_t         *mod_prop_list_parse_req)
{
   ar_result_t result       = AR_EOK;
   *mod_prop_list_parse_req = TRUE;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   spf_list_node_t *skip_sg_id_list_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.skip_sg_id_list_ptr;

   if (AR_EDUPLICATE ==
       apm_multi_client_check_skip_sub_graph(skip_sg_id_list_ptr, module_node_ptr->host_sub_graph_ptr->sub_graph_id))
   {
      *mod_prop_list_parse_req = FALSE;

#ifdef DEBUG_MULTI_CLIENT
      AR_MSG(DBG_MED_PRIO,
             "MOD_PROP_PARSE:[multi-client] skipping module[0x%lX] prop list for SG_ID:[0x%lX] for client "
             "port:[0x%lX], domain [%d]",
             module_node_ptr->instance_id,
             module_node_ptr->host_sub_graph_ptr->sub_graph_id,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);
#endif
   }

   return result;
}

/**
 * Checks if this client exists for this connection.
 * If same client exist returns error.
 * For a new client returns success with adding client context to the conn cfg
 */

ar_result_t apm_multi_client_parse_mod_conn_list(apm_t                 *apm_info_ptr,
                                                 apm_module_conn_cfg_t *curr_conn_cfg_ptr,
                                                 uint8_t              **curr_payload_ptr,
                                                 uint32_t              *num_connections,
                                                 bool_t                *conn_parse_req)
{
   ar_result_t                  result          = AR_EOK;
   spf_list_node_t             *client_list_ptr = NULL;
   apm_module_data_port_conn_t *conn_cfg_ptr    = NULL;
   apm_cmd_ctrl_t              *apm_cmd_ctrl_ptr;
   uint32_t                     cmd_opcode;

   *conn_parse_req = TRUE;

   /** Get the current command opcode being processed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /**Get the connection if present in apm database*/
   result =
      apm_db_get_mod_connection(&apm_info_ptr->graph_info, curr_conn_cfg_ptr, &conn_cfg_ptr, APM_DB_OBJ_CREATE_REQD);

   if (conn_cfg_ptr)
   {
      if (APM_CMD_GRAPH_CLOSE == cmd_opcode)
      {
         AR_MSG(DBG_MED_PRIO,
                "[multi-client] removing connection[0x%lX -> 0x%lX] in CONN only GRAPH_CLOSE context",
                conn_cfg_ptr->src_mod_inst_id,
                conn_cfg_ptr->dst_mod_inst_id);
         /*free client context*/
         apm_multi_client_free_conn_client_context(conn_cfg_ptr, packet_curr_client);

         if (!conn_cfg_ptr->num_client)
         {
            /*if we don't have any client,*/
            /*remove the connection node*/
            apm_db_remove_node_from_list(&apm_info_ptr->graph_info.conn_cfg_list_ptr,
                                         (void *)conn_cfg_ptr,
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

         return result;
      }

      client_list_ptr = conn_cfg_ptr->client_list_ptr;

      if (AR_EOK != (result = apm_multi_client_check_duplicate_client(client_list_ptr, packet_curr_client)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_DATA_LNK_PARSE:[multi-client] Duplicate link: [(0x%lX)0x%lX -> 0x%lX(0x%lX)] for client port: "
                "[0x%lX], domain [%d]",
                conn_cfg_ptr->src_host_sub_graph_id,
                curr_conn_cfg_ptr->src_mod_inst_id,
                curr_conn_cfg_ptr->dst_mod_inst_id,
                conn_cfg_ptr->dst_host_sub_graph_id,
                packet_curr_client->src_port,
                packet_curr_client->src_domain_id);
         return result;
      }

      /*For different client add to the client list & increment ref count*/
      if (AR_EOK != (result = apm_multi_client_add_conn_client_context(apm_info_ptr, conn_cfg_ptr)))
      {
         return result;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "MOD_DATA_LNK_PARSE:[multi-client] SG boundary link: [0x%lX -> 0x%lX] ref count increased to [%d], client "
             "port: "
             "[0x%lX], domain [%d]",
             curr_conn_cfg_ptr->src_mod_inst_id,
             curr_conn_cfg_ptr->dst_mod_inst_id,
             conn_cfg_ptr->num_client,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);

      /** Advance the pointer to start of next connection object  */
      *curr_payload_ptr += (sizeof(apm_module_conn_cfg_t));

      /** Decrement the num connection counter */
      (*num_connections)--;

      *conn_parse_req = FALSE;
   }

   return result;
}

/**
 * Adds each connection to the APM data base along with current client context
 */

ar_result_t apm_multi_client_cache_connections(apm_t                 *apm_info_ptr,
                                               apm_module_t          *module_node_ptr_list[2],
                                               apm_module_conn_cfg_t *curr_conn_cfg_ptr,
                                               bool_t                *conn_skip_req)
{
   ar_result_t                  result       = AR_EOK;
   apm_module_data_port_conn_t *conn_cfg_ptr = NULL;
   apm_cmd_ctrl_t              *apm_cmd_ctrl_ptr;
   *conn_skip_req = FALSE;

   /** Get the current command opcode being processed */
   /**For GRAPH CLOSE cmd we don't need to cache connections*/
   if (APM_CMD_GRAPH_CLOSE == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode)
   {
      return result;
   }

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   if (NULL == module_node_ptr_list[0] || NULL == module_node_ptr_list[1])
   {
      AR_MSG(DBG_LOW_PRIO,
             "SG_PARSE:[multi-client] Don't have both src[0x%lX] & dest[0x%lX]  info at this point to cache",
             module_node_ptr_list[0],
             module_node_ptr_list[1]);
      return AR_EOK;
   }

   apm_sub_graph_t *mod1_host_sub_graph_ptr = module_node_ptr_list[0]->host_sub_graph_ptr;
   apm_sub_graph_t *mod2_host_sub_graph_ptr = module_node_ptr_list[1]->host_sub_graph_ptr;

   /**
    * Cache the connection which are across the SGs.i.e the 2 miids belong to 2 different SGs
    */
   if (mod1_host_sub_graph_ptr->sub_graph_id != mod2_host_sub_graph_ptr->sub_graph_id)
   {
      /**
       * Currently container code gu_validate_graph_open_cmd is rejecting if any connection is duplicated.
       * So we are now caching all the connections and ref counting if being sent again.
       * For now logic remains same if same client duplicate error , different client ref count.
       * But do we need to ref count for all connections (inside SG level) along with boundary connections ?
       */

      /** If the connection does not exist */

      /** ALlocate memory for conn node for APM graph DB */
      if (NULL ==
          (conn_cfg_ptr = (apm_module_data_port_conn_t *)posal_memory_malloc(sizeof(apm_module_data_port_conn_t),
                                                                             APM_INTERNAL_STATIC_HEAP_ID)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "SG_PARSE:[multi-client] Failed to allocate conn node mem, src: [0x%lX], dest [0x%lX]",
                curr_conn_cfg_ptr->src_mod_inst_id,
                curr_conn_cfg_ptr->dst_mod_inst_id);

         return AR_ENOMEMORY;
      }
      /** Clear the allocated struct */
      memset(conn_cfg_ptr, 0, sizeof(apm_module_data_port_conn_t));

      /**cache the conn info*/
      memscpy(conn_cfg_ptr, sizeof(apm_module_conn_cfg_t), curr_conn_cfg_ptr, sizeof(apm_module_conn_cfg_t));

      /*caching the source/dest mod host SG ID*/
      conn_cfg_ptr->src_host_sub_graph_id = mod1_host_sub_graph_ptr->sub_graph_id;
      conn_cfg_ptr->dst_host_sub_graph_id = mod2_host_sub_graph_ptr->sub_graph_id;

      if (AR_EOK != (result = apm_multi_client_add_conn_client_context(apm_info_ptr, conn_cfg_ptr)))
      {
         return result;
      }

      /** Cache this connection as this across SGs/boundary*/
      if (AR_EOK != (result = apm_db_add_node_to_list(&apm_info_ptr->graph_info.conn_cfg_list_ptr,
                                                      conn_cfg_ptr,
                                                      &apm_info_ptr->graph_info.num_conn)))
      {
         AR_MSG(DBG_ERROR_PRIO, "[multi-client] Failed to add connection list");
         return AR_EFAILED;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "MOD_DATA_LNK_PARSE:[multi-client] adding SG boundary link: [0x%lX -> 0x%lX] for client port: [0x%lX] "
             "domain [%d], "
             "src mod host SG "
             "[0x%lX]",
             curr_conn_cfg_ptr->src_mod_inst_id,
             curr_conn_cfg_ptr->dst_mod_inst_id,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id,
             conn_cfg_ptr->src_host_sub_graph_id);
   }
   else // connection is within the same SG
   {
      /**if the connection belongs to the same SG and part of the skip SG list then don't pass this connection to
       * container instead skip*/
      spf_list_node_t *skip_sg_id_list_ptr = apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.skip_sg_id_list_ptr;

      if (AR_EDUPLICATE ==
          apm_multi_client_check_skip_sub_graph(skip_sg_id_list_ptr, mod1_host_sub_graph_ptr->sub_graph_id))
      {
         *conn_skip_req = TRUE;
         AR_MSG(DBG_HIGH_PRIO,
                "MOD_DATA_LNK_PARSE:[multi-client] skipping within SG link: [0x%lX -> 0x%lX] for client port: [0x%lX] "
                "domain [%d], "
                "host SG "
                "[0x%lX] as SG is part of skip SG list",
                curr_conn_cfg_ptr->src_mod_inst_id,
                curr_conn_cfg_ptr->dst_mod_inst_id,
                packet_curr_client->src_port,
                packet_curr_client->src_domain_id,
                mod1_host_sub_graph_ptr->sub_graph_id);
      }
   }

   return result;
}

/**
 * Adds client context to the connection cfg
 */

ar_result_t apm_multi_client_add_conn_client_context(apm_t *apm_info_ptr, apm_module_data_port_conn_t *conn_cfg_ptr)
{
   ar_result_t           result          = AR_EOK;
   apm_client_context_t *client_node_ptr = NULL;
   apm_cmd_ctrl_t       *apm_cmd_ctrl_ptr;

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** ALlocate memory for client node for sub graph client list */
   if (NULL == (client_node_ptr = (apm_client_context_t *)posal_memory_malloc(sizeof(apm_client_context_t),
                                                                              APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE: Failed to allocat client node mem, client/port: [0x%lX] domain [%d]",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);

      return AR_ENOMEMORY;
   }

   /** Clear the allocated struct */
   memset(client_node_ptr, 0, sizeof(apm_client_context_t));

   /**Cache the client info*/
   client_node_ptr->client_domain_id = packet_curr_client->src_domain_id;
   client_node_ptr->client_port_id   = packet_curr_client->src_port;

   /** Cache this client node in the SG client list ptr  */
   if (AR_EOK !=
       (result = apm_db_add_node_to_list(&conn_cfg_ptr->client_list_ptr, client_node_ptr, &conn_cfg_ptr->num_client)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "[multi-client] Failed to add client:[0x%lX] domain [%d] to client list",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);
      return AR_EFAILED;
   }

   return result;
}

ar_result_t apm_multi_client_parse_mod_ctrl_link_cfg(apm_t                      *apm_info_ptr,
                                                     apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr,
                                                     uint8_t                   **curr_payload_ptr,
                                                     uint32_t                   *num_ctrl_links,
                                                     bool_t                     *conn_parse_req)
{
   ar_result_t                  result            = AR_EOK;
   spf_list_node_t             *client_list_ptr   = NULL;
   apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr = NULL;
   apm_cmd_ctrl_t              *apm_cmd_ctrl_ptr;
   uint8_t                     *prop_data_ptr;
   uint32_t                     prop_data_size;
   uint32_t                     cmd_opcode;

   *conn_parse_req = TRUE;

   /** Get the current command opcode being processed */
   cmd_opcode = apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode;

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /**Get the connection if present in apm database*/
   result = apm_db_get_mod_ctrl_link(&apm_info_ptr->graph_info,
                                     curr_ctrl_link_cfg_ptr,
                                     &ctrl_link_cfg_ptr,
                                     APM_DB_OBJ_CREATE_REQD);

   if (ctrl_link_cfg_ptr)
   {
      if (APM_CMD_GRAPH_CLOSE == cmd_opcode)
      {
         AR_MSG(DBG_MED_PRIO,
                "[multi-client] removing ctrl link[0x%lX -> 0x%lX] in CTRL LINK only GRAPH_CLOSE context",
                ctrl_link_cfg_ptr->peer_1_mod_iid,
                ctrl_link_cfg_ptr->peer_2_mod_iid);
         /*free client context*/
         apm_multi_client_free_ctrl_link_client_context(ctrl_link_cfg_ptr, packet_curr_client);

         if (!ctrl_link_cfg_ptr->num_client)
         {
            /*if we don't have any client,*/
            /*remove the connection node*/
            apm_db_remove_node_from_list(&apm_info_ptr->graph_info.ctrl_link_cfg_list_ptr,
                                         (void *)ctrl_link_cfg_ptr,
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

         return result;
      }

      client_list_ptr = ctrl_link_cfg_ptr->client_list_ptr;

      if (AR_EOK != (result = apm_multi_client_check_duplicate_client(client_list_ptr, packet_curr_client)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "MOD_DATA_LNK_PARSE:[multi-client] Duplicate ctrl link: [(0x%lX)0x%lX -> 0x%lX(0x%lX)] for client "
                "port: "
                "[0x%lX], domain [%d]",
                ctrl_link_cfg_ptr->peer_1_sub_graph_id,
                curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
                curr_ctrl_link_cfg_ptr->peer_2_mod_iid,
                ctrl_link_cfg_ptr->peer_2_sub_graph_id,
                packet_curr_client->src_port,
                packet_curr_client->src_domain_id);
         return result;
      }

      /*For different client add to the client list & increment ref count*/
      if (AR_EOK != (result = apm_multi_client_add_ctrl_link_client_context(apm_info_ptr, ctrl_link_cfg_ptr)))
      {
         return result;
      }

      AR_MSG(DBG_HIGH_PRIO,
             "MOD_DATA_LNK_PARSE:[multi-client] ctrl link: [0x%lX -> 0x%lX] ref count increased to [%d], client port: "
             "[0x%lX], domain [%d]",
             curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
             curr_ctrl_link_cfg_ptr->peer_2_mod_iid,
             ctrl_link_cfg_ptr->num_client,
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);

      /** Get container prop data payload size */
      prop_data_ptr = (*curr_payload_ptr + sizeof(apm_module_ctrl_link_cfg_t));

      /** Get the overall property data size for this module prop configuration */
      if (AR_EOK != (result = apm_get_prop_data_size(prop_data_ptr,
                                                     curr_ctrl_link_cfg_ptr->num_props,
                                                     &prop_data_size,
                                                     apm_info_ptr->curr_cmd_ctrl_ptr->cmd_payload_size)))
      {
         return result;
      }

      /** Advance the pointer to start of next control link config
       *  object */
      *curr_payload_ptr += (sizeof(apm_module_ctrl_link_cfg_t) + prop_data_size);

      /** Decrement the num connection counter */
      (*num_ctrl_links)--;

      *conn_parse_req = FALSE;
   }

   return result;
}

ar_result_t apm_multi_client_cache_ctrl_links(apm_t                      *apm_info_ptr,
                                              apm_module_t               *module_node_ptr_list[2],
                                              apm_module_ctrl_link_cfg_t *curr_ctrl_link_cfg_ptr)
{
   ar_result_t                  result            = AR_EOK;
   apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr = NULL;
   apm_cmd_ctrl_t              *apm_cmd_ctrl_ptr;

   /** Get the current command opcode being processed */
   /**For GRAPH CLOSE cmd we don't need to cache connections*/
   if (APM_CMD_GRAPH_CLOSE == apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode)
   {
      return result;
   }

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   if (NULL == module_node_ptr_list[0] || NULL == module_node_ptr_list[1])
   {
      AR_MSG(DBG_LOW_PRIO,
             "SG_PARSE:[multi-client] Don't have both peer1[0x%lX] & peer2[0x%lX]  info at this point to cache",
             module_node_ptr_list[0],
             module_node_ptr_list[1]);
      return AR_EOK;
   }

   apm_sub_graph_t *mod1_host_sub_graph_ptr = module_node_ptr_list[0]->host_sub_graph_ptr;
   apm_sub_graph_t *mod2_host_sub_graph_ptr = module_node_ptr_list[1]->host_sub_graph_ptr;

   /** If the connection does not exist */

   /** ALlocate memory for conn node for APM graph DB */
   if (NULL ==
       (ctrl_link_cfg_ptr = (apm_module_ctrl_port_conn_t *)posal_memory_malloc(sizeof(apm_module_ctrl_port_conn_t),
                                                                               APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE:[multi-client] Failed to allocate ctrl link node mem, peer1: [0x%lX], peer2 [0x%lX]",
             curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
             curr_ctrl_link_cfg_ptr->peer_2_mod_iid);

      return AR_ENOMEMORY;
   }
   /** Clear the allocated struct */
   memset(ctrl_link_cfg_ptr, 0, sizeof(apm_module_ctrl_port_conn_t));

   /**cache the conn info*/
   memscpy(ctrl_link_cfg_ptr,
           sizeof(apm_module_ctrl_link_cfg_t),
           curr_ctrl_link_cfg_ptr,
           sizeof(apm_module_ctrl_link_cfg_t));

   /*caching the source/dest mod host SG ID*/
   ctrl_link_cfg_ptr->peer_1_sub_graph_id = mod1_host_sub_graph_ptr->sub_graph_id;
   ctrl_link_cfg_ptr->peer_2_sub_graph_id = mod2_host_sub_graph_ptr->sub_graph_id;

   if (AR_EOK != (result = apm_multi_client_add_ctrl_link_client_context(apm_info_ptr, ctrl_link_cfg_ptr)))
   {
      return result;
   }

   /** Cache this connection as this across SGs/boundary*/
   if (AR_EOK != (result = apm_db_add_node_to_list(&apm_info_ptr->graph_info.ctrl_link_cfg_list_ptr,
                                                   ctrl_link_cfg_ptr,
                                                   &apm_info_ptr->graph_info.num_ctrl_link)))
   {
      AR_MSG(DBG_ERROR_PRIO, "[multi-client] Failed to add connection list");
      return AR_EFAILED;
   }

   AR_MSG(DBG_HIGH_PRIO,
          "MOD_DATA_LNK_PARSE:[multi-client] adding ctrl link: [0x%lX -> 0x%lX] for client port: [0x%lX], peer1 host "
          "SG "
          "[0x%lX]",
          curr_ctrl_link_cfg_ptr->peer_1_mod_iid,
          curr_ctrl_link_cfg_ptr->peer_2_mod_iid,
          packet_curr_client->src_port,
          ctrl_link_cfg_ptr->peer_1_sub_graph_id);

   return result;
}

ar_result_t apm_multi_client_add_ctrl_link_client_context(apm_t                       *apm_info_ptr,
                                                          apm_module_ctrl_port_conn_t *ctrl_link_cfg_ptr)
{
   ar_result_t           result          = AR_EOK;
   apm_client_context_t *client_node_ptr = NULL;
   apm_cmd_ctrl_t       *apm_cmd_ctrl_ptr;

   /* Get the pointer to current APM command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** ALlocate memory for client node for sub graph client list */
   if (NULL == (client_node_ptr = (apm_client_context_t *)posal_memory_malloc(sizeof(apm_client_context_t),
                                                                              APM_INTERNAL_STATIC_HEAP_ID)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "SG_PARSE: Failed to allocat client node mem, client/port: [0x%lX] domain [%d]",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);

      return AR_ENOMEMORY;
   }

   /** Clear the allocated struct */
   memset(client_node_ptr, 0, sizeof(apm_client_context_t));

   /**Cache the client info*/
   client_node_ptr->client_domain_id = packet_curr_client->src_domain_id;
   client_node_ptr->client_port_id   = packet_curr_client->src_port;

   /** Cache this client node in the SG client list ptr  */
   if (AR_EOK != (result = apm_db_add_node_to_list(&ctrl_link_cfg_ptr->client_list_ptr,
                                                   client_node_ptr,
                                                   &ctrl_link_cfg_ptr->num_client)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "[multi-client] Failed to add client:[0x%lX] domain [%d] to client list",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id);
      return AR_EFAILED;
   }

   return result;
}

/**
 * Decides to put the SG to the APM cmn sequencer list
 */

ar_result_t apm_multi_client_check_sg_state_and_update_send_to_seqncr(apm_t           *apm_info_ptr,
                                                                      apm_sub_graph_t *sub_graph_node_ptr,
                                                                      uint32_t         cmd_opcode,
                                                                      bool_t          *seq_entry_reqd)
{
   ar_result_t           result = AR_EOK;
   apm_sub_graph_state_t curr_sg_state;
   apm_sub_graph_state_t new_cmd_state;
   apm_sub_graph_state_t aggregated_state = APM_SG_STATE_INVALID;
   apm_client_context_t *client_node_ptr  = NULL;

   /** Validate input arguments  */
   if (!sub_graph_node_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO, "sub_graph_node_ptr (0x%lX) is NULL", sub_graph_node_ptr);

      return AR_EFAILED;
   }

   /*For GRAPH management cmds*/
   if(sub_graph_node_ptr->prop.scenario_id == APM_SUB_GRAPH_SID_VOICE_CALL)
   {
	   *seq_entry_reqd = TRUE;

	   AR_MSG(DBG_LOW_PRIO,
			  "[multi-client] 'check sg state and update send to seqncr' is "
			  "being bypassed as SG[0x%lX] SID is 'APM_SUB_GRAPH_SID_VOICE_CALL'",
			  sub_graph_node_ptr->sub_graph_id);
	   return AR_EOK;
   }

   /*
    * FLUSH cmd is NOT a GRAPH mgmt cmd
    * FLUSH is NOT handled as graph mgmt cmd
    * FLUSH cmd issued for particular reason, like after PAUSE, and will be executed as it is.
    * SPF can't decide when to execute this CMD based on GRAPH STATE.
    *
    */
   if (APM_CMD_GRAPH_FLUSH == cmd_opcode)
   {
      *seq_entry_reqd = TRUE;
      return AR_EOK;
   }

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /*current actual state of the sub-graph*/
   curr_sg_state = sub_graph_node_ptr->state;

   /*new state of SG from the APM CMD*/
   apm_multi_client_get_translated_apm_sg_state(cmd_opcode, &new_cmd_state);

   /*Get the current client node from the SG client list.*/
   apm_multi_client_get_client_node(sub_graph_node_ptr, packet_curr_client, &client_node_ptr);

   if (client_node_ptr == NULL)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "[multi-client] Alien client [0x%lX] domain [%d] sending graph management cmd [0x%lX] for SG [0x%lX]",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id,
             cmd_opcode,
             sub_graph_node_ptr->sub_graph_id);
      return AR_EFAILED;
   }

#ifdef DEBUG_MULTI_CLIENT
   AR_MSG(DBG_HIGH_PRIO,
          "[multi-client] current mgmt cmd [0x%lX], client [0x%lX] domain [%d], current SG[0x%lX] state %d, client "
          "state %d, new "
          "client translated state %d, client prev state %d",
          cmd_opcode,
          client_node_ptr->client_port_id,
          client_node_ptr->client_domain_id,
          sub_graph_node_ptr->sub_graph_id,
          curr_sg_state,
          client_node_ptr->curr_state,
          new_cmd_state,
          client_node_ptr->prev_state);
#endif

   if (new_cmd_state > curr_sg_state)
   {
      *seq_entry_reqd = TRUE;

      client_node_ptr->prev_state = client_node_ptr->curr_state;
      client_node_ptr->curr_state = new_cmd_state;
   }
   else if (new_cmd_state < curr_sg_state)
   {
      /*aggregate MAX of all client state except the current client state*/
      apm_multi_client_aggregate_state(sub_graph_node_ptr->client_list_ptr, client_node_ptr, &aggregated_state);

      if (new_cmd_state >= aggregated_state)
      {
         *seq_entry_reqd = TRUE;
      }
      else
      {
         *seq_entry_reqd = FALSE;
      }

      // TODO table exmpale to understand the code

#ifdef DEBUG_MULTI_CLIENT
      AR_MSG(DBG_HIGH_PRIO,
             "[multi-client] SG [0x%lX] aggregated state %d for cmd [0x%lX] for the client [0x%lX] domain [%d]",
             sub_graph_node_ptr->sub_graph_id,
             aggregated_state,
             cmd_opcode,
             client_node_ptr->client_port_id,
             client_node_ptr->client_domain_id);
#endif

      client_node_ptr->prev_state = client_node_ptr->curr_state;
      client_node_ptr->curr_state = new_cmd_state;
   }
   else // new_cmd_state == curr_sg_state
   {
      *seq_entry_reqd = FALSE;

      if (client_node_ptr->curr_state == new_cmd_state)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "[multi-client] SG [0x%lX] is already in the state %d for cmd [0x%lX] for the client [0x%lX] domain "
                "[%d]",
                sub_graph_node_ptr->sub_graph_id,
                client_node_ptr->curr_state,
                cmd_opcode,
                client_node_ptr->client_port_id,
                client_node_ptr->client_domain_id);
         return AR_EFAILED;
      }

      client_node_ptr->prev_state = client_node_ptr->curr_state;
      client_node_ptr->curr_state = new_cmd_state;
   }

   // TODO- Aggregation descision w.r.t SUSPEND state needs to be revisited

   AR_MSG(DBG_HIGH_PRIO,
          "[multi-client] send to sequencer %d, new state %d for client [0x%lX] domain [%d]",
          *seq_entry_reqd,
          client_node_ptr->curr_state,
          client_node_ptr->client_port_id,
          client_node_ptr->client_domain_id);

   /*When GRAPH_CLOSE from a client but we can't honor it because other client still in other state,
    *we have to clear this/current client context only.
    *If we don't clear we will see duplicate client if this client again sends graph_mgmt cmd.*/

   if ((*seq_entry_reqd == FALSE) && (APM_CMD_GRAPH_CLOSE == cmd_opcode))
   {
      AR_MSG(DBG_HIGH_PRIO,
             "[multi-client] Removing all client context for client [0x%lX] domain [%d] for SG [0x%lX] for GRAPH_CLOSE "
             "but allow "
             "sequencer FALSE",
             packet_curr_client->src_port,
             packet_curr_client->src_domain_id,
             sub_graph_node_ptr->sub_graph_id);

      apm_multi_client_free_sub_graph_client_context(sub_graph_node_ptr, packet_curr_client);

      apm_multi_client_free_all_conn_client_context(apm_info_ptr, sub_graph_node_ptr, packet_curr_client);

      apm_multi_client_free_all_ctrl_link_client_context(apm_info_ptr, sub_graph_node_ptr, packet_curr_client);
   }

   return result;
}

/**
 * Clears spf list pool nodes and frees client & pointers used
 * for graph management states
 */

ar_result_t apm_multi_client_clear_graph_mgmt_context(apm_t *apm_info_ptr, apm_sub_graph_t *sub_graph_node_ptr)
{
   ar_result_t   result             = AR_EOK;
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   apm_multi_client_free_sub_graph_client_context(sub_graph_node_ptr, packet_curr_client);

   apm_multi_client_free_conn(apm_info_ptr, sub_graph_node_ptr);

   apm_multi_client_free_ctrl_link(apm_info_ptr, sub_graph_node_ptr);

   return result;
}

/**
 * Clears SG spf list pool nodes used in current cmd context
 */

ar_result_t apm_multi_client_clear_graph_open_cmd_info(apm_t *apm_info_ptr)
{
   apm_graph_open_cmd_ctrl_t *graph_open_cmd_ctrl_ptr;

   /** Get the pointer to graph open command control   */
   graph_open_cmd_ctrl_ptr = &apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl;

   if (apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl.skip_sg_id_list_ptr)
   {
      spf_list_delete_list(&graph_open_cmd_ctrl_ptr->skip_sg_id_list_ptr, TRUE /* pool_used */);

      AR_MSG(DBG_LOW_PRIO,
             "[multi-client] skip sub graph list of[%d] deleted from APM curr cmd ctrl ",
             graph_open_cmd_ctrl_ptr->num_skipped_sg);

      graph_open_cmd_ctrl_ptr->num_skipped_sg = 0;
   }

   return AR_EOK;
}

/**
 *  Check if subgraph is present subgraph list
 */

bool_t apm_multi_client_check_sg_in_parsed_sg_list(uint32_t sub_graph_id, apm_t *apm_info_ptr)
{
   if (NULL == apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.parsed_sg_list_ptr)
   {

#ifdef DEBUG_MULTI_CLIENT
      AR_MSG(DBG_MED_PRIO, "[multi-client] Subgraph id:[0x%lX] is not present in parsed Subgraph list ", sub_graph_id);
#endif

      return FALSE;
   }

   spf_list_node_t *curr_ptr = apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.parsed_sg_list_ptr;

   while (NULL != curr_ptr)
   {
      uint32_t *sgid = (uint32_t *)(curr_ptr->obj_ptr);
      if (*sgid == sub_graph_id)
      {

#ifdef DEBUG_MULTI_CLIENT
         AR_MSG(DBG_MED_PRIO,
                "[multi-client] Subgraph id:[0x%lX] is already present in parsed Subgraph list ",
                sub_graph_id);
#endif
         return TRUE;
      }

      curr_ptr = curr_ptr->next_ptr;
   }

#ifdef DEBUG_MULTI_CLIENT
   AR_MSG(DBG_MED_PRIO, "[multi-client] Subgraph id:[0x%lX] is not present in parsed Subgraph list ", sub_graph_id);
#endif

   return FALSE;
}

/**
 *  Add the subgraph node to parsed subgraph list
 */

ar_result_t apm_multi_client_update_parsed_sg_list(uint32_t sub_graph_id, apm_t *apm_info_ptr)
{
   ar_result_t   result             = AR_EOK;
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /** If subgraph is already present in parsed subgraph list then don't add to the list */
   if (FALSE == apm_multi_client_check_sg_in_parsed_sg_list(sub_graph_id, apm_info_ptr))
   {
      uint32_t *sub_graph = (uint32_t *)posal_memory_malloc(4, POSAL_HEAP_DEFAULT);
      *sub_graph          = sub_graph_id;

      /** Adding subgraph to parsed subgraph list */
      result = apm_db_add_node_to_list(&apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.parsed_sg_list_ptr,
                                       sub_graph,
                                       &apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.num_subgraph_node);

      if (AR_EOK != result)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "[multi-client]:Client[0x%lX]- FAILED to add subgraph:0x%lX to the subgraph_parsed_list",
                packet_curr_client->src_port,
                sub_graph_id);
         return result;
      }

#ifdef DEBUG_MULTI_CLIENT
      if (AR_EOK == result)
      {
         AR_MSG(DBG_MED_PRIO,
                "[multi-client]:Client[0x%lX]- Subgraph:0x%lX added to the subgraph_parsed_list",
                packet_curr_client->src_port,
                sub_graph_id);
      }
#endif
   }

   return result;
}

/**
 *  Update the calibration state of subgraph
 */

ar_result_t apm_multi_client_update_sg_calibration_state(apm_t *apm_info_ptr)
{
   ar_result_t      result             = AR_EOK;
   apm_sub_graph_t *sub_graph_node_ptr = NULL;
   spf_list_node_t *temp_cached_ptr;
   spf_list_node_t *curr_ptr = apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.parsed_sg_list_ptr;

   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   while (NULL != curr_ptr)
   {
      uint32_t sub_graph_id = *((uint32_t *)(curr_ptr->obj_ptr));

      result =
         apm_db_get_sub_graph_node(&apm_info_ptr->graph_info, sub_graph_id, &sub_graph_node_ptr, APM_DB_OBJ_QUERY);

      if (AR_EOK != result || NULL == sub_graph_node_ptr)
      {
         curr_ptr = curr_ptr->next_ptr;

         AR_MSG(DBG_ERROR_PRIO,
                "[multi-client]:Client[0x%lX]- Error subgraph [0x%lX] not found in subgraph node",
                packet_curr_client->src_port,
                sub_graph_id);

         result = AR_EFAILED;
         break;
      }

      sub_graph_node_ptr->is_calibrated = TRUE;

      AR_MSG(DBG_HIGH_PRIO,
             "[multi-client]:Client[0x%lX]- Subgraph [0x%lX] is marked as calibrated",
             packet_curr_client->src_port,
             sub_graph_id);

      temp_cached_ptr = curr_ptr;

      apm_db_remove_node_from_list(&apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.parsed_sg_list_ptr,
                                   (void *)(curr_ptr->obj_ptr),
                                   &apm_info_ptr->curr_cmd_ctrl_ptr->db_default_set_cfg_cmd_ctrl.num_subgraph_node);

      posal_memory_free(temp_cached_ptr->obj_ptr);
      curr_ptr = curr_ptr->next_ptr;
   }

   return result;
}
