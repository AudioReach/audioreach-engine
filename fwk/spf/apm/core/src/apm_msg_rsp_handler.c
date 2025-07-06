/**
 * \file apm_msg_rsp_handler.c
 *
 * \brief
 *     This file contains APM response queue function handlers and utility functions
 *
 *
 * \copyright
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/** =======================================================================
INCLUDE FILES FOR MODULE
========================================================================== */

#include "apm_cmd_utils.h"
#include "apm_gpr_if.h"
#include "apm_graph_utils.h"
#include "apm_internal.h"
#include "apm_proxy_def.h"
#include "apm_proxy_utils.h"
#include "apm_msg_rsp_handler.h"
#include "apm_offload_utils.h"
#include "apm_cmd_sequencer.h"

/** -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */

static ar_result_t apm_aggregate_cont_cmd_response(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t          result = AR_EOK;
   spf_msg_header_t *   msg_payload_ptr;
   apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the pointer to message header */
   msg_payload_ptr = (spf_msg_header_t *)rsp_msg_ptr->payload_ptr;

   /** Get the responding container command control object */
   cont_cmd_ctrl_ptr = (apm_cont_cmd_ctrl_t *)msg_payload_ptr->token.token_ptr;
   /** Get the APM command control object corresponding to this
    *  response */
   apm_cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   /** Update the current APM cmd corresponding to current
    *  response in process */
   apm_info_ptr->curr_cmd_ctrl_ptr = apm_cmd_ctrl_ptr;

   /** Cache the response result */
   cont_cmd_ctrl_ptr->rsp_ctrl.rsp_result = msg_payload_ptr->rsp_result;

   /** Save the response payload from container */
   cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg = *rsp_msg_ptr;

   /** Increment the response received counter */
   apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_rcvd++;

   /** Check if the return opcode is ETERMINATED, it should not
    *  be treated as failed response */
   if (AR_ETERMINATED != msg_payload_ptr->rsp_result)
   {
      /** Aggregate response result, send the unique result. If one or more failure with different error code occured
       * send AR_EFAILED. */
      if (AR_EOK != msg_payload_ptr->rsp_result && AR_EOK != apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status)
      {
         if (apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status != msg_payload_ptr->rsp_result)
         {
            apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status = AR_EFAILED;
         }
      }
      else if (AR_EOK != msg_payload_ptr->rsp_result && AR_EOK == apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status)
      {
         apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status = msg_payload_ptr->rsp_result;
      }

      /** Increment the failed response counter for any error code */
      if (AR_EOK != msg_payload_ptr->rsp_result)
      {
         /** Increment the failed response counter */
         apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_failed++;
      }
   }

   /** If number of response rcvd equal to number of cmds
    *  issued, set the response pending flag to FALSE */
   if (apm_cmd_ctrl_ptr->rsp_ctrl.num_rsp_rcvd == apm_cmd_ctrl_ptr->rsp_ctrl.num_cmd_issued)
   {
      /** Clear the response pending flag */
      apm_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending = FALSE;

      /** If all containers returned responses and opcode
          is GET_CFG, then cmd must have processed fully */
      if (APM_CMD_GET_CFG == apm_cmd_ctrl_ptr->cmd_opcode)
      {
         /** Cache aggregate rsp_status instead of
          *  moving to cmd_status and clearing */
         apm_cmd_ctrl_ptr->agg_rsp_status = apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status;
      }
      else if (AR_EOK != apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status)
      {
         /** If any of the container returned error, update the
          *  overall command status
          *  If no error occured previously, update the command status
          *  with first error received */
         if (AR_EOK == apm_cmd_ctrl_ptr->cmd_status)
         {
            apm_cmd_ctrl_ptr->cmd_status = apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status;
         }
         else if (apm_cmd_ctrl_ptr->cmd_status != apm_cmd_ctrl_ptr->rsp_ctrl.rsp_status)
         {
            /** Cmd is failed already and next container response is also
             *  failed but with a different error code, then overall
             *  failure code is EFAILED. Client can parse the error code
             *  in each PID to identify individual failures */
            apm_cmd_ctrl_ptr->cmd_status = AR_EFAILED;
         }
      }

      /** Set the current seq up status */
      if (AR_ETERMINATED != apm_cmd_ctrl_ptr->cmd_status)
      {
         apm_cmd_ctrl_ptr->cmd_seq.curr_op_seq_ptr->status = apm_cmd_ctrl_ptr->cmd_status;
      }

      /** Clear the APM cmd response control */
      apm_clear_cmd_rsp_ctrl(&apm_cmd_ctrl_ptr->rsp_ctrl);
   }
   else
   {
      result = AR_EPENDING;
   }

   return result;
}

ar_result_t apm_cmn_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;

   /** Get the pointer to current APM command control object */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Clear the pending container list, if non-empty.
    *  This will also release the cached container resposne
    *  messages */
   if (apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr)
   {
      /** Clear the list of pending containers */
      apm_clear_container_list(apm_cmd_ctrl_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container,
                               APM_CONT_CACHED_CFG_RELEASE,
                               APM_CONT_CMD_PARAMS_RELEASE,
                               APM_PENDING_CONT_LIST);
   }

   return result;
}

ar_result_t apm_graph_open_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;

   /** Get the pointer to current APM cmd control */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   if (AR_EOK == apm_cmd_ctrl_ptr->cmd_status)
   {
      /** If any error occured during the peer connection   */
      if (AR_EOK != (result = apm_connect_peer_containers_ext_ports(apm_info_ptr)))
      {
         apm_cmd_ctrl_ptr->cmd_status = result;
      }

      /**For multi_client context, remove the client context from SGs if the open failed at container level.
       * Should be done only in case APM cmd control status is SUCCESS ( in cmd control status failure case
       * multi-client code already handling it*/
      if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
          apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_graph_cmd_rsp_hdlr_fptr)
      {
         result |=
            apm_info_ptr->ext_utils.multi_client_vtbl_ptr
               ->apm_multi_client_graph_cmd_rsp_hdlr_fptr(apm_info_ptr, rsp_msg_ptr, apm_cmd_ctrl_ptr->cmd_opcode);
      }
   }
   else /** Container open failed */
   {
      /** If the overal command status is failure, set the graph
       *  open command status to execute appropriate clean up
       *  routines */

      if (apm_info_ptr->ext_utils.err_hdlr_vtbl_ptr &&
          apm_info_ptr->ext_utils.err_hdlr_vtbl_ptr->apm_err_hdlr_cache_container_rsp_fptr)
      {
         result = apm_info_ptr->ext_utils.err_hdlr_vtbl_ptr->apm_err_hdlr_cache_container_rsp_fptr(apm_info_ptr);
      }

      /** Change the overall command status */
      apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status = APM_OPEN_CMD_STATE_OPEN_FAIL;
   }

   /** Whether graph open response  is success or failure,
    *  container graph need to be update for further graph
    *  management processing on them in either case  */
   if (AR_EOK != (result |= apm_update_cont_graph_list(&apm_info_ptr->graph_info)))
   {
      apm_cmd_ctrl_ptr->cmd_status = result;

      /** Change the overall command status */
      apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status = APM_OPEN_CMD_STATE_OPEN_FAIL;
   }

   return result;
}

