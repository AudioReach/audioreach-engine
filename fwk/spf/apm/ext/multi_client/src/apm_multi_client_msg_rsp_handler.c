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

ar_result_t apm_multi_client_graph_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr, uint32_t cmd_opcode)
{
   ar_result_t           result = AR_EOK;
   spf_msg_header_t     *msg_payload_ptr;
   apm_container_t      *container_node_ptr;
   apm_cont_cmd_ctrl_t  *cont_cmd_ctrl_ptr;
   apm_sub_graph_t      *sub_graph_node_ptr = NULL;
   spf_list_node_t      *sub_graph_list_rsp_ptr;
   spf_list_node_t      *sub_graph_list_apm_cmd_ptr;
   apm_client_context_t *client_node_ptr = NULL;

   /** Get the message payload pointer */
   msg_payload_ptr = (spf_msg_header_t *)rsp_msg_ptr->payload_ptr;

   /** Get the responding container command control object */
   cont_cmd_ctrl_ptr = (apm_cont_cmd_ctrl_t *)msg_payload_ptr->token.token_ptr;

   /** Get the responding container node pointer */
   container_node_ptr = (apm_container_t *)cont_cmd_ctrl_ptr->host_container_ptr;

   /** Get the SG list pointer for this container in this response*/
   sub_graph_list_rsp_ptr = container_node_ptr->sub_graph_list_ptr;

   /** Get the SG list pointer for this APM cmd*/
   if (cmd_opcode == APM_CMD_GRAPH_OPEN)
   {
   sub_graph_list_apm_cmd_ptr = apm_info_ptr->curr_cmd_ctrl_ptr->graph_open_cmd_ctrl.sg_id_list_ptr;
   }
   else
   {
      sub_graph_list_apm_cmd_ptr = apm_info_ptr->curr_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;
   }

   /*Get the GPR packet for client info*/
   gpr_packet_t *packet_curr_client = (gpr_packet_t *)apm_info_ptr->curr_cmd_ctrl_ptr->cmd_msg.payload_ptr;

   /*If the response is FAILED, (ETERMINATED should not be treated as Failure)*/
   if (AR_EOK != msg_payload_ptr->rsp_result && AR_ETERMINATED != msg_payload_ptr->rsp_result)
   {
      AR_MSG(DBG_LOW_PRIO,
             "[multi-client] rsp result[%d], for container [0x%lX], cmd [0x%lX]",
             msg_payload_ptr->rsp_result,
             container_node_ptr->container_id,
             cmd_opcode);

      /**get SGs from container <> apm cmd*/
      /**Remove the client context from SG as well as from SG connections*/

      while (sub_graph_list_rsp_ptr)
      {
         apm_sub_graph_t *sub_graph_node_rsp_ptr = (apm_sub_graph_t *)sub_graph_list_rsp_ptr->obj_ptr;

         while (sub_graph_list_apm_cmd_ptr)
         {
            apm_sub_graph_t *sub_graph_node_apm_cmd_ptr = (apm_sub_graph_t *)sub_graph_list_apm_cmd_ptr->obj_ptr;

            if (sub_graph_node_rsp_ptr->sub_graph_id == sub_graph_node_apm_cmd_ptr->sub_graph_id)
            {
               AR_MSG(DBG_LOW_PRIO,
                      "[multi-client] rsp for container [0x%lX] found SG [0x%lX] "
                      "for which client [0x%lX] domain [%d] context will be removed, cmd [0x%lX]",
                      container_node_ptr->container_id,
                      sub_graph_node_rsp_ptr->sub_graph_id,
                      packet_curr_client->src_port,
                      packet_curr_client->src_domain_id,
                      cmd_opcode);

               /**Get the SG node*/
               result = apm_db_get_sub_graph_node(&apm_info_ptr->graph_info,
                                                  sub_graph_node_rsp_ptr->sub_graph_id,
                                                  &sub_graph_node_ptr,
                                                  APM_DB_OBJ_QUERY);

               switch (cmd_opcode)
               {
                  case APM_CMD_GRAPH_OPEN:
                  {
                     /**Remove client context from the SG*/
                     if (AR_EOK != (result |= apm_multi_client_free_sub_graph_client_context(sub_graph_node_ptr,
                                                                                             packet_curr_client)))
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "[multi-client] rsp failed to remove client [0x%lX] domain [%d] from "
                               "SG[0x%lX], cmd [0x%lX] ",
                               packet_curr_client->src_port,
                               packet_curr_client->src_domain_id,
                               sub_graph_node_ptr->sub_graph_id,
                               cmd_opcode);
                     }

                     /**Remove client context from all the connections linked to this
                      * SG*/
                     if (AR_EOK != (result |= apm_multi_client_free_all_conn_client_context(apm_info_ptr,
                                                                                            sub_graph_node_ptr,
                                                                                            packet_curr_client)))
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "[multi-client] rsp failed to remove client [0x%lX] domain [%d] from "
                               "SG[0x%lX] connection, cmd [0x%lX]",
                               packet_curr_client->src_port,
                               packet_curr_client->src_domain_id,
                               sub_graph_node_ptr->sub_graph_id,
                               cmd_opcode);
                     }

                     /**Remove client context from all the ctrl links linked to this
                      * SG*/
                     if (AR_EOK != (result |= apm_multi_client_free_all_ctrl_link_client_context(apm_info_ptr,
                                                                                                 sub_graph_node_ptr,
                                                                                                 packet_curr_client)))
                     {
                        AR_MSG(DBG_ERROR_PRIO,
                               "[multi-client] rsp failed to remove client [0x%lX] domain [%d] from "
                               "SG[0x%lX] ctrl link, cmd [0x%lX] ",
                               packet_curr_client->src_port,
                               packet_curr_client->src_domain_id,
                               sub_graph_node_ptr->sub_graph_id,
                               cmd_opcode);
                     }
                     break;
                  }

                   /*For GRAPH management cmds*/
                  case APM_CMD_GRAPH_PREPARE:
                  case APM_CMD_GRAPH_START:
                  case APM_CMD_GRAPH_STOP:
                  case APM_CMD_GRAPH_CLOSE:
                  case APM_CMD_CLOSE_ALL:
                  {
            	       if(sub_graph_node_ptr->prop.scenario_id == APM_SUB_GRAPH_SID_VOICE_CALL)
                	  {
                		  AR_MSG(DBG_LOW_PRIO,
                				  "[multi-client] 'apm_multi_client_graph_cmd_rsp_hdlr' is "
                				  "being bypassed as SG[0x%lX] SID is 'APM_SUB_GRAPH_SID_VOICE_CALL'",
								  sub_graph_node_ptr->sub_graph_id);
                		  continue;
                	  }
                     /*Get the current client node from the SG client list.*/
                     apm_multi_client_get_client_node(sub_graph_node_ptr, packet_curr_client, &client_node_ptr);

                     uint32_t temp_state = client_node_ptr->curr_state; // debug purpose
                     /*undo client SG states*/
                     client_node_ptr->curr_state = client_node_ptr->prev_state;

                     AR_MSG(DBG_LOW_PRIO,
                            "[multi-client] rsp client [0x%lX] domain [%d] SG[0x%lX] state[%d] is "
                            "undone to prev-state[%d], cmd [0x%lX]",
                            packet_curr_client->src_port,
                            packet_curr_client->src_domain_id,
                            sub_graph_node_ptr->sub_graph_id,
                            temp_state,
                            client_node_ptr->curr_state,
                            cmd_opcode);
                     break;
                  }

                  default:
                     break;
               }
            }

            LIST_ADVANCE(sub_graph_list_apm_cmd_ptr);
         }

         LIST_ADVANCE(sub_graph_list_rsp_ptr);
      }
   }

   /**In case of graph open failure due to any callibration data (only)
    * rsp handled gets called only after clean up happens as part of graph open failure.
    * As part of graph open failure fwk cleans up all the SGs and destroyes the container.
    * So all the SG nodes are also destroyed and no need of reverting "is_calibrated" of "apm_subgraph_t".*/

   return result;
}