ar_result_t apm_graph_connect_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)

{
   ar_result_t     result = AR_EOK;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;

   /** Get the pointer to APM cmd control object corresponding
    *  to current response */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Clear the pending container list, if non-empty.
    *  This will also release the cached container response
    *  messages */
   if (apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr)
   {
      /** Clear the list of pending containers */
      apm_clear_container_list(apm_cmd_ctrl_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                               &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container,
                               APM_CONT_CACHED_CFG_RELEASE,
                               APM_CONT_CMD_PARAMS_RELEASE,
                               APM_PENDING_CONT_LIST);
   }

   /** If any of the container returned error */
   if (AR_EOK != apm_cmd_ctrl_ptr->cmd_status)
   {
      /** Containers returning SUCCESS response need to be
       *  disconnected and close.
       *  Containers returned FAILURE response need to be closed
       *  only */
      apm_cmd_ctrl_ptr->graph_open_cmd_ctrl.cmd_status = APM_OPEN_CMD_STATE_CONNECT_FAIL;
   }

   return result;
}

ar_result_t apm_destroy_container(apm_t *apm_info_ptr, apm_container_t *container_node_ptr)
{
   ar_result_t          result = AR_EOK;
   apm_container_t *    container_peer_node_ptr;
   spf_list_node_t *    curr_peer_list_node_ptr;
   uint32_t             container_id;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;
   apm_ext_utils_t *    ext_utils_ptr;

   /** Get the pointer to ext utils vtbl   */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Get the pointer to container's command control object */
   if (AR_EOK != (result = apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr,
                                                               apm_info_ptr->curr_cmd_ctrl_ptr,
                                                               &cont_cmd_ctrl_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_destroy_container(): Failed to get cmd ctrl obj for CONT_ID [0x%lX]",
             container_node_ptr->container_id);

      return AR_EFAILED;
   }

   /** Validate the cmd control pointer */
   if (!cont_cmd_ctrl_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_destroy_container(): Cont cmd ctrl ptr is NULL, CONT_ID [0x%lX]",
             container_node_ptr->container_id);

      return AR_EFAILED;
   }

   /** Cache container ID */
   container_id = container_node_ptr->container_id;

   if (ext_utils_ptr->offload_vtbl_ptr && ext_utils_ptr->offload_vtbl_ptr->apm_clear_cont_satellite_cont_list_fptr)
   {
      if (AR_EOK !=
          (result = ext_utils_ptr->offload_vtbl_ptr->apm_clear_cont_satellite_cont_list_fptr(apm_info_ptr,
                                                                                             container_node_ptr)))
      {
         return result;
      }
   }

   /** remove container from all data paths*/
   if (ext_utils_ptr->data_path_vtbl_ptr &&
       ext_utils_ptr->data_path_vtbl_ptr->apm_clear_closed_cntr_from_data_paths_fptr)
   {
      if (AR_EOK !=
          (result = ext_utils_ptr->data_path_vtbl_ptr->apm_clear_closed_cntr_from_data_paths_fptr(apm_info_ptr,
                                                                                                  container_node_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_destroy_container(): Failed to clear container from data paths, CONT_ID [0x%lX]",
                container_node_ptr->container_id);
      }
   }

   /** Remove this container from the APM global module list */
   if (AR_EOK != (result = apm_db_remove_obj_from_list(apm_info_ptr->graph_info.container_list_ptr,
                                                       container_node_ptr,
                                                       container_node_ptr->container_id,
                                                       APM_OBJ_TYPE_CONTAINER,
                                                       &apm_info_ptr->graph_info.num_containers)))
   {
      return result;
   }

   /** If this container is part of any sorted container graph,
    *  remove it from that graph */
   if (container_node_ptr->cont_graph_ptr)
   {
      apm_remove_cont_from_graph(&apm_info_ptr->graph_info, container_node_ptr);
   }

   /** Get the pointer to this container's upstream container
    *  list */
   curr_peer_list_node_ptr = container_node_ptr->peer_list.upstream_cont_list_ptr;

   /** Iterate over this container's upstream containers */
   while (curr_peer_list_node_ptr)
   {
      container_peer_node_ptr = (apm_container_t *)curr_peer_list_node_ptr->obj_ptr;

      /** Delete this container from peer container's downstream peer
       *  container  list    */
      apm_db_remove_node_from_list(&container_peer_node_ptr->peer_list.downstream_cont_list_ptr,
                                   container_node_ptr,
                                   &container_peer_node_ptr->peer_list.num_downstream_cont);

      /** Advance to next node */
      curr_peer_list_node_ptr = curr_peer_list_node_ptr->next_ptr;
   }

   /** Get the pointer to this container's downstream container
    *  list */
   curr_peer_list_node_ptr = container_node_ptr->peer_list.downstream_cont_list_ptr;

   /** Iterate over this container's downstream containers */
   while (curr_peer_list_node_ptr)
   {
      container_peer_node_ptr = (apm_container_t *)curr_peer_list_node_ptr->obj_ptr;

      /** Delete this container from peer container's downstream peer
       *  container  list    */
      apm_db_remove_node_from_list(&container_peer_node_ptr->peer_list.upstream_cont_list_ptr,
                                   container_node_ptr,
                                   &container_peer_node_ptr->peer_list.num_upstream_cont);

      /** Advance to next node */
      curr_peer_list_node_ptr = curr_peer_list_node_ptr->next_ptr;
   }

   /** Delete upstream peer container list for this container */
   spf_list_delete_list(&container_node_ptr->peer_list.upstream_cont_list_ptr, TRUE /* pool_used */);

   /** Clear the list pointer and container counter */
   container_node_ptr->peer_list.upstream_cont_list_ptr = NULL;
   container_node_ptr->peer_list.num_upstream_cont      = 0;

   /** Delete downstream peer container list for this container */
   spf_list_delete_list(&container_node_ptr->peer_list.downstream_cont_list_ptr, TRUE /* pool_used */);

   /** Clear the list pointer and container counter */
   container_node_ptr->peer_list.downstream_cont_list_ptr = NULL;
   container_node_ptr->peer_list.num_downstream_cont      = 0;

   /** Release the cached configuration for this container */
   apm_release_cont_msg_cached_cfg(&cont_cmd_ctrl_ptr->cached_cfg_params, apm_info_ptr->curr_cmd_ctrl_ptr);

   /** Release all the params cached as part of the current
    *  command processed */
   apm_cont_release_cmd_params(cont_cmd_ctrl_ptr, apm_info_ptr->curr_cmd_ctrl_ptr);

   /** Call base container destroy API */
   if (AR_EOK != (result = cntr_cmn_destroy(container_node_ptr->cont_hdl_ptr)))
   {
      AR_MSG(DBG_ERROR_PRIO,
             "GRAPH_CLOSE: Failed to destroy container CONT_ID: 0x%lX",
             container_node_ptr->container_id);
   }

   /** Free up container memory */
   posal_memory_free(container_node_ptr);

   AR_MSG(DBG_HIGH_PRIO, "GRAPH_CLOSE: CONT_ID [0x%lX] destroyed", container_id);

   return result;
}

ar_result_t apm_destroy_cont_port_peer_sg_id(spf_list_node_t * cached_port_conn_list_per_sg_pair_ptr,
                                             spf_list_node_t **port_conn_list_per_sg_pair_pptr,
                                             uint32_t *        num_port_conn_list_per_sg_pair_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_node_ptr;
   apm_cont_port_connect_info_t *port_connect_info_node_ptr = NULL;

   curr_node_ptr = cached_port_conn_list_per_sg_pair_ptr;

   /** Iterate over the container port conn list */
   while (curr_node_ptr)
   {
      /** Get the pointer to input port handle list per sub-graph ID
       *  pair */
      port_connect_info_node_ptr = (apm_cont_port_connect_info_t *)curr_node_ptr->obj_ptr;

      if (port_connect_info_node_ptr)
      {
         spf_list_delete_list_and_free_objs(&port_connect_info_node_ptr->port_conn_list_ptr, TRUE /* pool_used */);

         port_connect_info_node_ptr->port_conn_list_ptr = NULL;
         port_connect_info_node_ptr->num_port_conn      = 0;

         /** Delete this port connect info node from container input
          *  port handle list */
         apm_db_remove_node_from_list(port_conn_list_per_sg_pair_pptr,
                                      port_connect_info_node_ptr,
                                      num_port_conn_list_per_sg_pair_ptr);

         /** Free up the per sub-graph port connection list node */
         posal_memory_free(port_connect_info_node_ptr);
      }

      /** Advance to next port handle list node */
      curr_node_ptr = curr_node_ptr->next_ptr;

   } /** End of while(cached  port handles) */

   return result;
}

ar_result_t apm_destroy_cont_port_conn(apm_t *          apm_info_ptr,
                                       spf_list_node_t *cached_port_conn_list_ptr,
                                       apm_list_t *     port_conn_list_per_sg_pair_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_cached_port_conn_node_ptr;
   spf_list_node_t *             curr_conn_list_per_sg_pair_node_ptr, *next_conn_list_per_sg_pair_node_ptr;
   spf_list_node_t *             curr_port_conn_node_ptr, *next_port_conn_node_ptr;
   apm_cont_port_connect_info_t *port_conn_list_per_sg_ptr;
   spf_module_port_conn_t *      cached_port_conn_obj_ptr;
   spf_module_port_conn_t *      port_conn_obj_ptr;
   uint32_t                      peer_sg_state = APM_SG_STATE_INVALID;

   curr_cached_port_conn_node_ptr = cached_port_conn_list_ptr;

   /** Iterate over the list of cached container port handles
    *  which are not grouped under particular sub-graph id pair.
    *  Find these handles under the container book keeping of
    *  port handles per sub-graph ID pair and destroy them */
   while (curr_cached_port_conn_node_ptr)
   {
      cached_port_conn_obj_ptr = (spf_module_port_conn_t *)curr_cached_port_conn_node_ptr->obj_ptr;

      /** Get the pointer to list of port handles per sub-graph ID
       *  pair */
      curr_conn_list_per_sg_pair_node_ptr = port_conn_list_per_sg_pair_ptr->list_ptr;

      /** Iterate over the list of container book keeping struct
       *  for port handle list per sub-graph ID pair */
      while (curr_conn_list_per_sg_pair_node_ptr)
      {
         /** Get the list node object for current port handle list per
          *  sub-graph ID pair */
         port_conn_list_per_sg_ptr = (apm_cont_port_connect_info_t *)curr_conn_list_per_sg_pair_node_ptr->obj_ptr;

         /** Get the next list node pointer */
         next_conn_list_per_sg_pair_node_ptr = curr_conn_list_per_sg_pair_node_ptr->next_ptr;

         /** Get the pointer to corresponding port handle list */
         curr_port_conn_node_ptr = port_conn_list_per_sg_ptr->port_conn_list_ptr;

         /** Iterate over the list of port handles */
         while (curr_port_conn_node_ptr)
         {
            /** Get the pointer to port connection object */
            port_conn_obj_ptr = (spf_module_port_conn_t *)curr_port_conn_node_ptr->obj_ptr;

            /** Get the next list node pointer */
            next_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;

            /** Check if the cached port handle matches with book keeping
             *  struct, then delete it */
            if (port_conn_obj_ptr == cached_port_conn_obj_ptr)
            {
               /** Remove this connection from the module book keeping */
               if ((PORT_TYPE_DATA_OP == port_conn_obj_ptr->self_mod_port_hdl.port_type) &&
                   apm_info_ptr->ext_utils.data_path_vtbl_ptr &&
                   apm_info_ptr->ext_utils.data_path_vtbl_ptr->apm_clear_module_single_port_conn_fptr)
               {
                  apm_info_ptr->ext_utils.data_path_vtbl_ptr->apm_clear_module_single_port_conn_fptr(apm_info_ptr,
                                                                                                     port_conn_obj_ptr);
               }

               /** Delete this node from the list */
               spf_list_delete_node_and_free_obj(&curr_port_conn_node_ptr,
                                                 &port_conn_list_per_sg_ptr->port_conn_list_ptr,
                                                 TRUE);

               /** Decrement number of connections */
               port_conn_list_per_sg_ptr->num_port_conn--;
            }

            /** Advance the list pointer */
            curr_port_conn_node_ptr = next_port_conn_node_ptr;

         } /** End of while (port hdl list under single sg id pair )*/

         /** If the number of port connections reaches zero, then
          *  delete the list node for port handle list per sub-graph
          *  ID pair */
         if (!port_conn_list_per_sg_ptr->num_port_conn)
         {
            /** If the head node is getting removed, update the start of
             *  the list pointer */
            if (curr_conn_list_per_sg_pair_node_ptr == port_conn_list_per_sg_pair_ptr->list_ptr)
            {
               port_conn_list_per_sg_pair_ptr->list_ptr = curr_conn_list_per_sg_pair_node_ptr->next_ptr;
            }

            /** Check if the peer sub-graph exists, if so, cache the current
             *  state */
            if (port_conn_list_per_sg_ptr->peer_sg_obj_ptr)
            {
               peer_sg_state = port_conn_list_per_sg_ptr->peer_sg_obj_ptr->state;
            }
            else
            {
               peer_sg_state = APM_SG_STATE_INVALID;
            }

            /** If the sub-graph ID's are different for this connection,
             *  this is inter sub-graph edge, remove it from the global
             *  graph data base  */
            if ((APM_SG_STATE_INVALID == peer_sg_state) ||
                ((APM_SG_STATE_INVALID != peer_sg_state) && (port_conn_list_per_sg_ptr->self_sg_obj_ptr->sub_graph_id !=
                                                             port_conn_list_per_sg_ptr->peer_sg_obj_ptr->sub_graph_id)))
            {
               apm_db_remove_node_from_list(&apm_info_ptr->graph_info.sub_graph_conn_list_ptr,
                                            port_conn_list_per_sg_ptr,
                                            &apm_info_ptr->graph_info.num_sub_graph_conn);
            }

            /** Delete the list of peer containers   */
            if (port_conn_list_per_sg_ptr->peer_cont_list.list_ptr)
            {
               spf_list_delete_list(&port_conn_list_per_sg_ptr->peer_cont_list.list_ptr, TRUE /** Pool used*/);

               /** CLear num peer containers  */
               port_conn_list_per_sg_ptr->peer_cont_list.num_nodes = 0;
            }

            /** Delete this node from the list */
            spf_list_delete_node_and_free_obj(&curr_conn_list_per_sg_pair_node_ptr,
                                              &port_conn_list_per_sg_pair_ptr->list_ptr,
                                              TRUE);

            /** Decrement the number of port handle list per sub-graph ID
             *  pair */
            (port_conn_list_per_sg_pair_ptr->num_nodes)--;
         }

         /** Else, keep traversing the list */
         curr_conn_list_per_sg_pair_node_ptr = next_conn_list_per_sg_pair_node_ptr;

      } /** End of while (container port hdl list per sg id pair )*/

      curr_cached_port_conn_node_ptr = curr_cached_port_conn_node_ptr->next_ptr;

   } /** End of while (container cached port hdl list)*/

   return result;
}

ar_result_t apm_destroy_cont_port_self_sg_id(apm_t *              apm_info_ptr,
                                             apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr,
                                             apm_list_t *         port_conn_list_per_sg_pair_ptr)
{
   ar_result_t                   result = AR_EOK;
   apm_cmd_ctrl_t *              apm_cmd_ctrl_ptr;
   spf_list_node_t *             curr_port_conn_node_ptr, *next_port_conn_node_ptr;
   apm_cont_port_connect_info_t *curr_port_conn_info_ptr;
   spf_list_node_t *             curr_sg_node_ptr;
   apm_sub_graph_t *             sub_graph_obj_ptr;

   /** Validate input arguments */
   if (!apm_info_ptr || !cont_cmd_ctrl_ptr || !port_conn_list_per_sg_pair_ptr)
   {
      AR_MSG(DBG_ERROR_PRIO,
             "apm_destroy_cont_port_self_sg_id(): Invalid i/p arg, apm_info_ptr[0x%lX], cont_cmd_ctrl_ptr[0x%lX], "
             "port_conn_list_per_sg_pair_ptr[0x%lX]",
             apm_info_ptr,
             cont_cmd_ctrl_ptr,
             port_conn_list_per_sg_pair_ptr);

      return AR_EFAILED;
   }

   /** Get the pointer to current APM command control  */
   apm_cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   /** Get the pointer to current SG ID list under process */
   curr_sg_node_ptr = apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.sg_list.reg_sg_list_ptr;

   /** Iterate over all the sub-graph ID's being processed as
    *  part of current APM command */
   while (curr_sg_node_ptr)
   {
      /** Get the current sub-graph object */
      sub_graph_obj_ptr = (apm_sub_graph_t *)curr_sg_node_ptr->obj_ptr;

      /** Get the pointer to the list of port connection list per
       *  sub-graph ID pair */
      curr_port_conn_node_ptr = port_conn_list_per_sg_pair_ptr->list_ptr;

      /** Iterate over the list of cont port handle list per SG-ID
       *  pair */
      while (curr_port_conn_node_ptr)
      {
         curr_port_conn_info_ptr = (apm_cont_port_connect_info_t *)curr_port_conn_node_ptr->obj_ptr;

         /** Get the next node pointer */
         next_port_conn_node_ptr = curr_port_conn_node_ptr->next_ptr;

         /** If the self sub-graph ID matches, remove this node from
          *  the list */
         if (curr_port_conn_info_ptr->self_sg_obj_ptr->sub_graph_id == sub_graph_obj_ptr->sub_graph_id)
         {
            /** Remove any dangling sub-graph edges */
            apm_db_remove_node_from_list(&apm_info_ptr->graph_info.sub_graph_conn_list_ptr,
                                         curr_port_conn_info_ptr,
                                         &apm_info_ptr->graph_info.num_sub_graph_conn);

            /** Delete the list of peer containers   */
            if (curr_port_conn_info_ptr->peer_cont_list.list_ptr)
            {
               spf_list_delete_list(&curr_port_conn_info_ptr->peer_cont_list.list_ptr, TRUE /** Pool used*/);

               /** CLear num peer containers  */
               curr_port_conn_info_ptr->peer_cont_list.num_nodes = 0;
            }

            /** Delete all the container port handles under current self
             *  sub-graph ID */
            spf_list_delete_list_and_free_objs(&curr_port_conn_info_ptr->port_conn_list_ptr, TRUE /* pool_used */);

            /** Delete this node from the list */
            spf_list_delete_node_and_free_obj(&curr_port_conn_node_ptr,
                                              &port_conn_list_per_sg_pair_ptr->list_ptr,
                                              TRUE);

            /** Decrement the number of list node objects */
            (port_conn_list_per_sg_pair_ptr->num_nodes)--;
         }

         /** Advance to next node in the list */
         curr_port_conn_node_ptr = next_port_conn_node_ptr;

      } /** End of while (cont_port_handle list) */

      /** Advance to next sub-graph in the list */

      curr_sg_node_ptr = curr_sg_node_ptr->next_ptr;

   } /** End of while (sg_id_list) */

   return result;
}

ar_result_t apm_destroy_cont_port_conn_list(apm_t *              apm_info_ptr,
                                            apm_container_t *    container_node_ptr,
                                            apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cached_cont_list_t cont_list_type;
   apm_list_t(*cont_cached_port_list_pptr)[PORT_TYPE_MAX];
   apm_list_t(*cont_db_port_list_pptr)[PORT_TYPE_MAX];

   /** Get the pointer to container's cached configuration
    *  parameters corresponding to current cmd-response */
   cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

   cont_cached_port_list_pptr = cont_cached_cfg_ptr->graph_mgmt_params.cont_ports;

   cont_db_port_list_pptr = container_node_ptr->cont_ports_per_sg_pair;

   cont_list_type = apm_get_curr_active_cont_list_type(apm_info_ptr->curr_cmd_ctrl_ptr);

   /** Destroy individual input,output and control port handles, if present in
    *  cached configuration */

   /** This loop will get called if the peer sub-graph is
    *  getting closed and for current container need to close
    *  corresponding ports. For such cases, separate calls are
    *  made for acyclic and cyclic links, hence the for loop is
    *  ran only once for currently active container list   */
   for (uint32_t port_type = 0; port_type < PORT_TYPE_MAX; port_type++)
   {
      if (cont_db_port_list_pptr[cont_list_type][port_type].list_ptr &&
          cont_cached_port_list_pptr[cont_list_type][port_type].list_ptr)
      {
         result |= apm_destroy_cont_port_conn(apm_info_ptr,
                                              cont_cached_port_list_pptr[cont_list_type][port_type].list_ptr,
                                              &cont_db_port_list_pptr[cont_list_type][port_type]);
      }
   }

   /** Now delete the container port handles owned by the
    *  current container. This is required for the cases e.g. where
    *  a sub-graph is getting closed but its peer is still
    *  present with no state change in current comand context. */

   for (uint32_t port_type = 0; port_type < PORT_TYPE_MAX; port_type++)
   {
      if (cont_db_port_list_pptr[cont_list_type][port_type].list_ptr)
      {
         result |= apm_destroy_cont_port_self_sg_id(apm_info_ptr,
                                                    cont_cmd_ctrl_ptr,
                                                    &cont_db_port_list_pptr[cont_list_type][port_type]);
      }
   }

   return result;
}

ar_result_t apm_graph_close_cmd_rsp_hdlr(apm_t *apm_info_ptr, apm_cmd_ctrl_t *apm_cmd_ctrl_ptr)
{
   ar_result_t            result = AR_EOK;
   apm_container_t *      container_node_ptr;
   spf_list_node_t *      curr_node_ptr, *next_node_ptr;
   apm_cont_cached_cfg_t *cont_cached_cfg_ptr;
   apm_cont_cmd_ctrl_t *  cont_cmd_ctrl_ptr;

   /** Get the pointer to list of containers pending message */
   curr_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate over the list of containers to which command has
    *  been sent */
   while (curr_node_ptr)
   {
      /** Get the next node pointer */
      next_node_ptr = curr_node_ptr->next_ptr;

      /** Get the handle to container node */
      container_node_ptr = (apm_container_t *)curr_node_ptr->obj_ptr;

      /** For any sub-graph, data/ctrl link closure within the
       *  container, clear the graph sorted status flag  */
      container_node_ptr->cont_graph_ptr->graph_is_sorted = FALSE;

      /** Get the pointer to container's command control object */
      if (AR_EOK != (result = apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr,
                                                                  apm_info_ptr->curr_cmd_ctrl_ptr,
                                                                  &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_graph_close_cmd_rsp_hdlr(): Failed to get cmd ctrl obj for CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Validate the cmd control pointer */
      if (!cont_cmd_ctrl_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_graph_close_cmd_rsp_hdlr(): Cont cmd ctrl ptr is NULL, CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Destroy container port as per the sub-graph list getting
       *  close */
      apm_destroy_cont_port_conn_list(apm_info_ptr, container_node_ptr, cont_cmd_ctrl_ptr);

      /** Sub-graphs are closed along with acyclic links.
       *  Operations below are performed only when operating on
       *  list of containers with acyclic links */
      if (APM_CONT_LIST_WITH_ACYCLIC_LINK == apm_get_curr_active_cont_list_type(apm_info_ptr->curr_cmd_ctrl_ptr))
      {
         /** Get the pointer to this container's cached params
          *  corresponding to current command/rsp */
         cont_cached_cfg_ptr = &cont_cmd_ctrl_ptr->cached_cfg_params;

         /** Clear the module list for current sub-graph and container
          *  pair. Also, decouple the container and sub-graph
          *  association */
         apm_clear_pspc_cont_mod_list(apm_info_ptr,
                                      container_node_ptr,
                                      cont_cmd_ctrl_ptr,
                                      cont_cached_cfg_ptr->graph_mgmt_params.sub_graph_cfg_list_ptr);

         /** If the container is destroyed and it's thread need to be
          *  joined */
         if (AR_ETERMINATED == cont_cmd_ctrl_ptr->rsp_ctrl.rsp_result)
         {
            /** Clear container command response control */
            apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, TRUE /** Release rsp msg buf */);

            /** Remove this node from the list of pending containers */
            apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                                         container_node_ptr,
                                         &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container);

            for (uint32_t idx = 0; idx < APM_CONT_LIST_MAX; idx++)
            {
               /** Remove this node from the list of containers with cached
                *  configuration */
               if (apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info.cached_cont_list[idx].list_ptr)
               {
                  apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info
                                                   .cached_cont_list[idx]
                                                   .list_ptr,
                                               container_node_ptr,
                                               &apm_cmd_ctrl_ptr->graph_mgmt_cmd_ctrl.cont_proc_info
                                                   .cached_cont_list[idx]
                                                   .num_nodes);
               }
            }

            /** Destroy container */
            apm_destroy_container(apm_info_ptr, container_node_ptr);

         } /** if container is getting destroyed */

      } /** End of if (current operating list of containers with acyclic link) */

      /** Advance the pointer to next node in the list */
      curr_node_ptr = next_node_ptr;

   } /** While (pending container list)*/

   return result;
}

ar_result_t apm_cont_destroy_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t          result = AR_EOK;
   apm_cmd_ctrl_t *     apm_cmd_ctrl_ptr;
   spf_list_node_t *    curr_cont_node_ptr, *next_cont_node_ptr;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Clear the command pending flag */
   apm_cmd_ctrl_ptr->cmd_pending = FALSE;

   /** DESTROY CONT msg is sent only in the error or failure
    *  scenario. In this case, get the overall command status as
    *  the containers may have returned success for destroy gk
    *  msg */

   result = apm_cmd_ctrl_ptr->cmd_status;

   /** Get the list of container pending msg processing */
   curr_cont_node_ptr = apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Iterate over the container list */
   while (curr_cont_node_ptr)
   {
      /** Cache the next node */
      next_cont_node_ptr = curr_cont_node_ptr->next_ptr;

      /** Get the pointer to current container node obj */
      container_node_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      /** Get the pointer to container's command control object */
      if (AR_EOK != (result = apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr,
                                                                  apm_info_ptr->curr_cmd_ctrl_ptr,
                                                                  &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cont_destroy_cmd_rsp_hdlr(): Failed to get cmd ctrl obj for CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Validate the cmd control pointer */
      if (!cont_cmd_ctrl_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cont_destroy_cmd_rsp_hdlr(): Cont cmd ctrl ptr is NULL, CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Clear the container command control */
      apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, TRUE /** Release rsp msg buf */);

      /** If this container is present in the list of un-sorted
       *  containers, remove that. This can happen in case of failures
       *  during graph open before the graph sorting happens. */
      if (apm_info_ptr->graph_info.standalone_cont_list_ptr)
      {
         spf_list_find_delete_node(&apm_info_ptr->graph_info.standalone_cont_list_ptr,
                                   container_node_ptr,
                                   TRUE /*pool_used*/);
      }

      /** Clear the module list for current sub-graph and container
       *  pair. Also, decouple the container and sub-graph
       *  association */
      if (container_node_ptr->pspc_module_list_node_ptr)
      {
         apm_clear_pspc_cont_mod_list(apm_info_ptr,
                                      container_node_ptr,
                                      cont_cmd_ctrl_ptr,
                                      container_node_ptr->sub_graph_list_ptr);
      }

      /** Remove this node from the list of pending containers */
      apm_db_remove_node_from_list(&apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                                   container_node_ptr,
                                   &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container);

      /** Destroy container */
      apm_destroy_container(apm_info_ptr, container_node_ptr);

      /** Advance to next container in the list */
      curr_cont_node_ptr = next_cont_node_ptr;

   } /** End of while (pending container list) */

   return result;
}

static ar_result_t apm_cont_graph_mgmt_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t     result = AR_EOK;
   uint32_t        rsp_msg_opcode;
   apm_cmd_ctrl_t *cmd_ctrl_ptr;

   /** Get the current command control obj pointer */
   cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /** Cache the response message opcode */
   rsp_msg_opcode = rsp_msg_ptr->msg_opcode;

   switch (rsp_msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         apm_gm_cmd_update_cont_cached_port_hdl_state(cmd_ctrl_ptr, rsp_msg_opcode);

         break;
      }
      case SPF_MSG_CMD_GRAPH_CLOSE:
      {
         result = apm_graph_close_cmd_rsp_hdlr(apm_info_ptr, cmd_ctrl_ptr);
         break;
      }
      case SPF_MSG_CMD_GRAPH_FLUSH:
      case SPF_MSG_CMD_GRAPH_DISCONNECT:
      {
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_cont_graph_mgmt_rsp_hdlr: Un-supported rsp msg opcode [0x%lX], cmd_opocde[0x%lX]",
                rsp_msg_opcode,
                apm_info_ptr->curr_cmd_ctrl_ptr->cmd_opcode);

         return AR_EFAILED;

         break;
      }
   } /** End of switch() */

   return result;
}

ar_result_t apm_graph_mgmt_cmd_rsp_hdlr(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t result = AR_EOK;
   // apm_cont_msg_opcode_t *cont_msg_opcode_ptr;
   // uint32_t               cont_msg_opcode;
   apm_cmd_ctrl_t *apm_cmd_ctrl_ptr;
   /** Get the pointer to current APM command in process */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   /**In multi-client context undo the client sg-state for the SGs for which this
    * mgmt cmd is failed*/
   if (apm_info_ptr->ext_utils.multi_client_vtbl_ptr &&
       apm_info_ptr->ext_utils.multi_client_vtbl_ptr->apm_multi_client_graph_cmd_rsp_hdlr_fptr)
   {
      apm_info_ptr->ext_utils.multi_client_vtbl_ptr
         ->apm_multi_client_graph_cmd_rsp_hdlr_fptr(apm_info_ptr, rsp_msg_ptr, apm_cmd_ctrl_ptr->cmd_opcode);
   }

   /** Pending container list is required for completion of this */
   apm_cont_graph_mgmt_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);

   /** Clear the list of pending containers, retain cached
    *  configuration */
   apm_clear_container_list(apm_cmd_ctrl_ptr,
                            &apm_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr,
                            &apm_cmd_ctrl_ptr->rsp_ctrl.num_pending_container,
                            APM_CONT_CACHED_CFG_RETAIN,
                            APM_CONT_CMD_PARAMS_RETAIN,
                            APM_PENDING_CONT_LIST);

   return result;
}

ar_result_t apm_parse_get_cfg_rsp_payload(apm_t *apm_info_ptr)
{
   ar_result_t                   result = AR_EOK;
   spf_list_node_t *             curr_cont_node_ptr;
   apm_container_t *             container_node_ptr;
   apm_cont_cmd_ctrl_t *         cont_cmd_ctrl_ptr;
   apm_module_param_data_t *     cont_param_data_hdr_ptr;
   spf_msg_header_t *            msg_hdr_ptr;
   spf_msg_cmd_param_data_cfg_t *msg_payload_ptr;
   apm_ext_utils_t *             ext_utils_ptr;

   /** Get the pointer to the list of containers pending send
    *  message */
   curr_cont_node_ptr = apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr;

   /** Get the pointer to ext utils vtbl  */
   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Iterate over the list */
   while (curr_cont_node_ptr)
   {
      /** Get the pointer to current container object */
      container_node_ptr = (apm_container_t *)curr_cont_node_ptr->obj_ptr;

      /** Get the pointer to this container's current command
       *  control object */
      if (AR_EOK != (result = apm_get_allocated_cont_cmd_ctrl_obj(container_node_ptr,
                                                                  apm_info_ptr->curr_cmd_ctrl_ptr,
                                                                  &cont_cmd_ctrl_ptr)))
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_parse_get_cfg_rsp_payload: Failed to get cmd ctrl obj for CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Validate the cmd control pointer */
      if (!cont_cmd_ctrl_ptr)
      {
         AR_MSG(DBG_ERROR_PRIO,
                "apm_parse_get_cfg_rsp_payload(): Cont cmd ctrl ptr is NULL, CONT_ID [0x%lX]",
                container_node_ptr->container_id);

         return AR_EFAILED;
      }

      /** Get the pointer to container's response message */
      msg_hdr_ptr = (spf_msg_header_t *)cont_cmd_ctrl_ptr->rsp_ctrl.rsp_msg.payload_ptr;

      /** Get the pointer to rsp message payload */
      msg_payload_ptr = (spf_msg_cmd_param_data_cfg_t *)msg_hdr_ptr->payload_start;

      for (uint32_t idx = 0; idx < msg_payload_ptr->num_param_id_cfg; idx++)
      {
         cont_param_data_hdr_ptr = (apm_module_param_data_t *)msg_payload_ptr->param_data_pptr[idx];

         /** Check if the module ID belongs to container, if not skip
          *  this param */
         if (container_node_ptr->container_id != cont_param_data_hdr_ptr->module_instance_id)
         {
            continue;
         }

         switch (cont_param_data_hdr_ptr->param_id)
         {
            case CNTR_PARAM_ID_PATH_DELAY_CFG:
            {
               if (ext_utils_ptr->data_path_vtbl_ptr &&
                   ext_utils_ptr->data_path_vtbl_ptr->apm_update_get_data_path_cfg_rsp_payload_fptr)
               {
                  result |= ext_utils_ptr->data_path_vtbl_ptr
                               ->apm_update_get_data_path_cfg_rsp_payload_fptr(apm_info_ptr,
                                                                               msg_hdr_ptr,
                                                                               cont_param_data_hdr_ptr);
               }

               break;
            }
            case CNTR_PARAM_ID_DATA_PORT_MEDIA_FORMAT:
            {

               AR_MSG(DBG_LOW_PRIO,
                      "apm_parse_get_cfg_rsp_payload(): Response Received, CONT_ID [0x%lX], param ID[0x%lX]",
                      container_node_ptr->container_id,
                      cont_param_data_hdr_ptr->param_id);

               break;
            }
            default:
            {
               AR_MSG(DBG_ERROR_PRIO,
                      "apm_parse_get_cfg_rsp_payload(): Un-supported param ID[0x%lX]",
                      cont_param_data_hdr_ptr->param_id);

               result |= AR_EUNSUPPORTED;
               break;
            }

         } /** End of switch (param_data_hdr_ptr->param_id) */

      } /** End of for (param data list) */

      /** Clear the response control */
      apm_clear_cont_cmd_rsp_ctrl(cont_cmd_ctrl_ptr, TRUE /** Release rsp msg buf */);

      /** Delete the current nod and also advance to next container in
       *  the pending list */
      spf_list_delete_node(&curr_cont_node_ptr, TRUE);

      /** Decrement the number of pending containers */
      apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.num_pending_container--;

   } /** End of while (pending container list) */

   /** If all the containers have been removed from the list,
    *  set the list pointer to NULL */
   if (!apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.num_pending_container)
   {
      apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.pending_cont_list_ptr = NULL;
   }

   if (ext_utils_ptr->data_path_vtbl_ptr && ext_utils_ptr->data_path_vtbl_ptr->apm_destroy_one_time_data_paths_fptr)
   {
      /** Destroy all the 1 time created data paths */
      result = ext_utils_ptr->data_path_vtbl_ptr->apm_destroy_one_time_data_paths_fptr(apm_info_ptr);
   }

   return result;
}

ar_result_t apm_set_get_cfg_cmd_rsp_handler(apm_t *              apm_info_ptr,
                                            spf_msg_t *          rsp_msg_ptr,
                                            apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr)
{
   ar_result_t      result = AR_EOK;
   apm_cmd_ctrl_t * apm_cmd_ctrl_ptr;
   apm_ext_utils_t *ext_utils_ptr;

   /** Get the pointer to current command control obj */
   apm_cmd_ctrl_ptr = apm_info_ptr->curr_cmd_ctrl_ptr;

   ext_utils_ptr = &apm_info_ptr->ext_utils;

   /** Clear the cached configuration for all the containers.
    *  Retain the pending container list. For the module params
    *  clear the response control and return the response
    *  message. For the container params retain the response
    *  message. For all the containers release the cached
    *  configuration */
   apm_set_get_cfg_msg_rsp_clear_cont_list(apm_cmd_ctrl_ptr);

   /** At this point only those containers are remaining in the
    *  pending list which needs further response handling */

   switch (apm_cmd_ctrl_ptr->cmd_opcode)
   {
      case APM_CMD_GET_CFG:
      {
         result = apm_parse_get_cfg_rsp_payload(apm_info_ptr);
         break;
      }
      case APM_CMD_GRAPH_OPEN:
      case SPF_EVT_TO_APM_FOR_PATH_DELAY:
      {
         /** Send the source module the list of delay pointers for all
          *  the containers in the path */
         if (ext_utils_ptr->data_path_vtbl_ptr &&
             ext_utils_ptr->data_path_vtbl_ptr->apm_cont_path_delay_msg_rsp_hdlr_fptr)
         {
            result = ext_utils_ptr->data_path_vtbl_ptr->apm_cont_path_delay_msg_rsp_hdlr_fptr(apm_info_ptr);
         }

         break;
      }
      case APM_CMD_GRAPH_CLOSE:
      {
         /** Update the delay data path list */
         if (ext_utils_ptr->data_path_vtbl_ptr && ext_utils_ptr->data_path_vtbl_ptr->apm_close_data_path_list_fptr)
         {
            result = ext_utils_ptr->data_path_vtbl_ptr->apm_close_data_path_list_fptr(apm_info_ptr);
         }
         break;
      }
      default:
      {
         break;
      }

   } /** End of switch(cmd_opcode) */

   /** Update the overall command status to aggregated any
    *  intermediate errors */
   apm_cmd_ctrl_ptr->cmd_status |= result;

   /** Complete the command */
   return result;
}

ar_result_t apm_rsp_q_container_msg_handler(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{
   ar_result_t          result = AR_EOK;
   spf_msg_header_t *   msg_payload_ptr;
   apm_container_t *    container_node_ptr;
   apm_cont_cmd_ctrl_t *cont_cmd_ctrl_ptr;

   /** Get the message payload pointer */
   msg_payload_ptr = (spf_msg_header_t *)rsp_msg_ptr->payload_ptr;

   /** Get the responding container command control object */
   cont_cmd_ctrl_ptr = (apm_cont_cmd_ctrl_t *)msg_payload_ptr->token.token_ptr;

   /** Get the responding container node pointer */
   container_node_ptr = (apm_container_t *)cont_cmd_ctrl_ptr->host_container_ptr;

   /** Update APM current command control context from the
    *  container sending response */
   apm_info_ptr->curr_cmd_ctrl_ptr = (apm_cmd_ctrl_t *)cont_cmd_ctrl_ptr->apm_cmd_ctrl_ptr;

   AR_MSG(DBG_HIGH_PRIO,
          "APM RSP HDLR: Received response/event Opcode[0x%lX], result[0x%lX], CONT_ID[0x%lX]",
          rsp_msg_ptr->msg_opcode,
          msg_payload_ptr->rsp_result,
          container_node_ptr->container_id);

   /** Aggregate container response */
   if (AR_EPENDING == (result = apm_aggregate_cont_cmd_response(apm_info_ptr, rsp_msg_ptr)))
   {
      return result;
   }

   switch (rsp_msg_ptr->msg_opcode)
   {
      case SPF_MSG_CMD_GRAPH_OPEN:
      case SPF_MSG_RSP_GRAPH_OPEN:
      {
         result = apm_graph_open_cmd_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);
         break;
      }
      case SPF_MSG_CMD_GRAPH_CONNECT:
      {
         result = apm_graph_connect_cmd_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);
         break;
      }
      case SPF_MSG_CMD_GRAPH_PREPARE:
      case SPF_MSG_CMD_GRAPH_START:
      case SPF_MSG_CMD_GRAPH_FLUSH:
      case SPF_MSG_CMD_GRAPH_STOP:
      case SPF_MSG_CMD_GRAPH_DISCONNECT:
      case SPF_MSG_CMD_GRAPH_CLOSE:
      case SPF_MSG_CMD_GRAPH_SUSPEND:
      {
         result = apm_graph_mgmt_cmd_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);
         break;
      }
      case SPF_MSG_CMD_REGISTER_CFG:
      case SPF_MSG_CMD_DEREGISTER_CFG:
      {
         result = apm_cmn_cmd_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);
         break;
      }
      case SPF_MSG_CMD_GET_CFG:
      case SPF_MSG_CMD_SET_CFG:
      {
         result = apm_set_get_cfg_cmd_rsp_handler(apm_info_ptr, rsp_msg_ptr, cont_cmd_ctrl_ptr);
         break;
      }
      case SPF_MSG_CMD_DESTROY_CONTAINER:
      {
         result = apm_cont_destroy_cmd_rsp_hdlr(apm_info_ptr, rsp_msg_ptr);
         break;
      }
      default:
      {
         AR_MSG(DBG_ERROR_PRIO, "APM RSP HDLR : Received unsupported msg opcode [0x%lX]", rsp_msg_ptr->msg_opcode);
         result = AR_EUNSUPPORTED;
         break;
      }

   } /** End of switch (p_msg_pkt->unOpCode) */

   return result;
}

ar_result_t apm_rsp_q_msg_handler(apm_t *apm_info_ptr, spf_msg_t *rsp_msg_ptr)
{

   ar_result_t       result = AR_EOK;
   spf_msg_header_t *msg_payload_ptr;
   apm_cmd_token_t * cmd_token_ptr = NULL;

   /** Get the message payload pointer */
   msg_payload_ptr = (spf_msg_header_t *)rsp_msg_ptr->payload_ptr;

   /** Read the token type */
   cmd_token_ptr = (apm_cmd_token_t *)msg_payload_ptr->token.token_ptr;

   switch (*cmd_token_ptr)
   {
      case APM_CMD_TOKEN_PROXY_CTRL_TYPE:
      {
         result = apm_proxy_manager_response_handler(apm_info_ptr, rsp_msg_ptr);
         break;
      }

      case APM_CMD_TOKEN_CONTAINER_CTRL_TYPE:
      {
         result = apm_rsp_q_container_msg_handler(apm_info_ptr, rsp_msg_ptr);
         break;
      }

      default:
      {
         /** Valid token is used to retreive the current command
          *  context for which the resposne is received. If the token
          *  is invalid, no further action can be taken, not even
          *  ending the command. */

         AR_MSG(DBG_ERROR_PRIO, "APM RSP HDLR: Received unknown Rspmsg Token type [0x%lX]", *cmd_token_ptr);

         return AR_EFAILED;
      }
   }

   /** If any container/proxy manager responses are pending in
    *  context of current command then return, else call the
    *  sequencer function to do further processing. */
   if (!apm_info_ptr->curr_cmd_ctrl_ptr->rsp_ctrl.cmd_rsp_pending)
   {
      /** Call the sequencer function to perform next action */
      apm_cmd_sequencer_cmn_entry(apm_info_ptr);
   }

   return result;
}
